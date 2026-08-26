/*
 * ant_garmin - ANT+ driver for the Garmin Edge
 * ==========================================================================
 * Placeholder module for everything sent to the Garmin Edge over ANT+,
 * from the same radio/module:
 *   1. Remote control - the 3 Garmin buttons (page right/left, lap),
 *      sent as an ANT+ Controls profile (Generic use-case) command page,
 *      the same profile used by the official Garmin Edge Remote
 *      accessory. Believed to be Page 73, but that exact page number/
 *      byte layout is NOT independently verified (requires a free ANT+
 *      Adopter account to check against the real device profile doc).
 *   2. Temperature broadcast - the internal die temperature (read via
 *      temp_sensor.h) sent via the ANT+ Environment profile (page
 *      layout likewise unverified) so it shows up as an Edge data field.
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
 * NOT IMPLEMENTED YET (ANT+ transmission itself). The chip is not the
 * blocker: Nordic's Zephyr-based ANT+ add-on (`sdk-ant`) supports both
 * the nRF52832 used here and the nRF52840, via its current "Add-on"
 * deployment model (nRF Connect SDK v2.9.2+) - no toolchain/chip change
 * needed, see doc/gopro_garmin_remote_specs.md, "ANT+ implementation
 * notes" for the full sourcing (this corrects an earlier, incomplete
 * assessment recorded and then reverted in this project's git history).
 * What IS still needed before real `ant_*` calls can be added here:
 * ANT+ Adopter signup + GitHub org access to the gated `sdk-ant` repo,
 * confirming the west-workspace integration steps, and upgrading this
 * project's nRF Connect SDK to v2.9.2+. See the implementation notes for
 * the Kconfig symbols and API surface (`ant_*`, no `sd_` prefix) once
 * that's done.
 *
 * The session timers below already run and call into temp_sensor.h; only
 * the actual radio transmission is a stub (it just logs a warning), so
 * the main FSM (main.c) and the timing behavior can be exercised
 * independently of that work.
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
