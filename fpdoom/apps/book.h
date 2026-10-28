#ifndef BOOK_H
#define BOOK_H

#include <stdio.h>
#include <stdint.h>

/* A book built by tools/mkbook.py (format: tools/bookfile.py). Only the
   header is kept in memory; pages and pictures are read from the card when
   drawn. */

enum { BK_END, BK_TEXT, BK_RECT, BK_IMAGE, BK_PAN, BK_SEARCH };
#define BK_LINE_HYPHEN 1
#define BK_LINE_REPEAT 2  /* a repeated table header, not searched */
#define BK_FLAG_PAN 1
#define BK_NONE 0xffffffffu
enum { BK_IMG_RGB = 1, BK_IMG_MASK = 2 };
/* colours of the items, mapped to the theme by book_pal */
enum { BC_TEXT, BC_DIM, BC_HEAD, BC_TITLE, BC_CODE, BC_CODE_BG, BC_QUOTE, BC_RULE,
	BC_TH_BG, BC_COUNT };

typedef struct {
	FILE *f;
	unsigned npages, flags, page_w, page_h;
	uint32_t pages_off, toc_off, images_off, cover;
	char title[128], author[128];
	uint8_t *page;         /* the loaded page */
	unsigned page_size, page_cap;
} book_t;

typedef struct { uint32_t blk, ch; } book_anchor_t;

extern uint16_t book_pal[BC_COUNT];
extern uint16_t book_bg;

int book_open(book_t *b, const char *path);
void book_close(book_t *b);
/* reads a page into b->page; returns its size or -1 */
int book_load(book_t *b, unsigned page);
book_anchor_t book_anchor(book_t *b, unsigned page);
/* the page that holds the anchor */
unsigned book_find(book_t *b, book_anchor_t a);

/* item walk over b->page: returns the item type, *p moves past the item */
int book_item(const uint8_t **p, const uint8_t *end);
static inline unsigned bk_u16(const uint8_t *p) { return p[0] | p[1] << 8; }
static inline int bk_s16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static inline uint32_t bk_u32(const uint8_t *p) {
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

/* draws the loaded page with its top left corner at x, y */
void book_draw(book_t *b, int x, int y);

int book_image_size(book_t *b, uint32_t id, int *w, int *h);
/* draws the part of a picture at sx, sy of size w x h to x, y on the screen,
   masks in the colour c; zoom 1, 2 (twice larger) or -2 (half) */
void book_image(book_t *b, uint32_t id, int x, int y, int sx, int sy, int w, int h,
		uint16_t c, int zoom);

/* contents: read whole when shown, free() the result; entries are
   u32 page, u8 level, u8 nbytes, title */
uint8_t *book_toc(book_t *b, unsigned *count);

#endif
