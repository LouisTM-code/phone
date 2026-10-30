/* screen and keys for puzzles_fp.c, which can't include gfx.h: the
   puzzles have their own draw_text() */
#include <stdint.h>

#define PZ_RGB(r, g, b) ((r) >> 3 << 11 | (g) >> 2 << 5 | (b) >> 3)
#define PZ_FONT_W 8
#define PZ_FONT_H 16

extern uint16_t *framebuf;
extern unsigned scr_w, scr_h;

/* key codes the puzzles use */
enum { PZ_UP = 1, PZ_DOWN, PZ_LEFT, PZ_RIGHT, PZ_CENTER, PZ_STAR, PZ_HASH,
	PZ_LSOFT, PZ_RSOFT, PZ_DIGIT0 = 0x30 };

void pz_init(void);
void pz_present(void);
/* PZ_* or a digit '0'..'9', 0 = none, -1 = quit */
int pz_poll_key(void);
void pz_wait_ms(unsigned ms);
unsigned pz_time_ms(void);
/* the 8x16 font glyph of an ASCII character */
const uint8_t *pz_glyph(unsigned ch);
/* text on a bar, UTF-8 */
void pz_text(int x, int y, const char *s, uint16_t colour);
void pz_fill(int x, int y, int w, int h, uint16_t colour);
/* list picker: index, -1 cancelled, -2 quit */
int pz_pick(const char *title, const char *const *items, int n);
void pz_note(const char *msg);
