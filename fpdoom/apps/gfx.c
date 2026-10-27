#include <stdlib.h>
#include <string.h>
#include "gfx.h"

static const uint8_t font_ascii[] = {
#include "font_ascii.h"
};
static const uint8_t font_cyr[] = {
#include "font_cyr.h"
};

uint16_t *framebuf;
unsigned scr_w, scr_h;
static void *framebuf_mem;

void lcd_appinit(void) {
	struct sys_display *disp = &sys_data.display;
	disp->w2 = disp->w1;
	disp->h2 = disp->h1;
}

void keytrn_init(void) {
	uint8_t keymap[64];
	int i;
	sys_getkeymap(keymap);
	/* pass raw keypad codes, bit 15 marks the power key combination */
	for (i = 0; i < 64; i++) {
		sys_data.keytrn[0][i] = keymap[i];
		sys_data.keytrn[1][i] = keymap[i] | 1 << 15;
	}
}

void gfx_init(void) {
	struct sys_display *disp = &sys_data.display;
	scr_w = disp->w2;
	scr_h = disp->h2;
	framebuf_mem = malloc(scr_w * scr_h * 2 + 31);
	framebuf = (uint16_t*)(((intptr_t)framebuf_mem + 31) & ~31);
	memset(framebuf, 0, scr_w * scr_h * 2);
	sys_framebuffer(framebuf);
	sys_start();
}

void gfx_free(void) {
	free(framebuf_mem);
}

void gfx_present(void) {
	sys_start_refresh();
	sys_wait_refresh();
}

void fill_rect(int x, int y, int w, int h, uint16_t c) {
	int i, j;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > (int)scr_w) w = scr_w - x;
	if (y + h > (int)scr_h) h = scr_h - y;
	for (j = 0; j < h; j++) {
		uint16_t *p = framebuf + (y + j) * scr_w + x;
		for (i = 0; i < w; i++) p[i] = c;
	}
}

unsigned utf8_next(const char **ps) {
	const uint8_t *s = (const uint8_t*)*ps;
	unsigned c = *s++;
	if (c >= 0xc0 && c < 0xe0 && (*s & 0xc0) == 0x80) {
		c = (c & 0x1f) << 6 | (*s++ & 0x3f);
	} else if (c >= 0xe0 && c < 0xf0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80) {
		c = (c & 0x0f) << 12 | (s[0] & 0x3f) << 6 | (s[1] & 0x3f);
		s += 2;
	} else if (c >= 0x80) {
		c = '?';
	}
	*ps = (const char*)s;
	return c;
}

int text_len(const char *s) {
	int n = 0;
	while (*s) { utf8_next(&s); n++; }
	return n;
}

static const uint8_t *glyph(unsigned c) {
	if (c >= 0x20 && c < 0x80) return font_ascii + (c - 0x20) * FONT_H;
	if (c >= 0x410 && c < 0x450) return font_cyr + (c - 0x410) * FONT_H;
	if (c == 0x401) return font_cyr + 64 * FONT_H;
	if (c == 0x451) return font_cyr + 65 * FONT_H;
	return font_ascii + ('?' - 0x20) * FONT_H;
}

static void draw_glyph(int x, int y, int s, unsigned ch, uint16_t c) {
	const uint8_t *g = glyph(ch);
	int i, j;
	for (j = 0; j < FONT_H; j++) {
		unsigned bits = g[j];
		for (i = 0; bits; i++, bits = bits << 1 & 0xff)
			if (bits & 0x80) fill_rect(x + i * s, y + j * s, s, s, c);
	}
}

int draw_text(int x, int y, int s, const char *str, uint16_t c) {
	while (*str) {
		draw_glyph(x, y, s, utf8_next(&str), c);
		x += FONT_W * s;
	}
	return x;
}

void draw_text_center(int y, int s, const char *str, uint16_t c) {
	draw_text(((int)scr_w - text_len(str) * FONT_W * s) / 2, y, s, str, c);
}

int draw_text_wrap(int x, int y, int w, int s, const char *str, uint16_t c) {
	int max_chars = w / (FONT_W * s), col = 0;
	if (max_chars < 1) max_chars = 1;
	while (*str) {
		const char *word = str, *p = str;
		int len = 0;
		while (*p && *p != ' ') { utf8_next(&p); len++; }
		if (col && col + 1 + len > max_chars) {
			col = 0;
			y += FONT_H * s + 2;
		} else if (col) {
			col++;
		}
		/* words longer than a line are broken anywhere */
		while (word < p) {
			if (col >= max_chars) { col = 0; y += FONT_H * s + 2; }
			draw_glyph(x + col * FONT_W * s, y, s, utf8_next(&word), c);
			col++;
		}
		str = p;
		while (*str == ' ') str++;
	}
	return y + FONT_H * s + 2;
}

int draw_text_n(int x, int y, int s, const char *str, int nbytes, uint16_t c) {
	const char *end = str + nbytes;
	while (str < end && *str) {
		draw_glyph(x, y, s, utf8_next(&str), c);
		x += FONT_W * s;
	}
	return x;
}

int wrap_text(const char *text, int cols, int *starts, int *lens, int max) {
	const char *p = text;
	int n = 0;
	if (cols < 1) cols = 1;
	while (*p && n < max) {
		const char *line = p, *brk = NULL, *q = p;
		int col = 0;
		while (*q && *q != '\n' && col < cols) {
			if (*q == ' ') brk = q;
			utf8_next(&q);
			col++;
		}
		if (*q && *q != '\n' && *q != ' ' && brk) q = brk;
		starts[n] = line - text;
		lens[n] = q - line;
		n++;
		p = q;
		if (*p == '\n' || *p == ' ') p++;
	}
	return n;
}

int poll_key(void) {
	int type, key;
	while ((type = sys_event(&key)) != EVENT_END) {
		if (type == EVENT_QUIT) return -1;
		if (type == EVENT_KEYDOWN && !(key >> 15)) return key & 0x7fff;
	}
	return 0;
}

int wait_key(void) {
	int key;
	while (!(key = poll_key())) sys_wait_ms(10);
	return key;
}
