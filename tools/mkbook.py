#!/usr/bin/env python3
"""Builds books for the phone reader (.bk, see bookfile.py).

Usage: mkbook.py DATA_DIR [--cat CATEGORY] BOOK [BOOK...]
BOOK: .md, .epub, .fb2, .fb2.zip, .html, .txt or .pdf. Writes
DATA_DIR/books/<name>.bk and updates DATA_DIR/books/index.txt:
"file<TAB>title<TAB>author<TAB>category<TAB>state id" lines. The state id is
the 8.3 file name of the book's state on the card (the phone can't create
long names).

The text is laid out here for the phone's page area and font, so the phone
only draws prepared lines. PDF pages are reflowed by k2pdfopt into
screen-size pictures (CONF "k2pdfopt"), or else become pictures the phone
pans over.
"""
import hashlib
import io
import os
import re
import struct
import subprocess
import sys
import tempfile
import unicodedata
import zlib

from PIL import Image, ImageChops, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bookfile as bf  # noqa: E402
import bookparse  # noqa: E402
from bookparse import CODE, MATH  # noqa: E402
from mkfont import load_psf  # noqa: E402

TOOLS = os.path.dirname(os.path.abspath(__file__))

# The reader's page area and the book layout. PAGE_W/PAGE_H must match
# fpdoom/apps/reader.c, the phone refuses books built for another size.
CONF = {
    "page_w": 232, "page_h": 292,
    "char_w": 8, "char_h": 16, "line_h": 17,
    "para_indent": 2,        # first line indent of a paragraph, in characters
    "para_gap": 0,           # pixels between paragraphs
    "justify": False,
    "hyphenate": True,
    "chapter_new_page": 1,   # headings up to this level start a new page
    "list_indent": 16, "quote_indent": 12,
    "full_image_max": 960,   # pictures opened full screen are at most this size
    "thumb": (36, 48),       # cover on the shelf
    # PDF: reflowed by k2pdfopt into page-size pictures; "" pans over whole
    # pages pdf_width wide instead (two screens)
    "k2pdfopt": os.path.join(os.path.dirname(TOOLS), "dl/k2pdfopt"),
    "k2pdfopt_args": ["-dpi", "100", "-j", "0", "-c", "-bpc", "8"],
    "pdf_width": 464,
    "font": os.path.join(TOOLS, "fonts/Uni3-Terminus16.psf.gz"),
    "font_bold": os.path.join(TOOLS, "fonts/Uni3-TerminusBold16.psf.gz"),
}

# characters the font lacks
REPLACE = {"ʼ": "'", "‛": "'", "‟": '"', "⁄": "/", "∗": "*", "∕": "/", "⋅": "·",
           "∑": "Σ", "∏": "П", "−": "-", "\t": " ", "\u2009": " ", "\u202f": "\xa0",
           "\u2002": " ", "\u2003": " ", "\u2007": "\xa0", "\u200b": "", "\u00ad": "",
           "\ufeff": "", "\u200e": "", "\u200f": "", "\u2060": ""}
GREEK = dict(zip("αβγδεζηθικλνξορστυφχψω", ["alpha", "beta", "gamma", "delta", "epsilon",
             "zeta", "eta", "theta", "iota", "kappa", "lambda", "nu", "xi", "o", "rho",
             "sigma", "tau", "upsilon", "phi", "chi", "psi", "omega"]))


class Font:
    def __init__(self):
        self.glyphs, _ = load_psf(CONF["font"])
        self.bold, _ = load_psf(CONF["font_bold"])

    def has(self, ch):
        return ord(ch) in self.glyphs

    def draw(self, img, x, y, text, bold=False, scale=1, value=255):
        """Draws onto an 'L' image."""
        g = self.bold if bold else self.glyphs
        px = img.load()
        for ch in text:
            rows = g.get(ord(ch), g[ord("?")])
            for j, bits in enumerate(rows):
                for i in range(8):
                    if bits & (0x80 >> i):
                        for a in range(scale):
                            for b in range(scale):
                                xx, yy = x + i * scale + a, y + j * scale + b
                                if 0 <= xx < img.width and 0 <= yy < img.height:
                                    px[xx, yy] = value
            x += 8 * scale


FONT = None


class Hyphenator:
    """Liang's algorithm with TeX patterns (tools/hyph/, from hyph-utf8)."""

    def __init__(self, lang, left, right):
        self.tree, self.exc = {}, {}
        self.left, self.right = left, right
        for line in open(os.path.join(TOOLS, f"hyph/hyph-{lang}.pat.txt"), encoding="utf-8"):
            for pat in line.split():
                letters = re.sub(r"\d", "", pat)
                points = [0] * (len(letters) + 1)
                i = 0
                for ch in pat:
                    if ch.isdigit():
                        points[i] = int(ch)
                    else:
                        i += 1
                self.tree[letters] = points
        for line in open(os.path.join(TOOLS, f"hyph/hyph-{lang}.hyp.txt"), encoding="utf-8"):
            for w in line.split():
                pos, n = [], 0
                for part in w.split("-")[:-1]:
                    n += len(part)
                    pos.append(n)
                self.exc[w.replace("-", "")] = pos

    def positions(self, word):
        """Indexes where the word may be broken."""
        w = word.lower()
        if w in self.exc:
            return self.exc[w]
        work = "." + w + "."
        points = [0] * (len(work) + 1)
        for i in range(len(work)):
            for j in range(i + 1, min(len(work), i + 12) + 1):
                p = self.tree.get(work[i:j])
                if p:
                    for k, v in enumerate(p):
                        if v > points[i + k]:
                            points[i + k] = v
        return [i for i in range(self.left, len(w) - self.right + 1) if points[i + 1] % 2]


HYPH = {}


def hyphenator(word):
    lang = "ru" if re.search("[а-яё]", word, re.I) else "en-us"
    if lang not in HYPH:
        HYPH[lang] = Hyphenator(lang, 2, 2) if lang == "ru" else Hyphenator(lang, 2, 3)
    return HYPH[lang]


# ------------------------------------------------------------------ images

def rgb565_rows(img):
    img = img.convert("RGB")
    w, h = img.size
    r, g, b = (bytes(c.tobytes()) for c in img.split())
    rows = []
    for y in range(h):
        s = y * w
        rows.append([(r[i] >> 3) << 11 | (g[i] >> 2) << 5 | b[i] >> 3 for i in range(s, s + w)])
    return rows


def mask_rows(img):
    """'L' image, 255 = ink -> alpha 0..15."""
    w, h = img.size
    d = img.tobytes()
    return [[(d[i] * 15 + 127) // 255 for i in range(y * w, y * w + w)] for y in range(h)]


def flatten(img):
    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
        return Image.alpha_composite(bg, img).convert("RGB")
    return img.convert("RGB")


def fit(w, h, mw, mh):
    k = min(1.0, mw / w, mh / h)
    return max(1, round(w * k)), max(1, round(h * k))


class Images:
    def __init__(self):
        self.blobs, self.seen = [], {}

    def add(self, img, kind):
        rows = rgb565_rows(img) if kind == bf.IMG_RGB else mask_rows(img)
        blob = bf.image_blob(img.width, img.height, kind, rows)
        key = hashlib.sha1(blob).digest()
        if key not in self.seen:
            self.seen[key] = len(self.blobs)
            self.blobs.append(blob)
        return self.seen[key]

    def picture(self, img, kind, mw, mh):
        """Adds a picture fitted into mw x mh and, if that shrank it, the
        larger version for the full screen view. Returns (id, w, h, full)."""
        w, h = fit(img.width, img.height, mw, mh)
        small = img if (w, h) == img.size else img.resize((w, h), Image.LANCZOS)
        full = bf.NONE
        if (w, h) != img.size:
            fw, fh = fit(img.width, img.height, CONF["full_image_max"], CONF["full_image_max"])
            full = self.add(img if (fw, fh) == img.size else img.resize((fw, fh), Image.LANCZOS), kind)
        return self.add(small, kind), w, h, full


def render_tex(tex, size_px):
    """A formula as an 'L' ink image, or None if matplotlib can't parse it."""
    import matplotlib
    matplotlib.use("Agg")
    from matplotlib import mathtext
    from matplotlib.font_manager import FontProperties
    b = io.BytesIO()
    try:
        mathtext.math_to_image(f"${tex}$", b, prop=FontProperties(size=size_px * 0.75),
                               dpi=96 * 4 / 3, format="png")
    except Exception:
        return None
    img = Image.open(io.BytesIO(b.getvalue())).convert("L")
    img = ImageChops.invert(img)
    box = img.getbbox()
    return img.crop(box) if box else None


TEX_SYMBOLS = {"\\pi": "π", "\\mu": "μ", "\\Delta": "Δ", "\\Omega": "Ω", "\\infty": "∞",
               "\\leq": "≤", "\\le": "≤", "\\geq": "≥", "\\ge": "≥", "\\neq": "≠", "\\ne": "≠",
               "\\approx": "≈", "\\pm": "±", "\\times": "×", "\\cdot": "·", "\\div": "÷",
               "\\to": "→", "\\rightarrow": "→", "\\leftarrow": "←", "\\in": "∈", "\\cap": "∩",
               "\\cup": "∪", "\\equiv": "≡", "\\sqrt": "√", "\\emptyset": "∅", "\\circ": "°",
               "\\,": " ", "\\;": " ", "\\ ": " ", "\\%": "%", "\\{": "{", "\\}": "}"}


def tex_as_text(tex):
    """Simple formulas (x^2, a \\le b) as plain text, else None."""
    s = tex.strip()
    for k in sorted(TEX_SYMBOLS, key=len, reverse=True):
        s = s.replace(k, TEX_SYMBOLS[k])
    s = re.sub(r"\^\{?([123n])\}?", lambda m: "¹²³ⁿ"["123n".index(m.group(1))], s)
    s = re.sub(r"\\(mathrm|text|mathit)\{([^{}]*)\}", r"\2", s)
    if re.search(r"[\\^_{}]", s) or not all(FONT.has(c) for c in s):
        return None
    return s


# ------------------------------------------------------------------ layout

class Box:
    """A piece of a page that is never split: a line, a picture, a table row."""

    def __init__(self, h, items, anchor, keep_next=False):
        self.h, self.items, self.anchor, self.keep_next = h, items, anchor, keep_next
        self.new_page = False
        self.toc = None
        self.repeat = None  # boxes repeated at the top of a new page (table header)


class Word:
    def __init__(self, pos):
        self.pieces = []  # [text, style, colour] or ["\0img", id, w, h]
        self.pos = pos

    def width(self):
        return sum(p[2] if p[0] == "\0img" else len(p[0]) * CONF["char_w"] * (2 if p[1] & bf.STYLE_BIG else 1)
                   for p in self.pieces)

    def text(self):
        return "".join(p[0] for p in self.pieces if p[0] != "\0img")

    def split(self, n):
        """Splits after n characters of text."""
        a, b = Word(self.pos), Word(self.pos + n)
        for p in self.pieces:
            if p[0] == "\0img" or n <= 0:
                (b if n <= 0 else a).pieces.append(p)
            elif len(p[0]) <= n:
                a.pieces.append(p)
                n -= len(p[0])
            else:
                a.pieces.append([p[0][:n], p[1], p[2]])
                b.pieces.append([p[0][n:], p[1], p[2]])
                n = 0
        return a, b


class Layout:
    def __init__(self, images, report):
        self.images, self.report = images, report
        self.boxes = []
        self.block = 0
        self.lh = CONF["line_h"]

    # ---- text

    def clean(self, text):
        out = []
        for ch in unicodedata.normalize("NFC", text):
            if unicodedata.category(ch) == "Mn":  # stress marks and other combining signs
                continue
            if ch in REPLACE:
                out.append(REPLACE[ch])
            elif FONT.has(ch) or ch == "\xa0":
                out.append(ch)
            elif ch in GREEK:
                out.append(GREEK[ch])
            else:
                self.report["missing"][ch] = self.report["missing"].get(ch, 0) + 1
                out.append("?")
        return "".join(out)

    def words(self, inl, style, colour):
        """Inline content -> words. Spaces end words; NBSP glues them."""
        words, cur, pos = [], None, 0
        for text, st in inl:
            if text == MATH:
                img = self.inline_math(st)
                if isinstance(img, str):
                    text, st = img, bf.STYLE_ITALIC
                else:
                    if cur is None:
                        cur = Word(pos)
                        words.append(cur)
                    cur.pieces.append(["\0img", img[0], img[1], img[2]])
                    pos += 1
                    continue
            s = style | (st & 7)
            col = bf.C_CODE if st & CODE else colour
            text = self.clean(text)
            text = glue(text)
            for ch in text:
                if ch in " \n":
                    cur = None
                else:
                    if cur is None:
                        cur = Word(pos)
                        words.append(cur)
                    p = cur.pieces
                    if p and p[-1][0] != "\0img" and p[-1][1] == s and p[-1][2] == col:
                        p[-1][0] += ch
                    else:
                        p.append([ch, s, col])
                pos += 1
        return words

    def inline_math(self, tex):
        t = tex_as_text(tex)
        if t is not None:
            return t
        img = render_tex(tex, CONF["char_h"])
        if img is None:
            self.report["bad_tex"].append(tex)
            return tex
        if img.height > self.lh * 2:
            img = img.resize(fit(img.width, img.height, CONF["page_w"], self.lh * 2), Image.LANCZOS)
        if img.width > CONF["page_w"]:
            img = img.resize(fit(img.width, img.height, CONF["page_w"], self.lh * 2), Image.LANCZOS)
        return self.images.add(img, bf.IMG_MASK), img.width, img.height

    def hyphen_split(self, word, room):
        """Splits a word to fit room pixels with a hyphen: (head, tail) or None."""
        if not CONF["hyphenate"]:
            return None
        text = word.text()
        if any(p[0] == "\0img" for p in word.pieces):
            return None
        cw = word.width() // max(1, len(text))
        # the word after the last non-breaking space can be hyphenated
        m = re.match(r"^(.*\xa0\W*|\W*)([^\W\d_][^\W\d_'’-]*[^\W\d_])(\W*)$", text)
        if not m or len(m.group(2)) < 4:
            # words with a hyphen already break after it
            k = text.rfind("-", 0, max(0, room // cw))
            if 1 < k < len(text) - 2:
                a, b = word.split(k + 1)
                return a, b, False
            return None
        lead = len(m.group(1))
        best = None
        for i in hyphenator(m.group(2)).positions(m.group(2)):
            if (lead + i + 1) * cw <= room:
                best = i
        if best is None:
            return None
        a, b = word.split(lead + best)
        a.pieces[-1][0] += "-"
        return a, b, True

    def lines(self, inl, x0, width, style=0, colour=bf.C_TEXT, indent=0, align="left",
              keep_next=False):
        """Lays out inline content into line boxes."""
        words = self.words(inl, style, colour)
        if not words:
            return []
        cw = CONF["char_w"] * (2 if style & bf.STYLE_BIG else 1)
        boxes, line, lw, queue = [], [], 0, list(words)
        first = True
        hyph = False

        def emit(last):
            nonlocal line, lw, first
            avail = width - (indent if first else 0)
            boxes.append(self.line_box(line, x0 + (indent if first else 0), avail, lw, cw,
                                       align, last, hyph, keep_next))
            line, lw, first = [], 0, False

        while queue:
            w = queue.pop(0)
            ww = w.width()
            avail = width - (indent if first else 0)
            need = ww + (cw if line else 0)
            if lw + need <= avail:
                line.append(w)
                lw += need
                continue
            room = avail - lw - (cw if line else 0)
            sp = self.hyphen_split(w, room) if room >= 3 * cw else None
            if sp:
                a, b, hy = sp
                line.append(a)
                lw += a.width() + (cw if len(line) > 1 else 0)
                hyph = hy
                emit(False)
                hyph = False
                queue.insert(0, b)
                continue
            if line:
                emit(False)
                queue.insert(0, w)
                continue
            # a word longer than the line: cut it
            n = max(1, avail // cw)
            a, b = w.split(n)
            if not a.pieces:
                a, b = b, Word(w.pos)
            line.append(a)
            lw = a.width()
            emit(False)
            if b.pieces:
                queue.insert(0, b)
        if line:
            emit(True)
        return boxes

    def line_box(self, words, x, avail, lw, cw, align, last, hyph, keep_next):
        h = self.lh * (2 if cw > CONF["char_w"] else 1)
        for w in words:
            for p in w.pieces:
                if p[0] == "\0img":
                    h = max(h, p[3] + 2)
        gaps = len(words) - 1
        extra = avail - lw
        if align == "center":
            x += extra // 2
        runs, items = [], []
        ty = (h - CONF["char_h"] * (2 if cw > CONF["char_w"] else 1)) // 2
        justify = CONF["justify"] and align == "left" and not last and gaps > 0
        for k, w in enumerate(words):
            for p in w.pieces:
                if p[0] == "\0img":
                    items.append(("image", x, (h - p[3]) // 2, p[1], bf.NONE, bf.C_TEXT))
                    x += p[2]
                    continue
                if runs and runs[-1][0] + len(runs[-1][3]) * cw == x and runs[-1][1] == p[1] \
                        and runs[-1][2] == p[2] and not (p[1] & bf.STYLE_BIG and cw != 16):
                    runs[-1][3] += p[0]
                else:
                    runs.append([x, p[1], p[2], p[0]])
                x += len(p[0]) * cw
            if k < gaps:
                if justify:
                    x += cw + extra // gaps + (1 if k < extra % gaps else 0)
                elif runs and runs[-1][0] + len(runs[-1][3]) * cw == x:
                    runs[-1][3] += " "
                    x += cw
                else:
                    x += cw
        for r in runs:
            r[3] = r[3].replace("\xa0", " ")
        items.insert(0, ("text", ty, bf.LINE_HYPHEN if hyph else 0, [tuple(r) for r in runs]))
        return Box(h, items, (self.block, words[0].pos), keep_next)

    # ---- blocks

    def add(self, boxes):
        self.boxes += boxes

    def gap(self, h):
        if h > 0 and self.boxes:
            self.boxes.append(Box(h, [], (self.block, 0)))

    def layout(self, blocks, x0=0, width=None, in_list=False):
        """Returns the boxes for the blocks (and doesn't add them)."""
        width = width or CONF["page_w"]
        saved, self.boxes = self.boxes, []
        for b in blocks:
            self.block_(b, x0, width, in_list)
        out, self.boxes = self.boxes, saved
        return out

    def block_(self, b, x0, width, in_list):
        t = b["t"]
        lh = self.lh
        if t == "p":
            self.block += 1
            if CONF["para_gap"]:
                self.gap(CONF["para_gap"])
            ind = 0 if b.get("verse") or in_list else CONF["para_indent"] * CONF["char_w"]
            self.add(self.lines(b["inl"], x0, width, indent=ind))
        elif t == "h":
            self.block += 1
            lvl = b["level"]
            style, col = {1: (bf.STYLE_BIG, bf.C_TITLE), 2: (bf.STYLE_BOLD, bf.C_HEAD),
                          3: (bf.STYLE_BOLD, bf.C_TEXT)}.get(lvl, (bf.STYLE_BOLD | bf.STYLE_ITALIC, bf.C_TEXT))
            boxes = self.lines(b["inl"], x0, width, style, col, align="center" if lvl == 1 else "left",
                               keep_next=True)
            if not boxes:
                return
            gap = Box(lh // 2 if lvl > 1 else lh, [], (self.block, 0), keep_next=True)
            boxes.insert(0, gap)
            boxes[0].new_page = lvl <= CONF["chapter_new_page"]
            if lvl <= 3:
                boxes[0].toc = (lvl, bookparse.inl_text(b["inl"]))
            self.add(boxes)
            self.add([Box(lh // 4, [], (self.block, 0), keep_next=True)])
        elif t == "gap":
            self.gap(lh)
        elif t == "hr":
            self.block += 1
            self.add([Box(lh, [("rect", x0 + width // 3, lh // 2, width // 3, 1, bf.C_RULE)],
                          (self.block, 0))])
        elif t == "code":
            self.block += 1
            pad, cw = 4, CONF["char_w"]
            cols = max(4, (width - 2 * pad) // cw)
            boxes = [Box(3, [("rect", x0, 0, width, 3, bf.C_CODE_BG)], (self.block, 0), True)]
            pos = 0
            for src in b["text"].split("\n"):
                src = self.clean(src.expandtabs(4)).replace("\xa0", " ")
                parts = [src[i:i + cols] for i in range(0, max(1, len(src)), cols)] or [""]
                for k, part in enumerate(parts):
                    items = [("rect", x0, 0, width, lh, bf.C_CODE_BG)]
                    if part.strip():
                        items.append(("text", (lh - CONF["char_h"]) // 2, 0,
                                      [(x0 + pad, 0, bf.C_CODE, part)]))
                    boxes.append(Box(lh, items, (self.block, pos)))
                    pos += len(part)
                pos += 1
            boxes.append(Box(3, [("rect", x0, 0, width, 3, bf.C_CODE_BG)], (self.block, pos)))
            self.gap(lh // 4)
            self.add(boxes)
            self.gap(lh // 4)
        elif t == "quote":
            qi = CONF["quote_indent"]
            boxes = self.layout(b["blocks"], x0 + qi, width - qi, in_list=True)
            for bx in boxes:
                bx.items.append(("rect", x0 + 3, 0, 2, bx.h, bf.C_QUOTE))
            self.gap(lh // 4)
            self.add(boxes)
            self.gap(lh // 4)
        elif t == "list":
            li = CONF["list_indent"]
            depth = getattr(self, "list_depth", 0)
            self.list_depth = depth + 1
            for k, item in enumerate(b["items"]):
                marker = f"{b['start'] + k}." if b["ordered"] else "•–·"[depth % 3]
                mw = len(marker) * CONF["char_w"]
                ind = max(li, mw + CONF["char_w"])
                boxes = self.layout(item, x0 + ind, width - ind, in_list=True)
                first = next((bx for bx in boxes if any(i[0] == "text" for i in bx.items)), None)
                if first:
                    ty = next(i for i in first.items if i[0] == "text")[1]
                    first.items.append(("text", ty, 0, [(x0 + ind - mw - CONF["char_w"], 0, bf.C_DIM, marker)]))
                self.add(boxes)
            self.list_depth = depth
        elif t == "img":
            self.block += 1
            try:
                img = flatten(Image.open(io.BytesIO(b["data"])))
            except Exception:
                self.report["bad_images"] += 1
                return
            cap = self.lines(b["caption"], x0, width, bf.STYLE_ITALIC, bf.C_DIM, align="center") \
                if b["caption"] else []
            mh = CONF["page_h"] - sum(c.h for c in cap) - 4
            iid, w, h, full = self.images.picture(img, bf.IMG_RGB, width, mh)
            bx = Box(h + 4, [("image", x0 + (width - w) // 2, 2, iid, full, bf.C_TEXT)],
                     (self.block, 0), keep_next=bool(cap))
            self.add([bx] + cap)
        elif t == "math":
            self.block += 1
            img = render_tex(b["tex"], CONF["char_h"] + 2)
            if img is None:
                self.report["bad_tex"].append(b["tex"])
                self.add(self.lines([(b["tex"], CODE)], x0, width, align="center"))
                return
            iid, w, h, full = self.images.picture(img, bf.IMG_MASK, width, CONF["page_h"])
            self.add([Box(h + 8, [("image", x0 + (width - w) // 2, 4, iid, full, bf.C_TEXT)],
                          (self.block, 0))])
        elif t == "table":
            self.block += 1
            self.table(b, x0, width)

    # ---- tables

    def table(self, b, x0, width):
        rows, head = b["rows"], b["head"]
        n = len(rows[0])
        cw, pad = CONF["char_w"], 3
        texts = [[self.clean(bookparse.inl_text(c)) for c in r] for r in rows]
        natural = [max(len(texts[r][c]) for r in range(len(rows))) for c in range(n)]
        longest = [max((max((len(w) for w in texts[r][c].split()), default=0)
                        for r in range(len(rows))), default=0) for c in range(n)]
        minw = [min(max(1, x), 8) for x in longest]  # longer words are hyphenated
        frame = (n + 1) + n * 2 * pad
        avail = (width - frame) // cw
        if n == 2 and len(rows) - head >= 2 and sum(natural) > avail and natural[0] <= 24:
            self.report["def_lists"] += 1
            for r in rows[:head]:
                self.add(self.lines(r[0] + [(" — ", 0)] + r[1], x0, width, colour=bf.C_DIM))
            for r in rows[head:]:
                self.block_({"t": "p", "inl": [(t, s | bf.STYLE_BOLD) for t, s in r[0]] or [("—", 0)],
                             "verse": True}, x0, width, True)
                self.add(self.layout([{"t": "p", "inl": r[1], "verse": True}], x0 + CONF["list_indent"],
                                     width - CONF["list_indent"], in_list=True))
            return
        if sum(minw) > avail:
            self.wide_table(rows, head, texts, natural, x0, width)
            return
        cols = list(minw)
        spare = avail - sum(cols)
        want = [max(0, natural[c] - cols[c]) for c in range(n)]
        tw = sum(want)
        if tw:
            add = [min(want[c], spare * want[c] // tw) for c in range(n)]
            for c in range(n):
                cols[c] += add[c]
            spare -= sum(add)
            for c in sorted(range(n), key=lambda c: -(want[c] - add[c])):
                if spare <= 0:
                    break
                if natural[c] > cols[c]:
                    cols[c] += 1
                    spare -= 1
        tablew = sum(cols) * cw + frame
        xs, x = [], x0
        for c in range(n):
            xs.append(x)
            x += 1 + 2 * pad + cols[c] * cw
        head_boxes, row_boxes = [], []
        for r, row in enumerate(rows):
            is_head = r < head
            cells = [self.lines(row[c], xs[c] + 1 + pad, cols[c] * cw,
                                bf.STYLE_BOLD if is_head else 0) for c in range(n)]
            # each row draws its bottom line, so a row ending a page is closed
            h = max([sum(bx.h for bx in cell) for cell in cells] + [self.lh]) + 3
            items = []
            if is_head:
                items.append(("rect", x0, 0, tablew, h, bf.C_TH_BG))
            if r == 0:
                items.append(("rect", x0, 0, tablew, 1, bf.C_RULE))
            items.append(("rect", x0, h - 1, tablew, 1, bf.C_RULE))
            for c in range(n + 1):
                items.append(("rect", xs[c] if c < n else x0 + tablew - 1, 0, 1, h, bf.C_RULE))
            for c, cell in enumerate(cells):
                y = 1
                for bx in cell:
                    for it in bx.items:
                        items.append((it[0], it[1], it[2] + y) + it[3:] if it[0] != "text"
                                     else ("text", it[1] + y, it[2], it[3]))
                    y += bx.h
            bx = Box(h, items, (self.block, 0), keep_next=is_head)
            (head_boxes if is_head else row_boxes).append(bx)
        for bx in row_boxes:
            bx.repeat = head_boxes
        self.gap(self.lh // 4)
        self.add(head_boxes + row_boxes)
        self.gap(self.lh // 2)

    def wide_table(self, rows, head, texts, natural, x0, width):
        """A table wider than the page becomes a picture: fitted on the page,
        1:1 in the full screen view."""
        self.report["wide_tables"] += 1
        cw, pad, lh = CONF["char_w"], 4, self.lh
        cols = [min(x, 40) for x in natural]
        wrapped = []
        for r in texts:
            cells = []
            for c, t in enumerate(r):
                lines, cur = [], ""
                for w in t.split():
                    while len(w) > cols[c]:
                        if cur:
                            lines.append(cur)
                            cur = ""
                        lines.append(w[:cols[c]])
                        w = w[cols[c]:]
                    if len(cur) + len(w) + (1 if cur else 0) <= cols[c]:
                        cur = f"{cur} {w}" if cur else w
                    else:
                        lines.append(cur)
                        cur = w
                if cur or not lines:
                    lines.append(cur)
                cells.append(lines)
            wrapped.append(cells)
        xs, x = [], 0
        for c in cols:
            xs.append(x)
            x += c * cw + 2 * pad + 1
        tw = x + 1
        hs = [max(len(c) for c in r) * lh + 4 for r in wrapped]
        th = sum(hs) + 1
        img = Image.new("L", (tw, th), 0)
        d = ImageDraw.Draw(img)
        y = 0
        for r, cells in enumerate(wrapped):
            d.line([(0, y), (tw - 1, y)], fill=160)
            for c, lines in enumerate(cells):
                for k, ln in enumerate(lines):
                    FONT.draw(img, xs[c] + 1 + pad, y + 2 + k * lh, ln, bold=r < head)
            y += hs[r]
        d.line([(0, y), (tw - 1, y)], fill=160)
        for c in range(len(cols)):
            d.line([(xs[c], 0), (xs[c], th - 1)], fill=160)
        d.line([(tw - 1, 0), (tw - 1, th - 1)], fill=160)
        w, h = fit(tw, th, width, CONF["page_h"] - 4)
        small = img.resize((w, h), Image.LANCZOS) if (w, h) != img.size else img
        iid = self.images.add(small, bf.IMG_MASK)
        full = self.images.add(img, bf.IMG_MASK) if (w, h) != img.size else bf.NONE
        bx = Box(h + 4, [("image", x0 + (width - w) // 2, 2, iid, full, bf.C_TEXT)], (self.block, 0))
        bx.search = " ".join(" ".join(r) for r in texts)
        self.add([bx])


def glue(text):
    """Non-breaking spaces: after one-letter words and between a number and a
    short unit (10 kg, 5 мм)."""
    text = re.sub(r"(?<![\w-])([а-яёА-ЯЁaAI]) ", "\\1\xa0", text)
    return re.sub(r"(\d) (?=[^\W\d_]{1,3}\b)", "\\1\xa0", text)


# ------------------------------------------------------------- pagination

def paginate(boxes):
    """Boxes -> pages of (anchor, items, toc entries)."""
    ph = CONF["page_h"]
    pages, cur, y = [], [], 0
    tocs = []

    def close():
        nonlocal cur, y
        if cur:
            pages.append(cur)
        cur, y = [], 0

    i = 0
    while i < len(boxes):
        b = boxes[i]
        if b.new_page and cur and any(x[1].items for x in cur):
            close()
        # keep a heading with the line after it; a chain longer than a page
        # (a list of headings) is not kept together
        h, j = b.h, i
        while (boxes[j].keep_next or (j > i and not boxes[j].items)) and j + 1 < len(boxes):
            j += 1
            h += boxes[j].h
            if h > ph:
                h = b.h
                break
        if y + h > ph and cur and any(x[1].items for x in cur):
            close()
            if b.repeat and b not in b.repeat:
                for r in b.repeat:
                    cur.append((y, Repeated(r)))
                    y += r.h
        if not cur and not b.items:  # no empty space at the top of a page
            if b.toc:
                tocs.append((len(pages), b.toc))
            i += 1
            continue
        cur.append((y, b))
        if b.toc:
            tocs.append((len(pages), b.toc))
        y += b.h
        i += 1
    close()
    return pages, tocs


class Repeated:
    """A table header box repeated at the top of a page."""

    def __init__(self, box):
        self.h, self.anchor, self.search = box.h, box.anchor, None
        self.items = [(it[0], it[1], it[2] | bf.LINE_REPEAT, it[3]) if it[0] == "text" else it
                      for it in box.items]


def encode_page(page):
    data = bytearray()
    anchor = None
    for y, b in page:
        if anchor is None and b.items:
            anchor = b.anchor
        for it in b.items:
            if it[0] == "text":
                data += bf.text_item(y + it[1], it[2], it[3])
            elif it[0] == "rect":
                data += bf.rect_item(it[1], y + it[2], it[3], it[4], it[5])
            elif it[0] == "image":
                data += bf.image_item(it[1], y + it[2], it[3], it[4], it[5])
        if getattr(b, "search", None):
            data += bf.search_item(b.search)
    return anchor or (page[0][1].anchor if page else (0, 0)), bytes(data)


# ------------------------------------------------------------------ covers

def cover_images(images, meta):
    """(cover page box or None, thumb id)."""
    tw, th = CONF["thumb"]
    img = None
    if meta.get("cover"):
        try:
            img = flatten(Image.open(io.BytesIO(meta["cover"])))
        except Exception:
            img = None
    if img is None:
        thumb = generated_cover(meta["title"], tw, th)
        return None, images.add(thumb, bf.IMG_RGB)
    thumb = img.copy()
    thumb.thumbnail((tw, th), Image.LANCZOS)
    bg = Image.new("RGB", (tw, th), (40, 40, 50))
    bg.paste(thumb, ((tw - thumb.width) // 2, (th - thumb.height) // 2))
    iid, w, h, full = images.picture(img, bf.IMG_RGB, CONF["page_w"], CONF["page_h"])
    box = Box(CONF["page_h"], [("image", (CONF["page_w"] - w) // 2, (CONF["page_h"] - h) // 2, iid, full,
                                bf.C_TEXT)], (0, 0))
    return box, images.add(bg, bf.IMG_RGB)


def generated_cover(title, w, h):
    hue = zlib.crc32(title.encode()) % 6
    colours = [(70, 90, 150), (130, 70, 90), (60, 120, 100), (140, 110, 50), (90, 70, 140), (60, 110, 140)]
    img = Image.new("RGB", (w, h), colours[hue])
    d = ImageDraw.Draw(img)
    d.rectangle([2, 2, w - 3, h - 3], outline=(230, 230, 230))
    letters = "".join(x[0] for x in re.findall(r"[^\W\d_]+", title)[:2]).upper() or "?"
    mask = Image.new("L", (w, h), 0)
    FONT.draw(mask, (w - 16 * len(letters)) // 2, (h - 32) // 2, letters, bold=True, scale=2)
    img.paste((255, 255, 255), (0, 0), mask)
    return img


# --------------------------------------------------------------------- PDF

def pdf_info(path):
    """(meta, number of pages, text of each page)."""
    info = subprocess.run(["pdfinfo", path], capture_output=True, text=True).stdout
    title = re.search(r"^Title:[ \t]*(.*)$", info, re.M)
    author = re.search(r"^Author:[ \t]*(.*)$", info, re.M)
    npages = int(re.search(r"^Pages:\s*(\d+)", info, re.M).group(1))
    text = subprocess.run(["pdftotext", "-enc", "UTF-8", path, "-"], capture_output=True).stdout
    texts = text.decode("utf-8", "replace").split("\f")
    texts += [""] * (npages - len(texts))
    meta = {"title": title.group(1).strip() if title else "", "author": author.group(1).strip() if author else ""}
    return meta, npages, texts


def pdf_thumb(img, images):
    tw, th = CONF["thumb"]
    thumb = img.copy()
    thumb.thumbnail((tw, th), Image.LANCZOS)
    bg = Image.new("RGB", (tw, th), (40, 40, 50))
    bg.paste(thumb, ((tw - thumb.width) // 2, (th - thumb.height) // 2))
    return images.add(bg, bf.IMG_RGB)


def build_pdf_reflow(path, images, report):
    """k2pdfopt cuts the text lines, formulas and figures out of the pages and
    lays them out again into pages of the reader's size: no panning. A page's
    text for the search goes with the first new page made from it."""
    pw, ph = CONF["page_w"], CONF["page_h"]
    if not os.access(CONF["k2pdfopt"], os.X_OK):
        sys.exit(f"{path}: no k2pdfopt at {CONF['k2pdfopt']} (README.md, books)")
    meta, npages, texts = pdf_info(path)
    lay = Layout(images, report)
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "out.pdf")
        r = subprocess.run([CONF["k2pdfopt"], "-ui-", "-a-", "-x", "-y", "-bp", "-w", str(pw), "-h", str(ph)]
                           + CONF["k2pdfopt_args"] + ["-o", out, path], capture_output=True, text=True)
        # "SOURCE PAGE 3 of 15 (...) ... 5 pages saved": new pages per page
        counts = [int(n) for n in re.findall(r"^SOURCE PAGE.*?(\d+) pages? saved", r.stdout, re.M)]
        if r.returncode or not os.path.exists(out) or len(counts) != npages:
            sys.exit(f"{path}: k2pdfopt failed\n{r.stdout[-500:]}{r.stderr[-500:]}")
        subprocess.run(["pdftoppm", "-r", CONF["k2pdfopt_args"][1], "-png", out, os.path.join(tmp, "p")],
                       check=True, capture_output=True)
        files = sorted(f for f in os.listdir(tmp) if f.endswith(".png"))
        pages = []
        for i, f in enumerate(files):
            img = Image.open(os.path.join(tmp, f)).convert("RGB")
            img = img.resize(fit(img.width, img.height, pw, ph), Image.LANCZOS)
            data = bf.image_item((pw - img.width) // 2, 0, images.add(img, bf.IMG_RGB), bf.NONE, bf.C_TEXT)
            pages.append([i, 0, data])
        first = []
        for i, n in enumerate(counts):
            p = min(sum(counts[:i]), len(pages) - 1)
            first.append(p)
            pages[p][2] += bf.search_item(lay.clean(" ".join(texts[i].split())))
        cover = subprocess.run(["pdftoppm", "-f", "1", "-l", "1", "-scale-to-x", str(pw), "-scale-to-y", "-1",
                                "-png", path], capture_output=True).stdout
    toc = [(first[p], lvl, t) for p, lvl, t in pdf_outline(path) if p < npages]
    thumb = pdf_thumb(Image.open(io.BytesIO(cover)).convert("RGB"), images)
    return meta, [tuple(p) for p in pages], toc, thumb


def build_pdf(path, images, report):
    pw = CONF["pdf_width"]
    meta, npages, texts = pdf_info(path)
    lay = Layout(images, report)
    pages = []
    cover = None
    for i in range(npages):
        out = subprocess.run(["pdftoppm", "-f", str(i + 1), "-l", str(i + 1), "-scale-to-x", str(pw * 2),
                              "-scale-to-y", "-1", "-png", path], capture_output=True).stdout
        img = Image.open(io.BytesIO(out)).convert("RGB")
        box = ImageChops.difference(img, Image.new("RGB", img.size, (255, 255, 255))).getbbox()
        if box:
            m = 8
            img = img.crop((max(0, box[0] - m), max(0, box[1] - m), min(img.width, box[2] + m),
                            min(img.height, box[3] + m)))
        img = img.resize((pw, max(1, round(img.height * pw / img.width))), Image.LANCZOS)
        if i == 0:
            cover = img
        iid = images.add(img, bf.IMG_RGB)
        data = bf.pan_item(iid) + bf.search_item(lay.clean(" ".join(texts[i].split())))
        pages.append((i, 0, data))
    return meta, pages, pdf_outline(path), pdf_thumb(cover, images)


def pdf_outline(path):
    """The PDF bookmarks as (page, level, title), via pdftohtml -xml."""
    try:
        r = subprocess.run(["pdftohtml", "-xml", "-i", "-stdout", "-q", path], capture_output=True, timeout=300)
    except (OSError, subprocess.TimeoutExpired):
        return []
    xml = r.stdout.decode("utf-8", "replace")
    m = re.search(r"<outline>.*</outline>", xml, re.S)
    if not m:
        return []
    out, level = [], 0
    for tok in re.finditer(r"<outline>|</outline>|<item page=\"(\d+)\">(.*?)</item>", m.group(0), re.S):
        if tok.group(0) == "<outline>":
            level += 1
        elif tok.group(0) == "</outline>":
            level -= 1
        elif level <= 3:
            out.append((int(tok.group(1)) - 1, level, re.sub(r"<[^>]+>", "", tok.group(2)).strip()))
    return out


# ------------------------------------------------------------------- build

def build(src, dst):
    global FONT
    if FONT is None:
        FONT = Font()
    images = Images()
    report = {"missing": {}, "bad_tex": [], "bad_images": 0, "wide_tables": 0, "def_lists": 0}
    if src.lower().endswith(".pdf"):
        if CONF["k2pdfopt"]:
            meta, pages, toc, thumb = build_pdf_reflow(src, images, report)
            flags = 0
        else:
            meta, pages, toc, thumb = build_pdf(src, images, report)
            flags = bf.FLAG_PAN
    else:
        meta, blocks = bookparse.read(src)
        lay = Layout(images, report)
        cover_box, thumb = cover_images(images, meta)
        boxes = lay.layout(blocks)
        if cover_box:
            boxes.insert(0, cover_box)
        raw, tocs = paginate(boxes)
        pages = [(a[0], a[1], d) for a, d in (encode_page(p) for p in raw)]
        toc = [(p, lvl, t) for p, (lvl, t) in tocs]
        flags = 0
    if not pages:
        pages = [(0, 0, b"")]
    title = meta.get("title") or os.path.splitext(os.path.basename(src))[0].replace("_", " ")
    title = Layout(images, report).clean(" ".join(title.split()))[:120]
    author = Layout(images, report).clean(" ".join((meta.get("author") or "").split()))[:120]
    bf.write(dst, CONF["page_w"], CONF["page_h"], flags, pages, toc, images.blobs, thumb, title, author)
    return title, author, len(pages), report


TRANSLIT = dict(zip("абвгдеёжзийклмнопрстуфхцчшщъыьэюя",
                    "a b v g d e e zh z i y k l m n o p r s t u f h ts ch sh sch _ y _ e yu ya".split()))
TRANSLIT["ъ"] = TRANSLIT["ь"] = ""


def file_name(base):
    """An ASCII name: the phone's FAT code matches long names in ASCII only."""
    s = "".join(TRANSLIT[c.lower()].capitalize() if c.lower() in TRANSLIT and c.isupper()
                else TRANSLIT.get(c, c) for c in base)
    return re.sub(r"[^A-Za-z0-9_-]+", "_", s)[:60].strip("_") or "book"


def state_id(fn):
    return f"{zlib.crc32(fn.encode()):08X}"


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        sys.exit(__doc__)
    data_dir, args = args[0], args[1:]
    cat = ""
    if args[0] == "--cat":
        cat, args = args[1], args[2:]
    books_dir = os.path.join(data_dir, "books")
    os.makedirs(books_dir, exist_ok=True)
    index_path = os.path.join(books_dir, "index.txt")
    index = {}
    if os.path.exists(index_path):
        for line in open(index_path, encoding="utf-8"):
            f = line.rstrip("\n").split("\t")
            if len(f) == 5:
                index[f[0]] = f[1:]
    for src in args:
        if os.path.isdir(src):  # pictures of .md and .html books
            continue
        base = os.path.basename(src)
        for ext in (".fb2.zip", ".epub", ".fb2", ".md", ".markdown", ".html", ".htm", ".txt", ".pdf"):
            if base.lower().endswith(ext):
                base = base[:-len(ext)]
                break
        name = file_name(base)
        fn = f"books/{name}.bk"
        title, author, npages, rep = build(src, os.path.join(data_dir, fn))
        index[fn] = [title.replace("\t", " "), author.replace("\t", " "), cat, state_id(fn)]
        size = os.path.getsize(os.path.join(data_dir, fn))
        notes = []
        if rep["missing"]:
            notes.append("no glyph for " + "".join(sorted(rep["missing"]))[:40])
        if rep["bad_tex"]:
            notes.append(f"{len(rep['bad_tex'])} formulas not drawn: {rep['bad_tex'][0][:40]}")
        if rep["bad_images"]:
            notes.append(f"{rep['bad_images']} unreadable pictures")
        if rep["wide_tables"]:
            notes.append(f"{rep['wide_tables']} wide tables as pictures")
        if rep["def_lists"]:
            notes.append(f"{rep['def_lists']} two-column tables as lists")
        print(f"{src} -> {fn} ({npages} pages, {size // 1024} KB) \"{title}\"")
        for n in notes:
            print(f"  {n}")
    order = sorted((fn for fn in index if os.path.exists(os.path.join(data_dir, fn))),
                   key=lambda fn: (index[fn][2].lower(), index[fn][0].lower()))
    with open(index_path, "w", encoding="utf-8") as f:
        for fn in order:
            f.write("\t".join([fn] + index[fn]) + "\n")
    write_covers(data_dir, order)


def write_covers(data_dir, order):
    """books/covers.bin: the shelf covers in the order of index.txt, raw
    RGB565 of CONF["thumb"] size each, so the shelf reads one file."""
    tw, th = CONF["thumb"]
    with open(os.path.join(data_dir, "books/covers.bin"), "wb") as f:
        for fn in order:
            book = bf.Book(os.path.join(data_dir, fn))
            px = [0] * (tw * th)
            if book.cover != bf.NONE:
                w, h, kind, rows = book.image(book.cover)
                for y in range(min(h, th)):
                    px[y * tw:y * tw + min(w, tw)] = rows[y][:tw]
            f.write(struct.pack(f"<{tw * th}H", *px))


if __name__ == "__main__":
    main()
