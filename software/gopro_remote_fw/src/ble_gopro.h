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
 */
void ble_gopro_send_shutter(bool on);

#endif /* BLE_GOPRO_H_ */
