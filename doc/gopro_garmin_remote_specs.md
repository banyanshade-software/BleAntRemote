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
- Exact broadcast profile/timing: see "Garmin Edge communication (ANT+)"
  below and "Open points".

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
  - `ble_gopro.[ch]` — BLE/GoPro module (Open GoPro API).
  - `ant_garmin.[ch]` — ANT+/Garmin module (currently a stub, see firmware README).
- Rationale: several protocols/features are still open or unimplemented
  (ANT+, mobile-app BLE configuration — see "Remote configuration" above).
  Keeping them isolated avoids one area's changes breaking another, keeps
  each module independently testable, and makes it easier for a new
  contributor to work on a single protocol without understanding the whole
  firmware.
- This requirement complements the "all documentation and code comments in
  English" convention already applied across this project.

### Overall architecture
- MCU **wakes only on button press** (System OFF between actions), no
  permanent radio connection.
- On wake-up: the firmware identifies the pressed button (GPIO controller
  LATCH register), activates the corresponding radio stack (BLE or ANT+),
  executes the command, then returns to deep sleep.
- **Only one protocol active at a time** (sequential multi-protocol, not
  simultaneous) → simplifies the firmware and limits power consumption.
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
   - **Session reuse, no extra wake-up**: the temperature reading is
     intended to be sent opportunistically whenever a Garmin button
     (page right/left, lap) already wakes the device and activates the
     ANT+ radio — this fits the existing "wake only on button press, one
     protocol at a time" architecture with no additional power cost.
   - **Open question**: this only updates the Edge's temperature field when
     a Garmin button is pressed, not continuously. Whether the product
     needs periodic (timer-driven) updates while idle — and the resulting
     battery-life trade-off against the "several months on CR2032" target
     — is **not decided yet** (see "Open points").
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
- Activated only occasionally (on the same wake-up as a Garmin button
  press, see "Garmin Edge communication (ANT+)" above), not continuously,
  to preserve battery life.

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
- **ANT+ temperature broadcast timing**: opportunistic (piggybacked on
  Garmin button presses, no extra wake-up) vs. periodic/timer-driven
  (continuously updated Edge temperature field, but adds wake-ups outside
  of button presses and impacts the CR2032 battery-life target) — not
  decided yet.
- **Internal sensor accuracy**: die temperature vs. true ambient
  temperature offset/calibration to be validated on real hardware.
