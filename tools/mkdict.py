#!/usr/bin/env python3
"""Builds dict.bin for the phone from English-Russian dictionaries.

Usage: mkdict.py OUT.bin SOURCE [SOURCE...]
Sources: a learner lexicon (a directory of *.txt with "@word" entries, see
sources/LEXICON_FORMAT.md), our words.txt ("word|перевод"), StarDict (.ifo next
to .idx and .dict/.dict.dz), Lingvo DSL (.dsl, .dsl.dz) or TSV (word<TAB>перевод).
The first source that has a word gives its article; words.txt only fills gaps,
big dictionaries (StarDict, DSL) are appended below a "---" line.
"""
import gzip
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from textclean import strip_html, tidy  # noqa: E402

MAX_DEF = 3000
HEAD_RE = re.compile(r"^[a-z][a-z' .-]*$")


def norm_head(w):
    w = tidy(w).lower().strip()
    w = re.sub(r"\s+", " ", w)
    return w if HEAD_RE.match(w) and len(w) <= 48 else None


def read_words_txt(path):
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if line and not line.startswith("#") and "|" in line:
            en, ru = line.split("|", 1)
            yield en, ru


def read_tsv(path):
    for line in open(path, encoding="utf-8", errors="replace"):
        if "\t" in line and not line.startswith("#"):
            en, ru = line.rstrip("\n").split("\t", 1)
            yield en, ru.replace("\\n", "\n").replace("\t", " ")


def read_stardict(ifo):
    base = ifo[:-4]
    info = {}
    for line in open(ifo, encoding="utf-8", errors="replace"):
        if "=" in line:
            k, v = line.strip().split("=", 1)
            info[k] = v
    idx_path = base + ".idx"
    if not os.path.exists(idx_path) and os.path.exists(idx_path + ".gz"):
        idx = gzip.open(idx_path + ".gz").read()
    else:
        idx = open(idx_path, "rb").read()
    if os.path.exists(base + ".dict.dz"):
        data = gzip.open(base + ".dict.dz").read()
    else:
        data = open(base + ".dict", "rb").read()
    off_size = 8 if info.get("idxoffsetbits") == "64" else 4
    seq = info.get("sametypesequence", "")
    pos = 0
    while pos < len(idx):
        end = idx.index(b"\0", pos)
        word = idx[pos:end].decode("utf-8", "replace")
        pos = end + 1
        if off_size == 8:
            off, size = struct.unpack(">QI", idx[pos:pos + 12])
        else:
            off, size = struct.unpack(">II", idx[pos:pos + 8])
        pos += off_size + 4
        raw = data[off:off + size]
        if not seq:
            # each field starts with a type char, text fields end with \0
            raw = raw[1:]
        text = raw.split(b"\0")[0].decode("utf-8", "replace")
        if not seq or seq[0] in "hxg":
            text = strip_html(text)
        # drop phonetic transcription like [ˈhaʊs]
        text = re.sub(r"\[[^\]\n]*[^\x00-\x7fЀ-ӿ\]][^\]\n]*\]", "", text)
        yield word, text


def read_dsl(path):
    raw = gzip.open(path).read() if path.endswith(".dz") else open(path, "rb").read()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        text = raw.decode("utf-16")
    else:
        text = raw.decode("utf-8-sig", "replace")
    heads, body = [], []

    def flush():
        if heads and body:
            art = "\n".join(body)
            # escaped brackets are text, hide them from the markup regexps
            art = art.replace("\\[", "\x01").replace("\\]", "\x02").replace("\\~", "~")
            art = re.sub(r"\[t\].*?\[/t\]", "", art)          # transcription
            art = re.sub(r"\[s\].*?\[/s\]", "", art)          # sounds, pictures
            art = re.sub(r"\[/?[^\]]*\]", "", art)            # other markup
            art = art.replace("\x01", "[").replace("\x02", "]")
            art = re.sub(r"\{\{.*?\}\}", "", art)
            for h in heads:
                yield h, art

    for line in text.splitlines():
        if line.startswith("#"):
            continue
        if line[:1] in ("\t", " "):
            body.append(line.strip())
        elif line.strip():
            if body:
                yield from flush()
                heads, body = [], []
            heads.append(re.sub(r"\{.*?\}", "", line).strip())
    yield from flush()


def read_lexicon(path):
    files = sorted(os.path.join(path, f) for f in os.listdir(path) if f.endswith(".txt")) \
        if os.path.isdir(path) else [path]
    for fn in files:
        head, body = None, []
        for line in open(fn, encoding="utf-8", errors="replace"):
            line = line.rstrip()
            if line.startswith("@"):
                if head and body:
                    yield head, "\n".join(body)
                head, body = line[1:].strip(), []
            elif head and line.strip():
                body.append(line)
        if head and body:
            yield head, "\n".join(body)


def read_source(path):
    low = path.lower()
    if os.path.isdir(path) or low.endswith(".lex"):
        return read_lexicon(path)
    if low.endswith(".ifo"):
        return read_stardict(path)
    if low.endswith(".dsl") or low.endswith(".dsl.dz"):
        return read_dsl(path)
    if os.path.basename(low) == "words.txt":
        return read_words_txt(path)
    return read_tsv(path)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out, sources = sys.argv[1], sys.argv[2:]
    arts = {}
    for src in sources:
        n = 0
        low = src.lower()
        big = low.endswith(".ifo") or ".dsl" in low
        lexicon = os.path.isdir(src) or low.endswith(".lex")
        for word, text in read_source(src):
            h = norm_head(word)
            text = tidy(text, MAX_DEF, keep_indent=lexicon)
            if not h or not text:
                continue
            if h in arts:
                if big and text not in arts[h] and len(arts[h]) < MAX_DEF:
                    arts[h] += "\n---\n" + text
                else:
                    continue
            else:
                arts[h] = text
            n += 1
        print(f"{src}: {n} entries")

    heads = sorted(arts)
    head_blob = b"".join(h.encode() + b"\0" for h in heads)
    defs, offsets, pos = [], [], 0
    for h in heads:
        d = arts[h][:MAX_DEF].encode("utf-8")
        offsets.append(pos)
        defs.append(d)
        pos += len(d)
    offsets.append(pos)
    with open(out, "wb") as f:
        f.write(b"DIC1")
        f.write(struct.pack("<III", len(heads), len(head_blob), pos))
        f.write(head_blob)
        f.write(struct.pack(f"<{len(offsets)}I", *offsets))
        f.write(b"".join(defs))
    ram = len(head_blob) + 8 * len(heads)
    print(f"{out}: {len(heads)} words, {os.path.getsize(out) // 1024} KB, "
          f"phone RAM for the index ~{ram // 1024} KB")


if __name__ == "__main__":
    main()
