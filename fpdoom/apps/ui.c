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

const char *str_find(const char *s, const char *sub) {
	int n = strlen(sub);
	for (; *s; s++) if (!strncmp(s, sub, n)) return s;
	return NULL;
}
