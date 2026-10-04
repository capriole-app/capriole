#!/usr/bin/env python3
"""Digital signatures as other verifiers see them (PLAN.md phase 8h, exit criterion).

With the test PKI of tests/data/signing (a CA, an RSA and an ECDSA signer, a time-stamping authority
run by `openssl ts -reply`), --files files of the corpora (in hash order of --seed; encrypted files,
which are not signed yet, and files that do not open are passed over) are signed with `cpdf sign` (the
C API): RSA and ECDSA in turn, visible (a box on the first page) and invisible in turn, every fourth
with a signature time stamp (B-T: the request to the TSA, its response put in). Then:

1. Poppler (`pdfsig`, NSS, with the test CA trusted) finds the signature valid, the total document
   signed, the certificate trusted;
2. OpenSSL (`openssl cms -verify`) verifies the CMS over the signed byte ranges with the test CA; a
   time stamp too (`openssl ts -verify`, over the signature value);
3. the original file is the start of the signed one (an incremental save), and `qpdf --check` is no
   worse; Capriole reads its signature back valid, at the level made (B-B, B-T), trusted;
4. one byte changed in the signed ranges: Poppler, OpenSSL and Capriole all find it not valid.

And over every file of the corpora with a /ByteRange (the signatures they have):

5. Capriole's verdict on each signature (valid or not) is Poppler's.

Exit criterion of phase 8h (engine): every signed file passes 1–4, and 5 holds for every signature.

    corpus-tools/check_signatures.py ../CORPORA/safedocs/0000 ../CORPORA/unsafe/x002 ../CORPORA/unsafe/x008 \\
        --cpdf build/release/tools/cpdf/cpdf --report-dir ../CORPORA/reports --name signatures-all
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
DATE = "D:20261004150000Z"
TOKEN_OID = bytes.fromhex("060b2a864886f70d010910020e")  # id-aa-signatureTimeStampToken


def der(data: bytes, at: int) -> tuple[int, int, int]:
    """(tag, start of content, end) of the DER element at `at`."""
    tag, length = data[at], data[at + 1]
    at += 2
    if length & 0x80:
        n = length & 0x7F
        length = int.from_bytes(data[at:at + n], "big")
        at += n
    return tag, at, at + length


def children(data: bytes, start: int, end: int) -> list[tuple[int, int, int, int]]:
    out, at = [], start
    while at < end:
        tag, s, e = der(data, at)
        out.append((tag, at, s, e))
        at = e
    return out


def signature_parts(cms: bytes) -> tuple[bytes, bytes | None]:
    """The signer's signature value and its time stamp token (none without one), read from the CMS."""
    _, s, e = der(cms, 0)
    _, _, wrapped, _ = children(cms, s, e)[1]
    _, s, e = der(cms, wrapped)
    _, _, set_start, set_end = children(cms, s, e)[-1]  # signerInfos
    _, _, si_start, si_end = children(cms, set_start, set_end)[0]
    parts = children(cms, si_start, si_end)
    value = next(cms[s:e] for tag, _, s, e in parts if tag == 0x04)
    token = None
    for tag, _, s, e in parts:
        if tag == 0xA1:  # unsigned attributes
            for _, a_at, a_s, a_e in children(cms, s, e):
                if cms[a_s:a_s + len(TOKEN_OID)] == TOKEN_OID:
                    _, v_s, v_e = der(cms, a_s + len(TOKEN_OID))
                    token = cms[v_s:v_e]
    return value, token


def last_signature(path: Path) -> tuple[bytes, bytes]:
    """The signed bytes and the CMS (DER) of the file's last signature."""
    data = path.read_bytes()
    match = list(re.finditer(rb"/ByteRange\s*\[\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*\]", data))[-1]
    a, b, c, d = map(int, match.groups())
    digits = data[a + b + 1:c - 1].decode()
    cms = bytes.fromhex(digits)
    _, _, end = der(cms, 0)
    return data[a:a + b] + data[c:c + d], cms[:end]


def pdfsig(path: Path, nss: Path, timeout: float) -> list[dict]:
    out = run(["pdfsig", "-nssdir", f"sql:{nss}", str(path)], timeout)["output"]
    signatures = []
    for block in re.split(r"\nSignature #\d+:", out)[1:]:
        validation = re.search(r"Signature Validation: (.*)", block)
        signatures.append({
            "valid": bool(validation) and validation.group(1).strip() == "Signature is Valid.",
            "total": "Total document signed" in block and "Not total document signed" not in block,
            "trusted": "Certificate is Trusted." in block,
            "validation": validation.group(1).strip() if validation else "",
        })
    return signatures


def capriole(args: argparse.Namespace, path: Path, anchors: bool = True) -> list[dict] | None:
    cmd = [args.cpdf, "signatures", str(path)] + ([str(SIGNING / "ca.pem")] if anchors else [])
    result = run(cmd, args.timeout)
    try:
        return json.loads(result["output"].strip().splitlines()[-1])
    except (json.JSONDecodeError, IndexError):
        return None


def openssl_verifies(work: Path, signed: bytes, cms: bytes, timeout: float) -> tuple[bool, str]:
    content, sig = work / "content.bin", work / "sig.der"
    content.write_bytes(signed)
    sig.write_bytes(cms)
    result = run(["openssl", "cms", "-verify", "-binary", "-inform", "DER", "-in", str(sig), "-content", str(content),
                  "-CAfile", str(SIGNING / "ca.pem"), "-purpose", "any", "-out", "/dev/null"], timeout)
    if "Verification successful" not in result["output"]:
        return False, result["output"].strip()[:200]
    value, token = signature_parts(cms)
    if token is not None:
        (work / "value.bin").write_bytes(value)
        (work / "token.der").write_bytes(token)
        ts = run(["openssl", "ts", "-verify", "-token_in", "-in", str(work / "token.der"), "-data", str(work / "value.bin"),
                  "-CAfile", str(SIGNING / "ca.pem"), "-untrusted", str(SIGNING / "tsa.pem")], timeout)
        if "Verification: OK" not in ts["output"]:
            return False, "time stamp: " + ts["output"].strip()[:200]
    return True, ""


def sign(args: argparse.Namespace, path: Path, out: Path, work: Path, spec: dict, key: str) -> tuple[bool, str]:
    text = json.dumps(spec)
    base = [args.cpdf, "sign", str(path), str(SIGNING / key), text, str(out)]
    if spec.get("timestamp"):
        query, reply = work / "query.tsq", work / "reply.tsr"
        result = run(base + ["--timestamp-request", str(query)], args.timeout)
        if not query.exists():
            return False, result["output"].strip()[:300]
        (work / "tsa-serial").write_text("01\n")
        config = work / "tsa.cnf"
        config.write_text((SIGNING / "tsa.cnf").read_text().replace("./tsa-serial", str(work / "tsa-serial")))
        run(["openssl", "ts", "-reply", "-queryfile", str(query), "-inkey", str(SIGNING / "tsa.key"), "-signer", str(SIGNING / "tsa.pem"),
             "-chain", str(SIGNING / "ca.pem"), "-config", str(config), "-out", str(reply)], args.timeout)
        if not reply.exists():
            return False, "the test TSA gave no response"
        base += ["--timestamp-response", str(reply)]
    result = run(base, args.timeout)
    if result["status"] != 0 or not out.exists():
        return False, result["output"].strip()[:300]
    return True, result["output"]


def check_file(args: argparse.Namespace, path: Path, index: int, work_root: Path) -> dict:
    work = Path(tempfile.mkdtemp(dir=work_root))
    record: dict = {"file": str(path), "checks": []}
    try:
        info = run(["qpdf", "--show-encryption", str(path)], args.timeout)
        if info["status"] not in (0, 3) or "File is not encrypted" not in info["output"]:
            return {**record, "outcome": "skipped", "why": "encrypted, or does not open"}
        visible, key = index % 2 == 0, ("rsa.p12", "ec.p12")[(index // 2) % 2]
        spec = {"date": DATE, "reason": "Verifica", "location": "Capriole"}
        if visible:
            spec["rect"] = [20, 20, 220, 70]
        if index % 4 == 3:
            spec["timestamp"] = True
        out = work / "signed.pdf"
        ok, detail = sign(args, path, out, work, spec, key)
        if not ok:
            reason = "skipped" if "needed repairs" in detail or "encrypted" in detail or "open:" in detail else "fail"
            return {**record, "outcome": reason, "why": detail, "checks": [{"check": "sign", "detail": detail}]}
        rewritten = "written anew" in detail
        record.update({"key": key, "visible": visible, "timestamp": bool(spec.get("timestamp")), "rewritten": rewritten})
        failures = record["checks"]

        signatures = pdfsig(out, args.nss, args.timeout)
        if not signatures or not (signatures[-1]["valid"] and signatures[-1]["total"] and signatures[-1]["trusted"]):
            failures.append({"check": "pdfsig", "detail": json.dumps(signatures[-1:] or "no signature")})
        signed, cms = last_signature(out)
        verified, why = openssl_verifies(work, signed, cms, args.timeout)
        if not verified:
            failures.append({"check": "openssl", "detail": why})
        original = path.read_bytes()
        if not rewritten and not out.read_bytes().startswith(original):
            failures.append({"check": "incremental", "detail": "the original bytes are not the start of the signed file"})
        if qpdf_severity(out, args.timeout) > qpdf_severity(path, args.timeout):
            failures.append({"check": "qpdf", "detail": "qpdf --check worse than the original"})
        mine = capriole(args, out)
        level = "B-T" if spec.get("timestamp") else "B-B"
        if not mine or mine[-1]["integrity"] != "valid" or mine[-1]["level"] != level or mine[-1]["trust"] != "trusted":
            failures.append({"check": "capriole", "detail": json.dumps(mine[-1] if mine else None)[:300]})

        # One byte changed in the first signed range, past the header: no verifier may find it valid.
        data = bytearray(out.read_bytes())
        a, b, _, _ = map(int, list(re.finditer(rb"/ByteRange\s*\[\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", bytes(data)))[-1].groups())
        at = a + 20 + (int(hashlib.sha256(path.name.encode()).hexdigest(), 16) % max(b - 40, 1))
        data[at] ^= 0x01
        tampered = work / "tampered.pdf"
        tampered.write_bytes(bytes(data))
        broken = pdfsig(tampered, args.nss, args.timeout)
        theirs = capriole(args, tampered)
        signed_t, cms_t = last_signature(tampered)
        still, _ = openssl_verifies(work, signed_t, cms_t, args.timeout)
        if (broken and broken[-1]["valid"]) or still or (theirs and theirs[-1]["integrity"] == "valid"):
            failures.append({"check": "tampered", "detail": f"byte {at}: pdfsig {broken[-1:]}, openssl {still}, capriole {(theirs or [{}])[-1].get('integrity')}"})
        record["outcome"] = "fail" if failures else "pass"
        return record
    except Exception as error:  # noqa: BLE001 — a crash of the check is a failure of the file, reported
        return {**record, "outcome": "fail", "checks": [{"check": "exception", "detail": repr(error)[:300]}]}
    finally:
        shutil.rmtree(work, ignore_errors=True)


def compare_existing(args: argparse.Namespace, path: Path) -> dict | None:
    if b"/ByteRange" not in path.read_bytes():
        return None
    theirs = pdfsig(path, args.nss, args.timeout)
    mine = capriole(args, path, anchors=False) or []
    pairs = list(zip(mine, theirs, strict=False))
    disagree = [{"field": m["fieldName"], "capriole": m["integrity"], "poppler": p["validation"], "problem": m["problem"]}
                for m, p in pairs if (m["integrity"] == "valid") != p["valid"]]
    # Signatures only one of them lists: none of them may be valid for the other to miss.
    extra = [m for m in mine[len(pairs):] if m["integrity"] == "valid"] + [p for p in theirs[len(pairs):] if p["valid"]]
    return {"file": str(path), "capriole": [m["integrity"] for m in mine], "poppler": [p["validation"] for p in theirs],
            "disagree": disagree + [{"unmatched": json.dumps(e)[:200]} for e in extra]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="+")
    parser.add_argument("--cpdf", required=True)
    parser.add_argument("--files", type=int, default=100)
    parser.add_argument("--seed", default="capriole-phase-8h")
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="signatures")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())
    os.environ["CAPRIOLE_P12_PASSWORD"] = "capriole"

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="signatures-", dir=report))
    # An NSS database trusting the test CA, for pdfsig.
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
    disagreeing = [c for c in existing if c["disagree"]]
    signatures = sum(len(c["capriole"]) for c in existing)
    valid = sum(c["capriole"].count("valid") for c in existing)
    met = len(results) >= args.files and len(passed) == len(results) and not disagreeing
    lines = [f"# Signatures {args.name} ({stamp})", "",
             f"- **Files signed (Poppler and OpenSSL verify it, incremental, qpdf no worse, Capriole reads it back, a changed byte breaks it): "
             f"{len(passed)} / {len(results)}**",
             f"- Of them: visible {sum(r.get('visible', False) for r in results)}, with a time stamp {sum(r.get('timestamp', False) for r in results)}, "
             f"written anew first (repairs) {sum(r.get('rewritten', False) for r in results)}; passed over (encrypted, not opening) {skipped}",
             f"- **Signatures already in the corpora: Capriole's verdict is Poppler's for {len(existing) - len(disagreeing)} / {len(existing)} files** "
             f"({signatures} signatures, {valid} valid)",
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
