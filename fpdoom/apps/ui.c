#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

static const char *pending_note;

void ui_note(const char *msg) { pending_note = msg; }

void ui_header(const char *title) {
	fill_rect(0, 0, scr_w, scr_h, UI_BG);
	fill_rect(0, 0, scr_w, UI_BAR_H, UI_BAR);
	draw_text(4, 3, 1, title, UI_TEXT);
}

void ui_footer(const char *hint) {
	int y = scr_h - FONT_H - 4;
	fill_rect(0, y - 2, scr_w, FONT_H + 6, UI_BAR);
	if (pending_note) {
		draw_text(4, y, 1, pending_note, UI_OK);
		pending_note = NULL;
	} else {
		draw_text(4, y, 1, hint, UI_DIM);
	}
}

#define MAX_LINES 400

/* dictionary article styling, by the source line a wrapped line belongs to */
static uint16_t line_color(const char *text, int start) {
	const char *s = text + start;
	int i;
	while (s > text && s[-1] != '\n') s--;
	if (s[0] == ' ' && s[1] == ' ') return RGB(150, 185, 235);   /* example */
	if (s[0] == '-' && s[1] == '-' && s[2] == '-') return UI_DIM;
	if (s[0] >= '0' && s[0] <= '9') return UI_TEXT;               /* sense */
	/* "n.", "v. (went, gone) A1", "phr. v." */
	for (i = 0; i < 9 && s[i] >= 'a' && s[i] <= 'z'; i++);
	if (i && s[i] == '.' && (!s[i + 1] || s[i + 1] == ' ' || s[i + 1] == '\n')) return UI_ACCENT;
	return UI_TEXT;
}

int text_view(const char *title, const char *text, const char *footer,
		int (*on_key)(int key, void *ctx), void *ctx) {
	static int starts[MAX_LINES], lens[MAX_LINES];
	int cols = (scr_w - 8) / FONT_W, line_h = FONT_H + 2;
	int rows = (scr_h - UI_BAR_H * 2 - 8) / line_h;
	int n = wrap_text(text, cols, starts, lens, MAX_LINES), top = 0;

	for (;;) {
		int i, key;
		ui_header(title);
		for (i = 0; i < rows && top + i < n; i++)
			draw_text_n(4, UI_BAR_H + 4 + i * line_h, 1, text + starts[top + i],
					lens[top + i], line_color(text, starts[top + i]));
		if (n > rows) {
			/* scrollbar */
			int track = rows * line_h, h = track * rows / n, y = track * top / n;
			fill_rect(scr_w - 3, UI_BAR_H + 4, 2, track, RGB(50, 60, 90));
			fill_rect(scr_w - 3, UI_BAR_H + 4 + y, 2, h < 6 ? 6 : h, UI_DIM);
		}
		ui_footer(footer);
		gfx_present();
		key = wait_key();
		if (key < 0) return -1;
		if (key == KEYPAD_UP || key == KEYPAD_2) { if (top > 0) top--; }
		else if (key == KEYPAD_DOWN || key == KEYPAD_8) { if (top + rows < n) top++; }
		else if (key == KEYPAD_LEFT || key == KEYPAD_4) top = top > rows ? top - rows : 0;
		else if (key == KEYPAD_RIGHT || key == KEYPAD_6) {
			if (top + rows < n) top += rows;
			if (top + rows > n) top = n > rows ? n - rows : 0;
		} else if (key == KEYPAD_RSOFT) return 0;
		else if (on_key) {
			int r = on_key(key, ctx);
			if (r) return r < 0 ? -1 : 0;
		} else if (key == KEYPAD_CENTER || key == KEYPAD_5) return 0;
	}
}

int ui_pick(const char *title, const char *const *items, int n, int sel) {
	int row_h = FONT_H + 6, rows = (scr_h - UI_BAR_H * 2 - 8) / row_h, top = 0;
	for (;;) {
		int i, key;
		if (sel < top) top = sel;
		if (sel >= top + rows) top = sel - rows + 1;
		ui_header(title);
		for (i = 0; i < rows && top + i < n; i++) {
			int y = UI_BAR_H + 6 + i * row_h;
			if (top + i == sel) fill_rect(0, y - 3, scr_w, row_h, UI_SEL);
			draw_text(6, y, 1, items[top + i], UI_TEXT);
		}
		ui_footer("5 - выбрать  П.софт - назад");
		gfx_present();
		key = wait_key();
		if (key < 0) return -2;
		if (key == KEYPAD_UP || key == KEYPAD_2) sel = (sel + n - 1) % n;
		else if (key == KEYPAD_DOWN || key == KEYPAD_8) sel = (sel + 1) % n;
		else if (key == KEYPAD_LEFT || key == KEYPAD_4) sel = sel > rows ? sel - rows : 0;
		else if (key == KEYPAD_RIGHT || key == KEYPAD_6) sel = sel + rows < n ? sel + rows : n - 1;
		else if (key == KEYPAD_CENTER || key == KEYPAD_5 || key == KEYPAD_LSOFT) return sel;
		else if (key == KEYPAD_RSOFT) return -1;
	}
}

/* a field with the text and a cursor, under the header */
static void draw_field(const char *title, const char *text, const char *hint, const char *footer) {
	int y = UI_BAR_H + 8;
	ui_header(title);
	draw_text(6, y, 1, hint, UI_DIM);
	y += FONT_H + 6;
	fill_rect(4, y - 3, scr_w - 8, FONT_H + 6, RGB(40, 46, 70));
	draw_text(draw_text(8, y, 1, text, UI_TEXT), y, 1, "_", UI_ACCENT);
	ui_footer(footer);
	gfx_present();
}

int ui_number(const char *title, const char *hint, int max) {
	char buf[12] = "";
	int n = 0;
	for (;;) {
		int key;
		draw_field(title, buf, hint, "OK - ввод  # стереть  П-назад");
		key = wait_key();
		if (key < 0) return -2;
		if (key == KEYPAD_RSOFT) return -1;
		if (key >= KEYPAD_0 && key <= KEYPAD_9 && n < 9) {
			buf[n++] = '0' + key - KEYPAD_0;
			buf[n] = 0;
			if (atoi(buf) > max) buf[--n] = 0;
		} else if (key == KEYPAD_HASH && n) {
			buf[--n] = 0;
		} else if ((key == KEYPAD_CENTER || key == KEYPAD_LSOFT) && n) {
			return atoi(buf);
		}
	}
}

/* multi-tap letters: the same key again within MULTITAP_MS changes the letter */
#define MULTITAP_MS 1000
static const char *const tap_en[10] = {
	" ", ".,-'?!", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz" };
static const char *const tap_ru[10] = {
	" ", ".,-'?!", "абвг", "деёжз", "ийкл", "мноп", "рсту", "фхцч", "шщъы", "ьэюя" };

/* the n-th UTF-8 character of s, its length in *len */
static const char *utf8_nth(const char *s, int n, int *len) {
	const char *p = s;
	while (n-- && *p) utf8_next(&p);
	s = p;
	utf8_next(&p);
	*len = p - s;
	return s;
}

int ui_input(const char *title, char *buf, int size) {
	int len = strlen(buf), last = -1, idx = 0, ru = 1, prev_len = 0;
	unsigned t = 0;
	for (;;) {
		int key;
		draw_field(title, buf, ru ? "Раскладка: русская (* - англ.)" : "Layout: English (* - рус.)",
				"OK ввод  # стереть  > буква");
		key = wait_key();
		if (key < 0) return -2;
		if (key == KEYPAD_RSOFT) return -1;
		if ((key == KEYPAD_CENTER || key == KEYPAD_LSOFT) && len) return len;
		if (key == KEYPAD_STAR) { ru = !ru; last = -1; continue; }
		if (key == KEYPAD_HASH) {
			while (len && ((unsigned char)buf[len - 1] & 0xc0) == 0x80) len--;
			if (len) len--;
			buf[len] = 0;
			last = -1;
		} else if (key == KEYPAD_RIGHT) {
			last = -1;
		} else if (key >= KEYPAD_0 && key <= KEYPAD_9) {
			int d = key - KEYPAD_0, n, l;
			const char *set = (ru ? tap_ru : tap_en)[d], *c;
			n = text_len(set);
			if (d == last && sys_timer_ms() - t < MULTITAP_MS) {
				len = prev_len;  /* replace the letter just typed */
				idx = (idx + 1) % n;
			} else {
				prev_len = len;
				idx = 0;
			}
			c = utf8_nth(set, idx, &l);
			if (prev_len + l < size) {
				memcpy(buf + prev_len, c, l);
				len = prev_len + l;
				buf[len] = 0;
			}
			last = d;
			t = sys_timer_ms();
		}
	}
}

int ui_message(const char *title, const char *text) {
	return text_view(title, text, "5 или П.софт - назад", NULL, NULL);
}

char *file_load(const char *path, long *size) {
	FILE *fp = fopen(path, "rb");
	char *buf;
	long n;
	if (!fp) return NULL;
	fseek(fp, 0, SEEK_END);
	n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	buf = n >= 0 ? malloc(n + 1) : NULL;
	if (buf) {
		n = fread(buf, 1, n, fp);
		buf[n] = 0;
		if (size) *size = n;
	}
	fclose(fp);
	return buf;
}

char *next_line(char **pos) {
	char *s = *pos, *e;
	if (!*s) return NULL;
	for (e = s; *e && *e != '\n'; e++);
	*pos = *e ? e + 1 : e;
	*e = 0;
	if (e > s && e[-1] == '\r') e[-1] = 0;
	return s;
}

char *next_field(char **p) {
	char *s = *p, *t;
	if (!s) return "";
	if ((t = strchr(s, '\t'))) *t = 0, *p = t + 1;
	else *p = NULL;
	return s;
}

void utf8_trim(char *s) {
	int n = strlen(s), i = n, need;
	while (i > 0 && ((unsigned char)s[i - 1] & 0xc0) == 0x80) i--;
	if (!i) return;
	need = (unsigned char)s[i - 1] >= 0xf0 ? 4 : (unsigned char)s[i - 1] >= 0xe0 ? 3 :
			(unsigned char)s[i - 1] >= 0xc0 ? 2 : 1;
	if (n - (i - 1) < need) s[i - 1] = 0;
}

const char *str_find(const char *s, const char *sub) {
	int n = strlen(sub);
	for (; *s; s++) if (!strncmp(s, sub, n)) return s;
	return NULL;
}
