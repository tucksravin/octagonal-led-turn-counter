# Setup lock, over-the-air diagnostics, and abort timing

**Date**: 2026-08-16
**Status**: draft

## Goal

Three fixes to how the table behaves around setup mode, all reachable without a
USB cable:

1. **Diagnostics over the air.** Piezo baselines, per-side last-tap times and
   link health are currently visible only on the serial console at boot. Judging
   whether a piezo has gone bad means carrying a laptop to the table.
2. **A setup lock.** The four-tap gesture is easy to trip by accident, and a
   guest who trips it resets the roster. There is no way to say "leave the table
   alone."
3. **Abort timing.** Phase-1 abort fires after five demos but there are only four
   modes, so the dial wraps and replays the opening mode in full before giving
   up.

## Non-goals

- No auth on the diagnostics endpoint. Same posture as the rest of the web UI
  and OTA: trusted LAN, nothing destructive exposed. `/api/diag` is read-only.
- The lock does not restrict the phone. It exists to stop hands at the table,
  and the phone is what unlocks it.
- No live oscilloscope view of piezo readings. Baseline plus time-since-tap
  answers "is this piezo alive and is its resting level sane," which is the
  question that keeps coming up.
- `eight` gains nothing here. It has no radio and no setup mode.

---

## 1. Diagnostics: `GET /api/diag`

### Motivating case

Side 7 reads a baseline of 287 while every other side sits between 9 and 19.
That is either a real fault (a disc coming unbonded, a marginal ground) or an
artifact of where the board sat during `octagonBegin()`'s seeding window. Right
now answering that means a cable and a reboot, because the baselines are printed
once at boot and never again. `/api/diag` reports the *live* value.

### New accessor in `octagon_core`

The per-side timestamps already exist as file-static state
(`lastTapPerSide[NUM_SIDES]`, written by the tap scanner). Only an accessor is
missing, mirroring the existing `baseline(i)`:

```cpp
// octagon_core.h
uint32_t lastTapForSide(uint8_t i);   // 0 if this side has never fired
```

```cpp
// octagon_core.cpp
uint32_t lastTapForSide(uint8_t i) {
  return (i < NUM_SIDES) ? lastTapPerSide[i] : 0;
}
```

`baseline()`, `totalLeds()`, `sideLedCounts[]`, `sidePiezoPin[]` and `TAP_DELTA`
are already exported, so nothing else in the library changes.

### Response shape

```json
{
  "tapDelta": 720,
  "uptimeMs": 1843221,
  "freeHeap": 214880,
  "rssi": -54,
  "sides": [
    {"pin": 5, "baseline": 14, "leds": 29, "sinceTapMs": 4211},
    {"pin": 2, "baseline": 11, "leds": 28, "sinceTapMs": -1}
  ]
}
```

`sinceTapMs` is `-1` for a side that has never tapped since boot. Reporting
`now - 0` there would read as "tapped at boot," which is the opposite of the
truth and exactly the case you are looking for when a piezo is dead.

Built with the same running-`snprintf` pattern as `handleConfig`, into a
`char[768]`: eight side objects at ~58 bytes plus a ~110-byte header is ~575, so
the buffer has room without being extravagant. Every `snprintf` is bounded by
the remaining space and the loop stops on overflow, as `handleConfig` already
does.

`WiFi.RSSI()` is valid here because the endpoint can only be reached over a live
link.

### Page

A `<details>` block at the bottom of the page, collapsed by default. Opening it
fetches `/api/diag` once and renders a table; a Refresh button re-fetches. It is
deliberately **not** part of the 2 s poll — diagnostics are something you go
look at, not something the page streams.

Rows where `baseline` is more than 4× the median across sides render in amber,
which is what makes the side-7 case jump out without anyone having to remember
what a normal baseline looks like.

---

## 2. Setup lock

### Behaviour

`setupLocked` lives in RAM only and defaults to `false` at boot, so **a power
cycle always clears it**. This mirrors `tableLit`, which is already unpersisted
for the same reason: plug in = on, plug in = unlocked, and no stored flag can
leave the table looking broken to someone who does not know it is there.

Turning the table off from the phone does *not* clear the lock — that is
`tableLit`, a separate piece of state. Only an actual reboot does.

When locked, a four-tap burst **while the table is lit** does not open setup.
Everything else is untouched: turns still pass, READY still toggles, brightness,
mode and power still work from the phone.

**Carve-out: the lock never applies while the table is dark.** The wake burst is
the only way to relight the table by hand, and a locked table that cannot be
woken is a table that looks broken. The two states are already mutually
exclusive in `commitTap` — the dark branch returns before any setup handling —
so this falls out of where the check goes rather than needing a special case.

### Where the check goes

`commitTap` has two paths that call `enterSetupMode()`: the READY branch and the
turn-passing branch. Both consume the burst first, so the guard belongs at the
gesture, not at the two call sites:

```cpp
// Returns true when the burst completed AND setup is allowed to open. A refused
// burst is still consumed, so it can't accumulate into the next one.
bool setupGestureFired(int8_t side, uint32_t now) {
  if (!registerTapForSetupGesture(side, now)) return false;
  if (!setupLocked) return true;
  refuseSetup(side, now);
  return false;
}
```

Both branches become `if (setupGestureFired(side, whenMs)) { enterSetupMode(); return; }`.

The dark-branch wake keeps calling `registerTapForSetupGesture` directly, which
is what encodes the carve-out.

### Refusal feedback

Silence would read as a broken piezo. The tapped side double-flashes amber
(`CRGB(255, 130, 0)`) and then the normal scene comes back.

This must not block. A `delay()` inside the tap handler would stall `tapsPoll`
and drag the adaptive baselines, so it runs as a small state machine driven from
`loop()`:

```cpp
const uint16_t REFUSE_BLINK_MS = 120;   // on/off/on/off = 480 ms total
int8_t   refuseSide  = -1;
uint32_t refuseStart = 0;

void refuseSetup(int8_t side, uint32_t now) {
  refuseSide = side;
  refuseStart = now;
  Serial.printf("Setup gesture on side %d refused - table is locked\n", side);
}

void refuseTick(uint32_t now) {
  if (refuseSide < 0) return;
  uint32_t phase = (now - refuseStart) / REFUSE_BLINK_MS;
  if (phase >= 4) { refuseSide = -1; renderCurrent(); return; }
  if (phase % 2 == 0) {
    fillSide(refuseSide, CRGB(255, 130, 0));
    FastLED.show();
  } else {
    renderCurrent();     // repaints the real scene, so the flash reads as a blink
  }
}
```

Alternating between the amber fill and `renderCurrent()` means the flash overlays
whatever is actually on screen without needing a saved copy of the buffer.
`renderCurrent()` is the safe repaint — it does not clear `ready[]` — so a READY
round survives a refused burst.

Called from `loop()` alongside `brightnessTick`, and only when `tableLit &&
!inSetupMode`.

### Phone

`POST /api/lock?value=on|off`, validated the same way as `/api/power`. `"locked"`
joins `/api/state`. The page gets a toggle in its own card, worded as what it
does rather than what it is set to — "Setup locked" with a pressed state, and a
one-line note that the table can still be woken while dark.

Locking while setup is already open does **not** abort it. The lock governs the
next gesture; yanking an in-progress setup out from under someone at the table
would be worse than letting it finish.

### The lockout trap

If the table is locked and Wi-Fi is down, there is no phone and no gesture — the
table would be stuck in whatever mode it was in. Not persisting the lock is what
makes that unreachable: unplug the table and plug it back in, and the gesture
works again. No cable, no console, no app, and it is the thing anyone will try
first anyway.

This is why the lock is not stored despite "leave the table alone" reading like a
standing preference. A persisted lock buys a small convenience — surviving a
power cut — and pays for it with a state that can strand the table. Re-locking
after a reboot is one tap on the phone.

---

## 3. Abort timing

`MODE_ABORT_IDLE_MS` is a hardcoded 25000 against a 5000 ms demo, so phase 1
runs five demos across four modes: the dial wraps and replays the mode it opened
on, in full, before aborting. This was hidden when demos were 7500 ms (35000 /
7500 = 4.67 — merely long); shortening the demo turned the remainder into a
whole extra mode.

The fix is to stop hardcoding it:

```cpp
const uint32_t MODE_ABORT_IDLE_MS = MODE_DEMO_MS * MODE_DIAL_COUNT;
```

Exactly one rotation — every mode shown once, then abort, no repeat.

Three details:

- **`uint32_t`, not `uint16_t`.** 5000 × 4 fits in 16 bits today, but a demo
  longer than 16 s would silently wrap. The comparison in `loop()` is already
  `uint32_t` arithmetic.
- **It must move below the `GameMode` enum**, since it now depends on a value the
  enum defines. This is the only structural change.
- **`MODE_DIAL_COUNT`, not `MODE_COUNT`.** They are both 4 today, so the
  distinction looks like noise. It is not: the companion spec
  (`2026-08-16-timed-modes-design.md`) adds two phone-only modes, which makes
  `MODE_COUNT` 6 while the dial still demos 4. Deriving from `MODE_COUNT` would
  quietly stretch the abort back out to 30 s the moment those land. Introducing
  the name here means the timing never has to be revisited.

```cpp
enum GameMode : uint8_t {
  MODE_CW = 0, MODE_CCW = 1, MODE_ARB = 2, MODE_READY = 3,
  MODE_DIAL_COUNT = 4,   // modes the table's setup dial can demo
  MODE_COUNT = 4         // modes applyMode() accepts; diverges in the timed-modes spec
};
```

At the boundary the abort check wins over the advance, so the worst case is a
single 5 ms frame of the fifth demo — below the flicker threshold.

Nothing else about the setup flow changes. `MODE_DEMO_MS` stays at 5000, and the
join phase's `SETUP_JOIN_IDLE_MS` is untouched.

---

## Interface change: `TableHooks`

`webUiBegin` currently takes four bare function pointers. The lock makes five and
the timed-modes spec makes six, all with similar-looking signatures — a
positional argument list that long is a swap waiting to happen, and the compiler
would not catch swapping two `bool(*)(uint8_t)` setters.

```cpp
struct TableHooks {
  StateReader      read;
  ModeSetter       setMode;
  BrightnessSetter setBrightness;
  PowerSetter      setPower;
  LockSetter       setLock;
};

void webUiBegin(const TableConfig &cfg, const TableHooks &hooks);
```

One call site (`beginOta()`), designated initialisers so each hook is named at
the point it is passed. The timed-modes spec adds one field and touches nothing
else.

## State and endpoint summary

`TableState` gains `bool locked`. The `/api/state` buffer grows from `char[256]`
to `char[384]` to cover it and the fields the timed-modes spec adds.

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/diag` | piezo baselines, per-side last tap, heap, RSSI, uptime |
| POST | `/api/lock?value=on\|off` | set the setup lock |

## Testing

There is no on-device test framework, so verification is split:

- **Host tests** (`tests/test_diag_json.py`) for the JSON the endpoint claims to
  produce: a Python model of the same `snprintf` layout, asserting it parses, that
  `sinceTapMs` is `-1` for an untapped side, and that eight sides fit inside 768
  bytes. This catches the buffer-sizing mistake, which is the one that would
  truncate silently in the field.
- **Bench checks** driven by the user, listed in the plan: refused burst
  double-flashes amber and leaves a READY round's greens intact; a locked *dark*
  table still wakes on four taps; a power cycle clears the lock while the phone's
  off/on does not; phase 1 aborts after one rotation with no repeat of the
  opening mode.

## Risk

The one behaviour that could surprise: a refused burst is *consumed*, so tapping
four times, seeing the amber flash, and immediately tapping four more times
starts a fresh burst rather than continuing the old one. That is intentional —
otherwise a locked table would accumulate taps indefinitely and open setup on the
next unlock.

Flash is at 69% (1,365,215 / 1,966,080). This adds an endpoint, a page section
and a small state machine — on the order of 3 KB.
