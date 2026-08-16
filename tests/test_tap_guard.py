"""Host model of octagon_core's tap guard (hysteresis + stuck-mute + quarantine).

Ports readPiezos/commitFire faithfully and drives them with synthetic signals at
the 5 ms scan cadence. Keep the constants and the winner/shadow/commit structure
in sync with octagon_core.cpp — these tests are the spec's fault-mode table made
executable.
"""

import pytest

NUM_SIDES = 8
TAP_DELTA = 720
DEBOUNCE_MS = 250
CHATTER_GAP_MS = 400
CHATTER_STREAK = 8
STUCK_MUTE_MS = 2000
UNMUTE_QUIET_MS = 5000
SCAN_MS = 5
BASE = 12          # a typical healthy resting level; the model holds it constant

HOT = BASE + TAP_DELTA + 200      # a real tap: comfortably above threshold
RAIL = 4095                       # a floating input: pegged at the 12-bit rail,
                                  # which is why it outranks every real tap
QUIET = BASE                      # comfortably below the re-arm level


class TapGuard:
    """The scanner's guard state, one instance per simulated board."""

    def __init__(self):
        self.armed = [True] * NUM_SIDES
        self.streak = [0] * NUM_SIDES
        self.muted = [False] * NUM_SIDES
        self.last_tap = [0] * NUM_SIDES     # lastTapPerSide; 0 = never
        self.last_loud = [0] * NUM_SIDES
        self.last_any_tap = 0               # lastTapMs — moves on ACCEPTED fires only
        self.mute_events = []               # (now, side, cause)
        self.unmute_events = []

    def commit_fire(self, i, now):
        gap = now - self.last_tap[i]
        if gap <= DEBOUNCE_MS:
            return False
        self.armed[i] = False
        self.streak[i] = self.streak[i] + 1 if (self.last_tap[i] != 0 and gap < CHATTER_GAP_MS) else 1
        self.last_tap[i] = now
        if self.streak[i] >= CHATTER_STREAK:
            self.muted[i] = True
            self.mute_events.append((now, i, "chatter"))
            return False
        self.last_any_tap = now
        return True

    def scan(self, now, readings):
        """One readPiezos pass. Returns the accepted-fire bitmask."""
        hot_mask = 0
        max_idx, max_delta = None, 0
        for i in range(NUM_SIDES):
            r = readings[i]
            hot = r > BASE + TAP_DELTA
            if r >= BASE + TAP_DELTA // 2:
                self.last_loud[i] = now
            else:
                self.armed[i] = True
            if self.muted[i]:
                if now - self.last_loud[i] >= UNMUTE_QUIET_MS:
                    self.muted[i] = False
                    self.streak[i] = 0
                    self.unmute_events.append((now, i))
                continue
            if not self.armed[i] and now - self.last_tap[i] > STUCK_MUTE_MS:
                self.muted[i] = True
                self.streak[i] = 0
                self.mute_events.append((now, i, "stuck"))
                continue
            if hot:
                hot_mask |= 1 << i
                delta = r - BASE
                if delta > max_delta:
                    max_delta, max_idx = delta, i
        if max_idx is None:
            return 0
        if not self.armed[max_idx]:
            return 0                        # ghost filter: a spent winner shadows the scan
        if not self.commit_fire(max_idx, now):
            return 0
        result = 1 << max_idx
        opp = (max_idx + NUM_SIDES // 2) % NUM_SIDES
        if (hot_mask >> opp) & 1 and self.armed[opp] and self.commit_fire(opp, now):
            result |= 1 << opp
        return result


def run(guard, signals, duration_ms, start_ms=1000):
    """Drive the guard; signals maps side -> f(t_ms) -> reading. Returns fires
    as a list of (t, side)."""
    fires = []
    for t in range(start_ms, start_ms + duration_ms, SCAN_MS):
        readings = [signals.get(i, lambda _: QUIET)(t) for i in range(NUM_SIDES)]
        mask = guard.scan(t, readings)
        for i in range(NUM_SIDES):
            if (mask >> i) & 1:
                fires.append((t, i))
    return fires


# --- signal generators ------------------------------------------------------

def pulse(at, width=30):
    """A clean tap: hot for `width` ms, quiet otherwise."""
    return lambda t: HOT if at <= t < at + width else QUIET

def pulses(times, width=30):
    return lambda t: HOT if any(a <= t < a + width for a in times) else QUIET

def sustained(at, width):
    """A ring pegged above threshold for `width` ms."""
    return lambda t: HOT if at <= t < at + width else QUIET

def pinned(at):
    """Floating input at the rail: pegged forever after `at`."""
    return lambda t: RAIL if t >= at else QUIET

def hum(at, until=None, period=17):
    """Mains pickup on a floating input: rail crest / quiet trough at ~60 Hz."""
    def f(t):
        if t < at or (until is not None and t >= until):
            return QUIET
        return RAIL if ((t - at) // period) % 2 == 0 else QUIET
    return f


# --- healthy play -----------------------------------------------------------

def test_clean_tap_fires_once():
    g = TapGuard()
    fires = run(g, {2: pulse(1500)}, 3000)
    assert fires == [(1500, 2)]
    assert not g.mute_events

def test_gesture_pace_never_mutes():
    """4 taps at 500 ms gaps — the setup gesture — must all land."""
    g = TapGuard()
    fires = run(g, {5: pulses([1500, 2000, 2500, 3000])}, 4000)
    assert [s for _, s in fires] == [5, 5, 5, 5]
    assert not g.mute_events

def test_same_scan_ghost_loses():
    """A smaller same-scan spike on a neighbour is cross-talk: only the real
    hit commits (unless it's the opposite side — the two-handed slap)."""
    g = TapGuard()
    sig = {3: pulse(1500), 4: (lambda t: BASE + TAP_DELTA + 50 if 1500 <= t < 1530 else QUIET)}
    fires = run(g, sig, 2000)
    assert fires == [(1500, 3)]

def test_next_scan_ghost_shadowed():
    """While the real winner is still hot and disarmed, a late ghost that now
    has the biggest delta must NOT commit — the spent winner rules the scan."""
    g = TapGuard()
    sig = {
        3: sustained(1500, 400),                                   # real hit, rings on
        4: (lambda t: BASE + TAP_DELTA + 100 if 1520 <= t < 1560 else QUIET),  # late ghost
    }
    fires = run(g, sig, 2500)
    assert fires == [(1500, 3)]

def test_accepted_fires_move_the_global_clock():
    """lastAnyTapMs (the setup/idle clock) must move only on accepted fires —
    a swallowed chatter fire must not refresh it."""
    g = TapGuard()
    run(g, {6: hum(1500)}, 4000)
    assert g.mute_events and g.mute_events[0][2] == "chatter"
    clock_at_mute = g.last_any_tap
    run(g, {6: hum(0)}, 4000, start_ms=5000)   # hum continues; side is muted
    assert g.last_any_tap == clock_at_mute


# --- fault modes ------------------------------------------------------------

def test_sustained_ring_fires_once():
    """The after-tap phantom case: 900 ms pegged above threshold = ONE tap."""
    g = TapGuard()
    fires = run(g, {7: sustained(1500, 900)}, 4000)
    assert fires == [(1500, 7)]
    assert not g.mute_events               # it re-armed on decay; no fault declared

def test_pinned_input_mutes_and_frees_the_table():
    g = TapGuard()
    sig = {7: pinned(1500), 2: pulse(6000)}
    fires = run(g, sig, 7000)
    assert (1500, 7) in fires and len([f for f in fires if f[1] == 7]) == 1
    assert g.mute_events and g.mute_events[0][2] == "stuck"
    assert g.mute_events[0][0] <= 1500 + STUCK_MUTE_MS + 3 * SCAN_MS
    assert (6000, 2) in fires              # the table works again after the mute
    assert g.muted[7]                      # pinned never goes quiet: stays muted

def test_pinned_shadow_cost_is_bounded():
    """During the pre-mute window a pinned side still eats other seats' taps —
    the documented, bounded cost of keeping the ghost filter."""
    g = TapGuard()
    sig = {7: pinned(1500), 2: pulse(2500)}
    fires = run(g, sig, 4000)
    assert (2500, 2) not in fires          # eaten during the 2 s shadow
    assert g.mute_events                   # but the fault was declared

def test_hum_mutes_within_bounds_then_recovers():
    g = TapGuard()
    fires = run(g, {6: hum(1500, until=4000)}, 12000)
    hum_fires = [f for f in fires if f[1] == 6]
    assert len(hum_fires) <= CHATTER_STREAK
    assert g.mute_events and g.mute_events[0][2] == "chatter"
    assert g.mute_events[0][0] - 1500 <= 2500          # muted within ~2.5 s
    assert g.unmute_events                             # hum ended at 4 s...
    assert abs(g.unmute_events[0][0] - (4000 + UNMUTE_QUIET_MS)) <= 30 + SCAN_MS

def test_drum_roll_mutes_then_self_heals():
    """8 human taps at 300 ms gaps trips the quarantine — annoying but visible,
    and the seat comes back on its own after 5 s of quiet."""
    g = TapGuard()
    taps = [1500 + 300 * k for k in range(10)]
    fires = run(g, {1: pulses(taps)}, 12000)
    assert g.mute_events and g.mute_events[0][2] == "chatter"
    assert len([f for f in fires if f[1] == 1]) < len(taps)
    assert g.unmute_events                 # quiet after the roll: seat restored
    assert not g.muted[1]
