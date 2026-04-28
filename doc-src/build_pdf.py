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


def strip_xml_decl(svg):
    return re.sub(r'<\?xml[^>]*\?>\s*', '', svg)

table_svg = strip_xml_decl(table_svg)
rim_svg = strip_xml_decl(rim_svg)
wiring_svg = strip_xml_decl(wiring_svg)
install_svg = strip_xml_decl(install_svg)


title_block_md = """**Project**: LED rim turn counter for an octagonal gaming table
**Geometry**: 8 sides × 20" each = 160" (~4 m) perimeter
**Player count**: Variable, 2–8 (configurable in firmware setup mode)
**Input method**: Piezo sensor per side (slap-to-advance)
**Brain**: ESP32 (with optional OTA firmware updates over Wi-Fi)
**Target**: Reliable, satisfying turn-passing mechanic"""

title_block_html = """<div class="metadata">
<dl>
<dt>Project</dt><dd>LED rim turn counter for an octagonal gaming table</dd>
<dt>Geometry</dt><dd>8 sides × 20" each = 160" (~4 m) perimeter</dd>
<dt>Player count</dt><dd>Variable, 2–8 (configurable in firmware setup mode)</dd>
<dt>Input method</dt><dd>Piezo sensor per side (slap-to-advance)</dd>
<dt>Brain</dt><dd>ESP32 (with optional OTA firmware updates over Wi-Fi)</dd>
<dt>Target</dt><dd>Reliable, satisfying turn-passing mechanic</dd>
</dl>
</div>"""

md_text = md_text.replace(title_block_md, title_block_html)


companion_block_md = """> **Companion files**:
> - `turn_counter_wiring.svg` — full schematic, print and keep at the bench
> - `turn_counter.ino` — main project firmware
> - `tap_light.ino` — Phase 0 starter project firmware
>
> Other diagrams (top view, edge cross-section, slab/frame architecture) are embedded inline in the relevant sections of this document."""

companion_block_html = """<div class="callout">
<p><strong>Companion files</strong>:</p>
<ul class="tight">
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


css = """
@page {
    size: letter;
    margin: 0.75in 0.7in 0.85in 0.7in;
    @bottom-center {
        content: "Octagonal Turn Counter — Design Document  •  page " counter(page) " of " counter(pages);
        font-family: 'Helvetica', 'Arial', sans-serif;
        font-size: 9pt;
        color: #777;
    }
    @top-right {
        content: string(chapter);
        font-family: 'Helvetica', 'Arial', sans-serif;
        font-size: 9pt;
        color: #777;
    }
}

@page :first {
    @top-right { content: ""; }
    margin: 1.2in 0.8in 1in 0.8in;
}

@page wide {
    size: letter landscape;
    margin: 0.5in 0.5in 0.5in 0.5in;
    @bottom-center {
        content: "Octagonal Turn Counter — Design Document  •  page " counter(page) " of " counter(pages);
        font-family: 'Helvetica', 'Arial', sans-serif;
        font-size: 9pt;
        color: #777;
    }
    @top-right { content: ""; }
}

html { font-family: 'Helvetica', 'Arial', sans-serif; }

body {
    font-size: 10.5pt;
    line-height: 1.45;
    color: #1a1a1a;
}

h1 {
    font-size: 24pt;
    margin-top: 0;
    margin-bottom: 0.5em;
    color: #111;
    border-bottom: 2px solid #333;
    padding-bottom: 0.3em;
    line-height: 1.2;
    page-break-after: avoid;
    break-after: avoid;
}

h2 {
    font-size: 16pt;
    margin-top: 1.4em;
    margin-bottom: 0.4em;
    color: #111;
    page-break-after: avoid;
    break-after: avoid;
    string-set: chapter content();
}

h2:first-of-type { margin-top: 0.5em; }

h3 {
    font-size: 13pt;
    margin-top: 1.1em;
    margin-bottom: 0.35em;
    color: #222;
    page-break-after: avoid;
    break-after: avoid;
}

h4 {
    font-size: 11.5pt;
    margin-top: 0.9em;
    margin-bottom: 0.3em;
    color: #333;
    page-break-after: avoid;
    break-after: avoid;
}

h2 + p, h3 + p, h4 + p {
    page-break-before: avoid;
    break-before: avoid;
    page-break-after: avoid;
    break-after: avoid;
}

h2 + ul, h3 + ul, h4 + ul,
h2 + ol, h3 + ol, h4 + ol,
h2 + .figure, h3 + .figure, h4 + .figure,
h2 + table, h3 + table, h4 + table,
h2 + pre, h3 + pre, h4 + pre,
h2 + .callout, h3 + .callout,
h2 + blockquote, h3 + blockquote, h4 + blockquote {
    page-break-before: avoid;
    break-before: avoid;
}

.page-break {
    page-break-after: always;
    break-after: page;
    height: 0;
}

p { margin: 0.45em 0; }

p, li, td, th { orphans: 3; widows: 3; }

strong { color: #111; }

ul, ol { margin: 0.45em 0; padding-left: 1.6em; }
li { margin: 0.12em 0; }

li.task {
    list-style: none;
    margin-left: -1.3em;
}

li.task .task-box {
    display: inline-block;
    width: 1em;
    margin-right: 0.4em;
    color: #444;
}

dl {
    margin: 0.5em 0;
    display: grid;
    grid-template-columns: max-content 1fr;
    gap: 0.2em 1em;
}

dt {
    font-weight: bold;
    color: #111;
}

dd {
    margin: 0;
}

.metadata {
    background: #f7f7f5;
    border-left: 3px solid #888;
    padding: 0.8em 1.2em;
    margin: 1em 0;
    font-size: 11pt;
}

.metadata dl {
    margin: 0;
}

.callout {
    background: #f7f7f5;
    border-left: 3px solid #888;
    padding: 0.6em 1em;
    margin: 1em 0;
    font-size: 10pt;
}

.callout p { margin: 0.2em 0; }

ul.tight, ol.tight { margin: 0.3em 0; }
ul.tight li, ol.tight li { margin: 0.05em 0; }

blockquote {
    margin: 0.8em 0;
    padding: 0.5em 1em;
    border-left: 3px solid #888;
    background: #f5f5f5;
    font-size: 10pt;
}

code {
    font-family: 'Menlo', 'Consolas', 'Courier New', monospace;
    font-size: 9.5pt;
    background: #f0f0f0;
    padding: 0.05em 0.3em;
    border-radius: 2px;
}

pre {
    background: #f5f5f5;
    border: 1px solid #ddd;
    border-radius: 3px;
    padding: 0.6em 0.8em;
    font-size: 8.5pt;
    line-height: 1.35;
    overflow: hidden;
    page-break-inside: avoid;
}

pre code {
    background: transparent;
    padding: 0;
    font-size: 8.5pt;
    white-space: pre;
}

table {
    border-collapse: collapse;
    margin: 0.7em 0;
    width: 100%;
    font-size: 9.5pt;
}

thead { display: table-header-group; }

tr { page-break-inside: avoid; break-inside: avoid; }

th, td {
    border: 1px solid #bbb;
    padding: 0.32em 0.55em;
    text-align: left;
    vertical-align: top;
}

th {
    background: #e8e8e8;
    font-weight: bold;
    color: #111;
}

tr:nth-child(even) td { background: #f7f7f7; }

hr {
    border: none;
    border-top: 1px solid #999;
    margin: 1.3em 0;
}

.figure {
    margin: 1em 0;
    text-align: center;
    page-break-inside: avoid;
    break-inside: avoid;
    page-break-before: avoid;
    break-before: avoid;
}

.figure svg {
    max-width: 100%;
    height: auto;
    max-height: 5.5in;
}

.figure.full-page {
    page: wide;
    page-break-before: always;
    page-break-after: always;
    margin: 0;
    text-align: center;
}

.figure.full-page svg {
    max-width: 100%;
    max-height: 6.8in;
    width: auto;
    height: auto;
}

.figure.standalone {
    page-break-before: always;
    page-break-after: always;
    margin: 0;
    text-align: center;
}

.figure.standalone svg {
    max-width: 100%;
    max-height: 8in;
    width: auto;
    height: auto;
}

.caption {
    font-size: 9pt;
    color: #555;
    font-style: italic;
    margin-top: 0.5em;
    padding: 0 1em;
    text-align: center;
}

h2 + .figure, h3 + .figure { margin-top: 0.6em; }
"""

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
