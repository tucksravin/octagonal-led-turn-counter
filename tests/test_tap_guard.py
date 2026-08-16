"""Host model of octagon_core's tap guard (hysteresis + duty-cycle quarantine).

Ports readPiezos/commitFire faithfully and drives them with synthetic signals at
the 5 ms scan cadence. The guard's discriminator is loudness DUTY, not fire
rate: a tap is loud ~30 ms out of every few hundred, a fault is loud half the
time or more. Keep the constants and structure in sync with octagon_core.cpp.

Timing rules of thumb (tau = 1 s EMA): a pinned input crosses the 38%% mute
mark in ~0.5 s; 50%%-duty hum in ~1.4 s; decay from hum level back to the 5%%
unmute mark takes ~2.3 s after the fault stops.
"""

import pytest

NUM_SIDES = 8
TAP_DELTA = 720
DEBOUNCE_MS = 250
DUTY_TAU_SCANS = 200
MUTE_DUTY_HI = 96          # ~38% loud
MUTE_DUTY_LO = 13          # ~5% loud
SCAN_MS = 5
BASE = 12                  # a typical healthy resting level; held constant here

HOT = BASE + TAP_DELTA + 200      # a real tap: comfortably above threshold
RAIL = 4095                       # a floating input: pegged at the 12-bit rail,
                                  # which is why it outranks every real tap
QUIET = BASE                      # comfortably below the re-arm level


class TapGuard:
    """The scanner's guard state, one instance per simulated board."""

    def __init__(self):
        self.armed = [True] * NUM_SIDES
        self.duty_acc = [0] * NUM_SIDES     # 8.8 fixed point, max 255<<8
        self.muted = [False] * NUM_SIDES
        self.sticky = [False] * NUM_SIDES
        self.last_tap = [0] * NUM_SIDES     # lastTapPerSide; 0 = never
        self.last_any_tap = 0               # lastTapMs — moves on ACCEPTED fires only
        self.mute_events = []               # (now, side)
        self.unmute_events = []

    def mute_side(self, i):
        """octagon_core's muteSide(): game-declared, sticky until power cycle."""
        self.muted[i] = True
        self.sticky[i] = True
        self.duty_acc[i] = 255 << 8

    def commit_fire(self, i, now):
        if now - self.last_tap[i] <= DEBOUNCE_MS:
            return False
        self.armed[i] = False
        self.last_tap[i] = now
        self.last_any_tap = now
        return True

    def scan(self, now, readings):
        """One readPiezos pass. Returns the accepted-fire bitmask."""
        hot_mask = 0
        max_idx, max_delta = None, 0
        for i in range(NUM_SIDES):
            r = readings[i]
            hot = r > BASE + TAP_DELTA
            loud = r >= BASE + TAP_DELTA // 2
            if not loud:
                self.armed[i] = True
            target = (255 << 8) if loud else 0
            # C integer division truncates toward zero
            self.duty_acc[i] += int((target - self.duty_acc[i]) / DUTY_TAU_SCANS)
            duty = self.duty_acc[i] >> 8
            if self.muted[i]:
                if not self.sticky[i] and duty <= MUTE_DUTY_LO:
                    self.muted[i] = False
                    self.unmute_events.append((now, i))
                continue
            if duty >= MUTE_DUTY_HI:
                self.muted[i] = True
                self.mute_events.append((now, i))
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

def test_wake_plus_setup_burst_all_eight_land():
    """The v1 regression: waking a dark table (4 taps) then opening setup
    (4 more) at an eager 300 ms cadence is ONE 8-tap burst. Every tap must
    fire and nothing may mute — tap duty is ~10%, far under the mark."""
    g = TapGuard()
    taps = [1500 + 300 * k for k in range(8)]
    fires = run(g, {5: pulses(taps)}, 5000)
    assert [s for _, s in fires] == [5] * 8
    assert not g.mute_events

def test_metronomic_taps_are_indistinguishable_and_accepted():
    """A slow tap-like phantom (450 ms period, tap-width crests) IS a patient
    player as far as any signal statistic goes — the scanner must accept it and
    never mute. Catching it is the game layer's job (muteSide after a setup
    session runs past human pace)."""
    g = TapGuard()
    taps = [1500 + 450 * k for k in range(20)]
    fires = run(g, {3: pulses(taps)}, 11000)
    assert len([f for f in fires if f[1] == 3]) == 20
    assert not g.mute_events

def test_same_scan_ghost_loses():
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
    assert not g.mute_events               # a 400 ms ring stays under the duty mark


# --- fault modes ------------------------------------------------------------

def test_long_ring_fires_once_and_recovers():
    """900 ms pegged above threshold = ONE tap (hysteresis). It also crosses
    the duty mark mid-ring — a brief mute that self-clears a few seconds after
    the ring ends, and the seat taps normally again."""
    g = TapGuard()
    sig = {7: (lambda t: HOT if (1500 <= t < 2400 or 6500 <= t < 6530) else QUIET)}
    fires = run(g, sig, 7000)
    assert (1500, 7) in fires
    assert len([f for f in fires if 1500 <= f[0] < 6000]) == 1   # the ring = one tap
    assert g.unmute_events                                        # mute self-cleared...
    assert (6500, 7) in fires                                     # ...and the seat works

def test_pinned_input_mutes_fast_and_frees_the_table():
    g = TapGuard()
    sig = {7: pinned(1500), 2: pulse(3000)}
    fires = run(g, sig, 4000)
    assert len([f for f in fires if f[1] == 7]) == 1
    assert g.mute_events and g.mute_events[0][0] <= 1500 + 700    # ~0.5 s to mute
    assert (3000, 2) in fires              # the table works after the mute
    assert g.muted[7]                      # pinned never calms: stays muted

def test_pinned_shadow_cost_is_bounded():
    """Before the mute lands (~0.5 s) a pinned side still eats other seats'
    taps — the documented cost of keeping the ghost filter."""
    g = TapGuard()
    sig = {7: pinned(1500), 2: pulse(1800)}
    fires = run(g, sig, 3000)
    assert (1800, 2) not in fires          # eaten during the shadow window
    assert g.mute_events                   # but the fault was declared

def test_hum_mutes_then_recovers_after_it_stops():
    g = TapGuard()
    fires = run(g, {6: hum(1500, until=5000)}, 10000)
    assert len([f for f in fires if f[1] == 6]) <= 7
    assert g.mute_events
    assert g.mute_events[0][0] - 1500 <= 1800          # ~1.4 s to mute at 50% duty
    assert g.unmute_events                             # hum ended at 5 s...
    assert g.unmute_events[0][0] - 5000 <= 3000        # ...decay to 5% takes ~2.3 s

def test_test_taps_cannot_hold_a_mute():
    """The v1 regression: a player poking a muted seat once a second must not
    keep it muted — 1 Hz taps are ~3% duty, below the unmute mark."""
    g = TapGuard()
    sig = {6: (lambda t: hum(1500, until=4000)(t) if t < 4000
               else (HOT if (t - 4000) % 1000 < 30 else QUIET))}
    run(g, sig, 12000)
    assert g.mute_events
    assert g.unmute_events                 # unmuted despite the ongoing test taps
    assert not g.muted[6]

def test_game_declared_mute_is_sticky():
    """muteSide() (the setup-session failsafe) survives any amount of quiet —
    behavioral evidence, so only a power cycle retries the channel."""
    g = TapGuard()
    g.mute_side(3)
    fires = run(g, {3: pulse(30000)}, 40000)
    assert not fires
    assert not g.unmute_events
    assert g.muted[3]

def test_accepted_fires_move_the_global_clock():
    """lastAnyTapMs (the setup/idle clock) must move only on accepted fires —
    a muted side's activity must not refresh it."""
    g = TapGuard()
    run(g, {6: hum(1500)}, 4000)
    assert g.mute_events
    clock_at_mute = g.last_any_tap
    run(g, {6: hum(0)}, 5000, start_ms=5000)   # hum continues; side is muted
    assert g.last_any_tap == clock_at_mute
