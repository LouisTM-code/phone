#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "apps.h"
#include "reader.h"
#include "marks.h"
#include "dictfile.h"
#include "log.h"

#define DICT_FILE "dict.bin"
#define MAX_PAGE_WORDS 400
#define SAVE_IDLE_MS 3000
/* the debug log notes pages and pictures drawn slower than this */
#define LOG_SLOW_MS 200

int dict_show(int i, const char *title);
static int view_menu(void);
static int theme, bright;  /* the look, state/reader.txt */

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

static void frame(int x, int y, int w, int h, uint16_t c) {
	fill_rect(x, y, w, 2, c);
	fill_rect(x, y + h - 2, w, 2, c);
	fill_rect(x, y, 2, h, c);
	fill_rect(x + w - 2, y, 2, h, c);
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
		LOG_T0(t0);
		snprintf(title, sizeof(title), "Картинка %dx%d%s", w, h, names[z]);
		ui_header(title);
		fill_rect(0, UI_BAR_H, scr_w, vh, book_bg);
		book_image(b, id, dx, UI_BAR_H + dy, x * q / k, y * q / k, vw * q / k + 1, vh * q / k + 1,
				book_pal[BC_TEXT], zoom);
		ui_footer("5 масштаб  <> двигать  П-назад");
		gfx_present();
		LOG_SLOW(t0, LOG_SLOW_MS, "image %dx%d zoom %d", w, h, zoom);
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

/* ---- searchable text of the loaded page */

#define TEXT_MAX 6144
#define NO_OFF 0xffff
static char ptext[TEXT_MAX];
static uint16_t poff[TEXT_MAX];  /* where each byte is in the page, NO_OFF if not drawn */
static int plen;

static void text_add(const char *s, int n, int off) {
	int i;
	for (i = 0; i < n && plen < TEXT_MAX - 1; i++) {
		ptext[plen] = s[i];
		poff[plen++] = off < 0 ? NO_OFF : off + i;
	}
	ptext[plen] = 0;
}

/* lines are joined by spaces, a hyphen of a broken word is dropped */
static void page_text(book_t *b) {
	const uint8_t *p = b->page, *end = p + b->page_size, *it;
	int t;
	plen = 0;
	ptext[0] = 0;
	while (it = p, (t = book_item(&p, end)) != BK_END) {
		if (t == BK_TEXT && !(it[3] & BK_LINE_REPEAT)) {
			int n = it[4], flags = it[3];
			it += 5;
			while (n--) {
				text_add((const char*)it + 4, it[3], it + 4 - b->page);
				it += 4 + it[3];
			}
			if (flags & BK_LINE_HYPHEN && plen && ptext[plen - 1] == '-') {
				ptext[--plen] = 0;
				continue;
			}
		} else if (t == BK_SEARCH) {
			text_add((const char*)it + 3, bk_u16(it + 1), -1);
		} else {
			continue;
		}
		text_add(" ", 1, -1);
	}
}

/* lower case in place: ASCII, Cyrillic, Latin-1; byte lengths stay the same */
static void lower(char *s, int n) {
	uint8_t *u = (uint8_t*)s;
	int i;
	for (i = 0; i < n; i++) {
		if (u[i] >= 'A' && u[i] <= 'Z') u[i] += 32;
		else if (u[i] == 0xd0 && i + 1 < n) {
			uint8_t c = u[i + 1];
			if (c >= 0x90 && c <= 0x9f) u[i + 1] = c + 0x20;
			else if (c >= 0xa0 && c <= 0xaf) u[i] = 0xd1, u[i + 1] = c - 0x20;
			else if (c == 0x81) u[i] = 0xd1, u[i + 1] = 0x91;
			i++;
		} else if (u[i] == 0xc3 && i + 1 < n) {
			if (u[i + 1] >= 0x80 && u[i + 1] <= 0x9e && u[i + 1] != 0x97) u[i + 1] += 0x20;
			i++;
		}
	}
}

/* ---- reading */

typedef struct {
	book_t b;
	shelf_book_t *k;
	int page, sel, qsel;
	char query[64];
	int found_page, found_end;  /* the last match: page and its end in the page text */
} reading_t;

static marks_t marks;

static int page_mark(reading_t *r) {
	book_anchor_t a = book_anchor(&r->b, r->page), e;
	int i;
	e = r->page + 1 < (int)r->b.npages ? book_anchor(&r->b, r->page + 1) : (book_anchor_t){ ~0u, ~0u };
	for (i = 0; i < marks.n; i++) {
		book_anchor_t m = marks.list[i].pos;
		if (marks.list[i].kind != 'b') continue;
		if ((m.blk > a.blk || (m.blk == a.blk && m.ch >= a.ch)) &&
				(m.blk < e.blk || (m.blk == e.blk && m.ch < e.ch))) return i;
	}
	return -1;
}

static void draw_status(reading_t *r) {
	book_t *b = &r->b;
	char num[24];
	const char *title = b->title, *p;
	int w, max, n = 0;
	if (r->qsel >= 0) title = "Цитата: 5 сохранить, П-отмена";
	else if (r->sel >= 0) title = "5 перевод  * цитата  # найти";
	fill_rect(0, 0, scr_w, UI_BAR_H, UI_BAR);
	snprintf(num, sizeof(num), "%d/%u", r->page + 1, b->npages);
	w = text_len(num);
	max = (scr_w - 8) / FONT_W - w - 1;
	for (p = title; *p && n < max; n++) utf8_next(&p);
	draw_text_n(4, 3, 1, title, p - title, r->sel >= 0 ? UI_ACCENT : UI_TEXT);
	draw_text(scr_w - 4 - w * FONT_W, 3, 1, num, UI_DIM);
	fill_rect(0, UI_BAR_H - 2, scr_w * (r->page + 1) / b->npages, 2, UI_ACCENT);
	/* a bookmark on this page: a ribbon in the corner */
	if (page_mark(r) >= 0) fill_rect(scr_w - 10, UI_BAR_H, 6, 10, UI_ACCENT);
}

static void draw_word(book_t *b, word_t *w, uint16_t bg) {
	fill_rect(page_x + w->x - 1, page_y + w->y - 1, w->w + 2, w->h + 2, bg);
	draw_text_style(page_x + w->x, page_y + w->y, w->style & 7,
			(const char*)b->page + w->off, w->len, UI_TEXT);
}

static void draw_page(reading_t *r) {
	book_t *b = &r->b;
	int sel = r->sel;
	fill_rect(0, UI_BAR_H, scr_w, scr_h - UI_BAR_H, book_bg);
	book_draw(b, page_x, page_y);
	if (r->qsel >= 0) {
		int i, a = r->qsel < sel ? r->qsel : sel, e = r->qsel < sel ? sel : r->qsel;
		for (i = a; i <= e; i++)
			if (words[i].img == BK_NONE) draw_word(b, &words[i], RGB(50, 80, 140));
	}
	if (sel >= 0 && sel < nwords) {
		word_t *w = &words[sel];
		if (w->img != BK_NONE) frame(page_x + w->x - 2, page_y + w->y - 2, w->w + 4, w->h + 4, UI_SEL);
		else draw_word(b, w, UI_SEL);
	}
	draw_status(r);
	gfx_present();
}

/* the page text from the start of word a to the end of word e */
static void quote_text(reading_t *r, int a, int e, char *out, int size) {
	int i, from = -1, to = -1, n;
	page_text(&r->b);
	for (i = 0; i < plen; i++) {
		if (poff[i] == words[a].off && from < 0) from = i;
		if (poff[i] == words[e].off + words[e].len - 1) to = i + 1;
	}
	if (from < 0 || to <= from) { out[0] = 0; return; }
	n = to - from < size - 1 ? to - from : size - 1;
	while (n && ((unsigned char)ptext[from + n] & 0xc0) == 0x80) n--;
	memcpy(out, ptext + from, n);
	out[n] = 0;
}

/* a short text for a bookmark: the start of the page */
static void page_snippet(reading_t *r, char *out, int size) {
	const char *p;
	int n = 0;
	page_text(&r->b);
	for (p = ptext; *p == ' '; p++);
	while (p[n] && n < size - 1 && n < 60) n++;
	while (n && ((unsigned char)p[n] & 0xc0) == 0x80) n--;
	memcpy(out, p, n);
	out[n] = 0;
}

static void toggle_bookmark(reading_t *r) {
	int i = page_mark(r);
	char text[80];
	if (i >= 0) {
		marks_remove(&marks, i);
		ui_note("Закладка убрана");
	} else {
		page_snippet(r, text, sizeof(text));
		marks_add(&marks, 'b', book_anchor(&r->b, r->page), text);
		ui_note("Закладка добавлена");
	}
	if (marks_save(&marks, r->k->id) < 0) ui_note("Не удалось записать закладки");
}

/* finds r->query from the byte pos of the page text of page from; moves
   there and selects the word. 1 found, 0 not found, -1 cancelled, -2 quit */
static int search(reading_t *r, int from, int pos) {
	book_t *b = &r->b;
	char q[64];
	int qn = strlen(r->query), p, i;
	memcpy(q, r->query, qn + 1);
	lower(q, qn);
	for (p = from; p < (int)b->npages; p++) {
		if (p > from && (p - from) % 50 == 0) {
			char msg[48];
			int key = poll_key();
			if (key < 0) return -2;
			if (key == KEYPAD_RSOFT) return -1;
			snprintf(msg, sizeof(msg), "Поиск... %d%%", p * 100 / b->npages);
			fill_rect(0, scr_h / 2 - FONT_H, scr_w, FONT_H * 2, UI_BAR);
			draw_text_center(scr_h / 2 - FONT_H / 2, 1, msg, UI_TEXT);
			gfx_present();
		}
		if (book_load(b, p) < 0) return 0;
		page_text(b);
		lower(ptext, plen);
		for (i = p == from ? pos : 0; i + qn <= plen; i++) {
			if (memcmp(ptext + i, q, qn)) continue;
			r->page = p;
			r->found_page = p;
			r->found_end = i + qn;
			r->sel = -1;
			if (poff[i] != NO_OFF) {
				int w;
				collect_words(b);
				for (w = 0; w < nwords; w++)
					if (words[w].img == BK_NONE && poff[i] < words[w].off + words[w].len) break;
				if (w < nwords) r->sel = w;
			}
			return 1;
		}
	}
	return 0;
}

static int search_next(reading_t *r, int from, int pos) {
	LOG_T0(t0);
	int ret = search(r, from, pos);
	LOG_OP(t0, "search p%d -> p%d ret %d", from, ret == 1 ? r->page : (int)r->b.npages, ret);
	if (ret == 0) ui_note("Больше не найдено");
	else if (ret == -1) ui_note("Поиск отменён");
	return ret == -2 ? -1 : 0;
}

/* position in the page text after the selected word */
static int text_after(reading_t *r) {
	int i;
	page_text(&r->b);
	for (i = 0; i < plen; i++)
		if (poff[i] == words[r->sel].off + words[r->sel].len - 1) return i + 1;
	return 0;
}

static int show_toc(reading_t *r) {
	unsigned n, i;
	uint8_t *toc = book_toc(&r->b, &n), *p;
	const char **items;
	char *names;
	int cur = 0, sel;
	if (!toc || !n) {
		free(toc);
		return ui_message("Оглавление", "В книге нет оглавления.");
	}
	items = malloc(n * sizeof(*items));
	names = malloc(n * 264);
	if (!items || !names) {
		free(toc); free(items); free(names);
		return 0;
	}
	for (i = 0, p = toc; i < n; i++) {
		int level = p[4], len = p[5];
		char *s = names + i * 264;
		memset(s, ' ', (level - 1) * 2);
		memcpy(s + (level - 1) * 2, p + 6, len);
		s[(level - 1) * 2 + len] = 0;
		items[i] = s;
		if ((int)bk_u32(p) <= r->page) cur = i;
		p += 6 + len;
	}
	sel = ui_pick("Оглавление", items, n, cur);
	if (sel >= 0) {
		for (i = 0, p = toc; (int)i < sel; i++) p += 6 + p[5];
		r->page = bk_u32(p);
		r->sel = -1;
	}
	free(toc); free(items); free(names);
	return sel == -2 ? -1 : 0;
}

static int show_marks(reading_t *r) {
	static char lines[MAX_MARKS][64];
	const char *items[MAX_MARKS];
	int i, sel = 0;
	for (;;) {
		if (!marks.n) return ui_message("Закладки и цитаты",
				"Пока пусто.\n\n* на странице - закладка.\n5, затем * на слове - начало цитаты, "
				"5 на последнем слове - сохранить.");
		for (i = 0; i < marks.n; i++) {
			snprintf(lines[i], sizeof(lines[i]), "%s %d: %s", marks.list[i].kind == 'b' ? "Закл." : "Цит.",
					book_find(&r->b, marks.list[i].pos) + 1, marks.list[i].text);
			items[i] = lines[i];
		}
		sel = ui_pick("Закладки и цитаты", items, marks.n, sel < marks.n ? sel : 0);
		if (sel == -2) return -1;
		if (sel < 0) return 0;
		{
			const char *acts[3] = { "Перейти", "Удалить", "Показать текст" };
			int a = ui_pick(marks.list[sel].kind == 'b' ? "Закладка" : "Цитата", acts,
					marks.list[sel].kind == 'q' ? 3 : 2, 0);
			if (a == -2) return -1;
			if (a == 0) {
				r->page = book_find(&r->b, marks.list[sel].pos);
				r->sel = -1;
				return 0;
			}
			if (a == 1) {
				marks_remove(&marks, sel);
				if (marks_save(&marks, r->k->id) < 0) ui_note("Не удалось записать закладки");
			}
			if (a == 2 && ui_message("Цитата", marks.list[sel].text) < 0) return -1;
		}
	}
}

static int reader_menu(reading_t *r) {
	const char *items[8];
	char nm[64];
	int n = 0, a;
	items[n++] = "Оглавление";
	items[n++] = "Перейти к странице";
	items[n++] = "Поиск";
	items[n++] = page_mark(r) >= 0 ? "Убрать закладку" : "Закладка здесь";
	snprintf(nm, sizeof(nm), "Закладки и цитаты (%d)", marks.n);
	items[n++] = nm;
	items[n++] = "Вид";
	items[n++] = "Клавиши";
	a = ui_pick(r->b.title, items, n, 0);
	if (a == -2) return -1;
	if (a == 0) return show_toc(r);
	if (a == 1) {
		char hint[40];
		int p;
		snprintf(hint, sizeof(hint), "Страница от 1 до %u:", r->b.npages);
		p = ui_number("Перейти", hint, r->b.npages);
		if (p == -2) return -1;
		if (p > 0) {
			r->page = p - 1;
			r->sel = -1;
		}
	} else if (a == 2) {
		int len = ui_input("Поиск", r->query, sizeof(r->query));
		if (len == -2) return -1;
		if (len > 0) return search_next(r, r->page, 0);
	} else if (a == 3) {
		toggle_bookmark(r);
	} else if (a == 4) {
		return show_marks(r);
	} else if (a == 5) {
		return view_menu();
	} else if (a == 6) {
		return ui_message("Клавиши",
				"<> или 4/6 - страницы, 1/3 - на 10 страниц.\n"
				"5 - выбрать слово, стрелки - по словам, 5 - перевод, "
				"* - начать цитату, # - найти это слово дальше.\n"
				"5 на картинке - во весь экран.\n"
				"* - закладка на странице, # - найти дальше.\n"
				"Л.софт - меню: оглавление, страница, поиск, закладки.\n"
				"П.софт - назад.");
	}
	return 0;
}

int read_book(shelf_book_t *k) {
	reading_t rd, *r = &rd;
	book_t *b = &rd.b;
	int ret = 0, dirty = 0;
	LOG_T0(t0);

	memset(r, 0, sizeof(*r));
	r->k = k;
	r->sel = r->qsel = r->found_page = -1;
	if (book_open(b, k->file) < 0) return ui_message("Книги", "Не удалось открыть книгу.");
	if ((int)b->page_w != page_w || (int)b->page_h != page_h) {
		book_close(b);
		return ui_message("Книги", "Книга собрана для другого экрана: пересоберите её (make data).");
	}
	r->page = k->stamp ? book_find(b, k->pos) : 0;
	k->stamp = shelf_stamp();
	if (k->status == ST_NEW) k->status = ST_READING;
	shelf_save();
	marks_load(&marks, k->id);
	sys_brightness(bright);
	LOG_OP(t0, "book_open %s %up", k->file, b->npages);

	for (;;) {
		int key, old = r->page;
		LOG_T0(tp);
		if (book_load(b, r->page) < 0) {
			ret = ui_message("Книги", "Ошибка чтения книги.");
			break;
		}
		collect_words(b);
		if (r->sel >= MAX_PAGE_WORDS) r->sel = nwords - 1;
		if (r->sel >= nwords) r->sel = nwords ? 0 : -1;
		if (r->qsel >= nwords) r->qsel = -1;
		draw_page(r);
		LOG_SLOW(tp, LOG_SLOW_MS, "page %d", r->page);
		key = wait_key_ms(dirty ? SAVE_IDLE_MS : -1);
		if (key < 0) { ret = -1; break; }
		if (!key) {
			shelf_save();
			dirty = 0;
			continue;
		}
		if (key == KEYPAD_LSOFT && r->qsel < 0) {
			if (reader_menu(r) < 0) { ret = -1; break; }
		} else if (key == KEYPAD_HASH && r->query[0] && r->sel < 0) {
			/* the next match after the last one on this page */
			if (search_next(r, r->page, r->found_page == r->page ? r->found_end : 0) < 0) {
				ret = -1;
				break;
			}
		} else if (r->sel < 0) {
			if (key == KEYPAD_RIGHT || key == KEYPAD_6 || key == KEYPAD_DOWN || key == KEYPAD_8) {
				if (r->page + 1 < (int)b->npages) r->page++;
			} else if (key == KEYPAD_LEFT || key == KEYPAD_4 || key == KEYPAD_UP || key == KEYPAD_2) {
				if (r->page) r->page--;
			} else if (key == KEYPAD_3) {
				r->page = r->page + 10 < (int)b->npages ? r->page + 10 : (int)b->npages - 1;
			} else if (key == KEYPAD_1) {
				r->page = r->page > 10 ? r->page - 10 : 0;
			} else if (key == KEYPAD_CENTER || key == KEYPAD_5) {
				if (nwords) r->sel = 0;
			} else if (key == KEYPAD_STAR) {
				toggle_bookmark(r);
			} else if (key == KEYPAD_RSOFT) {
				break;
			}
		} else {
			int t, sel = r->sel;
			if (key == KEYPAD_RIGHT || key == KEYPAD_6) {
				if (sel + 1 < nwords) r->sel++;
				else if (r->qsel < 0 && r->page + 1 < (int)b->npages) { r->page++; r->sel = 0; }
			} else if (key == KEYPAD_LEFT || key == KEYPAD_4) {
				if (sel > 0) r->sel--;
				else if (r->qsel < 0 && r->page) { r->page--; r->sel = MAX_PAGE_WORDS; }
			} else if (key == KEYPAD_DOWN || key == KEYPAD_8) {
				if ((t = word_vertical(sel, 1)) >= 0) r->sel = t;
			} else if (key == KEYPAD_UP || key == KEYPAD_2) {
				if ((t = word_vertical(sel, -1)) >= 0) r->sel = t;
			} else if (r->qsel >= 0) {
				if (key == KEYPAD_CENTER || key == KEYPAD_5) {
					char text[512];
					int a = r->qsel < sel ? r->qsel : sel, e = r->qsel < sel ? sel : r->qsel;
					quote_text(r, a, e, text, sizeof(text));
					if (text[0]) {
						marks_add(&marks, 'q', book_anchor(b, r->page), text);
						ui_note(marks_save(&marks, k->id) < 0 ? "Не удалось записать цитату" : "Цитата сохранена");
					}
					r->qsel = -1;
				} else if (key == KEYPAD_RSOFT || key == KEYPAD_STAR) {
					r->qsel = -1;
				}
			} else if (key == KEYPAD_CENTER || key == KEYPAD_5) {
				if (lookup_word(b, sel) < 0) { ret = -1; break; }
			} else if (key == KEYPAD_STAR && words[sel].img == BK_NONE) {
				r->qsel = sel;
			} else if (key == KEYPAD_HASH && words[sel].img == BK_NONE) {
				int len = words[sel].len < 63 ? words[sel].len : 63;
				memcpy(r->query, b->page + words[sel].off, len);
				r->query[len] = 0;
				if (search_next(r, r->page, text_after(r)) < 0) { ret = -1; break; }
			} else if (key == KEYPAD_RSOFT) {
				r->sel = -1;
			}
		}
		if (r->page != old) {
			book_progress(k, b, r->page);
			dirty = 1;
		}
	}
	book_progress(k, b, r->page);
	shelf_save();
	marks_free(&marks);
	book_close(b);
	sys_brightness(sys_data.brightness);
	return ret;
}

/* ---- the look: state/reader.txt "theme N" and "bright N" lines */

#define SETTINGS_FILE "state/reader.txt"
static const struct {
	const char *name;
	uint8_t c[BC_COUNT + 1][3];  /* background, then BC_* */
} themes[] = {
	{ "Тёмная", { {24, 26, 34}, {230, 230, 220}, {150, 160, 180}, {130, 190, 255}, {255, 210, 90},
		{150, 230, 150}, {40, 44, 56}, {120, 130, 170}, {90, 100, 130}, {44, 52, 76} } },
	{ "Ночь", { {0, 0, 0}, {150, 150, 140}, {100, 100, 100}, {80, 120, 160}, {170, 140, 70},
		{100, 150, 100}, {22, 22, 22}, {70, 70, 90}, {60, 60, 70}, {26, 26, 36} } },
	{ "Бумага", { {238, 230, 208}, {40, 36, 30}, {110, 100, 90}, {30, 70, 140}, {150, 60, 20},
		{20, 110, 40}, {222, 212, 186}, {160, 145, 120}, {170, 155, 130}, {222, 210, 180} } },
	{ "Белая", { {255, 255, 255}, {0, 0, 0}, {100, 100, 100}, {20, 60, 160}, {170, 40, 20},
		{0, 110, 0}, {236, 236, 236}, {170, 170, 170}, {170, 170, 170}, {225, 230, 240} } },
};
#define NTHEMES (int)(sizeof(themes) / sizeof(*themes))

static void set_theme(int t) {
	int i;
	theme = t >= 0 && t < NTHEMES ? t : 0;
	book_bg = RGB(themes[theme].c[0][0], themes[theme].c[0][1], themes[theme].c[0][2]);
	for (i = 0; i < BC_COUNT; i++)
		book_pal[i] = RGB(themes[theme].c[i + 1][0], themes[theme].c[i + 1][1], themes[theme].c[i + 1][2]);
}

static void load_settings(void) {
	char *buf = file_load(SETTINGS_FILE, NULL), *pos, *line;
	int t = 0;
	bright = sys_data.brightness;
	if (buf) {
		for (pos = buf; (line = next_line(&pos)); ) {
			if (!strncmp(line, "theme ", 6)) t = atoi(line + 6);
			else if (!strncmp(line, "bright ", 7)) bright = atoi(line + 7);
		}
		free(buf);
	}
	if (bright < 5 || bright > 100) bright = sys_data.brightness;
	set_theme(t);
}

static void save_settings(void) {
	FILE *fp = fopen(SETTINGS_FILE, "wb");
	LOG_T0(t0);
	if (!fp) return;
	fprintf(fp, "theme %d\nbright %d\n", theme, bright);
	fclose(fp);
	LOG_OP(t0, "settings_save %d", theme);
}

static int view_menu(void) {
	for (;;) {
		const char *items[3];
		char t[48], br[32];
		int a;
		snprintf(t, sizeof(t), "Подложка: %s", themes[theme].name);
		snprintf(br, sizeof(br), "Яркость: %d%%", bright);
		items[0] = t;
		items[1] = br;
		items[2] = "Назад";
		a = ui_pick("Вид", items, 3, 0);
		if (a == -2) return -1;
		if (a == 0) {
			const char *names[NTHEMES];
			int i;
			for (i = 0; i < NTHEMES; i++) names[i] = themes[i].name;
			i = ui_pick("Подложка", names, NTHEMES, theme);
			if (i == -2) return -1;
			if (i >= 0) set_theme(i);
		} else if (a == 1) {
			static const char *const levels[] = { "10%", "20%", "30%", "40%", "50%", "60%", "70%",
				"80%", "90%", "100%" };
			int i = ui_pick("Яркость", levels, 10, bright >= 10 ? bright / 10 - 1 : 0);
			if (i == -2) return -1;
			if (i >= 0) {
				bright = (i + 1) * 10;
				sys_brightness(bright);
			}
		} else {
			save_settings();
			return 0;
		}
	}
}

void reader_init(void) {
	page_x = 4;
	page_y = UI_BAR_H + 3;
	page_w = scr_w - 8;
	page_h = scr_h - page_y - 3;
	load_settings();
	dict_open(DICT_FILE);
}
