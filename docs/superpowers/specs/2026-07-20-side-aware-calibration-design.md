# Side-aware strip test + per-side LED calibration — design

**Date**: 2026-07-20
**Status**: approved

## Goal

The assembled octagon has uneven LED counts per side (corner cuts eat 1–2 LEDs
unpredictably). Both firmwares currently assume a uniform `LEDS_PER_SIDE = 30`,
so side boundaries drift off the physical corners. Replace the uniform math
with a calibrated per-side table, and give the bench (tap_light config: full
strip + one piezo) a way to discover that table without hand-counting LEDs.

## Design

### Shared side-mapping block

Duplicated in `tap_light.ino` and `turn_counter.ino` (~30 lines), matching the
repo's self-contained-sketch pattern:

- `uint8_t sideLedCounts[8]` — defaults `{30,…,30}`; overridden at boot by a
  calibrated table in NVS if one exists.
- `uint16_t sideStarts[9]` — prefix sums rebuilt whenever counts change;
  `sideStarts[8]` is the strip total. LEDs past the total render black.
- `NUM_LEDS` becomes a 240-LED buffer ceiling (`leds[]` allocation only).
- NVS: `Preferences` namespace `"octagon"`, key `"sides"`, 8 raw bytes via
  `putBytes`/`getBytes`. Validate on load (each count 1–60, total ≤ 240);
  fall back to defaults if invalid. Both sketches read the same namespace, so
  calibration done under tap_light carries over to turn_counter automatically.

### tap_light.ino

- New **side-colors mode** inserted into the tap cycle (`NUM_MODES` 6 → 7,
  off stays last): each side filled with `CHSV(side * 32, 255, 255)` so every
  corner is a visible color flip. Existing solids/rainbow/off remain the
  whole-strip smoke test.
- **Serial calibration** (always listening, single-char commands, 115200):
  - `0`–`7` — select a side and enter calibration view: side-colors render
    with the selected side overridden to white.
  - `+`/`=`/`-` — grow/shrink the selected side by one LED (shifts all later
    boundaries; side 7 effectively sets the strip total). Auto-saves to NVS,
    re-renders, prints the table.
  - `p` — print the table as a paste-ready C array with total.
  - `r` — reset table to defaults and save.
  - `q` — exit calibration view, return to the current mode.
  - Taps during calibration are logged but don't change modes.

### turn_counter.ino

- Same mapping block + NVS load in `setup()`.
- `renderTurn()` player zones: `sideCursor * LEDS_PER_SIDE` →
  `sideStarts[sideCursor]`; zone length over contiguous sides comes from
  prefix-sum differences.
- Victory percent fill and full-strip flashes: `NUM_LEDS` → `sideStarts[8]`.
- `LEDS_PER_SIDE` define removed.

## Testing

- Both sketches compile via the existing Makefile / arduino-cli setup
  (turn_counter needs `PartitionScheme=min_spiffs`).
- Flash tap_light on the bench (CP2102 `usbserial-*` port); user taps through
  all 7 modes to smoke-test the full strip, then calibrates each corner via
  serial and reports the printed table.
- Follow-up once calibrated: hard-code the printed table as the new
  `sideLedCounts` defaults in both sketches.

## Out of scope

- No shared header/library between sketches (repo convention is
  self-contained .ino files).
- No change to tap detection or turn logic.
