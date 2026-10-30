#ifndef UI_H
#define UI_H

#include "gfx.h"

/* the design: dark cards, each app has a colour and an icon; the selected
   item is filled with the app's colour and has UI_INK text */
#define UI_BG      RGB(12, 14, 22)
#define UI_CARD    RGB(28, 31, 46)
#define UI_LINE    RGB(40, 44, 64)
#define UI_TEXT    RGB(255, 255, 255)
#define UI_DIM     RGB(130, 135, 160)
#define UI_INK     RGB(20, 20, 30)
#define UI_ACCENT  RGB(255, 210, 80)
#define UI_IPA     RGB(140, 210, 190)   /* transcriptions */
#define UI_OK      RGB(110, 200, 120)
#define UI_BAD     RGB(220, 80, 80)
#define UI_SEL     ui_color

/* app colours */
#define UI_BOOKS   RGB(235, 130, 60)
#define UI_CARDS   RGB(90, 170, 240)
#define UI_DICT    RGB(110, 200, 120)
#define UI_NOTES   RGB(240, 200, 70)
#define UI_QUIZ    RGB(200, 110, 220)

#define UI_BAR_H (FONT_H + 6)

/* 16x16 icons (icons.c) */
enum { ICON_BOOK, ICON_CARDS, ICON_DICT, ICON_NOTES, ICON_QUIZ, ICON_EXIT,
	ICON_ALIAS, ICON_SPY, ICON_ODD, ICON_BOMB, ICON_SIGNAL, ICON_CHECK, ICON_N };
/* s = scale */
void draw_icon(int x, int y, int s, int icon, uint16_t c);

/* the colour and icon of the app on screen, for the header and the selection */
extern uint16_t ui_color;
void ui_app(uint16_t color, int icon);

/* clears the screen and draws the header: the app's icon and the title */
void ui_header(const char *title);
void ui_footer(const char *hint);
/* a card, filled with the app's colour and framed when selected */
void ui_tile(int x, int y, int w, int h, int sel);
/* buttons in two rows of two above the footer, i = 0..3: a colour strip on a
   card, or filled with the colour as the one 5 picks (def) */
#define UI_BTN_H (FONT_H + 10)
#define UI_BTN_TOP (int)(scr_h - UI_BAR_H - 2 * (UI_BTN_H + 4))
void ui_button(int i, const char *text, uint16_t c, int def);
/* a thin bar, filled to n of max */
void ui_progress(int x, int y, int w, int h, int n, int max, uint16_t c);
/* a short message shown in the footer on the next redraw */
void ui_note(const char *msg);

/* scrollable text; on_key gets other keys and returns 1 to close.
   returns 0 when closed, -1 on quit */
int text_view(const char *title, const char *text, const char *footer,
		int (*on_key)(int key, void *ctx), void *ctx);
/* the line of a dictionary article that lists its word family (tools/mkdict.py) */
#define FAMILY_LABEL "Однокоренные: "
/* dictionary article styling, by the source line the wrapped line at text + start belongs to */
uint16_t ui_line_color(const char *text, int start);

/* a grid menu of tiles in two columns, the description of the selected one
   under it, left (or NULL) and title in the header: the index (5 or its digit),
   -1 for П.софт, -2 on quit */
typedef struct {
	const char *name, *desc;
	uint16_t color;
	int icon;
} ui_item_t;
int ui_grid(const char *left, const char *title, const ui_item_t *items, int n, int sel);

/* list picker, returns the index, -1 when cancelled, -2 on quit */
int ui_pick(const char *title, const char *const *items, int n, int sel);
/* the same with a footer and other keys (a 0-terminated list, or NULL) that also
   return the index (-1 for an empty list) and put the key into *key_out (0 for 5) */
int ui_pick_keys(const char *title, const char *const *items, int n, int sel,
		const char *footer, const int *keys, int *key_out);

/* a number from 1 to max typed on the keypad: the number, -1 when cancelled,
   -2 on quit */
int ui_number(const char *title, const char *hint, int max);
/* text typed by multi-tap (* switches Russian/English, > ends a letter) into
   buf, which may hold a starting text, under a prompt (or NULL), starting in the
   Russian layout if ru: its length, -1 when cancelled, -2 on quit */
int ui_input(const char *title, const char *prompt, char *buf, int size, int ru);

/* shows a message until a key is pressed, returns -1 on quit */
int ui_message(const char *title, const char *text);

/* "29.10.2026" for a day since 1970 */
void date_text(int days, char *buf);

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
