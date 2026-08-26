/*
 * ant_garmin - ANT+ driver for the Garmin Edge (Generic Controls profile)
 * ==========================================================================
 * Placeholder module for the 3 Garmin buttons (page right/left, lap),
 * sent as ANT+ Generic Controls (Page 73) commands, the same profile
 * used by the official Garmin Edge Remote accessory.
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

/* Placeholder init for the future ANT+ radio stack. Currently a no-op. */
void ant_garmin_init(void);

/*
 * Handles a Garmin button press ("page_right", "page_left" or "lap").
 * Not implemented yet - logs a warning.
 */
void ant_garmin_handle_button(const char *which);

#endif /* ANT_GARMIN_H_ */
