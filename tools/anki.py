#!/usr/bin/env python3
"""Moves flashcards between Anki and the phone (DATA_DIR/cards.txt).

  anki.py import DATA_DIR DECK.apkg|DECK.txt   add Anki cards to the phone
  anki.py export DATA_DIR OUT.txt              phone cards -> file for Anki's File > Import

The phone keeps its own review schedule; import adds new cards only.
"""
import os
import sqlite3
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from textclean import strip_html, tidy  # noqa: E402

HEADER = "#cards v1"


def load_cards(path):
    header, cards = HEADER, []
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            line = line.rstrip("\n")
            if line.startswith("#"):
                header = line
            elif line:
                cards.append(line.split("\t"))
    return header, cards


def clean_field(s):
    return tidy(strip_html(s), 600).replace("\t", " ").replace("\n", " / ")


def apkg_notes(path):
    z = zipfile.ZipFile(path)
    names = z.namelist()
    with tempfile.TemporaryDirectory() as tmp:
        db = os.path.join(tmp, "col")
        if "collection.anki21b" in names:
            try:
                from compression import zstd  # Python 3.14+
            except ImportError:
                sys.exit("This deck uses the new Anki format; run with python3.14, "
                         "or export it from Anki with 'Support older Anki versions' checked")
            open(db, "wb").write(zstd.decompress(z.read("collection.anki21b")))
        else:
            name = "collection.anki21" if "collection.anki21" in names else "collection.anki2"
            open(db, "wb").write(z.read(name))
        con = sqlite3.connect(db)
        rows = con.execute("select flds from notes").fetchall()
        con.close()
    for (flds,) in rows:
        f = flds.split("\x1f")
        if len(f) >= 2:
            yield f[0], f[1]


def text_notes(path):
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("#"):
            continue
        sep = "\t" if "\t" in line else ";" if ";" in line else None
        if sep:
            f = line.rstrip("\n").split(sep)
            yield f[0], f[1]


def main():
    if len(sys.argv) < 4 or sys.argv[1] not in ("import", "export"):
        sys.exit(__doc__)
    cmd, data_dir, other = sys.argv[1:4]
    path = os.path.join(data_dir, "cards.txt")
    header, cards = load_cards(path)
    if cmd == "import":
        have = {c[0].lower() for c in cards}
        notes = apkg_notes(other) if other.lower().endswith((".apkg", ".colpkg")) else text_notes(other)
        added = 0
        for front, back in notes:
            front, back = clean_field(front), clean_field(back)
            if front and back and front.lower() not in have:
                cards.append([front, back, "0", "0", "2500", "0", "0"])
                have.add(front.lower())
                added += 1
        with open(path, "w", encoding="utf-8") as f:
            f.write(header + "\n")
            for c in cards:
                f.write("\t".join(c) + "\n")
        print(f"added {added} cards, {len(cards)} total in {path}")
    else:
        with open(other, "w", encoding="utf-8") as f:
            f.write("#separator:tab\n#html:false\n")
            for c in cards:
                f.write(f"{c[0]}\t{c[1]}\n")
        print(f"exported {len(cards)} cards to {other}; in Anki use File > Import")


if __name__ == "__main__":
    main()
