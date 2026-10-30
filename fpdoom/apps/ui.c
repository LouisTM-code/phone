#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

static const char *pending_note;

void ui_note(const char *msg) { pending_note = msg; }

uint16_t ui_color = UI_CARDS;
static int ui_icon = ICON_BOOK;

void ui_app(uint16_t color, int icon) { ui_color = color; ui_icon = icon; }

/* the bytes of the first cols characters of s */
static int fit(const char *s, int cols) {
	const char *p = s;
	while (*p && cols-- > 0) utf8_next(&p);
	return p - s;
}

void ui_header(const char *title) {
	fill_rect(0, 0, scr_w, scr_h, UI_BG);
	fill_rect(4, 2, 18, 18, ui_color);
	draw_icon(5, 3, 1, ui_icon, UI_TEXT);
	draw_text_style(28, 3, TEXT_BOLD, title, fit(title, (scr_w - 32) / FONT_W), UI_TEXT);
	fill_rect(0, UI_BAR_H - 1, scr_w, 1, UI_LINE);
}

void ui_footer(const char *hint) {
	int y = scr_h - FONT_H - 4;
	fill_rect(0, y - 2, scr_w, FONT_H + 6, UI_BG);
	fill_rect(0, y - 2, scr_w, 1, UI_LINE);
	if (pending_note) {
		draw_text(4, y, 1, pending_note, UI_OK);
		pending_note = NULL;
	} else {
		draw_text(4, y, 1, hint, UI_DIM);
	}
}

void ui_tile(int x, int y, int w, int h, int sel) {
	if (sel) {
		fill_rect(x - 2, y - 2, w + 4, h + 4, UI_TEXT);
		fill_rect(x, y, w, h, ui_color);
	} else {
		fill_rect(x, y, w, h, UI_CARD);
	}
}

void ui_button(int i, const char *text, uint16_t c, int def) {
	int w = (scr_w - 24) / 2, x = 8 + i % 2 * (w + 8), y = UI_BTN_TOP + i / 2 * (UI_BTN_H + 4);
	fill_rect(x, y, w, UI_BTN_H, def ? c : UI_CARD);
	if (!def) fill_rect(x, y, 3, UI_BTN_H, c);
	draw_text_style(x + 8, y + 5, def ? TEXT_BOLD : 0, text, -1, def ? UI_INK : c);
}

void ui_progress(int x, int y, int w, int h, int n, int max, uint16_t c) {
	fill_rect(x, y, w, h, UI_LINE);
	if (max > 0) fill_rect(x, y, w * (n < max ? n : max) / max, h, c);
}

void date_text(int days, char *buf) {
	int z = days + 719468, era = z / 146097, doe = z - era * 146097;
	int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	int d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
	sprintf(buf, "%02d.%02d.%d", d, m, yoe + era * 400 + (m <= 2));
}

/* the colour darkened to k/8 */
static uint16_t dim(uint16_t c, int k) {
	return ((c >> 11) * k / 8) << 11 | ((c >> 5 & 63) * k / 8) << 5 | (c & 31) * k / 8;
}

#define GRID_COLS 2

/* s cut to w pixels, centred in x..x + w */
static void draw_fit(int x, int w, int y, int style, const char *s, uint16_t c) {
	int cols = w / FONT_W, k = text_len(s);
	if (k > cols) k = cols;
	draw_text_style(x + (w - k * FONT_W) / 2, y, style, s, fit(s, cols), c);
}

static void draw_grid(const char *left, const char *title, const ui_item_t *items, int n, int sel) {
	int i, gap = 8, w = (scr_w - gap * 3) / GRID_COLS, y0 = UI_BAR_H + 6;
	int h = (scr_h - y0 - UI_BAR_H * 2 - 6) / ((n + GRID_COLS - 1) / GRID_COLS) - 6, note;
	uint16_t keep = ui_color;
	char num[12];
	fill_rect(0, 0, scr_w, scr_h, UI_BG);
	if (left) draw_text(6, 3, 1, left, UI_DIM);
	draw_text_style(scr_w - 6 - text_len(title) * FONT_W, 3, TEXT_BOLD, title, -1, UI_TEXT);
	fill_rect(0, UI_BAR_H - 1, scr_w, 1, UI_LINE);
	for (i = 0; i < n; i++) {
		int x = gap + i % GRID_COLS * (w + gap), y = y0 + i / GRID_COLS * (h + 6), s = i == sel;
		ui_color = items[i].color;
		ui_tile(x, y, w, h, s);
		sprintf(num, "%d", i + 1);
		draw_text(x + 4, y + 2, 1, num, s ? dim(items[i].color, 4) : UI_DIM);
		draw_icon(x + (w - 32) / 2, y + h / 2 - 28, 2, items[i].icon, s ? UI_TEXT : items[i].color);
		draw_fit(x + 4, w - 8, y + h - FONT_H - 8, TEXT_BOLD, items[i].name, s ? UI_INK : UI_TEXT);
	}
	ui_color = keep;
	draw_fit(0, scr_w, scr_h - UI_BAR_H * 2 + 2, 0, items[sel].desc, UI_DIM);
	/* a note takes the whole footer */
	note = pending_note != NULL;
	ui_footer("5 - выбрать");
	if (!note) {
		sprintf(num, "1-%d", n);
		draw_text(scr_w - 6 - text_len(num) * FONT_W, scr_h - FONT_H - 4, 1, num, UI_DIM);
	}
	gfx_present();
}

int ui_grid(const char *left, const char *title, const ui_item_t *items, int n, int sel) {
	for (;;) {
		int key, col = sel % GRID_COLS;
		draw_grid(left, title, items, n, sel);
		key = wait_key();
		if (key < 0) return -2;
		switch (key) {
		case KEYPAD_LEFT: sel = (sel + n - 1) % n; break;
		case KEYPAD_RIGHT: sel = (sel + 1) % n; break;
		/* up and down stay in the column, wrapping to its other end */
		case KEYPAD_UP:
			sel = sel >= GRID_COLS ? sel - GRID_COLS : col + (n - 1 - col) / GRID_COLS * GRID_COLS;
			break;
		case KEYPAD_DOWN: sel = sel + GRID_COLS < n ? sel + GRID_COLS : col; break;
		case KEYPAD_CENTER: case KEYPAD_5: case KEYPAD_LSOFT: return sel;
		case KEYPAD_RSOFT: return -1;
		default:
			if (key >= KEYPAD_1 && key < KEYPAD_1 + n) return key - KEYPAD_1;
		}
	}
}

#define MAX_LINES 400

uint16_t ui_line_color(const char *text, int start) {
	const char *s = text + start;
	int i;
	while (s > text && s[-1] != '\n') s--;
	if (s[0] == ' ' && s[1] == ' ') return RGB(150, 185, 235);   /* example */
	if (s[0] == '-' && s[1] == '-' && s[2] == '-') return UI_DIM;
	if (s[0] >= '0' && s[0] <= '9') return UI_TEXT;               /* sense */
	if (s[0] == '/') return UI_IPA;                               /* "/grɑːsp/" */
	if (!strncmp(s, FAMILY_LABEL, sizeof(FAMILY_LABEL) - 1)) return RGB(200, 160, 240);
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
					lens[top + i], ui_line_color(text, starts[top + i]));
		if (n > rows) {
			/* scrollbar */
			int track = rows * line_h, h = track * rows / n, y = track * top / n;
			fill_rect(scr_w - 3, UI_BAR_H + 4, 2, track, UI_LINE);
			fill_rect(scr_w - 3, UI_BAR_H + 4 + y, 2, h < 6 ? 6 : h, ui_color);
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
	return ui_pick_keys(title, items, n, sel, "5 - выбрать  П.софт - назад", NULL, NULL);
}

int ui_pick_keys(const char *title, const char *const *items, int n, int sel,
		const char *footer, const int *keys, int *key_out) {
	int row_h = FONT_H + 6, rows = (scr_h - UI_BAR_H * 2 - 8) / row_h, top = 0;
	if (key_out) *key_out = 0;
	for (;;) {
		int i, key;
		if (sel >= n) sel = n - 1;
		if (sel < 0) sel = 0;
		if (sel < top) top = sel;
		if (sel >= top + rows) top = sel - rows + 1;
		ui_header(title);
		for (i = 0; i < rows && top + i < n; i++) {
			int y = UI_BAR_H + 6 + i * row_h;
			int s = top + i == sel;
			if (s) fill_rect(4, y - 3, scr_w - 8, row_h, UI_SEL);
			draw_text_style(10, y, s ? TEXT_BOLD : 0, items[top + i],
					fit(items[top + i], (scr_w - 20) / FONT_W), s ? UI_INK : UI_TEXT);
		}
		if (!n) draw_text_center(UI_BAR_H + 24, 1, "Пусто", UI_DIM);
		ui_footer(footer);
		gfx_present();
		key = wait_key();
		if (key < 0) return -2;
		for (i = 0; keys && keys[i]; i++)
			if (key == keys[i]) break;
		if (keys && keys[i]) {
			*key_out = key;
			return n ? sel : -1;
		}
		if (!n) {
			if (key == KEYPAD_RSOFT) return -1;
			continue;
		}
		if (key == KEYPAD_UP || key == KEYPAD_2) sel = (sel + n - 1) % n;
		else if (key == KEYPAD_DOWN || key == KEYPAD_8) sel = (sel + 1) % n;
		else if (key == KEYPAD_LEFT || key == KEYPAD_4) sel = sel > rows ? sel - rows : 0;
		else if (key == KEYPAD_RIGHT || key == KEYPAD_6) sel = sel + rows < n ? sel + rows : n - 1;
		else if (key == KEYPAD_CENTER || key == KEYPAD_5 || key == KEYPAD_LSOFT) return sel;
		else if (key == KEYPAD_RSOFT) return -1;
	}
}

/* a field with the text and a cursor, under the header and the prompt;
   a long text wraps */
#define FIELD_MAX 1024
#define FIELD_LINES 32
static void draw_field(const char *title, const char *prompt, const char *text,
		const char *hint, const char *footer) {
	static char s[FIELD_MAX + 2];
	int starts[FIELD_LINES], lens[FIELD_LINES], n, i, line_h = FONT_H + 2;
	int y = UI_BAR_H + 8;
	ui_header(title);
	if (prompt) y = draw_text_wrap(8, y, scr_w - 16, 1, prompt, UI_TEXT) + 8;
	draw_text(6, y, 1, hint, UI_DIM);
	y += FONT_H + 6;
	/* the cursor is the last character, so it follows a typed space */
	snprintf(s, sizeof(s), "%s_", text);
	n = wrap_text(s, (scr_w - 16) / FONT_W, starts, lens, FIELD_LINES);
	fill_rect(4, y - 5, scr_w - 8, n * line_h + 8, ui_color);
	fill_rect(6, y - 3, scr_w - 12, n * line_h + 4, UI_CARD);
	for (i = 0; i < n; i++, y += line_h) {
		int len = lens[i] - (i == n - 1);
		int x = draw_text_n(8, y, 1, s + starts[i], len, UI_TEXT);
		if (i == n - 1) draw_text(x, y, 1, "_", ui_color);
	}
	ui_footer(footer);
	gfx_present();
}

int ui_number(const char *title, const char *hint, int max) {
	char buf[12] = "";
	int n = 0;
	for (;;) {
		int key;
		draw_field(title, NULL, buf, hint, "OK - ввод  # стереть  П-назад");
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

int ui_input(const char *title, const char *prompt, char *buf, int size, int ru) {
	int len = strlen(buf), last = -1, idx = 0, prev_len = 0;
	unsigned t = 0;
	for (;;) {
		int key;
		draw_field(title, prompt, buf, ru ? "Раскладка: русская (* - англ.)" : "Layout: English (* - рус.)",
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
