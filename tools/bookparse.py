"""Reads books into one document tree for mkbook.py.

A document is (meta, blocks). meta: title, author, cover (image bytes or None).
Blocks are dicts by "t":
  h      level 1..4, inl            p      inl, verse (no indent)
  list   ordered, start, items: [[block]]
  quote  blocks                      code   text
  hr                                 gap    (an empty line)
  table  rows: [[inl]], head: number of header rows
  img    data: bytes, caption: inl   math   tex (a display formula)
Inline content (inl) is a list of (text, style) and ("\\0math", tex) for an
inline formula; style is a set of bookfile.STYLE_* bits.
"""
import base64
import os
import posixpath
import re
import zipfile
import xml.etree.ElementTree as ET
from html.parser import HTMLParser

from bookfile import STYLE_BOLD, STYLE_ITALIC

MATH = "\0math"
CODE = 8  # inline code: a style bit used only here, drawn in C_CODE


class El:
    def __init__(self, tag, attrs=None):
        self.tag, self.attrs, self.children = tag, attrs or {}, []

    def text(self):
        return "".join(c if isinstance(c, str) else c.text() for c in self.children)


class DomBuilder(HTMLParser):
    VOID = {"br", "img", "hr", "meta", "link", "input", "col", "image", "empty-line"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = El("root")
        self.stack = [self.root]

    def handle_starttag(self, tag, attrs):
        el = El(tag.lower(), {k.lower(): v or "" for k, v in attrs})
        self.stack[-1].children.append(el)
        if el.tag not in self.VOID:
            self.stack.append(el)

    def handle_startendtag(self, tag, attrs):
        self.stack[-1].children.append(El(tag.lower(), {k.lower(): v or "" for k, v in attrs}))

    def handle_endtag(self, tag):
        tag = tag.lower()
        for i in range(len(self.stack) - 1, 0, -1):
            if self.stack[i].tag == tag:
                del self.stack[i:]
                return

    def handle_data(self, data):
        self.stack[-1].children.append(data)


def parse_html(s):
    b = DomBuilder()
    b.feed(s)
    b.close()
    return b.root


HEADINGS = {"h1": 1, "h2": 2, "h3": 3, "h4": 4, "h5": 4, "h6": 4}
BLOCK_TAGS = {"p", "div", "section", "article", "body", "blockquote", "ul", "ol", "li", "pre",
              "table", "hr", "figure", "figcaption", "dl", "dt", "dd", "header", "footer",
              "main", "nav", "aside", "center", "address", "img", "svg", "math", "image"} | set(HEADINGS)
SKIP_TAGS = {"script", "style", "head", "title", "noscript", "template", "button", "form"}
INLINE_STYLE = {"b": STYLE_BOLD, "strong": STYLE_BOLD, "i": STYLE_ITALIC, "em": STYLE_ITALIC,
                "cite": STYLE_ITALIC, "var": STYLE_ITALIC, "dfn": STYLE_ITALIC,
                "code": CODE, "tt": CODE, "kbd": CODE, "samp": CODE}
SUP = str.maketrans("0123456789+-=()n", "⁰¹²³⁴⁵⁶⁷⁸⁹⁺⁻⁼⁽⁾ⁿ")


def clean_inl(inl):
    """Collapses white space, merges equal styles, trims the ends."""
    out = []
    for text, style in inl:
        if text != MATH:
            text = re.sub(r"[ \t\r\n\f]+", " ", text)
            if out and out[-1][0] != MATH and out[-1][1] == style:
                out[-1] = (out[-1][0] + text, style)
                continue
        out.append((text, style))
    while out and out[0][0] != MATH and not out[0][0].strip():
        out.pop(0)
    while out and out[-1][0] != MATH and not out[-1][0].strip():
        out.pop()
    if out and out[0][0] != MATH:
        out[0] = (out[0][0].lstrip(), out[0][1])
    if out and out[-1][0] != MATH:
        out[-1] = (out[-1][0].rstrip(), out[-1][1])
    res = []
    for t, s in out:
        if t != MATH and res and res[-1][0] != MATH and res[-1][0].endswith(" ") and t.startswith(" "):
            t = t[1:]
        if t:
            res.append((t, s))
    return res


def inl_text(inl):
    return "".join(t if t != MATH else s for t, s in inl)


class HtmlDoc:
    """Converts an HTML/XHTML element tree (EPUB chapters, FB2 mapped to HTML
    tags) into blocks. get_image(src) returns image bytes or None."""

    def __init__(self, get_image=None):
        self.get_image = get_image or (lambda src: None)
        self.notes = []

    def blocks(self, el):
        out, inl = [], []
        self._walk_children(el, out, inl, 0)
        self._flush(out, inl)
        return out

    def _flush(self, out, inl, verse=False):
        c = clean_inl(inl)
        if c:
            out.append({"t": "p", "inl": c, "verse": verse})
        inl.clear()

    def _walk_children(self, el, out, inl, style):
        for c in el.children:
            if isinstance(c, str):
                inl.append((c, style))
            else:
                self._walk(c, out, inl, style)

    def _inline(self, el, style):
        inl = []
        self._walk_children(el, [], inl, style)  # nested blocks inside inline content are flattened
        return inl

    def _walk(self, el, out, inl, style):
        tag = el.tag
        if tag in SKIP_TAGS:
            return
        cls = el.attrs.get("class", "")
        if tag in HEADINGS:
            self._flush(out, inl)
            c = clean_inl(self._inline(el, style))
            if c:
                out.append({"t": "h", "level": HEADINGS[tag], "inl": c})
        elif tag == "p" or tag in ("dt", "dd", "figcaption", "address"):
            self._flush(out, inl)
            s = style | (STYLE_BOLD if tag == "dt" else 0) | (STYLE_ITALIC if tag == "figcaption" else 0)
            # blocks inside a paragraph (formulas, pictures, <br>) are flushed
            # into out in order by the walk itself
            n = len(out)
            self._walk_children(el, out, inl, s)
            # the rest of a paragraph after such a block continues without an indent
            self._flush(out, inl, verse="verse" in cls or tag == "dt" or len(out) > n)
        elif tag == "br":
            # lines broken by hand (verse, addresses) are drawn without indents
            self._flush(out, inl, verse=True)
        elif tag == "empty-line":
            self._flush(out, inl)
            out.append({"t": "gap"})
        elif tag == "hr":
            self._flush(out, inl)
            out.append({"t": "hr"})
        elif tag in ("ul", "ol"):
            self._flush(out, inl)
            items = []
            for li in el.children:
                if isinstance(li, El) and li.tag == "li":
                    items.append(self.blocks_styled(li, style))
                elif isinstance(li, El) and li.tag in ("ul", "ol") and items:
                    items[-1] += self.blocks_styled(li, style, wrap=True)
            if items:
                start = el.attrs.get("start", "1")
                out.append({"t": "list", "ordered": tag == "ol",
                            "start": int(start) if start.isdigit() else 1, "items": items})
        elif tag == "blockquote" or (tag == "div" and ("epigraph" in cls or "cite" in cls)):
            self._flush(out, inl)
            b = self.blocks_styled(el, style)
            if b:
                out.append({"t": "quote", "blocks": b})
        elif tag == "pre":
            self._flush(out, inl)
            out.append({"t": "code", "text": el.text().strip("\n")})
        elif tag == "table":
            self._flush(out, inl)
            t = self._table(el, style)
            if t:
                out.append(t)
        elif tag in ("img", "image"):
            src = el.attrs.get("src") or el.attrs.get("l:href") or el.attrs.get("xlink:href") \
                or el.attrs.get("href") or ""
            data = self.get_image(src)
            if data:
                self._flush(out, inl)
                out.append({"t": "img", "data": data, "caption": []})
            elif el.attrs.get("alt"):
                inl.append((f"[{el.attrs['alt']}]", style))
        elif tag == "figure":
            self._flush(out, inl)
            sub = self.blocks_styled(el, style)
            imgs = [b for b in sub if b["t"] == "img"]
            caps = [b for b in sub if b["t"] == "p"]
            if len(imgs) == 1 and caps:
                imgs[0]["caption"] = caps[-1]["inl"]
                sub.remove(caps[-1])
            out += sub
        elif tag == "math":
            tex = el.attrs.get("alttext", "")
            for a in el.children:
                if isinstance(a, El) and a.tag == "semantics":
                    for b in a.children:
                        if isinstance(b, El) and b.tag == "annotation" and "tex" in b.attrs.get("encoding", ""):
                            tex = b.text()
            if el.attrs.get("display") == "block":
                self._flush(out, inl)
                out.append({"t": "math", "tex": tex} if tex else {"t": "p", "inl": [(el.text(), style)], "verse": True})
            else:
                inl.append((MATH, tex) if tex else (el.text(), style))
        elif tag in ("sup", "sub"):
            t = el.text().strip()
            if tag == "sup" and t and all(ch in "0123456789+-=()n" for ch in t):
                inl.append((t.translate(SUP), style))
            else:
                inl.append((t, style))
        elif tag == "a" and ("noteref" in el.attrs.get("epub:type", "") or el.attrs.get("type") == "note"):
            inl.append((f"[{el.text().strip().strip('[]')}]", style))
        elif tag in INLINE_STYLE:
            self._walk_children(el, out, inl, style | INLINE_STYLE[tag])
        elif tag in ("li",):
            self._flush(out, inl)
            out += self.blocks_styled(el, style)
        elif tag in BLOCK_TAGS:
            self._flush(out, inl)
            sub = []
            self._walk_children(el, sub, inl, style)
            self._flush(sub, inl, verse="verse" in cls)
            out += sub
        else:
            self._walk_children(el, out, inl, style)

    def blocks_styled(self, el, style, wrap=False):
        out, inl = [], []
        if wrap:
            self._walk(el, out, inl, style)
        else:
            self._walk_children(el, out, inl, style)
        self._flush(out, inl)
        return out

    def _table(self, el, style):
        rows, head = [], 0

        def walk(e, in_head):
            nonlocal head
            for c in e.children:
                if not isinstance(c, El):
                    continue
                if c.tag in ("thead", "tbody", "tfoot"):
                    walk(c, c.tag == "thead")
                elif c.tag == "tr":
                    cells = []
                    all_th = True
                    for td in c.children:
                        if isinstance(td, El) and td.tag in ("td", "th"):
                            all_th &= td.tag == "th"
                            cells.append(clean_inl(self._inline(td, style)))
                            span = td.attrs.get("colspan", "1")
                            for _ in range(int(span) - 1 if span.isdigit() and int(span) < 20 else 0):
                                cells.append([])
                    if cells:
                        if (in_head or all_th) and len(rows) == head:
                            head += 1
                        rows.append(cells)
        walk(el, False)
        if not rows:
            return None
        n = max(len(r) for r in rows)
        rows = [r + [[]] * (n - len(r)) for r in rows]
        return {"t": "table", "rows": rows, "head": head}


# ---------------------------------------------------------------- formats

def read_epub(path):
    z = zipfile.ZipFile(path)
    container = ET.fromstring(z.read("META-INF/container.xml"))
    opf_path = container.find(".//{*}rootfile").get("full-path")
    opf = ET.fromstring(z.read(opf_path))
    dc = "{http://purl.org/dc/elements/1.1/}"
    title = opf.findtext(f".//{dc}title") or ""
    author = opf.findtext(f".//{dc}creator") or ""
    manifest = {i.get("id"): i for i in opf.findall(".//{*}item")}

    def full(href, rel_to):
        return posixpath.normpath(posixpath.join(posixpath.dirname(rel_to), href.split("#")[0]))

    cover = None
    cover_id = None
    for m in opf.findall(".//{*}meta"):
        if m.get("name") == "cover":
            cover_id = m.get("content")
    for i in manifest.values():
        if "cover-image" in (i.get("properties") or ""):
            cover_id = i.get("id")
    if cover_id in manifest:
        try:
            cover = z.read(full(manifest[cover_id].get("href"), opf_path))
        except KeyError:
            pass
    blocks = []
    for ref in opf.findall(".//{*}itemref"):
        item = manifest.get(ref.get("idref"))
        if item is None:
            continue
        name = full(item.get("href"), opf_path)
        try:
            src = z.read(name).decode("utf-8", "replace")
        except KeyError:
            continue

        def get_image(s, name=name):
            if not s or s.startswith("data:"):
                return base64.b64decode(s.split(",", 1)[1]) if s.startswith("data:") else None
            try:
                return z.read(full(s, name))
            except KeyError:
                return None

        dom = parse_html(src)
        body = find(dom, "body") or dom
        b = HtmlDoc(get_image).blocks(body)
        # a cover page that only shows the cover picture
        if cover and len(b) <= 1 and all(x["t"] == "img" for x in b):
            continue
        blocks += b
    return {"title": title.strip(), "author": author.strip(), "cover": cover}, blocks


def find(el, tag):
    for c in el.children:
        if isinstance(c, El):
            if c.tag == tag:
                return c
            r = find(c, tag)
            if r:
                return r
    return None


MATH_BLOCK = re.compile(r"^\$\$(.+?)\$\$[ \t]*$", re.S | re.M)
MATH_INLINE = re.compile(r"(?<![\\$])\$(?!\s)([^$\n]+?)(?<!\s)\$(?!\d)")


# TeX written in the text as on MathJax pages: $$..$$ and \[..\] are display
# formulas, $..$ (the rule of MATH_INLINE, not money) and \(..\) inline ones
TEX_IN_TEXT = re.compile(r"\$\$(.+?)\$\$|\\\[(.+?)\\\]|\\\((.+?)\\\)|" + MATH_INLINE.pattern, re.S)


def text_math(el):
    """Turns TeX in the text of el into math elements."""
    if el.tag in ("pre", "code", "math", "script", "style"):
        return
    out = []
    for c in el.children:
        if isinstance(c, El):
            text_math(c)
            out.append(c)
            continue
        pos = 0
        for m in TEX_IN_TEXT.finditer(c):
            out.append(c[pos:m.start()])
            tex = next(g for g in m.groups() if g is not None).strip()
            out.append(El("math", {"alttext": tex, "display": "block" if m.group(1) or m.group(2) else ""}))
            pos = m.end()
        out.append(c[pos:])
    el.children = [c for c in out if c != ""]


def read_html(path):
    src = open(path, "rb").read().decode("utf-8", "replace")
    dom = parse_html(src)
    text_math(dom)
    title = find(dom, "title")
    d = os.path.dirname(path)

    def get_image(s):
        p = os.path.join(d, s)
        return open(p, "rb").read() if s and os.path.isfile(p) else None

    blocks = HtmlDoc(get_image).blocks(find(dom, "body") or dom)
    return {"title": title.text().strip() if title else "", "author": "", "cover": None}, blocks


FB2_TAGS = {"emphasis": "i", "strong": "b", "epigraph": "blockquote", "cite": "blockquote",
            "poem": "div", "stanza": "div", "v": "p", "subtitle": "h4", "text-author": "p",
            "code": "code", "strikethrough": "span", "style": "span", "a": "a", "sup": "sup",
            "sub": "sub", "table": "table", "tr": "tr", "td": "td", "th": "th", "p": "p",
            "image": "img", "empty-line": "empty-line", "date": "p"}


def read_fb2(path):
    raw = open(path, "rb").read()
    if path.lower().endswith(".zip"):
        z = zipfile.ZipFile(path)
        raw = z.read(z.namelist()[0])
    root = ET.fromstring(raw)

    def local(t):
        return t.split("}")[-1]

    binaries = {}
    for b in root.iter():
        if local(b.tag) == "binary":
            binaries[b.get("id")] = base64.b64decode(b.text or "")

    def attr(e, name):
        for k, v in e.attrib.items():
            if local(k) == name:
                return v
        return None

    title = author = ""
    cover = None
    for e in root.iter():
        if local(e.tag) == "title-info":
            for c in e:
                if local(c.tag) == "book-title":
                    title = (c.text or "").strip()
                elif local(c.tag) == "author" and not author:
                    author = " ".join((x.text or "").strip() for x in c
                                      if local(x.tag) in ("first-name", "last-name") and x.text)
                elif local(c.tag) == "coverpage":
                    for im in c.iter():
                        if local(im.tag) == "image":
                            cover = binaries.get((attr(im, "href") or "").lstrip("#"))
            break

    def convert(e, depth):
        """FB2 element -> El with HTML tags; section titles become hN by depth."""
        tag = local(e.tag)
        if tag == "section":
            el = El("section")
            d = depth + 1
        elif tag == "title":
            el = El(f"h{min(depth, 4) or 1}")
            d = depth
        else:
            el = El(FB2_TAGS.get(tag, "div"))
            d = depth
            if tag in ("poem", "stanza"):
                el.attrs["class"] = "verse"
            if tag == "v":
                el.attrs["class"] = "verse"
            if tag == "text-author":
                el.tag = "p"
                el.attrs["class"] = "verse"
            if tag == "image":
                el.attrs["src"] = (attr(e, "href") or "").lstrip("#")
        if el.tag.startswith("h") and tag == "title":
            # paragraphs inside a title make one heading
            parts = [" ".join("".join(p.itertext()).split()) for p in e if local(p.tag) == "p"]
            el.children.append(" ".join(x for x in parts if x) or "".join(e.itertext()))
            return el
        if e.text:
            el.children.append(e.text)
        for c in e:
            el.children.append(convert(c, d))
            if c.tail:
                el.children.append(c.tail)
        return el

    doc = HtmlDoc(lambda src: binaries.get(src.lstrip("#")))
    blocks, notes = [], []
    for body in root:
        if local(body.tag) != "body":
            continue
        el = convert(body, 0)
        if body.get("name") in ("notes", "comments"):
            notes += doc.blocks(el)
        else:
            b = doc.blocks(el)
            # the body title repeats the book title
            if b and b[0]["t"] == "h" and inl_text(b[0]["inl"]).strip() == title:
                b = b[1:]
            blocks += b
    if notes:
        blocks.append({"t": "h", "level": 1, "inl": [("Примечания", 0)]})
        blocks += [x if x["t"] != "h" else {"t": "p", "inl": x["inl"], "verse": True} for x in notes]
    return {"title": title, "author": author, "cover": cover}, blocks


HEADING_RE = re.compile(r"^(chapter|part|book|глава|часть|книга|том|prologue|epilogue|"
                        r"пролог|эпилог)\b[\w .,:-]{0,60}$|^[IVXLC]{1,7}\.?$", re.I)


PART_RE = re.compile(r"^(part|book|часть|книга|том)\b", re.I)


def read_txt(path):
    raw = open(path, "rb").read()
    for enc in ("utf-8-sig", "cp1251", "latin-1"):
        try:
            text = raw.decode(enc)
            break
        except UnicodeDecodeError:
            continue
    text = text.replace("\r", "")
    if re.search(r"\n\s*\n", text):
        paras = re.split(r"\n\s*\n", text.strip())
        paras = [re.sub(r"\s*\n\s*", " ", p).strip() for p in paras]
    else:
        paras = [p.strip() for p in text.split("\n")]  # one line - one paragraph
    paras = [p for p in paras if p]
    title = ""
    if paras and len(paras[0]) <= 80 and not paras[0].endswith("."):
        title = paras[0]
        paras = paras[1:]
    heads = [len(p) <= 70 and bool(HEADING_RE.match(p)) for p in paras]
    # three or more heading lines in a row are a list of contents; only the
    # last of them (and a part heading just before it) starts the text
    real = list(heads)
    i = 0
    while i < len(paras):
        j = i
        while j < len(paras) and heads[j]:
            j += 1
        if j - i >= 3:
            for k in range(i, j - 1):
                real[k] = False
            if PART_RE.match(paras[j - 2]):
                real[j - 2] = True
        i = j + 1 if j > i else i + 1
    blocks = []
    for p, h, r in zip(paras, heads, real):
        if r:
            blocks.append({"t": "h", "level": 1 if PART_RE.match(p) else 2, "inl": [(p, 0)]})
        else:
            blocks.append({"t": "p", "inl": [(p, 0)], "verse": h})
    return {"title": title, "author": "", "cover": None}, blocks


def read_md(path):
    from markdown_it import MarkdownIt
    src = open(path, encoding="utf-8").read()
    maths = []

    def keep(m, display):
        maths.append(m.group(1).strip())
        k = len(maths) - 1
        return f"\n\n<div data-math=\"{k}\"></div>\n\n" if display else f"<span data-math=\"{k}\"></span>"

    src = MATH_BLOCK.sub(lambda m: keep(m, True), src)
    src = MATH_INLINE.sub(lambda m: keep(m, False), src)
    md = MarkdownIt("commonmark", {"html": True}).enable("table").enable("strikethrough")
    dom = parse_html(md.render(src))

    def fix(el):
        for i, c in enumerate(el.children):
            if isinstance(c, El):
                k = c.attrs.get("data-math")
                if k is not None:
                    m = El("math", {"alttext": maths[int(k)]})
                    if c.tag == "div":
                        m.attrs["display"] = "block"
                    el.children[i] = m
                else:
                    fix(c)
    fix(dom)
    d = os.path.dirname(path)

    def get_image(s):
        p = os.path.join(d, s)
        return open(p, "rb").read() if s and os.path.isfile(p) else None

    blocks = HtmlDoc(get_image).blocks(dom)
    title = ""
    if blocks and blocks[0]["t"] == "h" and blocks[0]["level"] == 1:
        title = inl_text(blocks[0]["inl"])
    # a lone image paragraph becomes a figure with the next italic line as caption
    out = []
    for b in blocks:
        if b["t"] == "p" and out and out[-1]["t"] == "img" and not out[-1]["caption"] \
                and all(s & STYLE_ITALIC for t, s in b["inl"] if t != MATH):
            out[-1]["caption"] = b["inl"]
            continue
        out.append(b)
    return {"title": title, "author": "", "cover": None}, out


def read(path):
    low = path.lower()
    if low.endswith(".epub"):
        return read_epub(path)
    if low.endswith(".fb2") or low.endswith(".fb2.zip"):
        return read_fb2(path)
    if low.endswith((".md", ".markdown")):
        return read_md(path)
    if low.endswith((".html", ".htm", ".xhtml")):
        return read_html(path)
    return read_txt(path)
