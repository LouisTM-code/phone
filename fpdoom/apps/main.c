#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "apps.h"
#include "cards.h"

#define COL_BG      RGB(18, 18, 28)
#define COL_HEAD    RGB(60, 60, 100)
#define COL_TEXT    RGB(255, 255, 255)
#define COL_DIM     RGB(140, 140, 170)
#define COL_SEL     RGB(90, 120, 220)

int app_today;

/* to add a program: write its *_main() and put it here */
static const struct {
	const char *name, *desc;
	int (*run)(void);
} menu[] = {
	{ "Книги", "Чтение с переводом слов", reader_main },
	{ "Карточки", "Повторение, как в Anki", cards_main },
	{ "Словарь", "Английский - русский", dict_main },
	{ "Выход", "Закрыть меню", NULL },
};

#define MENU_N (int)(sizeof(menu) / sizeof(*menu))

static void draw_menu(int sel) {
	int i, item_h = FONT_H * 2 + 4, gap = 4, y0 = FONT_H + 10;
	fill_rect(0, 0, scr_w, scr_h, COL_BG);
	fill_rect(0, 0, scr_w, FONT_H + 6, COL_HEAD);
	draw_text_center(3, 1, "Меню", COL_TEXT);

	for (i = 0; i < MENU_N; i++) {
		int y = y0 + i * (item_h + gap);
		char num[4];
		uint16_t bg = i == sel ? COL_SEL : RGB(34, 34, 52);
		fill_rect(6, y, scr_w - 12, item_h, bg);
		sprintf(num, "%d", i + 1);
		draw_text(12, y + (item_h - FONT_H * 2) / 2, 2, num, COL_DIM);
		draw_text(40, y + 2, 1, menu[i].name, COL_TEXT);
		draw_text(40, y + FONT_H + 2, 1, menu[i].desc, i == sel ? COL_TEXT : COL_DIM);
	}
	gfx_present();
}

int main(int argc, char **argv) {
	int sel = 0, i;

	for (i = 1; i + 1 < argc; i++)
		if (!strcmp(argv[i], "--day")) app_today = atoi(argv[i + 1]);

	gfx_init();
	printf("apps: screen %ux%u, day %d\n", scr_w, scr_h, app_today);

	for (;;) {
		int key, run = -1;
		draw_menu(sel);
		key = wait_key();
		if (key < 0) break;
		switch (key) {
		case KEYPAD_UP: sel = (sel + MENU_N - 1) % MENU_N; break;
		case KEYPAD_DOWN: sel = (sel + 1) % MENU_N; break;
		case KEYPAD_CENTER: case KEYPAD_5: case KEYPAD_LSOFT: run = sel; break;
		default:
			if (key >= KEYPAD_1 && key < KEYPAD_1 + MENU_N) run = sel = key - KEYPAD_1;
		}
		if (run < 0) continue;
		if (!menu[run].run) break;
		printf("apps: start %s\n", menu[run].name);
		if (menu[run].run() < 0) break;
	}
	cards_save();
	gfx_free();
	return 0;
}
