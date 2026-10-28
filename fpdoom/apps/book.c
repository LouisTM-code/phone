#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "book.h"

uint16_t book_pal[BC_COUNT];
uint16_t book_bg;

static int read_at(book_t *b, uint32_t off, void *buf, unsigned n) {
	if (fseek(b->f, off, SEEK_SET)) return -1;
	return fread(buf, 1, n, b->f) == n ? 0 : -1;
}

int book_open(book_t *b, const char *path) {
	uint8_t h[64], m[258];
	unsigned n;
	char *nl;
	memset(b, 0, sizeof(*b));
	if (!(b->f = fopen(path, "rb"))) return -1;
	if (fread(h, 1, 64, b->f) != 64 || memcmp(h, "FPBK", 4) || bk_u16(h + 4) != 2)
		goto err;
	b->page_w = bk_u16(h + 6);
	b->page_h = bk_u16(h + 8);
	b->npages = bk_u32(h + 12);
	b->pages_off = bk_u32(h + 16);
	b->toc_off = bk_u32(h + 20);
	b->images_off = bk_u32(h + 24);
	if (!b->npages || read_at(b, bk_u32(h + 32), m, 2)) goto err;
	n = bk_u16(m);
	if (n > 256) n = 256;
	if (fread(m, 1, n, b->f) != n) goto err;
	m[n] = 0;
	if ((nl = strchr((char*)m, '\n'))) {
		*nl = 0;
		snprintf(b->author, sizeof(b->author), "%s", nl + 1);
	}
	snprintf(b->title, sizeof(b->title), "%s", (char*)m);
	return 0;
err:
	fclose(b->f);
	b->f = NULL;
	return -1;
}

void book_close(book_t *b) {
	if (b->f) fclose(b->f);
	free(b->page);
	memset(b, 0, sizeof(*b));
}

int book_load(book_t *b, unsigned page) {
	uint8_t e[24];
	unsigned size;
	if (page >= b->npages || read_at(b, b->pages_off + page * 12, e, 24)) return -1;
	size = bk_u32(e + 12) - bk_u32(e);
	if (bk_u32(e + 12) < bk_u32(e) || size > (1 << 20)) return -1;
	if (size > b->page_cap) {
		uint8_t *p = realloc(b->page, size);
		if (!p) return -1;
		b->page = p;
		b->page_cap = size;
	}
	if (read_at(b, bk_u32(e), b->page, size)) return -1;
	b->page_size = size;
	return size;
}

book_anchor_t book_anchor(book_t *b, unsigned page) {
	uint8_t e[12];
	book_anchor_t a = { 0, 0 };
	if (!read_at(b, b->pages_off + page * 12, e, 12)) {
		a.blk = bk_u32(e + 4);
		a.ch = bk_u32(e + 8);
	}
	return a;
}

static int anchor_le(book_anchor_t a, book_anchor_t b) {
	return a.blk < b.blk || (a.blk == b.blk && a.ch <= b.ch);
}

unsigned book_find(book_t *b, book_anchor_t a) {
	unsigned lo = 0, hi = b->npages - 1;
	while (lo < hi) {
		unsigned mid = (lo + hi + 1) >> 1;
		if (anchor_le(book_anchor(b, mid), a)) lo = mid;
		else hi = mid - 1;
	}
	return lo;
}

/* an item that doesn't fit in the page (a damaged file) ends the page, so
   the callers may read the whole item */
int book_item(const uint8_t **pp, const uint8_t *end) {
	const uint8_t *p = *pp;
	int t, n;
	if (p >= end) return BK_END;
	t = *p++;
	switch (t) {
	case BK_TEXT:
		if (end - p < 4) return BK_END;
		n = p[3];
		p += 4;
		while (n--) {
			if (end - p < 4 || end - p < 4 + p[3]) return BK_END;
			p += 4 + p[3];
		}
		break;
	case BK_RECT: p += 9; break;
	case BK_IMAGE: p += 13; break;
	case BK_SEARCH:
		if (end - p < 2) return BK_END;
		p += 2 + bk_u16(p);
		break;
	default: return BK_END;
	}
	if (p > end) return BK_END;
	*pp = p;
	return t;
}

void book_draw(book_t *b, int x, int y) {
	const uint8_t *p = b->page, *end = p + b->page_size, *it;
	int t;
	while (it = p, (t = book_item(&p, end)) != BK_END) {
		it++;
		if (t == BK_TEXT) {
			int ty = y + bk_s16(it), n = it[3];
			it += 4;
			while (n--) {
				int st = it[2];
				draw_text_style(x + bk_s16(it), ty, st & 7, (const char*)it + 4, it[3],
						book_pal[st >> 4 < BC_COUNT ? st >> 4 : 0]);
				it += 4 + it[3];
			}
		} else if (t == BK_RECT) {
			fill_rect(x + bk_s16(it), y + bk_s16(it + 2), bk_u16(it + 4), bk_u16(it + 6),
					book_pal[it[8] < BC_COUNT ? it[8] : 0]);
		} else if (t == BK_IMAGE) {
			int w, h;
			uint32_t id = bk_u32(it + 4);
			if (!book_image_size(b, id, &w, &h))
				book_image(b, id, x + bk_s16(it), y + bk_s16(it + 2), 0, 0, w, h,
						book_pal[it[12] < BC_COUNT ? it[12] : 0], 1);
		}
	}
}

/* blob: u16 w, u16 h, u8 kind, 3 pad, u32 row offsets[h + 1] */
static uint32_t image_blob(book_t *b, uint32_t id, uint8_t *head) {
	uint8_t e[4];
	uint32_t off;
	if (read_at(b, b->images_off, e, 4) || id >= bk_u32(e)) return 0;
	if (read_at(b, b->images_off + 4 + id * 4, e, 4)) return 0;
	off = bk_u32(e);
	return read_at(b, off, head, 8) ? 0 : off;
}

int book_image_size(book_t *b, uint32_t id, int *w, int *h) {
	uint8_t head[8];
	if (!image_blob(b, id, head)) return -1;
	*w = bk_u16(head);
	*h = bk_u16(head + 2);
	return 0;
}

static uint16_t blend(uint16_t bg, uint16_t fg, unsigned a) {
	int r = bg >> 11, g = bg >> 5 & 63, bl = bg & 31;
	r += ((fg >> 11) - r) * (int)a / 15;
	g += ((fg >> 5 & 63) - g) * (int)a / 15;
	bl += ((fg & 31) - bl) * (int)a / 15;
	return r << 11 | g << 5 | bl;
}

static void put(int x, int y, int zoom, unsigned v, int kind, uint16_t c) {
	uint16_t *d = framebuf + y * scr_w + x;
	if (kind != BK_IMG_RGB) {
		if (!v) return;
		v = v >= 15 ? c : blend(*d, c, v);
	}
	d[0] = v;
	if (zoom == 2) d[1] = d[scr_w] = d[scr_w + 1] = v;
}

void book_image(book_t *b, uint32_t id, int x, int y, int sx, int sy, int w, int h,
		uint16_t c, int zoom) {
	uint8_t head[8], *rows = NULL, *buf = NULL;
	uint32_t off;
	int iw, ih, kind, j, maxrow = 0, k = zoom > 1 ? zoom : 1, q = zoom < -1 ? -zoom : 1;
	if (!(off = image_blob(b, id, head))) return;
	iw = bk_u16(head);
	ih = bk_u16(head + 2);
	kind = head[4];
	/* clip to the picture, then to the screen; w, h are in picture pixels,
	   drawn k times larger or q times smaller */
	if (sx < 0) { x -= sx * k / q; w += sx; sx = 0; }
	if (sy < 0) { y -= sy * k / q; h += sy; sy = 0; }
	if (sx + w > iw) w = iw - sx;
	if (sy + h > ih) h = ih - sy;
	if (x < 0) { int d = (-x * q + k - 1) / k; sx += d; w -= d; x += d * k / q; }
	if (y < 0) { int d = (-y * q + k - 1) / k; sy += d; h -= d; y += d * k / q; }
	if (x + w * k / q > (int)scr_w) w = ((int)scr_w - x) * q / k;
	if (y + h * k / q > (int)scr_h) h = ((int)scr_h - y) * q / k;
	if (w <= 0 || h <= 0) return;
	rows = malloc((h + 1) * 4);
	if (!rows || read_at(b, off + 8 + sy * 4, rows, (h + 1) * 4)) goto end;
	for (j = 0; j < h; j++) {
		int n = bk_u32(rows + j * 4 + 4) - bk_u32(rows + j * 4);
		if (n < 0) goto end;
		if (n > maxrow) maxrow = n;
	}
	if (!(buf = malloc(maxrow)) || fseek(b->f, off + bk_u32(rows), SEEK_SET)) goto end;
	for (j = 0; j < h; j++) {
		int n = bk_u32(rows + j * 4 + 4) - bk_u32(rows + j * 4), px = 0, size = kind == BK_IMG_RGB ? 2 : 1;
		int dy = y + j * k / q;
		const uint8_t *p = buf, *e = buf + n;
		if ((int)fread(buf, 1, n, b->f) != n) break;
		if (j % q) continue;
		while (p < e && px < sx + w) {
			int cnt = *p++, rep = cnt >= 128, i;
			cnt = rep ? cnt - 126 : cnt + 1;
			if (e - p < (rep ? 1 : cnt) * size) break;
			for (i = 0; i < cnt && px < sx + w; i++, px++) {
				unsigned v = size == 2 ? bk_u16(p) : *p;
				if (!rep) p += size;
				if (px >= sx && !((px - sx) % q))
					put(x + (px - sx) * k / q, dy, k, v, kind, c);
			}
			if (rep) p += size;
			else p += (cnt - i) * size;
		}
	}
end:
	free(rows);
	free(buf);
}

uint8_t *book_toc(book_t *b, unsigned *count) {
	uint8_t e[4], *buf, *p;
	uint32_t size = b->images_off - b->toc_off, n, i;
	*count = 0;
	if (size < 4 || size > (1 << 20) || read_at(b, b->toc_off, e, 4)) return NULL;
	if (!(buf = malloc(size))) return NULL;
	if (fread(buf, 1, size - 4, b->f) != size - 4) {
		free(buf);
		return NULL;
	}
	/* only the entries that fit, with a level of 1-4 and a page of the book */
	n = bk_u32(e);
	for (i = 0, p = buf; i < n && p + 6 <= buf + size - 4 && p + 6 + p[5] <= buf + size - 4 &&
			p[4] >= 1 && p[4] <= 4 && bk_u32(p) < b->npages; i++)
		p += 6 + p[5];
	*count = i;
	return buf;
}
