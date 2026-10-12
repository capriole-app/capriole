#!/usr/bin/env python3
"""The notices of the third-party code and data built into the engine (every binary: native, WebAssembly,
the apps), as their licences ask: THIRD_PARTY_NOTICES.md for people, third_party/notices.json for the
apps' «Licenze» page (the apps add their own JavaScript packages, the desktop shell its Rust crates).

The texts are taken verbatim from the dependencies' sources as the build fetched them (cmake/Dependencies.cmake,
pinned by SHA-256), and from third_party/licenses/ for data generated once (the Adobe Glyph List). Tests-only
dependencies (GoogleTest) are not in a binary and not listed.

    scripts/gen_notices.py [--build-dir build/release]           # writes both files
    scripts/gen_notices.py --check [--build-dir build/asan]      # fails when they are not up to date
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# name, version (as it appears in cmake/Dependencies.cmake), licence (SPDX), where in the engine, the text's
# files (relative to the build directory, or to the repository for third_party/), and a note.
DEPENDENCIES = [
    ("zlib", "1.3.1", "Zlib", "Flate compression", ["_deps/zlib-src/LICENSE"], None),
    ("Blend2D", "58ca9460b4138af6e793184682e9324e0a8053b2", "Zlib", "rendering", ["_deps/blend2d-src/LICENSE.md"], None),
    ("AsmJit", "0d7f01052f2d3f6d6b9bb6714ef32bd7b80177eb", "Zlib", "inside Blend2D", ["_deps/asmjit-src/LICENSE.md"], None),
    ("libjpeg-turbo", "3.2.0", "IJG AND BSD-3-Clause", "JPEG images (DCTDecode)",
     ["_deps/libjpeg-turbo/build/src/libjpeg_turbo/LICENSE.md", "ijg:_deps/libjpeg-turbo/build/src/libjpeg_turbo/README.ijg"],
     "This software is based in part on the work of the Independent JPEG Group."),
    ("OpenJPEG", "2.5.4", "BSD-2-Clause", "JPEG 2000 images (JPXDecode)", ["_deps/openjpeg/build/src/openjpeg/LICENSE"], None),
    ("FreeType", "2.14.3", "FTL", "glyph outlines", ["_deps/freetype-src/docs/FTL.TXT"],
     "Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved."),
    ("Little CMS", "2.19.1", "MIT", "colour management (ICC profiles)", ["_deps/lcms2-src/LICENSE"], None),
    ("Mbed TLS", "3.6.7", "Apache-2.0", "digital signatures (X.509, RSA, ECDSA, hashes)", ["apache:_deps/mbedtls-src/LICENSE"],
     "Mbed TLS is offered under Apache-2.0 or GPL-2.0-or-later; Capriole uses it under Apache-2.0."),
    ("Foxit standard fonts (PDFium)", "2.16.105", "BSD-3-Clause", "substitutes for the 14 standard fonts",
     ["_deps/foxit_fonts-src/standard_fonts/LICENSE_FOXIT"], None),
    ("Adobe Glyph List and AGLFN", "4036a9ca", "BSD-3-Clause", "glyph names → Unicode (generated tables)",
     ["repo:third_party/licenses/agl-aglfn-LICENSE.md"], "Copyright 2002-2019 Adobe (https://github.com/adobe-type-tools/agl-aglfn)."),
    ("SWOP2006_Coated3v2 colour samples", "registry.color.org", "LicenseRef-IDEAlliance-SWOP", "DeviceCMYK → sRGB table (generated)",
     [], "SWOP2006_Coated3v2.icc: made available by IDEAlliance, with permission of X-Rite, Inc.; may be used, embedded, "
         "exchanged, and shared without restriction. It may not be altered, or sold without written permission of IDEAlliance. "
         "Source: https://registry.color.org/profile-registry/SWOP2006_Coated3v2"),
]

# The IJG licence is the "LEGAL ISSUES" section of README.ijg (the rest is documentation).
IJG = re.compile(r"^LEGAL ISSUES\n=+\n(.*?)^REFERENCES\n=+\n", re.S | re.M)


def text_of(build: Path, source: str) -> str:
    if source.startswith("repo:"):
        return (ROOT / source[5:]).read_text()
    if source.startswith("apache:"):
        # A dual-licensed project's LICENSE: the Apache-2.0 text only (up to the separator before the other).
        text = (build / source[7:]).read_text()
        return text.split("\n" + "=" * 79 + "\n")[0]
    if source.startswith("ijg:"):
        m = IJG.search((build / source[4:]).read_text())
        if not m:
            sys.exit(f"gen_notices: no LEGAL ISSUES section in {source[4:]}")
        return "The Independent JPEG Group's licence (README.ijg, LEGAL ISSUES):\n\n" + m.group(1).strip() + "\n"
    path = build / source
    if not path.exists():
        sys.exit(f"gen_notices: {path} is missing — build first (cmake --build --preset ...), or pass --build-dir")
    return path.read_text(errors="replace")


def notices(build: Path) -> list[dict]:
    dependencies_cmake = (ROOT / "cmake/Dependencies.cmake").read_text()
    out = []
    for name, version, spdx, use, sources, note in DEPENDENCIES:
        # A version that is no longer the one the build pins: the table here is out of date.
        if version not in dependencies_cmake and version != "registry.color.org" and not version.startswith("4036a9ca"):
            sys.exit(f"gen_notices: {name} {version} is not what cmake/Dependencies.cmake pins: update DEPENDENCIES here")
        texts = [text_of(build, s).rstrip() + "\n" for s in sources]
        out.append({"name": name, "version": version, "license": spdx, "use": use, "note": note, "text": "\n\n".join(texts)})
    return out


def markdown(items: list[dict]) -> str:
    lines = [
        "# Third-party notices",
        "",
        "Capriole itself is licensed under 0BSD (`LICENSE`). Built into the engine — and so into every binary:",
        "the native library, the WebAssembly module, the apps — are the following, under their own licences.",
        "The apps list, in their «Licenze» page, these and the JavaScript packages (and, on the desktop, the Rust",
        "crates) they bundle; the resource pack's fonts, CMaps and profile carry their licences in",
        "`resources/LICENSES/`.",
        "",
        "Generated by `scripts/gen_notices.py` from the dependencies' sources: do not edit.",
        "",
    ]
    for item in items:
        lines += [f"## {item['name']} {item['version']}", "", f"Licence: {item['license']} — used for: {item['use']}.", ""]
        if item["note"]:
            lines += [item["note"], ""]
        if item["text"]:
            lines += ["```text", item["text"].rstrip("\n").replace("```", "'''"), "```", ""]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="build/release")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    build = (ROOT / args.build_dir).resolve()
    items = notices(build)
    files = {
        ROOT / "THIRD_PARTY_NOTICES.md": markdown(items),
        ROOT / "third_party/notices.json": json.dumps(items, ensure_ascii=False, indent=1) + "\n",
    }
    if args.check:
        stale = [str(p.relative_to(ROOT)) for p, text in files.items() if not p.exists() or p.read_text() != text]
        if stale:
            sys.exit(f"gen_notices: out of date: {', '.join(stale)} — run scripts/gen_notices.py")
        print("notices up to date")
        return
    for path, text in files.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        print(f"wrote {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
