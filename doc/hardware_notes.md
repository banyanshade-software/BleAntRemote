# GoPro + Garmin Edge Bike Remote — Hardware Notes

Companion document to `gopro_garmin_remote_specs.md`, focused on the
hardware/electronics side (PCB, power, programming, enclosure-adjacent
electrical decisions). Same conventions as the specs document: sourced
claims are marked, unverified/assumed points are flagged as such.

---

## 1. Hardware summary

- **MCU / radio**: Nordic **nRF52832**, via the **Ebyte E73-2G4M08S1E**
  module (18.0 x 13.0mm, 43-pin castellated package). Single chip covers
  both BLE (GoPro link) and ANT+ (Garmin Edge link) — see the specs
  document for why this chip was chosen over alternatives (e.g. ESP32).
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
- large enough to operate **with cycling gloves on** — actuator
  diameter roughly **7–10mm**, possibly a bit less,
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

**Recommendation: APEM/MEC `Multimec 5G` series, through-hole, with a
round "floating cap" accessory.**
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

**Why not a smaller, lower-stack sealed THT switch instead**: sealed
THT tact switches with actuators already in the 7–10mm range and a
sub-10mm total stack do not appear to exist as a single stocked part —
this class of switch (CIT's `STV` series, C&K's sealed sealed lines,
etc.) sticks to the same small-actuator-plus-cap pattern as the SMD
world once sealing is required; the Multimec 5G's ~12.5mm stack is
close to the practical minimum for "sealed + through-hole + big enough
for a gloved finger" today, not a suboptimal pick within that
constraint set.

**Open points**:
- Final button count (4 vs. 5) — tied to the still-open "Handling of
  the 2 camera buttons" question in the specs doc.
- Pick the exact cap code (`1ES` Ø9.6mm vs. the Ø12mm polyamide round
  cap) once the enclosure's front-panel button-hole size is decided,
  and get a firm per-unit price for the switch+cap combination for the
  BOM.
- The ~12.5mm total switch+cap stack height now sets a floor on the
  enclosure's thickness behind each button — fold this into the
  enclosure design (specs doc §4).
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
switches. It is a legitimate, very compelling alternative to the
Multimec 5G recommendation in §5 on exactly the axis this project cares
about: **compactness**.

- **Thickness**: a standard (non-waterproofed) membrane switch stack is
  on the order of **~0.7mm**; a waterproofed version (thicker overlay +
  edge-sealing adhesive) is on the order of **~1.4mm** — vs. the
  **~12.5mm** total stack for a Multimec 5G switch + oversized cap
  (§5). This is the single biggest advantage: essentially the entire
  vertical budget a switch+cap combo needs disappears.
  [LID/tactilemembrane: standard vs. waterproof membrane switch stack-up](https://tactilemembrane.com/waterproof-membrane-switch-harsh-environment-design/)
- **Sealing**: the membrane is bonded to the front panel with a
  continuous perimeter adhesive (e.g. 3M acrylic adhesive films
  specifically sold for this), so the "sealing surface" is the whole
  keypad sheet, not a per-button gasket — **IP65/IP67 is a normal,
  well-documented outcome** for this construction, not a stretch.
  [LID/tactilemembrane: IP67/NEMA 4X sealed membrane switch design](https://tactilemembrane.com/waterproof-membrane-switch-harsh-environment-design/)
- **Actuator size/shape is free**: because each "button" is just
  printed artwork plus a dome underneath, the 7–10mm glove-friendly
  target from §5 is trivial to hit exactly (or any shape/spacing) —
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

**Recommendation**: keep §5's `Multimec 5G` (through-hole switch +
cap) as the pick for the **first prototype board** — it's a
buy-today, solder-today catalog part with no MOQ/lead-time risk, even
though it costs ~12mm of stack height. Treat the membrane keypad as
the **strong follow-up option once the design stabilizes**:
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

## 8. Open points (hardware)

- **P-MOSFET reverse-polarity stage** (§3) — part selection (Vgs(th),
  Rds(on) at the CR2032's low current levels, package size) not yet
  done; add to schematic/BOM.
- **SWD header (`J1`) wiring convention** — make sure schematic/BOM
  notes explicitly document "sense-only, board stays coin-cell-powered
  during programming" (§2) so it isn't miswired as a power line later.
- **Button hardware** (§5–§6) — decide between the `Multimec 5G`
  through-hole switch+cap (buildable now, ~12.5mm stack) and a
  membrane keypad (far thinner, but either fixed at 4 keys off-the-shelf
  pending a pinout check, or custom-manufactured with MOQ/lead-time
  implications) — final button count, and this choice, both still open.
- **Temperature sensor accuracy figure** (§7) — reconcile ±4°C vs.
  ±5°C against the nRF52832 datasheet, and confirm the first-sample
  timing in `ant_garmin.c` relative to the triggering button press.
- Everything else affecting the PCB (enclosure, mounting, sealing) is
  still tracked in `gopro_garmin_remote_specs.md` §4 ("Open points").
