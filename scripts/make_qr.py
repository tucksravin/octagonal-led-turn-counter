#!/usr/bin/env python3
"""Generate a printable QR sticker for the table's web UI — run via `make qr`.

Discovery needs three layers because no single one reaches every phone: mDNS
(`turn-counter.local`) covers Apple devices, a DHCP reservation gives Android a
stable IP, and this is the third — something physical to stick under the table
so a guest can reach the controls without being told an address.

Encode whichever address actually works on your network:

    make qr URL=192.168.0.50
    make qr URL=turn-counter.local

Outputs a scalable SVG plus a print-ready PDF with the URL spelled out
underneath, since a QR nobody can read back is hard to debug.
"""
import argparse
import sys
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit

try:
    import segno
except ImportError:
    sys.exit("segno not installed — run: .venv/bin/python3 -m pip install -r requirements.txt")

SVG_OUT = Path("doc-src/table_qr.svg")
PDF_OUT = Path("table_qr.pdf")


def normalize_url(value):
    """Turn user input into a URL a phone camera will open.

    Bare hosts gain `http://` and a root path; an existing http/https scheme and
    path are preserved. Anything else raises ValueError, because a QR that opens
    the wrong kind of handler is worse than no QR.
    """
    v = (value or "").strip()
    if not v:
        raise ValueError("URL is empty — pass one, e.g. make qr URL=192.168.0.50")
    if any(c.isspace() for c in v):
        raise ValueError(f"URL contains whitespace: {v!r}")

    if "://" not in v:
        v = "http://" + v

    parts = urlsplit(v)
    if parts.scheme not in ("http", "https"):
        raise ValueError(f"unsupported scheme {parts.scheme!r} — use http or https")
    if not parts.netloc:
        raise ValueError(f"no host in URL: {value!r}")

    return urlunsplit((parts.scheme, parts.netloc, parts.path or "/", parts.query, ""))


def write_svg(url, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    # Error correction 'h' tolerates ~30% damage — this is going under a table.
    segno.make(url, error="h").save(str(path), kind="svg", scale=10, border=2,
                                    dark="#000000", light="#ffffff")
    return path


def write_pdf(url, svg_path, path):
    from weasyprint import HTML
    html = f"""<html><head><style>
      @page {{ size: 90mm 110mm; margin: 8mm; }}
      body {{ font-family: -apple-system, sans-serif; text-align: center; }}
      img {{ width: 100%; }}
      .u {{ font-family: monospace; font-size: 11pt; margin-top: 4mm; word-break: break-all; }}
      .c {{ font-size: 9pt; color: #666; margin-top: 2mm; }}
    </style></head><body>
      <img src="{svg_path.name}">
      <div class="u">{url}</div>
      <div class="c">Turn counter — mode, brightness, on/off</div>
    </body></html>"""
    HTML(string=html, base_url=str(svg_path.parent)).write_pdf(str(path))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--url", required=True,
                        help="address the QR should open, e.g. 192.168.0.50")
    parser.add_argument("--svg", default=str(SVG_OUT))
    parser.add_argument("--pdf", default=str(PDF_OUT))
    args = parser.parse_args()

    try:
        url = normalize_url(args.url)
    except ValueError as err:
        sys.exit(str(err))

    svg = write_svg(url, Path(args.svg))
    pdf = write_pdf(url, svg, Path(args.pdf))
    print(f"{url}\n  {svg}\n  {pdf}  — print, trim, stick under the table")


if __name__ == "__main__":
    main()
