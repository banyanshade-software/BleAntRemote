/*
 * ant_garmin - ANT+ driver (bring-up base)
 * ==========================================================================
 * The ANT profile is selected at compile time by ANT_GARMIN_PROFILE in
 * ant_garmin.c (default: VIRB):
 *
 *   ANT_GARMIN_PROFILE_VIRB (1, default) - ANT+ *Controls* device (type 16)
 *     master. Broadcasts and is detected as a controllable device (VIRB-like).
 *     Current working bring-up target.
 *
 *   ANT_GARMIN_PROFILE_THERMO (2) - ANT+ *Environment* profile (type 25):
 *     broadcasts the nRF52 internal die temperature (via temp_sensor.h).
 *
 *   ANT_GARMIN_PROFILE_REMOTE (3) - the real Garmin Edge Remote behaviour.
 *     ANT+ Controls *control device* = SLAVE: searches for the Edge (master,
 *     device type 16) with a wildcard device number, receives its Control
 *     Device Availability page (0x02), and sends the Generic Command page
 *     (0x49 / 73) on a button press via ant_garmin_handle_button().
 *
 *   ANT_GARMIN_PROFILE_BSC (0) - clone of the sdk-ant bsc_tx sample (Bicycle
 *     Speed & Cadence sensor). Known-good reference transmitter.
 *
 * All profiles expose their setup through ant_garmin_init(), since main.c
 * owns the real main() and only calls into this module.
 *
 * Build requirements (prj.conf): CONFIG_ANT, CONFIG_ANT_EVALUATION_KEY,
 * CONFIG_ANT_CHANNEL_CONFIG, CONFIG_ANT_KEY_MANAGER, CONFIG_ANT_STATE_INDICATOR
 * (+ CONFIG_ANT_BSC / CONFIG_ANT_BSC_SIMULATOR for the BSC reference profile).
 * Needs the gated sdk-ant add-on module in the west workspace.
 */
#ifndef ANT_GARMIN_H_
#define ANT_GARMIN_H_

/*
 * Brings up the ANT stack and opens the ANT+ BSC sensor channel (clone of
 * the bsc_tx sample). Called once from main(). On fatal setup failure it
 * signals the ANT state indicator and halts (k_oops()), like the sample.
 */
void ant_garmin_init(void);

/*
 * Handles a Garmin button press ("page_right", "page_left" or "lap").
 * In the REMOTE profile this sends the matching ANT+ Controls Generic
 * Command (page 0x49) to the paired Edge. In other profiles it is a stub
 * that logs a warning. Kept so the module API is stable for main.c.
 */
void ant_garmin_handle_button(const char *which);

/*
 * Notifies the module that a button was pressed. No-op in this bring-up base
 * (the activity-gated temperature session is deferred); logs at debug level.
 */
void ant_garmin_note_activity(void);

#endif /* ANT_GARMIN_H_ */
