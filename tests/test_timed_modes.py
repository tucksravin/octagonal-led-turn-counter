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
    """renderTimer(): LEDs still lit. 0 means the expired branch was taken.
    While draining, the last LED holds — plain truncation zeroed the bar for the
    final total/len ms of every turn, reading as a dead table before the pulse."""
    if elapsed_ms >= total_ms:
        return 0
    return max(1, side_len * (total_ms - elapsed_ms) // total_ms)


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


def test_timer_never_fully_dark_before_expiry():
    """The regression that shipped: truncation gave 0 LEDs for the last ~2.2 s
    of a 60 s turn on a 27-LED side, indistinguishable from the table being off."""
    total = 60_000
    assert all(timer_leds(SIDE_LEN, e, total) >= 1 for e in range(0, total, 100))
    assert timer_leds(SIDE_LEN, total - 1, total) == 1


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
