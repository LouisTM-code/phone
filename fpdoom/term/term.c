#include <stdlib.h>
#include <string.h>
#include "term.h"

static const uint8_t font16_ascii[] = {
#include "font_ascii.h"
};
static const uint8_t font16_cyr[] = {
#include "font_cyr.h"
};
static const uint8_t font8[] = {
#include "font8x8.h"
};

static const uint16_t palette[16] = {
	RGB(0, 0, 0), RGB(205, 0, 0), RGB(0, 205, 0), RGB(205, 205, 0),
	RGB(60, 90, 238), RGB(205, 0, 205), RGB(0, 205, 205), RGB(210, 210, 210),
	RGB(127, 127, 127), RGB(255, 60, 60), RGB(60, 255, 60), RGB(255, 255, 60),
	RGB(110, 140, 255), RGB(255, 60, 255), RGB(60, 255, 255), RGB(255, 255, 255),
};

typedef struct { uint16_t ch; uint8_t attr; } cell_t;

int term_cols, term_rows, term_vis_cols, term_vis_rows;
static int font_h, cur_row, cur_col = -1;
static cell_t *cells;
static const char *status;

#define TAP_MS 900

void term_init(int fh, int cols, int rows) {
	gfx_init();
	font_h = fh;
	term_vis_cols = scr_w / FONT_W;
	term_vis_rows = scr_h / font_h;
	term_cols = cols ? cols : term_vis_cols;
	term_rows = rows ? rows : term_vis_rows;
	cells = malloc(term_cols * term_rows * sizeof(cell_t));
	term_erase(0, 0, term_rows, term_cols);
}

void term_put(int row, int col, unsigned ch, int attr) {
	cell_t *c;
	if ((unsigned)row >= (unsigned)term_rows || (unsigned)col >= (unsigned)term_cols) return;
	c = &cells[row * term_cols + col];
	c->ch = ch;
	c->attr = attr;
}

unsigned term_char(int row, int col) {
	if ((unsigned)row >= (unsigned)term_rows || (unsigned)col >= (unsigned)term_cols) return ' ';
	return cells[row * term_cols + col].ch;
}

int term_puts(int row, int col, const char *s, int attr) {
	while (*s && col < term_cols) term_put(row, col++, utf8_next(&s), attr);
	return col;
}

void term_erase(int top, int left, int bottom, int right) {
	int i, j;
	if (top < 0) top = 0;
	if (left < 0) left = 0;
	if (bottom > term_rows) bottom = term_rows;
	if (right > term_cols) right = term_cols;
	for (j = top; j < bottom; j++)
		for (i = left; i < right; i++) {
			cells[j * term_cols + i].ch = ' ';
			cells[j * term_cols + i].attr = TERM_DEFAULT;
		}
}

void term_scroll(int top, int bottom, int n) {
	int len = bottom - top - abs(n);
	if (len <= 0) { term_erase(top, 0, bottom, term_cols); return; }
	if (n > 0) {
		memmove(cells + top * term_cols, cells + (top + n) * term_cols,
				len * term_cols * sizeof(cell_t));
		term_erase(bottom - n, 0, bottom, term_cols);
	} else if (n < 0) {
		memmove(cells + (top - n) * term_cols, cells + top * term_cols,
				len * term_cols * sizeof(cell_t));
		term_erase(top, 0, top - n, term_cols);
	}
}

void term_cursor(int row, int col) {
	cur_row = row;
	cur_col = col;
}

void term_status(const char *s) {
	status = s;
}

static const uint8_t *glyph(unsigned c) {
	unsigned i;
	if (c >= 0x20 && c < 0x80) i = c - 0x20;
	else if (c >= 0x410 && c < 0x450) i = c - 0x410 + 96;
	else if (c == 0x401) i = 96 + 64;
	else if (c == 0x451) i = 96 + 65;
	else i = '?' - 0x20;
	if (font_h == 8) return font8 + i * 8;
	if (i < 96) return font16_ascii + i * 16;
	return font16_cyr + (i - 96) * 16;
}

static void draw_cell(int x, int y, unsigned ch, uint16_t fg, uint16_t bg) {
	const uint8_t *g = glyph(ch);
	uint16_t *p = framebuf + y * scr_w + x;
	int i, j;
	for (j = 0; j < font_h; j++, p += scr_w) {
		unsigned bits = g[j];
		for (i = 0; i < 8; i++, bits <<= 1) p[i] = bits & 0x80 ? fg : bg;
	}
}

/* the first visible cell along one axis */
static int view_start(int cur, int size, int vis) {
	int v;
	if (size <= vis) return 0;
	v = cur - vis / 2;
	if (v < 0) v = 0;
	if (v > size - vis) v = size - vis;
	return v;
}

void term_refresh(void) {
	int x0 = view_start(cur_col < 0 ? 0 : cur_col, term_cols, term_vis_cols);
	int y0 = view_start(cur_row, term_rows, term_vis_rows);
	int i, j, nc = term_cols - x0, nr = term_rows - y0;
	if (nc > term_vis_cols) nc = term_vis_cols;
	if (nr > term_vis_rows) nr = term_vis_rows;
	fill_rect(0, 0, scr_w, scr_h, palette[0]);
	for (j = 0; j < nr; j++)
		for (i = 0; i < nc; i++) {
			cell_t *c = &cells[(y0 + j) * term_cols + x0 + i];
			uint16_t fg = palette[TERM_COLOR(c->attr)], bg = palette[0];
			int rev = c->attr & TERM_REV;
			if (y0 + j == cur_row && x0 + i == cur_col) rev = !rev;
			if (rev) { uint16_t t = fg; fg = bg; bg = t; if (fg == bg) fg = palette[0], bg = palette[7]; }
			draw_cell(i * FONT_W, j * font_h, c->ch, fg, bg);
		}
	if (status) {
		int n = text_len(status), x = scr_w - n * FONT_W;
		const char *s = status;
		for (i = 0; i < n; i++) draw_cell(x + i * FONT_W, 0, utf8_next(&s), palette[0], palette[11]);
	}
	gfx_present();
}

int term_key(int ms) {
	unsigned start = sys_timer_ms();
	int key;
	while (!(key = poll_key())) {
		if (ms >= 0 && sys_timer_ms() - start >= (unsigned)ms) return 0;
		sys_wait_ms(10);
	}
	return key;
}

/* multi-tap */

static const char *const tap_keys[3][10] = {
	{ " 0", ".,?!'-1\"():;/", "abc2", "def3", "ghi4", "jkl5", "mno6", "pqrs7", "tuv8", "wxyz9" },
	{ " 0", ".,?!'-1\"():;/", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9" },
	{ "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" },
};
static const char *const tap_mode_name[3] = { "abc", "ABC", "123" };
static int tap_mode;

enum { TAP_NONE, TAP_CHAR, TAP_CONFIRM, TAP_CANCEL, TAP_QUIT };

/* runs the multi-tap for one character, starting with the key first (0 = none).
   The pending character *ch is shown by show; the result says what ended it,
   *key is the key that ended it (0 = timeout or mode change) */
static int tap_run(int first, int *ch, int *key, void (*show)(int ch, void *ctx), void *ctx) {
	int last = -1, idx = 0, k;
	*ch = 0;
	*key = 0;
	for (;;) {
		term_status(tap_mode_name[tap_mode]);
		show(*ch, ctx);
		k = first ? first : term_key(*ch ? TAP_MS : -1);
		first = 0;
		if (k < 0) return TAP_QUIT;
		if (!k) return TAP_CHAR;
		if (k >= KEYPAD_0 && k <= KEYPAD_9) {
			const char *set = tap_keys[tap_mode][k - KEYPAD_0];
			if (*ch && k != last) { *key = k; return TAP_CHAR; }
			idx = k == last ? idx + 1 : 0;
			if (!set[idx]) idx = 0;
			*ch = set[idx];
			last = k;
			if (tap_mode == 2) return TAP_CHAR;
			continue;
		}
		if (k == KEYPAD_STAR) {
			tap_mode = (tap_mode + 1) % 3;
			if (*ch) return TAP_CHAR;
			continue;
		}
		*key = k;
		if (*ch) return TAP_CHAR;
		if (k == KEYPAD_HASH || k == KEYPAD_CENTER) return TAP_CONFIRM;
		if (k == KEYPAD_RSOFT) return TAP_CANCEL;
		return TAP_NONE;
	}
}

static void show_one(int ch, void *ctx) {
	(void)ctx;
	if (cur_col >= 0) term_put(cur_row, cur_col, ch ? ch : ' ', TERM_DEFAULT);
	term_refresh();
}

int term_tap_char(int first) {
	int ch, key, r;
	unsigned old = term_char(cur_row, cur_col);
	for (;;) {
		r = tap_run(first, &ch, &key, show_one, NULL);
		first = 0;
		if (cur_col >= 0) term_put(cur_row, cur_col, old, TERM_DEFAULT);
		term_status(NULL);
		if (r == TAP_QUIT) return -1;
		if (r == TAP_CHAR) return key == KEYPAD_RSOFT ? 0 : ch;
		if (r == TAP_CANCEL) return 0;
	}
}

typedef struct { int row, col, len, width; char *buf; } line_t;

static void show_line(int ch, void *ctx) {
	line_t *l = ctx;
	int n = l->len - (l->width - 2), i, c = l->col;
	const char *s = l->buf;
	if (n < 0) n = 0;
	term_erase(l->row, l->col, l->row + 1, term_cols);
	for (i = n; i < l->len; i++) term_put(l->row, c++, (uint8_t)s[i], TERM_DEFAULT);
	if (ch) term_put(l->row, c++, ch, TERM_DEFAULT | TERM_REV);
	term_cursor(l->row, ch ? -1 : c);
	term_refresh();
}

static char last_line[128];

int term_getline(int row, int col, char *buf, int max, const char *const *words) {
	line_t l;
	int ch, key = 0, r, n;
	l.row = row; l.col = col; l.buf = buf;
	l.len = strlen(buf);
	l.width = term_cols - col;
	for (;;) {
		/* a letter key that ended the pending letter starts the next one */
		r = tap_run(key >= KEYPAD_0 && key <= KEYPAD_9 ? key : 0, &ch, &key, show_line, &l);
		if (r == TAP_QUIT) { term_status(NULL); return -1; }
		if (r == TAP_CHAR) {
			/* RSOFT and LEFT drop the pending letter */
			if (key != KEYPAD_RSOFT && key != KEYPAD_LEFT && l.len < max - 1) buf[l.len++] = ch;
			buf[l.len] = 0;
			continue;
		}
		if (r == TAP_CONFIRM) break;
		if (key == KEYPAD_RSOFT || key == KEYPAD_LEFT) {
			if (l.len) buf[--l.len] = 0;
		} else if (key == KEYPAD_UP && last_line[0]) {
			strncpy(buf, last_line, max - 1);
			buf[max - 1] = 0;
			l.len = strlen(buf);
		} else if (key == KEYPAD_DOWN) {
			l.len = 0;
			buf[0] = 0;
		} else if (key == KEYPAD_LSOFT && words) {
			for (n = 0; words[n]; n++);
			n = term_pick("Слово", words, n, 0);
			if (n == -2) { term_status(NULL); return -1; }
			if (n >= 0) {
				const char *w = words[n];
				if (l.len && buf[l.len - 1] != ' ' && l.len < max - 1) buf[l.len++] = ' ';
				while (*w && l.len < max - 1) buf[l.len++] = *w++;
				buf[l.len] = 0;
			}
		}
		key = 0;
	}
	term_status(NULL);
	if (l.len) {
		strncpy(last_line, buf, sizeof(last_line) - 1);
		last_line[sizeof(last_line) - 1] = 0;
	}
	show_line(0, &l);
	return l.len;
}

int term_pick(const char *title, const char *const *items, int n, int sel) {
	int top = 0, k, i;
	int lines = scr_h / (FONT_H + 2) - 1;
	for (;;) {
		if (sel < top) top = sel;
		if (sel >= top + lines) top = sel - lines + 1;
		fill_rect(0, 0, scr_w, scr_h, RGB(18, 18, 28));
		fill_rect(0, 0, scr_w, FONT_H + 2, RGB(60, 60, 100));
		draw_text(4, 1, 1, title, RGB(255, 255, 255));
		for (i = 0; i < lines && top + i < n; i++) {
			int y = (i + 1) * (FONT_H + 2);
			if (top + i == sel) fill_rect(0, y, scr_w, FONT_H + 2, RGB(90, 120, 220));
			draw_text(4, y + 1, 1, items[top + i], RGB(255, 255, 255));
		}
		gfx_present();
		k = term_key(-1);
		if (k < 0) return -2;
		switch (k) {
		case KEYPAD_UP: case KEYPAD_2: sel = (sel + n - 1) % n; break;
		case KEYPAD_DOWN: case KEYPAD_8: sel = (sel + 1) % n; break;
		case KEYPAD_LEFT: case KEYPAD_4: sel = sel > lines ? sel - lines : 0; break;
		case KEYPAD_RIGHT: case KEYPAD_6: sel = sel + lines < n ? sel + lines : n - 1; break;
		case KEYPAD_CENTER: case KEYPAD_5: case KEYPAD_LSOFT: case KEYPAD_HASH: return sel;
		case KEYPAD_RSOFT: return -1;
		}
	}
}
