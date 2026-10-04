#!/usr/bin/env python3
"""PDF/A conversion as the reference validator sees it (PLAN.md phase 8i, exit criterion).

--files files of the corpora (in hash order of --seed; files that do not open are passed over) are
converted with `cpdf pdfa` (the C API: PDF/A-2b, or -3b for a document with embedded files). Then:

1. every file the engine converts passes veraPDF (--verapdf) for the part it says (2b, 3b);
2. every file the engine refuses says why (the reasons are counted);
3. the converted pages draw as before in Capriole (`cpdf render`, the first --pages pages: similarity of
   compare_render.py >= --threshold) — before: the original written anew in full (`cpdf save`), where the
   conversion starts from, so that what a damaged file's repairs change is not counted;
4. Poppler (`pdftotext`) loses no character it read before (compared as characters, spaces aside: the
   fonts' metrics put right move its reading order and where it guesses spaces; a file it could not read
   and now can reads more);
5. `qpdf --check` is no worse.

And, on the first --invoices converted files: a Factur-X invoice embedded in the converted file
(`cpdf facturx embed`, the EN 16931 profile), then the whole converted to PDF/A-3: veraPDF 3b passes, and
the factur-x library (--facturx-python) finds the invoice, valid against its XSD.

Exit criterion of phase 8i (engine): every converted file passes 1, 3, 4, 5, and the invoices pass.

    corpus-tools/check_pdfa.py ../CORPORA/safedocs/0000 ../CORPORA/unsafe/x002 ../CORPORA/unsafe/x008 \\
        --cpdf build/release/tools/cpdf/cpdf --verapdf ../CORPORA/tools/verapdf/verapdf \\
        --facturx-python ../CORPORA/tools/venv/bin/python --report-dir ../CORPORA/reports --name pdfa-all
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from check_attachments import invoice
from check_pages import qpdf_severity
from compare_render import load, page_count, run, similarity


def reason_kind(reason: str) -> str:
    """A refusal's reason without its particulars (font names, codes, places)."""
    text = re.sub(r"font \S+(?: \S+)*?(?=:| is | cannot)", "font X", reason)
    text = re.sub(r"\(code \d+\)|code \d+|\([^)]*\)", "", text)
    text = re.sub(r": /\S+ in .*", "", text)
    return re.sub(r"\s+", " ", text).strip()


def verapdf(args: argparse.Namespace, flavour: str, path: Path, form: str) -> str:
    """veraPDF's report. Not under run()'s memory cap: the JVM reserves far more address space than it
    uses; its heap is bounded instead, its garbage collector one thread."""
    env = {**os.environ, "JAVA_TOOL_OPTIONS": "-Xmx1g -XX:+UseSerialGC -XX:TieredStopAtLevel=1"}
    with tempfile.TemporaryDirectory() as cwd:
        try:
            proc = subprocess.run([args.verapdf, "--flavour", flavour, "--format", form, str(path)], cwd=cwd, env=env,
                                  stdin=subprocess.DEVNULL, capture_output=True, timeout=args.timeout * 3)
            return proc.stdout.decode("utf-8", "replace")
        except subprocess.TimeoutExpired:
            return "TIMEOUT"


def text_of(path: Path, timeout: float) -> str:
    """What pdftotext reads (its standard output only: its complaints about a damaged file are not text)."""
    with tempfile.TemporaryDirectory() as cwd:
        try:
            proc = subprocess.run(["pdftotext", "-enc", "UTF-8", str(path), "-"], cwd=cwd, stdin=subprocess.DEVNULL, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return ""
    return re.sub(r"\s+", " ", proc.stdout.decode("utf-8", "replace")).strip()


def check_file(args: argparse.Namespace, path: Path, index: int, work_root: Path) -> dict:
    work = Path(tempfile.mkdtemp(dir=work_root))
    record: dict = {"file": str(path), "checks": []}
    try:
        pages = page_count(path)
        if not pages:
            return {**record, "outcome": "skipped", "why": "does not open"}
        out = work / "pdfa.pdf"
        converted = run([args.cpdf, "pdfa", str(path), str(out)], args.timeout)
        if converted["status"] != 0 or not out.exists():
            message = converted["output"].strip().splitlines()[-1] if converted["output"].strip() else "no output"
            if "pdfa:" not in message:
                return {**record, "outcome": "fail", "checks": [{"check": "cpdf", "detail": message[-300:]}]}
            reasons = message.split("cannot be made PDF/A: ", 1)[-1].split("; ")
            return {**record, "outcome": "refused", "reasons": reasons}
        report = json.loads(converted["output"].strip().splitlines()[-1])
        part = report["part"]
        record.update({"part": part, "changes": report["changes"]})
        fail = record["checks"]

        vera = verapdf(args, f"{part}b", out, "text")
        if not vera.lstrip().startswith("PASS"):
            details = verapdf(args, f"{part}b", out, "json")
            rules = sorted(set(f"{c}-{t}" for c, t in re.findall(r'"clause"\s*:\s*"([^"]+)",\s*"testNumber"\s*:\s*(\d+)', details)))
            fail.append({"check": "verapdf", "detail": ", ".join(rules)[:400] or vera[:200]})

        sound = work / "sound.pdf"
        run([args.cpdf, "save", str(path), str(sound)], args.timeout)
        reference = sound if sound.exists() else path
        # The pages of the converted file (a damaged original may claim more than it has).
        for page in range(1, min(pages, page_count(out) or 0, args.pages) + 1):
            before, after = work / f"before-{page}.png", work / f"after-{page}.png"
            for source, image in ((reference, before), (out, after)):
                run([args.cpdf, "render", "--dpi", str(args.dpi), str(source), str(page), str(image)], args.timeout)
            a, b = load(before), load(after)
            if a is None or b is None:
                fail.append({"check": "render", "detail": f"page {page} not drawn"})
                continue
            s = similarity(a, b, args.blur, args.tolerance)
            if s < args.threshold:
                fail.append({"check": "render", "detail": f"page {page}: similarity {s:.4f}"})
        lost = collections.Counter(text_of(path, args.timeout).replace(" ", "")) - collections.Counter(text_of(out, args.timeout).replace(" ", ""))
        if lost:
            fail.append({"check": "text", "detail": f"Poppler no longer reads {sum(lost.values())} characters: {''.join(list(lost)[:20])}"})
        if qpdf_severity(out, args.timeout) > qpdf_severity(path, args.timeout):
            fail.append({"check": "qpdf", "detail": "qpdf --check worse than the original"})

        if index < args.invoices:
            xml = work / "factur-x.xml"
            xml.write_bytes(invoice("EN 16931"))
            with_invoice, as3 = work / "invoice.pdf", work / "invoice-pdfa.pdf"
            # Into the converted file (a sound one: an original that needed repairs is refused by embed).
            run([args.cpdf, "facturx", "embed", str(out), str(xml), str(with_invoice)], args.timeout)
            third = run([args.cpdf, "pdfa", str(with_invoice), str(as3), "3"], args.timeout) if with_invoice.exists() else {"status": 1, "output": ""}
            record["invoice"] = third["status"] == 0 and as3.exists()
            if record["invoice"]:
                vera3 = verapdf(args, "3b", as3, "text")
                if not vera3.lstrip().startswith("PASS"):
                    details = verapdf(args, "3b", as3, "json")
                    rules = sorted(set(f"{c}-{t}" for c, t in re.findall(r'"clause"\s*:\s*"([^"]+)",\s*"testNumber"\s*:\s*(\d+)', details)))
                    fail.append({"check": "invoice verapdf 3b", "detail": ", ".join(rules)[:400] or vera3[:200]})
                if args.facturx_python:
                    code = ("import sys, logging; logging.disable(logging.CRITICAL); import facturx; from lxml import etree;"
                            "name, xml = facturx.get_facturx_xml_from_pdf(open(sys.argv[1],'rb').read(), check_xsd=True);"
                            "print('FX', name, xml == open(sys.argv[2],'rb').read(), facturx.get_level(etree.fromstring(xml)))")
                    lib = run([args.facturx_python, "-c", code, str(as3), str(xml)], args.timeout)
                    if "FX factur-x.xml True en16931" not in lib["output"]:
                        fail.append({"check": "invoice factur-x", "detail": lib["output"][-200:]})
        record["outcome"] = "fail" if fail else "pass"
        return record
    except Exception as error:  # noqa: BLE001 — a crash of the check is a failure of the file, reported
        return {**record, "outcome": "fail", "checks": [{"check": "exception", "detail": repr(error)[:300]}]}
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="+")
    parser.add_argument("--cpdf", required=True)
    parser.add_argument("--verapdf", required=True)
    parser.add_argument("--facturx-python", default=None)
    parser.add_argument("--files", type=int, default=100)
    parser.add_argument("--invoices", type=int, default=10)
    parser.add_argument("--pages", type=int, default=3)
    parser.add_argument("--seed", default="capriole-phase-8i")
    parser.add_argument("--dpi", type=int, default=50)
    parser.add_argument("--blur", type=float, default=1.0)
    parser.add_argument("--tolerance", type=int, default=32)
    parser.add_argument("--threshold", type=float, default=0.99)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="pdfa")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())
    args.verapdf = str(Path(args.verapdf).resolve())
    args.facturx_python = os.path.abspath(args.facturx_python) if args.facturx_python else None

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="pdfa-", dir=report))
    files = [p.resolve() for d in args.corpus for p in Path(d).glob("*.pdf")]
    files.sort(key=lambda f: hashlib.sha256(f"{args.seed}:{f.name}".encode()).hexdigest())
    results: list[dict] = []
    converted = 0
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool, open(report / "results.jsonl", "w") as log:
        for start in range(0, len(files), args.jobs * 2):
            if len(results) >= args.files:
                break
            batch = files[start:start + args.jobs * 2]
            # The invoices go with the first converted files: the index counts conversions so far.
            for r in pool.map(lambda p: check_file(args, p, converted, work), batch):
                if r["outcome"] == "skipped" or len(results) >= args.files:
                    continue
                results.append(r)
                converted += r["outcome"] in ("pass", "fail") and "part" in r
                log.write(json.dumps(r) + "\n")
    shutil.rmtree(work)

    done = [r for r in results if "part" in r]
    passed = [r for r in done if r["outcome"] == "pass"]
    refused = [r for r in results if r["outcome"] == "refused"]
    broken = [r for r in results if r["outcome"] == "fail" and "part" not in r]
    kinds = collections.Counter(reason_kind(x) for r in refused for x in r["reasons"])
    invoices = [r for r in done if "invoice" in r]
    met = len(results) >= args.files and len(passed) == len(done) and not broken
    lines = [f"# PDF/A {args.name} ({stamp})", "",
             f"- Files: {len(results)} — converted {len(done)}, refused {len(refused)}, failed to run {len(broken)}",
             f"- **Converted files that veraPDF passes, drawn and read as before, qpdf no worse: {len(passed)} / {len(done)}** "
             f"(PDF/A-2b {sum(r['part'] == 2 for r in done)}, PDF/A-3b {sum(r['part'] == 3 for r in done)})",
             f"- Factur-X invoices converted to PDF/A-3: {sum(r['invoice'] for r in invoices)} of {len(invoices)} tried "
             f"({'with' if args.facturx_python else 'without'} the factur-x library)",
             f"- Exit criterion: **{'met' if met else 'not met'}**", "", "## Refusals, by reason (a file may have several)", ""]
    lines += [f"- {n} × {k}" for k, n in kinds.most_common()] or ["- none"]
    lines += ["", "## Failures", ""]
    lines += [f"- {Path(r['file']).name}: " + "; ".join(f"{c['check']}: {c['detail']}" for c in r["checks"])[:700]
              for r in results if r["outcome"] == "fail"] or ["- none"]
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:7]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
