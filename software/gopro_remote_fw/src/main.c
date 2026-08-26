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
 *   - Discovers the GP-0072/73 (Command/Response) and GP-0074/75
 *     (Query/Response) characteristics, subscribes to both.
 *   - Runs a small recording FSM (see the "Recording FSM" comment right
 *     above main()): "Camera ON"/"Camera OFF" (multi-button mode) or
 *     SW1 (mono-button toggle mode) drive it towards REC_ON/REC_OFF;
 *     the shutter=1/0 command is only sent on an actual on<->off
 *     transition, never repeated for an unchanged target, so it can't
 *     double-send the GoPro's confirmation beep. A ~1s status poll
 *     confirms the command took effect and also catches the camera
 *     being started/stopped manually (or disconnecting).
 *   - Buttons are debounced with a simple per-button cooldown.
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

//LOG_MODULE_REGISTER(gopro_remote, LOG_LEVEL_INF);
LOG_MODULE_REGISTER(gopro_remote, LOG_LEVEL_DBG);

/* -------------------------------------------------------------------
 * Buttons (gpio-keys via devicetree)
 * ------------------------------------------------------------------- */
#define BTN_CAM_ON_NODE  DT_ALIAS(sw_cam_on)
#define BTN_CAM_OFF_NODE DT_ALIAS(sw_cam_off)
#define BTN_SW1_NODE     DT_ALIAS(sw1)

/* Action queue processed outside of interrupt context. STATUS_TICK/
 * STATUS_RESULT are not button presses - see the status timer and
 * ble_gopro status callback further below - but they go through the
 * same queue so the FSM in main() stays single-threaded. */
enum rmt_event {
	EVENT_NONE = 0,
	EVENT_CAM_ON = 1,
	EVENT_CAM_OFF = 2,
	EVENT_BTN_SW1 = 3,
	EVENT_STATUS_TICK = 4,
	EVENT_STATUS_RES_CAM_ON = 5,
	EVENT_STATUS_RES_CAM_OFF = 6,
	EVENT_CAM_DISCOVERED = 7	, /* GoPro connected but not recording (e.g. just powered on) */
};


 struct remote_msg {
        enum rmt_event action;
 };

K_MSGQ_DEFINE(action_msgq, sizeof(struct remote_msg), 8, 4);

/* -------------------------------------------------------------------
 * Buttons : ISR -> just posts an event to the queue, all the (BLE)
 * work happens in the main loop (normal thread).
 * ------------------------------------------------------------------- */



#define DEBOUNCE_MS 200
struct button {
	const struct gpio_dt_spec spec;
	enum rmt_event action;
	const char *name;
	struct gpio_callback cb;
	int64_t last_press_ms;
};

static   struct button btn_cam_on = {
	.spec = GPIO_DT_SPEC_GET(BTN_CAM_ON_NODE, gpios),
	.action = EVENT_CAM_ON,
	.name = "CAM_ON",
};
static  struct button btn_cam_off = {
	.spec = GPIO_DT_SPEC_GET(BTN_CAM_OFF_NODE, gpios),
	.action = EVENT_CAM_OFF,
	.name = "CAM_OFF",
};
static  struct button btn_sw1 = {
	.spec = GPIO_DT_SPEC_GET(BTN_SW1_NODE, gpios),
	.action = EVENT_BTN_SW1,
	.name = "SW1",
};

static void btn_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct button *btn = CONTAINER_OF(cb, struct button, cb);
	int64_t now = k_uptime_get();

	if (now - btn->last_press_ms < DEBOUNCE_MS) {
		return; /* bounce (or a too-fast repeat press): ignore */
	}
	btn->last_press_ms = now;

	LOG_INF("Button %s pressed", btn->name);
	struct remote_msg msg = { .action = btn->action };

	k_msgq_put(&action_msgq, &msg, K_NO_WAIT);
}

static int setup_one_button( struct button *btn)
{
	int err;

	if (!gpio_is_ready_dt(&btn->spec)) {
		LOG_ERR("Button %s GPIO not ready", btn->name);
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&btn->spec, GPIO_INPUT);
	err |= gpio_pin_interrupt_configure_dt(&btn->spec, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		LOG_ERR("Button %s GPIO configuration failed (%d)", btn->name, err);
		return err;
	}

	gpio_init_callback(&btn->cb, btn_isr, BIT(btn->spec.pin));
	gpio_add_callback(btn->spec.port, &btn->cb);
	return 0;
}



static int setup_buttons(void)
{
	int err  = 0;
	err |= setup_one_button(&btn_cam_on);
	err |= setup_one_button(&btn_cam_off);
	err |= setup_one_button(&btn_sw1);
	return err;
}

/* -------------------------------------------------------------------
 * ~1s status poll timer and ble_gopro status callback: both just post
 * to the action queue, all the FSM logic lives in main()'s loop below.
 * k_timer expiry and the BT RX thread are not the main thread, so this
 * is all they're allowed to do.
 * ------------------------------------------------------------------- */


static void status_timer_handler(struct k_timer *timer)
{
	struct remote_msg msg = { .action = EVENT_STATUS_TICK };
	//LOG_DBG("TTT isr");
	k_msgq_put(&action_msgq, &msg, K_NO_WAIT);
}


K_TIMER_DEFINE(status_timer, status_timer_handler, NULL);

static void on_gopro_status(enum gopro_rec_state status)
{
	LOG_DBG("on_gopro_status: status=%d", status);
	struct remote_msg msg = { 0 };
	switch (status) {
		case GOPRO_REC_STARTED:
			msg.action = EVENT_STATUS_RES_CAM_ON;
			break;
		case GOPRO_REC_STOPPED:
			msg.action = EVENT_STATUS_RES_CAM_OFF;
			break;
		case GOPRO_REC_DISCOVERED:
			msg.action = EVENT_CAM_DISCOVERED;
			break;
		case GOPRO_REC_UNKNOWN:
			LOG_DBG("on_gopro_status: GOPRO_REC_UNKNOWN - ignoring");
			return;
		default:
			LOG_ERR("on_gopro_status: unknown status %d", status);
			return;
	}
	k_msgq_put(&action_msgq, &msg, K_NO_WAIT);
}

/*
 * Recording FSM states (the whole FSM lives in main()'s loop below):
 *   REC_OFF      - not recording, idle.
 *   REC_ON_SENT  - "start recording" sent, waiting for the camera to
 *                  confirm it (via the ~1s status poll).
 *   REC_ON       - camera confirmed it is recording.
 *   REC_OFF_SENT - "stop recording" sent, waiting for confirmation.
 * No other state is needed: ON_SENT/OFF_SENT only exist to remember
 * "a command is outstanding" so a same-direction button press can be
 * ignored (that's what stops the BLE "start recording" command from
 * ever being sent twice in a row, which is what makes the camera beep
 * an extra time) - REC_ON/REC_ON_SENT together mean "target: on",
 * REC_OFF/REC_OFF_SENT together mean "target: off".
 */
enum rec_state {
	REC_UNKNOWN = 0,
	REC_OFF,
	REC_ON_SENT,
	REC_ON,
	REC_OFF_SENT,
};

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

	ble_gopro_set_status_cb(on_gopro_status);
	//k_timer_init(&status_timer, status_timer_handler, NULL);
	//LOG_DBG("TTT Starting status timer (1s period)");
	//k_timer_start(&status_timer, K_SECONDS(1), K_SECONDS(1));

	enum rec_state rec_state = REC_UNKNOWN; /* start unknown, will be set by the first status poll */
	bool status_query_sent = false; /* at most one status query outstanding */

	/* Main loop: wait for a button/status action and run the
	 * recording FSM. Each protocol module (ble_gopro, ant_garmin)
	 * manages its own connection/reconnection state internally. */
	struct remote_msg msg;
	while (1) {
		k_msgq_get(&action_msgq, &msg, K_FOREVER);

		enum rmt_event action = msg.action;

		if (EVENT_BTN_SW1 == action) {
			switch (rec_state) {
				case REC_UNKNOWN:	//FALLTHRU
				case REC_ON_SENT:	//FALLTHRU
				case REC_OFF_SENT:	//FALLTHRU
				default:
					/* ignore button presses until we know the camera's state (via the status poll) */
					break;
				case REC_OFF:
					action = EVENT_CAM_ON;
					break;	
				case REC_ON:
					action = EVENT_CAM_OFF;
					break;
			}
		}
		LOG_DBG("FSM: event %d, rec_state %d", action, rec_state);
		switch (action) {
		case EVENT_CAM_DISCOVERED:
			LOG_INF("======= GoPro discovered (GOPRO_REC_DISCOVERED)");
			switch (rec_state) {
				case REC_UNKNOWN:
					if ((0)) {
						LOG_DBG("CAM_DISCOVERED in REC_UNKNOWN: start status ble_gopro_query_status()");
						status_query_sent = true;
						ble_gopro_query_status();
					} else {
						LOG_DBG("CAM_DISCOVERED in REC_UNKNOWN");
					}
					k_timer_start(&status_timer, K_SECONDS(1), K_SECONDS(1));
					break;
				default:
					LOG_WRN("CAM_DISCOVERED while in state %d: ignore", rec_state);
					break;
			}	
			break;
		case EVENT_CAM_ON:
			switch (rec_state) {
				case REC_OFF_SENT:
					k_timer_stop(&status_timer);
					status_query_sent = false; /* poll right away next tick */
					//FALLTHRU
				case REC_OFF:
					ble_gopro_send_shutter(true);
					rec_state = REC_ON_SENT;
					status_query_sent = false; /* poll right away next tick */
					k_timer_start(&status_timer, K_SECONDS(1), K_SECONDS(1));
					break;
				
				default:
					// ignore button in any other states
					LOG_INF("BAD STATE FOR CAM_ON: %d -> REC_ON_SENT", rec_state);
					break;
			}
			break;

		case EVENT_CAM_OFF:
			switch (rec_state) {
			case REC_ON_SENT:
					k_timer_stop(&status_timer);
					status_query_sent = false; /* poll right away next tick */
					//FALLTHRU
				case REC_ON:
					ble_gopro_send_shutter(false);
					rec_state = REC_OFF_SENT;
					status_query_sent = false; /* poll right away next tick */
					k_timer_start(&status_timer, K_SECONDS(1), K_SECONDS(1));
					break;
				default:
					// ignore button in any other states
					LOG_INF("BAD STATE FOR CAM_OFF: %d -> REC_OFF_SENT", rec_state);
					break;
			}
			break;
		case EVENT_STATUS_TICK:
			if (rec_state == REC_OFF) {
				LOG_ERR("STATUS_TICK while REC_OFF: shouldn't have polled, stopping timer");	
				k_timer_stop(&status_timer);
				break;
			}
				
			if (!status_query_sent) {
				LOG_DBG("STATUS_TICK: sending status ble_gopro_query_status()");
				ble_gopro_query_status();
				status_query_sent = true;
			} else if (rec_state == REC_UNKNOWN	) {
				LOG_DBG("STATUS_TICK while REC_UNKNOWN ignoring status_query_sent=%d", status_query_sent);
				status_query_sent = true;
				ble_gopro_query_status();
			} else {
				LOG_DBG("STATUS_TICK while status_query_sent: ignore");
			}
			break;
		case EVENT_STATUS_RES_CAM_ON:
			if (!status_query_sent) {
				LOG_ERR("STATUS_RES_CAM_ON while !status_query_sent: ignore ressponse");
				//k_timer_stop(&status_timer);
				break;
			}
			switch(rec_state) {
				case REC_UNKNOWN:
					LOG_DBG("STATUS_RES_CAM_ON while REC_UNKNOWN: set to REC_ON");
					rec_state = REC_ON;
					break;
				case REC_ON_SENT:
					rec_state = REC_ON;
					break;
				case REC_OFF_SENT:
					LOG_ERR("STATUS_RES_CAM_ON while REC_OFF_SENT");
					break;
				default:
					LOG_ERR("STATUS_RES_CAM_ON while in state %d: shouldn't have polled, stopping timer", rec_state);
					k_timer_stop(&status_timer);
					break;
			}
	
		case EVENT_STATUS_RES_CAM_OFF:
			if (!status_query_sent) {
				LOG_ERR("STATUS_RES_CAM_OFF while !status_query_sent: ignore ressponse");
				//k_timer_stop(&status_timer);
				break;
			}			
			switch(rec_state) {
				case REC_UNKNOWN:
					LOG_DBG("STATUS_RES_CAM_OFF while REC_UNKNOWN: set to REC_OFF");
					rec_state = REC_OFF;
					k_timer_stop(&status_timer);
					break;
				case REC_OFF_SENT:
					rec_state = REC_OFF;
					k_timer_stop(&status_timer);
					break;
				case REC_ON_SENT:
					LOG_ERR("STATUS_RES_CAM_OFF while REC_ON_SENT");
					break;
				default:
					LOG_ERR("STATUS_RES_CAM_OFF while in state %d: shouldn't have polled, stopping timer", rec_state);
					k_timer_stop(&status_timer);
					break;
			}
			break;
		default:
			LOG_ERR("Unknown event %d", msg.action);
			break;
		}
		
		/*
		 * TODO once the 3 Garmin buttons (page right/left, lap) are
		 * wired to GPIOs (see boards/nrf52840dongle_nrf52840.overlay)
		 * and added to `enum rmt_event` above:
		 *
		 * case EVENT_PAGE_R:
		 * case EVENT_PAGE_L:
		 * case EVENT_LAP:
		 *	ant_garmin_note_activity();
		 *	ant_garmin_handle_button(...);
		 *	break;
		 */
		
	}

	return 0;
}
