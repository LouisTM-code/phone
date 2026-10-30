#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "party.h"

/* Bomb: a task on the screen ("a city", "a word starting with ПО"), each player
   names a word and passes the phone on; it goes off at a hidden random moment.
   Tasks come from party/bomb.txt, a task per line. */

#define SETTINGS_FILE "state/bomb.txt"
#define TASKS_FILE "party/bomb.txt"
#define MAX_TASKS 512
#define MAX_TIME 600

#define BOOM_BG RGB(150, 30, 30)

/* state/bomb.txt: "key value" lines, the time to the blast in seconds */
static int set_min = 20, set_max = 60;

static void settings_check(void) {
	if (set_min < 1 || set_min > MAX_TIME) set_min = 20;
	if (set_max < set_min || set_max > MAX_TIME) set_max = set_min;
}

static void settings_load(void) {
	char *buf = file_load(SETTINGS_FILE, NULL), *pos, *line;
	if (!buf) return;
	for (pos = buf; (line = next_line(&pos)); ) {
		char *v = strchr(line, ' ');
		if (!v) continue;
		*v++ = 0;
		if (!strcmp(line, "min")) set_min = atoi(v);
		else if (!strcmp(line, "max")) set_max = atoi(v);
	}
	free(buf);
	settings_check();
}

static void settings_save(void) {
	FILE *fp = fopen(SETTINGS_FILE, "wb");
	if (!fp) { ui_note("Не удалось записать настройки"); return; }
	fprintf(fp, "min %d\nmax %d\n", set_min, set_max);
	fclose(fp);
}

static const char pass_note[] = "Назовите слово по заданию и передайте телефон соседу. Повторять нельзя.";

/* one bomb with the task: 1 when it went off, 0 when stopped, -1 on quit */
static int tick(const char *task) {
	/* in tenths of a second: rng() gives up to 65535 */
	uint32_t end = sys_timer_ms() + set_min * 1000 + rng((set_max - set_min) * 10 + 1) * 100;
	draw_card("Бомба тикает", task, UI_TEXT, pass_note, "П.софт остановить");
	for (;;) {
		int left = (int)(end - sys_timer_ms()), key;
		if (left <= 0) break;
		key = wait_key_ms(left);
		if (key < 0) return -1;
		if (key == KEYPAD_RSOFT) return 0;
	}
	ui_header("Бомба");
	fill_rect(0, UI_BAR_H, scr_w, scr_h - UI_BAR_H, BOOM_BG);
	draw_icon((scr_w - 64) / 2, UI_BAR_H + 30, 4, ICON_BOMB, UI_TEXT);
	draw_text_center(UI_BAR_H + 110, 2, "БУМ!", UI_ACCENT);
	draw_text_wrap(8, UI_BAR_H + 160, scr_w - 16, 1, "Проиграл тот, у кого телефон в руках.", UI_TEXT);
	ui_footer("5 новое задание  П.софт выход");
	gfx_present();
	buzz(3);
	return 1;
}

static int play(void) {
	const char *tasks[MAX_TASKS];
	char *buf;
	int n, i, r;
	n = list_load(TASKS_FILE, &buf, tasks, MAX_TASKS);
	if (!n) {
		free(buf);
		return ui_message("Бомба", "Нет заданий: на карте нет party/bomb.txt.");
	}
	rng_start();
	for (i = n - 1; i > 0; i--) {
		int j = rng(i + 1);
		const char *t = tasks[i];
		tasks[i] = tasks[j];
		tasks[j] = t;
	}
	for (i = 0; ; i = (i + 1) % n) {
		draw_card("Бомба", tasks[i], UI_TEXT, pass_note, "5 запустить  П.софт выход");
		if ((r = wait_go()) <= 0) break;
		if ((r = tick(tasks[i])) < 0) break;
		if (r && (r = wait_go()) <= 0) break;
	}
	free(buf);
	return r < 0 ? -1 : 0;
}

static int settings(void) {
	static char l[2][48];
	int sel = 0;
	for (;;) {
		const char *items[3];
		int r;
		sprintf(l[0], "Взрыв не раньше: %d с", set_min);
		sprintf(l[1], "Взрыв не позже: %d с", set_max);
		items[0] = l[0], items[1] = l[1], items[2] = "Назад";
		r = ui_pick("Настройки Бомбы", items, 3, sel);
		if (r == -2) return -1;
		if (r < 0 || r == 2) break;
		sel = r;
		if (r == 0 && ask_number("Не раньше", "Секунд до взрыва, не меньше:", MAX_TIME, &set_min) < 0) return -1;
		if (r == 1 && ask_number("Не позже", "Секунд до взрыва, не больше:", MAX_TIME, &set_max) < 0) return -1;
		if (set_max < set_min) {
			if (r == 0) set_max = set_min;
			else set_min = set_max;
		}
	}
	settings_save();
	return 0;
}

static const char rules[] =
	"На экране задание: например, «город» или «слово на ПО». Игрок нажимает 5, "
	"называет подходящее слово и передаёт телефон соседу, тот называет своё "
	"и передаёт дальше. Слова повторять нельзя.\n\n"
	"Бомба взрывается в случайный момент (время задаётся в настройках): экран "
	"краснеет, телефон вибрирует. Проиграл тот, у кого телефон в руках.\n\n"
	"Задания лежат в party/bomb.txt на карте, по одному в строке.";

int bomb_main(void) {
	static const char *const items[] = { "Новая игра", "Настройки", "Правила", "Назад" };
	int sel = 0;
	settings_load();
	for (;;) {
		sel = ui_pick("Бомба", items, 4, sel);
		if (sel == -2) return -1;
		if (sel < 0 || sel == 3) return 0;
		if (sel == 0 && play() < 0) return -1;
		if (sel == 1 && settings() < 0) return -1;
		if (sel == 2 && ui_message("Правила Бомбы", rules) < 0) return -1;
	}
}
