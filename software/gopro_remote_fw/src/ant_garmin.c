#include "ant_garmin.h"
#include "temp_sensor.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if 1

#include <ant_interface.h>

LOG_MODULE_REGISTER(ant_garmin, LOG_LEVEL_DBG);

/*
 * ============================================================================
 * Build/verification status (read this before touching channel constants)
 * ============================================================================
 * The sdk-ant *source* repo (github.com/ant-nrfconnect/sdk-ant) is still
 * gated (ANT+ Adopter + GitHub org access) and still not reachable from
 * this dev sandbox (`git ls-remote` -> "Repository not found"). However
 * its **documentation site**, ant-nrfconnect.github.io, is public - no
 * auth needed - and was used to check this file against the real API:
 *   - doc/api/interface.html gave the exact confirmed signatures for
 *     every ant_* call used below (ant_stack_init, ant_network_address_set,
 *     ant_channel_assign, ant_channel_id_set, ant_channel_radio_freq_set,
 *     ant_channel_period_set, ant_channel_open [a macro for
 *     ant_channel_open_with_offset(ch, CHANNEL_START_OFFSET_NONE)],
 *     ant_broadcast_message_tx, ant_event_get) - all match what's called
 *     here exactly, arg order/types included.
 *   - doc/api/interface.html also revealed that ant_stack_init() is an
 *     "internal glue function intended for SYS_INIT" - it runs
 *     automatically at boot from CONFIG_ANT_LICENSE_KEY /
 *     CONFIG_ANT_EVALUATION_KEY (see prj.conf), so this file does NOT
 *     call it itself (an earlier version of this file did - fixed).
 *   - doc/compatibility.html confirms nRF52832 AND nRF52840 are
 *     supported by sdk-ant's "Add-on" model for both sdk-nrf v2.9.2
 *     (sdk-ant v2.0.0) and the current sdk-nrf v3.2.4 (sdk-ant v2.1.0) -
 *     see the firmware README for what this means for setting up the
 *     west workspace (short version: `west init` sdk-ant itself as the
 *     top-level manifest - confirmed, not just documented for a "fresh
 *     workspace" - rather than adding it to this project's existing
 *     manifest).
 *   - doc/kconfig/ *.html confirmed CONFIG_ANT, CONFIG_ANT_LIBRARY_CORE,
 *     CONFIG_ANT_CHANNEL_CONFIG, CONFIG_ANT_KEY_MANAGER,
 *     CONFIG_ANT_EVALUATION_KEY/CONFIG_ANT_LICENSE_KEY (see prj.conf) all
 *     exist as described, and that no CONFIG_ANT_ENVIRONMENT or
 *     CONFIG_ANT_CONTROLS profile library exists (only BPWR/BSC/HRM) -
 *     confirming the Environment/Controls pages still need hand-encoding.
 *
 * Still NOT verified - this documentation site covers the SDK, not the
 * ANT+ Alliance's device profile documents (those are gated separately,
 * at thisisant.com, ANT+ Adopter account required):
 *   - ANT_ENVIRONMENT_DEVICE_TYPE (25): commonly cited in third-party/
 *     community ANT+ references, not confirmed against the official
 *     device profile document.
 *   - ANT_TEMP_CHANNEL_PERIOD, ANT_TEMP_TRANSMISSION_TYPE: plausible
 *     placeholders, not sourced from the real profile doc at all.
 *   - The temperature page byte layout in
 *     ant_garmin_build_temperature_page() below: only the page-number-in-
 *     byte-0 convention and the "signed, 0.01 degC" temperature encoding
 *     are reasonably well-established ANT+ conventions; the exact byte
 *     offset of the temperature field within the Environment page is a
 *     placeholder.
 * The ANT+ RF frequency (2457 MHz, "RF channel 57") IS a safe, publicly
 * documented constant - it's the same fixed frequency for all ANT+
 * devices, not part of the gated profile docs.
 *
 * Fix the still-unverified items above against the real ANT+ Environment
 * profile document before testing against a real Garmin Edge - until
 * then, treat every broadcast as "correctly shaped ANT+ traffic on the
 * right channel, using confirmed-correct API calls", not "a Garmin Edge
 * will necessarily understand the payload".
 * ============================================================================
 */

/*
 * Temperature broadcast session parameters - example defaults from the
 * product requirement, tune once real ANT+/battery testing is possible.
 */
#define TEMP_BROADCAST_INTERVAL_MIN 1 // in minutes 
#define TEMP_SESSION_DURATION_MIN   30

/* ---------------------------------------------------------------------
 * ANT+ channel configuration (temperature / Environment profile)
 * --------------------------------------------------------------------- */

/* ANT+ network key: licensed data from the ANT+ Adopter program, must
 * NOT be committed to a (possibly public) repo. Replace the zeros below
 * with your real key locally, and be careful not to `git add` that
 * change if this repo is public - e.g. `git update-index --skip-worktree
 * src/ant_garmin.c` after editing, or move this array to your own
 * untracked file and declare it `extern` here instead. */
#define ANT_PLUS_NETWORK_NUMBER 0
static const uint8_t ant_plus_network_key[8] = {
	// found in several public ANT+ references, but not confirmed against the
	// official gated profile doc (see status comment at the top of this file)
	0xB9, 0xA5, 0x21, 0xFB, 0xBD, 0x72, 0xC3, 0x45,
	//0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

#define ANT_TEMP_CHANNEL_NUMBER 0

/* ANT+ Environment sensor device type. Commonly cited as 25 in
 * third-party ANT+ references - NOT independently verified against the
 * official gated device profile document (see status comment above). */
#define ANT_ENVIRONMENT_DEVICE_TYPE 25

/* Placeholder device number (this device's ANT+ "serial number" on the
 * channel) - any value is fine for evaluation/bring-up, but should
 * probably be derived from the chip's unique ID for a real product so
 * multiple units don't collide. Unverified against the real profile
 * doc either way. */
#define ANT_TEMP_DEVICE_NUMBER 1

/* Placeholder transmission type (shared-channel / global-data-pages
 * flags per the ANT+ spec) - 1 is a common "plain, independent channel"
 * default seen in ANT+ sample code, not confirmed for this profile. */
#define ANT_TEMP_TRANSMISSION_TYPE 1

/* RF channel 57 -> 2400 MHz + 57 MHz = 2457 MHz. Fixed for all ANT+
 * devices (public, non-gated constant). */
#define ANT_PLUS_RF_FREQ 57

/* Channel message period, in 32768ths of a second. Placeholder value
 * (8192 counts =~ 4 Hz) - NOT sourced from the real Environment profile
 * doc. The actual over-the-air rate only matters while a broadcast
 * session is active (see TEMP_BROADCAST_INTERVAL_MIN above); ANT+
 * channels transmit on this fixed period regardless, repeating the last
 * message set via ant_broadcast_message_tx() until a new one is pushed. */
#define ANT_TEMP_CHANNEL_PERIOD 8192

/* Master, broadcast (not shared, not acknowledged) channel type.
 * CHANNEL_TYPE_MASTER == 0x10, confirmed in doc/api/parameters.html
 * (defined by sdk-ant's ant_parameters.h - using our own macro name
 * here only because that header isn't available to compile against in
 * this sandbox; swap for the real CHANNEL_TYPE_MASTER once it is). */
#define ANT_CHANNEL_TYPE_MASTER 0x10

/* Confirmed constant (doc/api/parameters.html): every ANT+ broadcast
 * page is a fixed 8-byte payload. */
#define ANT_TEMP_PAGE_SIZE ANT_STANDARD_DATA_PAYLOAD_SIZE

static bool ant_stack_ready;

/*
 * Builds one ANT+ Environment "current temperature" broadcast page.
 * Layout is a PLACEHOLDER (see the status comment at the top of this
 * file) - only the page-number-in-byte-0 convention and the signed,
 * 0.01 degC temperature encoding are reasonably safe assumptions; the
 * rest (which byte(s) actually carry the temperature field, what the
 * other bytes mean) needs checking against the real ANT+ Environment
 * device profile document.
 */
static void ant_garmin_build_temperature_page(uint8_t page[ANT_TEMP_PAGE_SIZE],
					       int16_t temp_centi)
{
	memset(page, 0xFF, ANT_TEMP_PAGE_SIZE);

	page[0] = 0x00; /* page number - PLACEHOLDER, unverified */

	/* Temperature, signed 16-bit, 0.01 degC resolution, little-endian
	 * - PLACEHOLDER byte offset (bytes 6-7 chosen only because they're
	 * the last two bytes of the page; not sourced from the real
	 * profile doc). */
	page[6] = (uint8_t)(temp_centi & 0xFF);
	page[7] = (uint8_t)((temp_centi >> 8) & 0xFF);
}

/* Actual ANT+ transmission of one reading. */
static void ant_garmin_send_temperature(int16_t temp_centi)
{
	uint8_t page[ANT_TEMP_PAGE_SIZE];
	int err;

	if (!ant_stack_ready) {
		LOG_WRN("ANT+ stack not ready, dropping this broadcast");
		return;
	}

	ant_garmin_build_temperature_page(page, temp_centi);

	err = ant_broadcast_message_tx(ANT_TEMP_CHANNEL_NUMBER,
					ANT_TEMP_PAGE_SIZE, page);
	if (err) {
		LOG_WRN("ant_broadcast_message_tx() failed (%d)", err);
		return;
	}

	LOG_INF("Temperature broadcast queued: %d.%02d C", temp_centi / 100,
		abs(temp_centi % 100));
}

/*
 * k_timer expiry callbacks run in ISR context, where a sensor read, a
 * radio transmission or heavy logging would be unsafe/blocking. The
 * periodic timer below only submits this work item; the actual
 * sample+broadcast happens here, in the system workqueue thread.
 */
static void temp_broadcast_work_handler(struct k_work *work)
{
	int16_t temp_centi;
	LOG_INF("Temperature broadcast work handler");
	if (!temp_sensor_read(&temp_centi)) {
		LOG_WRN("Temperature read failed, skipping this broadcast");
		return;
	}
	ant_garmin_send_temperature(temp_centi);
}

K_WORK_DEFINE(temp_broadcast_work, temp_broadcast_work_handler);

static void temp_broadcast_timer_expiry(struct k_timer *timer)
{
	LOG_INF("Temperature broadcast timer expired, submitting work");
	k_work_submit(&temp_broadcast_work);
}

/* Periodic broadcast timer: runs only while a session is active (see
 * ant_garmin_note_activity() / temp_session_timer below). */
K_TIMER_DEFINE(temp_broadcast_timer, temp_broadcast_timer_expiry, NULL);

static void temp_session_timer_expiry(struct k_timer *timer)
{
	/* SESSION_DURATION_MIN elapsed since the last button press:
	 * stop the periodic broadcasts and go back to fully idle. */
	k_timer_stop(&temp_broadcast_timer);
	LOG_INF("Temperature broadcast session ended (idle for %d min)",
		TEMP_SESSION_DURATION_MIN);
}

/* One-shot "session length" timer, restarted on every activity note. */
K_TIMER_DEFINE(temp_session_timer, temp_session_timer_expiry, NULL);

/* ---------------------------------------------------------------------
 * ANT+ event processing
 * --------------------------------------------------------------------- */

/*
 * sdk-ant delivers channel events (TX complete, channel closed, RX for
 * slave channels, ...) via ant_event_get(), confirmed (doc/api/
 * interface.html) to be a plain non-blocking getter - it fills
 * pucChannel/pucEvent/aucANTMesg and returns when an event is
 * available, an error otherwise (no documented "block until an event
 * arrives" mode, and no separate signal/callback registration for
 * single-core builds - ant_cb_register()/ant_evt_callback_t only exist
 * for the nRF5340 dual-core host side, per doc/init/
 * ant_init_host_cpuapp.html, not relevant to this project's nRF52832/
 * nRF52840 single-core targets). So this polls it on a plain thread:
 * simple and correct, just not the most power-optimal option - fine for
 * this bring-up pass given the temperature broadcast is already a rare,
 * bounded-duration event (see the session timers above).
 */
static void ant_event_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint8_t channel;
	uint8_t event;
	uint8_t evt_buffer[ANT_TEMP_PAGE_SIZE];

	LOG_DBG("----- ANT+ event thread started");
	k_timer_start(&temp_broadcast_timer,
		      K_MINUTES(TEMP_BROADCAST_INTERVAL_MIN),
		      K_MINUTES(TEMP_BROADCAST_INTERVAL_MIN));
	LOG_DBG("----- ANT+ broadcast timer started (%d min interval)",
		TEMP_BROADCAST_INTERVAL_MIN);
	while (1) {
		int err = ant_event_get(&channel, &event, evt_buffer);

		if (err == 0) {
			switch (event) {
			case EVENT_TX:
				LOG_DBG("ANT+ ch%u: EVENT_TX", channel);
				break;
			case EVENT_CHANNEL_CLOSED:
				LOG_WRN("ANT+ ch%u: EVENT_CHANNEL_CLOSED",
					channel);
				break;
			default:
				LOG_DBG("ANT+ ch%u: event %u", channel, event);
				break;
			}
		}

		k_sleep(K_MSEC(10));
	}
}

K_THREAD_DEFINE(ant_event_thread, 512, ant_event_thread_fn, NULL, NULL, NULL,
		 K_PRIO_COOP(7), 0, 0);

/* ---------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

void ant_garmin_init(void)
{
	int err;

	LOG_INF("+++ ant_garmin_init");
	/* ant_stack_init() is NOT called here - it's an internal glue
	 * function that sdk-ant's own SYS_INIT hook already calls
	 * automatically at boot, using the CONFIG_ANT_LICENSE_KEY /
	 * CONFIG_ANT_EVALUATION_KEY value from prj.conf (confirmed in
	 * doc/api/interface.html - see the status comment above). By the
	 * time this function runs from main(), the stack is already up. */

	err = ant_network_address_set(ANT_PLUS_NETWORK_NUMBER,
				       ant_plus_network_key);
	if (err) {
		LOG_ERR("ant_network_address_set() failed (%d)", err);
		return;
	}

	err = ant_channel_assign(ANT_TEMP_CHANNEL_NUMBER,
				  ANT_CHANNEL_TYPE_MASTER,
				  ANT_PLUS_NETWORK_NUMBER, 0);
	if (err) {
		LOG_ERR("ant_channel_assign() failed (%d)", err);
		return;
	}

	err = ant_channel_id_set(ANT_TEMP_CHANNEL_NUMBER,
				  ANT_TEMP_DEVICE_NUMBER,
				  ANT_ENVIRONMENT_DEVICE_TYPE,
				  ANT_TEMP_TRANSMISSION_TYPE);
	if (err) {
		LOG_ERR("ant_channel_id_set() failed (%d)", err);
		return;
	}

	err = ant_channel_radio_freq_set(ANT_TEMP_CHANNEL_NUMBER,
					  ANT_PLUS_RF_FREQ);
	if (err) {
		LOG_ERR("ant_channel_radio_freq_set() failed (%d)", err);
		return;
	}

	err = ant_channel_period_set(ANT_TEMP_CHANNEL_NUMBER,
				      ANT_TEMP_CHANNEL_PERIOD);
	if (err) {
		LOG_ERR("ant_channel_period_set() failed (%d)", err);
		return;
	}

	err = ant_channel_open(ANT_TEMP_CHANNEL_NUMBER);
	if (err) {
		LOG_ERR("ant_channel_open() failed (%d)", err);
		return;
	}

	bool key_is_placeholder = true;
	for (size_t i = 0; i < sizeof(ant_plus_network_key); i++) {
		if (ant_plus_network_key[i] != 0x00) {
			key_is_placeholder = false;
			break;
		}
	}
	if (key_is_placeholder) {
		LOG_WRN("ant_plus_network_key is still all-zero placeholder "
			"- broadcasts won't be recognized by real ANT+ "
			"devices until you set your real ANT+ Adopter "
			"network key");
	}

	ant_stack_ready = true;
	LOG_INF("ANT+ stack initialized, temperature channel open "
		"(channel %d, device type %d)",
		ANT_TEMP_CHANNEL_NUMBER, ANT_ENVIRONMENT_DEVICE_TYPE);

}

void ant_garmin_handle_button(const char *which)
{
	/* Controls (Generic) profile - deferred to a follow-up pass, see
	 * ant_garmin.h. */
	LOG_WRN("Garmin button '%s' pressed - ANT+ Controls profile not "
		"implemented yet", which);
}

void ant_garmin_note_activity(void)
{
	LOG_INF("Activity detected - (re)arming temperature broadcast "
		"(every %d min, for up to %d min)", TEMP_BROADCAST_INTERVAL_MIN,
		TEMP_SESSION_DURATION_MIN);

	/* First broadcast TEMP_BROADCAST_INTERVAL_MIN from now, then every
	 * TEMP_BROADCAST_INTERVAL_MIN. k_timer_start() re-arms a timer
	 * that's already running, so repeated activity simply resets the
	 * cadence rather than stacking timers. */
	k_timer_start(&temp_broadcast_timer,
		      K_MINUTES(TEMP_BROADCAST_INTERVAL_MIN),
		      K_MINUTES(TEMP_BROADCAST_INTERVAL_MIN));

	/* Reset the session window back to its full duration. */
	k_timer_start(&temp_session_timer,
		      K_MINUTES(TEMP_SESSION_DURATION_MIN), K_NO_WAIT);
}
#else
void ant_garmin_init(void) 
{

}
#endif