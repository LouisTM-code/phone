#ifndef CARDS_H
#define CARDS_H

/* flashcards with Anki-like spaced repetition, stored in cards.txt; new words
   come from the decks (decks/index.txt) that are on and from "my" ones */
int cards_load(void);
int cards_save(void);
/* a word of "my" deck; ex: an example sentence or NULL; returns 1 added,
   0 already there (now in "my" too), -1 error */
int cards_add(const char *front, const char *back, const char *ex);
/* the same without saving, for several cards before one cards_save() */
int cards_put(const char *front, const char *back, const char *ex);
int cards_count(void);
/* today's cards to review and new words to learn, as the Учить line counts them; -1 on error */
int cards_today(int *nreview, int *nnew);
/* 1 when there is a card for the word */
int cards_has(const char *front);
/* where a form of word ("runs", "making" for run, make) stands in text: its byte
   offset and *len, -1 if none; a phrase may have its first word in any form and
   a few words between its words: "gave it up" for "give up" (tools/mkdecks.py
   picks examples the same way) */
int cards_cloze(const char *word, const char *text, int *len);

#endif
