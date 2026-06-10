import markdown
import re
from pathlib import Path
from weasyprint import HTML, CSS
from doc_style import BASE_CSS

doc_dir = Path(__file__).resolve().parent
out_dir = doc_dir.parent

with open(out_dir / 'dry_run.md') as f:
    md_text = f.read()


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


md_text = ensure_blank_before_lists(md_text)

html_body = markdown.markdown(
    md_text,
    extensions=['tables', 'fenced_code', 'attr_list', 'sane_lists', 'def_list', 'md_in_html', 'toc']
)

# Turn "- [ ] ..." / "- [x] ..." into printable check boxes (python-markdown
# renders them as literal text; same substitution as the design-doc builder).
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

# Override just the footer string from the shared stylesheet (cascade: a later
# stylesheet with the same @page margin box wins). Everything else — fonts,
# tables, task boxes, page rules — comes from BASE_CSS so the two PDFs match.
footer_css = """
@page {
    @bottom-center {
        content: "Octagonal Turn Counter — Phase −1 Dry Run  •  page " counter(page) " of " counter(pages);
    }
}
"""

html_doc = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Octagonal Turn Counter — Phase −1 Dry Run</title>
</head>
<body>
{html_body}
</body>
</html>
"""

with open(doc_dir / '_build_dry_run.html', 'w') as f:
    f.write(html_doc)

pdf_path = out_dir / 'dry_run.pdf'
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
