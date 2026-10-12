#!/usr/bin/env python3
"""Generates the JBIG2 fixtures of tests/unit/jbig2_test.cpp. Run from this directory.

A small JBIG2 encoder (ITU-T T.88: the MQ coder of Annex E.2, the integer coding of Annex A, Huffman
tables of Annex B) writes embedded streams — segments without a file header, as PDF holds them — for
the parts of the standard the corpus does not exercise. Every fixture is checked with jbig2dec (an
external reference decoder, never linked) before it is written: <name>.jb2 is the stream, <name>.pbm
the page (P4, 1 = black) that jbig2dec and the intended image agree on.

  generic_t1, generic_t2, generic_t3   generic regions, templates 1–3 (arithmetic)
  generic_t0_at_tpgdon                 template 0 with moved AT pixels and TPGDON
  generic_mmr                          generic region, MMR (T.6, encoded with libtiff through Pillow)
  refine_page                          a generic region, then a refinement of the page (template 0, TPGRON),
                                       placed with its combination operator (OR)
  refine_page_t1                       the same with template 1
  unchecked_refine_intermediate_t1_tpgron  an intermediate region refined (template 1, TPGRON) at an offset —
                                       not checked with jbig2dec, which cannot decode it (see below)

Where jbig2dec is no oracle (checked while writing this, 2026-09-28): it does not implement intermediate
generic regions; for TPGRON with refinement template 1 its pseudo-pixel context is the reference pixel
right of the centre, where pdfium and pdf.js use the centre (as template 0 does); and refining the page
it reads the reference from the page's origin, not from under the region (§7.4.7.5, pdfium). Those
cases are covered by the unit tests alone.
  text_huffman                         symbol dictionary and text region, Huffman coded (standard tables)
  text_refine_transposed               arithmetic text region: refinement, transposed, bottom-right corner
  halftone                             pattern dictionary and halftone region (arithmetic)
"""
import io
import random
import subprocess
import tempfile

from PIL import Image, TiffImagePlugin

# ── MQ encoder (Annex E.2) ──────────────────────────────────────────────────────────────────────

QE = [
    (0x5601, 1, 1, 1), (0x3401, 2, 6, 0), (0x1801, 3, 9, 0), (0x0AC1, 4, 12, 0), (0x0521, 5, 29, 0), (0x0221, 38, 33, 0),
    (0x5601, 7, 6, 1), (0x5401, 8, 14, 0), (0x4801, 9, 14, 0), (0x3801, 10, 14, 0), (0x3001, 11, 17, 0), (0x2401, 12, 18, 0),
    (0x1C01, 13, 20, 0), (0x1601, 29, 21, 0), (0x5601, 15, 14, 1), (0x5401, 16, 14, 0), (0x5101, 17, 15, 0), (0x4801, 18, 16, 0),
    (0x3801, 19, 17, 0), (0x3401, 20, 18, 0), (0x3001, 21, 19, 0), (0x2801, 22, 19, 0), (0x2401, 23, 20, 0), (0x2201, 24, 21, 0),
    (0x1C01, 25, 22, 0), (0x1801, 26, 23, 0), (0x1601, 27, 24, 0), (0x1401, 28, 25, 0), (0x1201, 29, 26, 0), (0x1101, 30, 27, 0),
    (0x0AC1, 31, 28, 0), (0x09C1, 32, 29, 0), (0x08A1, 33, 30, 0), (0x0521, 34, 31, 0), (0x0441, 35, 32, 0), (0x02A1, 36, 33, 0),
    (0x0221, 37, 34, 0), (0x0141, 38, 35, 0), (0x0111, 39, 36, 0), (0x0085, 40, 37, 0), (0x0049, 41, 38, 0), (0x0025, 42, 39, 0),
    (0x0015, 43, 40, 0), (0x0009, 44, 41, 0), (0x0005, 45, 42, 0), (0x0001, 45, 43, 0), (0x5601, 46, 46, 0),
]


class MQ:
    def __init__(self):
        self.a, self.c, self.ct = 0x8000, 0, 12
        self.buf = [0]  # a dummy byte before the output, as in E.2.8

    def contexts(self, n):
        return [[0, 0] for _ in range(n)]  # [index, mps]

    def encode(self, cx, d):
        index, mps = cx
        qe, nmps, nlps, switch = QE[index]
        if d == mps:
            self.a -= qe
            if self.a & 0x8000 == 0:
                if self.a < qe:
                    self.a = qe
                else:
                    self.c += qe
                cx[0] = nmps
                self.renorm()
            else:
                self.c += qe
        else:
            self.a -= qe
            if self.a < qe:
                self.c += qe
            else:
                self.a = qe
            if switch:
                cx[1] = 1 - mps
            cx[0] = nlps
            self.renorm()

    def renorm(self):
        while True:
            self.a = (self.a << 1) & 0xFFFFFFFF
            self.c = (self.c << 1) & 0xFFFFFFFF
            self.ct -= 1
            if self.ct == 0:
                self.byte_out()
            if self.a & 0x8000:
                break

    def byte_out(self):
        if self.buf[-1] == 0xFF:
            self.buf.append(self.c >> 20)
            self.c &= 0xFFFFF
            self.ct = 7
        elif self.c < 0x8000000:
            self.buf.append(self.c >> 19)
            self.c &= 0x7FFFF
            self.ct = 8
        else:
            self.buf[-1] += 1
            if self.buf[-1] == 0xFF:
                self.c &= 0x7FFFFFF
                self.buf.append(self.c >> 20)
                self.c &= 0xFFFFF
                self.ct = 7
            else:
                self.buf.append(self.c >> 19)
                self.c &= 0x7FFFF
                self.ct = 8

    def flush(self):
        temp = self.c + self.a
        self.c |= 0xFFFF
        if self.c >= temp:
            self.c -= 0x8000
        self.c = (self.c << self.ct) & 0xFFFFFFFF
        self.byte_out()
        self.c = (self.c << self.ct) & 0xFFFFFFFF
        self.byte_out()
        out = bytes(b & 0xFF for b in self.buf[1:])
        return out + b"\xff\xac"  # the end marker


def encode_int(mq, cxs, value):
    """IAx (A.2); value None is OOB."""
    prev = 1

    def bit(b):
        nonlocal prev
        mq.encode(cxs[prev], b)
        prev = ((prev << 1) | b) if prev < 256 else ((((prev << 1) | b) & 511) | 256)

    def bits(v, n):
        for i in range(n - 1, -1, -1):
            bit((v >> i) & 1)

    if value is None:
        bit(1), bit(0), bits(0, 2)
        return
    bit(1 if value < 0 else 0)
    a = abs(value)
    for prefix, low, n in (([0], 0, 2), ([1, 0], 4, 4), ([1, 1, 0], 20, 6), ([1, 1, 1, 0], 84, 8), ([1, 1, 1, 1, 0], 340, 12), ([1, 1, 1, 1, 1], 4436, 32)):
        high = low + (1 << n)
        if a < high:
            for b in prefix:
                bit(b)
            bits(a - low, n)
            return


def encode_id(mq, cxs, value, length):
    prev = 1
    for i in range(length - 1, -1, -1):
        b = (value >> i) & 1
        mq.encode(cxs[prev], b)
        prev = (prev << 1) | b


# ── Bitmaps and contexts ───────────────────────────────────────────────────────────────────────


class Bitmap:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[0] * w for _ in range(h)]

    def get(self, x, y):
        return self.px[y][x] if 0 <= x < self.w and 0 <= y < self.h else 0

    def place(self, other, x0, y0, op="or"):
        for y in range(other.h):
            for x in range(other.w):
                if 0 <= x0 + x < self.w and 0 <= y0 + y < self.h:
                    if op == "replace":
                        self.px[y0 + y][x0 + x] = other.px[y][x]
                    else:
                        self.px[y0 + y][x0 + x] |= other.px[y][x]


def random_bitmap(w, h, seed, density=0.3, repeat_rows=False):
    rng = random.Random(seed)
    b = Bitmap(w, h)
    for y in range(h):
        if repeat_rows and y > 0 and rng.random() < 0.4:
            b.px[y] = list(b.px[y - 1])
            continue
        for x in range(w):
            b.px[y][x] = 1 if rng.random() < density else 0
    return b


def generic_context(b, x, y, t, at):
    g = lambda dx, dy: b.get(x + dx, y + dy)  # noqa: E731
    if t == 0:
        return (g(-1, 0) | g(-2, 0) << 1 | g(-3, 0) << 2 | g(-4, 0) << 3 | g(at[0], at[1]) << 4 | g(2, -1) << 5 | g(1, -1) << 6 | g(0, -1) << 7
                | g(-1, -1) << 8 | g(-2, -1) << 9 | g(at[2], at[3]) << 10 | g(at[4], at[5]) << 11 | g(1, -2) << 12 | g(0, -2) << 13 | g(-1, -2) << 14
                | g(at[6], at[7]) << 15)
    if t == 1:
        return (g(-1, 0) | g(-2, 0) << 1 | g(-3, 0) << 2 | g(at[0], at[1]) << 3 | g(2, -1) << 4 | g(1, -1) << 5 | g(0, -1) << 6 | g(-1, -1) << 7
                | g(-2, -1) << 8 | g(2, -2) << 9 | g(1, -2) << 10 | g(0, -2) << 11 | g(-1, -2) << 12)
    if t == 2:
        return (g(-1, 0) | g(-2, 0) << 1 | g(at[0], at[1]) << 2 | g(1, -1) << 3 | g(0, -1) << 4 | g(-1, -1) << 5 | g(-2, -1) << 6 | g(1, -2) << 7
                | g(0, -2) << 8 | g(-1, -2) << 9)
    return (g(-1, 0) | g(-2, 0) << 1 | g(-3, 0) << 2 | g(-4, 0) << 3 | g(at[0], at[1]) << 4 | g(1, -1) << 5 | g(0, -1) << 6 | g(-1, -1) << 7
            | g(-2, -1) << 8 | g(-3, -1) << 9)


GENERIC_SIZE = {0: 1 << 16, 1: 1 << 13, 2: 1 << 10, 3: 1 << 10}
SLTP = {0: 0x9B25, 1: 0x0795, 2: 0x00E5, 3: 0x0195}
DEFAULT_AT = {0: [3, -1, -3, -1, 2, -2, -2, -2], 1: [3, -1], 2: [2, -1], 3: [2, -1]}


def encode_generic(mq, cxs, b, t, at, tpgdon=False):
    at = at + [0] * (8 - len(at))
    ltp = 0
    for y in range(b.h):
        if tpgdon:
            same = y > 0 and b.px[y] == b.px[y - 1] or (y == 0 and not any(b.px[0]))
            mq.encode(cxs[SLTP[t]], int(same) ^ ltp)
            ltp = int(same)
            if same:
                continue
        for x in range(b.w):
            mq.encode(cxs[generic_context(b, x, y, t, at)], b.px[y][x])


def refinement_context(b, ref, x, y, t, at, dx, dy):
    r = lambda i, j: ref.get(x - dx + i, y - dy + j)  # noqa: E731
    c = lambda i, j: b.get(x + i, y + j)  # noqa: E731
    if t == 0:
        return (r(1, 1) | r(0, 1) << 1 | r(-1, 1) << 2 | r(1, 0) << 3 | r(0, 0) << 4 | r(-1, 0) << 5 | r(1, -1) << 6 | r(0, -1) << 7
                | r(at[2], at[3]) << 8 | c(-1, 0) << 9 | c(1, -1) << 10 | c(0, -1) << 11 | c(at[0], at[1]) << 12)
    return r(1, 1) | r(0, 1) << 1 | r(1, 0) << 2 | r(0, 0) << 3 | r(-1, 0) << 4 | r(0, -1) << 5 | c(-1, 0) << 6 | c(1, -1) << 7 | c(0, -1) << 8 | c(-1, -1) << 9


def encode_refinement(mq, cxs, b, ref, t, at, dx=0, dy=0, tpgron=False):
    ltp = 0
    for y in range(b.h):
        if tpgron:
            # LTP set when every pixel of the row that can be predicted is predicted right.
            ok = True
            for x in range(b.w):
                v = ref.get(x - dx - 1, y - dy - 1)
                if all(ref.get(x - dx + i, y - dy + j) == v for i in (-1, 0, 1) for j in (-1, 0, 1)) and b.px[y][x] != v:
                    ok = False
            mq.encode(cxs[0x0010 if t == 0 else 0x0008], int(ok) ^ ltp)
            ltp = int(ok)
        for x in range(b.w):
            if ltp:
                v = ref.get(x - dx - 1, y - dy - 1)
                if all(ref.get(x - dx + i, y - dy + j) == v for i in (-1, 0, 1) for j in (-1, 0, 1)):
                    continue
            mq.encode(cxs[refinement_context(b, ref, x, y, t, at, dx, dy)], b.px[y][x])


# ── Huffman (Annex B) ───────────────────────────────────────────────────────────────────────────

STANDARD = {
    1: [(0, 1, 4, "n"), (16, 2, 8, "n"), (272, 3, 16, "n"), (65808, 3, 32, "u")],
    2: [(0, 1, 0, "n"), (1, 2, 0, "n"), (2, 3, 0, "n"), (3, 4, 3, "n"), (11, 5, 6, "n"), (75, 6, 32, "u"), (0, 6, 0, "oob")],
    4: [(1, 1, 0, "n"), (2, 2, 0, "n"), (3, 3, 0, "n"), (4, 4, 3, "n"), (12, 5, 6, "n"), (76, 5, 32, "u")],
    6: [(-2048, 5, 10, "n"), (-1024, 4, 9, "n"), (-512, 4, 8, "n"), (-256, 4, 7, "n"), (-128, 5, 6, "n"), (-64, 5, 5, "n"), (-32, 4, 5, "n"),
        (0, 2, 7, "n"), (128, 3, 7, "n"), (256, 3, 8, "n"), (512, 4, 9, "n"), (1024, 4, 10, "n"), (-2049, 6, 32, "l"), (2048, 6, 32, "u")],
    8: [(-15, 8, 3, "n"), (-7, 9, 1, "n"), (-5, 8, 1, "n"), (-3, 9, 0, "n"), (-2, 7, 0, "n"), (-1, 4, 0, "n"), (0, 2, 1, "n"), (2, 5, 0, "n"),
        (3, 6, 0, "n"), (4, 3, 4, "n"), (20, 6, 1, "n"), (22, 4, 4, "n"), (38, 4, 5, "n"), (70, 5, 6, "n"), (134, 5, 7, "n"), (262, 6, 7, "n"),
        (390, 7, 8, "n"), (646, 6, 10, "n"), (-16, 9, 32, "l"), (1670, 9, 32, "u"), (0, 2, 0, "oob")],
    11: [(1, 1, 0, "n"), (2, 2, 1, "n"), (4, 4, 0, "n"), (5, 4, 1, "n"), (7, 5, 1, "n"), (9, 5, 2, "n"), (13, 6, 2, "n"), (17, 7, 2, "n"), (21, 7, 3, "n"),
         (29, 7, 4, "n"), (45, 7, 5, "n"), (77, 7, 6, "n"), (141, 7, 32, "u")],
}


def canonical(lengths):
    top = max(lengths + [0])
    count = [0] * (top + 1)
    for n in lengths:
        count[n] += 1
    count[0] = 0
    codes = [0] * len(lengths)
    first = 0
    for length in range(1, top + 1):
        first = (first + count[length - 1]) << 1
        code = first
        for i, n in enumerate(lengths):
            if n == length:
                codes[i] = code
                code += 1
    return codes


class Bits:
    def __init__(self):
        self.bits = []

    def put(self, value, n):
        for i in range(n - 1, -1, -1):
            self.bits.append((value >> i) & 1)

    def align(self):
        while len(self.bits) % 8:
            self.bits.append(0)

    def data(self):
        self.align()
        return bytes(int("".join(map(str, self.bits[i : i + 8])), 2) for i in range(0, len(self.bits), 8))


def huffman(bits, table, value):
    lines = STANDARD[table]
    codes = canonical([line[1] for line in lines])
    for (low, prefix, rng, kind), code in zip(lines, codes):
        if value is None and kind == "oob":
            bits.put(code, prefix)
            return
        if value is not None and kind == "n" and low <= value < low + (1 << rng):
            bits.put(code, prefix)
            bits.put(value - low, rng)
            return
    raise ValueError(f"{value} not in table B.{table}")


# ── Segments ────────────────────────────────────────────────────────────────────────────────────


def u32(v):
    return (v & 0xFFFFFFFF).to_bytes(4, "big")


def segment(number, kind, data, referred=()):
    return u32(number) + bytes([kind, len(referred) << 5]) + bytes(referred) + bytes([1]) + u32(len(data)) + data


def page_info(w, h, default=0, override=False):
    return segment(0, 48, u32(w) + u32(h) + u32(0) + u32(0) + bytes([(default << 2) | (0x40 if override else 0)]) + b"\0\0")


def region(w, h, x=0, y=0, op=0):
    return u32(w) + u32(h) + u32(x) + u32(y) + bytes([op])


def s8(values):
    return bytes(v & 0xFF for v in values)


def generic_segment(number, b, t, at=None, tpgdon=False, kind=38, x=0, y=0):
    at = at if at is not None else DEFAULT_AT[t]
    mq = MQ()
    encode_generic(mq, mq.contexts(GENERIC_SIZE[t]), b, t, at, tpgdon)
    flags = (t << 1) | (8 if tpgdon else 0)
    return segment(number, kind, region(b.w, b.h, x, y) + bytes([flags]) + s8(at) + mq.flush())


# ── Fixtures ────────────────────────────────────────────────────────────────────────────────────


def pbm(page):
    row = (page.w + 7) // 8
    data = bytearray(row * page.h)
    for y in range(page.h):
        for x in range(page.w):
            if page.px[y][x]:
                data[y * row + x // 8] |= 0x80 >> (x % 8)
    return f"P4\n{page.w} {page.h}\n".encode() + bytes(data)


def check_and_write(name, stream, page):
    expected = pbm(page)
    with tempfile.TemporaryDirectory() as d:
        open(f"{d}/s.jb2", "wb").write(stream)
        subprocess.run(["jbig2dec", "-e", "-q", "-t", "pbm", "-o", f"{d}/o.pbm", f"{d}/s.jb2"], check=True)
        got = open(f"{d}/o.pbm", "rb").read()
    assert got == expected, f"{name}: jbig2dec disagrees with the intended page"
    open(f"{name}.jb2", "wb").write(stream)
    open(f"{name}.pbm", "wb").write(expected)


def write_unchecked(name, stream, page):
    """For the cases jbig2dec cannot judge (see above): the encoder's intent is the expectation."""
    open(f"{name}.jb2", "wb").write(stream)
    open(f"{name}.pbm", "wb").write(pbm(page))


def generic_fixtures():
    for t in (1, 2, 3):
        b = random_bitmap(37, 23, 10 + t)
        check_and_write(f"generic_t{t}", page_info(37, 23) + generic_segment(1, b, t), b)
    b = random_bitmap(40, 30, 20, repeat_rows=True)
    check_and_write("generic_t0_at_tpgdon", page_info(40, 30) + generic_segment(1, b, 0, at=[-2, 0, 1, -1, -3, -2, 4, -2], tpgdon=True), b)


def mmr_fixture():
    b = random_bitmap(45, 21, 30, density=0.4)
    im = Image.new("1", (b.w, b.h), 1)
    for y in range(b.h):
        for x in range(b.w):
            if not b.px[y][x]:
                im.putpixel((x, y), 0)  # the negative: Pillow's fax runs start on its 0 pixels
    info = TiffImagePlugin.ImageFileDirectory_v2()
    info[278] = b.h
    buf = io.BytesIO()
    im.save(buf, "TIFF", compression="group4", tiffinfo=info)
    tiff = Image.open(io.BytesIO(buf.getvalue()))
    fax = buf.getvalue()[tiff.tag_v2[273][0] : tiff.tag_v2[273][0] + tiff.tag_v2[279][0]]
    check_and_write("generic_mmr", page_info(b.w, b.h) + segment(1, 38, region(b.w, b.h) + bytes([1]) + fax), b)


def refine_fixtures():
    # The page gets a generic region, then an immediate refinement of that area of the page.
    base = random_bitmap(30, 20, 40)
    refined = Bitmap(30, 20)
    rng = random.Random(41)
    for y in range(20):
        for x in range(30):
            refined.px[y][x] = base.px[y][x] ^ (1 if rng.random() < 0.08 else 0)
    at = [-1, -1, -1, -1]
    mq = MQ()
    encode_refinement(mq, mq.contexts(1 << 13), refined, base, 0, at, tpgron=True)
    stream = page_info(30, 20) + generic_segment(1, base, 0) + segment(2, 42, region(30, 20) + bytes([0 | 2]) + s8(at) + mq.flush())
    # The refined region is placed with its combination operator (OR), like any region.
    page = Bitmap(30, 20)
    page.place(base, 0, 0)
    page.place(refined, 0, 0)
    check_and_write("refine_page", stream, page)
    # Template 1 (without TPGRON: see below), region and page the same size.
    mq = MQ()
    encode_refinement(mq, mq.contexts(1 << 10), refined, base, 1, [0, 0, 0, 0])
    page = Bitmap(30, 20)
    page.place(base, 0, 0)
    page.place(refined, 0, 0)
    stream = page_info(30, 20) + generic_segment(1, base, 0) + segment(2, 42, region(30, 20) + bytes([1]) + mq.flush())
    check_and_write("refine_page_t1", stream, page)
    # Unchecked: an intermediate region refined with TPGRON and template 1, at an offset on the page.
    mq = MQ()
    encode_refinement(mq, mq.contexts(1 << 10), refined, base, 1, [0, 0, 0, 0], tpgron=True)
    page = Bitmap(40, 28)
    page.place(refined, 5, 4)
    stream = page_info(40, 28) + generic_segment(1, base, 0, kind=36) + segment(2, 42, region(30, 20, 5, 4) + bytes([1 | 2]) + mq.flush(), referred=(1,))
    write_unchecked("unchecked_refine_intermediate_t1_tpgron", stream, page)


def symbols(seed, count, w, h):
    return [random_bitmap(w, h, seed + i, density=0.45) for i in range(count)]


def text_huffman_fixture():
    syms = symbols(50, 4, 6, 7)
    # Symbol dictionary, Huffman: one height class, the symbols side by side in an uncompressed
    # collective bitmap (BMSIZE 0).
    bits = Bits()
    huffman(bits, 4, 7)  # HCDH (B.4)
    for i in range(len(syms)):
        huffman(bits, 2, 6 if i == 0 else 0)  # DW (B.2): the first width, then no change
    huffman(bits, 2, None)  # end of the height class
    huffman(bits, 1, 0)  # BMSIZE 0: uncompressed
    bits.align()
    total = 6 * len(syms)
    for y in range(7):
        for x in range((total + 7) // 8 * 8):
            bits.bits.append(syms[x // 6].px[y][x % 6] if x < total else 0)
    huffman(bits, 1, 0)  # export flags: none skipped,
    huffman(bits, 1, len(syms))  # then all exported
    dictionary = segment(1, 0, bytes([0, 1]) + u32(len(syms)) + u32(len(syms)) + bits.data())
    # Text region, Huffman: two strips of instances at chosen places.
    placed = [(0, 2, 3), (1, 10, 3), (3, 17, 3), (2, 4, 14), (0, 13, 14)]  # (symbol, x, y) — y the top (top-left corner)
    page = Bitmap(30, 24)
    for sid, x, y in placed:
        page.place(syms[sid], x, y)
    bits = Bits()
    # The symbol ID table: run code lengths (4 bits each), every symbol ID of length 2.
    for value in range(35):
        bits.put(1 if value == 2 else 0, 4)
    for _ in syms:
        bits.put(0, 1)  # run code "2" (its only code, 1 bit)
    bits.align()
    huffman(bits, 11, 3)  # initial STRIPT: T = -3 * 1 ... decoded as -3, so the first DT lands on 3
    strips = {}
    for sid, x, y in placed:
        strips.setdefault(y, []).append((x, sid))
    t, first_s = -3, 0
    for y in sorted(strips):
        huffman(bits, 11, y - t)  # DT
        t = y
        row = sorted(strips[y])
        huffman(bits, 6, row[0][0] - first_s)  # FS
        first_s = row[0][0]
        s = row[0][0]
        for i, (x, sid) in enumerate(row):
            bits.put(sid, 2)  # ID (the table: length 2 codes in order are the IDs)
            s += 6 - 1
            if i + 1 < len(row):
                huffman(bits, 8, row[i + 1][0] - s)  # DS
                s = row[i + 1][0]
            else:
                huffman(bits, 8, None)  # end of strip
    flags = 1 | (1 << 4)  # SBHUFF, top-left corner
    text = segment(2, 38 if False else 6, region(30, 24) + flags.to_bytes(2, "big") + (0).to_bytes(2, "big") + u32(len(placed)) + bits.data(), referred=(1,))
    check_and_write("text_huffman", page_info(30, 24) + dictionary + text, page)


def text_refine_transposed_fixture():
    syms = symbols(60, 3, 5, 8)
    mq = MQ()
    ix = {k: mq.contexts(512) for k in ("dh", "dw", "ex")}
    gb = mq.contexts(GENERIC_SIZE[0])
    encode_int(mq, ix["dh"], 8)
    for i, s in enumerate(syms):
        encode_int(mq, ix["dw"], 5 if i == 0 else 0)
        encode_generic(mq, gb, s, 0, DEFAULT_AT[0])
    encode_int(mq, ix["dw"], None)
    encode_int(mq, ix["ex"], 0)
    encode_int(mq, ix["ex"], len(syms))
    dictionary = segment(1, 0, (0).to_bytes(2, "big") + s8(DEFAULT_AT[0]) + u32(len(syms)) + u32(len(syms)) + mq.flush())
    # Transposed, bottom-right reference corner, one strip; the second instance refined.
    refined = Bitmap(5, 8)
    for y in range(8):
        for x in range(5):
            refined.px[y][x] = syms[1].px[y][x] ^ (1 if (x + y) % 5 == 0 else 0)
    page = Bitmap(24, 30)
    # Transposed: S runs down the page (y), T across (x); the bottom-right corner is at (T, CURS).
    t = 15
    instances = [(0, 4, syms[0]), (1, 14, refined), (2, 25, syms[2])]  # (ID, S of the bottom edge, bitmap)
    for sid, s, b in instances:
        page.place(b, t - (b.w - 1), s - (b.h - 1))
    mq = MQ()
    c = {k: mq.contexts(512) for k in ("dt", "fs", "ds", "ri", "rdw", "rdh", "rdx", "rdy")}
    ids = mq.contexts(1 << 2)
    gr = mq.contexts(1 << 13)
    encode_int(mq, c["dt"], 0)  # STRIPT 0
    encode_int(mq, c["dt"], t)
    # Transposed with a bottom corner: CURS moves by the height before placing.
    curs = instances[0][1] - (8 - 1)
    encode_int(mq, c["fs"], curs)
    for i, (sid, s, b) in enumerate(instances):
        encode_id(mq, ids, sid, 2)
        refine = b is refined
        encode_int(mq, c["ri"], 1 if refine else 0)
        if refine:
            for key in ("rdw", "rdh", "rdx", "rdy"):
                encode_int(mq, c[key], 0)
            encode_refinement(mq, gr, refined, syms[1], 0, [-1, -1, -1, -1])
        curs += 8 - 1  # to the bottom edge, where it is placed
        if i + 1 < len(instances):
            nxt = instances[i + 1][1] - (8 - 1)
            encode_int(mq, c["ds"], nxt - curs)
            curs = nxt
        else:
            encode_int(mq, c["ds"], None)
    flags = 2 | (2 << 4) | 0x40  # SBREFINE, bottom-right corner, transposed; template 0
    text = segment(2, 6, region(24, 30) + flags.to_bytes(2, "big") + s8([-1, -1, -1, -1]) + u32(len(instances)) + mq.flush(), referred=(1,))
    check_and_write("text_refine_transposed", page_info(24, 30) + dictionary + text, page)


def halftone_fixture():
    size, count = 4, 4
    patterns = [Bitmap(size, size) for _ in range(count)]
    for g, p in enumerate(patterns):  # g of the 16 pixels black, in a fixed order
        for k in range(g * 4):
            p.px[k // size][k % size] = 1
    collective = Bitmap(size * count, size)
    for g, p in enumerate(patterns):
        collective.place(p, g * size, 0)
    at = [-size, 0, -3, -1, 2, -2, -2, -2]
    mq = MQ()
    encode_generic(mq, mq.contexts(GENERIC_SIZE[0]), collective, 0, at)
    dictionary = segment(1, 16, bytes([0, size, size]) + u32(count - 1) + mq.flush())
    grid_w, grid_h = 6, 5
    rng = random.Random(70)
    gray = [[rng.randrange(count) for _ in range(grid_w)] for _ in range(grid_h)]
    page = Bitmap(grid_w * size, grid_h * size)
    for m in range(grid_h):
        for n in range(grid_w):
            page.place(patterns[gray[m][n]], n * size, m * size)
    bpp = 2
    mq = MQ()
    cxs = mq.contexts(GENERIC_SIZE[0])
    for j in range(bpp - 1, -1, -1):  # most significant plane first, Gray coded
        plane = Bitmap(grid_w, grid_h)
        for m in range(grid_h):
            for n in range(grid_w):
                v = gray[m][n]
                plane.px[m][n] = ((v ^ (v >> 1)) >> j) & 1
        encode_generic(mq, cxs, plane, 0, [3, -1, -3, -1, 2, -2, -2, -2])
    body = region(page.w, page.h) + bytes([0]) + u32(grid_w) + u32(grid_h) + u32(0) + u32(0) + (size * 256).to_bytes(2, "big") + (0).to_bytes(2, "big")
    halftone = segment(2, 22, body + mq.flush(), referred=(1,))
    check_and_write("halftone", page_info(page.w, page.h) + dictionary + halftone, page)


generic_fixtures()
mmr_fixture()
refine_fixtures()
text_huffman_fixture()
text_refine_transposed_fixture()
halftone_fixture()
print("fixtures written and checked with jbig2dec")
