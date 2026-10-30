#include "term.h"
#include "pz_ui.h"

static const uint8_t font16[] = {
#include "font_ascii.h"
};

void pz_init(void) { gfx_init(); }
void pz_present(void) { gfx_present(); }
void pz_wait_ms(unsigned ms) { sys_wait_ms(ms); }
unsigned pz_time_ms(void) { return sys_timer_ms(); }

int pz_poll_key(void) {
	int k = poll_key();
	switch (k) {
	case -1: return -1;
	case KEYPAD_UP: return PZ_UP;
	case KEYPAD_DOWN: return PZ_DOWN;
	case KEYPAD_LEFT: return PZ_LEFT;
	case KEYPAD_RIGHT: return PZ_RIGHT;
	case KEYPAD_CENTER: return PZ_CENTER;
	case KEYPAD_STAR: return PZ_STAR;
	case KEYPAD_HASH: return PZ_HASH;
	case KEYPAD_LSOFT: return PZ_LSOFT;
	case KEYPAD_RSOFT: return PZ_RSOFT;
	}
	if (k >= KEYPAD_0 && k <= KEYPAD_9) return PZ_DIGIT0 + k - KEYPAD_0;
	return 0;
}

const uint8_t *pz_glyph(unsigned ch) {
	return font16 + ((ch >= 0x20 && ch < 0x80 ? ch : '?') - 0x20) * FONT_H;
}

void pz_text(int x, int y, const char *s, uint16_t colour) { draw_text(x, y, 1, s, colour); }
void pz_fill(int x, int y, int w, int h, uint16_t colour) { fill_rect(x, y, w, h, colour); }

int pz_pick(const char *title, const char *const *items, int n) {
	return term_pick(title, items, n, 0);
}

void pz_note(const char *msg) {
	fill_rect(0, scr_h / 2 - FONT_H, scr_w, 2 * FONT_H + 8, RGB(60, 60, 100));
	draw_text_wrap(4, scr_h / 2 - FONT_H + 4, scr_w - 8, 1, msg, RGB(255, 255, 255));
	gfx_present();
	wait_key();
}
