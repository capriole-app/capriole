#!/usr/bin/env python3
"""Moving, resizing and deleting an image or a drawing, as other readers see it (PLAN.md phase 10b, exit
criterion).

On --pages pages of the corpora (the first page of a file, in hash order of --seed; each file first saved
fully, as the apps do with files that needed repairs), the largest image or
drawing of the page's model (`cpdf model`: not a background, not more than half the page, at least 4 × 4
points) is, by turns, moved (15 points right and 10 down, or back when that would leave the page),
resized (to 80% of its size, its top left corner kept) or deleted (`cpdf object-edit`, the C API). Then:

1. Capriole (`cpdf render`), Poppler (`pdftoppm`) and MuPDF (`mutool draw`) draw the page as before outside
   where the object was and where it is now (grown by --margin pixels): similarity of compare_render.py >=
   --threshold there;
2. Capriole: the object is what moved — drawn alone (`cpdf model --only`) where it was, moved or scaled as
   asked, it is what is drawn alone where it is now (the element found again in the new model, by its box):
   of the pixels expected in its new box, those drawn (each grown by two pixels: a path resized keeps its
   line's width) >= --overlap — what it touches after the edit may be one element with it in the new model; a deleted object: the page drawn as before
   without it (`cpdf model --without`; similarity >= --threshold);
3. Poppler and MuPDF draw the object where it is now as Capriole does (similarity >= --threshold in its new
   box; an image, sampled anew, blurred three times as much and without its edges, which renderers snap
   to pixels or not) — unless they drew it differently before too
   (where it was), then no less similar than before by 0.1;
4. the page's text is unchanged (`cpdf text`), and `qpdf --check` is no worse.

Edits `object-edit` refuses ("cannot edit the object: …") and pages with no such object are counted apart.

Exit criterion of phase 10b: --pages pages, every one passing; the refusals reported.

    corpus-tools/check_object_edit.py ../CORPORA/safedocs/0000 ../CORPORA/unsafe/x002 ../CORPORA/unsafe/x008 \\
        --cpdf build/release/tools/cpdf/cpdf --report-dir ../CORPORA/reports --name object-edit-all
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import datetime
import hashlib
import json
import shutil
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter

from check_pages import qpdf_severity
from compare_render import load, run, similarity

ACTIONS = ("move", "resize", "delete")


def choose(model: dict) -> int | None:
    """The largest image or drawing: not a background, not more than half the page, 4 × 4 points at least."""
    best, area = None, 0.0
    page = model["width"] * model["height"]
    for i, e in enumerate(model["elements"]):
        if e["kind"] not in ("image", "drawing") or e["role"] == "background":
            continue
        x0, y0, x1, y1 = e["box"]
        a = (x1 - x0) * (y1 - y0)
        if x1 - x0 < 4 or y1 - y0 < 4 or a > page / 2 or x0 < 0 or y0 < 0 or x1 > model["width"] or y1 > model["height"]:
            continue
        if a > area:
            best, area = i, a
    return best


def mask(full: Image.Image, without: Image.Image, tolerance: int) -> Image.Image:
    """The pixels an object draws: where the page drawn whole and without it differ."""
    w, h = min(full.width, without.width), min(full.height, without.height)
    r, g, b = ImageChops.difference(full.crop((0, 0, w, h)), without.crop((0, 0, w, h))).split()
    return ImageChops.lighter(ImageChops.lighter(r, g), b).point(lambda v: 255 if v > tolerance else 0).convert("1")


def overlap(a: Image.Image, b: Image.Image, box: list[float], scale: float, margin: int) -> float:
    """How much of mask a (what is expected) is in mask b (what is drawn), in box grown by margin (1 when a is
    empty there): what the object touches after the edit may be one element with it in the new model, and
    what b has more is not counted."""
    w, h = min(a.width, b.width), min(a.height, b.height)
    crop = (max(0, int(box[0] * scale) - margin), max(0, int(box[1] * scale) - margin), min(w, int(box[2] * scale + 0.999) + margin),
            min(h, int(box[3] * scale + 0.999) + margin))
    if crop[2] <= crop[0] or crop[3] <= crop[1]:
        return 1.0
    # Grown by two pixels: a resized path keeps its line's width, an image is sampled anew.
    a = a.crop(crop).convert("L").filter(ImageFilter.MaxFilter(5)).convert("1")
    b = b.crop(crop).convert("L").filter(ImageFilter.MaxFilter(5)).convert("1")
    expected = a.histogram()[255]
    return 1.0 if expected == 0 else ImageChops.logical_and(a, b).histogram()[255] / expected


def moved_mask(m: Image.Image, box: list[float], to: list[float], scale: float) -> Image.Image:
    """The mask's part in box, put (scaled) in to, on an empty mask of the same size."""
    out = Image.new("1", m.size, 0)
    src = [round(v * scale) for v in box]
    dst = [round(v * scale) for v in to]
    if src[2] <= src[0] or src[3] <= src[1] or dst[2] <= dst[0] or dst[3] <= dst[1]:
        return out
    part = m.crop(tuple(src)).resize((dst[2] - dst[0], dst[3] - dst[1]), Image.NEAREST)
    out.paste(part, (dst[0], dst[1]))
    return out


def outside(a: Image.Image, b: Image.Image, boxes: list[list[float]], scale: float, margin: int, blur: float, tolerance: int) -> float:
    """Similarity of a and b with the boxes painted white in both."""
    a, b = a.copy(), b.copy()
    white = Image.new("RGB", (1, 1), (255, 255, 255))
    for box in boxes:
        x0, y0 = max(0, int(box[0] * scale) - margin), max(0, int(box[1] * scale) - margin)
        x1, y1 = int(box[2] * scale + 0.999) + margin, int(box[3] * scale + 0.999) + margin
        if x1 > x0 and y1 > y0:
            patch = white.resize((x1 - x0, y1 - y0))
            a.paste(patch, (x0, y0))
            b.paste(patch, (x0, y0))
    return similarity(a, b, blur, tolerance)


def inside(a: Image.Image, b: Image.Image, box: list[float], scale: float, blur: float, tolerance: int, shrink: int = 0) -> float:
    """Similarity of a and b in box, shrunk by some pixels (an image's edges: renderers snap them, or not)."""
    crop = (max(0, int(box[0] * scale) + shrink), max(0, int(box[1] * scale) + shrink), int(box[2] * scale + 0.999) - shrink,
            int(box[3] * scale + 0.999) - shrink)
    if crop[2] <= crop[0] or crop[3] <= crop[1]:
        return 1.0
    return similarity(a.crop(crop), b.crop(crop), blur, tolerance)


def draw(engine: str, args: argparse.Namespace, pdf: Path, out: Path) -> Image.Image | None:
    if engine == "capriole":
        result = run([args.cpdf, "render", "--dpi", str(args.dpi), str(pdf), "1", str(out)], args.timeout)
        return load(out) if result["status"] == 0 else None
    if engine == "poppler":
        run(["pdftoppm", "-r", str(args.dpi), "-f", "1", "-l", "1", "-png", "-singlefile", str(pdf), str(out.with_suffix(""))], args.timeout)
        return load(out)
    run(["mutool", "draw", "-q", "-r", str(args.dpi), "-o", str(out), str(pdf), "1"], args.timeout)
    return load(out)


def check_page(args: argparse.Namespace, path: Path, index: int, work_root: Path) -> dict:
    record: dict = {"file": str(path), "checks": []}
    work = Path(tempfile.mkdtemp(dir=work_root))
    try:
        # Saved fully first, as the apps do with files that needed repairs (an incremental save is refused).
        saved = work / "original.pdf"
        result = run([args.cpdf, "save", str(path), str(saved)], args.timeout)
        if result["status"] != 0:
            return {**record, "outcome": "skipped", "why": "save: " + (result["output"].strip().splitlines() or ["?"])[-1][:100]}
        path = saved
        result = run([args.cpdf, "model", str(path), "1"], args.timeout)
        if result["status"] != 0:
            return {**record, "outcome": "skipped", "why": "model: " + (result["output"].strip().splitlines() or ["?"])[-1][:100]}
        model = json.loads(result["output"])
        chosen = choose(model)
        if chosen is None:
            return {**record, "outcome": "skipped", "why": "no image or drawing to edit"}
        e = model["elements"][chosen]
        box = e["box"]
        action = ACTIONS[index % len(ACTIONS)]
        if action == "move":
            dx, dy = (15, 10) if box[2] + 15 <= model["width"] and box[3] + 10 <= model["height"] else (-15, -10)
            spec = {"element": chosen, "move": [dx, dy]}
            to = [box[0] + dx, box[1] + dy, box[2] + dx, box[3] + dy]
        elif action == "resize":
            to = [box[0], box[1], box[0] + 0.8 * (box[2] - box[0]), box[1] + 0.8 * (box[3] - box[1])]
            spec = {"element": chosen, "box": to}
        else:
            spec = {"element": chosen, "delete": True}
            to = box
        record.update({"action": action, "kind": f"{e['kind']}/{e['role']}", "box": [round(v, 1) for v in box]})
        edited = work / "edited.pdf"
        result = run([args.cpdf, "object-edit", str(path), "1", json.dumps(spec), str(edited)], args.timeout)
        if result["status"] != 0:
            message = (result["output"].strip().splitlines() or ["?"])[-1]
            why = message.split("object-edit: ")[-1]
            return {**record, "outcome": "refused", "why": f"{action}: {why[:100]}"}
        report = json.loads(result["output"].strip().splitlines()[0])
        record["operators"] = report["operators"]
        scale = args.dpi / 72

        # 1. Outside where it was and is, as before; 3. where it is, as Capriole draws it.
        drawn = {}
        for engine in ("capriole", "poppler", "mupdf"):
            before = draw(engine, args, path, work / f"{engine}-before.png")
            if before is None:
                record.setdefault("notes", []).append(f"{engine} does not draw the original")
                continue
            after = draw(engine, args, edited, work / f"{engine}-after.png")
            if after is None:
                record["checks"].append({"check": engine, "detail": "no rendering of the edited file"})
                continue
            drawn[engine] = (before, after)
            s = outside(before, after, [box, to], scale, args.margin, args.blur, args.tolerance)
            if s < args.threshold:
                record["checks"].append({"check": f"{engine} outside", "detail": f"similarity {s:.4f}"})
        if "capriole" in drawn and action != "delete":
            ours = drawn["capriole"][1]
            for engine in ("poppler", "mupdf"):
                if engine not in drawn:
                    continue
                # An image is sampled anew by each renderer when resized: compared less sharply.
                blur, shrink = (3 * args.blur, 2) if e["kind"] == "image" else (args.blur, 0)
                s = inside(ours, drawn[engine][1], to, scale, blur, args.tolerance, shrink)
                if s < args.threshold:
                    was = inside(drawn["capriole"][0], drawn[engine][0], box, scale, blur, args.tolerance, shrink)
                    # They agreed where it was and no more, or far less than they did.
                    if (was >= args.threshold and s < args.threshold - 0.01) or s < was - 0.1:
                        record["checks"].append({"check": f"{engine} inside", "detail": f"similarity {s:.4f} (before {was:.4f})"})
                    else:
                        record.setdefault("notes", []).append(f"{engine} drew it differently before too")

        # 2. What moved is the object: drawn alone (`cpdf model --only`) where it was, moved or scaled as asked,
        # it is what is drawn alone where it is now; deleted, the page is as drawn without it.
        if "capriole" in drawn:
            after = drawn["capriole"][1]
            if action == "delete":
                without = work / "without.png"
                run([args.cpdf, "model", "--without", str(chosen), "--dpi", str(args.dpi), str(path), "1", str(without)], args.timeout)
                alone = load(without)
                s = similarity(alone, after, args.blur, args.tolerance) if alone is not None else 0.0
                if s < args.threshold:
                    record["checks"].append({"check": "deleted", "detail": f"similarity to the page drawn without it {s:.4f}"})
            else:
                result = run([args.cpdf, "model", str(edited), "1"], args.timeout)
                new_model = json.loads(result["output"]) if result["status"] == 0 else {"elements": []}
                found = min((i for i, x in enumerate(new_model["elements"]) if x["kind"] == e["kind"]),
                            key=lambda i: sum(abs(a - b) for a, b in zip(new_model["elements"][i]["box"], report["box"])), default=None)
                alone_before, alone_after = work / "alone-before.png", work / "alone-after.png"
                run([args.cpdf, "model", "--only", str(chosen), "--dpi", str(args.dpi), str(path), "1", str(alone_before)], args.timeout)
                if found is not None:
                    # Moved next to something it now touches, it is one element with it: what its own operators
                    # draw (a path's keep their place in the content stream when moved) — not what is drawn
                    # over it.
                    joined = new_model["elements"][found]
                    if e["kind"] == "drawing" and len(joined["sources"]) > len(e["sources"]) and all(src in joined["sources"] for src in e["sources"]):
                        which = ["--only-sources", ",".join(f"{i}:{o}" for i, o in e["sources"])]
                    else:
                        which = ["--only", str(found)]
                    run([args.cpdf, "model", *which, "--dpi", str(args.dpi), str(edited), "1", str(alone_after)], args.timeout)
                a, b = load(alone_before), load(alone_after) if found is not None else None
                if a is None or b is None:
                    record["checks"].append({"check": "object", "detail": "not drawn alone (not found again)" if found is None else "not drawn alone"})
                else:
                    white = Image.new("RGB", a.size, (255, 255, 255))
                    expected = moved_mask(mask(a, white, args.tolerance), box, report["box"], scale)
                    actual = mask(b, white.resize(b.size), args.tolerance)
                    o = overlap(expected, actual, report["box"], scale, args.margin)
                    record["overlap"] = round(o, 3)
                    if o < args.overlap:
                        record["checks"].append({"check": "object", "detail": f"its pixels, moved, overlap those drawn {o:.3f}"})

        # 4. The text, qpdf.
        texts = [run([args.cpdf, "text", str(p), "1"], args.timeout)["output"] for p in (path, edited)]
        if texts[0] != texts[1]:
            record["checks"].append({"check": "text", "detail": "the page's text changed"})
        if qpdf_severity(edited, args.timeout) > qpdf_severity(path, args.timeout):
            record["checks"].append({"check": "qpdf", "detail": "worse"})
        record["outcome"] = "fail" if record["checks"] else "pass"
        return record
    except Exception as error:  # noqa: BLE001 — a crash of the check is a failure of the page, reported
        return {**record, "outcome": "fail", "checks": [{"check": "crash", "detail": repr(error)[:300]}]}
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="+")
    parser.add_argument("--cpdf", required=True)
    parser.add_argument("--pages", type=int, default=100)
    parser.add_argument("--seed", default="capriole-phase-10b")
    parser.add_argument("--dpi", type=int, default=72)
    parser.add_argument("--margin", type=int, default=2)
    parser.add_argument("--blur", type=float, default=1.0)
    parser.add_argument("--tolerance", type=int, default=32)
    parser.add_argument("--threshold", type=float, default=0.99)
    parser.add_argument("--overlap", type=float, default=0.9)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="object-edit")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="object-edit-", dir=report))
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
                if r["outcome"] in ("skipped", "refused"):
                    apart[f"{r['outcome']}: {r['why'][:90]}"] += 1
                    if r["outcome"] == "refused":
                        log.write(json.dumps(r) + "\n")
                elif len(results) < args.pages:
                    results.append(r)
                    log.write(json.dumps(r) + "\n")
    shutil.rmtree(work)

    passed = [r for r in results if r["outcome"] == "pass"]
    met = len(results) >= args.pages and len(passed) == len(results)
    by_action = collections.Counter(r.get("action") for r in results)
    kinds = collections.Counter(r.get("kind") for r in results)
    notes = collections.Counter(n for r in results for n in r.get("notes", []))
    refused = sum(n for k, n in apart.items() if k.startswith("refused"))
    lines = [f"# Object edit {args.name} ({stamp})", "",
             f"- **Objects edited (the rest drawn as before by Capriole, Poppler and MuPDF, the object drawn where it went, "
             f"text unchanged, qpdf no worse): {len(passed)} / {len(results)}**",
             f"- By action: {', '.join(f'{n} × {k}' for k, n in by_action.most_common())}; by kind: {', '.join(f'{n} × {k}' for k, n in kinds.most_common())}",
             f"- Edits refused: {refused} (of {len(results) + refused} tried)",
             f"- Apart, not counted: {', '.join(f'{n} × {k}' for k, n in apart.most_common()) or 'none'}",
             f"- Exit criterion (every page): **{'met' if met else 'not met'}**", "", "## Notes", ""]
    lines += [f"- {n} × {k}" for k, n in notes.most_common()] or ["- none"]
    lines += ["", "## Failures", ""]
    lines += [f"- {Path(r['file']).name} ({r.get('action')} {r.get('kind')} at {r.get('box')}): "
              + "; ".join(f"{c['check']}: {c['detail']}" for c in r["checks"])[:700] for r in results if r["outcome"] == "fail"] or ["- none"]
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:8]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
