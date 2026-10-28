#!/usr/bin/env python3
"""Checks the books built by mkbook.py in a card directory.

Usage: test_books.py CARD_DIR SOURCE...
Every book in CARD_DIR/books/index.txt: all pages decode, everything is drawn
inside the page area, anchors and the contents go forward, a page is small
(the phone reads one page at a time) and the text of the book is the text of
its source: the same letters in the same order. SOURCE files are matched to
the books by name.
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
import bookfile as bf  # noqa: E402
import bookparse  # noqa: E402
import mkbook  # noqa: E402

MAX_PAGE_BYTES = 8192


def letters(s):
    return re.sub(r"[\W\d_]+", "", s).lower()


def source_text(src):
    _, blocks = bookparse.read(src)
    out = []

    def walk(bs):
        for b in bs:
            t = b["t"]
            if t in ("p", "h"):
                # inline formulas are pictures, unless simple enough for text
                out.append("".join(t if t != bookparse.MATH else (mkbook.tex_as_text(s) or "")
                                   for t, s in b["inl"]))
            elif t == "code":
                out.append(b["text"])
            elif t == "list":
                for item in b["items"]:
                    walk(item)
            elif t == "quote":
                walk(b["blocks"])
            elif t == "table":
                out.append(" ".join(bookparse.inl_text(c) for r in b["rows"] for c in r))
            elif t == "img":
                out.append(bookparse.inl_text(b["caption"]))
    walk(blocks)
    lay = mkbook.Layout(None, {"missing": {}})
    return lay.clean(" ".join(out))


def check(path, src):
    errors = []
    b = bf.Book(path)
    pw, ph = b.page_w, b.page_h
    sizes = []
    text = []
    prev = (0, 0)
    for n in range(b.npages):
        start, blk, ch = b.pages[n]
        sizes.append(b.pages[n + 1][0] - start)
        if (blk, ch) < prev:
            errors.append(f"page {n}: anchor {(blk, ch)} before {prev}")
        prev = (blk, ch)
        for it in b.items(n):
            if it[0] == "text":
                _, y, flags, runs = it
                for x, st, col, t in runs:
                    s = 2 if st & bf.STYLE_BIG else 1
                    if x < 0 or x + len(t) * 8 * s > pw or y < 0 or y + 16 * s > ph:
                        errors.append(f"page {n}: text outside the page at {x},{y}: {t!r}")
            elif it[0] == "rect":
                _, x, y, w, h, c = it
                if x < 0 or y < 0 or x + w > pw or y + h > ph + 1:
                    errors.append(f"page {n}: rect outside the page {it}")
            elif it[0] == "image":
                _, x, y, iid, full, c = it
                w, h, kind, _ = b.image(iid)
                if x < 0 or y < 0 or x + w > pw or y + h > ph:
                    errors.append(f"page {n}: image {iid} {w}x{h} outside the page at {x},{y}")
        text.append(b.page_text(n))
    for i, (page, level, title) in enumerate(b.toc):
        if page >= b.npages or (i and page < b.toc[i - 1][0]):
            errors.append(f"contents: {title!r} on page {page}")
    if max(sizes) > MAX_PAGE_BYTES and not b.flags & bf.FLAG_PAN:
        errors.append(f"page {sizes.index(max(sizes))} has {max(sizes)} bytes")
    if src and not b.flags & bf.FLAG_PAN:
        got, want = letters("".join(text)), letters(source_text(src))
        if got != want:
            k = next((i for i in range(min(len(got), len(want))) if got[i] != want[i]), min(len(got), len(want)))
            errors.append(f"text differs from the source at letter {k}: book {got[k - 20:k + 30]!r}, "
                          f"source {want[k - 20:k + 30]!r}")
    return errors, b, max(sizes)


def main():
    card, sources = sys.argv[1], sys.argv[2:]
    mkbook.FONT = mkbook.Font()
    by_name = {}
    for s in sources:
        base = os.path.basename(s)
        for ext in (".fb2.zip", ".epub", ".fb2", ".md", ".html", ".txt", ".pdf"):
            if base.lower().endswith(ext):
                base = base[:-len(ext)]
                break
        by_name[re.sub(r"[^\w-]+", "_", base)[:60].strip("_")] = s
    failed = 0
    for line in open(os.path.join(card, "books/index.txt"), encoding="utf-8"):
        fn = line.split("\t")[0]
        name = os.path.basename(fn)[:-3]
        errors, b, big = check(os.path.join(card, fn), by_name.get(name))
        print(f"{'FAIL' if errors else 'ok  '} {fn}: {b.npages} pages, {len(b.toc)} contents, "
              f"{len(b.image_offs)} images, largest page {big} B")
        for e in errors[:5]:
            print("     " + e)
        failed += bool(errors)
    if failed:
        sys.exit(f"{failed} books failed")


if __name__ == "__main__":
    main()
