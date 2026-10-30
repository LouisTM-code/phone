#!/usr/bin/env python3
"""Makes the learner lexicon (sources/LEXICON_FORMAT.md) from free dictionaries,
with no manual work and no LLM.

Usage: mklexicon.py DL OLD_LEXICON OUT_DIR [WORD...]
DL holds (make lexdl):
  mueller7.index, mueller7.dict.dz      Mueller English-Russian (GPL), the meanings
  eng_sentences.tsv.bz2, rus_sentences.tsv.bz2, eng-rus_links.tsv.bz2
                                        Tatoeba (CC BY), the examples
  kaikki.org-dictionary-English.jsonl   English Wiktionary (CC BY-SA), the English
                                        meanings and word forms; cut on the first
                                        run to the words of Mueller in wikt.jsonl
  cefrj-vocabulary-profile-1.5.csv, octanove-vocabulary-profile-c1c2-1.0.csv
                                        CEFR-J word lists, the words and levels
The words are those of the CEFR-J lists and the headwords of OLD_LEXICON. A word's
meanings are Mueller's without special labels (мед., юр. ...), ordered by how many
Tatoeba sentences of the word have them in their Russian translation, at most
MAX_SENSES; the example of a meaning is such a sentence, else a phrase of Mueller.
The English meaning is the Wiktionary sense whose Russian translations share a
word with it; the level is CEFR-J's, else the old lexicon's. Phrases of Mueller
met in Tatoeba become "phr." lines. A word Mueller has nothing for keeps its
article of OLD_LEXICON. WORD... makes only these words (to try changes).
"""
import bz2
import csv
import gzip
import json
import os
import re
import sys
import unicodedata
from bisect import bisect_left

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from textclean import IPA  # noqa: E402
from mkdict import (read_lexicon, norm_head, form_of, has_form, PARTICLES, OBJECTS,  # noqa: E402
                    POS_RE)

MAX_SENSES = 4
MAX_PHRASAL = 6       # phrasal verbs of a word, met in Tatoeba MIN_PHRASAL times
MIN_PHRASAL = 2
MAX_IDIOMS = 3        # other phrases, met MIN_IDIOM times
MIN_IDIOM = 3
# prepositions met after any verb: "run to" is not counted as a phrasal verb
WEAK_PARTICLES = {"to", "at", "for", "with", "from", "by", "upon", "onto"}
RU_MAX = 34           # the Russian part of a meaning, characters
EX_EN_MAX = 60        # an example sentence and its translation
EX_RU_MAX = 70
# Mueller's parts of speech; participles (_p-p.) and the rest are skipped
M_POS = {"n": "n", "v": "v", "a": "adj", "adv": "adv", "prep": "prep", "cj": "conj",
         "pron": "pron", "interj": "excl", "n-card": "num", "n-ord": "num"}
# labels a meaning may have; any other (мед., юр., уст., ам., attr ...) drops it
OK_LABELS = {"разг", "перен", "книж", "pl", "refl", "predic", "шутл", "неодобр", "ирон",
             "презр", "собир", "эмоц-усил", "усил", "обыкн", "inf", "pass"}
CEFRJ_POS = {"noun": "n", "verb": "v", "adjective": "adj", "adverb": "adv",
             "preposition": "prep", "conjunction": "conj", "pronoun": "pron",
             "determiner": "det", "number": "num", "interjection": "excl",
             "modal auxiliary": "modal v", "be-verb": "v", "do-verb": "v", "have-verb": "v"}
# a CEFR-J part of speech matches these of Mueller
POS_MATCH = {"det": {"adj", "pron"}, "modal v": {"v"}, "num": {"num", "n", "adj"}}
WIKT_POS = {"noun": "n", "verb": "v", "adj": "adj", "adv": "adv", "prep": "prep",
            "conj": "conj", "pron": "pron", "det": "det", "num": "num", "intj": "excl"}
LEVELS = ["A1", "A2", "B1", "B2", "C1", "C2"]
UK_TAGS = {"Received-Pronunciation", "UK", "British"}
RU_STOP = {"что", "кто", "как", "для", "или", "это", "свой", "быть", "кого", "чего", "чему",
           "кем", "чем", "нибудь", "либо", "обыкн", "особ", "напр", "тж", "чьих", "чьей",
           "чей", "чья", "своих", "который", "такой", "тот", "так", "его", "она", "они"}
RU_ENDS = sorted("""ывать ивать ование ение ость ешь ете ают яют ять ать еть ить уть ыть
    ит ет ут ют ат ят ишь ила ило или ала ало али ела ели ил ал ел ыл
    ого его ому ему ыми ими ая яя ое ее ые ие ый ий ой ую юю ам ям ах ях ов ев ей ом ем
    ию ия ь а я о е ы и у ю й""".split(), key=len, reverse=True)
NAMES = re.compile(r"\b(Tom|Mary|John|Alice)\b")


# --- Russian words

def ru_words(text):
    return re.findall(r"[а-яё]+", text.lower().replace("ё", "е").replace("́", ""))


def ru_stem(w):
    for e in ("ся", "сь"):
        if w.endswith(e) and len(w) > 4:
            w = w[:-2]
    for e in RU_ENDS:
        if w.endswith(e) and len(w) - len(e) >= 3:
            return w[:-len(e)]
    return w


def ru_stems(text):
    return {ru_stem(w) for w in ru_words(text) if len(w) >= 3 and w not in RU_STOP}


RU_PREFIXES = ("пере", "раз", "рас", "про", "при", "под", "от", "за", "вы", "по", "на", "до",
               "об", "у", "с")


def unprefix(w):
    """"законч" -> "конч": a verb and its perfective"""
    for p in RU_PREFIXES:
        if w.startswith(p) and len(w) - len(p) >= 4:
            return w[len(p):]
    return w


def stem_match(a, b):
    if a == b or unprefix(a) == unprefix(b):
        return True
    n = min(len(a), len(b))
    cp = 0
    while cp < n and a[cp] == b[cp]:
        cp += 1
    return (cp >= n and n >= 4) or (cp >= 5 and cp >= n - 1)


def stems_meet(xs, ys):
    return any(stem_match(x, y) for x in xs for y in ys)


ADJ_ENDS = ("ый", "ий", "ой", "ая", "яя", "ое", "ее", "ые", "ие", "ого", "его", "о")


def item_stems(segs):
    """the stems of each meaning of Mueller's Russian part ("быстро
    распространяться": the verb only), without the words in brackets"""
    out = []
    for seg in segs:
        for it in split_top(seg, ",;"):
            ws = [w for w in ru_words(re.sub(r"\([^)]*\)", "", it)) if len(w) >= 3 and w not in RU_STOP]
            if len(ws) > 1:
                ws = [w for w in ws if not w.endswith(ADJ_ENDS)] or ws
            if ws:
                out.append({ru_stem(w) for w in ws})
    return out


def match_score(items, stems):
    """3: the first meaning has a stem of the sentence, 2: its stem starts one,
    1: another meaning has, 0: none"""
    best = 0
    for i, st in enumerate(items):
        for a in st:
            for b in stems:
                if a == b and i == 0:
                    return 3
                if stem_match(a, b):
                    best = max(best, 2 if i == 0 else 1)
    return best


# --- Mueller

def b64(s):
    a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    n = 0
    for c in s:
        n = n * 64 + a.index(c)
    return n


def read_mueller(dl):
    data = gzip.open(os.path.join(dl, "mueller7.dict.dz")).read()
    arts = {}
    for line in open(os.path.join(dl, "mueller7.index"), encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) == 3:
            off, size = b64(f[1]), b64(f[2])
            arts.setdefault(f[0].lower(), []).append(data[off:off + size].decode("utf-8", "replace"))
    return arts


def split_top(text, seps=";"):
    """text cut at seps outside brackets"""
    out, depth, cur = [], 0, ""
    for c in text:
        depth += c in "({"
        depth -= c in ")}" and depth > 0
        if c in seps and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += c
    out.append(cur)
    return [s.strip() for s in out if s.strip()]


def labels_of(text):
    """(labels, text without them): "_разг. навеселе" """
    labs = re.findall(r"(?<![\w])_([a-zа-я-]+)\.", text)
    return set(labs), re.sub(r"(?<![\w])_[a-zа-я-]+\.\s*", "", text).strip()


def first_cyr(s):
    m = re.search(r"[а-яёА-ЯЁ]", s)
    return m.start() if m else -1


def clean_phrase(en):
    en = re.sub(r"\([^)]*[а-яё][^)]*\)", "", en)          # (или to put), (c _inf.)
    en = en.replace("smb.", "someone").replace("smth.", "something")
    en = re.sub(r"\s+", " ", en).strip(" ,.:!?")
    return re.sub(r"^to ", "", en)


def parse_mueller(texts):
    """[(pos, forms, [sense])] of a word's articles; a sense is
    {"ru": [segments], "phrases": [[en, ru, [other meanings]]], "labels": set}"""
    blocks = []
    for text in texts:
        text = re.sub(r"\[[^\]а-яё]*\]", "", text)         # transcription
        text = re.sub(r"\{[^}]*\}", "", text)               # {см. тж. ...}
        lines = text.split("\n")[1:]
        block = sense = None
        sub = None
        depth = 0     # inside a bracket of a part of speech line: "(обыкн. употр. ..."

        def new_sense():
            s = {"items": [], "labels": set()}
            block[2].append(s)
            return s
        for line in lines:
            s = line.strip()
            if not s or re.match(r"^_[IVX]+$", s):
                continue
            if depth > 0:
                depth += s.count("(") - s.count(")")
                continue
            s = re.sub(r"^_[IVX]+\s+", "", s)
            m = re.match(r"^(?:\d+\.\s+)?_([a-z-]+)\.(.*)$", s)
            if m and (m.group(1) in M_POS or m.group(1) in ("pres-p", "p-p", "p", "obj")):
                pos = M_POS.get(m.group(1))
                rest = m.group(2).strip()
                fm = re.match(r"^\(([a-z]+(?:\s*-\s*[a-z]+)+)\)\s*", rest)
                forms = tuple(x.strip() for x in fm.group(1).split("-")) if fm else ()
                if fm:
                    rest = rest[fm.end():]
                block = [pos, forms, []]
                blocks.append(block)
                sense, sub = None, None
                if rest.startswith("("):
                    depth = rest.count("(") - rest.count(")")
                elif rest:
                    sense = new_sense()
                    sense["items"].append(("text", rest))
                continue
            if block is None:
                continue
            m = re.match(r"^(\d+)\)\s*(.*)$", s)
            if m:
                sense, sub = new_sense(), None
                sense["items"].append(("text", m.group(2)))
                continue
            m = re.match(r"^([а-я])\)\s*(.*)$", s)
            if m and sense:
                sense["items"].append(("sub", m.group(1), m.group(2)))
                continue
            if sense is None:
                sense = new_sense()
                sense["items"].append(("text", s))
            else:
                last = sense["items"][-1]
                sense["items"][-1] = last[:-1] + (last[-1] + " " + s,)
    out = []
    for pos, forms, senses in blocks:
        if not pos:
            continue
        done = []
        for s in senses:
            done.append(sense_parts(s["items"]))
        out.append((pos, forms, done))
    return out


def sense_parts(items):
    ru, phrases, labels, pending = [], [], set(), None
    subs = None   # the phrase whose sub-meanings а), б) ... these are
    for it in items:
        if it[0] == "sub":
            labs, seg = labels_of(split_top(it[2])[0] if split_top(it[2]) else "")
            if it[1] == "а":
                subs, pending = pending, None
            ok = not labs - OK_LABELS and first_cyr(seg) == 0
            if subs is not None and ok:
                if not phrases[subs][1]:
                    phrases[subs][1] = seg
                else:
                    phrases[subs][2].append(seg)
            # phrases after the sub-meanings: "...; give back возвращать"
            for seg in split_top(it[2])[1:]:
                n = len(phrases)
                pending = add_segment(seg, ru, phrases, labels, pending, after_sub=True)
                if len(phrases) > n:
                    subs = None
            continue
        subs = None
        for seg in split_top(it[1]):
            pending = add_segment(seg, ru, phrases, labels, pending)
    return {"ru": ru, "phrases": [p for p in phrases if p[1]], "labels": labels}


def add_segment(seg, ru, phrases, labels, pending, after_sub=False):
    labs, seg = labels_of(seg)
    seg = re.sub(r"\s*\(или [^)]*\)", "", seg)                # happy thought (или idea)
    seg = re.sub(r"\(\s*(?:обыкн\.|тж\.)?\s*\)\s*", "", seg).strip()
    c = first_cyr(seg)
    lat = re.search(r"[a-zA-Z]", seg)
    if lat and (c < 0 or lat.start() < c) and not seg.startswith("("):
        en = clean_phrase(seg[:c] if c > 0 else seg)
        r = seg[c:].strip() if c > 0 else ""
        if labs - OK_LABELS:
            return None
        phrases.append([en, r, []])
        return len(phrases) - 1 if not r else None
    if not seg or after_sub:
        return pending
    if not phrases and not ru:
        labels |= labs
    if not phrases:
        ru.append(seg)
    elif pending is not None and not phrases[pending][1]:
        phrases[pending][1] = seg
        return None
    return pending


def ru_display(segs):
    """"схватывать, зажимать (в руке)": the first meanings, short"""
    items = []
    for seg in segs:
        if len(seg.split()) > 8:
            continue    # an explanation: "с различными ... существительными образует ..."
        for it in split_top(seg, ",;"):
            it = re.sub(r"\(([^)]*)\)", lambda m: "" if (len(m.group(1)) > 25 or re.search(
                r"[a-zA-Z]", m.group(1))) else m.group(0), it)
            it = re.sub(r"\s*\bи т\. ?п\.", "", it)
            it = re.sub(r"\s+", " ", it).strip(" ,;.")
            if not it or len(it.split()) > 5 or first_cyr(it) < 0:
                continue
            if it not in items:
                items.append(it)
    out = ""
    for it in items:
        cand = it if not out else out + ", " + it
        if out and len(cand) > RU_MAX:
            break
        out = cand
    return out


# --- Tatoeba

class Tatoeba:
    def __init__(self, dl):
        links = {}
        with bz2.open(os.path.join(dl, "eng-rus_links.tsv.bz2"), "rt") as f:
            for line in f:
                a, b = line.split()
                links.setdefault(int(a), []).append(int(b))
        want_ru = {b for v in links.values() for b in v}
        ru = {}
        with bz2.open(os.path.join(dl, "rus_sentences.tsv.bz2"), "rt", encoding="utf-8") as f:
            for line in f:
                i, _, t = line.rstrip("\n").split("\t", 2)
                if int(i) in want_ru:
                    ru[int(i)] = t
        self.pairs, self.index = [], {}
        with bz2.open(os.path.join(dl, "eng_sentences.tsv.bz2"), "rt", encoding="utf-8") as f:
            for line in f:
                i, _, t = line.rstrip("\n").split("\t", 2)
                tr = [ru[b] for b in links.get(int(i), ()) if b in ru]
                if not tr or len(t) > 120:
                    continue
                n = len(self.pairs)
                self.pairs.append((t, tr))
                for tok in set(re.findall(r"[a-z']+", t.lower())):
                    self.index.setdefault(tok, []).append(n)
        self.vocab = sorted(self.index)

    def with_forms(self, word, forms=(), exact=False):
        """pairs having a form of the one-word word: the word or one of forms when
        exact (Wiktionary's forms: no "runner" for run), else what form_of() takes"""
        out = set(self.index.get(word, ()))
        for f in forms:
            out.update(self.index.get(f, ()))
        if exact:
            return out
        stem = word[:-1] if len(word) > 3 and word[-1] in "ey" else word
        i = bisect_left(self.vocab, stem)
        while i < len(self.vocab) and self.vocab[i].startswith(stem):
            if form_of(word, self.vocab[i]):
                out.update(self.index[self.vocab[i]])
            i += 1
        return out

    def with_phrase(self, phrase, forms=()):
        words = re.findall(r"[a-z']+", phrase)
        if not words:
            return []
        cands = self.with_forms(words[0], forms, exact=bool(forms))
        for w in words[1:]:
            if w not in OBJECTS and w != "one's":
                cands &= set(self.index.get(w, ()))
        return [n for n in cands if has_form(phrase, self.pairs[n][0], forms)]

    def phrase_hits(self, phrase, ru, forms=()):
        """sentences of a phrase: (those whose translation has the phrase's Russian
        meaning, those that only have its words one after another: "gave up")"""
        words = phrase.split()
        items = item_stems([ru])
        meant, near = [], []
        for n in self.with_phrase(phrase, forms):
            toks = re.findall(r"[a-z']+", self.pairs[n][0].lower())
            if match_score(items, ru_stems(" ".join(self.pairs[n][1]))):
                meant.append(n)
            elif any((t in forms or form_of(words[0], t)) and toks[i + 1:i + len(words)] == words[1:]
                     for i, t in enumerate(toks)):
                near.append(n)
        return meant, near


# --- Wiktionary

def wikt_slim(dl, words):
    """{word: {pos: {"forms": [...], "tr": [(sense, ru)], "ipa": "/ɡrɑːsp/"}}} of
    Wiktionary, cut once in DL/wikt.jsonl to the words of Mueller and those with
    Russian translations; the British transcription (the American one if there is none)"""
    slim = os.path.join(dl, "wikt.jsonl")
    if not os.path.exists(slim):
        with open(os.path.join(dl, "kaikki.org-dictionary-English.jsonl"), encoding="utf-8") as f, \
                open(slim + ".tmp", "w", encoding="utf-8") as out:
            for line in f:
                e = json.loads(line)
                w = e.get("word", "")
                if e.get("lang_code") != "en" or e.get("pos") not in WIKT_POS:
                    continue
                forms = sorted({x["form"] for x in e.get("forms", []) if re.fullmatch(r"[a-z]+", x.get("form", ""))
                                and not set(x.get("tags", [])) & {"alternative", "table-tags", "inflection-template",
                                                                   "obsolete", "archaic", "dialectal", "nonstandard"}})
                trs = e.get("translations", []) + [t for s in e.get("senses", []) for t in s.get("translations", [])]
                tr = [(t.get("sense", ""), t["word"].replace("́", "")) for t in trs
                      if t.get("code") == "ru" and t.get("word")]
                if w not in words and not tr:
                    continue
                ipas = [(not set(x.get("tags", [])) & UK_TAGS, i, x["ipa"])
                        for i, x in enumerate(e.get("sounds", [])) if x.get("ipa", "").startswith("/")]
                out.write(json.dumps({"w": w, "pos": WIKT_POS[e["pos"]], "forms": forms, "tr": tr,
                                      "ipa": min(ipas)[2] if ipas else ""}, ensure_ascii=False) + "\n")
        os.rename(slim + ".tmp", slim)
    wikt = {}
    for line in open(slim, encoding="utf-8"):
        e = json.loads(line)
        d = wikt.setdefault(e["w"], {}).setdefault(e["pos"], {"forms": set(), "tr": [], "ipa": ""})
        d["forms"].update(e["forms"])
        d["ipa"] = d["ipa"] or e["ipa"]
        d["tr"] += [tuple(t) for t in e["tr"]]
    return wikt


def gloss_for(ru_segs, wpos):
    """the English meaning of the Wiktionary translation group that has the first
    Russian meaning ("" if none), and that group's words"""
    groups = {}
    for sense, ru in wpos.get("tr", []):
        if sense:
            groups.setdefault(sense, []).append(ru)
    # the group whose first word is the first meaning wins; then the most shared
    # words, the earlier group (Wiktionary's main sense) on a tie
    items = item_stems(ru_segs)
    mine = set().union(*items) if items else set()
    best, score = None, (0, 0, 0)
    for sense, rus in groups.items():
        # a word of the group is the first Russian meaning ("трудный" of rough is
        # "difficult": суровый, трудный, тяжёлый); "коридор" of passage is no
        # "section of text" (отрывок, пассаж) though both have "пассаж"
        if not items or not any(stem_match(a, b) for r in rus for a in ru_stems(r) for b in items[0]):
            continue
        n = (":" not in sense,    # "slang: marijuana", "gambling: banker's funds"
             match_score(items, ru_stems(rus[0])),
             sum(1 for r in rus if ru_stems(r) & mine))
        if n > score:
            best, score = sense, n
    if not best:
        return "", set()
    g = re.split(r";", best)[0].split(":")[-1].strip()
    g = re.sub(r"^to ", "", g)
    g = re.sub(r"\s*\((?:transitive|intransitive|figuratively|informal|colloquial)[^)]*\)\s*", " ", g).strip()
    if len(g) > 45:
        g = re.split(r"[,(]", g)[0].strip()
    if len(g) > 45:
        g = ""    # cut, it would read wrong: "the proportional relationship between one"
    return g, ru_stems(" ".join(groups[best]))


# --- word lists

# Wiktionary's signs the font hasn't got, as the nearest it has
IPA_MAP = {"ɡ": "g", "ɹ": "r", "ɫ": "l", "ɾ": "t", "ɐ": "ʌ", "ɚ": "ə", "ɝ": "ɜː", "ʧ": "tʃ",
           "ʤ": "dʒ", "ɘ": "ə", "ɵ": "əʊ", ".": "", "‿": "", "ˑ": ""}


def transcription(ipa):
    """"/ɡɹɑːsp/" -> "/grɑːsp/"; "" if it has a sign the font can't draw"""
    s = re.sub(r"\([^)]*\)", "", ipa.split(",")[0].strip())   # /ˈwɔː(ɹ)/: optional sounds
    s = "".join(c for c in unicodedata.normalize("NFD", s) if not unicodedata.combining(c))
    s = "".join(IPA_MAP.get(c, c) for c in s)
    ok = s.startswith("/") and s.endswith("/") and len(s) > 2
    return s if ok and all(c in IPA or c in "/ " or "a" <= c <= "z" for c in s) else ""


def read_levels(dl):
    """{word: {pos: level}} of the CEFR-J lists, {word: its other spellings}"""
    out, variants = {}, {}
    for name in ("cefrj-vocabulary-profile-1.5.csv", "octanove-vocabulary-profile-c1c2-1.0.csv"):
        for r in csv.DictReader(open(os.path.join(dl, name), encoding="utf-8")):
            pos = CEFRJ_POS.get(r["pos"].strip())
            lv = r["CEFR"].strip()
            if not pos or lv not in LEVELS:
                continue
            heads = [h for h in map(norm_head, r["headword"].split("/")) if h]
            for h in heads[:1]:
                d = out.setdefault(h, {})
                if pos not in d or LEVELS.index(lv) < LEVELS.index(d[pos]):
                    d[pos] = lv
                variants.setdefault(h, [])
                variants[h] += [v for v in heads[1:] if v not in variants[h]]
    return out, variants


def old_levels(body):
    """{pos: level} of an old lexicon article"""
    out = {}
    for line in body.split("\n"):
        m = POS_RE.match(line)
        if m and m.group(1) != "phr":
            lv = re.search(r"\b(A1|A2|B1|B2|C1|C2)\b", line)
            if lv:
                out["v" if m.group(1) in ("modal v", "phr. v") else m.group(1)] = lv.group(1)
    return out


# --- the article

def pick_example(tat, nums, used):
    best = None
    for n in nums:
        en, trs = tat.pairs[n]
        if en in used or len(en) > EX_EN_MAX or len(en) < 12:
            continue
        ru = min(trs, key=len)
        if len(ru) > EX_RU_MAX:
            continue
        key = (bool(NAMES.search(en)), abs(len(en) - 38), en)
        if best is None or key < best[0]:
            best = (key, en, ru)
    if best:
        used.add(best[1])
        return f"{best[1]} - {best[2]}"
    return None


DETS = {"a", "an", "the", "my", "your", "his", "her", "its", "our", "their", "this", "that",
        "these", "those", "every", "each", "no", "some", "any"}
VERB_BEFORE = {"to", "will", "would", "can", "could", "should", "must", "might", "may", "shall",
               "i", "you", "we", "they", "don't", "didn't", "doesn't", "won't", "can't", "let's",
               "please", "not", "never", "always", "often", "he", "she", "it", "who"}


def pos_hint(sentence, word, pos_forms):
    """the parts of speech the word may be in the sentence, None if not known:
    a form only verbs have ("ran"), a word after "the" (noun) or after "to", "can"
    (verb)"""
    toks = re.findall(r"[a-z']+", sentence.lower())
    nouns, verbs = pos_forms.get("n", set()) | {word}, pos_forms.get("v", set()) | {word}
    if "n" not in pos_forms or "v" not in pos_forms:
        return None
    for i, t in enumerate(toks):
        if t not in nouns and t not in verbs:
            continue
        if t in verbs and t not in nouns:
            return {"v"}
        if t in nouns and t not in verbs:
            return {"n", "adj"}
        prev = toks[i - 1] if i else ""
        if prev in DETS or toks[i - 2:i - 1] == ["a"]:
            return {"n", "adj"}
        if prev in VERB_BEFORE:
            return {"v"}
        return None
    return None


def wikt_blocks(wd):
    """blocks as parse_mueller() makes them from Wiktionary's translations: a word
    Mueller hasn't got ("blog")"""
    out = []
    for pos, d in wd.items():
        groups = {}
        for sense, ru in d["tr"]:
            groups.setdefault(sense, []).append(ru)
        senses = [{"ru": [", ".join(dict.fromkeys(rus))], "phrases": [], "labels": set()}
                  for sense, rus in groups.items() if sense]
        if senses:
            out.append((pos, (), senses))
    return out


def make_article(word, blocks, levels, wikt, tat):
    wd = wikt.get(word, {})
    forms = set()
    for p in wd.values():
        forms |= p["forms"]
    for _, f, _ in blocks:
        forms |= set(f)
    pairs = tat.with_forms(word, forms, exact=bool(wd))
    # phrases met in Tatoeba
    phr, phrasal = {}, set()
    vforms = set(forms)
    for pos, _, senses in blocks:
        for s in senses:
            for en, ru, alts in s["phrases"]:
                ws = en.split()
                if len(ws) < 2 or len(ws) > 5 or not norm_head(en) or en in phr:
                    continue
                verb = (pos == "v" and ws[0] == word
                        and all(x in PARTICLES or x in OBJECTS for x in ws[1:])
                        and any(x in PARTICLES - WEAK_PARTICLES for x in ws[1:]))
                if pos == "v" and ws[0] == word and not verb and len(ws) == 2 and ws[1] in WEAK_PARTICLES:
                    continue
                if not verb and not any(form_of(word, x) or x in vforms for x in ws):
                    continue
                # the sub-meaning of the phrase its sentences have: "run out"
                # а) выбегать б) вытекать ... д) истощаться
                best = None
                for r in [ru] + alts:
                    meant, near = tat.phrase_hits(en, r, vforms)
                    if best is None or len(meant) > len(best[1]):
                        best = (r, meant, near)
                ru, meant, near = best
                # a phrasal verb's words in a row are the verb ("run out of"), an
                # idiom's may be meant literally ("borrow money on the house")
                nums = meant + near if verb else meant
                if verb:
                    phrasal |= set(nums)
                disp = ru_display([ru])
                if disp and len(nums) >= (MIN_PHRASAL if verb else MIN_IDIOM):
                    phr[en] = (verb, nums, disp)
    # a sentence of a phrasal verb ("get off the train") is no example of the word
    pairs -= phrasal
    ru_of = {n: ru_stems(" ".join(tat.pairs[n][1])) for n in pairs}

    allowed = set()
    for p in levels:
        allowed |= POS_MATCH.get(p, {p})
    cands = []   # (pos, forms, sense, stems, gloss, matched pairs, order)
    order = 0
    for pos, vforms, senses in blocks:
        for s in senses:
            order += 1
            if s["labels"] - OK_LABELS or not s["ru"]:
                continue
            disp = ru_display(s["ru"])
            if not disp:
                continue
            # the group's words are a meaning more: "дать" for "давать" ("дал")
            gloss, wstems = gloss_for(s["ru"], wd.get(pos, {}))
            cands.append({"pos": pos, "forms": vforms, "s": s, "ru": disp, "gloss": gloss,
                          "items": item_stems(s["ru"]) + [wstems], "hits": [], "sure": [], "order": order})
    if not cands:
        return None
    # each sentence goes to the meaning it matches best, the earlier one on a tie
    pos_forms = {p: d["forms"] for p, d in wd.items()}
    for pos, f, _ in blocks:
        pos_forms.setdefault(pos, set()).update(f)
    cand_pos = {c["pos"] for c in cands}
    for n in pairs:
        best, score = None, 0
        hint = pos_hint(tat.pairs[n][0], word, pos_forms)
        for c in cands:
            if hint and c["pos"] not in hint:
                continue
            sc = match_score(c["items"], ru_of[n])
            if sc > score:
                best, score = c, sc
        if best:
            best["hits"].append(n)
            if hint or not {"n", "v"} <= cand_pos:
                best["sure"].append(n)
    # the parts of speech of the word lists, and the others met as often
    top = max(len(c["hits"]) for c in cands)
    kept = [c for c in cands if c["pos"] in allowed or (c["hits"] and len(c["hits"]) * 4 >= top)]
    kept = kept or [c for c in cands if c["hits"]] or cands[:1]
    by_pos = {}
    for c in kept:
        by_pos.setdefault(c["pos"], []).append(c)
    for p in by_pos:
        by_pos[p].sort(key=lambda c: (-len(c["hits"]), c["order"]))

    def plevel(p):
        for lp, lv in levels.items():
            if p in POS_MATCH.get(lp, {lp}):
                return lv
        return min(levels.values(), key=LEVELS.index) if levels else ""
    first = {}
    for c in cands:
        first.setdefault(c["pos"], c["order"])
    pos_order = sorted(by_pos, key=lambda p: (LEVELS.index(plevel(p)) if plevel(p) else 9,
                                              -sum(len(c["hits"]) for c in by_pos[p]), first[p]))
    # the first meaning of every part of speech, then the most met ones
    chosen = [by_pos[p][0] for p in pos_order][:MAX_SENSES]
    rest = sorted((c for p in pos_order for c in by_pos[p][1:] if c["hits"]),
                  key=lambda c: -len(c["hits"]))
    for c in rest:
        if len(chosen) >= MAX_SENSES:
            break
        same = any(x["pos"] == c["pos"] and x["gloss"] and x["gloss"] == c["gloss"] for x in chosen)
        if len(c["hits"]) * 10 >= len(chosen[0]["hits"]) and not same:
            chosen.append(c)

    lines, used, num = [], set(), 0
    for p in pos_order:
        mine = [c for c in by_pos[p] if c in chosen]
        if not mine:
            continue
        forms_note = f" ({', '.join(mine[0]['forms'])})" if p == "v" and mine[0]["forms"] else ""
        lines.append(f"{p}.{forms_note} {plevel(p)}".rstrip())
        for c in mine:
            num += 1
            lines.append(f"{num}. {c['ru']}" + (f" - {c['gloss']}" if c["gloss"] else ""))
            # "He who chases two hares" is no example of the noun chase
            ex = pick_example(tat, c["hits"] if p == pos_order[0] else c["sure"], used)
            if not ex:
                ph = [x for x in c["s"]["phrases"] if len(x[0]) + len(x[1]) < EX_EN_MAX]
                if ph:
                    en, ru, _ = min(ph, key=lambda x: len(x[0]))
                    ex = f"{en} - {ru_display([ru]) or ru}"
            if ex:
                lines.append("   " + ex)

    verbs = sorted((x for x in phr.items() if x[1][0]), key=lambda x: -len(x[1][1]))[:MAX_PHRASAL]
    idioms = sorted((x for x in phr.items() if not x[1][0]), key=lambda x: -len(x[1][1]))[:MAX_IDIOMS]
    for en, (_, nums, ru) in verbs + idioms:
        lines.append(f"phr. {en} - {ru}")
        ex = pick_example(tat, nums, used)
        if ex:
            lines.append("   " + ex)
    return "\n".join(lines) if num else None


def main():
    args = sys.argv[1:]
    if len(args) < 3:
        sys.exit(__doc__)
    dl, old_lex, out = args[:3]
    only = set(args[3:])
    old = {norm_head(w): b for w, b in read_lexicon(old_lex)}
    levels, variants = read_levels(dl)
    for w, b in old.items():
        if w:
            for p, lv in old_levels(b).items():
                levels.setdefault(w, {}).setdefault(p, lv)
    words = sorted(w for w in levels if w and (not only or w in only))
    mueller = read_mueller(dl)
    print(f"words {len(words)}, Mueller {len(mueller)}", flush=True)
    wikt = wikt_slim(dl, set(mueller))
    print(f"Wiktionary {len(wikt)} words", flush=True)
    tat = Tatoeba(dl)
    print(f"Tatoeba {len(tat.pairs)} pairs", flush=True)

    arts, stats = {}, {"made": 0, "wikt": 0, "old": 0, "none": 0}
    for w in words:
        art = None
        # Mueller has "colour", not "color"; Wiktionary has new words: "blog"
        for v in [w] + variants.get(w, []):
            if v in mueller:
                art = make_article(v, parse_mueller(mueller[v]), levels[w], wikt, tat)
                if art:
                    stats["made"] += 1
                    break
        if not art:
            for v in [w] + variants.get(w, []):
                art = make_article(v, wikt_blocks(wikt.get(v, {})), levels[w], wikt, tat)
                if art:
                    stats["wikt"] += 1
                    break
        if art:
            pass
        elif w in old:
            art = old[w]
            stats["old"] += 1
        else:
            stats["none"] += 1
            continue
        # the first line of an article is its transcription: "/grɑːsp/"
        ipa = next((transcription(p["ipa"]) for v in [w] + variants.get(w, [])
                    for p in wikt.get(v, {}).values() if transcription(p["ipa"])), "")
        arts[w] = f"{ipa}\n{art}" if ipa else art
    os.makedirs(out, exist_ok=True)
    files = {}
    for w in sorted(arts):
        files.setdefault(w[0], []).append(f"@{w}\n{arts[w]}\n")
    for k, v in files.items():
        with open(os.path.join(out, f"{k}.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(v))
    print(f"{out}: {len(arts)} articles; from Mueller {stats['made']}, from Wiktionary "
          f"{stats['wikt']}, kept from the old lexicon {stats['old']}, dropped {stats['none']}")


if __name__ == "__main__":
    main()
