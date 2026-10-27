#!/usr/bin/env python3
"""Makes flashcard sets by CEFR level from the learner lexicon.

Usage: mkdecks.py LEXICON_DIR DATA_DIR
Writes DATA_DIR/decks/<level>.txt ("word<TAB>meaning") and decks/index.txt.
"""
import os
import random
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkdict import read_lexicon, norm_head  # noqa: E402
from textclean import tidy  # noqa: E402

LEVELS = ["A1", "A2", "B1", "B2", "C1"]
POS_RE = re.compile(r"^(modal v\.|phr\. v\.|[a-z]+\.)")
SENSE_RE = re.compile(r"^\d+\.\s*(.+?)(?:\s+-\s+.*)?$")
LEVEL_RE = re.compile(r"\b(A1|A2|B1|B2|C1|C2)\b")


def card_back(body):
    """Short answer side: part of speech + the Russian meanings."""
    parts, pos, senses = [], None, []
    for line in body.split("\n"):
        s = line.strip()
        if not s or line.startswith(" "):
            continue
        m = POS_RE.match(s)
        if m and not s[0].isdigit():
            if pos and senses:
                parts.append(f"{pos} {'; '.join(senses)}")
            pos, senses = m.group(1), []
            continue
        m = SENSE_RE.match(s)
        if m and len(senses) < 3:
            senses.append(m.group(1))
    if pos and senses:
        parts.append(f"{pos} {'; '.join(senses)}")
    return tidy(" / ".join(parts), 160)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    lex, data = sys.argv[1:]
    decks = {lv: [] for lv in LEVELS}
    for word, body in read_lexicon(lex):
        h = norm_head(word)
        m = LEVEL_RE.search(body.split("\n", 1)[0]) or LEVEL_RE.search(body)
        if not h or not m:
            continue
        level = "C1" if m.group(1) == "C2" else m.group(1)
        back = card_back(body)
        if back:
            decks[level].append((h, back))
    out = os.path.join(data, "decks")
    os.makedirs(out, exist_ok=True)
    rnd = random.Random(42)
    with open(os.path.join(out, "index.txt"), "w", encoding="utf-8") as idx:
        for lv in LEVELS:
            cards = decks[lv]
            if not cards:
                continue
            # alphabetical order would make daily new words boring
            rnd.shuffle(cards)
            with open(os.path.join(out, f"{lv}.txt"), "w", encoding="utf-8") as f:
                for w, b in cards:
                    f.write(f"{w}\t{b}\n")
            idx.write(f"decks/{lv}.txt\tУровень {lv} - {len(cards)} слов\n")
            print(f"{lv}: {len(cards)} cards")


if __name__ == "__main__":
    main()
