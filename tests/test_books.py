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
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
import bookfile as bf  # noqa: E402
import bookparse  # noqa: E402
import mkbook  # noqa: E402

MAX_PAGE_BYTES = 8192
TEXT_MAX = 6144


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
        # positions are saved as anchors: each page needs its own
        if n and (blk, ch) <= prev:
            errors.append(f"page {n}: anchor {(blk, ch)} not after {prev}")
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
        # the phone searches a page's text in a buffer of TEXT_MAX bytes (reader.c)
        if len(text[-1].encode("utf-8")) >= TEXT_MAX:
            errors.append(f"page {n}: {len(text[-1].encode())} bytes of text, the phone searches {TEXT_MAX}")
    for i, (page, level, title) in enumerate(b.toc):
        if page >= b.npages or (i and page < b.toc[i - 1][0]):
            errors.append(f"contents: {title!r} on page {page}")
    if max(sizes) > MAX_PAGE_BYTES:
        errors.append(f"page {sizes.index(max(sizes))} has {max(sizes)} bytes")
    # DjVu and scanned PDF pages are pictures: their text is only for the search
    if src and not mkbook.is_picture_book(src):
        got, want = letters("".join(text)), letters(source_text(src))
        if got != want:
            k = next((i for i in range(min(len(got), len(want))) if got[i] != want[i]), min(len(got), len(want)))
            errors.append(f"text differs from the source at letter {k}: book {got[k - 20:k + 30]!r}, "
                          f"source {want[k - 20:k + 30]!r}")
    return errors, b, max(sizes)


# (source, extension, expected blocks text with formulas as [tex])
PARSE_CASES = [
    ("Код `echo $HOME/$USER` и $x^2$.", ".md", "Код echo $HOME/$USER и [x^2]."),
    ("```\nprice=$a$b $(OBJDIR)\n```", ".md", "price=$a$b $(OBJDIR)"),
    ("<p><kbd>$PATH$</kbd> и \\(a+b\\)</p>", ".html", "$PATH$ и [a+b]"),
]
TEX_CASES = [("a \\le b", "a ≤ b"), ("x^2", "x²"), ("\\int f", None), ("a \\top b", None),
             ("f \\circ g", None), ("\\inf x", None)]


def check_parsers(tmp):
    """Formulas are found outside code only; TeX is written as text by whole commands."""
    errors = []
    for i, (src, ext, want) in enumerate(PARSE_CASES):
        path = os.path.join(tmp, f"case{i}{ext}")
        open(path, "w", encoding="utf-8").write(src)
        _, blocks = bookparse.read(path)
        got = " ".join(b["text"] if b["t"] == "code" else f"[{b['tex']}]" if b["t"] == "math" else
                       "".join(t if t != bookparse.MATH else f"[{x}]" for t, x in b.get("inl", []))
                       for b in blocks)
        if got != want:
            errors.append(f"parse {src!r}: {got!r}, want {want!r}")
    # a table row taller than a page: the table becomes a list, nothing is cut
    path = os.path.join(tmp, "tall.md")
    open(path, "w", encoding="utf-8").write("| a | b | c |\n|---|---|---|\n| x | " + "слово " * 150 + "| y |\n")
    _, _, _, rep = mkbook.build(path, path[:-3] + ".bk")
    errs, _, _ = check(path[:-3] + ".bk", path)
    if errs or rep["tall_tables"] != 1:
        errors.append(f"tall table: {errs[:2]}, tall_tables={rep['tall_tables']}")
    # two books with one file name stop the build; other scripts get stable names
    for d in ("A", "B"):
        os.makedirs(os.path.join(tmp, "src", d), exist_ok=True)
        open(os.path.join(tmp, "src", d, "Book.md"), "w").write("# x\n")
    r = subprocess.run([sys.executable, mkbook.__file__, os.path.join(tmp, "dup"), os.path.join(tmp, "src")],
                       capture_output=True, text=True)
    if r.returncode == 0 or "both become books/Book.bk" not in r.stderr:
        errors.append(f"same names: exit {r.returncode} {r.stderr.strip()!r}")
    if not re.fullmatch(r"book_[0-9a-f]{8}", mkbook.file_name("東京")) or \
            mkbook.file_name("東京") == mkbook.file_name("大阪"):
        errors.append(f"names in other scripts: {mkbook.file_name('東京')}, {mkbook.file_name('大阪')}")
    cut = mkbook.utf8_cut("абв" * 60, mkbook.CONF["title_max"] - 1)
    if len(cut.encode()) > mkbook.CONF["title_max"] - 1 or cut != "абв" * 15 + "аб":
        errors.append(f"title cut: {cut!r}")
    for tex, want in TEX_CASES:
        if mkbook.tex_as_text(tex) != want:
            errors.append(f"tex_as_text({tex!r}) = {mkbook.tex_as_text(tex)!r}, want {want!r}")
    return errors


def main():
    card, sources = sys.argv[1], sys.argv[2:]
    mkbook.FONT = mkbook.Font()
    errors = check_parsers(os.path.dirname(os.path.abspath(card)))
    for e in errors:
        print("FAIL " + e)
    by_name = {}
    for s in sources:
        base = os.path.basename(s)
        for ext in (".fb2.zip", ".epub", ".fb2", ".md", ".html", ".txt", ".pdf", ".djvu"):
            if base.lower().endswith(ext):
                base = base[:-len(ext)]
                break
        by_name[mkbook.file_name(base)] = s
    failed = len(errors)
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
        sys.exit(f"{failed} checks failed")


if __name__ == "__main__":
    main()
