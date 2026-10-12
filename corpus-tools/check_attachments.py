#!/usr/bin/env python3
"""Attachments and Factur-X as other readers see them (PLAN.md phase 8f, exit criterion).

1. Reading: for every file of the corpora whose attachments Poppler lists (`pdfdetach -list`), Capriole
   lists the same file names, and each attachment Capriole extracts (`cpdf attach extract`, the C API)
   is byte for byte the one `pdfdetach -saveall` extracts.
2. Adding and removing: on --files files (hash order of --seed; each first saved fully, as the apps do
   with files that needed repairs), a file with a name beyond ASCII is attached (`cpdf attach add`,
   saved incrementally). Poppler (`pdfdetach`), qpdf (`--list-attachments`, `--show-attachment`) and
   MuPDF (`mutool run`: the /EmbeddedFiles name tree read by its parser) list it and give its bytes
   back exactly; `qpdf --check` is no worse than before. Removed (`cpdf attach remove`), none of them
   lists it.
3. Factur-X: an invoice of each profile (MINIMUM, BASIC WL, BASIC, EN 16931, EXTENDED) embedded
   (`cpdf facturx embed`) in a blank page and in --facturx-files files of the corpus. Poppler extracts
   factur-x.xml identical; MuPDF finds it in the catalog's /AF with /AFRelationship (Data for MINIMUM and
   BASIC WL, Alternative otherwise), /Subtype text/xml and /Params /ModDate; the XMP (read by MuPDF,
   parsed by lxml) is well-formed and says fx:DocumentType INVOICE, fx:DocumentFileName factur-x.xml,
   fx:Version 1.0, fx:ConformanceLevel the profile, with the fx extension schema declared once, and
   PDF/A-3 declared only if the input declared it. When the Python package `factur-x` is importable it
   reads the invoice back too (its own checks); veraPDF, when given (--verapdf), validates as PDF/A-3B
   the outputs whose input was PDF/A-3.

Exit criterion of phase 8f (engine): every check passes.

    corpus-tools/check_attachments.py ../CORPORA/safedocs/0000 ../CORPORA/unsafe/x002 ../CORPORA/unsafe/x008 \\
        --cpdf build/release/tools/cpdf/cpdf --report-dir ../CORPORA/reports --name attachments-all
"""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from check_pages import qpdf_severity
from compare_render import run

TEST_NAME = "allegato di prova è.bin"
TEST_BYTES = bytes((i * 37 + 11) % 256 for i in range(6000))

# MuPDF's view of the attachments: the /EmbeddedFiles name tree walked by its own parser, each file
# specification's embedded stream read (decoded), and the catalog's /AF, /Metadata.
MUTOOL_SCRIPT = r"""
var doc = Document.openDocument(scriptArgs[0]);
var root = doc.getTrailer().get("Root");
var out = { files: [], af: [], xmp: null };
function str(o) { return o && o.isString() ? o.asString() : null; }
function nameOf(o) { return o && o.isName() ? o.asName() : null; }
function get(o, key) { return o && o.isDictionary() ? o.get(key) : null; }
function present(o) { return o && !o.isNull(); }
// A buffer's bytes, one character each (MuPDF 1.23: a buffer is indexed by byte; Python decodes them).
function text(b) { var s = ""; for (var i = 0; i < b.length; i++) s += String.fromCharCode(b[i]); return s; }
function walk(node, depth) {
  if (!node || depth > 32 || !node.isDictionary()) return;
  var names = node.get("Names");
  if (names && names.isArray()) {
    for (var i = 0; i + 1 < names.length; i += 2) {
      var spec = names.get(i + 1), ef = get(spec, "EF"), file = present(get(ef, "UF")) ? get(ef, "UF") : get(ef, "F");
      var entry = { key: str(names.get(i)), name: str(get(spec, "UF")) || str(get(spec, "F")), size: -1,
                    relationship: nameOf(get(spec, "AFRelationship")), subtype: null, moddate: null,
                    num: spec && spec.isIndirect() ? spec.asIndirect() : -1 };
      if (file && file.isStream()) {
        entry.size = file.readStream().length;
        entry.subtype = nameOf(get(file, "Subtype"));
        entry.moddate = str(get(get(file, "Params"), "ModDate"));
      }
      out.files.push(entry);
    }
  }
  var kids = node.get("Kids");
  if (kids && kids.isArray()) for (var k = 0; k < kids.length; k++) walk(kids.get(k), depth + 1);
}
var namesDict = root.get("Names");
if (namesDict && namesDict.isDictionary()) walk(namesDict.get("EmbeddedFiles"), 0);
var af = root.get("AF");
if (af && af.isArray()) for (var a = 0; a < af.length; a++) out.af.push(af.get(a).isIndirect() ? af.get(a).asIndirect() : -1);
var meta = root.get("Metadata");
if (meta && meta.isStream()) out.xmp = text(meta.readStream());
print(JSON.stringify(out));
"""

GUIDELINES = {
    "MINIMUM": "urn:factur-x.eu:1p0:minimum",
    "BASIC WL": "urn:factur-x.eu:1p0:basicwl",
    "BASIC": "urn:cen.eu:en16931:2017#compliant#urn:factur-x.eu:1p0:basic",
    "EN 16931": "urn:cen.eu:en16931:2017",
    "EXTENDED": "urn:cen.eu:en16931:2017#conformant#urn:factur-x.eu:1p0:extended",
}


def invoice(level: str) -> bytes:
    """A small Cross Industry Invoice of a profile, valid against its official XSD (as the factur-x
    library checks it): one line for the profiles that have lines (BASIC and up), the tax of the
    document from BASIC WL up."""
    lines = level in ("BASIC", "EN 16931", "EXTENDED")
    line = """    <ram:IncludedSupplyChainTradeLineItem>
      <ram:AssociatedDocumentLineDocument><ram:LineID>1</ram:LineID></ram:AssociatedDocumentLineDocument>
      <ram:SpecifiedTradeProduct><ram:Name>Consulenza</ram:Name></ram:SpecifiedTradeProduct>
      <ram:SpecifiedLineTradeAgreement>
        <ram:NetPriceProductTradePrice><ram:ChargeAmount>100.00</ram:ChargeAmount></ram:NetPriceProductTradePrice>
      </ram:SpecifiedLineTradeAgreement>
      <ram:SpecifiedLineTradeDelivery><ram:BilledQuantity unitCode="C62">1</ram:BilledQuantity></ram:SpecifiedLineTradeDelivery>
      <ram:SpecifiedLineTradeSettlement>
        <ram:ApplicableTradeTax><ram:TypeCode>VAT</ram:TypeCode><ram:CategoryCode>S</ram:CategoryCode><ram:RateApplicablePercent>22</ram:RateApplicablePercent></ram:ApplicableTradeTax>
        <ram:SpecifiedTradeSettlementLineMonetarySummation><ram:LineTotalAmount>100.00</ram:LineTotalAmount></ram:SpecifiedTradeSettlementLineMonetarySummation>
      </ram:SpecifiedLineTradeSettlement>
    </ram:IncludedSupplyChainTradeLineItem>
""" if lines else ""
    tax = """      <ram:ApplicableTradeTax>
        <ram:CalculatedAmount>22.00</ram:CalculatedAmount><ram:TypeCode>VAT</ram:TypeCode><ram:BasisAmount>100.00</ram:BasisAmount>
        <ram:CategoryCode>S</ram:CategoryCode><ram:RateApplicablePercent>22</ram:RateApplicablePercent>
      </ram:ApplicableTradeTax>
""" if level != "MINIMUM" else ""
    line_total = "<ram:LineTotalAmount>100.00</ram:LineTotalAmount>" if level != "MINIMUM" else ""
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<rsm:CrossIndustryInvoice xmlns:rsm="urn:un:unece:uncefact:data:standard:CrossIndustryInvoice:100"
 xmlns:qdt="urn:un:unece:uncefact:data:standard:QualifiedDataType:100"
 xmlns:ram="urn:un:unece:uncefact:data:standard:ReusableAggregateBusinessInformationEntity:100"
 xmlns:udt="urn:un:unece:uncefact:data:standard:UnqualifiedDataType:100">
  <rsm:ExchangedDocumentContext>
    <ram:GuidelineSpecifiedDocumentContextParameter><ram:ID>{GUIDELINES[level]}</ram:ID></ram:GuidelineSpecifiedDocumentContextParameter>
  </rsm:ExchangedDocumentContext>
  <rsm:ExchangedDocument>
    <ram:ID>FA-2026-0042</ram:ID>
    <ram:TypeCode>380</ram:TypeCode>
    <ram:IssueDateTime><udt:DateTimeString format="102">20261003</udt:DateTimeString></ram:IssueDateTime>
  </rsm:ExchangedDocument>
  <rsm:SupplyChainTradeTransaction>
{line}    <ram:ApplicableHeaderTradeAgreement>
      <ram:SellerTradeParty><ram:Name>Capriole S.r.l.</ram:Name>
        <ram:PostalTradeAddress><ram:CountryID>IT</ram:CountryID></ram:PostalTradeAddress>
        <ram:SpecifiedTaxRegistration><ram:ID schemeID="VA">IT01234567890</ram:ID></ram:SpecifiedTaxRegistration>
      </ram:SellerTradeParty>
      <ram:BuyerTradeParty><ram:Name>Cliente S.p.A.</ram:Name></ram:BuyerTradeParty>
    </ram:ApplicableHeaderTradeAgreement>
    <ram:ApplicableHeaderTradeDelivery/>
    <ram:ApplicableHeaderTradeSettlement>
      <ram:InvoiceCurrencyCode>EUR</ram:InvoiceCurrencyCode>
{tax}      <ram:SpecifiedTradeSettlementHeaderMonetarySummation>
        {line_total}<ram:TaxBasisTotalAmount>100.00</ram:TaxBasisTotalAmount>
        <ram:TaxTotalAmount currencyID="EUR">22.00</ram:TaxTotalAmount>
        <ram:GrandTotalAmount>122.00</ram:GrandTotalAmount>
        <ram:DuePayableAmount>122.00</ram:DuePayableAmount>
      </ram:SpecifiedTradeSettlementHeaderMonetarySummation>
    </ram:ApplicableHeaderTradeSettlement>
  </rsm:SupplyChainTradeTransaction>
</rsm:CrossIndustryInvoice>
""".encode()


def blank_pdf() -> bytes:
    bodies = ["<</Type/Catalog/Pages 2 0 R>>", "<</Type/Pages/Kids[3 0 R]/Count 1>>", "<</Type/Page/Parent 2 0 R/MediaBox[0 0 595 842]>>"]
    pdf = "%PDF-1.7\n"
    offsets = []
    for i, body in enumerate(bodies):
        offsets.append(len(pdf))
        pdf += f"{i + 1} 0 obj\n{body}\nendobj\n"
    xref = len(pdf)
    pdf += f"xref\n0 {len(bodies) + 1}\n0000000000 65535 f \n" + "".join(f"{o:010d} 00000 n \n" for o in offsets)
    pdf += f"trailer\n<</Size {len(bodies) + 1}/Root 1 0 R>>\nstartxref\n{xref}\n%%EOF\n"
    return pdf.encode("latin-1")


def poppler_list(path: Path, timeout: float) -> list[str] | None:
    result = run(["pdfdetach", "-enc", "UTF-8", "-list", str(path)], timeout)
    if result["status"] != 0:
        return None
    lines = [l for l in result["output"].splitlines() if l and l[0].isdigit() and ": " in l]
    return [l.split(": ", 1)[1] for l in lines]


def poppler_save_all(path: Path, out: Path, timeout: float) -> list[bytes] | None:
    out.mkdir(parents=True, exist_ok=True)
    names = poppler_list(path, timeout)
    if names is None:
        return None
    files = []
    for i in range(1, len(names) + 1):
        target = out / f"{i}.bin"
        if run(["pdfdetach", "-save", str(i), "-o", str(target), str(path)], timeout)["status"] != 0 or not target.exists():
            return None
        files.append(target.read_bytes())
    return files


def mutool_view(path: Path, script: Path, timeout: float) -> dict | None:
    result = run(["mutool", "run", str(script), str(path)], timeout)
    for line in result["output"].splitlines():
        if line.startswith("{"):
            try:
                view = json.loads(line)
            except json.JSONDecodeError:
                return None
            if view.get("xmp"):
                view["xmp"] = view["xmp"].encode("latin-1", "replace").decode("utf-8", "replace")
            return view
    return None


def capriole_list(args: argparse.Namespace, path: Path) -> list[dict] | None:
    result = run([args.cpdf, "attach", "list", str(path)], args.timeout)
    try:
        return json.loads(result["output"]) if result["status"] == 0 else None
    except json.JSONDecodeError:
        return None


def capriole_extract(args: argparse.Namespace, path: Path, number: int, out: Path) -> bytes | None:
    result = run([args.cpdf, "attach", "extract", str(path), str(number), str(out)], args.timeout)
    return out.read_bytes() if result["status"] == 0 and out.exists() else None


def clean(work: Path, stem: str) -> None:
    """What the checks of one file left in work (files and directories)."""
    for leftover in work.glob(f"{stem}-*"):
        if leftover.is_dir():
            shutil.rmtree(leftover, ignore_errors=True)
        else:
            leftover.unlink(missing_ok=True)


def check_reading(args: argparse.Namespace, path: Path, work: Path) -> dict:
    result = {"file": str(path), "check": "reading", "outcome": "pass", "attachments": 0, "details": []}
    names = poppler_list(path, args.timeout)
    if not names:
        result["outcome"] = "skipped"
        return result
    result["attachments"] = len(names)
    mine = capriole_list(args, path)
    if mine is None:
        result["outcome"] = "fail"
        result["details"].append("cpdf attach list failed")
        return result
    if sorted(names) != sorted(a["fileName"] or a["name"] for a in mine):
        result["outcome"] = "fail"
        result["details"].append(f"names: poppler {names}, capriole {[a['fileName'] for a in mine]}")
        return result
    theirs = poppler_save_all(path, work / f"r{path.stem}-poppler", args.timeout)
    if theirs is None:
        result["details"].append("pdfdetach cannot extract: bytes not compared")
        return result
    ours = []
    for i, a in enumerate(mine):
        data = capriole_extract(args, path, a["ref"][0], work / f"r{path.stem}-{i}.bin")
        if data is None:
            result["outcome"] = "fail"
            result["details"].append(f"{a['fileName']}: cpdf attach extract failed")
            return result
        ours.append(data)
    if sorted(hashlib.sha256(b).hexdigest() for b in theirs) != sorted(hashlib.sha256(b).hexdigest() for b in ours):
        result["outcome"] = "fail"
        result["details"].append("the bytes differ from pdfdetach's")
    clean(work, f"r{path.stem}")
    return result


def qpdf_attachment(path: Path, key: str, timeout: float) -> bytes | None:
    with tempfile.TemporaryDirectory() as tmp:
        try:
            proc = subprocess.run(["qpdf", f"--show-attachment={key}", str(path)], capture_output=True, timeout=timeout, cwd=tmp)
        except subprocess.TimeoutExpired:
            return None
        return proc.stdout if proc.returncode in (0, 3) else None


def check_editing(args: argparse.Namespace, path: Path, work: Path, script: Path) -> dict:
    result = {"file": str(path), "check": "editing", "outcome": "pass", "details": []}

    def fail(detail: str) -> None:
        result["outcome"] = "fail"
        result["details"].append(detail[:300])

    sound = work / f"{path.stem}-sound.pdf"
    if run([args.cpdf, "save", str(path), str(sound)], args.timeout)["status"] != 0:
        result["outcome"] = "skipped"
        return result
    # In its own directory: the jobs run side by side.
    own = work / f"{path.stem}-data"
    own.mkdir(exist_ok=True)
    data = own / TEST_NAME
    data.write_bytes(TEST_BYTES)
    added = work / f"{path.stem}-added.pdf"
    out = run([args.cpdf, "attach", "add", str(sound), str(data), str(added), "application/octet-stream", "Supplement", "prova"], args.timeout)
    if out["status"] != 0 and "the document catalog cannot be read" in out["output"]:
        # No catalog (missing, or not a dictionary): nowhere to attach a file. Reported apart.
        result["outcome"] = "skipped"
        result["no_catalog"] = True
        clean(work, path.stem)
        return result
    if out["status"] != 0:
        fail(f"add: {out['output']}")
        return result
    number = int(out["output"].split("added ", 1)[1].split()[0])
    if qpdf_severity(added, args.timeout) > qpdf_severity(sound, args.timeout):
        fail("qpdf --check worse than the original")
    # Poppler.
    names = poppler_list(added, args.timeout) or []
    if TEST_NAME not in names:
        fail(f"pdfdetach does not list it: {names}")
    else:
        target = work / f"{path.stem}-poppler.bin"
        run(["pdfdetach", "-save", str(names.index(TEST_NAME) + 1), "-o", str(target), str(added)], args.timeout)
        if not target.exists() or target.read_bytes() != TEST_BYTES:
            fail("pdfdetach extracts other bytes")
    # qpdf.
    listed = run(["qpdf", "--list-attachments", str(added)], args.timeout)["output"]
    if TEST_NAME not in listed:
        fail("qpdf does not list it")
    elif qpdf_attachment(added, TEST_NAME, args.timeout) != TEST_BYTES:
        fail("qpdf --show-attachment gives other bytes")
    # MuPDF.
    view = mutool_view(added, script, args.timeout)
    entry = next((f for f in (view or {}).get("files", []) if f.get("num") == number), None)
    if entry is None:
        fail("MuPDF does not find it in /EmbeddedFiles")
    elif entry["size"] != len(TEST_BYTES) or entry["relationship"] != "Supplement" or number not in view["af"]:
        fail(f"MuPDF reads {entry['size']} bytes, relationship {entry['relationship']}, /AF {view['af']}")
    # Removed.
    removed = work / f"{path.stem}-removed.pdf"
    if run([args.cpdf, "attach", "remove", str(added), str(number), str(removed)], args.timeout)["status"] != 0:
        fail("remove failed")
    else:
        if TEST_NAME in (poppler_list(removed, args.timeout) or []):
            fail("pdfdetach still lists it")
        if TEST_NAME in run(["qpdf", "--list-attachments", str(removed)], args.timeout)["output"]:
            fail("qpdf still lists it")
        after = mutool_view(removed, script, args.timeout) or {}
        if any(f.get("num") == number for f in after.get("files", [])) or number in after.get("af", []):
            fail("MuPDF still finds it")
    clean(work, path.stem)
    return result


def xmp_facts(xmp: str) -> dict:
    """The XMP's Factur-X facts, parsed as XML (lxml): None for a packet that is not well-formed."""
    from lxml import etree

    body = xmp[xmp.find("<x:xmpmeta") : xmp.rfind("</x:xmpmeta>") + len("</x:xmpmeta>")]
    root = etree.fromstring(body.encode("utf-8"))
    fx = "urn:factur-x:pdfa:CrossIndustryDocument:invoice:1p0#"
    rdf = "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
    facts: dict = {}
    for d in root.iter(f"{{{rdf}}}Description"):
        for child in d:
            if child.tag.startswith(f"{{{fx}}}"):
                facts[child.tag.split("}")[1]] = child.text
        for attr, value in d.attrib.items():
            if attr.startswith(f"{{{fx}}}"):
                facts[attr.split("}")[1]] = value
            if attr == "{http://www.aiim.org/pdfa/ns/id/}part":
                facts["pdfa_part"] = value
        for child in d:
            if child.tag == "{http://www.aiim.org/pdfa/ns/id/}part":
                facts["pdfa_part"] = child.text
    facts["extensions"] = sum(1 for e in root.iter("{http://www.aiim.org/pdfa/ns/schema#}namespaceURI") if (e.text or "").strip() == fx)
    return facts


def check_facturx(args: argparse.Namespace, path: Path, work: Path, script: Path, level: str) -> dict:
    result = {"file": str(path), "check": f"facturx {level}", "outcome": "pass", "details": []}

    def fail(detail: str) -> None:
        result["outcome"] = "fail"
        result["details"].append(detail[:300])

    stem = f"{path.stem}-{level.replace(' ', '')}"
    sound = work / f"{stem}-sound.pdf"
    if run([args.cpdf, "save", str(path), str(sound)], args.timeout)["status"] != 0:
        result["outcome"] = "skipped"
        return result
    before = mutool_view(sound, script, args.timeout) or {}
    was_pdfa3 = False
    if before.get("xmp"):
        try:
            was_pdfa3 = xmp_facts(before["xmp"]).get("pdfa_part") == "3"
        except Exception:  # noqa: BLE001 - a packet lxml cannot read: not PDF/A-3 as far as this check goes
            was_pdfa3 = False
    xml = work / f"{stem}.xml"
    xml.write_bytes(invoice(level))
    out = work / f"{stem}-fx.pdf"
    embedded = run([args.cpdf, "facturx", "embed", str(sound), str(xml), str(out)], args.timeout)
    if embedded["status"] != 0:
        fail(f"embed: {embedded['output']}")
        return result
    if qpdf_severity(out, args.timeout) > qpdf_severity(sound, args.timeout):
        fail("qpdf --check worse than the original")
    names = poppler_list(out, args.timeout) or []
    if "factur-x.xml" not in names:
        fail(f"pdfdetach does not list factur-x.xml: {names}")
    else:
        target = work / f"{stem}-poppler.xml"
        run(["pdfdetach", "-save", str(names.index("factur-x.xml") + 1), "-o", str(target), str(out)], args.timeout)
        if not target.exists() or target.read_bytes() != xml.read_bytes():
            fail("pdfdetach extracts another factur-x.xml")
    view = mutool_view(out, script, args.timeout)
    entry = next((f for f in (view or {}).get("files", []) if f.get("name") == "factur-x.xml"), None)
    expected_relationship = "Data" if level in ("MINIMUM", "BASIC WL") else "Alternative"
    if entry is None:
        fail("MuPDF does not find factur-x.xml")
    else:
        if entry["num"] not in view["af"]:
            fail("factur-x.xml is not in the catalog's /AF")
        if entry["relationship"] != expected_relationship:
            fail(f"/AFRelationship {entry['relationship']}, expected {expected_relationship}")
        if entry["subtype"] != "text/xml":
            fail(f"/Subtype {entry['subtype']}")
        if not entry["moddate"]:
            fail("no /Params /ModDate")
    try:
        facts = xmp_facts((view or {}).get("xmp") or "")
    except Exception as error:  # noqa: BLE001 - reported
        fail(f"the XMP is not well-formed: {error}")
        facts = {}
    want = {"DocumentType": "INVOICE", "DocumentFileName": "factur-x.xml", "Version": "1.0", "ConformanceLevel": level}
    for key, value in want.items():
        if facts and facts.get(key) != value:
            fail(f"XMP fx:{key} is {facts.get(key)!r}, expected {value!r}")
    if facts and facts.get("extensions") != 1:
        fail(f"the fx extension schema is declared {facts.get('extensions')} times")
    if facts and (facts.get("pdfa_part") == "3") != was_pdfa3:
        fail(f"PDF/A-3 declared {facts.get('pdfa_part')!r}, the input {'was' if was_pdfa3 else 'was not'}")
    result["pdfa3"] = was_pdfa3
    # The reference library, when installed.
    if args.facturx_python:
        # It finds the invoice in the PDF, checks it against the profile's XSD, and says the profile.
        code = (
            "import sys, logging; logging.disable(logging.CRITICAL); import facturx; from lxml import etree;"
            "name, xml = facturx.get_facturx_xml_from_pdf(open(sys.argv[1],'rb').read(), check_xsd=True);"
            "same = xml == open(sys.argv[2],'rb').read();"
            "print('FX', name, same, facturx.get_level(etree.fromstring(xml)))"
        )
        lib = run([args.facturx_python, "-c", code, str(out), str(xml)], args.timeout)
        library_level = {"MINIMUM": "minimum", "BASIC WL": "basicwl", "BASIC": "basic", "EN 16931": "en16931", "EXTENDED": "extended"}[level]
        if lib["status"] != 0 or f"FX factur-x.xml True {library_level}" not in lib["output"]:
            fail(f"factur-x library: {lib['output'][-300:]}")
    if args.verapdf and was_pdfa3:
        vera = run([args.verapdf, "--flavour", "3b", "--format", "text", str(out)], args.timeout * 2)
        if "PASS" not in vera["output"]:
            fail(f"veraPDF: {vera['output'][-300:]}")
    clean(work, stem)
    for leftover in work.glob(f"{stem}.xml"):
        leftover.unlink()
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", nargs="+")
    parser.add_argument("--cpdf", required=True)
    parser.add_argument("--files", type=int, default=100)
    parser.add_argument("--facturx-files", type=int, default=20)
    parser.add_argument("--seed", default="capriole-phase-8f")
    parser.add_argument("--facturx-python", default=None, help="a Python with the factur-x package (e.g. a venv's python)")
    parser.add_argument("--verapdf", default=None, help="the veraPDF command line")
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--report-dir", default="../CORPORA/reports")
    parser.add_argument("--name", default="attachments")
    args = parser.parse_args()
    args.cpdf = str(Path(args.cpdf).resolve())
    # Absolute, not resolved: a venv's python is a link, and resolving it would leave the venv.
    args.facturx_python = os.path.abspath(args.facturx_python) if args.facturx_python else None
    args.verapdf = os.path.abspath(args.verapdf) if args.verapdf else None

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report = Path(args.report_dir).resolve() / f"{args.name}-{stamp}"
    report.mkdir(parents=True)
    work = Path(tempfile.mkdtemp(prefix="attachments-", dir=report))
    script = work / "attachments.js"
    script.write_text(MUTOOL_SCRIPT)
    blank = work / "blank.pdf"
    blank.write_bytes(blank_pdf())

    files = [p.resolve() for d in args.corpus for p in sorted(Path(d).glob("*.pdf"))]
    ordered = sorted(files, key=lambda f: hashlib.sha256(f"{args.seed}:{f.name}".encode()).hexdigest())
    results: list[dict] = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool, open(report / "results.jsonl", "w") as log:

        def keep(r: dict) -> None:
            results.append(r)
            log.write(json.dumps(r) + "\n")

        for r in pool.map(lambda p: check_reading(args, p, work), files):
            keep(r)
        edited = 0
        no_catalog = 0
        for chunk_start in range(0, len(ordered), args.jobs * 4):
            if edited >= args.files:
                break
            for r in pool.map(lambda p: check_editing(args, p, work, script), ordered[chunk_start : chunk_start + args.jobs * 4]):
                if edited < args.files and r["outcome"] != "skipped":
                    edited += 1
                    keep(r)
                no_catalog += 1 if r.get("no_catalog") else 0
        targets = [blank] + ordered[: args.facturx_files]
        jobs = [(p, level) for p in targets for level in GUIDELINES]
        for r in pool.map(lambda job: check_facturx(args, job[0], work, script, job[1]), jobs):
            if r["outcome"] != "skipped":
                keep(r)
    shutil.rmtree(work)

    def summary(kind: str) -> tuple[int, int]:
        counted = [r for r in results if r["check"].startswith(kind) and r["outcome"] != "skipped"]
        return sum(r["outcome"] == "pass" for r in counted), len(counted)

    reading, editing, facturx = summary("reading"), summary("editing"), summary("facturx")
    attachments = sum(r.get("attachments", 0) for r in results if r["check"] == "reading" and r["outcome"] != "skipped")
    met = all(p == n and n > 0 for p, n in (reading, editing, facturx))
    lines = [f"# Attachments {args.name} ({stamp})", "",
             f"- **Reading (as pdfdetach: names, bytes): {reading[0]} / {reading[1]} files ({attachments} attachments)**",
             f"- **Adding and removing (Poppler, qpdf, MuPDF; qpdf no worse): {editing[0]} / {editing[1]} files**",
             f"- **Factur-X (five profiles; Poppler, MuPDF, the XMP): {facturx[0]} / {facturx[1]}**"
             + (" — with the factur-x library" if args.facturx_python else " — without the factur-x library")
             + (", veraPDF on PDF/A-3 inputs" if args.verapdf else ", without veraPDF"),
             f"- Inputs declared PDF/A-3: {sum(1 for r in results if r.get('pdfa3'))}; files without a readable catalog "
             f"(nothing to attach to: refused, not counted): {no_catalog}",
             f"- Exit criterion (every check): **{'met' if met else 'not met'}**", "", "## Failures", ""]
    lines += [f"- {r['check']} {Path(r['file']).name}: " + "; ".join(r["details"])[:600] for r in results if r["outcome"] == "fail"] or ["- none"]
    (report / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:7]))
    print(f"report: {report}")


if __name__ == "__main__":
    main()
