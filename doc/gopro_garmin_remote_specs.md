# GoPro + Garmin Edge Bike Remote — Specs & BOM

## 1. Functional specifications

### Objective
Compact housing mounted on the handlebar, allowing remote control of:
- the **GoPro Hero 11 Black Mini** camera (via BLE / Open GoPro API)
- the **Garmin Edge Explore** cycling computer (via ANT+ / **Controls** profile,
  Generic use-case — see the "ANT+ primer" below for naming), which also
  carries the ambient temperature broadcast (see "Thermometer" below)

### Controls (5 buttons)

| # | Button | Action | Protocol |
|---|--------|--------|-----------|
| 1 | Camera ON | Starts GoPro recording | BLE (Open GoPro) |
| 2 | Camera OFF | Stops GoPro recording | BLE (Open GoPro) |
| 3 | Page right | Scrolls the Edge screen to the right | ANT+ (Controls, Generic) |
| 4 | Page left | Scrolls the Edge screen to the left | ANT+ (Controls, Generic) |
| 5 | Lap | Marks a lap on the Edge | ANT+ (Controls, Generic) |

Each button triggers an independent, immediate action (no mode/menu to navigate).

### Thermometer (ANT+ broadcast)
- Ambient/chip temperature is measured using the **nRF52's built-in die
  temperature sensor** — no external thermistor. This replaces the earlier
  "external NTC thermistor" plan.
- The reading is **broadcast over ANT+** (the same radio/link already used
  for the Garmin buttons) so it can be displayed directly on the Garmin
  Edge, instead of only being logged internally.
- **Broadcast policy — idle by default, activity-triggered session**:
  - The temperature sub-system is **completely idle by default**: no ANT+
    transmission at all until something happens.
  - **Any button press** (camera or Garmin — not just the Garmin ones)
    wakes the sub-system and starts a broadcast session.
  - While a session is active, a reading is **sent every 5 minutes**
    (example value, tunable), for **up to 30 minutes** (example value,
    tunable) since the *last* button press.
  - Each new button press during an active session **resets the 30-minute
    window** back to its full duration (it does not stack additional
    sessions).
  - After 30 minutes with no further button press, the session ends and
    the sub-system goes back to fully idle until the next button press.
- Exact broadcast profile: see "Garmin Edge communication (ANT+)" below.
  The session behavior above is implemented in
  `software/gopro_remote_fw/src/ant_garmin.c`
  (`TEMP_BROADCAST_INTERVAL_MIN` / `TEMP_SESSION_DURATION_MIN`); only the
  actual ANT+ transmission is still a stub — see "Open points".

### Remote configuration (mobile app)
- The device must be **configurable from a companion mobile app** (iOS/Android),
  for example to remap buttons, adjust behavior, or check battery/status.
- **Transport protocol: to be defined.** BLE is the natural default since the
  nRF52 (nRF52840, see "MCU / radio") already runs a BLE stack for the
  GoPro link and BLE is required for iOS compatibility (classic Bluetooth
  is not supported by this chip family and is not usable from iOS apps).
  "Classic Bluetooth" is listed as an alternative only in case a future
  MCU/radio choice made it relevant; no decision is needed there given the
  nRF52 choice (true for either the nRF52832 or nRF52840).
- Expected approach (draft, see "Technical specifications" below): a
  dedicated BLE **Configuration Service** (custom GATT service), separate
  from the Open GoPro command exchange, exposed while the device advertises
  itself as a peripheral for a configuration session.
- Exact configurable parameters (button mapping, sleep/wake timing, device
  name, firmware version reporting, battery level, etc.) are **not frozen
  yet** — see "Open points" below.

### Out of scope for this version
- **Screen**: set aside for now (may be added in a v2, see previous discussions
  on the HT1621 segment LCD).

### Usage constraints
- Handlebar mount, outdoor use (vibration, weather — housing sealing to be planned).
- Target battery life: several months on a single CR2032 cell.
- Acceptable response time: on the order of a second between button press and
  execution (on-the-fly BLE/ANT+ connection, no "always-on" radio).

---

## 2. Technical specifications

### Maintainability & modularity
- The firmware must be **modular**: each radio protocol (BLE for the GoPro,
  ANT+ for the Garmin Edge, and any future link such as the mobile-app BLE
  configuration service) is implemented in its own source module behind a
  small header, independent of the others.
- The main application logic (button handling, wake/sleep, dispatch) is kept
  separate from protocol-specific code, so it does not need to change when a
  protocol module is modified, replaced, or extended.
- Concretely, in `software/gopro_remote_fw/src/`:
  - `main.c` — main finite-state machine (buttons, action queue, dispatch).
    Only reports "a button was pressed" to `ant_garmin.c` — it does not
    manage the temperature broadcast session itself.
  - `ble_gopro.[ch]` — BLE/GoPro module (Open GoPro API).
  - `ant_garmin.[ch]` — ANT+/Garmin module (currently a stub, see firmware
    README): Garmin button commands, and the temperature broadcast session
    (timing/policy) built on top of `temp_sensor.[ch]`.
  - `temp_sensor.[ch]` — reads the internal die temperature sensor; knows
    nothing about ANT+ or when to broadcast.
- Rationale: several protocols/features are still open or unimplemented
  (ANT+, mobile-app BLE configuration — see "Remote configuration" above).
  Keeping them isolated avoids one area's changes breaking another, keeps
  each module independently testable, and makes it easier for a new
  contributor to work on a single protocol without understanding the whole
  firmware.
- This requirement complements the "all documentation and code comments in
  English" convention already applied across this project.

### Overall architecture
- MCU **wakes only on button press** (System OFF between actions) by
  default, no permanent radio connection.
- On wake-up: the firmware identifies the pressed button (GPIO controller
  LATCH register), activates the corresponding radio stack (BLE or ANT+),
  executes the command, then returns to deep sleep.
- **Only one protocol active at a time** (sequential multi-protocol, not
  simultaneous) → simplifies the firmware and limits power consumption.
- **Exception — temperature broadcast session**: any button press also
  starts a bounded ANT+ temperature broadcast session (every 5 min, for up
  to 30 min — see "Thermometer" above). For the duration of an active
  session, the MCU can no longer drop to full System OFF between actions:
  it needs a lighter low-power sleep (RTC/kernel-timer wake, not GPIO-only)
  so it can wake up every few minutes to send a reading, then goes back to
  pure button-only System OFF once the session ends. This is a deliberate,
  bounded exception to the "wake only on button press" rule above, not a
  permanent always-on radio.
- The mobile-app configuration link (BLE, see above) follows the same
  wake-on-demand principle: the device advertises as a BLE peripheral only
  when entering a configuration session (e.g. triggered by a dedicated
  button combo or a magnetic/reed switch, to be defined), not continuously.

### MCU / radio
- **DECISION UPDATE**: the MCU target is changing from the **nRF52832**
  (Ebyte E73-2G4M08S1E module, used in the design up to this point) to the
  **nRF52840** — see "ANT+ implementation notes" at the end of this
  document for why. Both natively support BLE and ANT+ on the same radio
  silicon; the nRF52832 just isn't supported by Nordic's Zephyr-based
  ANT+ add-on today, while the nRF52840 is.
- **Why nRF52840 over nRF5340** (the add-on's other supported chip):
  the nRF52840 is a direct family relative of the nRF52832 — same
  single-core Cortex-M4F architecture, same general peripheral set, just
  more flash/RAM (1MB/256KB vs. 512KB/64KB) and native USB device
  support. The nRF5340 is a fundamentally different, dual-core chip
  (separate Cortex-M33 application + network cores with inter-core IPC,
  TrustZone) — a much bigger firmware architecture change than this
  project needs. The nRF52840 also happens to be the chip on the
  **nRF52840 Dongle (PCA10059)** already used in this project for BLE
  prototyping (see `boards/nrf52840dongle_nrf52840.overlay`) — so no
  change to the existing `west build -b nrf52840dongle_nrf52840`
  prototyping workflow is needed; only the final custom PCB (currently
  designed around the E73-2G4M08S1E) needs to move to an nRF52840-based
  module. That module choice is not sourced yet — see "Open points".
- Software stack: **nRF Connect SDK (Zephyr)**, low-level C development,
  for both BLE (already implemented, see `ble_gopro.c`) and — once the
  build integration below is done — ANT+. See "ANT+ implementation
  notes" for the concrete integration steps and what's still unverified
  (in particular, whether concurrent BLE+ANT+ from one Zephyr firmware on
  the nRF52840 is actually demonstrated anywhere, which this project
  needs since it already uses BLE for the GoPro).

### Buttons & wake-up
- 5 touch buttons, individual GPIOs (no matrix), internal pull-up, active-low logic.
- Wake-up via **PORT event** (SENSE + LATCH register): a single interrupt type
  handles all 5 buttons without consuming a dedicated GPIOTE channel,
  negligible sleep-mode consumption (< 1µA on the GPIO side).
- Software debounce (re-read ~20-30ms after wake-up).

### GoPro communication (BLE)
- Based on the **Open GoPro API** (official GoPro documentation, MIT license).
- Classic BLE pairing (bonding) required beforehand; keys stored in flash
  (Zephyr NVS) for direct reconnection afterwards.
- Shutter commands written to characteristic **GP-0072**, response read on
  **GP-0073**.
- Notification re-subscription required on every connection (the GoPro does
  not persist subscription state).

### ANT+ primer (for readers new to ANT+)
A quick, non-exhaustive overview so the rest of this document is readable
if you've never touched ANT+ before.

- **What it is.** ANT+ is a low-power wireless protocol in the 2.4 GHz ISM
  band (same band as BLE and Wi-Fi, but a different, incompatible radio
  protocol), created by Dynastream Innovations, a Garmin subsidiary. It's
  the protocol behind most sports/fitness sensor ecosystems: heart-rate
  straps, bike speed/cadence/power sensors, and — relevant here — Garmin's
  own accessories like the *Edge Remote*.
- **ANT vs. ANT+.** "ANT" is the base, open, low-level radio protocol.
  "ANT+" is a managed layer on top: officially standardized, certified
  **device profiles** (see below) plus a shared **network key**, so any
  ANT+ heart-rate strap works with any ANT+ head unit regardless of
  manufacturer. Using the ANT+ name/logo/network key requires ANT+
  Alliance membership — see the implementation notes further down.
- **Channels, not connections.** Unlike BLE's connection-oriented GATT
  model (pairing, a persistent link, read/write/notify on characteristics),
  ANT+ communication happens over **channels**: a matching set of radio
  parameters (RF frequency, channel period/message rate, device number,
  device type, transmission type) configured independently on both sides.
  If both ends use the same parameters, they simply "hear" each other —
  no handshake/bonding step like BLE. One ANT+ radio can run several
  channels at once, which is how a Garmin Edge talks to a heart-rate
  strap, a cadence sensor, and a remote control simultaneously.
- **Master and slave.** Each channel has a **master** (the transmitter —
  typically the sensor/accessory) and a **slave** (the receiver —
  typically the head unit). In this project, the remote is the master
  (like a heart-rate strap, or the official Edge Remote), and the Garmin
  Edge is the slave.
- **Message types.** *Broadcast* messages are fire-and-forget, sent on a
  fixed schedule (the channel period) — what most sensors use, and what
  this project uses for both button commands and the temperature reading.
  *Acknowledged* messages wait for a receipt (one-off, must-arrive
  commands). *Burst* messages stream larger payloads. Every broadcast
  message is a fixed 8-byte payload, called a **page**, whose first byte
  usually identifies the page type.
- **Device profiles.** ANT+ standardizes the page byte layout per device
  category (Heart Rate Monitor, Bike Power, **Controls** — remote controls
  like the Edge Remote, covering four use-cases: Audio/Video/**Generic**/
  Keypad, **Environment** — ambient sensors, etc.), identified by a numeric
  **device type**. Conceptually similar to a BLE GATT service, but far
  simpler: no characteristics/descriptors, just a small set of
  fixed-format 8-byte pages repeated on a schedule.
- **Why ANT+ instead of BLE for the Garmin side?** Garmin Edge units
  support both, but their whole accessory ecosystem (sensors, remotes) is
  ANT+-based, and the official Edge Remote itself is an ANT+ Controls
  (Generic) device — matching that is what makes this remote behave like a
  "real" Garmin accessory instead of a one-off custom integration.

See "Garmin Edge communication (ANT+)" below for how this project's two
use cases (button commands, temperature broadcast) map onto these
concepts, and the implementation notes further down for what's actually
needed to build and license this on real hardware.

### Garmin Edge communication (ANT+)
ANT+ is used for two purposes, both from the same radio/module. **Naming
correction** (see "ANT+ implementation notes" at the end of this document
for full sourcing): the official ANT+ profile name is **Controls**, which
bundles four use-cases (Audio, Video, **Generic**, Keypad) — "Generic
Controls" as used in earlier revisions of this document was an informal
shorthand, not the profile's real name. Similarly the temperature profile
is officially named **Environment**, not "Environment Sensor."

1. **Remote control** — ANT+ **Controls** profile, **Generic** use-case
   (the same one used by the official Garmin *Edge Remote* accessory).
   - Commands are expected to be sent as a **Page 73** command page with a
     corresponding key code (page right / page left / lap) — **this exact
     page number/byte layout is asserted from general background
     knowledge and has NOT been independently verified against the
     official ANT+ Controls device profile document**, which requires a
     free ANT+ Adopter account to access (see implementation notes).
     Treat "Page 73" as plausible, not confirmed, until checked against
     that document.
   - Edge Explore confirmed compatible with ANT+ and with the Edge Remote
     accessory (same profile) — this part is a product-compatibility fact
     from Garmin's own accessory listings, not the page-format claim above.
2. **Temperature broadcast** — the internal die temperature (see
   "Thermometer" above) is sent so the Edge can show it as a data field.
   - Expected profile: ANT+ **Environment** (temperature), the profile
     Garmin Edge units recognize for ambient temperature accessories —
     the exact page/byte layout is likewise **to be confirmed** against
     the ANT+ Environment device profile document (Adopter account
     required).
   - **Broadcast policy (decided)**: idle by default; **any** button press
     (camera or Garmin) arms a session that sends a reading every 5 minutes
     for up to 30 minutes since the last press, then returns to idle — see
     "Thermometer" above for the full behavior. This is triggered by any
     button, not only the Garmin ones, so a camera-button press (BLE-only
     action) also arms the ANT+ session; the actual periodic ANT+
     transmissions still happen sequentially/one-protocol-at-a-time, just
     scheduled a few minutes later rather than piggybacked on an
     already-open ANT+ link.
   - **Open question**: whether the Controls channel and the Environment
     broadcast need to run as two separate concurrent ANT+ channels
     (channel count depends on the ANT stack/SoftDevice
     chosen) or can be combined, is **to be confirmed** once the ANT+
     stack is obtained.

### Mobile app configuration (BLE) — draft design
- **Role**: while communicating with the GoPro the device acts as a BLE
  *central* (it connects out to the GoPro); a configuration session with a
  mobile app requires the device to act as a BLE *peripheral* (the phone
  connects to it) and advertise a custom **Configuration Service** UUID.
  The nRF52/Zephyr BLE stack supports multi-role operation, but the
  current firmware (`software/gopro_remote_fw/src/ble_gopro.c`) only
  implements the central role — adding the peripheral/config role (likely
  as its own module, e.g. `ble_config.[ch]`, per the modularity requirement
  above) is a firmware task still to be done, not yet started.
- **Session trigger**: how a configuration session is entered (dedicated
  button combo, magnet/reed switch, always-advertise-briefly-on-wake, etc.)
  is **not decided yet**.
- **Data exchanged**: likely a small set of read/write GATT characteristics
  (e.g. button-to-action mapping, battery level, firmware version, device
  name) — exact list **to be defined** with the mobile app requirements.
- **Security**: same bonding approach as the GoPro link is assumed (BLE
  pairing), to avoid an unauthenticated phone reconfiguring the device;
  to be confirmed.
- This is a functional requirement captured here for tracking; the detailed
  GATT service design and firmware implementation are follow-up work.

### Power supply
- **CR2032 cell**, directly powering the nRF52 (no regulator — the chip
  natively operates between 1.7V and 3.6V).
- Buffer capacitor (tantalum/polymer, compact SMD package) to absorb
  BLE/ANT+ current spikes without dropping the voltage seen by the chip near
  end-of-life of the battery.
- Ceramic HF filtering capacitor as a complement.
- **Battery measurement**: software reading via the nRF52's internal SAADC
  VDD channel — no additional component required.

### Internal temperature sensor
- Uses the **nRF52's built-in die temperature sensor** peripheral (a
  dedicated TEMP peripheral, separate from the SAADC used for battery
  measurement above) — no external thermistor, no extra ADC input, no
  additional BOM component.
- Read via Zephyr's standard sensor API (`SENSOR_CHAN_DIE_TEMP`); see
  `software/gopro_remote_fw/src/temp_sensor.c`.
- **Caveat**: this measures the chip's die temperature, not true free-air
  ambient temperature — expect an offset from self-heating and the
  enclosure, and the sensor's stock (uncalibrated) accuracy is roughly
  ±4°C per Nordic's datasheet. Acceptable for an indicative "feels like"
  reading on the Edge, not for precision measurement. To be validated once
  real hardware is available.
- Sampled only during an active broadcast session (every 5 minutes, for up
  to 30 minutes after the last button press — see "Thermometer" above),
  not continuously, to preserve battery life.

### PCB
- **Needs rework**: previously based on the **Ebyte E73-2G4M08S1E** module
  (nRF52832, 18.0 x 13.0mm, 43-pin castellated package, extended-pad
  hand-solder footprint). Following the nRF52840 chip decision above, this
  schematic/footprint needs to move to an nRF52840-based module — not yet
  sourced/chosen (nRF52840 has more GPIOs and a different pinout than the
  nRF52832, so this is not a drop-in footprint swap). See "Open points".
- Programming/debug via **SWD** (SWDIO/SWCLK/GND/VDD pins on a header or
  dedicated pads) — expected to carry over regardless of module choice.

### Development tooling
- Programming probe: **J-Link EDU Mini** (non-commercial use), alternative
  possible with an **ST-Link V2** driven via OpenOCD (not ST-Link V3, which
  is restricted to ST chips).
- Recommended prototyping board before the final PCB: **nRF52840-DK**
  (integrated debugger, ready-to-use Zephyr examples) — updated from the
  nRF52832-based **nRF52-DK** following the chip decision above. The
  **nRF52840 Dongle (PCA10059)** already used for BLE bring-up in this
  project (see `boards/nrf52840dongle_nrf52840.overlay`) remains usable
  too, but has no on-board debugger (USB DFU flashing only, see the
  firmware README).

---

## 3. Bill of materials (BOM)

| Ref. | Component | Qty | Role | Notes |
|------|-----------|----------|------|-----------|
| U1 | nRF52840 module — **not yet sourced/chosen** | 1 | MCU + BLE/ANT+ radio | Replaces the previous Ebyte E73-2G4M08S1E (nRF52832); see "Open points" |
| BT1 | **CR2032** coin cell + holder | 1 | Power supply | Direct supply, no regulator |
| SW1–SW5 | Push buttons (tact switch) | 5 | Camera ON, Camera OFF, Page right, Page left, Lap | GPIO pull-up, active-low |
| C1 | **Tantalum/polymer 47–100µF** capacitor, 0805/1206 package | 1 | Voltage-drop buffer (radio current spikes) | Low ESR required |
| C2 | **100nF** ceramic capacitor, 0402 package | 1 | HF filtering | — |
| J1 | **SWD** header/pads (SWDIO, SWCLK, GND, VDD) | 1 | Programming/debug | Not populated in production, useful for prototyping |
| PCB | Custom board (extended-pad E73 footprint) | 1 | Carrier | KiCad, template already drafted in the project |
| — | Enclosure (3D printed or other) | 1 | Protection/handlebar mount | To be defined (sealing to be planned) |

> Note: no thermistor (NTC) or divider resistor is needed — ambient
> temperature is read from the nRF52's internal die temperature sensor
> (see "Internal temperature sensor" above). The earlier NTC1/R1 BOM lines
> from previous revisions of this document have been removed.

### Tooling (not mounted on the final board)
| Tool | Role |
|-------|------|
| J-Link EDU Mini (or ST-Link V2 + OpenOCD) | SWD flash / debug |
| nRF52-DK | Prototyping before the final PCB |

---

## 4. Open points / to be decided

- **nRF52840 module sourcing & PCB rework** (decided to move off nRF52832,
  see "ANT+ implementation notes"): no specific nRF52840 module has been
  chosen yet to replace the Ebyte E73-2G4M08S1E in the KiCad design; the
  nRF52840 has more GPIOs and a different pinout, so this isn't a simple
  footprint swap. Schematic/PCB rework is needed once a module is picked.
- **ANT+ build integration (nRF52840)**: the exact `west.yml` manifest
  changes and Kconfig symbols needed to pull in Nordic's `sdk-ant` add-on
  and enable it in this project's `prj.conf`, and confirmation that
  concurrent BLE+ANT+ from one Zephyr firmware is actually supported on
  the nRF52840 (this project needs both at once), are not verified yet —
  see "ANT+ implementation notes".
- **Mobile app configuration transport**: confirm BLE (vs. "classic
  Bluetooth", listed only as a placeholder — see "Remote configuration"
  above) and define the Configuration Service (session trigger, exposed
  parameters, security). Currently unimplemented in firmware.
- **User feedback** (LED/buzzer): mentioned but not settled — decide whether
  to add a visual/audio confirmation of an action.
- **Enclosure**: material, handlebar mounting method (clamp, standard GoPro
  mount?), sealing.
- **Handling of the 2 camera buttons**: confirm whether separate ON/OFF
  buttons are more practical than a single toggle button (intentional
  redundancy to avoid mistakes while riding).
- **Screen**: deferred, but the architecture (SAADC battery reading,
  internal temperature sensor) remains compatible with a future addition
  (HT1621 segment LCD already studied).
- **ANT+ temperature broadcast profile**: confirm the exact ANT+ Environment
  page/device-type used, and whether it needs its own ANT+ channel
  alongside the Controls (Generic) channel (concurrent channel count
  depends on the ANT stack/toolchain chosen — see "ANT+ implementation
  notes" below, this is now blocked on a real chip/SDK decision, not just
  "not obtained yet").
- **Temperature broadcast timing values**: the 5-minute interval / 30-minute
  session length are example defaults (`TEMP_BROADCAST_INTERVAL_MIN`,
  `TEMP_SESSION_DURATION_MIN` in `ant_garmin.c`) — the overall
  idle-by-default, activity-triggered policy is decided (see "Thermometer"
  above), but the exact numbers should be revisited once real battery-life
  testing is possible.
- **Battery-life impact of the broadcast session**: needs to be measured
  against the "several months on CR2032" target once hardware and the real
  ANT+ stack are available — the session bounds the extra power draw to
  (at most) 30 minutes after each button press rather than being always-on,
  but the actual current draw during that window (RTC-wake sleep mode +
  periodic ANT+ TX) is not yet characterized.
- **Internal sensor accuracy**: die temperature vs. true ambient
  temperature offset/calibration to be validated on real hardware.

---

## 5. Zephyr API reference (for readers coming from another RTOS)

This firmware is built on the **nRF Connect SDK**, whose RTOS layer is
**Zephyr**. If you're used to FreeRTOS (or a vendor SDK built on it, like
ESP-IDF), most of what you'll read in `software/gopro_remote_fw/src/` maps
onto familiar concepts, but the names and a few semantics differ. This
section lists every Zephyr call/macro actually used in this codebase,
grouped by purpose, with the closest FreeRTOS analogue where one exists.
It's a project-specific cheat sheet, not a full Zephyr tutorial.

### Two build-time concepts with no FreeRTOS equivalent
FreeRTOS is just a scheduler — pin numbers, peripheral init, and feature
flags are usually hardcoded `#define`s or a vendor HAL config file. Zephyr
splits that into two separate systems, both of which show up before you
even get to runtime API calls:

- **Kconfig** (`prj.conf`): build-time feature flags, e.g. `CONFIG_BT=y`,
  `CONFIG_GPIO=y`. Roughly like enabling modules/`menuconfig` options in
  ESP-IDF; FreeRTOS itself has no equivalent (`FreeRTOSConfig.h` is the
  closest thing, but it only configures the kernel, not drivers/subsystems).
- **Devicetree** (`boards/nrf52840dongle_nrf52840.overlay`): a hardware
  description (which pins, which peripherals) compiled separately from your
  C code, then queried from C via macros like `DT_ALIAS()` / `DT_NODELABEL()`
  (see "Devicetree & device model" below). Comparable to a board support
  package's pin/peripheral table, but resolved at build time from a
  dedicated `.overlay`/`.dts` file instead of being hand-written `#define`s.

### Kernel primitives (`<zephyr/kernel.h>`)

| Zephyr | What it does here | Used in | FreeRTOS analogue |
|---|---|---|---|
| `K_MSGQ_DEFINE(name, size, count, align)` | Statically declares a fixed-size message queue (no heap allocation). | `main.c` — `action_msgq`, carries button-press events out of ISR context | `xQueueCreate()` — but Zephyr's `_DEFINE` macros allocate the object statically at compile time by default, closer to FreeRTOS's `xQueueCreateStatic()` than to the heap-allocating call |
| `k_msgq_put(&q, &item, timeout)` | Enqueues one item. | `main.c` button ISRs (`btn_cam_on_isr`, `btn_cam_off_isr`), called with `K_NO_WAIT` since it must not block in ISR context | `xQueueSendFromISR()` |
| `k_msgq_get(&q, &item, timeout)` | Blocks (up to `timeout`) waiting for an item. | `main.c` main loop, called with `K_FOREVER` | `xQueueReceive()` |
| `K_TIMER_DEFINE(name, expiry_fn, stop_fn)` | Statically declares a software timer. | `ant_garmin.c` — `temp_broadcast_timer` (periodic), `temp_session_timer` (one-shot) | `xTimerCreate()` (again, static by default rather than heap-allocated) |
| `k_timer_start(&t, duration, period)` | (Re)arms a timer: fires once after `duration`, then every `period` (or once only if `period` is `K_NO_WAIT`). Safe to call on an already-running timer to reschedule it. | `ant_garmin_note_activity()` | `xTimerStart()` / `xTimerChangePeriod()` |
| `k_timer_stop(&t)` | Cancels a running timer. | `temp_session_timer_expiry()`, to end the broadcast session | `xTimerStop()` |
| **Timer expiry context — important difference** | A `k_timer`'s `expiry_fn` runs **in ISR context** (the system clock interrupt), always — there is no separate "timer task". | Why `temp_broadcast_timer_expiry()` only calls `k_work_submit()` instead of doing the sensor read/broadcast directly | FreeRTOS timer callbacks run in the **Timer Daemon task** (a real task, not an ISR) — so blocking-unsafe code in a Zephyr timer callback is a correctness bug, not just bad practice, in a way it wouldn't automatically be in FreeRTOS |
| `K_WORK_DEFINE(name, handler)` | Statically declares a work item bound to the system workqueue. | `ant_garmin.c` — `temp_broadcast_work` | No first-class equivalent; you'd typically hand-roll this in FreeRTOS as "ISR posts to a queue, a dedicated task drains it" (exactly the pattern this project's own `action_msgq` uses for buttons) |
| `k_work_submit(&work)` | Schedules a work item to run (soon) on the system workqueue thread — safe to call from ISR context. | `temp_broadcast_timer_expiry()` | Closest is `xQueueSendFromISR()` to a queue that a worker task reads, or `xTimerPendFunctionCallFromISR()` |
| `K_NO_WAIT`, `K_FOREVER`, `K_MSEC(n)`, `K_MINUTES(n)` | Timeout/duration values used across the calls above. | Throughout | `0` / `portMAX_DELAY` / `pdMS_TO_TICKS(n)` — Zephyr timeouts are typed (`k_timeout_t`) and unit-named instead of raw tick counts |

### Devicetree & device model (`<zephyr/devicetree.h>`, `<zephyr/device.h>`)

| Zephyr | What it does here | Used in |
|---|---|---|
| `DT_ALIAS(name)` | Resolves a devicetree alias (defined in the `.overlay`, e.g. `sw1`) to a node identifier at build time. | `main.c` — `BTN_SW1_NODE`, etc. |
| `DT_NODELABEL(name)` | Resolves a devicetree node by its label (e.g. `temp`) instead of an alias. | `temp_sensor.c` |
| `GPIO_DT_SPEC_GET(node, prop)` | Builds a `struct gpio_dt_spec` (port + pin + flags) from devicetree data, at compile time. | `main.c` — `btn_sw1` |
| `DEVICE_DT_GET_OR_NULL(node)` | Gets a `const struct device *` handle for a devicetree node, or `NULL` if it doesn't exist/isn't enabled. | `temp_sensor.c` — `temp_dev` |
| `device_is_ready(dev)` | Checks a driver finished initializing successfully before using it — Zephyr's device model always requires this check. | `temp_sensor_init()` |

There's no direct FreeRTOS equivalent for this group — FreeRTOS has no
device driver model of its own; you'd normally call a vendor HAL's
`_Init()`/`_IsReady()` function directly instead of going through a
generic `struct device`.

### GPIO (`<zephyr/drivers/gpio.h>`)

| Zephyr | What it does here | Used in |
|---|---|---|
| `gpio_is_ready_dt(&spec)` | Readiness check for a `gpio_dt_spec` (built on `device_is_ready()` above). | `setup_buttons()` |
| `gpio_pin_configure_dt(&spec, flags)` | Configures a pin's direction/pull per its devicetree flags. | `setup_buttons()` |
| `gpio_pin_interrupt_configure_dt(&spec, trigger)` | Arms a GPIO interrupt (here, `GPIO_INT_EDGE_TO_ACTIVE`). | `setup_buttons()` |
| `gpio_init_callback(&cb, handler, pin_mask)` + `gpio_add_callback(port, &cb)` | Registers an ISR-context callback for one or more pins on a port. | `setup_buttons()` |

Conceptually the same as registering a GPIO/EXTI interrupt handler with a
vendor HAL (e.g. `HAL_GPIO_EXTI_Callback()` on STM32, `gpio_isr_handler_add()`
on ESP-IDF) — Zephyr just standardizes the registration API across chips.

### Logging (`<zephyr/logging/log.h>`)

| Zephyr | What it does here |
|---|---|
| `LOG_MODULE_REGISTER(name, level)` | Declares a named log source with a default level, once per file (see the top of each `.c` file here). |
| `LOG_INF(...)`, `LOG_WRN(...)`, `LOG_ERR(...)`, `LOG_HEXDUMP_INF(...)` | `printf`-style logging at increasing severity, routed through Zephyr's logging subsystem (here, out over USB CDC-ACM — see `prj.conf`'s `CONFIG_LOG`). |

FreeRTOS has no built-in logging subsystem; projects typically wrap
`printf`/UART writes themselves. Zephyr's logging adds per-module levels,
optional deferred/async processing, and multiple backends (UART, USB,
RTT, …) for free.

### Sensor driver API (`<zephyr/drivers/sensor.h>`)

| Zephyr | What it does here | Used in |
|---|---|---|
| `sensor_sample_fetch(dev)` | Triggers a fresh reading from the device. | `temp_sensor_read()` |
| `sensor_channel_get(dev, channel, &val)` | Reads one channel (here, `SENSOR_CHAN_DIE_TEMP`) from the last-fetched sample, as a `struct sensor_value` (integer + micro-fraction pair, to avoid requiring float). | `temp_sensor_read()` |

This is Zephyr's generic sensor abstraction — the same two calls work for
any Zephyr-supported sensor (accelerometer, humidity, etc.), not just this
one. FreeRTOS has no equivalent; you'd call a specific sensor driver's own
read function directly.

### Settings / persistent storage (`<zephyr/settings/settings.h>`)

| Zephyr | What it does here | Used in |
|---|---|---|
| `settings_load()` | Loads all registered persistent key-value settings from flash (here, BLE bonding keys) back into RAM at boot. | `ble_gopro_init()` |

Comparable to calling a vendor NVS/EEPROM-emulation library's "load"
function yourself (e.g. ESP-IDF's `nvs_get_*()`) — Zephyr's settings
subsystem is that pattern formalized and wired directly into the
Bluetooth stack's bonding storage.

### Bluetooth LE host API (`<zephyr/bluetooth/*.h>`)
`bt_enable()`, `bt_conn_*()` (`BT_CONN_CB_DEFINE`, `bt_conn_set_security`,
`bt_conn_ref`/`unref`, `bt_conn_auth_cb_register`, …), `bt_gatt_*()`
(`bt_gatt_discover`, `bt_gatt_subscribe`, `bt_gatt_write_without_response`),
`bt_le_scan_start`/`stop`, `bt_data_parse` — all used in `ble_gopro.c`.

These aren't a general-RTOS concept, so there isn't a FreeRTOS analogue as
such: FreeRTOS itself has no built-in BLE stack. The closest comparison is
using a standalone BLE host stack directly on top of FreeRTOS (e.g. Apache
NimBLE, or a chip vendor's proprietary BLE SDK) — Zephyr just ships one
(its own native BLE Host) already integrated with the kernel and
devicetree, so you don't wire it up yourself.

---

## 6. ANT+ implementation notes

Before writing real ANT+ radio code, background research (web search
against Nordic/ANT+ Alliance public sources, sourced below) surfaced a
**toolchain/chip gap that blocks the straightforward path** of just adding
ANT+ calls to the existing Zephyr firmware. This section documents that
finding, the licensing process, and the API surface — captured now so the
next step (picking a path forward) is an informed decision rather than a
guess baked into code.

### The core finding: nRF52832 isn't (yet) supported by Zephyr's ANT+ add-on
Nordic ships ANT+ support for the Zephyr-based nRF Connect SDK — the
toolchain this project's BLE code already uses — as a **separate add-on
repository**, "ANT for nRF Connect SDK" (`ant-nrfconnect/sdk-ant` on
GitHub; docs at ant-nrfconnect.github.io). Support was rolled out per-chip:
the nRF5340 first, then nRF52840 (released alongside nRF Connect SDK
v2.6, ~March 2024). **The nRF52832 used in this project (Ebyte
E73-2G4M08S1E) is not on the supported list.** A Nordic engineer confirmed
on DevZone that ANT/ANT+ was "not yet available on the nRF52-series" in
NCS (only nRF53 at the time), and a January 2025 DevZone thread titled
*"Add ANT support to nRF52832 with nRF Connect SDK v2.9.0"* indicates it
still wasn't as of that SDK version, consistent with the official v1.3.0
release notes stating the add-on is "production ready for nRF5340 and
nRF52840" only.
[Getting Started](https://www.thisisant.com/APIassets/1.1.0_ANTnRFConnectDoc/doc/getting_started.html) ·
[DevZone #107934](https://devzone.nordicsemi.com/f/nordic-q-a/107934/add-ant-central-function-to-nrf52840-with-nrf-connect-sdk) ·
[DevZone #118127](https://devzone.nordicsemi.com/f/nordic-q-a/118127/add-ant-support-to-nrf52832-with-nrf-connect-sdk-v2-9-0) ·
[v1.3.0 release notes](https://www.thisisant.com/APIassets/ANTnRFConnectDoc/doc/releases/release-notes-1.3.0.html)

Separately, the **older, non-Zephyr "nRF5 SDK"** (classic C SDK, its own
build system, no `west`/CMake/Zephyr) *does* support ANT+ on the nRF52832,
via the **S212** (ANT-only) or **S332** (BLE+ANT+ concurrent) SoftDevices —
Nordic/ANT+ Alliance bulletins explicitly confirm these are available for
the nRF52832.
[S212 product page](https://www.nordicsemi.com/Products/Development-software/S212-ANT) ·
[tech bulletin](https://www.thisisant.com/developer/resources/tech-bulletin/updated-s212-and-s332-v091-ant-protocol-stacks-now-available-for-nordic-n)

**This corrects earlier revisions of this document and of the firmware
comments**, which described "SoftDevice S212/S332 or the nRF5 SDK ANT
module" as if they were interchangeable options for the same toolchain.
They're actually the same (older) generation of tooling. The real fork in
the road is: **nRF5 SDK (old, non-Zephyr, nRF52832-capable) vs. "ANT for
nRF Connect SDK" (new, Zephyr, currently nRF52840/nRF5340-only)** — and
this project's existing Zephyr BLE firmware cannot pull in ANT+ on its
current chip without either changing chip or splitting the toolchain.

### Options considered, and the decision

1. ~~Port to the classic nRF5 SDK for the ANT+ side, on the current
   nRF52832.~~ Not chosen — would mean either a second, separate firmware
   image/toolchain alongside the existing Zephyr one (nRF5 SDK doesn't use
   `west`/CMake), or migrating the whole project off Zephyr, losing the
   Zephyr-specific work already done in `ble_gopro.c`/`temp_sensor.c`.
2. **CHOSEN: move the target chip to the nRF52840**, supported by the
   Zephyr ANT+ add-on today. Keeps one unified Zephyr toolchain — the
   existing `ble_gopro.c`/`ant_garmin.c`/`temp_sensor.c` structure carries
   over, adding `ant_*` calls (see API surface below) instead of a second
   toolchain. Chosen over the nRF5340 (the add-on's other supported chip)
   for architectural continuity — see "MCU / radio" above for the
   reasoning. This **is** a hardware change: new module, PCB/footprint
   rework — tracked in "Open points" below, not yet done.
3. ~~Reconsider whether ANT+ is required at all~~ Not chosen — not
   researched, and the user's direction was to keep ANT+ and resolve the
   chip/toolchain gap instead.

**Still open, now that the chip decision is made** (see "Open points"):
sourcing a specific nRF52840 module for the PCB; the concrete west
manifest / Kconfig steps to pull in `sdk-ant` and enable it alongside the
existing Zephyr BLE host; and confirming that concurrent BLE+ANT+ from one
Zephyr firmware is actually supported on the nRF52840 (this project needs
both, since GoPro control stays on BLE) — none of this was verified in the
research behind this section yet.

### Licensing & cost (confirmed)
- A free **"ANT+ Adopter"** signup at thisisant.com grants the ANT+
  network key, the ANT+ device profile documents (needed to confirm the
  exact page layouts flagged as unverified elsewhere in this document —
  see "Garmin Edge communication (ANT+)"), forum access, and a
  shared-source license for commercial ANT+ products. This is the right
  first step regardless of which option above is chosen.
  [Licensing](https://www.thisisant.com/developer/ant/licensing) ·
  [Network Keys](https://www.thisisant.com/developer/ant-plus/ant-plus-basics/network-keys)
- A separate, paid **"ANT+ Membership"** (~$1,500 USD/yr) exists for
  Technical Working Group participation and pre-release profile access —
  **not needed** for this project.
- **Shipping a commercial product is not free**: on the nRF52-series ANT+
  protocol stack, evaluation/non-commercial use is free
  (`CONFIG_ANT_EVALUATION_KEY`), but a commercial product requires a paid,
  **per-unit royalty** license key (`CONFIG_ANT_LICENSE_KEY`). The exact
  royalty amount is not publicly disclosed — get a quote from ANT Wireless
  before committing to ANT+ for a product intended for sale.
  [Licensing page](https://www.thisisant.com/developer/ant/licensing) ·
  [`CONFIG_ANT_LICENSE_KEY` docs](https://www.thisisant.com/APIassets/ANTnRFConnectDoc/doc/kconfig/CONFIG_ANT_LICENSE_KEY.html)

### API surface (confirmed to exist; not yet used in this codebase)
Two differently-named APIs exist, matching the two toolchains above — do
not mix them up when eventually writing real code:

| Toolchain | Prefix | Example calls | Source |
|---|---|---|---|
| Classic nRF5 SDK (S212/S332) | `sd_ant_*` | `sd_ant_channel_assign`, `sd_ant_channel_id_set`, `sd_ant_channel_radio_freq_set`, `sd_ant_channel_period_set`, `sd_ant_channel_open`, `sd_ant_broadcast_message_tx` | Public GitHub mirrors of the nRF5 SDK, e.g. [particle-iot/nrf5_sdk example](https://github.com/particle-iot/nrf5_sdk/blob/master/examples/ant/ant_relay_demo/main.c) |
| ANT for nRF Connect SDK (Zephyr) | `ant_*` (no `sd_` prefix) | `ant_stack_init`, `ant_stack_reset`, `ant_network_address_set`, `ant_channel_assign`, `ant_channel_id_set`, `ant_channel_radio_freq_set`, `ant_channel_period_set`, `ant_channel_open`, `ant_broadcast_message_tx`, `ant_event_get` | [ANT Interface Reference](https://www.thisisant.com/APIassets/1.1.0_ANTnRFConnectDoc/doc/api/interface.html) |

The nRF5 SDK zip (bundling `ant_interface.h` and the S212/S332 SoftDevice
binaries) appears to be a plain download from Nordic's site without a
clear account gate, but this wasn't conclusively confirmed either way.
[Download page](https://www.nordicsemi.com/Products/Development-software/nRF5-SDK/Download)

### What's still unverified
- The exact byte layout of the Controls (Generic) command page and the
  Environment temperature page — both require the free ANT+ Adopter login
  to access officially; not found from a public, unauthenticated source
  in this research pass. The "Page 73" figure used elsewhere in this
  document is background knowledge, not independently confirmed.
- Whether nRF52832 support for the Zephyr ANT+ add-on is on any public
  roadmap, or permanently excluded.
- Whether the nRF5 SDK download truly requires no account.
