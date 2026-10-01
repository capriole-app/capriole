#!/usr/bin/env python3
"""Page operations on real files (PLAN.md phase 8a, exit criterion).

For a sample of files (chosen by a hash of the seed and the file name, as in compare_render.py), Capriole
applies its page operations with `cpdf` — the file first saved fully, as an application does before
editing a file that needed repairs —:

- rotate one page by 90° (and, incrementally, another by 180°);
- arrange: up to three pages in a new order, the others deleted;
- extract one page into a new file (compact save);
- merge: that extracted page followed by every page of the file.

Each output must pass `qpdf --check` no worse than the original file does (severity: ok < warnings <
errors), and the pages it holds must look like the pages they come from: Poppler (`pdftoppm`) draws
both, and their similarity (the share of pixels within --tolerance after a --blur, as compare_render.py)
must reach --threshold. A rotated page is checked by its rotation (`pdfinfo`) and by rotating it back
with `cpdf`, which must give the original's pixels: a renderer draws text turned by 90° differently
(hinting, anti-aliasing), so the original's picture turned afterwards is not a fair reference.
A file passes when every operation succeeds and every check holds. Files Poppler cannot draw are left
out (and counted). Exit criterion of phase 8a: every file passes.

    corpus-tools/check_pages.py ../CORPORA/safedocs/0000 --cpdf build/release/tools/cpdf/cpdf \\
        --files 200 --report-dir ../CORPORA/reports --name pages-safedocs-0000

Outputs, under --report-dir/<name>-<timestamp>/: results.jsonl (one object per file) and summary.md.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import datetime
import hashlib
import json
import re
import shutil
import tempfile
from pathlib import Path

from PIL import Image

from compare_render import load, page_count, run, similarity

QPDF_SEVERITY = {0: 0, 3: 1, 2: 2}  # qpdf --check exit codes: ok, warnings, errors


def qpdf_severity(path: Path, timeout: float) -> int:
    result = run(["qpdf", "--check", str(path)], timeout)
    return QPDF_SEVERITY.get(result["status"], 3)


def rotation(path: Path, page: int, timeout: float) -> int | None:
    """The page's rotation as Poppler reads it."""
    result = run(["pdfinfo", "-f", str(page), "-l", str(page), str(path)], timeout)
    match = re.search(rf"Page\s+{page} rot:\s+(\d+)", result["output"])
    return int(match.group(1)) if match else None


def draw(path: Path, page: int, out: Path, dpi: int, timeout: float) -> Image.Image | None:
    stem = out / f"{path.stem}-{page}"
    run(["pdftoppm", "-r", str(dpi), "-f", str(page), "-l", str(page), "-png", "-singlefile", str(path), str(stem)], timeout)
    return load(Path(f"{stem}.png"))


def check_file(args: argparse.Namespace, path: Path, work: Path) -> dict:
    result: dict = {"file": str(path), "checks": [], "outcome": "pass"}
    count = page_count(path)
    if not count:
        result["outcome"] = "no reference"
        return result
    digest = int(hashlib.sha256(f"{args.seed}:{path.name}".encode()).hexdigest(), 16)
    k = 1 + digest % count  # the page the operations use
    others = sorted({1 + (digest >> 16) % count, 1 + (digest >> 32) % count, k})
    original = draw(path, k, work, args.dpi, args.timeout)
    if original is None:
        result["outcome"] = "no reference"
        return result
    base = qpdf_severity(path, args.timeout)
    cpdf = lambda *a: run([args.cpdf, *map(str, a)], args.timeout)  # noqa: E731

    def fail(what: str, detail: str) -> None:
        result["outcome"] = "fail"
        result["checks"].append({"check": what, "detail": detail[:300]})

    def output_ok(name: str, out: Path) -> bool:
        if not out.exists():
            fail(name, "no output")
            return False
        severity = qpdf_severity(out, args.timeout)
        if severity > base:
            fail(name, f"qpdf --check severity {severity} > original's {base}")
        return True

    def same(name: str, out: Path, page: int, source_page: int) -> None:
        picture = draw(out, page, work, args.dpi, args.timeout)
        expected = original if source_page == k else draw(path, source_page, work, args.dpi, args.timeout)
        if picture is None or expected is None:
            fail(name, f"page {page} not drawn")
            return
        if abs(picture.width - expected.width) > 2 or abs(picture.height - expected.height) > 2:
            fail(name, f"page {page}: {picture.size} vs {expected.size}")
            return
        value = similarity(picture, expected, args.blur, args.tolerance)
        result.setdefault("similarity", []).append(round(value, 4))
        if value < args.threshold:
            fail(name, f"page {page} similarity {value:.3f}")

    sound = work / f"{path.stem}-sound.pdf"
    saved = cpdf("save", path, sound)
    if saved["status"] != 0:
        fail("save", saved["output"])
        return result

    before = rotation(path, k, args.timeout) or 0

    def rotated_right(name: str, out: Path, degrees: int, incremental: bool) -> None:
        if rotation(out, k, args.timeout) != (before + degrees) % 360:
            fail(name, f"page {k} rotation {rotation(out, k, args.timeout)}, {(before + degrees) % 360} expected")
        back = work / f"{path.stem}-back.pdf"
        r = cpdf("rotate", *(["--incremental"] if incremental else []), out, k, -degrees, back)
        if r["status"] != 0:
            fail(name, "rotating back: " + r["output"])
        else:
            same(name, back, k, k)

    for degrees, incremental in ((90, False), (180, True)):
        name = "rotate --incremental" if incremental else "rotate"
        rotated = work / f"{path.stem}-rotated.pdf"
        r = cpdf("rotate", *(["--incremental"] if incremental else []), sound, k, degrees, rotated)
        if r["status"] != 0:
            fail(name, r["output"])
        elif output_ok(name, rotated):
            rotated_right(name, rotated, degrees, incremental)

    order = list(reversed(others))
    arranged = work / f"{path.stem}-arranged.pdf"
    r = cpdf("arrange", sound, ",".join(map(str, order)), arranged)
    if r["status"] != 0:
        fail("arrange", r["output"])
    elif output_ok("arrange", arranged):
        if page_count(arranged) != len(order):
            fail("arrange", f"{page_count(arranged)} pages, {len(order)} expected")
        for i, source in enumerate(order):
            same("arrange", arranged, i + 1, source)

    extracted = work / f"{path.stem}-extracted.pdf"
    r = cpdf("extract", sound, k, extracted)
    if r["status"] != 0:
        fail("extract", r["output"])
    elif output_ok("extract", extracted):
        same("extract", extracted, 1, k)
        result["extract_bytes"] = [extracted.stat().st_size, sound.stat().st_size]

        merged = work / f"{path.stem}-merged.pdf"
        r = cpdf("merge", merged, extracted, sound)
        if r["status"] != 0:
            fail("merge", r["output"])
        elif output_ok("merge", merged):
            if page_count(merged) != count + 1:
                fail("merge", f"{page_count(merged)} pages, {count + 1} expected")
            same("merge", merged, 1, k)
            same("merge", merged, 1 + k, k)

    for leftover in work.glob(f"{path.stem}-*"):
        leftover.unlink()
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="+", help="directories of PDF files")
    parser.add_argument("--cpdf", required=True)
    parser.add_argument("--files", type=int, default=200)
    parser.add_argument("--seed", default="capriole-phase-8a")
    parser.add_argument("--dpi", type=int, default=50)
    parser.add_argument("--blur", type=float, default=1.0)
    parser.add_argument("--tolerance", type=int, default=32)
    parser.add_argument("--threshold", type=float, default=0.99)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="pages")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())

    files = [p for d in args.corpus
             for p in sorted({f.resolve() for f in Path(d).glob("*.pdf")}, key=lambda f: hashlib.sha256(f"{args.seed}:{f.name}".encode()).hexdigest())]
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="pages-", dir=report))

    results: list[dict] = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool, open(report / "results.jsonl", "w") as log:
        running = set()

        def collect(futures) -> None:
            for future in futures:
                r = future.result()
                log.write(json.dumps(r) + "\n")
                results.append(r)

        for path in files:
            running.add(pool.submit(check_file, args, path, work))
            if len(running) >= args.jobs * 2:
                done, running = concurrent.futures.wait(running, return_when=concurrent.futures.FIRST_COMPLETED)
                collect(done)
            if sum(r["outcome"] != "no reference" for r in results) >= args.files:
                break
        collect(concurrent.futures.as_completed(running))
    shutil.rmtree(work)

    counted = [r for r in results if r["outcome"] != "no reference"][: args.files]
    passed = [r for r in counted if r["outcome"] == "pass"]
    rate = len(passed) / len(counted) if counted else 0
    failures = collections.Counter(c["check"] for r in counted for c in r["checks"])
    sizes = [r["extract_bytes"] for r in counted if "extract_bytes" in r]
    lines = [f"# Page operations {args.name} ({stamp})", "",
             f"Files: **{len(counted)}** (seed `{args.seed}`; pages drawn by pdftoppm at {args.dpi} dpi, similarity ≥ {args.threshold}); "
             f"left out (Poppler cannot draw them): {sum(r['outcome'] == 'no reference' for r in results)}.", "",
             f"- **Pass (every operation, qpdf --check no worse, pages identical): {len(passed)} / {len(counted)} = {rate:.1%}** — "
             f"exit criterion: every file: **{'met' if passed and len(passed) == len(counted) else 'not met'}**",
             f"- Extracting one page: on average {sum(e for e, _ in sizes) / max(sum(s for _, s in sizes), 1):.1%} of the file's bytes",
             "", "## Failed checks", ""]
    lines += [f"- {check}: {n}" for check, n in failures.most_common()] or ["- none"]
    lines += ["", "## Failing files", ""]
    for r in counted:
        if r["outcome"] == "fail":
            lines.append(f"- {Path(r['file']).name}: " + "; ".join(f"{c['check']}: {c['detail']}" for c in r["checks"])[:400])
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:8]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
