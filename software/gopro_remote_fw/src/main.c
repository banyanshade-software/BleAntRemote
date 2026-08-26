/*
 * gopro_remote - bring-up firmware for the nRF52840 Dongle
 * ==========================================================
 *
 * This file owns the main finite-state machine: it wakes up on button
 * press, decides which action to run, and dispatches it to the
 * protocol-specific modules. It does not know about BLE or ANT+
 * internals - that logic lives in ble_gopro.[ch] (GoPro over BLE) and
 * ant_garmin.[ch] (Garmin Edge over ANT+, stub for now). temp_sensor.[ch]
 * provides the internal temperature reading; ant_garmin.c owns *when* to
 * read and broadcast it (a bounded, activity-triggered session - see
 * ant_garmin.h). main.c only reports "a button was pressed" via
 * ant_garmin_note_activity(), it does not manage the session itself.
 * This split keeps each radio protocol/sensor independently maintainable
 * and testable, and makes it straightforward to add a new module later
 * (e.g. a BLE peripheral "configuration service" for a companion mobile
 * app) without touching the others.
 *
 * What this firmware does (functional) :
 *   - Scans for and connects to a GoPro (filtering on the BLE service
 *     0xFEA6, advertised by Open GoPro compatible GoPros).
 *   - Performs bonding (secure pairing), required to talk to the
 *     GoPro. Keys are persisted to flash (CONFIG_SETTINGS), so
 *     subsequent connections won't need to re-pair.
 *   - Discovers the GP-0072 (Command) and GP-0073 (Command Response)
 *     characteristics, subscribes to GP-0073 notifications.
 *   - On "Camera ON" button press -> writes the shutter=1 command.
 *     On "Camera OFF" button press -> writes the shutter=0 command.
 *
 * What this firmware does NOT do (to be added later) :
 *   - ANT+ (Garmin page right/left/lap buttons, and the temperature
 *     broadcast) : Nordic's Zephyr-based ANT+ add-on (`sdk-ant`) does
 *     support the nRF52832 used here (no chip change needed - see
 *     doc/gopro_garmin_remote_specs.md, "ANT+ implementation notes"),
 *     but getting ANT+ Adopter/GitHub access to the gated add-on repo
 *     and integrating its west workspace hasn't been done yet. Not
 *     included here. The Garmin buttons and the temperature broadcast
 *     are wired to stubs (see
 *     ant_garmin.c) in the meantime. The 3 Garmin buttons themselves
 *     are not wired to GPIOs/actions yet either (see README).
 *   - Fine-grained power management (System OFF between connections) :
 *     the dongle is USB-powered, so not critical for testing, but
 *     needs to be revisited for the final CR2032 version. Note this
 *     also now needs to account for the temperature broadcast session
 *     (see ant_garmin.h): while a session is active (up to
 *     TEMP_SESSION_DURATION_MIN minutes after the last button press),
 *     the MCU must wake periodically via its RTC/kernel timer to send
 *     a reading, so it cannot use full System OFF sleep during that
 *     window - only once the session ends does it go back to pure
 *     button-interrupt wake-up.
 *   - Battery voltage reading (ADC) : not wired on the bare dongle, to
 *     be added with the real enclosure. (Ambient temperature no longer
 *     needs a thermistor/ADC - see temp_sensor.c, which reads the
 *     nRF52's internal die temperature sensor instead.)
 *
 * Button GPIO pinout : see boards/nrf52840dongle_nrf52840.overlay
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "ble_gopro.h"
#include "ant_garmin.h"
#include "temp_sensor.h"

LOG_MODULE_REGISTER(gopro_remote, LOG_LEVEL_INF);

/* -------------------------------------------------------------------
 * Buttons (gpio-keys via devicetree)
 * ------------------------------------------------------------------- */
#define BTN_CAM_ON_NODE  DT_ALIAS(sw_cam_on)
#define BTN_CAM_OFF_NODE DT_ALIAS(sw_cam_off)
#define BTN_SW1_NODE     DT_ALIAS(sw1)

//static const struct gpio_dt_spec btn_cam_on =
//	GPIO_DT_SPEC_GET(BTN_CAM_ON_NODE, gpios);
//static const struct gpio_dt_spec btn_cam_off =
//	GPIO_DT_SPEC_GET(BTN_CAM_OFF_NODE, gpios);

//* BUTTON1 = SW1 = P1.6

static const struct gpio_dt_spec btn_sw1 =
		GPIO_DT_SPEC_GET(BTN_SW1_NODE, gpios);

static struct gpio_callback btn_cam_on_cb __attribute__((unused));
static struct gpio_callback btn_cam_off_cb __attribute__((unused));
static struct gpio_callback btn_sw1_cb;

/* Action queue processed outside of interrupt context */
enum remote_action {
	ACTION_CAM_ON,
	ACTION_CAM_OFF,
	ACTION_BTN_SW1
};

K_MSGQ_DEFINE(action_msgq, sizeof(enum remote_action), 8, 4);

/* -------------------------------------------------------------------
 * Buttons : ISR -> just posts an event to the queue, all the (BLE)
 * work happens in the main loop (normal thread).
 * ------------------------------------------------------------------- */

 static void btn_sw1_isr(const struct device *dev, struct gpio_callback *cb,
			    uint32_t pins)
{
	LOG_INF("Button SW1 pressed (GPIO %d)", pins);
	enum remote_action a = ACTION_BTN_SW1;
	k_msgq_put(&action_msgq, &a, K_NO_WAIT);
}


static void btn_cam_on_isr(const struct device *dev, struct gpio_callback *cb,
			    uint32_t pins)  __attribute__((unused));
static void btn_cam_on_isr(const struct device *dev, struct gpio_callback *cb,
			    uint32_t pins)
{
	LOG_INF("Button on pressed (GPIO %d)", pins);
	enum remote_action a = ACTION_CAM_ON;
	k_msgq_put(&action_msgq, &a, K_NO_WAIT);
}

static void btn_cam_off_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) __attribute__((unused));
static void btn_cam_off_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	LOG_INF("Button off pressed (GPIO %d)", pins);
	enum remote_action a = ACTION_CAM_OFF;
	k_msgq_put(&action_msgq, &a, K_NO_WAIT);
}

static int setup_buttons(void)
{
	int err;

	/*if (!gpio_is_ready_dt(&btn_cam_on) || !gpio_is_ready_dt(&btn_cam_off)) {
		LOG_ERR("Button GPIOs not ready");
		return -ENODEV;
	}*/
	if (!gpio_is_ready_dt(&btn_sw1)) {
		LOG_ERR("Button GPIO not ready");
		return -ENODEV;
	}

	/*
	err = gpio_pin_configure_dt(&btn_cam_on, GPIO_INPUT);
	err |= gpio_pin_configure_dt(&btn_cam_off, GPIO_INPUT);
	err |= gpio_pin_interrupt_configure_dt(&btn_cam_on, GPIO_INT_EDGE_TO_ACTIVE);
	err |= gpio_pin_interrupt_configure_dt(&btn_cam_off, GPIO_INT_EDGE_TO_ACTIVE);
	*/
	err = gpio_pin_configure_dt(&btn_sw1, GPIO_INPUT);
	err |= gpio_pin_interrupt_configure_dt(&btn_sw1, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		LOG_ERR("Button GPIO configuration failed (%d)", err);
		return err;
	}

	/*
	gpio_init_callback(&btn_cam_on_cb, btn_cam_on_isr, BIT(btn_cam_on.pin));
	gpio_add_callback(btn_cam_on.port, &btn_cam_on_cb);

	gpio_init_callback(&btn_cam_off_cb, btn_cam_off_isr, BIT(btn_cam_off.pin));
	gpio_add_callback(btn_cam_off.port, &btn_cam_off_cb);
	*/
	gpio_init_callback(&btn_sw1_cb, btn_sw1_isr, BIT(btn_sw1.pin));
	gpio_add_callback(btn_sw1.port, &btn_sw1_cb);
	return 0;
}

/* -------------------------------------------------------------------
 * main - button/action finite-state machine
 * ------------------------------------------------------------------- */
int main(void)
{
	int err;

	LOG_INF("=== GoPro Remote (nRF52840 Dongle) - starting ===");

	err = setup_buttons();
	if (err) {
		LOG_ERR("Aborting: buttons not functional");
	}

	err = ble_gopro_init();
	if (err) {
		LOG_ERR("ble_gopro_init() failed (%d)", err);
		return 0;
	}

	ant_garmin_init();

	err = temp_sensor_init();
	if (err) {
		LOG_WRN("Internal temperature sensor unavailable (%d)", err);
	}

	/* Main loop: wait for a button action and dispatch it to the
	 * relevant protocol module. Each module manages its own
	 * connection/reconnection state internally. */
	enum remote_action action;
	while (1) {
		k_msgq_get(&action_msgq, &action, K_FOREVER);

		/* Any button press (camera or Garmin) counts as activity:
		 * (re)arms the bounded temperature broadcast session. See
		 * ant_garmin.h for the session's timing/behavior. */
		ant_garmin_note_activity();

		switch (action) {
		case ACTION_BTN_SW1:
			LOG_INF("Button SW1 pressed");
			ble_gopro_send_shutter(true);
			break;
		case ACTION_CAM_ON:
			LOG_INF("Camera ON button pressed");
			ble_gopro_send_shutter(true);
			break;
		case ACTION_CAM_OFF:
			LOG_INF("Camera OFF button pressed");
			ble_gopro_send_shutter(false);
			break;
		/*
		 * TODO once the 3 Garmin buttons (page right/left, lap) are
		 * wired to GPIOs (see boards/nrf52840dongle_nrf52840.overlay)
		 * and added to `enum remote_action` above:
		 *
		 * case ACTION_PAGE_R:
		 * case ACTION_PAGE_L:
		 * case ACTION_LAP:
		 *	ant_garmin_handle_button(...);
		 *	break;
		 */
		}
	}

	return 0;
}
