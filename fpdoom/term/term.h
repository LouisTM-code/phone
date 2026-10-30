#ifndef TERM_H
#define TERM_H

/* Text terminal for the ports' menus and text screens: a grid of
   character cells drawn with the 8x8 or 8x16 font, keypad input with
   multi-tap letters. Built on ../apps/gfx.c. */

#include "gfx.h"

/* cell attribute: colour 0..15 (ANSI order, 8..15 bright) and flags */
#define TERM_COLOR(a) ((a) & 15)
#define TERM_REV 0x10
#define TERM_DEFAULT 7

/* grid size; when bigger than the screen, the visible part follows the cursor */
extern int term_cols, term_rows;
/* visible part in cells */
extern int term_vis_cols, term_vis_rows;

/* font_h: 8 or 16; cols, rows: grid size, 0 = fit the screen */
void term_init(int font_h, int cols, int rows);

void term_put(int row, int col, unsigned ch, int attr);
unsigned term_char(int row, int col);
/* writes UTF-8 text from (row, col), cut at the grid end; returns the next col */
int term_puts(int row, int col, const char *s, int attr);
/* blanks rows [top, bottom) and cols [left, right) */
void term_erase(int top, int left, int bottom, int right);
/* moves rows [top, bottom) up by n (down when n < 0), blanking the new rows */
void term_scroll(int top, int bottom, int n);
/* cursor position, col < 0 hides it */
void term_cursor(int row, int col);
/* a short text drawn reversed in the top right corner, NULL removes it */
void term_status(const char *s);
/* draws the grid and shows it */
void term_refresh(void);

/* keypad code (KEYPAD_*), 0 on timeout (ms < 0 waits forever), -1 on quit */
int term_key(int ms);

/* one character typed with multi-tap: keys 2..9 letters, 0 space, 1 punctuation,
   * changes the case/digits, # or CENTER confirms; it is shown at the cursor
   while typing. first: the key already pressed (0 = none). Returns the
   character, 0 when cancelled (RSOFT), -1 on quit */
int term_tap_char(int first);

/* line editor with multi-tap at (row, col) up to the grid end. buf (max bytes
   with NUL) holds the initial text. CENTER/# = enter, RSOFT/LEFT = backspace,
   UP = the previous line, LSOFT = pick a word from words (NULL-terminated,
   may be NULL). Returns the length, -1 on quit. */
int term_getline(int row, int col, char *buf, int max, const char *const *words);

/* full screen list, returns the index, -1 when cancelled, -2 on quit */
int term_pick(const char *title, const char *const *items, int n, int sel);

#endif
