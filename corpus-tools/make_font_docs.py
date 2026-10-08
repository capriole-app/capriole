#!/usr/bin/env python3
"""Documents for checking the extension of embedded font subsets (PLAN.md phase 9c): PDFs made here, with
fonts installed here, so that their subsets are of the same version as the installed fonts — the case of
a person editing a document made on their own computer. The corpora have few such pages (fonts are
installed in other versions, or not at all), so check_font_extend.py runs on these too.

Two producers, as people make PDFs: LibreOffice (Writer, --convert-to pdf: Latin text in simple TrueType
subsets with WinAnsiEncoding) and Chromium (Playwright's, page.pdf(): CID TrueType subsets, Identity-H).
Each document has paragraphs of several lines in one family and style, in texts that leave out the
letters check_font_extend.py adds (Q, Z, X, J, K, W, Y, V — and their lowercase).

    corpus-tools/make_font_docs.py ../CORPORA/generated/font-extend      # needs libreoffice, node, Playwright
"""

from __future__ import annotations

import argparse
import html
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FONTS = [
    ("DejaVu Sans", "normal", "normal"), ("DejaVu Sans", "bold", "normal"), ("DejaVu Serif", "normal", "normal"),
    ("DejaVu Serif", "normal", "italic"), ("DejaVu Sans Mono", "normal", "normal"), ("Liberation Sans", "normal", "normal"),
    ("Liberation Sans", "bold", "normal"), ("Liberation Serif", "normal", "normal"), ("Liberation Serif", "normal", "italic"),
    ("Liberation Mono", "normal", "normal"), ("Noto Sans", "normal", "normal"), ("Noto Serif", "normal", "normal"),
    ("Ubuntu", "normal", "normal"), ("Ubuntu", "bold", "normal"), ("DejaVu Sans Condensed", "normal", "normal"),
]

# Texts without Q, Z, X, J, K, W, Y, V (either case).
TEXTS = [
    "Il presente documento descrive le attività previste per il prossimo semestre, con le date principali, "
    "le persone coinvolte e le risorse necessarie per portare a termine il progetto in modo ordinato e senza ritardi. "
    "Le scadenze sono state concordate durante la riunione del consiglio e saranno comunicate a tutti gli interessati.",
    "The annual report presents the results of the institute, the main achievements of its research groups and the "
    "plans for the coming terms. The tables at the end of the document list the figures for each department, "
    "compared with those of the last three periods.",
    "Le comité a examiné les demandes reçues pendant le dernier trimestre et a retenu celles qui répondent aux "
    "critères publiés. Les résultats seront communiqués aux candidats par courrier dans les meilleurs délais.",
    "Die Mitglieder des Rates haben die Anträge des letzten Quartals geprüft und die Entscheidungen in diesem "
    "Protokoll festgehalten. Die Ergebnisse gelten ab dem ersten Tag des folgenden Monats für alle Abteilungen.",
]


def paragraphs(text: str) -> list[str]:
    """Three paragraphs: the text, then two others (words shown once on the page are what is edited)."""
    at = TEXTS.index(text)
    return [text, TEXTS[(at + 1) % len(TEXTS)], TEXTS[(at + 2) % len(TEXTS)]]


def libreoffice(out: Path, work: Path) -> int:
    """Writer documents (flat ODF), one per font, converted to PDF."""
    made = 0
    for i, (family, weight, style) in enumerate(FONTS):
        text = TEXTS[i % len(TEXTS)]
        body = "".join(f'<text:p text:style-name="P">{html.escape(p)}</text:p>' for p in paragraphs(text))
        odf = f"""<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
  xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
  office:version="1.3" office:mimetype="application/vnd.oasis.opendocument.text">
 <office:font-face-decls><style:font-face style:name="F" svg:font-family="'{family}'" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"/></office:font-face-decls>
 <office:automatic-styles><style:style style:name="P" style:family="paragraph"><style:paragraph-properties fo:margin-bottom="0.4cm" fo:text-align="justify"/>
  <style:text-properties style:font-name="F" fo:font-size="11pt" fo:font-weight="{weight}" fo:font-style="{style}"/></style:style></office:automatic-styles>
 <office:body><office:text>{body}</office:text></office:body>
</office:document>"""
        source = work / f"lo-{i:02d}.fodt"
        source.write_text(odf)
        subprocess.run(["libreoffice", "--headless", "--convert-to", "pdf", "--outdir", str(work), str(source)], capture_output=True, check=False, timeout=180)
        pdf = source.with_suffix(".pdf")
        if pdf.exists():
            shutil.move(pdf, out / f"libreoffice-{i:02d}-{family.replace(' ', '')}-{weight}-{style}.pdf")
            made += 1
    return made


def chromium(out: Path, work: Path) -> int:
    """HTML pages, one per font, printed to PDF by Playwright's Chromium."""
    pages = []
    for i, (family, weight, style) in enumerate(FONTS):
        text = TEXTS[(i + 1) % len(TEXTS)]
        body = "".join(f"<p>{html.escape(p)}</p>" for p in paragraphs(text))
        page = (f"<!doctype html><meta charset=utf-8><style>body{{font-family:'{family}';font-weight:{weight};font-style:{style};"
                f"font-size:11pt;width:15cm;margin:2cm}} p{{text-align:{'justify' if i % 2 else 'left'};margin:0 0 .5cm}}</style>{body}")
        source = work / f"ch-{i:02d}.html"
        source.write_text(page)
        pages.append((str(source), str(out / f"chromium-{i:02d}-{family.replace(' ', '')}-{weight}-{style}.pdf")))
    script = work / "print.cjs"
    script.write_text(
        "const { chromium } = require(process.argv[2]);\n"
        "(async () => { const browser = await chromium.launch(); const page = await browser.newPage();\n"
        "  for (const [src, dst] of JSON.parse(process.argv[3])) { await page.goto('file://' + src); await page.pdf({ path: dst, format: 'A4' }); }\n"
        "  await browser.close(); })();\n")
    playwright = ROOT / "apps/capriole/node_modules/playwright-core"
    import json
    subprocess.run(["node", str(script), str(playwright), json.dumps(pages)], check=False, timeout=600)
    return sum(1 for _, dst in pages if Path(dst).exists())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out")
    args = parser.parse_args()
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    # LibreOffice (a snap) reads and writes under the home directory only.
    work = Path(tempfile.mkdtemp(prefix=".make-font-docs-", dir=out))
    try:
        made = libreoffice(out, work) + chromium(out, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print(f"{made} documents in {out}")


if __name__ == "__main__":
    main()
