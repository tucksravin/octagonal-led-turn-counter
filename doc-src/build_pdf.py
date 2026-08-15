import markdown
import re
from pathlib import Path
from weasyprint import HTML, CSS

doc_dir = Path(__file__).resolve().parent
out_dir = doc_dir.parent

with open(out_dir / 'turn_counter_design_doc.md') as f:
    md_text = f.read()

with open(doc_dir / 'table_layout.svg') as f:
    table_svg = f.read()
with open(doc_dir / 'rim_section.svg') as f:
    rim_svg = f.read()
with open(doc_dir / 'turn_counter_wiring.svg') as f:
    wiring_svg = f.read()
with open(doc_dir / 'installation_arch.svg') as f:
    install_svg = f.read()
with open(doc_dir / 'corner_cap.svg') as f:
    cap_svg = f.read()
with open(doc_dir / 'corner_cap_fab.svg') as f:
    cap_fab_svg = f.read()


def strip_xml_decl(svg):
    return re.sub(r'<\?xml[^>]*\?>\s*', '', svg)

table_svg = strip_xml_decl(table_svg)
rim_svg = strip_xml_decl(rim_svg)
wiring_svg = strip_xml_decl(wiring_svg)
install_svg = strip_xml_decl(install_svg)
cap_svg = strip_xml_decl(cap_svg)
cap_fab_svg = strip_xml_decl(cap_fab_svg)


title_block_md = """**Project**: LED rim turn counter for an octagonal gaming table
**Geometry**: 8 sides × 20" each = 160" (~4 m) perimeter
**Player count**: Variable, 2–8 (configurable in firmware setup mode)
**Input method**: Piezo sensor per side (slap-to-advance)
**Brain**: ESP32-S3 (with optional OTA firmware updates over Wi-Fi)
**Target**: Reliable, satisfying turn-passing mechanic"""

title_block_html = """<div class="metadata">
<dl>
<dt>Project</dt><dd>LED rim turn counter for an octagonal gaming table</dd>
<dt>Geometry</dt><dd>8 sides × 20" each = 160" (~4 m) perimeter</dd>
<dt>Player count</dt><dd>Variable, 2–8 (configurable in firmware setup mode)</dd>
<dt>Input method</dt><dd>Piezo sensor per side (slap-to-advance)</dd>
<dt>Brain</dt><dd>ESP32-S3 (with optional OTA firmware updates over Wi-Fi)</dd>
<dt>Target</dt><dd>Reliable, satisfying turn-passing mechanic</dd>
</dl>
</div>"""

md_text = md_text.replace(title_block_md, title_block_html)


companion_block_md = """> **Companion files**:
> - `dry_run.md` — Phase −1 bench & skills shakedown on penny parts; do this first
> - `turn_counter_wiring.svg` — full schematic, print and keep at the bench
> - `turn_counter.ino` — main project firmware
> - `tap_light.ino` — Phase 0 starter project firmware
>
> Other diagrams (top view, edge cross-section, slab/frame architecture) are embedded inline in the relevant sections of this document."""

companion_block_html = """<div class="callout">
<p><strong>Companion files</strong>:</p>
<ul class="tight">
<li><code>dry_run.md</code> — Phase −1 bench &amp; skills shakedown on penny parts; do this first</li>
<li><code>turn_counter_wiring.svg</code> — full schematic, print and keep at the bench</li>
<li><code>turn_counter.ino</code> — main project firmware</li>
<li><code>tap_light.ino</code> — Phase 0 starter project firmware</li>
</ul>
<p style="margin-top: 0.4em; margin-bottom: 0;">Other diagrams (top view, edge cross-section, slab/frame architecture) are embedded inline in the relevant sections of this document.</p>
</div>"""

md_text = md_text.replace(companion_block_md, companion_block_html)


md_text = re.sub(r'\n---\n+(?=## [0-9]+\. )', '\n\n', md_text)


for section_heading in [
    '## 1. The Broad Plan',
    '## 2. Bill of Materials',
    '## 3. Electrical Design',
    '## 4. Mechanical Design',
    '## 5. Phase-by-Phase Build Steps',
    '## 6. Controls & Setup Mode',
    '## 7. Firmware',
    '## 8. Troubleshooting',
    '## 9. Future Enhancements',
    '## 10. Quick Reference Card',
]:
    md_text = md_text.replace(
        section_heading,
        f'<div class="page-break"></div>\n\n{section_heading}'
    )


def ensure_blank_before_lists(text):
    lines = text.split('\n')
    out = []
    for i, line in enumerate(lines):
        is_list_item = (
            re.match(r'^\d+\.\s', line) or
            re.match(r'^[-*]\s', line) or
            re.match(r'^- \[[ x]\]', line)
        )
        if is_list_item and i > 0:
            prev = lines[i-1]
            prev_is_list = (
                re.match(r'^\d+\.\s', prev) or
                re.match(r'^[-*]\s', prev) or
                re.match(r'^\s+', prev) or
                prev.strip() == ''
            )
            if not prev_is_list and prev.strip() != '':
                out.append('')
        out.append(line)
    return '\n'.join(out)

md_text = ensure_blank_before_lists(md_text)


md_text = md_text.replace(
    '## 0. Before You Start: Tools, Skills, and a Practice Project',
    f'<div class="figure standalone">{table_svg}<p class="caption">Figure 0.1 — top-down view of the finished table. The lit edge indicates the current player\'s section; tapping that section advances the turn clockwise to the next player.</p></div>\n\n## 0. Before You Start: Tools, Skills, and a Practice Project'
)

md_text = md_text.replace(
    '## 3. Electrical Design',
    '<div class="figure full-page">' + wiring_svg + '<p class="caption">Figure 3.1 — full wiring diagram. PSU at top-left feeds +5V/GND rails across the top. ESP32 drives 8 piezo inputs and one LED data line through a 470 Ω resistor and 74AHCT125 level shifter. Three power injection points distribute current along the strip. Per-piezo input network detail at bottom-left.</p></div>\n\n## 3. Electrical Design'
)

rim_figure = f'<div class="figure">{rim_svg}<p class="caption">Figure 4.1 — edge cross-section. The aluminum LED channel mounts to the outer edge of the slab; the piezo glues to the underside, centered on each player\'s wedge of the octagon. Air space below means the slab vibrates freely, so a tap anywhere on the bare top surface couples efficiently into the piezo underneath.</p></div>'

md_text = md_text.replace('[RIM_SECTION_FIGURE]', rim_figure)


cap_figure = f'<div class="figure">{cap_svg}<p class="caption">Figure 4.2 — corner caps. Top: one of the 8 corners seen from above — the cap wraps the 135° corner, covers the jumper bridge in the channel miter gap, and stands ~5 mm proud of the diffuser. Bottom: the slab leaned on its side for bumper pool rests only on the two rubber-skinned caps; the channel and diffuser never touch the floor.</p></div>'

md_text = md_text.replace('[CORNER_CAP_FIGURE]', cap_figure)


cap_fab_figure = f'<div class="figure full-page">{cap_fab_svg}<p class="caption">Figure 4.3 — making the caps with a circular saw and a drill. A: the cavity is a through cut, so biasing the channel to one edge of the slab turns it into a rabbet (two rip cuts) instead of a walled groove. B: mill the profile down one long stick, chamfer it there too, and crosscut the caps last. C: instead of gluing two 22.5° miters, cut a 45° V-notch in the back of one blank and fold it — the show face stays continuous. D: crosscut layout and the dimensions to take off the real parts first.</p></div>'

md_text = md_text.replace('[CORNER_CAP_FAB_FIGURE]', cap_fab_figure)


install_figure = f'<div class="figure">{install_svg}<p class="caption">Figure 4.6 — slab/frame architecture. Everything turn-counter-related lives on the removable top slab; the PSU and AC mains live permanently on the bumper pool frame. A single Anderson Powerpole DC connector is the only thing you disconnect when lifting the top.</p></div>'

md_text = md_text.replace('[INSTALLATION_ARCH_FIGURE]', install_figure)


html_body = markdown.markdown(
    md_text,
    extensions=['tables', 'fenced_code', 'attr_list', 'sane_lists', 'def_list', 'md_in_html', 'toc']
)


html_body = re.sub(
    r'<li>\[ \] ',
    '<li class="task"><span class="task-box">☐</span> ',
    html_body
)
html_body = re.sub(
    r'<li>\[x\] ',
    '<li class="task"><span class="task-box">☑</span> ',
    html_body
)


from doc_style import BASE_CSS as css

html_doc = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Octagonal Turn Counter — Design Document</title>
</head>
<body>
{html_body}
</body>
</html>
"""

with open(doc_dir / '_build_doc.html', 'w') as f:
    f.write(html_doc)

pdf_path = out_dir / 'turn_counter_design_doc.pdf'
HTML(string=html_doc, base_url=str(doc_dir)).write_pdf(
    pdf_path,
    stylesheets=[CSS(string=css)]
)

import os
size_kb = os.path.getsize(pdf_path) / 1024
print(f"PDF written: {pdf_path} ({size_kb:.1f} KB)")

from pypdf import PdfReader
reader = PdfReader(pdf_path)
print(f"Pages: {len(reader.pages)}")
