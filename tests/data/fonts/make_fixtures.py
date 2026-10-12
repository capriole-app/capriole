#!/usr/bin/env python3
"""Tiny font programs for the font tests, written from scratch (no font of anyone else's).

Every font has the same two glyphs besides .notdef, in glyph space of 1000 units per em:
  A — a square, (100, 0) to (600, 500)
  B — a triangle, (100, 0) (350, 700) (600, 0) (TrueType: its apex is a quadratic control point)
advance 700. Files:
  square.ttf        TrueType, units per em 2048, cmaps (3,1) and (1,0), post names (A, B)
  symbol.ttf        TrueType, units per em 1000, a (3,0) cmap only, at 0xF041 and 0xF042
  square.cff        bare CFF (FontFile3 /Type1C), names A and B, standard encoding
  square_cid.cff    bare CID-keyed CFF (FontFile3 /CIDFontType0C): A is CID 100, B is CID 200
  square.pfa        Type 1 (FontFile), eexec-encrypted, standard encoding

For extending embedded subsets (phase 9c), "CprSquare" (PostScript name), units per em 1000, a (3,1) cmap
and an OS/2 table, with C — a wide bar, (100, 0) to (900, 200) — besides A and B:
  full.ttf          .notdef, A, B, C; fsType 0 (installable)
  subset.ttf        .notdef and A only, as an embedded subset of full.ttf would be
  other.ttf         full.ttf with A moved 10 units right: another version of the font
  restricted.ttf    full.ttf with fsType 2 (restricted license embedding)
  symbolic.ttf      .notdef and A only, a (1,0) cmap at code 1: a symbolic subset as LibreOffice embeds one

Checked by FreeType (the engine) and by rendering them with MuPDF and Poppler in a PDF (scratch).
"""

import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent


# ── TrueType ──────────────────────────────────────────────────────────────────────────────

def glyf_simple(contours):
    """contours: lists of (x, y, on_curve). Flags 0x01 (on curve), coordinates as 16-bit deltas."""
    points = [p for c in contours for p in c]
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    out = struct.pack(">hhhhh", len(contours), min(xs), min(ys), max(xs), max(ys))
    end = -1
    for c in contours:
        end += len(c)
        out += struct.pack(">H", end)
    out += struct.pack(">H", 0)  # no instructions
    out += bytes(1 if p[2] else 0 for p in points)
    x = y = 0
    for p in points:
        out += struct.pack(">h", p[0] - x)
        x = p[0]
    for p in points:
        out += struct.pack(">h", p[1] - y)
        y = p[1]
    return out + b"\0" * (len(out) % 2)


def cmap_format4(mapping):
    """(3,x) subtable, one segment per code (enough for two glyphs) and the final 0xFFFF segment."""
    codes = sorted(mapping)
    ends = codes + [0xFFFF]
    starts = codes + [0xFFFF]
    deltas = [(mapping[c] - c) & 0xFFFF for c in codes] + [1]
    seg = len(ends)
    body = struct.pack(">HHHH", seg * 2, 2 * (1 << (seg.bit_length() - 1)), seg.bit_length() - 1, 2 * seg - 2 * (1 << (seg.bit_length() - 1)))
    body += b"".join(struct.pack(">H", e) for e in ends) + b"\0\0"
    body += b"".join(struct.pack(">H", s) for s in starts)
    body += b"".join(struct.pack(">H", d) for d in deltas)
    body += b"\0\0" * seg  # idRangeOffset
    return struct.pack(">HHH", 4, 6 + len(body), 0) + body


def cmap_format0(mapping):
    table = bytearray(256)
    for c, g in mapping.items():
        table[c] = g
    return struct.pack(">HHH", 0, 262, 0) + bytes(table)


def sfnt(tables):
    tags = sorted(tables)
    n = len(tags)
    entry = 1 << (n.bit_length() - 1)
    out = struct.pack(">IHHHH", 0x00010000, n, entry * 16, entry.bit_length() - 1, n * 16 - entry * 16)
    offset = 12 + 16 * n
    data = b""
    for tag in tags:
        t = tables[tag]
        padded = t + b"\0" * (-len(t) % 4)
        checksum = sum(struct.unpack(f">{len(padded) // 4}I", padded)) & 0xFFFFFFFF
        out += struct.pack(">4sIII", tag.encode(), checksum, offset + len(data), len(t))
        data += padded
    return out + data


def truetype(units, cmaps, post_names, ps_name=None, fs_type=None, glyph_set="AB", shift=0):
    s = units / 1000

    def u(v):
        return round(v * s)

    square = [[(u(100 + shift), 0, True), (u(600 + shift), 0, True), (u(600 + shift), u(500), True), (u(100 + shift), u(500), True)]]
    triangle = [[(u(100), 0, True), (u(350), u(1400), False), (u(600), 0, True)]]  # control at 2× the apex height
    bar = [[(u(100), 0, True), (u(900), 0, True), (u(900), u(200), True), (u(100), u(200), True)]]
    shapes = {"A": square, "B": triangle, "C": bar}
    glyphs = [b""] + [glyf_simple(shapes[g]) for g in glyph_set]
    glyf = b"".join(glyphs)
    loca = b""
    at = 0
    for g in glyphs:
        loca += struct.pack(">H", at // 2)
        at += len(g)
    loca += struct.pack(">H", at // 2)
    advance = u(700)
    n = len(glyphs)
    x_max = u(900) if "C" in glyph_set else u(600 + shift)
    head = struct.pack(">IIIIHHQQhhhhHHhhh", 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0, units, 0, 0, 0, 0, x_max, u(1400), 0, 8, 2, 0, 0)
    hhea = struct.pack(">IhhhHhhhhhhhhhhhH", 0x00010000, u(800), -u(200), 0, advance, 0, 0, x_max, 1, 0, 0, 0, 0, 0, 0, 0, n)
    maxp = struct.pack(">IHHHHHHHHHHHHHH", 0x00010000, n, 4, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)
    hmtx = struct.pack(">Hh", advance, 0) + b"".join(struct.pack(">Hh", advance, u(100)) for _ in glyph_set)
    subtables = b""
    records = b""
    offset = 4 + 8 * len(cmaps)
    for (platform, encoding), table in cmaps:
        records += struct.pack(">HHI", platform, encoding, offset + len(subtables))
        subtables += table
    cmap = struct.pack(">HH", 0, len(cmaps)) + records + subtables
    if post_names:
        post = struct.pack(">IihhIIIII", 0x00020000, 0, -100, 50, 0, 0, 0, 0, 0) + struct.pack(">HHHH", 3, 0, 36, 37)  # .notdef, A, B (Mac order)
    else:
        post = struct.pack(">IihhIIIII", 0x00030000, 0, -100, 50, 0, 0, 0, 0, 0)
    if ps_name is None:
        name = struct.pack(">HHH", 0, 0, 6)
    else:
        text = ps_name.encode("utf-16-be")
        name = struct.pack(">HHH", 0, 1, 18) + struct.pack(">HHHHHH", 3, 1, 0x409, 6, len(text), 0) + text
    tables = {"head": head, "hhea": hhea, "maxp": maxp, "hmtx": hmtx, "loca": loca, "glyf": glyf, "cmap": cmap, "post": post, "name": name}
    if fs_type is not None:
        tables["OS/2"] = struct.pack(">HhHHH", 0, u(600), 400, 5, fs_type) + b"\0" * 68  # version 0: 78 bytes
    return sfnt(tables)


# ── CFF ───────────────────────────────────────────────────────────────────────────────────

def cff_int(v):
    if -107 <= v <= 107:
        return bytes([v + 139])
    if 108 <= v <= 1131:
        v -= 108
        return bytes([(v >> 8) + 247, v & 0xFF])
    if -1131 <= v <= -108:
        v = -v - 108
        return bytes([(v >> 8) + 251, v & 0xFF])
    return b"\x1c" + struct.pack(">h", v)


def dict_offset(v):
    return b"\x1d" + struct.pack(">i", v)  # always 5 bytes: offsets can be laid out before they are known


def index(items):
    if not items:
        return b"\0\0"
    data = b"".join(items)
    offsets = [1]
    for item in items:
        offsets.append(offsets[-1] + len(item))
    size = 4 if offsets[-1] > 0xFFFF else 2
    fmt = ">I" if size == 4 else ">H"
    return struct.pack(">HB", len(items), size) + b"".join(struct.pack(fmt, o) for o in offsets) + data


def charstring(ops):
    out = b""
    for op in ops:
        out += op if isinstance(op, bytes) else cff_int(op)
    return out


RMOVETO, RLINETO, HLINETO, VLINETO, ENDCHAR = b"\x15", b"\x05", b"\x06", b"\x07", b"\x0e"
CFF_GLYPHS = [
    charstring([ENDCHAR]),
    charstring([100, 0, RMOVETO, 500, 500, -500, HLINETO, ENDCHAR]),  # h500 v500 h-500
    charstring([100, 0, RMOVETO, 250, 700, 250, -700, RLINETO, ENDCHAR]),
]


def cff(cid_keyed):
    header = bytes([1, 0, 4, 4])
    name = index([b"Square"])
    strings = [b"Adobe", b"Identity", b"Square-FD"] if cid_keyed else []
    string_index = index(strings)
    gsubrs = index([])
    private = cff_int(700) + b"\x14"  # defaultWidthX
    # charset format 0: SIDs of A (34) and B (35); for a CID-keyed font, the CIDs.
    charset = b"\0" + (struct.pack(">HH", 100, 200) if cid_keyed else struct.pack(">HH", 34, 35))
    charstrings = index(CFF_GLYPHS)
    fdselect = b"\0" + bytes([0, 0, 0])

    def top(offsets):
        d = b""
        if cid_keyed:
            d += cff_int(391) + cff_int(392) + cff_int(0) + b"\x0c\x1e"  # ROS
            d += cff_int(201) + b"\x0c\x22"  # CIDCount
            d += dict_offset(offsets["fdarray"]) + b"\x0c\x24"
            d += dict_offset(offsets["fdselect"]) + b"\x0c\x25"
        d += dict_offset(offsets["charset"]) + b"\x0f"
        d += dict_offset(offsets["charstrings"]) + b"\x11"
        if not cid_keyed:
            d += cff_int(len(private)) + dict_offset(offsets["private"]) + b"\x12"
        return d

    def fd(offsets):
        return cff_int(393) + b"\x0c\x26" + cff_int(len(private)) + dict_offset(offsets["private"]) + b"\x12"  # FontName, Private

    offsets = {k: 0 for k in ("charset", "charstrings", "private", "fdarray", "fdselect")}
    for _ in range(2):  # the second pass has the real offsets (fixed-size operands: same layout)
        top_index = index([top(offsets)])
        at = len(header) + len(name) + len(top_index) + len(string_index) + len(gsubrs)
        offsets["charset"] = at
        at += len(charset)
        if cid_keyed:
            offsets["fdselect"] = at
            at += len(fdselect)
        offsets["charstrings"] = at
        at += len(charstrings)
        if cid_keyed:
            fdarray = index([fd(offsets)])
            offsets["fdarray"] = at
            at += len(fdarray)
        offsets["private"] = at
    body = header + name + index([top(offsets)]) + string_index + gsubrs + charset
    if cid_keyed:
        body += fdselect
    body += charstrings
    if cid_keyed:
        body += index([fd(offsets)])
    return body + private


# ── Type 1 ────────────────────────────────────────────────────────────────────────────────

def t1_int(v):
    if -107 <= v <= 107:
        return bytes([v + 139])
    if 108 <= v <= 1131:
        v -= 108
        return bytes([(v >> 8) + 247, v & 0xFF])
    if -1131 <= v <= -108:
        v = -v - 108
        return bytes([(v >> 8) + 251, v & 0xFF])
    return b"\xff" + struct.pack(">i", v)


def encrypt(data, r, skip=4):
    out = bytearray()
    for c in bytes(skip) + data:
        e = c ^ (r >> 8)
        r = ((e + r) * 52845 + 22719) & 0xFFFF
        out.append(e)
    return bytes(out)


HSBW, CLOSEPATH, T1_RMOVETO, T1_RLINETO, T1_ENDCHAR = b"\x0d", b"\x09", b"\x15", b"\x05", b"\x0e"


def t1_charstring(ops):
    return encrypt(b"".join(op if isinstance(op, bytes) else t1_int(op) for op in ops), 4330)


def type1():
    glyphs = {
        ".notdef": t1_charstring([0, 700, HSBW, T1_ENDCHAR]),
        "A": t1_charstring([0, 700, HSBW, 100, 0, T1_RMOVETO, 500, 0, T1_RLINETO, 0, 500, T1_RLINETO, -500, 0, T1_RLINETO, CLOSEPATH, T1_ENDCHAR]),
        "B": t1_charstring([0, 700, HSBW, 100, 0, T1_RMOVETO, 250, 700, T1_RLINETO, 250, -700, T1_RLINETO, CLOSEPATH, T1_ENDCHAR]),
    }
    private = b"dup /Private 8 dict dup begin\n/RD {string currentfile exch readstring pop} executeonly def\n"
    private += b"/ND {noaccess def} executeonly def\n/NP {noaccess put} executeonly def\n/BlueValues [] ND\n/MinFeature {16 16} ND\n/password 5839 def\n"
    private += b"2 index /CharStrings %d dict dup begin\n" % len(glyphs)
    for name, data in glyphs.items():
        private += b"/%s %d RD " % (name.encode(), len(data)) + data + b" ND\n"
    private += b"end\nend\nreadonly put\nnoaccess put\ndup /FontName get exch definefont pop\nmark currentfile closefile\n"
    clear = (b"%!PS-AdobeFont-1.0: Square 001\n12 dict begin\n/FontName /Square def\n/PaintType 0 def\n/FontType 1 def\n"
             b"/FontMatrix [0.001 0 0 0.001 0 0] readonly def\n/FontBBox {0 0 700 700} readonly def\n/Encoding StandardEncoding def\n"
             b"currentdict end\ncurrentfile eexec\n")
    trailer = b"\n" + (b"0" * 64 + b"\n") * 8 + b"cleartomark\n"
    return clear + encrypt(private, 55665) + trailer


def main():
    (HERE / "square.ttf").write_bytes(truetype(2048, [((1, 0), cmap_format0({0x41: 1, 0x42: 2})), ((3, 1), cmap_format4({0x41: 1, 0x42: 2}))], True))
    (HERE / "symbol.ttf").write_bytes(truetype(1000, [((3, 0), cmap_format4({0xF041: 1, 0xF042: 2}))], False))
    (HERE / "square.cff").write_bytes(cff(False))
    (HERE / "square_cid.cff").write_bytes(cff(True))
    (HERE / "square.pfa").write_bytes(type1())
    full = ((3, 1), cmap_format4({0x41: 1, 0x42: 2, 0x43: 3}))
    (HERE / "full.ttf").write_bytes(truetype(1000, [full], False, "CprSquare", 0, "ABC"))
    (HERE / "subset.ttf").write_bytes(truetype(1000, [((3, 1), cmap_format4({0x41: 1}))], False, "CprSquare", 0, "A"))
    (HERE / "other.ttf").write_bytes(truetype(1000, [full], False, "CprSquare", 0, "ABC", shift=10))
    (HERE / "restricted.ttf").write_bytes(truetype(1000, [full], False, "CprSquare", 2, "ABC"))
    (HERE / "symbolic.ttf").write_bytes(truetype(1000, [((1, 0), cmap_format0({1: 1}))], False, "CprSquare", 0, "A"))


if __name__ == "__main__":
    main()
