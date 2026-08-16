# Timed modes — countdown timer and time share

**Date**: 2026-08-16
**Status**: implemented
**Depends on**: `2026-08-16-setup-lock-and-diagnostics-design.md` (introduces
`MODE_DIAL_COUNT` and the `TableHooks` struct this spec extends)

## Goal

Two new game modes, both about *time* rather than turn order:

- **Countdown timer** — a shot clock. The current seat's lights click off as the
  turn runs down.
- **Time share** — the current seat's bar length tracks that player's share of
  total table time, so it grows while you sit there and is shorter when the turn
  comes back around.

Both are **phone-only**. The table's four-tap setup dial keeps demoing the same
four modes it always has.

## Non-goals

- No per-player timer lengths. One duration for the table.
- No persisted time-share history. Stats are RAM-only and reset with the game.
- The timer does not enforce anything. At zero it complains; the turn still
  passes on a tap.
- Neither mode changes turn order. Both pass clockwise.
- `eight` is untouched.

---

## Mode space splits from dial space

The setup dial has to keep demoing exactly four modes — a mode it cannot animate
has no business on a dial whose whole premise is "the animation IS the mode." So
the enum grows two values while `MODE_DIAL_COUNT` stays put:

```cpp
enum GameMode : uint8_t {
  MODE_CW = 0, MODE_CCW = 1, MODE_ARB = 2, MODE_READY = 3,
  MODE_DIAL_COUNT = 4,             // what the table's dial demos
  MODE_TIMER = 4, MODE_SHARE = 5,
  MODE_COUNT = 6                   // what applyMode() accepts
};
```

`MODE_DIAL_COUNT` and `MODE_TIMER` deliberately share the value 4. They mean
different things — one is a count, one is an index — and tying them together is
the point: the dial demos `[0, MODE_DIAL_COUNT)` and the phone-only modes start
exactly where the dial stops.

Four places key off the distinction:

| Site | Change |
|---|---|
| `renderModeDemo` advance | `dialMode = (dialMode + 1) % MODE_DIAL_COUNT` |
| `MODE_ABORT_IDLE_MS` | already `MODE_DEMO_MS * MODE_DIAL_COUNT` from the companion spec |
| `MODE_COLORS[]` | declared `[MODE_DIAL_COUNT]` — it only ever feeds the demo |
| `enterSetupMode` | `dialMode = (gameMode < MODE_DIAL_COUNT) ? gameMode : MODE_CW` |

That last one is the trap: `dialMode = gameMode` would start the dial on a mode
it cannot demo, indexing `MODE_COLORS` out of bounds. The clamp also gives the
gesture a useful meaning — **the four-tap gesture is always a way back to a table
mode**, which is the manual escape from a phone-only mode when the phone is gone.

`MODE_NAMES[]` stays `[MODE_COUNT]` and gains `"countdown timer"` and
`"time share"`; it feeds serial logging and `/api/config`, both of which need all
six. The page builds its mode buttons from `/api/config`, so six buttons appear
with no page change.

Turn passing needs no work at all: `advanceTurn()`'s `default:` case is clockwise,
so both new modes inherit it.

---

## Shared timing state

```cpp
uint32_t turnStartMs = 0;             // when the current seat's turn began
uint32_t sideMs[NUM_SIDES] = {0};     // banked play time per seat, RAM only
uint32_t modeFrameMs = 0;             // last timed-mode repaint
```

Time is **banked on transitions, never accumulated per frame**. A per-frame
`sideMs[cur] += delta` drifts and depends on the loop rate; banking is exact:

```cpp
// Move the current seat's in-progress time into its bank and restart the clock.
void bankTurnTime() {
  uint32_t now = millis();
  if (currentSide >= 0 && currentSide < NUM_SIDES) {
    sideMs[currentSide] += now - turnStartMs;
  }
  turnStartMs = now;
}
```

The live display adds `now - turnStartMs` on the fly, so the bar moves smoothly
without anything being written every frame.

`bankTurnTime()` is called from `advanceTurn()` (before the side changes) and
from `applyPower(false)`.

`startPlay()` sets `turnStartMs = millis()` outright rather than banking — it
means "a fresh start," so the in-progress turn is discarded rather than credited.
`applyMode()` reaches it through `startPlay()`, so switching into TIMER always
begins on a full clock.

**The clock pauses while the table is dark.** Going off banks the elapsed time
and freezes; coming back on gives the current player a fresh turn clock rather
than a shot clock that expired in the dark. Time share keeps its history across
an off/on cycle — only the in-progress turn restarts.

### Resetting

Time-share stats are the *game*, so they reset when a game starts:

```cpp
void resetShareStats() {
  memset(sideMs, 0, sizeof(sideMs));
  turnStartMs = millis();
}
```

Called from `applyMode(MODE_SHARE)` — including when SHARE is already the active
mode, so **re-tapping the mode button on the phone is the "new game" gesture** —
and from `exitSetupMode()`, since a new roster is a new game.

---

## Countdown timer

### Rendering

The current seat lights in its own color, full side, and LEDs go dark **from both
ends inward** so the last light left is directly in front of the player. At 60 s
across a 28-LED side that is one LED every ~2.1 s.

```cpp
uint16_t lit = (uint32_t)len * (total - elapsed) / total;   // LEDs remaining
uint8_t  off = (len - lit) / 2;                             // centred block
```

Over the final quarter the remaining block warms toward red, so "almost out"
reads across a loud room without anyone counting LEDs:

```cpp
CRGB col = PLAYER_COLORS[currentSide];
uint32_t warn = total / 4;
uint32_t left = total - elapsed;
if (left < warn) {
  col = blend(col, CRGB(255, 0, 0), 255 - (uint8_t)(left * 255 / warn));
}
```

### Expiry

At zero the whole side goes red and breathes at 2 Hz, and **holds there
indefinitely**. The turn passes only when someone taps, which restarts the clock.
The table complains; it never takes the shot away mid-stroke.

```cpp
fillSide(currentSide, CRGB(beatsin8(120, 40, 255), 0, 0));
```

`beatsin8` is FastLED's own `millis()`-driven oscillator, so the pulse needs no
state of its own and stays in phase across repaints.

### Duration

```cpp
const uint16_t TIMER_SECONDS_DEFAULT = 60;
const uint16_t TIMER_SECONDS_MIN     = 10;
const uint16_t TIMER_SECONDS_MAX     = 300;

uint16_t turnSeconds = TIMER_SECONDS_DEFAULT;   // persisted, "turntable"/"tsec"
```

Set from the phone; loaded and clamped at boot. Changing it **restarts the
current turn's clock** — otherwise dropping from 120 s to 30 s mid-turn would
expire it instantly, which reads as a crash.

The NVS write is deferred exactly the way brightness already does it: a dirty
flag plus a 2 s settle, so dragging the slider costs one flash write rather than
twenty. The page also throttles to one POST per 250 ms, but that alone would
still be ~10 writes per adjustment.

**`TIMER_SECONDS_MAX` is capped at 300 because `parseUInt` in `web_ui.cpp`
rejects anything over 3 digits.** Raising the ceiling past 999 means widening
that guard first. Noted so the next person does not chase a silent 400.

---

## Time share

### The formula

Bar length is the seat's share of table time measured against a **fair share of
half the side**:

```
lit = sideLen × mine × activeCount / (2 × total)          capped at sideLen
```

At an even table every seat sits at half-lit, which gives the bar headroom in
both directions — you can see you are *under* your share, not only over. A full
side means you have burned double your share.

The dynamics fall out correctly. While it is your turn, `mine` and `total` climb
together; with both rates equal the derivative of `mine/total` is
`(total − mine)/total²`, positive as long as you are not the only player, so the
bar **grows**. While anyone else plays, `mine` is fixed and `total` climbs, so it
**shrinks**. It responds to every other player, not just the leader — which is
what makes "my bar is shorter than I left it" mean something.

Before any time accrues (`total == 0`) it opens at half the side, so the table
looks neutral at the start rather than dark or full.

A one-LED floor keeps the current seat visible: a bar that reaches zero is
indistinguishable from a dead piezo.

### Overflow

`mine × activeCount × sideLen` in `uint32_t` overflows at roughly six hours of
banked play (18,000,000 ms × 8 × 29 ≈ 4.18 × 10⁹, against a ceiling of
4.29 × 10⁹). A long night would wrap the bar to nonsense. The intermediate is
computed in `uint64_t`:

```cpp
uint16_t lit = (uint32_t)(((uint64_t)mine * n * len) / (2ULL * total));
```

64-bit multiply-divide costs a few microseconds on the S3 and runs 20 times a
second. Cheap insurance against a bug that would only ever appear at the end of a
long session, which is the worst possible time to find it.

### Direction

The bar is drawn centred, from the ends inward, matching the timer. One visual
grammar for both timed modes: the lit block always sits in front of the player
and shrinks symmetrically.

---

## The frame loop

Both modes animate continuously, unlike every existing mode, which repaints only
on a tap.

```cpp
const uint16_t MODE_FRAME_MS = 50;    // 20 fps
int16_t lastDrawnLeds = -1;           // -1 = force a repaint

bool timedMode() { return gameMode == MODE_TIMER || gameMode == MODE_SHARE; }

void modeTick(uint32_t now) {
  if (!tableLit || inSetupMode || !timedMode()) return;
  if (now - modeFrameMs < MODE_FRAME_MS) return;
  modeFrameMs = now;
  renderTimed(now);
}
```

`renderTimed` does the drawing with no interval guard, so `renderCurrent()` can
call it directly for an immediate repaint.

`FastLED.show()` blocks for ~6.6 ms on 221 LEDs, so a naive 20 fps repaint would
spend 13% of the loop with interrupts disabled. Setup mode already shows on every
single pass and taps register fine through it, so this is well inside proven
territory — but there is no reason to pay it when nothing moved:

- **Timer, outside the final quarter**: repaint only when the LED count changes.
- **Timer, final quarter or expired**: repaint every frame (the colour warm and
  the pulse both change continuously). At most 15 s of a 60 s turn.
- **Share**: repaint only when the LED count changes.

`lastDrawnLeds` holds the last count drawn; `-1` forces the next frame. It is
invalidated wherever the scene changes underneath: `startPlay()`,
`applyMode()`, `applyPower(true)`, and `renderCurrent()`.

### Routing

`renderCurrent()` and `startPlay()` both grow a timed-mode branch. The
distinction between them still matters and still bites the same way:
`startPlay()` resets the turn clock, `renderCurrent()` does not — so a brightness
change or a power-on repaint must not silently hand the current player a fresh
shot clock, exactly as it must not clear a READY round's greens.

```cpp
void renderCurrent() {
  if (!tableLit) { renderOff(); return; }
  if (inSetupMode) return;
  if (readyMode()) { renderReady(); return; }
  if (timedMode()) { lastDrawnLeds = -1; renderTimed(millis()); return; }
  renderTurn();
}
```

`commitTap`'s turn-passing branch changes `renderTurn()` to `renderCurrent()`, so
one call site covers all five turn-passing modes. For CW/CCW/ARB the two are
identical.

---

## Phone

### State

```cpp
struct TableState {
  ...
  uint16_t turnSeconds;          // countdown length
  int32_t  secondsLeft;          // -1 when the mode has no clock; 0 = expired
  uint8_t  sharePct[NUM_SIDES];  // percent of table time per seat
};
```

`web_ui.h` picks up `#include <octagon_core.h>` for `NUM_SIDES`, consistent with
the boundary it already documents: hardware facts come from the library, game
facts come through the interface.

`sharePct[s]` is `sideMs[s] × 100 / total` — a plain percentage of table time,
*not* the doubled fair-share figure the LED bar uses. The bar's job is "am I over
or under my share" at a glance across a room; the phone's job is the real
numbers, and eight percentages that sum to 100 are what someone expects to read.
All zeros when `total == 0`. The live seat includes its in-progress time, so the
phone and the ring agree.

`sharePct` is what makes this mode legible. With only your own seat lit, the ring
cannot show you the whole picture — the phone is the only place to see who is
hogging the table. Eight bytes in the struct and ~30 in the JSON.

`TableHooks` (from the companion spec) gains one field:

```cpp
typedef bool (*TurnSecondsSetter)(uint16_t seconds);
```

### Endpoint

| Method | Path | Purpose |
|---|---|---|
| POST | `/api/timer?seconds=10..300` | set the countdown length |

Out of range is 400, matching `/api/brightness`. There is no 409 case — the
duration can be set in any mode, so a player can dial it in before switching.

### Page

Two cards, each shown only when its mode is active (the page already toggles
`.hide` off polled state):

- **Turn timer** — a 10–300 s slider in 5 s steps with its value beside the
  label, plus a live "43 s left" from `secondsLeft`, or "time up" at zero.
- **Time share** — the eight seats with their percentages and a mini bar in each
  seat's colour, plus a **Reset** button that re-posts `/api/mode?value=5`. The
  reset semantics are otherwise invisible, and a button is more discoverable
  than a note explaining that re-tapping the mode does it.

Adds roughly 1.5 KB of PROGMEM.

---

## Testing

The share arithmetic is fixed-point integer maths with a cap, a floor, a
divide-by-zero case and an overflow guard — precisely the shape that has already
bitten once on this project, when the brightness lerp crawled sub-raw-unit
fractions for 600 ms. That bug was caught by modelling the maths in Python before
flashing, and this gets the same treatment.

`tests/test_timed_modes.py` ports both formulas exactly and asserts:

**Time share**
- `total == 0` → half the side (neutral open)
- an even table → every seat at half, whatever the seat count
- a seat's bar grows across its own turn
- a seat's bar shrinks while *any* other seat plays, including one that is not
  the leader
- a seat at double its fair share caps at the full side and stays there
- a seat with near-zero time floors at 1 LED, never 0
- six hours of banked play across eight seats produces a sane value — the
  regression test for the `uint64_t` intermediate, run against a 32-bit model to
  prove it would have wrapped

**Countdown timer**
- LEDs remaining decreases monotonically to 0 across the duration
- the lit block stays centred: `off + lit <= len` at every step
- the warn blend is 0 at the quarter mark and 255 at expiry, with no wrap
- `elapsed >= total` selects the expired branch rather than a negative length

Bench checks for the user, listed in the plan: the timer drains evenly and holds
red at zero; a tap restarts it; changing the duration mid-turn restarts rather
than expiring; the share bar grows on your turn and is shorter when it returns;
switching to SHARE twice resets; the four-tap gesture from TIMER opens the dial
on clockwise rather than misbehaving.

## Risk and cost

The largest behavioural surface is `renderCurrent()` vs `startPlay()`. Getting it
backwards hands out free shot clocks on every brightness nudge — the same class
of mistake as the READY-greens hazard, in the same two functions, so it gets the
same explicit test at the bench.

Flash is at 69% (1,365,215 / 1,966,080) before the companion spec. Two renderers,
a frame tick, an endpoint and two page cards land around 5 KB, so roughly 70%
with both specs in. There is ~600 KB of headroom; this is not close to the
partition.
