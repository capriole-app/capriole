#!/usr/bin/env python3
"""Extending embedded font subsets, as other readers see it (PLAN.md phase 9c, exit criterion).

On pages of the corpora (the first --max-pages pages of a file, in hash order of --seed; each file first saved fully)
with an embedded TrueType subset — composite (Identity-H, CIDFontType2), simple (WinAnsiEncoding) or
symbolic without an encoding (LibreOffice's, Cairo's: a /ToUnicode saying its codes) — of a
font installed here (fc-list: its PostScript name), the paragraph (block of Capriole's text, as laid out)
of a word of letters only in that font is written anew (`cpdf edit-paragraph --shrink`) with the word
followed by a letter the installed font has (fc-query's charset) and the subset lacks (written without
the font, the edit names it among the fonts that lacked letters) — the installed font given (`--font`).
Then, in the region the paragraph takes:

1. the letter was added to the subset (addedGlyphs >= 1, the font no longer among those lacking letters);
2. Capriole, Poppler and MuPDF read the new text there (letters and digits, in order);
3. Poppler and MuPDF draw the new letter (ink in its box, the page drawn at --dpi), and the page outside
   the paragraph as before (Capriole and Poppler; similarity of compare_render.py >= --threshold);
4. `qpdf --check` is no worse, and Poppler (pdffonts, pdftotext) reports no more font errors.

Pages where the font is not extended — another version of it installed, permissions that do not allow
it, a kind of font not extended — are counted apart and replaced by further pages.

Exit criterion of phase 9c (engine): at least --pages pages, every one passing.

    corpus-tools/check_font_extend.py ../CORPORA/safedocs/0000 ../CORPORA/unsafe/x002 ../CORPORA/unsafe/x008 \\
        --cpdf build/release/tools/cpdf/cpdf --report-dir ../CORPORA/reports --name font-extend-all
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

from check_pages import qpdf_severity
from check_paragraph_edit import capriole_items, capriole_text, in_region, letters, read_in
from check_redaction import masked, mupdf_chars, poppler_words
from check_text_edit import draw
from compare_render import load, run, similarity

# Letters to try, in turn: Latin letters fonts have and short texts often lack, then accented ones.
CANDIDATES = "QZXJKWYVqzxjkwyvéèàùçñüöäßÉÀŽžŠšČčŘřĐđŁłĞğŞş"


def installed_fonts() -> dict[str, str]:
    """PostScript name (letters and digits, lowercase) → font file, for the fonts fontconfig knows."""
    out = subprocess.run(["fc-list", "--format", "%{postscriptname}\t%{file}\n"], capture_output=True, text=True, check=False).stdout
    fonts = {}
    for line in out.splitlines():
        name, _, path = line.partition("\t")
        if name and path.lower().endswith((".ttf", ".ttc", ".otf")):
            fonts.setdefault(key(name), path)
    return fonts


def key(name: str) -> str:
    return "".join(c for c in re.sub(r"^[A-Z]{6}\+", "", name) if c.isalnum()).lower()


def charset(path: str, cache: dict) -> set[int]:
    """The characters a font file has (its first face), from fc-query."""
    if path in cache:
        return cache[path]
    out = subprocess.run(["fc-query", "--index", "0", "--format", "%{charset}", path], capture_output=True, text=True, check=False).stdout
    chars: set[int] = set()
    for part in out.split():
        a, _, b = part.partition("-")
        try:
            lo, hi = int(a, 16), int(b or a, 16)
        except ValueError:
            continue
        chars.update(range(lo, hi + 1))
    cache[path] = chars
    return chars


def font_kind(args: argparse.Namespace, path: Path, ref: list, cache: dict) -> tuple[str, str] | None:
    """(PostScript name, "composite" | "simple") of an embedded TrueType subset that can be extended."""
    k = (str(path), tuple(ref))
    if k in cache:
        return cache[k]
    font = run([args.cpdf, "object", str(path), str(ref[0]), str(ref[1])], args.timeout)["output"]
    name = re.search(r"/BaseFont\s*/([^\s/<>\[\]()]+)", font)
    result = None
    if name and re.match(r"^[A-Z]{6}\+", name.group(1)):
        if "/Type0" in font and "/Identity-H" in font:
            child = re.search(r"/DescendantFonts\s*\[\s*(\d+)\s+(\d+)\s+R", font)
            if child:
                sub = run([args.cpdf, "object", str(path), child.group(1), child.group(2)], args.timeout)["output"]
                if "/CIDFontType2" in sub:
                    result = (name.group(1), "composite")
        elif "/TrueType" in font and "/WinAnsiEncoding" in font:
            result = (name.group(1), "simple")
        elif "/TrueType" in font and "/Encoding" not in font and "/ToUnicode" in font:
            result = (name.group(1), "symbolic")
    cache[k] = result
    return result


def check_page(args: argparse.Namespace, path: Path, fonts: dict[str, str], work_root: Path) -> dict:
    work = Path(tempfile.mkdtemp(dir=work_root))
    record: dict = {"file": str(path), "checks": []}
    cache: dict = {}
    sets: dict = {}
    try:
        sound = work / "sound.pdf"
        run([args.cpdf, "save", str(path), str(sound)], args.timeout)
        if not sound.exists():
            return {**record, "outcome": "skipped", "why": "does not open"}
        info = run(["pdfinfo", str(sound)], args.timeout)["output"]
        count_match = re.search(r"Pages:\s+(\d+)", info)
        for number in range(1, min(args.max_pages, int(count_match.group(1)) if count_match else 1) + 1):
            found = check_on(args, record, work, sound, number, fonts, cache, sets)
            if found is not None:
                return found
        return {**record, "outcome": "skipped", "why": "no paragraph in a subset of an installed font lacking a letter"}
    except Exception as error:  # noqa: BLE001 — a crash of the check is a failure of the page, reported
        return {**record, "outcome": "fail", "checks": [{"check": "exception", "detail": repr(error)[:300]}]}
    finally:
        shutil.rmtree(work, ignore_errors=True)


def check_on(args: argparse.Namespace, record: dict, work: Path, sound: Path, number: int, fonts: dict[str, str], cache: dict, sets: dict) -> dict | None:
    """The page `number` checked, when it has a paragraph to check (else None)."""
    page = capriole_text(args, sound, number)
    if not page:
        return None
    chars = page["chars"]
    texts = ["".join(chars[c][0] for c in range(w[4], w[4] + w[5])) for w in page["words"]]
    counts = collections.Counter(texts)
    block_of = {}
    for b, (x0, y0, x1, y1, first, count) in enumerate(page["blocks"]):
        for line in page["lines"][first:first + count]:
            for w in range(line[4], line[4] + line[5]):
                block_of[w] = b
    tried_blocks = set()
    for index, (text, w) in enumerate(zip(texts, page["words"])):
        if counts[text] != 1 or len(text) < 3 or not text.isalpha() or index not in block_of or block_of[index] in tried_blocks:
            continue
        refs = {json.dumps(chars[c][12]) for c in range(w[4], w[4] + w[5])}
        if len(refs) != 1 or chars[w[4]][12] is None or chars[w[4]][6] != 0 or chars[w[4]][7] == 1:
            continue
        kind = font_kind(args, sound, chars[w[4]][12], cache)
        if not kind or key(kind[0]) not in fonts:
            continue
        tried_blocks.add(block_of[index])
        font_file = fonts[key(kind[0])]
        have = charset(font_file, sets)
        x0, y0, x1, y1, first, count = page["blocks"][block_of[index]]
        words = [i for line in page["lines"][first:first + count] for i in range(line[4], line[4] + line[5])]
        if any(not texts[i] or any(ord(ch) < 32 for ch in texts[i]) for i in words):
            continue
        area = ",".join(f"{v:.3f}" for v in (x0 - 0.3, y0 - 0.3, x1 + 0.3, y1 + 0.3))
        base = re.sub(r"^[A-Z]{6}\+", "", kind[0])
        for letter in CANDIDATES:
            if ord(letter) not in have or (kind[1] == "simple" and letter.encode("cp1252", "ignore") == b""):  # WinAnsi codes only
                continue
            new = " ".join(texts[i] + letter if i == index else texts[i] for i in words)
            argument = new.replace("\\", "\\\\")
            probe = run([args.cpdf, "edit-paragraph", "--shrink", str(sound), str(number), area, argument, str(work / "probe.pdf")], args.timeout)
            if probe["status"] != 0:
                break  # the paragraph cannot be edited (styles, room…): another one
            if base not in json.loads(probe["output"].strip().splitlines()[0]).get("missingFonts", []):
                continue  # the subset has the letter
            record.update({"page": number, "word": text, "letter": letter, "font": kind[0], "kind": kind[1], "full": font_file})
            return finish(args, record, work, sound, number, area, [x0, y0, x1, y1], count, new, text + letter, base, font_file)
    return None


def ink(image, box: list[float], scale: float) -> int:
    """Dark pixels in a box of a drawing."""
    if image is None:
        return 0
    crop = image.convert("L").crop((int(box[0] * scale), int(box[1] * scale), int(box[2] * scale) + 1, int(box[3] * scale) + 1))
    return sum(1 for v in crop.getdata() if v < 128)


def font_errors(path: Path, timeout: float) -> int:
    out = run(["pdffonts", str(path)], timeout)["output"] + run(["pdftotext", str(path), "-"], timeout)["output"]
    return len(re.findall(r"(?i)(syntax error|error).*font|font.*error", out))


def finish(args: argparse.Namespace, record: dict, work: Path, sound: Path, number: int, area: str, box: list[float], old_lines: int, new: str,
           word: str, base: str, font_file: str) -> dict:
    out = work / "edited.pdf"
    argument = new.replace("\\", "\\\\")
    edited = run([args.cpdf, "edit-paragraph", "--shrink", "--font", font_file, str(sound), str(number), area, argument, str(out)], args.timeout)
    if edited["status"] != 0 or not out.exists():
        return {**record, "outcome": "fail", "checks": [{"check": "edit-paragraph", "detail": edited["output"].strip()[-300:]}]}
    report = json.loads(edited["output"].strip().splitlines()[0])
    record.update({"added": report.get("addedGlyphs", 0), "method": report["method"]})
    if report.get("addedGlyphs", 0) < 1 or base in report.get("missingFonts", []):
        # Not extended: another version installed, permissions, a kind of font not extended.
        return {**record, "outcome": "apart", "why": f"not extended ({base})"}
    fail = record["checks"]
    pitch = (box[3] - box[1]) / max(old_lines, 1)
    region = [box[0] - 1, box[1] - 1, box[2] + 1, max(box[3], box[1] + report["lines"] * pitch * report["scale"]) + 0.2 * pitch]
    # 2. Read by all three.
    before = capriole_text(args, sound, number)
    after = capriole_text(args, out, number)
    expected = letters(new)
    if not after or read_in(capriole_items(after), region) != expected:
        fail.append({"check": "capriole", "detail": f"reads {read_in(capriole_items(after), region)[:100] if after else ''!r}"})
    old = " ".join(t for t, b in capriole_items(before) if in_region(b, [box[0] - 1, box[1] - 1, box[2] + 1, box[3] + 1]))
    for engine, read in (("poppler", lambda p: poppler_words(p, number, args.timeout)), ("mupdf", lambda p: mupdf_chars(p, number, args.timeout))):
        if read_in(read(sound) or [], region) != letters(old):
            record.setdefault("notes", []).append(f"{engine} reads the paragraph otherwise before")
            continue
        got = read_in(read(out) or [], region)
        if got != expected:
            fail.append({"check": engine, "detail": f"reads {got[:100]!r}"})
    # 3. The new letter drawn (its box: the last letter of the word, in Capriole's text); the rest as before.
    scale = args.dpi / 72
    letter_box = None
    if after:
        for x0, y0, x1, y1, first, count in after["words"]:
            if "".join(after["chars"][c][0] for c in range(first, first + count)) == word:
                letter_box = after["chars"][first + count - 1][1:5]
    if letter_box is None:
        fail.append({"check": "letter", "detail": f"no word {word!r} in Capriole's text"})
    else:
        mupdf_png = work / "mupdf.png"
        run(["mutool", "draw", "-q", "-r", str(args.dpi), "-o", str(mupdf_png), str(out), str(number)], args.timeout)
        for engine, image in (("poppler", draw(args, out, work / "poppler-after.png", "poppler", number)), ("mupdf", load(mupdf_png))):
            if ink(image, letter_box, scale) == 0:
                fail.append({"check": f"{engine} glyph", "detail": f"nothing drawn where the new letter is"})
    for engine in ("capriole", "poppler"):
        a = masked(draw(args, sound, work / f"{engine}-before.png", engine, number), [region], scale)
        b = masked(draw(args, out, work / f"{engine}-after2.png", engine, number), [region], scale)
        if a is None or b is None or similarity(a, b, args.blur, args.tolerance) < args.threshold:
            fail.append({"check": f"{engine} drawing", "detail": "the page outside the paragraph changed"})
    # 4. qpdf; font errors.
    if qpdf_severity(out, args.timeout) > qpdf_severity(sound, args.timeout):
        fail.append({"check": "qpdf", "detail": "qpdf --check worse than before"})
    if font_errors(out, args.timeout) > font_errors(sound, args.timeout):
        fail.append({"check": "font errors", "detail": "Poppler reports font errors it did not before"})
    record["outcome"] = "fail" if fail else "pass"
    return record


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="+")
    parser.add_argument("--cpdf", required=True)
    parser.add_argument("--pages", type=int, default=30)
    parser.add_argument("--max-pages", type=int, default=5, help="pages of a file looked at for a paragraph")
    parser.add_argument("--seed", default="capriole-phase-9c")
    parser.add_argument("--dpi", type=int, default=96)
    parser.add_argument("--blur", type=float, default=1.0)
    parser.add_argument("--tolerance", type=int, default=32)
    parser.add_argument("--threshold", type=float, default=0.99)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="font-extend")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())

    fonts = installed_fonts()
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="font-extend-", dir=report))
    files = [p.resolve() for d in args.corpus for p in Path(d).glob("*.pdf")]
    files.sort(key=lambda f: hashlib.sha256(f"{args.seed}:{f.name}".encode()).hexdigest())
    results: list[dict] = []
    apart: collections.Counter = collections.Counter()
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool, open(report / "results.jsonl", "w") as log:
        for start in range(0, len(files), args.jobs * 4):
            if len(results) >= args.pages:
                break
            for r in pool.map(lambda f: check_page(args, f, fonts, work), files[start:start + args.jobs * 4]):
                if r["outcome"] == "apart":
                    apart[r["why"][:80]] += 1
                    log.write(json.dumps(r) + "\n")
                elif r["outcome"] != "skipped" and len(results) < args.pages:
                    results.append(r)
                    log.write(json.dumps(r) + "\n")
    shutil.rmtree(work)
    passed = [r for r in results if r["outcome"] == "pass"]
    kinds = collections.Counter(r.get("kind", "?") for r in results)
    met = len(results) >= args.pages and len(passed) == len(results)
    lines = [f"# Font extend {args.name} ({stamp})", "",
             f"- **Pages (a letter the subset lacked added from the installed font, read by Capriole, Poppler and MuPDF, drawn by Poppler "
             f"and MuPDF, the rest as before, qpdf and fonts no worse): {len(passed)} / {len(results)}**",
             f"- Fonts: {', '.join(f'{k} {n}' for k, n in kinds.most_common())}; "
             f"{', '.join(f'{k} {n}' for k, n in collections.Counter(Path(r.get('full', '?')).name for r in results).most_common(8))}",
             f"- Apart, not extended: {', '.join(f'{n} × {k}' for k, n in apart.most_common()) or 'none'}",
             f"- Exit criterion (at least {args.pages} pages, every one): **{'met' if met else 'not met'}**", "", "## Failures", ""]
    lines += [f"- {Path(r['file']).name} ({r.get('font')} {r.get('kind')}, {r.get('word')}+{r.get('letter')}): "
              + "; ".join(f"{c['check']}: {c['detail']}" for c in r["checks"])[:600] for r in results if r["outcome"] == "fail"] or ["- none"]
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:7]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
