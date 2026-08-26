#include "ble_gopro.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

LOG_MODULE_REGISTER(ble_gopro, LOG_LEVEL_INF);

/* -------------------------------------------------------------------
 * Open GoPro UUIDs (confirmed via the official docs + reference
 * implementations) :
 *   - Service advertised over BLE (16-bit)      : 0xFEA6
 *   - 128-bit base used by all GoPro             : b5f9XXXX-aa8d-11e3-
 *     characteristics (XXXX = the number)          9046-0002a5d5c51b
 *   - GP-0072 = Command (write)
 *   - GP-0073 = Command Response (notify)
 * ------------------------------------------------------------------- */

#define BT_UUID_GOPRO_SERVICE_VAL   0xfea6

#define BT_UUID_GOPRO_CMD_VAL \
	BT_UUID_128_ENCODE(0xb5f90072, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)
#define BT_UUID_GOPRO_CMD_RSP_VAL \
	BT_UUID_128_ENCODE(0xb5f90073, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)

static struct bt_uuid_128 uuid_gopro_cmd = BT_UUID_INIT_128(BT_UUID_GOPRO_CMD_VAL);
static struct bt_uuid_128 uuid_gopro_cmd_rsp = BT_UUID_INIT_128(BT_UUID_GOPRO_CMD_RSP_VAL);

/* "Set Shutter" TLV command : [Length][Command ID 0x01][param] */
static const uint8_t SHUTTER_ON[]  = { 0x03, 0x01, 0x01, 0x01 };
static const uint8_t SHUTTER_OFF[] = { 0x03, 0x01, 0x01, 0x00 };

/* -------------------------------------------------------------------
 * Connection / GATT discovery state
 * ------------------------------------------------------------------- */
static struct bt_conn *gopro_conn;
static uint16_t cmd_handle;       /* GP-0072 characteristic handle */
static uint16_t cmd_rsp_handle;   /* GP-0073 characteristic handle */
static uint16_t cmd_rsp_ccc_handle;
static bool gopro_ready;          /* true once ready to receive commands */

static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;

static void start_scan(void);

/* -------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------- */
void ble_gopro_send_shutter(bool on)
{
	if (!gopro_ready || gopro_conn == NULL) {
		LOG_WRN("GoPro not connected / not ready, command ignored");
		return;
	}

	const uint8_t *payload = on ? SHUTTER_ON : SHUTTER_OFF;
	int err = bt_gatt_write_without_response(gopro_conn, cmd_handle,
						  payload, sizeof(SHUTTER_ON),
						  false);
	if (err) {
		LOG_ERR("Failed to write shutter command (%d)", err);
	} else {
		LOG_INF("Shutter command %s sent", on ? "ON" : "OFF");
	}
}

/* -------------------------------------------------------------------
 * GATT callbacks : response notification (GP-0073)
 * ------------------------------------------------------------------- */
static uint8_t on_cmd_rsp_notify(struct bt_conn *conn,
				  struct bt_gatt_subscribe_params *params,
				  const void *data, uint16_t length)
{
	if (!data) {
		LOG_INF("Unsubscribed from GP-0073");
		return BT_GATT_ITER_STOP;
	}
	LOG_HEXDUMP_INF(data, length, "GoPro response (GP-0073):");
	return BT_GATT_ITER_CONTINUE;
}

static void start_subscribe(struct bt_conn *conn)
{
	subscribe_params.notify = on_cmd_rsp_notify;
	subscribe_params.value = BT_GATT_CCC_NOTIFY;
	subscribe_params.value_handle = cmd_rsp_handle;
	subscribe_params.ccc_handle = cmd_rsp_ccc_handle;

	int err = bt_gatt_subscribe(conn, &subscribe_params);
	if (err && err != -EALREADY) {
		LOG_ERR("Failed to subscribe to GP-0073 notifications (%d)", err);
	} else {
		LOG_INF("Subscribed to GP-0073 notifications - GoPro ready");
		gopro_ready = true;
	}
}

/* -------------------------------------------------------------------
 * GATT discovery : look up the GoPro service, then its 2
 * characteristics. Subscription is triggered automatically as soon as
 * both handles have been found and discovery completes.
 * ------------------------------------------------------------------- */
static uint8_t discover_func(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr,
			      struct bt_gatt_discover_params *params)
{
	if (!attr) {
		LOG_INF("GATT discovery complete");
		memset(params, 0, sizeof(*params));

		if (cmd_handle && cmd_rsp_handle) {
			start_subscribe(conn);
		} else {
			LOG_WRN("GoPro command characteristics not found");
		}
		return BT_GATT_ITER_STOP;
	}

	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		struct bt_gatt_chrc *chrc = attr->user_data;

		if (bt_uuid_cmp(chrc->uuid, &uuid_gopro_cmd.uuid) == 0) {
			cmd_handle = chrc->value_handle;
			LOG_INF("GP-0072 (Command) found, handle=%u", cmd_handle);
		} else if (bt_uuid_cmp(chrc->uuid, &uuid_gopro_cmd_rsp.uuid) == 0) {
			cmd_rsp_handle = chrc->value_handle;
			/* The CCC descriptor usually follows right after */
			cmd_rsp_ccc_handle = chrc->value_handle + 1;
			LOG_INF("GP-0073 (Command Response) found, handle=%u",
				cmd_rsp_handle);
		}
	}

	return BT_GATT_ITER_CONTINUE;
}

static void start_discovery(struct bt_conn *conn)
{
	discover_params.uuid = NULL; /* all characteristics of the service */
	discover_params.func = discover_func;
	discover_params.start_handle = 0x0001;
	discover_params.end_handle = 0xffff;
	discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

	int err = bt_gatt_discover(conn, &discover_params);
	if (err) {
		LOG_ERR("Failed to start GATT discovery (%d)", err);
		return;
	}
}

/* -------------------------------------------------------------------
 * Connection callbacks
 * ------------------------------------------------------------------- */
static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed (%u)", err);
		gopro_conn = NULL;
		return;
	}

	LOG_INF("Connected to  GoPro");
	gopro_conn = bt_conn_ref(conn);

	/* Security request = bonding (required for Open GoPro) */
	LOG_INF("security/bonding request L2...");
	int sec_err = bt_conn_set_security(conn,  BT_SECURITY_L2);
	if (sec_err) {
		LOG_ERR("Failed to request security/bonding (%d)", sec_err);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason %u)", reason);
	gopro_ready = false;
	cmd_handle = 0;
	cmd_rsp_handle = 0;
	if (gopro_conn) {
		bt_conn_unref(gopro_conn);
		gopro_conn = NULL;
	}

	/* No GoPro connected anymore: resume scanning right away. */
	start_scan();
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			      enum bt_security_err err)
{
	if (err) {
		LOG_ERR("security_changed Error  (%d)", err);
		return;
	}
	LOG_INF("Link secured (bonding OK), level %d - GATT discovery...", level);
	start_discovery(conn);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

/* -------------------------------------------------------------------
 * Scan : filter on the GoPro service (0xFEA6) so we don't connect to
 * any random BLE peripheral nearby.
 * ------------------------------------------------------------------- */
static bool ad_has_gopro_service(struct bt_data *data, void *user_data)
{
	if (data->type != BT_DATA_UUID16_SOME && data->type != BT_DATA_UUID16_ALL) {
		return true; /* keep walking the advertising data */
	}
	for (int i = 0; i < data->data_len; i += 2) {
		uint16_t u = sys_get_le16(&data->data[i]);
		if (u == BT_UUID_GOPRO_SERVICE_VAL) {
			*(bool *)user_data = true;
			return false; /* found, stop walking */
		}
	}
	return true;
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		     struct net_buf_simple *ad)
{
	if (gopro_conn) {
		return; /* already connected, ignore */
	}

	bool is_gopro = false;
	struct net_buf_simple ad_copy = *ad; /* bt_data_parse consumes the buffer */
	bt_data_parse(&ad_copy, ad_has_gopro_service, &is_gopro);

	if (!is_gopro) {
		return;
	}

	LOG_INF("GoPro detected (RSSI %d), stop scan & connect...", rssi);

	if (bt_le_scan_stop()) {
		return;
	}

	/* Initiate an LE connection to the GoPro. */
	struct bt_conn_le_create_param create_param = *BT_CONN_LE_CREATE_PARAM(
		BT_CONN_LE_OPT_NONE, BT_GAP_SCAN_FAST_INTERVAL,
		BT_GAP_SCAN_FAST_INTERVAL);

	struct bt_conn *conn = NULL;
	int err = bt_conn_le_create(addr, &create_param, BT_LE_CONN_PARAM_DEFAULT, &conn);
	if (err) {
		LOG_ERR("bt_conn_le_create failed (%d)", err);
		start_scan();
		return;
	}
	/* release the local reference right away: connected() will take
	 * its OWN reference if the connection succeeds */
	bt_conn_unref(conn);
}

static void start_scan(void)
{
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	int err = bt_le_scan_start(&scan_param, scan_cb);
	if (err) {
		LOG_ERR("Error BLE scan (%d)", err);
	} else {
		LOG_INF("Scan BLE started, looking for GoPro...");
	}
}

/* -------------------------------------------------------------------
 * Authentication callbacks : declares our IO capabilities
 * ------------------------------------------------------------------- */
static void auth_cancel(struct bt_conn *conn)
{
	LOG_INF("Pairing canceled");
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	LOG_INF("Pairing complete, bonded=%d", bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_ERR("Pairing failed, reason=%d", reason);
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	LOG_INF("Passkey displayed (no real screen): %06u", passkey);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	LOG_INF("Passkey to confirm: %06u (auto-confirm)", passkey);
	bt_conn_auth_passkey_confirm(conn);
}

static struct bt_conn_auth_cb auth_cb = {
	.passkey_display = auth_passkey_display,
	.passkey_confirm = auth_passkey_confirm,
	.cancel = auth_cancel,
};
/* NoInputNoOutput -> "Just Works" pairing, no MITM protection (no
 * screen/keyboard on our dongle, and the GoPro only asks for a physical
 * confirmation on its own screen, no code to enter on the client side)
static struct bt_conn_auth_cb auth_cb = {
	.cancel = auth_cancel
};
*/

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

int ble_gopro_init(void)
{
	int err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable() failed (%d)", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		LOG_INF("+++ load settings");
		settings_load(); /* reload saved bonding keys */
	}

	err = bt_conn_auth_cb_register(&auth_cb);
	if (err) {
		LOG_ERR("bt_conn_auth_cb_register failed (%d)", err);
	}
	err = bt_conn_auth_info_cb_register(&auth_info_cb);
	if (err) {
		LOG_ERR("bt_conn_auth_info_cb_register failed (%d)", err);
	}

	start_scan();

	return 0;
}
