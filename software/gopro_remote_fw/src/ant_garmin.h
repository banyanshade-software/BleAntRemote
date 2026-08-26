/*
 * ant_garmin - ANT+ driver for the Garmin Edge
 * ==========================================================================
 * Placeholder module for everything sent to the Garmin Edge over ANT+,
 * from the same radio/module:
 *   1. Remote control - the 3 Garmin buttons (page right/left, lap),
 *      sent as ANT+ Generic Controls (Page 73) commands, the same
 *      profile used by the official Garmin Edge Remote accessory.
 *   2. Temperature broadcast - the internal die temperature (read via
 *      temp_sensor.h) sent via the ANT+ Environment Sensor profile (to
 *      be confirmed) so it shows up as an Edge data field.
 *
 * Temperature broadcast policy: idle by default (no ANT+ activity at
 * all for temperature). Any button press - camera or Garmin, reported
 * via ant_garmin_note_activity() - (re)arms a bounded broadcast
 * session: a reading is sent every TEMP_BROADCAST_INTERVAL_MIN minutes
 * for up to TEMP_SESSION_DURATION_MIN minutes since the *last* button
 * press (each new press resets the window), then the session goes back
 * to fully idle until the next button press. See
 * doc/gopro_garmin_remote_specs.md, "Thermometer (ANT+ broadcast)" for
 * the full rationale, including the power/architecture trade-off this
 * implies (the MCU can no longer use full System OFF sleep for the
 * duration of an active session).
 *
 * NOT IMPLEMENTED YET (ANT+ transmission itself): requires Nordic's
 * proprietary ANT stack (SoftDevice S212/S332 or the nRF5 SDK ANT
 * module), under a separate license from Nordic/ANT+ Alliance. The
 * session timers below already run and call into temp_sensor.h; only
 * the actual radio transmission is a stub (it just logs a warning), so
 * the main FSM (main.c) and the timing behavior can be exercised
 * before the ANT+ stack is available.
 */
#ifndef ANT_GARMIN_H_
#define ANT_GARMIN_H_

/* Placeholder init for the future ANT+ radio stack. Currently a no-op. */
void ant_garmin_init(void);

/*
 * Handles a Garmin button press ("page_right", "page_left" or "lap").
 * Not implemented yet - logs a warning.
 */
void ant_garmin_handle_button(const char *which);

/*
 * Notifies the module that a button was pressed (any of the 5 - camera
 * or Garmin). (Re)arms the bounded temperature broadcast session
 * described above.
 */
void ant_garmin_note_activity(void);

#endif /* ANT_GARMIN_H_ */
