# GoPro + Garmin Edge Bike Remote — Specs & BOM

## 1. Functional specifications

### Objective
Compact housing mounted on the handlebar, allowing remote control of:
- the **GoPro Hero 11 Black Mini** camera (via BLE / Open GoPro API)
- the **Garmin Edge Explore** cycling computer (via ANT+ / Generic Controls profile),
  which also carries the ambient temperature broadcast (see "Thermometer" below)

### Controls (5 buttons)

| # | Button | Action | Protocol |
|---|--------|--------|-----------|
| 1 | Camera ON | Starts GoPro recording | BLE (Open GoPro) |
| 2 | Camera OFF | Stops GoPro recording | BLE (Open GoPro) |
| 3 | Page right | Scrolls the Edge screen to the right | ANT+ (Generic Controls) |
| 4 | Page left | Scrolls the Edge screen to the left | ANT+ (Generic Controls) |
| 5 | Lap | Marks a lap on the Edge | ANT+ (Generic Controls) |

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
- Chosen because it natively supports both **BLE and ANT+** on the same radio
  (different stacks depending on the protocol in use), and because it is the
  only chip successfully tested in practice for BLE pairing with a GoPro
  (unlike the ESP32).
- Software stack: **nRF Connect SDK (Zephyr)**, low-level C development.

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

### Garmin Edge communication (ANT+)
ANT+ is used for two purposes, both from the same radio/module:

1. **Remote control** — ANT+ **Generic Controls** profile (the same one used
   by the official Garmin *Edge Remote* accessory).
   - Commands sent as **Page 73 (Generic Command)** with the corresponding
     key code (page right / page left / lap).
   - Edge Explore confirmed compatible with ANT+ and with the Edge Remote
     accessory (same profile).
2. **Temperature broadcast** — the internal die temperature (see
   "Thermometer" above) is sent so the Edge can show it as a data field.
   - Expected profile: ANT+ **Environment Sensor** (temperature), the
     common device type Garmin Edge units already recognize for ambient
     temperature accessories — **to be confirmed** against the ANT+
     specification once the ANT stack is available.
   - **Broadcast policy (decided)**: idle by default; **any** button press
     (camera or Garmin) arms a session that sends a reading every 5 minutes
     for up to 30 minutes since the last press, then returns to idle — see
     "Thermometer" above for the full behavior. This is triggered by any
     button, not only the Garmin ones, so a camera-button press (BLE-only
     action) also arms the ANT+ session; the actual periodic ANT+
     transmissions still happen sequentially/one-protocol-at-a-time, just
     scheduled a few minutes later rather than piggybacked on an
     already-open ANT+ link.
   - **Open question**: whether the Generic Controls channel and the
     Environment Sensor broadcast need to run as two separate concurrent
     ANT+ channels (channel count depends on the ANT stack/SoftDevice
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
  (integrated debugger, ready-to-use Zephyr examples).

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
  Sensor page/device-type used, and whether it needs its own ANT+ channel
  alongside the Generic Controls channel (concurrent channel count depends
  on the ANT stack/SoftDevice chosen — not obtained yet).
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
