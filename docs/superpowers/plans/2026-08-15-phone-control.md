# Phone control (mode, brightness, off) — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Serve a phone-friendly web page from the table that sets game mode and LED brightness, turns the LEDs off and on, and shows live game status.

**Architecture:** `octagon_core` gains runtime, NVS-backed brightness. `turn_counter` gains a `tableLit` state plus three `apply*` functions that are the only way in. A new `web_ui.h`/`web_ui.cpp` pair inside the sketch folder owns HTTP and the page, talking to the game exclusively through a struct-and-callbacks interface, so neither side knows the other's internals. A host script generates a printable QR sticker for discovery.

**Tech Stack:** ESP32-S3 / Arduino (`WebServer` from the ESP32 core — no new library), FastLED, Preferences, arduino-cli + Make, Python 3 + segno for the QR, pytest.

**Spec:** `docs/superpowers/specs/2026-08-15-phone-control-design.md`

---

## Three hazards found while writing this plan

These are not in the spec. They were discovered working through the actual code,
and each is a silent bug rather than a crash — the kind that survives a compile
and shows up mid-game.

1. **`applyPower(true)` must not call `startPlay()`.** `startPlay()` clears
   `ready[]`. The spec promises that turning the table back on resumes a READY
   round with everyone's green intact. This is the same hazard the spec already
   flags for brightness, in a second place it doesn't mention. Both use
   `renderCurrent()`.
2. **Brightness must not write NVS on every slider tick.** The page throttles to
   one POST per 250 ms, so a five-second drag is ~20 NVS writes. Flash wear is
   real. Applying is immediate; persisting is deferred 2 s past the last change.
3. **`server.arg("value").toInt()` returns 0 for non-numeric input.** So
   `?value=abc` would be a silent, successful request for mode 0. Digits get
   validated explicitly before the conversion.

## A note on verification

Same split as the OTA work, for the same reason: last round's unit tests all
passed while a protocol assumption underneath them was wrong.

- **Host script** (`make_qr.py`) is pure and gets real TDD.
- **Firmware** is compile-gated plus operator bench checks, written out as
  commands with expected output. Do not report a firmware task verified on a
  compile alone — say "compiles, bench check pending".
- **The API is verified with `curl` before the page is trusted**, so a page bug
  and an API bug cannot hide behind each other.

`make compile-all` and every `make flash-*` are operator commands.

## File structure

**Created:**
- `firmware/turn_counter/web_ui.h` — the game↔HTTP interface: `TableState`, `TableConfig`, setter typedefs, three entry points
- `firmware/turn_counter/web_ui.cpp` — `WebServer`, JSON serialisation, request validation, the embedded page
- `scripts/make_qr.py` — URL normalisation + QR SVG/PDF generation
- `tests/test_make_qr.py` — normalisation edge cases

**Modified:**
- `firmware/libraries/octagon_core/src/octagon_core.h` — brightness API, drop the `BRIGHTNESS` constant
- `firmware/libraries/octagon_core/src/octagon_core.cpp` — brightness state, NVS load, deferred persist
- `firmware/turn_counter/turn_counter.ino` — `tableLit`, `renderCurrent()`, `applyMode/applyBrightness/applyPower`, `readState`, wake gesture, lifecycle wiring
- `Makefile` — `qr` target
- `requirements.txt` — segno
- `README.md`, `turn_counter_design_doc.md`, `bench_build_guide.md` — docs

---

## Task 1: Runtime brightness in octagon_core

**Files:**
- Modify: `firmware/libraries/octagon_core/src/octagon_core.h`
- Modify: `firmware/libraries/octagon_core/src/octagon_core.cpp`

- [ ] **Step 1: Replace the compile-time constant in the header**

In `octagon_core.h`, delete the `#define BRIGHTNESS 128` line and put in its place:

```cpp
// Brightness is runtime state, not a constant — see brightnessPercent() below.
#define BRIGHTNESS_DEFAULT_PCT 50   // 50% == raw 127, one step off the 128 this ran at for years
#define BRIGHTNESS_MIN_PCT     5    // floor, not zero: darkening the table entirely is the
                                    // on/off control's job, and a zeroed slider reads as broken
```

- [ ] **Step 2: Declare the brightness API in the header**

In `octagon_core.h`, next to `printPiezoMap()`:

```cpp
// LED brightness as a percentage, BRIGHTNESS_MIN_PCT..100. Persisted in NVS
// ("octagon"/"bri") so it survives a reboot and both sketches agree.
uint8_t brightnessPercent();

// Applies immediately; the NVS write is deferred (see brightnessPersistTick).
// Values outside the range are clamped, not rejected.
void setBrightnessPercent(uint8_t pct);

// Call once per loop with millis(). Writes a pending brightness change to NVS
// once it has settled, so dragging a slider costs one write, not twenty.
void brightnessPersistTick(uint32_t now);
```

- [ ] **Step 3: Implement it in the .cpp**

In `octagon_core.cpp`, add after the `MAP_SETTLE_MS` constant:

```cpp
static const uint16_t BRIGHTNESS_SETTLE_MS = 2000;  // quiet time before a slider change hits flash
```

and add above `octagonBegin()`:

```cpp
static uint8_t  briPct        = BRIGHTNESS_DEFAULT_PCT;
static bool     briDirty      = false;
static uint32_t briChangedMs  = 0;

uint8_t brightnessPercent() { return briPct; }

// FastLED wants 0-255. The floor is applied here too, so no code path can
// produce a dark table by way of the brightness control.
static uint8_t briRaw(uint8_t pct) {
  uint16_t raw = (uint16_t)pct * 255 / 100;
  uint16_t floorRaw = (uint16_t)BRIGHTNESS_MIN_PCT * 255 / 100;
  if (raw < floorRaw) raw = floorRaw;
  if (raw > 255) raw = 255;
  return (uint8_t)raw;
}

void setBrightnessPercent(uint8_t pct) {
  if (pct < BRIGHTNESS_MIN_PCT) pct = BRIGHTNESS_MIN_PCT;
  if (pct > 100) pct = 100;
  if (pct == briPct) return;
  briPct = pct;
  FastLED.setBrightness(briRaw(briPct));
  briDirty = true;               // the flash write waits for the drag to stop
  briChangedMs = millis();
}

void brightnessPersistTick(uint32_t now) {
  if (!briDirty || now - briChangedMs < BRIGHTNESS_SETTLE_MS) return;
  briDirty = false;
  sidePrefs.putUChar("bri", briPct);
  Serial.printf("LED brightness saved: %u%%\n", briPct);
}

static void loadBrightness() {
  uint8_t stored = sidePrefs.getUChar("bri", BRIGHTNESS_DEFAULT_PCT);
  briPct = (stored >= BRIGHTNESS_MIN_PCT && stored <= 100) ? stored : BRIGHTNESS_DEFAULT_PCT;
  Serial.printf("LED brightness: %u%%\n", briPct);
}
```

- [ ] **Step 4: Wire it into octagonBegin**

In `octagonBegin()`, change the FastLED setup block. Replace:

```cpp
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
```

with:

```cpp
  loadBrightness();
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(briRaw(briPct));
```

`loadBrightness()` must come after `sidePrefs.begin("octagon", false)`, which it
does — that is the first line of `octagonBegin()`.

- [ ] **Step 5: Call the persist tick from both sketches**

In `firmware/eight/eight.ino`, change `loop()` to:

```cpp
void loop() {
  handleSerial();
  uint32_t now = millis();
  brightnessPersistTick(now);
  tapsPoll(now);
  delay(5);
}
```

In `firmware/turn_counter/turn_counter.ino`, in `loop()`, add immediately after
the existing `serviceWiFi(now);` line:

```cpp
  brightnessPersistTick(now);
```

- [ ] **Step 6: Compile**

Run: `make compile-all`
Expected: all sketches link.

Removing the `BRIGHTNESS` define is safe, and this was checked rather than
assumed — it is referenced in exactly two places in the tree:
`octagon_core.cpp:343`, which Step 4 replaces, and `firmware/tap_light/tap_light.ino`,
which defines its own copy at 255 and does not include `octagon_core.h`. The
other sketches that do include the library (`eight`, `piezo_test`,
`piezo_stream`, `turn_counter`) never reference it.

- [ ] **Step 7: Commit**

```bash
git add firmware/libraries/octagon_core/src/octagon_core.h \
        firmware/libraries/octagon_core/src/octagon_core.cpp \
        firmware/eight/eight.ino firmware/turn_counter/turn_counter.ino
git commit -m "octagon_core: runtime NVS-backed brightness"
```

---

## Task 2: Off state, the wake gesture, and the apply* entry points

**Files:**
- Modify: `firmware/turn_counter/turn_counter.ino`

- [ ] **Step 1: Add the state flag**

Next to `bool otaReady = false;`:

```cpp
bool     tableLit = true;   // false = LEDs dark, game state preserved. Never persisted:
                            // plug in = on, so a power cut can't leave it looking broken.
```

- [ ] **Step 2: Add renderCurrent() and make startPlay() respect the off state**

Insert `renderCurrent()` immediately above the existing `startPlay()`:

```cpp
// Redraw whatever should be on screen, without changing any game state. This is
// the safe repaint: startPlay() clears ready[], which would wipe a READY round
// in progress if it were used for a brightness or power change.
void renderCurrent() {
  if (!tableLit) { renderOff(); return; }
  if (inSetupMode) return;            // setup re-renders from loop() every pass
  if (readyMode()) renderReady(); else renderTurn();
}
```

and change `startPlay()` to bail out while dark:

```cpp
void startPlay() {
  if (!tableLit) { renderOff(); return; }
  if (readyMode()) {
    for (uint8_t s = 0; s < NUM_SIDES; s++) ready[s] = false;
    renderReady();
  } else {
    renderTurn();
  }
}
```

- [ ] **Step 3: Add the three apply* functions**

Insert directly below `startPlay()`:

```cpp
// The only ways to change mode, brightness and power from outside the tap flow.
// Each returns false only for "valid but refused right now" — range checking is
// the caller's job, so the web layer can tell 400 from 409.

bool applyMode(uint8_t newMode) {
  if (newMode >= MODE_COUNT) return false;
  if (inSetupMode) return false;   // someone is mid-gesture at the table; don't
                                   // yank the mode out from under the demo dial
  gameMode = newMode;
  if (gameMode == MODE_ARB && joinCount == 0) rebuildJoinOrderFromRoster();
  if (currentSide < 0 || currentSide >= NUM_SIDES || !sideActive[currentSide]) {
    currentSide = (gameMode == MODE_ARB && joinCount > 0) ? joinOrder[0] : firstActiveSide();
  }
  prefs.putUChar("mode", gameMode);
  prefs.putUChar("curside", currentSide);
  startPlay();                     // a mode change SHOULD clear ready[]
  Serial.printf("Mode set to %s\n", MODE_NAMES[gameMode]);
  return true;
}

bool applyBrightness(uint8_t pct) {
  setBrightnessPercent(pct);
  renderCurrent();                 // visible at once, without disturbing ready[]
  return true;
}

bool applyPower(bool lit) {
  if (lit == tableLit) return true;
  if (!lit && inSetupMode) {
    abortSetupMode();              // restores the previous roster and mode, writes nothing
  }
  tableLit = lit;
  if (lit) renderCurrent(); else renderOff();   // NOT startPlay() — see above
  Serial.printf("Table %s\n", lit ? "on" : "off");
  return true;
}
```

`applyMode` and `applyPower` are referenced by `commitTap()` and by Task 3, and
must be defined above both. `startPlay()` sits above `advanceTurn()`, which is
above `commitTap()`, so inserting here satisfies that.

- [ ] **Step 4: Add the wake gesture to commitTap**

At the very top of `commitTap()`, before the `if (inSetupMode)` block:

```cpp
  if (!tableLit) {
    // Dark: the only gesture that does anything is the wake burst — four fast
    // taps on one side, the same burst that opens setup while lit. The two
    // states are mutually exclusive, so there's no ambiguity. Single taps stay
    // inert, so bumping the table doesn't relight it.
    if (registerTapForSetupGesture(side, whenMs)) {
      firstTapInBurstMs = 0;   // consume the burst, so waking never also opens
      tapsInBurst = 0;         // setup or passes a turn
      burstSide = -1;
      applyPower(true);
    }
    return;
  }
```

- [ ] **Step 5: Compile**

Run: `make compile-all`
Expected: clean.

- [ ] **Step 6: Bench check (operator)**

```bash
make flash-turn
make monitor
```

The table behaves exactly as before — nothing yet turns it off, so this only
proves the refactor is inert. Confirm turns still pass and the 4-tap setup
gesture still opens setup.

- [ ] **Step 7: Commit**

```bash
git add firmware/turn_counter/turn_counter.ino
git commit -m "turn_counter: off state, wake gesture, and apply* entry points"
```

---

## Task 3: The web_ui interface, GET endpoints, and lifecycle

**Files:**
- Create: `firmware/turn_counter/web_ui.h`
- Create: `firmware/turn_counter/web_ui.cpp`
- Modify: `firmware/turn_counter/turn_counter.ino`

- [ ] **Step 1: Write the interface header**

Create `firmware/turn_counter/web_ui.h`:

```cpp
#pragma once
#include <Arduino.h>

// The phone UI, and the only thing the game knows about HTTP.
//
// The boundary: hardware facts (NUM_SIDES, PLAYER_COLORS) web_ui reads straight
// from octagon_core, because those describe the table. Game facts arrive only
// through this interface, so web_ui never sees joinOrder or prevRosterMask, and
// the game never sees a request object.

struct TableState {
  uint8_t mode;               // index into TableConfig::modeNames
  uint8_t brightnessPercent;  // 5..100, retained while off
  bool    lit;                // false = table dark, game state preserved
  int8_t  currentSide;        // -1 when the mode has no single current seat
  uint8_t rosterMask;         // bit s = seat s is in
  uint8_t readyMask;          // bit s = seat s is green (READY only)
  bool    inSetupMode;
};

struct TableConfig {
  const char* const* modeNames;
  uint8_t modeCount;
};

typedef void (*StateReader)(TableState &out);
typedef bool (*ModeSetter)(uint8_t mode);           // false = valid but refused
typedef bool (*BrightnessSetter)(uint8_t percent);  // false = valid but refused
typedef bool (*PowerSetter)(bool lit);              // always true today

void webUiBegin(const TableConfig &cfg, StateReader read, ModeSetter setMode,
                BrightnessSetter setBrightness, PowerSetter setPower);
void webUiHandle();   // call once per loop
void webUiEnd();      // on Wi-Fi loss, so the socket rebinds on reconnect
```

- [ ] **Step 2: Write the server with its two GET endpoints**

Create `firmware/turn_counter/web_ui.cpp`:

```cpp
#include "web_ui.h"
#include <WebServer.h>
#include <octagon_core.h>

static WebServer server(80);
static TableConfig cfg = {nullptr, 0};
static StateReader      readState   = nullptr;
static ModeSetter       modeSetter  = nullptr;
static BrightnessSetter briSetter   = nullptr;
static PowerSetter      pwrSetter   = nullptr;
static bool started = false;

// Hand-rolled JSON: six fields don't justify a library's flash cost. Fixed
// buffers, no heap churn in a handler that runs from the game loop.
static void sendState(int code) {
  TableState s;
  readState(s);
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"mode\":%u,\"brightness\":%u,\"lit\":%s,\"currentSide\":%d,"
           "\"roster\":%u,\"ready\":%u,\"setup\":%s}",
           s.mode, s.brightnessPercent, s.lit ? "true" : "false", s.currentSide,
           s.rosterMask, s.readyMask, s.inSetupMode ? "true" : "false");
  server.send(code, "application/json", buf);
}

static void handleState() { sendState(200); }

// Static facts, fetched once by the page. Split from /api/state so the 2 s poll
// stays small, and so mode names and seat colours are single-sourced from the
// firmware instead of duplicated into the page.
static void handleConfig() {
  char buf[512];
  int n = snprintf(buf, sizeof(buf), "{\"sides\":%u,\"colors\":[", NUM_SIDES);
  for (uint8_t i = 0; i < NUM_SIDES && n < (int)sizeof(buf); i++) {
    n += snprintf(buf + n, sizeof(buf) - n, "%s\"#%02X%02X%02X\"", i ? "," : "",
                  PLAYER_COLORS[i].r, PLAYER_COLORS[i].g, PLAYER_COLORS[i].b);
  }
  if (n < (int)sizeof(buf)) n += snprintf(buf + n, sizeof(buf) - n, "],\"modes\":[");
  for (uint8_t i = 0; i < cfg.modeCount && n < (int)sizeof(buf); i++) {
    n += snprintf(buf + n, sizeof(buf) - n, "%s\"%s\"", i ? "," : "", cfg.modeNames[i]);
  }
  if (n < (int)sizeof(buf)) snprintf(buf + n, sizeof(buf) - n, "]}");
  server.send(200, "application/json", buf);
}

void webUiBegin(const TableConfig &c, StateReader read, ModeSetter setMode,
                BrightnessSetter setBrightness, PowerSetter setPower) {
  if (started) return;
  cfg = c;
  readState = read;
  modeSetter = setMode;
  briSetter = setBrightness;
  pwrSetter = setPower;

  server.on("/api/config", HTTP_GET, handleConfig);
  server.on("/api/state",  HTTP_GET, handleState);
  server.onNotFound([]() { server.send(404, "text/plain", "no such endpoint"); });

  server.begin();
  started = true;
  Serial.println("Web UI ready on port 80");
}

void webUiHandle() { if (started) server.handleClient(); }

void webUiEnd() {
  if (!started) return;
  server.stop();
  started = false;
}
```

- [ ] **Step 3: Add the state reader to turn_counter**

In `turn_counter.ino`, add `#include "web_ui.h"` below the existing includes, and
insert this above `handleSerial()`:

```cpp
// Fills the snapshot the phone page renders from. READY has no single current
// seat, so it reports -1 rather than a stale one.
void readTableState(TableState &s) {
  s.mode              = gameMode;
  s.brightnessPercent = brightnessPercent();
  s.lit               = tableLit;
  s.currentSide       = readyMode() ? -1 : currentSide;
  s.rosterMask        = rosterMask();
  s.readyMask         = 0;
  for (uint8_t i = 0; i < NUM_SIDES; i++) if (ready[i]) s.readyMask |= (1 << i);
  s.inSetupMode       = inSetupMode;
}

const TableConfig WEB_CONFIG = {MODE_NAMES, MODE_COUNT};
```

- [ ] **Step 4: Start and stop the web UI with the link**

The OTA listener and the web UI have identical lifetimes — both bind to the
current Wi-Fi link. Rename the flag so it says so, and start both together.

Replace `bool otaReady = false;` with:

```cpp
bool     netServicesUp = false;   // OTA listener + web UI are bound to the current link
```

In `beginOta()`, replace the trailing `otaReady = true;` with:

```cpp
  webUiBegin(WEB_CONFIG, readTableState, applyMode, applyBrightness, applyPower);
  netServicesUp = true;
```

In `serviceWiFi()`, replace the connected branch and the teardown branch:

```cpp
  if (WiFi.status() == WL_CONNECTED) {
    if (!netServicesUp) beginOta();
    return;
  }

  if (netServicesUp) {       // link dropped — tear both down so the reconnect
    ArduinoOTA.end();        // rebinds them against the new address
    webUiEnd();
    netServicesUp = false;
    Serial.println("WiFi lost - OTA and web UI offline until it returns");
  }
```

In `loop()`, replace `if (otaReady) ArduinoOTA.handle();` with:

```cpp
  if (netServicesUp) {
    ArduinoOTA.handle();
    webUiHandle();
  }
```

- [ ] **Step 5: Print the MAC at boot for the DHCP reservation**

In `setupWiFi()`, immediately after the `Serial.print("WiFi connected: ");`
/ `Serial.println(WiFi.localIP());` pair:

```cpp
    Serial.printf("MAC %s (use this for a DHCP reservation)\n", WiFi.macAddress().c_str());
```

- [ ] **Step 6: Compile**

Run: `make compile-all`
Expected: clean. Report the new `turn_counter` flash percentage — it was 67%; anything above 90% should be raised rather than absorbed.

- [ ] **Step 7: Bench check (operator) — API before page, always**

```bash
make flash-turn
make ping                                        # note the IP and the MAC line
curl -s http://turn-counter.local/api/config     # sides, 8 colors, 4 mode names
curl -s http://turn-counter.local/api/state      # mode/brightness/lit/currentSide/roster/ready/setup
curl -s -o /dev/null -w '%{http_code}\n' http://turn-counter.local/nope   # 404
```

- [ ] **Step 8: Commit**

```bash
git add firmware/turn_counter/web_ui.h firmware/turn_counter/web_ui.cpp \
        firmware/turn_counter/turn_counter.ino
git commit -m "turn_counter: web UI scaffolding and read-only endpoints"
```

---

## Task 4: The POST endpoints

**Files:**
- Modify: `firmware/turn_counter/web_ui.cpp`

- [ ] **Step 1: Add strict numeric parsing**

In `web_ui.cpp`, add above `sendState()`:

```cpp
// String::toInt() returns 0 for non-numeric input, which would quietly turn
// "?value=abc" into a successful request for mode 0. Validate the digits first.
static bool parseUInt(const String &s, long &out) {
  if (s.length() == 0 || s.length() > 3) return false;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (!isDigit(s[i])) return false;
  }
  out = s.toInt();
  return true;
}
```

- [ ] **Step 2: Add the three handlers**

In `web_ui.cpp`, add below `handleConfig()`:

```cpp
// Range checking lives here, not in the setters, because this side knows the
// bounds — and it's what lets a rejection be 400 (impossible) rather than 409
// (possible, but not right now).

static void handleMode() {
  long v;
  if (!server.hasArg("value") || !parseUInt(server.arg("value"), v)) {
    server.send(400, "text/plain", "value must be a number");
    return;
  }
  if (v >= cfg.modeCount) {
    server.send(400, "text/plain", "mode out of range");
    return;
  }
  if (!modeSetter((uint8_t)v)) {
    server.send(409, "text/plain", "setup in progress at the table");
    return;
  }
  sendState(200);
}

static void handleBrightness() {
  long v;
  if (!server.hasArg("value") || !parseUInt(server.arg("value"), v)) {
    server.send(400, "text/plain", "value must be a number");
    return;
  }
  if (v < 5 || v > 100) {
    server.send(400, "text/plain", "brightness must be 5-100");
    return;
  }
  if (!briSetter((uint8_t)v)) {
    server.send(409, "text/plain", "busy");
    return;
  }
  sendState(200);
}

static void handlePower() {
  String v = server.arg("value");
  if (v != "on" && v != "off") {
    server.send(400, "text/plain", "value must be on or off");
    return;
  }
  pwrSetter(v == "on");
  sendState(200);
}
```

- [ ] **Step 3: Register them**

In `webUiBegin()`, below the existing `server.on(...)` calls and above
`server.onNotFound(...)`:

```cpp
  server.on("/api/mode",       HTTP_POST, handleMode);
  server.on("/api/brightness", HTTP_POST, handleBrightness);
  server.on("/api/power",      HTTP_POST, handlePower);
```

- [ ] **Step 4: Compile**

Run: `make compile-all`
Expected: clean.

- [ ] **Step 5: Bench check (operator) — every endpoint, including the refusals**

```bash
make flash-turn

curl -s -X POST "http://turn-counter.local/api/mode?value=1"          # CCW, table changes
curl -s -X POST "http://turn-counter.local/api/brightness?value=20"   # visibly dimmer
curl -s -X POST "http://turn-counter.local/api/power?value=off"       # goes dark
curl -s -X POST "http://turn-counter.local/api/power?value=on"        # same seat resumes

# these must all be 400, not silent successes
curl -s -w ' -> %{http_code}\n' -X POST "http://turn-counter.local/api/mode?value=9"
curl -s -w ' -> %{http_code}\n' -X POST "http://turn-counter.local/api/mode?value=abc"
curl -s -w ' -> %{http_code}\n' -X POST "http://turn-counter.local/api/brightness?value=0"
curl -s -w ' -> %{http_code}\n' -X POST "http://turn-counter.local/api/power?value=maybe"
```

Then the behavioural checks that only a person at the table can do:

- Enter READY mode, tap two seats green, then `POST /api/brightness?value=30`.
  The two greens must survive.
- Turn it off with the phone mid-READY, turn it back on. Same greens.
- 4-tap one side to open setup, then `POST /api/mode?value=0` — expect **409**
  and the demo dial undisturbed.
- Tap single taps on several sides while off — nothing lights. Then 4 rapid taps
  on one side — it wakes, to the same state, and is **not** in setup mode.
- Power-cycle while off — it must boot lit.

- [ ] **Step 6: Commit**

```bash
git add firmware/turn_counter/web_ui.cpp
git commit -m "turn_counter: web UI control endpoints"
```

---

## Task 5: The page

**Files:**
- Modify: `firmware/turn_counter/web_ui.cpp`

- [ ] **Step 1: Add the page as a PROGMEM literal**

In `web_ui.cpp`, add below the `parseUInt()` helper. No external stylesheets,
fonts or scripts — the board has no internet path, so anything external simply
would not load.

```cpp
static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Turn Counter</title>
<style>
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;padding:20px;background:#111;color:#eee;
     font:16px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
h1{font-size:19px;margin:0 0 18px;letter-spacing:.02em}
.card{background:#1c1c1e;border-radius:14px;padding:16px;margin-bottom:14px}
.lbl{font-size:13px;text-transform:uppercase;letter-spacing:.08em;color:#8e8e93;margin-bottom:10px}
button{font:inherit;color:inherit;background:#2c2c2e;border:2px solid transparent;
       border-radius:11px;padding:14px;width:100%;text-align:left;margin-bottom:8px}
button:last-child{margin-bottom:0}
button[aria-pressed="true"]{border-color:#0a84ff;background:#0a84ff22}
#power{text-align:center;font-weight:600}
#power[data-lit="false"]{background:#3a3a3c;color:#8e8e93}
input[type=range]{width:100%;height:34px}
.row{display:flex;align-items:center;gap:10px}
.dots{display:flex;gap:7px;margin-top:12px}
.dot{width:22px;height:22px;border-radius:50%;border:2px solid #3a3a3c;
     display:flex;align-items:center;justify-content:center;font-size:11px;color:#000}
.swatch{width:15px;height:15px;border-radius:4px;flex:none}
#banner{background:#ff9f0a22;border:1px solid #ff9f0a;color:#ff9f0a;
        border-radius:11px;padding:11px;margin-bottom:14px;font-size:14px}
.hide{display:none}
#err{color:#ff453a}
.note{font-size:12px;color:#8e8e93;margin-top:9px}
</style></head><body>
<h1>Turn Counter</h1>
<div id="banner" class="hide">Setup in progress at the table — mode changes are locked.</div>
<div class="card"><button id="power" data-lit="true">Turn off</button></div>
<div class="card">
  <div class="lbl">Now</div>
  <div class="row"><div class="swatch" id="swatch"></div><div id="now">…</div></div>
  <div class="dots" id="dots"></div>
</div>
<div class="card"><div class="lbl">Mode</div><div id="modes"></div></div>
<div class="card">
  <div class="lbl">Brightness <span id="bripct"></span></div>
  <input type="range" id="bri" min="5" max="100" step="5">
  <div class="note">All-on scenes are already at the power cap, so above ~60% only
  single-seat play gets brighter.</div>
</div>
<div id="err"></div>
<script>
let cfg=null, lastLocal=0, pending=null, dragging=false;
const $=i=>document.getElementById(i);

async function post(path){
  try{
    const r=await fetch(path,{method:'POST'});
    if(r.status===409){ $('err').textContent='Table is in setup mode — try again in a moment.'; return; }
    if(!r.ok){ $('err').textContent='Rejected: '+await r.text(); return; }
    $('err').textContent=''; render(await r.json());
  }catch(e){ $('err').textContent='Table not responding.'; }
}

function render(s){
  $('power').textContent = s.lit ? 'Turn off' : 'Turn on';
  $('power').dataset.lit = s.lit;
  $('banner').classList.toggle('hide', !s.setup);

  document.querySelectorAll('#modes button').forEach((b,i)=>
    b.setAttribute('aria-pressed', i===s.mode));

  if(s.currentSide<0){
    $('now').textContent = s.lit ? cfg.modes[s.mode] : 'Off';
    $('swatch').style.background='transparent';
  }else{
    $('now').textContent = 'Seat '+s.currentSide+' — '+cfg.modes[s.mode]+(s.lit?'':' (off)');
    $('swatch').style.background = cfg.colors[s.currentSide];
  }

  $('dots').innerHTML='';
  for(let i=0;i<cfg.sides;i++){
    const d=document.createElement('div'); d.className='dot'; d.textContent=i;
    const inRoster = (s.roster>>i)&1;
    if(inRoster) d.style.background = ((s.ready>>i)&1) ? '#30d158' : cfg.colors[i];
    else d.style.opacity=.25;
    $('dots').appendChild(d);
  }

  // Don't fight the finger: ignore polled brightness right after a local change.
  if(!dragging && Date.now()-lastLocal>2000){ $('bri').value=s.brightness; }
  $('bripct').textContent=$('bri').value+'%';
}

async function poll(){
  try{
    const r=await fetch('/api/state');
    if(r.ok){ $('err').textContent=''; render(await r.json()); }
  }catch(e){ $('err').textContent='Table not responding.'; }
}

(async ()=>{
  cfg = await (await fetch('/api/config')).json();
  cfg.modes.forEach((name,i)=>{
    const b=document.createElement('button');
    b.textContent=name;
    b.onclick=()=>post('/api/mode?value='+i);
    $('modes').appendChild(b);
  });
  $('power').onclick=()=>post('/api/power?value='+
    ($('power').dataset.lit==='true'?'off':'on'));

  const bri=$('bri');
  const send=()=>{ lastLocal=Date.now(); post('/api/brightness?value='+bri.value); };
  bri.oninput=()=>{
    $('bripct').textContent=bri.value+'%';
    dragging=true; lastLocal=Date.now();
    if(pending) return;                       // throttle to 1 POST / 250 ms
    pending=setTimeout(()=>{ pending=null; send(); },250);
  };
  bri.onchange=()=>{ dragging=false; send(); };

  await poll();
  setInterval(poll,2000);
})();
</script></body></html>)HTML";
```

- [ ] **Step 2: Serve it**

In `web_ui.cpp`, add below `handlePower()`:

```cpp
static void handleRoot() {
  server.send_P(200, "text/html", PAGE_HTML);
}
```

and register it in `webUiBegin()`, above the `/api/config` line:

```cpp
  server.on("/", HTTP_GET, handleRoot);
```

- [ ] **Step 3: Compile**

Run: `make compile-all`
Expected: clean. Report the final `turn_counter` flash percentage.

- [ ] **Step 4: Bench check (operator)**

```bash
make flash-turn
```

Open `http://turn-counter.local/` on an iPhone and `http://<ip>/` on an Android
phone. Confirm:

- Mode buttons show the four real names and highlight the active one.
- Tapping a seat at the table updates "Seat N" on the phone within ~2 s.
- The brightness slider moves smoothly without snapping back mid-drag.
- Turn off → button reads "Turn on", table dark, dots and mode stay visible.
- Enter setup at the table → the orange banner appears and mode taps show the
  setup message.
- Kill Wi-Fi → the page shows "Table not responding" rather than freezing.

- [ ] **Step 5: Commit**

```bash
git add firmware/turn_counter/web_ui.cpp
git commit -m "turn_counter: the phone page"
```

---

## Task 6: QR sticker generator (TDD)

**Files:**
- Create: `tests/test_make_qr.py`
- Create: `scripts/make_qr.py`
- Modify: `requirements.txt`, `Makefile`

- [ ] **Step 1: Add the dependency**

In `requirements.txt`, append:

```
segno~=1.6
```

Install it: `.venv/bin/python3 -m pip install -r requirements.txt`

- [ ] **Step 2: Write the failing tests**

Create `tests/test_make_qr.py`:

```python
import pytest

from make_qr import normalize_url


def test_bare_ip_gets_scheme_and_root_path():
    assert normalize_url("192.168.0.50") == "http://192.168.0.50/"


def test_bare_hostname_gets_scheme_and_root_path():
    assert normalize_url("turn-counter.local") == "http://turn-counter.local/"


def test_existing_scheme_is_preserved():
    assert normalize_url("https://turn-counter.local/") == "https://turn-counter.local/"


def test_existing_path_is_kept():
    assert normalize_url("http://192.168.0.50/panel") == "http://192.168.0.50/panel"


def test_surrounding_whitespace_is_stripped():
    assert normalize_url("  192.168.0.50\n") == "http://192.168.0.50/"


def test_empty_is_rejected():
    with pytest.raises(ValueError):
        normalize_url("")


def test_internal_whitespace_is_rejected():
    with pytest.raises(ValueError):
        normalize_url("192.168.0.50 /panel")


def test_non_web_scheme_is_rejected():
    # A QR that opens a telnet handler is not what anyone stuck under a table wants.
    with pytest.raises(ValueError):
        normalize_url("ftp://192.168.0.50/")


def test_scheme_with_no_host_is_rejected():
    with pytest.raises(ValueError):
        normalize_url("http://")
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `.venv/bin/python3 -m pytest tests/test_make_qr.py -v`
Expected: collection error — `ModuleNotFoundError: No module named 'make_qr'`.

- [ ] **Step 4: Write the script**

Create `scripts/make_qr.py`:

```python
#!/usr/bin/env python3
"""Generate a printable QR sticker for the table's web UI — run via `make qr`.

Discovery needs three layers because no single one reaches every phone: mDNS
(`turn-counter.local`) covers Apple devices, a DHCP reservation gives Android a
stable IP, and this is the third — something physical to stick under the table
so a guest can reach the controls without being told an address.

Encode whichever address actually works on your network:

    make qr URL=192.168.0.50
    make qr URL=turn-counter.local

Outputs a scalable SVG plus a print-ready PDF with the URL spelled out
underneath, since a QR nobody can read back is hard to debug.
"""
import argparse
import sys
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit

try:
    import segno
except ImportError:
    sys.exit("segno not installed — run: .venv/bin/python3 -m pip install -r requirements.txt")

SVG_OUT = Path("doc-src/table_qr.svg")
PDF_OUT = Path("table_qr.pdf")


def normalize_url(value):
    """Turn user input into a URL a phone camera will open.

    Bare hosts gain `http://` and a root path; an existing http/https scheme and
    path are preserved. Anything else raises ValueError, because a QR that opens
    the wrong kind of handler is worse than no QR.
    """
    v = (value or "").strip()
    if not v:
        raise ValueError("URL is empty — pass one, e.g. make qr URL=192.168.0.50")
    if any(c.isspace() for c in v):
        raise ValueError(f"URL contains whitespace: {v!r}")

    if "://" not in v:
        v = "http://" + v

    parts = urlsplit(v)
    if parts.scheme not in ("http", "https"):
        raise ValueError(f"unsupported scheme {parts.scheme!r} — use http or https")
    if not parts.netloc:
        raise ValueError(f"no host in URL: {value!r}")

    return urlunsplit((parts.scheme, parts.netloc, parts.path or "/", parts.query, ""))


def write_svg(url, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    # Error correction 'h' tolerates ~30% damage — this is going under a table.
    segno.make(url, error="h").save(str(path), kind="svg", scale=10, border=2,
                                    dark="#000000", light="#ffffff")
    return path


def write_pdf(url, svg_path, path):
    from weasyprint import HTML
    html = f"""<html><head><style>
      @page {{ size: 90mm 110mm; margin: 8mm; }}
      body {{ font-family: -apple-system, sans-serif; text-align: center; }}
      img {{ width: 100%; }}
      .u {{ font-family: monospace; font-size: 11pt; margin-top: 4mm; word-break: break-all; }}
      .c {{ font-size: 9pt; color: #666; margin-top: 2mm; }}
    </style></head><body>
      <img src="{svg_path.name}">
      <div class="u">{url}</div>
      <div class="c">Turn counter — mode, brightness, on/off</div>
    </body></html>"""
    HTML(string=html, base_url=str(svg_path.parent)).write_pdf(str(path))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--url", required=True,
                        help="address the QR should open, e.g. 192.168.0.50")
    parser.add_argument("--svg", default=str(SVG_OUT))
    parser.add_argument("--pdf", default=str(PDF_OUT))
    args = parser.parse_args()

    try:
        url = normalize_url(args.url)
    except ValueError as err:
        sys.exit(str(err))

    svg = write_svg(url, Path(args.svg))
    pdf = write_pdf(url, svg, Path(args.pdf))
    print(f"{url}\n  {svg}\n  {pdf}  — print, trim, stick under the table")


if __name__ == "__main__":
    main()
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `.venv/bin/python3 -m pytest tests/ -v`
Expected: 20 passed (7 map_piezos + 4 ota_flash + 9 here).

- [ ] **Step 6: Add the Makefile target and ignore the output**

Add `qr` to `.PHONY`, and add the target after `map-piezos`:

```make
qr: ## printable QR sticker for the web UI (make qr URL=192.168.0.50)
	@test -n "$(URL)" || { echo "Pass an address: make qr URL=192.168.0.50"; exit 1; }
	.venv/bin/python3 scripts/make_qr.py --url "$(URL)"
```

- [ ] **Step 7: Generate one for real**

Run: `make qr URL=turn-counter.local`
Expected: prints the normalised URL and two paths; `table_qr.pdf` opens and the code scans with a phone camera.

- [ ] **Step 8: Commit**

```bash
git add scripts/make_qr.py tests/test_make_qr.py requirements.txt Makefile
git commit -m "scripts: printable QR sticker for the web UI"
```

---

## Task 7: Documentation

**Files:**
- Modify: `README.md`, `turn_counter_design_doc.md`, `bench_build_guide.md`

- [ ] **Step 1: README — make targets**

In the `make` shortcuts code block, after the `make map-piezos` line:

```
make qr URL=...     # printable QR sticker pointing at the table's web UI
```

- [ ] **Step 2: README — a section on the phone UI**

After the `make ota` paragraph, add:

```markdown
### Phone control

With Wi-Fi configured, the table serves a control page on port 80: game mode,
LED brightness, and an on/off switch, plus live status showing whose turn it is
and who's seated.

- **iPhone/iPad**: `http://turn-counter.local/`
- **Android**: use the IP — Android has no system-wide mDNS resolver, so `.local`
  won't resolve. Give the board a DHCP reservation on your router so the address
  stops moving; the firmware prints its MAC at boot for exactly this.
- **Guests**: `make qr URL=<address>` prints a sticker for under the table.

There's no authentication. Anyone who can reach the table can change these three
settings — at a game table that's the point — and nothing destructive is exposed.

Off is separate from brightness: the LEDs go dark but mode, roster, whose turn it
is and everyone's ready flags are all preserved, and brightness keeps its value.
It isn't persisted, so the table always boots lit. To wake it without a phone,
tap one side four times quickly — the same burst that opens setup while it's lit.
```

- [ ] **Step 3: Design doc — config table**

Replace the `BRIGHTNESS` row:

```markdown
| `BRIGHTNESS` | 128 | Lower if too bright, max 255 |
```

with:

```markdown
| `BRIGHTNESS_DEFAULT_PCT` | 50 | Startup brightness before any phone change; runtime value lives in NVS `"octagon"/"bri"` |
| `BRIGHTNESS_MIN_PCT` | 5 | Slider floor. Going fully dark is the on/off control's job, not the slider's |
| `BRIGHTNESS_SETTLE_MS` | 2000 | Quiet time before a brightness change is written to flash, so dragging costs one write |
```

- [ ] **Step 4: Design doc — a phone control section**

Add a new `### 7.2 Phone control` immediately after the 7.1 OTA section:

````markdown
### 7.2 Phone control

The board serves a single self-contained page on port 80 — no app, no external
assets (it has no internet path, so anything external wouldn't load anyway).

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | the page |
| GET | `/api/config` | mode names, seat colours, side count — fetched once |
| GET | `/api/state` | mode, brightness, lit, current seat, roster, ready, setup — polled every 2 s |
| POST | `/api/mode?value=0..3` | set mode; 409 while a tap-setup session is running |
| POST | `/api/brightness?value=5..100` | set brightness |
| POST | `/api/power?value=on\|off` | light or darken the table |

Range errors are 400; 409 means the request was valid but the table is mid-setup
and the person physically at it wins.

A mode change from the phone leaves the roster alone — it isn't a setup session.
Off preserves all game state and isn't persisted, so the table always boots lit;
four fast taps on one side wake it without a phone.

```bash
curl -s http://turn-counter.local/api/state
curl -s -X POST "http://turn-counter.local/api/power?value=off"
```
````

- [ ] **Step 5: Bench guide checklist**

After the OTA checklist line, add:

```markdown
- [ ] Phone UI: open `http://turn-counter.local/` (iPhone) and `http://<ip>/` (Android), confirm mode buttons, brightness slider and on/off all work and that status follows taps at the table
- [ ] Guest access: DHCP-reserve the board's IP (its MAC is printed at boot), then `make qr URL=<ip>` and stick the sticker under the table
```

- [ ] **Step 6: Rebuild the affected PDFs**

```bash
.venv/bin/python3 doc-src/build_pdf.py
.venv/bin/python3 doc-src/build_bench_guide_pdf.py
```

- [ ] **Step 7: Commit**

```bash
git add README.md turn_counter_design_doc.md turn_counter_design_doc.pdf \
        bench_build_guide.md bench_build_guide.pdf doc-src/_build_bench_guide.html
git commit -m "docs: phone control"
```

---

## Task 8: Final verification

- [ ] **Step 1: Full compile**

Run: `make compile-all`
Expected: all sketches link. Record `turn_counter`'s flash percentage against the 67% it started at.

- [ ] **Step 2: Full test run**

Run: `make test`
Expected: 20 passed.

- [ ] **Step 3: Confirm no secret leaked into the tree**

Run: `git status --porcelain`
Expected: no `secrets.h`, no `build/`.

- [ ] **Step 4: Bench acceptance (operator)**

- Every `curl` in Task 4 Step 5 returns the documented status code.
- Brightness change during a READY round leaves the greens alone.
- Off then on during a READY round leaves the greens alone.
- Mode change during tap-setup returns 409.
- Four fast taps wake a dark table, into the same state, not into setup.
- Single taps do nothing while dark.
- Brightness survives a power cycle; the off state does not — it boots lit.
- The page works on an iPhone via `.local` and an Android phone via IP.
- `make ota` still works, and the web UI comes back after the reboot.
