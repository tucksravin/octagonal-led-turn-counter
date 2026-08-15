#!/usr/bin/env python3
"""Prove the board is alive without reflashing it — run via `make ping`.

Pulses the CP2102 auto-reset line (RTS -> EN) and tails serial for a few
seconds. The ESP32-S3 ROM prints its boot banner at 115200 before any sketch
runs, so a reply proves the USB link, the bridge and the chip itself —
independent of whatever firmware happens to be loaded. Whatever the sketch
prints on boot (hello_board's chip dump, turn_counter's side LED table) comes
through right behind it.

Unlike `make monitor` this holds the port only for the read window, so it
frees the port for the next upload instead of blocking it.
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial not installed — run: .venv/bin/python3 -m pip install -r requirements.txt")

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", required=True, help="serial device, e.g. /dev/cu.usbserial-1130")
parser.add_argument("--baud", type=int, default=115200)
parser.add_argument("--seconds", type=float, default=5.0, help="how long to listen after reset")
args = parser.parse_args()

ser = serial.Serial()
ser.port = args.port
ser.baudrate = args.baud
ser.timeout = 0.2
# Stage the handshake lines before open(): pyserial applies them as the port
# comes up, so opening doesn't fire its own uncontrolled reset.
ser.dtr = False
ser.rts = False

try:
    ser.open()
except serial.SerialException as err:
    sys.exit(f"Could not open {args.port}: {err}\n"
             "If a `make monitor` is running in another terminal, quit it first (Ctrl+C).")

print(f"Resetting {args.port} at {args.baud} baud, listening {args.seconds:g}s…\n")
ser.reset_input_buffer()
# Hard reset only: RTS drives EN low. DTR stays deasserted so GPIO0 stays high
# and the chip boots the application instead of the ROM download stub.
ser.rts = True
time.sleep(0.1)
ser.rts = False

lines = 0
deadline = time.monotonic() + args.seconds
while time.monotonic() < deadline:
    chunk = ser.readline()
    if chunk:
        lines += 1
        print("  " + chunk.decode("utf-8", "replace").rstrip())
ser.close()

if not lines:
    sys.exit(f"\n{args.port}: silent for {args.seconds:g}s. The port enumerated but the chip "
             "said nothing — check the cable is on the UART port (not the native-USB one), "
             "then try `make flash-hello`.")
print(f"\n{args.port}: {lines} line(s) back — board is connected and running.")
