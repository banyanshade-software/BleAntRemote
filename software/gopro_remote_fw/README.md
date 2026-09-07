# GoPro Remote Firmware — nRF52840 Dongle (BLE prototype)

## Code layout
The firmware is split into modules, for maintainability: each radio
protocol (and the temperature sensor) is isolated behind a small header,
and the main file only knows about the button/action state machine.

| File | Role |
|------|------|
| `src/main.c` | Main finite-state machine: button GPIO/interrupt setup, action queue, dispatch to the modules below. No BLE/ANT+ API usage. |
| `src/ble_gopro.[ch]` | BLE central role driver for the GoPro (Open GoPro API): stack init, bonding, scan/connect, GATT discovery, shutter command. |
| `src/ant_garmin.[ch]` | ANT+ driver for the Garmin Edge. Temperature broadcast (Environment profile) is now implemented for real, using sdk-ant's `ant_*` API — see below for what's still unverified. The Controls-profile (Generic) button commands remain a stub, deferred to a follow-up pass. |
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
  below), which now really transmits over ANT+ (Environment profile) —
  see "ANT+ temperature broadcast" below for the build/access status
  and what's still unverified about the channel parameters and page
  layout.

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

## ANT+ temperature broadcast (Environment profile)
Implemented in `src/ant_garmin.c` using Nordic's Zephyr-based ANT+ add-on
(`sdk-ant`). The add-on's **source repo** is gated (ANT+ Adopter +
GitHub org access), but its **documentation site**,
[ant-nrfconnect.github.io](https://ant-nrfconnect.github.io), is public —
no auth needed — and was used to check this file's `ant_*` calls
against the real, confirmed API (exact signatures, not guesses). See the
large status comment at the top of `ant_garmin.c` for exactly what that
covers vs. what's still unverified (the ANT+ Environment device profile
document itself — device type, channel period, and the temperature
page's byte layout — is gated separately, at thisisant.com).

**Confirmed from that public doc site** (`doc/compatibility.html`,
`doc/getting_started.html`):
| sdk-ant | sdk-nrf | nRF52 support |
|---|---|---|
| v2.0.0 | v2.9.2 | nRF52832, nRF52840 |
| v2.1.0 (current) | v3.2.4 | nRF52832, nRF52840 |

Both versions support this project's chip (nRF52832) and the nRF52840
dongle used for bring-up — pick whichever pairing you prefer (v2.1.0/
v3.2.4 for the latest, v2.0.0/v2.9.2 if you want the exact pairing this
project's earlier research targeted).

Still needed before this builds or is trustworthy against a real Garmin
Edge:
- **Set up a west workspace from `sdk-ant`.** Confirmed (not just a
  "fresh workspace only" guess anymore): sdk-ant's Add-on model "specify
  the compatible revision of sdk-nrf in their own `west.yml` manifest
  file" — i.e. `sdk-ant` really is meant to be the **top-level**
  manifest (`west init -m "https://github.com/ant-nrfconnect/sdk-ant"
  --mr main && west update`), which pulls its own matching `sdk-nrf`
  automatically. It is not a one-line addition to this project's
  existing (BLE-only) NCS workspace manifest. Build `gopro_remote_fw`
  from inside that sdk-ant-initialized workspace instead (same "copy the
  app folder in, then `west build`" flow as today, just a different
  workspace root). This dev sandbox still can't do the `west init` step
  itself (`git ls-remote` to the actual source repo returns "Repository
  not found"), so this remains untested here regardless.
- **Set your real ANT+ network key.** `ant_garmin.c`'s
  `ant_plus_network_key` is an all-zero placeholder on purpose —
  the real key is licensed data from your ANT+ Adopter account and
  must not be committed to a (possibly public) repo. `ant_garmin_init()`
  logs a warning at boot if it's still all-zero.
- **Verify the channel parameters and page layout** against the real
  ANT+ Environment device profile document (thisisant.com, ANT+ Adopter
  access) — device type, channel period, transmission type, and the
  temperature field's byte offset in the broadcast page are all
  placeholders; the SDK doc site above doesn't cover them (it only
  ships ready profile libraries for Bike Power/Speed-Cadence/Heart Rate,
  confirmed via its live Kconfig option list — Environment and Controls
  aren't among them, so hand-encoding is genuinely required, not just
  undocumented).

The Garmin **Controls (Generic)** button commands (page right/left, lap)
are still a stub (`ant_garmin_handle_button()`) — deferred to a
follow-up pass; only the temperature broadcast was in scope for this one.

## What's still missing (intentionally, see prior discussion)
1. **ANT+ Controls profile (3 Garmin buttons)**: not implemented —
   `ant_garmin_handle_button()` remains a stub that just logs a warning.
   Same `sdk-ant` access/build prerequisites as the temperature broadcast
   above apply once this is picked up; the exact page layout (believed
   Page 73, unverified) is likewise pending ANT+ Adopter profile-doc
   access — see the specs doc.
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
