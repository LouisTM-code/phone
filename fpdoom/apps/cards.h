#ifndef CARDS_H
#define CARDS_H

/* flashcards with Anki-like spaced repetition, stored in cards.txt */
int cards_load(void);
int cards_save(void);
/* returns 1 added, 0 already there, -1 error */
int cards_add(const char *front, const char *back);
int cards_count(void);
/* adds all new words of a set file ("word<TAB>meaning" lines), returns how many */
int cards_import(const char *path);

#endif
