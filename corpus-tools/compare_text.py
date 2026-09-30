#!/usr/bin/env python3
"""Comparison of Capriole's text extraction with reference engines (PLAN.md phase 7, exit criterion).

A sample of pages — one per file, chosen by a hash of the seed, the file name and the page number (the
same sample at every run, not only first pages); when the files are fewer than --pages, further rounds
take another page from the files that have one — is extracted by Capriole (`cpdf text`, with the resource pack when there is one) and by two reference
engines used strictly as external programs: Poppler (`pdftotext`, its reading order mode) and MuPDF
(`mutool draw -F txt`).

The texts are normalized (Unicode NFKC, soft hyphens dropped) and split into words at white space. Two
measures against a reference:
- content: the F1 of the words as multisets (both what we find and what we miss count);
- reading order: the similarity of the word sequences (difflib's ratio, 2·matches / total).
A page passes when, against at least one reference, content >= --content (0.98) AND order >= --order
(0.90); strictly when against both. Pages where no reference finds any word (scans, pictures) are left
out and counted; pages the references read and Capriole does not, fail. Exit criterion of phase 7: pass
rate >= 95% on 1,000 pages.

    corpus-tools/compare_text.py ../CORPORA/safedocs/0000 --cpdf build/release/tools/cpdf/cpdf \\
        --report-dir ../CORPORA/reports --name text-safedocs-0000

Outputs, under --report-dir/<name>-<timestamp>/: results.jsonl (one object per page), summary.md (rates,
the references' own agreement, failures by kind, the worst pages, regressions against the previous run
of the same --name) and worst/ (the three texts of the worst pages).

Each tool runs in its own process from an empty directory, with a timeout and a memory cap. Needs qpdf,
poppler-utils, mupdf-tools, prlimit. Run it at low intensity (nice, --jobs 2 or 3).
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import datetime
import difflib
import hashlib
import json
import shutil
import tempfile
import unicodedata
from pathlib import Path

from compare_render import page_count, run


def words_of(text: str) -> list[str]:
    return unicodedata.normalize("NFKC", text).replace("­", "").split()


def content_f1(ours: list[str], reference: list[str]) -> float:
    if not ours and not reference:
        return 1.0
    common = sum((collections.Counter(ours) & collections.Counter(reference)).values())
    if common == 0:
        return 0.0
    precision, recall = common / len(ours), common / len(reference)
    return 2 * precision * recall / (precision + recall)


def order_similarity(ours: list[str], reference: list[str]) -> float:
    if not ours and not reference:
        return 1.0
    ids: dict[str, int] = {}
    a = [ids.setdefault(w, len(ids)) for w in ours]
    b = [ids.setdefault(w, len(ids)) for w in reference]
    return difflib.SequenceMatcher(None, a, b, autojunk=False).ratio()


def read(path: Path) -> str | None:
    try:
        return path.read_text("utf-8", "replace")
    except OSError:
        return None


def compare(args: argparse.Namespace, path: Path, page: int, out: Path) -> dict:
    name = f"{path.stem}-p{page}"
    ours, poppler, mupdf = out / f"{name}.txt", out / f"{name}-pdftotext.txt", out / f"{name}-mutool.txt"
    cmd = [args.cpdf, "text", "--order", args.reading_order] + (["--resources", args.resources] if args.resources else []) + [str(path), str(page)]
    c = run(cmd, args.timeout)
    if c["status"] == 0:
        ours.write_text(c["output"], "utf-8")
    run(["pdftotext", "-enc", "UTF-8", "-f", str(page), "-l", str(page), str(path), str(poppler)], args.timeout)
    run(["mutool", "draw", "-q", "-F", "txt", "-o", str(mupdf), str(path), str(page)], args.timeout)
    texts = {"pdftotext": read(poppler), "mutool": read(mupdf)}
    references = {k: words_of(v) for k, v in texts.items() if v is not None}
    mine = words_of(c["output"]) if c["status"] == 0 else []
    result = {"file": str(path), "page": page, "capriole": c["status"], "seconds": c["seconds"], "words": len(mine),
              "reference_words": {k: len(v) for k, v in references.items()}}
    if not any(references.values()):
        result["outcome"] = "no text"
        return result
    scores = {k: {"content": content_f1(mine, v), "order": order_similarity(mine, v)} for k, v in references.items() if v}
    result["scores"] = scores
    passing = [k for k, s in scores.items() if s["content"] >= args.content and s["order"] >= args.order]
    result["outcome"] = "pass" if passing else "fail"
    result["strict"] = len(passing) == len(scores) == 2
    best = max(scores.values(), key=lambda s: (min(s["content"] / args.content, s["order"] / args.order)))
    result["best"] = best
    result["kind"] = "pass" if passing else ("nothing extracted" if not mine else "content" if best["content"] < args.content else "order")
    if len(references) == 2 and all(references.values()):
        p, m = references["pdftotext"], references["mutool"]
        result["references"] = {"content": content_f1(p, m), "order": order_similarity(p, m)}
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="+", help="directories of PDF files")
    parser.add_argument("--cpdf", required=True)
    parser.add_argument("--resources", default="build/resources" if Path("build/resources/cmap").is_dir() else None)
    parser.add_argument("--pages", type=int, default=1000)
    parser.add_argument("--seed", default="capriole-phase-7")
    parser.add_argument("--content", type=float, default=0.98)
    parser.add_argument("--order", type=float, default=0.90)
    parser.add_argument("--reading-order", default="content", choices=["content", "layout"], help="Capriole's reading order (cpdf text --order)")
    parser.add_argument("--rounds", type=int, default=3, help="pages per file at most, when the files are too few")
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--keep-worst", type=int, default=30)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="text")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())
    args.resources = str(Path(args.resources).resolve()) if args.resources else None

    files = [p for d in args.corpus
             for p in sorted({f.resolve() for f in Path(d).glob("*.pdf")}, key=lambda f: hashlib.sha256(f"{args.seed}:{f.name}".encode()).hexdigest())]
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="texts-", dir=report))  # removed at the end: only worst/ is kept

    counts: dict[Path, int | None] = {}

    def job(path: Path, round_: int) -> dict | None:
        if path not in counts:
            counts[path] = page_count(path)
        count = counts[path] or 1
        if round_ >= count:
            return None
        # The pages of the file in the order of their hashes: round r takes the r-th, so rounds never repeat a page.
        pages = sorted(range(1, count + 1), key=lambda n: hashlib.sha256(f"{args.seed}:{path.name}:{n}".encode()).hexdigest())
        return compare(args, path, pages[round_], work)

    results: list[dict] = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool, open(report / "results.jsonl", "w") as log:
        running = set()

        def collect(futures) -> None:
            for future in futures:
                r = future.result()
                if r is not None:
                    log.write(json.dumps(r) + "\n")
                    results.append(r)

        enough = lambda: sum(r["outcome"] != "no text" for r in results) >= args.pages  # noqa: E731
        for round_ in range(args.rounds):
            for path in files:  # keep a window of jobs; stop once enough pages were compared
                running.add(pool.submit(job, path, round_))
                if len(running) >= args.jobs * 2:
                    done, running = concurrent.futures.wait(running, return_when=concurrent.futures.FIRST_COMPLETED)
                    collect(done)
                if enough():
                    break
            collect(concurrent.futures.as_completed(running))
            running = set()
            if enough():
                break

    counted = [r for r in results if r["outcome"] != "no text"][: args.pages]
    passed = [r for r in counted if r["outcome"] == "pass"]
    strict = [r for r in counted if r.get("strict")]
    with_both = [r for r in counted if r.get("references")]
    agree = [r for r in with_both if r["references"]["content"] >= args.content and r["references"]["order"] >= args.order]
    failures = [r for r in counted if r["outcome"] == "fail"]
    rank = lambda r: min(r["best"]["content"] / args.content, r["best"]["order"] / args.order)  # noqa: E731
    worst = sorted(counted, key=rank)[: args.keep_worst]

    (report / "worst").mkdir()
    for r in worst:
        stem = f"{Path(r['file']).stem}-p{r['page']}"
        for suffix in ("", "-pdftotext", "-mutool"):
            if (work / f"{stem}{suffix}.txt").exists():
                shutil.copy(work / f"{stem}{suffix}.txt", report / "worst" / f"{stem}{suffix or '-capriole'}.txt")
    shutil.rmtree(work)

    rate = len(passed) / len(counted) if counted else 0
    mean = lambda key: sum(r["best"][key] for r in counted) / max(len(counted), 1)  # noqa: E731
    lines = [f"# Text comparison {args.name} ({stamp})", "",
             f"Pages compared: **{len(counted)}** (sample seed `{args.seed}`; content F1 ≥ {args.content} and order ≥ {args.order} against the "
             f"same reference); left out (no reference finds text): {sum(r['outcome'] == 'no text' for r in results)}.",
             "", f"- **Pass (against at least one reference): {len(passed)} / {len(counted)} = {rate:.1%}** — exit criterion ≥ 95%: "
             f"**{'met' if rate >= 0.95 else 'not met'}**",
             f"- Strict (against both references): {len(strict)} / {len(counted)} = {len(strict) / max(len(counted), 1):.1%}",
             f"- The references agree with each other on {len(agree)} / {len(with_both)} = {len(agree) / max(len(with_both), 1):.1%} of the pages",
             f"- Mean of the best reference: content {mean('content'):.3f}, order {mean('order'):.3f}",
             f"- Capriole failed: {sum(r['capriole'] != 0 for r in counted)}; timeouts: {sum(r['capriole'] == 'timeout' for r in counted)}",
             "", "## Failures by kind", ""]
    for label, n in collections.Counter(r["kind"] for r in failures).most_common():
        lines.append(f"- {label}: {n}")
    lines += ["", f"## The {len(worst)} worst pages (worst/: the three texts)", "",
              "| page | words (ours / pdftotext / mutool) | pdftotext content · order | mutool content · order | references |", "|---|---|---|---|---|"]
    fmt = lambda s: "—" if not s else f"{s['content']:.3f} · {s['order']:.3f}"  # noqa: E731
    for r in worst:
        rw = r["reference_words"]
        lines.append(f"| {Path(r['file']).name} p{r['page']} | {r['words']} / {rw.get('pdftotext', '—')} / {rw.get('mutool', '—')} | "
                     f"{fmt(r['scores'].get('pdftotext'))} | {fmt(r['scores'].get('mutool'))} | {fmt(r.get('references'))} |")
    previous = sorted(p for p in Path(args.report_dir).resolve().glob(f"{args.name}-*") if p != report and (p / "results.jsonl").exists())
    if previous:
        before = {(r["file"], r["page"]): r for r in map(json.loads, open(previous[-1] / "results.jsonl"))}
        regressed = [r for r in counted if before.get((r["file"], r["page"]), {}).get("outcome") == "pass" and r["outcome"] == "fail"]
        lines += ["", f"## Regressions against {previous[-1].name}: {len(regressed)}", ""]
        lines += [f"- {Path(r['file']).name} p{r['page']}: {fmt(r.get('best'))}" for r in regressed]
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:10]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
