#!/usr/bin/env python3
"""Pixel comparison of Capriole's rendering with reference engines (PLAN.md phase 6, exit criterion).

A sample of pages — one per file, chosen by a hash of the seed and the file name, so the sample is the
same at every run and not only first pages — is drawn by Capriole (`cpdf render`, with the resource pack
when there is one) and by two reference engines used strictly as external programs: Poppler
(`pdftoppm`) and MuPDF (`mutool draw`), all at the same resolution from the CropBox.

Similarity of two renderings: the share of pixels whose colour differs by at most --tolerance (0–255, per
channel) after a Gaussian blur of --blur pixels on both — the blur absorbs anti-aliasing differences, the
tolerance small colour differences. A page passes when its similarity to at least one reference is at
least --threshold (with two references a majority is one agreeing reference); it passes strictly when
close to both. Pages that no reference can draw are left out (and counted); pages the references draw
and Capriole does not, fail. Exit criterion of phase 6: pass rate >= 95% on 1,000 pages.

    corpus-tools/compare_render.py ../CORPORA/safedocs/0000 --cpdf build/release/tools/cpdf/cpdf \\
        --report-dir ../CORPORA/reports --name render-safedocs-0000

Outputs, under --report-dir/<name>-<timestamp>/: results.jsonl (one object per page), summary.md
(rates, the references' own agreement, failures by category, the worst pages, regressions against the
previous run of the same --name) and worst/ (the worst pages side by side: Capriole | pdftoppm | mutool);
the other renderings are deleted.

Each tool runs in its own process from an empty directory, with a timeout and a memory cap. Needs
Pillow, qpdf, poppler-utils, mupdf-tools, prlimit. Run it at low intensity (nice, --jobs 2 or 3).
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
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter

MEMORY_CAP_BYTES = 4 * 1024**3

# Capriole's warnings that name a cause, for the failure categories (first match wins).
CATEGORIES = [
    ("optional content", r"optional content|/OC"),
    ("fonts", r"font|glyph|CMap|Type 3"),
    ("images", r"image"),
    ("shadings and patterns", r"shading|pattern"),
    ("transparency", r"soft mask|knockout|blend|group"),
    ("annotations", r"annotation|/Widget|/FreeText"),
    ("colour", r"colour|color|ICC|tint"),
    ("content", r"operator|content stream|syntax"),
]


def run(cmd: list[str], timeout: float) -> dict:
    """cmd in an empty working directory, with a memory cap (prlimit) and a timeout."""
    with tempfile.TemporaryDirectory() as cwd:
        start = datetime.datetime.now()
        try:
            proc = subprocess.run(["prlimit", f"--as={MEMORY_CAP_BYTES}", "--", *cmd], cwd=cwd, stdin=subprocess.DEVNULL,
                                  capture_output=True, timeout=timeout)
            status = proc.returncode
            out = proc.stdout.decode("utf-8", "replace") + proc.stderr.decode("utf-8", "replace")
        except subprocess.TimeoutExpired:
            status, out = "timeout", ""
    return {"status": status, "output": out, "seconds": round((datetime.datetime.now() - start).total_seconds(), 3)}


def page_count(path: Path) -> int | None:
    result = run(["qpdf", "--show-npages", str(path)], 60)
    match = re.match(r"\s*(\d+)", result["output"])
    return int(match.group(1)) if match and int(match.group(1)) > 0 else None


def similarity(a: Image.Image, b: Image.Image, blur: float, tolerance: int) -> float:
    w, h = min(a.width, b.width), min(a.height, b.height)
    if w == 0 or h == 0:
        return 0.0
    a = a.crop((0, 0, w, h)).filter(ImageFilter.GaussianBlur(blur))
    b = b.crop((0, 0, w, h)).filter(ImageFilter.GaussianBlur(blur))
    channels = ImageChops.difference(a, b).split()
    worst = ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]), channels[2])
    return sum(worst.histogram()[: tolerance + 1]) / (w * h)


def load(path: Path) -> Image.Image | None:
    try:
        with Image.open(path) as image:
            return image.convert("RGB")
    except Exception:  # noqa: BLE001 - a missing or broken output is simply no rendering
        return None


def compare(args: argparse.Namespace, path: Path, page: int, out: Path) -> dict:
    name = f"{path.stem}-p{page}"
    ours, poppler, mupdf = out / f"{name}.png", out / f"{name}-pdftoppm", out / f"{name}-mutool.png"
    cmd = [args.cpdf, "render", "--dpi", str(args.dpi)] + (["--resources", args.resources] if args.resources else []) + [str(path), str(page), str(ours)]
    c = run(cmd, args.timeout)
    run(["pdftoppm", "-cropbox", "-r", str(args.dpi), "-f", str(page), "-l", str(page), "-png", "-singlefile", str(path), str(poppler)], args.timeout)
    run(["mutool", "draw", "-q", "-r", str(args.dpi), "-F", "png", "-o", str(mupdf), str(path), str(page)], args.timeout)
    a, p, m = load(ours), load(Path(f"{poppler}.png")), load(mupdf)
    result = {"file": str(path), "page": page, "capriole": c["status"], "seconds": c["seconds"],
              "warnings": [l.strip() for l in c["output"].splitlines() if "warning:" in l][:20]}
    if p is None and m is None:
        result["outcome"] = "no reference"
        return result
    s_p = similarity(a, p, args.blur, args.tolerance) if a and p else None
    s_m = similarity(a, m, args.blur, args.tolerance) if a and m else None
    result.update({"pdftoppm": s_p, "mutool": s_m, "references": similarity(p, m, args.blur, args.tolerance) if p and m else None})
    best = max(v for v in (s_p, s_m, 0.0) if v is not None)
    both = [v for v in (s_p, s_m) if v is not None]
    result["outcome"] = "pass" if best >= args.threshold else "fail"
    result["strict"] = bool(both) and len(both) == (p is not None) + (m is not None) and min(both) >= args.threshold
    result["best"] = best
    return result


def category(warnings: list[str]) -> str:
    text = " ".join(warnings)
    for label, pattern in CATEGORIES:
        if re.search(pattern, text, re.IGNORECASE):
            return label
    return "no warning (rendering differences)"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="+", help="directories of PDF files")
    parser.add_argument("--cpdf", required=True)
    parser.add_argument("--resources", default="build/resources" if Path("build/resources/cmap").is_dir() else None)
    parser.add_argument("--pages", type=int, default=1000)
    parser.add_argument("--seed", default="capriole-phase-6")
    parser.add_argument("--dpi", type=int, default=72)
    parser.add_argument("--blur", type=float, default=1.0)
    parser.add_argument("--tolerance", type=int, default=32)
    parser.add_argument("--threshold", type=float, default=0.95)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--keep-worst", type=int, default=30)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="render")
    args = parser.parse_args()
    # The tools run from an empty directory: every path absolute.
    args.cpdf = str(Path(args.cpdf).resolve())
    args.resources = str(Path(args.resources).resolve()) if args.resources else None

    # Corpus by corpus (a later one completes the sample when an earlier has too few drawable pages), each
    # in the order of its files' hashes.
    files = [p for d in args.corpus
             for p in sorted({f.resolve() for f in Path(d).glob("*.pdf")}, key=lambda f: hashlib.sha256(f"{args.seed}:{f.name}".encode()).hexdigest())]
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="renders-", dir=report))  # removed at the end: only worst/ is kept

    def job(path: Path) -> dict:
        count = page_count(path)
        digest = int(hashlib.sha256(f"{args.seed}:{path.name}".encode()).hexdigest(), 16)
        page = 1 + (digest % count) if count else 1
        return compare(args, path, page, work)

    results: list[dict] = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool, open(report / "results.jsonl", "w") as log:
        pending = iter(files)
        running = set()
        for path in pending:  # keep a window of jobs; stop once enough pages were compared
            running.add(pool.submit(job, path))
            if len(running) >= args.jobs * 2:
                done, running = concurrent.futures.wait(running, return_when=concurrent.futures.FIRST_COMPLETED)
                for future in done:
                    r = future.result()
                    log.write(json.dumps(r) + "\n")
                    results.append(r)
            if sum(r["outcome"] != "no reference" for r in results) >= args.pages:
                break
        for future in concurrent.futures.as_completed(running):
            r = future.result()
            log.write(json.dumps(r) + "\n")
            results.append(r)

    counted = [r for r in results if r["outcome"] != "no reference"][: args.pages]
    passed = [r for r in counted if r["outcome"] == "pass"]
    strict = [r for r in counted if r.get("strict")]
    agree = [r for r in counted if r.get("references") is not None and r["references"] >= args.threshold]
    with_both = [r for r in counted if r.get("references") is not None]
    failures = [r for r in counted if r["outcome"] == "fail"]
    worst = sorted(counted, key=lambda r: r.get("best", 0))[: args.keep_worst]

    (report / "worst").mkdir()
    for r in worst:
        stem = f"{Path(r['file']).stem}-p{r['page']}"
        images = [load(work / f"{stem}.png"), load(work / f"{stem}-pdftoppm.png"), load(work / f"{stem}-mutool.png")]
        images = [i if i is not None else Image.new("RGB", (100, 100), "red") for i in images]
        sheet = Image.new("RGB", (sum(i.width for i in images) + 20, max(i.height for i in images)), "white")
        x = 0
        for image in images:
            sheet.paste(image, (x, 0))
            x += image.width + 10
        sheet.save(report / "worst" / f"{stem}.png")

    shutil.rmtree(work)
    rate = len(passed) / len(counted) if counted else 0
    lines = [f"# Render comparison {args.name} ({stamp})", "",
             f"Pages compared: **{len(counted)}** (sample seed `{args.seed}`, {args.dpi} dpi, blur {args.blur}, tolerance {args.tolerance}, "
             f"threshold {args.threshold}); left out (no reference could draw them): {sum(r['outcome'] == 'no reference' for r in results)}.",
             "", f"- **Pass (close to at least one reference): {len(passed)} / {len(counted)} = {rate:.1%}** — exit criterion ≥ 95%: "
             f"**{'met' if rate >= 0.95 else 'not met'}**",
             f"- Strict (close to every reference that drew the page): {len(strict)} / {len(counted)} = {len(strict) / max(len(counted), 1):.1%}",
             f"- The references agree with each other on {len(agree)} / {len(with_both)} = {len(agree) / max(len(with_both), 1):.1%} of the pages",
             f"- Capriole did not draw: {sum(r['capriole'] != 0 for r in counted)}; timeouts: {sum(r['capriole'] == 'timeout' for r in counted)}",
             "", "## Failures by category (from Capriole's warnings)", ""]
    for label, n in collections.Counter(category(r["warnings"]) for r in failures).most_common():
        lines.append(f"- {label}: {n}")
    lines += ["", f"## The {len(worst)} worst pages (worst/: Capriole | pdftoppm | mutool)", "", "| page | pdftoppm | mutool | references | warning |", "|---|---|---|---|---|"]
    fmt = lambda v: "—" if v is None else f"{v:.3f}"  # noqa: E731
    for r in worst:
        lines.append(f"| {Path(r['file']).name} p{r['page']} | {fmt(r.get('pdftoppm'))} | {fmt(r.get('mutool'))} | {fmt(r.get('references'))} | "
                     f"{(r['warnings'][0] if r['warnings'] else '').replace('|', '/')[:90]} |")
    previous = sorted(p for p in Path(args.report_dir).resolve().glob(f"{args.name}-*") if p != report and (p / "results.jsonl").exists())
    if previous:
        before = {(r["file"], r["page"]): r for r in map(json.loads, open(previous[-1] / "results.jsonl"))}
        regressed = [r for r in counted if before.get((r["file"], r["page"]), {}).get("outcome") == "pass" and r["outcome"] == "fail"]
        lines += ["", f"## Regressions against {previous[-1].name}: {len(regressed)}", ""]
        lines += [f"- {Path(r['file']).name} p{r['page']}: {fmt(r.get('best'))}" for r in regressed]
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:9]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
