# GoPro + Garmin Edge Bike Remote — Hardware Notes

Companion document to `gopro_garmin_remote_specs.md`, focused on the
hardware/electronics side (PCB, power, programming, enclosure-adjacent
electrical decisions). Same conventions as the specs document: sourced
claims are marked, unverified/assumed points are flagged as such.

---

## 1. Hardware summary

- **MCU / radio**: Nordic nRF52 with BLE + ANT+ on one chip — see the
  specs document for why this family was chosen over alternatives (e.g.
  ESP32). **Module under revision (§9)**: the Ebyte E73-2G4M08S1E
  previously listed here turned out to be an **nRF52833** (not supported
  by sdk-ant) and is dropped. Preferred replacement: **Fanstel BT832
  (nRF52832, 14 × 16mm, castellated, in the JLCPCB library) + 32.768kHz
  crystal on our board**; alternatives Raytac MDBT42Q, E73-2G4M08S1C,
  E73-2G4M04S1B. Until §9 is closed, the nRF52832 figures
  quoted in this document are provisional.
- **Power**: single **CR2032** coin cell, directly powering the nRF52
  (no regulator — native 1.7–3.6V operating range). A buffer capacitor
  (tantalum/polymer, 47–100µF, low ESR) absorbs BLE/ANT+ current spikes;
  a 100nF ceramic capacitor handles HF filtering. Battery level is read
  purely in software via the internal SAADC VDD channel — no extra BOM
  component.
- **Inputs**: 5 tact switches (individual GPIOs, no matrix), internal
  pull-up, active-low. Wake-up is GPIO PORT-event based (SENSE + LATCH),
  so idle current is dominated by leakage, not polling.
- **Sensing**: ambient temperature is read from the nRF52832's *internal*
  die-temperature peripheral — no external thermistor, no extra ADC
  input.
- **PCB**: custom board built around the E73 module's castellated
  footprint, with **extended pads** so it can be hand-soldered with an
  iron (no reflow oven required).
- **User feedback**: red + yellow-green (AlGaInP) indicator LEDs,
  resistor-fed straight from the cell and sunk by high-drive GPIOs
  (≈ 5mA red / ≈ 10mA green peak, short flashes) — see §8.
- **Programming/debug**: **SWD** (SWDIO / SWCLK / GND / VDD) exposed on
  a header or bare pads (`J1`), not populated in production, only used
  during development/flashing.
- **Enclosure**: not yet defined — handlebar mount, outdoor-rated
  (vibration, weather), sealing still to be designed (see specs doc,
  "Open points").

This is a summary view; see `gopro_garmin_remote_specs.md` §2–3 for the
full functional/technical spec and BOM.

---

## 2. Battery vs. programmer: can the CR2032 stay in place while flashing with an ST-Link V2?

**Short answer: yes, this works, and it's the normal way to do it — the
CR2032 stays as the only power source, and the ST-Link V2 only taps the
SWD signal lines, it does not need to (and normally should not) power
the board itself.**

- **What the SWD connector's "VDD" pin actually does.** On both the
  nRF52's SWD pins and the ST-Link V2 side, the pin often labeled
  `VDD`/`VAPP`/`VDD_TARGET` on this kind of connector is a **voltage
  *sense* input**, not a power output: the probe reads it to know the
  target's logic level (for signal-level compatibility / level
  shifting), it doesn't source current to power the board unless you
  deliberately wire it that way. ST's own community forum threads on
  this are explicit: *"The STLINK V2 adapter does not supply your
  target unless you do it yourself using a separate 3.3V source"*, and
  *"On a real ST-LINK this is meant to be an input for
  detecting/measuring board voltage."* Boards that *are* powered by the
  probe (e.g. some Nucleo setups) do so through a separate, deliberate
  jumper/wiring choice, not as default SWD behavior.
  [ST community: SWD operation without connecting VDD_TARGET](https://community.st.com/t5/stm32cubeprogrammer-mcus/stlink-v2-swd-operation-without-connecting-vdd-target-pin/td-p/837357) ·
  [ST community: power output *to* target?](https://community.st.com/t5/stm32-mcus-boards-and-hardware/st-link-v2-power-output-to-target/td-p/457476)
- **Practical consequence for this board**: wire `J1` as SWDIO / SWCLK /
  GND, plus the sense line back to the board's own VDD (i.e. the CR2032
  rail) so the probe knows it's talking to a ~3V part — do **not** tie
  that pin to the probe's own 3.3V output. The CR2032 keeps powering the
  nRF52 exactly as in normal operation; the ST-Link V2 (via OpenOCD, as
  already noted in the specs doc) just observes/drives the two SWD
  signal lines.
- **Is a coin cell "enough" to power a board *while* it's being
  flashed/debugged?** Yes for this use case. The debug link itself adds
  negligible current (a couple of logic-level GPIOs), so it doesn't
  meaningfully add to what the CR2032 already has to supply when the
  radio (BLE/ANT+) is active — which the buffer capacitor is already
  sized for (see specs doc, "Power supply"). The only caveat: a partly
  depleted cell with high internal resistance could sag under a flash
  operation's brief current pulses the same way it could during normal
  radio TX — using a reasonably fresh cell for bring-up/debug sessions
  avoids flakiness that would otherwise be misdiagnosed as a firmware or
  SWD-link bug.
- **J-Link EDU Mini vs. ST-Link V2**: the same reasoning applies to
  either probe (per the specs doc, J-Link EDU Mini is the primary
  option, ST-Link V2 + OpenOCD the noted alternative) — both are
  sense-only on the target-voltage pin by default.

**Conclusion**: no hardware change needed to support "flash while
running off the coin cell" — this is already what the existing `J1` SWD
header (per the specs doc BOM) is for. Worth adding to the PCB/BOM notes
explicitly so nobody wires VDD as a power feed by mistake later.

**`J1` now also carries SWO** (one extra pin, P0.18 on the nRF52832) for
ITM/printf trace — see §9.7 "Debug & trace" for the full 5-pin `J1`
pinout and why ETM/parallel trace is out of scope.

---

## 3. Reverse-polarity protection for the CR2032

**Question**: does this design need explicit reverse-polarity protection
(diode or MOSFET) on the coin cell, or is the mechanical protection from
the holder itself (most CR2032 holders only accept the cell one way)
sufficient — and what do comparable designs actually do?

**Findings (not yet a final decision — see recommendation below):**

- **Diode protection is a poor fit for a CR2032 design.** A series
  diode's forward drop (~0.3–0.7V depending on type) eats directly into
  the CR2032's already-thin 3.0V→~2.0V discharge curve. Community
  discussion on exactly this trade-off (Nordic DevZone, for an
  nRF51822/coin-cell product) frames it the same way this project would
  need to: a diode effectively raises the *usable* end-of-life voltage
  from ~2.0V to ~2.7V, which can throw away a meaningful fraction of the
  cell's usable capacity — a real problem for the "several months on one
  CR2032" target already stated in the specs doc.
  [Nordic DevZone: MOSFET or Schottky diode for reverse battery protection with a coin cell + nRF51822](https://devzone.nordicsemi.com/f/nordic-q-a/14408/mosfet-or-schottky-diode-for-reverse-battery-protection-with-coin-cell-battery-and-nrf51822-in-a-power-efficient-product)
- **A P-channel MOSFET is the standard low-loss alternative**, when
  protection is wanted at all: wired so its body diode/gate-source
  relationship only turns it on for correct polarity, its on-resistance
  drop is a small fraction of a diode's forward drop (single-digit mV to
  low tens of mV at these currents, depending on the part), which is
  much more compatible with a coin-cell power budget.
  [components101: PMOS design guide for reverse-polarity protection](https://components101.com/articles/design-guide-pmos-mosfet-for-reverse-voltage-polarity-protection) ·
  [Hackaday: reverse voltage protection with a P-FET](https://hackaday.com/2011/12/06/reverse-voltage-protection-with-a-p-fet/)
- **Correction vs. the earlier draft of this section — "mechanical
  keying" is weaker than it sounds for a plain CR2032.** A CR2032 is a
  **symmetric disc** (20mm diameter on both faces, only the polarity
  marking differs) — it is not "keyed" by its own shape the way, say, a
  polarized connector is. Generic open-frame/spring-clip holders (the
  common, cheap kind, including the through-hole part picked in §4
  below) mechanically accept the cell **either way round**; only a
  dedicated anti-reversal mechanism (e.g. a housing shaped so the cell
  only seats correctly one way, the subject of at least one US patent
  specifically on this problem) truly blocks reversal, and that is not
  what most inexpensive holders implement. Forum discussion of this
  exact question (Arduino Forum, ST community, Contextual Electronics
  Forum) confirms the same conclusion: physical reversal is generally
  *possible* in a standard holder, so "mechanical protection" in
  practice means clear +/- silkscreen/labeling plus the mild inconvenience
  of doing it wrong, not a hard mechanical block.
  [Arduino Forum: reverse polarity protection for coin cell?](https://forum.arduino.cc/t/reverse-polarity-protection-for-coin-cell/508371) ·
  [ST community: typical practice for protecting VBAT from a reversed coin cell](https://community.st.com/t5/stm32-mcus-products/what-is-the-typical-practice-for-protecting-vbat-from-a-reversed/td-p/569910) ·
  [Contextual Electronics Forum: CR2032 reverse polarity protection?](https://forum.contextualelectronics.com/t/cr2032-reverse-polarity-protection/3594) ·
  [US7118817B2 — coin cell protection against reverse insertion in cell holder](https://patents.google.com/patent/US7118817)
- **Other real designs go either way depending on risk tolerance /
  target market**, not a single universal answer:
  - Some nRF52 + coin-cell reference designs add explicit reverse
    protection (one example found uses an LDO plus a dedicated
    reverse-polarity protection diode ahead of it) — but that's in a
    *regulated* design, where the diode's drop is absorbed by the LDO's
    headroom, a very different power budget from this project's
    *unregulated*, direct-to-chip CR2032 supply.
    [Northwest Engineering Solutions: nRF52 module with battery power](https://www.nwengineeringllc.com/resources/examples/nrf52-module-battery-power.php)
  - TI's own CR2032-powered wireless-sensor reference designs (e.g.
    MSP430FR5969 / HDC2010-based designs cited above) are built the
    same way this project is (direct-to-chip, no regulator) and do not
    appear to add a discrete reverse-protection component in their BOM —
    consistent with relying on the holder's mechanical keying.
    [TI TIDUDW6 reference design](https://www.ti.com/lit/ug/tidudw6/tidudw6.pdf) ·
    [TI TIDUDW5 reference design](https://www.ti.com/lit/ug/tidudw5/tidudw5.pdf)

**Recommendation for this project**: given the direct-to-chip
(unregulated) power architecture and the "several months on one CR2032"
target, a series diode is not a good fit (voltage-budget cost too high).
Given the correction above — the through-hole holder selected in §4
does **not** mechanically block reversed insertion — "mechanical-only"
is not actually a safe option here on its own:
1. ~~**Mechanical-only**~~ — ruled out: the holder chosen in §4 (like
   most generic open-frame CR2032 holders) physically accepts the cell
   both ways round, so this would mean shipping with no real protection
   beyond a silkscreen marking.
2. **Add a P-channel MOSFET** in the battery-negative or -positive leg
   for near-zero-drop protection — a few extra cents/mm² of PCB space.
   **This is the recommended option**, specifically because the chosen
   holder (§4) is not self-keying and the battery is user-replaceable
   (a rider will swap the CR2032 in the field, without the assembly-line
   care a sealed/factory-only device might get).

**Decision**: add the P-MOSFET reverse-polarity protection stage to the
BOM/schematic. This supersedes the earlier "open, pending holder choice"
framing now that §4 has picked a specific (non-keying) holder part.

---

## 4. CR2032 through-hole holder selection

**Requirement**: a through-hole (solder-in-PCB) CR2032 holder, easily
sourced from standard distributors — and, per user correction, the cell
must sit **flat against the PCB** (lying parallel to the board, behind
the Ebyte module), not standing on edge or protruding vertically, since
this has to fit in a slim handlebar-mounted enclosure.

**Correction — the Keystone 1074 (previously recommended) does not
fit this constraint.** It is a "top-loading" holder: the cell sits
with its flat faces roughly perpendicular to the PCB and is dropped in
from above, with the manufacturer's own listing giving a **0.72"
(18.3mm) height above the board** — essentially another cell's diameter
of clearance standing straight up off the PCB. That's fine for a
project with a deep case (or the battery on a separate compartment) but
wrong here, where the datasheet directly says the cell must lie flat
behind the Ebyte module in a slim enclosure.

**Revised recommendation: MPD (Memory Protection Devices) `BC2032-E2`**
— a horizontal, through-hole, "open" coin-cell holder where the CR2032
lies flat in the plane of the PCB.
- **Through-hole / PC-pin mount, low profile**: **5.08mm height above
  the board** (per DigiKey's listing) — only slightly more than the
  CR2032's own 3.2mm thickness, consistent with "flat behind the
  module," not standing proud of the board.
  [DigiKey: BC2032-E2](https://www.digikey.com/en/products/detail/mpd-memory-protection-devices/BC2032-E2/2077836)
- **Widely available / not single-sourced**: stocked at DigiKey, LCSC,
  JLCPCB's parts library (convenient if boards are ever assembled
  there), and listed by several other distributors under the same
  MPD part number.
  [LCSC: BC2032-E2](https://www.lcsc.com/product-detail/C6129150.html) ·
  [JLCPCB parts: BC2032-E2](https://jlcpcb.com/partdetail/7072211-BC2032E2/C6129150) ·
  [MPD/batteryholders.com: BC2032-E2](https://www.batteryholders.com/part.php?pn=BC2032-E2&original=CR2032&override=CR2032)
- **Advertised "reverse polarity protection"**: several distributor
  listings describe this specific part as including reverse-polarity
  protection, which — if real and mechanical rather than just a
  marketing label — would be a nicer property than the generic
  open-frame holders discussed in §3. **Not independently verified
  here**: no source found actually diagrams *how* the mechanism works
  (contact shaping, an insulating step, etc.), so treat this as an
  unconfirmed bonus, not a substitute for the P-MOSFET protection
  decided in §3 — confirm from MPD's actual datasheet/mechanical
  drawing before relying on it, and keep the MOSFET regardless.
- Accepts the wider 20mm coin-cell family (CR2032 and similarly-sized
  cells), same as most parts in this class.

**Alternative (thicker, but with a more clearly documented mechanical
outline): NTE Electronics `23-BHCC-1`** — also horizontal/through-hole,
"Low Profile for High Density Packaging" per NTE's own datasheet,
9.2mm height above board (roughly double the BC2032-E2's), stocked at
DigiKey/Hawk Electronics/Micro Center. Worth keeping as a fallback if
the BC2032-E2 ever goes out of stock or its reverse-protection claim
doesn't check out against the real datasheet.
[DigiKey: 23-BHCC-1](https://www.digikey.com/en/products/detail/nte-electronics-inc/23-BHCC-1/16499218) ·
[NTE datasheet (coin-cell holders)](https://datasheet.octopart.com/23-BHCC-1-NTE-Electronics-datasheet-164421914.pdf)

**Still applies from §3**: whichever of these is used, treat it as
"polarity-marked," not "polarity-proof" (pending the BC2032-E2
datasheet check above) — the P-MOSFET protection stage stays in the
BOM.

---

## 5. Buttons — size, sealing, sourcing

**Requirement** (per project owner): 4 or 5 momentary push buttons
(2 for the camera, page-left/page-right for the Garmin, optionally a
5th — see "Handling of the 2 camera buttons" in the specs doc's open
points for whether camera ON/OFF stays 2 separate buttons), each:
- operable **with cycling gloves on** — actuator diameter **may be
  reduced to ~4–5mm** (user decision, supersedes the earlier 7–10mm
  target); a small cap can still enlarge the touch target if wanted,
- **rain-resistant** (splashing/dripping water while riding, not
  submersion),
- **easy to source and cheap**.

**Rain-resistant vs. waterproof — refining the IP target.** "Rain on a
handlebar" is the IEC 60529 **IP65** condition (dust-tight + protected
against low-pressure water jets from any direction), not the stricter
**IP67/IP68** (temporary/continuous full immersion) that most
"waterproof switch" marketing copy leads with. Any switch rated IP65 or
better covers the actual requirement here — IP67-rated parts (common
and not meaningfully pricier in this switch class) are fine too, just
not something to pay extra to chase beyond IP65.

**Revised requirement (user correction #1): PCB-mount, not
panel-mount.** The panel-mount switches originally proposed here
(bolted through the enclosure wall, wired to the board) are the wrong
shape for a genuinely compact design — they need standoff depth behind
the panel for the switch body/bushing/nut, and separate wiring.
**Soldered directly onto the PCB** is both more compact and simpler to
assemble; that constrains the choice to a *sealed tactile switch*, not
a generic panel-mount button.

**Revised requirement (user correction #2): through-hole, not SMD.**
The first PCB-mount candidate proposed here (APEM/MEC's `Ultramec 6C`)
was surface-mount — ruled out, since this project's hand-solder-with-an-iron
approach (already the rationale for the Ebyte module's extended-pad
footprint in §1) favors through-hole parts. That rules out most
"low-profile sealed keypad switch + floating cap" families, which
tend to be SMD-first (including Ultramec 6C and C&K's KSE mentioned
in an earlier revision of this section) — but not all of them.

**Revised requirement (user correction #3): actuator may shrink to
~4–5mm.** This changes the pick. With a 4–5mm target, a plain **sealed
through-hole tact switch no longer needs an oversized cap**, which
collapses the stack height from the Multimec 5G's ~12.5mm to ~5mm and
drops the cost — exactly the class of part the earlier revision wrongly
said did not exist (corrected below).

**Recommendation (new primary): a sealed IP67 through-hole 6×6mm tact
switch — Omron `B3W-AQ` series (e.g. `B3W-1002-AQ`).**
- **Sealed to IP67** (IEC 60529, sealing excludes the terminals) with a
  genuine sealed construction, **through-hole**, **6×6mm** body,
  mounting-compatible with the ubiquitous B3F series.
- **Flat plunger ~4.3mm** above the board (B3W-1002-AQ) — inside the new
  4–5mm target without any cap, ~5mm total stack vs the Multimec's
  ~12.5mm. A **projected-plunger** variant (B3W-4000 series, ~7.3mm) is
  available if a slightly taller/larger target is wanted.
- **230gf** operating force, bounce ≤5ms, **300,000 operations** min —
  adequate for a handlebar remote (lighter duty than the Multimec's 10M,
  but 300k presses is still years of use).
- **Widely sourced** at Farnell/element14, DigiKey, Mouser — a standard
  Omron catalog part, hand-solderable (through-hole), cheap.
  [Omron B3W-AQ datasheet](https://omronfs.omron.com/en_US/ecb/products/pdf/en-b3w-aq.pdf) ·
  [Omron B3W product page](https://components.omron.com/us-en/products/switches/B3W) ·
  [Farnell: B3W-1002 sealed tact switch](https://uk.farnell.com/omron/b3w1002/switch-sealed-flat-230gf/dp/1961037)

**Alternatives in the same class** (sealed IP67 THT 6×6mm tact, if the
Omron is out of stock or a different feel is wanted): **E-Switch
`TL6059`** (IP67, radial leads, two actuator heights) and **Multicomp
Pro `MPTSHW-67T-V` / `-69T-V`** (6×6mm IP67, round button, stocked at
Farnell).
[E-Switch TL6059 (IP67 THT tact)](https://www.e-switch.com/news-events/blog/new-product/tl6059-series-ip67-tht-tact-switch-radial-leads/) ·
[Farnell: MPTSHW-69T-V](https://uk.farnell.com/multicomp-pro/mptshw-69t-v/tactile-switch-0-05a-12vdc-th/dp/4153464)

**Note on the glove target**: 4–5mm is small for a thick glove. If field
testing shows it's too small, add a small snap-on cap (Omron offers caps
for the B3W/B3F family) to grow the touch target a few mm without
changing the switch or its sealing — a far smaller stack-up than the
Multimec + Ø9.6–12mm cap below.

---

**Fallback (larger glove target): APEM/MEC `Multimec 5G` series,
through-hole, with a round "floating cap" accessory.** Kept here only if
the 4–5mm sealed tact switch proves too small even with a cap, and a
Ø9.6–12mm actuator is really required (at the cost of the ~12.5mm stack).
- **Switch itself**: **through-hole**, IP67 sealed to IEC 60529 (PPS
  housing + actuator, **silicone rubber sealing**, stainless steel
  contact springs) — a genuinely sealed switch, not "sealed by an
  external boot." Base footprint **10mm x 10mm, 6.4mm body height**,
  rated **10 million actuations** on the general 5G platform (a
  specific through-hole/cap combination found at RS is speced to
  500,000 — check the exact ordering code chosen against its own
  datasheet page, cycle rating varies by variant).
  [Mouser: Multimec 5 series datasheet — 10M actuations, IP67 sealing](https://www.mouser.com/datasheet/2/26/Apem_08222017_Multimec_5G-1158362.pdf) ·
  [TTI: APEM Multimec 5G series datasheet](https://www.tti.com/content/dam/tti-commons/supplier/apem/doc/apem-multimec-5g-series-switches-datasheet-specifications.pdf)
- **What reaches the 7–10mm actuator target without an SMD floating
  cap**: the 5G platform takes the same "separately-molded oversized
  cap on a compact sealed switch" approach as the SMD Ultramec 6C
  considered earlier, just on a through-hole base — e.g. cap code
  **`1ES`, a round cap Ø9.6mm**, sitting right inside the requested
  7–10mm window, or the plain round polyamide cap seen in a stocked RS
  variant at **Ø12mm** (slightly larger) if a bigger target is
  preferred. Total stack height (switch body + this class of cap) is
  on the order of **~12.5mm** per the datasheet — noticeably more than
  the bare 2.5–6.4mm switch body, since a sealed, glove-sized cap
  necessarily adds some stack-up; treat this as the real constraint on
  enclosure thickness behind each button, not the switch body alone.
  [Enika: Multimec 5G through-hole dimensions incl. 1ES cap](https://www.enika.eu/data/files/3Fxx.pdf)
- **Widely available, in through-hole, at mainstream distributors**:
  confirmed stocked at **RS Components** (e.g. `5ETH935+1SS09-12.0`
  with the 12mm polyamide round cap, IP67, silicone-sealed, 2mm
  recommended panel thickness), with the wider Multimec 5G platform
  also documented via Mouser/Farnell/TTI datasheets — this is a
  standard, multi-sourced industrial switch family, not a boutique or
  single-distributor part.
  [RS: MEC IP67 round-button tactile switch, 12mm, through hole](https://uk.rs-online.com/web/p/tactile-switches/0431926)
- **Hand-solder compatibility**: through-hole pins, no reflow needed —
  matches the project's existing iron-solder approach better than the
  SMD Ultramec 6C did.
- **Bonus for this project's 4–5 button layout**: like the Ultramec 6C,
  caps come in multiple colors (the RS-listed variants above are
  black/grey/red/white combinations) — useful for marking the two
  camera buttons distinctly from the Garmin page-left/page-right/lap
  buttons without separate labeling.

**Correction to the earlier "no small sealed THT switch exists" claim**:
that reasoning only held for a **7–10mm** actuator, where sealing did
force a small-switch-plus-oversized-cap stack. Once the target drops to
**4–5mm** (correction #3), the requirement is met by a plain sealed IP67
6×6mm tact switch (Omron B3W-AQ and the alternatives above) at a ~5mm
stack — so the Multimec 5G's ~12.5mm is no longer "the practical
minimum", it is specifically the price of insisting on a Ø9.6–12mm
gloved-finger target.

**Open points**:
- Final button count (4 vs. 5) — tied to the still-open "Handling of
  the 2 camera buttons" question in the specs doc.
- Confirm the Omron `B3W-1002-AQ` actuator Ø and feel are acceptable
  with a riding glove (bench + field); decide whether a snap-on cap is
  needed, and only then fall back to the Multimec 5G if 4–5mm is truly
  too small. Get a firm per-unit price for the chosen switch for the BOM.
- The sealed-tact stack is now **~5mm** above the board (B3W flat
  plunger), not ~12.5mm — a much lower floor on the enclosure thickness
  behind each button (fold into the enclosure design, specs doc §4). The
  ~12.5mm figure only applies if the Multimec fallback is taken.
- Real-world IP65/IP67 validation once a prototype board + enclosure
  exist — with a PCB-mount switch, sealing at the enclosure depends on
  how the case meets the PCB around each button (a gasket, or the
  switch's own panel-thickness spec, at the enclosure's button
  openings).

---

## 6. Alternative: membrane keypad (ZX81-style)

**The comparison is apt.** A membrane keypad is exactly the technology
behind the ZX81's flat keyboard (and calculators, microwave panels,
medical-device front panels since): a thin flexible sandwich — usually
two printed-circuit polyester layers with a spacer between them, a
metal "snap dome" under each key position for tactile click, and a
printed graphic overlay on top — instead of discrete mechanical
switches. It is a legitimate alternative to the sealed-tact-switch
recommendation in §5 on exactly the axis this project cares about:
**compactness**.

- **Thickness**: a standard (non-waterproofed) membrane switch stack is
  on the order of **~0.7mm**; a waterproofed version (thicker overlay +
  edge-sealing adhesive) is on the order of **~1.4mm** — vs. **~5mm**
  for the sealed B3W-AQ tact switch now picked in §5 (or ~12.5mm for the
  Multimec + cap fallback). The thickness advantage is smaller against
  the 4–5mm sealed tact switch than it was against the Multimec, but a
  membrane is still the thinnest option and gives free icon printing.
  [LID/tactilemembrane: standard vs. waterproof membrane switch stack-up](https://tactilemembrane.com/waterproof-membrane-switch-harsh-environment-design/)
- **Sealing**: the membrane is bonded to the front panel with a
  continuous perimeter adhesive (e.g. 3M acrylic adhesive films
  specifically sold for this), so the "sealing surface" is the whole
  keypad sheet, not a per-button gasket — **IP65/IP67 is a normal,
  well-documented outcome** for this construction, not a stretch.
  [LID/tactilemembrane: IP67/NEMA 4X sealed membrane switch design](https://tactilemembrane.com/waterproof-membrane-switch-harsh-environment-design/)
- **Actuator size/shape is free**: because each "button" is just
  printed artwork plus a dome underneath, any glove-friendly target
  (the 4–5mm of §5, or larger) is trivial to hit exactly (any
  shape/spacing) —
  and the graphic layer can print icons/labels directly on each button
  (camera on/off, left/right arrows, lap) instead of relying on
  discrete colored caps.

**The catch: sourcing is a real trade-off, not a free upgrade.**

1. **A genuinely off-the-shelf, catalog-stocked option exists**: **NKK
   Switches' `FM` series**, e.g. part `FMBN04BE` — a **4-key**
   non-illuminated membrane keypad, snap-dome tactile feedback,
   adhesive-backed for panel mounting, 1,000,000+ cycle life, stocked
   at DigiKey/Mouser/RS/Farnell (not a custom order). This is the
   closest thing to "buy a membrane keypad the same way you'd buy a
   switch."
   [DigiKey: FM series membrane keypads](https://www.digikey.com/en/product-highlight/n/nkk-switches/fm-series-of-membrane-switch-keypads) ·
   [DigiKey: FMBN04BE](https://www.digikey.com/en/products/detail/nkk-switches/FMBN04BE/360-2295-ND/1830605) ·
   [NKK FM series datasheet (Mouser-hosted PDF)](http://www.mouser.com/pdfdocs/NKK_FM_Series_Membrane_Keypads.pdf)
   - **Only comes in 4-key or 16-key layouts** — no 5-key option. A
     4-key FM keypad is a clean fit *if* the project settles on 4
     buttons (i.e. the still-open "camera ON/OFF as one toggle button"
     question in the specs doc resolves that way); if 5 buttons are
     kept, this specific catalog part doesn't fit and the choice falls
     back to option 2 below.
   - **Listed as "Matrix Output"** despite only 5 pins for 4 keys —
     **not yet confirmed** whether that's a true scanned row/column
     matrix (which would conflict with the specs doc's firmware
     architecture: "5 touch buttons, individual GPIOs, no matrix,"
     wake via GPIO PORT event) or effectively "4 individual switches +
     1 shared common," which would be a drop-in fit for the existing
     GPIO/pull-up design. **Must check the actual pinout in NKK's
     datasheet before adopting this part** — this is the single
     biggest open question standing between this option and a firm
     recommendation.
   - **Termination is a cable with connector**, not bare solder pads —
     so unlike a discrete through-hole switch, this needs a mating
     connector footprint on the PCB (a small added BOM line), not a
     direct hand-solder joint. Still compact and still avoids SMD
     switch parts, just a different kind of PCB interface than §5's
     switches.
   - **Operating temperature -15°C to +50°C** (per search results,
     unconfirmed against the full datasheet) — worth double-checking
     against realistic cold-weather riding conditions as part of
     verifying this part.
2. **A fully custom membrane keypad**, made to this project's exact
   4-or-5-button layout/graphics by a specialty manufacturer (e.g.
   RSP, LID/tactilemembrane, Kingway USA, JRPanel, ALLPCB) — the "real"
   ZX81-style approach: any button count, shape, icon set, and IP
   rating, and cheap per unit at volume. But it is genuinely a
   **custom-manufactured part, not a catalog purchase**: typical MOQ
   for a standard custom design is **~50 units** (some vendors offer
   5–10-piece prototype/first-article runs), with **2–8 week**
   production lead time (a few vendors advertise 24–48h turnarounds
   specifically for early prototypes), plus one-time artwork/tooling
   setup. That's a real mismatch for "build one or two units to test,"
   though very attractive once the design is stable and a small
   production batch is being planned.
   [JRPanel: membrane switch prototype quoting](https://www.jrpanel.com/switch/buy/) ·
   [RSP: custom membrane switch manufacturing](https://www.rspinc.com/capabilities/membrane-switches/) ·
   [LID/tactilemembrane: custom membrane switches, 2-week prototype](https://tactilemembrane.com/custom-membrane-switches-and-keypads-precision-durability-fast-turnaround/)

**Recommendation**: use §5's sealed IP67 THT tact switch (Omron
`B3W-AQ`) for the **first prototype board** — a buy-today, solder-today
catalog part, no MOQ/lead-time risk, and now only ~5mm of stack (the
4–5mm actuator decision removed the Multimec's height penalty that
previously motivated the membrane). Treat the membrane keypad as the
**follow-up option once the design stabilizes**:
- If the button count settles at exactly 4, revisit the NKK `FM`
  series (`FMBN04BE`) as a possible catalog drop-in — pending the
  matrix-output pinout check above.
- If a small production run (tens to low hundreds of units) is ever
  planned, a fully custom membrane keypad is likely the better
  long-term choice on compactness, sealing, and per-unit cost alike —
  revisit once the enclosure and final button layout are locked.

**Open points**:
- Confirm `FMBN04BE`'s actual pinout (true matrix vs. common+4) against
  NKK's full datasheet before treating it as firmware-compatible.
- Resolve the 4-vs-5-button question (specs doc open point) — it
  gates whether the off-the-shelf NKK 4-key part is even usable.
- If pursuing a custom membrane keypad later, get concrete MOQ/lead
  time/price quotes from 2–3 of the vendors above once the layout is
  final.

---

## 7. Temperature sensor — does idle operation make it read true ambient?

**Question**: since the device is idle most of the time, and the
nRF52832/nRF52840 both have a built-in die-temperature sensor, should
that reading effectively track outside/ambient temperature?

**Partial confirmation — idle operation solves the *electronic*
self-heating problem, but that's not the only error source.**

- **What idle operation actually fixes**: yes, being idle almost all
  the time removes the dominant *electronic* self-heating concern.
  Nordic's own DevZone guidance on this exact question notes the
  nRF52832 is "an ultra low power device and produces very little
  heat, so the die temperature is approximately the same as the
  ambient temperature," citing a measured **~2°C rise on the older
  nRF51 under close-to-full activity**, with the nRF52 expected to run
  cooler still for the same workload thanks to its better power
  efficiency. This project's duty cycle — brief BLE/ANT+ bursts on a
  button press, then long idle stretches, with temperature sampling
  only every 5 minutes during an already-bounded broadcast session
  (see specs doc "Thermometer") — is far below "close to full
  activity," so self-heating from the radio/CPU itself should be a
  minor contributor, consistent with what the specs doc already
  states.
  [Nordic DevZone: nRF52832 temperature sensor accuracy](https://devzone.nordicsemi.com/f/nordic-q-a/21755/nrf52832-temperature-sensor-accuracy)
- **Minor discrepancy to resolve**: that DevZone thread cites **±5°C**
  stock accuracy, while the existing specs doc's "Internal temperature
  sensor" section cites **±4°C** "per Nordic's datasheet" — worth
  reconciling against the actual nRF52832 datasheet's `TEMP` electrical
  characteristics table (the two figures may reflect different
  conditions, e.g. calibrated vs. raw, or a different temperature
  range) before quoting a single number with confidence.
- **What idle operation does *not* fix: the sensor still reads the
  chip/PCB/enclosure temperature, not free air — and this is where
  most of the real-world error comes from in comparable devices.**
  This is a well-documented issue on Garmin Edge and Wahoo ELEMNT units
  (both of which read temperature the same architectural way: an
  internal sensor on the board, not an external probe), independent of
  how "busy" the unit's electronics are:
  - Garmin Edge forum reports describe units in direct sun reading
    **5–10°F (~3–6°C) higher** than an identical unit in the shade,
    and up to **10–15°F (~5.5–8°C) higher** in full sun with low wind.
  - Wahoo's own support documentation states the temperature sensor
    "is placed where it best reads the temperature of the internal
    electronics, rather than the ambient external air temperature,"
    and that Wahoo considers **±10°F (~5.5°C)** to be within spec —
    and explicitly calls out direct sunlight, holding the unit in hand,
    or charging from an external battery as things that push the
    reading above true ambient.
  - Both sources agree the reading is "mostly accurate... at high
    airspeed and under overcast conditions" — i.e. exactly the
    opposite of a stationary, sunny handlebar mount.
  - **Directly answering "so shade would be fine?"**: yes — the same
    reports describe both Garmin and Wahoo units as accurate to
    **within ~1°F (~0.5°C) in the shade**, vs. the 5–15°F (~3–8°C)
    over-read cited above once the unit sits in direct sun. Shade
    removes essentially all of the *dominant* error source for this
    class of device; what's left is closer to the sensor's own stock
    tolerance (±4–5°C, still unresolved above) rather than the much
    larger solar-gain effect.
  [Wahoo: Data Accuracy — temperature sensor placement and tolerance](https://support.wahoofitness.com/hc/en-us/articles/20547628744210-Data-Accuracy-ELEMNT) ·
  [Garmin Forums: Edge 1030 Plus temperature reading discussion](https://forums.garmin.com/sports-fitness/cycling/f/edge-1030-plus/289861/temperature-reading-edge-1030-plus) ·
  [Bike Forums: the Garmin Edge thermometer](https://www.bikeforums.net/road-cycling/791570-garmin-edge-thermometer.html)

**Net answer**: idle operation is genuinely the right reasoning for
*why self-heating from the chip itself won't be a big factor* here —
that part is confirmed, and it's a meaningfully stronger position than
"any die-temperature sensor is unreliable," since this project's duty
cycle is much lighter than the "close to full activity" case Nordic
measured ~2°C for. But it doesn't make the reading equivalent to true
outside air temperature: the sensor still measures a small
sun-exposed plastic enclosure sitting still on a handlebar, and that
enclosure's own thermal behavior (solar gain, low airflow when
stationary, proximity to a warm hand/body) is — per real-world data
from architecturally identical commercial devices — good for several
°C of error on its own, on top of the sensor's own ±4–5°C stock
tolerance. This matches, and now quantifies with real precedent, the
specs doc's existing framing: **fine for an indicative "feels like"
reading on the Edge, not for precision measurement** — no change to
that conclusion, just firmer evidence behind it.

**Open points**:
- Reconcile the ±4°C vs. ±5°C accuracy figures against the nRF52832
  datasheet directly.
- Check `ant_garmin.c`'s actual session timing: is the *first*
  temperature sample of a session taken immediately when the
  triggering button press starts the session, or only after the first
  full `TEMP_BROADCAST_INTERVAL_MIN` has elapsed? The former would sit
  closer in time to that button's own BLE/ANT+ activity (a much
  smaller effect than the enclosure/solar one above, but easy to avoid
  if it's cheap to delay the first sample slightly).
- Once real hardware exists, field-test the "feels like" framing
  directly: compare the device's reading against a known-good ambient
  reference in both shaded and direct-sun handlebar mounting, similar
  to the Garmin/Wahoo reports cited above.

---

## 8. User-feedback LEDs on an unregulated, decaying CR2032

**Question**: one or two indicator LEDs (red/green) would help the user
(action confirmed, failure, low battery). How do we drive them when the
supply is the bare coin cell, whose voltage drifts from ~3.0V (fresh)
down to ~2.0V (end of life) and sags further under load?

All figures below were checked against the datasheets linked in §8.8
(values read from datasheet *graphs* are marked "≈ graph").

### 8.1 The cell voltage is flatter than it sounds

The Energizer CR2032 datasheet shows the Li/MnO₂ discharge curve is a
**plateau around 2.9V (background load) / ~2.8V (6.8mA pulse load)**
for roughly the first 75–80% of capacity, then drops steeply towards
the 2.0V cut-off (235mAh rated to 2.0V). The same datasheet's IR chart
gives an internal resistance of **≈ 10Ω fresh, ≈ 20–25Ω at ~175mAh,
then rising past 100Ω** in the last ~30mAh (≈ graph).

**Consequence**: a plain series resistor sized for ~2.8V gives
essentially constant brightness for most of the cell's life. The
"varying voltage" problem is real only near end of life — where a
dimming LED coincides with the low-battery warning anyway.

### 8.2 Colour choice: forward voltage

| Part (Kingbright, 0805) | Colour / material | Vf typ @20mA | Vf at 5–10mA (≈ graph) | Iv typ @20mA | Usable on CR2032? |
|---|---|---|---|---|---|
| APT2012SURCK | Hyper red 630nm, AlGaInP | 1.95V | ≈ 1.8–1.85V | 230 mcd | **Yes** |
| APT2012CGCK | Green 570nm, AlGaInP | 2.1V | ≈ 1.95–2.0V | 50 mcd | **Yes**, fades below ~2.2V |
| APT2012SGC | Green 568nm, GaP | 2.2V | ≈ 2.0V | 12 mcd | Yes, but very dim |
| APT2012ZGCK | Green 525nm, InGaN | **3.3V** | — | 550 mcd | **No** — Vf above the cell voltage |

- "Green" must be a **yellow-green AlGaInP (≈570nm)** die, not InGaN:
  the InGaN part needs 3.3V typ, more than a CR2032 ever delivers.
  Many red/green bicolour LEDs use InGaN for the green — check Vf.
- Even the best usable green (CGCK, 50 mcd) is **~4.6× less luminous
  than the red** (230 mcd) at the same current (mcd already includes the
  eye's sensitivity). The green needs more current than the red to look
  comparable.
- Luminous intensity is **linear with current** on both parts
  (≈ graph), and temperature-dependent: ≈ ×1.5 at −20°C, ≈ ×0.7 at
  +60°C relative to 25°C (≈ graph). Vf has a −1.9mV/°C tempco, so
  **+≈ 85mV at −20°C** — slightly less headroom in winter.

### 8.3 How much current? (≈ 1–3mA is too little)

An earlier draft of this section targeted 1–3mA. That is **too low for
outdoor use**: from the datasheets' linear Iv(I), 2mA gives ≈ 23 mcd
(red) and ≈ 5 mcd (green). That is fine indoors but marginal for a
handlebar in daylight, especially the green. These parts are specified
at 20mA.

What limits the current is not energy (a 10mA × 100ms flash costs
≈ 0.28µAh — 1000 flashes ≈ 0.12% of 235mAh) but:

1. **GPIO drive** (nRF52832 PS v1.9 §20.4 *Electrical specification*,
   p. 158, high drive):
   - VDD ≥ 2.7V: **IOL,HDH 6 / 10 / 15mA** (min/typ/max) at
     VSS+0.4V; IOH,HDH 6 / 9 / 14mA at VDD−0.4V.
   - VDD ≥ 1.7V: only **3mA guaranteed** (IOL,HDL / IOH,HDL) at 0.4V
     drop.
   - Standard drive: 1 / 2 / 4mA — unusable here.
   - Nordic (DevZone, Nordic staff) recommends **≤ 15mA total
     sink/source for the whole chip**, which may all go to one pin, and
     recommends **sinking** rather than sourcing (more GND pads than VDD
     pads).
   → An effective pin resistance of ≈ 40Ω typ / ≈ 67Ω worst case
   (0.4V / 10mA, 0.4V / 6mA) at VDD ≥ 2.7V is a reasonable
   back-of-envelope model.
2. **Cell sag**: 10mA × 10–25Ω = 0.1–0.25V for most of the life. On a
   worn cell (IR > 100Ω) it reaches > 1V.

**Recommended target: one LED at a time, peak ≈ 10mA for the green
and ≈ 5mA for the red** (≈ 25 mcd green, ≈ 58 mcd red — closer in
perceived brightness than equal currents). This stays inside the
guaranteed high-drive spec and the 15mA chip budget. Going beyond
(15–20mA) means an external logic-level N-MOSFET with Rds(on)
specified at Vgs ≤ 1.8V, to be justified only if the outdoor test
(§8.7) shows 10mA isn't enough.

### 8.4 Sizing (direct GPIO drive, LED sinking into the pin)

`R = (V_design − Vf) / I − R_pin`, V_design = 2.8V (plateau under
load), R_pin ≈ 40Ω typ:

| LED | Target I | Vf | R_total | R (E12) | I worst-case pin (67Ω) | I at VDD = 2.4V | I at −20°C |
|---|---|---|---|---|---|---|---|
| Green CGCK | 10mA | 2.0V | 80Ω | **39Ω** | ≈ 7.5mA | ≈ 5mA | ≈ 9mA |
| Red SURCK | 5mA | 1.8V | 200Ω | **150Ω** (+ R_pin ≈ 40–50Ω at 5mA) | ≈ 4.6mA | ≈ 3.2mA | ≈ 4.6mA |

(Derived values — first-order model, to be checked on the bench.) A
fresh, unloaded cell (~3.0–3.2V) pushes these up by ~20–25%, which is
harmless (parts rated 30mA DC).

Below ~2.4V the LEDs fade to about half brightness, and the green goes
out around 2.1V, which is when the cell is nearly empty anyway (§8.1).

**PWM is optional, not required**: since the plateau already gives
near-constant brightness, PWM is useful mainly to
(a) **retune brightness in firmware** after the outdoor test, by sizing
R for the maximum wanted current and dimming down, and (b) balance red
vs. green by eye. Compensating VDD from the SAADC reading is possible
but buys little before end of life.

### 8.5 Coin-cell interactions

- **The bulk capacitor does not carry a flash**: ΔV = I·t/C =
  10mA × 100ms / 100µF = 10V, so the cell supplies the LED itself. The
  cap only smooths µs–ms radio bursts (or a PWM ripple at ≥ 1kHz,
  where the cell sees roughly the average current).
- **Don't overlap the flash with radio TX/RX**: nRF52832 TX at 0dBm
  draws **11.6mA (LDO) / 5.3mA (DC/DC at 3V)**, RX 11.7 / 5.4mA
  (PS v1.9 RADIO electrical specification, p. 234). LED + radio
  ≈ 15–22mA would sag a worn cell (IR > 100Ω) below 1.7V, the
  minimum supply voltage (VDD 1.7–3.6V). The brown-out reset is at
  **1.5–1.7V** (VBOR,ON, PS v1.9 p. 103). Flash *after* the BLE/ANT+ exchange, which is also the right UX
  (report the result). Whether the chosen module (§9) has the DC/DC
  (inductor fitted) is to be checked.
- **Measure VDD with the LED off**, or the sag skews the battery-level
  reading.
- Keep flashes short (50–150ms) and use a blink pattern for "session
  active" rather than a steady LED.

### 8.6 Hardware form factor and pins

- **Two discrete 0805 LEDs** (APT2012SURCK + APT2012CGCK), each with its
  own resistor, **common anode to VDD, cathode → R → GPIO (sink)**: this
  matches Nordic's sink recommendation, and the LED stays off if the
  pin floats or is in reset. 0805 is hand-solderable (§1).
- **2-lead anti-parallel bicolour is ruled out at these currents**: the
  current goes through two GPIO pins in series (2 × ≈ 0.4V at 10mA),
  so the green would need ≈ 2.0 + 0.8 = 2.8V, the entire plateau
  voltage. E.g. Kingbright L-59EGW (5mm, GaAsP red 617nm + GaP green
  568nm, Vf 2.0 / 2.2V typ, 60 mcd @20mA, per distributor listing) only
  works at a few mA. A **3-lead common-anode** bicolour would be fine
  (one GPIO per die, same as discrete) if a single light pipe is wanted,
  but its green must be AlGaInP/GaP (verify Vf).
- **Pins: avoid P0.22–P0.31.** PS v1.9 §4.3.1 *GPIO located near the
  radio*, Table 5 (QFN48), p. 18 restricts them to "low drive, low frequency I/O only",
  which is incompatible with high-drive LED pins. Note that P0.25–P0.31
  are also the analog inputs. Also avoid P0.09/P0.10, which are NFC by
  default (the UICR must be changed to use them as GPIO). Pick from
  P0.02–P0.08 / P0.11–P0.20, cross-checked against the chosen module's pad map (§9).
- **Zephyr**: plain GPIO → `NRF_GPIO_DRIVE_H0H1` (or `H0S1` for
  sink-only) in the devicetree `gpios` flags; via PWM/pinctrl →
  `nordic,drive-mode = <NRF_DRIVE_H0H1>`.
- **Daylight**: a recessed window or light pipe through the sealed
  enclosure; choose together with the enclosure design.

### 8.7 Net answer

Red (AlGaInP 630nm) + **yellow-green AlGaInP 570nm** (not InGaN), each
with a **series resistor sized for the ~2.8V plateau**, sunk by a
**high-drive GPIO**, at **≈ 10mA green / ≈ 5mA red** peak, one at a
time, in short flashes after the radio exchange. No regulator, no
boost, no extra IC. The CR2032's flat discharge curve keeps brightness
nearly constant for most of its life; PWM is kept only as a firmware
brightness knob.

**Open points**:
- Bench-check the actual currents (R_pin model) and an **outdoor
  daylight test** through the enclosure window; move to a MOSFET switch
  only if 10mA isn't visible enough.
- Confirm whether the chosen module (§9) has the DC/DC inductor fitted
  (radio current 5 vs. 11mA on the nRF52832). If an nRF52840 module is
  chosen instead, redo the GPIO figures with the nRF52840 PS.
- Choose GPIOs on the chosen module's footprint (§9), outside the
  radio-restricted pins; add LEDs and
  resistors to the KiCad schematic (`hardware/board`).
- Define the blink vocabulary in the specs doc (e.g. green = OK,
  red = failure/no link, red double = low battery). This closes the
  specs doc's "User feedback (LED/buzzer)" open point on the LED side.

### 8.8 References

- Nordic nRF52832 Product Specification **v1.9**, local copy
  [`doc/data/nRF52832_PS_v1.9.pdf`](data/nRF52832_PS_v1.9.pdf) — §4.3.1
  *GPIO located near the radio* (Table 5, p. 18), §20.4 GPIO
  *Electrical specification* (p. 158), POWER VBOR (p. 103), RADIO
  current consumption (p. 234) ·
  [online version](https://docs.nordicsemi.com/bundle/ps_nrf52832/page/gpio.html).
  The same GPIO / radio / VBOR values appear unchanged in PS v1.0.
- Nordic DevZone, staff answers on GPIO current:
  [max GPIO current for nRF52 (15mA total, sink preferred)](https://devzone.nordicsemi.com/f/nordic-q-a/12614/max-gpio-current-for-nrf52) ·
  [GPIO sink current on nRF52832 (15mA max = at 0.4V drop, 10mA typ)](https://devzone.nordicsemi.com/f/nordic-q-a/15800/gpio-sink-current-on-nrf52832)
- [Energizer CR2032 datasheet](https://data.energizer.com/pdfs/cr2032.pdf) —
  discharge curves, pulse test (400Ω, ≈ 6.8mA @2.7V), IR vs. capacity
- Kingbright datasheets:
  [APT2012SURCK (hyper red AlGaInP)](https://www.kingbrightusa.com/images/catalog/SPEC/APT2012SURCK.pdf) ·
  [APT2012CGCK (green AlGaInP 570nm)](https://www.kingbrightusa.com/images/catalog/SPEC/APT2012CGCK.pdf) ·
  [APT2012SGC (green GaP)](https://www.kingbrightusa.com/images/catalog/SPEC/APT2012SGC.pdf) ·
  [APT2012ZGCK (green InGaN, counter-example)](https://www.kingbrightusa.com/images/catalog/SPEC/APT2012ZGCK.pdf) ·
  [L-59EGW bicolour (element14 listing)](https://nz.element14.com/kingbright/l-59egw/led-red-green-617-568nm-5mm/dp/2001770)
- Zephyr nRF drive-mode macros:
  [`nordic-nrf-gpio.h`](https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/dt-bindings/gpio/nordic-nrf-gpio.h) ·
  [`nrf-pinctrl.h`](https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/dt-bindings/pinctrl/nrf-pinctrl.h)

---

## 9. Module choice — the E73-2G4M08S1E is an nRF52833

**Finding**: the module this project was built around, the **Ebyte
E73-2G4M08S1E**, is **not an nRF52832**. Its own user manual (v1.9,
2026-03, local copy
[`doc/data/E73-2G4M08S1E_UserManual_EN_v1.9_fr.pdf`](data/E73-2G4M08S1E_UserManual_EN_v1.9_fr.pdf))
states "nRF52833 … nRF52833-QIAA / aQFN73" (pp. 2 and 4). Its E73 series
table (p. 11) lists the nRF52832 variant as a different part, the
**E73-2G4M04S1B**. The 13 × 18mm, 43-pad description in §1 and in the
specs doc matches the S1E. The "nRF52832" label attached to it is what
was wrong.

**Why it matters**: the ANT add-on for nRF Connect SDK lists only the
**nRF52832 and nRF52840** in the nRF52 series (sdk-ant v2.0.0 / v2.1.x,
[compatibility table](https://ant-nrfconnect.github.io/doc/compatibility.html)).
The **nRF52833 is not listed**. It might work, but nothing confirms it,
and ANT+ is a core requirement. **Decision: the nRF52833 / E73-2G4M08S1E
is dropped (too risky).**

### 9.1 Hard requirement found on the way: a 32.768kHz crystal

The nRF52832 PS v1.9 (§19.4.3–19.4.4, p. 112) gives:

| Parameter | Value |
|---|---|
| LFRC (internal RC) tolerance after calibration, `fTOL_CAL_LFRC` | ±250 ppm |
| LFXO tolerance required by the **BLE** stack, `fTOL_LFXO_BLE` | ±250 ppm |
| LFXO tolerance required by the **ANT** stack, `fTOL_LFXO_ANT` | **±50 ppm** |

→ BLE alone can run on the calibrated RC oscillator, **ANT cannot**: it
needs a 32.768kHz crystal (LFXO). A DevZone answer about ANT on the
LFRC sums it up as "may work in laboratory environments, but will
probably fail in the field"
([DevZone: using ANT with LFRC](https://devzone.nordicsemi.com/f/nordic-q-a/43403/using-ant-with-lfrc)).
**Any candidate module must therefore have the 32.768kHz crystal fitted,
or at least expose XL1/XL2 so one can be added on our board.**

### 9.2 Candidates

An external 32.768kHz crystal is acceptable. It costs one crystal
(±20–50 ppm) and two capacitors on our board. The module only has to
expose XL1/XL2 (P0.00/P0.01). Most third-party modules work this way.

| Module | SoC | ANT (sdk-ant) | Size (mm) | 32.768kHz crystal | Pads / hand-solder | DC/DC inductor | Factory firmware | Verdict |
|---|---|---|---|---|---|---|---|---|
| Ebyte **E73-2G4M08S1E** | nRF52833 | **not listed** | 13 × 18 | not on module, XL1/XL2 exposed | castellated, 1.27mm | currents quoted "DC/DC, 3V" | none | **dropped** |
| Ebyte **E104-BT5032A** | nRF52832 | listed | **11.6 × 16** | **no, and XL1/XL2 not exposed** | castellated, 1.27mm, 24 pads | probably not (TX 13 typ / 19mA max ≈ LDO-mode figures) | **AT firmware, read-protected** | **ruled out** |
| Ebyte **E73-2G4M04S1B** | nRF52832 | listed | 17.5 × 28.7 | **built in** | castellated | to check | none | fallback (big) |
| Ebyte **E73-2G4M08S1C** | nRF52840 | listed | 13 × 18 | **built in** | to check | to check | none | candidate |
| Fanstel **BT832** | nRF52832 | listed | 14 × 16 × 1.9 | external, **XL1/XL2 on castellated pins 3/4** | **16 castellated pins (iron OK)** + 24 LGA pads underneath (optional) | **inductors inside the module** | none stated | **preferred** |
| Raytac **MDBT42Q** (-P = PCB antenna) | nRF52832 | listed | **10 × 16 × 2.2** | external, **XL1/XL2 on pads 13/14** | **edge pads extend past the outline → iron OK**. Only P0.23/P0.24 sit underneath (can stay unused) | DC/DC is the default mode. The ref. circuit shows L2/L3 → location to check | none stated (an "-AT" variant exists separately) | alternative (hand assembly) |

Sources:
- E73-2G4M08S1E manual (local copy above).
- E104-BT5032A manual v1.5 ([PDF](https://www.rcscomponents.kiev.ua/datasheets/e104-bt5032a+usermanual_en_v1_5.pdf), §3–4).
- Ebyte product pages:
  [E104-BT5032A](https://www.cdebyte.com/products/E104-BT5032A),
  [E73-2G4M04S1B](https://www.cdebyte.com/products/E73-2G4M04S1B)
  ("Built in 32.768K real-time clock crystal oscillator"),
  [E73-2G4M08S1C](https://www.cdebyte.com/products/E73-2G4M08S1C)
  ("nRF52840 … 13.0*18.0mm … With 32.768K real-time clock"). The S1B
  size comes from the series table in the S1E manual (p. 11).
- Raytac datasheet:
  [MDBT42Q](https://www.espruino.com/datasheets/MDBT42Q-E.pdf) (§2.1
  pinout, §2.2 recommended pads, §8 reference circuit: "Module's default
  is using DC-DC mode, and must connect it to external 32.768khz"). It
  asks for a **3.8mm no-ground zone** under the antenna end.
- Fanstel BT832 datasheet v2.04
  ([PDF](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/40/BT832_A_F_AF_V2.04_Dec-2017.pdf)): "16 castellated pins … SMT equipment is not required
  for soldering castellated pins", pin table p. 11, "Two inductors
  required for DCDC converter are inside BT832 module" and "we suggest
  adding a 32.768 kHz crystal and 2 capacitors" (p. 18). The antenna
  area should overhang the host PCB / ground plane by 6mm (p. 12).

**E104-BT5032A, details**: attractive on size and chip, but:
- **No 32.768kHz crystal.** A DevZone user says so
  ([thread 74531](https://devzone.nordicsemi.com/f/nordic-q-a/74531/trying-to-flash-e104-bt5032a)),
  and it is consistent with the pinout: P0.00/P0.01 (XL1/XL2) are not
  among the 24 pads (manual §4). One cannot be added, which **rules it
  out for ANT** (§9.1).
- Ships with Ebyte's serial-to-BLE AT firmware and is read-protected.
  It needs `nrfjprog --recover` over SWD (pads 23/24). Two DevZone users
  report trouble
  ([74531](https://devzone.nordicsemi.com/f/nordic-q-a/74531/trying-to-flash-e104-bt5032a),
  [82036](https://devzone.nordicsemi.com/f/nordic-q-a/82036/trying-to-do-a-flash-of-e104-bt5032a)),
  probably a SoftDevice configured for an absent LFXO.
- Only 8 freely named GPIOs (P0.02–P0.07, P0.25, P0.26). The 9 AT
  function pads (MOD, WKP, DISC, LINK, DATA, RXD, TXD, RTS, CTS) have no
  documented nRF pin mapping.

**Fanstel BT832 (nRF52832)**:
- **Watch the variant suffix — only the plain `BT832` is an nRF52832.**
  The family shares a footprint but not the SoC: `BT832` = **nRF52832**
  (512KB/64KB, Cortex-M4F with FPU, NFC); `BT832A` and `BT832AF` =
  **nRF52810** (192KB/24KB, M4 without FPU) — *avoid*: wrong chip, too
  little flash/RAM for the ANT+BLE SoftDevice, and the nRF52810 is **not
  in the sdk-ant compatibility list** (only 52832/52840). `BT832F` is
  also an nRF52832 but a larger long-range (FEM) variant, unnecessary
  here. The JLCPCB part `C7092400` (§9.5) is the correct plain `BT832`.
  [Mouser: BT832A/AF are nRF52810](https://www.mouser.ie/new/fanstel/fanstel-bt832a-bt832af-modules) ·
  [DigiKey: BT832F (nRF52832)](https://www.digikey.com/en/product-highlight/f/fanstel/bt832f-long-range-bluetooth-5-module)
- Same chip as the firmware, the specs doc and §8 assume.
- 14 × 16mm, smaller than the E73.
- **DC/DC inductors confirmed inside the module** (open point §8.5
  closed for this module).
- **The 16 castellated pins alone cover our needs**:
  - power and debug: VDD, GND, SWDIO/SWDCLK, P0.21/RESET;
  - crystal: XL1/XL2 (P0.00/P0.01);
  - LEDs: two of P0.02, P0.03, P0.13, P0.18, P0.20, which are high
    drive and outside the radio-restricted range;
  - buttons: P0.26, P0.27 (radio-near, fine for slow inputs) plus the
    remaining three of P0.02/P0.03/P0.13/P0.18/P0.20;
  - spare: P0.09/P0.10, once switched from NFC in the UICR.
  The 24 LGA pads underneath can stay unconnected, so the module can be
  soldered with an iron.
- **In the JLCPCB parts library** (§9.5).
- Needs the external 32.768kHz crystal, and its PCB antenna should
  overhang the board edge by 6mm, which constrains the enclosure layout.

**Raytac MDBT42Q (nRF52832)**:
- It is the chip the firmware, the specs doc and §8 were written for,
  so the §8 GPIO figures stay valid.
- It is the **smallest hand-solderable option** (160mm², vs 234mm² for
  the E73 13 × 18).
- All 32 GPIOs are brought out. On the left edge: P0.25–P0.31, DEC4,
  DCC, VDD. On the right edge: P0.11–P0.22, SWDIO/SWDCLK and
  P0.21/RESET. On the bottom edge: XL1/XL2, P0.02–P0.10.
- Raytac is a Nordic module partner and the module is sold through the
  usual distributors (Mouser datasheet listing, Tindie).
- It needs the 32.768kHz crystal on our board.

**E73-2G4M04S1B (nRF52832)**: built-in LFXO and no factory firmware,
but **~2.1× the area** of the S1E (17.5 × 28.7 vs 13 × 18mm), and a PCB
antenna with its keep-out. Only worth it if the MDBT42Q is unavailable.

**E73-2G4M08S1C (nRF52840)**: ANT-supported, and the **same chip as the
nRF52840 Dongle** used for BLE bring-up. The firmware already plans to
support both nRF52832 and nRF52840 (specs doc, "Decision: support both
nRF52832 and nRF52840"). The S1C has a built-in LFXO but its pads are
not yet checked. Choosing it means redoing §8 against the nRF52840 PS.
(The Raytac MDBT50Q nRF52840 module, previously listed here, is dropped:
it needs hot-air soldering and is not in the JLCPCB library — see §9.5.)

### 9.3 Recommendation

**Decision (updated) — three modules kept, in this order:**

1. **Fanstel BT832 (nRF52832) + 32.768kHz crystal on our board** —
   primary. Works for both hand soldering (castellated pins only) and
   JLCPCB assembly (§9.5, Economic PCBA, no X-ray).
2. **Ebyte E73-2G4M08S1E (nRF52833)** — second choice, **kept because we
   already have this module in hand** (useful for immediate BLE bring-up
   and, importantly, to *test whether ANT actually runs on the
   nRF52833*). This partially reverses the "dropped" verdict at the top
   of §9: the risk is unchanged — **the nRF52833 is still not in the
   sdk-ant compatibility list (§9)**, so ANT support stays *unverified*;
   it is a second choice precisely because that risk is only acceptable
   on hardware already owned, not for a new build. If the ANT test fails,
   fall back to option 1 or 3.
3. **Ebyte E73-2G4M04S1B (nRF52832)** — third choice: ANT-supported and
   with a built-in LFXO (no external crystal needed), but ~2.1× the area
   of the others (17.5 × 28.7mm).

Previously-listed alternatives now set aside (nRF52840 path, or
non-JLCPCB): Raytac MDBT42Q, E73-2G4M08S1C. The Raytac MDBT50Q and the
E104-BT5032A are ruled out.

Before freezing the PCB:
1. **Footprint** from the BT832 datasheet (castellated pins extended
   outward for iron soldering, like the current E73 template; LGA pads
   either omitted or left unconnected). The antenna overhangs the board
   edge by 6mm.
2. **32.768kHz crystal**: ±20 ppm class, CL compatible with the nRF52832
   LFXO (`CL_LFXO` 12.5pF max, ESR ≤ 100kΩ — PS v1.9 §19.4.4), plus its
   two load capacitors, placed close to castellated pins 3/4. Firmware:
   `CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL=y` and the matching
   `..._K32SRC_*PPM` option.
3. **DC/DC**: inductors are inside the BT832; enable the DC/DC in
   firmware (radio current 5.3 vs 11.6mA TX 0dBm, nRF52832 PS).
4. **Pin plan** (castellated pins only): LEDs on two of P0.02 / P0.03
   / P0.13 / P0.18 / P0.20; buttons on P0.26, P0.27 and the remaining
   three. Consistent with §8.6.
5. Bring-up: flash an sdk-ant BLE + ANT sample (e.g. the HRM relay
   sample) on a BT832 board (e.g. Fanstel's EV-BT832) with the LFXO
   enabled.

**Knock-on edits once the module is confirmed** (not done yet):
`gopro_garmin_remote_specs.md` (MCU section, BOM, ANT section),
`software/gopro_remote_fw/README.md`, the comment in
`software/gopro_remote_fw/src/main.c`, `hardware/board/README.md` and
the KiCad symbol/footprint. All of them currently say
"E73-2G4M08S1E (nRF52832)".

### 9.4 Other manufacturers (not yet checked)

Nordic keeps an official list of nRF52832 modules
([nRF52832 modules](https://www.nordicsemi.com/Products/nRF52832/Modules?lang=en)),
with about 30 vendors. The table below is a shortlist filtered for size
≤ ~16 × 16mm, enough GPIOs (≥ 7 needed: 5 buttons + 2 LEDs) and an
"open CPU" listing. Sizes and GPIO counts come from that list. Only the
crystal column was checked, and only where a source is given.

| Vendor / module | Size (mm) | GPIO | Notes |
|---|---|---|---|
| Raytac MDBT42 (no "Q") | 8.8 × 13.8 | 32 | Smaller sibling of the MDBT42Q, pads not checked |
| Fanstel BM832 | 10.2 × 15 | 32 | Smaller Fanstel sibling of the BT832 (§9.2), pads not checked |
| Ezurio (ex-Laird) BL652-SA | 10 × 14 | 32 | External 32.768kHz crystal optional ([Ezurio FAQ](https://www.ezurio.com/support/faqs/does-bl652-use-different-clock-source-internal-rc-oscillator)). Industrial-grade, pricier |
| u-blox BMD-300 / BMD-350, ANNA-B112 | 14 × 9.8 / 8.65 × 6.4 / 6.5 × 6.5 | 32 / 32 / 25 | **No LF crystal on module** ([u-blox AppNote UBX-20009617](https://www.u-blox.com/sites/default/files/LFCLK-source-in-mbed-openCPU-nRF5_AppNote_%28UBX-20009617%29.pdf)). Small parts likely LGA (not iron-friendly) |
| Dynastream D52MD2M8IA | 14 × 9.8 | 24 | Dynastream is Garmin's ANT subsidiary, so an ANT-oriented module. Availability not checked |
| MinewSemi MS50SFA | 15.8 × 12 | 13 | Not checked |
| Holyiot 17095 | 9.4 × 9.25 | 11 | Cheap (AliExpress), not checked |

Ruled out from the same list: AT-command-only parts (e.g. Würth
Proteus-I/II, u-blox NINA-B11x "u-connect" variants), and LGA/SiP parts
too small to hand-solder (Insight SiP ISP1507, KAGA FEI ES2832,
WISOL BTS100A…).

### 9.5 JLCPCB assembly (PCB + SMT)

JLCPCB can assemble the board if the module is in its parts library
(LCSC stock). With that, the hand-solder constraint (§1) only matters
for prototypes, and the passives (crystal, capacitors, LED resistors)
can be placed by JLCPCB too. Library status checked on the JLCPCB part
pages. Stock and price change daily, so check them when ordering.

| Module | SoC | JLCPCB part | Library | PCBA type | Notes |
|---|---|---|---|---|---|
| **Fanstel BT832** | nRF52832 | [C7092400](https://jlcpcb.com/partdetail/Fanstel-BT832/C7092400) | Extended | **Economic + Standard** | EasyEDA symbol/footprint available |
| Ebyte E73-2G4M08S1C | nRF52840 | [C356849](https://jlcpcb.com/partdetail/Chengdu_Ebyte_ElecTech-E73_2G4M08S1C/C356849) | Extended | **Standard only, X-ray required** | Built-in LFXO |
| Ebyte E73-2G4M04S1B | nRF52832 | [C411306](https://jlcpcb.com/partdetail/Chengdu_Ebyte_ElecTech-E73_2G4M04S1B/C411306) | — | Economic + Standard | 28.7 × 17.5mm, built-in LFXO |
"Extended" parts carry a per-part loading fee at JLCPCB (Basic parts
don't). One module plus a few passives keeps that small.

**Raytac in the JLCPCB/LCSC library (checked 2026-10)** — asked whether a
Raytac nRF52832 or nRF52840 module could be assembled by JLCPCB:

| Raytac module | SoC | In JLCPCB library? |
|---|---|---|
| MDBT42Q | nRF52832 | **No** — not in LCSC/JLCPCB (distributors only: TME, Mouser) |
| MDBT50Q-1MV2 | nRF52840 | **No** — not in LCSC/JLCPCB (TME/DigiKey/Ozdisan only) |
| MDBT50Q-512K | nRF52833 | Yes ([C6904155](https://jlcpcb.com/partdetail/RAYTAC-MDBT50Q512K/C6904155)) — but **nRF52833, not ANT-usable**, so irrelevant |

**Conclusion**: the *only* Raytac module JLCPCB can place is the nRF52833
MDBT50Q-512K, which we can't use for ANT. No Raytac nRF52832/nRF52840
module is in the library, so a Raytac part would mean JLCPCB global
sourcing / consigned parts, not standard SMT. The JLCPCB-assemblable set
therefore stays: **Fanstel BT832 (52832)**, **E73-2G4M08S1C (52840)**,
**E73-2G4M04S1B (52832)**. (Re-check at order time — the library changes.)

### 9.6 Cost estimate (module placed at JLCPCB)

Rough all-in cost *of the module slot only* (module price + its share of
the PCBA overhead), to compare the three realistic candidates. Excludes
the bare-PCB fab, the other components, the 32.768kHz crystal + 2 caps
needed by the nRF52832 modules (~$0.2–0.5 BOM), and shipping. Figures
are indicative (LCSC/JLCPCB, ~Sep–Oct 2025; the BT832 was out of stock
at the time of the check — reconfirm in the quote tool at order time).

Module prices (LCSC, per piece):

| Module | SoC | LCSC/JLCPCB | @1 | @10/100 |
|---|---|---|---|---|
| E73-2G4M04S1B | nRF52832 | C411306 | ~$5.03 | ~$4.59 (100) |
| **Fanstel BT832** | nRF52832 | C7092400 | ~$7.37 | ~$7.19 (10), ~$5.9 (1000) |
| E73-2G4M08S1C | nRF52840 | C356849 | ~$7.59 | ~$5.52 (100) |

JLCPCB overhead that differs between them:

- **Filière**: the two nRF52832 modules (S1B, BT832) are **Economic
  PCBA-eligible → no mandatory X-ray**. The nRF52840 **S1C is Standard
  PCBA only and requires X-ray** (§9.5).
- **Setup fee** (once per order): Economic $8 · Standard single-side
  $25.
- **X-ray** (per board, S1C only): $1.57 (1–10) · $0.79 (11–50) ·
  $0.47 (51–200).
- **Extended-part loading**: ~$3 once per order (all three are Extended).

All-in per board (setup/X-ray amortised over the lot):

| Module | @5 boards | @100 boards |
|---|---|---|
| E73-2G4M04S1B (52832, Economic) | ≈ $7.2 | ≈ $4.7 |
| **Fanstel BT832** (52832, Economic) | ≈ $9.6 | ≈ $6.4 |
| E73-2G4M08S1C (52840, Standard + X-ray) | ≈ $14.8 | ≈ $6.3 |

**Reading**: in prototype quantities the nRF52840 S1C costs ~2× the S1B
to place, driven by the forced Standard PCBA + X-ray, *not* by the chip
price; the gap nearly closes at 100+. The BT832 is the dearest of the
two nRF52832 options as a bare module (~$7.4 vs ~$5.0), but still well
under the S1C in prototype runs because it avoids X-ray — the premium
buys a module ~2× more compact (14×16 vs 17.5×28.7mm) than the S1B with
the DC/DC inductors already inside.

Sources:
[JLCPCB C7092400 (BT832)](https://jlcpcb.com/partdetail/Fanstel-BT832/C7092400) ·
[LCSC C356849 (S1C)](https://lcsc.com/product-detail/C356849.html) ·
[LCSC C411306 (S1B)](https://www.lcsc.com/product-image/C411306.html) ·
[JLCPCB PCB assembly price (setup + X-ray tiers)](https://jlcpcb.com/help/article/pcb-assembly-price).

### 9.7 I/O (GPIO) selection and pin mapping

We need **7 user I/O**: 5 buttons + 2 LEDs (see §5–§6 and §8), plus the
fixed lines (crystal, SWD, RESET, power). This section picks GPIOs that
avoid the pins Nordic flags near the radio, and gives the mapping on the
pads of each of the three selected modules (§9.3).

**Selection rules**

- **LEDs → high-drive GPIOs, *outside* the radio-restricted set.** The
  LEDs are driven at ≈5–10mA from a high-drive pin (§8.3–§8.6), which is
  exactly the "high current near the radio" case Nordic warns degrades
  RX sensitivity.
- **Buttons → anything left, radio-restricted pins allowed.** A button is
  a slow, DC-level input (≤10kHz, the "low frequency I/O" limit), so the
  restricted pins are fine for buttons — that is where we put them.
- **Avoid the NFC pins P0.09 / P0.10** unless we deliberately switch them
  to GPIO in the UICR (`CONFIG_NFCT_PINS_AS_GPIOS=y` in Zephyr). They
  default to the NFC antenna function.
- **Reserved, not usable as user I/O**: XL1/XL2 (P0.00/P0.01, the
  32.768kHz crystal — on the BT832 and S1E; the S1B has it internal),
  SWDIO/SWDCLK (debug), RESET (**P0.21** on nRF52832, **P0.18** on
  nRF52833), and **SWO** for trace (see "Debug & trace" below) —
  **P0.18 on the nRF52832** (BT832, S1B).

**Radio-restricted pins ("… low frequency I/O only" in the PS pin table)**

- **nRF52832** (BT832, E73-2G4M04S1B): **P0.22–P0.31**. Note P0.25–P0.31
  double as the analog inputs.
  [nRF52832 pin assignments](https://docs.nordicsemi.com/r/bundle/ps_nrf52832/page/pin.html)
- **nRF52833** (E73-2G4M08S1E) — a *different* list, must not be assumed
  equal to the 52832's: **P0.02, P0.03, P0.09, P0.10, P0.19, P0.23,
  P0.25, P0.28, P0.29, P0.30, P0.31, P1.01, P1.02, P1.03, P1.04, P1.05,
  P1.06, P1.07**.
  [nRF52833 pin assignments](https://docs.nordicsemi.com/r/bundle/ps_nrf52833/page/pin.html)

#### Unified pin plan (one firmware for all three boards)

The intersection of the three modules' broken-out GPIOs is small (the
BT832's 16 castellated pins are the limiting factor, and the S1E does not
break out P0.27, while its P0.18 is RESET). Exactly **seven** lines are
common to all three, which is just enough:

| Function | Pin | High-drive? | Notes |
|---|---|---|---|
| LED green | **P0.13** | yes, non-restricted on 52832 **and** 52833 | ✔ ideal for an LED |
| LED red | **P0.20** | yes, non-restricted on 52832 **and** 52833 | ✔ ideal for an LED |
| Button 1 | **P0.02** | input (restricted on 52833, fine as button) | AIN0 |
| Button 2 | **P0.03** | input (restricted on 52833, fine as button) | AIN1; also the BT832 AT-mode strap — irrelevant with our own firmware |
| Button 3 | **P0.26** | input (restricted on 52832, fine as button) | — |
| Button 4 | **P0.09** | input | NFC1 → set `NFCT_PINS_AS_GPIOS` |
| Button 5 | **P0.10** | input | NFC2 → set `NFCT_PINS_AS_GPIOS` |

Cost of this plan: it spends the two NFC pins on buttons (one UICR
setting; we use no NFC). If that is unwanted, use the per-module plans
below, which have spare non-NFC pins on the S1E and S1B (only the BT832,
limited to 16 castellated pins, has no alternative to P0.09/P0.10 for the
5th button unless an LGA pad is brought out).

#### Per-module mapping

**Fanstel BT832 (nRF52832)** — castellated pins only (LGA pads left
unconnected, §9.2). All 16:

| Cast. pin | Signal | Role in our design |
|---|---|---|
| 1 | P0.26 | Button 3 (radio-restricted, ok for input) |
| 2 | P0.27 | spare button (radio-restricted) |
| 3 | P0.00/XL1 | 32.768kHz crystal |
| 4 | P0.01/XL2 | 32.768kHz crystal |
| 5 | P0.02/AIN0 | Button 1 |
| 6 | P0.03/AIN1 | Button 2 (AT-mode strap — n/a for our fw) |
| 7 | P0.09/NFC1 | Button 4 (needs `NFCT_PINS_AS_GPIOS`) |
| 8 | P0.10/NFC2 | Button 5 (needs `NFCT_PINS_AS_GPIOS`) |
| 9 | VDD | power |
| 10 | GND | power |
| 11 | P0.13 | **LED green** (high-drive, non-restricted) |
| 12 | P0.18 | **SWO** (trace) → `J1`; also TRACEDATA[0]/SWO on nRF52832 |
| 13 | P0.20 | **LED red** (high-drive, non-restricted) |
| 14 | P0.21/RESET | RESET |
| 15 | SWDCLK | debug |
| 16 | SWDIO | debug |

P0.18 (pin 12) is now reserved for **SWO** (trace, see below), so on the
BT832 the only remaining spare is **P0.27 (pin 2)**. With 5 buttons +
2 LEDs + SWO we are at the limit of the 16 castellated pins: freeing the
two NFC pins (P0.09/P0.10) would require routing an LGA pad, since P0.18
is no longer available as a fallback. If SWO is not populated on a given
board, pin 12 / P0.18 can fall back to a spare high-drive I/O.
[BT832 datasheet (pin functions)](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/40/BT832_A_F_AF_V2.04_Dec-2017.pdf)

**Ebyte E73-2G4M08S1E (nRF52833)** — plenty of GPIOs, so no NFC pin is
needed. Recommended assignment (all high-drive, non-restricted for the
LEDs):

| Module pad | Signal | Role |
|---|---|---|
| 11 | XL1 (P0.00) | 32.768kHz crystal |
| 13 | XL2 (P0.01) | 32.768kHz crystal |
| 33 | P0.13 | **LED green** (non-restricted) |
| 32 | P0.20 | **LED red** (non-restricted) |
| 14 | P0.06 | Button 1 (non-restricted, could also be an LED) |
| 16 | P0.08 | Button 2 |
| 22 | P0.07 | Button 3 |
| 20 | P0.12 | Button 4 |
| 12 | P0.26 | Button 5 (non-restricted on 52833) |
| 37 / 39 | SWDIO / SWDCLK | debug |
| 26 | RST (P0.18) | RESET |

(Pads for P0.02/P0.03/P0.25/P0.28–P0.31 and the P1.x lines are the
radio-restricted ones on the 52833 — keep them for extra buttons or leave
unused. This module can run the unified plan above too, but the plan
here avoids burning the NFC pins.) Needs an external 32.768kHz crystal
(XL1/XL2, pads 11/13), same as the BT832.

**Ebyte E73-2G4M04S1B (nRF52832)** — built-in LFXO (no external crystal;
XL1/XL2 are not broken out). High-drive pins P0.02–P0.08 / P0.11–P0.20
are plentiful:

| Module pin | Signal | Role |
|---|---|---|
| 28 | P0.13 | **LED green** (non-restricted) |
| 35 | P0.20 | **LED red** (non-restricted) |
| 17 | P0.02 | Button 1 |
| 18 | P0.03 | Button 2 |
| 19 | P0.04 | Button 3 |
| 20 | P0.05 | Button 4 |
| 21 | P0.06 | Button 5 |
| 37 / 38 | SWDCLK / SWDIO | debug |
| 36 | P0.21/RST | RESET |

(Avoid P0.22–P0.31 — pins 5–11 and 39–41 — for the LEDs; they are the
radio-restricted set and P0.25–P0.31 are also analog inputs. They remain
fine as extra button inputs. The NFC pins P0.09/P0.10 are pins 24/25
here and are left free in this plan.)

#### Debug & trace — SWD + SWO only (J1)

**Decision: expose SWD plus SWO, and nothing more.** This gives full
halt-mode debug *and* ITM (printf / PC-sampling) trace, at the cost of a
single extra pin. Full instruction trace (ETM) is explicitly **out of
scope** — it needs the 4-bit parallel trace port (TRACECLK + TRACEDATA
[0:3] = 5 high-speed pins) and a J-Trace probe; TRACECLK is P0.20 (our
red LED) and TRACEDATA[1:3] are not even on the BT832 castellated pins,
so parallel trace is not buildable here anyway.

What SWD alone already gives (nRF52832 PS §16): the full Debug Access
Port — **6 hardware breakpoints + 2 literal comparators (FPB)**, **4
watchpoints (DWT)**, single-step, register/memory/variable inspection,
flashing, and real-time debug (high-priority IRQs keep running while
halted). **No extra pin is needed for breakpoints.**

Trace is multiplexed onto GPIOs in two mutually-exclusive TPIU modes
(PS §16.6): **Serial (SWO)** carries ITM only on **one pin, SWO**;
**Parallel** carries ITM+ETM on five pins. We take SWO.
- **SWO pin = P0.18 on the nRF52832** (BT832, S1B) — `TRACEDATA[0]/SWO`.
  On the BT832 this is castellated pin 12 (reserved above).
- On the **nRF52833** (S1E) P0.18 is the RESET pin; its SWO/trace pins
  are on different GPIOs — check the nRF52833 PS trace-pin table before
  wiring SWO on that module.
- SWO needs a probe that supports it (J-Link: yes; ST-Link V2: flaky).
  Set the SWO GPIO drive to `H0H1` if the default `S0S1` is too slow
  (PS §16.6), and don't let firmware override it during a trace session.

**`J1` pinout (5 signals):**

| J1 pin | Signal | Notes |
|---|---|---|
| 1 | SWDIO | internal pull-up |
| 2 | SWDCLK | internal pull-down |
| 3 | GND | |
| 4 | VDD (sense) | **sense only**, board stays coin-cell-powered (§2) |
| 5 | **SWO** | P0.18 (nRF52832); optional to populate |

For day-to-day logging, **SEGGER RTT over SWD needs no pin at all** (it
rides the existing SWD link) and is the Zephyr `log` backend of choice;
SWO/ITM is the fallback when precise timing or PC sampling is wanted. If
SWO is left unpopulated on a board, P0.18 reverts to a spare high-drive
I/O.

**Common to all three**: SWDIO/SWDCLK on `J1` (§2), RESET broken out,
LEDs on **P0.13 / P0.20** (the two pins that are high-drive and
non-restricted on both the 52832 and the 52833 — the safe choice across
the whole selected set). Firmware pin assignments and the KiCad
schematic should follow whichever plan (unified or per-module) is chosen;
fold this into the knock-on edits listed in §9.3.

---

## 10. Open points (hardware)

- **Module choice** (§9) — E73-2G4M08S1E (nRF52833) dropped; confirm
  Fanstel BT832 (nRF52832): footprint (castellated pins), 32.768kHz
  crystal, pin plan, ANT bring-up, JLCPCB stock at order time; then
  propagate to specs doc, firmware README/comment, `hardware/board` and
  KiCad. Alternatives Raytac MDBT42Q, E73-2G4M08S1C, E73-2G4M04S1B.
- **P-MOSFET reverse-polarity stage** (§3) — part selection (Vgs(th),
  Rds(on) at the CR2032's low current levels, package size) not yet
  done; add to schematic/BOM.
- **SWD header (`J1`) wiring convention** — make sure schematic/BOM
  notes explicitly document "sense-only, board stays coin-cell-powered
  during programming" (§2) so it isn't miswired as a power line later.
- **Button hardware** (§5–§6) — primary pick is now the sealed IP67 THT
  tact switch (Omron `B3W-AQ`, ~5mm stack) following the 4–5mm actuator
  decision; validate the gloved-finger feel, else add a cap or fall back
  to the `Multimec 5G` + cap (~12.5mm). Membrane keypad stays the thin
  follow-up option (4-key off-the-shelf pending pinout check, or custom
  with MOQ/lead-time). Final button count (4 vs. 5) still open.
- **Temperature sensor accuracy figure** (§7) — reconcile ±4°C vs.
  ±5°C against the nRF52832 datasheet, and confirm the first-sample
  timing in `ant_garmin.c` relative to the triggering button press.
- **User-feedback LEDs** (§8) — APT2012SURCK + APT2012CGCK candidates
  (AlGaInP, not InGaN), GPIOs outside P0.22–P0.31, resistors; add to
  schematic; bench current check + outdoor daylight test.
- Everything else affecting the PCB (enclosure, mounting, sealing) is
  still tracked in `gopro_garmin_remote_specs.md` §4 ("Open points").
