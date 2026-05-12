# Shopping List

Concrete parts list for the build. DigiKey-first sourcing for authenticity (especially the ESP32-S3, the Mean Well PSU, and the 74AHCT125 — all of which have counterfeits floating around on Amazon).

> **Manufacturer PNs are listed below.** They're stable; DigiKey stock numbers can change. Search DigiKey by the manufacturer PN to find the current stock SKU.

## Vendor strategy

| Vendor | Why |
|--------|-----|
| **DigiKey** | ~75% of the BOM. Free ground shipping ≥ $100. Authentic ICs/PSU/connectors. |
| **Amazon** | Four items: LED strip, aluminum LED channel, JST connector kit, and 14 AWG silicone wire (DigiKey-grade Alpha Wire is ~2× the price for the same thing). |
| **Hardware store** | Wood block, screws, P-clips — cheaper and same-day. |

## On "bundles"

DigiKey doesn't bundle parts kits the way Amazon does. Two workarounds:

- **Single-value bulk packs**: 100× of one resistor or 25× of one Zener is usually cheaper per piece than Amazon assortment kits. Used below for resistors, Zeners, and caps.
- **Manufacturer assortments**: Adafruit "Parts Pal" (~$20) covers most common resistors / caps / diodes / LEDs in one box. Worth it if you want spares for future projects.

---

## DigiKey order (search by manufacturer PN)

### Active components

| Qty | Part | Manufacturer PN | ~$ ea | Notes |
|----:|------|-----------------|------:|-------|
| 2 | ESP32-S3-DevKitC-1, N8R8 | Espressif `ESP32-S3-DEVKITC-1-N8R8` (PCB antenna — **not** the `-1U-N8R8` external-antenna variant) | $15 | N8 and N8R2 are end-of-life at DigiKey; N8R8 is what's stocked. Octal PSRAM uses GPIOs 35/36/37 internally — fine for this build (the pinout doesn't touch those), just don't reassign anything to those pins. Two boards lets the Phase 0 Tap Light survive as a permanent desk lamp. |
| 2 | Level shifter, DIP-14 | TI `SN74AHCT125N` | $0.85 | One spare is cheap insurance. |
| 1 | 5V / 18A PSU | Mean Well `LRS-100-5` | $18 | Authentic Mean Well — Amazon is full of counterfeits at this price point. LRS-50-5 was the original spec; substituted to LRS-100-5 when the -50 was out of stock at DigiKey. Same series, bigger footprint (~129×97 mm vs 99×82 mm — wood block is still plenty), more headroom. The 5A inline fuse is *more* important now since the PSU itself won't trip on a downstream short. |
| 1 | Switched IEC C14 inlet w/ fuse holder | Schurter `DG12` series — search "Schurter DG12 fused IEC C14 switch" | $20 | Combines plug, switch, and fuse holder in one panel-mount. Far safer for a first electronics project than a discrete rocker switch. |

**Active components subtotal: ~$70.**

### Passives — single-value bulk packs

| Qty | Part | Manufacturer PN | Order qty | ~$ |
|----:|------|-----------------|----------:|---:|
| 12 | 1 MΩ, 1/4 W axial resistor | Yageo `CFR-25JT-52-1M0` | 100-pack | $1.50 |
| 5 | 470 Ω, 1/4 W axial resistor | Yageo `CFR-25JT-52-470R` | 100-pack | $1.50 |
| 12 | 1N4728A 3.3 V Zener diode | ON Semi `1N4728ATR` (or Vishay equivalent) | 25-pack | $4 |
| 5 | 1000 µF, 50 V electrolytic capacitor | Panasonic `ECA-1HM102` (DigiKey P5186-ND) | individually (~$1.45 ea) | $7.25 |

**Passives subtotal: ~$14.** Buying 100-packs feels excessive but they're $1.50 a pack — cheaper *per piece* than Amazon assortment kits, and you get authentic parts.

### Connectors

JST-XH connectors come from the Amazon kit. Powerpoles + terminal blocks from DigiKey:

| Qty | Part | Manufacturer PN | ~$ |
|----:|------|-----------------|---:|
| 3 | Anderson Powerpole 30 A housing, red | Anderson `1327G6` | $0.80 ea |
| 3 | Anderson Powerpole 30 A housing, black | Anderson `1327G7` | $0.80 ea |
| 6 | Anderson 30 A contact | Anderson `1331` | $0.80 ea |

> Three Powerpole housing-pairs total: one on the slab, one on the frame side it mates with, one for the bench-test pigtail. (Earlier draft listed 4 pairs; corrected.)
>
> Screw-terminal blocks were dropped — the On Shore Tech OSTYK225/OSTYK223 barrier strips were both out of stock at DigiKey, and WAGO 221 lever connectors (now in the Amazon order) are the cleaner choice for the branching network anyway. See the design doc §4.6 — WAGOs were already the doc's preferred option.

**Connectors subtotal: ~$9.60.**

### Mechanical

| Qty | Part | Manufacturer PN | ~$ |
|----:|------|-----------------|---:|
| 10 | 27 mm piezo disc | Murata `7BB-27-4L0` (or PUI Audio `AB2720B-LW100-R`) | $1.20 ea |
| 1 | Project box, ABS, ~110×60×30 mm | Hammond `1591BSBK` | $9 |
| 1 | Half-size breadboard, 400 tie-point | BPS `BB400` (or Twin Industries `400-pt`) | $5 |
| 2 | Protoboard, ~5×7 cm, 0.1" pitch | Adafruit `1609` (Perma-Proto) or generic | $4 ea |
| 1 | Inline blade-fuse holder + 5 A blade fuse | Littlefuse `0FHM0001ZXJ` + `0287005.PXCN` | $4 |

**Mechanical subtotal: ~$38.** (BOM only needs 2 protoboards — one for starter, one for main; earlier draft had 5.)

### Wire & consumables

| Qty | Part | Manufacturer PN | ~$ |
|----:|------|-----------------|---:|
| 25 ft (6 colors, mixed) | 22 AWG stranded hookup wire | Adafruit `1311` (6-color spool set) | $16 |

**Wire & consumables subtotal: ~$16.** (14 AWG silicone wire moved to Amazon — DigiKey's Alpha Wire is ~$30 for the same length BNTECHGO sells for $16. Rubber grommet for slab cable entry moved to the hardware store — the original Heyco BOM part `2092` is designed for 3 mm sheet-metal panels, not 1″ wood.)

---

### **DigiKey total: ~$148**

Easily clears the $100 free-shipping threshold.

---

## Amazon order

| Qty | Search term | ~$ |
|----:|-------------|---:|
| 1 | **"BTF-LIGHTING WS2812B 16.4ft 5m 300 IP30 60leds/m"** (LED strip) | $20 |
| 4-5 sections × 1 m | **"Muzata U01 1m LED aluminum channel with frosted cover"** | $28-35 |
| 1 | **"MUYI JST-XH connector kit 2/3/4/5-pin 530pcs with pre-crimped wires"** | $18 |
| 1 ea | **"BNTECHGO 14 AWG silicone wire 25 ft red"** + **"...black"** | $16 |
| 1 | **"Anker PowerLine USB-C to USB-A 3ft data cable"** (or any known-good USB-C *data* cable) | $10 |
| 1 | **"ELEGOO Electronic Component Fun Kit"** (assorted resistors, caps, LEDs, transistors, diodes) | $18 |
| 1 | **"WAGO 221-415 5-conductor lever connector"** (25-pack) — for branching the slab DC rail into 4 nodes | $10 |

**Amazon subtotal: ~$120-127.**

> The USB-C cable is for flashing the ESP32-S3-DevKitC-1, which ships without one. Many USB-C cables you have lying around are charge-only — confirm data support before flash day. The ELEGOO Fun Kit isn't strictly needed (the DigiKey bulk packs cover this build), but at $18 it's the right call if you'll ever do another project — it's the "I need a 10 kΩ at midnight" insurance. Component values in cheap assortment kits are loosely binned; for any precision-critical use, fall back to the DigiKey passives.
>
> Order 4 sections of channel if your perimeter is exactly 4 m and you're confident in the corner cuts; 5 if you want one spare meter for mistakes (recommended for first build).

---

## Hardware store (Home Depot / Lowe's / local)

| Qty | Part | ~$ |
|----:|------|---:|
| 1 | Pine 1×6 scrap, ~6" length | $0-3 |
| 1 box | #8 wood screws, 3/4" + 1.5" assortment | $5 |
| 1 pack | P-clips or adhesive cable mounts, mixed sizes | $5 |
| 1 | Soft rubber grommet, ½″ or ⅝″ panel-hole, ID matched to your DC cable OD (the 14 AWG silicone twisted pair is ~5/16″ / 8 mm OD) | $2 |

**Hardware-store subtotal: ~$12-17.**

---

## Powerpole assembly: solder + heat-shrink (no extra tool)

Decision locked in: skipping the TRIcrimp. Powerpole contacts solder reliably to wire — slower per pair (~5 min vs ~30 sec with the TRIcrimp) but the joint is at least as strong, and you already own the iron.

Process per contact:

1. Strip ~5 mm of insulation off the silicone wire.
2. Tin the wire end and the back of the Powerpole contact.
3. Hold the contact in your helping hands, slide the wire into the cup, reflow with the iron until solder wets both surfaces (~2-3 seconds).
4. Heat-shrink a piece over the joint (cover the contact's barrel and onto the wire insulation).
5. Push the contact into the housing until it clicks (the spring tab locks it in).

You'll do 6 of these total (3 housing-pairs × 2 contacts each). Budget 30-40 minutes the first time.

---

## Tools (Amazon — only if you don't have them)

DigiKey carries Hakko irons and many of these but typically at +20–40% over Amazon. Tools are commodity items where Amazon authenticity is fine.

| Tool | Specific recommendation | ~$ |
|------|------------------------|---:|
| Soldering iron | **Pinecil V2** (USB-PD; great iron at any price) | $30 |
| Solder, 1 lb spool | **Kester `24-6337-0027`** (60/40 leaded, 0.031" rosin core) | $30 |
| Side cutters | **Hakko CHP-170** | $8 |
| Wire strippers | **Irwin Vise-Grip 2078300** self-adjusting | $25 |
| Multimeter | **AstroAI DM6000AR** (auto-ranging, capacitance, frequency) | $35 |
| Helping hands | Generic with weighted base + alligator clips | $15 |
| Flux pen | **Kester 951** | $10 |
| Heat-shrink kit | **Eventronic 560pc assortment** | $10 |
| Solder wick | **MG Chemicals 4-25** (3.5 mm × 5 ft) | $5 |
| Silicone bench mat | iFixit ProTech or generic 24×16 silicone | $20 |
| Wire ferrule kit + ratcheting crimper combo | **"IWISS self-adjustable ferrule crimping kit"** (crimper + ~1,200 assorted ferrules AWG 28-10) | $25 |

**Tools subtotal: ~$213.** Full-comfort tier — items chosen so they keep earning their keep on future projects rather than being one-build throwaways. The ferrule crimper is for terminating stranded silicone wire into the Phoenix-style screw terminals — bare strands clamped raw will slowly back out under thermal cycling.

---

## Bench organization (recommended)

The leftover passives, JST kit, wire spools, and heat-shrink will swamp a desk if not corralled. Modest upfront spend ($90-100) saves hours of "where did I put the 470 Ω resistors" later.

### Essentials

| Item | Specific recommendation | ~$ |
|------|------------------------|---:|
| Compartmented parts cabinet (for passives + small ICs) | **Akro-Mils 10164** (64 small drawers) | $55 |
| Tackle / hardware organizer (for connectors, Powerpole spares, terminal blocks) | **Plano 3700 ProLatch** with adjustable dividers | $15 |
| Pegboard + hooks above the bench | **IKEA SKÅDIS** 22"×22" panel + assorted hooks | $25-30 |
| Solder spool stand | Generic solder spool holder | $5 |

**Bench essentials subtotal: ~$100-105.**

### Optional additions

| Item | Specific recommendation | ~$ |
|------|------------------------|---:|
| Wire spool wall holder (keeps the 14 AWG silicone + 22 AWG spools dispensing cleanly) | search "wire spool wall mount holder" | $15-20 |
| ESD wrist strap (only if you start working with QFN/MEMS-sensitive parts later) | Generic ESD wrist strap | $8 |

### Notes by category

- **Resistors / Zeners**: dump each bulk pack into its own Akro-Mils drawer; label each drawer (you've already got the label maker).
- **Spare ESP32-S3** (after Phase 0 → main project transition leaves zero spare boards): keep in original anti-static bag, labeled drawer.
- **Heat-shrink**: the Eventronic 560-pc kit it ships in is already well-organized — don't decant.
- **WS2812B leftover (~1 m) + channel offcuts**: ESD bag the strip came in, then a labeled gallon zip-bag.

### Skip for now

Rolling tool chests, smart-app parts management systems, anything calling itself "professional grade" at >$200. Bench organization scales fine with a $100 setup until you genuinely outgrow it.

---

## Cost summary

| Category | Cost |
|----------|-----:|
| DigiKey (parts) | ~$148 |
| Amazon (LED strip + channel + JST + silicone wire + USB-C cable + ELEGOO kit + WAGOs) | ~$120-127 |
| Hardware store | ~$12-17 |
| **Parts subtotal** | **~$280-292** |
| Tools (skip if owned) | ~$213 |
| Bench organization (recommended) | ~$100-105 |
| **Project total** | **~$593-610** |

**The numbers honestly:** the doc's §2 estimate of "$80-120 parts" was optimistic — based on Amazon-everything sourcing with mystery-source kits. Authenticated parts from DigiKey + a real LED channel + spares pushes it to ~$230. There isn't much more to trim without compromising safety (the IEC inlet) or the build (the LED channel diffuser is what makes the rim look professional vs janky).

If you already own any tools, deduct directly from the ~$188 total — they're all standard bench items. Bench organization is genuinely optional for one project (cardboard boxes work) but pays back fast if this becomes a recurring hobby.

---

## Order sequencing

1. **Hardware store** — same day; nothing depends on slow-shipping vendors.
2. **DigiKey** — place first (1-3 day shipping). Long lead-time risk is the ESP32-S3 (occasionally back-ordered).
3. **Amazon** — place same day as DigiKey; LED strip + channel arrive in 2-3 days.

## Pre-order checklist

Before placing the DigiKey order:

- [ ] Confirm slab dimensions (8 sides × 20" each = 4 m perimeter — order 5 channel sections so you have a spare meter for first-time corner cuts)
- [x] 2 ESP32-S3 boards (keeps Phase 0 Tap Light alive as a permanent desk lamp)
- [x] Powerpole assembly method: solder + heat-shrink (no TRIcrimp)
- [x] Wi-Fi reaches the planned PSU mounting location (OTA path is viable)
- [x] Tools tier: full comfort (~$188; investing in keepers, not throwaways)
