# Runtime piezo map + live OTA — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a crossed piezo harness fixable on the table (tap-guided remap stored in NVS) and turn the already-written OTA path on for real (gitignored credentials, background reconnect, `make ota`).

**Architecture:** `octagon_core` gains a runtime `sidePiezoPin[8]` table loaded from NVS alongside the LED side table it already loads, plus a blocking LED-guided wizard that discovers it by lighting each side and recording which piezo answers. `turn_counter` moves its Wi-Fi credentials into a gitignored `secrets.h`, splits `ArduinoOTA.begin()` behind a flag so it starts whenever the link comes up rather than only at boot, and gains a non-blocking reconnect. Two host scripts wrap the serial and network protocols.

**Tech Stack:** ESP32-S3 / Arduino (FastLED, Preferences, ArduinoOTA), arduino-cli + Make, Python 3 + pyserial for bench scripts, pytest for the scripts' parsing logic.

**Spec:** `docs/superpowers/specs/2026-08-14-piezo-map-and-ota-design.md`

---

## A note on verification

This repo has no firmware test harness and the board is not on this machine's
control path — the operator drives the bench terminal. So:

- **Firmware tasks** are gated on `make compile-all` (which must pass) plus an
  explicit bench check the operator runs. Those bench checks are written out as
  commands with expected serial output. Do not claim a firmware task verified on
  a compile alone; say "compiles, bench check pending."
- **Host-script tasks** are real TDD: the parsing and discovery logic is pure and
  gets tested with pytest before it is written. Serial and socket I/O are not
  unit-tested — they are covered by the bench checks.

`make compile-all` is an operator command too. When a task says "run it", that
means hand it to the operator and wait, unless the current session has been told
otherwise.

## File structure

**Created:**
- `firmware/turn_counter/secrets.example.h` — committed credentials template
- `scripts/map_piezos.py` — drives the remap wizard over serial
- `scripts/ota_flash.py` — locates espota.py, pre-flights the board, pushes the build
- `tests/conftest.py` — puts `scripts/` on `sys.path`
- `tests/test_map_piezos.py` — board-protocol parsing
- `tests/test_ota_flash.py` — secrets parsing + platform discovery

**Modified:**
- `firmware/libraries/octagon_core/src/octagon_core.h` — export the map, the wizard, the printer
- `firmware/libraries/octagon_core/src/octagon_core.cpp` — map table, NVS load/validate, `seedBaselines(pins)`, one-line `readPiezos` change, the wizard
- `firmware/turn_counter/turn_counter.ino` — serial commands, `secrets.h` guard, OTA split + reconnect
- `firmware/eight/eight.ino` — serial commands
- `Makefile` — `map-piezos` and `ota` targets
- `.gitignore` — `firmware/*/secrets.h`, `build/`
- `requirements.txt` — pytest
- `README.md`, `turn_counter_design_doc.md`, `bench_build_guide.md` — docs

---

## Task 1: Runtime piezo map table in octagon_core

**Files:**
- Modify: `firmware/libraries/octagon_core/src/octagon_core.h:33` (after the `PIEZO_PINS` extern)
- Modify: `firmware/libraries/octagon_core/src/octagon_core.cpp:6`, `:115-132`, `:201-227`

- [ ] **Step 1: Export the map from the header**

In `octagon_core.h`, directly after the existing `extern const uint8_t PIEZO_PINS[NUM_SIDES];` / `extern const CRGB PLAYER_COLORS[NUM_SIDES];` block, add:

```cpp
// Which ADC pin serves each side. Defaults to PIEZO_PINS in order; a tap-guided
// calibration (runPiezoMapWizard) can permute it and store the result in NVS,
// so a crossed harness is fixed on the table instead of in the source. Always a
// permutation of PIEZO_PINS — an NVS table that isn't one is ignored.
extern uint8_t sidePiezoPin[NUM_SIDES];
```

- [ ] **Step 2: Define the table and its NVS load**

In `octagon_core.cpp`, immediately after the `PIEZO_PINS` definition on line 6, add:

```cpp
// Filled by loadPiezoMap() at boot — PIEZO_PINS order, or the calibrated
// permutation from NVS. Not usable before octagonBegin().
uint8_t sidePiezoPin[NUM_SIDES] = {0};
```

Then, immediately after the existing `loadSideTable()` function (ends line 77), add:

```cpp
// Valid only if the stored table is a true permutation of PIEZO_PINS: every
// entry a pin we actually read, and no pin serving two sides. This is what
// makes an NVS table written by a different firmware safe to ignore.
static bool validPiezoMap(const uint8_t *m) {
  uint8_t seen = 0;
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    uint8_t idx = 0xFF;
    for (uint8_t i = 0; i < NUM_SIDES; i++) {
      if (PIEZO_PINS[i] == m[s]) { idx = i; break; }
    }
    if (idx == 0xFF || (seen & (1 << idx))) return false;
    seen |= (1 << idx);
  }
  return true;
}

void printPiezoMap() {
  Serial.print("uint8_t sidePiezoPin[NUM_SIDES] = {");
  for (uint8_t s = 0; s < NUM_SIDES; s++) {
    Serial.printf("%u%s", sidePiezoPin[s], s < NUM_SIDES - 1 ? ", " : "");
  }
  Serial.println("};");
}

static void loadPiezoMap() {
  memcpy(sidePiezoPin, PIEZO_PINS, NUM_SIDES);
  uint8_t stored[NUM_SIDES];
  if (sidePrefs.getBytes("pmap", stored, NUM_SIDES) == NUM_SIDES && validPiezoMap(stored)) {
    memcpy(sidePiezoPin, stored, NUM_SIDES);
    Serial.println("Loaded calibrated piezo map from NVS");
  }
  printPiezoMap();
}
```

- [ ] **Step 3: Declare the printer in the header**

In `octagon_core.h`, alongside the other diagnostics (next to `uint16_t baseline(uint8_t i);`), add:

```cpp
void printPiezoMap();          // current side→GPIO map as a paste-ready C array line
```

- [ ] **Step 4: Extract seedBaselines so it can be re-run on a different indexing**

In `octagon_core.cpp`, add this above `octagonBegin()`:

```cpp
// Seeds every baseline from ~0.5 s of quiet readings. Takes the pin array so the
// same buffer can be seeded side-indexed (sidePiezoPin, for play) or
// channel-indexed (PIEZO_PINS, for the remap wizard).
static void seedBaselines(const uint8_t *pins) {
  uint32_t sums[NUM_SIDES] = {0};
  for (uint8_t s = 0; s < 100; s++) {
    for (uint8_t i = 0; i < NUM_SIDES; i++) {
      sums[i] += analogRead(pins[i]);
    }
    delay(5);
  }
  Serial.print("Piezo baselines:");
  for (uint8_t i = 0; i < NUM_SIDES; i++) {
    baselineAcc[i] = (sums[i] / 100) << 6;
    Serial.printf(" %u", baseline(i));
  }
  Serial.printf(" (tap fires at baseline + %u)\n", TAP_DELTA);
}
```

- [ ] **Step 5: Rewrite octagonBegin to use both**

Replace the whole body of `octagonBegin()` (currently lines 201-227) with:

```cpp
void octagonBegin() {
  sidePrefs.begin("octagon", false);
  loadSideTable();
  loadPiezoMap();

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_POWER_MA);  // runs off the board's USB; dims all-on peaks

  // Every hardware pin is configured regardless of the map — the wizard reads
  // them all directly.
  for (uint8_t i = 0; i < NUM_SIDES; i++) {
    pinMode(PIEZO_PINS[i], INPUT);
  }

  seedBaselines(sidePiezoPin);
}
```

- [ ] **Step 6: Route tap reads through the map**

In `readPiezos()`, change the one read (currently line 121):

```cpp
    uint16_t reading = analogRead(sidePiezoPin[i]);
```

Nothing else in `readPiezos` changes — the cross-talk filter, debounce and opposite-pair geometry all stay in side space.

- [ ] **Step 7: Compile**

Run: `make compile-all`
Expected: all eight sketches link. `turn_counter` still needs `min_spiffs` (the Makefile handles it).

- [ ] **Step 8: Commit**

```bash
git add firmware/libraries/octagon_core/src/octagon_core.h firmware/libraries/octagon_core/src/octagon_core.cpp
git commit -m "octagon_core: runtime side→piezo map backed by NVS"
```

---

## Task 2: The tap-guided remap wizard

**Files:**
- Modify: `firmware/libraries/octagon_core/src/octagon_core.h` (declare `runPiezoMapWizard`)
- Modify: `firmware/libraries/octagon_core/src/octagon_core.cpp` (implement it, after `printPiezoMap`)

- [ ] **Step 1: Declare it**

In `octagon_core.h`, next to `printPiezoMap()`:

```cpp
// Blocking, LED-guided remap: lights each side in turn and records which piezo
// the operator taps, so nobody has to know which wire went where. Saves to NVS
// and re-seeds the baselines. Serial 'q', or 20 s of silence at any prompt,
// aborts with nothing written. Call from a sketch's serial handler; the caller
// must re-render afterwards.
void runPiezoMapWizard();
```

- [ ] **Step 2: Add the wizard's timing constants**

In `octagon_core.cpp`, next to the existing `DEBOUNCE_MS` / `OPPOSITE_PAIR_WINDOW_MS` constants:

```cpp
static const uint16_t MAP_PROMPT_TIMEOUT_MS = 20000;  // silence at a prompt aborts the whole remap
static const uint16_t MAP_SETTLE_MS         = 400;    // ring-down gap so one tap can't answer two prompts
```

- [ ] **Step 3: Implement it**

Add after `printPiezoMap()` in `octagon_core.cpp`:

```cpp
// Lights one side at a time and records which physical piezo answers. Reads
// PIEZO_PINS directly, bypassing the map it is replacing — a crossed harness
// can't confuse the wizard that fixes it. Baselines are channel-indexed for the
// duration and re-seeded side-indexed on the way out.
void runPiezoMapWizard() {
  uint8_t built[NUM_SIDES];
  uint8_t claimed = 0;  // bitmask of hardware channels already assigned

  Serial.println("MAP START - tap each lit side; q aborts");
  seedBaselines(PIEZO_PINS);

  for (uint8_t s = 0; s < NUM_SIDES; ) {
    showOnlySide(s, CRGB(CRGB::White));
    Serial.printf("MAP side %u - tap the lit seat\n", s);

    int8_t hit = -1;
    uint32_t promptedMs = millis();
    while (hit < 0) {
      if (millis() - promptedMs > MAP_PROMPT_TIMEOUT_MS) {
        Serial.println("MAP TIMEOUT - nothing saved");
        renderOff();
        seedBaselines(sidePiezoPin);
        return;
      }
      if (Serial.available() && Serial.read() == 'q') {
        Serial.println("MAP ABORT - nothing saved");
        renderOff();
        seedBaselines(sidePiezoPin);
        return;
      }
      // Biggest jump above its own baseline wins, so an adjacent side's
      // cross-talk ghost loses to the seat actually struck.
      uint16_t best = 0;
      for (uint8_t c = 0; c < NUM_SIDES; c++) {
        uint16_t reading = analogRead(PIEZO_PINS[c]);
        if (reading > baseline(c) + TAP_DELTA) {
          uint16_t delta = reading - baseline(c);
          if (delta > best) { best = delta; hit = c; }
        } else {
          baselineAcc[c] += reading - baseline(c);
        }
      }
      delay(5);
    }

    if (claimed & (1 << hit)) {
      uint8_t owner = 0;
      for (uint8_t p = 0; p < s; p++) {
        if (built[p] == PIEZO_PINS[hit]) { owner = p; break; }
      }
      Serial.printf("MAP DUP - GPIO %u already mapped to side %u, retry\n", PIEZO_PINS[hit], owner);
      showOnlySide(s, CRGB(200, 0, 0));
      delay(MAP_SETTLE_MS);
      continue;  // same side, prompt again
    }

    built[s] = PIEZO_PINS[hit];
    claimed |= (1 << hit);
    Serial.printf("MAP side %u = GPIO %u\n", s, PIEZO_PINS[hit]);
    showOnlySide(s, CRGB(0, 200, 0));
    delay(MAP_SETTLE_MS);
    s++;
  }

  memcpy(sidePiezoPin, built, NUM_SIDES);
  sidePrefs.putBytes("pmap", sidePiezoPin, NUM_SIDES);

  fill_solid(leds, totalLeds(), CRGB(0, 200, 0));
  FastLED.show();
  delay(600);
  renderOff();

  seedBaselines(sidePiezoPin);
  Serial.println("MAP DONE - saved to NVS");
  printPiezoMap();
}
```

- [ ] **Step 4: Compile**

Run: `make compile-all`
Expected: clean link. If the compiler complains that `showOnlySide` is used before declaration, move the wizard below `showOnlySide`'s definition (line 89) — it must sit after it in the file.

- [ ] **Step 5: Commit**

```bash
git add firmware/libraries/octagon_core/src/octagon_core.h firmware/libraries/octagon_core/src/octagon_core.cpp
git commit -m "octagon_core: tap-guided piezo remap wizard"
```

---

## Task 3: Serial commands in both sketches

**Files:**
- Modify: `firmware/eight/eight.ino:44-47`
- Modify: `firmware/turn_counter/turn_counter.ino:496-526`

- [ ] **Step 1: Add the handler to eight**

In `firmware/eight/eight.ino`, insert above `setup()`:

```cpp
// Bench commands over USB serial: m remaps the piezos, p prints the map.
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'm') {
      runPiezoMapWizard();
      showTurn();
    } else if (c == 'p') {
      printPiezoMap();
    }
  }
}
```

and change `loop()` to:

```cpp
void loop() {
  handleSerial();
  tapsPoll(millis());
  delay(5);
}
```

- [ ] **Step 2: Add the handler to turn_counter**

In `firmware/turn_counter/turn_counter.ino`, insert above `setup()`:

```cpp
// Bench commands over USB serial: m remaps the piezos, p prints the map. The
// wizard is blocking and owns the LEDs, so any setup session in progress is
// dropped and play restarts cleanly when it returns.
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'm') {
      runPiezoMapWizard();
      inSetupMode = false;
      startPlay();
    } else if (c == 'p') {
      printPiezoMap();
    }
  }
}
```

and add the call in `loop()`, right after the `otaActive` early-return block and before `uint32_t now = millis();`:

```cpp
  handleSerial();
```

- [ ] **Step 3: Compile**

Run: `make compile-all`
Expected: clean.

- [ ] **Step 4: Bench check (operator)**

```bash
make flash-eight
make monitor
```

Expected on boot: the existing side-table line, then `uint8_t sidePiezoPin[NUM_SIDES] = {1, 2, 4, 5, 6, 7, 8, 9};`, then the baselines line. Type `p` + Enter — the same map line prints again. Tap play still passes turns exactly as before (the default map is the identity, so nothing should have changed).

- [ ] **Step 5: Commit**

```bash
git add firmware/eight/eight.ino firmware/turn_counter/turn_counter.ino
git commit -m "eight, turn_counter: serial commands for the piezo remap"
```

---

## Task 4: scripts/map_piezos.py (TDD)

**Files:**
- Create: `tests/conftest.py`
- Create: `tests/test_map_piezos.py`
- Create: `scripts/map_piezos.py`
- Modify: `requirements.txt`, `Makefile`, `.gitignore`

Note: unlike `ping_board.py` and `record_piezos.py`, which run argparse at module
level, the new scripts keep argparse inside `main()`. That is deliberate — module-level
argparse makes a script unimportable, and the parsing logic below is worth testing
without a board attached.

- [ ] **Step 1: Add pytest and the test path shim**

In `requirements.txt`, append:

```
pytest~=8.4
```

Create `tests/conftest.py`:

```python
"""Put scripts/ on sys.path so the bench scripts can be imported by name."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
```

- [ ] **Step 2: Write the failing tests**

Create `tests/test_map_piezos.py`:

```python
from map_piezos import classify


def test_prompt_line():
    assert classify("MAP side 3 - tap the lit seat") == ("prompt", {"side": 3})


def test_assignment_line():
    assert classify("MAP side 3 = GPIO 6") == ("assign", {"side": 3, "gpio": 6})


def test_duplicate_channel_line():
    assert classify("MAP DUP - GPIO 6 already mapped to side 1, retry") == (
        "dup",
        {"gpio": 6, "owner": 1},
    )


def test_terminal_lines():
    assert classify("MAP DONE - saved to NVS")[0] == "done"
    assert classify("MAP TIMEOUT - nothing saved")[0] == "timeout"
    assert classify("MAP ABORT - nothing saved")[0] == "abort"


def test_source_line_yields_the_pin_order():
    kind, payload = classify("uint8_t sidePiezoPin[NUM_SIDES] = {1, 2, 4, 5, 6, 7, 8, 9};")
    assert kind == "source"
    assert payload["pins"] == [1, 2, 4, 5, 6, 7, 8, 9]


def test_game_chatter_is_not_mistaken_for_protocol():
    # The board keeps printing gameplay lines; none of them may parse as protocol.
    for line in [
        "Tap on side 2 ignored - not the current seat",
        "Setup: side 3 IN",
        "Side LED counts: 29 28 27 27 27 28 28 27 (total 221)",
        "",
    ]:
        assert classify(line)[0] == "other"


def test_trailing_whitespace_and_cr_are_tolerated():
    assert classify("MAP side 0 = GPIO 1\r\n") == ("assign", {"side": 0, "gpio": 1})
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `.venv/bin/python3 -m pytest tests/test_map_piezos.py -v`
Expected: collection error — `ModuleNotFoundError: No module named 'map_piezos'`.
(If pytest itself is missing: `.venv/bin/python3 -m pip install -r requirements.txt`.)

- [ ] **Step 4: Write the script**

Create `scripts/map_piezos.py`:

```python
#!/usr/bin/env python3
"""Reassign piezos to sides by tapping — run via `make map-piezos`.

The board does the work: it lights one side white at a time and records which
piezo answers, so a crossed harness gets fixed without anyone tracing wires.
This script just drives that conversation — it opens the port, sends `m`, turns
the board's MAP lines into readable prompts, and prints the finished map plus a
paste-ready source line for the record.

The map is saved to NVS on the board, so it survives reboots and OTA pushes and
no reflash is needed. Like ping_board.py this opens the port without touching
DTR/RTS, so the board keeps its baselines instead of rebooting — quit any
`make monitor` first, or the port will be busy.
"""
import argparse
import glob
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial not installed — run: .venv/bin/python3 -m pip install -r requirements.txt")

NUM_SIDES = 8
SESSION_TIMEOUT_S = 300  # whole-session ceiling; the board's own per-prompt timeout is 20 s

RE_PROMPT = re.compile(r"^MAP side (\d+) - tap the lit seat$")
RE_ASSIGN = re.compile(r"^MAP side (\d+) = GPIO (\d+)$")
RE_DUP = re.compile(r"^MAP DUP - GPIO (\d+) already mapped to side (\d+), retry$")
RE_SOURCE = re.compile(r"^uint8_t sidePiezoPin\[NUM_SIDES\] = \{([^}]*)\};$")


def classify(line):
    """Turn one line from the board into (kind, payload).

    kind is one of prompt / assign / dup / done / timeout / abort / source /
    other. Everything the board prints that isn't wizard protocol — gameplay
    chatter, the boot banner — falls through to "other", so it can be shown or
    ignored without being mistaken for a result.
    """
    line = line.strip()

    m = RE_PROMPT.match(line)
    if m:
        return ("prompt", {"side": int(m.group(1))})

    m = RE_ASSIGN.match(line)
    if m:
        return ("assign", {"side": int(m.group(1)), "gpio": int(m.group(2))})

    m = RE_DUP.match(line)
    if m:
        return ("dup", {"gpio": int(m.group(1)), "owner": int(m.group(2))})

    m = RE_SOURCE.match(line)
    if m:
        return ("source", {"line": line, "pins": [int(p) for p in m.group(1).split(",")]})

    if line.startswith("MAP DONE"):
        return ("done", {})
    if line.startswith("MAP TIMEOUT"):
        return ("timeout", {})
    if line.startswith("MAP ABORT"):
        return ("abort", {})

    return ("other", {"line": line})


def render(kind, payload):
    """The operator-facing line for a protocol event, or None to stay quiet."""
    if kind == "prompt":
        return f"\n  Side {payload['side']}: that side is lit WHITE — tap that seat."
    if kind == "assign":
        return f"    ok  side {payload['side']} -> GPIO {payload['gpio']}"
    if kind == "dup":
        return (f"    !!  GPIO {payload['gpio']} is already side {payload['owner']} — "
                "you tapped a seat that's already mapped. Tap the seat that's lit.")
    if kind == "timeout":
        return "\n  Timed out waiting for a tap. Nothing was saved."
    if kind == "abort":
        return "\n  Aborted. Nothing was saved."
    return None


def autodetect_port():
    ports = sorted(glob.glob("/dev/cu.usbserial-*"))
    if not ports:
        sys.exit("No /dev/cu.usbserial-* port found — the usbmodem ports are the "
                 "LG monitor, not the board. Plug in the UART port or pass --port.")
    return ports[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial device; default: first /dev/cu.usbserial-*")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    port = args.port or autodetect_port()

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = args.baud
    ser.timeout = 0.2
    # Staged before open() so bringing the port up doesn't reset the board and
    # throw away the piezo baselines the wizard is about to use.
    ser.dtr = False
    ser.rts = False

    try:
        ser.open()
    except serial.SerialException as err:
        sys.exit(f"Could not open {port}: {err}\n"
                 "If a `make monitor` is running in another terminal, quit it first (Ctrl+C).")

    print(f"Piezo remap on {port} @ {args.baud}. Tap the seat that lights up, one at a time.")
    print("Ctrl+C to give up (nothing is saved until all 8 sides are mapped).\n")

    ser.reset_input_buffer()
    ser.write(b"m\n")

    assigned = {}
    source_line = None
    outcome = "interrupted"
    deadline = time.monotonic() + SESSION_TIMEOUT_S

    try:
        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            kind, payload = classify(raw.decode("utf-8", "replace"))

            if kind == "assign":
                assigned[payload["side"]] = payload["gpio"]
            elif kind == "source":
                source_line = payload["line"]

            text = render(kind, payload)
            if text:
                print(text)

            if kind in ("done", "timeout", "abort"):
                outcome = kind
                break
        else:
            outcome = "session-timeout"
    except KeyboardInterrupt:
        print("\n  Interrupted.")
    finally:
        ser.close()

    if outcome != "done":
        sys.exit(f"\nRemap did not complete ({outcome}). The board kept its previous map.")

    print("\n  Saved to the board's NVS — survives reboots and OTA, no reflash needed.\n")
    for side in range(NUM_SIDES):
        print(f"    side {side}  ->  GPIO {assigned.get(side, '?')}")
    if source_line:
        print("\n  Record the as-built wiring in octagon_core.cpp:\n")
        print(f"    {source_line}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `.venv/bin/python3 -m pytest tests/test_map_piezos.py -v`
Expected: 7 passed.

- [ ] **Step 6: Wire up the Makefile and .gitignore**

In `Makefile`, add `map-piezos` to the `.PHONY` list, and add the target after `record`:

```make
map-piezos: check-port ## guided piezo→side remap (each side lights, you tap it)
	.venv/bin/python3 scripts/map_piezos.py --port $(PORT) --baud $(BAUD)
```

In `.gitignore`, add:

```
.pytest_cache/
```

- [ ] **Step 7: Bench check (operator)**

```bash
make flash-turn
make map-piezos
```

Expected: side 0 lights white, the terminal prompts for it, tapping that seat prints `ok side 0 -> GPIO n` and moves on. Deliberately tap an already-mapped seat once — expect the `!!` duplicate line and the same side re-prompting rather than the map advancing. After side 7, the summary and the source line print. Power-cycle and confirm the boot banner reports `Loaded calibrated piezo map from NVS` and taps land on the right seats.

- [ ] **Step 8: Commit**

```bash
git add scripts/map_piezos.py tests/conftest.py tests/test_map_piezos.py requirements.txt Makefile .gitignore
git commit -m "scripts: map_piezos.py + make map-piezos"
```

---

## Task 5: Move Wi-Fi credentials into a gitignored secrets.h

**Files:**
- Create: `firmware/turn_counter/secrets.example.h`
- Modify: `firmware/turn_counter/turn_counter.ino:15-18`
- Modify: `.gitignore`

- [ ] **Step 1: Ignore the real file first**

In `.gitignore`, add:

```
firmware/*/secrets.h
build/
```

Doing this before the file exists is the point — it can never be staged by accident.

- [ ] **Step 2: Add the committed template**

Create `firmware/turn_counter/secrets.example.h`:

```cpp
#pragma once

// Copy this file to secrets.h (same folder) and fill it in. secrets.h is
// gitignored — nothing here should ever be committed with real values.
//
// Without a secrets.h the sketch still builds and simply runs with the radio
// off: no Wi-Fi, no OTA, everything else identical. That's what keeps
// `make compile-all` green on a fresh clone.

#define WIFI_SSID     "your-network-here"
#define WIFI_PASSWORD "your-password-here"
#define OTA_HOSTNAME  "turn-counter"    // reachable at <hostname>.local
#define OTA_PASSWORD  "change-me"       // required to push an OTA update
```

- [ ] **Step 3: Replace the hardcoded constants in the sketch**

In `turn_counter.ino`, delete lines 15-18 (the four `const char*` definitions) and put in their place:

```cpp
// Wi-Fi/OTA credentials live in a gitignored secrets.h — copy secrets.example.h
// and fill it in. Absent that file the SSID is empty and setupWiFi() skips the
// radio entirely, so a fresh clone still compiles and runs.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID     ""
  #define WIFI_PASSWORD ""
  #define OTA_HOSTNAME  "turn-counter"
  #define OTA_PASSWORD  ""
#endif
```

- [ ] **Step 4: Compile with no secrets.h present**

Run: `make compile-all`
Expected: clean. This is the case that matters — it proves a fresh clone builds.

- [ ] **Step 5: Commit**

```bash
git add firmware/turn_counter/secrets.example.h firmware/turn_counter/turn_counter.ino .gitignore
git commit -m "turn_counter: move Wi-Fi/OTA credentials to a gitignored secrets.h"
```

---

## Task 6: Start OTA whenever the link comes up, not only at boot

**Files:**
- Modify: `firmware/turn_counter/turn_counter.ino:20` (constants), `:54` (state), `:424-470` (`setupWiFiAndOta`), `:491` (setup call), `:496-526` (loop)

- [ ] **Step 1: Add the retry constant and state**

Next to `WIFI_CONNECT_TIMEOUT_MS` (line 20), add:

```cpp
const uint32_t WIFI_RETRY_MS = 30000;   // how often loop() re-attempts a down link
```

Next to `bool otaActive = false;` (line 54), add:

```cpp
bool     otaReady = false;          // ArduinoOTA.begin() has run against the current link
uint32_t lastWifiAttemptMs = 0;
```

- [ ] **Step 2: Split setupWiFiAndOta into a connect and an OTA-start**

Replace the entire `setupWiFiAndOta()` function (lines 424-470) with:

```cpp
bool wifiConfigured() { return WIFI_SSID[0] != '\0'; }

// Installs the OTA handlers and opens the listener. Separate from the connect
// so it can run the first time the link comes up, however late that is.
void beginOta() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    otaActive = true;
    FastLED.clear();
    FastLED.show();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    uint8_t percent = (uint32_t)progress * 100 / total;
    renderProgressBar(percent, CRGB(0, 80, 255));
  });

  ArduinoOTA.onEnd([]() {
    fill_solid(leds, totalLeds(), CRGB(0, 200, 0));
    FastLED.show();
    delay(500);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    fill_solid(leds, totalLeds(), CRGB(255, 0, 0));
    FastLED.show();
    delay(2000);
    otaActive = false;
  });

  ArduinoOTA.begin();
  otaReady = true;
  Serial.printf("OTA ready at %s.local (", OTA_HOSTNAME);
  Serial.print(WiFi.localIP());
  Serial.println(")");
}

void setupWiFi() {
  if (!wifiConfigured()) {
    Serial.println("No secrets.h (or empty SSID) - WiFi and OTA disabled");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttemptMs = millis();

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
    beginOta();
  } else {
    Serial.println("WiFi not up yet - retrying in the background, OTA starts when it joins");
  }
}

// Non-blocking link maintenance. Before this existed a slow AP at boot meant no
// OTA until the table was power-cycled. Never delays, so tap latency is
// unaffected.
void serviceWiFi(uint32_t now) {
  if (!wifiConfigured()) return;

  if (WiFi.status() == WL_CONNECTED) {
    if (!otaReady) beginOta();
    return;
  }

  if (otaReady) {          // link dropped — tear the listener down so the
    ArduinoOTA.end();      // reconnect can rebind it against the new address
    otaReady = false;
    Serial.println("WiFi lost - OTA offline until it returns");
  }

  if (now - lastWifiAttemptMs < WIFI_RETRY_MS) return;
  lastWifiAttemptMs = now;
  WiFi.reconnect();
}
```

- [ ] **Step 3: Update the call in setup()**

In `setup()`, change `setupWiFiAndOta();` (line 491) to:

```cpp
  setupWiFi();
```

- [ ] **Step 4: Guard OTA handling and add the service call in loop()**

At the top of `loop()`, replace the bare `ArduinoOTA.handle();` with:

```cpp
  if (otaReady) ArduinoOTA.handle();
```

and add `serviceWiFi(now);` immediately after `uint32_t now = millis();`. The
`handleSerial()` call from Task 3 stays where it is, above that line — the
result should read:

```cpp
  handleSerial();

  uint32_t now = millis();

  serviceWiFi(now);

  tapsPoll(now);
```

- [ ] **Step 5: Compile**

Run: `make compile-all`
Expected: clean. If `ArduinoOTA.end()` is not declared in the installed ESP32
core, report that rather than deleting the call — losing the teardown means OTA
silently stops working after a Wi-Fi drop, which is the bug this task exists to
fix.

- [ ] **Step 6: Commit**

```bash
git add firmware/turn_counter/turn_counter.ino
git commit -m "turn_counter: start OTA when the link comes up, retry a dropped one"
```

---

## Task 7: scripts/ota_flash.py + `make ota` (TDD)

**Files:**
- Create: `tests/test_ota_flash.py`
- Create: `scripts/ota_flash.py`
- Modify: `Makefile`

- [ ] **Step 1: Write the failing tests**

Create `tests/test_ota_flash.py`:

```python
import pytest

from ota_flash import find_espota, parse_secrets


def test_parse_secrets_reads_defines():
    text = '''#pragma once
#define WIFI_SSID     "my network"
#define WIFI_PASSWORD "hunter2"
#define OTA_HOSTNAME  "turn-counter"
#define OTA_PASSWORD  "letsplayagame"
'''
    got = parse_secrets(text)
    assert got["OTA_PASSWORD"] == "letsplayagame"
    assert got["OTA_HOSTNAME"] == "turn-counter"
    assert got["WIFI_SSID"] == "my network"


def test_parse_secrets_ignores_comments_and_blanks():
    text = '// #define OTA_PASSWORD "not-this-one"\n#define OTA_PASSWORD "real"\n'
    assert parse_secrets(text)["OTA_PASSWORD"] == "real"


def test_find_espota_picks_the_newest_platform_by_version_not_string(tmp_path):
    # "3.9.0" sorts after "3.10.0" as a string — the newest install must still win.
    for version in ("3.9.0", "3.10.0"):
        tools = tmp_path / "esp32" / "hardware" / "esp32" / version / "tools"
        tools.mkdir(parents=True)
        (tools / "espota.py").write_text("")
    assert find_espota(tmp_path).parent.parent.name == "3.10.0"


def test_find_espota_raises_when_no_platform_installed(tmp_path):
    with pytest.raises(FileNotFoundError):
        find_espota(tmp_path)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `.venv/bin/python3 -m pytest tests/test_ota_flash.py -v`
Expected: collection error — `ModuleNotFoundError: No module named 'ota_flash'`.

- [ ] **Step 3: Write the script**

Create `scripts/ota_flash.py`:

```python
#!/usr/bin/env python3
"""Push a compiled turn_counter build to the table over Wi-Fi — run via `make ota`.

Wraps the ESP32 core's espota.py, which is what the Arduino IDE's "Network
Ports" upload uses under the hood. What this adds is the part that makes a
failed push diagnosable: it finds espota.py in whatever core version is
installed, reads the OTA password out of the gitignored secrets.h so it never
lands in a Makefile or shell history, and confirms the board is actually
answering on the OTA port before starting — the difference between a clear
"can't reach it" and a silent 60-second hang.

Recovery: if a push leaves the board unreachable (bad credentials, a crash
before WiFi comes up), fall back to USB with `make flash-turn`.
"""
import argparse
import re
import socket
import subprocess
import sys
from pathlib import Path

OTA_PORT = 3232
ARDUINO_PACKAGES = Path.home() / "Library" / "Arduino15" / "packages"
SECRETS = Path("firmware/turn_counter/secrets.h")

DEFINE_RE = re.compile(r'^\s*#define\s+(\w+)\s+"([^"]*)"', re.M)


def parse_secrets(text):
    """Pull `#define NAME "value"` pairs out of a secrets.h into a dict."""
    return {m.group(1): m.group(2) for m in DEFINE_RE.finditer(text)}


def find_espota(packages_root):
    """Newest installed ESP32 platform's espota.py.

    Sorted by version tuple, not lexically — "3.10.0" must beat "3.9.0".
    """
    found = list(Path(packages_root).glob("esp32/hardware/esp32/*/tools/espota.py"))
    if not found:
        raise FileNotFoundError(
            f"No espota.py under {packages_root}/esp32/hardware/esp32/*/tools/.\n"
            "Install the ESP32 core first: arduino-cli core install esp32:esp32"
        )

    def version_key(path):
        return tuple(int(p) if p.isdigit() else -1
                     for p in path.parent.parent.name.split("."))

    return sorted(found, key=version_key)[-1]


def port_open(host, port, timeout=3.0):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bin", required=True, help="compiled .bin to push")
    parser.add_argument("--host", help="board address; default: <OTA_HOSTNAME>.local from secrets.h")
    parser.add_argument("--secrets", default=str(SECRETS), help="path to secrets.h")
    args = parser.parse_args()

    binary = Path(args.bin)
    if not binary.is_file():
        sys.exit(f"No such build: {binary}\nRun `make ota`, which compiles before pushing.")

    secrets_path = Path(args.secrets)
    if not secrets_path.is_file():
        sys.exit(f"No {secrets_path} — copy firmware/turn_counter/secrets.example.h to it "
                 "and fill in your Wi-Fi and OTA details.\n"
                 "Without it the firmware runs with the radio off, so there's nothing to push to.")

    secrets = parse_secrets(secrets_path.read_text())
    password = secrets.get("OTA_PASSWORD", "")
    host = args.host or f"{secrets.get('OTA_HOSTNAME', 'turn-counter')}.local"

    try:
        espota = find_espota(ARDUINO_PACKAGES)
    except FileNotFoundError as err:
        sys.exit(str(err))

    try:
        address = socket.gethostbyname(host)
    except socket.gaierror:
        sys.exit(f"Can't resolve {host}.\n"
                 "mDNS may not be working across your network — find the board's IP in the "
                 "boot serial output (`make ping`) and pass it: make ota HOST=192.168.x.x")

    print(f"{host} -> {address}, checking OTA port {OTA_PORT}…")
    if not port_open(address, OTA_PORT):
        sys.exit(f"{address}:{OTA_PORT} is not answering.\n"
                 "The board resolved but isn't listening for OTA. Most likely it never joined "
                 "Wi-Fi (check `make ping` for the boot output), or it's on a different subnet.\n"
                 "Fall back to USB with `make flash-turn`.")

    cmd = [sys.executable, str(espota),
           "-i", address, "-p", str(OTA_PORT),
           "-a", password, "-f", str(binary), "-r", "-d"]
    print(f"Pushing {binary.name} ({binary.stat().st_size // 1024} KB)…\n")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        sys.exit(f"\nOTA upload failed (exit {result.returncode}). "
                 "A wrong OTA_PASSWORD shows up as an auth failure above; a mid-transfer drop "
                 "is safe to retry — the board keeps running the old firmware until a push "
                 "completes. USB fallback: make flash-turn")
    print("\nPushed. The board reboots into the new firmware on its own.")


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `.venv/bin/python3 -m pytest tests/ -v`
Expected: 11 passed (7 from Task 4, 4 here).

- [ ] **Step 5: Add the Makefile target**

Add `ota` to `.PHONY`, and add the target after `flash-turn`:

```make
ota: ## compile turn_counter and push it over Wi-Fi (needs firmware/turn_counter/secrets.h)
	arduino-cli compile $(VERBOSE) $(LIBS) --fqbn $(FQBN_TURN) --output-dir build/turn_counter firmware/turn_counter
	.venv/bin/python3 scripts/ota_flash.py --bin build/turn_counter/turn_counter.ino.bin $(if $(HOST),--host $(HOST),)
```

Note `ota` deliberately has no `check-port` dependency — the whole point is that
no USB cable is attached.

- [ ] **Step 6: Bench check (operator)**

```bash
cp firmware/turn_counter/secrets.example.h firmware/turn_counter/secrets.h
# edit secrets.h with the real SSID/password, pick an OTA password
make flash-turn        # one last USB flash to get the credentials onto the board
make ping              # confirm the boot output shows "WiFi connected: <ip>" and "OTA ready"
ping -c 3 turn-counter.local
make ota
```

Expected: the pre-flight resolves and finds port 3232 open, espota streams a
progress percentage, the rim shows a blue progress bar filling, then all-green,
then the board reboots into the new firmware.

Then test the reconnect path: power the board with the AP off (or the router
rebooting), wait for boot, bring the network back, and confirm within ~30 s the
serial shows the connect and `OTA ready` without a power cycle.

- [ ] **Step 7: Commit**

```bash
git add scripts/ota_flash.py tests/test_ota_flash.py Makefile
git commit -m "scripts: ota_flash.py + make ota"
```

---

## Task 8: Documentation

**Files:**
- Modify: `README.md:53`
- Modify: `turn_counter_design_doc.md` (config table ~line 638, §7.1 ~line 646, troubleshooting table)
- Modify: `bench_build_guide.md:221`

- [ ] **Step 1: README**

Replace the bullet at line 53:

```markdown
- Edit Wi-Fi credentials, mDNS hostname, and OTA password at the top of `turn_counter.ino` before the first flash
```

with:

```markdown
- Wi-Fi credentials, mDNS hostname and OTA password live in `firmware/turn_counter/secrets.h` — copy `secrets.example.h` to it and fill it in. That file is gitignored; without it the sketch still builds and just runs with the radio off.
```

And add to the Make shortcuts section:

```markdown
- `make map-piezos` — reassign piezos to sides by tapping. Each side lights white in turn; tap that seat. The result is stored on the board (NVS), so it survives reboots and OTA and needs no reflash. Use it when a tap lights the wrong seat.
- `make ota` — compile `turn_counter` and push it over Wi-Fi instead of USB. Needs `secrets.h`, and the board must already be running firmware that joined the network. `make ota HOST=192.168.x.x` if mDNS won't resolve `turn-counter.local`.
```

- [ ] **Step 2: Design doc config table**

Replace the three credential rows in the §7 config table with:

```markdown
| `WIFI_SSID` / `WIFI_PASSWORD` | (unset) | Your Wi-Fi, in `firmware/turn_counter/secrets.h` (gitignored; copy `secrets.example.h`). Unset = radio off |
| `OTA_HOSTNAME` | `turn-counter` | In `secrets.h`; reach the device at `turn-counter.local` |
| `OTA_PASSWORD` | `change-me` | In `secrets.h`; required to push firmware updates |
| `WIFI_RETRY_MS` | 30000 | How often a down link is retried; OTA starts whenever the link comes up |
```

- [ ] **Step 3: Design doc §7.1**

After the "To push an update from Arduino IDE" list, add (the inner fence below
is part of the text to insert):

````markdown
**To push an update from the command line**:

```bash
make ota                      # compiles, resolves turn-counter.local, pre-flights port 3232, pushes
make ota HOST=192.168.1.42    # same, when mDNS won't resolve
```

The device retries a failed or dropped Wi-Fi connection every 30 seconds and
starts OTA the moment it joins, so a router that was slow or down at boot no
longer means power-cycling the table to get OTA back.
````

- [ ] **Step 4: Design doc troubleshooting row**

Replace:

```markdown
| Tap on side 1 lights side 3 | Piezo wire mapping wrong | Check `PIEZO_PINS[]` order vs physical wiring |
```

with:

```markdown
| Tap on side 1 lights side 3 | Piezo wire mapping wrong | Run `make map-piezos` — each side lights in turn, you tap it, the corrected map is stored on the board. No reflash, no wire tracing |
```

- [ ] **Step 5: Bench build guide**

Replace line 221:

```markdown
- [ ] OTA (optional): set Wi-Fi creds, confirm `ping turn-counter.local` resolves
```

with:

```markdown
- [ ] Piezo map: `make map-piezos`, tap each lit side, confirm taps land on the right seats after a power cycle
- [ ] OTA (optional): copy `secrets.example.h` to `secrets.h` and fill it in, `make flash-turn` once over USB, confirm `ping turn-counter.local` resolves, then confirm `make ota` completes
```

- [ ] **Step 6: Rebuild the PDFs**

Run: `make pdf`
Expected: the design doc and bench guide PDFs regenerate without error.

- [ ] **Step 7: Commit**

```bash
git add README.md turn_counter_design_doc.md turn_counter_design_doc.pdf bench_build_guide.md bench_build_guide.pdf
git commit -m "docs: piezo remap wizard and the OTA workflow"
```

---

## Task 9: Final verification

- [ ] **Step 1: Full compile**

Run: `make compile-all`
Expected: all eight sketches link, with and without `secrets.h` present.

- [ ] **Step 2: Full test run**

Run: `.venv/bin/python3 -m pytest tests/ -v`
Expected: 11 passed.

- [ ] **Step 3: Confirm no secret is staged**

Run: `git status --porcelain firmware/turn_counter/secrets.h`
Expected: no output at all — the file exists on the bench machine but is ignored.

- [ ] **Step 4: Bench acceptance (operator)**

- Remap survives a power cycle and taps land on the correct seats.
- A wrong-seat tap during the wizard produces the duplicate warning and re-prompts.
- `make ota` completes and the board comes back running the pushed build.
- Killing Wi-Fi and restoring it brings OTA back within ~30 s without a power cycle.
- `make flash-turn` over USB still works as the recovery path.
