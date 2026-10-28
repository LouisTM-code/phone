#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "apps.h"
#include "reader.h"
#include "log.h"

#define INDEX_FILE "books/index.txt"
/* covers in the order of index.txt, raw RGB565 (tools/mkbook.py) */
#define COVERS_FILE "books/covers.bin"
#define THUMB_W 36
#define THUMB_H 48
/* state/ is made by make sdcard: the phone can't create folders */
#define SHELF_FILE "state/books.txt"
#define MAX_BOOKS 200
#define MAX_CATS 16
#define ROW_H (THUMB_H + 6)

static shelf_book_t *books;
static int nbooks, last_stamp;
static char cats[MAX_CATS][32];
static int ncats;

/* the books shown under the current filter */
static int view[MAX_BOOKS], nview;
enum { F_READING, F_ALL, F_DONE, F_CAT };

static char *field(char **p) {
	char *s = *p, *t;
	if (!s) return "";
	if ((t = strchr(s, '\t'))) *t = 0, *p = t + 1;
	else *p = NULL;
	return s;
}

/* books/index.txt: file, title, author, category, state id */
static int load_index(void) {
	char *buf = file_load(INDEX_FILE, NULL), *pos, *line;
	nbooks = ncats = 0;
	if (!buf) return -1;
	if (!books && !(books = malloc(MAX_BOOKS * sizeof(*books)))) {
		free(buf);
		return -1;
	}
	for (pos = buf; nbooks < MAX_BOOKS && (line = next_line(&pos)); ) {
		shelf_book_t *k = &books[nbooks];
		int i;
		memset(k, 0, sizeof(*k));
		snprintf(k->file, sizeof(k->file), "%s", field(&line));
		snprintf(k->title, sizeof(k->title), "%s", field(&line));
		snprintf(k->author, sizeof(k->author), "%s", field(&line));
		snprintf(k->cat, sizeof(k->cat), "%s", field(&line));
		snprintf(k->id, sizeof(k->id), "%s", field(&line));
		if (!k->file[0] || !k->id[0]) continue;
		nbooks++;
		if (!k->cat[0]) continue;
		for (i = 0; i < ncats && strcmp(cats[i], k->cat); i++);
		if (i == ncats && ncats < MAX_CATS) strcpy(cats[ncats++], k->cat);
	}
	free(buf);
	return 0;
}

/* state/books.txt: "id<TAB>block<TAB>char<TAB>percent<TAB>status<TAB>stamp" */
static void load_state(void) {
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

void shelf_save(void) {
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

int shelf_stamp(void) { return ++last_stamp; }

static const char *filter_name(int f) {
	static const char *const names[] = { "Читаю", "Все книги", "Прочитано" };
	return f < F_CAT ? names[f] : cats[f - F_CAT];
}

static void build_view(int f) {
	int i, j;
	nview = 0;
	for (i = 0; i < nbooks; i++) {
		shelf_book_t *k = &books[i];
		if (f == F_ALL || (f == F_READING && k->status == ST_READING) ||
				(f == F_DONE && k->status == ST_DONE) ||
				(f >= F_CAT && !strcmp(k->cat, cats[f - F_CAT])))
			view[nview++] = i;
	}
	/* reading and read books: the latest first */
	if (f == F_READING || f == F_DONE)
		for (i = 1; i < nview; i++)
			for (j = i; j > 0 && books[view[j]].stamp > books[view[j - 1]].stamp; j--) {
				int t = view[j];
				view[j] = view[j - 1];
				view[j - 1] = t;
			}
}

static void draw_cover(FILE *covers, int i, int x, int y) {
	static uint16_t px[THUMB_W * THUMB_H];
	int j;
	if (!covers || fseek(covers, (long)i * sizeof(px), SEEK_SET) ||
			fread(px, 1, sizeof(px), covers) != sizeof(px)) {
		fill_rect(x, y, THUMB_W, THUMB_H, UI_BAR);
		return;
	}
	for (j = 0; j < THUMB_H; j++)
		memcpy(framebuf + (y + j) * scr_w + x, px + j * THUMB_W, THUMB_W * 2);
}

static void draw_row(FILE *covers, int i, int y, int selected) {
	shelf_book_t *k = &books[i];
	static int starts[2], lens[2];
	int tx = THUMB_W + 10, cols = (scr_w - tx - 4) / FONT_W, n, l;
	char info[64];
	if (selected) fill_rect(0, y, scr_w, ROW_H, UI_SEL);
	draw_cover(covers, i, 4, y + 3);
	n = wrap_text(k->title, cols, starts, lens, 2);
	for (l = 0; l < n; l++)
		draw_text_n(tx, y + 2 + l * FONT_H, 1, k->title + starts[l], lens[l], UI_TEXT);
	if (k->status == ST_DONE) snprintf(info, sizeof(info), "прочитано");
	else if (k->status == ST_READING) snprintf(info, sizeof(info), "%d%%", k->percent);
	else snprintf(info, sizeof(info), "%s", k->author[0] ? k->author : "новая");
	{
		const char *p = info;
		int c = 0;
		while (*p && c < cols) { utf8_next(&p); c++; }
		draw_text_n(tx, y + 2 + 2 * FONT_H, 1, info, p - info,
				k->status == ST_DONE ? UI_OK : selected ? UI_TEXT : UI_DIM);
	}
	if (k->status == ST_READING) {
		int bx = tx + 5 * FONT_W, bw = scr_w - bx - 8;
		fill_rect(bx, y + 2 + 2 * FONT_H + 7, bw, 3, RGB(60, 70, 100));
		fill_rect(bx, y + 2 + 2 * FONT_H + 7, bw * k->percent / 100, 3, UI_ACCENT);
	}
}

static void draw_shelf(FILE *covers, int f, int top, int sel) {
	char title[64];
	int rows = (scr_h - UI_BAR_H * 2 - 4) / ROW_H, r;
	snprintf(title, sizeof(title), "< %s >", filter_name(f));
	ui_header("");
	draw_text_center(3, 1, title, UI_TEXT);
	for (r = 0; r < rows && top + r < nview; r++)
		draw_row(covers, view[top + r], UI_BAR_H + 2 + r * ROW_H, top + r == sel);
	if (!nview)
		draw_text_center(scr_h / 2 - FONT_H, 1, "Здесь пока пусто", UI_DIM);
	ui_footer("5 читать  <> полки  Л-меню");
	gfx_present();
}

/* LSOFT on a book */
static void book_menu(shelf_book_t *k) {
	const char *items[3];
	int r;
	items[0] = k->status == ST_DONE ? "Вернуть в «Читаю»" : "Отметить прочитанной";
	items[1] = "Читать с начала";
	items[2] = "Назад";
	r = ui_pick(k->title, items, 3, 0);
	if (r == 0) {
		k->status = k->status == ST_DONE ? ST_READING : ST_DONE;
		if (!k->stamp) k->stamp = shelf_stamp();
	} else if (r == 1) {
		k->pos.blk = k->pos.ch = 0;
		k->percent = 0;
		k->status = ST_READING;
	} else {
		return;
	}
	shelf_save();
}

int reader_main(void) {
	FILE *covers;
	int f, sel = 0, top = 0, i, nf, ret = 0;
	int rows = (scr_h - UI_BAR_H * 2 - 4) / ROW_H;

	reader_init();
	if (load_index() < 0 || !nbooks)
		return ui_message("Книги", "Книг пока нет.\n\n"
				"Положите книгу (.epub, .fb2, .md, .txt, .pdf) в sources/books/ "
				"и выполните make update.");
	load_state();
	nf = F_CAT + ncats;
	/* open on the books being read, the latest first: 5 goes on reading */
	for (i = 0; i < nbooks && books[i].status != ST_READING; i++);
	f = i < nbooks ? F_READING : F_ALL;
	build_view(f);
	covers = fopen(COVERS_FILE, "rb");
	for (;;) {
		int key;
		if (sel >= nview) sel = nview ? nview - 1 : 0;
		if (sel < top) top = sel;
		if (sel >= top + rows) top = sel - rows + 1;
		draw_shelf(covers, f, top, sel);
		key = wait_key();
		if (key < 0) { ret = -1; break; }
		if (key == KEYPAD_RSOFT) break;
		if (key == KEYPAD_UP || key == KEYPAD_2) {
			if (sel) sel--;
		} else if (key == KEYPAD_DOWN || key == KEYPAD_8) {
			if (sel + 1 < nview) sel++;
		} else if (key == KEYPAD_LEFT || key == KEYPAD_4 || key == KEYPAD_RIGHT || key == KEYPAD_6) {
			f = (f + (key == KEYPAD_LEFT || key == KEYPAD_4 ? nf - 1 : 1)) % nf;
			build_view(f);
			sel = top = 0;
		} else if (nview && (key == KEYPAD_CENTER || key == KEYPAD_5)) {
			int b = view[sel];
			if (read_book(&books[b]) < 0) { ret = -1; break; }
			/* the book may have moved: keep it selected */
			build_view(f);
			for (sel = 0; sel < nview && view[sel] != b; sel++);
		} else if (nview && key == KEYPAD_LSOFT) {
			book_menu(&books[view[sel]]);
			build_view(f);
		}
	}
	if (covers) fclose(covers);
	return ret;
}
