#!/usr/bin/env python3
"""Draws pages of a .bk book to PNG, the way the phone does (for checking mkbook.py).

Usage: bkview.py BOOK.bk OUT_DIR [PAGE...]   (all pages by default)
"""
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bookfile as bf  # noqa: E402
from mkfont import load_psf  # noqa: E402

TOOLS = os.path.dirname(os.path.abspath(__file__))
BG = (24, 26, 34)
COLOURS = {bf.C_TEXT: (230, 230, 220), bf.C_DIM: (150, 160, 180), bf.C_HEAD: (130, 190, 255),
           bf.C_TITLE: (255, 210, 90), bf.C_CODE: (150, 230, 150), bf.C_CODE_BG: (40, 44, 56),
           bf.C_QUOTE: (120, 130, 170), bf.C_RULE: (90, 100, 130), bf.C_TH_BG: (44, 52, 76)}


def main():
    book = bf.Book(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    pages = [int(p) for p in sys.argv[3:]] or range(book.npages)
    font, _ = load_psf(os.path.join(TOOLS, "fonts/Uni3-Terminus16.psf.gz"))
    bold, _ = load_psf(os.path.join(TOOLS, "fonts/Uni3-TerminusBold16.psf.gz"))
    for n in pages:
        img = Image.new("RGB", (book.page_w, book.page_h), BG)
        px = img.load()

        def put(x, y, c):
            if 0 <= x < img.width and 0 <= y < img.height:
                px[x, y] = c

        for it in book.items(n):
            if it[0] == "rect":
                _, x, y, w, h, c = it
                for j in range(h):
                    for i in range(w):
                        put(x + i, y + j, COLOURS[c])
            elif it[0] == "text":
                _, y, flags, runs = it
                for x, st, c, text in runs:
                    s = 2 if st & bf.STYLE_BIG else 1
                    g = bold if st & bf.STYLE_BOLD else font
                    for ch in text:
                        for j, bits in enumerate(g.get(ord(ch), g[ord("?")])):
                            sh = (16 - j) // 5 if st & bf.STYLE_ITALIC else 0
                            for i in range(8):
                                if bits & (0x80 >> i):
                                    for a in range(s):
                                        for b in range(s):
                                            put(x + (i + sh) * s + a, y + j * s + b, COLOURS[c])
                        x += 8 * s
            elif it[0] in ("image", "pan"):
                iid = it[3] if it[0] == "image" else it[1]
                x, y = (it[1], it[2]) if it[0] == "image" else (0, 0)
                w, h, kind, rows = book.image(iid)
                col = COLOURS[it[5]] if it[0] == "image" else (255, 255, 255)
                for j, row in enumerate(rows):
                    for i, p in enumerate(row):
                        if kind == bf.IMG_RGB:
                            put(x + i, y + j, ((p >> 11) << 3, (p >> 5 & 63) << 2, (p & 31) << 3))
                        elif p:
                            q = img.getpixel((x + i, y + j)) if 0 <= x + i < img.width and 0 <= y + j < img.height else BG
                            put(x + i, y + j, tuple((a * p + b * (15 - p)) // 15 for a, b in zip(col, q)))
        img.save(os.path.join(out, f"p{n:04d}.png"))


if __name__ == "__main__":
    main()
