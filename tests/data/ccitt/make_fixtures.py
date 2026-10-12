#!/usr/bin/env python3
"""Generates the CCITT fixtures of tests/unit/ccitt_test.cpp with libtiff (through Pillow) as the
reference encoder. Run from this directory.

For each image: <name>.fax, the fax data (the TIFF strip, as a PDF stream would hold it), and
<name>.pbm, the pixels (P4: 1 = black). The images are text-like: random rectangles and dots, 300 x 97
(width not a multiple of 8, height odd), seeded so that the output is reproducible.
  g4      Group 4 (T.6)                          → /K -1
  g3_1d   Group 3 one-dimensional, EOL per row    → /K 0 /EndOfLine true
  g3_2d   Group 3 two-dimensional, EOL + tag bits → /K 4 /EndOfLine true
  g3_fill Group 3 one-dimensional, EOLs padded to end on a byte boundary → /K 0 /EndOfLine true
  mh      Modified Huffman (TIFF "CCITT RLE"): no EOLs, rows byte-aligned → /K 0 /EncodedByteAlign true
"""
import io
import random

from PIL import Image, TiffImagePlugin

WIDTH, HEIGHT = 300, 97


def image(seed):
    rng = random.Random(seed)
    im = Image.new("1", (WIDTH, HEIGHT), 1)  # 1 = white in mode "1"
    px = im.load()
    for _ in range(60):
        x0, y0 = rng.randrange(WIDTH), rng.randrange(HEIGHT)
        w, h = rng.randrange(1, 40), rng.randrange(1, 12)
        for y in range(y0, min(HEIGHT, y0 + h)):
            for x in range(x0, min(WIDTH, x0 + w)):
                px[x, y] = 0
    for _ in range(400):
        px[rng.randrange(WIDTH), rng.randrange(HEIGHT)] = 0
    return im


def strip(im, compression, t4options=None):
    info = TiffImagePlugin.ImageFileDirectory_v2()
    if t4options is not None:
        info[292] = t4options  # T4Options
    info[278] = HEIGHT  # RowsPerStrip: one strip
    buf = io.BytesIO()
    # Pillow writes 1-bit TIFFs as BlackIsZero, where the fax's "white" runs are the 0 pixels: encode the
    # negative, so that white runs are white.
    negative = im.convert("L").point(lambda v: 255 - v).convert("1")
    negative.save(buf, "TIFF", compression=compression, tiffinfo=info)
    tiff = Image.open(io.BytesIO(buf.getvalue()))
    offset = tiff.tag_v2[273][0]
    count = tiff.tag_v2[279][0]
    assert tiff.tag_v2.get(266, 1) == 1, "FillOrder must be MSB first"
    return buf.getvalue()[offset : offset + count]


def pbm(im):
    # P4 packs 1 for black; mode "1" stores 0 for black.
    inverted = Image.eval(im.convert("L"), lambda v: 255 - v).convert("1")
    return f"P4\n{WIDTH} {HEIGHT}\n".encode() + inverted.tobytes()


for name, seed, compression, t4 in [
    ("g4", 1, "group4", None),
    ("g3_1d", 2, "group3", 0),
    ("g3_2d", 3, "group3", 1),
    ("g3_fill", 4, "group3", 4),
    ("mh", 5, "tiff_ccitt", None),
]:
    im = image(seed)
    open(f"{name}.fax", "wb").write(strip(im, compression, t4))
    open(f"{name}.pbm", "wb").write(pbm(im))
