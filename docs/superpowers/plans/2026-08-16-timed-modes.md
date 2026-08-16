# Timed Modes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a countdown-timer mode and a time-share mode, both selectable only from the phone.

**Architecture:** The `GameMode` enum grows past `MODE_DIAL_COUNT`, so the table's four-tap dial keeps demoing four modes while `applyMode()` accepts six. Both new modes animate continuously, which is new — every existing mode repaints only on a tap — so a `modeTick()` frame loop joins `brightnessTick()` in `loop()`. Time is banked on turn transitions rather than accumulated per frame.

**Tech Stack:** Arduino framework for ESP32-S3 via `arduino-cli`, FastLED 3.10.5 (`blend`, `beatsin8`), `Preferences` (NVS), pytest for host-side tests.

**Spec:** `docs/superpowers/specs/2026-08-16-timed-modes-design.md`

**Prerequisite:** `docs/superpowers/plans/2026-08-16-setup-lock-and-diagnostics.md` must be complete. It introduces `MODE_DIAL_COUNT`, the derived `MODE_ABORT_IDLE_MS`, and the `TableHooks` struct this plan extends.

---

## Critical context for the implementer

**You cannot flash, compile, or open a serial console.** The user drives the
bench terminal. When a task says "verify," write the command and stop for them
to run it. Do not run `make`, `arduino-cli`, or anything touching `/dev/cu.*`.

**Host-side pytest you CAN run.** `pytest` executes on the Mac.

**The one hazard worth internalising before you start.** `renderCurrent()` and
`startPlay()` look interchangeable and are not. `startPlay()` means "a fresh
game starts now" — it clears `ready[]` and, after this plan, resets the turn
clock. `renderCurrent()` means "repaint what is already true." Routing a
brightness nudge or a power-on through `startPlay()` hands the current player a
free shot clock, the same way it used to wipe a READY round's greens. Every
repaint path in this plan is explicit about which one it uses.

**Verify the prerequisite before starting:**

```bash
grep -n "MODE_DIAL_COUNT\|TableHooks" firmware/turn_counter/turn_counter.ino firmware/turn_counter/web_ui.h
```

Expected: `MODE_DIAL_COUNT` in the enum and in `MODE_ABORT_IDLE_MS`, and a
`TableHooks` struct in `web_ui.h`. If either is missing, stop — the other plan
is not finished.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `tests/test_timed_modes.py` | host test | new — both formulas, including the overflow regression |
| `turn_counter.ino` | game rules | + two modes, timing state, two renderers, frame tick |
| `web_ui.h` | game↔HTTP interface | + `turnSeconds`, `secondsLeft`, `sharePct[]`, timer bounds and mode indices in config |
| `web_ui.cpp` | HTTP + page | + `/api/timer`, two page cards |

No change to `octagon_core` — both renderers build on `fillSide`, `sideStarts`,
`sideLedCounts` and `PLAYER_COLORS`, which are all already exported.

---

## Task 1: Host tests for the timing arithmetic

Written first. This is fixed-point integer maths with a cap, a floor, a
divide-by-zero case and an overflow guard — the exact shape that already bit this
project once, when the brightness lerp crawled sub-raw-unit fractions for 600 ms.
That bug was caught by modelling the maths in Python before flashing.

**Files:**
- Create: `tests/test_timed_modes.py`

- [ ] **Step 1: Write the tests**

Create `tests/test_timed_modes.py`:

```python
"""Host model of the fixed-point maths in turn_counter's timed modes.

There is no on-device test framework, so both formulas are reproduced here
exactly as the firmware computes them and checked against the behaviour the spec
promises. Keep these in sync with renderShare() and renderTimer().
"""

import pytest

UINT32_MAX = 2**32 - 1
SIDE_LEN = 29  # widest calibrated side; the table is {29,28,27,27,27,28,28,27}


# --- models of the firmware ------------------------------------------------

def share_leds(side_len, mine_ms, total_ms, active_count):
    """renderShare(): bar length against a fair share of HALF the side, so an
    even table sits half-lit and the bar has headroom in both directions."""
    if total_ms == 0:
        return side_len // 2                      # neutral open
    lit = (mine_ms * active_count * side_len) // (2 * total_ms)
    return max(1, min(side_len, lit))             # floor 1, cap full side


def share_leds_uint32(side_len, mine_ms, total_ms, active_count):
    """The same thing WITHOUT the uint64_t intermediate, to prove it wraps."""
    if total_ms == 0:
        return side_len // 2
    num = (mine_ms * active_count * side_len) & UINT32_MAX
    lit = num // (2 * total_ms)
    return max(1, min(side_len, lit))


def timer_leds(side_len, elapsed_ms, total_ms):
    """renderTimer(): LEDs still lit. 0 means the expired branch was taken."""
    if elapsed_ms >= total_ms:
        return 0
    return side_len * (total_ms - elapsed_ms) // total_ms


def timer_offset(side_len, lit):
    """Where the surviving block starts within the side — it shrinks from both
    ends inward, so the last light sits in front of the player."""
    return (side_len - lit) // 2


def warn_blend(elapsed_ms, total_ms):
    """0 = seat colour, 255 = full red. Warms over the final quarter."""
    warn = total_ms // 4
    left = total_ms - elapsed_ms
    if left >= warn:
        return 0
    return 255 - (left * 255) // warn


# --- time share ------------------------------------------------------------

def test_share_opens_neutral():
    """Before anyone has played, the table shows half a side — not dark (reads
    as broken) and not full (reads as 'you are hogging it' before you started)."""
    assert share_leds(SIDE_LEN, 0, 0, 4) == SIDE_LEN // 2


@pytest.mark.parametrize("seats", [2, 3, 4, 5, 8])
def test_share_even_table_sits_at_half(seats):
    each = 60_000
    assert share_leds(SIDE_LEN, each, each * seats, seats) == SIDE_LEN // 2


def test_share_grows_across_your_own_turn():
    """mine and total climb together; the ratio still rises."""
    seats, others = 4, 90_000
    lengths = [
        share_leds(SIDE_LEN, 30_000 + t, others + 30_000 + t, seats)
        for t in (0, 10_000, 20_000, 30_000)
    ]
    assert lengths == sorted(lengths)
    assert lengths[-1] > lengths[0]


def test_share_shrinks_while_a_non_leader_plays():
    """The point of dividing by the total rather than by the leader: your bar
    responds to EVERY other player, not only whoever is ahead."""
    seats = 4
    mine = 80_000
    leader, small = 200_000, 20_000
    before = share_leds(SIDE_LEN, mine, mine + leader + small, seats)
    after = share_leds(SIDE_LEN, mine, mine + leader + small + 30_000, seats)
    assert after < before


def test_share_caps_at_a_full_side():
    """Double your fair share fills the side and stays there."""
    seats = 4
    assert share_leds(SIDE_LEN, 100_000, 200_000, seats) == SIDE_LEN
    assert share_leds(SIDE_LEN, 190_000, 200_000, seats) == SIDE_LEN


def test_share_floors_at_one_led():
    """A bar that reaches zero is indistinguishable from a dead piezo."""
    assert share_leds(SIDE_LEN, 100, 10_000_000, 8) == 1


def test_share_survives_a_long_session():
    """~5.3 h banked on one seat overflows mine * n * len in 32 bits. Guard: the
    64-bit model caps at a full side; the 32-bit one wraps to near-nothing."""
    mine, total, seats = 19_000_000, 20_000_000, 8
    assert mine * seats * SIDE_LEN > UINT32_MAX, "test no longer exercises the wrap"
    assert share_leds(SIDE_LEN, mine, total, seats) == SIDE_LEN
    assert share_leds_uint32(SIDE_LEN, mine, total, seats) < 5


# --- countdown timer -------------------------------------------------------

def test_timer_drains_monotonically_to_zero():
    total = 60_000
    lengths = [timer_leds(SIDE_LEN, e, total) for e in range(0, total + 1, 500)]
    assert lengths[0] == SIDE_LEN
    assert lengths[-1] == 0
    assert all(b <= a for a, b in zip(lengths, lengths[1:]))


def test_timer_block_stays_inside_the_side():
    total = 60_000
    for e in range(0, total, 250):
        lit = timer_leds(SIDE_LEN, e, total)
        assert timer_offset(SIDE_LEN, lit) + lit <= SIDE_LEN


def test_timer_expired_branch_not_a_negative_length():
    """elapsed > total must select the pulse, never wrap into a huge length."""
    assert timer_leds(SIDE_LEN, 60_000, 60_000) == 0
    assert timer_leds(SIDE_LEN, 999_999, 60_000) == 0


def test_warn_blend_spans_the_final_quarter():
    total = 60_000
    assert warn_blend(0, total) == 0
    assert warn_blend(total - total // 4, total) == 0       # exactly at the mark
    assert warn_blend(total - 1, total) > 250               # essentially red
    quarter = [warn_blend(e, total) for e in range(total - total // 4, total, 100)]
    assert all(b >= a for a, b in zip(quarter, quarter[1:]))
    assert all(0 <= v <= 255 for v in quarter)
```

- [ ] **Step 2: Run them**

```bash
pytest tests/test_timed_modes.py -v
```

Expected: all pass. They model code that does not exist yet — their job is to
pin the behaviour Tasks 4 and 5 must reproduce, and to fail loudly if anyone
later "simplifies" the 64-bit intermediate away.

- [ ] **Step 3: Commit**

```bash
git add tests/test_timed_modes.py
git commit -m "test: model the countdown and time-share arithmetic"
```

---

## Task 2: Split the mode space from the dial space

**Files:**
- Modify: `firmware/turn_counter/turn_counter.ino`

- [ ] **Step 1: Extend the enum**

Replace the enum the prerequisite plan left behind:

```cpp
enum GameMode : uint8_t {
  MODE_CW = 0, MODE_CCW = 1, MODE_ARB = 2, MODE_READY = 3,
  MODE_DIAL_COUNT = 4,
  MODE_COUNT = 4
};
```

with:

```cpp
// MODE_DIAL_COUNT and MODE_TIMER deliberately share the value 4. One is a count,
// one is an index, and tying them together is the point: the dial demos
// [0, MODE_DIAL_COUNT) and the phone-only modes begin exactly where it stops.
// A mode the dial cannot animate has no business on a dial whose whole premise
// is "the animation IS the mode."
enum GameMode : uint8_t {
  MODE_CW = 0, MODE_CCW = 1, MODE_ARB = 2, MODE_READY = 3,
  MODE_DIAL_COUNT = 4,
  MODE_TIMER = 4, MODE_SHARE = 5,
  MODE_COUNT = 6
};
```

- [ ] **Step 2: Fix the demo colour array's bound**

`MODE_COLORS` is declared `[MODE_COUNT]` with four initialisers. That silently
becomes a six-element array with two black entries. It only ever feeds the demo,
so bind it to the dial:

```cpp
const CRGB MODE_COLORS[MODE_DIAL_COUNT] = {
```

- [ ] **Step 3: Add the two names**

`MODE_NAMES` stays `[MODE_COUNT]` — it feeds serial logging and `/api/config`,
both of which need all six:

```cpp
const char* const MODE_NAMES[MODE_COUNT] = {
  "clockwise", "counter-clockwise", "arbitrary (join order)", "ready-or-not",
  "countdown timer", "time share"
};
```

- [ ] **Step 4: Clamp the dial on setup entry**

This is the trap. In `enterSetupMode()`, replace:

```cpp
  dialMode = gameMode;            // demos start from the active mode
```

with:

```cpp
  // Demos start from the active mode — unless that's a phone-only mode the dial
  // can't animate, which would also index MODE_COLORS out of bounds. Falling
  // back to CW gives the gesture a second job: it's the manual way out of a
  // phone-only mode when the phone isn't around.
  dialMode = (gameMode < MODE_DIAL_COUNT) ? gameMode : MODE_CW;
```

- [ ] **Step 5: Add the mode predicate**

Next to `readyMode()`:

```cpp
bool timedMode() { return gameMode == MODE_TIMER || gameMode == MODE_SHARE; }
```

- [ ] **Step 6: Verify it compiles**

Ask the user to run `make compile-all`. Expected: links. The two new modes are
selectable and currently render as ordinary single-seat play. **Stop and wait.**

- [ ] **Step 7: Commit**

```bash
git add firmware/turn_counter/turn_counter.ino
git commit -m "turn_counter: split mode space from dial space, add timer/share modes"
```

---

## Task 3: Timing state and turn banking

**Files:**
- Modify: `firmware/turn_counter/turn_counter.ino`

- [ ] **Step 1: Add the constants**

With the other timing constants:

```cpp
const uint16_t TIMER_SECONDS_DEFAULT = 60;
const uint16_t TIMER_SECONDS_MIN     = 10;
const uint16_t TIMER_SECONDS_MAX     = 300;   // 3 digits: parseUInt in web_ui.cpp
                                              // rejects anything longer
const uint16_t TIMER_SETTLE_MS       = 2000;  // defer the NVS write past a slider drag
const uint16_t MODE_FRAME_MS         = 50;    // timed modes repaint at 20 fps
```

- [ ] **Step 2: Add the state**

After the existing game-state block:

```cpp
uint16_t turnSeconds = TIMER_SECONDS_DEFAULT;   // persisted, "turntable"/"tsec"
bool     turnSecondsDirty = false;
uint32_t turnSecondsChangedMs = 0;

uint32_t turnStartMs = 0;              // when the current seat's turn began
uint32_t sideMs[NUM_SIDES] = {0};      // banked play time per seat. RAM only:
                                       // this is the game, not a setting
uint32_t modeFrameMs = 0;              // last timed-mode repaint
int16_t  lastDrawnLeds = -1;           // suppresses redundant show(); -1 = repaint
```

- [ ] **Step 3: Add the banking helpers**

Place these above `advanceTurn()`:

```cpp
// Time is banked on transitions, never accumulated per frame: a per-frame
// `sideMs[cur] += delta` drifts and depends on the loop rate. The live display
// adds (now - turnStartMs) on the fly instead.
void bankTurnTime() {
  uint32_t now = millis();
  if (currentSide >= 0 && currentSide < NUM_SIDES) {
    sideMs[currentSide] += now - turnStartMs;
  }
  turnStartMs = now;
}

// Time-share stats are the game, so they reset when a game starts.
void resetShareStats() {
  memset(sideMs, 0, sizeof(sideMs));
  turnStartMs = millis();
}
```

- [ ] **Step 4: Bank on every turn change**

In `advanceTurn()`, add as the first line, before the `switch`:

```cpp
  bankTurnTime();   // credit the outgoing seat before currentSide moves
```

Banking unconditionally, in every mode, is deliberate: it is one code path
instead of a mode test, and the stats reset on entry to SHARE anyway.

- [ ] **Step 5: Reset on a committed setup**

In `exitSetupMode()`, add immediately before the `prefs.putUChar("roster", ...)`
line:

```cpp
  resetShareStats();   // a new roster is a new game
```

- [ ] **Step 6: Verify it compiles**

Ask the user to run `make compile-all`. **Stop and wait.**

- [ ] **Step 7: Commit**

```bash
git add firmware/turn_counter/turn_counter.ino
git commit -m "turn_counter: per-seat turn-time banking"
```

---

## Task 4: The two renderers

**Files:**
- Modify: `firmware/turn_counter/turn_counter.ino`

- [ ] **Step 1: Add the shared block helper**

Insert after `renderReady()` and before `renderCurrent()`:

```cpp
// Both timed modes draw a centred block that shrinks from the ends inward, so
// the last light left sits directly in front of the player. One visual grammar
// for both modes.
void drawCentredBlock(uint8_t side, uint16_t lit, const CRGB &col) {
  uint8_t len = sideLedCounts[side];
  if (lit > len) lit = len;
  FastLED.clear();
  uint16_t start = sideStarts[side] + (len - lit) / 2;
  for (uint16_t i = 0; i < lit; i++) leds[start + i] = col;
}
```

- [ ] **Step 2: Add the countdown renderer**

Directly beneath it:

```cpp
// Shot clock: the seat's own colour drains from both ends, warming toward red
// over the final quarter so "almost out" reads across a loud room without
// anyone counting LEDs. At zero it complains; it never takes the shot away.
void renderTimer(uint32_t now) {
  uint8_t  len     = sideLedCounts[currentSide];
  uint32_t total   = (uint32_t)turnSeconds * 1000;
  uint32_t elapsed = now - turnStartMs;

  if (elapsed >= total) {
    // beatsin8 runs off millis(), so the pulse keeps phase across repaints
    // without carrying any state of its own.
    FastLED.clear();
    fillSide(currentSide, CRGB(beatsin8(120, 40, 255), 0, 0));
    FastLED.show();
    lastDrawnLeds = -1;          // pulsing: every frame differs
    return;
  }

  uint32_t left = total - elapsed;
  uint16_t lit  = (uint32_t)len * left / total;
  uint32_t warn = total / 4;

  CRGB col = PLAYER_COLORS[currentSide];
  bool warming = left < warn;
  if (warming) col = blend(col, CRGB(255, 0, 0), 255 - (uint8_t)(left * 255 / warn));

  // Outside the final quarter nothing changes between LED steps, so skip the
  // ~6.6 ms show(). Inside it the colour moves continuously, so repaint.
  if (!warming && lit == lastDrawnLeds) return;
  lastDrawnLeds = lit;
  drawCentredBlock(currentSide, lit, col);
  FastLED.show();
}
```

- [ ] **Step 3: Add the time-share renderer**

Directly beneath that:

```cpp
// Your share of table time, measured against a fair share of HALF the side — so
// an even table sits half-lit and the bar has headroom both ways. It grows while
// you sit there and is shorter when the turn comes back, because everyone else's
// play diluted it.
void renderShare(uint32_t now) {
  uint8_t  len   = sideLedCounts[currentSide];
  uint32_t live  = now - turnStartMs;
  uint32_t mine  = sideMs[currentSide] + live;
  uint32_t total = live;
  for (uint8_t s = 0; s < NUM_SIDES; s++) total += sideMs[s];

  uint8_t n = activeCount();
  if (n == 0) n = 1;

  uint16_t lit;
  if (total == 0) {
    lit = len / 2;                 // nobody has played: open neutral
  } else {
    // 64-bit intermediate: mine * n * len overflows uint32_t at ~5 h on one
    // seat, and the bar would wrap to nonsense at the end of a long night —
    // the worst possible time to find it.
    lit = (uint16_t)(((uint64_t)mine * n * len) / ((uint64_t)2 * total));
    if (lit > len) lit = len;
    if (lit < 1) lit = 1;          // zero is indistinguishable from a dead piezo
  }

  if (lit == lastDrawnLeds) return;
  lastDrawnLeds = lit;
  drawCentredBlock(currentSide, lit, PLAYER_COLORS[currentSide]);
  FastLED.show();
}

// Draws immediately, with no frame-interval guard, so renderCurrent() can force
// a repaint. modeTick() is the throttled caller.
void renderTimed(uint32_t now) {
  if (currentSide < 0 || currentSide >= NUM_SIDES) { renderOff(); return; }
  if (gameMode == MODE_TIMER) renderTimer(now); else renderShare(now);
}
```

- [ ] **Step 4: Verify it compiles**

Ask the user to run `make compile-all`. Nothing calls these yet, so expect only a
small flash increase. **Stop and wait.**

- [ ] **Step 5: Commit**

```bash
git add firmware/turn_counter/turn_counter.ino
git commit -m "turn_counter: countdown and time-share renderers"
```

---

## Task 5: Frame loop and repaint routing

The task where the `renderCurrent()`/`startPlay()` distinction is load-bearing.

**Files:**
- Modify: `firmware/turn_counter/turn_counter.ino`

- [ ] **Step 1: Route `renderCurrent()`**

Replace `renderCurrent()` entirely:

```cpp
// Redraw whatever should be on screen, without changing any game state. This is
// the safe repaint: startPlay() clears ready[] and restarts the turn clock,
// either of which would be wrong for a brightness or power change.
void renderCurrent() {
  if (!tableLit) { renderOff(); return; }
  if (inSetupMode) return;            // setup re-renders from loop() every pass
  if (readyMode()) { renderReady(); return; }
  if (timedMode()) { lastDrawnLeds = -1; renderTimed(millis()); return; }
  renderTurn();
}
```

- [ ] **Step 2: Route `startPlay()`**

Replace `startPlay()` entirely:

```cpp
// Start (or resume) play in the current mode. Unlike renderCurrent() this is a
// fresh start: the READY round is cleared and the turn clock restarts.
void startPlay() {
  if (!tableLit) { renderOff(); return; }
  turnStartMs = millis();
  lastDrawnLeds = -1;
  if (readyMode()) {
    for (uint8_t s = 0; s < NUM_SIDES; s++) ready[s] = false;
    renderReady();
  } else if (timedMode()) {
    renderTimed(millis());
  } else {
    renderTurn();
  }
}
```

- [ ] **Step 3: Add the frame tick**

Immediately after `renderTimed()`:

```cpp
// Timed modes animate continuously — every other mode repaints only on a tap.
// FastLED.show() blocks ~6.6 ms on 221 LEDs, so the renderers skip it when
// nothing moved; setup mode already shows on every pass and taps register fine
// through it, so 20 fps is well inside proven territory.
void modeTick(uint32_t now) {
  if (!tableLit || inSetupMode || !timedMode()) return;
  if (now - modeFrameMs < MODE_FRAME_MS) return;
  modeFrameMs = now;
  renderTimed(now);
}
```

- [ ] **Step 4: Call it from `loop()`**

After the `refuseTick(now);` line added by the prerequisite plan:

```cpp
  modeTick(now);
```

- [ ] **Step 5: Pause the clock while dark**

In `applyPower()`, replace the body between the setup-abort check and the
`Serial.printf`:

```cpp
  if (!lit) bankTurnTime();        // freeze the share clock where it stands
  tableLit = lit;
  if (lit) {
    // Relighting gives the current player a fresh turn clock — a shot clock
    // that expired in the dark isn't a shot clock. Share history survives;
    // only the in-progress turn restarts.
    turnStartMs = millis();
    lastDrawnLeds = -1;
    renderCurrent();               // NOT startPlay() — that would clear ready[]
  } else {
    renderOff();
  }
```

- [ ] **Step 6: Reset share stats on mode selection**

In `applyMode()`, add immediately before the `prefs.putUChar("mode", ...)` line:

```cpp
  // Re-selecting SHARE from the phone is the "new game" gesture, so this fires
  // even when SHARE was already the active mode.
  if (gameMode == MODE_SHARE) resetShareStats();
```

`applyMode()` already ends in `startPlay()`, which resets `turnStartMs` and
`lastDrawnLeds`, so switching into TIMER always begins on a full clock.

- [ ] **Step 7: Repaint through `renderCurrent()` on a turn pass**

In `commitTap()`'s turn-passing branch, replace:

```cpp
  if (side == currentSide) {
    advanceTurn();
    renderTurn();
```

with:

```cpp
  if (side == currentSide) {
    advanceTurn();
    lastDrawnLeds = -1;   // new seat, new length — don't suppress the first frame
    renderCurrent();      // identical to renderTurn() for CW/CCW/ARB
```

- [ ] **Step 8: Verify it compiles**

Ask the user to run `make compile-all`. **Stop and wait.**

- [ ] **Step 9: Commit**

```bash
git add firmware/turn_counter/turn_counter.ino
git commit -m "turn_counter: timed-mode frame loop and repaint routing"
```

---

## Task 6: Timer duration and its deferred NVS write

**Files:**
- Modify: `firmware/turn_counter/turn_counter.ino`

- [ ] **Step 1: Add the setter and the deferred write**

In the `apply*` block, after `applyLock()`:

```cpp
bool applyTurnSeconds(uint16_t secs) {
  turnSeconds = secs;
  turnSecondsDirty = true;
  turnSecondsChangedMs = millis();
  if (gameMode == MODE_TIMER) {
    // Restart rather than let the new length apply mid-turn: dropping 120 s to
    // 30 s would expire the turn instantly, which reads as a crash.
    turnStartMs = millis();
    lastDrawnLeds = -1;
    renderCurrent();
  }
  Serial.printf("Turn timer set to %u s\n", turnSeconds);
  return true;
}

// Mirrors brightnessTick's deferred write: dragging a slider costs one flash
// write instead of twenty.
void turnSecondsTick(uint32_t now) {
  if (!turnSecondsDirty || now - turnSecondsChangedMs < TIMER_SETTLE_MS) return;
  turnSecondsDirty = false;
  prefs.putUShort("tsec", turnSeconds);
  Serial.printf("Turn timer saved: %u s\n", turnSeconds);
}
```

- [ ] **Step 2: Load it at boot**

In `setup()`, after the `gameMode` load and clamp:

```cpp
  turnSeconds = prefs.getUShort("tsec", TIMER_SECONDS_DEFAULT);
  if (turnSeconds < TIMER_SECONDS_MIN || turnSeconds > TIMER_SECONDS_MAX) {
    turnSeconds = TIMER_SECONDS_DEFAULT;
  }
```

- [ ] **Step 3: Drive the deferred write from `loop()`**

After the `modeTick(now);` line:

```cpp
  turnSecondsTick(now);
```

- [ ] **Step 4: Verify it compiles**

Ask the user to run `make compile-all`. **Stop and wait.**

- [ ] **Step 5: Commit**

```bash
git add firmware/turn_counter/turn_counter.ino
git commit -m "turn_counter: persisted turn-timer duration"
```

---

## Task 7: Expose both modes to the phone

**Files:**
- Modify: `firmware/turn_counter/web_ui.h`
- Modify: `firmware/turn_counter/web_ui.cpp`
- Modify: `firmware/turn_counter/turn_counter.ino`

- [ ] **Step 1: Extend the interface**

In `web_ui.h`, add the include at the top (for `NUM_SIDES` — consistent with the
boundary this header already documents: hardware facts come from the library):

```cpp
#include <octagon_core.h>
```

Add to `TableState`, after `bool locked;`:

```cpp
  uint16_t turnSeconds;             // countdown length
  int32_t  secondsLeft;             // -1 when the mode has no clock, 0 = expired
  uint8_t  sharePct[NUM_SIDES];     // percent of table time per seat
```

Add to `TableConfig`:

```cpp
  uint16_t timerMinSeconds;
  uint16_t timerMaxSeconds;
  uint8_t  timerMode;    // mode indices, so the page tests identity rather than
  uint8_t  shareMode;    // matching on display names
```

Add the typedef after `LockSetter`:

```cpp
typedef bool (*TurnSecondsSetter)(uint16_t seconds);   // always true today
```

And to `TableHooks`, after `setLock`:

```cpp
  TurnSecondsSetter setTurnSeconds;
```

- [ ] **Step 2: Emit the new state**

In `web_ui.cpp`'s `sendState()`, extend the format string (the buffer is already
`char[384]` from the prerequisite plan):

```cpp
  int n = snprintf(buf, sizeof(buf),
                   "{\"mode\":%u,\"brightness\":%u,\"lit\":%s,\"currentSide\":%d,"
                   "\"roster\":%u,\"ready\":%u,\"setup\":%s,\"locked\":%s,"
                   "\"turnSeconds\":%u,\"secondsLeft\":%ld,\"sharePct\":[",
                   s.mode, s.brightnessPercent, s.lit ? "true" : "false", s.currentSide,
                   s.rosterMask, s.readyMask, s.inSetupMode ? "true" : "false",
                   s.locked ? "true" : "false",
                   s.turnSeconds, (long)s.secondsLeft);
  for (uint8_t i = 0; i < NUM_SIDES && n > 0 && n < (int)sizeof(buf); i++) {
    n += snprintf(buf + n, sizeof(buf) - n, "%s%u", i ? "," : "", s.sharePct[i]);
  }
  if (n > 0 && n < (int)sizeof(buf)) snprintf(buf + n, sizeof(buf) - n, "]}");
  server.send(code, "application/json", buf);
```

- [ ] **Step 3: Emit the new config**

In `handleConfig()`, replace the final `snprintf` (the one writing `"]}"`) with:

```cpp
  if (n < (int)sizeof(buf)) {
    snprintf(buf + n, sizeof(buf) - n,
             "],\"timerMin\":%u,\"timerMax\":%u,\"timerMode\":%u,\"shareMode\":%u}",
             cfg.timerMinSeconds, cfg.timerMaxSeconds, cfg.timerMode, cfg.shareMode);
  }
```

The existing `char[512]` still holds: six mode names run ~103 bytes, eight
colours ~96, and the new fields ~70, for roughly 300.

- [ ] **Step 4: Add the endpoint**

After `handleLock()`:

```cpp
static void handleTimer() {
  long v;
  if (!server.hasArg("seconds") || !parseUInt(server.arg("seconds"), v)) {
    server.send(400, "text/plain", "seconds must be a number");
    return;
  }
  if (v < cfg.timerMinSeconds || v > cfg.timerMaxSeconds) {
    server.send(400, "text/plain", "seconds out of range");
    return;
  }
  hooks.setTurnSeconds((uint16_t)v);
  sendState(200);
}
```

And the route in `webUiBegin()`:

```cpp
  server.on("/api/timer",      HTTP_POST, handleTimer);
```

- [ ] **Step 5: Fill the state in the sketch**

In `turn_counter.ino`'s `readTableState()`, add before the closing brace:

```cpp
  uint32_t live  = millis() - turnStartMs;
  uint32_t total = live;
  for (uint8_t i = 0; i < NUM_SIDES; i++) total += sideMs[i];
  for (uint8_t i = 0; i < NUM_SIDES; i++) {
    uint32_t ms = sideMs[i] + ((int8_t)i == currentSide ? live : 0);
    // A plain percentage of table time, NOT the doubled fair-share figure the
    // LED bar uses: the bar answers "over or under my share" across a room, the
    // phone shows the real numbers, and eight of these should sum to 100.
    s.sharePct[i] = (total == 0) ? 0 : (uint8_t)((uint64_t)ms * 100 / total);
  }

  s.turnSeconds = turnSeconds;
  if (gameMode == MODE_TIMER) {
    uint32_t span    = (uint32_t)turnSeconds * 1000;
    uint32_t elapsed = millis() - turnStartMs;
    // Round up, so the last second reads "1 s" rather than "0 s left".
    s.secondsLeft = (elapsed >= span) ? 0 : (int32_t)((span - elapsed + 999) / 1000);
  } else {
    s.secondsLeft = -1;
  }
```

- [ ] **Step 6: Update the config and hooks**

Replace the `WEB_CONFIG` definition:

```cpp
const TableConfig WEB_CONFIG = {MODE_NAMES, MODE_COUNT, TIMER_SECONDS_MIN,
                                TIMER_SECONDS_MAX, MODE_TIMER, MODE_SHARE};
```

And add to the `TableHooks` initialiser in `beginOta()`:

```cpp
    .setTurnSeconds = applyTurnSeconds,
```

- [ ] **Step 7: Verify it compiles**

Ask the user to run `make compile-all`. **Stop and wait.**

- [ ] **Step 8: Commit**

```bash
git add firmware/turn_counter/web_ui.h firmware/turn_counter/web_ui.cpp firmware/turn_counter/turn_counter.ino
git commit -m "web_ui: timer duration endpoint and time-share state"
```

---

## Task 8: The two page cards

**Files:**
- Modify: `firmware/turn_counter/web_ui.cpp` (the `PAGE_HTML` PROGMEM string)

- [ ] **Step 1: Add the styles**

In the `<style>` block, after the `.note` rule:

```css
.srow{display:flex;align-items:center;gap:9px;margin-bottom:7px;font-size:13px}
.sbar{flex:1;height:9px;border-radius:5px;background:#2c2c2e;overflow:hidden}
.sfill{height:100%;border-radius:5px}
.spct{width:38px;text-align:right;color:#8e8e93}
```

- [ ] **Step 2: Add the markup**

After the brightness card, before `<div id="err"></div>`:

```html
<div class="card hide" id="timercard">
  <div class="lbl">Turn timer <span id="tsecs"></span></div>
  <input type="range" id="tsec" step="5">
  <div class="note" id="tleft"></div>
</div>
<div class="card hide" id="sharecard">
  <div class="lbl">Time share</div>
  <div id="sharerows"></div>
  <button id="sharereset" style="text-align:center;margin-top:10px">Reset stats</button>
</div>
```

- [ ] **Step 3: Extend `render(s)`**

Add at the end of `render(s)`:

```js
  $('timercard').classList.toggle('hide', s.mode!==cfg.timerMode);
  $('sharecard').classList.toggle('hide', s.mode!==cfg.shareMode);

  if(s.mode===cfg.timerMode){
    if(!tdragging && Date.now()-tlastLocal>2000){ $('tsec').value=s.turnSeconds; }
    $('tsecs').textContent=$('tsec').value+' s';
    $('tleft').textContent = !s.lit ? 'Paused while the table is off.'
      : s.secondsLeft===0 ? 'Time up — tap the seat to pass.'
      : s.secondsLeft+' s left';
  }

  if(s.mode===cfg.shareMode){
    $('sharerows').innerHTML = s.sharePct.map((p,i)=>
      ((s.roster>>i)&1) ?
        '<div class="srow"><span>'+i+'</span><div class="sbar"><div class="sfill" '+
        'style="width:'+p+'%;background:'+cfg.colors[i]+'"></div></div>'+
        '<span class="spct">'+p+'%</span></div>'
      : '').join('');
  }
```

- [ ] **Step 4: Add the slider state and wiring**

Extend the declarations at the top of the script:

```js
let cfg=null, lastLocal=0, pending=null, dragging=false;
let tlastLocal=0, tpending=null, tdragging=false;
```

In the IIFE, after the brightness wiring and before `await poll();`:

```js
  const tsec=$('tsec');
  tsec.min=cfg.timerMin; tsec.max=cfg.timerMax;
  const tsend=()=>{ tlastLocal=Date.now(); post('/api/timer?seconds='+tsec.value); };
  tsec.oninput=()=>{
    $('tsecs').textContent=tsec.value+' s';
    tdragging=true; tlastLocal=Date.now();
    if(tpending) return;                        // throttle to 1 POST / 250 ms
    tpending=setTimeout(()=>{ tpending=null; tsend(); },250);
  };
  tsec.onchange=()=>{ tdragging=false; tsend(); };

  $('sharereset').onclick=()=>post('/api/mode?value='+cfg.shareMode);
```

Re-posting the active mode is what resets the stats. A button is more
discoverable than a note explaining that re-tapping the mode does it.

- [ ] **Step 5: Verify it compiles**

Ask the user to run `make compile-all`. Expected flash ~70%. **Stop and wait.**

- [ ] **Step 6: Commit**

```bash
git add firmware/turn_counter/web_ui.cpp
git commit -m "web_ui: turn-timer slider and time-share readout"
```

- [ ] **Step 7: Bench check**

Ask the user to run `make ota`, then work through these. Each is a case that
would otherwise fail quietly:

**Countdown timer**
1. Select it on the phone. The current seat lights full and LEDs go dark from
   both ends, one every ~2 s at the default 60 s.
2. Let it run out. The side goes red and breathes, and **holds** — it must not
   auto-pass.
3. Tap the seat. The turn passes and the clock restarts full.
4. Drag the slider mid-turn. The turn must restart, not expire instantly. Leave
   it alone for 3 s and confirm the serial line `Turn timer saved: N s`.
5. Power-cycle the table and confirm the slider comes back at the value you set.

**Time share**
6. Select it. All seats open at half a side.
7. Sit on one seat for ~30 s. The bar grows.
8. Pass around the table, then come back. Your bar is **shorter** than you left
   it. This is the whole mode.
9. Check the phone's percentages sum to ~100 and track the ring.
10. Hit **Reset stats**. Everything returns to half.

**Interaction**
11. From TIMER, four-tap a side (unlock first if needed). The dial must open on
    **clockwise**, not on a mode it cannot animate.
12. In TIMER, change brightness from the phone. The clock must **not** restart —
    that is the `renderCurrent()`/`startPlay()` hazard.
13. In TIMER, turn the table off and on from the phone. The clock restarts full
    and share history survives.

---

## Task 9: Update the docs

**Files:**
- Modify: `turn_counter_design_doc.md`
- Modify: `design_doc_simple.md`

- [ ] **Step 1: Find the affected claims**

```bash
grep -n "four modes\|4 modes\|Game modes\|game mode\|/api/" turn_counter_design_doc.md design_doc_simple.md
```

- [ ] **Step 2: Update them**

Both docs say the table has four game modes chosen on the dial. Correct that to:
four dial modes plus two phone-only modes, and state plainly that the four-tap
gesture always returns the table to a dial mode. Describe each new mode in a
sentence — the countdown drains the current seat and holds red at zero; time
share grows your bar while you play and shrinks it while others do.

Add `POST /api/timer?seconds=10..300` to the endpoint tables.

- [ ] **Step 3: Commit**

```bash
git add turn_counter_design_doc.md design_doc_simple.md
git commit -m "docs: countdown timer and time-share modes"
```

---

## Done criteria

- [ ] `pytest tests/ -v` passes — 20 existing, 4 from the diagnostics plan, 15
      here (11 functions, one parametrised over 5 seat counts)
- [ ] `make compile-all` links, flash ~70%
- [ ] All 13 bench checks pass, especially #8 (the bar is shorter on return),
      #11 (the dial clamp) and #12 (brightness does not restart the clock)
- [ ] Docs describe six modes and say which two are phone-only
