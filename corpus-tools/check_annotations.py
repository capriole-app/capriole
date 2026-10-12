#!/usr/bin/env python3
"""Annotations made by Capriole, as other readers see them (PLAN.md phase 8d, exit criterion).

Two checks, with `cpdf annot add` (the C API) making every kind of annotation:

1. Fidelity, on a blank page (upright, and turned by /Rotate 90): each kind is drawn by Poppler
   (`pdftoppm`) and MuPDF (`mutool draw`) as Capriole draws it — they all draw the appearance Capriole
   wrote — with a similarity ≥ --threshold (the metric of compare_render.py).
2. Real files: on one page per file (hash order of --seed), the ten kinds are added — the text markup
   over words of the page (`cpdf text --words`), the others at places relative to the page — and saved
   incrementally. The output must pass `qpdf --check` no worse than the original, list the ten
   annotations when read back, and every engine (Capriole, Poppler, MuPDF) must draw something inside
   each annotation's rectangle: ≥ --min-change of its pixels (two around it included) differ from the
   page without annotations. (How much differs varies with anti-aliasing and line rounding — a 1.5 pt
   underline is one pixel in Poppler, two in Capriole at 72 dpi —, so the engines are not compared with
   each other here: fidelity is check 1.)

Exit criterion of phase 8d (engine): every fidelity case and every file pass.

    corpus-tools/check_annotations.py ../CORPORA/safedocs/0000 --cpdf build/release/tools/cpdf/cpdf \\
        --files 100 --report-dir ../CORPORA/reports --name annotations-safedocs-0000
"""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import hashlib
import json
import re
import shutil
import tempfile
from pathlib import Path

from PIL import Image, ImageChops

from compare_render import load, page_count, run, similarity
from check_pages import qpdf_severity

KINDS = ["highlight", "underline", "strikeout", "squiggly", "note", "ink", "square", "circle", "line", "freetext"]


def blank_pdf(rotate: int) -> bytes:
    page = f"<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 200]{'/Rotate %d' % rotate if rotate else ''}>>"
    bodies = ["<</Type/Catalog/Pages 2 0 R>>", "<</Type/Pages/Kids[3 0 R]/Count 1>>", page]
    pdf = "%PDF-1.7\n"
    offsets = []
    for i, body in enumerate(bodies):
        offsets.append(len(pdf))
        pdf += f"{i + 1} 0 obj\n{body}\nendobj\n"
    xref = len(pdf)
    pdf += f"xref\n0 {len(bodies) + 1}\n0000000000 65535 f \n" + "".join(f"{o:010d} 00000 n \n" for o in offsets)
    pdf += f"trailer\n<</Size {len(bodies) + 1}/Root 1 0 R>>\nstartxref\n{xref}\n%%EOF\n"
    return pdf.encode("latin-1")


def specs(width: float, height: float, words: list[list[float]]) -> list[dict]:
    """One annotation of each kind, placed relative to a page shown width × height."""
    w, h = width, height
    def at(fx: float, fy: float) -> list[float]:
        return [w * fx, h * fy]
    def box(fx0: float, fy0: float, fx1: float, fy1: float) -> list[float]:
        return [w * fx0, h * fy0, w * fx1, h * fy1]
    marks = words[:4] if len(words) >= 4 else [box(0.1, 0.1 + 0.08 * i, 0.45, 0.15 + 0.08 * i) for i in range(4)]
    return [
        {"kind": "highlight", "quads": [marks[0]]},
        {"kind": "underline", "quads": [marks[1]], "color": [1, 0, 0]},
        {"kind": "strikeout", "quads": [marks[2]], "color": [1, 0, 0]},
        {"kind": "squiggly", "quads": [marks[3]], "color": [0, 0, 1]},
        {"kind": "note", "rect": [w * 0.85, h * 0.05, w * 0.85 + 20, h * 0.05 + 20], "contents": "Nota"},
        {"kind": "ink", "strokes": [[at(0.1, 0.6), at(0.2, 0.7), at(0.3, 0.6), at(0.4, 0.7)]], "color": [0, 0.5, 0], "width": 2},
        {"kind": "square", "rect": box(0.5, 0.55, 0.65, 0.7), "color": [0, 0, 1], "fill": [0.85, 0.9, 1], "width": 2},
        {"kind": "circle", "rect": box(0.7, 0.55, 0.85, 0.7), "color": [1, 0, 0], "width": 1.5},
        {"kind": "line", "from": at(0.1, 0.8), "to": at(0.5, 0.9), "color": [0, 0, 0], "width": 1},
        {"kind": "freetext", "rect": [w * 0.55, h * 0.8, w * 0.55 + 160, h * 0.8 + 36], "contents": "Testo libero di prova, a capo.", "fontSize": 10,
         "color": [0, 0, 0.6]},
    ]


def draw_all(args: argparse.Namespace, path: Path, page: int, out: Path, stem: str) -> dict[str, Image.Image | None]:
    cap, pp, mu = out / f"{stem}-capriole.png", out / f"{stem}-pdftoppm", out / f"{stem}-mutool.png"
    resources = ["--resources", args.resources] if args.resources else []
    run([args.cpdf, "render", "--dpi", str(args.dpi), *resources, str(path), str(page), str(cap)], args.timeout)
    run(["pdftoppm", "-cropbox", "-r", str(args.dpi), "-f", str(page), "-l", str(page), "-png", "-singlefile", str(path), str(pp)], args.timeout)
    run(["mutool", "draw", "-q", "-r", str(args.dpi), "-o", str(mu), str(path), str(page)], args.timeout)
    return {"capriole": load(cap), "pdftoppm": load(Path(f"{pp}.png")), "mutool": load(mu)}


def add_all(args: argparse.Namespace, source: Path, page: int, annotations: list[dict], out: Path) -> tuple[Path, list[str]]:
    errors = []
    current = source
    for i, spec in enumerate(annotations):
        target = out / f"{source.stem}-a{i}.pdf"
        result = run([args.cpdf, "annot", "add", str(current), str(page), json.dumps(spec), str(target)], args.timeout)
        if result["status"] != 0:
            errors.append(f"{spec['kind']}: {result['output'].strip()[:200]}")
            continue
        current = target
    return current, errors


def fidelity(args: argparse.Namespace, work: Path) -> list[dict]:
    results = []
    for rotate in (0, 90):
        for spec in specs(*((300, 200) if rotate == 0 else (200, 300)), []):
            name = f"blank-r{rotate}-{spec['kind']}"
            blank = work / f"{name}.pdf"
            blank.write_bytes(blank_pdf(rotate))
            annotated, errors = add_all(args, blank, 1, [spec], work)
            r = {"case": name, "errors": errors}
            if not errors:
                images = draw_all(args, annotated, 1, work, name)
                cap = images["capriole"]
                for ref in ("pdftoppm", "mutool"):
                    other = images[ref]
                    r[ref] = round(similarity(cap, other, args.blur, args.tolerance), 4) if cap and other else None
            r["pass"] = not errors and all((r.get(k) or 0) >= args.threshold for k in ("pdftoppm", "mutool"))
            results.append(r)
    return results


def changed_in(before: Image.Image | None, after: Image.Image | None, rect: list[float], scale: float) -> float:
    """The share of pixels inside rect (points, as shown) that differ between the two renderings."""
    if before is None or after is None or before.size != after.size:
        return 0.0
    # Two pixels around: a mark on the rectangle's edge (an underline) is still inside.
    x0, y0, x1, y1 = (int(round(v * scale)) + d for v, d in zip(rect, (-2, -2, 2, 2)))
    box = (max(x0, 0), max(y0, 0), min(x1, after.width), min(y1, after.height))
    if box[2] <= box[0] or box[3] <= box[1]:
        return 0.0
    diff = ImageChops.difference(before.crop(box), after.crop(box)).convert("L").point(lambda v: 255 if v > 24 else 0)
    return diff.histogram()[255] / ((box[2] - box[0]) * (box[3] - box[1]))


def check_file(args: argparse.Namespace, path: Path, work: Path) -> dict:
    result: dict = {"file": str(path), "checks": [], "outcome": "pass"}
    count = page_count(path)
    if not count:
        result["outcome"] = "skipped"
        return result
    digest = int(hashlib.sha256(f"{args.seed}:{path.name}".encode()).hexdigest(), 16)
    page = 1 + digest % count
    result["page"] = page

    def fail(what: str, detail: str) -> None:
        result["outcome"] = "fail"
        result["checks"].append({"check": what, "detail": detail[:300]})

    sound = work / f"{path.stem}-sound.pdf"
    if run([args.cpdf, "save", str(path), str(sound)], args.timeout)["status"] != 0:
        result["outcome"] = "skipped"
        return result
    before = draw_all(args, sound, page, work, f"{path.stem}-before")
    if before["pdftoppm"] is None:
        result["outcome"] = "skipped"  # Poppler cannot draw it: nothing to compare with
        return result
    scale = args.dpi / 72
    width, height = before["pdftoppm"].width / scale, before["pdftoppm"].height / scale
    words = []
    text = run([args.cpdf, "text", "--words", str(sound), str(page)], args.timeout)
    for line in text["output"].splitlines():
        parts = line.split("\t")
        if len(parts) >= 2 and len(parts[0]) >= 3:
            box = [float(v) for v in parts[1].split()]
            # Horizontal words (text markup takes boxes of horizontal text: vertical text is not its case).
            if box[2] - box[0] > box[3] - box[1] > 4 and box[2] < width and box[3] < height:
                words.append(box)
    annotations = specs(width, height, words[::max(1, len(words) // 8)])
    annotated, errors = add_all(args, sound, page, annotations, work)
    for e in errors:
        fail("add", e)
    if errors:
        return result
    if qpdf_severity(annotated, args.timeout) > qpdf_severity(sound, args.timeout):
        fail("qpdf", "qpdf --check worse than the original")
    listed = run([args.cpdf, "annot", "list", str(annotated), str(page)], args.timeout)
    try:
        mine = [a for a in json.loads(listed["output"]) if a.get("author") == "" and a["subtype"] in
                {"Highlight", "Underline", "StrikeOut", "Squiggly", "Text", "Ink", "Square", "Circle", "Line", "FreeText"}]
    except json.JSONDecodeError:
        mine = []
        fail("list", listed["output"][:200])
    if len(mine) < len(annotations):
        fail("list", f"{len(mine)} annotations read back, {len(annotations)} added")
    after = draw_all(args, annotated, page, work, f"{path.stem}-after")
    for engine in ("capriole", "pdftoppm", "mutool"):
        if before[engine] is None:
            continue
        for a in mine[-len(annotations):]:
            share = changed_in(before[engine], after[engine], a["rect"], scale)
            if share < args.min_change:
                fail(engine, f"{a['subtype']} not drawn (changed {share:.4f} of its rectangle)")
    for leftover in work.glob(f"{path.stem}-*"):
        leftover.unlink()
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="+")
    parser.add_argument("--cpdf", required=True)
    parser.add_argument("--resources", default="build/resources" if Path("build/resources/cmap").is_dir() else None)
    parser.add_argument("--files", type=int, default=100)
    parser.add_argument("--seed", default="capriole-phase-8d")
    parser.add_argument("--dpi", type=int, default=72)
    parser.add_argument("--blur", type=float, default=1.0)
    parser.add_argument("--tolerance", type=int, default=32)
    parser.add_argument("--threshold", type=float, default=0.99)
    parser.add_argument("--min-change", type=float, default=0.003)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="annotations")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())
    args.resources = str(Path(args.resources).resolve()) if args.resources else None

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="annotations-", dir=report))
    fidelity_results = fidelity(args, work)

    files = [p for d in args.corpus
             for p in sorted({f.resolve() for f in Path(d).glob("*.pdf")}, key=lambda f: hashlib.sha256(f"{args.seed}:{f.name}".encode()).hexdigest())]
    results: list[dict] = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool, open(report / "results.jsonl", "w") as log:
        for r in fidelity_results:
            log.write(json.dumps(r) + "\n")
        running = set()
        for path in files:
            running.add(pool.submit(check_file, args, path, work))
            if len(running) >= args.jobs * 2:
                done, running = concurrent.futures.wait(running, return_when=concurrent.futures.FIRST_COMPLETED)
                for future in done:
                    results.append(future.result())
                    log.write(json.dumps(results[-1]) + "\n")
            if sum(r["outcome"] != "skipped" for r in results) >= args.files:
                break
        for future in concurrent.futures.as_completed(running):
            results.append(future.result())
            log.write(json.dumps(results[-1]) + "\n")
    shutil.rmtree(work)

    counted = [r for r in results if r["outcome"] != "skipped"][: args.files]
    passed = [r for r in counted if r["outcome"] == "pass"]
    fid_pass = [r for r in fidelity_results if r["pass"]]
    met = len(fid_pass) == len(fidelity_results) and len(passed) == len(counted) and counted
    lines = [f"# Annotations {args.name} ({stamp})", "",
             f"- **Fidelity (blank page, upright and turned; Capriole vs pdftoppm and mutool ≥ {args.threshold}): "
             f"{len(fid_pass)} / {len(fidelity_results)}**",
             f"- **Real files (ten kinds added, qpdf no worse, read back, drawn by all three engines): {len(passed)} / {len(counted)}**",
             f"- Exit criterion (every case and every file): **{'met' if met else 'not met'}**", "", "## Fidelity", "",
             "| case | pdftoppm | mutool |", "|---|---|---|"]
    lines += [f"| {r['case']} | {r.get('pdftoppm')} | {r.get('mutool')} |" + (f" {r['errors']}" if r["errors"] else "") for r in fidelity_results]
    lines += ["", "## Failing files", ""]
    lines += [f"- {Path(r['file']).name} p{r.get('page')}: " + "; ".join(f"{c['check']}: {c['detail']}" for c in r["checks"])[:500]
              for r in counted if r["outcome"] == "fail"] or ["- none"]
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:5]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
