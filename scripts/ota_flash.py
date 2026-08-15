#!/usr/bin/env python3
"""Push a compiled turn_counter build to the table over Wi-Fi — run via `make ota`.

Wraps the ESP32 core's espota.py, which is what the Arduino IDE's "Network
Ports" upload uses under the hood. What this adds is the part that makes a
failed push diagnosable: it finds espota.py in whatever core version is
installed, reads the OTA password out of the gitignored secrets.h so it never
lands in a Makefile or shell history, and confirms the board is actually
answering on the OTA port before starting — the difference between a clear
"can't reach it" and a silent 60-second hang.

Recovery: if a push leaves the board unreachable (bad credentials, a crash
before Wi-Fi comes up), fall back to USB with `make flash-turn`.
"""
import argparse
import re
import socket
import subprocess
import sys
from pathlib import Path

OTA_PORT = 3232
ARDUINO_PACKAGES = Path.home() / "Library" / "Arduino15" / "packages"
SECRETS = Path("firmware/turn_counter/secrets.h")

DEFINE_RE = re.compile(r'^\s*#define\s+(\w+)\s+"([^"]*)"', re.M)


def parse_secrets(text):
    """Pull `#define NAME "value"` pairs out of a secrets.h into a dict."""
    return {m.group(1): m.group(2) for m in DEFINE_RE.finditer(text)}


def find_espota(packages_root):
    """Newest installed ESP32 platform's espota.py.

    Sorted by version tuple, not lexically — "3.10.0" must beat "3.9.0".
    """
    found = list(Path(packages_root).glob("esp32/hardware/esp32/*/tools/espota.py"))
    if not found:
        raise FileNotFoundError(
            f"No espota.py under {packages_root}/esp32/hardware/esp32/*/tools/.\n"
            "Install the ESP32 core first: arduino-cli core install esp32:esp32"
        )

    def version_key(path):
        return tuple(int(p) if p.isdigit() else -1
                     for p in path.parent.parent.name.split("."))

    return sorted(found, key=version_key)[-1]


def port_open(host, port, timeout=3.0):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bin", required=True, help="compiled .bin to push")
    parser.add_argument("--host", help="board address; default: <OTA_HOSTNAME>.local from secrets.h")
    parser.add_argument("--secrets", default=str(SECRETS), help="path to secrets.h")
    args = parser.parse_args()

    binary = Path(args.bin)
    if not binary.is_file():
        sys.exit(f"No such build: {binary}\nRun `make ota`, which compiles before pushing.")

    secrets_path = Path(args.secrets)
    if not secrets_path.is_file():
        sys.exit(f"No {secrets_path} — copy firmware/turn_counter/secrets.example.h to it "
                 "and fill in your Wi-Fi and OTA details.\n"
                 "Without it the firmware runs with the radio off, so there's nothing to push to.")

    secrets = parse_secrets(secrets_path.read_text())
    password = secrets.get("OTA_PASSWORD", "")
    host = args.host or f"{secrets.get('OTA_HOSTNAME', 'turn-counter')}.local"

    try:
        espota = find_espota(ARDUINO_PACKAGES)
    except FileNotFoundError as err:
        sys.exit(str(err))

    try:
        address = socket.gethostbyname(host)
    except socket.gaierror:
        sys.exit(f"Can't resolve {host}.\n"
                 "mDNS may not be working across your network — find the board's IP in the "
                 "boot serial output (`make ping`) and pass it: make ota HOST=192.168.x.x")

    print(f"{host} -> {address}, checking OTA port {OTA_PORT}…")
    if not port_open(address, OTA_PORT):
        sys.exit(f"{address}:{OTA_PORT} is not answering.\n"
                 "The board resolved but isn't listening for OTA. Most likely it never joined "
                 "Wi-Fi (check `make ping` for the boot output), or it's on a different subnet.\n"
                 "Fall back to USB with `make flash-turn`.")

    cmd = [sys.executable, str(espota),
           "-i", address, "-p", str(OTA_PORT),
           "-a", password, "-f", str(binary), "-r", "-d"]
    print(f"Pushing {binary.name} ({binary.stat().st_size // 1024} KB)…\n")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        sys.exit(f"\nOTA upload failed (exit {result.returncode}). "
                 "A wrong OTA_PASSWORD shows up as an auth failure above; a mid-transfer drop "
                 "is safe to retry — the board keeps running the old firmware until a push "
                 "completes. USB fallback: make flash-turn")
    print("\nPushed. The board reboots into the new firmware on its own.")


if __name__ == "__main__":
    main()
