/*
 * ant_garmin - ANT+ driver (bring-up base)
 * ==========================================================================
 * This module is, for now, a near-verbatim clone of Nordic's sdk-ant
 * `ant_plus/ant_bsc/bsc_tx` sample: it brings up the ANT stack and runs an
 * ANT+ *Bicycle Speed and Cadence* (BSC) TX sensor, with the built-in BSC
 * simulator sweeping the speed/cadence values automatically (the sample's
 * CONFIG_UPDATE_AUTO mode). The goal is a known-good, reference-faithful
 * ANT+ transmitter to validate the radio/stack/hardware before layering the
 * project-specific Garmin behaviour (Controls buttons + temperature
 * broadcast) back on top.
 *
 * Differences from the stock sample (kept intentionally small):
 *   - The sample's `main()` body lives in ant_garmin_init() here, because
 *     main.c owns the real main() and only calls into this module.
 *   - The sample's own Kconfig options (CONFIG_BSC_TX_*, CONFIG_SENSOR_TYPE,
 *     CONFIG_UPDATE_*) are not present in this project, so the channel/
 *     product constants are #defined locally in ant_garmin.c using the
 *     sample's Kconfig default values, and only the AUTO update path is kept
 *     (no dk_buttons - main.c handles buttons).
 *
 * Build requirements (prj.conf): CONFIG_ANT, CONFIG_ANT_EVALUATION_KEY,
 * CONFIG_ANT_BSC, CONFIG_ANT_BSC_SIMULATOR, CONFIG_ANT_KEY_MANAGER,
 * CONFIG_ANT_CHANNEL_CONFIG, CONFIG_ANT_STATE_INDICATOR. Needs the gated
 * sdk-ant add-on module in the west workspace.
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
 * NOT IMPLEMENTED in this bring-up base (the Controls profile is deferred);
 * logs a warning. Kept so the module API is stable for main.c.
 */
void ant_garmin_handle_button(const char *which);

/*
 * Notifies the module that a button was pressed. No-op in this bring-up base
 * (the activity-gated temperature session is deferred); logs at debug level.
 */
void ant_garmin_note_activity(void);

#endif /* ANT_GARMIN_H_ */
