/*
 * ant_garmin - ANT+ driver for the Garmin Edge
 * ==========================================================================
 * Placeholder module for everything sent to the Garmin Edge over ANT+,
 * from the same radio/module:
 *   1. Remote control - the 3 Garmin buttons (page right/left, lap),
 *      sent as ANT+ Generic Controls (Page 73) commands, the same
 *      profile used by the official Garmin Edge Remote accessory.
 *   2. Temperature broadcast - the internal die temperature (see
 *      temp_sensor.h) sent via the ANT+ Environment Sensor profile (to
 *      be confirmed) so it shows up as an Edge data field. Intended to
 *      be sent opportunistically whenever a Garmin button already
 *      wakes the device and activates the ANT+ radio (no extra
 *      wake-up) - see doc/gopro_garmin_remote_specs.md, "Garmin Edge
 *      communication (ANT+)" for the open question of periodic
 *      (timer-driven) updates while idle.
 *
 * NOT IMPLEMENTED YET: requires Nordic's proprietary ANT stack
 * (SoftDevice S212/S332 or the nRF5 SDK ANT module), under a separate
 * license from Nordic/ANT+ Alliance. This module currently only logs
 * a warning so the main FSM (main.c) has a stable interface to call
 * into once the ANT+ stack is available, without needing further
 * changes to button handling or to the BLE/GoPro module.
 */
#ifndef ANT_GARMIN_H_
#define ANT_GARMIN_H_

#include <stdint.h>

/* Placeholder init for the future ANT+ radio stack. Currently a no-op. */
void ant_garmin_init(void);

/*
 * Handles a Garmin button press ("page_right", "page_left" or "lap").
 * Not implemented yet - logs a warning.
 */
void ant_garmin_handle_button(const char *which);

/*
 * Broadcasts a temperature reading (hundredths of a degree Celsius,
 * see temp_sensor_read()) over ANT+. Not implemented yet - logs a
 * warning.
 */
void ant_garmin_send_temperature(int16_t temp_centi);

#endif /* ANT_GARMIN_H_ */
