# Octagonal Gaming Table Turn Counter — Design Document

**Project**: LED rim turn counter for an octagonal gaming table
**Geometry**: 8 sides × 20" each = 160" (~4 m) perimeter
**Player count**: Variable, 2–8 (configurable in firmware setup mode)
**Input method**: Piezo sensor per side (slap-to-advance)
**Brain**: ESP32 (with optional OTA firmware updates over Wi-Fi)
**Target**: Reliable, satisfying turn-passing mechanic

> **Companion files**:
> - `turn_counter_wiring.svg` — full schematic, print and keep at the bench
> - `turn_counter.ino` — main project firmware
> - `tap_light.ino` — Phase 0 starter project firmware
>
> Other diagrams (top view, edge cross-section, slab/frame architecture) are embedded inline in the relevant sections of this document.

## Summary

A perimeter LED rim around an octagonal gaming table shows whose turn it is — one section glows in the active player's color while the rest stays dark. The active player taps their section to pass turn; the lit zone advances clockwise to the next player. Player count is configurable from 2 to 8, and a two-handed slap on opposite sides toggles the whole thing on or off.

The build is a thin octagonal slab that sits on top of an existing bumper pool table. The slab is removable — a single DC connector pops apart so it can lift away, leaving the PSU and AC mains permanently mounted to the pool table frame underneath.

This document walks through the build end-to-end: tools and a Phase 0 starter project for first-time soldering, bill of materials, electrical and mechanical design, step-by-step assembly, firmware reference, and troubleshooting.

---

## 0. Before You Start: Tools, Skills, and a Practice Project

This is your first electronics project. Don't skip this section. Going straight to the main build means doing 40+ solder joints on parts you can't easily replace, while learning to solder, on a project that won't work if any one of them is bad. That's a bad time.

Phase 0 has three goals:
1. Get you a real bench setup with the tools you'll need.
2. Teach you the techniques (soldering, multimeter use, ESP32 flashing) on a small, forgiving project.
3. Leave you with a finished, working **tap-activated desk light** that uses the same parts and patterns as the main build, just at 1/8 scale.

Plan on **4–6 hours** for Phase 0 spread over a couple of evenings. First-time soldering is slow, and that's fine.

### 0.1 Tools You'll Need

**Essential (~$80–120 total)**:

| Tool | Recommendation | Notes |
|------|----------------|-------|
| Soldering iron with adjustable temp | Pinecil V2 (~$30) or Hakko FX-888D (~$110) | Skip the $15 fixed-temp pencil irons. Variable temp is the difference between fun and frustration |
| Solder | 60/40 leaded rosin core, 0.6 mm or 0.8 mm | Leaded is much easier to learn on. Ventilate and wash hands. Lead-free is harder and not necessary at this scale |
| Side cutters / flush cutters | Hakko CHP-170 (~$8) | For trimming component leads |
| Wire strippers | Any self-adjusting strippers (~$15) | Not the manual ones with a sliding stop |
| Multimeter | Any cheap one (~$20) | You only need continuity and DC voltage. AstroAI on Amazon is fine |

**Strongly recommended (~$30 total)**:

| Tool | Recommendation | Notes |
|------|----------------|-------|
| Helping hands or PCB vise | Anything with alligator clips and a base | Genuinely transformative. Soldering with no third hand is miserable |
| Flux pen or paste | Kester 951 or MG Chemicals | Makes solder flow properly. You need this |
| Heat-shrink assortment | $10 mixed pack | Insulating splices and joints |
| Lighter or hot air gun | Any Bic | For heat-shrink. Hair dryer doesn't get hot enough |
| Solder wick / desolder braid | $5 | For fixing mistakes (you will make some) |
| Bench mat | Silicone, ~$15 | Heat-resistant, prevents you from burning your desk |

**Nice to have, but skip for now**: fume extractor, hot air rework station, magnifier, "third hand" with magnifier.

### 0.2 Soldering Primer

The skills are mostly muscle memory. Watch a video before you start — these two are good:
- "How to Solder" by EEVblog (12 min — fundamentals)
- "Common Soldering Mistakes" by Branchus Creations (8 min — what to avoid)

The condensed version that text can convey:

1. **Set the iron to ~330°C / 625°F** for leaded solder. Higher for lead-free, but you're using leaded.
2. **Tin the tip** when hot — touch a bit of solder to the tip, wipe on a damp sponge or brass coil. The tip should look shiny silver. A black, dull tip transfers heat poorly.
3. **Heat the joint, not the solder.** Touch the iron tip to both pieces being joined. Wait ~2 seconds. Then feed solder into the joint *from the opposite side of the iron*. The solder should flow toward the heat and wet both surfaces.
4. **Pull the solder away first, then the iron.** Don't move the joint while it cools (~1–2 seconds).
5. **Good joint**: shiny, smooth, slightly concave, wets both surfaces. **Bad joint**: dull, blobby, ball-shaped, only stuck to one side ("cold joint").
6. **If a joint is bad**, add a touch of flux, reheat, and either reflow or remove with wick and redo.

**Warmup exercise before touching real parts**: strip 4–5 pieces of scrap wire (any gauge), twist pairs together, and solder them. You'll do ~10 joints. By the last one, they should look noticeably better than the first. Don't proceed to real components until you can produce consistent shiny joints.

### 0.3 Multimeter Primer

You need exactly two functions for this project:

**Continuity** (often a 🎵 or diode symbol). Probes touched together = beep. Use it to:
- Verify a wire goes from where you think it does to where you think it does
- Check that solder joints actually connected
- Confirm a fuse is intact

**DC voltage** (V with a straight line, often "V⎓" or "VDC"). Set the range to 20V or use auto-range. Use it to:
- Verify a 5V power supply is putting out 5V (and not 12V because you grabbed the wrong PSU)
- Check polarity (red probe to +, black to GND, expect a positive number)

**Critical habit**: every time you connect power to something new, multimeter the polarity at the destination side first. Reversed 5V on a WS2812B strip kills LEDs instantly and silently.

### 0.4 Arduino IDE Setup

Before any soldering, get the dev environment working:

1. **Install Arduino IDE 2.x** from arduino.cc.
2. **Add ESP32 board support**: File → Preferences → Additional Board Manager URLs → add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`. Then Tools → Board → Boards Manager → search "esp32" → install "esp32 by Espressif Systems".
3. **Install libraries**: Tools → Manage Libraries → install **FastLED** (by Daniel Garcia). For the main project later, you'll also install **ArduinoOTA** (already bundled with the ESP32 core, no separate install).
4. **Plug in your ESP32 dev board** via USB. If your computer doesn't recognize it, install the CP210x or CH340 USB-to-serial driver depending on your board (ESP32-WROOM-32 dev boards usually have CP210x).
5. **Test upload**: File → Examples → 01.Basics → Blink. Tools → Board → ESP32 Arduino → "ESP32 Dev Module". Tools → Port → select the new port that appeared when you plugged in the board. Click upload. The onboard LED should blink. If this works, you're ready.

If upload fails: hold the BOOT button on the board during upload, or drop the upload speed to 115200 in Tools → Upload Speed.

### 0.5 The Starter Project: Tap Light

A small WS2812B accent light with a single piezo sensor. Tap it to cycle through 6 modes (warm white-ish, blue, green, amber, rainbow, off). Power it from the ESP32's USB port — no separate PSU, no mains wiring, no level shifter to start.

This is essentially **one zone of the main project**. Every skill you exercise here transfers directly.

#### Parts (all from the main BOM)

| Qty | Part |
|----:|------|
| 1 | ESP32 dev board |
| 30 LEDs | WS2812B strip (cut from the 5 m roll — you have plenty) |
| 1 | Piezo disc |
| 1 | 1 MΩ resistor |
| 1 | 3.3 V Zener diode |
| 1 | 470 Ω resistor |
| 1 | 1000 µF capacitor (optional at this scale, but install it for practice) |
| ~ | Hookup wire, heat-shrink |
| 1 | Breadboard (~$5, get one anyway, useful for life) |
| 1 | USB cable for the ESP32 |

Skip the level shifter for the starter. WS2812B at short lengths often works fine on 3.3 V data directly — and if it doesn't, that's a teaching moment about why we add the level shifter for the main build.

#### Wiring

```
ESP32 USB  ─────────────  (powers everything)

ESP32 5V   ──┬──── Strip 5V
             │
         [1000 µF cap]
             │
ESP32 GND  ──┴──── Strip GND
                       │
                   Piezo (–)

ESP32 GPIO 5  ──[470 Ω]──── Strip DIN

ESP32 GPIO 32 ──┬──── Piezo (+)
                ├──── 1 MΩ ──── GND
                └──── Zener (cathode toward GPIO) ──── GND
```

This is identical to the main wiring diagram, just with one piezo channel and no level shifter.

#### Build Steps

**Session 1: Solder practice + LED strip prep (1–2 hours)**

- [ ] Set up your bench: iron heating, solder, helping hands, multimeter, scrap wire
- [ ] Warmup: 5 wire-to-wire solder joints on scrap. Inspect each. Don't proceed until they look good
- [ ] Cut a 30-LED segment from the strip (between LED pads, follow the cut lines printed on the strip)
- [ ] Tin the three pads at one end of the segment (5V, DIN, GND). Add solder until each pad has a small dome
- [ ] Strip ~4 mm of insulation off three short hookup wires (red for 5V, black for GND, any other color for data)
- [ ] Tin the wire ends
- [ ] Solder each wire to its pad. Touch iron to pad, melt the dome, slide wire in, hold steady, remove iron. ~2 seconds per joint
- [ ] Continuity test: probe each wire end against the matching pad on the strip. Should beep
- [ ] Heat-shrink each joint individually for strain relief

**Session 2: Build the input network and breadboard (30–60 min)**

- [ ] Solder leads to the piezo: red to (+), black to (–). Be **fast** — under 2 seconds per pad. Piezo discs lose sensitivity when overheated
- [ ] On a breadboard, lay out the circuit per the wiring above
- [ ] Place the ESP32 across the breadboard's center channel
- [ ] Use jumper wires for the ESP32-to-component connections
- [ ] The 1 MΩ resistor sits between GPIO 32 and GND
- [ ] The Zener sits between GPIO 32 and GND, with the **black band (cathode) toward GPIO 32**, body toward GND. Polarity matters
- [ ] Don't connect power yet

**Session 3: Test and tune (1 hour)**

- [ ] Visual check: walk the wiring against the schematic one more time
- [ ] Multimeter polarity check: with USB unplugged, probe ESP32 5V to GND with continuity — should NOT beep (no shorts)
- [ ] Plug in USB. The onboard ESP32 LED should light. If you smell anything or the board gets warm, unplug immediately and check for shorts
- [ ] Open Arduino IDE, load `tap_light.ino`, upload
- [ ] Strip should light up in the first mode (warm orange)
- [ ] Open Serial Monitor at 115200 baud
- [ ] Tap the piezo. Mode should advance, and a line should print to serial showing the peak reading
- [ ] If no response: tap harder, or lower `PIEZO_THRESHOLD` in the code and re-upload. Watch the readings to find a good value
- [ ] Cycle through all 6 modes by tapping. Confirm the rainbow mode looks right (each LED a different color)
- [ ] Unplug USB, plug back in. The strip should resume on whatever mode you left it on (state persists in NVS)

**Session 4 (optional): Move from breadboard to protoboard, add an enclosure**

If you want a finished thing rather than a breadboard prototype:

- [ ] Lay out the circuit on a small protoboard (5×7 cm)
- [ ] Use female headers for the ESP32 so you don't solder it directly
- [ ] Solder the resistor and Zener through-hole; trim leads with side cutters
- [ ] Run wires to the strip and piezo via JST connectors or solder direct
- [ ] Mount in a small project box, or hot-glue everything to a piece of scrap wood

### 0.6 Skills Check

If you can honestly check all of these, you're ready for the main build.

- [ ] My solder joints look shiny and properly wetted (not blobby or dull)
- [ ] I can solder a wire to an LED strip pad without lifting the pad
- [ ] I soldered a piezo without killing it
- [ ] I know how to test continuity with my multimeter and have done so on real circuits
- [ ] I successfully uploaded firmware to the ESP32 via USB
- [ ] I read serial output and used it to tune a parameter (the piezo threshold)
- [ ] I understand the per-piezo input network well enough that I could draw it from memory
- [ ] My Tap Light works end-to-end and persists state across power cycles

**If any of these are no**, repeat the relevant section before moving on. The main build has 8× the joints, mains wiring, and corner soldering on a strip mounted in aluminum channel — none of which is forgiving.

---

## 1. The Broad Plan

The main build splits into seven phases on top of Phase 0. Do them in order.

| Phase | What happens | Why it's here |
|-------|--------------|---------------|
| **0. Starter project** | Build the Tap Light (see §0) | Skills, bench setup, confidence |
| **1. Plan & gather** | Parts list ordered, work area set up | Nothing worse than getting halfway and realizing you're missing a Zener |
| **2. Bench prototype** | ESP32 + 1 LED segment + 1 piezo on a breadboard, firmware running | Proves the full firmware works before anything is permanent |
| **3. LED rim** | Cut, route, and mount the strip into aluminum channel around the octagon | The most visible part of the build. Take your time on the corners |
| **4. Piezo mounting** | Glue 8 piezos to the underside of the slab, run leads | Cross-talk between sides is inherent to a single slab; firmware filters it, but careful placement helps |
| **5. Control box** | ESP32 + level shifter + resistors + connectors on protoboard, in an enclosure | Everything terminates here. JST connectors so the table is serviceable |
| **6. Final assembly** | Mount PSU on the bumper pool frame, wire the Powerpole disconnect, dress wiring on the slab, mate and test | The slab/frame split (§4.6) is most of the work here |
| **7. Tune & test** | Dial in piezo threshold per side, calibrate brightness, play a real game | Real-world testing always reveals something the bench didn't |

**Estimated time**: 4–6 hours for Phase 0, plus 10–14 hours for Phases 1–7 spread over a weekend. Most of the main build is mechanical (channel cutting, mounting), not electronics.

---

## 2. Bill of Materials

Quantities below cover the **main project**. The starter project (Phase 0) uses a subset — see §0.5. Order everything together; the per-unit prices on extras are negligible and you'll appreciate having spares.

| Qty | Part | Specifics | Notes |
|----:|------|-----------|-------|
| 1 | ESP32 dev board | ESP32-WROOM-32, USB-C preferred | Buy 2 — one for the starter, one for the main. They're $8 |
| 5 m | WS2812B LED strip | 60 LEDs/m, IP30, 5V | 4 m used; the extra is for the starter and mistakes |
| 4 m | Aluminum LED channel | With frosted/diffuser cover | Massively improves the look |
| 10 | Piezo disc | 27 mm brass, 2 leads | Bag of 10 covers main + starter + spares |
| 12 | Resistor | 1 MΩ, 1/4 W | Pulldown across each piezo. Buy a 100-pack, they're nothing |
| 12 | Zener diode | 3.3 V, 500 mW (1N4728A) | ADC overvoltage clamp |
| 5 | Resistor | 470 Ω, 1/4 W | Series resistor on data line |
| 3 | Capacitor | 1000 µF, 10 V or higher, electrolytic | Across 5V/GND at strip start |
| 2 | Level shifter | 74AHCT125 (DIP-14) | 3.3 V → 5 V data line |
| 1 | Power supply | Mean Well LRS-50-5 (5 V, 10 A) | Don't cheap out here |
| 1 | Power switch | Panel-mount rocker, 250 V AC rated, 6 A+ | Or use a switched IEC inlet (safer) |
| 1 | DC barrel jack or terminal block | For PSU output | Personal preference |
| ~ | Hookup wire | 22 AWG stranded for signal, 18 AWG for power | A few colors helps |
| 12 | JST-XH 2-pin connector pairs | For piezo leads | Makes service easy |
| 2 | JST-XH 3-pin connector pair | For LED strip data + ground | Detachable rim |
| 2 | Terminal blocks, 2-pin | For 5V/GND power injection points | Or solder direct |
| 1 | Project box | ~100×60×30 mm | See §4 for mounting options |
| 2 | Protoboard | 5×7 cm or similar | One for starter, one for main |
| 1 | Inline fuse | 5 A automotive blade fuse + holder | Between PSU 5V output and Powerpole pigtail |
| 1 | Breadboard | Standard half-size | For prototyping. Useful forever |
| | | | |
| | **Removable-top installation (§4.6)** | | |
| 2 | Anderson Powerpole 30 A connector kit | Pair of red + black housings + crimp contacts | One pair for the slab, one pair for a bench-test pigtail |
| 1 | Powerpole crimp tool | TRIcrimp (~$30) | Or use a ratcheting crimper carefully. One-time tool cost, reusable forever |
| 6 ft | Silicone-insulated wire | 14 AWG, red and black (3 ft each) | DC run from PSU to slab |
| 1 | Rubber grommet | 1/2" inside diameter | Cable entry through slab |
| 2 | P-clips or adhesive cable mounts | Various sizes | Cable strain relief inside slab |
| 1 | Pine block or scrap | ~4×6×1" | PSU mounting platform on frame |
| ~ | #8 wood screws | 3/4" and 1.5" lengths | Mount block to frame and PSU to block |

**Total cost**: roughly $80–$120 for parts, plus $80–$150 for tools if you don't have them. Tools are a one-time investment that'll serve every future project.

**Where to source**: Strip and channel from BTF-Lighting or Adafruit. Piezos, resistors, Zeners, level shifter from any electronics supplier (DigiKey/Mouser if you want quality, Amazon if you want speed). PSU from Mean Well's authorized resellers — there are a lot of fakes on Amazon.

**Power switch wiring note**: the rocker switch goes on the **AC mains side**, between the wall plug and the PSU's L (live) input. Wire L through the switch, and N (neutral) and Earth direct. That way nothing downstream is ever live with the switch off. If you're not comfortable with mains wiring, the alternative is a switched IEC inlet — they exist as a single panel-mount unit with built-in fuse holder, switch, and IEC C14 socket. Slightly more expensive but much safer to wire, and a good choice for a first electronics project.

---

## 3. Electrical Design

> **See `turn_counter_wiring.svg` for the full schematic.** This section summarizes; the SVG is authoritative.

### 3.1 Pinout

| ESP32 GPIO | Function | Notes |
|-----------:|----------|-------|
| 5 | LED data out (→ 470 Ω → level shifter → strip) | |
| 32 | Piezo side 1 ADC | |
| 33 | Piezo side 2 ADC | |
| 34 | Piezo side 3 ADC | Input-only pin |
| 35 | Piezo side 4 ADC | Input-only pin |
| 36 | Piezo side 5 ADC | Input-only pin (VP) |
| 39 | Piezo side 6 ADC | Input-only pin (VN) |
| 25 | Piezo side 7 ADC | |
| 26 | Piezo side 8 ADC | |

Avoid GPIO 0, 2, 12, 15 — these are strapping pins and pulling them at boot can mess with the boot mode.

### 3.2 Per-piezo circuit (×8)

```
                   ADC pin (e.g. GPIO 32)
                         │
            ┌────────────┼────────────┐
            │            │            │
         Piezo (+)    1 MΩ         Zener
            │            │       (cathode up)
         Piezo (–)       │            │
            │            │            │
           GND──────────GND──────────GND
```

The 1 MΩ resistor pulls the ADC line down so it doesn't float. The Zener clamps spikes — a hard slap on a piezo can produce 20+ volts momentarily and the ESP32 ADC tops out at 3.3 V.

### 3.3 LED strip + power

Three injection points along the strip — start, middle, and end — each tapping the +5V and GND rails on the slab. This prevents voltage droop along the 4 m run. Note that those rails on the slab come from the PSU on the bumper pool frame via the Powerpole DC disconnect; see §4.6 for the full slab/frame architecture. The schematic shows the electrical connections; the disconnect itself is a physical break in the +5V/GND wiring between PSU and slab.

The 74AHCT125 level shifter takes the ESP32's 3.3 V data signal and bumps it to 5 V. Tie its OE pin (and any unused channel OE pins) to GND. Power VCC from +5V rail.

A 1000 µF cap across the 5V/GND rails right where the strip starts. Acts as a local energy reservoir for sudden current draw.

An inline 5 A fuse between PSU 5V output and the rest of the system. If something shorts, the fuse blows instead of the strip.

### 3.4 Power budget sanity check

- 240 LEDs × 60 mA worst case (full white) = 14.4 A
- One side (30 LEDs) lit at 50% brightness, single color = ~0.5 A
- Idle (lights off, ESP32 only) = ~50 mA without Wi-Fi, ~80–120 mA with Wi-Fi up for OTA

A 10 A PSU is comfortable. We'll never light more than ~30 LEDs at once during normal play.

---

## 4. Mechanical Design

### 4.1 LED channel layout

The aluminum LED channel mounts to the **outer edge of the slab**, running around the entire octagonal perimeter. The diffuser faces outward so the glow is visible from any seat at the table.

[RIM_SECTION_FIGURE]

The slab is thin (~1") and rests on the bumper pool frame with air space between, which means it vibrates freely. This is exactly what lets piezos on the underside register taps from anywhere on the slab.

Alternative: if you prefer a recessed look, route a 1/2"-wide × 1/4"-deep rabbet into the outer edge of the slab and recess the channel flush. More work, looks cleaner. Your call.

### 4.2 Corner handling

8 sides means 8 closed-loop interior corners. At each corner:

- **Cut the channel** at 22.5° miters with a hacksaw or chop saw. The angle for a regular octagon is 135° interior, so each piece gets a 22.5° cut on each end.
- **Cut the LED strip** between LED pads at the corner. Solder short jumper wires (3-conductor: 5V, GND, Data) to bridge across the corner gap. Heat-shrink the joints.
- **LED count per side**: at 60 LEDs/m, a 20" (508 mm) side fits ~30 LEDs. Cut cleanly between pads, you'll lose maybe 1–2 LEDs in the corner gaps.

### 4.3 Piezo placement

One piezo glued centered on the **underside of the slab**, one for each player's section (8 total). Brass face against the wood, leads soldered before mounting.

Layout: divide the underside of the octagonal slab into 8 wedge-shaped sections matching the 8 sides above. Place each piezo at roughly the centroid of its wedge — about 3–4" inward from the outer edge. Avoid placing them where the slab rests on the bumper pool frame; a piezo at a clamped point won't vibrate freely.

The thin slab + air-gap-below configuration is excellent for piezo coupling. Vibration from a tap anywhere on a player's section transmits efficiently to the piezo directly underneath. The bare top means nothing damps the slab from above either.

**Mounting**: hot glue is fine for prototyping. For permanent installation use cyanoacrylate (super glue) or a thin layer of epoxy. Press flat, hold 30 seconds. Don't crush the disc — even pressure across the whole face.

**Wire routing**: 22 AWG stranded leads from each piezo, soldered to the disc terminals (be quick — piezos are heat-sensitive), routed along the slab underside toward the control box. Secure with adhesive cable mounts every 6". From there to JST-XH connectors.

### 4.4 Cross-talk mitigation

Because the whole slab is one continuous piece of wood, every tap reaches all 8 piezos to some degree. The piezo directly under the tap reads strongest; piezos on adjacent sections read moderately; piezos on the opposite side read very weakly.

**First-line mitigation**: the firmware already picks the *strongest* reading among all sides, so cross-talk shows up as background noise and the strongest hit wins. This handles most cases.

**If you're getting false-side detection** (a tap on side 1 occasionally registers as side 2):
- Raise `PIEZO_THRESHOLD` so weak cross-talk doesn't even cross the threshold to begin with
- If that's not enough, modify `readPiezos()` to require the strongest reading to be at least 2× the second-strongest before accepting it as a real tap

**On the on/off gesture specifically**: this configuration is *especially good* for opposite-side detection. Cross-talk falls off with distance through the slab, and opposite sides are physically as far apart as possible. A single hard tap will never produce strong readings on opposite piezos — the slab simply doesn't transmit cross-talk that far at strong amplitudes. So "two strong readings exactly 4 sides apart" is unambiguously a deliberate two-handed slap.

### 4.5 Control box: enclosure options

Three choices, ranked from least to most effort:

**Option A — Project box, surface-mounted under the slab** (default).
A small ABS or aluminum project box (~100×60×30 mm) screws to the underside of the slab. Cable glands or grommets for entries. Cheapest, fastest, easiest to service. Slight visual penalty if you ever flip the slab over.

**Option B — Recessed pocket in the slab underside**.
Route a rectangular pocket into the underside of the slab, sized to fit the protoboard + ESP32 + connectors. Cover plate (wood or thin metal) screws over it. Flush, invisible from below, but you have to commit to a location and dimension before routing — and any future hardware change means another router pass. Pay attention to slab thickness: with a ~1" slab and a typical protoboard depth of ~10–15 mm, you have very little wood left between the pocket bottom and the top surface. Best if you're confident in the design and the protoboard is thin enough.

**Option C — External enclosure on the bumper pool frame**.
If you don't mind a small box mounted to the frame underneath rather than on the slab, you can keep the slab clean. Trade-off: now you have signal cables (8 piezo lines + 1 strip data line) crossing the disconnect along with power, which means a much larger multi-pin connector instead of the simple Powerpole DC pair. Not recommended for this build, but worth knowing it's an option if the slab needs to be perfectly minimal.

Whichever you pick:
- Leave room for **airflow** around any heat-generating components.
- Provide a **service access path** for the ESP32 USB port, even if you primarily flash via OTA. You may need wired serial for debugging.
- Make sure the **power switch** (on the frame, not the slab) is reachable without crouching all the way under the table.

### 4.6 Installation: removable top

The gaming table sits on top of an existing bumper pool table, and the top slab needs to be liftable. The build splits into two parts that connect via a single **DC quick-disconnect**. Mains AC stays put on the frame; only +5V DC crosses the joint.

[INSTALLATION_ARCH_FIGURE]

**Why DC and not AC at the disconnect**:
- *Safety*. AC mains at the disconnect point means a panel-mount connector that's potentially live every time you reach for it. Disconnecting mains at a casual touchpoint is a nope.
- *Practicality*. AC mains-rated panel connectors are bulky, expensive, and require strict polarity and earth bonding. Comparable DC connectors are cheap, robust, and idiot-proof.

**What lives where**:

*Slab (top, removable)*: LED strip + channel, all 8 piezos, ESP32 control box, level shifter, all signal wiring, and a DC pigtail terminating in a Powerpole connector.

*Frame (bottom, permanent)*: PSU, switch or IEC inlet, AC cord to wall, inline 5A fuse, DC pigtail terminating in the mating Powerpole connector.

**The disconnect itself**: **Anderson Powerpole 30A** connectors. They're polarized (can't connect backwards), genderless (the same housing mates with itself), rated comfortably above the 10A budget, and click together solidly. Red + black housing pairs are about $5; a proper TRIcrimp tool is ~$30 (or use ratcheting pliers carefully). XT60 connectors (RC vehicle world) are $2/pair and electrically equivalent, but the exposed bullet pins on the disconnected end aren't great if anyone ever pokes at them. Powerpole contacts are recessed.

**Cable**: 14 AWG silicone-insulated wire is overkill for 10A but stays flexible at the connector and is pleasant to work with. 16 AWG is the practical minimum. Run twisted pairs (red 5V + black GND) for tidy routing. Length: PSU location to slab + 18" of slack so you can lift the slab a foot before having to disconnect.

**Slab cable entry**: Drill a 1/2" hole through the slab (edge or underside, depending on routing). Install a rubber grommet to protect the cable. Inside the slab, secure the cable with a P-clip or adhesive cable mount within 4 inches of the entry — any pull on the cord should hit the clip, not the connector terminations.

**PSU mounting**: The Mean Well's mounting flanges screw to a small wood block (~4×6×1"), which then screws to the bumper pool frame underside in a ventilated location. Don't seal it inside an enclosed box — these run warm and need airflow.

**Bench testing**: When the slab is off the table, the lights have no power. Keep a spare Powerpole pigtail wired to a 5V/3A bench supply — plug into the slab's connector for moderate-brightness operation away from the table. Not enough current to drive the strip hard, but plenty for testing.

---

## 5. Phase-by-Phase Build Steps

### Phase 1: Plan & Gather

- [ ] Phase 0 complete and Tap Light works (see §0)
- [ ] Confirm BOM and order remaining parts
- [ ] Print this doc and the wiring diagram (`turn_counter_wiring.svg`), keep them on the bench
- [ ] Measure the actual slab: confirm it's ~1" thick, all 8 sides equal length, lies flat on the bumper pool frame
- [ ] Identify a ventilated mounting location on the underside of the bumper pool frame for the PSU (and note where the AC outlet is relative to it)
- [ ] Plan the cable run from PSU → Powerpole → up to the slab. Mark roughly where the slab cable will enter
- [ ] Confirm you have wall outlet access within reach of the planned PSU location, ideally on a switchable surge protector

### Phase 2: Bench Prototype

- [ ] Wire ESP32 + 30-LED test segment + 1 piezo on breadboard per the wiring diagram
- [ ] Flash the main firmware, with `NUM_LEDS = 30` and `NUM_SIDES = 1` temporarily
- [ ] Open Serial Monitor at 115200 baud
- [ ] Add a temporary `if (analogRead(PIEZO_PINS[0]) > 100) Serial.println(analogRead(PIEZO_PINS[0]));` inside `loop()` and tap the piezo — the threshold of 100 keeps the serial output quiet at idle and only prints when something interesting happens
- [ ] Note the reading at rest (probably 0–50) and the peak when tapped (likely 500–3000+)
- [ ] Set `PIEZO_THRESHOLD` to ~30% of peak — adjust by feel
- [ ] Confirm the LEDs change color/zone on tap before proceeding
- [ ] Test power-cycle persistence: tap a few times, unplug, replug, verify state restored
- [ ] If using OTA: configure Wi-Fi credentials in firmware, confirm device appears on the network and can be reached via `ping turn-counter.local`

**Don't move forward until this works reliably.**

### Phase 3: LED Rim

- [ ] Measure all 8 sides — confirm they're actually 20", not 19.875" or something
- [ ] Cut aluminum channel: 8 pieces with 22.5° miters at each end
- [ ] Pre-fit the channel dry around the slab's outer edge, mark mounting screw locations
- [ ] Cut LED strip into 8 segments, ~30 LEDs each, between pad gaps
- [ ] Lay strips into channels, peel adhesive backing
- [ ] Solder jumper wires at each corner (3-conductor: 5V, GND, Data); heat-shrink each joint
- [ ] Test continuity: data line from start to end of the assembled strip with multimeter
- [ ] Mount channel pieces to the **outer edge of the slab** with screws (or recessed in a routed rabbet — see §4.1)
- [ ] **Count the actual installed LEDs** (most likely 232–240 depending on corner cuts) and update `NUM_LEDS` in firmware to match. Also recompute `LEDS_PER_SIDE = NUM_LEDS / 8` and round to the nearest integer
- [ ] Solder pigtails for power injection at three points along the strip: at the start (DIN end), at the corner roughly halfway around (the corner between the 4th and 5th side as the strip runs), and at the end of the strip. Each pigtail joins +5V and GND from the slab's main DC rails to the strip's 5V and GND pads

### Phase 4: Piezo Mounting

- [ ] Solder leads to all 8 piezos (red to +, black to –). Quick — under 2 seconds per pad
- [ ] Test each piezo before mounting: connect to multimeter on AC voltage, tap it, expect a brief swing
- [ ] Crimp JST-XH connectors onto piezo leads
- [ ] On the slab underside, mark 8 piezo locations: one per wedge, ~3–4" inward from each outer edge, avoiding any spot where the slab rests on the pool table frame
- [ ] Glue each piezo at its mark, brass face against wood
- [ ] Route piezo cables along the slab underside toward where the control box will live; secure with adhesive cable mounts every 6"

### Phase 5: Control Box

- [ ] Build the protoboard: ESP32 socket (use female headers, don't solder it directly), level shifter, 8× pulldown resistors, 8× Zeners, 470 Ω data resistor, 1000 µF cap
- [ ] Add 8× JST-XH 2-pin headers for piezo inputs
- [ ] Add 1× JST-XH 3-pin header for strip data + ground reference
- [ ] Add 2× screw terminals for 5V/GND from PSU
- [ ] Test before installing: power up with the bench-test pigtail (see §4.6) connected to a 5V supply — either a small bench supply, or a USB-C wall adapter with a USB-C-to-Powerpole adapter cable, or even just the ESP32's USB power if you only need to verify the firmware logic without driving the LED strip — then run firmware and tap piezo leads with a screwdriver to fake hits
- [ ] Mount protoboard inside the chosen enclosure (see §4.5 — typically a project box under the slab); cut cable entries for power and signal

### Phase 6: Final Assembly

The build splits into frame-side and slab-side work — see §4.6 for the architecture.

**Phase 6a — Frame side (permanent installation)**

- [ ] Mount Mean Well PSU to the wood block with #8 screws through its mounting flanges
- [ ] Screw the wood block to the underside of the bumper pool frame, in a ventilated location
- [ ] Wire AC mains. Two configurations to choose from: (a) wall plug → separate panel-mount rocker switch → PSU's L (live) terminal, with N (neutral) and Earth direct from wall to PSU; or (b) a switched IEC inlet that combines plug socket, fuse, and switch in one unit, wired directly to PSU's L/N/Earth. Option (b) is preferred for a first electronics project — fewer mains-rated joints to make
- [ ] Wire PSU's +V output through the inline 5 A fuse to the +V contact of a Powerpole housing; PSU's −V direct to the −V contact
- [ ] Crimp Powerpole contacts onto silicone wire ends; insert into housings (red = +V, black = GND); slide the two housings together so they're locked
- [ ] Leave the cable long enough to reach the slab's connector with ~18" of slack
- [ ] First power test (frame only, no slab connected): switch on, multimeter the Powerpole, expect +5V between red and black
- [ ] Switch off before continuing

**Phase 6b — Slab side (mobile assembly)**

- [ ] Drill a 1/2" hole near the slab edge for the cable; install the rubber grommet
- [ ] Run the slab-side DC cable through the grommet into the slab
- [ ] Crimp Powerpole contacts onto the slab-side cable ends; insert into mating housings (the genderless design means it's the same housing as the frame side)
- [ ] Inside the slab: branch from the slab-side DC cable to the control box's 5V/GND terminals AND to the 3 strip injection points (start, middle, end of strip). Branching method: solder all incoming wires into a small set of WAGO-style lever connectors (cleanest), or twist-and-solder them with heat-shrink (works fine but harder to service). The BOM's "Terminal blocks, 2-pin" can serve as the strip-side termination at each injection point
- [ ] **Verify polarity at every junction with a multimeter before connecting.** Reversed 5V instantly kills the strip
- [ ] Secure the cable with a P-clip or adhesive cable mount within 4" of the grommet — strain relief should hit the clip, not the connector
- [ ] Connect all 8 piezo JST connectors to the control box
- [ ] Connect the strip data + ground JST
- [ ] Mount the control box; dress all wiring with cable clips; nothing dangling

**Phase 6c — Mate and test**

- [ ] **Slab bench test (before connecting to the frame)**: with the slab on a workbench (sawhorses or table), connect the bench-test Powerpole pigtail to the slab's connector and to a 5V supply. Confirm the strip lights up and the firmware advances on tap. If anything's wrong, fix it now while access is easy
- [ ] Disconnect the bench supply
- [ ] Mate the slab Powerpole to the frame Powerpole
- [ ] First full power-up: switch on, watch for smoke (good sign: no smoke)
- [ ] Confirm all 8 sides light correctly when you cycle through manually
- [ ] Test the disconnect: switch off → unmate Powerpole → lift the slab a foot → set it back → reconnect Powerpole → switch on → confirm everything works

### Phase 7: Tune & Test

- [ ] Re-flash with `Serial.println` instrumentation enabled (over OTA if Wi-Fi is set up, or USB). For calibration, temporarily add a print of the raw `analogRead` values for *every* side that exceeds a low threshold (say, 100) — this captures both direct hits and cross-talk so you can see the cross-talk geometry
- [ ] Open the Serial Monitor (or for OTA, a network log viewer) and keep it visible while you tap
- [ ] **For the per-side reading test**: temporarily set the firmware to single-player mode (8 sides as one zone) so the active-side rule doesn't filter taps. Tap each side firmly and note peak readings — they should be roughly similar across all 8
- [ ] If any side reads much lower, check the glue contact and lead solder joints
- [ ] Set `PIEZO_THRESHOLD` to the value that ignores incidental table bumps but catches deliberate taps (typically ~30% of the peak reading)
- [ ] Restore the default player count (or whatever you intend to play with)
- [ ] **Cross-talk test**: tap side 1 hard, look at the serial output. The strongest reading should be side 1; sides 2 and 8 (adjacent) will show smaller readings; side 5 (opposite) should be near-zero. Confirm the firmware identifies the hit as side 1 (look for "Tap on side N" or the lit-zone advance, depending on which player is active). If the firmware ever picks a non-adjacent side as the strongest, raise threshold or add a relative-strength check (see §4.4)
- [ ] **On/off gesture test**: with two hands, slap two opposite sides simultaneously. Lights should toggle. If no toggle, check that both piezos register above threshold; raise `OPPOSITE_PAIR_WINDOW_MS` if your slap timing isn't quite synchronized
- [ ] **Setup gesture test**: rapidly tap 4 times within 2 seconds. Strip should start blinking. Tap once more to cycle player count. Wait 3 seconds; strip resumes normal play at player 1
- [ ] Set `BRIGHTNESS` to your preferred level (start at 128, adjust)
- [ ] Play an actual game. Take notes on anything weird

---

## 6. Controls & Setup Mode

Once installed, the user-facing controls are:

| Action | Result |
|--------|--------|
| Tap your **own** (lit) side during play | Advance turn to next player |
| Tap a side that *isn't* lit | Nothing — only the active player can pass turn |
| Tap 4 times within 2 seconds (any sides — the gesture counts taps, not sides) | Enter setup mode (sides flash) |
| Tap two **opposite** sides simultaneously (a two-handed slap, on sides directly across from each other) | Toggle on/off |
| In setup: tap any side | Cycle player count (2 → 3 → … → 8 → 2) |
| In setup: stop tapping for 3 s | Save player count, exit setup, reset to player 1 |
| Power cycle | Resume on the same player and same on/off state (state persists) |

Number of *blinking* sides = current player count selection.

**The active-side rule**: turn advance only triggers when the tapped side belongs to the currently lit zone. If player 3 is active, only taps within player 3's wedge advance the turn. This means players can't accidentally (or deliberately) advance someone else's turn by tapping their own section while waiting. It also reinforces "look at the lights to know whose turn it is" as the natural way to play. Non-active-side taps are silently ignored — no flash, no sound, the lit zone stays put.

This rule applies only to turn advance. Setup-mode entry and the on/off gesture work from any side.

**Why the entry gesture isn't a "hold"**: piezos detect vibration, not pressure. They produce a brief voltage spike when struck and then return to baseline — there's no signal to read while you keep your hand there. So setup entry uses a rapid tap burst instead. The 4 taps will spin the lit zone forward once on the first tap (since that one happens to be on the active side); the next three taps land on now-inactive sides and only register for the gesture counter, not the turn. Exiting setup mode resets to player 1 anyway.

**The on/off gesture in detail**: any two-handed slap on diametrically opposite sides toggles the LEDs. The firmware buffers each tap for 150 ms before committing it as a turn advance — long enough to detect a near-simultaneous opposite tap, short enough that the resulting delay on normal turn passes is barely perceptible. Off state is fully dark; the device still polls the piezos and watches for the on-gesture, but ignores everything else. The 150 ms buffer also handles the asymmetric-strike case where one hand lands a few milliseconds before the other.

**Why opposite sides specifically**: cross-talk through the wooden slab falls off with distance. Adjacent piezos (sides 1 + 2) might both register from a single hard hit on side 1. Opposite piezos (sides 1 + 5) physically can't — they're as far apart as possible in the slab, so two strong simultaneous readings on opposites can only come from a deliberate two-handed slap.

---

## 7. Firmware

The main firmware is in `turn_counter.ino`. The Phase 0 starter firmware is in `tap_light.ino`. Key tunables at the top of the main file:

| Constant | Default | Tune to… |
|----------|---------|----------|
| `NUM_LEDS` | 240 | Actual LED count after corner trim |
| `LEDS_PER_SIDE` | 30 | `NUM_LEDS / 8` |
| `BRIGHTNESS` | 128 | Lower if too bright, max 255 |
| `PIEZO_THRESHOLD` | 400 | Calibrate during Phase 7 |
| `DEBOUNCE_MS` | 250 | Raise if double-triggers, lower if it feels sluggish |
| `SETUP_TAP_COUNT` | 4 | Taps required to enter setup mode |
| `SETUP_TAP_WINDOW_MS` | 2000 | Window in which the taps must occur |
| `SETUP_EXIT_IDLE_MS` | 3000 | Idle time in setup mode before saving and exiting |
| `OPPOSITE_PAIR_WINDOW_MS` | 150 | Window for detecting the on/off two-handed gesture; raise to make it more forgiving, lower for snappier turn passes |
| `WIFI_SSID` / `WIFI_PASSWORD` | placeholder | Your home Wi-Fi (for OTA) |
| `OTA_HOSTNAME` | `turn-counter` | mDNS name; reach device at `turn-counter.local` |
| `OTA_PASSWORD` | `change-me` | Required to push firmware updates |

`PLAYER_COLORS[]` array defines the color for each player — edit to taste.

**Build environment**: Arduino IDE with ESP32 board support, FastLED library installed (ArduinoOTA is bundled with the ESP32 core). Board: "ESP32 Dev Module". Upload speed: 921600.

### 7.1 OTA firmware updates

The device tries to join Wi-Fi at boot with a 5-second timeout. If it joins, OTA is available; if not, it just runs locally — the device works fine without a network.

**To push an update from Arduino IDE**:
1. With the device powered and on the same network as your computer, the IDE's "Port" menu will list `turn-counter at 192.168.x.x` (or whatever the mDNS resolves to) under "Network Ports".
2. Select that port, hit Upload, enter the OTA password when prompted.
3. The strip goes dark, then a blue progress bar fills around the rim as the update transfers.
4. On success, all LEDs flash green briefly. On failure, all red for 2 seconds.
5. Device reboots into the new firmware.

**Security note**: anyone on your local network can attempt to flash the device. The OTA password protects against accidental or casual attacks but isn't strong security. Don't put this on a network with untrusted devices, and definitely don't expose it to the internet.

**If OTA breaks** (most common cause: pushing firmware that crashes immediately and loses Wi-Fi): you can always fall back to USB. Plug into the ESP32, hit Upload, done. Keep a USB cable accessible.

---

## 8. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| LEDs flicker or first LED is wrong color | Missing or weak data signal | Confirm 470 Ω resistor + level shifter + common ground |
| Far-end LEDs tint pink/orange | Voltage drop along strip | Add or check power injection at midpoint and end |
| Whole strip dark | PSU off, switch off, blown fuse, or reversed polarity | Check switch first, then fuse, then PSU output, then polarity |
| Tap doesn't register | Threshold too high, bad piezo solder, glue not contacting wood | Lower threshold, reflow joint, re-glue |
| Tap on side 1 lights side 3 | Piezo wire mapping wrong | Check `PIEZO_PINS[]` order vs physical wiring |
| Adjacent sides cross-trigger | Mechanical cross-talk through wood | Foam break, kerf cut, or relative-strength filter in firmware |
| Weird boot behavior | Strapping pin pulled wrong | Confirm GPIO 0/2/12/15 unused |
| ESP32 won't flash via USB | Upload speed, missing driver, or held button | Drop to 115200 baud, install CP210x or CH340 driver, hold BOOT during upload |
| OTA port doesn't appear in IDE | Wi-Fi didn't join, wrong network, mDNS not resolving | Check Serial Monitor at boot for IP; ping `turn-counter.local`; fall back to USB |
| OTA fails midway | Network drop or insufficient flash | Retry; consider partition scheme with larger OTA region |
| Player count resets unexpectedly | NVS partition issue | Erase flash and re-flash: `esptool.py erase_flash` |
| Solder joint won't take | Tip too cold, no flux, dirty surface | Increase iron temp, add flux pen, clean the surface with isopropyl |
| Solder joint looks dull / blobby | Cold joint — moved while cooling, or insufficient heat | Reflow with flux, hold steady ~2 sec while cooling |

---

## 9. Future Enhancements

If the basic build lands well, things worth considering later:

- **Turn timer**: each player has N seconds; the lit zone slowly drains around the side as time runs out. Tap before it empties or it auto-advances with a red flash.
- **Companion app**: ESP32 already has Wi-Fi up for OTA. Run a small web server, expose a phone-friendly page for round counting, scoring, initiative tracking. Especially useful for RPGs.
- **Audio feedback**: a small piezo *buzzer* (different from the sensor piezos) on a free GPIO for soft turn-pass clicks.
- **Per-game profiles**: store named configurations in NVS. "Magic 4-player", "RPG initiative for 6", etc.
- **Initiative mode**: instead of round-robin, player order is a stored sequence. Useful for D&D-style turn order that isn't seat-based.
- **Color customization**: the companion app could let players pick their own colors before the game starts.
- **Round counter**: subtle flash or pulse when a full round completes. Easy to add given current architecture.

---

## 10. Quick Reference Card

Keep this part handy at the table:

> **To pass turn**: tap the lit section in front of you. Only the active player can advance.
> **To turn on/off**: slap any two opposite sides of the table at the same time (a two-handed gesture: left hand on one side, right hand on the side directly across).
> **To change player count**: rapid-tap the table 4 times within 2 seconds. The lights will start blinking. Then tap to cycle through counts. Stop tapping for 3 seconds to save.
> **To reset to player 1**: enter setup mode and exit (count stays the same, turn resets).
> **State persists across power cycles** — including on/off, current player, and player count.

---

*End of document.*
