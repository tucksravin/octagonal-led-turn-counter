# Setup Lock and OTA Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a read-only diagnostics endpoint, a RAM-only setup lock with amber refusal feedback, and fix phase-1 setup abort to run exactly one demo rotation.

**Architecture:** Three independent changes to `firmware/turn_counter/`, plus one accessor in the shared `octagon_core` library. The web layer's four bare function pointers become a `TableHooks` struct because this plan adds a fifth. Nothing here touches the `eight` sketch.

**Tech Stack:** Arduino framework for ESP32-S3 via `arduino-cli`, FastLED 3.10.5, ESP32 core 3.3.10 `WebServer`, pytest for host-side tests.

**Spec:** `docs/superpowers/specs/2026-08-16-setup-lock-and-diagnostics-design.md`

---

## Critical context for the implementer

**You cannot flash, compile, or open a serial console.** The user drives the
bench terminal. Your job is to edit files. When a task says "verify at the
bench," write the exact commands the user should run and stop for them to run
them. Do not run `make`, `arduino-cli`, `screen`, or any script that opens
`/dev/cu.*`.

**Host-side pytest you CAN run.** `make test` and `pytest` execute on the Mac and
touch no hardware.

**File layout:**

- `firmware/turn_counter/turn_counter.ino` — game logic, ~680 lines
- `firmware/turn_counter/web_ui.h` / `web_ui.cpp` — the HTTP layer
- `firmware/libraries/octagon_core/src/octagon_core.h` / `.cpp` — shared table hardware
- `tests/` — host-side pytest

**Ordering matters.** Task 1 (the `octagon_core` accessor) is needed by Task 3.
Task 5 (`TableHooks`) must land before Task 6 adds a hook to it. Task 8 (abort
timing) is independent and could go anywhere, but it introduces
`MODE_DIAL_COUNT`, which the companion timed-modes plan depends on.

**Commit after every task.** The repo is on `main` and the user commits here
routinely; no branch needed unless they ask.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `octagon_core.h/.cpp` | table hardware | + `lastTapForSide()` accessor |
| `turn_counter.ino` | game rules | + lock state, refusal animation, `MODE_DIAL_COUNT`, hook wiring |
| `web_ui.h` | game↔HTTP interface | + `locked` in state, `LockSetter`, `TableHooks` struct |
| `web_ui.cpp` | HTTP + page | + `/api/diag`, `/api/lock`, diag `<details>` block, lock toggle |
| `tests/test_diag_json.py` | host test | new — JSON layout and buffer sizing |

---

## Task 1: Per-side last-tap accessor in octagon_core

The tap scanner already records `lastTapPerSide[NUM_SIDES]` as file-static state.
Only the accessor is missing.

**Files:**
- Modify: `firmware/libraries/octagon_core/src/octagon_core.h`
- Modify: `firmware/libraries/octagon_core/src/octagon_core.cpp`

- [ ] **Step 1: Declare the accessor**

In `octagon_core.h`, find this line (~line 86):

```cpp
uint32_t lastAnyTapMs();      // 0 until the first tap since boot
```

Add directly beneath it:

```cpp
uint32_t lastTapForSide(uint8_t i);  // 0 until side i's first tap since boot
```

- [ ] **Step 2: Define it**

In `octagon_core.cpp`, find this line (~line 58):

```cpp
uint32_t lastAnyTapMs() { return lastTapMs; }
```

Add directly beneath it:

```cpp
uint32_t lastTapForSide(uint8_t i) { return (i < NUM_SIDES) ? lastTapPerSide[i] : 0; }
```

The bounds check is not paranoia — `web_ui.cpp` will call this in a loop and a
future refactor could change the bound.

- [ ] **Step 3: Commit**

```bash
git add firmware/libraries/octagon_core/src/octagon_core.h firmware/libraries/octagon_core/src/octagon_core.cpp
git commit -m "octagon_core: expose per-side last-tap timestamps"
```

---

## Task 2: Host test for the diagnostics JSON

Written before the endpoint. The failure this catches is buffer truncation —
`snprintf` silently cuts the response and the page gets unparseable JSON with no
error anywhere.

**Files:**
- Create: `tests/test_diag_json.py`

- [ ] **Step 1: Write the failing test**

Create `tests/test_diag_json.py`:

```python
"""Models the /api/diag response that web_ui.cpp builds with snprintf.

There is no on-device test framework, so the layout is reproduced here and
checked for the two things that fail silently on the board: JSON that does not
parse, and a response that overruns its fixed buffer and gets truncated.

Keep BUFFER_BYTES and the format strings in sync with handleDiag().
"""

import json

import pytest

BUFFER_BYTES = 768
NUM_SIDES = 8


def build_diag(tap_delta, uptime_ms, free_heap, rssi, sides):
    """Reproduce handleDiag()'s snprintf layout exactly.

    `sides` is a list of (pin, baseline, leds, last_tap_ms) tuples. A last_tap_ms
    of 0 means "never tapped" and must serialise as -1, not as `uptime - 0`.
    """
    parts = [
        '{"tapDelta":%d,"uptimeMs":%d,"freeHeap":%d,"rssi":%d,"sides":['
        % (tap_delta, uptime_ms, free_heap, rssi)
    ]
    for i, (pin, base, leds, last_tap) in enumerate(sides):
        since = -1 if last_tap == 0 else uptime_ms - last_tap
        parts.append(
            '%s{"pin":%d,"baseline":%d,"leds":%d,"sinceTapMs":%d}'
            % ("," if i else "", pin, base, leds, since)
        )
    parts.append("]}")
    return "".join(parts)


def worst_case_sides():
    """Widest plausible values: 2-digit pins, 4-digit baselines, 3-digit LED
    counts, and a sinceTapMs at the far end of a 49-day millis() range."""
    return [(10, 4095, 999, 1) for _ in range(NUM_SIDES)]


def test_parses_as_json():
    body = build_diag(720, 1843221, 214880, -54, [(5, 14, 29, 1839010)] * NUM_SIDES)
    parsed = json.loads(body)
    assert parsed["tapDelta"] == 720
    assert len(parsed["sides"]) == NUM_SIDES


def test_untapped_side_reports_minus_one():
    """A side that has never fired must not read as 'tapped at boot' — that is
    the exact case you go looking for when a piezo is dead."""
    sides = [(5, 14, 29, 0)] + [(2, 11, 28, 1839010)] * (NUM_SIDES - 1)
    parsed = json.loads(build_diag(720, 1843221, 214880, -54, sides))
    assert parsed["sides"][0]["sinceTapMs"] == -1
    assert parsed["sides"][1]["sinceTapMs"] == 4211


def test_worst_case_fits_the_buffer():
    body = build_diag(720, 4294967295, 8388608, -99, worst_case_sides())
    assert len(body) + 1 <= BUFFER_BYTES, (
        "diag response is %d bytes + NUL, buffer is %d — widen the buffer in "
        "handleDiag() or this truncates silently on the board"
        % (len(body), BUFFER_BYTES)
    )


def test_buffer_has_headroom_for_one_more_field():
    """Not a hard requirement, just a tripwire: if the response is within 80
    bytes of the buffer, the next field added will be the one that breaks it."""
    body = build_diag(720, 4294967295, 8388608, -99, worst_case_sides())
    assert BUFFER_BYTES - len(body) > 80
```

- [ ] **Step 2: Run it to confirm it passes against the model**

```bash
pytest tests/test_diag_json.py -v
```

Expected: 4 passed. This test models code that does not exist yet — it is
locking down the layout Task 3 must implement, and its real job is failing later
if someone widens a field.

- [ ] **Step 3: Commit**

```bash
git add tests/test_diag_json.py
git commit -m "test: model the /api/diag JSON layout and buffer sizing"
```

---

## Task 3: The `/api/diag` endpoint

**Files:**
- Modify: `firmware/turn_counter/web_ui.cpp`

- [ ] **Step 1: Add the handler**

In `web_ui.cpp`, find `handleConfig()` and add `handleDiag()` directly after it,
before the `// Range checking lives here` comment block:

```cpp
// Read-only bench diagnostics, so a piezo can be judged from a phone instead of
// a laptop and a reboot. Deliberately not part of the 2 s poll — this is
// something you go and look at.
static void handleDiag() {
  char buf[768];
  int n = snprintf(buf, sizeof(buf),
                   "{\"tapDelta\":%u,\"uptimeMs\":%lu,\"freeHeap\":%lu,\"rssi\":%d,\"sides\":[",
                   TAP_DELTA, (unsigned long)millis(),
                   (unsigned long)ESP.getFreeHeap(), WiFi.RSSI());
  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_SIDES && n > 0 && n < (int)sizeof(buf); i++) {
    uint32_t last = lastTapForSide(i);
    // 0 means "never fired". Reporting now - 0 would read as "tapped at boot",
    // the opposite of the truth, in exactly the case you are diagnosing.
    long since = (last == 0) ? -1L : (long)(now - last);
    n += snprintf(buf + n, sizeof(buf) - n,
                  "%s{\"pin\":%u,\"baseline\":%u,\"leds\":%u,\"sinceTapMs\":%ld}",
                  i ? "," : "", sidePiezoPin[i], baseline(i), sideLedCounts[i], since);
  }
  if (n > 0 && n < (int)sizeof(buf)) snprintf(buf + n, sizeof(buf) - n, "]}");
  server.send(200, "application/json", buf);
}
```

- [ ] **Step 2: Add the `WiFi.h` include**

`WiFi.RSSI()` needs it and `web_ui.cpp` does not include it today. At the top of
the file, after `#include <WebServer.h>`:

```cpp
#include <WiFi.h>
```

- [ ] **Step 3: Register the route**

In `webUiBegin()`, add to the route list after the `/api/state` line:

```cpp
  server.on("/api/diag",       HTTP_GET,  handleDiag);
```

- [ ] **Step 4: Verify it compiles**

Ask the user to run:

```bash
make compile-all
```

Expected: `turn_counter` links, flash usage around 69%. Warnings from FastLED
internals are pre-existing and expected. **Stop and wait for their output.**

- [ ] **Step 5: Commit**

```bash
git add firmware/turn_counter/web_ui.cpp
git commit -m "web_ui: add read-only /api/diag endpoint"
```

---

## Task 4: The diagnostics section on the page

**Files:**
- Modify: `firmware/turn_counter/web_ui.cpp` (the `PAGE_HTML` PROGMEM string)

- [ ] **Step 1: Add the styles**

In the `<style>` block, after the `.note` rule, add:

```css
details{margin-top:4px}
summary{font-size:13px;text-transform:uppercase;letter-spacing:.08em;color:#8e8e93;
        padding:6px 0;cursor:pointer}
table{width:100%;border-collapse:collapse;font-size:13px;margin-top:8px}
th,td{text-align:left;padding:5px 4px;border-bottom:1px solid #2c2c2e}
th{color:#8e8e93;font-weight:500}
td.warn{color:#ff9f0a}
#diagmeta{font-size:12px;color:#8e8e93;margin-top:10px}
```

- [ ] **Step 2: Add the markup**

Immediately before `<div id="err"></div>`, add:

```html
<div class="card"><details id="diagbox">
  <summary>Diagnostics</summary>
  <table><thead><tr><th>Seat</th><th>Pin</th><th>Baseline</th><th>LEDs</th><th>Last tap</th></tr></thead>
  <tbody id="diagrows"></tbody></table>
  <div id="diagmeta"></div>
  <button id="diagrefresh" style="margin-top:12px;text-align:center">Refresh</button>
</details></div>
```

- [ ] **Step 3: Add the fetch and render**

In the `<script>` block, add these two functions after `render(s)`:

```js
function ago(ms){
  if(ms<0) return 'never';
  if(ms<1000) return 'just now';
  if(ms<60000) return Math.round(ms/1000)+'s ago';
  return Math.round(ms/60000)+'m ago';
}

async function loadDiag(){
  try{
    const d = await (await fetch('/api/diag')).json();
    // Flag a baseline well above the pack. A piezo losing its ground return
    // reads high and steady, which is invisible unless you know the norm.
    const sorted=d.sides.map(s=>s.baseline).sort((a,b)=>a-b);
    const median=sorted[Math.floor(sorted.length/2)]||1;
    $('diagrows').innerHTML = d.sides.map((s,i)=>
      '<tr><td>'+i+'</td><td>'+s.pin+'</td><td'+
      (s.baseline > median*4 ? ' class="warn"' : '')+'>'+s.baseline+
      '</td><td>'+s.leds+'</td><td>'+ago(s.sinceTapMs)+'</td></tr>').join('');
    $('diagmeta').textContent =
      'tap fires at baseline + '+d.tapDelta+
      ' · up '+Math.round(d.uptimeMs/60000)+'m'+
      ' · heap '+Math.round(d.freeHeap/1024)+'k'+
      ' · wifi '+d.rssi+' dBm';
  }catch(e){ $('diagmeta').textContent='Diagnostics unavailable.'; }
}
```

- [ ] **Step 4: Wire the triggers**

In the IIFE at the bottom, before `await poll();`, add:

```js
  $('diagbox').ontoggle=()=>{ if($('diagbox').open) loadDiag(); };
  $('diagrefresh').onclick=(e)=>{ e.preventDefault(); loadDiag(); };
```

`e.preventDefault()` matters: the button is inside `<details>`, and without it a
click can collapse the block you are trying to refresh.

- [ ] **Step 5: Verify it compiles**

Ask the user to run `make compile-all`. Expected: links, flash up ~1 KB from
Task 3. **Stop and wait.**

- [ ] **Step 6: Commit**

```bash
git add firmware/turn_counter/web_ui.cpp
git commit -m "web_ui: diagnostics section on the phone page"
```

- [ ] **Step 7: Bench check**

Ask the user to run `make ota`, then open the page and expand **Diagnostics**.

What to confirm, and why each one matters:

- Eight rows, pins matching the calibrated map `{5, 2, 1, 4, 9, 6, 8, 7}`.
- **Side 7's baseline.** It read 287 at boot against 9–19 elsewhere. If it is
  still ~287 the disc or its ground has a real problem; if it now reads in the
  teens, the boot seeding window caught the board while something was moving.
  This endpoint exists mainly to answer that question.
- Tap a side, hit Refresh, confirm that side's "Last tap" resets to "just now"
  and no other side's does. That is the cross-talk filter working.
- Any side reading "never" is a piezo that has not fired since boot.

---

## Task 5: `TableHooks` struct

Mechanical refactor, done before adding the fifth hook rather than after.

**Files:**
- Modify: `firmware/turn_counter/web_ui.h`
- Modify: `firmware/turn_counter/web_ui.cpp`
- Modify: `firmware/turn_counter/turn_counter.ino`

- [ ] **Step 1: Replace the `webUiBegin` declaration**

In `web_ui.h`, replace:

```cpp
void webUiBegin(const TableConfig &cfg, StateReader read, ModeSetter setMode,
                BrightnessSetter setBrightness, PowerSetter setPower);
```

with:

```cpp
// Bundled rather than passed positionally: these are five same-shaped function
// pointers, and swapping two of them compiles clean and fails at the table.
struct TableHooks {
  StateReader      read;
  ModeSetter       setMode;
  BrightnessSetter setBrightness;
  PowerSetter      setPower;
};

void webUiBegin(const TableConfig &cfg, const TableHooks &hooks);
```

- [ ] **Step 2: Update the definition**

In `web_ui.cpp`, replace the five static pointers:

```cpp
static StateReader      readState = nullptr;
static ModeSetter       modeSetter = nullptr;
static BrightnessSetter briSetter = nullptr;
static PowerSetter      pwrSetter = nullptr;
```

with:

```cpp
static TableHooks hooks = {nullptr, nullptr, nullptr, nullptr};
```

Then update every use:

- `readState(s)` → `hooks.read(s)` (in `sendState`)
- `modeSetter((uint8_t)v)` → `hooks.setMode((uint8_t)v)` (in `handleMode`)
- `briSetter((uint8_t)v)` → `hooks.setBrightness((uint8_t)v)` (in `handleBrightness`)
- `pwrSetter(v == "on")` → `hooks.setPower(v == "on")` (in `handlePower`)

And replace the head of `webUiBegin`:

```cpp
void webUiBegin(const TableConfig &c, StateReader read, ModeSetter setMode,
                BrightnessSetter setBrightness, PowerSetter setPower) {
  if (started) return;
  cfg = c;
  readState = read;
  modeSetter = setMode;
  briSetter = setBrightness;
  pwrSetter = setPower;
```

with:

```cpp
void webUiBegin(const TableConfig &c, const TableHooks &h) {
  if (started) return;
  cfg = c;
  hooks = h;
```

- [ ] **Step 3: Update the call site**

In `turn_counter.ino`, inside `beginOta()`, replace:

```cpp
  webUiBegin(WEB_CONFIG, readTableState, applyMode, applyBrightness, applyPower);
```

with:

```cpp
  const TableHooks hooks = {
    .read           = readTableState,
    .setMode        = applyMode,
    .setBrightness  = applyBrightness,
    .setPower       = applyPower,
  };
  webUiBegin(WEB_CONFIG, hooks);
```

- [ ] **Step 4: Verify it compiles**

Ask the user to run `make compile-all`. Expected: links, flash essentially
unchanged — this is a pure refactor. **Stop and wait.**

- [ ] **Step 5: Commit**

```bash
git add firmware/turn_counter/web_ui.h firmware/turn_counter/web_ui.cpp firmware/turn_counter/turn_counter.ino
git commit -m "web_ui: bundle callbacks into TableHooks"
```

---

## Task 6: Setup lock state and refusal

**Files:**
- Modify: `firmware/turn_counter/turn_counter.ino`

- [ ] **Step 1: Add the constant and state**

After the `bool tableLit = true;` declaration and its comment (~line 66), add:

```cpp
bool     setupLocked = false;       // refuses the four-tap gesture while lit. Never
                                    // persisted, same as tableLit: a power cycle
                                    // always clears it, so a locked table with no
                                    // Wi-Fi can never be stranded.
int8_t   refuseSide  = -1;          // side mid-refusal-flash, -1 = idle
uint32_t refuseStart = 0;
```

With the other timing constants (after `SETUP_JOIN_IDLE_MS`, ~line 36), add:

```cpp
const uint16_t REFUSE_BLINK_MS = 120;   // on/off/on/off = 480 ms total
const CRGB     REFUSE_AMBER    = CRGB(255, 130, 0);
```

- [ ] **Step 2: Add the refusal animation**

Insert these two functions immediately after `renderCurrent()` and before
`startPlay()` — `refuseTick` calls `renderCurrent()`, so it must come after it:

```cpp
// A refused gesture has to say something, or it reads as a dead piezo. Runs as a
// state machine from loop() rather than a delay() in the tap handler, which
// would stall tapsPoll() and drag the adaptive baselines.
void refuseSetup(int8_t side, uint32_t now) {
  refuseSide = side;
  refuseStart = now;
  Serial.printf("Setup gesture on side %d refused - table is locked\n", side);
}

void refuseTick(uint32_t now) {
  if (refuseSide < 0) return;
  if (!tableLit || inSetupMode) { refuseSide = -1; return; }
  uint32_t phase = (now - refuseStart) / REFUSE_BLINK_MS;
  if (phase >= 4) {
    refuseSide = -1;
    renderCurrent();
    return;
  }
  if (phase % 2 == 0) {
    fillSide(refuseSide, REFUSE_AMBER);   // overlays the live scene, so no saved buffer
    FastLED.show();
  } else {
    renderCurrent();                      // safe repaint: does NOT clear ready[]
  }
}
```

- [ ] **Step 3: Add the gesture guard**

Immediately after `registerTapForSetupGesture()` (before `enterSetupMode()`), add:

```cpp
// True only when the burst completed AND setup is allowed to open. A refused
// burst is still consumed by registerTapForSetupGesture, so taps can't pile up
// against the lock and spring setup open the moment it's lifted.
bool setupGestureFired(int8_t side, uint32_t now) {
  if (!registerTapForSetupGesture(side, now)) return false;
  if (!setupLocked) return true;
  refuseSetup(side, now);
  return false;
}
```

- [ ] **Step 4: Route both setup entries through it**

In `commitTap()`, there are exactly two calls that open setup. In the READY
branch, replace:

```cpp
    if (registerTapForSetupGesture(side, whenMs)) {
      enterSetupMode();
      return;
    }
```

with:

```cpp
    if (setupGestureFired(side, whenMs)) {
      enterSetupMode();
      return;
    }
```

In the turn-passing branch's `else`, make the identical replacement.

**Do not change the third call**, the one at the top of `commitTap()` under
`if (!tableLit)`. That is the wake burst, and it must keep calling
`registerTapForSetupGesture` directly — that is what encodes "the lock never
applies while the table is dark."

- [ ] **Step 5: Add the setter**

In the `apply*` block, after `applyPower()`, add:

```cpp
bool applyLock(bool locked) {
  setupLocked = locked;               // deliberately not persisted — see the declaration
  Serial.printf("Setup %s\n", locked ? "locked" : "unlocked");
  return true;                        // never refused; locking mid-setup lets the
}                                     // session at the table finish rather than
                                      // yanking it away
```

- [ ] **Step 6: Drive it from `loop()`**

In `loop()`, after the `brightnessTick(now);` line, add:

```cpp
  refuseTick(now);
```

- [ ] **Step 7: Verify it compiles**

Ask the user to run `make compile-all`. **Stop and wait.**

- [ ] **Step 8: Commit**

```bash
git add firmware/turn_counter/turn_counter.ino
git commit -m "turn_counter: RAM-only setup lock with amber refusal flash"
```

---

## Task 7: Expose the lock to the phone

**Files:**
- Modify: `firmware/turn_counter/web_ui.h`
- Modify: `firmware/turn_counter/web_ui.cpp`
- Modify: `firmware/turn_counter/turn_counter.ino`

- [ ] **Step 1: Extend the interface**

In `web_ui.h`, add to `TableState` after `bool inSetupMode;`:

```cpp
  bool    locked;             // setup gesture refused while lit
```

Add the typedef after `PowerSetter`:

```cpp
typedef bool (*LockSetter)(bool locked);            // always true today
```

And add to `TableHooks` after `setPower`:

```cpp
  LockSetter       setLock;
```

- [ ] **Step 2: Widen the state buffer and emit the field**

In `web_ui.cpp`'s `sendState()`, change `char buf[256];` to `char buf[384];`
(the companion timed-modes plan adds four more fields to this response), and
extend the format:

```cpp
  snprintf(buf, sizeof(buf),
           "{\"mode\":%u,\"brightness\":%u,\"lit\":%s,\"currentSide\":%d,"
           "\"roster\":%u,\"ready\":%u,\"setup\":%s,\"locked\":%s}",
           s.mode, s.brightnessPercent, s.lit ? "true" : "false", s.currentSide,
           s.rosterMask, s.readyMask, s.inSetupMode ? "true" : "false",
           s.locked ? "true" : "false");
```

- [ ] **Step 3: Add the handler and route**

After `handlePower()`, add:

```cpp
static void handleLock() {
  String v = server.arg("value");
  if (v != "on" && v != "off") {
    server.send(400, "text/plain", "value must be on or off");
    return;
  }
  hooks.setLock(v == "on");
  sendState(200);
}
```

And in `webUiBegin()`, after the `/api/power` route:

```cpp
  server.on("/api/lock",       HTTP_POST, handleLock);
```

- [ ] **Step 4: Add the page control**

In `PAGE_HTML`, immediately after the power card
(`<div class="card"><button id="power" ...></div>`), add:

```html
<div class="card">
  <button id="lock">Lock setup</button>
  <div class="note">Stops the four-tap setup gesture at the table. Turning the
  table off and on from here keeps the lock; unplugging it clears it.</div>
</div>
```

In `render(s)`, after the `$('banner')` line, add:

```js
  $('lock').textContent = s.locked ? 'Setup locked' : 'Lock setup';
  $('lock').setAttribute('aria-pressed', s.locked);
```

In the IIFE, after the `$('power').onclick` wiring, add:

```js
  $('lock').onclick=()=>post('/api/lock?value='+
    ($('lock').getAttribute('aria-pressed')==='true'?'off':'on'));
```

- [ ] **Step 5: Fill the state field**

In `turn_counter.ino`'s `readTableState()`, add before the closing brace:

```cpp
  s.locked            = setupLocked;
```

- [ ] **Step 6: Wire the hook**

In `beginOta()`, add to the `TableHooks` initialiser:

```cpp
    .setLock        = applyLock,
```

- [ ] **Step 7: Verify it compiles**

Ask the user to run `make compile-all`. **Stop and wait.**

- [ ] **Step 8: Commit**

```bash
git add firmware/turn_counter/web_ui.h firmware/turn_counter/web_ui.cpp firmware/turn_counter/turn_counter.ino
git commit -m "web_ui: setup lock toggle on the phone"
```

- [ ] **Step 9: Bench check**

Ask the user to run `make ota`, then work through these. Each one is a case that
would otherwise fail quietly:

1. **Lock, then four fast taps on one side while lit.** Setup must not open; the
   tapped side double-flashes amber and the normal scene returns.
2. **Same, during a READY round with some seats green.** The greens must survive
   the flash. This is the `renderCurrent()`-not-`startPlay()` hazard.
3. **Turn the table off from the phone, then four fast taps.** It must wake. The
   lock does not apply while dark.
4. **Unplug and replug.** The phone must show "Lock setup" again — the lock is
   gone.
5. **Turn the table off and on from the phone.** The lock must *survive* that.
   It is a different piece of state.
6. **Unlock, then four fast taps.** Setup opens normally.

---

## Task 8: Setup abort after exactly one rotation

Independent of everything above, and the change the companion timed-modes plan
builds on.

**Files:**
- Modify: `firmware/turn_counter/turn_counter.ino`

- [ ] **Step 1: Introduce `MODE_DIAL_COUNT`**

Replace the `GameMode` enum (~line 51):

```cpp
enum GameMode : uint8_t { MODE_CW = 0, MODE_CCW = 1, MODE_ARB = 2, MODE_READY = 3, MODE_COUNT = 4 };
```

with:

```cpp
// MODE_DIAL_COUNT is what the table's setup dial can demo; MODE_COUNT is what
// applyMode() accepts. Equal today — the timed-modes work adds phone-only modes
// past the dial's end, and the abort timing below must follow the dial, not the
// total.
enum GameMode : uint8_t {
  MODE_CW = 0, MODE_CCW = 1, MODE_ARB = 2, MODE_READY = 3,
  MODE_DIAL_COUNT = 4,
  MODE_COUNT = 4
};
```

- [ ] **Step 2: Move the abort constant below the enum and derive it**

Delete this line from the timing constants block (~line 34):

```cpp
const uint16_t MODE_ABORT_IDLE_MS       = 25000;  // phase 1: no tap for a full demo rotation — abort, change nothing
```

Add directly beneath the enum:

```cpp
// One full rotation, derived rather than hardcoded. The old fixed 25000 against
// a 5000 ms demo ran five demos across four modes, so the dial wrapped and
// replayed its opening mode in full before giving up. uint32_t because a demo
// longer than 16 s would silently wrap a uint16_t.
const uint32_t MODE_ABORT_IDLE_MS = (uint32_t)MODE_DEMO_MS * MODE_DIAL_COUNT;
```

- [ ] **Step 3: Point the dial advance at the dial count**

In `renderModeDemo()`, change:

```cpp
    dialMode = (dialMode + 1) % MODE_COUNT;
```

to:

```cpp
    dialMode = (dialMode + 1) % MODE_DIAL_COUNT;
```

This is a no-op today (both are 4) and load-bearing the moment the timed modes
land. Making it now means that change cannot forget it.

- [ ] **Step 4: Verify it compiles**

Ask the user to run `make compile-all`. **Stop and wait.**

- [ ] **Step 5: Commit**

```bash
git add firmware/turn_counter/turn_counter.ino
git commit -m "turn_counter: abort setup after exactly one demo rotation"
```

- [ ] **Step 6: Bench check**

Ask the user to run `make ota`, then four-tap a side to open setup and **let it
sit without tapping**. Time it: the dial should show all four modes, one after
another, and abort at ~20 s without ever returning to the mode it opened on. The
roster and mode must be exactly as before — check the phone shows the same mode.

---

## Task 9: Update the docs

Three docs describe firmware behaviour and will now be wrong.

**Files:**
- Modify: `turn_counter_design_doc.md`
- Modify: `design_doc_simple.md`
- Modify: `bench_build_guide.md`

- [ ] **Step 1: Find every affected claim**

```bash
grep -n "25\|abort\|setup\|four-tap\|4 times\|/api/" turn_counter_design_doc.md design_doc_simple.md bench_build_guide.md
```

- [ ] **Step 2: Update them**

For each doc, correct:

- The phase-1 abort timeout, wherever a number is quoted. It is now one demo
  rotation (~20 s), described as derived rather than as a magic number.
- The endpoint table, adding `GET /api/diag` and `POST /api/lock?value=on|off`.
- The phone-UI description, adding the setup lock and the diagnostics section,
  including that the lock clears on a power cycle but not on the phone's off/on.

`design_doc_simple.md` §6 also still says `BRIGHTNESS = 128` and tells the reader
to revert an `isOn` bench line that no longer exists. Fix both while you are in
there — they are stale from before the brightness and power work.

- [ ] **Step 3: Commit**

```bash
git add turn_counter_design_doc.md design_doc_simple.md bench_build_guide.md
git commit -m "docs: setup lock, /api/diag, and the one-rotation abort"
```

---

## Done criteria

- [ ] `pytest tests/ -v` passes (existing 20 tests plus the 4 new ones)
- [ ] `make compile-all` links, flash ~70%
- [ ] All six lock bench checks pass, including the READY-greens case and both
      power-cycle cases
- [ ] `/api/diag` answers the side-7 baseline question one way or the other
- [ ] Phase 1 aborts at ~20 s with no repeated mode
- [ ] Docs match the firmware
