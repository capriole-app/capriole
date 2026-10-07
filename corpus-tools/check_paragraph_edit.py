#!/usr/bin/env python3
"""Editing paragraphs, laid out anew, as other readers see it (PLAN.md phase 9a, exit criterion).

On --pages pages of the corpora (the first page of a file, in hash order of --seed; each file first saved
fully, as the apps do with files that needed repairs), the longest block of at least three lines and
fifteen words of Capriole's text (as laid out), in one font and size and with no other text in its box, is
written anew (`cpdf edit-paragraph`, the C API): by
turns longer — a phrase put in after its second word, so that every line after it moves — and shorter —
its middle third left out. Then, in the region the paragraph takes now:

1. Capriole (`cpdf text --json`), Poppler (`pdftotext -bbox`) and MuPDF (`mutool draw -F stext`) read the
   new text there and nothing else (letters and digits compared, in order);
2. outside it, the text is the same for Capriole and Poppler (their words, as multisets);
3. outside it, the page draws as before in Capriole and Poppler (similarity of compare_render.py >=
   --threshold, the region painted white in both);
4. the new lines keep to the paragraph's margins; a justified paragraph's lines but the last end at its
   right margin (within 1 point);
5. `qpdf --check` is no worse.

Paragraphs `edit-paragraph` refuses by design (more than one style, invisible or an annotation's text, not
horizontal, characters neither the paragraph's font nor a standard one can write) are counted apart and replaced by further pages; new text with no room below is counted
apart and tried again with --shrink. How each was written — the same font or a similar one —, and its
alignment, are counted.

--mixed (phase 9b): paragraphs of at least two styles (fonts or sizes) instead of one; and, besides the
above, every word the new text keeps from the old (one shown once in both, in one style) has its style
still: the same font, or — written in a similar one — the same class (regular, bold, italic, bold italic,
from the font's name and descriptor), and its size (times the shrinking).

Exit criterion of phases 9a and 9b (engine): every page passes, without and with --mixed.

    corpus-tools/check_paragraph_edit.py ../CORPORA/safedocs/0000 ../CORPORA/unsafe/x002 ../CORPORA/unsafe/x008 \\
        --cpdf build/release/tools/cpdf/cpdf --report-dir ../CORPORA/reports --name paragraph-edit-all
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
import unicodedata
from pathlib import Path

from check_pages import qpdf_severity
from check_redaction import masked, mupdf_chars, poppler_words
from check_text_edit import difference, draw
from compare_render import run, similarity


def letters(text: str) -> str:
    """Letters and digits only, compatibility forms unfolded: what every reader agrees on."""
    return "".join(c for c in unicodedata.normalize("NFKC", text) if c.isalnum())


def capriole_text(args: argparse.Namespace, path: Path) -> dict | None:
    result = run([args.cpdf, "text", "--json", "--order", "layout", str(path), "1"], args.timeout)
    if result["status"] != 0:
        return None
    try:
        return json.loads(result["output"].strip().splitlines()[-1])
    except (json.JSONDecodeError, IndexError):
        return None


def capriole_items(page: dict) -> list[tuple[str, list[float]]]:
    """The words of Capriole's text, with their boxes."""
    chars = page["chars"]
    out = []
    for x0, y0, x1, y1, first, count in page["words"]:
        out.append(("".join(chars[c][0] for c in range(first, first + count)), [x0, y0, x1, y1]))
    return out


def in_region(box: list[float], region: list[float]) -> bool:
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    return region[0] <= cx <= region[2] and region[1] <= cy <= region[3]


def read_in(items: list[tuple[str, list[float]]], region: list[float]) -> str:
    """The letters of what lies in a region, top to bottom, left to right: words whose centres are within
    half a word's height of each other are on one line (words of different fonts on one baseline have boxes
    of different heights)."""
    inside = sorted(((b[1] + b[3]) / 2, b[0], b[3] - b[1], t) for t, b in items if in_region(b, region))
    lines: list[list[tuple[float, str]]] = []
    centre = None
    for cy, x, height, t in inside:
        if centre is None or cy - centre > max(height, 1) / 2:
            lines.append([])
            centre = cy
        lines[-1].append((x, t))
    return letters("".join(t for line in lines for _, t in sorted(line)))


def outside(items: list[tuple[str, list[float]]], region: list[float]) -> collections.Counter:
    """The words outside a region (blank ones aside: space glyphs left where text was are no text)."""
    return collections.Counter(t for t, b in items if t.strip() and not in_region(b, region))


def font_class(args: argparse.Namespace, path: Path, ref: list | None, cache: dict) -> str:
    """Regular, bold, italic or bold italic: from the font's name, and its descriptor's weight, flags and angle."""
    if ref is None:
        return "?"
    key = (str(path), tuple(ref))
    if key in cache:
        return cache[key]
    font = run([args.cpdf, "object", str(path), str(ref[0]), str(ref[1])], args.timeout)["output"]
    name = re.search(r"/BaseFont\s*/([^\s/<>\[\]()]+)", font)
    base = re.sub(r"^[A-Z]{6}\+", "", name.group(1)) if name else ""
    descriptor = re.search(r"/FontDescriptor\s+(\d+)\s+(\d+)\s+R", font)
    if not descriptor and "/DescendantFonts" in font:
        child = re.search(r"/DescendantFonts\s*\[\s*(\d+)\s+(\d+)\s+R", font)
        if child:
            sub = run([args.cpdf, "object", str(path), child.group(1), child.group(2)], args.timeout)["output"]
            descriptor = re.search(r"/FontDescriptor\s+(\d+)\s+(\d+)\s+R", sub)
    weight, flags, angle = 400.0, 0, 0.0
    if descriptor:
        d = run([args.cpdf, "object", str(path), descriptor.group(1), descriptor.group(2)], args.timeout)["output"]
        if m := re.search(r"/FontWeight\s+([\d.]+)", d):
            weight = float(m.group(1))
        if m := re.search(r"/Flags\s+(\d+)", d):
            flags = int(m.group(1))
        if m := re.search(r"/ItalicAngle\s+(-?[\d.]+)", d):
            angle = float(m.group(1))
    lower = base.lower()
    bold = any(k in lower for k in ("bold", "black", "heavy", "semibold", "demi")) or weight >= 600 or bool(flags & (1 << 18))
    italic = any(k in lower for k in ("italic", "oblique")) or angle != 0 or bool(flags & (1 << 6))
    cache[key] = ("bold " if bold else "") + ("italic" if italic else "regular" if not bold else "")
    cache[key] = cache[key].strip()
    return cache[key]


def word_styles(page: dict, region: list[float]) -> dict[str, list]:
    """Each word in a region shown once there, in one style: its font and size (else left out)."""
    chars = page["chars"]
    seen: dict[str, list] = {}
    twice = set()
    for x0, y0, x1, y1, first, count in page["words"]:
        if not in_region([x0, y0, x1, y1], region):
            continue
        text = "".join(chars[c][0] for c in range(first, first + count))
        styles = {(json.dumps(chars[c][12]), round(chars[c][5], 2)) for c in range(first, first + count)}
        if text in seen or len(styles) != 1:
            twice.add(text)
        seen[text] = [chars[first][12], chars[first][5]]
    return {t: v for t, v in seen.items() if t not in twice}


def choose(page: dict, mixed: bool = False) -> tuple[list[float], str, int] | None:
    """The longest block of at least 3 lines and 15 words, horizontal and visible: its box, text, lines."""
    best = None
    chars = page["chars"]
    for x0, y0, x1, y1, first, count in page["blocks"]:
        if count < 3:
            continue
        lines = page["lines"][first:first + count]
        words = [w for l in lines for w in page["words"][l[4]:l[4] + l[5]]]
        if len(words) < 15:
            continue
        members = {c for w in words for c in range(w[4], w[4] + w[5])}
        cs = [chars[c] for c in members]
        # Horizontal, visible, one font and size (the engine refuses mixed styles by design).
        styles = len({(json.dumps(c[12]), round(c[5], 1)) for c in cs})
        if any(c[6] != 0 or c[7] == 1 for c in cs) or (styles < 2 if mixed else styles != 1):
            continue
        # No other text inside its box (the area would take it too).
        if any(i not in members and in_region(c[1:5], [x0 - 0.5, y0 - 0.5, x1 + 0.5, y1 + 0.5]) for i, c in enumerate(chars)):
            continue
        text = " ".join("".join(chars[c][0] for c in range(w[4], w[4] + w[5])) for w in words)
        if any(unicodedata.category(ch)[0] == "C" for ch in text):
            continue
        if best is None or len(text) > len(best[1]):
            best = ([x0, y0, x1, y1], text, count)
    return best


def check_page(args: argparse.Namespace, path: Path, index: int, work_root: Path) -> dict:
    work = Path(tempfile.mkdtemp(dir=work_root))
    record: dict = {"file": str(path), "checks": []}
    try:
        sound = work / "sound.pdf"
        run([args.cpdf, "save", str(path), str(sound)], args.timeout)
        if not sound.exists():
            return {**record, "outcome": "skipped", "why": "does not open"}
        before = capriole_text(args, sound)
        if not before:
            return {**record, "outcome": "skipped", "why": "no text"}
        chosen = choose(before, args.mixed)
        if not chosen:
            return {**record, "outcome": "skipped", "why": "no paragraph of three lines"}
        box, old, old_lines = chosen
        words = old.split(" ")
        if index % 2 == 0:
            new = " ".join(words[:2] + ["(with", "a", "few", "more", "words", "written", "in", "here)"] + words[2:])
            record["change"] = "longer"
        else:
            third = len(words) // 3
            new = " ".join(words[:third] + words[2 * third:])
            record["change"] = "shorter"
        area = ",".join(f"{v:.3f}" for v in (box[0] - 0.3, box[1] - 0.3, box[2] + 0.3, box[3] + 0.3))
        out = work / "edited.pdf"
        # cpdf reads the text as a JSON string's inside (\n: a line break): backslashes in it doubled.
        argument = new.replace("\\", "\\\\")
        edited = run([args.cpdf, "edit-paragraph", str(sound), "1", area, argument, str(out)], args.timeout)
        if edited["status"] != 0 and "no room" in edited["output"]:
            record["no_room"] = True
            edited = run([args.cpdf, "edit-paragraph", "--shrink", str(sound), "1", area, argument, str(out)], args.timeout)
            if edited["status"] != 0 and "no room" in edited["output"]:
                return {**record, "outcome": "no room", "why": edited["output"].strip()[-160:]}
        refusal = re.search(r"more than one font|invisible|an annotation's|only horizontal|several paragraphs|cannot write|superscripts", edited["output"])
        if edited["status"] != 0 and refusal:
            return {**record, "outcome": "refused", "why": refusal.group(0)}
        if edited["status"] != 0 or not out.exists():
            return {**record, "outcome": "fail", "checks": [{"check": "edit-paragraph", "detail": edited["output"].strip()[-300:]}]}
        report = json.loads(edited["output"].strip().splitlines()[0])
        record.update({k: report[k] for k in ("method", "font", "alignment", "lines", "oldLines", "scale", "styles", "sameFontStyles")})
        fail = record["checks"]
        # The region the paragraph takes now: its box, or as far down as its lines go now (at its pitch,
        # shrunk: fewer lines than before may need no more height).
        pitch = (box[3] - box[1]) / max(old_lines, 1)
        height = report["lines"] * pitch * report["scale"]
        region = [box[0] - 1, box[1] - 1, box[2] + 1, max(box[3], box[1] + height) + 0.2 * pitch]

        # 1. The new text there, and only it.
        after = capriole_text(args, out)
        if not after:
            return {**record, "outcome": "fail", "checks": [{"check": "capriole", "detail": "no text after the edit"}]}
        expected = letters(new)
        got = read_in(capriole_items(after), region)
        if got != expected:
            fail.append({"check": "capriole", "detail": f"reads {got[:80]!r}…, expected {expected[:80]!r}…"})
        for engine, items_before, items_after in (
            ("poppler", poppler_words(sound, 1, args.timeout) or [], poppler_words(out, 1, args.timeout) or []),
            ("mupdf", mupdf_chars(sound, 1, args.timeout) or [], mupdf_chars(out, 1, args.timeout) or []),
        ):
            if read_in(items_before, region) != letters(old):
                record.setdefault("notes", []).append(f"{engine} reads the paragraph otherwise before")
                continue
            got = read_in(items_after, region)
            if got != expected:
                fail.append({"check": engine, "detail": f"reads {got[:80]!r}…"})
        # 2. The text outside, as it was.
        if outside(capriole_items(before), region) != outside(capriole_items(after), region):
            fail.append({"check": "capriole outside", "detail": difference(outside(capriole_items(before), region), outside(capriole_items(after), region))})
        p_before, p_after = poppler_words(sound, 1, args.timeout) or [], poppler_words(out, 1, args.timeout) or []
        if outside(p_before, region) != outside(p_after, region):
            fail.append({"check": "poppler outside", "detail": difference(outside(p_before, region), outside(p_after, region))})
        # 3. The drawing outside.
        scale = args.dpi / 72
        for engine in ("capriole", "poppler"):
            a = masked(draw(args, sound, work / f"{engine}-before.png", engine), [region], scale)
            b = masked(draw(args, out, work / f"{engine}-after.png", engine), [region], scale)
            if a is None or b is None:
                fail.append({"check": f"{engine} drawing", "detail": "not drawn"})
                continue
            s = similarity(a, b, args.blur, args.tolerance)
            if s < args.threshold:
                fail.append({"check": f"{engine} drawing", "detail": f"similarity {s:.4f} outside the paragraph"})
        # 4. Margins; justification.
        lines = [l for l in after["lines"] if in_region(l[:4], region)]
        for l in lines:
            if l[0] < box[0] - 1 or l[2] > box[2] + 1:
                fail.append({"check": "margins", "detail": f"a line spans {l[0]:.1f}–{l[2]:.1f}, the paragraph {box[0]:.1f}–{box[2]:.1f}"})
                break
        if report["alignment"] == "justified":
            for l in sorted(lines, key=lambda l: l[1])[:-1]:
                if abs(l[2] - box[2]) > 1:
                    fail.append({"check": "justified", "detail": f"a line ends at {l[2]:.1f}, the margin is {box[2]:.1f}"})
                    break
        # 4b. Mixed styles: the words kept keep their style.
        if args.mixed:
            cache: dict = {}
            old_styles = word_styles(before, [box[0] - 1, box[1] - 1, box[2] + 1, box[3] + 1])
            new_styles = word_styles(after, region)
            kept = [w for w in old_styles if w in new_styles and new.split(" ").count(w) == 1 and old.split(" ").count(w) == 1]
            record["kept_words"] = len(kept)
            for w in kept:
                (old_font, old_size), (new_font, new_size) = old_styles[w], new_styles[w]
                if abs(new_size - old_size * report["scale"]) > 0.05:
                    fail.append({"check": "style", "detail": f"{w!r}: size {old_size} → {new_size}"})
                    break
                if old_font != new_font and font_class(args, sound, old_font, cache) != font_class(args, out, new_font, cache):
                    fail.append({"check": "style", "detail": f"{w!r}: {font_class(args, sound, old_font, cache)} → {font_class(args, out, new_font, cache)}"})
                    break
        # 5. qpdf.
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
    parser.add_argument("--seed", default=None, help="default: capriole-phase-9a, or -9b with --mixed")
    parser.add_argument("--dpi", type=int, default=72)
    parser.add_argument("--blur", type=float, default=1.0)
    parser.add_argument("--tolerance", type=int, default=32)
    parser.add_argument("--threshold", type=float, default=0.99)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="paragraph-edit")
    parser.add_argument("--mixed", action="store_true", help="paragraphs of several styles (phase 9b)")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())
    args.seed = args.seed or ("capriole-phase-9b" if args.mixed else "capriole-phase-9a")

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="paragraph-edit-", dir=report))
    files = [p.resolve() for d in args.corpus for p in Path(d).glob("*.pdf")]
    files.sort(key=lambda f: hashlib.sha256(f"{args.seed}:{f.name}".encode()).hexdigest())
    results: list[dict] = []
    apart: collections.Counter = collections.Counter()
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool, open(report / "results.jsonl", "w") as log:
        for start in range(0, len(files), args.jobs * 4):
            if len(results) >= args.pages:
                break
            batch = files[start:start + args.jobs * 4]
            for r in pool.map(lambda pair: check_page(args, pair[1], start + pair[0], work), enumerate(batch)):
                if r["outcome"] in ("refused", "no room"):
                    apart[f"{r['outcome']}: {r['why'][:60]}" if r["outcome"] == "refused" else "no room even shrunk to 70%"] += 1
                    log.write(json.dumps(r) + "\n")
                elif r["outcome"] != "skipped" and len(results) < args.pages:
                    results.append(r)
                    log.write(json.dumps(r) + "\n")
    shutil.rmtree(work)

    passed = [r for r in results if r["outcome"] == "pass"]
    methods = collections.Counter(r.get("method", "?") for r in results)
    alignments = collections.Counter(r.get("alignment", "?") for r in results)
    shrunk = sum(1 for r in results if r.get("no_room"))
    met = len(results) >= args.pages and len(passed) == len(results)
    notes = collections.Counter(n for r in results for n in r.get("notes", []))
    lines = [f"# Paragraph edit {args.name} ({stamp})", "",
             f"- **Paragraphs (the new text read in its place by Capriole, Poppler and MuPDF, the text and the drawing outside it as "
             f"before, margins and justification kept, qpdf no worse): {len(passed)} / {len(results)}**",
             f"- Written in the same font: {methods.get('sameFont', 0)}; in a similar standard font: {methods.get('similarFont', 0)}",
             f"- Alignment: {', '.join(f'{k} {n}' for k, n in alignments.most_common())}",
             f"- No room at first, written shrunk: {shrunk}",
             f"- Styles: {sum(r.get('styles', 0) for r in results)} written, {sum(r.get('sameFontStyles', 0) for r in results)} in their own font; "
             f"words kept and checked for their style: {sum(r.get('kept_words', 0) for r in results)}",
             f"- Apart, not counted: {', '.join(f'{n} × {k}' for k, n in apart.most_common()) or 'none'}",
             f"- Exit criterion (every page): **{'met' if met else 'not met'}**", "", "## Notes", ""]
    lines += [f"- {n} × {k}" for k, n in notes.most_common()] or ["- none"]
    lines += ["", "## Failures", ""]
    lines += [f"- {Path(r['file']).name} ({r.get('change')}, {r.get('method')}, {r.get('alignment')}): "
              + "; ".join(f"{c['check']}: {c['detail']}" for c in r["checks"])[:700] for r in results if r["outcome"] == "fail"] or ["- none"]
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:10]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
