# Octagonal Gaming Table Turn Counter

LED rim turn counter for a removable octagonal gaming table that sits on top of a bumper pool table. Players tap the lit section in front of them to pass turn; the lit zone advances clockwise to the next player.

The full design — bill of materials, wiring schematics, mechanical drawings, build steps, and firmware reference — is in `turn_counter_design_doc.pdf`.

## Repo layout

```text
.
├── README.md
├── turn_counter_design_doc.pdf   ← read this first
├── turn_counter_design_doc.md    ← edit this to change content
├── firmware/
│   ├── turn_counter.ino          ← main project firmware
│   └── tap_light.ino             ← Phase 0 starter (tap-activated desk light, for skill-building)
└── doc-src/
    ├── build_pdf.py              ← PDF build script
    ├── table_layout.svg          ← Figure 0.1 (top-down view)
    ├── rim_section.svg           ← Figure 4.1 (edge cross-section)
    ├── installation_arch.svg     ← Figure 4.6 (slab/frame architecture)
    └── turn_counter_wiring.svg   ← Figure 3.1 (wiring schematic, also good as a bench printout)
```

## Firmware

Open the `.ino` files in Arduino IDE.

- Board: ESP32 Dev Module, upload speed 921600
- Required libraries: FastLED, ArduinoOTA (ArduinoOTA bundles with the ESP32 core)
- Edit Wi-Fi credentials, mDNS hostname, and OTA password at the top of `turn_counter.ino` before the first flash

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
python3 doc-src/build_pdf.py
```

> Windows users: substitute `py` for `python3` in the commands above.

The script reads `turn_counter_design_doc.md` from the root and SVGs from `doc-src/`, then writes `turn_counter_design_doc.pdf` back to the root.

## Editing the doc

The markdown supports tables, fenced code, definition lists, and HTML attribute lists. SVGs are read fresh on each build (no cache). Rebuild before committing.
