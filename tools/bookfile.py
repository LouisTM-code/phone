"""The phone book format (.bk): constants, writer helpers and a decoder.

The PC lays the book out for the reader's page area; the phone only draws
the items of a page and reads one page at a time. Little-endian throughout.

Header, 64 bytes:
  0  "FPBK"   4  u16 version   6  u16 page w   8  u16 page h   10 u16 0
  12 u32 npages                  16 u32 pages_off (npages + 1 entries)
  20 u32 toc_off                 24 u32 images_off
  28 u32 cover image or NONE     32 u32 meta_off
Page entry, 12 bytes: u32 data offset, u32 block, u32 char. (block, char) is
the anchor of the page's first text: the paragraph number in the document and
the character in it. Saved positions use anchors, so they survive a new layout.
The extra last entry holds the end of the page data.
TOC: u32 count, then u32 page, u8 level, u8 nbytes, title.
Images: u32 count, then u32 offsets of the blobs. Blob: u16 w, u16 h, u8 kind,
3 pad, u32 row offsets[h + 1] from the blob start, the rows. A row is a run of
tokens: n < 128 - n + 1 literal pixels follow; n >= 128 - the next pixel
repeats n - 126 times. A pixel is u16 RGB565 (IMG_RGB) or u8 alpha 0..15 drawn
in the item's colour (IMG_MASK: formulas, tables drawn as pictures).
Meta: u16 nbytes, "title\\nauthor".

Page data is a list of items, each starting with a u8 type:
  TEXT  s16 y, u8 flags, u8 nruns, runs: s16 x, u8 style, u8 nbytes, UTF-8
  RECT  s16 x, s16 y, u16 w, u16 h, u8 colour
  IMAGE s16 x, s16 y, u32 image, u32 full image or NONE, u8 colour
  SEARCH u16 nbytes, UTF-8: text that is searched but not drawn
  END
Run style: bits 0-3 STYLE_*, bits 4-7 colour.
"""
import struct

MAGIC = b"FPBK"
VERSION = 2
NONE = 0xFFFFFFFF
HEADER = struct.Struct("<4sHHHHIIIIII28x")
PAGE = struct.Struct("<III")

END, TEXT, RECT, IMAGE, SEARCH = range(5)
LINE_HYPHEN = 1  # the line ends with an inserted hyphen
LINE_REPEAT = 2  # a table header repeated on a new page: not searched

STYLE_BOLD, STYLE_ITALIC, STYLE_BIG = 1, 2, 4

# colours, the phone maps them to its theme
(C_TEXT, C_DIM, C_HEAD, C_TITLE, C_CODE, C_CODE_BG, C_QUOTE, C_RULE,
 C_TH_BG) = range(9)

IMG_RGB, IMG_MASK = 1, 2


def rle_row(pixels, wide):
    """Encodes one row: list of ints (u16 for RGB, u8 for masks)."""
    fmt = "<H" if wide else "<B"
    out, i, n = bytearray(), 0, len(pixels)
    lit = []

    def flush():
        while lit:
            chunk = lit[:128]
            del lit[:128]
            out.append(len(chunk) - 1)
            for p in chunk:
                out.extend(struct.pack(fmt, p))

    while i < n:
        j = i
        while j < n and j - i < 129 and pixels[j] == pixels[i]:
            j += 1
        if j - i >= 2:
            flush()
            out.append(j - i + 126)
            out += struct.pack(fmt, pixels[i])
            i = j
        else:
            lit.append(pixels[i])
            i += 1
    flush()
    return bytes(out)


def image_blob(w, h, kind, rows):
    """rows: list of h pixel lists."""
    enc = [rle_row(r, kind == IMG_RGB) for r in rows]
    offs, pos = [], 8 + 4 * (h + 1)
    for e in enc:
        offs.append(pos)
        pos += len(e)
    offs.append(pos)
    return struct.pack("<HHB3x", w, h, kind) + struct.pack(f"<{h + 1}I", *offs) + b"".join(enc)


def text_item(y, flags, runs):
    """runs: (x, style, colour, text)."""
    out = bytearray(struct.pack("<BhBB", TEXT, y, flags, len(runs)))
    for x, style, colour, text in runs:
        b = text.encode("utf-8")
        assert len(b) < 256, text
        out += struct.pack("<hBB", x, style | colour << 4, len(b)) + b
    return bytes(out)


def rect_item(x, y, w, h, colour):
    return struct.pack("<BhhHHB", RECT, x, y, w, h, colour)


def image_item(x, y, image, full, colour):
    return struct.pack("<BhhIIB", IMAGE, x, y, image, full, colour)


def search_item(text):
    b = text.encode("utf-8")[:65535].decode("utf-8", "ignore").encode("utf-8")
    return struct.pack("<BH", SEARCH, len(b)) + b


def write(path, page_w, page_h, pages, toc, images, cover, title, author):
    """pages: list of (anchor_block, anchor_char, data bytes without END).
    toc: (page, level, title). images: blobs."""
    body = bytearray()
    page_offs = []
    pos = HEADER.size
    for _, _, data in pages:
        page_offs.append(pos + len(body))
        body += data + bytes([END])
    end = pos + len(body)
    pages_off = end
    tab = bytearray()
    for (b, c, _), off in zip(pages, page_offs):
        tab += PAGE.pack(off, b, c)
    last = pages[-1] if pages else (0, 0, b"")
    tab += PAGE.pack(end, last[0], last[1])
    toc_off = pages_off + len(tab)
    tocb = bytearray(struct.pack("<I", len(toc)))
    for page, level, t in toc:
        tb = t.encode("utf-8")[:255]
        while True:
            try:
                tb.decode("utf-8")
                break
            except UnicodeDecodeError:
                tb = tb[:-1]
        tocb += struct.pack("<IBB", page, level, len(tb)) + tb
    images_off = toc_off + len(tocb)
    imgtab = bytearray(struct.pack("<I", len(images)))
    blobs = bytearray()
    ipos = images_off + 4 + 4 * len(images)
    for blob in images:
        imgtab += struct.pack("<I", ipos + len(blobs))
        blobs += blob
    meta_off = ipos + len(blobs)
    mb = f"{title}\n{author}".encode("utf-8")
    meta = struct.pack("<H", len(mb)) + mb
    head = HEADER.pack(MAGIC, VERSION, page_w, page_h, 0, len(pages), pages_off,
                       toc_off, images_off, cover, meta_off)
    with open(path, "wb") as f:
        f.write(head + body + tab + tocb + imgtab + blobs + meta)


class Book:
    """Decoder, used by the tests and tools."""

    def __init__(self, path):
        self.data = d = open(path, "rb").read()
        (magic, self.version, self.page_w, self.page_h, _, self.npages,
         pages_off, toc_off, images_off, self.cover, meta_off) = HEADER.unpack_from(d)
        assert magic == MAGIC, path
        self.pages = [PAGE.unpack_from(d, pages_off + i * PAGE.size) for i in range(self.npages + 1)]
        n, = struct.unpack_from("<I", d, toc_off)
        self.toc, p = [], toc_off + 4
        for _ in range(n):
            page, level, nb = struct.unpack_from("<IBB", d, p)
            p += 6
            self.toc.append((page, level, d[p:p + nb].decode("utf-8")))
            p += nb
        n, = struct.unpack_from("<I", d, images_off)
        self.image_offs = struct.unpack_from(f"<{n}I", d, images_off + 4)
        nb, = struct.unpack_from("<H", d, meta_off)
        self.title, self.author = d[meta_off + 2:meta_off + 2 + nb].decode("utf-8").split("\n")

    def items(self, page):
        d, p = self.data, self.pages[page][0]
        out = []
        while True:
            t = d[p]
            p += 1
            if t == END:
                return out
            if t == TEXT:
                y, flags, nr = struct.unpack_from("<hBB", d, p)
                p += 4
                runs = []
                for _ in range(nr):
                    x, st, nb = struct.unpack_from("<hBB", d, p)
                    p += 4
                    runs.append((x, st & 15, st >> 4, d[p:p + nb].decode("utf-8")))
                    p += nb
                out.append(("text", y, flags, runs))
            elif t == RECT:
                out.append(("rect",) + struct.unpack_from("<hhHHB", d, p))
                p += 9
            elif t == IMAGE:
                out.append(("image",) + struct.unpack_from("<hhIIB", d, p))
                p += 13
            elif t == SEARCH:
                nb, = struct.unpack_from("<H", d, p)
                out.append(("search", d[p + 2:p + 2 + nb].decode("utf-8")))
                p += 2 + nb
            else:
                raise ValueError(f"page {page}: bad item {t} at {p - 1}")

    def page_text(self, page):
        """The text of a page as the phone searches it."""
        parts = []
        for it in self.items(page):
            if it[0] == "text" and not it[2] & LINE_REPEAT:
                s = "".join(r[3] for r in it[3])
                if it[2] & LINE_HYPHEN and s.endswith("-"):
                    parts.append(s[:-1])
                else:
                    parts.append(s + " ")
            elif it[0] == "search":
                parts.append(it[1] + " ")
        return "".join(parts)

    def image(self, i):
        """(w, h, kind, rows of pixel lists)."""
        d, off = self.data, self.image_offs[i]
        w, h, kind = struct.unpack_from("<HHB", d, off)
        offs = struct.unpack_from(f"<{h + 1}I", d, off + 8)
        size, fmt = (2, "<H") if kind == IMG_RGB else (1, "<B")
        rows = []
        for y in range(h):
            p, end, row = off + offs[y], off + offs[y + 1], []
            while p < end:
                n = d[p]
                p += 1
                if n < 128:
                    for _ in range(n + 1):
                        row.append(struct.unpack_from(fmt, d, p)[0])
                        p += size
                else:
                    row += [struct.unpack_from(fmt, d, p)[0]] * (n - 126)
                    p += size
            assert len(row) == w, (i, y, len(row), w)
            rows.append(row)
        return w, h, kind, rows
