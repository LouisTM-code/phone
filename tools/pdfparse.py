#!/usr/bin/env python3
"""Reads a PDF that has text (typeset, or a scan with OCR) into blocks, the
way bookparse.py reads EPUB, so mkbook.py lays it out as text: headings by
font size and the PDF bookmarks, paragraphs by indents and gaps, bold and
italic, lists; running heads and page numbers are dropped. Pictures, charts
and tables (rows of three cells and more) are cut out of the page drawn by
pdftoppm: the parts with ink and little text. A PDF without text (a scan) is
left to k2pdfopt (mkbook.py), has_text() tells which.

Usage (to look at the result): pdfparse.py BOOK.pdf
"""
import io
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from collections import Counter
from concurrent.futures import ThreadPoolExecutor

import numpy as np
from PIL import Image

from bookfile import STYLE_BOLD, STYLE_ITALIC

PDF = {
    "min_words": 50,       # words per page on average: less is a scan, for k2pdfopt
    "zoom": 1.5,           # pdftohtml -xml coordinates are points * zoom
    "crop_zoom": 3.0,      # pictures are cut out at points * crop_zoom
    "head_ratio": 1.12,    # a line this much larger than the text is a heading
    "head_levels": 3,      # heading levels by size, larger first
    "size_jitter": 0.2,    # lines this much apart in size can be one paragraph (OCR)
    "margin": 0.08,        # top and bottom part of a page with running heads
    "repeat": 3,           # a margin line seen on so many pages is a running head
    "white": 225,          # a lighter pixel is paper (0-255)
    "ink": 30,             # and a pixel this much darker than the page's usual shade
    "cell": 8,             # pictures are found on a grid of cells this size
    "fig_min": 0.08,       # the least picture side, a part of the page width
    "fig_text": 0.25,      # a part with more of its area in text lines is a text box
    "label_max": 0.25,     # a line inside a picture narrower than this part of it is its label
    "fig_gap": 0.012,      # a white band this wide (a part of the page width) parts two pictures
    "chunk": 5,            # pages drawn by one pdftoppm call (10 MB a page)
    "threads": max(1, (os.cpu_count() or 1) - 2),  # parts read and drawn at once
    "xml_chunk": 25,       # pages read by one pdftohtml call
}

BULLET = re.compile(r"^\s*([•●▪■◦❖►✓✔□]|\d{1,2}[.)])\s+")
SENTENCE_END = re.compile(r"[.!?…:;»\")]\s*$")
BOLD_FONT = re.compile(r"bold|black|heavy|semibold|demi", re.I)
ITALIC_FONT = re.compile(r"italic|oblique|[a-z]It\b|-It$", re.I)
SYMBOL_FONT = re.compile(r"wingding|symbol|dingbat", re.I)


def has_text(path):
    """True when the PDF has a text layer worth reading as text."""
    info = subprocess.run(["pdfinfo", path], capture_output=True, text=True).stdout
    m = re.search(r"^Pages:\s*(\d+)", info, re.M)
    pages = int(m.group(1)) if m else 0
    text = subprocess.run(["pdftotext", "-enc", "UTF-8", path, "-"], capture_output=True).stdout
    return pages > 0 and len(text.decode("utf-8", "replace").split()) >= PDF["min_words"] * pages


class Line:
    """A text piece of pdftohtml: a line, or a cell of a table row."""

    def __init__(self, page, el, fonts):
        self.page = page
        self.top, self.left = int(el.get("top")), int(el.get("left"))
        self.width, self.height = int(el.get("width")), int(el.get("height"))
        family, self.size = fonts.get(el.get("font"), ("", 0))
        self.symbol = bool(SYMBOL_FONT.search(family))
        self.bullet = False  # a list item: a sign of a symbol font stood before it
        base = (STYLE_BOLD if BOLD_FONT.search(family) else 0) | \
            (STYLE_ITALIC if ITALIC_FONT.search(family) else 0)
        self.runs = []
        walk_runs(el, base, self.runs)
        self.text = "".join(t for t, _ in self.runs).strip()
        self.bold = bool(self.runs) and all(s & STYLE_BOLD for t, s in self.runs if t.strip())

    @property
    def right(self):
        return self.left + self.width

    @property
    def bottom(self):
        return self.top + self.height


def walk_runs(el, style, out):
    if el.text:
        out.append((el.text, style))
    for c in el:
        s = style | (STYLE_BOLD if c.tag == "b" else 0) | (STYLE_ITALIC if c.tag == "i" else 0)
        walk_runs(c, s, out)
        if c.tail:
            out.append((c.tail, style))


def load(path, tmp):
    """(pages, outline): pages are (number, width, height, [Line]),
    outline is [(page, level, title)]. pdftohtml reads the book by parts of
    xml_chunk pages on several threads (it is slow on scans)."""
    info = subprocess.run(["pdfinfo", path], capture_output=True, text=True).stdout
    npages = int(re.search(r"^Pages:\s*(\d+)", info, re.M).group(1))
    starts = list(range(1, npages + 1, PDF["xml_chunk"]))
    with ThreadPoolExecutor(PDF["threads"]) as ex:
        roots = list(ex.map(lambda f: xml_part(path, tmp, f, min(f + PDF["xml_chunk"] - 1, npages)), starts))
    pages = []
    for root in roots:
        fonts = {}  # each part numbers its fonts anew
        for pe in root.iter("page"):
            for f in pe.iter("fontspec"):
                fonts[f.get("id")] = (f.get("family", ""), int(f.get("size", "0")))
            n = int(pe.get("number"))
            lines = [ln for ln in (Line(n, t, fonts) for t in pe.iter("text")) if ln.text]
            pages.append((n, int(pe.get("width")), int(pe.get("height")), bullets(lines)))
    outline = []

    def walk(el, level):
        for c in el:
            if c.tag == "outline":
                walk(c, level + 1)
            elif c.tag == "item" and c.get("page"):
                outline.append((int(c.get("page")), level, "".join(c.itertext()).strip()))
    for o in roots[0].findall("outline") if roots else []:
        walk(o, 1)
    return pages, outline


def xml_part(path, tmp, first, last):
    """The pdftohtml -xml tree of pages first..last."""
    out = os.path.join(tmp, f"x{first}")
    subprocess.run(["pdftohtml", "-xml", "-q", "-i", "-hidden", "-nodrm", "-f", str(first), "-l", str(last),
                    path, out], check=True, capture_output=True)
    data = open(out + ".xml", "rb").read().decode("utf-8", "replace")
    os.remove(out + ".xml")
    return ET.fromstring(re.sub(r"[\x00-\x08\x0b\x0c\x0e-\x1f]", "", re.sub(r"<!DOCTYPE[^>]*>", "", data)))


def bullets(lines):
    """A sign of a symbol font (a list marker of Wingdings comes as "9") makes
    the line on its right a list item and is left out."""
    out = []
    for m in lines:
        row = [ln for ln in lines if ln is not m and abs(ln.top - m.top) <= m.height / 2]
        # a sign with text just before it is inside a line ("30 × 63"), not a marker
        first = not any(0 <= m.left - ln.right < 2 * max(m.size, 1) for ln in row)
        if m.symbol and len(m.text) <= 2 and first:
            right = [ln for ln in row if not ln.symbol and 0 <= ln.left - m.right < 3 * max(m.size, 1)]
            if right:
                min(right, key=lambda ln: ln.left).bullet = True
                continue
        out.append(m)
    return out


def key(text):
    """A line's text without numbers and spaces: running heads repeat by it."""
    return re.sub(r"[\W\d_]+", "", text.lower())


def drop_running_heads(pages):
    """Lines in the top and bottom margins that repeat on pages, and page numbers.
    The pages get all their lines as a fifth item: running heads are text, not
    pictures."""
    seen = Counter()
    for n, w, h, lines in pages:
        seen.update({key(ln.text) for ln in lines if in_margin(ln, h)})
    out = []
    for n, w, h, lines in pages:
        keep = [ln for ln in lines if not (in_margin(ln, h) and
                (not key(ln.text) or seen[key(ln.text)] >= PDF["repeat"]))]
        out.append((n, w, h, keep, lines))
    return out


def in_margin(ln, h):
    return ln.top < PDF["margin"] * h or ln.bottom > (1 - PDF["margin"]) * h


def table_rows(lines):
    """Boxes (left, top, right, bottom) of tables: three rows and more in a row
    with three pieces and more side by side."""
    rows = {}
    for ln in lines:
        rows.setdefault(round(ln.top / 4), []).append(ln)
    marked = sorted(k for k, r in rows.items() if len(r) >= 3)
    boxes, run = [], []
    for k in marked + [None]:
        if run and (k is None or k - run[-1] > 12):
            if len(run) >= 3:
                cells = [ln for r in run for ln in rows[r]]
                boxes.append((min(c.left for c in cells), min(c.top for c in cells),
                              max(c.right for c in cells), max(c.bottom for c in cells)))
            run = []
        if k is not None:
            run.append(k)
    return boxes


def find_pictures(img, lines, all_lines, w):
    """Boxes of the parts of a page drawn in img (gray, a pixel a unit) with
    ink outside the text lines, and not mostly text."""
    a = np.asarray(img)
    # paper is the usual shade of the page: a scan may be gray or tinted
    a = a < min(PDF["white"], int(np.median(a)) - PDF["ink"])
    for ln in all_lines:
        a[max(ln.top - 1, 0):ln.bottom + 1, max(ln.left - 1, 0):ln.right + 1] = False
    c = PDF["cell"]
    gh, gw = a.shape[0] // c, a.shape[1] // c
    cells = a[:gh * c, :gw * c].reshape(gh, c, gw, c).sum(axis=(1, 3)) > 2
    # one cell around joins the parts of a chart
    grown = cells.copy()
    grown[1:, :] |= cells[:-1, :]
    grown[:-1, :] |= cells[1:, :]
    grown[:, 1:] |= cells[:, :-1]
    grown[:, :-1] |= cells[:, 1:]
    seen = np.zeros_like(grown)
    boxes = []
    for y0, x0 in zip(*np.nonzero(grown)):
        if seen[y0, x0]:
            continue
        stack, ys, xs = [(y0, x0)], [], []
        seen[y0, x0] = True
        while stack:
            y, x = stack.pop()
            ys.append(y)
            xs.append(x)
            for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                if 0 <= ny < gh and 0 <= nx < gw and grown[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    stack.append((ny, nx))
        for box in split(a, (min(xs) * c, min(ys) * c, (max(xs) + 1) * c, (max(ys) + 1) * c),
                         PDF["fig_gap"] * w):
            if box[2] - box[0] < PDF["fig_min"] * w or box[3] - box[1] < PDF["fig_min"] * w:
                continue
            area = (box[2] - box[0]) * (box[3] - box[1])
            text = sum(ln.width * ln.height for ln in lines if inside(ln, box))
            if text <= PDF["fig_text"] * area:
                boxes.append(box)
    return boxes


def split(ink, box, gap):
    """box cut along white bands at least gap wide across all of it (pictures
    side by side), trimmed to its ink."""
    x0, y0, x1, y1 = (int(v) for v in box)
    part = ink[y0:y1, x0:x1]
    if not part.any():
        return []
    for axis in (0, 1):  # 0: columns, 1: rows
        used = part.any(axis=axis)
        idx = np.nonzero(used)[0]
        lo, hi = idx[0], idx[-1] + 1
        start = None
        for i in range(lo, hi):
            if not used[i] and start is None:
                start = i
            elif used[i] and start is not None:
                if i - start >= gap:
                    a, b = ((x0, y0, x0 + start, y1), (x0 + i, y0, x1, y1)) if axis == 0 else \
                        ((x0, y0, x1, y0 + start), (x0, y0 + i, x1, y1))
                    return split(ink, a, gap) + split(ink, b, gap)
                start = None
    ys, xs = np.nonzero(part)
    return [(x0 + int(xs.min()), y0 + int(ys.min()), x0 + int(xs.max()) + 1, y0 + int(ys.max()) + 1)]


def inside(ln, box):
    cx, cy = ln.left + ln.width / 2, ln.top + ln.height / 2
    return box[0] <= cx <= box[2] and box[1] <= cy <= box[3]


def merge(boxes):
    boxes = list(boxes)
    merged = True
    while merged:
        merged = False
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                a, b = boxes[i], boxes[j]
                if a[0] <= b[2] and b[0] <= a[2] and a[1] <= b[3] and b[1] <= a[3]:
                    boxes[i] = (min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]))
                    del boxes[j]
                    merged = True
                    break
            if merged:
                break
    return boxes


def crop(img, box):
    """PNG bytes of a part of a page drawn at crop_zoom (box in pdftohtml units);
    mkbook.py makes it smaller, so the PNG is packed fast, not small."""
    k = PDF["crop_zoom"] / PDF["zoom"]
    out = io.BytesIO()
    img.crop(tuple(int(v * k) for v in box)).save(out, "PNG", compress_level=1)
    return out.getvalue()


def caption(box, lines, body):
    """The small text right under a picture (or over it) across its width, in
    the order of reading (two pictures side by side: caption by caption)."""
    small = [ln for ln in lines if ln.size < body and box[0] - 10 <= ln.left < box[2]]
    for below in (True, False):
        got, edge, grew = set(), box[3] if below else box[1], True
        while grew:
            grew = False
            for ln in small:
                near = ln.top <= edge + 3 * ln.height if below else ln.bottom >= edge - 3 * ln.height
                if id(ln) not in got and near and \
                        (ln.top >= box[3] - 4 if below else ln.bottom <= box[1] + 4):
                    got.add(id(ln))
                    edge = max(edge, ln.bottom) if below else min(edge, ln.top)
                    grew = True
        if got:
            return [ln for ln in lines if id(ln) in got]
    # beside it: small text in its height, next to its left or right edge
    side = [ln for ln in lines if ln.size < body and box[1] - 4 <= ln.top and ln.bottom <= box[3] + 4
            and (0 <= ln.left - box[2] < 4 * ln.height or 0 <= box[0] - ln.right < 4 * ln.height)]
    if side:
        return side
    return []


def items(path, pages, tmp, body):
    """The lines of the book in reading order with ("img", PNG bytes, caption)
    where a picture is, the lines inside pictures left out. Pages are drawn by
    chunks on several threads (pdftoppm and numpy work outside Python's lock)."""
    chunks = [pages[i:i + PDF["chunk"]] for i in range(0, len(pages), PDF["chunk"])]
    with ThreadPoolExecutor(PDF["threads"]) as ex:
        parts = ex.map(lambda c: chunk_items(path, c, os.path.join(tmp, f"c{c[0][0]}"), body), chunks)
        return [it for part in parts for it in part]


def chunk_items(path, part, tmp, body):
    """items() of a few pages. Each page is drawn once, at crop_zoom: pictures
    are found on it made gray and smaller to the pdftohtml units, and cut out of it."""
    out = []
    k = round(PDF["crop_zoom"] / PDF["zoom"])
    os.makedirs(tmp)
    subprocess.run(["pdftoppm", "-f", str(part[0][0]), "-l", str(part[-1][0]), "-r", str(72 * PDF["crop_zoom"]),
                    path, os.path.join(tmp, "p")], check=True, capture_output=True)
    drawn = {int(re.search(r"(\d+)\.ppm$", f).group(1)): os.path.join(tmp, f)
             for f in os.listdir(tmp) if f.endswith(".ppm")}
    for n, w, h, lines, all_lines in part:
        img = Image.open(drawn[n])
        gray = img.convert("L").reduce(k)
        boxes = merge(find_pictures(gray, lines, all_lines, w) + table_rows(lines))
        # short small lines inside a picture are its labels; a paragraph or a
        # heading over a photo stays text
        label = {id(ln) for ln in lines for b in boxes
                 if inside(ln, b) and ln.size <= body and ln.width < PDF["label_max"] * (b[2] - b[0])}
        free = [ln for ln in lines if id(ln) not in label]
        caps = {b: caption(b, free, body) for b in boxes}
        used = label | {id(ln) for c in caps.values() for ln in c}
        text = [ln for ln in lines if id(ln) not in used]
        # a picture goes before the first line below it that it is above
        for b in sorted(boxes, key=lambda b: (b[1], b[0])):
            j = next((j for j, ln in enumerate(text) if not isinstance(ln, tuple) and ln.top >= b[1]
                      and ln.left < b[2] and ln.right > b[0]), len(text))
            runs = [r for ln in caps[b] for r in ln.runs + [(" ", 0)]]
            text.insert(j, ("img", crop(img, b), runs))
        out += text
        img.close()
        os.remove(drawn[n])
    return out


def heading_levels(lines, body):
    """{font size: level} for the sizes larger than the text, larger first."""
    sizes = sorted({ln.size for ln in lines if ln.size >= body * PDF["head_ratio"]}, reverse=True)
    return {s: min(i + 1, PDF["head_levels"]) for i, s in enumerate(sizes)}


def blocks_of(path, seq, outline, body, levels):
    out = []
    marks = {}
    for page, level, title in outline:
        marks.setdefault(page, []).append((key(title)[:24], min(level, PDF["head_levels"])))
    para, plines, prev = [], [], None
    lst = None  # the list being made: its block

    def flush():
        nonlocal para, plines, lst
        inl = clean(para)
        if inl:
            item = BULLET.match(plines[0].text) if plines else None
            if item or (plines and plines[0].bullet):
                block = {"t": "p", "inl": strip_marker(inl) if item else inl, "verse": False}
                ordered = bool(item) and item.group(1)[0].isdigit()
                if not lst or lst["ordered"] != ordered:
                    lst = {"t": "list", "ordered": ordered,
                           "start": int(item.group(1)[:-1]) if ordered else 1, "items": []}
                    out.append(lst)
                lst["items"].append([block])
            else:
                lst = None
                out.append({"t": "p", "inl": inl, "verse": False})
        para, plines = [], []

    for it in seq:
        if isinstance(it, tuple):
            flush()
            lst = None
            out.append({"t": "img", "data": it[1], "caption": clean(it[2])})
            prev = it
            continue
        ln = it
        level = levels.get(ln.size)
        k = key(ln.text)
        for mk, ml in marks.get(ln.page, []):
            if k and mk and (k.startswith(mk) or mk.startswith(k)):
                level = ml
        if level is None and ln.bold and len(ln.text) < 80 and ln.size <= body + 1 and \
                not SENTENCE_END.search(ln.text) and starts_block(ln, prev, plines, body):
            level = PDF["head_levels"]
        if level is not None:
            flush()
            lst = None
            last = out[-1] if out else None
            # a heading of two lines and more is one heading, of its larger level
            if last and last["t"] == "h" and isinstance(prev, Line) \
                    and prev.page == ln.page and -ln.height / 2 <= ln.top - prev.bottom < 2 * ln.height:
                last["inl"] = clean(last["inl"] + [(" ", 0)] + ln.runs)
                last["level"] = min(last["level"], level)
            else:
                out.append({"t": "h", "level": level, "inl": clean(ln.runs)})
            prev = ln
            continue
        if plines and starts_block(ln, prev, plines, body):
            flush()
        if para:
            text = "".join(t for t, _ in para).rstrip()
            if text.endswith(("-", "\xad")) and ln.text[:1].islower():
                last_t, last_s = para[-1]
                para[-1] = (last_t.rstrip()[:-1], last_s)
            else:
                para.append((" ", 0))
        para += ln.runs
        plines.append(ln)
        prev = ln
    flush()
    for b in out:
        if b["t"] == "h":
            b["inl"] = heading_case(b["inl"])
    return out


def heading_case(inl):
    """A heading drawn in capitals may have its text in random case ("КаК
    выбрать обои"): it becomes "Как выбрать обои". The first letter is a capital."""
    text = "".join(t for t, _ in inl)
    if re.search(r"[^\W\d_][^\W\d_]*[a-zа-яё][A-ZА-ЯЁ]", text):
        inl = [(t.lower(), st) for t, st in inl]
    if inl and inl[0][0][:1].islower():
        inl[0] = (inl[0][0][:1].upper() + inl[0][0][1:], inl[0][1])
    return inl


def starts_block(ln, prev, plines, body):
    """True when ln begins a new paragraph after prev."""
    if not isinstance(prev, Line) or not plines:
        return True
    if ln.bullet or BULLET.match(ln.text):
        return True
    # OCR gives the lines of one paragraph sizes a point or two apart
    if abs(ln.size - prev.size) > max(1, PDF["size_jitter"] * prev.size):
        return True
    em = max(ln.size, 1)
    same_flow = prev.page == ln.page and ln.top > prev.top and abs(ln.left - prev.left) < 6 * em
    if not same_flow:
        # a new page or column: the paragraph goes on unless a sentence ended
        return bool(SENTENCE_END.search(prev.text)) and ln.text[:1].isupper() and \
            ln.left > min(p.left for p in plines) + 0.5 * em
    if ln.top - prev.bottom > 0.7 * em:
        return True
    if 0.8 * em < ln.left - prev.left < 5 * em:
        return True
    widest = max(p.right for p in plines)
    return len(plines) >= 2 and prev.right < widest - 3 * em and bool(SENTENCE_END.search(prev.text))


def clean(inl):
    out = []
    for t, s in inl:
        t = re.sub(r"\s+", " ", t.replace("\xad", ""))
        if out and out[-1][1] == s:
            out[-1] = (out[-1][0] + t, s)
        elif t:
            out.append((t, s))
    while out and not out[0][0].strip():
        out.pop(0)
    while out and not out[-1][0].strip():
        out.pop()
    if out:
        out[0] = (out[0][0].lstrip(), out[0][1])
        out[-1] = (out[-1][0].rstrip(), out[-1][1])
    return [(re.sub(r" {2,}", " ", t), s) for t, s in out if t]


def strip_marker(inl):
    t, s = inl[0]
    t = BULLET.sub("", t, count=1)
    return ([(t, s)] if t else []) + inl[1:]


def cover(path):
    return subprocess.run(["pdftoppm", "-f", "1", "-l", "1", "-scale-to", "600", "-png", path],
                          capture_output=True).stdout


def read(path):
    """(meta, blocks) of bookparse.py. The title is left to mkbook.py (the file
    name): PDF metadata mostly names the program that made the file."""
    with tempfile.TemporaryDirectory() as tmp:
        pages, outline = load(path, tmp)
        pages = drop_running_heads(pages)
        lines = [ln for p in pages for ln in p[3]]
        sizes = Counter()
        for ln in lines:
            sizes[ln.size] += len(ln.text)
        body = sizes.most_common(1)[0][0] if sizes else 0
        seq = items(path, pages, tmp, body)
        blocks = blocks_of(path, seq, outline, body, heading_levels(lines, body))
    return {"title": "", "author": "", "cover": cover(path) or None}, blocks


if __name__ == "__main__":
    meta, blocks = read(sys.argv[1])
    for b in blocks:
        if b["t"] == "img":
            print(f"[img {len(b['data'])} B] {''.join(t for t, _ in b['caption'])}")
        elif b["t"] == "list":
            for item in b["items"]:
                print("  • " + "".join(t for t, _ in item[0]["inl"])[:100])
        else:
            print(f"{b['t']}{b.get('level', '')}: " + "".join(t for t, _ in b["inl"])[:100])
