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

    `sides` is a list of (pin, baseline, leds, last_tap_ms[, muted]) tuples. A
    last_tap_ms of 0 means "never tapped" and must serialise as -1, not as
    `uptime - 0`.
    """
    parts = [
        '{"tapDelta":%d,"uptimeMs":%d,"freeHeap":%d,"rssi":%d,"sides":['
        % (tap_delta, uptime_ms, free_heap, rssi)
    ]
    for i, side in enumerate(sides):
        pin, base, leds, last_tap = side[:4]
        muted = side[4] if len(side) > 4 else 0
        # Firmware clamps the gap to INT32_MAX before the (long) cast, so a
        # >24.8-day gap can't wrap %ld negative and masquerade as "never".
        since = -1 if last_tap == 0 else min(uptime_ms - last_tap, 2**31 - 1)
        parts.append(
            '%s{"pin":%d,"baseline":%d,"leds":%d,"sinceTapMs":%d,"muted":%d}'
            % ("," if i else "", pin, base, leds, since, muted)
        )
    parts.append("]}")
    return "".join(parts)


def worst_case_sides():
    """Widest plausible values: 2-digit pins, 4-digit baselines, 3-digit LED
    counts, a sinceTapMs at the far end of a 49-day millis() range, muted."""
    return [(10, 4095, 999, 1, 1) for _ in range(NUM_SIDES)]


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
