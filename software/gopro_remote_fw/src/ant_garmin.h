/*
 * ant_garmin - ANT+ driver for the Garmin Edge
 * ==========================================================================
 * Everything sent to the Garmin Edge over ANT+, from the same radio/module:
 *   1. Remote control - the 3 Garmin buttons (page right/left, lap),
 *      sent as an ANT+ Controls profile (Generic use-case) command page,
 *      the same profile used by the official Garmin Edge Remote
 *      accessory. Believed to be Page 73, but that exact page number/
 *      byte layout is NOT independently verified (requires an ANT+
 *      Adopter account to check against the real device profile doc).
 *      NOT IMPLEMENTED YET (deferred - see ant_garmin_handle_button()):
 *      this pass only covers the temperature broadcast below.
 *   2. Temperature broadcast - the internal die temperature (read via
 *      temp_sensor.h) sent via the ANT+ Environment profile. Implemented
 *      below using the sdk-ant `ant_*` API - see the big comment at the
 *      top of ant_garmin.c for exactly what is/isn't verified about the
 *      channel parameters and page layout used.
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
 * Build/access status: this module now makes real `ant_*` calls (no
 * more stub for the temperature path), but building it requires adding
 * Nordic's gated `sdk-ant` add-on module to your west workspace manifest
 * (ANT+ Adopter + GitHub org access - the dev sandbox this was written
 * in does not have that access, see ant_garmin.c). Nothing here is
 * chip-specific (works for both the nRF52832 primary target and the
 * nRF52840 dongle used for bring-up), per the specs doc's "What this
 * means for the firmware" section.
 */
#ifndef ANT_GARMIN_H_
#define ANT_GARMIN_H_

/*
 * Initializes the ANT+ stack and opens the temperature broadcast
 * (Environment profile) channel. The channel is opened immediately at
 * boot (ANT+ channels are inherently "fire and forget" - there is no
 * per-message cost to having the channel open, only to the periodic
 * ant_broadcast_message_tx() calls, which are already gated by the
 * activity session below), but no message is actually queued until
 * ant_garmin_note_activity() arms a broadcast session.
 */
void ant_garmin_init(void);

/*
 * Handles a Garmin button press ("page_right", "page_left" or "lap").
 * NOT IMPLEMENTED YET - the Controls (Generic) profile is deferred to a
 * follow-up pass (this one only covers the Environment/temperature
 * broadcast). Logs a warning.
 */
void ant_garmin_handle_button(const char *which);

/*
 * Notifies the module that a button was pressed (any of the 5 - camera
 * or Garmin). (Re)arms the bounded temperature broadcast session
 * described above.
 */
void ant_garmin_note_activity(void);

#endif /* ANT_GARMIN_H_ */
