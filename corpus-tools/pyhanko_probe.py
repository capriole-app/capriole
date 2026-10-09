#!/usr/bin/env python3
"""What pyHanko makes of an original file, before Capriole signs it (corpus-tools/check_ltv.py).

Run with the Python of pyHanko's virtual environment; prints one JSON object:

- "strict" / "lenient": pyHanko reads every object of the file, in strict mode / with
  --no-strict-syntax (hybrid cross-references, syntax it refuses in strict mode);
- "metadataNotXmp": the catalog's /Metadata is not XMP (its root is not x:xmpmeta) — pyHanko's
  difference analysis then refuses every later revision that updates the catalog;
- "directAnnots": a page's /Annots holds annotation dictionaries directly — pyHanko then refuses every
  later revision that adds a form field;
- "badStructure": the structure tree's root, or an element of it, is null (or the tree cannot be
  walked) — pyHanko's difference analysis fails on it;
- "badSignatures": a signature the file has cannot be parsed — pyHanko fails on the whole file.

    ../CORPORA/tools/pyhanko-venv/bin/python corpus-tools/pyhanko_probe.py file.pdf
"""

from __future__ import annotations

import io
import json
import logging
import sys

from lxml import etree
from pyhanko.pdf_utils import generic
from pyhanko.pdf_utils.reader import PdfFileReader


def read(path: str, strict: bool) -> PdfFileReader | None:
    try:
        with open(path, "rb") as f:
            data = f.read()
        reader = PdfFileReader(io.BytesIO(data), strict=strict)
        # Every object of every revision: pyHanko reads lazily, and a verifier walks what it needs.
        for revision in range(reader.xrefs.total_revisions):
            for ref in reader.xrefs.explicit_refs_in_revision(revision):
                reader.get_object(ref)
        return reader
    except Exception:  # noqa: BLE001 — any failure means pyHanko cannot read it
        return None


def main() -> None:
    logging.disable(logging.CRITICAL)  # pyHanko's warnings about the file: its verdict is what is printed
    path = sys.argv[1]
    strict = read(path, True)
    reader = strict or read(path, False)
    out = {"strict": strict is not None, "lenient": reader is not None, "metadataNotXmp": False, "directAnnots": False, "badStructure": False,
           "badSignatures": False}
    if reader is not None:
        try:
            metadata = reader.root.get("/Metadata")
            if metadata is not None:
                root = etree.fromstring(metadata.get_object().data)
                out["metadataNotXmp"] = root.tag != "{adobe:ns:meta/}xmpmeta"
        except Exception:  # noqa: BLE001 — unreadable XML is not XMP either
            out["metadataNotXmp"] = True
        try:
            out["directAnnots"] = any(direct_annots(page) for page in pages(reader.root["/Pages"].get_object()))
        except Exception:  # noqa: BLE001 — a tree pyHanko cannot walk: nothing to say
            pass
        out["badStructure"] = bad_structure(reader)
        try:
            for signature in reader.embedded_signatures:
                signature.signed_data  # noqa: B018 — parsing it is the test
        except Exception:  # noqa: BLE001
            out["badSignatures"] = True
    print(json.dumps(out))


def bad_structure(reader: PdfFileReader) -> bool:
    """A structure element of the tree that is null (pyHanko's difference analysis fails on one)."""
    try:
        if "/StructTreeRoot" not in reader.root:
            return False
        tree = reader.root.raw_get("/StructTreeRoot").get_object()
        if isinstance(tree, generic.NullObject):
            return True  # a root that is null: pyHanko takes it for an element
        todo, seen = [tree], 0
        while todo and seen < 100_000:
            element = todo.pop()
            seen += 1
            kids = element.get("/K") if isinstance(element, generic.DictionaryObject) else None
            if kids is None:
                continue
            kids = kids.get_object()
            for kid in kids if isinstance(kids, generic.ArrayObject) else [kids]:
                kid = kid.get_object()
                if isinstance(kid, generic.NullObject):
                    return True
                if isinstance(kid, generic.DictionaryObject) and "/MCID" not in kid and "/Obj" not in kid:
                    todo.append(kid)
        return False
    except Exception:  # noqa: BLE001 — a tree that cannot be walked is bad too
        return True


def pages(node, depth: int = 0):
    """The page dictionaries under a node of the page tree."""
    kids = node.get("/Kids")
    if kids is None or depth > 32:
        yield node
        return
    for kid in kids:
        yield from pages(kid.get_object(), depth + 1)


def direct_annots(page) -> bool:
    annots = page.get("/Annots")
    if annots is None:
        return False
    items = annots.get_object()
    return any(not isinstance(item, generic.IndirectObject) for item in items)


if __name__ == "__main__":
    main()
