#!/usr/bin/env python3
"""Generates the JPEG fixtures of tests/unit/dct_test.cpp (Pillow). Run from this directory.

Small images of flat colour blocks, so that decoded samples can be checked within a JPEG tolerance:
  gray.jpg         16x8 gray, left half 0x40, right half 0xC0 (baseline)
  rgb.jpg          16x8 RGB (JFIF, YCbCr), left half red, right half blue
  progressive.jpg  the same image, progressive
  cmyk.jpg         16x8 CMYK with the Adobe marker, left half C=255 (as libjpeg stores it), right half 0
  truncated.jpg    the same colours at 16x64 (several rows of blocks), cut halfway through the
                   entropy-coded data
"""
import os

from PIL import Image

def halves(mode, left, right, size=(16, 8)):
    image = Image.new(mode, size, left)
    image.paste(Image.new(mode, (size[0] // 2, size[1]), right), (size[0] // 2, 0))
    return image

halves("L", 0x40, 0xC0).save("gray.jpg", quality=95)
rgb = halves("RGB", (255, 0, 0), (0, 0, 255))
rgb.save("rgb.jpg", quality=95)
rgb.save("progressive.jpg", quality=95, progressive=True)
halves("CMYK", (255, 0, 0, 0), (0, 0, 0, 0)).save("cmyk.jpg", quality=95)
halves("RGB", (255, 0, 0), (0, 0, 255), size=(16, 64)).save("tall.jpg", quality=95)
data = open("tall.jpg", "rb").read()
scan = data.index(b"\xff\xda")  # start of scan: cut halfway through the entropy-coded data after it
open("truncated.jpg", "wb").write(data[: scan + (len(data) - scan) // 2])
os.remove("tall.jpg")
