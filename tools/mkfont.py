#!/usr/bin/env python3
"""Extracts Cyrillic glyphs from a Linux console font (PSF) into font_cyr.h.

usage: mkfont.py OUT.h [FONT.psf.gz] [--ascii]
Default font: CyrSlav-VGA16, the same VGA style as font_ascii.h (glyphs keep the
right column empty, which gives the gap between letters).
--ascii puts U+0020..U+007F before the Cyrillic glyphs (one header for the whole
font, used by fpdoom/term with CyrSlav-VGA8). Glyphs are 8 pixels wide, one byte
per row, any height.
"""
import gzip
import struct
import sys

DEFAULT_FONT = "/usr/share/consolefonts/CyrSlav-VGA16.psf.gz"
CHARS = list(range(0x410, 0x450)) + [0x401, 0x451]


def load_psf(path):
    data = gzip.open(path).read() if path.endswith(".gz") else open(path, "rb").read()
    uni = {}
    if data[:2] == b"\x36\x04":  # PSF1
        mode, height = data[2], data[3]
        count = 512 if mode & 1 else 256
        glyphs = [data[4 + i * height:4 + (i + 1) * height] for i in range(count)]
        if not mode & 2:
            sys.exit(f"{path}: no unicode table")
        pos = 4 + count * height
        for i in range(count):
            while True:
                (u,) = struct.unpack("<H", data[pos:pos + 2])
                pos += 2
                if u == 0xFFFF:
                    break
                if u != 0xFFFE:
                    uni.setdefault(u, glyphs[i])
    else:  # PSF2
        magic, _, hsize, flags, count, size, height, width = struct.unpack("<8I", data[:32])
        if magic != 0x864AB572 or width != 8 or not flags & 1:
            sys.exit(f"{path}: need an 8-pixel wide PSF2 font with a unicode table")
        glyphs = [data[hsize + i * size:hsize + (i + 1) * size] for i in range(count)]
        table = data[hsize + count * size:].split(b"\xff")
        for i in range(count):
            for ch in table[i].split(b"\xfe")[0].decode("utf-8", "ignore"):
                uni.setdefault(ord(ch), glyphs[i])
    return uni, height


def main():
    args = [a for a in sys.argv[1:] if a != "--ascii"]
    ascii_too = len(args) < len(sys.argv) - 1
    if len(args) not in (1, 2):
        sys.exit(__doc__)
    font = args[1] if len(args) == 2 else DEFAULT_FONT
    uni, height = load_psf(font)
    chars = (list(range(0x20, 0x80)) if ascii_too else []) + CHARS
    uni.setdefault(0x7f, bytes(height))
    missing = [hex(c) for c in chars if c not in uni]
    if missing:
        sys.exit(f"{font}: missing {', '.join(missing)}")
    name = font.rsplit("/", 1)[-1]
    with open(args[0], "w") as f:
        first = "U+0020..U+007F, " if ascii_too else ""
        f.write(f"/* 8x{height} glyphs: {first}U+0410..U+044F, U+0401, U+0451 ({name}) */\n")
        for c in chars:
            f.write(",".join(f"0x{b:02x}" for b in uni[c]) + f", /* U+{c:x} */\n")


if __name__ == "__main__":
    main()
