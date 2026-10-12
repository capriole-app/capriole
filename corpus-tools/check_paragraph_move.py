#!/usr/bin/env python3
"""Moving what follows a longer paragraph down the page, as other readers see it (PLAN.md phase 9d, exit
criterion).

On --pages pages of the corpora (the first page of a file, in hash order of --seed; each file first saved
fully, as the apps do with files that needed repairs), a paragraph of Capriole's text (as laid out: a block of
at least two lines and eight words, horizontal and visible, with no other text in its box) with text below
it, across its width, is made longer by one or two lines' worth of words (by turns), and written anew
moving what follows down (`cpdf edit-paragraph --move`, the C API). When it moved (`moved` > 0):

1. Capriole (`cpdf text --json`), Poppler (`pdftotext -bbox`) and MuPDF (`mutool draw -F stext`) read the
   new text in the paragraph's place (letters and digits compared, in order);
2. for the three, every word (MuPDF: character) outside the paragraph is where it was, or — below the
   paragraph, across its width — `moved` points lower, at the same x (within 0.3 point); in the page's last
   eighth (a footer may stay) either;
3. the page draws as before above the paragraph and beside it (but where the lines that move were and
   are), and below it, across its width, as the page drawn `moved` lower (its content wrapped in a translation with
   pikepdf: drawn by the same renderer) — Capriole and Poppler, similarity of compare_render.py >=
   --threshold;
4. `qpdf --check` is no worse.

Moves `edit-paragraph` refuses ("cannot move what follows: …"), other refusals by design (styles, fonts),
and paragraphs that had room (nothing moved) are counted apart, by reason, and replaced by further pages.

Exit criterion of phase 9d-1: --pages paragraphs moved, every one passing; the refusals reported.

    corpus-tools/check_paragraph_move.py ../CORPORA/safedocs/0000 ../CORPORA/unsafe/x002 ../CORPORA/unsafe/x008 \\
        --cpdf build/release/tools/cpdf/cpdf --report-dir ../CORPORA/reports --name paragraph-move-all
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
from check_paragraph_edit import capriole_items, capriole_text, in_region, letters, read_in
from check_redaction import masked, mupdf_chars, poppler_words
from check_text_edit import draw
from compare_render import run, similarity

FILLER = "and then a few more words written in here to make it longer"
FOOTER_ZONE = 0.875


def choose(page: dict) -> tuple[list[float], str, int] | None:
    """The longest block of at least 2 lines and 8 words, horizontal and visible, alone in its box, with
    text below it across its width: its box, text, lines."""
    best = None
    chars = page["chars"]
    for x0, y0, x1, y1, first, count in page["blocks"]:
        if count < 2:
            continue
        lines = page["lines"][first:first + count]
        words = [w for l in lines for w in page["words"][l[4]:l[4] + l[5]]]
        if len(words) < 8:
            continue
        members = {c for w in words for c in range(w[4], w[4] + w[5])}
        cs = [chars[c] for c in members]
        if any(c[6] != 0 or c[7] == 1 for c in cs):
            continue
        if any(i not in members and in_region(c[1:5], [x0 - 0.5, y0 - 0.5, x1 + 0.5, y1 + 0.5]) for i, c in enumerate(chars)):
            continue
        below = [w for w in page["words"] if w[1] >= y1 - 0.5 and w[2] > x0 + 0.5 and w[0] < x1 - 0.5 and w[1] < FOOTER_ZONE * page["height"]]
        if not below:
            continue
        text = " ".join("".join(chars[c][0] for c in range(w[4], w[4] + w[5])) for w in words)
        if any(unicodedata.category(ch)[0] == "C" for ch in text):
            continue
        if best is None or len(text) > len(best[1]):
            best = ([x0, y0, x1, y1], text, count)
    return best


def line_kinds(page: dict, box: list[float], cut: float) -> tuple[list[list[float]], list[list[float]], list[list[float]]]:
    """Capriole's lines below the paragraph: those that move (across its width, and what goes with them), those
    in the last eighth (a footer may stay), the others (they stay)."""
    moving, either, rest = [], [], []
    for x0, y0, x1, y1, _, _ in page["lines"]:
        line = [x0 - 0.5, y0 - 0.5, x1 + 0.5, y1 + 0.5]
        if y0 < cut - 0.5:
            continue
        if y0 >= FOOTER_ZONE * page["height"]:
            either.append(line)
        elif x1 > box[0] + 0.5 and x0 < box[2] - 0.5:
            moving.append(line)
        else:
            rest.append(line)
    # As the engine does: what is on a line that moves, near it and not taller than twice it, moves with it.
    more = True
    while more:
        more = False
        for line in list(rest):
            for m in moving:
                shared = min(line[3], m[3]) - max(line[1], m[1])
                height = m[3] - m[1]
                gap = max(line[0], m[0]) - min(line[2], m[2])
                # More of the line (a word's space away), or a small mark near it: as the engine does.
                near = gap <= height / 2 or (line[2] - line[0] <= 3 * height and gap <= 2 * height)
                if shared >= min(line[3] - line[1], height) / 2 and line[3] - line[1] <= 2 * height and near:
                    moving.append(line)
                    rest.remove(line)
                    more = True
                    break
            else:
                # Between lines that move, above and below it across its width: it goes with them.
                across = [m for m in moving if min(m[2], line[2]) - max(m[0], line[0]) > 0]
                if any(m[3] <= line[1] + 1 for m in across) and any(m[1] >= line[3] - 1 for m in across):
                    moving.append(line)
                    rest.remove(line)
                    more = True
    return moving, either, rest


def placed(before: list, after: list, region: list[float], moving: list[list[float]], either: list[list[float]], staying: list[list[float]],
           by: float, height: float, cut: float) -> list[str]:
    """What is not where it should be: each item outside the paragraph where it was, or `by` lower (in a line
    that moves); in the last eighth either. Each reader's items go with Capriole's lines they lie in."""
    pool = collections.defaultdict(list)
    for text, box in after:
        if text.strip() and not in_region(box, region):
            pool[text].append(box)
    wrong = []
    for text, box in before:
        if not text.strip():
            continue
        below = any(in_region(box, line) for line in moving)
        footer = any(in_region(box, line) for line in either) or (not below and box[1] >= FOOTER_ZONE * height)
        # A small mark on the row of a line that moves (a list's number, a bullet): it may go with what the
        # engine sees and Capriole's text does not (a drawing between the lines that move).
        h = box[3] - box[1]
        footer = footer or (not below and box[2] - box[0] <= 3 * max(h, 1) and any(
            min(box[3], line[3]) - max(box[1], line[1]) >= h / 2 for line in moving))
        # Text below the paragraph that is in none of Capriole's lines (an annotation's, which MuPDF reads):
        # it stays or moves with its annotation.
        footer = footer or (not below and box[1] >= cut - 0.5 and not any(in_region(box, line) for line in staying))
        targets = [box[1], box[1] + by] if footer else [box[1] + by] if below else [box[1]]
        match = next((b for b in pool[text] if abs(b[0] - box[0]) < 0.3 and any(abs(b[1] - t) < 0.3 for t in targets)), None)
        if match is None:
            wrong.append(f"{text!r} at {box[0]:.1f},{box[1]:.1f} ({'moved' if below else 'kept'})")
        else:
            pool[text].remove(match)
    return wrong


def shifted_copy(source: Path, out: Path, by: float) -> bool:
    """The first page drawn `by` points lower as shown (its content and annotations): what the part of the
    page that moves must look like. False for a page whose content cannot be wrapped."""
    import pikepdf

    try:
        with pikepdf.open(source) as pdf:
            page = pdf.pages[0]
            rotate = int(page.obj.get("/Rotate", 0)) % 360
            dx, dy = {0: (0.0, -by), 90: (by, 0.0), 180: (0.0, by), 270: (-by, 0.0)}[rotate]
            page.contents_coalesce()
            page.contents_add(pdf.make_stream(f"q 1 0 0 1 {dx:.6f} {dy:.6f} cm\n".encode()), prepend=True)
            page.contents_add(pdf.make_stream(b"\nQ\n"))
            for annot in page.obj.get("/Annots", []):
                if "/Rect" in annot:
                    r = [float(v) for v in annot.Rect]
                    annot.Rect = [r[0] + dx, r[1] + dy, r[2] + dx, r[3] + dy]
            pdf.save(out)
        return True
    except Exception:  # noqa: BLE001 — a page pikepdf cannot rewrite: its drawing is not compared moved
        return False


def either_similarity(before, shifted, after, blur: float, tolerance: int) -> float:
    """The share of pixels of after as in before, or as in shifted (what moved with a line or a drawing)."""
    from PIL import ImageChops, ImageFilter

    w, h = min(before.width, after.width, shifted.width), min(before.height, after.height, shifted.height)
    if w == 0 or h == 0:
        return 0.0
    a, b, c = (i.crop((0, 0, w, h)).filter(ImageFilter.GaussianBlur(blur)) for i in (before, after, shifted))

    def worst(x, y):
        ch = ImageChops.difference(x, y).split()
        return ImageChops.lighter(ImageChops.lighter(ch[0], ch[1]), ch[2])

    best = ImageChops.darker(worst(a, b), worst(c, b))
    return sum(best.histogram()[: tolerance + 1]) / (w * h)


def crop(image, box: list[float], scale: float):
    return image.crop(tuple(round(v * scale) for v in box))


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
        chosen = choose(before)
        if not chosen:
            return {**record, "outcome": "skipped", "why": "no paragraph with text below"}
        box, old, old_lines = chosen
        per_line = max(len(old) // max(old_lines, 1), 10)
        grow = 1 + index % 2
        record["grow"] = grow
        extra = ""
        while len(extra) < per_line * grow:
            extra += " " + FILLER
        words = old.split(" ")
        new = " ".join(words[:2] + extra.split() + words[2:])
        area = ",".join(f"{v:.3f}" for v in (box[0] - 0.3, box[1] - 0.3, box[2] + 0.3, box[3] + 0.3))
        out = work / "moved.pdf"
        argument = new.replace("\\", "\\\\")
        edited = run([args.cpdf, "edit-paragraph", "--move", str(sound), "1", area, argument, str(out)], args.timeout)
        if edited["status"] != 0:
            m = re.search(r"cannot move what follows: ([^(\n]*)", edited["output"])
            if m:
                return {**record, "outcome": "refused", "why": "move: " + m.group(1).strip()}
            refusal = re.search(r"more than one font|invisible|an annotation's|only horizontal|several paragraphs|one paragraph|cannot write|superscripts",
                                edited["output"])
            if refusal:
                return {**record, "outcome": "refused", "why": refusal.group(0)}
            return {**record, "outcome": "fail", "checks": [{"check": "edit-paragraph", "detail": edited["output"].strip()[-300:]}]}
        report = json.loads(edited["output"].strip().splitlines()[0])
        if report["moved"] <= 0:
            return {**record, "outcome": "refused", "why": "room enough: nothing moved"}
        by = report["moved"]
        record.update({k: report[k] for k in ("method", "lines", "oldLines", "moved", "movedItems", "movedAnnotations")})
        fail = record["checks"]
        cut = box[3]
        moving, either, staying = line_kinds(before, box, cut)
        pitch = (box[3] - box[1]) / max(old_lines, 1)
        region = [box[0] - 1, box[1] - 1, box[2] + 1, box[1] + report["lines"] * pitch + 0.2 * pitch]
        height = before["height"]

        # 1. The new text in the paragraph's place.
        after = capriole_text(args, out)
        if not after:
            return {**record, "outcome": "fail", "checks": [{"check": "capriole", "detail": "no text after the edit"}]}
        expected = letters(new)
        readers = {
            "capriole": (capriole_items(before), capriole_items(after)),
            "poppler": (poppler_words(sound, 1, args.timeout) or [], poppler_words(out, 1, args.timeout) or []),
            "mupdf": (mupdf_chars(sound, 1, args.timeout) or [], mupdf_chars(out, 1, args.timeout) or []),
        }
        for engine, (items_before, items_after) in readers.items():
            if engine != "capriole" and read_in(items_before, [box[0] - 1, box[1] - 1, box[2] + 1, box[3] + 1]) != letters(old):
                record.setdefault("notes", []).append(f"{engine} reads the paragraph otherwise before")
                continue
            got = read_in(items_after, region)
            if got != expected:
                fail.append({"check": engine, "detail": f"reads {got[:80]!r}…, expected {expected[:80]!r}…"})
            # 2. The rest where it was, or moved.
            old_region = [box[0] - 1, box[1] - 1, box[2] + 1, box[3] + 1]
            wrong = placed([(t, b) for t, b in items_before if not in_region(b, old_region)], items_after, region, moving, either, staying, by, height, cut)
            if wrong:
                fail.append({"check": f"{engine} places", "detail": f"{len(wrong)} misplaced: " + "; ".join(wrong[:4])})
        # 3. The drawing: above and beside as before; below, across its width, as before moved down.
        scale = args.dpi / 72
        width = before["width"]
        # Above it: as before. Across its width below it: as before moved down. Beside it: as before, but for
        # the lines that move (wider than it, some), each compared with itself moved down.
        zones = [("above", [0, 0, width, max(box[1] - 1, 0)], 0.0), ("below", [box[0], cut + 1 + by, box[2], FOOTER_ZONE * height], by)]
        beside = [("left", [0, cut + 1, max(box[0] - 2, 0), height]), ("right", [min(box[2] + 2, width), cut + 1, width, height])]
        reference = work / "shifted.pdf"
        has_reference = shifted_copy(sound, reference, by)
        if not has_reference:
            record.setdefault("notes", []).append("no shifted reference: the moved part not compared drawn")
        drawn_by: dict = {}
        areas = {zone: z for zone, z, _ in zones} | dict(beside)

        def excused(engine: str, zone: str) -> bool:
            """Another reader that drew the zone otherwise than Capriole before and as Capriole after: it did not
            draw the page as it was (and draws the page rewritten). Noted."""
            if engine == "capriole" or "capriole" not in drawn_by or zone not in areas:
                return False
            (ca, cb), (pa, pb) = drawn_by["capriole"], drawn_by[engine]
            z = areas[zone]
            if (similarity(crop(pa, z, scale), crop(ca, z, scale), args.blur, args.tolerance) < args.threshold
                    and similarity(crop(pb, z, scale), crop(cb, z, scale), args.blur, args.tolerance) >= args.threshold):
                record.setdefault("notes", []).append(f"{engine} drew the page otherwise than Capriole before, as it after")
                return True
            return False
        for engine in ("capriole", "poppler"):
            a = draw(args, sound, work / f"{engine}-before.png", engine)
            b = draw(args, out, work / f"{engine}-after.png", engine)
            c = draw(args, reference, work / f"{engine}-shifted.png", engine) if has_reference else None
            if a is None or b is None:
                fail.append({"check": f"{engine} drawing", "detail": "not drawn"})
                continue
            a, b = a.convert("RGB"), b.convert("RGB")
            drawn_by[engine] = (a, b)
            checks = []
            for zone, z, shift in zones:
                if z[2] - z[0] < 2 or z[3] - z[1] < 2 or (shift and c is None):
                    continue
                checks.append((zone, crop(c.convert("RGB") if shift else a, z, scale), crop(b, z, scale)))
            for zone, x, y in checks:
                s = similarity(x, y, args.blur, args.tolerance)
                if s < args.threshold and not excused(engine, zone):
                    fail.append({"check": f"{engine} drawing {zone}", "detail": f"similarity {s:.4f}"})
            # Beside it: as before, but where the lines that move were and are (as far as what goes with them
            # reaches: twice their height each side) — their text is checked in its place above.
            reach = [[l[0] - 2 * (l[3] - l[1]), l[1] - (l[3] - l[1]) / 2, l[2] + 2 * (l[3] - l[1]), l[3] + (l[3] - l[1]) / 2] for l in moving]
            holes = reach + [[r[0], r[1] + by, r[2], r[3] + by] for r in reach]
            a_kept, b_kept = masked(a, holes, scale), masked(b, holes, scale)
            c_kept = masked(c.convert("RGB"), holes, scale) if c is not None else None
            for zone, z in beside:
                if z[2] - z[0] >= 2 and z[3] - z[1] >= 2:
                    # As before, or (a drawing that moved with what is across the paragraph's width) moved.
                    s = (either_similarity(crop(a_kept, z, scale), crop(c_kept, z, scale), crop(b_kept, z, scale), args.blur, args.tolerance)
                         if c_kept is not None else similarity(crop(a_kept, z, scale), crop(b_kept, z, scale), args.blur, args.tolerance))
                    if s < args.threshold and not excused(engine, zone):
                        fail.append({"check": f"{engine} drawing {zone}", "detail": f"similarity {s:.4f}"})
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
    parser.add_argument("--seed", default="capriole-phase-9d")
    parser.add_argument("--dpi", type=int, default=72)
    parser.add_argument("--blur", type=float, default=1.0)
    parser.add_argument("--tolerance", type=int, default=32)
    parser.add_argument("--threshold", type=float, default=0.99)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="paragraph-move")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="paragraph-move-", dir=report))
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
                if r["outcome"] == "refused":
                    apart[r["why"][:80]] += 1
                    log.write(json.dumps(r) + "\n")
                elif r["outcome"] != "skipped" and len(results) < args.pages:
                    results.append(r)
                    log.write(json.dumps(r) + "\n")
    shutil.rmtree(work)

    passed = [r for r in results if r["outcome"] == "pass"]
    moves = sum(1 for r in results)
    refused_moves = sum(n for k, n in apart.items() if k.startswith("move:"))
    met = len(results) >= args.pages and len(passed) == len(results)
    notes = collections.Counter(n for r in results for n in r.get("notes", []))
    lines = [f"# Paragraph move {args.name} ({stamp})", "",
             f"- **Paragraphs moved (the new text read in place by Capriole, Poppler and MuPDF, the rest where it was or moved by as much, "
             f"drawn as before or moved, qpdf no worse): {len(passed)} / {len(results)}**",
             f"- Moved by one line's worth: {sum(1 for r in results if r.get('grow') == 1)}, by two: {sum(1 for r in results if r.get('grow') == 2)}; "
             f"operators moved {sum(r.get('movedItems', 0) for r in results)}, annotations {sum(r.get('movedAnnotations', 0) for r in results)}",
             f"- Moves refused: {refused_moves} (of {moves + refused_moves} tried)",
             f"- Apart, not counted: {', '.join(f'{n} × {k}' for k, n in apart.most_common()) or 'none'}",
             f"- Exit criterion (every page): **{'met' if met else 'not met'}**", "", "## Notes", ""]
    lines += [f"- {n} × {k}" for k, n in notes.most_common()] or ["- none"]
    lines += ["", "## Failures", ""]
    lines += [f"- {Path(r['file']).name} (+{r.get('grow')} lines, moved {r.get('moved')}): "
              + "; ".join(f"{c['check']}: {c['detail']}" for c in r["checks"])[:700] for r in results if r["outcome"] == "fail"] or ["- none"]
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:8]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
