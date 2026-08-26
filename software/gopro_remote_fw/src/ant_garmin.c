#include "ant_garmin.h"
#include "temp_sensor.h"

#include <stdint.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ant_garmin, LOG_LEVEL_INF);

/*
 * Temperature broadcast session parameters - example defaults from the
 * product requirement, tune once real ANT+/battery testing is possible.
 */
#define TEMP_BROADCAST_INTERVAL_MIN 5
#define TEMP_SESSION_DURATION_MIN   30

void ant_garmin_init(void)
{
	LOG_WRN("ANT+ stack not implemented yet - Garmin buttons and "
		"temperature broadcast are stubs");
}

void ant_garmin_handle_button(const char *which)
{
	LOG_WRN("Garmin button '%s' pressed - ANT+ not implemented yet", which);
}

/* Actual ANT+ transmission of one reading. Not implemented yet. */
static void ant_garmin_send_temperature(int16_t temp_centi)
{
	LOG_WRN("Temperature broadcast (%d.%02d C) not sent - ANT+ not "
		"implemented yet", temp_centi / 100, abs(temp_centi % 100));
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

	if (!temp_sensor_read(&temp_centi)) {
		LOG_WRN("Temperature read failed, skipping this broadcast");
		return;
	}
	ant_garmin_send_temperature(temp_centi);
}

K_WORK_DEFINE(temp_broadcast_work, temp_broadcast_work_handler);

static void temp_broadcast_timer_expiry(struct k_timer *timer)
{
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
