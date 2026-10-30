#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include "syscode.h"

#define X(num, name) KEYPAD_##name = num,
enum { KEYPAD_ENUM(X) };
#undef X

/* RGB565 */
#define RGB(r, g, b) ((r) >> 3 << 11 | (g) >> 2 << 5 | (b) >> 3)

#define FONT_W 8
#define FONT_H 16

extern uint16_t *framebuf;
extern unsigned scr_w, scr_h;

void gfx_init(void);
void gfx_free(void);
void gfx_present(void);

void fill_rect(int x, int y, int w, int h, uint16_t c);

/* UTF-8 text with the 8x16 font (Terminus, 528 glyphs: Latin, Cyrillic,
   Greek capitals, punctuation, arrows, box drawing, the IPA of transcriptions;
   others are drawn as '?'),
   s = scale 1 or 2 */
unsigned utf8_next(const char **ps);
int text_len(const char *s);
int draw_text(int x, int y, int s, const char *str, uint16_t c);
void draw_text_center(int y, int s, const char *str, uint16_t c);
/* word-wraps to width w, returns y below the last line */
int draw_text_wrap(int x, int y, int w, int s, const char *str, uint16_t c);

#define TEXT_BOLD   1
#define TEXT_ITALIC 2
#define TEXT_BIG    4  /* twice the size */
/* draws nbytes of str (all if nbytes < 0) in a TEXT_* style, returns the x after it */
int draw_text_style(int x, int y, int style, const char *str, int nbytes, uint16_t c);

/* draws only the first nbytes of str */
int draw_text_n(int x, int y, int s, const char *str, int nbytes, uint16_t c);
/* splits text into lines of at most cols characters, breaking at spaces and \n;
   fills byte offsets and byte lengths, returns the number of lines */
int wrap_text(const char *text, int cols, int *starts, int *lens, int max);

/* waits for a key press, returns its keypad code or -1 on quit */
int wait_key(void);
/* the same, but gives up after ms milliseconds (ms < 0: never) and returns 0 */
int wait_key_ms(int ms);
/* non-blocking: returns keypad code, 0 if no key, -1 on quit */
int poll_key(void);

#endif
