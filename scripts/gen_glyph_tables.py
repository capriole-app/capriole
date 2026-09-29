#!/usr/bin/env python3
"""Generates engine/src/font/glyph_tables.cpp: the Adobe Glyph List (glyph name -> Unicode) and the
standard Latin encodings of PDF (ISO 32000-2 Annex D) as glyph names.

    scripts/gen_glyph_tables.py            # downloads the AGL at the pinned commit, checks its hashes
    scripts/gen_glyph_tables.py DIR        # or reads glyphlist.txt, aglfn.txt, zapfdingbats.txt from DIR

Sources:
- Adobe Glyph List and AGLFN (BSD-3-Clause, adobe-type-tools/agl-aglfn), pinned below.
- WinAnsiEncoding: Windows code page 1252 (Python's cp1252), with Annex D's additions: the unused codes
  above 0x7E are bullets, 0xA0 is also "space" and 0xAD also "hyphen".
- MacRomanEncoding: Mac OS Roman (Python's mac_roman) with "currency" at 0xDB, as in Annex D (Mac OS
  later put the euro there), and no Apple logo.
- StandardEncoding: Adobe's standard Latin encoding of Type 1 fonts, written out below.
Glyph names for Unicode values come from AGLFN, else the first AGL name.
"""

import hashlib
import sys
import urllib.request
from pathlib import Path

COMMIT = "4036a9ca80a62f64f9de4f7321a9a045ad0ecfd6"
FILES = {
    "glyphlist.txt": "a3b2f61ced9f3644cc0d4ecde5c59df34ca286c689d9484a43a710a81c466789",
    "aglfn.txt": "ed735a9ea58549b4cb2b8e804341548adb25aff43e81001ac7f3917247cacca1",
    "zapfdingbats.txt": "f6394e3cb8a447e84a1dad75d4baaf2aa7f45dc104faf369f4720e1a774ef2dc",
}
OUT = Path(__file__).resolve().parent.parent / "engine/src/font/glyph_tables.cpp"


def fetch(name, directory):
    if directory:
        data = (Path(directory) / name).read_bytes()
    else:
        url = f"https://raw.githubusercontent.com/adobe-type-tools/agl-aglfn/{COMMIT}/{name}"
        with urllib.request.urlopen(url) as response:
            data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != FILES[name]:
        sys.exit(f"{name}: SHA-256 {digest}, expected {FILES[name]}")
    return data.decode("ascii")


def entries(text):
    for line in text.splitlines():
        if line and not line.startswith("#"):
            yield line.split(";")


def main():
    directory = sys.argv[1] if len(sys.argv) > 1 else None
    names = {}  # glyph name -> first Unicode value
    for name, values in entries(fetch("glyphlist.txt", directory)):
        names[name] = int(values.split()[0], 16)
    for name, value in entries(fetch("zapfdingbats.txt", directory)):
        names.setdefault(name, int(value, 16))
    preferred = {}  # Unicode value -> AGLFN name
    for value, name, _ in entries(fetch("aglfn.txt", directory)):
        preferred[int(value, 16)] = name
    first = {}
    for name, value in names.items():
        first.setdefault(value, name)

    def name_of(char):
        value = ord(char)
        name = preferred.get(value) or first.get(value)
        if name is None:
            sys.exit(f"no glyph name for U+{value:04X}")
        return name

    win = [""] * 256
    mac = [""] * 256
    for code in range(0x20, 256):
        byte = bytes([code])
        try:
            win[code] = name_of(byte.decode("cp1252"))
        except UnicodeDecodeError:
            win[code] = "bullet"
        mac[code] = name_of(byte.decode("mac_roman"))
    win[0x7F] = "bullet"
    win[0xA0] = "space"
    win[0xAD] = "hyphen"
    mac[0x7F] = ""
    mac[0xCA] = "space"
    mac[0xDB] = "currency"
    mac[0xF0] = ""

    standard = [""] * 256
    for code in range(0x20, 0x7F):
        standard[code] = name_of(chr(code))
    standard[0x27] = "quoteright"
    standard[0x60] = "quoteleft"
    upper = {
        0xA1: "exclamdown", 0xA2: "cent", 0xA3: "sterling", 0xA4: "fraction", 0xA5: "yen", 0xA6: "florin",
        0xA7: "section", 0xA8: "currency", 0xA9: "quotesingle", 0xAA: "quotedblleft", 0xAB: "guillemotleft",
        0xAC: "guilsinglleft", 0xAD: "guilsinglright", 0xAE: "fi", 0xAF: "fl", 0xB1: "endash", 0xB2: "dagger",
        0xB3: "daggerdbl", 0xB4: "periodcentered", 0xB6: "paragraph", 0xB7: "bullet", 0xB8: "quotesinglbase",
        0xB9: "quotedblbase", 0xBA: "quotedblright", 0xBB: "guillemotright", 0xBC: "ellipsis", 0xBD: "perthousand",
        0xBF: "questiondown", 0xC1: "grave", 0xC2: "acute", 0xC3: "circumflex", 0xC4: "tilde", 0xC5: "macron",
        0xC6: "breve", 0xC7: "dotaccent", 0xC8: "dieresis", 0xCA: "ring", 0xCB: "cedilla", 0xCD: "hungarumlaut",
        0xCE: "ogonek", 0xCF: "caron", 0xD0: "emdash", 0xE1: "AE", 0xE3: "ordfeminine", 0xE8: "Lslash",
        0xE9: "Oslash", 0xEA: "OE", 0xEB: "ordmasculine", 0xF1: "ae", 0xF5: "dotlessi", 0xF8: "lslash",
        0xF9: "oslash", 0xFA: "oe", 0xFB: "germandbls",
    }
    for code, name in upper.items():
        standard[code] = name

    for table in (win, mac, standard):
        for name in table:
            if name and name not in names:
                sys.exit(f"encoding glyph {name} not in the AGL")

    def encoding(identifier, table):
        cells = ", ".join(f'"{name}"' for name in table)
        return f"constexpr std::array<std::string_view, 256> {identifier} = {{{cells}}};\n"

    out = [
        "// Generated by scripts/gen_glyph_tables.py — do not edit. Adobe Glyph List and AGLFN: BSD-3-Clause,",
        f"// adobe-type-tools/agl-aglfn at {COMMIT}.",
        "// clang-format off",
        '#include "glyph_tables.hpp"',
        "",
        "#include <array>",
        "",
        "namespace capriole::font::detail {",
        "",
        "namespace {",
        "",
        encoding("kStandard", standard),
        encoding("kWinAnsi", win),
        encoding("kMacRoman", mac),
        f"constexpr std::array<GlyphUnicode, {len(names)}> kGlyphList = {{{{",
    ]
    for name in sorted(names):
        out.append(f'    {{.name = "{name}", .unicode = 0x{names[name]:04X}}},')
    out += [
        "}};",
        "",
        "}  // namespace",
        "",
        "std::span<const GlyphUnicode> glyph_list() { return kGlyphList; }",
        "",
        "std::span<const std::string_view, 256> encoding_table(BaseEncoding encoding) {",
        "  switch (encoding) {",
        "    case BaseEncoding::kWinAnsi:",
        "      return kWinAnsi;",
        "    case BaseEncoding::kMacRoman:",
        "      return kMacRoman;",
        "    case BaseEncoding::kStandard:",
        "      break;",
        "  }",
        "  return kStandard;",
        "}",
        "",
        "}  // namespace capriole::font::detail",
        "",
    ]
    OUT.write_text("\n".join(out))
    print(f"{OUT}: {len(names)} glyph names")


if __name__ == "__main__":
    main()
