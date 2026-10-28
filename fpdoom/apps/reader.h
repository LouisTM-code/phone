#ifndef READER_H
#define READER_H

#include "book.h"

/* a book on the shelf: books/index.txt and its state in state/books.txt */
typedef struct {
	char file[72], title[96], author[64], cat[32], id[12];
	book_anchor_t pos;
	int percent, status, stamp;  /* stamp: order of opening, the latest is the largest */
} shelf_book_t;
enum { ST_NEW, ST_READING, ST_DONE };

/* shelf.c */
void shelf_save(void);
int shelf_stamp(void);

/* reader.c: sets the page area, theme and dictionary */
void reader_init(void);
/* reads the book from its saved place; -1 on quit */
int read_book(shelf_book_t *k);

#endif
