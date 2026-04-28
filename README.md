# Octagonal Gaming Table Turn Counter

LED rim turn counter for a removable octagonal gaming table that sits on top of a bumper pool table. Players tap the lit section in front of them to pass turn; the lit zone advances clockwise to the next player.

The full design — bill of materials, wiring schematics, mechanical drawings, build steps, and firmware reference — is in `turn_counter_design_doc.pdf`.

## Files in this folder

### Read this first

- **`turn_counter_design_doc.pdf`** — the complete design and build document. Start here.

### Source files for the design doc

These are the source files used to generate the PDF. Only matters if you want to edit the doc.

- **`turn_counter_design_doc.md`** — markdown source for the document body
- **`build_pdf.py`** — Python build script that renders the PDF
- **`table_layout.svg`** — top-down view (embedded as Figure 0.1)
- **`rim_section.svg`** — edge cross-section (embedded as Figure 4.1)
- **`installation_arch.svg`** — slab/frame architecture (embedded as Figure 4.6)
- **`turn_counter_wiring.svg`** — full wiring schematic (embedded as Figure 3.1, also worth printing standalone for use at the bench)

### Firmware

These are the Arduino sketches you'll flash to the ESP32. Open them in the Arduino IDE.

- **`turn_counter.ino`** — main project firmware
- **`tap_light.ino`** — Phase 0 starter project firmware (a small standalone tap-activated desk light, for skill-building before the main build)

## Rebuilding the PDF

If you edit the markdown or any SVG, regenerate the PDF with:

```
pip install markdown weasyprint
python3 build_pdf.py
```

All files must be in the same directory when you run the build. Output is written to `turn_counter_design_doc.pdf` in the same folder.

## Editing notes

**To change content**: edit `turn_counter_design_doc.md` and rebuild. The markdown supports tables, fenced code blocks, definition lists, and HTML attribute lists.

**To change a diagram**: edit the relevant SVG file directly and rebuild. The build script reads SVGs fresh each time.

**To control page breaks**: the page-break logic is near the top of `build_pdf.py` in a list of section headings. Add or remove entries from that list to change which sections start on a fresh page.

**To change styling**: the CSS is a triple-quoted string inside `build_pdf.py`. Search for `STYLE = """`. Major rules:

- `@page wide` controls landscape pages (used for the wiring diagram)
- `.figure.standalone` and `.figure.full-page` control how figures occupy whole pages
- The `h2 + p`, `h3 + p` rules with `break-before: avoid` keep headings from getting orphaned at the bottom of pages

## Firmware build environment

- Arduino IDE with ESP32 board support installed
- FastLED library
- ArduinoOTA library (bundled with the ESP32 core)
- Board: ESP32 Dev Module
- Upload speed: 921600

Wi-Fi credentials, mDNS hostname, and OTA password live at the top of `turn_counter.ino` — edit these before first flash.
