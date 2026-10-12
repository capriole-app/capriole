#!/usr/bin/env python3
"""Long-term signatures as other verifiers see them (PLAN.md phase 11b, exit criterion).

With the test PKI of tests/data/signing (make_ltv_fixtures.sh: a CA, a responder it authorised, a signer
whose certificate says where to ask, a revoked one, a time-stamping authority that says where to ask),
--files files of the corpora (in hash order of --seed; encrypted files and files that do not open are
passed over) are signed with `cpdf sign` at B-T (the test TSA), every fifth with the revoked signer.
Then, as the desktop app will:

1. `cpdf ltv needs` says what the chains need answers for; the answers are made offline — the OCSP
   responder (`openssl ocsp -index`) answers the requests Capriole made, or (every other file) the CA's
   CRL is given — and `cpdf ltv add` writes them into the /DSS (B-LT);
2. `cpdf doc-timestamp` adds a document time stamp from the TSA, then the validation data for its chain
   (B-LTA).

The URLs in the certificates point where nothing answers: what a verifier finds, it finds in the file.

3. pyHanko (`pyhanko sign adesverify`: AdES past validation, the test CA trusted) finds the signature
   intact and trusted, the changes after it acceptable — and the revoked signer's revoked. What pyHanko
   makes of the original first (corpus-tools/pyhanko_probe.py): one it cannot read is not judged by it
   (counted); one it reads only with --no-strict-syntax is judged so; a refusal of the later revisions
   is excused only when the original is the reason (its /Metadata is not XMP, its pages hold
   annotations directly: pyHanko refuses its own updates of such a file as well), and counted;
4. Capriole reads it back valid, its revocation good from the file alone, at level B-LTA (the revoked
   signer's: revoked);
5. Poppler (`pdfsig`) still finds the signature valid (each revision appended; it does not verify
   document time stamps), and `qpdf --check` is no worse.

And over every file of the corpora with a /ByteRange:

6. where Capriole checks the first signature's revocation from what the file keeps, pyHanko says
   revoked when, and only when, Capriole does.

Exit criterion of phase 11b: every file passes 1–5, and 6 holds.

    corpus-tools/check_ltv.py ../CORPORA/safedocs/0000 ../CORPORA/unsafe/x002 ../CORPORA/unsafe/x008 \\
        --cpdf build/release/tools/cpdf/cpdf --pyhanko ../CORPORA/tools/pyhanko-venv/bin/pyhanko --name ltv-all
"""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import re
import shutil
import tempfile
from pathlib import Path

from check_pages import qpdf_severity
from compare_render import run

SIGNING = Path(__file__).resolve().parent.parent / "tests" / "data" / "signing"
DATE = "D:20261009150000Z"


def tsa_reply(args: argparse.Namespace, query: Path, reply: Path, work: Path) -> bool:
    (work / "tsa-serial").write_text("01\n")
    config = work / "tsa.cnf"
    config.write_text((SIGNING / "tsa.cnf").read_text().replace("./tsa-serial", str(work / "tsa-serial")))
    chain = work / "tsa-chain.pem"
    chain.write_text((SIGNING / "ca.pem").read_text())
    run(["openssl", "ts", "-reply", "-queryfile", str(query), "-inkey", str(SIGNING / "ltv-tsa.key"), "-signer", str(SIGNING / "ltv-tsa.pem"),
         "-chain", str(chain), "-config", str(config), "-out", str(reply)], args.timeout)
    return reply.exists()


def answers(args: argparse.Namespace, needs: list[dict], work: Path, use_crl: bool) -> dict:
    """The validation data a host would fetch for what Capriole says is needed: made offline."""
    data: dict = {"date": DATE, "certificates": [], "ocsps": [], "crls": []}
    for k, need in enumerate(needs):
        if need["answered"]:
            continue
        if need["issuer"] is None:
            # Fetched from caIssuers: the CA's certificate.
            run(["openssl", "x509", "-in", str(SIGNING / "ca.pem"), "-outform", "DER", "-out", str(work / "ca.der")], args.timeout)
            data["certificates"].append((work / "ca.der").read_bytes().hex())
            continue
        if use_crl and need["crl"]:
            crl = (SIGNING / "ca.crl").read_bytes().hex()
            if crl not in data["crls"]:
                data["crls"].append(crl)
            continue
        request, response = work / f"request-{k}.der", work / f"response-{k}.der"
        request.write_bytes(bytes.fromhex(need["ocspRequest"]))
        run(["openssl", "ocsp", "-index", str(SIGNING / "ca-index.txt"), "-CA", str(SIGNING / "ca.pem"), "-rsigner", str(SIGNING / "ocsp.pem"),
             "-rkey", str(SIGNING / "ocsp.key"), "-reqin", str(request), "-respout", str(response), "-ndays", "36500"], args.timeout)
        if response.exists():
            data["ocsps"].append(response.read_bytes().hex())
    return data


def add_ltv(args: argparse.Namespace, path: Path, out: Path, work: Path, use_crl: bool) -> tuple[bool, str]:
    needs_run = run([args.cpdf, "ltv", "needs", str(path)], args.timeout)
    try:
        needs = json.loads(needs_run["output"].strip().splitlines()[-1])
    except (json.JSONDecodeError, IndexError):
        return False, "ltv needs: " + needs_run["output"].strip()[:300]
    data = answers(args, needs, work, use_crl)
    if any(n["issuer"] is None for n in needs):
        # An issuer fetched: ask again, with it.
        (work / "known.json").write_text(json.dumps(data))
        needs = json.loads(run([args.cpdf, "ltv", "needs", str(path), str(work / "known.json")], args.timeout)["output"].strip().splitlines()[-1])
        more = answers(args, needs, work, use_crl)
        for key in ("ocsps", "crls"):
            data[key] += more[key]
    spec = work / "ltv.json"
    spec.write_text(json.dumps(data))
    added = run([args.cpdf, "ltv", "add", str(path), str(spec), str(out)], args.timeout)
    if added["status"] != 0 or not out.exists():
        return False, "ltv add: " + added["output"].strip()[:300]
    return True, ""


def probe(args: argparse.Namespace, path: Path) -> dict:
    """What pyHanko makes of the original (corpus-tools/pyhanko_probe.py, in pyHanko's environment)."""
    python = Path(args.pyhanko).parent / "python"
    result = run([str(python), str(Path(__file__).resolve().parent / "pyhanko_probe.py"), str(path)], args.timeout)
    # (stderr follows stdout in the output: the JSON line is the one that starts so.)
    try:
        return json.loads(next(line for line in result["output"].splitlines() if line.startswith('{"strict"')))
    except (json.JSONDecodeError, StopIteration):
        return {"strict": False, "lenient": False, "metadataNotXmp": False, "directAnnots": False, "badStructure": False, "badSignatures": False}


def pyhanko(args: argparse.Namespace, path: Path, lenient: bool, trust: bool = True, last: bool = True, diff: bool = True) -> tuple[str, list[str]]:
    """pyHanko's AdES verdict on the file's last signature (or first: last=False; `adesverify`: past
    validation, with the document time stamps as proofs of existence), its summary line
    ("Signature1:…:INTACT:TRUSTED,…"), and the reasons its difference analysis gave for refusing a
    revision (SuspiciousModification). lenient: with --no-strict-syntax, for an original pyHanko reads
    only so (hybrid cross-references, syntax it refuses in strict mode).

    At a validation time a little ahead: pyHanko gives what no time stamp covers a proof of existence
    at the moment it first looks at it, later than the "now" it fixed at the start, and so would leave
    out the validation data added after the last document time stamp.
    """
    ahead = (datetime.datetime.now(datetime.UTC) + datetime.timedelta(minutes=30)).strftime("%Y-%m-%dT%H:%M:%S+00:00")
    command = [args.pyhanko, "--verbose", "sign", "adesverify", "--validation-time", ahead]
    if trust:
        command += ["--trust", str(SIGNING / "ca.pem"), "--trust-replace"]
    if lenient:
        command.append("--no-strict-syntax")
    if not diff:
        command.append("--no-diff-analysis")
    output = run(command + [str(path)], args.timeout)["output"]
    reasons = sorted({m.group(1).strip() for m in re.finditer(r"SuspiciousModification: (.*)", output)})
    lines = [line for line in output.splitlines() if re.match(r"^[^ ]+:[0-9a-f]{64}:", line)]
    if not lines:
        return output.strip()[-300:], reasons
    return (lines[-1] if last else lines[0]), reasons


# Why pyHanko's difference analysis may refuse what comes after a signature, when the original is so —
# not what Capriole writes (pyHanko would refuse its own updates as well).
EXCUSES = {"Metadata does not look like XMP": "metadataNotXmp", "Array contains direct objects": "directAnnots"}


def poppler_verdict(args: argparse.Namespace, path: Path, signer: str) -> str:
    """pdfsig's validation of the signature whose signer is signer (the test CA trusted)."""
    out = run(["pdfsig", "-nssdir", f"sql:{args.nss}", str(path)], args.timeout)["output"]
    for block in re.split(r"\nSignature #\d+:", out)[1:]:
        if f"Signer Certificate Common Name: {signer}" in block:
            found = re.search(r"Signature Validation: (.*)", block)
            return found.group(1).strip() if found else "no validation"
    return "not listed: " + out[-200:]


def capriole(args: argparse.Namespace, path: Path) -> list[dict] | None:
    result = run([args.cpdf, "signatures", str(path), str(SIGNING / "ca.pem")], args.timeout)
    try:
        return json.loads(result["output"].strip().splitlines()[-1])
    except (json.JSONDecodeError, IndexError):
        return None


def check_file(args: argparse.Namespace, path: Path, index: int, work_root: Path) -> dict:
    work = Path(tempfile.mkdtemp(dir=work_root))
    record: dict = {"file": str(path), "checks": []}
    try:
        info = run(["qpdf", "--show-encryption", str(path)], args.timeout)
        if info["status"] not in (0, 3) or "File is not encrypted" not in info["output"]:
            return {**record, "outcome": "skipped", "why": "encrypted, or does not open"}
        revoked, use_crl = index % 5 == 4, index % 2 == 1
        key = "revoked.p12" if revoked else "ltv.p12"
        record.update({"revoked": revoked, "crl": use_crl})
        failures = record["checks"]

        # B-T: signed, with a signature time stamp.
        spec = json.dumps({"date": DATE, "reason": "Verifica", "timestamp": True})
        signed, query, reply = work / "signed.pdf", work / "query.tsq", work / "reply.tsr"
        base = [args.cpdf, "sign", str(path), str(SIGNING / key), spec, str(signed)]
        first = run(base + ["--timestamp-request", str(query)], args.timeout)
        if not query.exists():
            reason = "skipped" if "needed repairs" in first["output"] or "open:" in first["output"] else "fail"
            return {**record, "outcome": reason, "checks": [{"check": "sign", "detail": first["output"].strip()[:300]}]}
        if not tsa_reply(args, query, reply, work):
            return {**record, "outcome": "fail", "checks": [{"check": "tsa", "detail": "the test TSA gave no response"}]}
        done = run(base + ["--timestamp-response", str(reply)], args.timeout)
        if done["status"] != 0 or not signed.exists():
            return {**record, "outcome": "fail", "checks": [{"check": "sign", "detail": done["output"].strip()[:300]}]}
        record["rewritten"] = "written anew" in first["output"]

        # B-LT, then B-LTA: the document time stamp and the validation data for its chain.
        lt = work / "lt.pdf"
        ok, why = add_ltv(args, signed, lt, work, use_crl)
        if not ok:
            failures.append({"check": "ltv", "detail": why})
            record["outcome"] = "fail"
            return record
        stamped, lta = work / "stamped.pdf", work / "lta.pdf"
        ts_spec = json.dumps({"date": DATE})
        run([args.cpdf, "doc-timestamp", str(lt), ts_spec, str(stamped), "--timestamp-request", str(work / "doc.tsq")], args.timeout)
        if not (work / "doc.tsq").exists() or not tsa_reply(args, work / "doc.tsq", work / "doc.tsr", work):
            failures.append({"check": "doc-timestamp", "detail": "no request, or no response from the TSA"})
            record["outcome"] = "fail"
            return record
        made = run([args.cpdf, "doc-timestamp", str(lt), ts_spec, str(stamped), "--timestamp-response", str(work / "doc.tsr")], args.timeout)
        if made["status"] != 0 or not stamped.exists():
            failures.append({"check": "doc-timestamp", "detail": made["output"].strip()[:300]})
            record["outcome"] = "fail"
            return record
        ok, why = add_ltv(args, stamped, lta, work, use_crl)
        if not ok:
            failures.append({"check": "ltv (time stamp)", "detail": why})
            record["outcome"] = "fail"
            return record

        # 3. pyHanko, for the long term — where it can read the original at all.
        # The original, or what Capriole wrote anew (repairs) and signed: the start of what is verified.
        original = probe(args, signed if record["rewritten"] else path)
        record["probe"] = original
        if not original["lenient"] and record["rewritten"]:
            failures.append({"check": "pyhanko", "detail": "pyHanko cannot read what Capriole wrote anew"})
        elif not original["lenient"]:
            record["pyhanko"] = "cannot read the original"
        elif original["badSignatures"]:
            record["pyhanko"] = "fails on a signature the original has"
        else:
            record["lenient"] = not original["strict"]
            theirs, reasons = pyhanko(args, lta, lenient=not original["strict"])
            if "Generic processing error" in theirs and original["badStructure"]:
                # pyHanko's difference analysis fails on the original's structure tree (a null element):
                # the signature without it, counted.
                theirs, reasons = pyhanko(args, lta, lenient=not original["strict"], diff=False)
                record["noDiff"] = True
            record["pyhanko"] = theirs
            verdict = theirs.split(":", 2)[2] if theirs.count(":") >= 2 else theirs
            wanted = "INTACT:REVOKED" if revoked else "INTACT:TRUSTED"
            excused = bool(reasons) and all(original.get(EXCUSES.get(r, ""), False) for r in reasons)
            record["excused"] = excused and "ILLEGAL_MODIFICATIONS" in verdict
            if not verdict.startswith(wanted) or ("ACCEPTABLE_MODIFICATIONS" not in verdict and not record["excused"] and not record.get("noDiff")):
                failures.append({"check": "pyhanko", "detail": f"{wanted} expected: {theirs[-300:]} {reasons}"})

        # 4. Capriole.
        mine = capriole(args, lta)
        sig = next((s for s in reversed(mine or []) if not s["documentTimestamp"]), None)  # ours: the last
        stamps = [s for s in mine or [] if s["documentTimestamp"]]
        expected = "revoked" if revoked else "good"
        if (sig is None or sig["integrity"] != "valid" or sig["revocation"] != expected or (not revoked and (not sig["revocationInFile"] or sig["level"] != "B-LTA"))
                or not stamps or stamps[0]["integrity"] != "valid"):
            failures.append({"check": "capriole", "detail": json.dumps(sig)[:400]})

        # 5. Poppler, qpdf.
        # (pdfsig does not verify document time stamps: the signature only, ours by its signer's name —
        # pdfsig does not list them in the order of the revisions.)
        ours = poppler_verdict(args, lta, "Rita Neri (test)" if revoked else "Luca Verdi (test)")
        if ours != "Signature is Valid.":
            failures.append({"check": "pdfsig", "detail": ours[:300]})
        if not record["rewritten"] and not lta.read_bytes().startswith(path.read_bytes()):
            failures.append({"check": "incremental", "detail": "the original bytes are not the start of the file"})
        if qpdf_severity(lta, args.timeout) > qpdf_severity(path, args.timeout):
            failures.append({"check": "qpdf", "detail": "qpdf --check worse than the original"})
        record["outcome"] = "fail" if failures else "pass"
        return record
    except Exception as error:  # noqa: BLE001 — a crash of the check is a failure of the file, reported
        return {**record, "outcome": "fail", "checks": [{"check": "exception", "detail": repr(error)[:300]}]}
    finally:
        shutil.rmtree(work, ignore_errors=True)


def compare_existing(args: argparse.Namespace, path: Path) -> dict | None:
    if b"/ByteRange" not in path.read_bytes():
        return None
    result = run([args.cpdf, "signatures", str(path)], args.timeout)
    try:
        mine = json.loads(result["output"].strip().splitlines()[-1])
    except (json.JSONDecodeError, IndexError):
        mine = []
    checked = [m for m in mine if m["revocation"] != "notChecked"]
    if not checked:
        return {"file": str(path), "checked": 0, "disagree": []}
    original = probe(args, path)
    if not original["lenient"]:
        return {"file": str(path), "checked": 0, "disagree": []}
    theirs, _ = pyhanko(args, path, lenient=not original["strict"], trust=False, last=False)
    disagree = [{"field": m["fieldName"], "capriole": m["revocation"], "pyhanko": theirs[-300:]} for m in checked[:1]
                if (m["revocation"] == "revoked") != (":REVOKED" in theirs)]
    return {"file": str(path), "checked": len(checked), "disagree": disagree}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="+")
    parser.add_argument("--cpdf", required=True)
    parser.add_argument("--pyhanko", required=True)
    parser.add_argument("--files", type=int, default=100)
    parser.add_argument("--seed", default="capriole-phase-11b")
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="ltv")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())
    args.pyhanko = str(Path(args.pyhanko).resolve())
    os.environ["CAPRIOLE_P12_PASSWORD"] = "capriole"

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="ltv-", dir=report))
    args.nss = work / "nss"
    args.nss.mkdir()
    run(["certutil", "-N", "-d", f"sql:{args.nss}", "--empty-password"], 60)
    run(["certutil", "-A", "-d", f"sql:{args.nss}", "-n", "capriole-test-ca", "-t", "CT,C,C", "-i", str(SIGNING / "ca.pem")], 60)

    files = [p.resolve() for d in args.corpus for p in Path(d).glob("*.pdf")]
    files.sort(key=lambda f: hashlib.sha256(f"{args.seed}:{f.name}".encode()).hexdigest())
    results: list[dict] = []
    skipped = 0
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool, open(report / "results.jsonl", "w") as log:
        for start in range(0, len(files), args.jobs * 4):
            if len(results) >= args.files:
                break
            batch = files[start:start + args.jobs * 4]
            for r in pool.map(lambda pair: check_file(args, pair[1], start + pair[0], work), enumerate(batch)):
                if r["outcome"] == "skipped":
                    skipped += 1
                elif len(results) < args.files:
                    results.append(r)
                    log.write(json.dumps(r) + "\n")
        existing = [c for c in pool.map(lambda p: compare_existing(args, p), files) if c is not None]
    with open(report / "existing.jsonl", "w") as log:
        for c in existing:
            log.write(json.dumps(c) + "\n")
    shutil.rmtree(work)

    passed = [r for r in results if r["outcome"] == "pass"]
    checked = [c for c in existing if c["checked"]]
    disagreeing = [c for c in checked if c["disagree"]]
    met = len(results) >= args.files and len(passed) == len(results) and not disagreeing
    lines = [f"# Long-term signatures {args.name} ({stamp})", "",
             f"- **Files signed B-LTA (pyHanko for the long term with nothing fetched, Capriole from the file alone, pdfsig, incremental, qpdf): "
             f"{len(passed)} / {len(results)}**",
             f"- Of them: revoked signer {sum(r.get('revoked', False) for r in results)}, answered by the CRL {sum(r.get('crl', False) for r in results)}, "
             f"written anew first (repairs) {sum(r.get('rewritten', False) for r in results)}; "
             f"pyHanko with --no-strict-syntax (an original it reads only so) {sum(r.get('lenient', False) for r in results)}; "
             f"pyHanko cannot read the original {sum(r.get('pyhanko') == 'cannot read the original' for r in results)}; "
             f"pyHanko refuses what follows the signature for what the original is (non-XMP metadata, direct annotations) "
             f"{sum(r.get('excused', False) for r in results)}; "
             f"pyHanko without its difference analysis (it fails on the original's structure tree) {sum(r.get('noDiff', False) for r in results)}; "
             f"pyHanko fails on a signature the original has {sum(r.get('pyhanko') == 'fails on a signature the original has' for r in results)}; "
             f"passed over (encrypted, not opening) {skipped}",
             f"- **Signatures already in the corpora: of {len(checked)} files whose revocation Capriole checks from the file, pyHanko agrees on "
             f"{len(checked) - len(disagreeing)}** ({len(existing)} signed files)",
             f"- Exit criterion: **{'met' if met else 'not met'}**", "", "## Failures", ""]
    lines += [f"- {Path(r['file']).name}: " + "; ".join(f"{c['check']}: {c['detail']}" for c in r["checks"])[:700]
              for r in results if r["outcome"] == "fail"] or ["- none"]
    lines += ["", "## Verdicts that differ", ""]
    lines += [f"- {Path(c['file']).name}: {json.dumps(c['disagree'])[:600]}" for c in disagreeing] or ["- none"]
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:6]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
