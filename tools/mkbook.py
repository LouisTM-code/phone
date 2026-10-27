#!/usr/bin/env python3
"""Converts books (.txt, .epub, .fb2, .fb2.zip) to plain text for the phone reader.

Usage: mkbook.py DATA_DIR BOOK [BOOK...]
Writes DATA_DIR/books/<name>.txt and updates DATA_DIR/books/index.txt.
"""
import os
import posixpath
import re
import sys
import zipfile
import xml.etree.ElementTree as ET
from html.parser import HTMLParser

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from textclean import tidy  # noqa: E402

MAX_SIZE = 1500 * 1024  # what fits into the phone's RAM next to the dictionary


class TextExtractor(HTMLParser):
    BLOCK = {"p", "div", "br", "h1", "h2", "h3", "h4", "h5", "h6", "li", "tr", "blockquote", "section"}
    SKIP = {"script", "style", "head", "title"}

    def __init__(self):
        super().__init__()
        self.parts, self.skip = [], 0

    def handle_starttag(self, tag, attrs):
        if tag in self.SKIP:
            self.skip += 1
        elif tag in self.BLOCK:
            self.parts.append("\n")

    def handle_endtag(self, tag):
        if tag in self.SKIP:
            self.skip = max(0, self.skip - 1)
        elif tag in self.BLOCK:
            self.parts.append("\n")

    def handle_data(self, data):
        if not self.skip:
            self.parts.append(data.replace("\n", " "))


def html_text(s):
    p = TextExtractor()
    p.feed(s)
    return "".join(p.parts)


def read_txt(path):
    raw = open(path, "rb").read()
    for enc in ("utf-8-sig", "cp1251", "latin-1"):
        try:
            text = raw.decode(enc)
            break
        except UnicodeDecodeError:
            continue
    # join lines wrapped inside paragraphs (Project Gutenberg style)
    paras = [p for p in re.split(r"\n\s*\n", text.replace("\r", "").strip()) if p.strip()]
    first = paras[0].strip() if paras else ""
    # a short first line without a full stop is the title
    title = first if 0 < len(first) <= 80 and "\n" not in first and not first.endswith(".") else None
    return title, "\n".join(re.sub(r"\s*\n\s*", " ", p) for p in paras)


def read_epub(path):
    z = zipfile.ZipFile(path)
    container = ET.fromstring(z.read("META-INF/container.xml"))
    opf_path = container.find(".//{*}rootfile").get("full-path")
    opf = ET.fromstring(z.read(opf_path))
    base = posixpath.dirname(opf_path)
    title = opf.findtext(".//{http://purl.org/dc/elements/1.1/}title")
    items = {i.get("id"): i.get("href") for i in opf.findall(".//{*}item")}
    parts = []
    for ref in opf.findall(".//{*}itemref"):
        href = items.get(ref.get("idref"))
        if not href:
            continue
        name = posixpath.normpath(posixpath.join(base, href.split("#")[0]))
        try:
            parts.append(html_text(z.read(name).decode("utf-8", "replace")))
        except KeyError:
            pass
    return title, "\n".join(parts)


def read_fb2(path):
    data = zipfile.ZipFile(path).read(zipfile.ZipFile(path).namelist()[0]) \
        if path.lower().endswith(".zip") else open(path, "rb").read()
    root = ET.fromstring(data)
    title = root.findtext(".//{*}book-title")
    parts = []
    for body in root.findall(".//{*}body"):
        for el in body.iter():
            tag = el.tag.split("}")[-1]
            if tag in ("p", "v", "subtitle", "text-author"):
                parts.append("".join(el.itertext()))
            elif tag == "empty-line":
                parts.append("")
    return title, "\n".join(parts)


def convert(path):
    low = path.lower()
    if low.endswith(".epub"):
        title, text = read_epub(path)
    elif low.endswith(".fb2") or low.endswith(".fb2.zip"):
        title, text = read_fb2(path)
    else:
        title, text = read_txt(path)
    text = tidy(text)
    text = "\n".join(line.strip() for line in text.split("\n") if line.strip())
    return title, text


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    data_dir = sys.argv[1]
    books_dir = os.path.join(data_dir, "books")
    os.makedirs(books_dir, exist_ok=True)
    index_path = os.path.join(books_dir, "index.txt")
    index = {}
    if os.path.exists(index_path):
        for line in open(index_path, encoding="utf-8"):
            if "\t" in line:
                f, t = line.rstrip("\n").split("\t", 1)
                index[f] = t
    for src in sys.argv[2:]:
        title, text = convert(src)
        name = re.sub(r"[^A-Za-z0-9_-]+", "_", os.path.splitext(os.path.basename(src))[0])[:40]
        name = name.strip("_") or "book"
        title = tidy(title or os.path.splitext(os.path.basename(src))[0].replace("_", " "), 56)
        level = re.match(r"(A1|A2|B1|B2|C1|C2)_", os.path.basename(src))
        if level and not title.startswith(level.group(1)):
            title = f"{level.group(1)}: {title}"
        title = title.replace("\t", " ")
        # two books with the same name must not overwrite each other
        base, k = name, 2
        while f"books/{name}.txt" in index and index[f"books/{name}.txt"] != title:
            if not os.path.exists(os.path.join(data_dir, f"books/{name}.txt")):
                break
            name = f"{base}_{k}"
            k += 1
        data = text.encode("utf-8")
        if len(data) > MAX_SIZE:
            data = data[:MAX_SIZE].rsplit(b"\n", 1)[0]
            print(f"{src}: too long for the phone, cut to {len(data) // 1024} KB")
        fn = f"books/{name}.txt"
        open(os.path.join(data_dir, fn), "wb").write(data)
        index[fn] = title
        print(f"{src} -> {fn} ({len(data) // 1024} KB) \"{index[fn]}\"")
    with open(index_path, "w", encoding="utf-8") as f:
        for fn, t in sorted(index.items(), key=lambda x: x[1].lower()):
            f.write(f"{fn}\t{t}\n")


if __name__ == "__main__":
    main()
