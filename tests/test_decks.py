#!/usr/bin/env python3
"""Checks the decks built by mkdecks.py and the phrases of dict.bin in a card directory.

Usage: test_decks.py CARD_DIR
Every deck of CARD_DIR/decks/index.txt: five fields in its index line, the word
count of its file, 2-3 fields a line, no word twice; the phrase decks hold
phrases, the test topic (tests/decks/) is there with meanings of its own;
dict.bin has the phrases as headwords with the examples of their word; the
examples are found as the phone finds them, "gave it up" for "give up".
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
import mkdecks  # noqa: E402


def dict_articles(path):
    """dict.bin as {headword: article}"""
    data = open(path, "rb").read()
    n, size = struct.unpack("<II", data[4:12])
    heads = data[16:16 + size].decode().split("\0")[:n]
    offs = struct.unpack(f"<{n + 1}I", data[16 + size:16 + size + 4 * (n + 1)])
    base = 16 + size + 4 * (n + 1)
    return {h: data[base + offs[i]:base + offs[i + 1]].decode() for i, h in enumerate(heads)}


def main():
    card = sys.argv[1]
    errors, decks = [], {}
    for line in open(os.path.join(card, "decks/index.txt"), encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) != 5 or f[4] not in ("dict", "own") or not f[3].isdigit():
            errors.append(f"index line: {line!r}")
            continue
        did = os.path.basename(f[0])[:-4]
        if did in decks or did == "my" or not did.isascii():
            errors.append(f"deck id {did}")
        decks[did] = f
        words, n = set(), 0
        for row in open(os.path.join(card, f[0]), encoding="utf-8"):
            r = row.rstrip("\n").split("\t")
            n += 1
            if len(r) not in (2, 3) or not all(r):
                errors.append(f"{did}: line {row!r}")
            elif r[0].lower() in words:
                errors.append(f"{did}: {r[0]} twice")
            words.add(r[0].lower())
            if did == "phrasal" and (" " not in r[0] or not r[1].startswith("phr. v. ")):
                errors.append(f"phrasal: {row!r}")
            if did == "idioms" and (" " not in r[0] or not r[1].startswith("phr. ")):
                errors.append(f"idioms: {row!r}")
        if n != int(f[3]):
            errors.append(f"{did}: {n} words, the index says {f[3]}")
    if "Priroda" not in decks or decks["Priroda"][1:3] != ["Природа", "Темы"] or decks["Priroda"][4] != "own":
        errors.append(f"the test topic: {decks.get('Priroda')}")
    phrasal = open(os.path.join(card, "decks/phrasal.txt"), encoding="utf-8").read()
    for w in ("give up", "cut down", "look forward to"):
        if f"\n{w}\t" not in "\n" + phrasal:
            errors.append(f"phrasal: no {w}")
    arts = dict_articles(os.path.join(card, "dict.bin"))
    for w, start in (("give up", "phr. v.\n1. оставить"), ("cut down", "phr. v.\n"),
                     ("in the long run", "phr.\n1. в конце концов")):
        if not arts.get(w, "").startswith(start):
            errors.append(f"dict.bin: {w}: {arts.get(w)!r}")
    if "cut down on salt" not in arts.get("cut down", ""):
        errors.append("dict.bin: cut down has no example of its word's article")
    for word, text, forms, want in (("give up", "She gave it up at last.", ("gave", "given"), True),
                                    ("give up", "Don't give in, give it all up.", (), True),
                                    ("give up", "Give me the cup, I'll go up.", (), False),
                                    ("cut down", "I'm cutting down on sugar.", (), True),
                                    ("run", "He was running.", (), True)):
        if mkdecks.has_form(word, text, forms) != want:
            errors.append(f"has_form({word!r}, {text!r}) is not {want}")
    for e in errors:
        print(e)
    if errors:
        sys.exit(f"{len(errors)} errors in the decks of {card}")
    print(f"decks ok: {len(decks)}")


if __name__ == "__main__":
    main()
