# GoPro Remote Firmware — nRF52840 Dongle (BLE prototype)

## What this firmware does today
- Scans over BLE and automatically connects to the first GoPro it detects
  (filtering on service `0xFEA6`).
- Performs **bonding** (secure pairing), required by the Open GoPro API.
  Keys are saved to flash (`CONFIG_SETTINGS`) → no need to re-pair on
  every reboot.
- Discovers the **GP-0072** (Command) and **GP-0073** (Command Response)
  characteristics, subscribes to notifications.
- 2 buttons wired as GPIO (Camera ON / Camera OFF) → send the
  corresponding shutter command (`03 01 01 01` / `03 01 01 00`).

## ⚠️ Important: this code has not been built/tested in this environment
No full nRF Connect SDK / Zephyr toolchain is available here (the SDK is
several GB, incompatible with the sandbox used to write this). The code
follows the Zephyr Bluetooth Host APIs closely (same patterns as Nordic's
official `central_hr` / `peripheral` examples), but **you'll need to build
it and fix any minor syntax errors** on your side — it's a solid starting
point, not a validated binary.

## What's still missing (intentionally, see prior discussion)
1. **ANT+ (3 Garmin buttons)**: requires Nordic's proprietary ANT stack
   (SoftDevice S212/S332 or the nRF5 SDK ANT module), under a separate
   license from Nordic/ANT+ Alliance. Not included here — a stub is
   present (`handle_garmin_button()`), to be completed once the ANT
   stack is obtained.
2. **Fine-grained power management (System OFF)**: the dongle is
   USB-powered during testing, so not critical right now. Needs to be
   revisited for the final CR2032 version (see the "wake on PORT event"
   architecture discussed previously).
3. **Thermistor / battery measurement (ADC)**: not wired on the bare
   dongle.
4. **3 remaining Garmin buttons** (page right/left/lap): not yet in the
   overlay or in `main.c`, to be added following the same pattern as
   `btn_cam_on`/`btn_cam_off` once ANT+ is in place.

## ⚠️ Button pinout to verify before soldering
In `boards/nrf52840dongle_nrf52840.overlay`, **P0.13 and P0.15** were used
as an example. **Check them against the "Pin assignment" section of the
Nordic nRF52840 Dongle (PCA10059) user guide** before wiring anything: some
castellated pads on the edge of the board are shared with the USB D+/D-
lines and must not be used as GPIO.

## How to build

1. Install the **nRF Connect SDK** (via the "nRF Connect for VS Code"
   extension, the easiest method) or `west` on the command line.
2. Copy this `gopro_remote_fw/` folder into your nRF Connect SDK
   workspace (alongside your other `west` applications).
3. Build:
   ```
   west build -b nrf52840dongle_nrf52840 gopro_remote_fw
   ```
4. The bare dongle boots into **USB DFU bootloader mode** (no direct SWD
   unless you solder the dedicated test pads). Flash with `nrfutil`:
   ```
   nrfutil pkg generate --hw-version 52 --sd-req 0x00 \
     --application build/zephyr/zephyr.hex \
     --application-version 1 pkg.zip
   nrfutil dfu usb-serial -pkg pkg.zip -p /dev/ttyACM0
   ```
   (Press the dongle's small button while plugging it in to force DFU
   mode if needed — the LED blinks red while waiting.)
5. Logs are printed over the USB CDC-ACM serial port (`CONFIG_LOG` +
   `CONFIG_USB_CDC_ACM`) — open it with `screen`, `minicom`, or VS Code's
   serial monitor to follow the process (scan, connection, bonding, GATT
   discovery, command sending).

## Expected test
1. Put your GoPro Hero 11 Mini into BLE pairing mode (Settings →
   Connections → Pair a device, or the equivalent menu).
2. Flash and run the firmware — it should scan, connect, bond, then log
   "GoPro ready".
3. Press the wired Camera ON button → the GoPro should start recording.
   Camera OFF → it should stop.
