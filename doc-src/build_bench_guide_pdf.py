import markdown
import re
import os
from pathlib import Path
from weasyprint import HTML, CSS
from doc_style import BASE_CSS

doc_dir = Path(__file__).resolve().parent
out_dir = doc_dir.parent

with open(out_dir / 'bench_build_guide.md') as f:
    md_text = f.read()


def strip_xml_decl(svg):
    return re.sub(r'<\?xml[^>]*\?>\s*', '', svg)


def load_svg(name):
    with open(doc_dir / name) as f:
        return strip_xml_decl(f.read())


wiring_svg = load_svg('turn_counter_wiring.svg')
proto_layout_svg = load_svg('protoboard_half_layout.svg')
proto_wiring_svg = load_svg('protoboard_wiring.svg')


def figure(svg, caption):
    # All three diagrams are landscape (wide); use the full-page landscape rule.
    return (
        f'<div class="figure full-page">{svg}'
        f'<p class="caption">{caption}</p></div>'
    )


md_text = md_text.replace('[WIRING_FIGURE]', figure(
    wiring_svg,
    'Figure 1 — full system wiring. PSU (top-left) feeds the +5V/GND rails; the '
    'ESP32-S3 drives 8 piezo ADC inputs and one LED data line through a 470 Ω '
    'resistor. The 74AHCT125 level shifter shown in the data path is an OPTIONAL '
    'robustness upgrade — the default build drives the strip directly at 3.3 V '
    '(§3). Three injection points distribute current along the 240-LED strip.'
))

md_text = md_text.replace('[PROTO_LAYOUT_FIGURE]', figure(
    proto_layout_svg,
    'Figure 2 — control-box protoboard placement (half-size Perma-Proto, 30 cols), '
    'direct-drive default. ESP32-S3 socketed at cols 1–22 (USB ports left); cols '
    '23–30 spare. Only the socket, the 1000 µF cap, and the GPIO 11 → 470 Ω → strip '
    'data run are on the board — the 1 MΩ + Zener live at each disc and the pigtails '
    'solder directly. The dashed box shows where the optional 74AHCT125 would go. '
    'Wire runs and solder order are in §5.2.'
))

md_text = md_text.replace('[PROTO_WIRING_FIGURE]', figure(
    proto_wiring_svg,
    'Figure 3 — retired full-size wiring diagram, kept for its DevKitC-1 v1.1 pin '
    'map and the electrical topology (identical in the half-size build). Column '
    'numbers here are the OLD full-size positions — for the current build follow '
    '§5.2 and Figure 2; this drawing also shows the piezo networks on-board, which '
    'now live at the discs. ● = solder joint; a crossing without a dot is NOT connected.'
))


# Page-break before each major numbered section (matches build_pdf.py).
for n in range(1, 13):
    md_text = md_text.replace(
        f'\n## {n}. ',
        f'\n<div class="page-break"></div>\n\n## {n}. ',
        1,
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

html_body = markdown.markdown(
    md_text,
    extensions=['tables', 'fenced_code', 'attr_list', 'sane_lists', 'def_list', 'md_in_html', 'toc']
)

# Checkbox list items → styled task boxes (same transform as build_pdf.py).
html_body = re.sub(r'<li>\[ \] ', '<li class="task"><span class="task-box">☐</span> ', html_body)
html_body = re.sub(r'<li>\[x\] ', '<li class="task"><span class="task-box">☑</span> ', html_body)

# Per-doc footer string; everything else comes from BASE_CSS so the PDFs match.
footer_css = """
@page {
    @bottom-center {
        content: "Octagonal Turn Counter — At-the-Bench Build Guide  •  page " counter(page) " of " counter(pages);
    }
}
@page wide {
    @bottom-center {
        content: "Octagonal Turn Counter — At-the-Bench Build Guide  •  page " counter(page) " of " counter(pages);
    }
}
"""

html_doc = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Octagonal Turn Counter — At-the-Bench Build Guide</title>
</head>
<body>
{html_body}
</body>
</html>
"""

with open(doc_dir / '_build_bench_guide.html', 'w') as f:
    f.write(html_doc)

pdf_path = out_dir / 'bench_build_guide.pdf'
HTML(string=html_doc, base_url=str(doc_dir)).write_pdf(
    pdf_path,
    stylesheets=[CSS(string=BASE_CSS), CSS(string=footer_css)]
)

size_kb = os.path.getsize(pdf_path) / 1024
print(f"PDF written: {pdf_path} ({size_kb:.1f} KB)")

from pypdf import PdfReader
reader = PdfReader(pdf_path)
print(f"Pages: {len(reader.pages)}")
