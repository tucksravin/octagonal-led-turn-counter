# Runtime piezo map + live OTA — design

**Date**: 2026-08-14
**Status**: approved

## Goal

Two independent gaps, sharing a theme: things that currently require editing
source and reflashing over USB should be fixable on the assembled table.

1. **Piezo map.** `PIEZO_PINS[NUM_SIDES] = {1,2,4,5,6,7,8,9}` in
   `octagon_core.cpp` is `const`, so a crossed harness — tap seat 3, seat 5
   lights — is only fixable by editing the array and reflashing. The *LED* side
   table right beside it is already runtime-calibrated into NVS by tap_light;
   the piezo map should get the same treatment, discovered by a tap gesture
   that needs no knowledge of which wire went where.
2. **OTA.** `turn_counter.ino` already contains a complete ArduinoOTA
   implementation — WiFi STA, password, LED progress bar, green/end and
   red/error states — and builds on `min_spiffs` so the dual app slots exist.
   It has never run: `WIFI_SSID` is still `"your-network-here"`, there is no
   network upload path in the Makefile, and the boot-time connect never
   retries. Close those three gaps.

## Design — Part 1: runtime piezo map

### octagon_core

- `uint8_t sidePiezoPin[NUM_SIDES]` — new runtime table, exported from the
  header. Defaults to a copy of `PIEZO_PINS`, so behavior is identical to
  today until a calibration is written.
- `PIEZO_PINS` stays as-is: the fixed list of the 8 hardware ADC1 pins. It
  becomes the *validation domain* and the wizard's raw-read source, not the
  side lookup.
- NVS: existing `Preferences` namespace `"octagon"`, new key `"pmap"`, 8 raw
  bytes via `putBytes`/`getBytes`. Loaded in `octagonBegin()` immediately
  after `loadSideTable()`, and reported on the same boot banner. Validation:
  the 8 stored bytes must be a permutation of `PIEZO_PINS` — every entry a
  member, no duplicates. Anything else falls back to the default and says so,
  mirroring `validSideTable`'s behavior.
- `readPiezos()` changes one line: `analogRead(PIEZO_PINS[i])` →
  `analogRead(sidePiezoPin[i])`. Baselines, the cross-talk "biggest delta
  wins" filter, opposite-pair geometry and debounce all stay in side space,
  untouched.
- `seedBaselines(const uint8_t *pins)` — extracted from `octagonBegin()`'s
  inline 100-sample loop, taking the pin array to sample so the same
  `baselineAcc[]` buffer can be seeded in either indexing. `octagonBegin()`
  passes `sidePiezoPin` (side-indexed, as today). The wizard passes
  `PIEZO_PINS` on entry so its raw reads are channel-indexed, and
  `sidePiezoPin` again on exit so the game resumes with correct per-side
  baselines — they are stale once a side's channel moves.

### The wizard

`void runPiezoMapWizard()` in octagon_core — blocking, LED-guided, serial-driven.
Blocking is acceptable: the game is paused for the duration, and the routine
runs for tens of seconds, not minutes. Both sketches share the one
implementation.

For each side 0→7:

- Light that side white, everything else dark. The LEDs are what teach the
  operator which seat "side 3" means, so no seat numbering has to be known in
  advance.
- Print a prompt (`MAP side 3 - tap the lit seat`).
- Wait for a spike on any hardware channel, reading `PIEZO_PINS[]` **directly**
  — the map under construction is bypassed, so a crossed harness can't confuse
  the wizard building the fix for it. Threshold is the game's `TAP_DELTA`
  against freshly seeded baselines.
- 20 s with no tap → print `MAP timeout`, abort, write nothing.
- A channel already claimed by an earlier side → print
  `MAP dup - GPIO 6 already mapped to side 1, retry` and re-prompt the same
  side. This is the one failure mode that would otherwise silently corrupt the
  permutation.
- On accept: flash the side green, print `MAP side 3 = GPIO 6`, then hold ~400 ms
  before prompting the next side. Without that settle the ring-down from the
  tap just accepted would immediately claim the next side.

On completion: re-seed baselines, write NVS, flash the whole table green, print
the summary plus a paste-ready `sidePiezoPin[NUM_SIDES] = {...};` line so the
as-built wiring can be recorded in git. `q` at any prompt aborts with nothing
written.

`void printPiezoMap()` prints that same line on demand.

### Sketch changes

`turn_counter.ino` and `eight.ino` each get a small `handleSerial()` called
from `loop()`: `m` runs the wizard, `p` prints the map. `turn_counter` has no
serial command handling at all today, so this is new there; `eight` likewise.

### scripts/map_piezos.py + `make map-piezos`

A friendly wrapper over the firmware protocol, following the conventions of
the existing bench scripts:

- Autodetects the CP2102 port (`/dev/cu.usbserial-*`), `--port` overrides,
  with the same "the usbmodem ports are the LG monitor" error text as
  `record_piezos.py`.
- Opens with `dtr`/`rts` staged `False` before `open()` so the board doesn't
  reboot as the session starts and lose its baselines.
- Sends `m`, mirrors the board's `MAP …` lines as clean numbered terminal
  instructions, and surfaces timeouts and duplicate-channel retries as
  prompts rather than raw log lines.
- On `MAP DONE`, prints the final map as a table plus the paste-ready source
  line. On abort or timeout, exits non-zero with what went wrong.

## Design — Part 2: finishing OTA

### Credentials

- `firmware/turn_counter/secrets.h` — gitignored, holds `WIFI_SSID`,
  `WIFI_PASSWORD`, `OTA_HOSTNAME`, `OTA_PASSWORD`.
- `firmware/turn_counter/secrets.example.h` — committed template.
- `.gitignore` gains `firmware/*/secrets.h`.
- `turn_counter.ino` guards with `#if __has_include("secrets.h")`. Absent the
  file it defines `WIFI_SSID` as `""`, and `setupWiFiAndOta()` skips WiFi
  entirely with an explanatory print — not a doomed connect to a placeholder
  SSID. This keeps `make compile-all` green on a fresh clone.

### Reconnect

`setupWiFiAndOta()` splits. The 5 s blocking connect at boot stays as-is, but
`ArduinoOTA.begin()` and its callbacks move behind an `otaReady` flag so they
run the first time the link actually comes up, whenever that is. `loop()` gains
a non-blocking check: if the SSID is configured and the link is down and 30 s
have passed since the last attempt, call `WiFi.reconnect()`; when it succeeds,
start OTA. No `delay()` on that path — tap latency is unaffected.

Today a slow AP at boot means no OTA until the table is power-cycled.

### scripts/ota_flash.py + `make ota`

- `make ota` compiles turn_counter with the existing `FQBN_TURN` +
  `--output-dir`, then invokes the script against the resulting `.bin`.
- The script locates `espota.py` inside the installed ESP32 platform
  (`~/Library/Arduino15/packages/esp32/hardware/esp32/*/tools/espota.py`) and
  fails with an actionable message if the platform isn't installed.
- Reads `OTA_PASSWORD` out of `secrets.h` so it isn't duplicated in the
  Makefile, the environment, or shell history.
- Pre-flight: resolve `<hostname>.local`, then confirm TCP 3232 accepts a
  connection *before* starting the upload. This is the difference between
  "board is on a different subnet" and a 60-second silent hang.
- `--host <ip>` overrides when mDNS is being unhelpful.
- Streams espota's progress through to the terminal.

### Out of scope for OTA

`eight` stays USB-only. It is the no-radio fallback that fits the stock 1.25 MB
partition, and `octagon_core.h` documents that as the reason the library never
touches WiFi. It still gets the piezo wizard, which needs no radio.

## Risks

- **A bad `secrets.h` pushed over OTA kills the OTA path** and needs USB to
  recover. The service access to the ESP32's USB port that the design doc
  already calls for (§7, "Provide a service access path") stays mandatory, not
  optional.
- **Stale baselines after a remap** would make the table briefly deaf or
  trigger-happy; the wizard re-seeds before returning, which is why
  `seedBaselines()` is extracted rather than duplicated.
- **NVS `"pmap"` written by an older/newer firmware** is handled by the
  permutation validation — an unrecognized table is ignored, not trusted.

## Testing

Bench-driven; the operator flashes and reports, per the current hardware phase.

- `make compile-all` passes, including on a tree with no `secrets.h`.
- `make flash-turn` over USB, confirm the boot banner reports both the side
  table and the piezo map, and that gameplay is unchanged with the default
  (identity) map.
- `make map-piezos`, walk all 8 sides, confirm each lit side accepts a tap on
  the seat that is lit. Deliberately tap the *wrong* seat once to confirm the
  duplicate-channel retry fires rather than corrupting the map.
- Power-cycle, confirm the map survives and taps land on the right seats.
- Populate `secrets.h`, `make flash-turn` once over USB, confirm
  `ping turn-counter.local` resolves, then `make ota` and confirm the progress
  bar renders and the board comes back on the new firmware.
- Pull the AP for the first 30 s after boot, confirm the table reconnects and
  OTA becomes reachable without a power cycle.

## Out of scope

- No change to tap detection thresholds, turn logic, or the LED side table.
- No web UI, no OTA-over-internet, no rollback slot management beyond what
  `min_spiffs` already provides.
- No per-board secrets provisioning into NVS (considered and set aside; the
  gitignored header needs no extra firmware).
