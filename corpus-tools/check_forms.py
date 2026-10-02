#!/usr/bin/env python3
"""Forms filled by Capriole, as other readers see them (PLAN.md phase 8e, exit criterion).

Every fillable field of every form — the files of the corpus whose AcroForm has fields, and forms made
here with a field of every kind (upright, and on a page turned by /Rotate 90 with widgets turned by
/MK /R) — is given a value different from the one it has (`cpdf form fill`, the C API, saved
incrementally):

- text: "Capriole è ok" (two lines in a multiline field; cut to /MaxLen);
- check box: its on state, or "Off" when it is on; radio button: another of its states;
- combo box: another option (any text when editable); list box: other options (two when multi-select).

Each output must:
1. read back in MuPDF (`mutool run`, an engine that does not share Capriole's code) with exactly the
   values written, for every widget of every field;
2. read back in Capriole (`cpdf form list`) the same;
3. show the change in every engine (Capriole, Poppler, MuPDF): ≥ --min-change of the pixels of each
   visible widget's rectangle (two around it included) differ from the form before;
4. pass `qpdf --check` no worse than the original.

Fields that cannot be filled (read-only, signatures, push buttons) are counted, not filled. Exit
criterion of phase 8e (engine): every field of every form passes.

    corpus-tools/check_forms.py ../CORPORA/safedocs/0000 --cpdf build/release/tools/cpdf/cpdf \\
        --report-dir ../CORPORA/reports --name forms-safedocs-0000
"""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import json
import re
import shutil
import tempfile
from pathlib import Path

from check_annotations import changed_in, draw_all
from check_pages import qpdf_severity
from compare_render import run

MUTOOL_SCRIPT = """
var doc = Document.openDocument(scriptArgs[0]);
var n = doc.countPages();
for (var i = 0; i < n; i++) {
  var widgets = doc.loadPage(i).getWidgets();
  for (var j = 0; j < widgets.length; j++) {
    var w = widgets[j];
    // /V as MuPDF's parser reads it, from the widget or the field it inherits it from.
    var node = w.getObject(), v = null;
    for (var depth = 0; depth < 32 && node && node.isDictionary(); depth++) {
      var here = node.get("V");
      if (here && !here.isNull()) { v = here; break; }
      node = node.get("Parent");
    }
    var raw = null;
    if (v && v.isArray()) { raw = []; for (var k = 0; k < v.length; k++) raw.push(v.get(k).asString()); }
    print(JSON.stringify({page: i, name: w.getName(), type: w.getFieldType(), value: w.getValue(), raw: raw}));
  }
}
"""

TEXT = "Capriole è ok"


def pdf(objects: list[tuple[int, str]], size: int) -> bytes:
    out = "%PDF-1.7\n"
    offsets: dict[int, int] = {}
    for number, body in objects:
        offsets[number] = len(out.encode("latin-1"))
        out += f"{number} 0 obj\n{body}\nendobj\n"
    xref = len(out.encode("latin-1"))
    out += f"xref\n0 {size}\n0000000000 65535 f \n"
    out += "".join(f"{offsets[n]:010d} 00000 n \n" if n in offsets else "0000000000 65535 f \n" for n in range(1, size))
    out += f"trailer\n<</Size {size}/Root 1 0 R>>\nstartxref\n{xref}\n%%EOF\n"
    return out.encode("latin-1")


def made_form(rotate: int) -> bytes:
    """A form with a field of every kind. Turned: the same form as shown, on a landscape page turned by
    /Rotate 90 — each widget's rectangle in the page's space, its appearance turned back (/MK /R 90)."""
    r = f"/MK<</R {rotate}>>" if rotate else ""
    mk = f"/MK<</BC[0 0 0]/BG[0.92 0.92 1]{'/R %d' % rotate if rotate else ''}>>"
    w = "/Subtype/Widget/P 3 0 R/F 4"

    def stream(n: int, data: str) -> tuple[int, str]:
        return n, f"<</Type/XObject/Subtype/Form/BBox[0 0 14 14]/Length {len(data)}>>\nstream\n{data}\nendstream"

    annots = " ".join(f"{n} 0 R" for n in (10, 12, 13, 14, 15, 16, 17, 18, 20, 21, 22, 25))
    fields = " ".join(f"{n} 0 R" for n in (10, 11, 13, 14, 15, 16, 19, 20, 21, 22, 25))
    objects = [
        (1, "<</Type/Catalog/Pages 2 0 R/AcroForm 4 0 R>>"),
        (2, "<</Type/Pages/Kids[3 0 R]/Count 1>>"),
        (3, f"<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 400]{'/Rotate %d' % rotate if rotate else ''}/Annots[{annots}]>>"),
        (4, f"<</Fields[{fields}]/DR<</Font<</Helv 5 0 R/TiRo 6 0 R>>>>/DA(/Helv 0 Tf 0 g)>>"),
        (5, "<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding>>"),
        (6, "<</Type/Font/Subtype/Type1/BaseFont/Times-Roman/Encoding/WinAnsiEncoding>>"),
        (10, f"<</FT/Tx/T(name){w}/Rect[20 360 220 380]{mk}>>"),
        (11, "<</FT/Tx/T(person)/Kids[12 0 R]>>"),
        (12, f"<</T(address)/Parent 11 0 R/Ff 4096/Q 1/DA(/TiRo 11 Tf 0 0 0.6 rg){w}/Rect[20 300 220 350]{mk}>>"),
        (13, f"<</FT/Tx/T(code)/Ff 16777216/MaxLen 5{w}/Rect[20 270 120 290]{mk}>>"),
        (14, f"<</FT/Tx/T(pin)/Ff 8192/Q 2{w}/Rect[130 270 220 290]{mk}>>"),
        (15, f"<</FT/Btn/T(agree)/V/Off/AS/Off/AP<</N<</Yes 30 0 R/Off 31 0 R>>>>{w}/Rect[20 240 34 254]{r}>>"),
        (16, f"<</FT/Btn/T(bare)/DA(0 0 1 rg){w}/Rect[40 240 54 254]{mk}>>"),
        (17, f"<</Parent 19 0 R/AS/Red/AP<</N<</Red 30 0 R/Off 31 0 R>>>>{w}/Rect[20 210 34 224]{r}>>"),
        (18, f"<</Parent 19 0 R/AS/Off/AP<</N<</Blue 30 0 R/Off 31 0 R>>>>{w}/Rect[40 210 54 224]{r}>>"),
        (19, "<</FT/Btn/Ff 49152/T(color)/Kids[17 0 R 18 0 R]/V/Red>>"),
        (20, f"<</FT/Ch/Ff 131072/T(city)/Opt[(Roma)(Milano)(Torino)]/V(Roma){w}/Rect[20 180 120 200]{mk}>>"),
        (21, f"<</FT/Ch/Ff 2097152/T(langs)/Opt[[(it)(Italiano)][(en)(English)][(de)(Deutsch)]]{w}/Rect[130 150 220 200]{mk}>>"),
        (22, f"<</FT/Ch/T(one)/Opt[(alfa)(beta)(gamma)]{w}/Rect[20 100 120 160]{mk}>>"),
        (25, f"<</FT/Tx/T(big)/DA(/Helv 14 Tf 0.6 0 0 rg){w}/Rect[130 60 280 120]/Ff 4096{mk}>>"),
        stream(30, "0 g 2 2 10 10 re f"),
        stream(31, ""),
    ]
    if rotate:
        # Shown upright as the form above: page x is the shown y, page y the shown x (y down from the top).
        def turn(m: re.Match) -> str:
            x0, y0, x1, y1 = (float(v) for v in m.group(1).split())
            return f"/Rect[{400 - y1:g} {x0:g} {400 - y0:g} {x1:g}]"
        objects = [(n, re.sub(r"/Rect\[([^\]]*)\]", turn, body).replace("/MediaBox[0 0 300 400]", "/MediaBox[0 0 400 300]")) for n, body in objects]
    return pdf(objects, 32)


def value_for(field: dict) -> list[str] | None:
    """A value the field takes, different from the one it has; None when there is none."""
    current = field["value"]
    options = [o["value"] for o in field["options"]]
    kind = field["type"]
    if kind == "text":
        text = "Capriole è ok\nseconda riga" if field["multiline"] else TEXT
        if field["maxLength"] is not None:
            text = text[: field["maxLength"]]
        return [text] if text and [text] != current else ([TEXT[:1]] if current != [TEXT[:1]] else None)
    if kind == "checkbox":
        on = options[0] if options else "Yes"
        return ["Off"] if current == [on] else [on]
    if kind == "radio":
        others = [o for o in options if [o] != current]
        return [others[0]] if others else (["Off"] if current != ["Off"] else None)
    if kind == "combobox":
        others = [o for o in options if [o] != current]
        return [others[0]] if others else (["Capriole"] if field["editable"] else None)
    if kind == "listbox":
        others = [o for o in options if o not in current]
        chosen = others[:2] if field["multiSelect"] else others[:1]
        return chosen or None
    return None


def mutool_cannot_draw_widgets(path: Path, page: int, timeout: float) -> bool:
    """MuPDF makes form appearances itself in some files, and draws nothing where it fails ("cannot create
    appearance stream"): then it shows no value of the form, before filling or after."""
    drawn = run(["mutool", "draw", "-q", "-o", "/dev/null", "-F", "png", str(path), str(page + 1)], timeout)
    return "cannot create appearance stream" in drawn["output"]


def mutool_values(path: Path, script: Path, timeout: float) -> list[dict] | None:
    result = run(["mutool", "run", str(script), str(path)], timeout)
    if result["status"] != 0:
        return None
    out = []
    for line in result["output"].splitlines():
        try:
            out.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return out


def mutool_reads(field: dict, value: list[str], widget: dict) -> bool:
    """Whether MuPDF reads value: getValue() for one value (line breaks may come back as CR); /V itself for several."""
    if len(value) > 1:
        return widget.get("raw") == value
    text = value[0] if value else ""
    return widget["value"] in {text, text.replace("\n", "\r")}


def check_form(args: argparse.Namespace, path: Path, work: Path, script: Path) -> dict:
    result: dict = {"file": str(path), "outcome": "pass", "fields": 0, "filled": 0, "not_fillable": 0, "checks": []}

    def fail(what: str, detail: str) -> None:
        result["outcome"] = "fail"
        result["checks"].append({"check": what, "detail": detail[:300]})

    listed = run([args.cpdf, "form", "list", str(path)], args.timeout)
    try:
        fields = json.loads(listed["output"]) if listed["status"] == 0 else None
    except json.JSONDecodeError:
        fields = None
    if fields is None:
        if ": open: " in listed["output"]:
            result["outcome"] = "unreadable"  # not opened (a password, not a PDF): no form to fill
            return result
        fail("list", listed["output"][:200])
        return result
    if not fields:
        result["outcome"] = "skipped"
        return result
    # As the apps do: a file that needed repairs is saved fully and read back before it is edited; every
    # file starts from its full save, so that the comparisons are with what is filled.
    sound = work / f"{path.stem}-sound.pdf"
    saved = run([args.cpdf, "save", str(path), str(sound)], args.timeout)
    if saved["status"] != 0:
        fail("save", saved["output"])
        return result
    path = sound
    listed = run([args.cpdf, "form", "list", str(path)], args.timeout)
    try:
        fields = json.loads(listed["output"]) if listed["status"] == 0 else []
    except json.JSONDecodeError:
        fields = []
    result["fields"] = len(fields)
    if not fields:
        fail("save", "the full save lost the form's fields")
        return result
    names = [f["name"] for f in fields]
    assignments, wanted = [], {}
    for f in fields:
        if not f["fillable"]:
            result["not_fillable"] += 1
            continue
        value = value_for(f)
        if value is None:
            fail("value", f"{f['name']}: no value differs from the current one")
            continue
        assignments.append(f"{f['ref'][0]}={json.dumps(value[0] if len(value) == 1 and f['type'] != 'listbox' else value)}")
        wanted[f["ref"][0]] = (f, value)
    result["filled"] = len(wanted)
    if not assignments:
        if result["outcome"] == "pass":
            result["outcome"] = "nothing"  # only signatures, push buttons, read-only fields
        return result
    out = work / f"{path.stem}-filled.pdf"
    filled = run([args.cpdf, "form", "fill", str(path), str(out), *assignments], args.timeout)
    if filled["status"] != 0:
        fail("fill", filled["output"])
        return result
    if "incremental" not in filled["output"]:
        result["full_save"] = True  # the file needed repairs: written anew

    # 2. Capriole reads them back.
    reread = run([args.cpdf, "form", "list", str(out)], args.timeout)
    try:
        after = {f["ref"][0]: f for f in json.loads(reread["output"])}
    except json.JSONDecodeError:
        after = {}
        fail("reread", reread["output"][:200])
    for number, (f, value) in wanted.items():
        got = after.get(number, {}).get("value")
        expected = value if f["type"] != "text" or f["multiline"] else [value[0].replace("\n", " ")]
        if got != expected:
            fail("capriole", f"{f['name']}: read back {got!r}, wrote {value!r}")

    # 1. MuPDF reads them back, every widget of every field (by name; names shared by two fields are ambiguous).
    widgets = mutool_values(out, script, args.timeout)
    if widgets is None and mutool_values(path, script, args.timeout) is None:
        result.setdefault("notes", []).append("mutool run cannot read the form, before or after")
    elif widgets is None:
        fail("mutool", "mutool run failed on the filled form, not on the original")
    else:
        by_name: dict[str, list[dict]] = {}
        for w in widgets:
            by_name.setdefault(w["name"], []).append(w)
        for number, (f, value) in wanted.items():
            if not f["name"] or names.count(f["name"]) > 1:
                continue  # ambiguous by name
            seen = by_name.get(f["name"], [])
            on_pages = [w for w in f["widgets"] if w["page"] is not None]
            if len(seen) < len(on_pages):
                fail("mutool", f"{f['name']}: {len(seen)} widgets seen, {len(on_pages)} on pages")
            for w in seen:
                if not mutool_reads(f, value, w):
                    fail("mutool", f"{f['name']}: MuPDF reads {w['value']!r}, wrote {value!r}")
                    break

    # 3. Every engine shows the change in every visible widget.
    hidden: set[int] = set()
    pages = sorted({w["page"] for f, _ in wanted.values() for w in f["widgets"] if w["page"] is not None})
    for page in pages:
        annots = run([args.cpdf, "annot", "list", str(path), str(page + 1)], args.timeout)
        try:
            hidden |= {a["ref"][0] for a in json.loads(annots["output"]) if a["hidden"] and a["ref"]}
        except json.JSONDecodeError:
            pass
    scale = args.dpi / 72
    for page in pages:
        before = draw_all(args, path, page + 1, work, f"{path.stem}-p{page}-before")
        drawn = draw_all(args, out, page + 1, work, f"{path.stem}-p{page}-after")
        for engine in ("capriole", "pdftoppm", "mutool"):
            if before[engine] is None:
                result.setdefault("notes", []).append(f"{engine} cannot draw page {page + 1}, before or after")
                continue
            if drawn[engine] is None:
                fail(engine, f"page {page + 1} drawn before filling, not after")
                continue
            if before["capriole"] is not None and abs(before[engine].height - before["capriole"].height) > 2:
                # A damaged page read another way (another box, another page object): its widgets are elsewhere.
                result.setdefault("notes", []).append(f"{engine} draws page {page + 1} with another size: widgets not compared")
                continue
            if engine == "mutool" and mutool_cannot_draw_widgets(path, page, args.timeout):
                result.setdefault("notes", []).append(f"mutool cannot make the form's appearances on page {page + 1}, before filling either")
                continue
            width, height = before[engine].width / scale, before[engine].height / scale
            for f, value in wanted.values():
                if engine == "mutool" and not f["name"]:
                    # MuPDF does not draw the widgets of a field without a name, appearance or not.
                    result.setdefault("notes", []).append(f"mutool does not draw fields without a name: {f['ref'][0]} not compared")
                    continue
                for w in f["widgets"]:
                    # A button's widget changes when it turns on or off; widgets of other states stay as they are.
                    if f["type"] in ("checkbox", "radio") and w["onState"] and (w["onState"] in f["value"]) == (w["onState"] in value):
                        continue
                    x0, y0, x1, y1 = w["rect"]
                    if w["page"] != page or w["ref"][0] in hidden or x1 - x0 < 2 or y1 - y0 < 2 or x1 <= 0 or y1 <= 0 or x0 >= width or y0 >= height:
                        continue
                    share = changed_in(before[engine], drawn[engine], w["rect"], scale)
                    if share < args.min_change:
                        fail(engine, f"{f['name']} ({f['type']}) not shown changed on page {page + 1} ({share:.4f} of its widget)")
    # 4. No worse in qpdf.
    if qpdf_severity(out, args.timeout) > qpdf_severity(path, args.timeout):
        fail("qpdf", "qpdf --check worse than the original")
    for leftover in work.glob(f"{path.stem}-*"):
        leftover.unlink()
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="*")
    parser.add_argument("--cpdf", required=True)
    parser.add_argument("--resources", default="build/resources" if Path("build/resources/cmap").is_dir() else None)
    parser.add_argument("--dpi", type=int, default=72)
    parser.add_argument("--min-change", type=float, default=0.01)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="forms")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())
    args.resources = str(Path(args.resources).resolve()) if args.resources else None

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="forms-", dir=report))
    script = work / "fields.js"
    script.write_text(MUTOOL_SCRIPT)
    made = []
    for rotate in (0, 90):
        p = work / f"made-form-r{rotate}.pdf"
        p.write_bytes(made_form(rotate))
        made.append(p)
    # Every file is listed (the form may be in a compressed object stream); those without fields are skipped.
    files = made + [p.resolve() for d in args.corpus for p in sorted(Path(d).glob("*.pdf"))]
    results: list[dict] = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool, open(report / "results.jsonl", "w") as log:
        for r in pool.map(lambda p: check_form(args, p, work, script), files):
            results.append(r)
            log.write(json.dumps(r) + "\n")
    shutil.rmtree(work)

    counted = [r for r in results if r["outcome"] in ("pass", "fail")]
    nothing = [r for r in results if r["outcome"] == "nothing"]
    unreadable = [r for r in results if r["outcome"] == "unreadable"]
    passed = [r for r in counted if r["outcome"] == "pass"]
    fields = sum(r["filled"] for r in counted)
    failing_fields = sum(len({c["detail"].split(":")[0] for c in r["checks"]}) for r in counted if r["outcome"] == "fail")
    met = bool(counted) and len(passed) == len(counted)
    lines = [f"# Forms {args.name} ({stamp})", "",
             f"- **Forms (every fillable field filled, read back by MuPDF and Capriole, shown by all three engines, qpdf no worse): "
             f"{len(passed)} / {len(counted)}**",
             f"- Fields filled: {fields}; not fillable (read-only, signatures, push buttons): {sum(r['not_fillable'] for r in counted)}; "
             f"fields with a failing check: {failing_fields}",
             f"- Forms with nothing to fill (only signatures, push buttons, read-only fields): {len(nothing)} "
             f"({sum(r['not_fillable'] for r in nothing)} fields); files not opened: {len(unreadable)}",
             f"- Reference engines not compared where they cannot show the form (notes in results.jsonl): "
             f"{sum(1 for r in counted if r.get('notes'))} forms",
             f"- Exit criterion (every field of every form): **{'met' if met else 'not met'}**", "", "## Forms", "",
             "| file | fields | filled | not fillable | outcome |", "|---|---|---|---|---|"]
    lines += [f"| {Path(r['file']).name} | {r['fields']} | {r['filled']} | {r['not_fillable']} | {r['outcome']} |" for r in counted + nothing]
    lines += ["", "## Failures", ""]
    lines += [f"- {Path(r['file']).name}: " + "; ".join(f"{c['check']}: {c['detail']}" for c in r["checks"])[:800]
              for r in counted if r["outcome"] == "fail"] or ["- none"]
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:7]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
