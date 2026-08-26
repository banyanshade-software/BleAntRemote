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
- Ambient/chip temperature is measured using the **nRF52832's built-in die
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
  nRF52832 already runs a BLE stack for the GoPro link and BLE is required for
  iOS compatibility (classic Bluetooth is not supported by this chip and is
  not usable from iOS apps). "Classic Bluetooth" is listed as an alternative
  only in case a future MCU/radio choice made it relevant; no decision is
  needed there today given the confirmed nRF52832 choice.
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
- **Nordic nRF52832** (Ebyte E73-2G4M08S1E module) — Cortex-M4F, 512KB flash / 64KB RAM.
  Remains the **primary target**: existing KiCad schematic/BOM, no
  hardware rework needed.
- Chosen because the silicon natively supports both **BLE and ANT+** on
  the same radio, and because it is the only chip successfully tested in
  practice for BLE pairing with a GoPro (unlike the ESP32).
- Software stack: **nRF Connect SDK (Zephyr)**, low-level C development,
  for both BLE (already implemented, see `ble_gopro.c`) and ANT+.
  Nordic's Zephyr-based ANT+ add-on (`sdk-ant`) supports the nRF52832
  directly via its current "Add-on" deployment model (nRF Connect SDK
  v2.9.2+) — an earlier check of this project only found older
  compatibility data (nRF52840/nRF5340 only) and briefly recorded a
  decision to switch chips as a result; that's been reverted now that the
  fuller picture is confirmed. See "ANT+ implementation notes" at the end
  of this document for the sourcing, the access/build-integration process
  (still to be done), and the plan to also support the **nRF52840** as a
  secondary target (e.g. the nRF52840 Dongle already used for BLE
  bring-up) from the same firmware source tree.

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
  The nRF52832/Zephyr BLE stack supports multi-role operation, but the
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
- Uses the **nRF52832's built-in die temperature sensor** peripheral (a
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
- Based on the **Ebyte E73-2G4M08S1E** module (18.0 x 13.0mm, 43-pin
  castellated package).
- Footprint with **extended pads** (tabs protruding outward from the module)
  to allow hand soldering with an iron, without a reflow oven.
- Programming/debug via **SWD** (SWDIO/SWCLK/GND/VDD pins on a header or
  dedicated pads).

### Development tooling
- Programming probe: **J-Link EDU Mini** (non-commercial use), alternative
  possible with an **ST-Link V2** driven via OpenOCD (not ST-Link V3, which
  is restricted to ST chips).
- Recommended prototyping board before the final PCB: **nRF52-DK**
  (integrated debugger, ready-to-use Zephyr examples). The already-used
  **nRF52840 Dongle (PCA10059)** doubles as a way to exercise the
  secondary nRF52840 target (see "MCU / radio").

---

## 3. Bill of materials (BOM)

| Ref. | Component | Qty | Role | Notes |
|------|-----------|----------|------|-----------|
| U1 | **Ebyte E73-2G4M08S1E** module (nRF52832) | 1 | MCU + BLE/ANT+ radio | Castellated package, extended-pad footprint |
| BT1 | **CR2032** coin cell + holder | 1 | Power supply | Direct supply, no regulator |
| SW1–SW5 | Push buttons (tact switch) | 5 | Camera ON, Camera OFF, Page right, Page left, Lap | GPIO pull-up, active-low |
| C1 | **Tantalum/polymer 47–100µF** capacitor, 0805/1206 package | 1 | Voltage-drop buffer (radio current spikes) | Low ESR required |
| C2 | **100nF** ceramic capacitor, 0402 package | 1 | HF filtering | — |
| J1 | **SWD** header/pads (SWDIO, SWCLK, GND, VDD) | 1 | Programming/debug | Not populated in production, useful for prototyping |
| PCB | Custom board (extended-pad E73 footprint) | 1 | Carrier | KiCad, template already drafted in the project |
| — | Enclosure (3D printed or other) | 1 | Protection/handlebar mount | To be defined (sealing to be planned) |

> Note: no thermistor (NTC) or divider resistor is needed — ambient
> temperature is read from the nRF52832's internal die temperature sensor
> (see "Internal temperature sensor" above). The earlier NTC1/R1 BOM lines
> from previous revisions of this document have been removed.

### Tooling (not mounted on the final board)
| Tool | Role |
|-------|------|
| J-Link EDU Mini (or ST-Link V2 + OpenOCD) | SWD flash / debug |
| nRF52-DK | Prototyping before the final PCB |

---

## 4. Open points / to be decided

- **ANT+ build integration (see "ANT+ implementation notes" at the end of
  this document)**: the chip question is resolved (nRF52832 works, no
  hardware change needed; nRF52840 kept as a secondary supported target).
  Still open: getting ANT+ Adopter access to the gated `sdk-ant` repo, the
  concrete west-workspace integration steps (only a "fresh workspace"
  flow is documented, not composition with an existing manifest like this
  project's), and confirming the project's nRF Connect SDK is upgraded to
  v2.9.2+ (needed for nRF52832 support under the "Add-on" deployment
  model).
- **Zephyr board definition for the custom PCB**: none exists yet, for
  either chip — only the reference `nrf52840dongle_nrf52840` board
  (Nordic's prototyping dongle) has a devicetree overlay today. Needed
  before the "support both chips" firmware goal can actually be built for
  the real hardware.
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
against Nordic/ANT+ Alliance public sources, sourced below) checked
whether the existing Zephyr firmware can simply add ANT+ calls, or whether
a toolchain/chip change is needed. **Short answer: no chip change is
needed.** This section documents the finding, the licensing process, the
access/integration process, and the API surface.

### Decision: support both nRF52832 and nRF52840
Nordic ships ANT+ support for the Zephyr-based nRF Connect SDK — the
toolchain this project's BLE code already uses — as a separate add-on
repository, "ANT for nRF Connect SDK" (`ant-nrfconnect/sdk-ant` on GitHub;
docs at ant-nrfconnect.github.io). It has shipped via two different
deployment models over time:

| sdk-nrf version | Deployment model | sdk-ant version | Supported nRF52 chips |
|---|---|---|---|
| v2.6 – v2.7 | "Manifest" | v1.2.0 – v1.3.0 | nRF52840 only |
| v2.9.2+ | "Add-on" | v2.0.0+ | **nRF52832 and nRF52840** |

**The nRF52832 already used in this project (Ebyte E73-2G4M08S1E, existing
KiCad design) is supported**, provided the project's nRF Connect SDK is on
v2.9.2 or later (the "Add-on" deployment model). An initial research pass
only found the older "Manifest"-model compatibility data (nRF52840/
nRF5340 only) and this document briefly recorded a decision to move the
MCU target to the nRF52840 as a result — **that decision has been
reverted** (see git history) once the newer, corrected data was found.
[Compatibility table](https://ant-nrfconnect.github.io/) (redirects from the older thisisant.com compatibility URL)

Further, direct confirmation that this project's actual requirement —
**concurrent BLE + ANT+ from one firmware, on a single-core nRF52 chip** —
is realistic: Nordic's own sample **"ANT and Bluetooth LE Heart Rate
Monitor Relay"** does exactly that (*"aggregated data received from ANT is
relayed and sent as Bluetooth LE notifications"*) and explicitly lists the
plain **nRF52 DK (nrf52dk, i.e. nRF52832)** as a supported board, alongside
nrf52840dk and nrf5340dk.
[Sample docs](https://ant-nrfconnect.github.io/samples/ble_ant_app_hrm/README.html)

**Given that, the plan is to support both chips from one firmware source
tree**: nRF52832 stays the primary/default target (matches the existing
KiCad schematic and BOM — no PCB rework required), with nRF52840 kept as a
secondary supported target (e.g. for the nRF52840 Dongle already used for
BLE bring-up, or a future higher-memory-headroom board variant). This is a
natural fit for Zephyr, where board/chip selection is a `west build -b
<board>` build-time parameter — see "What this means for the firmware"
below for what is and isn't chip-specific in this codebase already.

Separately, the **older, non-Zephyr "nRF5 SDK"** (classic C SDK, its own
build system) also supports ANT+ on the nRF52832 via the S212/S332
SoftDevices — this is **not needed** now that the Zephyr add-on covers
nRF52832 directly, but is left documented below since it's a real,
differently-named API surface that's easy to confuse with the Zephyr one.
[S212 product page](https://www.nordicsemi.com/Products/Development-software/S212-ANT) ·
[tech bulletin](https://www.thisisant.com/developer/resources/tech-bulletin/updated-s212-and-s332-v091-ant-protocol-stacks-now-available-for-nordic-n)

### Access & integration process (confirmed)
- **The `sdk-ant` repository itself is access-gated**, not merely
  "public but needs an account for the network key": both the GitHub repo
  page and the GitHub API return 404 unauthenticated. Per the docs, access
  is granted to ANT+ Adopters after accepting the license agreement and
  authenticating through GitHub — i.e., sign up as an ANT+ Adopter first
  (see "Licensing & cost" below), then request GitHub org access, before
  any of this can actually be built.
- **Integration mechanism**: the documented getting-started flow is
  `west init -m "https://github.com/ant-nrfconnect/sdk-ant" --mr main &&
  west update` — i.e. `sdk-ant` is used as the **top-level west manifest**,
  not added as one extra project line inside this project's existing
  manifest. In practice this likely means a separate/parallel west
  workspace for ANT+-enabled builds, rather than a one-line addition to
  the current one. The exact composition with an *existing* application's
  manifest wasn't confirmed — `sdk-ant`'s own `west.yml` isn't visible
  without Adopter+GitHub access.
  [Getting Started](https://ant-nrfconnect.github.io/doc/getting_started.html)
  — **corroborated from the other direction**: `nrfconnect/sdk-nrf`'s own
  `west.yml` (public, checked directly on both `main` and the `v2.9.2`
  tag) contains **zero** reference to `ant-nrfconnect`/`sdk-ant`/ANT+ of
  any kind. This project's existing BLE firmware manifest has nothing to
  hook into for ANT+ — a separate workspace really is required, not just
  an undocumented option. Nordic's own SDK docs agree: current
  `nrfconnectdocs.nordicsemi.com` protocol-support pages for the nRF52
  don't mention ANT/ANT+ at all (an older v2.4.4 doc page went further,
  stating outright *"the nRF Connect SDK does not support ANT"* — true
  for that version, superseded by the separate `sdk-ant` add-on since).
  ANT+ support genuinely lives entirely outside `sdk-nrf`'s own tree.
- **No public mirror or cache of `sdk-ant` exists** either: direct,
  unauthenticated fetches (not archive/cache lookups) to both
  `github.com/ant-nrfconnect/sdk-ant` and the GitHub API for that repo
  return HTTP 404. The access gate is real, not just under-documented.
- **A closer-fit reference sample, once access is available**:
  `ant_broadcast_tx`/`ant_broadcast_rx` (plain ANT+ broadcast, no BLE
  relay) is a better template for this project's needs than the
  BLE+ANT+ HRM relay sample cited above — both are only described in the
  docs (page/parameter tables), not with embedded source, but both
  explicitly list `nrf52dk/nrf52832` as a supported board target
  alongside nrf52840dk/nrf5340dk, reinforcing that the nRF52832 is
  first-class, not an edge case, across multiple samples.
- **Kconfig**: top-level enable is `CONFIG_ANT`. For single-core chips
  (nRF52832 and nRF52840, both used here) `CONFIG_ANT_LIBRARY_CORE`
  applies (the nRF5340's dual-core split instead uses
  `CONFIG_ANT_NP_HOST`/`CONFIG_ANT_NP_REMOTE` — not relevant to this
  project). `CONFIG_ANT_CHANNEL_CONFIG` and `CONFIG_ANT_KEY_MANAGER`
  provide channel/key helpers. `CONFIG_ANT_LICENSE_KEY` (paid, commercial)
  vs. `CONFIG_ANT_EVALUATION_KEY` (free, eval/non-commercial) gate actual
  radio use — see "Licensing & cost".
  [Kconfig reference](https://ant-nrfconnect.github.io/doc/kconfig/index.html)
- **No ready-made profile helper for this project's two use cases**:
  Kconfig offers `CONFIG_ANT_COMMON`, `CONFIG_ANT_HRM`, `CONFIG_ANT_BSC`,
  `CONFIG_ANT_BPWR` — but **no `CONFIG_ANT_CONTROLS` or
  `CONFIG_ANT_ENVIRONMENT`**. The Controls (Generic) command page and the
  Environment temperature page will need to be **hand-encoded** on top of
  `CONFIG_ANT_COMMON`/`CONFIG_ANT_CHANNEL_CONFIG`, not pulled from a
  ready-made profile library — reinforcing why the exact byte layouts
  (flagged as unverified elsewhere in this document) matter and need the
  real device profile documents from an ANT+ Adopter account.
- A stable **32.768 kHz LF clock** (±50ppm max) is a hard requirement —
  standard for Zephyr/BLE-capable designs already, not expected to be a
  new constraint for either chip.

### What this means for the firmware (supporting both chips)
- `main.c`, `ble_gopro.c`, `ant_garmin.c`, and `temp_sensor.c` contain no
  chip-specific code today (no `#ifdef` on a chip/board symbol) — this
  should stay true. Anything that differs between the nRF52832 and
  nRF52840 belongs in **board-specific devicetree overlays** and
  board-specific Kconfig fragments, not in application `.c` files, per the
  existing "Maintainability & modularity" requirement.
- One concrete difference already in the codebase: `prj.conf`'s USB
  CDC-ACM logging setup applies to the nRF52840 Dongle (which has native
  USB) and is **not applicable to the nRF52832** (no native USB
  peripheral) — the final CR2032-powered board was always going to use
  SWD/RTT logging instead (see "Development tooling"), so this isn't a new
  problem, but it's a concrete example of a board-specific setting that
  needs to live in a board overlay/Kconfig fragment rather than a single
  shared `prj.conf`, once a second board target is added.
- No Zephyr board definition exists yet for the final custom PCB, on
  *either* chip — only the reference `nrf52840dongle_nrf52840` board
  (Nordic's dongle) has an overlay today. Adding one (or two, one per
  chip) for the actual custom board is separate follow-up work, tracked
  in "Open points".

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
The API this project will actually use is the Zephyr add-on's `ant_*`
family (no `sd_` prefix): `ant_stack_init`, `ant_stack_reset`,
`ant_network_address_set`, `ant_channel_assign`, `ant_channel_id_set`,
`ant_channel_radio_freq_set`, `ant_channel_period_set`, `ant_channel_open`,
`ant_broadcast_message_tx`, `ant_event_get`.
[ANT Interface Reference](https://www.thisisant.com/APIassets/1.1.0_ANTnRFConnectDoc/doc/api/interface.html)

For reference only (not needed for this project, see the decision above):
the older, non-Zephyr nRF5 SDK uses a **differently-named**, non-
interchangeable `sd_ant_*` API (`sd_ant_channel_assign`,
`sd_ant_broadcast_message_tx`, etc.), visible in public GitHub mirrors of
the nRF5 SDK, e.g.
[particle-iot/nrf5_sdk example](https://github.com/particle-iot/nrf5_sdk/blob/master/examples/ant/ant_relay_demo/main.c).
Do not mix the two up if ever cross-referencing nRF5 SDK sample code.

### What's still unverified
- The exact byte layout of the Controls (Generic) command page and the
  Environment temperature page — both require the free ANT+ Adopter login
  to access officially; not found from a public, unauthenticated source
  in this research pass. The "Page 73" figure used elsewhere in this
  document is background knowledge, not independently confirmed.
- How `sdk-ant`'s own west manifest actually composes with an *existing*
  application's manifest (this project's) — only the "fresh workspace
  init" flow is publicly documented; the repo's own `west.yml` isn't
  visible without Adopter + GitHub org access. Corroborated as a real gap
  (not just under-documented): `nrfconnect/sdk-nrf`'s own public `west.yml`
  has no ANT+ reference at all, and no public mirror/cache of `sdk-ant`
  could be found (direct 404s on both the GitHub page and API,
  double-checked with no archived/cached sources used per user
  instruction) — so this remains genuinely unknown until someone with
  Adopter + GitHub access actually looks.
- Whether ANT+ Adopter GitHub org access has any review/wait time after
  signup, and the exact steps once granted.
- If a future nRF52840-specific board is added later: whether an
  nRF52840-family Ebyte module (e.g. **E73-2G4M08S1C**, confirmed to
  exist as a real shipping part) is pin-compatible with the existing
  E73-2G4M08S1E KiCad footprint — not verified, and the nRF52840 has more
  GPIOs than the nRF52832, so a pin-for-pin match is unlikely even within
  the same module family. Treat as needing its own footprint, not a
  drop-in swap, until checked against both datasheets.
