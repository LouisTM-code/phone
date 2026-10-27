#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "apps.h"
#include "dictfile.h"
#include "log.h"

#define INDEX_FILE "books/index.txt"
#define PROGRESS_FILE "progress.txt"
#define DICT_FILE "dict.bin"
#define MAX_BOOKS 100
#define MAX_BOOK_SIZE (1600 * 1024)
#define MAX_PAGE_WORDS 160

int dict_show(int i, const char *title);

static char book_files[MAX_BOOKS][64], book_titles[MAX_BOOKS][64];
static int nbooks;

static char *text;
static int text_len_b;
static int *pages, npages;
static int cols, lines_per_page, line_h, text_y;

/* words on the current page */
static struct { int line, col, off, len, chars; } words[MAX_PAGE_WORDS];
static int nwords;

static int load_index(void) {
	char *buf = file_load(INDEX_FILE, NULL), *pos, *line;
	nbooks = 0;
	if (!buf) return -1;
	for (pos = buf; nbooks < MAX_BOOKS && (line = next_line(&pos)); ) {
		char *tab = strchr(line, '\t');
		if (!tab) continue;
		*tab = 0;
		snprintf(book_files[nbooks], 64, "%s", line);
		snprintf(book_titles[nbooks], 64, "%s", tab + 1);
		nbooks++;
	}
	free(buf);
	return 0;
}

/* progress.txt: "file<TAB>offset" lines */
static int progress_get(const char *file) {
	char *buf = file_load(PROGRESS_FILE, NULL), *pos, *line;
	int off = 0;
	if (!buf) return 0;
	for (pos = buf; (line = next_line(&pos)); ) {
		char *tab = strchr(line, '\t');
		if (!tab) continue;
		*tab = 0;
		if (!strcmp(line, file)) off = atoi(tab + 1);
	}
	free(buf);
	return off;
}

static void progress_set(const char *file, int off) {
	static char files[MAX_BOOKS][64];
	static int offs[MAX_BOOKS];
	char *buf = file_load(PROGRESS_FILE, NULL), *pos, *line;
	FILE *fp;
	int n = 0, i, found = 0;
	if (buf) {
		for (pos = buf; n < MAX_BOOKS && (line = next_line(&pos)); ) {
			char *tab = strchr(line, '\t');
			if (!tab) continue;
			*tab = 0;
			snprintf(files[n], 64, "%s", line);
			offs[n++] = atoi(tab + 1);
		}
		free(buf);
	}
	for (i = 0; i < n; i++)
		if (!strcmp(files[i], file)) { offs[i] = off; found = 1; }
	if (!found && n < MAX_BOOKS) {
		snprintf(files[n], 64, "%s", file);
		offs[n++] = off;
	}
	fp = fopen(PROGRESS_FILE, "wb");
	if (!fp) return;
	for (i = 0; i < n; i++) fprintf(fp, "%s\t%d\n", files[i], offs[i]);
	fclose(fp);
}

/* one screen line starting at pos: returns its byte length, *next = start of the next line */
static int layout_line(int pos, int *next) {
	int q = pos, brk = -1, col = 0;
	while (q < text_len_b && text[q] != '\n' && col < cols) {
		const char *p = text + q;
		if (text[q] == ' ') brk = q;
		utf8_next(&p);
		q = p - text;
		col++;
	}
	if (q < text_len_b && text[q] != '\n' && text[q] != ' ' && brk > pos) q = brk;
	*next = q;
	if (*next < text_len_b && (text[*next] == '\n' || text[*next] == ' ')) (*next)++;
	return q - pos;
}

static void paginate(void) {
	int pos = 0, n = 0, cap = text_len_b / (cols * lines_per_page / 2 + 1) + 16;
	pages = malloc(cap * sizeof(*pages));
	while (pages && pos < text_len_b) {
		int i, next;
		if (n == cap) {
			int *p = realloc(pages, (cap *= 2) * sizeof(*pages));
			if (!p) break;
			pages = p;
		}
		pages[n++] = pos;
		for (i = 0; i < lines_per_page && pos < text_len_b; i++) {
			layout_line(pos, &next);
			pos = next;
		}
	}
	npages = n;
}

static int is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static void draw_page(int page, int sel, const char *title) {
	char buf[80];
	int pos = pages[page], i;

	ui_header("");
	snprintf(buf, sizeof(buf), "%d/%d", page + 1, npages);
	{
		int w = text_len(buf), max_title = cols - w - 2, n = 0;
		const char *p = title;
		while (*p && n < max_title) { utf8_next(&p); n++; }
		draw_text_n(4, 3, 1, title, p - title, UI_TEXT);
		draw_text(scr_w - 4 - w * FONT_W, 3, 1, buf, UI_DIM);
	}

	nwords = 0;
	for (i = 0; i < lines_per_page && pos < text_len_b; i++) {
		int next, len = layout_line(pos, &next), k = 0, col = 0;
		int y = text_y + i * line_h;
		draw_text_n(4, y, 1, text + pos, len, RGB(235, 235, 225));
		/* collect the words of this line */
		while (k < len) {
			if (is_letter(text[pos + k]) && nwords < MAX_PAGE_WORDS) {
				int s = k, c = col;
				while (k < len && (is_letter(text[pos + k]) ||
						((text[pos + k] == '\'' || text[pos + k] == '-') &&
						 k + 1 < len && is_letter(text[pos + k + 1])))) { k++; col++; }
				words[nwords].line = i;
				words[nwords].col = c;
				words[nwords].off = pos + s;
				words[nwords].len = k - s;
				words[nwords].chars = col - c;
				nwords++;
			} else {
				const char *p = text + pos + k;
				utf8_next(&p);
				k = p - text - pos;
				col++;
			}
		}
		pos = next;
	}
	if (sel >= 0 && sel < nwords) {
		int x = 4 + words[sel].col * FONT_W, y = text_y + words[sel].line * line_h;
		fill_rect(x - 1, y - 1, words[sel].chars * FONT_W + 2, FONT_H + 2, UI_SEL);
		draw_text_n(x, y, 1, text + words[sel].off, words[sel].len, UI_TEXT);
	}
	/* progress bar */
	fill_rect(0, UI_BAR_H - 2, scr_w * (page + 1) / npages, 2, UI_ACCENT);
	ui_footer(sel >= 0 ? "5 перевод  <> слово  П-назад" : "<> листать  5 слово  П-выход");
	gfx_present();
}

/* nearest word on another line */
static int word_on_line(int cur, int line) {
	int i, best = -1, bd = 1000;
	for (i = 0; i < nwords; i++) {
		int d;
		if (words[i].line != line) continue;
		d = words[i].col - words[cur].col;
		if (d < 0) d = -d;
		if (d < bd) { bd = d; best = i; }
	}
	return best;
}

static int lookup_word(int w) {
	char word[64], base[64], title[400];
	int len = words[w].len, i;
	if (len > 63) len = 63;
	memcpy(word, text + words[w].off, len);
	word[len] = 0;
	i = dict_lookup(word, base, sizeof(base));
	if (i < 0) {
		snprintf(title, sizeof(title), "Слова \"%s\" нет в словаре.\n\n"
				"Большой словарь можно добавить на Mac командой tools/mkdict.py.", word);
		return ui_message("Не найдено", title);
	}
	if (strcmp(word, base)) {
		char low[64];
		int k;
		for (k = 0; word[k]; k++) low[k] = word[k] >= 'A' && word[k] <= 'Z' ? word[k] + 32 : word[k];
		low[k] = 0;
		if (strcmp(low, base)) snprintf(title, sizeof(title), "%s -> %s", word, base);
		else snprintf(title, sizeof(title), "%s", base);
	} else {
		snprintf(title, sizeof(title), "%s", base);
	}
	return dict_show(i, title);
}

static int read_book(int b) {
	FILE *fp = fopen(book_files[b], "rb");
	long size;
	int page = 0, sel = -1, off, ret = 0;
	LOG_T0(t0);

	if (!fp) return ui_message("Книги", "Не удалось открыть файл книги.");
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (size <= 0 || size > MAX_BOOK_SIZE) {
		fclose(fp);
		return ui_message("Книги", "Книга слишком большая для памяти телефона.");
	}
	text = malloc(size + 1);
	if (!text) { fclose(fp); return ui_message("Книги", "Не хватает памяти."); }
	ui_header(book_titles[b]);
	draw_text_center(scr_h / 2, 1, "Открываю книгу...", UI_DIM);
	gfx_present();
	text_len_b = fread(text, 1, size, fp);
	text[text_len_b] = 0;
	fclose(fp);
	paginate();
	LOG_OP(t0, "book_open %s %ldK %dp", book_files[b], size >> 10, npages);
	if (!npages) { free(text); free(pages); return ui_message("Книги", "Книга пустая."); }

	off = progress_get(book_files[b]);
	while (page + 1 < npages && pages[page + 1] <= off) page++;

	for (;;) {
		int key;
		draw_page(page, sel, book_titles[b]);
		key = wait_key();
		if (key < 0) { ret = -1; break; }
		if (sel < 0) {
			int old = page;
			if (key == KEYPAD_RIGHT || key == KEYPAD_6 || key == KEYPAD_DOWN || key == KEYPAD_8) {
				if (page + 1 < npages) page++;
			} else if (key == KEYPAD_LEFT || key == KEYPAD_4 || key == KEYPAD_UP || key == KEYPAD_2) {
				if (page) page--;
			} else if (key == KEYPAD_3) {
				page = page + 10 < npages ? page + 10 : npages - 1;
			} else if (key == KEYPAD_1) {
				page = page > 10 ? page - 10 : 0;
			} else if (key == KEYPAD_CENTER || key == KEYPAD_5) {
				if (nwords) sel = 0;
			} else if (key == KEYPAD_RSOFT) {
				break;
			}
			if (page != old) progress_set(book_files[b], pages[page]);
		} else {
			int t;
			if (key == KEYPAD_RIGHT || key == KEYPAD_6) {
				if (sel + 1 < nwords) sel++;
				else if (page + 1 < npages) { page++; sel = 0; }
			} else if (key == KEYPAD_LEFT || key == KEYPAD_4) {
				if (sel > 0) sel--;
				else if (page) { page--; sel = MAX_PAGE_WORDS; }
			} else if (key == KEYPAD_DOWN || key == KEYPAD_8) {
				for (t = words[sel].line + 1; t < lines_per_page; t++) {
					int w = word_on_line(sel, t);
					if (w >= 0) { sel = w; break; }
				}
			} else if (key == KEYPAD_UP || key == KEYPAD_2) {
				for (t = words[sel].line - 1; t >= 0; t--) {
					int w = word_on_line(sel, t);
					if (w >= 0) { sel = w; break; }
				}
			} else if (key == KEYPAD_CENTER || key == KEYPAD_5) {
				if (lookup_word(sel) < 0) { ret = -1; break; }
			} else if (key == KEYPAD_RSOFT) {
				sel = -1;
			}
			/* after going back a page the word index is clamped on redraw */
			if (sel >= MAX_PAGE_WORDS) {
				draw_page(page, -1, book_titles[b]);
				sel = nwords ? nwords - 1 : -1;
			}
		}
	}
	progress_set(book_files[b], pages[page]);
	free(text); text = NULL;
	free(pages); pages = NULL;
	return ret;
}

int reader_main(void) {
	const char *items[MAX_BOOKS];
	int i, sel = 0;

	cols = (scr_w - 8) / FONT_W;
	line_h = FONT_H + 1;
	text_y = UI_BAR_H + 3;
	lines_per_page = (scr_h - UI_BAR_H * 2 - 6) / line_h;
	dict_open(DICT_FILE);

	if (load_index() < 0 || !nbooks)
		return ui_message("Книги", "Книг пока нет.\n\n"
				"Скачайте книгу на английском (.epub, .fb2 или .txt) "
				"и добавьте её на Mac командой tools/mkbook.py.");
	for (;;) {
		int r;
		for (i = 0; i < nbooks; i++) items[i] = book_titles[i];
		r = ui_pick("Книги", items, nbooks, sel);
		if (r == -2) return -1;
		if (r < 0) return 0;
		sel = r;
		if (read_book(r) < 0) return -1;
	}
}
