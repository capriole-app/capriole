#!/usr/bin/env python3
"""Editing text as other readers see it (PLAN.md phase 8j, exit criterion).

On --pages pages of the corpora (the first page of a file with at least 8 words on it, in hash order of
--seed; each file first saved fully, as the apps do with files that needed repairs), one word is replaced
(`cpdf edit-text`, the C API: its box as Capriole's text gives it, grown by a third of a point): the
longest word of letters only that the page shows once, by turns with a word of the same letters moved round
(the same length: the same font can usually write it) and with a longer one ("Q" + the word + "Z", letters a
subset font often lacks). Then:

1. Capriole (`cpdf text --words`), Poppler (`pdftotext -bbox`) and MuPDF (`mutool draw -F stext`) read the
   new word on the line where the old one was (the line's text, spaces aside, holds it), and no longer
   read the old one there;
2. the text of the other lines is the same for Capriole and Poppler (their words, as multisets);
3. outside the line the page draws as before in Capriole and Poppler (similarity of compare_render.py >=
   --threshold, the line's band painted white in both);
4. `qpdf --check` is no worse.

How each word was written — the same font, or a similar standard one — is counted. Pages whose word
`edit-text` refuses by design (a scan's invisible text layer, an annotation's text) are counted apart and
replaced by further pages.

Exit criterion of phase 8j (engine): every page passes.

    corpus-tools/check_text_edit.py ../CORPORA/safedocs/0000 ../CORPORA/unsafe/x002 ../CORPORA/unsafe/x008 \\
        --cpdf build/release/tools/cpdf/cpdf --report-dir ../CORPORA/reports --name text-edit-all
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

from check_pages import qpdf_severity
from check_redaction import capriole_words, masked, mupdf_chars, poppler_words
from compare_render import load, run, similarity


def band_text(items: list[tuple[str, list[float]]], band: tuple[float, float]) -> str:
    """The text of what lies on a line (its centre within the band), left to right, spaces aside."""
    on = [(b[0], t) for t, b in items if band[0] <= (b[1] + b[3]) / 2 <= band[1]]
    return "".join(t for _, t in sorted(on)).replace(" ", "")


def difference(a: collections.Counter, b: collections.Counter) -> str:
    """What one multiset has that the other has not, both ways."""
    return f"gone {list((a - b).items())[:4]}, new {list((b - a).items())[:4]}"


def outside(items: list[tuple[str, list[float]]], band: tuple[float, float]) -> collections.Counter:
    return collections.Counter(t for t, b in items if not band[0] - 1 <= (b[1] + b[3]) / 2 <= band[1] + 1)


def draw(args: argparse.Namespace, path: Path, out: Path, engine: str, page: int = 1) -> object:
    if engine == "capriole":
        run([args.cpdf, "render", "--dpi", str(args.dpi), str(path), str(page), str(out)], args.timeout)
        return load(out)
    run(["pdftoppm", "-cropbox", "-r", str(args.dpi), "-f", str(page), "-l", str(page), "-png", "-singlefile", str(path), str(out.with_suffix(""))],
        args.timeout)
    return load(out)


def check_page(args: argparse.Namespace, path: Path, index: int, work_root: Path) -> dict:
    work = Path(tempfile.mkdtemp(dir=work_root))
    record: dict = {"file": str(path), "checks": []}
    try:
        sound = work / "sound.pdf"
        run([args.cpdf, "save", str(path), str(sound)], args.timeout)
        if not sound.exists():
            return {**record, "outcome": "skipped", "why": "does not open"}
        words = capriole_words(args, sound, 1)
        if not words or len(words) < 8:
            return {**record, "outcome": "skipped", "why": "too little text"}
        counts = collections.Counter(t for t, _ in words)
        candidates = [(t, b) for t, b in words if counts[t] == 1 and len(t) >= 4 and re.fullmatch(r"[A-Za-z]+", t)]
        if not candidates:
            return {**record, "outcome": "skipped", "why": "no word of letters shown once"}
        old, box = max(candidates, key=lambda w: (len(w[0]), w[0]))
        new = old[1:] + old[0] if index % 2 == 0 else "Q" + old + "Z"
        if new == old:
            new = old[::-1]
        record.update({"old": old, "new": new})
        area = ",".join(f"{v:.3f}" for v in (box[0] - 0.33, box[1] - 0.33, box[2] + 0.33, box[3] + 0.33))
        out = work / "edited.pdf"
        edited = run([args.cpdf, "edit-text", str(sound), "1", area, new, str(out)], args.timeout)
        refusal = re.search(r"the text there is (invisible|an annotation's)", edited["output"])
        if edited["status"] != 0 and refusal:
            return {**record, "outcome": "refused", "why": refusal.group(0)}
        if edited["status"] != 0 or not out.exists():
            return {**record, "outcome": "fail", "checks": [{"check": "edit-text", "detail": edited["output"].strip()[-300:]}]}
        report = json.loads(edited["output"].strip().splitlines()[0])
        record.update({"method": report["method"], "font": report["font"], "scale": report["scale"]})
        fail = record["checks"]
        band = (box[1] - 0.5, box[3] + 0.5)

        # 1. The new word on the line, the old one gone from it.
        after_words = capriole_words(args, out, 1) or []
        line = band_text(after_words, band)
        if new not in line:
            fail.append({"check": "capriole", "detail": f"line reads {line[:120]!r}"})
        if any(t == old and abs(b[0] - box[0]) < 1 for t, b in after_words):
            fail.append({"check": "capriole", "detail": "the old word is still there"})
        for engine, read in (("poppler", lambda p: poppler_words(p, 1, args.timeout)), ("mupdf", lambda p: mupdf_chars(p, 1, args.timeout))):
            before_items, after_items = read(sound) or [], read(out) or []
            before_line, after_line = band_text(before_items, band), band_text(after_items, band)
            if old not in before_line:
                record.setdefault("notes", []).append(f"{engine} reads the line otherwise before: {before_line[:80]!r}")
                continue
            if new not in after_line:
                fail.append({"check": engine, "detail": f"line reads {after_line[:120]!r}"})
        # 2. The other lines as they were.
        if outside(words, band) != outside(after_words, band):
            fail.append({"check": "capriole other lines", "detail": difference(outside(words, band), outside(after_words, band))})
        p_before, p_after = poppler_words(sound, 1, args.timeout) or [], poppler_words(out, 1, args.timeout) or []
        if outside(p_before, band) != outside(p_after, band):
            fail.append({"check": "poppler other lines", "detail": difference(outside(p_before, band), outside(p_after, band))})
        # 3. The drawing outside the line.
        scale = args.dpi / 72
        line_area = [[0, band[0], 10_000, band[1]]]
        for engine in ("capriole", "poppler"):
            a = masked(draw(args, sound, work / f"{engine}-before.png", engine), line_area, scale)
            b = masked(draw(args, out, work / f"{engine}-after.png", engine), line_area, scale)
            if a is None or b is None:
                fail.append({"check": f"{engine} drawing", "detail": "not drawn"})
                continue
            s = similarity(a, b, args.blur, args.tolerance)
            if s < args.threshold:
                fail.append({"check": f"{engine} drawing", "detail": f"similarity {s:.4f} outside the line"})
        # 4. qpdf.
        if qpdf_severity(out, args.timeout) > qpdf_severity(sound, args.timeout):
            fail.append({"check": "qpdf", "detail": "qpdf --check worse than before"})
        record["outcome"] = "fail" if fail else "pass"
        return record
    except Exception as error:  # noqa: BLE001 — a crash of the check is a failure of the page, reported
        return {**record, "outcome": "fail", "checks": [{"check": "exception", "detail": repr(error)[:300]}]}
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="+")
    parser.add_argument("--cpdf", required=True)
    parser.add_argument("--pages", type=int, default=100)
    parser.add_argument("--seed", default="capriole-phase-8j")
    parser.add_argument("--dpi", type=int, default=72)
    parser.add_argument("--blur", type=float, default=1.0)
    parser.add_argument("--tolerance", type=int, default=32)
    parser.add_argument("--threshold", type=float, default=0.99)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="text-edit")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="text-edit-", dir=report))
    files = [p.resolve() for d in args.corpus for p in Path(d).glob("*.pdf")]
    files.sort(key=lambda f: hashlib.sha256(f"{args.seed}:{f.name}".encode()).hexdigest())
    results: list[dict] = []
    refused: collections.Counter = collections.Counter()
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool, open(report / "results.jsonl", "w") as log:
        for start in range(0, len(files), args.jobs * 4):
            if len(results) >= args.pages:
                break
            batch = files[start:start + args.jobs * 4]
            for r in pool.map(lambda pair: check_page(args, pair[1], start + pair[0], work), enumerate(batch)):
                if r["outcome"] == "refused":
                    refused[r["why"]] += 1
                elif r["outcome"] != "skipped" and len(results) < args.pages:
                    results.append(r)
                    log.write(json.dumps(r) + "\n")
    shutil.rmtree(work)

    passed = [r for r in results if r["outcome"] == "pass"]
    methods = collections.Counter(r.get("method", "?") for r in results)
    met = len(results) >= args.pages and len(passed) == len(results)
    notes = collections.Counter(n.split(":")[0] for r in results for n in r.get("notes", []))
    lines = [f"# Text edit {args.name} ({stamp})", "",
             f"- **Pages (the new word read on its line by Capriole, Poppler and MuPDF, the old one gone, the other lines and the drawing "
             f"outside the line as before, qpdf no worse): {len(passed)} / {len(results)}**",
             f"- Written in the same font: {methods.get('sameFont', 0)}; in a similar standard font: {methods.get('similarFont', 0)}",
             f"- Refused by design, not counted: {', '.join(f'{n} × {k}' for k, n in refused.most_common()) or 'none'}",
             f"- Exit criterion (every page): **{'met' if met else 'not met'}**", "", "## Notes", ""]
    lines += [f"- {n} × {k}" for k, n in notes.most_common()] or ["- none"]
    lines += ["", "## Failures", ""]
    lines += [f"- {Path(r['file']).name} ({r.get('old')} → {r.get('new')}, {r.get('method')}): "
              + "; ".join(f"{c['check']}: {c['detail']}" for c in r["checks"])[:700] for r in results if r["outcome"] == "fail"] or ["- none"]
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:7]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
