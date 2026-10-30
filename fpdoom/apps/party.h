#ifndef PARTY_H
#define PARTY_H

#include "ui.h"

/* the party games of fpbin/party.bin (party.c): Alias, Spy, Odd one, Bomb */

enum { SIG_VIBRO, SIG_KPLED, SIG_NONE };
/* the signal for all the games, kept in state/party.txt */
extern int party_signal;

/* pulses of the vibration or the keypad light */
void buzz(int pulses);
/* seeds rng() for a new game: the clock's day and a game counter kept on the card */
void rng_start(void);
/* a number below n */
unsigned rng(unsigned n);
/* the lines of a list file (empty ones and "#" comments skipped) into lines,
   their count; *buf holds them, free() it */
int list_load(const char *path, char **buf, const char **lines, int max);
/* a number setting from 1 to max into *val; -1 on quit */
int ask_number(const char *title, const char *hint, int max, int *val);

/* big text in color c on a card at y, centred, wrapped (a long one in the
   normal size): the y below the card */
int draw_big(int y, const char *big, uint16_t c);
/* a screen with big text in color c (wrapped, a long one in the normal size) and a note under it */
void draw_card(const char *title, const char *big, uint16_t c, const char *note, const char *footer);
/* waits for 5 or the center key: 1, RSOFT: 0, -1 on quit */
int wait_go(void);

/* each returns -1 on quit */
int alias_main(void);
int spy_main(void);
int odd_main(void);
int bomb_main(void);

#endif
