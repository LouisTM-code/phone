#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "apps.h"
#include "cards.h"
#include "reader.h"
#include "log.h"

int app_today;

/* to add a program: write its *_main() and put it in both lists */
static ui_item_t menu[] = {
	{ "Книги", "Чтение с переводом слов", UI_BOOKS, ICON_BOOK },
	{ "Карточки", "Повторение, как в Anki", UI_CARDS, ICON_CARDS },
	{ "Словарь", "Английский - русский", UI_DICT, ICON_DICT },
	{ "Заметки", "Свои записи", UI_NOTES, ICON_NOTES },
	{ "Викторины", "Выбор из 4 вариантов", UI_QUIZ, ICON_QUIZ },
	{ "Выход", "Закрыть меню", UI_BAD, ICON_EXIT },
};
static int (*const run_app[])(void) = { reader_main, cards_main, dict_main, notes_main, quiz_main, NULL };

#define MENU_N (int)(sizeof(menu) / sizeof(*menu))
/* fails to compile when the lists differ in length */
typedef char menu_lists_match[MENU_N == (int)(sizeof(run_app) / sizeof(*run_app)) ? 1 : -1];

/* Книги and Карточки tell what is waiting: the book being read (read from the
   card at the start and after Книги only), today's cards (from memory) */
static void book_status(void) {
	static char book[112];
	const char *title = shelf_reading();
	menu[0].desc = "Чтение с переводом слов";
	if (title) {
		snprintf(book, sizeof(book), "Читаю: %s", title);
		menu[0].desc = book;
	}
}

static void cards_status(void) {
	static char due[48];
	int nrev, nnew;
	menu[1].desc = "Повторение, как в Anki";
	if (cards_today(&nrev, &nnew) == 0) {
		if (nrev && nnew) snprintf(due, sizeof(due), "Повторить %d, новых %d", nrev, nnew);
		else if (nrev) snprintf(due, sizeof(due), "Повторить %d", nrev);
		else if (nnew) snprintf(due, sizeof(due), "Новых слов: %d", nnew);
		else snprintf(due, sizeof(due), "На сегодня всё");
		menu[1].desc = due;
	}
}

int main(int argc, char **argv) {
	int sel = 0, i, ret;
	char date[16] = "";

	for (i = 1; i + 1 < argc; i++)
		if (!strcmp(argv[i], "--day")) app_today = atoi(argv[i + 1]);
	if (!app_today) app_today = sys_rtc_day();
	if (app_today) date_text(app_today, date);

	gfx_init();
	log_start();
	printf("apps: screen %ux%u, day %d\n", scr_w, scr_h, app_today);

	book_status();
	for (;;) {
		int run;
		cards_status();
		run = ui_grid(date, "Меню", menu, MENU_N, sel);
		if (run == -2) break;
		if (run < 0) continue;
		sel = run;
		if (!run_app[run]) break;
		printf("apps: start %s\n", menu[run].name);
		log_app(menu[run].name);
		ui_app(menu[run].color, menu[run].icon);
		ret = run_app[run]();
		log_app(NULL);
		if (ret < 0) break;
		if (run_app[run] == reader_main) book_status();
	}
	cards_save();
	log_end();
	gfx_free();
	return 0;
}
