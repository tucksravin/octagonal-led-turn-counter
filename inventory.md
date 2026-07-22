# Inventory

Running record of parts bought for this project and — the point of this file — **what's left over for future projects.** A lot of the BOM came in bulk (100-packs of resistors, a 25-pack of Zeners, a 530-pc JST kit, 5 m of strip for a 4 m build), so the surplus is real. Check here before re-buying.

**How to maintain it:** the counts below are derived from the [shopping list](shopping_list.md) *orders*, not a physical count — reconcile against what actually arrived, then keep the **Spare** column current as you consume parts or restock. When a future project pulls from stock, decrement here and add a line under [Future / added stock](#future--added-stock).

**Where it's stored:** see the shopping list's "Bench organization" section — passives in the Akro-Mils 10164 drawers (labeled), connectors/Powerpole spares in the Plano 3700, strip offcuts in labeled ESD/zip bags.

> Surplus assumes the **Mean Well PSU** power path. If you switch to the **USB powerbank** (design doc §3.5), the PSU, IEC inlet, switch, Powerpoles, DC fuse, and most of the 14 AWG wire all become spare — those rows are tagged ⚡.

---

## Semiconductors & modules

| Part | Mfr PN | Bought | Build uses | Spare | Notes |
|------|--------|-------:|-----------:|------:|-------|
| ESP32-S3-DevKitC-1, N8R8 | `ESP32-S3-DEVKITC-1-N8R8` | 2 | 1 | 1\* | \*2nd board is the Phase 0 Tap Light desk lamp — spoken for, not truly free |
| 74AHCT125 level shifter, DIP-14 | `SN74AHCT125N` | 2 | 0 | 2 | Optional add-back; default build direct-drives at 3.3 V |

## Passives

| Part | Mfr PN | Bought | Build uses | Spare | Notes |
|------|--------|-------:|-----------:|------:|-------|
| 1 MΩ 1/4 W resistor | `CFR-25JT-52-1M0` | 100 | 8 | ~92 | one bleed resistor per piezo |
| 470 Ω 1/4 W resistor | `CFR-25JT-52-470R` | 100 | 1 | ~99 | strip data series resistor |
| 1N4728A 3.3 V Zener | `1N4728ATR` | 25 | 8 | ~17 | one clamp per piezo |
| 1000 µF 50 V electrolytic | `ECA-1HM102` | 5 | 1–3 | 2–4 | strip-start reservoir (+ one per injection point if used) |
| Assorted R/C/LED/transistor/diode | ELEGOO Fun Kit | 1 kit | a few | most | "midnight 10 kΩ" insurance; values loosely binned |

## Connectors

| Part | Mfr PN | Bought | Build uses | Spare | Notes |
|------|--------|-------:|-----------:|------:|-------|
| Powerpole 30 A housing, red | Anderson `1327` | 3 | 3 | 0 ⚡ | slab / frame / bench-test pairs — all 3 spare on powerbank path |
| Powerpole 30 A housing, black | Anderson `1327G6` | 3 | 3 | 0 ⚡ | " |
| Powerpole 30 A contact | Anderson `1331` | 6 | 6 | 0 ⚡ | " |
| JST-XH kit, 2–5 pin, pre-crimped | MUYI 530 pc | 530 pc | ~19 pins | hundreds | 8× piezo 2-pin + strip 3-pin inline disconnects; still enormous surplus |
| WAGO 221-415 lever, 5-cond | 25-pack | 25 | ~4 | ~21 | slab DC-rail branch nodes |
| 2.54 mm female header, 1×40 | 10-pack | 10 | 1–2 | ~8 | two 1×22 strips for the ESP socket |

## Piezos & mechanical

| Part | Mfr PN | Bought | Build uses | Spare | Notes |
|------|--------|-------:|-----------:|------:|-------|
| 27 mm piezo disc | `7BB-27-4L0` | 10 | 8 | 2 | one per side |
| Project box, ABS ~112×62×31 | Hammond `1591BBK` | 1 | 1 | 0 | control box |
| Half-size breadboard, 400-pt | `BB400` | 1 | 1 (bench) | reusable | solderless prototyping |
| Perma-Proto half | Adafruit `1609` | 1 | 1 (tap light) | — | Phase 0 starter board |
| Perma-Proto half, 30-col | Adafruit `571` | 1 | 1 (control board) | — | main control board |
| Inline blade-fuse holder + 5 A fuse | Littlefuse `0FHM…` | 1 | 1 | 0 ⚡ | DC-side fuse; not needed behind a current-limited bank |
| M3 nylon standoff assortment | — | 1 kit | a few | most | mounts control board in box |
| Rubber grommet assortment | — | 1 kit | several | some | cable entries |

## Wire

| Part | Bought | Build uses | Spare | Notes |
|------|-------:|-----------:|------:|-------|
| 22 AWG stranded, 6-color | Adafruit `3111`, 25 ft | partial | most | piezo pairs + signal runs |
| 14 AWG silicone, red + black | BNTECHGO, 25 ft ea | ~6 ft | ~19 ft ea ⚡ | PSU→slab DC run; mostly spare on powerbank path |
| 20 AWG silicone, red + black | BNTECHGO, 10 ft ea | partial | some | board pigtails + rail bridges |

## LED strip & channel

| Part | Bought | Build uses | Spare | Notes |
|------|-------:|-----------:|------:|-------|
| WS2812B, 60/m, IP30, 5 V | 5 m / 300 LED (BTF) | 4 m / 221 lit | ~1 m (~60 LED) | offcut in ESD bag; spares for repairs |
| Aluminum channel U01 + frosted cover | 4–5 × 1 m (Muzata) | ~4 m | ~1 m + offcuts | order-5 leaves a spare meter |

## Finishing / consumables

| Part | Bought | Build uses | Spare | Notes |
|------|-------:|-----------:|------:|-------|
| Plasti Dip, black, 14.5 oz | 1 can | 3 coats × 8 caps | most of can | corner-cap rubber skin |
| Rubber bumpers, 7/8" screw-on *(opt)* | up to 2×4-pack | up to 8 | as used | mid-side standoffs |
| Corner guard, 4 ft *(opt)* | 1 if bought | cut into 8 | offcut | alt to hardwood caps |

## Hardware-store / commodity

Pine 1×6 scrap, #8 wood screws (¾" + 1.5"), P-clips / adhesive mounts, ½–⅝" rubber grommet, 9 V battery (dry-run). Restock at the store as needed — not worth tracking counts.

---

## Tools (reusable equipment)

Owned after this project; available for anything future. Consumables among them (**bold**) deplete — restock when low.

Pinecil V2 iron · **Kester 63/37 solder (1 lb)** · Hakko CHP-170 cutters · Irwin 2078300 strippers · AstroAI DM6000AR multimeter · helping hands · **Kester 951 flux pen** · **Eventronic heat-shrink kit** · **MG Chemicals 4-25 solder wick** · silicone bench mat · IWISS ferrule crimper + **ferrule assortment**.

**Bench organization:** Akro-Mils 10164 (64-drawer) · Plano 3700 ProLatch · IKEA SKÅDIS pegboard + hooks · solder spool stand.

---

## Future / added stock

Log anything bought *after* the initial order here (date, part, qty), and note when future projects draw down stock.

| Date | Part | Qty | +/− | Note |
|------|------|----:|:---:|------|
| — | *(e.g. USB powerbank w/ always-on mode + a plain USB cable into the board, if going the §3.5 power-through-the-board route)* | — | + | not yet purchased |
