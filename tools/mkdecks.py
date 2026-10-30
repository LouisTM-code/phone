#!/usr/bin/env python3
"""Makes the flashcard decks: CEFR levels and phrases from the learner lexicon,
topics from a folder of Anki decks and tables.

Usage: mkdecks.py LEXICON_DIR DATA_DIR [TOPICS_DIR]
Writes DATA_DIR/decks/<id>.txt ("word<TAB>meaning[<TAB>example]") and decks/index.txt,
a line per deck: "decks/<id>.txt<TAB>name<TAB>group<TAB>words<TAB>kind". The id names
the deck in the phone's cards.txt; kind "dict" is a deck made from the dictionary
(its meanings are the dictionary's), "own" one with meanings of its own.
A topic is TOPICS_DIR/<Тема>.apkg (.colpkg) or .txt ("word<TAB>meaning[<TAB>example]",
or ";"-separated); its id is the name in Latin letters. The subdecks of an Anki deck are
topics of their own ("<subdeck> (<Тема>)"); notes without a Russian field are left out.
"""
import os
import random
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkdict import (read_lexicon, norm_head, phrase_lines, is_phrasal, parts_of_speech,  # noqa: E402
                    has_form, verb_forms)
from textclean import tidy, file_name  # noqa: E402
from anki import CYRILLIC  # noqa: E402

LEVELS = ["A1", "A2", "B1", "B2", "C1"]
POS_RE = re.compile(r"^(modal v\.|phr\. v\.|[a-z]+\.)")
SENSE_RE = re.compile(r"^\d+\.\s*(.+?)(?:\s+-\s+.*)?$")
LEVEL_RE = re.compile(r"\b(A1|A2|B1|B2|C1|C2)\b")
# the phone's cards.txt names the words added from the dictionary and books so
RESERVED = {"my"}


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


def example(word, body, forms=()):
    """The first example sentence (an indented line) with a form of the word."""
    for line in body.split("\n"):
        if line.startswith("---"):
            break
        if line.startswith(" ") and has_form(word, line, forms):
            return tidy(line.strip(), 300)
    return ""


def topic_notes(path):
    """(word, meaning, example, subdeck) of an Anki deck or a table"""
    from anki import apkg_notes, clean_field
    if path.lower().endswith((".apkg", ".colpkg")):
        notes = apkg_notes(path)
    else:
        notes = []
        for line in open(path, encoding="utf-8", errors="replace"):
            if line.startswith("#"):
                continue
            sep = "\t" if "\t" in line else ";" if ";" in line else None
            if sep:
                f = line.rstrip("\n").split(sep)
                notes.append((f[0], f[1], f[2] if len(f) > 2 else "", ""))
    for f, b, e, sub in notes:
        yield clean_field(f), clean_field(b), clean_field(e) if e else "", sub


def write_deck(out, idx, did, name, group, kind, cards):
    with open(os.path.join(out, f"{did}.txt"), "w", encoding="utf-8") as f:
        for w, b, e in cards:
            f.write(f"{w}\t{b}\t{e}\n" if e else f"{w}\t{b}\n")
    idx.write(f"decks/{did}.txt\t{name}\t{group}\t{len(cards)}\t{kind}\n")
    print(f"{did}: {len(cards)} cards")


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    lex, data = sys.argv[1:3]
    topics = sys.argv[3] if len(sys.argv) == 4 else None
    decks = {lv: [] for lv in LEVELS}
    phrasal, idioms, seen = [], [], set()
    entries = list(read_lexicon(lex))
    bodies = {norm_head(w): body for w, body in entries}
    heads = {norm_head(w) for w, body in entries}
    verbs = {norm_head(w) for w, body in entries if "v" in parts_of_speech(body)}
    for word, body in entries:
        h = norm_head(word)
        if not h:
            continue
        forms = verb_forms(body)
        for ph, meaning in phrase_lines(body):
            if ph in seen:
                continue
            seen.add(ph)
            verb = is_phrasal(ph, verbs, heads)
            back = tidy(f"{'phr. v.' if verb else 'phr.'} {meaning}", 160)
            (phrasal if verb else idioms).append((ph, back, example(ph, body, forms)))
        m = LEVEL_RE.search(body.split("\n", 1)[0]) or LEVEL_RE.search(body)
        if not m or h in seen:
            continue
        seen.add(h)
        level = "C1" if m.group(1) == "C2" else m.group(1)
        back = card_back(body)
        if back:
            decks[level].append((h, back, example(h, body, forms)))
    out = os.path.join(data, "decks")
    os.makedirs(out, exist_ok=True)
    rnd = random.Random(42)
    with open(os.path.join(out, "index.txt"), "w", encoding="utf-8") as idx:
        for lv in LEVELS:
            if decks[lv]:
                # alphabetical order would make daily new words boring
                rnd.shuffle(decks[lv])
                write_deck(out, idx, lv, f"Уровень {lv}", "Уровни", "dict", decks[lv])
        rnd.shuffle(phrasal)
        rnd.shuffle(idioms)
        write_deck(out, idx, "phrasal", "Фразовые глаголы", "Фразы", "dict", phrasal)
        write_deck(out, idx, "idioms", "Выражения", "Фразы", "dict", idioms)
        ids = set(LEVELS) | {"phrasal", "idioms"} | RESERVED
        for fn in sorted(os.listdir(topics)) if topics and os.path.isdir(topics) else []:
            base, ext = os.path.splitext(fn)
            if ext.lower() not in (".apkg", ".colpkg", ".txt", ".tsv", ".csv"):
                continue
            subs, words = {}, set()
            for w, b, e, sub in topic_notes(os.path.join(topics, fn)):
                # the decks are English -> Russian: a note of an English-only part is left out
                if w and CYRILLIC.search(b) and w.lower() not in words:
                    words.add(w.lower())
                    # a verb's irregular forms from the lexicon: "go" -> "(went, gone) идти"
                    forms = verb_forms(bodies.get(norm_head(w), ""))
                    subs.setdefault(sub, []).append((w, f"({', '.join(forms)}) {b}" if forms else b,
                                                     e.strip('"')))
            # an Anki deck's subdecks are decks of their own, to be turned on one by one;
            # a topic keeps the order of its file
            for sub, cards in subs.items():
                # the subdeck first: a long name is cut on the phone
                name = f"{sub} ({base})" if len(subs) > 1 else base
                did = file_name(name)
                if did in ids:
                    sys.exit(f"{fn}: the deck id {did} is taken")
                ids.add(did)
                write_deck(out, idx, did, name, "Темы", "own", cards)


if __name__ == "__main__":
    main()
