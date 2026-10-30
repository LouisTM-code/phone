#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "party.h"

/* Party games on one phone: Alias (alias.c), Spy and Odd one (roles.c), Bomb (bomb.c).
   A program of its own, fpbin/party.bin (key 1 at power on), so the menu
   does not hold its memory. */

#define SETTINGS_FILE "state/party.txt"

/* state/party.txt: "key value" lines */
int party_signal;
static int games;  /* games started, seeds rng() */

static uint32_t rng_state;

unsigned rng(unsigned n) {
	rng_state = rng_state * 1103515245 + 12345;
	return (rng_state >> 16) % n;
}

static void settings_load(void) {
	char *buf = file_load(SETTINGS_FILE, NULL), *pos, *line;
	if (!buf) return;
	for (pos = buf; (line = next_line(&pos)); ) {
		char *v = strchr(line, ' ');
		if (!v) continue;
		*v++ = 0;
		if (!strcmp(line, "signal")) party_signal = atoi(v);
		else if (!strcmp(line, "games")) games = atoi(v);
	}
	free(buf);
	if (party_signal < SIG_VIBRO || party_signal > SIG_NONE) party_signal = SIG_VIBRO;
}

static void settings_save(void) {
	FILE *fp = fopen(SETTINGS_FILE, "wb");
	if (!fp) { ui_note("Не удалось записать настройки"); return; }
	fprintf(fp, "signal %d\ngames %d\n", party_signal, games);
	fclose(fp);
}

void rng_start(void) {
	rng_state = sys_rtc_day() * 7919 + ++games;
	settings_save();
}

void buzz(int pulses) {
	int i;
	for (i = 0; i < pulses; i++) {
		if (i) sys_wait_ms(150);
		if (party_signal == SIG_VIBRO) {
			sys_vibrate(1);
			sys_wait_ms(300);
			sys_vibrate(0);
		} else if (party_signal == SIG_KPLED) {
			sys_kpled(100);
			sys_wait_ms(300);
			sys_kpled(0);
		}
	}
	if (party_signal == SIG_KPLED) sys_kpled(sys_data.kpled);
}

int list_load(const char *path, char **buf, const char **lines, int max) {
	char *pos, *line;
	int n = 0;
	if (!(*buf = file_load(path, NULL))) return 0;
	for (pos = *buf; n < max && (line = next_line(&pos)); )
		if (*line && *line != '#') lines[n++] = line;
	return n;
}

int ask_number(const char *title, const char *hint, int max, int *val) {
	int r = ui_number(title, hint, max);
	if (r == -2) return -1;
	if (r > 0) *val = r;
	return 0;
}

/* the characters in the n bytes of s */
static int chars(const char *s, int n) {
	const char *p = s;
	int k = 0;
	while (p < s + n && *p) { utf8_next(&p); k++; }
	return k;
}

int draw_big(int y, const char *big, uint16_t c) {
	static int starts[8], lens[8];
	/* up to two lines' worth big, longer (a question) in the normal size */
	int w = text_len(big) * FONT_W, s = w <= (int)scr_w - 24 ? 2 : 1;
	int n = wrap_text(big, (scr_w - 24) / (FONT_W * s), starts, lens, 8), h, i;
	h = n * FONT_H * s + 32;
	fill_rect(4, y, scr_w - 8, h, UI_CARD);
	fill_rect(4, y, scr_w - 8, 3, ui_color);
	for (i = 0; i < n; i++) {
		int x = (scr_w - chars(big + starts[i], lens[i]) * FONT_W * s) / 2;
		draw_text_n(x, y + 17 + i * FONT_H * s, s, big + starts[i], lens[i], c);
	}
	return y + h;
}

void draw_card(const char *title, const char *big, uint16_t c, const char *note, const char *footer) {
	int y;
	ui_header(title);
	y = draw_big(UI_BAR_H + 40, big, c);
	if (note) draw_text_wrap(8, y + 10, scr_w - 16, 1, note, UI_DIM);
	ui_footer(footer);
	gfx_present();
}

int wait_go(void) {
	for (;;) {
		int key = wait_key();
		if (key < 0) return -1;
		if (key == KEYPAD_5 || key == KEYPAD_CENTER) return 1;
		if (key == KEYPAD_RSOFT) return 0;
	}
}

int main(int argc, char **argv) {
	static const char *const sigs[] = { "Вибрация", "Подсветка клавиш", "Без сигнала" };
	/* the games come first in the menu, then the signal and the exit */
	static int (*const run_game[])(void) = { alias_main, spy_main, odd_main, bomb_main };
	enum { SIGNAL = sizeof(run_game) / sizeof(*run_game), EXIT, ITEMS_N };
	ui_item_t items[ITEMS_N] = {
		{ "Alias", "Объясните слово команде", RGB(240, 100, 140), ICON_ALIAS },
		{ "Шпион", "Найдите шпиона", RGB(120, 140, 240), ICON_SPY },
		{ "Нарушитель", "Кто здесь лишний", RGB(110, 200, 180), ICON_ODD },
		{ "Бомба", "Успейте передать телефон", RGB(240, 150, 60), ICON_BOMB },
		{ "Сигнал", NULL, RGB(200, 190, 110), ICON_SIGNAL },
		{ "Выход", "Закрыть игры", UI_BAD, ICON_EXIT },
	};
	int sel = 0, r = 0;
	(void)argc; (void)argv;
	gfx_init();
	settings_load();
	while (r >= 0) {
		int run, p;
		items[SIGNAL].desc = sigs[party_signal];
		run = ui_grid(NULL, "Игры для компании", items, ITEMS_N, sel);
		if (run == -2 || run == EXIT) break;
		if (run < 0) continue;  /* П.софт: as in the main menu */
		sel = run;
		ui_app(items[sel].color, items[sel].icon);
		if (sel < SIGNAL) {
			r = run_game[sel]();
		} else if ((p = ui_pick("Сигнал", sigs, 3, party_signal)) == -2) {
			r = -1;
		} else if (p >= 0) {
			party_signal = p;
			settings_save();
			buzz(1);
		}
	}
	gfx_free();
	return 0;
}
