# Tap guard — surviving a broken piezo

**Date**: 2026-08-16
**Status**: draft

## Goal

Keep the table playable when a piezo channel goes bad mid-game. The 2026-08-16
bench session proved how much damage one fault does: side 7's lost ground return
opened setup by itself, held it open forever (both setup exits key off
`lastAnyTapMs()`), ate real taps from the other seven seats (the biggest-delta
winner rules each scan), and disguised itself as three different bugs before the
hardware was found. The physical fault is fixed; this spec makes the *next* one
a nuisance instead of an outage.

All changes live in `octagon_core`'s tap scanner, so `turn_counter` and `eight`
are both covered automatically.

## Non-goals

- No attempt to keep a *faulty* side playable. A muted seat is dead until its
  signal goes quiet; the goal is that the other seven keep working.
- No persistence. Mute state is RAM-only, like the lock — a power cycle retries
  the hardware from scratch.
- No change to `TAP_DELTA`, `DEBOUNCE_MS`, the baseline EMA, or the wizard.

## The three fault modes

A broken channel presents one of three signatures, and no single mechanism
covers all of them:

| Signature | Example cause | Killed by |
|---|---|---|
| Sustained ring: pegged above threshold, decays in under ~1 s | hard strike, curing bond | hysteresis re-arm |
| Pinned high: above threshold indefinitely, never dips | floating input at rail | stuck-mute timeout |
| Oscillating: crosses threshold at line rate with quiet troughs | floating input picking up mains hum | rate quarantine |

Hysteresis alone cannot stop the oscillating case — the troughs re-arm it every
cycle. The rate quarantine alone cannot stop the pinned case — a side that fires
once and never re-arms never builds a streak. All three are needed.

## Mechanisms

### 1. Hysteresis re-arm

A side that fires is *disarmed* until a scan reads below
`baseline + TAP_DELTA / 2`. A disarmed side cannot fire, no matter how long it
stays hot — so a 900 ms mechanical ring produces exactly one tap instead of one
per `DEBOUNCE_MS`. Real taps decay below the re-arm level in tens of
milliseconds, so normal play never notices.

```cpp
static bool     armed[NUM_SIDES];        // init true; cleared on fire,
                                         // restored by a quiet scan
```

### 2. Stuck-mute

A side that is disarmed and still has not re-armed `STUCK_MUTE_MS` (2000 ms)
after its fire is muted. No genuine ring survives two seconds; a pinned input
never re-arms at all. Without this, a pinned side would shadow the whole table
forever (see "shadowing" below) while never firing — a silent table-wide outage,
strictly worse than the chatter it replaced.

### 3. Rate quarantine

Each fire whose gap since the same side's previous fire is under
`CHATTER_GAP_MS` (400 ms) extends that side's streak; a gap at or above it
resets the streak to 1. At `CHATTER_STREAK` (8) the side is muted and the
crossing fire is swallowed. Mains hum fires at the 250 ms debounce floor, so it
mutes in ~2 s after at most 8 phantom taps. Eight *consecutive* sub-400 ms taps
from a human is a deliberate drum-roll, not play — the setup gesture is 4 taps
in a 2 s window and never gets near it. (A drum-roll that does trip it self-
heals: see unmute.)

### 4. Unmute

A muted side is restored after `UNMUTE_QUIET_MS` (5000 ms) continuously below
the re-arm level. A real fault never goes quiet, so it stays muted until fixed;
a transient (something vibrating on the rim, a drum-roll) clears itself. Tracked
with `lastLoudMs[i]`, stamped by any scan at or above the re-arm level.

## What muted means — the starvation fix

A muted side is invisible to the scanner: it cannot fire, **and it cannot be
the scan's biggest-delta winner**. That second half is the fix for the review's
starvation finding — before, a hot faulty side outranked every real tap and the
debounce discard threw the whole scan away, so the table felt dead while the
fault chattered.

The shadowing behaviour for *healthy* sides is deliberately preserved: the
loudest unmuted hot side still rules the scan, and if it is disarmed or inside
its debounce window the scan is suppressed entirely. That suppression is the
existing cross-talk ghost filter — a neighbour still ringing from a real tap
must not let its ghost commit — and weakening it to fix starvation would trade
a fault-mode bug for an every-game bug. The cost: during a fault's pre-mute
window (at most ~2 s), other seats' taps can still be eaten. Bounded, then over.

## Scanner structure

`readPiezos` keeps its shape; the per-side loop gains the guard bookkeeping and
the fire path funnels through one helper:

```cpp
// Commit a fire on side i: refractory + chatter accounting. False = swallowed.
static bool commitFire(uint8_t i, uint32_t now) {
  uint32_t gap = now - lastTapPerSide[i];
  if (gap <= DEBOUNCE_MS) return false;
  armed[i] = false;
  fireStreak[i] = (lastTapPerSide[i] != 0 && gap < CHATTER_GAP_MS) ? fireStreak[i] + 1 : 1;
  lastTapPerSide[i] = now;
  if (fireStreak[i] >= CHATTER_STREAK) {
    muted[i] = true;
    // serial: side, streak, unmute condition
    return false;
  }
  lastTapMs = now;      // the global setup/idle clock only moves on ACCEPTED taps,
  return true;          // so a muted-in-progress side can no longer wedge setup
}
```

The winner path becomes: pick max delta over hot unmuted sides → if the winner
is disarmed, suppress the scan (ghost filter) → `commitFire(winner)` → the
opposite-pair bit additionally requires `armed[opp]` and its own `commitFire`.

`lastTapMs` moving only on accepted fires is itself part of the insurance: the
setup-wedge mechanism was phantom taps refreshing the idle clocks.

## Visibility

Every transition prints to serial (`MUTED`, `unmuted`, with side, cause and the
unmute condition). `/api/diag` gains `"muted":0|1` per side via a new
`bool sideMuted(uint8_t i)` accessor, and the phone page renders a muted row in
red with a "muted" tag — so the failure mode announces itself instead of
masquerading as a game bug.

## Testing

`tests/test_tap_guard.py` ports the scanner's guard logic and drives it with
synthetic signals at the 5 ms scan cadence: a clean tap (one fire), a 900 ms
sustained ring (one fire — the phantom case), a pinned input (one fire, muted at
2 s, other sides usable after), 60 Hz hum (≤ 8 fires, muted ~2 s, unmutes 5 s
after the hum stops), a 4-tap gesture at 500 ms gaps (never mutes), an 8-tap
drum-roll at 300 ms (mutes, then self-heals), and the ghost cases (same-scan
loser suppressed; next-scan ghost suppressed by the disarmed winner's shadow).

Bench: with the table healthy, confirm play, the gesture and READY feel
unchanged; then simulate a fault by tapping one disc continuously and confirm
the mute announces itself and the other seats keep passing turns.

## Constants

| Name | Value | Why |
|---|---|---|
| re-arm level | `baseline + TAP_DELTA / 2` | standard half-threshold hysteresis |
| `CHATTER_GAP_MS` | 400 | machine pace; gesture taps land ≥ ~450 ms apart |
| `CHATTER_STREAK` | 8 | ~2 s of hum; beyond any human burst |
| `STUCK_MUTE_MS` | 2000 | no real ring survives 2 s above half-threshold |
| `UNMUTE_QUIET_MS` | 5000 | long enough that a fault must actually be gone |

All in `octagon_core.cpp`; none are game-visible.
