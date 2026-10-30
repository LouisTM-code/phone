#!/usr/bin/env python3
"""Draws the currency signs of a quiz set into pictures.

Usage: mksigns.py SIGNS_TXT OUT_DIR   (sources/quiz/signs/signs.txt sources/quiz/signs)
SIGNS_TXT lines are "currency<TAB>code<TAB>sign" (# starts a comment); each sign
is drawn white on transparent into OUT_DIR/<code>.png, cut to its ink. Run it
when the signs change and keep the pictures with the set: the fonts are this
machine's (fonts-noto), not the phone's.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

NOTO = "/usr/share/fonts/truetype/noto/"
FONT = NOTO + "NotoSans-Bold.ttf"
# signs of scripts NotoSans lacks
SCRIPT_FONTS = {
    "֏": NOTO + "NotoSansArmenian-Bold.ttf",
    "؋": NOTO + "NotoSansArabic-Bold.ttf",
    "﷼": NOTO + "NotoSansArabic-Bold.ttf",
    "৳": NOTO + "NotoSansBengali-Bold.ttf",
    "៛": NOTO + "NotoSansKhmer-Bold.ttf",
    "฿": "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
}
SIZE = 160


def font_for(sign):
    for ch, path in SCRIPT_FONTS.items():
        if ch in sign:
            return path if os.path.exists(path) else path.replace("-Bold", "-Regular")
    return FONT


def draw(sign):
    font = ImageFont.truetype(font_for(sign), SIZE)
    img = Image.new("RGBA", (SIZE * len(sign) + SIZE, SIZE * 2), (255, 255, 255, 0))
    ImageDraw.Draw(img).text((SIZE // 2, SIZE // 2), sign, font=font, fill=(255, 255, 255, 255))
    return img.crop(img.getbbox())


def main():
    src, out = sys.argv[1:3]
    os.makedirs(out, exist_ok=True)
    for line in open(src, encoding="utf-8"):
        if not line.strip() or line.startswith("#"):
            continue
        _, code, sign = line.rstrip("\n").split("\t")
        draw(sign).save(os.path.join(out, code + ".png"))


if __name__ == "__main__":
    main()
