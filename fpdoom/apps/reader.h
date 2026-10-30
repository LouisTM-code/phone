#ifndef READER_H
#define READER_H

#include "book.h"

/* a book on the shelf: books/index.txt and its place in state/<id>.pos */
typedef struct {
	char file[72], title[96], author[64], cat[32], id[12];
	book_anchor_t pos;
	int percent, status, stamp;  /* stamp: order of opening, the latest is the largest */
	int deleted;  /* marked for deletion: make update-data removes the book from the card */
} shelf_book_t;
enum { ST_NEW, ST_READING, ST_DONE };

/* shelf.c */
/* writes the place of one book: a small file each, a failed write hurts one book */
void shelf_save(shelf_book_t *k);
int shelf_stamp(void);
/* the title of the book read last, NULL if none is being read */
const char *shelf_reading(void);

/* reader.c: sets the page area, theme and dictionary */
void reader_init(void);
/* reads the book from its saved place; -1 on quit */
int read_book(shelf_book_t *k);

#endif
