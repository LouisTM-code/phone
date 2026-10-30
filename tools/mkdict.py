#!/usr/bin/env python3
"""Builds dict.bin for the phone from English-Russian dictionaries.

Usage: mkdict.py [--families SKIP.txt] OUT.bin SOURCE [SOURCE...]
Sources: a learner lexicon (a directory of *.txt with "@word" entries, see
sources/LEXICON_FORMAT.md), our words.txt ("word|перевод"), StarDict (.ifo next
to .idx and .dict/.dict.dz), Lingvo DSL (.dsl, .dsl.dz) or TSV (word<TAB>перевод).
The first source that has a word gives its article; words.txt only fills gaps,
big dictionaries (StarDict, DSL) are appended below a "---" line.
--families ends each lexicon article with its word family, the headwords made
from one another by suffixes and prefixes ("Однокоренные: happiness, unhappy");
SKIP.txt lists the "word base" pairs the rules link by mistake.
"""
import gzip
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from textclean import strip_html, tidy  # noqa: E402

MAX_DEF = 3000
# the phone finds the family line by this label (fpdoom/apps/dict.c)
FAMILY_LABEL = "Однокоренные: "
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


POS_RE = re.compile(r"^(modal v|phr\. v|[a-z]+)\.(\s|$)")
PHR_RE = re.compile(r"^phr\. (.+?) - (.+)$")
FORMS_RE = re.compile(r"^v\.\s*\(([^)]*)\)", re.M)
PARTICLES = {"up", "down", "out", "off", "on", "in", "away", "back", "over", "around", "round",
             "through", "along", "about", "across", "after", "by", "into", "for", "with", "apart",
             "ahead", "forward", "together", "onto", "upon", "at", "to", "from"}
OBJECTS = {"someone", "something", "somebody", "sb", "sth", "it"}


def phrase_lines(body):
    """(headword, meaning) of the "phr. cut down (on) - сократить" lines of a
    lexicon article: "cut down"; the bracketed words stay in the meaning"""
    for line in body.split("\n"):
        m = PHR_RE.match(line.strip())
        if not m:
            continue
        head = norm_head(re.sub(r"\([^)]*\)", "", m.group(1)))
        if head and " " in head:
            extra = re.findall(r"\(([^)]*)\)", m.group(1))
            yield head, m.group(2).strip() + (f" ({m.group(1)})" if extra else "")


# endings of a word's forms after its stem, as is_form() in fpdoom/apps/cards.c
ENDS = ["", "s", "es", "e", "ed", "d", "ing", "er", "ers", "est", "ies", "ied", "y", "ying", "ly"]
# at most so many words between the words of a phrase: "gave it up" (cards.c)
PHRASE_GAP = 3


def form_of(word, tok):
    """True if tok is a form of word ("runs", "making" for run, make)"""
    stem = word[:-1] if len(word) > 3 and word[-1] in "ey" else word
    if len(tok) < len(stem) or not tok.startswith(stem):
        return False
    rest = tok[len(stem):]
    if rest and len(stem) > 2 and stem == word and rest[0] == word[-1]:
        rest = rest[1:]
    return rest in ENDS


def has_form(word, text, forms=()):
    """True if a form of word is in text; a phrase ("give up") with its first word
    in any form (or one of forms: "gave") and up to PHRASE_GAP words between its
    words: "gave it up" """
    toks = re.findall(r"[a-z]+", text.lower())
    words = word.split()
    for i, tok in enumerate(toks):
        if tok not in forms and not form_of(words[0], tok):
            continue
        j = i
        for w in words[1:]:
            j = next((k for k in range(j + 1, min(j + 2 + PHRASE_GAP, len(toks))) if toks[k] == w), -1)
            if j < 0:
                break
        if j >= 0:
            return True
    return False


def verb_forms(body):
    """the irregular forms of a verb's article: "v. (gave, given)" """
    m = FORMS_RE.search(body)
    return tuple(f.strip() for f in m.group(1).split(",")) if m else ()


def is_phrasal(head, verbs, heads):
    """a verb (or a word of no article: "hush") with particles: "give up",
    "catch up with", "put someone off" """
    w = head.split()
    return (len(w) <= 4 and (w[0] in verbs or w[0] not in heads) and w[0] != "be"
            and any(x in PARTICLES for x in w[1:])
            and all(x in PARTICLES or x in OBJECTS for x in w[1:]))

# (suffix of the derived word, endings that make the base from what is left,
# part of speech of the derived word, of the base)
SUFFIXES = [
    ("ically", ["ic", "ical"], "adv", "adj"),
    ("ily", ["y"], "adv", "adj"),
    ("ly", ["", "le", "e"], "adv", "adj"),
    ("iness", ["y"], "n", "adj"),
    ("ness", [""], "n", "adj"),
    ("ility", ["le"], "n", "adj"),
    ("ity", ["", "e"], "n", "adj"),
    ("ication", ["y"], "n", "v"),
    ("ation", ["ate", "e", ""], "n", "v"),
    ("ition", ["ite", "e", ""], "n", "v"),
    ("tion", ["te", "", "t"], "n", "v"),
    ("sion", ["d", "de", "se", "t"], "n", "v"),
    ("ion", ["e", ""], "n", "v"),
    ("ment", [""], "n", "v"),
    ("ance", ["ant"], "n", "adj"),
    ("ence", ["ent"], "n", "adj"),
    ("ance", ["e", ""], "n", "v"),
    ("ence", ["e", ""], "n", "v"),
    ("er", ["e", ""], "n", "v"),
    ("or", ["e", ""], "n", "v"),
    ("ist", ["", "y", "e"], "n", "n"),
    ("ful", ["", "y"], "adj", "n"),
    ("less", [""], "adj", "n"),
    ("able", ["e", ""], "adj", "v"),
    ("ible", ["e", ""], "adj", "v"),
    ("ous", ["e", "", "y"], "adj", "n"),
    ("ive", ["e", ""], "adj", "v"),
    ("al", ["e", ""], "adj", "n"),
    ("ial", ["", "y"], "adj", "n"),
    ("ic", ["", "y"], "adj", "n"),
    ("y", ["e", ""], "adj", "n"),
    ("ing", ["e", ""], "adj", "v"),
    ("ed", ["e", ""], "adj", "v"),
    ("en", ["e", ""], "v", "adj"),
    ("ize", ["", "e"], "v", "adj"),
    ("ise", ["", "e"], "v", "adj"),
    ("ship", [""], "n", "n"),
    ("hood", [""], "n", "n"),
]
# prefixes that keep the part of speech; in-/im-/il-/ir- only make adjectives
PREFIXES = ["un", "dis", "mis", "non"]
NEG_PREFIXES = ["in", "im", "il", "ir"]


def parts_of_speech(body):
    out = set()
    for line in body.split("\n"):
        m = POS_RE.match(line)
        if m and m.group(1) != "phr":
            out.add("v" if m.group(1) in ("modal v", "phr. v") else m.group(1))
    return out


def family_bases(w, pos, skip):
    """the headwords w is made from"""
    for suf, ends, dpos, bpos in SUFFIXES:
        stem = w[:-len(suf)]
        if not w.endswith(suf) or dpos not in pos[w] or len(stem) < 3:
            continue
        cands = [stem + e for e in ends]
        if len(stem) > 3 and stem[-1] == stem[-2]:
            cands.append(stem[:-1])  # beginner, running
        for c in cands:
            if c != w and c in pos and bpos in pos[c]:
                if (w, c) not in skip:
                    yield c
                break
    for p in PREFIXES + NEG_PREFIXES:
        b = w[len(p):]
        if not w.startswith(p) or len(b) < 3 or b not in pos or (w, b) in skip:
            continue
        if p in NEG_PREFIXES and not ("adj" in pos[w] and "adj" in pos[b]):
            continue
        if pos[w] & pos[b]:
            yield b


def word_families(bodies, skip):
    """{headword: the other words of its family}"""
    pos = {w: parts_of_speech(b) for w, b in bodies.items()}
    parent = {w: w for w in bodies}

    def root(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for w in bodies:
        for b in family_bases(w, pos, skip):
            parent[root(w)] = root(b)
    groups = {}
    for w in bodies:
        groups.setdefault(root(w), []).append(w)
    return {w: sorted(x for x in g if x != w) for g in groups.values() if len(g) > 1 for w in g}


def read_skip(path):
    skip = set()
    for line in open(path, encoding="utf-8"):
        f = line.split("#")[0].split()
        if len(f) == 2:
            skip.add((f[0], f[1]))
    return skip


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
    args = sys.argv[1:]
    skip = None
    if args[:1] == ["--families"] and len(args) > 1:
        skip = read_skip(args[1])
        args = args[2:]
    if len(args) < 2:
        sys.exit(__doc__)
    out, sources = args[0], args[1:]
    arts, lexicon_heads, phrases = {}, set(), []
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
                if lexicon:
                    lexicon_heads.add(h)
                    phrases.append((h, text))
            n += 1
        print(f"{src}: {n} entries")

    # the phrases of lexicon articles ("phr. give up - сдаваться") as headwords
    verbs = {h for h in lexicon_heads if "v" in parts_of_speech(arts[h])}
    n = 0
    for h, text in phrases:
        forms = verb_forms(text)
        for ph, meaning in phrase_lines(text):
            if ph not in arts:
                pos = "phr. v." if is_phrasal(ph, verbs, lexicon_heads) else "phr."
                # the examples of the word's article that have the phrase
                ex = [line for line in text.split("\n") if line.startswith(" ") and has_form(ph, line, forms)]
                arts[ph] = "\n".join([pos, f"1. {meaning}"] + ex + [f"См. {h}"])
                n += 1
    print(f"phrases: {n} entries")

    if skip is not None:
        fams = word_families({h: arts[h].split("\n---\n")[0] for h in lexicon_heads}, skip)
        for h, others in fams.items():
            # before the articles of big dictionaries
            text = arts[h].split("\n---\n", 1)
            text[0] += "\n" + FAMILY_LABEL + ", ".join(others)
            arts[h] = "\n---\n".join(text)
        print(f"families: {len(fams)} words")

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
