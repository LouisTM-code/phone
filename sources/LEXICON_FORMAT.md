# Learner dictionary format (English -> Russian)

For a Russian speaker at level A2-B2 reading English on a small phone screen
(29 characters per line). Plain UTF-8 text.

## Allowed characters

Only ASCII (letters, digits, punctuation) and Russian Cyrillic letters (including ё).
NO IPA/transcription, NO curly quotes, NO em/en dashes (use "-"), NO emoji,
NO accented Latin letters (write "cafe"), NO tabs.

## Entry layout

```
@house
n. A1
1. дом - a building where people live
   We live in a small house. - Мы живём в маленьком доме.
2. семья, домочадцы - the people in a house
   The whole house was asleep.
v. B2
3. размещать, вмещать - to give space to
   The museum houses old paintings.

@run
v. (ran, run) A1
1. бегать, бежать - move fast on your legs
   She runs every morning. - Она бегает каждое утро.
2. управлять, руководить - manage a business
   He runs a small cafe.
3. работать (о машине) - operate, work
   The engine is running.
n. B1
4. пробежка - a time when you run
   I went for a run in the park.
phr. run out (of) - закончиться, иссякнуть
   We ran out of milk.
```

Rules:
- `@headword` starts an entry: lowercase, the base form (go, not went).
- Each part of speech starts a line: `n.`, `v.`, `adj.`, `adv.`, `prep.`, `conj.`, `pron.`,
  `det.`, `num.`, `excl.`, `modal v.`, `phr. v.`. After it, for irregular verbs the forms in
  brackets `(went, gone)`, irregular plurals `(pl. children)`, then the CEFR level A1/A2/B1/B2/C1.
- Senses are numbered across the entry: `1. русский перевод - short simple English meaning`.
  The Russian part says WHICH meaning (add a clarifying word in brackets when the
  translation alone is ambiguous: `кран (водопроводный)`).
- Under most senses, an indented (3 spaces) example sentence. Give the Russian translation
  of the example for A1-A2 words or when the meaning is tricky; for others English only is fine.
- 1-4 senses per word: the common ones a B2 learner meets in books. Order by frequency.
- Useful phrasal verbs / fixed phrases go inside the main word as `phr. ...` lines
  (e.g. under "give": `phr. give up - сдаваться; бросать (привычку)`).
- Keep each entry under ~600 characters. Separate entries by one blank line.
- Typical CEFR levels: follow the Oxford 3000/5000 lists as closely as you know them.
