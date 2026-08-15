#!/usr/bin/env python3
"""Record the piezo_stream firmware's CSV and report per-side sensitivity.

Workflow: flash the streaming sketch (`make flash-stream`), then `make record`
and tap each side ~10 times at game strength. Ctrl+C ends the session, the
raw stream lands in data/piezo/, and a report follows: per-side baseline,
idle noise ceiling, tap peak range, worst cross-talk, and how many of your
taps would have cleared the game's TAP_DELTA. Re-crunch an old recording
with --analyze <file> (no board needed).

The port is opened without touching DTR/RTS, so the board keeps streaming
through its existing baselines instead of rebooting. Like ping_board.py this
holds the port only while recording — quit any `make monitor` first.

Each data row is "board_ms,p0..p7" where pN is that side's PEAK raw ADC
reading over a 10 ms window (see piezo_stream.ino for why peak-hold). A tap
therefore shows up at full amplitude no matter how brief the spike.
"""
import argparse
import datetime
import glob
import re
import statistics
import sys
import time
from pathlib import Path

NUM_SIDES = 8
DATA_RE = re.compile(r"^(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)$")
EVENT_GAP_MS = 150    # rows closer together than this are one tap ringing down
EVENT_PAD_MS = 200    # rows this close to a tap are excluded from idle stats
GAME_TAP_DELTA = 720  # keep in sync with octagon_core.cpp

parser = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--port", help="serial device; default: first /dev/cu.usbserial-*")
parser.add_argument("--baud", type=int, default=115200)
parser.add_argument("--seconds", type=float, default=0,
                    help="stop after this long; 0 = record until Ctrl+C (default)")
parser.add_argument("--out", help="output CSV; default data/piezo/<timestamp>.csv")
parser.add_argument("--label", default="", help="tag appended to the default filename")
parser.add_argument("--event-delta", type=int, default=150,
                    help="delta over baseline that counts as a tap (default 150)")
parser.add_argument("--analyze", metavar="FILE",
                    help="skip recording; re-run the report on an existing CSV")
args = parser.parse_args()


def autodetect_port():
    ports = sorted(glob.glob("/dev/cu.usbserial-*"))
    if not ports:
        sys.exit("No /dev/cu.usbserial-* port found — the usbmodem ports are the "
                 "LG monitor, not the board. Plug in the UART port or pass --port.")
    return ports[0]


def parse_row(line):
    m = DATA_RE.match(line)
    if not m:
        return None
    nums = [int(g) for g in m.groups()]
    return nums[0], nums[1:]


def record():
    try:
        import serial
    except ImportError:
        sys.exit("pyserial not installed — run: .venv/bin/python3 -m pip install -r requirements.txt")

    port = args.port or autodetect_port()
    if args.out:
        out_path = Path(args.out)
    else:
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        name = f"{stamp}-{args.label}.csv" if args.label else f"{stamp}.csv"
        out_path = Path("data/piezo") / name
    out_path.parent.mkdir(parents=True, exist_ok=True)

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = args.baud
    ser.timeout = 0.2
    # Stage the handshake lines before open() so the board doesn't reset —
    # same trick as ping_board.py, but here we *want* the stream mid-flow.
    ser.dtr = False
    ser.rts = False
    try:
        ser.open()
    except serial.SerialException as err:
        sys.exit(f"Could not open {port}: {err}\n"
                 "If a `make monitor` is running in another terminal, quit it first (Ctrl+C).")

    until = f"for {args.seconds:g}s" if args.seconds else "until Ctrl+C"
    print(f"Recording {port} @ {args.baud} -> {out_path}  ({until})")
    print("Tap each side ~10 times at game strength. Watch for tap lines below.\n")

    rows = []
    garbled = 0
    baselines = None
    event_peaks = None
    event_end_ms = 0
    tap_count = 0
    last_status = time.monotonic()
    start = time.monotonic()

    with open(out_path, "w") as out:
        out.write(f"# piezo_stream recording {datetime.datetime.now().isoformat(timespec='seconds')}\n")
        out.write(f"# port {port} @ {args.baud}; columns: board_ms, per-side peak ADC over 10 ms\n")
        ser.reset_input_buffer()
        try:
            while True:
                if args.seconds and time.monotonic() - start >= args.seconds:
                    break
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", "replace").strip()
                if not line:
                    continue
                parsed = parse_row(line)
                if parsed is None:
                    # Boot banner, side table, or a line clipped mid-stream.
                    out.write(f"# serial: {line}\n")
                    garbled += 1
                    continue
                rows.append(parsed)
                out.write(line + "\n")
                ms, vals = parsed

                # Seed baselines from the first ~2 s, then track slowly so the
                # live tap prints stay honest; the final report recomputes from
                # the whole file with proper event exclusion.
                if baselines is None:
                    if len(rows) >= 200:
                        cols = list(zip(*(v for _, v in rows)))
                        baselines = [statistics.median(c) for c in cols]
                        print(f"  baselines settled: "
                              + " ".join(f"s{i}:{int(b)}" for i, b in enumerate(baselines)))
                    continue

                deltas = [max(0, v - baselines[s]) for s, v in enumerate(vals)]
                if max(deltas) >= args.event_delta:
                    if event_peaks is None:
                        event_peaks = deltas[:]
                    else:
                        event_peaks = [max(a, b) for a, b in zip(event_peaks, deltas)]
                    event_end_ms = ms + EVENT_GAP_MS
                elif event_peaks is not None and ms > event_end_ms:
                    tap_count += 1
                    loudest = event_peaks.index(max(event_peaks))
                    others = "  ".join(f"s{s}:{int(d)}" for s, d in enumerate(event_peaks)
                                       if s != loudest and d >= args.event_delta / 2)
                    game = "GAME" if event_peaks[loudest] >= GAME_TAP_DELTA else "weak"
                    print(f"  tap #{tap_count:<3} side {loudest}  Δ{int(event_peaks[loudest]):<5} {game}"
                          + (f"   bleed: {others}" if others else ""))
                    event_peaks = None
                else:
                    for s, v in enumerate(vals):
                        baselines[s] += (v - baselines[s]) * 0.005  # ~2 s time constant

                if time.monotonic() - last_status >= 15:
                    last_status = time.monotonic()
                    print(f"  [{time.monotonic() - start:5.0f}s] {len(rows)} rows, {tap_count} taps so far")
        except KeyboardInterrupt:
            print()
        finally:
            ser.close()

    elapsed = time.monotonic() - start
    print(f"\nStopped after {elapsed:.0f}s: {len(rows)} rows, {garbled} non-data lines -> {out_path}")
    if not rows:
        sys.exit("No data rows at all — the board isn't streaming CSV. "
                 "Flash the streamer first: make flash-stream")
    return rows


def load(path):
    rows = []
    for line in Path(path).read_text().splitlines():
        parsed = parse_row(line.strip())
        if parsed:
            rows.append(parsed)
    if not rows:
        sys.exit(f"{path}: no data rows found.")
    return rows


def percentile(sorted_vals, p):
    return sorted_vals[min(len(sorted_vals) - 1, int(p * len(sorted_vals)))]


def analyze(rows):
    if len(rows) < 500:
        print(f"(only {len(rows)} rows ≈ {len(rows) // 100}s — stats will be rough)")

    cols = list(zip(*(vals for _, vals in rows)))
    base = [int(statistics.median(c)) for c in cols]
    deltas = [[max(0, v - base[s]) for s, v in enumerate(vals)] for _, vals in rows]

    # Group over-threshold rows into tap events, bridging ring-down gaps.
    events = []  # (start_ms, end_ms, per-side peak delta)
    cur = None
    for (ms, _), d in zip(rows, deltas):
        if max(d) >= args.event_delta:
            if cur and ms - cur[1] <= EVENT_GAP_MS:
                cur[1] = ms
                cur[2] = [max(a, b) for a, b in zip(cur[2], d)]
            else:
                if cur:
                    events.append(tuple(cur))
                cur = [ms, ms, d[:]]
    if cur:
        events.append(tuple(cur))

    # Idle rows: everything comfortably clear of any event.
    spans = [(s - EVENT_PAD_MS, e + EVENT_PAD_MS) for s, e, _ in events]
    idle = [d for (ms, _), d in zip(rows, deltas)
            if not any(s <= ms <= e for s, e in spans)]

    print(f"\n{len(events)} taps across {len(rows)} rows "
          f"({rows[-1][0] - rows[0][0]} ms of board time), {len(idle)} idle rows\n")
    hdr = (f"{'side':>4} {'baseline':>8} {'idle p99':>9} {'idle max':>9} "
           f"{'taps':>5} {'tap Δ min/med/max':>18} {'x-talk':>7} {'≥game':>6} {'suggest':>8}")
    print(hdr)
    print("-" * len(hdr))

    for s in range(NUM_SIDES):
        idle_d = sorted(d[s] for d in idle) or [0]
        own = sorted(e[2][s] for e in events if max(e[2]) == e[2][s] and e[2][s] > 0)
        xtalk = max((e[2][s] for e in events if max(e[2]) != e[2][s]), default=0)
        floor = max(idle_d[-1], xtalk)

        if own:
            med = own[len(own) // 2]
            taps_col = f"{own[0]:>5}/{med:>4}/{own[-1]:>5}"
            game_col = f"{sum(1 for p in own if p >= GAME_TAP_DELTA)}/{len(own)}"
            # Geometric mean sits proportionally between the junk ceiling and
            # the weakest real tap; degenerate spacing gets flagged below.
            suggest = int(round((max(floor, 1) * own[0]) ** 0.5 / 25) * 25)
            flag = "  <- no margin" if own[0] <= floor * 1.5 else ""
        else:
            taps_col, game_col, suggest, flag = f"{'-':>16}", "-", "-", "  <- never loudest"

        print(f"{s:>4} {int(base[s]):>8} {percentile(idle_d, 0.99):>9} {idle_d[-1]:>9} "
              f"{len(own):>5} {taps_col:>18} {xtalk:>7} {game_col:>6} {suggest:>8}{flag}")

    print(f"\n'≥game' = taps clearing the firmware's TAP_DELTA ({GAME_TAP_DELTA}). "
          "'suggest' splits the gap between\nthat side's noise/cross-talk ceiling and its "
          "weakest real tap — the flags matter more\nthan the numbers: 'no margin' means "
          "reglue or re-seat that sensor before tuning thresholds.")


if args.analyze:
    analyze(load(args.analyze))
else:
    analyze(record())
