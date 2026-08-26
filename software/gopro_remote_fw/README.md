# GoPro Remote Firmware — nRF52840 Dongle (BLE prototype)

## Code layout
The firmware is split into modules, for maintainability: each radio
protocol (and the temperature sensor) is isolated behind a small header,
and the main file only knows about the button/action state machine.

| File | Role |
|------|------|
| `src/main.c` | Main finite-state machine: button GPIO/interrupt setup, action queue, dispatch to the modules below. No BLE/ANT+ API usage. |
| `src/ble_gopro.[ch]` | BLE central role driver for the GoPro (Open GoPro API): stack init, bonding, scan/connect, GATT discovery, shutter command. |
| `src/ant_garmin.[ch]` | ANT+ driver for the Garmin Edge: Controls-profile (Generic) button commands, plus the temperature broadcast session (idle by default; any button press arms a bounded, periodic broadcast — see below). ANT+ transmission itself is currently a stub, blocked on a toolchain/chip decision — see below. |
| `src/temp_sensor.[ch]` | Reads the nRF52832's internal die temperature sensor (no external thermistor). Called internally by `ant_garmin.c`'s broadcast timer. |

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
- Any button press also arms the temperature broadcast session (see
  below) — the timers run today, only the actual ANT+ transmission is
  a stub.

## Temperature broadcast session
The internal temperature sensor is **idle by default**: no periodic
activity, no ANT+ transmission, until something happens.

1. **Any button press** (Camera ON/OFF today; the 3 Garmin buttons once
   wired) calls `ant_garmin_note_activity()`.
2. This (re)arms a session: a temperature reading is broadcast every
   `TEMP_BROADCAST_INTERVAL_MIN` minutes (5, by default) for up to
   `TEMP_SESSION_DURATION_MIN` minutes (30, by default) since the *last*
   button press — pressing a button again during an active session resets
   the 30-minute window rather than starting a second, overlapping one.
3. After 30 minutes with no further button press, the session ends and
   the sub-system returns to fully idle.

Both constants are defined at the top of `src/ant_garmin.c` and are
example defaults from the product requirement — tune them once real
battery-life testing is possible. Implementation-wise, `ant_garmin.c`
uses one Zephyr `k_timer` for the periodic broadcast (deferred to a
`k_work` item, since timer expiry callbacks run in ISR context) and a
second one-shot `k_timer` to end the session; see the code comments there
for details.

## ⚠️ Important: this code has not been built/tested in this environment
No full nRF Connect SDK / Zephyr toolchain is available here (the SDK is
several GB, incompatible with the sandbox used to write this). The code
follows the Zephyr Bluetooth Host APIs closely (same patterns as Nordic's
official `central_hr` / `peripheral` examples), but **you'll need to build
it and fix any minor syntax errors** on your side — it's a solid starting
point, not a validated binary.

## What's still missing (intentionally, see prior discussion)
1. **ANT+ transmission itself (3 Garmin buttons + temperature broadcast)**:
   currently blocked on a **toolchain/chip decision**, not just a
   licensing step — the nRF52832 used here isn't supported by Nordic's
   Zephyr-based ANT+ add-on (only nRF52840/nRF5340 are); ANT+ on this chip
   needs the older, separate nRF5 SDK (S212/S332 SoftDevice), a different
   toolchain from the Zephyr one the rest of this firmware is built on.
   See `doc/gopro_garmin_remote_specs.md`, "ANT+ implementation notes" for
   the full finding, sources, and the options being weighed (port to nRF5
   SDK / change chip / reconsider ANT+) — nothing is decided yet.
   `ant_garmin_handle_button()` and the internal
   `ant_garmin_send_temperature()` remain stubs that just log a warning
   until that's resolved. The session *timing* (see "Temperature
   broadcast session" above) already runs independently of it. The exact
   ANT+ page layout for both the button commands (Controls profile,
   Generic use-case) and the temperature broadcast (Environment profile)
   is also unverified — see the specs doc.
2. **Fine-grained power management (System OFF)**: the dongle is
   USB-powered during testing, so not critical right now. Needs to be
   revisited for the final CR2032 version (see the "wake on PORT event"
   architecture discussed previously) — and now also needs to account for
   the temperature broadcast session, which requires a lighter sleep mode
   (RTC/timer wake) than full System OFF for up to 30 minutes after a
   button press.
3. **Battery measurement (ADC)**: not wired on the bare dongle. (Ambient
   temperature no longer needs an ADC/thermistor — see `temp_sensor.c`.)
4. **3 remaining Garmin buttons** (page right/left/lap): not yet in the
   overlay or in `main.c`, to be added following the same pattern as
   `btn_cam_on`/`btn_cam_off` once ANT+ is in place, dispatching to
   `ant_garmin_handle_button()` (see the commented-out TODO block in
   `main.c`'s main loop). `ant_garmin_note_activity()` is already called
   for every action in the main loop, so the temperature session will
   automatically be armed by these buttons too once wired — no extra code
   needed there.
5. **Internal temperature sensor devicetree/Kconfig**: `temp_sensor.c`
   follows Zephyr's standard sensor API (`SENSOR_CHAN_DIE_TEMP`) for the
   nRF52's built-in TEMP peripheral, but the exact devicetree node label
   and Kconfig symbols were not verified against a real SDK build here —
   double-check them when you build.

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
