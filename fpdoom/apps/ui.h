#ifndef UI_H
#define UI_H

#include "gfx.h"

#define UI_BG      RGB(20, 24, 40)
#define UI_BAR     RGB(40, 70, 140)
#define UI_TEXT    RGB(255, 255, 255)
#define UI_DIM     RGB(150, 160, 190)
#define UI_SEL     RGB(70, 110, 200)
#define UI_ACCENT  RGB(255, 210, 80)
#define UI_OK      RGB(120, 220, 120)

#define UI_BAR_H (FONT_H + 6)

void ui_header(const char *title);
void ui_footer(const char *hint);
/* a short message shown in the footer on the next redraw */
void ui_note(const char *msg);

/* scrollable text; on_key gets other keys and returns 1 to close.
   returns 0 when closed, -1 on quit */
int text_view(const char *title, const char *text, const char *footer,
		int (*on_key)(int key, void *ctx), void *ctx);

/* list picker, returns the index, -1 when cancelled, -2 on quit */
int ui_pick(const char *title, const char *const *items, int n, int sel);

/* a number from 1 to max typed on the keypad: the number, -1 when cancelled,
   -2 on quit */
int ui_number(const char *title, const char *hint, int max);
/* text typed by multi-tap (* switches Russian/English, > ends a letter) into
   buf, which may hold a starting text: its length, -1 when cancelled, -2 on quit */
int ui_input(const char *title, char *buf, int size);

/* shows a message until a key is pressed, returns -1 on quit */
int ui_message(const char *title, const char *text);

/* the phone's libc has no fgets/strstr: whole-file helpers instead */
/* NUL-terminated file contents (free() it), NULL if missing */
char *file_load(const char *path, long *size);
/* cuts the next line out of the buffer at *pos, NULL at the end */
char *next_line(char **pos);
/* cuts the next tab-separated field out of the line at *p, "" after the last */
char *next_field(char **p);
/* drops a letter cut in half at the end of s (snprintf cuts by bytes) */
void utf8_trim(char *s);
const char *str_find(const char *s, const char *sub);

#endif
