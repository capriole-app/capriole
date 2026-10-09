#!/usr/bin/env python3
"""The page model against what the page draws (PLAN.md phase 10a, exit criterion).

On --pages pages of the corpora (the first page of a file, in hash order of --seed; pages whose model has no
element are left out, and counted), `cpdf model` (the C API's JSON) is checked:

1. coverage: every command the page's content streams draw (`drawn`) is in one element exactly — a text
   run in the paragraphs whose characters it shows, and in no other element —, every element's commands
   are among them, and no character is in two paragraphs;
2. provenance: for up to --elements elements of the page (all, or spread over them by kind), the page drawn
   without the element (`cpdf model --without`: its commands left out, a paragraph's glyphs taken out of
   its runs) differs from the page drawn whole (`cpdf render`, the same renderer) only inside the element's
   box (grown by --margin pixels) — what an element covers is what its commands draw. An element whose
   removal changes nothing (drawn under something else, white on white) is counted apart, not failed.

Exit criterion of phase 10a: --pages pages, every one passing; the elements by kind and role reported.

    corpus-tools/check_page_model.py ../CORPORA/safedocs/0000 ../CORPORA/unsafe/x002 ../CORPORA/unsafe/x008 \\
        --cpdf build/release/tools/cpdf/cpdf --report-dir ../CORPORA/reports --name page-model-all
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

from PIL import Image, ImageChops

from compare_render import load, run


def coverage(model: dict) -> list[str]:
    """What breaks the model's coverage of what the page draws (empty: nothing)."""
    problems = []
    drawn = set(model["drawn"])
    owners: dict[int, list[str]] = collections.defaultdict(list)
    for i, e in enumerate(model["elements"]):
        for c in e["commands"]:
            if c not in drawn:
                problems.append(f"element {i} ({e['kind']}) has command {c}, not drawn by the page's content")
            owners[c].append(e["kind"])
    for c in sorted(drawn):
        kinds = owners.get(c, [])
        if not kinds:
            problems.append(f"command {c} is in no element")
        elif len(kinds) > 1 and set(kinds) != {"paragraph"}:
            problems.append(f"command {c} is in {len(kinds)} elements ({', '.join(kinds)})")
    seen: set[int] = set()
    for i, p in enumerate(model["paragraphs"]):
        twice = seen.intersection(p["chars"])
        if twice:
            problems.append(f"paragraph {i} has characters of another ({len(twice)})")
        seen.update(p["chars"])
    return problems


def spread(elements: list[dict], most: int) -> list[int]:
    """Up to most elements (not annotations: their appearances are not the page's content), spread by kind;
    text with nothing to read (mostly spaces, which draw nothing) last."""
    by_kind: dict[str, list[int]] = collections.defaultdict(list)
    for i, e in enumerate(elements):
        if e["kind"] not in ("annotation", "glyphs"):
            by_kind[f"{e['kind']}/{e['role']}"].append(i)
    chosen: list[int] = []
    queues = [list(v) for v in by_kind.values()] + [[i for i, e in enumerate(elements) if e["kind"] == "glyphs"]]
    while len(chosen) < most and any(queues):
        for q in queues:
            if q and len(chosen) < most:
                # From each kind in turn, spread over the page: the middle of what is left first.
                chosen.append(q.pop(len(q) // 2))
    return sorted(chosen)


def changed_outside(full: Image.Image, without: Image.Image, box: list[float], scale: float, margin: int, tolerance: int) -> tuple[int, int]:
    """Pixels that changed (a channel by more than tolerance) in all, and of them outside the box."""
    w, h = min(full.width, without.width), min(full.height, without.height)
    diff = ImageChops.difference(full.crop((0, 0, w, h)), without.crop((0, 0, w, h)))
    r, g, b = diff.split()
    worst = ImageChops.lighter(ImageChops.lighter(r, g), b).point(lambda v: 255 if v > tolerance else 0)
    total = worst.histogram()[255]
    if total == 0:
        return 0, 0
    x0 = max(0, int(box[0] * scale) - margin)
    y0 = max(0, int(box[1] * scale) - margin)
    x1 = min(w, int(box[2] * scale + 0.999) + margin)
    y1 = min(h, int(box[3] * scale + 0.999) + margin)
    inside = worst.crop((x0, y0, x1, y1)).histogram()[255] if x1 > x0 and y1 > y0 else 0
    return total, total - inside


def check_page(args: argparse.Namespace, path: Path, work_root: Path) -> dict:
    record: dict = {"file": str(path), "checks": []}
    work = Path(tempfile.mkdtemp(dir=work_root))
    try:
        result = run([args.cpdf, "model", str(path), "1"], args.timeout)
        if result["status"] != 0:
            return {**record, "outcome": "skipped", "why": "model: " + (result["output"].strip().splitlines() or ["?"])[-1][:120]}
        model = json.loads(result["output"])
        elements = model["elements"]
        if not elements:
            return {**record, "outcome": "skipped", "why": "an empty page"}
        record["elements"] = collections.Counter(f"{e['kind']}/{e['role']}" for e in elements)
        record["zones"] = collections.Counter(e["zone"] for e in elements)
        record["paragraphs"] = len(model["paragraphs"])
        record["columns"] = len(model["columns"])
        for problem in coverage(model)[:20]:
            record["checks"].append({"check": "coverage", "detail": problem})

        full = work / "full.png"
        rendered = run([args.cpdf, "render", "--dpi", str(args.dpi), str(path), "1", str(full)], args.timeout)
        image = load(full) if rendered["status"] == 0 else None
        if image is None:
            return {**record, "outcome": "skipped", "why": "render: " + (rendered["output"].strip().splitlines() or ["?"])[-1][:120]}
        scale = args.dpi / 72
        record["tried"] = 0
        record["unseen"] = collections.Counter()
        for i in spread(elements, args.elements):
            e = elements[i]
            out = work / f"without-{i}.png"
            drawn = run([args.cpdf, "model", "--without", str(i), "--dpi", str(args.dpi), str(path), "1", str(out)], args.timeout)
            without = load(out) if drawn["status"] == 0 else None
            if without is None:
                record["checks"].append({"check": "without", "detail": f"element {i}: " + (drawn["output"].strip().splitlines() or ["?"])[-1][:120]})
                continue
            record["tried"] += 1
            total, outside = changed_outside(image, without, e["box"], scale, args.margin, args.tolerance)
            if total == 0:
                record["unseen"][e["kind"]] += 1
            elif outside > 0:
                record["checks"].append({"check": "provenance",
                                         "detail": f"element {i} ({e['kind']}/{e['role']} at {[round(v) for v in e['box']]}): "
                                                   f"{outside} of {total} pixels changed outside it"})
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
    parser.add_argument("--seed", default="capriole-phase-10a")
    parser.add_argument("--elements", type=int, default=25)
    parser.add_argument("--dpi", type=int, default=72)
    parser.add_argument("--margin", type=int, default=2)
    parser.add_argument("--tolerance", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="page-model")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="page-model-", dir=report))
    files = [p.resolve() for d in args.corpus for p in Path(d).glob("*.pdf")]
    files.sort(key=lambda f: hashlib.sha256(f"{args.seed}:{f.name}".encode()).hexdigest())
    results: list[dict] = []
    apart: collections.Counter = collections.Counter()
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool, open(report / "results.jsonl", "w") as log:
        for start in range(0, len(files), args.jobs * 4):
            if len(results) >= args.pages:
                break
            batch = files[start:start + args.jobs * 4]
            for r in pool.map(lambda f: check_page(args, f, work), batch):
                if r["outcome"] == "skipped":
                    apart[r["why"][:80]] += 1
                elif len(results) < args.pages:
                    results.append(r)
                    log.write(json.dumps(r) + "\n")
    shutil.rmtree(work)

    passed = [r for r in results if r["outcome"] == "pass"]
    met = len(results) >= args.pages and len(passed) == len(results)
    kinds: collections.Counter = collections.Counter()
    zones: collections.Counter = collections.Counter()
    unseen: collections.Counter = collections.Counter()
    for r in results:
        kinds.update(r.get("elements", {}))
        zones.update(r.get("zones", {}))
        unseen.update(r.get("unseen", {}))
    lines = [f"# Page model {args.name} ({stamp})", "",
             f"- **Pages whose model covers what they draw, each element drawing inside its box: {len(passed)} / {len(results)}**",
             f"- Elements removed and drawn again: {sum(r.get('tried', 0) for r in results)} "
             f"(changing nothing — drawn under something else, white on white, spaces: {', '.join(f'{n} × {k}' for k, n in unseen.most_common()) or 'none'})",
             f"- Paragraphs {sum(r.get('paragraphs', 0) for r in results)}, columns {sum(r.get('columns', 0) for r in results)}",
             f"- Elements: {', '.join(f'{n} × {k}' for k, n in kinds.most_common())}",
             f"- Zones: {', '.join(f'{n} × {k}' for k, n in zones.most_common())}",
             f"- Apart, not counted: {', '.join(f'{n} × {k}' for k, n in apart.most_common()) or 'none'}",
             f"- Exit criterion (every page): **{'met' if met else 'not met'}**", "", "## Failures", ""]
    lines += [f"- {Path(r['file']).name}: " + "; ".join(f"{c['check']}: {c['detail']}" for c in r["checks"])[:900]
              for r in results if r["outcome"] == "fail"] or ["- none"]
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:9]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
