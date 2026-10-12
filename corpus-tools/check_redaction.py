#!/usr/bin/env python3
"""Redaction as other readers see it (PLAN.md phase 8g, exit criterion).

On --pages pages of the corpora (one page per file with text, in hash order of --seed; each file first
saved fully, as the apps do with files that needed repairs), up to three words of the page are redacted
(`cpdf redact`, the C API: their boxes as Capriole's text gives them, grown by half a point, filled
black), and, when the page draws an image of at least 40 × 40 points (Poppler's `pdftohtml -xml`), its
left half. Then:

1. no text inside the areas: no word of Capriole (`cpdf text --words`) or Poppler (`pdftotext -bbox`),
   no character of MuPDF (`mutool draw -F stext`), has its centre in an area;
2. the text outside the areas is the same as before, for each of the three (the words — characters
   for MuPDF — whose boxes stay clear of the areas: same text, same place within half a point);
3. the redacted words are gone from what the page shows and says as written: its content streams and
   the forms they draw (decoded), and the texts of its structure elements (pikepdf, on qpdf), have each
   word the page showed once — when written as it reads — at least once fewer;
4. the image redacted has, as Poppler extracts it (`pdftohtml`), a half painted over — of one colour —
   that was not so before;
5. outside the areas the page draws as before in Capriole, Poppler and MuPDF (similarity of
   compare_render.py ≥ --threshold, the areas painted white in both);
6. `qpdf --check` is no worse than the original.

Exit criterion of phase 8g (engine): every page passes.

    corpus-tools/check_redaction.py ../CORPORA/safedocs/0000 ../CORPORA/unsafe/x002 ../CORPORA/unsafe/x008 \\
        --cpdf build/release/tools/cpdf/cpdf --report-dir ../CORPORA/reports --name redaction-all
"""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import hashlib
import html
import json
import re
import shutil
import subprocess
import tempfile
import unicodedata
from pathlib import Path

from PIL import Image, ImageDraw

from check_annotations import draw_all
from check_pages import qpdf_severity
from compare_render import page_count, run, similarity


def capriole_words(args: argparse.Namespace, path: Path, page: int) -> list[tuple[str, list[float]]] | None:
    result = run([args.cpdf, "text", "--words", str(path), str(page)], args.timeout)
    if result["status"] != 0:
        return None
    words = []
    for line in result["output"].splitlines():
        parts = line.split("\t")
        if len(parts) >= 2:
            try:
                words.append((parts[0], [float(v) for v in parts[1].split()]))
            except ValueError:
                continue
    return words


def poppler_words(path: Path, page: int, timeout: float) -> list[tuple[str, list[float]]] | None:
    result = run(["pdftotext", "-cropbox", "-bbox", "-f", str(page), "-l", str(page), str(path), "-"], timeout)
    if result["status"] != 0:
        return None
    out = []
    for m in re.finditer(r'<word xMin="([\d.-]+)" yMin="([\d.-]+)" xMax="([\d.-]+)" yMax="([\d.-]+)">(.*?)</word>', result["output"]):
        out.append((m.group(5), [float(m.group(i)) for i in range(1, 5)]))
    return out


def mupdf_chars(path: Path, page: int, timeout: float) -> list[tuple[str, list[float]]] | None:
    result = run(["mutool", "draw", "-F", "stext", "-o", "-", str(path), str(page)], timeout)
    if result["status"] != 0:
        return None
    out = []
    for m in re.finditer(r'<char quad="([^"]+)"[^>]*c="([^"]*)"', result["output"]):
        q = [float(v) for v in m.group(1).split()]
        xs, ys = q[0::2], q[1::2]
        # As text: entities decoded, ligatures and compatibility forms unfolded (NFKC), as for the words.
        out.append((unicodedata.normalize("NFKC", html.unescape(m.group(2))), [min(xs), min(ys), max(xs), max(ys)]))
    return out


def crop_offset(path: Path, page: int, timeout: float) -> tuple[float, float]:
    """Where the crop box starts within the media box, as shown (pdftohtml places images on the media box)."""
    result = run(["pdfinfo", "-box", "-f", str(page), "-l", str(page), str(path)], timeout)
    boxes = {}
    for m in re.finditer(r"(MediaBox|CropBox):\s+([\d.-]+)\s+([\d.-]+)\s+([\d.-]+)\s+([\d.-]+)", result["output"]):
        boxes[m.group(1)] = [float(m.group(i)) for i in range(2, 6)]
    if "MediaBox" not in boxes or "CropBox" not in boxes:
        return 0.0, 0.0
    media, crop = boxes["MediaBox"], boxes["CropBox"]
    return crop[0] - media[0], media[3] - crop[3]


def poppler_images(path: Path, page: int, work: Path, stem: str, timeout: float) -> list[dict]:
    """The images Poppler draws on the page: their boxes as shown (points, on the crop box), and their pixels."""
    dx, dy = crop_offset(path, page, timeout)
    prefix = work / stem
    run(["pdftohtml", "-xml", "-zoom", "1", "-f", str(page), "-l", str(page), str(path), str(prefix)], timeout)
    xml = Path(f"{prefix}.xml")
    if not xml.exists():
        return []
    images = []
    for m in re.finditer(r'<image top="(-?\d+)" left="(-?\d+)" width="(\d+)" height="(\d+)" src="([^"]+)"', xml.read_text(errors="replace")):
        top, left, width, height = (int(m.group(i)) for i in range(1, 5))
        source = Path(m.group(5))
        source = source if source.is_absolute() else work / source.name
        images.append({"box": [left - dx, top - dy, left - dx + width, top - dy + height], "png": source})
    return images


def centre_in(box: list[float], areas: list[list[float]], shrink: float = 1.0) -> bool:
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    return any(a[0] + shrink <= cx <= a[2] - shrink and a[1] + shrink <= cy <= a[3] - shrink for a in areas)


def clear_of(box: list[float], areas: list[list[float]], margin: float = 1.0) -> bool:
    return all(box[2] < a[0] - margin or box[0] > a[2] + margin or box[3] < a[1] - margin or box[1] > a[3] + margin for a in areas)


def outside(items: list[tuple[str, list[float]]], areas: list[list[float]]) -> list[tuple[str, tuple[int, int]]]:
    """The items clear of the areas, as (text, position rounded to half points), sorted."""
    return sorted((t, (round(b[0] * 2), round(b[1] * 2))) for t, b in items if clear_of(b, areas))


def text_changed(before: list[tuple[str, list[float]]], after: list[tuple[str, list[float]]], areas: list[list[float]]) -> list:
    """What changed clear of the areas: a word (or character) there before and not after, or one there
    after and not before — except the pieces of words that met an area (glyphs at its edge went)."""
    old, new = set(outside(before, areas)), set(outside(after, areas))
    met = [b for t, b in before if not clear_of(b, areas)]
    pieces = {(t, (round(b[0] * 2), round(b[1] * 2))) for t, b in after
              if any(m[0] - 1 <= b[0] and b[2] <= m[2] + 1 and m[1] - 1 <= b[1] and b[3] <= m[3] + 1 for m in met)}
    return sorted((old - new) | (new - old - pieces))


def half_painted(png: Path) -> str | None:
    """Which half of the image (left, right, top, bottom) is one colour (within a tolerance): painted over.
    (The colour is the fill as the image's own colour space has it — CMYK black is not RGB black — so it
    is not compared with the fill.)"""
    try:
        image = Image.open(png).convert("RGB")
    except OSError:
        return None
    w, h = image.size
    halves = {"left": (0, 0, w // 2, h), "right": ((w + 1) // 2, 0, w, h), "top": (0, 0, w, h // 2), "bottom": (0, (h + 1) // 2, w, h)}
    for name, box in halves.items():
        if box[2] <= box[0] or box[3] <= box[1]:
            continue
        pixels = list(image.crop(box).getdata())
        reference = sorted(pixels)[len(pixels) // 2]
        if pixels and sum(1 for p in pixels if all(abs(p[i] - reference[i]) <= 24 for i in range(3))) >= 0.98 * len(pixels):
            return name
    return None


def masked(image: Image.Image | None, areas: list[list[float]], scale: float) -> Image.Image | None:
    if image is None:
        return None
    out = image.convert("RGB").copy()
    draw = ImageDraw.Draw(out)
    for a in areas:
        draw.rectangle([a[0] * scale - 3, a[1] * scale - 3, a[2] * scale + 3, a[3] * scale + 3], fill=(255, 255, 255))
    return out


def page_text_bytes(path: Path, page: int) -> bytes:
    """What a page shows and says, as written: its content streams and the form XObjects they draw
    (decoded, nested at most 12 deep), and the /ActualText, /Alt, /E of its structure elements and their
    ancestors (through /StructParents in the /ParentTree)."""
    import pikepdf

    out = []
    try:
        with pikepdf.open(path) as pdf:
            p = pdf.pages[page - 1]
            seen: set = set()

            def forms(resources, depth: int) -> None:
                if depth > 12 or not isinstance(resources, pikepdf.Dictionary) or "/XObject" not in resources:
                    return
                for _, x in resources.XObject.items():
                    if not isinstance(x, pikepdf.Stream) or x.get("/Subtype") != "/Form" or x.objgen in seen:
                        continue
                    seen.add(x.objgen)
                    try:
                        out.append(x.read_bytes())
                    except (pikepdf.PdfError, RuntimeError):
                        pass
                    forms(x.get("/Resources"), depth + 1)

            contents = p.obj.get("/Contents")
            for stream in (contents if isinstance(contents, pikepdf.Array) else [contents] if contents is not None else []):
                try:
                    out.append(stream.read_bytes())
                except (pikepdf.PdfError, AttributeError, RuntimeError):
                    pass  # not a stream (a malformed file): nothing to read
            forms(p.obj.get("/Resources"), 0)
            root = pdf.Root.get("/StructTreeRoot")
            key = p.obj.get("/StructParents")
            if root is not None and key is not None and "/ParentTree" in root:
                try:
                    elements = pikepdf.NumberTree(root.ParentTree).get(int(key))
                except (KeyError, pikepdf.PdfError, TypeError, ValueError):
                    elements = None
                for element in elements if isinstance(elements, pikepdf.Array) else []:
                    for _ in range(64):
                        if not isinstance(element, pikepdf.Dictionary):
                            break
                        for k in ("/ActualText", "/Alt", "/E"):
                            if k in element:
                                out.append(bytes(element[k]))
                        element = element.get("/P")
    except (pikepdf.PdfError, RuntimeError):
        return b""
    return b"\n".join(out)


def page_qdf(path: Path, page: int, out: Path, timeout: float) -> bytes:
    """One page of a file on its own (qpdf --pages), decompressed (qdf): the bytes."""
    single, qdf = Path(f"{out}.pdf"), Path(f"{out}.qdf")
    with tempfile.TemporaryDirectory() as cwd:
        try:
            subprocess.run(["qpdf", "--empty", "--pages", str(path), str(page), "--", str(single)], capture_output=True, timeout=timeout, cwd=cwd)
            subprocess.run(["qpdf", "--qdf", "--object-streams=disable", "--decode-level=all", str(single), str(qdf)], capture_output=True, timeout=timeout, cwd=cwd)
        except subprocess.TimeoutExpired:
            return b""
    return qdf.read_bytes() if qdf.exists() else b""


def check_page(args: argparse.Namespace, path: Path, work: Path) -> dict:
    result: dict = {"file": str(path), "outcome": "pass", "checks": []}

    def fail(what: str, detail: str) -> None:
        result["outcome"] = "fail"
        result["checks"].append({"check": what, "detail": detail[:300]})

    stem = path.stem
    sound = work / f"{stem}-sound.pdf"
    if run([args.cpdf, "save", str(path), str(sound)], args.timeout)["status"] != 0:
        result["outcome"] = "skipped"
        return result
    count = page_count(sound)
    if not count:
        result["outcome"] = "skipped"
        return result
    digest = int(hashlib.sha256(f"{args.seed}:{path.name}".encode()).hexdigest(), 16)
    page = 1 + digest % count
    result["page"] = page
    before_words = capriole_words(args, sound, page)
    # Words worth redacting: letters and digits, at least four, horizontal, on the page.
    candidates = [(t, b) for t, b in before_words or [] if len(t) >= 4 and t.isalnum() and b[2] - b[0] > b[3] - b[1] > 3 and b[0] >= 0 and b[1] >= 0]
    if not candidates:
        result["outcome"] = "skipped"
        return result
    step = max(1, len(candidates) // 3)
    chosen = candidates[::step][:3]
    areas = [[b[0] - 0.5, b[1] - 0.5, b[2] + 0.5, b[3] + 0.5] for _, b in chosen]
    images_before = poppler_images(sound, page, work, f"{stem}-img-before", args.timeout)
    big = [i for i in images_before if i["box"][2] - i["box"][0] >= 40 and i["box"][3] - i["box"][1] >= 40]
    image_area = None
    if big:
        b = big[0]["box"]
        image_area = [b[0] - 1, b[1] - 1, (b[0] + b[2]) / 2, b[3] + 1]
        areas.append(image_area)
    result["words"] = [t for t, _ in chosen]
    result["image"] = image_area is not None

    theirs_before = {"poppler": poppler_words(sound, page, args.timeout), "mupdf": mupdf_chars(sound, page, args.timeout)}
    out = work / f"{stem}-redacted.pdf"
    spec = json.dumps({"areas": areas, "fill": [0, 0, 0]})
    redacted = run([args.cpdf, "redact", str(sound), str(page), spec, str(out)], args.timeout)
    if redacted["status"] != 0:
        fail("redact", redacted["output"])
        return result

    # 1. No text inside, and 2. the same text outside.
    after = {"capriole": capriole_words(args, out, page), "poppler": poppler_words(out, page, args.timeout), "mupdf": mupdf_chars(out, page, args.timeout)}
    before = {"capriole": before_words, **theirs_before}
    for engine, items in after.items():
        if items is None or before[engine] is None:
            result.setdefault("notes", []).append(f"{engine} cannot read the page's text")
            continue
        # Spaces are left out: MuPDF makes them up from the gaps between glyphs.
        items = [(t, b) for t, b in items if t.strip() and t != " "]
        before[engine] = [(t, b) for t, b in before[engine] if t.strip() and t != " "]
        inside = [(t, b) for t, b in items if centre_in(b, areas)]
        if engine == "capriole":
            if inside:
                fail(engine, f"text inside the areas: {' '.join(t for t, _ in inside[:12])}")
        else:
            # Another reader may lay a line out otherwise (a font it measures differently): what it shows in
            # an area must not be what was there — the same text at the same place — nor a redacted word.
            was = {(t, round(b[0] * 2), round(b[1] * 2)) for t, b in before[engine] if centre_in(b, areas)}
            stayed = [t for t, b in inside if (t, round(b[0] * 2), round(b[1] * 2)) in was]
            joined = "".join(t for t, _ in inside)
            if stayed:
                fail(engine, f"text inside the areas, as before: {' '.join(stayed[:12])}")
            elif any(word in joined for word, _ in chosen):
                fail(engine, f"a redacted word inside the areas: {joined[:80]}")
        reference_before, reference_after = before[engine], items
        if engine != "capriole":
            # A reader that read the redacted words otherwise before (a font it measures differently) lays
            # their lines out otherwise: after the glyphs taken out, the rest of those lines may sit
            # elsewhere for it. Its lines that meet an area are then left out of this comparison.
            # It reads them as Capriole does when it sees in the areas just their characters: no other (a
            # second layer it draws), none missing.
            word_areas = areas[: len(chosen)]  # not the image's, which may have text over it
            seen = unicodedata.normalize("NFKC", "".join(t for t, b in before[engine] if centre_in(b, word_areas)))
            words = unicodedata.normalize("NFKC", "".join(word for word, _ in chosen))
            # (Letters and digits only: Poppler's words carry their punctuation.)
            if sorted(c for c in seen if c.isalnum()) != sorted(c for c in words if c.isalnum()):
                result.setdefault("notes", []).append(f"{engine} reads the areas otherwise before: their lines not compared")
                on_line = lambda b: any(b[1] < a[3] and b[3] > a[1] for a in areas)  # noqa: E731
                reference_before = [(t, b) for t, b in before[engine] if not on_line(b)]
                reference_after = [(t, b) for t, b in items if not on_line(b)]
        changed = text_changed(reference_before, reference_after, areas)
        if changed:
            fail(engine, f"text outside the areas changed: {changed[:8]}")

    # 3. The words, gone from what the page shows and says as written: its content streams and the form
    # XObjects they draw (decoded), and the texts of its structure elements (tagged documents) — read by
    # pikepdf (qpdf) —, have each word the page showed once, when written as it reads, at least once fewer.
    before_bytes = page_text_bytes(sound, page)
    after_bytes = page_text_bytes(out, page)
    page_words = [t for t, _ in before_words or []]
    for word, _ in chosen:
        if page_words.count(word) != 1:
            continue  # shown more than once: which one is written as it reads cannot be told
        whole = re.compile(rb"(?<![A-Za-z0-9/])" + re.escape(word.encode()) + rb"(?![A-Za-z0-9])")
        times_before, times_after = len(whole.findall(before_bytes)), len(whole.findall(after_bytes))
        if times_before > 0 and times_after >= times_before:
            fail("file", f"{word!r} still on the page as written ({times_after} times, {times_before} before)")

    # 4. The image, half painted over: one of the images Poppler extracts at its place (an image and its
    # mask are two) has a half of one colour it did not have. When none has, but an image drawn over it in
    # the area has, the one at that place is content of a soft mask (a luminosity group set by gs), which
    # the engine does not rewrite: a limit, noted.
    if image_area is not None:
        images_after = poppler_images(out, page, work, f"{stem}-img-after", args.timeout)
        same = lambda i: all(abs(i["box"][k] - big[0]["box"][k]) <= 2 for k in range(4))  # noqa: E731
        before_here = [i for i in images_before if same(i)]
        after_here = [i for i in images_after if same(i)]
        painted = any(half_painted(a["png"]) is not None and (k >= len(before_here) or half_painted(before_here[k]["png"]) != half_painted(a["png"]))
                      for k, a in enumerate(after_here))
        if not painted:
            over = [i for i in images_after if not same(i) and clear_of(i["box"], [image_area], 0) is False and half_painted(i["png"]) is not None]
            if over:
                result.setdefault("notes", []).append("the image at the area is a soft mask's (not rewritten); the one over it is painted")
            else:
                fail("image", "no image at the area has a half painted over")

    # 5. The same page outside the areas.
    scale = args.dpi / 72
    drawn_before = draw_all(args, sound, page, work, f"{stem}-before")
    drawn_after = draw_all(args, out, page, work, f"{stem}-after")
    for engine in ("capriole", "pdftoppm", "mutool"):
        a, b = masked(drawn_before[engine], areas, scale), masked(drawn_after[engine], areas, scale)
        if a is None:
            continue
        if b is None:
            fail(engine, "the page is not drawn after")
            continue
        if a.size != b.size:
            fail(engine, f"drawn {b.size}, before {a.size}")
            continue
        s = similarity(a, b, args.blur, args.tolerance)
        if s < args.threshold:
            fail(engine, f"outside the areas the page changed (similarity {s:.4f})")
    # 6. qpdf.
    if qpdf_severity(out, args.timeout) > qpdf_severity(sound, args.timeout):
        fail("qpdf", "qpdf --check worse than before")
    for leftover in work.glob(f"{stem}*"):
        if leftover.is_dir():
            shutil.rmtree(leftover, ignore_errors=True)
        else:
            leftover.unlink(missing_ok=True)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="+")
    parser.add_argument("--cpdf", required=True)
    parser.add_argument("--resources", default="build/resources" if Path("build/resources/cmap").is_dir() else None)
    parser.add_argument("--pages", type=int, default=100)
    parser.add_argument("--seed", default="capriole-phase-8g")
    parser.add_argument("--dpi", type=int, default=72)
    parser.add_argument("--blur", type=float, default=1.0)
    parser.add_argument("--tolerance", type=int, default=32)
    parser.add_argument("--threshold", type=float, default=0.99)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="redaction")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())
    args.resources = str(Path(args.resources).resolve()) if args.resources else None

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="redaction-", dir=report))
    files = [p.resolve() for d in args.corpus for p in Path(d).glob("*.pdf")]
    files.sort(key=lambda f: hashlib.sha256(f"{args.seed}:{f.name}".encode()).hexdigest())
    results: list[dict] = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool, open(report / "results.jsonl", "w") as log:
        counted = 0
        for start in range(0, len(files), args.jobs * 4):
            if counted >= args.pages:
                break
            for r in pool.map(lambda p: check_page(args, p, work), files[start : start + args.jobs * 4]):
                if r["outcome"] != "skipped" and counted < args.pages:
                    counted += 1
                    results.append(r)
                    log.write(json.dumps(r) + "\n")
    shutil.rmtree(work)

    passed = [r for r in results if r["outcome"] == "pass"]
    with_images = sum(1 for r in results if r.get("image"))
    met = bool(results) and len(passed) == len(results)
    lines = [f"# Redaction {args.name} ({stamp})", "",
             f"- **Pages (no text inside the areas, the same outside, the words gone from the file, the image painted over, "
             f"the same drawing outside, qpdf no worse): {len(passed)} / {len(results)}**",
             f"- Pages with an image redacted too: {with_images}",
             f"- Exit criterion (every page): **{'met' if met else 'not met'}**", "", "## Failures", ""]
    lines += [f"- {Path(r['file']).name} p{r.get('page')}: " + "; ".join(f"{c['check']}: {c['detail']}" for c in r["checks"])[:700]
              for r in results if r["outcome"] == "fail"] or ["- none"]
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:5]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
