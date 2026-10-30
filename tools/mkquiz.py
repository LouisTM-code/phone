#!/usr/bin/env python3
"""Makes the quiz sets for the phone (fpdoom/apps/quiz.c).

Usage: mkquiz.py SRC_DIR DATA_DIR
Each SRC_DIR/<name>.txt is a set: <name> is at most 8 ASCII letters or digits,
as the phone keeps its statistics in state/<name>.qz. A set is a UTF-8 table,
fields separated by tabs:

  # a comment
  !title  Страны мира                     the name in the list of sets
  !group  Часть света                     a column to choose a part by (optional)
  !image  Флаг                            a column of pictures (optional, any number)
  !icon   Валюта  Знак                    a column of signs drawn beside the
                                          text of another column (optional)
  !mode   Столицы  Страна  Столица        a topic: its name and two columns,
                                          asked both ways
  Страна  Столица  Часть света  Флаг      the first other line: the column names
  Франция Париж    Европа       flags/fr.png   then a line per item

The first column names an item in the statistics, so it must be unique. A
picture is a file relative to SRC_DIR; it is drawn into DATA_DIR/quiz/<name>.bin
and its field becomes the picture's number there. A sign is a white picture on
transparent (tools/mksigns.py), kept as a mask the phone paints in the text
colour. A set without pictures can be written straight to the card's quiz/
with its line in quiz/index.txt.

Writes DATA_DIR/quiz/<name>.txt, <name>.bin and index.txt ("quiz/<name>.txt<TAB>title").
The .bin, little-endian: u32 count, then per picture two entries, the big and
the small one, of u32 offset, u16 width, height, kind (0: RGB565 pixels, 1: an
alpha byte per pixel), u16 0; then the pixels. A picture is fitted into the
box of its kind below (the sizes must suit quiz.c), a picture with a frame.
"""
import os
import re
import struct
import sys

from PIL import Image

PICTURE = ((200, 120), (112, 72))   # big: asked about, small: one of four answers
SIGN = ((120, 56), (40, 26))         # big: over the question, small: in an answer
FRAME = (90, 100, 130)


def rgb565(img):
    px = img.tobytes()
    return b"".join(struct.pack("<H", (px[i] >> 3) << 11 | (px[i + 1] >> 2) << 5 | px[i + 2] >> 3)
                    for i in range(0, len(px), 3))


def scaled(src, box):
    k = min(box[0] / src.width, box[1] / src.height)
    return src.resize((max(1, round(src.width * k)), max(1, round(src.height * k))), Image.LANCZOS)


def picture(path, box):
    """(w, h, kind 0, pixels): the picture in box with a 1-pixel frame."""
    src = Image.open(path).convert("RGBA")
    src = Image.alpha_composite(Image.new("RGBA", src.size, FRAME + (255,)), src).convert("RGB")
    src = scaled(src, (box[0] - 2, box[1] - 2))
    out = Image.new("RGB", (src.width + 2, src.height + 2), FRAME)
    out.paste(src, (1, 1))
    return out.width, out.height, 0, rgb565(out)


def sign(path, box):
    """(w, h, kind 1, alpha bytes): the sign's mask in box."""
    src = scaled(Image.open(path).convert("RGBA").getchannel("A"), box)
    return src.width, src.height, 1, src.tobytes()


def write_pictures(path, pics):
    """pics: (file, is_sign) in their order."""
    head, body = [struct.pack("<I", len(pics))], []
    off = 4 + len(pics) * 2 * 12
    for f, is_sign in pics:
        for box in (SIGN if is_sign else PICTURE):
            w, h, kind, px = (sign if is_sign else picture)(f, box)
            head.append(struct.pack("<I4H", off, w, h, kind, 0))
            body.append(px)
            off += len(px)
    with open(path, "wb") as fp:
        fp.write(b"".join(head + body))


def make_set(src_dir, path, out_dir):
    name = os.path.basename(path)[:-4]
    if not re.fullmatch(r"[a-z0-9_]{1,8}", name):
        sys.exit(f"{path}: the name must be 1-8 letters a-z, digits or _")
    title, images, signs, cols, out, pics, numbers = name, [], [], None, [], [], {}
    for n, line in enumerate(open(path, encoding="utf-8").read().split("\n"), 1):
        line = line.rstrip("\r")
        if not line.strip() or line.startswith("#"):
            continue
        f = line.split("\t")
        if line.startswith("!"):
            if f[0] == "!title":
                title = f[1]
            elif f[0] == "!image":
                images.append(f[1])
            elif f[0] == "!icon":
                signs.append(f[2])
            out.append(line)
            continue
        if cols is None:
            cols = f
            if any(c not in cols for c in images + signs):
                sys.exit(f"{path}: !image or !icon names a missing column")
            idx = [(cols.index(c), c in signs) for c in images + signs]
            out.append(line)
            continue
        if len(f) != len(cols):
            sys.exit(f"{path}:{n}: {len(f)} fields, the header has {len(cols)}")
        for i, is_sign in idx:
            if f[i]:
                key = (os.path.join(src_dir, f[i]), is_sign)
                if key not in numbers:
                    numbers[key] = len(pics)
                    pics.append(key)
                f[i] = str(numbers[key])
        out.append("\t".join(f))
    with open(os.path.join(out_dir, name + ".txt"), "w", encoding="utf-8") as fp:
        fp.write("\n".join(out) + "\n")
    if pics:
        write_pictures(os.path.join(out_dir, name + ".bin"), pics)
    return name, title


def main():
    src_dir, data_dir = sys.argv[1:3]
    out_dir = os.path.join(data_dir, "quiz")
    os.makedirs(out_dir, exist_ok=True)
    index = []
    for f in sorted(os.listdir(src_dir)):
        if f.endswith(".txt"):
            name, title = make_set(src_dir, os.path.join(src_dir, f), out_dir)
            index.append(f"quiz/{name}.txt\t{title}\n")
    with open(os.path.join(out_dir, "index.txt"), "w", encoding="utf-8") as fp:
        fp.write("".join(index))


if __name__ == "__main__":
    main()
