# Octagonal Gaming Table Turn Counter

LED rim turn counter for a removable octagonal gaming table that sits on top of a bumper pool table. Players tap the lit section in front of them to pass turn; the lit zone advances clockwise to the next player.

**Start with [`design_doc_simple.md`](design_doc_simple.md)** — the current default build: USB-powered, everything on the lid, no mains wiring. The full mains-powered / permanent-install version — complete BOM, wiring schematics, mechanical drawings, and phased checklist — is in `turn_counter_design_doc.pdf` (source: `turn_counter_design_doc.md`), kept as the advanced reference.

## Repo layout

```text
.
├── README.md
├── design_doc_simple.md          ← the current default build — start here
├── turn_counter_design_doc.pdf   ← advanced / permanent-install reference (mains PSU, disconnect)
├── turn_counter_design_doc.md    ← source for the PDF above
├── dry_run.pdf                   ← Phase −1 bench checklist, print this for the bench
├── dry_run.md                    ← edit this to change the dry-run checklist
├── shopping_list.md              ← concrete parts list with PNs and vendors
├── inventory.md                  ← what was bought + bulk-pack surplus for future projects
├── requirements.txt              ← Python deps for the PDF build scripts
├── Makefile                      ← bench shortcuts: make flash-tap, make monitor, …
├── firmware/                     ← one sketch folder per firmware (arduino-cli/IDE layout)
│   ├── turn_counter/
│   │   └── turn_counter.ino      ← main project firmware
│   ├── tap_light/
│   │   └── tap_light.ino         ← Phase 0 starter (tap-activated desk light, for skill-building)
│   ├── hello_board/
│   │   └── hello_board.ino       ← board-connection smoke test (serial heartbeat + onboard RGB)
│   └── strip_test/
│       └── strip_test.ino        ← minimal WS2812B smoke test (onboard RGB pixel or short strip)
└── doc-src/
    ├── build_pdf.py              ← design-doc PDF build script
    ├── build_dry_run_pdf.py      ← dry-run PDF build script
    ├── doc_style.py              ← shared stylesheet for both PDFs
    ├── table_layout.svg          ← Figure 0.1 (top-down view)
    ├── breadboard_layout.svg     ← Phase 2 bench-prototype breadboard placement (bench printout)
    ├── rim_section.svg           ← Figure 4.1 (edge cross-section)
    ├── installation_arch.svg     ← Figure 4.6 (slab/frame architecture)
    ├── turn_counter_wiring.svg   ← Figure 3.1 (wiring schematic, also good as a bench printout)
    ├── protoboard_layout.svg     ← Phase 5 control-box protoboard placement (bench printout)
    └── protoboard_wiring.svg     ← Phase 5 point-to-point wiring + numbered wire list (bench printout)
```

## Firmware

### Arduino IDE

Open the `.ino` files in Arduino IDE.

- Board: **ESP32S3 Dev Module** (Tools → Board → ESP32 Arduino → ESP32S3 Dev Module), upload speed 921600
- Tools → **USB CDC On Boot: Enabled** (so `Serial.println` reaches the IDE over the native USB port)
- Required libraries: FastLED, ArduinoOTA (ArduinoOTA bundles with the ESP32 core)
- The ESP32-S3 enumerates over native USB — no CP210x/CH340 driver needed
- Wi-Fi credentials, mDNS hostname and OTA password live in `firmware/turn_counter/secrets.h` — copy `secrets.example.h` to it and fill it in. That file is gitignored; without it the sketch still builds and just runs with the radio off

### arduino-cli (no IDE needed)

`arduino-cli` reuses the IDE's installed cores and libraries (macOS: `~/Library/Arduino15` and `~/Documents/Arduino/libraries`), so there is no separate setup and it produces the same binaries.

```bash
brew install arduino-cli    # macOS; other platforms: arduino.github.io/arduino-cli

arduino-cli compile --fqbn esp32:esp32:esp32s3 firmware/strip_test
arduino-cli upload  -p /dev/cu.usbserial-1130 --fqbn esp32:esp32:esp32s3 firmware/strip_test
arduino-cli monitor -p /dev/cu.usbserial-1130 -c baudrate=115200
```

- Find your port with `arduino-cli board list` or `ls /dev/cu.*` (the port number varies with USB topology — `-1130`, `-140`, etc.). The board has two USB ports: the **UART** port (CP2102 bridge) enumerates as `/dev/cu.usbserial-*` and is the one to flash through; the board's **native-USB** port would show up as `/dev/cu.usbmodem*` — but so do unrelated USB devices (hubs, monitors), which is why the `usbserial-*` entry is the unambiguous choice on a busy Mac.
- `turn_counter` overflows the default 1.25 MB app partition (Wi-Fi + OTA + mDNS), so plain `esp32:esp32:esp32s3` fails to link with "text section exceeds available space". Compile **and upload** it with `--fqbn esp32:esp32:esp32s3:PartitionScheme=min_spiffs` (1.9 MB app slots, keeps the OTA partition). `tap_light` and `strip_test` fit the default scheme fine (~52%).
- Default board options leave **USB CDC On Boot disabled**, so `Serial` output arrives on the same `usbserial-*` UART port you flash through — no IDE setting to remember. To instead match the IDE configuration above (serial over the native-USB port), compile with `--fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc`.
- The CLI builds *sketch folders*, not bare `.ino` files — every sketch under `firmware/` has its own folder.

### Make shortcuts

The `Makefile` wraps the commands above with the port auto-detected from `/dev/cu.usbserial-*` (override with `make <target> PORT=/dev/cu.usbserial-XXXX`):

```bash
make flash-hello    # compile + upload the connection smoke test
make flash-strip    # compile + upload the WS2812B strip test
make flash-tap      # compile + upload the Phase 0 tap light
make flash-turn     # compile + upload turn_counter (applies the min_spiffs partition)
make ota            # compile turn_counter and push it over Wi-Fi instead of USB
make map-piezos     # reassign piezos to sides by tapping each lit side
make monitor        # serial monitor at 115200 (Ctrl+C to quit — frees the port for uploads)
make compile-all    # build everything without touching the board (verbose + all warnings)
make test           # run the host-script test suite (no board needed)
make ports          # list connected boards
make pdf            # rebuild all doc PDFs (design, simple, dry-run, tap-light, bench guide)
make vscode         # regenerate .vscode IntelliSense config from the installed toolchain
make upgrade        # arduino-cli core+lib upgrade, then vscode regen + compile-all
```

Only one program can hold the serial port — quit `make monitor` before any `flash-*` target.

**If a tap lights the wrong seat**, run `make map-piezos`. Each side lights white in turn; tap that seat. The corrected map is stored on the board (NVS), so it survives reboots and OTA and needs no reflash — no wire tracing either.

**`make ota`** pushes over Wi-Fi instead of USB. It needs `firmware/turn_counter/secrets.h`, and the board must already be running firmware that joined the network — so the first flash after setting credentials is always a USB one. Use `make ota HOST=192.168.x.x` if mDNS won't resolve `turn-counter.local`.

## Rebuilding the PDF

### Prerequisites

- **Python 3.9 or newer**
- **WeasyPrint native libraries** (Pango, Cairo, GDK-PixBuf, libffi, HarfBuzz). Install varies by platform:
  - **macOS**: `brew install pango python` (Cairo and the rest come along as dependencies). On Apple Silicon, **use Homebrew's Python** (`/opt/homebrew/bin/python3`) to create the venv below — Apple's stock `/usr/bin/python3` can't find Homebrew's libraries at runtime and fails with `OSError: cannot load library 'libgobject-2.0-0'`. As a fallback if you must use a non-Homebrew Python, run with `DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib python3 doc-src/build_pdf.py`.
  - **Debian / Ubuntu**: `sudo apt install libpango-1.0-0 libpangoft2-1.0-0`
  - **Windows**: install the GTK3 runtime — follow [WeasyPrint's Windows guide](https://doc.courtbouillon.org/weasyprint/stable/first_steps.html#windows)
  - Other platforms / troubleshooting: [WeasyPrint first steps](https://doc.courtbouillon.org/weasyprint/stable/first_steps.html)

### Build

From the repo root, in a virtual environment (required on systems with PEP 668, including recent Homebrew Python on macOS and Debian/Ubuntu):

```bash
python3 -m venv .venv
source .venv/bin/activate          # Windows PowerShell: .venv\Scripts\Activate.ps1
python3 -m pip install -r requirements.txt
python3 doc-src/build_pdf.py            # design doc  → turn_counter_design_doc.pdf
python3 doc-src/build_dry_run_pdf.py    # dry run     → dry_run.pdf
```

> Windows users: substitute `py` for `python3` in the commands above.

`build_pdf.py` reads `turn_counter_design_doc.md` from the root and the SVGs from `doc-src/`, then writes `turn_counter_design_doc.pdf` back to the root. `build_dry_run_pdf.py` reads `dry_run.md` and writes `dry_run.pdf`. Both share the stylesheet in `doc-src/doc_style.py`, so the two PDFs stay visually consistent — edit styling there once.

## Editing the doc

The markdown supports tables, fenced code, definition lists, and HTML attribute lists. SVGs are read fresh on each build (no cache). Rebuild before committing.
