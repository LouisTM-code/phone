#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "apps.h"
#include "book.h"
#include "dictfile.h"
#include "log.h"

#define INDEX_FILE "books/index.txt"
/* state/ is made by make sdcard: the phone can't create folders */
#define SHELF_FILE "state/books.txt"
#define DICT_FILE "dict.bin"
#define MAX_BOOKS 200
#define MAX_PAGE_WORDS 400
#define SAVE_IDLE_MS 3000

int dict_show(int i, const char *title);

/* books/index.txt: file, title, author, category, state id */
typedef struct {
	char file[72], title[96], author[64], cat[32], id[12];
	/* from state/books.txt */
	book_anchor_t pos;
	int percent, status, stamp;
} shelf_book_t;
enum { ST_NEW, ST_READING, ST_DONE };

static shelf_book_t *books;
static int nbooks, last_stamp;

static int page_x, page_y, page_w, page_h;

/* words and pictures on the current page, in reading order */
typedef struct {
	int16_t x, y, w, h;
	uint16_t off, len;   /* bytes in the page */
	uint8_t chars, style, hyphen;
	uint32_t img;        /* a picture: the one shown full screen, else BK_NONE */
} word_t;
static word_t words[MAX_PAGE_WORDS];
static int nwords;

static char *field(char **p) {
	char *s = *p, *t;
	if (!s) return "";
	if ((t = strchr(s, '\t'))) *t = 0, *p = t + 1;
	else *p = NULL;
	return s;
}

static int load_index(void) {
	char *buf = file_load(INDEX_FILE, NULL), *pos, *line;
	nbooks = 0;
	if (!buf) return -1;
	if (!books && !(books = malloc(MAX_BOOKS * sizeof(*books)))) {
		free(buf);
		return -1;
	}
	for (pos = buf; nbooks < MAX_BOOKS && (line = next_line(&pos)); ) {
		shelf_book_t *k = &books[nbooks];
		memset(k, 0, sizeof(*k));
		snprintf(k->file, sizeof(k->file), "%s", field(&line));
		snprintf(k->title, sizeof(k->title), "%s", field(&line));
		snprintf(k->author, sizeof(k->author), "%s", field(&line));
		snprintf(k->cat, sizeof(k->cat), "%s", field(&line));
		snprintf(k->id, sizeof(k->id), "%s", field(&line));
		if (k->file[0] && k->id[0]) nbooks++;
	}
	free(buf);
	return 0;
}

/* state/books.txt: "id<TAB>block<TAB>char<TAB>percent<TAB>status<TAB>stamp" */
static void load_shelf(void) {
	char *buf = file_load(SHELF_FILE, NULL), *pos, *line;
	last_stamp = 0;
	if (!buf) return;
	for (pos = buf; (line = next_line(&pos)); ) {
		char *id = field(&line);
		int i;
		for (i = 0; i < nbooks; i++) {
			shelf_book_t *k = &books[i];
			if (strcmp(k->id, id)) continue;
			k->pos.blk = strtoul(field(&line), NULL, 10);
			k->pos.ch = strtoul(field(&line), NULL, 10);
			k->percent = atoi(field(&line));
			k->status = atoi(field(&line));
			k->stamp = atoi(field(&line));
			if (k->stamp > last_stamp) last_stamp = k->stamp;
			break;
		}
	}
	free(buf);
}

static void save_shelf(void) {
	FILE *fp = fopen(SHELF_FILE, "wb");
	int i;
	LOG_T0(t0);
	if (!fp) return;
	for (i = 0; i < nbooks; i++) {
		shelf_book_t *k = &books[i];
		if (k->status == ST_NEW && !k->stamp) continue;
		fprintf(fp, "%s\t%u\t%u\t%d\t%d\t%d\n", k->id, (unsigned)k->pos.blk, (unsigned)k->pos.ch,
				k->percent, k->status, k->stamp);
	}
	fclose(fp);
	LOG_OP(t0, "shelf_save %d", nbooks);
}

/* ---- the page */

static unsigned utf8_at(const uint8_t *s, int *len) {
	const char *p = (const char*)s;
	unsigned c = utf8_next(&p);
	*len = p - (const char*)s;
	return c;
}

static int is_letter(unsigned c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= 0x410 && c < 0x450) || c == 0x401 || c == 0x451 ||
		(c >= 0xc0 && c < 0x250 && c != 0xd7 && c != 0xf7);
}

static void collect_words(book_t *b) {
	const uint8_t *p = b->page, *end = p + b->page_size, *it;
	int t;
	nwords = 0;
	while (it = p, (t = book_item(&p, end)) != BK_END) {
		int y, flags, n, last = -1;
		if (t == BK_IMAGE && nwords < MAX_PAGE_WORDS) {
			word_t *w = &words[nwords];
			int iw, ih;
			uint32_t full = bk_u32(it + 9);
			if (book_image_size(b, bk_u32(it + 5), &iw, &ih)) continue;
			memset(w, 0, sizeof(*w));
			w->x = bk_s16(it + 1);
			w->y = bk_s16(it + 3);
			w->w = iw;
			w->h = ih;
			w->img = full != BK_NONE ? full : bk_u32(it + 5);
			nwords++;
			continue;
		}
		if (t != BK_TEXT) continue;
		y = bk_s16(it + 1);
		flags = it[3];
		n = it[4];
		it += 5;
		while (n--) {
			int x = bk_s16(it), st = it[2], len = it[3], k = 0, col = 0;
			int cw = FONT_W * (st & TEXT_BIG ? 2 : 1);
			const uint8_t *s = it + 4;
			while (k < len) {
				int l, l2;
				unsigned c = utf8_at(s + k, &l);
				if (is_letter(c) && nwords < MAX_PAGE_WORDS) {
					word_t *w = &words[nwords++];
					w->x = x + col * cw;
					w->y = y;
					w->off = s + k - b->page;
					w->style = st;
					w->chars = 0;
					w->hyphen = 0;
					w->img = BK_NONE;
					while (k < len) {
						c = utf8_at(s + k, &l);
						if (!is_letter(c)) {
							/* an apostrophe or a hyphen inside a word */
							if ((c != '\'' && c != '-' && c != 0x2019) || k + l >= len ||
									!is_letter(utf8_at(s + k + l, &l2))) break;
						}
						k += l;
						col++;
						w->chars++;
					}
					w->len = s + k - b->page - w->off;
					w->w = w->chars * cw;
					w->h = FONT_H * (cw / FONT_W);
					last = nwords - 1;
				} else {
					k += l;
					col++;
				}
			}
			it += 4 + len;
		}
		if (last >= 0 && flags & BK_LINE_HYPHEN) words[last].hyphen = 1;
	}
}

static void draw_status(book_t *b, int page) {
	char num[24];
	int w, max, n = 0;
	const char *p = b->title;
	fill_rect(0, 0, scr_w, UI_BAR_H, UI_BAR);
	snprintf(num, sizeof(num), "%d/%u", page + 1, b->npages);
	w = text_len(num);
	max = (scr_w - 8) / FONT_W - w - 1;
	while (*p && n < max) { utf8_next(&p); n++; }
	draw_text_n(4, 3, 1, b->title, p - b->title, UI_TEXT);
	draw_text(scr_w - 4 - w * FONT_W, 3, 1, num, UI_DIM);
	fill_rect(0, UI_BAR_H - 2, scr_w * (page + 1) / b->npages, 2, UI_ACCENT);
}

/* PDF: the page is a picture larger than the screen, the view moves over it */
static uint32_t pan_image(book_t *b) {
	const uint8_t *p = b->page, *end = p + b->page_size, *it;
	while (it = p, book_item(&p, end) != BK_END)
		if (*it == BK_PAN) return bk_u32(it + 1);
	return BK_NONE;
}

static void frame(int x, int y, int w, int h, uint16_t c) {
	fill_rect(x, y, w, 2, c);
	fill_rect(x, y + h - 2, w, 2, c);
	fill_rect(x, y, 2, h, c);
	fill_rect(x + w - 2, y, 2, h, c);
}

static void draw_page(book_t *b, int page, int sel, int pan_x, int pan_y) {
	fill_rect(0, UI_BAR_H, scr_w, scr_h - UI_BAR_H, book_bg);
	if (b->flags & BK_FLAG_PAN) {
		uint32_t id = pan_image(b);
		if (id != BK_NONE)
			book_image(b, id, 0, UI_BAR_H, pan_x, pan_y, scr_w, scr_h - UI_BAR_H, 0, 1);
	} else {
		book_draw(b, page_x, page_y);
	}
	if (sel >= 0 && sel < nwords) {
		word_t *w = &words[sel];
		if (w->img != BK_NONE) {
			frame(page_x + w->x - 2, page_y + w->y - 2, w->w + 4, w->h + 4, UI_SEL);
		} else {
			fill_rect(page_x + w->x - 1, page_y + w->y - 1, w->w + 2, w->h + 2, UI_SEL);
			draw_text_style(page_x + w->x, page_y + w->y, w->style & 7,
					(const char*)b->page + w->off, w->len, UI_TEXT);
		}
	}
	draw_status(b, page);
	gfx_present();
}

/* a picture full screen: arrows move it, 5 changes the scale x1, x2, 1/2,
   RSOFT closes */
static int view_image(book_t *b, uint32_t id) {
	static const int zooms[] = { 1, 2, -2 };
	static const char *const names[] = { "", "  x2", "  1/2" };
	int w, h, x = 0, y = 0, z = 0;
	if (book_image_size(b, id, &w, &h)) return 0;
	for (;;) {
		int zoom = zooms[z], k = zoom > 0 ? zoom : 1, q = zoom < 0 ? -zoom : 1;
		int vw = scr_w, vh = scr_h - UI_BAR_H * 2, zw = w * k / q, zh = h * k / q, key;
		int dx = zw < vw ? (vw - zw) / 2 : 0, dy = zh < vh ? (vh - zh) / 2 : 0;
		char title[48];
		if (x > zw - vw) x = zw - vw;
		if (y > zh - vh) y = zh - vh;
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		snprintf(title, sizeof(title), "Картинка %dx%d%s", w, h, names[z]);
		ui_header(title);
		fill_rect(0, UI_BAR_H, scr_w, vh, book_bg);
		book_image(b, id, dx, UI_BAR_H + dy, x * q / k, y * q / k, vw * q / k + 1, vh * q / k + 1,
				book_pal[BC_TEXT], zoom);
		ui_footer("5 масштаб  <> двигать  П-назад");
		gfx_present();
		key = wait_key();
		if (key < 0) return -1;
		if (key == KEYPAD_RSOFT) return 0;
		if (key == KEYPAD_RIGHT || key == KEYPAD_6) x += vw / 2;
		else if (key == KEYPAD_LEFT || key == KEYPAD_4) x -= vw / 2;
		else if (key == KEYPAD_DOWN || key == KEYPAD_8) y += vh / 2;
		else if (key == KEYPAD_UP || key == KEYPAD_2) y -= vh / 2;
		else if (key == KEYPAD_CENTER || key == KEYPAD_5) {
			/* keep the middle of the view in place */
			int z2 = (z + 1) % 3, k2 = zooms[z2] > 0 ? zooms[z2] : 1, q2 = zooms[z2] < 0 ? -zooms[z2] : 1;
			x = (x + vw / 2) * q / k * k2 / q2 - vw / 2;
			y = (y + vh / 2) * q / k * k2 / q2 - vh / 2;
			z = z2;
		}
	}
}

/* the nearest word on the next line up or down */
static int word_vertical(int cur, int dir) {
	int i, best = -1, by = 0, bd = 0;
	for (i = 0; i < nwords; i++) {
		int dy = (words[i].y - words[cur].y) * dir, d;
		if (dy <= 0) continue;
		d = words[i].x - words[cur].x;
		if (d < 0) d = -d;
		if (best < 0 || dy < by || (dy == by && d < bd)) {
			best = i;
			by = dy;
			bd = d;
		}
	}
	return best;
}

static int lookup_word(book_t *b, int sel) {
	char word[64], base[64], title[200];
	word_t *w = &words[sel];
	int len = w->len, i;
	if (w->img != BK_NONE) return view_image(b, w->img);
	if (len > 40) len = 40;
	memcpy(word, b->page + w->off, len);
	word[len] = 0;
	/* a word broken with a hyphen goes on at the start of the next line */
	if (w->hyphen && sel + 1 < nwords && words[sel + 1].img == BK_NONE) {
		int l2 = words[sel + 1].len;
		if (l2 > 62 - len) l2 = 62 - len;
		memcpy(word + len, b->page + words[sel + 1].off, l2);
		word[len + l2] = 0;
	}
	if ((unsigned char)word[0] >= 0x80) {
		ui_note("Перевод только с английского");
		return 0;
	}
	i = dict_lookup(word, base, sizeof(base));
	if (i < 0) {
		snprintf(title, sizeof(title), "Слова \"%s\" нет в словаре.", word);
		return ui_message("Не найдено", title);
	}
	/* "went -> go" when the word is a form of the headword */
	{
		int k;
		char low[64];
		for (k = 0; word[k]; k++) low[k] = word[k] >= 'A' && word[k] <= 'Z' ? word[k] + 32 : word[k];
		low[k] = 0;
		if (strcmp(low, base)) snprintf(title, sizeof(title), "%s -> %s", word, base);
		else snprintf(title, sizeof(title), "%s", base);
	}
	return dict_show(i, title);
}

static void book_progress(shelf_book_t *k, book_t *b, int page) {
	k->pos = book_anchor(b, page);
	k->percent = b->npages > 1 ? page * 100 / (b->npages - 1) : 100;
	if (page + 1 >= (int)b->npages) k->status = ST_DONE;
	else if (k->status == ST_NEW) k->status = ST_READING;
}

/* PDF pages: RIGHT moves the view right, then to the next row and the next
   page, like reading; LEFT goes back the same way, UP and DOWN scroll.
   Returns the new page; *x, *y = -1 mean the end of the page. */
static int pan_key(book_t *b, int key, int page, int *x, int *y) {
	int w = 0, h = 0, vw = scr_w, vh = scr_h - UI_BAR_H, mx, my, step = 24;
	uint32_t id = pan_image(b);
	if (id != BK_NONE) book_image_size(b, id, &w, &h);
	mx = w > vw ? w - vw : 0;
	my = h > vh ? h - vh : 0;
	if (key == KEYPAD_RIGHT || key == KEYPAD_6) {
		if (*x < mx) *x = *x + vw - step < mx ? *x + vw - step : mx;
		else if (*y < my) { *x = 0; *y = *y + vh - step < my ? *y + vh - step : my; }
		else if (page + 1 < (int)b->npages) { page++; *x = *y = 0; }
	} else if (key == KEYPAD_LEFT || key == KEYPAD_4) {
		if (*x > 0) *x = *x - (vw - step) > 0 ? *x - (vw - step) : 0;
		else if (*y > 0) { *x = mx; *y = *y - (vh - step) > 0 ? *y - (vh - step) : 0; }
		else if (page) { page--; *x = *y = -1; }
	} else if (key == KEYPAD_DOWN || key == KEYPAD_8) {
		*y = *y + vh / 2 < my ? *y + vh / 2 : my;
	} else if (key == KEYPAD_UP || key == KEYPAD_2) {
		*y = *y > vh / 2 ? *y - vh / 2 : 0;
	} else if (key == KEYPAD_3) {
		page = page + 10 < (int)b->npages ? page + 10 : (int)b->npages - 1;
		*x = *y = 0;
	} else if (key == KEYPAD_1) {
		page = page > 10 ? page - 10 : 0;
		*x = *y = 0;
	}
	return page;
}

static int read_book(shelf_book_t *k) {
	book_t bk, *b = &bk;
	int page, sel = -1, ret = 0, dirty = 0, pan_x = 0, pan_y = 0;
	LOG_T0(t0);

	if (book_open(b, k->file) < 0) return ui_message("Книги", "Не удалось открыть книгу.");
	if ((int)b->page_w != page_w || (int)b->page_h != page_h) {
		book_close(b);
		return ui_message("Книги", "Книга собрана для другого экрана: пересоберите её (make data).");
	}
	page = k->stamp ? book_find(b, k->pos) : 0;
	k->stamp = ++last_stamp;
	if (k->status == ST_NEW) k->status = ST_READING;
	save_shelf();
	LOG_OP(t0, "book_open %s %up", k->file, b->npages);

	for (;;) {
		int key, old = page;
		if (book_load(b, page) < 0) {
			ret = ui_message("Книги", "Ошибка чтения книги.");
			break;
		}
		collect_words(b);
		if (sel >= MAX_PAGE_WORDS) sel = nwords - 1;
		if (sel >= nwords) sel = nwords ? 0 : -1;
		if (pan_x < 0) {  /* came back from the next page: show its end */
			int w = 0, h = 0;
			if (pan_image(b) != BK_NONE) book_image_size(b, pan_image(b), &w, &h);
			pan_x = w > (int)scr_w ? w - (int)scr_w : 0;
			pan_y = h > (int)scr_h - UI_BAR_H ? h - ((int)scr_h - UI_BAR_H) : 0;
		}
		draw_page(b, page, sel, pan_x, pan_y);
		key = wait_key_ms(dirty ? SAVE_IDLE_MS : -1);
		if (key < 0) { ret = -1; break; }
		if (!key) {
			save_shelf();
			dirty = 0;
			continue;
		}
		if (b->flags & BK_FLAG_PAN) {
			if (key == KEYPAD_RSOFT) break;
			if (key == KEYPAD_CENTER || key == KEYPAD_5) {
				if (pan_image(b) != BK_NONE && view_image(b, pan_image(b)) < 0) { ret = -1; break; }
			} else {
				page = pan_key(b, key, page, &pan_x, &pan_y);
			}
		} else if (sel < 0) {
			if (key == KEYPAD_RIGHT || key == KEYPAD_6 || key == KEYPAD_DOWN || key == KEYPAD_8) {
				if (page + 1 < (int)b->npages) page++;
			} else if (key == KEYPAD_LEFT || key == KEYPAD_4 || key == KEYPAD_UP || key == KEYPAD_2) {
				if (page) page--;
			} else if (key == KEYPAD_3) {
				page = page + 10 < (int)b->npages ? page + 10 : (int)b->npages - 1;
			} else if (key == KEYPAD_1) {
				page = page > 10 ? page - 10 : 0;
			} else if (key == KEYPAD_CENTER || key == KEYPAD_5) {
				if (nwords) sel = 0;
			} else if (key == KEYPAD_RSOFT) {
				break;
			}
		} else {
			int t;
			if (key == KEYPAD_RIGHT || key == KEYPAD_6) {
				if (sel + 1 < nwords) sel++;
				else if (page + 1 < (int)b->npages) { page++; sel = 0; }
			} else if (key == KEYPAD_LEFT || key == KEYPAD_4) {
				if (sel > 0) sel--;
				else if (page) { page--; sel = MAX_PAGE_WORDS; }
			} else if (key == KEYPAD_DOWN || key == KEYPAD_8) {
				if ((t = word_vertical(sel, 1)) >= 0) sel = t;
			} else if (key == KEYPAD_UP || key == KEYPAD_2) {
				if ((t = word_vertical(sel, -1)) >= 0) sel = t;
			} else if (key == KEYPAD_CENTER || key == KEYPAD_5) {
				if (lookup_word(b, sel) < 0) { ret = -1; break; }
			} else if (key == KEYPAD_RSOFT) {
				sel = -1;
			}
		}
		if (page != old) {
			book_progress(k, b, page);
			dirty = 1;
		}
	}
	book_progress(k, b, page);
	save_shelf();
	book_close(b);
	return ret;
}

static void set_theme(void) {
	book_bg = RGB(24, 26, 34);
	book_pal[BC_TEXT] = RGB(230, 230, 220);
	book_pal[BC_DIM] = RGB(150, 160, 180);
	book_pal[BC_HEAD] = RGB(130, 190, 255);
	book_pal[BC_TITLE] = RGB(255, 210, 90);
	book_pal[BC_CODE] = RGB(150, 230, 150);
	book_pal[BC_CODE_BG] = RGB(40, 44, 56);
	book_pal[BC_QUOTE] = RGB(120, 130, 170);
	book_pal[BC_RULE] = RGB(90, 100, 130);
	book_pal[BC_TH_BG] = RGB(44, 52, 76);
}

int reader_main(void) {
	const char **items;
	int i, sel = 0;

	page_x = 4;
	page_y = UI_BAR_H + 3;
	page_w = scr_w - 8;
	page_h = scr_h - page_y - 3;
	set_theme();
	dict_open(DICT_FILE);

	if (load_index() < 0 || !nbooks)
		return ui_message("Книги", "Книг пока нет.\n\n"
				"Положите книгу (.epub, .fb2, .md, .txt, .pdf) в sources/books/ "
				"и выполните make update.");
	load_shelf();
	if (!(items = malloc(nbooks * sizeof(*items)))) return 0;
	for (;;) {
		int r;
		for (i = 0; i < nbooks; i++) items[i] = books[i].title;
		r = ui_pick("Книги", items, nbooks, sel);
		if (r == -2) { free(items); return -1; }
		if (r < 0) break;
		sel = r;
		if (read_book(&books[r]) < 0) { free(items); return -1; }
	}
	free(items);
	return 0;
}
