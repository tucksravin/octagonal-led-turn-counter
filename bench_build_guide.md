# Octagonal Turn Counter — At-the-Bench Build Guide

**Scope**: everything you solder and assemble to build the finished counter — control-box protoboard, the off-board harnesses (LED rim, 8 piezo wedges, slab DC rail), and the frame-side power. Centered on the control-box protoboard wiring.
**Print this**: one printout lives at the bench. Tick the boxes as you go.
**Authoritative sources**: `turn_counter_design_doc.md` (full rationale), `shopping_list.md` (sourcing + manufacturer PNs), and the two protoboard SVGs embedded here. Where this guide and those disagree, the SVGs win for wiring.

<div class="callout">
<p><strong>How to use this guide</strong>: work top to bottom. Sections 5 → 7 are the soldering; do them in order (control board first — you can bench-test it before committing to the off-board harnesses). Every checklist item is a checkbox. Do not skip Section 8 (pre-power continuity) — it is the difference between "it works" and a dead LED strip.</p>
</div>

## 1. What you're building — signal & power flow

One ESP32-S3 reads 8 piezo discs (one per table side) and drives a single 240-LED WS2812B strip that rings the octagon. The strip is driven directly at 3.3 V through a 470 Ω resistor (a 74AHCT125 level shifter is an optional add-back — §5.2). A frame-mounted 5 V / 18 A PSU feeds the slab through a single Anderson Powerpole DC disconnect so the top lifts off.

[WIRING_FIGURE]

**The five nets that matter at the bench:**

| Net | Runs | Wire |
|-----|------|------|
| **+5 V** | PSU → fuse → Powerpole → slab WAGO node → control board + 3 strip injection points | 14 AWG (disconnect), 20 AWG (board pigtail/rails) |
| **GND** | common return for everything — PSU, board, strip, all 8 piezos | 14 / 20 / 22 AWG |
| **LED data** | GPIO 11 → 470 Ω → strip DIN (direct 3.3 V drive; level shifter optional — see §5.2) | 22 AWG stranded |
| **Piezo ADC ×8** | each piezo → 1 MΩ + Zener **at the disc** → twisted pair → its ADC pin | 22 AWG stranded twisted pair |
| **AC mains** | wall → IEC inlet → SPST switch (Line only) → PSU | stays on the frame, never crosses the disconnect |

**Pin map (from `turn_counter.ino` — verify against your DevKitC-1 v1.1 silkscreen):**

| ESP32-S3 GPIO | Function | Note |
|--------------:|----------|------|
| 11 | LED data out → 470 Ω → strip DIN | direct 3.3 V drive |
| 1, 2, 4, 5, 6, 7, 8, 9 | Piezo sides 1–8 (ADC1) | all on **ADC1** so tap detection survives Wi-Fi/BLE |

Avoid GPIO 0, 3, 19, 20, 45, 46 (strap/USB/PSRAM pins). All 8 piezos are deliberately on ADC1 — ADC2 returns 0 whenever the radio is active.

## 2. Bill of materials — build view

Full sourcing, quantities, and manufacturer PNs are in `shopping_list.md`. This is the bench-side "is it in front of me" list.

**On the control board**

| ✔ | Part | Qty | Note |
|---|------|----:|------|
| | ESP32-S3-DevKitC-1 (N8R8) | 1 | socketed on 2× 1×22 female headers — do **not** solder directly |
| | 470 Ω 1/4 W resistor | 1 | LED data series — spliced **in-line** in the data run, near DIN |
| | 1000 µF electrolytic | 1 | across 5V/GND at strip entry, **+ to 5V** |
| | Half-size Perma-Proto (30-col, Adafruit 571) | 1 | the control board — see `doc-src/protoboard_half_layout.svg` |
| | JST-XH 3-pin in-line pair | 1 | strip harness service disconnect (a few inches off-board) |
| | JST-XH 2-pin in-line pair | 8 | one per piezo — inline service disconnect, off-board |
| | *74AHCT125 + DIP-14 socket* | *(opt)* | *optional level-shifter add-back if pixel 1 glitches (§3) — not built by default* |

*(Off the board by design: the 8× 1 MΩ + 8× Zener are twisted into the pigtails at the discs (§4.3); the optional 10–47 kΩ ADC series resistors were dropped; direct 3.3 V drive means no level shifter.)*

**Off-board (slab)**

| ✔ | Part | Qty | Note |
|---|------|----:|------|
| | WS2812B strip, 60 LED/m, ~4 m | 1 | cut into 8 side-segments |
| | Aluminum LED channel + diffuser | 4–5 m | 22.5° miters at each corner |
| | 27 mm piezo disc | 8 | one per side, in a rim bore with ~3–5 mm floor (design doc §4.3) |
| | 1 MΩ 1/4 W + 3.3 V Zener (1N4728A) | 8 + 8 | twisted + soldered into each disc's pigtail, band toward signal |
| | JST-XH pre-crimped pigtails | 9 pr | strip 3-pin + 8× piezo 2-pin in-line disconnects (all off-board) |
| | WAGO 221 lever connectors | few | branch the slab DC rail |
| | Project box (Hammond 1591BBK) + M3 standoffs | 1 | control-box enclosure |
| | Rubber grommet | 1+ | slab cable entry (buy first, drill to match) |

**Frame side**

| ✔ | Part | Qty | Note |
|---|------|----:|------|
| | Mean Well LRS-100-5 PSU (5 V / 18 A) | 1 | on a ventilated wood block |
| | IEC C14 inlet + SPST 20 A rocker switch | 1 ea | discrete AC approach |
| | Inline 5 A blade fuse + holder | 1 | **DC side**, PSU +V → Powerpole |
| | Anderson Powerpole 30 A housings + contacts | 3 pr | the DC disconnect (solder + heat-shrink) |
| | 14 AWG silicone wire (red + black) | ~25 ft | disconnect run, twisted pair |

## 3. Tools & consumables

**Essential**: temp-controlled iron (Pinecil V2 / Hakko), 63/37 leaded rosin solder, flush cutters, self-adjusting strippers, multimeter (continuity + DC volts).
**Strongly recommended**: helping hands / PCB vise, flux pen (Kester 951), heat-shrink assortment, lighter or heat gun, solder wick, silicone bench mat.
**Consumables at hand**: 22 AWG stranded hookup wire (6 colors), 20 AWG silicone (red/black), 14 AWG silicone (red/black), heat-shrink, isopropyl for cleanup.

<div class="callout">
<p><strong>Wire gauge rule for this build</strong>: signal = 22 AWG stranded (insulated). Board power runs/pigtail = 20 AWG (14 AWG won't fit protoboard holes; 22 AWG is under the layout's ≥20 AWG spec). DC disconnect run = 14 AWG silicone.</p>
</div>

## 4. Before you solder

- [ ] Iron tinned and at temp (~330–350 °C for leaded); flux pen within reach
- [ ] Bench mat down; strip is powered **off** and unplugged from any supply
- [ ] Piezos: solder leads in **under 2 seconds** per pad — the discs are heat-sensitive
- [ ] Keep the mains habit from day one: **unplug from the wall before opening the enclosure** (the SPST switch breaks Line only)
- [ ] Have `shopping_list.md` open for the Powerpole solder procedure when you get there

## 5. Control-box protoboard — the heart of the build

Two diagrams drive this section. The **placement insert** shows where each component sits on the Perma-Proto; the **wiring diagram** is the point-to-point wire list you solder in order. Print both; keep them side by side.

[PROTO_LAYOUT_FIGURE]

[PROTO_WIRING_FIGURE]

### 5.1 Place components (per the layout insert)

- [ ] Solder the two 1×22 female header strips (ESP32 socket) at rows b & i, **cols 1–22**, **USB ports facing left**
- [ ] Place the 1000 µF cap (**+ leg to the 5 V rail**)
- [ ] That's the whole board. The piezo networks live at the discs (§6.2), the 470 Ω is an in-line splice in the data wire, and there is no level shifter (direct 3.3 V drive — cols 23–30 stay empty; that's where the optional '125 would go)

### 5.2 Solder the wire list — in this order

Power & rails first, then LED data, then the piezo pairs. Columns refer to `doc-src/protoboard_half_layout.svg`.

**Power in & rails**

- [ ] **P1** — pigtail red 20 AWG: WAGO +5V → bottom +5V rail (left end)
- [ ] **P2** — pigtail black 20 AWG: WAGO GND → bottom GND rail (left end)
- [ ] **P3** — bridge 20 AWG: bottom +5V rail ↔ top +5V rail, routed around the board's **left edge** (the socket occupies col 1)
- [ ] **P4** — bridge 20 AWG: bottom GND rail ↔ top GND rail, same route
- [ ] **P5** — 1000 µF: + → bottom +5V rail, − → bottom GND rail (at entry)

**ESP32 power**

- [ ] **P6** — col 1 row a (DevKit GND, USB end) → top GND rail
- [ ] **P7** — col 2 row a (DevKit 5V, USB end) → top +5V rail

**LED data path** *(direct 3.3 V drive — no level shifter)*

- [ ] **D1** — col 6 row a (GPIO 11) → blue harness lead out the right board edge (strip DATA). The **470 Ω is spliced in-line** in this run, heat-shrunk, as close to the strip DIN as practical
- [ ] *(optional robustness upgrade — skip unless pixel 1 glitches)* insert a 74AHCT125 in this run: GPIO 11 → '125 input → '125 output → 470 Ω → DIN; VCC → +5V, 1OE → GND. See design doc §3

**LED strip harness** *(direct-soldered — no board JST)*

- [ ] **L1** — red harness lead: bottom +5V rail (right end) → strip harness
- [ ] **L2** — black harness lead: bottom GND rail (right end) → strip harness
- [ ] **L3** — bundle DATA + 5V + GND; fit the **in-line JST-XH 3-pin** a few inches off-board (service disconnect) → strip DIN + start injection

**Piezo pairs (22 AWG twisted; signal lands direct on the ADC hole — no series resistor; each pair carries an inline JST-XH 2-pin off-board, labeled S1–S8)**

- [ ] **A1** — S1 (GPIO 1): signal → col 19 row j · GND lead → bottom GND rail
- [ ] **A2** — S2 (GPIO 2): signal → col 18 row j · GND lead → bottom GND rail
- [ ] **A3** — S3 (GPIO 4): signal → col 19 row a · GND lead → top GND rail
- [ ] **A4** — S4 (GPIO 5): signal → col 18 row a · GND lead → top GND rail
- [ ] **A5** — S5 (GPIO 6): signal → col 17 row a · GND lead → top GND rail
- [ ] **A6** — S6 (GPIO 7): signal → col 16 row a · GND lead → top GND rail
- [ ] **A7** — S7 (GPIO 8): signal → col 11 row a · GND lead → top GND rail
- [ ] **A8** — S8 (GPIO 9): signal → col 8 row a · GND lead → top GND rail

**Off-board handoff**

- [ ] **O1** — **label every pair S1–S8 at both ends** before routing — there are no keyed connectors to save you now
- [ ] **O2** — pigtail lands in the WAGO 221 node on the slab DC rail · leave a USB service loop reachable

<div class="callout">
<p><strong>Row a/j reminder</strong>: those are the free holes above/below the socket pins — every other hole in those columns is hidden under the DevKit. Don't insert the ESP32 until after Section 8 passes.</p>
</div>

## 6. Off-board harnesses (slab)

### 6.1 LED rim

- [ ] Cut 8 channel pieces, 22.5° miters each end; dry-fit around the slab's outer edge
- [ ] Cut the strip into 8 ~30-LED segments **between pads**; lay into channels, peel adhesive
- [ ] Solder 3-conductor jumpers (5V/GND/Data) across each corner; heat-shrink each joint
- [ ] Continuity-check the data line end-to-end across the assembled strip
- [ ] Solder power-injection pigtails at **3 points**: strip start (DIN), the mid corner (~between sides 4 & 5), and the strip end
- [ ] **Calibrate the per-side LED counts** over serial (tap_light: `0`–`7` select side, `+`/`-` adjust, `p` print) until every color flip lands on a corner — the table persists in NVS and turn_counter reads it automatically; paste the printed array into both sketches' defaults

### 6.2 Piezo modules (×8)

- [ ] Drill 8 rim bores from the underside, one per side near the outer edge, leaving a **~3–5 mm floor** — never through to the laminate (design doc §4.3)
- [ ] Solder leads to all 8 piezos (red +, black −), under 2 s per pad
- [ ] Twist each disc's 1 MΩ + Zener legs into the pigtail wires ~1/2" off the disc — one soldered bundle per node, **Zener band toward signal** (check before twisting); heat-shrink each node separately. Terminate the disc pigtail in its **inline JST-XH 2-pin**, labeled S1–S8 (signal on pin 1, GND on pin 2 — keep all 8 consistent)
- [ ] Bench-test each: multimeter on AC volts, tap, expect a brief swing
- [ ] Glue each piezo into its bore, brass face to the wood floor (CA or thin epoxy); vacuum the dust first; press 30 s
- [ ] Route the twisted pairs through the underside kerfs to the control box; **label each pair to its side (S1–S8)**

### 6.3 Slab DC rail & disconnect

- [ ] Drill the grommet hole (buy grommet first, drill to match); run the slab-side DC cable in
- [ ] Solder Powerpole contacts to the slab-side cable ends; insert into housings (red +V, black GND)
- [ ] Branch the slab DC cable into a WAGO 221 node → control-box 5V/GND **and** the 3 strip injection points
- [ ] **Verify polarity at every junction with a multimeter** — reversed 5 V instantly kills the strip
- [ ] P-clip / cable mount within 4" of the grommet — strain relief hits the clip, not the connector
- [ ] Land the 8 labeled piezo pairs on the board (per §5.2 A1–A8), then click the 8 piezo JSTs and the strip harness's in-line JST together

## 7. Frame side (permanent) — AC mains & PSU

<div class="callout">
<p><strong>Mains safety</strong>: do this section with everything unplugged from the wall. AC never crosses the disconnect — only +5 V DC does. Verify all AC continuity with a meter <em>before</em> the plug ever goes in the wall.</p>
</div>

- [ ] Mount the PSU to a ~4×6×1" wood block; screw the block to a **ventilated** spot under the frame
- [ ] Wire AC: wall plug → IEC C14 inlet → SPST switch (Line only) → PSU **L**. Neutral runs inlet → PSU **N** direct (no switch). Earth runs inlet Earth → PSU chassis lug (no switch). 5 faston connections total (3 Line + 1 N + 1 Earth)
- [ ] Wire PSU **+V → inline 5 A fuse → Powerpole +V**; PSU **−V → Powerpole −V** direct
- [ ] Solder Powerpole contacts (procedure in `shopping_list.md`), insert red=+V / black=GND, slide housings together
- [ ] Leave ~18" of slack so you can lift the slab before disconnecting
- [ ] Continuity check: switch ON → Line continuous inlet→PSU; switch OFF → Line open; N & Earth always continuous
- [ ] **Frame-only power test** (no slab): switch on, meter the Powerpole → expect +5 V red-to-black. Switch off.

## 8. Pre-power-up continuity checklist — DO NOT SKIP

Do all of these with the board **unpowered** and the ESP32 still **out of its socket**, before the first power-up.

- [ ] **+5 V ↔ GND**: probe for shorts. Expect open (the cap may give a brief charge beep, then settle open). A hard short here = find it before you apply power
- [ ] **Buzz each S column → its GPIO pin** (S1→GPIO1 … S8→GPIO9), with the piezo JSTs mated — confirms the ADC jumpers A1–A8 through the connectors
- [ ] **LED DATA → IC pin 3 (1Y)** continuous — confirms D2
- [ ] **1OE (IC pin 1) → GND** continuous — confirms P9 (or the strip stays Hi-Z / dark)
- [ ] **IC pin 7 (GND) → GND** and **IC pin 14 (VCC) → +5 V** — confirms P8/P10
- [ ] **1000 µF polarity**: + leg on the 5 V rail
- [ ] Only now: seat the ESP32-S3 into its socket, orientation correct (plus the 74AHCT125 if you fitted the optional shifter)

## 9. Firmware flash

- [ ] Arduino IDE: install the **ESP32 board package**; select **ESP32-S3-DevKitC-1**
- [ ] **Partition Scheme → Minimal SPIFFS (min_spiffs)** — `turn_counter.ino` is ~9 KB too big for the default 1.25 MB app partition and won't link otherwise
- [ ] **USB CDC On Boot → Enabled**; upload speed 115200 if it's flaky
- [ ] Port: pick the **`usbserial-*` (CP2102)** device — the `usbmodem` ports are not the board
- [ ] Confirm `PIEZO_PINS[] = {1,2,4,5,6,7,8,9}` and `NUM_LEDS` / `LEDS_PER_SIDE` match the installed strip
- [ ] Flash, open Serial Monitor at 115200; watch the per-side baseline seed lines at boot
- [ ] If it won't enter download mode: hold **BOOT**, tap **RESET**, release **BOOT**
- [ ] OTA (optional): set Wi-Fi creds, confirm `ping turn-counter.local` resolves

## 10. First power-up & smoke test

- [ ] **Slab bench test** (before mating to the frame): connect the bench-test Powerpole pigtail + a 5 V supply → confirm the strip lights and taps advance the lit zone
- [ ] Disconnect the bench supply; **mate slab Powerpole → frame Powerpole**
- [ ] First full power-up: switch on, **watch for smoke** (no smoke = good)
- [ ] Tap through every player position — the lit zone should sweep cleanly across all 8 sides; look for dead LEDs, wrong colors, corner gaps
- [ ] Test the disconnect: off → unmate → lift the slab → set back → reconnect → on → confirm state restored

## 11. Bench troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Whole strip dark | switch off, blown fuse, reversed polarity, or **1OE not grounded** | check switch → fuse → PSU output → polarity → P9 |
| First LED wrong color / flicker | weak data into pixel 1 (3.3 V direct is marginal) | confirm 470 Ω + common ground (D1); if it persists, add the optional 74AHCT125 (§5.2) |
| Far-end LEDs tint pink/orange | voltage droop | check mid/end power injection |
| **Constant/chaotic taps regardless of piezo**, rail-to-rail ADC with a stable baseline | **floating ADC pin — lost GND return** (broken ground), not a threshold problem | trace the GND chain (G1) and the piezo/board common ground; it's a broken ground, chase continuity |
| Tap doesn't register | `TAP_DELTA` too high, bad piezo joint, glue not contacting | lower `TAP_DELTA`, reflow, re-glue |
| Tap on one side lights another | piezo cable mapping wrong | check `PIEZO_PINS[]` order vs physical wiring |
| Won't flash via USB | wrong port / CDC / speed | use `usbserial-*` port, USB CDC On Boot = Enabled, drop to 115200, BOOT+RESET |
| Solder joint dull/blobby | cold joint | reflow with flux, hold ~2 s while cooling |

## 12. Quick reference

**Pin map**: LED data = **GPIO 11** → 470 Ω → strip DIN (direct 3.3 V; optional 74AHCT125 in between). Piezos = **GPIO 1, 2, 4, 5, 6, 7, 8, 9** (ADC1), sides 1–8 in order.
**Firmware knobs**: `NUM_LEDS` (match installed count), `LEDS_PER_SIDE = NUM_LEDS/8` rounded down, `TAP_DELTA` ≈ 30% of a deliberate tap's jump above baseline, `DEBOUNCE_MS = 250`.
**Golden rules**: 1OE → GND or no light · + to 5 V on the cap · polarity-check every DC junction · unplug the wall before opening the box · label every off-board cable.

*Companion to `turn_counter_design_doc.md`. Wiring per `doc-src/protoboard_wiring.svg`.*
