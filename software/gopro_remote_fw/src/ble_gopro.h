/*
 * ble_gopro - BLE central role driver for the GoPro (Open GoPro API)
 * ====================================================================
 * Owns everything Bluetooth-related for talking to the GoPro: enabling
 * the BT stack, bonding, scanning/connecting, GATT discovery and
 * command/notification handling. The main FSM (main.c) only calls the
 * functions below and never touches Bluetooth APIs directly - this
 * keeps the GoPro/BLE protocol details isolated from button/state
 * handling and from the ANT+/Garmin side (see ant_garmin.h).
 */
#ifndef BLE_GOPRO_H_
#define BLE_GOPRO_H_

#include <stdbool.h>

/*
 * Actual recording state as last reported by the GoPro itself (via the
 * status query below), as opposed to what we last *asked* it to do.
 * GOPRO_REC_UNKNOWN covers "not connected yet" / "no status received
 * yet" / "just disconnected".
 */
enum gopro_rec_state {
	GOPRO_REC_UNKNOWN = 0,
	GOPRO_REC_STOPPED,
	GOPRO_REC_STARTED,
};

/*
 * Invoked (from the BT RX thread, not an ISR) whenever the recording
 * state reported by the GoPro becomes known/changes: on disconnect
 * (-> GOPRO_REC_UNKNOWN) and whenever a status query response is
 * parsed (-> GOPRO_REC_STARTED/STOPPED). At most one callback is kept
 * registered.
 */
typedef void (*ble_gopro_status_cb_t)(enum gopro_rec_state state);
void ble_gopro_set_status_cb(ble_gopro_status_cb_t cb);

/*
 * Enables the Bluetooth stack, reloads bonding keys from flash,
 * registers the pairing/authentication callbacks and starts scanning
 * for a GoPro. Reconnection after a disconnect is handled internally
 * (no polling required from the caller).
 *
 * Returns 0 on success, a negative errno on failure.
 */
int ble_gopro_init(void);

/*
 * Sends the shutter start/stop command to the currently connected and
 * ready GoPro. No-op (logs a warning) if no GoPro is connected yet.
 * Fire-and-forget: whether it actually took effect is only known once
 * ble_gopro_query_status() gets a response (see status callback above).
 */
void ble_gopro_send_shutter(bool on);

/*
 * Asks the GoPro for its current status (encoding/recording state
 * among others). Result arrives asynchronously via the status
 * callback. No-op (logs a warning) if no GoPro is connected yet -
 * the caller can call this unconditionally on a timer regardless of
 * connection state.
 */
void ble_gopro_query_status(void);

#endif /* BLE_GOPRO_H_ */
