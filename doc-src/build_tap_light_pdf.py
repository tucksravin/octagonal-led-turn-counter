import markdown
import re
from pathlib import Path
from weasyprint import HTML, CSS
from doc_style import BASE_CSS

doc_dir = Path(__file__).resolve().parent
out_dir = doc_dir.parent

with open(out_dir / 'tap_light_circuit.md') as f:
    md_text = f.read()

with open(doc_dir / 'tap_light_layout.svg') as f:
    layout_svg = f.read()
layout_svg = re.sub(r'<\?xml[^>]*\?>\s*', '', layout_svg)


def ensure_blank_before_lists(text):
    """python-markdown needs a blank line before a list that follows a
    paragraph; insert one where the source omits it (same helper as the
    other builders)."""
    lines = text.split('\n')
    out = []
    for i, line in enumerate(lines):
        is_list_item = (
            re.match(r'^\d+\.\s', line) or
            re.match(r'^[-*]\s', line) or
            re.match(r'^- \[[ x]\]', line)
        )
        if is_list_item and i > 0:
            prev = lines[i - 1]
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

layout_figure = (
    f'<div class="figure">{layout_svg}'
    '<p class="caption">Figure 1 — Phase 0 Tap Light breadboard layout. '
    'GPIO 11 → 470 Ω → WS2812B strip DIN; GPIO 1 reads the piezo through a 1 MΩ '
    'load resistor and a 3.3 V Zener clamp. The ESP32\'s 5 V pin powers the strip; '
    'two ground pins share the one common GND rail. No level shifter.</p></div>'
)
md_text = md_text.replace('[TAP_LIGHT_LAYOUT_FIGURE]', layout_figure)

html_body = markdown.markdown(
    md_text,
    extensions=['tables', 'fenced_code', 'attr_list', 'sane_lists', 'def_list', 'md_in_html', 'toc']
)

# Override just the footer string from the shared stylesheet (cascade: a later
# stylesheet with the same @page margin box wins). Everything else — fonts,
# tables, page rules — comes from BASE_CSS so all the PDFs match.
footer_css = """
@page {
    @bottom-center {
        content: "Octagonal Turn Counter — Tap Light Circuit Walkthrough  •  page " counter(page) " of " counter(pages);
    }
}
"""

html_doc = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Tap Light — Circuit Walkthrough</title>
</head>
<body>
{html_body}
</body>
</html>
"""

with open(doc_dir / '_build_tap_light.html', 'w') as f:
    f.write(html_doc)

pdf_path = out_dir / 'tap_light_circuit.pdf'
HTML(string=html_doc, base_url=str(doc_dir)).write_pdf(
    pdf_path,
    stylesheets=[CSS(string=BASE_CSS), CSS(string=footer_css)]
)

import os
size_kb = os.path.getsize(pdf_path) / 1024
print(f"PDF written: {pdf_path} ({size_kb:.1f} KB)")

from pypdf import PdfReader
reader = PdfReader(pdf_path)
print(f"Pages: {len(reader.pages)}")
