#!/usr/bin/env python3
"""Generates the JPEG 2000 fixtures of tests/unit/jpx_test.cpp (Pillow, which encodes with OpenJPEG).
Run from this directory. Lossless (reversible 5/3 wavelet), so the pixels can be compared exactly;
<name>.raw holds them (8 bits per component, interleaved, rows top to bottom).

  rgb.jp2      48x32 RGB in a JP2 file
  gray.jp2     48x32 grayscale
  rgba.jp2     48x32 RGB with an opacity channel
  rgb.j2k      the RGB image as a bare codestream (no JP2 boxes)
  large.jp2    512x512 RGB gradient, for decoding at a reduced resolution
"""
import random

from PIL import Image


def picture(mode, w, h, seed):
    rng = random.Random(seed)
    im = Image.new(mode, (w, h))
    bands = len(mode)
    im.putdata([tuple(rng.randrange(256) for _ in range(bands)) if bands > 1 else rng.randrange(256) for _ in range(w * h)])
    return im


for name, mode, fmt in [("rgb.jp2", "RGB", "JPEG2000"), ("gray.jp2", "L", "JPEG2000"), ("rgba.jp2", "RGBA", "JPEG2000"), ("rgb.j2k", "RGB", "JPEG2000")]:
    im = picture(mode, 48, 32, 7)
    im.save(name, fmt, irreversible=False, no_jp2=name.endswith(".j2k"))
    open(name.rsplit(".", 1)[0] + ("_j2k" if name.endswith(".j2k") else "") + ".raw", "wb").write(im.tobytes())
gradient = Image.new("RGB", (512, 512))
gradient.putdata([(x // 2, y // 2, (x + y) // 4) for y in range(512) for x in range(512)])
gradient.save("large.jp2", "JPEG2000", irreversible=False, num_resolutions=6)
