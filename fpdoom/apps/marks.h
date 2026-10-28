#ifndef MARKS_H
#define MARKS_H

#include "book.h"

/* bookmarks and quotes of a book, state/<id>.txt on the card */
#define MAX_MARKS 200
typedef struct {
	char kind;  /* 'b' bookmark, 'q' quote */
	book_anchor_t pos;
	char *text;
} mark_t;
typedef struct {
	mark_t list[MAX_MARKS];
	int n;
} marks_t;

void marks_load(marks_t *m, const char *id);
int marks_save(marks_t *m, const char *id);
void marks_free(marks_t *m);
/* adds in book order, returns the index or -1 */
int marks_add(marks_t *m, int kind, book_anchor_t pos, const char *text);
void marks_remove(marks_t *m, int i);

#endif
