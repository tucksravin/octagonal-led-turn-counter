import markdown
import re
from pathlib import Path
from weasyprint import HTML, CSS
from doc_style import BASE_CSS

# Builds design_doc_simple.pdf from design_doc_simple.md. That doc is
# markdown-only (ASCII diagrams in fenced blocks, no SVG placeholders), so this
# is the same shape as build_dry_run_pdf.py — no figure embedding needed.
doc_dir = Path(__file__).resolve().parent
out_dir = doc_dir.parent

with open(out_dir / 'design_doc_simple.md') as f:
    md_text = f.read()

with open(doc_dir / 'simple_system.svg') as f:
    system_svg = f.read()
system_svg = re.sub(r'<\?xml[^>]*\?>\s*', '', system_svg)  # strip XML decl for inline embed


def ensure_blank_before_lists(text):
    """python-markdown needs a blank line before a list that follows a
    paragraph; insert one where the source omits it (same helper as the
    design-doc builder)."""
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


system_figure = (
    f'<div class="figure">{system_svg}'
    '<p class="caption">Figure 1 — the whole system on one lid. A USB powerbank (or wall '
    'adapter) feeds the ESP32-S3 over one USB cable; the board passes 5 V to the WS2812B rim '
    'and drives its data line through a 470 Ω resistor. Eight piezo discs report to the ADC. '
    'Nothing crosses to the pool-table frame. Normal one-side play draws ~0.85 A — within the '
    'board\'s USB path — and <code>MAX_POWER_MA = 1500</code> dims the rare all-on frames to fit.</p></div>'
)
md_text = md_text.replace('[SIMPLE_SYSTEM_FIGURE]', system_figure)

md_text = ensure_blank_before_lists(md_text)

html_body = markdown.markdown(
    md_text,
    extensions=['tables', 'fenced_code', 'attr_list', 'sane_lists', 'def_list', 'md_in_html', 'toc']
)

# Turn "- [ ] ..." / "- [x] ..." into printable check boxes (harmless if the
# doc has none; keeps parity with the other builders).
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

# Override just the footer string from the shared stylesheet (a later stylesheet
# with the same @page margin box wins). Everything else comes from BASE_CSS so
# all the PDFs match.
footer_css = """
@page {
    @bottom-center {
        content: "Octagonal Turn Counter — Simple Build  •  page " counter(page) " of " counter(pages);
    }
}
"""

html_doc = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Octagonal Turn Counter — Simple Build</title>
</head>
<body>
{html_body}
</body>
</html>
"""

with open(doc_dir / '_build_simple.html', 'w') as f:
    f.write(html_doc)

pdf_path = out_dir / 'design_doc_simple.pdf'
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
