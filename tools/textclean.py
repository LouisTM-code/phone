"""Shared text cleanup: keeps only characters the phone font can draw."""
import html
import re

REPLACE = {
    "—": "-", "–": "-", "‒": "-", "−": "-", "‐": "-", "‑": "-",
    "‘": "'", "’": "'", "‚": "'", "′": "'", "´": "'", "`": "'",
    "“": '"', "”": '"', "„": '"', "«": '"', "»": '"', "″": '"',
    "…": "...", " ": " ", " ": " ", " ": " ", " ": " ", " ": " ",
    "­": "", "​": "", "﻿": "", "•": "*", "·": "*", "→": "->",
    "é": "e", "è": "e", "ê": "e", "ë": "e", "à": "a", "â": "a",
    "ä": "a", "á": "a", "î": "i", "ï": "i", "í": "i", "ô": "o",
    "ö": "o", "ó": "o", "û": "u", "ü": "u", "ú": "u", "ç": "c",
    "ñ": "n", "æ": "ae", "œ": "oe", "ß": "ss", "É": "E", "À": "A",
    "№": "N", "§": "S", "°": "o", "×": "x",
}

TAG_RE = re.compile(r"<[^>]*>")


def font_safe(s):
    """Maps text to ASCII + Cyrillic, which is all the phone font has."""
    out = []
    for ch in s:
        o = ord(ch)
        if ch == "\n" or 0x20 <= o < 0x7f:
            out.append(ch)
        elif 0x410 <= o <= 0x44f or ch in "Ёё":
            out.append(ch)
        elif ch == "\t":
            out.append(" ")
        elif ch in REPLACE:
            out.append(REPLACE[ch])
        # everything else (IPA, accents marks, emoji) is dropped
    return "".join(out)


def strip_html(s):
    s = re.sub(r"(?i)<br\s*/?>|</p>|</div>|</li>", "\n", s)
    s = TAG_RE.sub("", s)
    return html.unescape(s)


def tidy(s, max_len=None, keep_indent=False):
    s = font_safe(s)
    if keep_indent:
        # indented lines (examples) keep a two-space indent
        s = re.sub(r" +\n", "\n", s)
        s = re.sub(r"\n {2,}", "\n\x01", s)
        s = re.sub(r"[ ]{2,}", " ", s)
        s = re.sub(r"\n +", "\n", s).replace("\x01", "  ")
    else:
        s = re.sub(r"[ ]{2,}", " ", s)
        s = re.sub(r" *\n *", "\n", s)
    s = re.sub(r"\n{3,}", "\n\n", s).strip()
    if max_len and len(s) > max_len:
        s = s[:max_len].rsplit(" ", 1)[0] + "..."
    return s
