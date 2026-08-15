#!/usr/bin/env python3
"""Reassign piezos to sides by tapping — run via `make map-piezos`.

The board does the work: it lights one side white at a time and records which
piezo answers, so a crossed harness gets fixed without anyone tracing wires.
This script just drives that conversation — it opens the port, sends `m`, turns
the board's MAP lines into readable prompts, and prints the finished map plus a
paste-ready source line for the record.

The map is saved to NVS on the board, so it survives reboots and OTA pushes and
no reflash is needed. Like ping_board.py this opens the port without touching
DTR/RTS, so the board keeps its baselines instead of rebooting — quit any
`make monitor` first, or the port will be busy.
"""
import argparse
import glob
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial not installed — run: .venv/bin/python3 -m pip install -r requirements.txt")

NUM_SIDES = 8
SESSION_TIMEOUT_S = 300  # whole-session ceiling; the board's own per-prompt timeout is 20 s

RE_PROMPT = re.compile(r"^MAP side (\d+) - tap the lit seat$")
RE_ASSIGN = re.compile(r"^MAP side (\d+) = GPIO (\d+)$")
RE_DUP = re.compile(r"^MAP DUP - GPIO (\d+) already mapped to side (\d+), retry$")
RE_SOURCE = re.compile(r"^uint8_t sidePiezoPin\[NUM_SIDES\] = \{([^}]*)\};$")


def classify(line):
    """Turn one line from the board into (kind, payload).

    kind is one of prompt / assign / dup / done / timeout / abort / source /
    other. Everything the board prints that isn't wizard protocol — gameplay
    chatter, the boot banner — falls through to "other", so it can be shown or
    ignored without being mistaken for a result.
    """
    line = line.strip()

    m = RE_PROMPT.match(line)
    if m:
        return ("prompt", {"side": int(m.group(1))})

    m = RE_ASSIGN.match(line)
    if m:
        return ("assign", {"side": int(m.group(1)), "gpio": int(m.group(2))})

    m = RE_DUP.match(line)
    if m:
        return ("dup", {"gpio": int(m.group(1)), "owner": int(m.group(2))})

    m = RE_SOURCE.match(line)
    if m:
        return ("source", {"line": line, "pins": [int(p) for p in m.group(1).split(",")]})

    if line.startswith("MAP DONE"):
        return ("done", {})
    if line.startswith("MAP TIMEOUT"):
        return ("timeout", {})
    if line.startswith("MAP ABORT"):
        return ("abort", {})

    return ("other", {"line": line})


def render(kind, payload):
    """The operator-facing line for a protocol event, or None to stay quiet."""
    if kind == "prompt":
        return f"\n  Side {payload['side']}: that side is lit WHITE — tap that seat."
    if kind == "assign":
        return f"    ok  side {payload['side']} -> GPIO {payload['gpio']}"
    if kind == "dup":
        return (f"    !!  GPIO {payload['gpio']} is already side {payload['owner']} — "
                "you tapped a seat that's already mapped. Tap the seat that's lit.")
    if kind == "timeout":
        return "\n  Timed out waiting for a tap. Nothing was saved."
    if kind == "abort":
        return "\n  Aborted. Nothing was saved."
    return None


def autodetect_port():
    ports = sorted(glob.glob("/dev/cu.usbserial-*"))
    if not ports:
        sys.exit("No /dev/cu.usbserial-* port found — the usbmodem ports are the "
                 "LG monitor, not the board. Plug in the UART port or pass --port.")
    return ports[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial device; default: first /dev/cu.usbserial-*")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    port = args.port or autodetect_port()

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = args.baud
    ser.timeout = 0.2
    # Staged before open() so bringing the port up doesn't reset the board and
    # throw away the piezo baselines the wizard is about to use.
    ser.dtr = False
    ser.rts = False

    try:
        ser.open()
    except serial.SerialException as err:
        sys.exit(f"Could not open {port}: {err}\n"
                 "If a `make monitor` is running in another terminal, quit it first (Ctrl+C).")

    print(f"Piezo remap on {port} @ {args.baud}. Tap the seat that lights up, one at a time.")
    print("Ctrl+C to give up (nothing is saved until all 8 sides are mapped).\n")

    ser.reset_input_buffer()
    ser.write(b"m\n")

    assigned = {}
    source_line = None
    outcome = "interrupted"
    deadline = time.monotonic() + SESSION_TIMEOUT_S

    try:
        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            kind, payload = classify(raw.decode("utf-8", "replace"))

            if kind == "assign":
                assigned[payload["side"]] = payload["gpio"]
            elif kind == "source":
                source_line = payload["line"]

            text = render(kind, payload)
            if text:
                print(text)

            if kind in ("done", "timeout", "abort"):
                outcome = kind
                break
        else:
            outcome = "session-timeout"
    except KeyboardInterrupt:
        print("\n  Interrupted.")
    finally:
        ser.close()

    if outcome != "done":
        sys.exit(f"\nRemap did not complete ({outcome}). The board kept its previous map.")

    print("\n  Saved to the board's NVS — survives reboots and OTA, no reflash needed.\n")
    for side in range(NUM_SIDES):
        print(f"    side {side}  ->  GPIO {assigned.get(side, '?')}")
    if source_line:
        print("\n  Record the as-built wiring in octagon_core.cpp:\n")
        print(f"    {source_line}")


if __name__ == "__main__":
    main()
