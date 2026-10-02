#!/usr/bin/env python3
"""Recompresses the extracted bitmaps (images/**/*.png) as lossless WebP, in parallel, and removes the PNGs.

Usage: webp.py <extracted-dir>
Lossless with `exact` (the colour under fully transparent pixels is kept too): the decoded pixels are identical to the PNG's.
WebP lossless is about a third smaller than PNG for this art; the game decodes it with libwebp (src/engine/image_io.cpp).
"""
import os
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

from PIL import Image


def convert(png):
    png = Path(png)
    im = Image.open(png)
    im.load()
    mode = "RGBA" if im.mode in ("RGBA", "LA", "P") else "RGB"
    out = png.with_suffix(".webp")
    tmp = out.with_suffix(".webp.tmp")
    im.convert(mode).save(tmp, "WEBP", lossless=True, quality=100, method=6, exact=True)
    os.replace(tmp, out)
    png.unlink()
    return 1


def main(root):
    pngs = sorted(str(p) for p in (Path(root) / "images").rglob("*.png"))
    with ProcessPoolExecutor() as pool:
        n = sum(pool.map(convert, pngs, chunksize=16))
    print("recompressed %d bitmaps as lossless WebP" % n)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
