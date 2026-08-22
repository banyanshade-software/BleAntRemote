/*
 * gopro_remote - firmware de demarrage pour nRF52840 Dongle
 * ==========================================================
 *
 * Ce que fait ce firmware (fonctionnel) :
 *   - Scanne et se connecte a une GoPro (filtre sur le service BLE
 *     0xFEA6, celui annonce par les GoPro compatibles Open GoPro).
 *   - Effectue le bonding (pairing securise), obligatoire pour parler
 *     a la GoPro. Les cles sont persistees en flash (CONFIG_SETTINGS),
 *     donc les connexions suivantes n'auront plus besoin de re-pairer.
 *   - Decouvre les caracteristiques GP-0072 (Command) et GP-0073
 *     (Command Response), s'abonne aux notifications de GP-0073.
 *   - Sur appui du bouton "Camera ON" -> ecrit la commande shutter=1.
 *     Sur appui du bouton "Camera OFF" -> ecrit la commande shutter=0.
 *
 * Ce que ce firmware NE fait PAS (a ajouter plus tard) :
 *   - ANT+ (boutons Garmin page droite/gauche/tour) : necessite la pile
 *     ANT proprietaire de Nordic (SoftDevice S212/S332 ou module ANT du
 *     nRF5 SDK), sous licence separee aupres de Nordic/ANT+ Alliance.
 *     Non incluse ici. Le code des 3 boutons Garmin est laisse en stub
 *     (voir handle_garmin_button()) en attendant.
 *   - Gestion fine de la consommation (System OFF entre connexions) :
 *     le dongle est alimente par USB, donc pas critique pour les tests,
 *     mais a reprendre pour la version finale sur CR2032.
 *   - Lecture thermistance / tension batterie (ADC) : pas cablee sur le
 *     dongle nu, a ajouter avec le vrai boitier.
 *
 * Pinout GPIO boutons : voir boards/nrf52840dongle_nrf52840.overlay
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

LOG_MODULE_REGISTER(gopro_remote, LOG_LEVEL_INF);

/* -------------------------------------------------------------------
 * UUIDs Open GoPro (confirmes via la doc officielle + implementations
 * de reference) :
 *   - Service annonce en BLE (16 bits)         : 0xFEA6
 *   - Base 128 bits utilisee par toutes les     : b5f9XXXX-aa8d-11e3-
 *     caracteristiques GoPro (XXXX = le numero)   9046-0002a5d5c51b
 *   - GP-0072 = Command (ecriture)
 *   - GP-0073 = Command Response (notification)
 * ------------------------------------------------------------------- */
#define BT_UUID_GOPRO_SERVICE_VAL   0xfea6
static struct bt_uuid_16 uuid_gopro_service =
	BT_UUID_INIT_16(BT_UUID_GOPRO_SERVICE_VAL);

#define BT_UUID_GOPRO_CMD_VAL \
	BT_UUID_128_ENCODE(0xb5f90072, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)
#define BT_UUID_GOPRO_CMD_RSP_VAL \
	BT_UUID_128_ENCODE(0xb5f90073, 0xaa8d, 0x11e3, 0x9046, 0x0002a5d5c51b)

static struct bt_uuid_128 uuid_gopro_cmd = BT_UUID_INIT_128(BT_UUID_GOPRO_CMD_VAL);
static struct bt_uuid_128 uuid_gopro_cmd_rsp = BT_UUID_INIT_128(BT_UUID_GOPRO_CMD_RSP_VAL);

/* Commande TLV "Set Shutter" : [Longueur][ID commande 0x01][param] */
static const uint8_t SHUTTER_ON[]  = { 0x03, 0x01, 0x01, 0x01 };
static const uint8_t SHUTTER_OFF[] = { 0x03, 0x01, 0x01, 0x00 };

/* -------------------------------------------------------------------
 * Etat de connexion / decouverte GATT
 * ------------------------------------------------------------------- */
static struct bt_conn *gopro_conn;
static uint16_t cmd_handle;       /* handle de la caracteristique GP-0072 */
static uint16_t cmd_rsp_handle;   /* handle de la caracteristique GP-0073 */
static uint16_t cmd_rsp_ccc_handle;
static bool gopro_ready;          /* true une fois pret a recevoir des commandes */

static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;

/* -------------------------------------------------------------------
 * Boutons (gpio-keys via devicetree)
 * ------------------------------------------------------------------- */
#define BTN_CAM_ON_NODE  DT_ALIAS(sw_cam_on)
#define BTN_CAM_OFF_NODE DT_ALIAS(sw_cam_off)

static const struct gpio_dt_spec btn_cam_on =
	GPIO_DT_SPEC_GET(BTN_CAM_ON_NODE, gpios);
static const struct gpio_dt_spec btn_cam_off =
	GPIO_DT_SPEC_GET(BTN_CAM_OFF_NODE, gpios);

static struct gpio_callback btn_cam_on_cb;
static struct gpio_callback btn_cam_off_cb;

/* File d'attente d'actions a traiter en dehors du contexte interruption */
enum remote_action {
	ACTION_CAM_ON,
	ACTION_CAM_OFF,
};

K_MSGQ_DEFINE(action_msgq, sizeof(enum remote_action), 8, 4);

/* -------------------------------------------------------------------
 * Envoi d'une commande shutter (une fois la GoPro prete)
 * ------------------------------------------------------------------- */
static void gopro_send_shutter(bool on)
{
	if (!gopro_ready || gopro_conn == NULL) {
		LOG_WRN("GoPro non connectee / non prete, commande ignoree");
		return;
	}

	const uint8_t *payload = on ? SHUTTER_ON : SHUTTER_OFF;
	int err = bt_gatt_write_without_response(gopro_conn, cmd_handle,
						  payload, sizeof(SHUTTER_ON),
						  false);
	if (err) {
		LOG_ERR("Echec ecriture commande shutter (%d)", err);
	} else {
		LOG_INF("Commande shutter %s envoyee", on ? "ON" : "OFF");
	}
}

/* Stub pour les futurs boutons Garmin (ANT+, pas encore implemente) */
static void handle_garmin_button(const char *which)
{
	LOG_WRN("Bouton Garmin '%s' presse - ANT+ non implemente pour l'instant",
		which);
}

/* -------------------------------------------------------------------
 * Callbacks GATT : notification de reponse (GP-0073)
 * ------------------------------------------------------------------- */
static uint8_t on_cmd_rsp_notify(struct bt_conn *conn,
				  struct bt_gatt_subscribe_params *params,
				  const void *data, uint16_t length)
{
	if (!data) {
		LOG_INF("Desabonnement de GP-0073");
		return BT_GATT_ITER_STOP;
	}
	LOG_HEXDUMP_INF(data, length, "Reponse GoPro (GP-0073) :");
	return BT_GATT_ITER_CONTINUE;
}

/* -------------------------------------------------------------------
 * Decouverte GATT : on cherche le service GoPro, puis ses 2
 * caracteristiques, puis on s'abonne a la notification de reponse.
 * ------------------------------------------------------------------- */
static uint8_t discover_func(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr,
			      struct bt_gatt_discover_params *params)
{
	if (!attr) {
		LOG_INF("Decouverte GATT terminee");
		memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		struct bt_gatt_chrc *chrc = attr->user_data;

		if (bt_uuid_cmp(chrc->uuid, &uuid_gopro_cmd.uuid) == 0) {
			cmd_handle = chrc->value_handle;
			LOG_INF("GP-0072 (Command) trouve, handle=%u", cmd_handle);
		} else if (bt_uuid_cmp(chrc->uuid, &uuid_gopro_cmd_rsp.uuid) == 0) {
			cmd_rsp_handle = chrc->value_handle;
			/* Le CCC descriptor suit generalement juste apres */
			cmd_rsp_ccc_handle = chrc->value_handle + 1;
			LOG_INF("GP-0073 (Command Response) trouve, handle=%u",
				cmd_rsp_handle);
		}
	}

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
		LOG_ERR("Echec abonnement notification GP-0073 (%d)", err);
	} else {
		LOG_INF("Abonne aux notifications GP-0073 - GoPro prete");
		gopro_ready = true;
	}
}

static void start_discovery(struct bt_conn *conn)
{
	discover_params.uuid = NULL; /* toutes les caracteristiques du service */
	discover_params.func = discover_func;
	discover_params.start_handle = 0x0001;
	discover_params.end_handle = 0xffff;
	discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

	int err = bt_gatt_discover(conn, &discover_params);
	if (err) {
		LOG_ERR("Echec demarrage decouverte GATT (%d)", err);
		return;
	}
}

/* -------------------------------------------------------------------
 * Callbacks de connexion
 * ------------------------------------------------------------------- */
static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connexion echouee (%u)", err);
		gopro_conn = NULL;
		return;
	}

	LOG_INF("Connecte a la GoPro");
	gopro_conn = bt_conn_ref(conn);

	/* Demande de securite = bonding (obligatoire pour Open GoPro) */
	int sec_err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (sec_err) {
		LOG_ERR("Echec demande de securite/bonding (%d)", sec_err);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Deconnecte (raison %u)", reason);
	gopro_ready = false;
	cmd_handle = 0;
	cmd_rsp_handle = 0;
	if (gopro_conn) {
		bt_conn_unref(gopro_conn);
		gopro_conn = NULL;
	}
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			      enum bt_security_err err)
{
	if (err) {
		LOG_ERR("Echec securisation du lien (%d)", err);
		return;
	}
	LOG_INF("Lien securise (bonding OK), niveau %d - decouverte GATT...", level);
	start_discovery(conn);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

/* Une fois GP-0072 ET GP-0073 trouves, on peut s'abonner. On verifie
 * ca simplement en pollant apres chaque decouverte terminee (cf boucle
 * principale) plutot que par un evenement dedie, pour rester simple. */

/* -------------------------------------------------------------------
 * Scan : on filtre sur le service GoPro (0xFEA6) pour ne pas se
 * connecter a n'importe quel peripherique BLE alentour.
 * ------------------------------------------------------------------- */
static bool ad_has_gopro_service(struct bt_data *data, void *user_data)
{
	if (data->type != BT_DATA_UUID16_SOME && data->type != BT_DATA_UUID16_ALL) {
		return true; /* continue le parcours */
	}
	for (int i = 0; i < data->data_len; i += 2) {
		uint16_t u = sys_get_le16(&data->data[i]);
		if (u == BT_UUID_GOPRO_SERVICE_VAL) {
			*(bool *)user_data = true;
			return false; /* trouve, on arrete */
		}
	}
	return true;
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		     struct net_buf_simple *ad)
{
	if (gopro_conn) {
		return; /* deja connecte, on ignore */
	}

	bool is_gopro = false;
	struct net_buf_simple ad_copy = *ad; /* bt_data_parse consomme le buffer */
	bt_data_parse(&ad_copy, ad_has_gopro_service, &is_gopro);

	if (!is_gopro) {
		return;
	}

	LOG_INF("GoPro detectee (RSSI %d), connexion...", rssi);

	if (bt_le_scan_stop()) {
		return;
	}

	struct bt_conn_le_create_param create_param = *BT_CONN_LE_CREATE_PARAM(
		BT_CONN_LE_OPT_NONE, BT_GAP_SCAN_FAST_INTERVAL,
		BT_GAP_SCAN_FAST_INTERVAL);

	bt_conn_le_create(addr, &create_param, BT_LE_CONN_PARAM_DEFAULT, &gopro_conn);
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
		LOG_ERR("Echec demarrage scan (%d)", err);
	} else {
		LOG_INF("Scan BLE demarre, recherche d'une GoPro...");
	}
}

/* -------------------------------------------------------------------
 * Boutons : ISR -> poste juste un evenement dans la queue, tout le
 * travail (BLE) se fait dans la boucle principale (thread normal).
 * ------------------------------------------------------------------- */
static void btn_cam_on_isr(const struct device *dev, struct gpio_callback *cb,
			    uint32_t pins)
{
	enum remote_action a = ACTION_CAM_ON;
	k_msgq_put(&action_msgq, &a, K_NO_WAIT);
}

static void btn_cam_off_isr(const struct device *dev, struct gpio_callback *cb,
			     uint32_t pins)
{
	enum remote_action a = ACTION_CAM_OFF;
	k_msgq_put(&action_msgq, &a, K_NO_WAIT);
}

static int setup_buttons(void)
{
	int err;

	if (!gpio_is_ready_dt(&btn_cam_on) || !gpio_is_ready_dt(&btn_cam_off)) {
		LOG_ERR("GPIO boutons non pretes");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&btn_cam_on, GPIO_INPUT);
	err |= gpio_pin_configure_dt(&btn_cam_off, GPIO_INPUT);
	err |= gpio_pin_interrupt_configure_dt(&btn_cam_on, GPIO_INT_EDGE_TO_ACTIVE);
	err |= gpio_pin_interrupt_configure_dt(&btn_cam_off, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		LOG_ERR("Config GPIO boutons echouee (%d)", err);
		return err;
	}

	gpio_init_callback(&btn_cam_on_cb, btn_cam_on_isr, BIT(btn_cam_on.pin));
	gpio_add_callback(btn_cam_on.port, &btn_cam_on_cb);

	gpio_init_callback(&btn_cam_off_cb, btn_cam_off_isr, BIT(btn_cam_off.pin));
	gpio_add_callback(btn_cam_off.port, &btn_cam_off_cb);

	return 0;
}

/* -------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------- */
int main(void)
{
	int err;

	LOG_INF("=== GoPro Remote (nRF52840 Dongle) - demarrage ===");

	err = setup_buttons();
	if (err) {
		LOG_ERR("Abandon : boutons non fonctionnels");
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable() a echoue (%d)", err);
		return 0;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load(); /* recharge les cles de bonding sauvegardees */
	}

	start_scan();

	/* Boucle principale : on traite les actions boutons. La
	 * decouverte GATT / subscribe s'enchainent automatiquement via
	 * les callbacks de connexion (voir security_changed ci-dessus). */
	enum remote_action action;
	while (1) {
		if (k_msgq_get(&action_msgq, &action, K_MSEC(200)) == 0) {
			switch (action) {
			case ACTION_CAM_ON:
				LOG_INF("Bouton Camera ON presse");
				gopro_send_shutter(true);
				break;
			case ACTION_CAM_OFF:
				LOG_INF("Bouton Camera OFF presse");
				gopro_send_shutter(false);
				break;
			}
		}

		/* Une fois la decouverte terminee (cmd_handle et
		 * cmd_rsp_handle trouves) et pas encore abonnes, on
		 * declenche l'abonnement. */
		if (gopro_conn && cmd_handle && cmd_rsp_handle && !gopro_ready
		    && subscribe_params.value_handle == 0) {
			start_subscribe(gopro_conn);
		}

		/* Si deconnecte, on relance le scan */
		if (!gopro_conn) {
			static bool scanning;
			if (!scanning) {
				start_scan();
				scanning = true;
			}
		}
	}

	return 0;
}
