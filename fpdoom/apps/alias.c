#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "party.h"

/* Alias: one player explains a word, the team guesses it against the clock;
   the teams take turns on one phone. Words come from the level decks
   (decks/index.txt, "word<TAB>meaning<TAB>example" lines). */

#define SETTINGS_FILE "state/alias.txt"
#define DECK_INDEX "decks/index.txt"
#define MAX_DECKS 16
#define MAX_WORDS 8192
#define MAX_TEAMS 6
#define MAX_TURN 200     /* words shown in one turn */
#define WARN_SEC 10      /* the signal before the end of a turn */

/* state/alias.txt: "key value" lines */
static int set_teams = 2, set_time = 60, set_goal = 30, set_ru, set_skip = 1;
static char set_deck[48] = "all";

static char *bufs[MAX_DECKS];
static int nbufs;
static const char *word_main[MAX_WORDS], *word_hint[MAX_WORDS];
static int order[MAX_WORDS], nwords, next_word;
static int score[MAX_TEAMS];

static void settings_load(void) {
	char *buf = file_load(SETTINGS_FILE, NULL), *pos, *line;
	if (!buf) return;
	for (pos = buf; (line = next_line(&pos)); ) {
		char *v = strchr(line, ' ');
		if (!v) continue;
		*v++ = 0;
		if (!strcmp(line, "teams")) set_teams = atoi(v);
		else if (!strcmp(line, "time")) set_time = atoi(v);
		else if (!strcmp(line, "goal")) set_goal = atoi(v);
		else if (!strcmp(line, "ru")) set_ru = atoi(v);
		else if (!strcmp(line, "skip")) set_skip = atoi(v);
		else if (!strcmp(line, "deck")) snprintf(set_deck, sizeof(set_deck), "%s", v);
	}
	free(buf);
	if (set_teams < 2 || set_teams > MAX_TEAMS) set_teams = 2;
	if (set_time < 5) set_time = 60;
	if (set_goal < 1) set_goal = 30;
}

static void settings_save(void) {
	FILE *fp = fopen(SETTINGS_FILE, "wb");
	if (!fp) { ui_note("Не удалось записать настройки"); return; }
	fprintf(fp, "teams %d\ntime %d\ngoal %d\nru %d\nskip %d\ndeck %s\n",
			set_teams, set_time, set_goal, set_ru, set_skip, set_deck);
	fclose(fp);
}

/* ---- words ---- */

/* the first meaning of "n. встречать(ся); знакомиться / v. ..." in place: "встречать" */
static char *first_meaning(char *s) {
	char *t, *d;
	if (!strncmp(s, "modal ", 6)) s += 6;
	for (;;) {
		for (t = s; *t >= 'a' && *t <= 'z'; t++);
		if (t == s || t[0] != '.' || t[1] != ' ') break;
		s = t + 2;
	}
	for (t = d = s; *t && *t != ';' && *t != ',' && strncmp(t, " / ", 3); t++) {
		if (*t == '(') {
			while (*t && *t != ')') t++;
			if (!*t) break;
			continue;
		}
		*d++ = *t;
	}
	while (d > s && d[-1] == ' ') d--;
	*d = 0;
	while (*s == ' ') s++;
	return s;
}

static void words_free(void) {
	while (nbufs) free(bufs[--nbufs]);
	nwords = 0;
}

static void load_deck(const char *path) {
	char *buf, *pos, *line;
	if (nbufs == MAX_DECKS || !(buf = file_load(path, NULL))) return;
	bufs[nbufs++] = buf;
	for (pos = buf; nwords < MAX_WORDS && (line = next_line(&pos)); ) {
		char *en = next_field(&line), *ru = first_meaning(next_field(&line));
		if (!*en || !*ru) continue;
		word_main[nwords] = set_ru ? ru : en;
		word_hint[nwords] = set_ru ? en : ru;
		nwords++;
	}
}

/* the decks of decks/index.txt into files and names, their count */
static int deck_list(char files[][48], char names[][64]) {
	char *buf = file_load(DECK_INDEX, NULL), *pos, *line;
	int n = 0;
	if (!buf) return 0;
	for (pos = buf; n < MAX_DECKS && (line = next_line(&pos)); ) {
		char *tab = strchr(line, '\t');
		if (!tab) continue;
		*tab = 0;
		snprintf(files[n], 48, "%s", line);
		/* "file<TAB>name<TAB>group<TAB>words<TAB>kind" (tools/mkdecks.py) */
		line = tab + 1;
		if ((tab = strchr(line, '\t'))) *tab = 0;
		snprintf(names[n], 64, "%s", line);
		n++;
	}
	free(buf);
	return n;
}

static void words_load(void) {
	static char files[MAX_DECKS][48], names[MAX_DECKS][64];
	int i, n;
	words_free();
	if (strcmp(set_deck, "all")) load_deck(set_deck);
	else for (n = deck_list(files, names), i = 0; i < n; i++) load_deck(files[i]);
	for (i = 0; i < nwords; i++) order[i] = i;
	next_word = nwords;
}

static int take_word(void) {
	int i;
	if (next_word == nwords) {
		for (i = nwords - 1; i > 0; i--) {
			int j = rng(i + 1), t = order[i];
			order[i] = order[j];
			order[j] = t;
		}
		next_word = 0;
	}
	return order[next_word++];
}

/* ---- a turn ---- */

static int turn_words[MAX_TURN], turn_res[MAX_TURN], nturn, last_word;
enum { RES_SKIP, RES_GOT, RES_MISSED };  /* RES_MISSED: the last word, no penalty */

static void draw_word(const char *title, int sec, int w, const char *footer) {
	char buf[64];
	int y, got = 0, skip = 0, i;
	ui_header(title);
	if (sec >= 0) {
		sprintf(buf, "%d:%02d", sec / 60, sec % 60);
		draw_text_style(scr_w - 4 - text_len(buf) * FONT_W, 3, TEXT_BOLD, buf, -1,
				sec <= WARN_SEC ? UI_ACCENT : UI_TEXT);
	}
	y = draw_big(UI_BAR_H + 40, word_main[w], UI_TEXT);
	draw_text_wrap(8, y + 10, scr_w - 16, 1, word_hint[w], UI_DIM);
	for (i = 0; i < nturn; i++) {
		if (turn_res[i] == RES_GOT) got++;
		if (turn_res[i] == RES_SKIP) skip++;
	}
	sprintf(buf, "Угадано %d", got);
	ui_button(2, buf, UI_OK, 0);
	sprintf(buf, "Пропущено %d", skip);
	ui_button(3, buf, UI_BAD, 0);
	ui_footer(footer);
	gfx_present();
}

/* the words of the turn, 5 changes a mark; the points into *pts, -2 on quit */
static int review(int *pts) {
	static char lines[MAX_TURN + 1][64];
	const char *items[MAX_TURN + 1];
	int sel = 0, i;
	for (;;) {
		for (*pts = i = 0; i < nturn; i++) {
			int r = turn_res[i];
			*pts += r == RES_GOT ? 1 : r == RES_SKIP && set_skip ? -1 : 0;
			snprintf(lines[i], sizeof(lines[i]), "%s %s", r == RES_GOT ? "+" : r == RES_SKIP ? "-" : " ",
					word_main[turn_words[i]]);
			utf8_trim(lines[i]);
			items[i] = lines[i];
		}
		sprintf(lines[nturn], "Готово: %+d", *pts);
		items[nturn] = lines[nturn];
		sel = ui_pick("Проверка: 5 меняет", items, nturn + 1, sel);
		if (sel == -2) return -2;
		if (sel < 0 || sel == nturn) return 0;
		turn_res[sel] = turn_res[sel] != RES_GOT ? RES_GOT :
				last_word && sel == nturn - 1 ? RES_MISSED : RES_SKIP;
	}
}

/* team t explains, its points into *pts: 0, -1 to end the game, -2 on quit */
static int play_turn(int t, int *pts) {
	static char title[32];
	const char *const pause[] = { "Продолжить", "Закончить ход", "Закончить игру" };
	uint32_t end = sys_timer_ms() + set_time * 1000;
	int warned = set_time <= WARN_SEC, key, w;
	sprintf(title, "Команда %d", t + 1);
	nturn = last_word = 0;
	turn_words[nturn] = w = take_word();
	for (;;) {
		int left = (int)(end - sys_timer_ms()), sec = (left + 999) / 1000;
		if (left <= 0) break;
		if (!warned && sec <= WARN_SEC) { warned = 1; buzz(1); }
		draw_word(title, sec, w, "5 угадали # пропуск Л-пауза");
		key = wait_key_ms(left - (sec - 1) * 1000);
		if (key < 0) return -2;
		if (key == KEYPAD_LSOFT) {
			uint32_t t0 = sys_timer_ms();
			int r = ui_pick("Пауза", pause, 3, 0);
			if (r == -2) return -2;
			if (r == 2) return -1;
			end += sys_timer_ms() - t0;
			if (r == 1) break;
		} else if (key == KEYPAD_5 || key == KEYPAD_CENTER || key == KEYPAD_HASH) {
			turn_res[nturn++] = key == KEYPAD_HASH ? RES_SKIP : RES_GOT;
			if (nturn == MAX_TURN) break;
			turn_words[nturn] = w = take_word();
		}
	}
	if (nturn < MAX_TURN) {
		/* the last word: the team may still guess it */
		buzz(3);
		draw_word("Время! Последнее слово", -1, w, "5 угадали  # не угадали");
		do key = wait_key();
		while (key > 0 && key != KEYPAD_5 && key != KEYPAD_CENTER && key != KEYPAD_HASH);
		if (key < 0) return -2;
		turn_res[nturn++] = key == KEYPAD_HASH ? RES_MISSED : RES_GOT;
		last_word = 1;
	}
	return review(pts);
}

/* the scores before team t's turn: 1 to play, 0 to end the game, -1 on quit */
static int scoreboard(int t) {
	static const char *const no_yes[] = { "Нет, играть дальше", "Да, закончить" };
	for (;;) {
		char buf[48];
		int i, key, y = UI_BAR_H + 12;
		sprintf(buf, "Alias: игра до %d", set_goal);
		ui_header(buf);
		for (i = 0; i < set_teams; i++, y += FONT_H + 10) {
			ui_tile(6, y - 4, scr_w - 12, FONT_H + 8, i == t);
			sprintf(buf, "Команда %d", i + 1);
			draw_text_style(12, y, i == t ? TEXT_BOLD : 0, buf, -1, i == t ? UI_INK : UI_TEXT);
			sprintf(buf, "%d", score[i]);
			draw_text_style(scr_w - 12 - text_len(buf) * FONT_W, y, TEXT_BOLD, buf, -1,
					i == t ? UI_INK : UI_SEL);
		}
		sprintf(buf, "Объясняет команда %d", t + 1);
		draw_text_center(y + 12, 1, buf, UI_TEXT);
		ui_footer("5 начать ход  П.софт выход");
		gfx_present();
		key = wait_key();
		if (key < 0) return -1;
		if (key == KEYPAD_5 || key == KEYPAD_CENTER) return 1;
		if (key == KEYPAD_RSOFT) {
			int r = ui_pick("Закончить игру?", no_yes, 2, 0);
			if (r == -2) return -1;
			if (r == 1) return 0;
		}
	}
}

/* the winner after a full round: the only one at the top with the goal reached, or -1 */
static int winner(void) {
	int i, best = 0, n = 0;
	for (i = 1; i < set_teams; i++) if (score[i] > score[best]) best = i;
	for (i = 0; i < set_teams; i++) n += score[i] == score[best];
	return n == 1 && score[best] >= set_goal ? best : -1;
}

static int play(void) {
	int t = 0, r, pts;
	words_load();
	if (!nwords) {
		words_free();
		return ui_message("Alias", "Нет слов: на карте нет наборов decks/.");
	}
	rng_start();
	memset(score, 0, sizeof(score));
	for (;;) {
		r = scoreboard(t);
		if (r <= 0) break;
		r = play_turn(t, &pts);
		if (r == -2) { r = -1; break; }
		if (r == -1) { r = 0; break; }
		score[t] += pts;
		if (++t == set_teams) {
			t = 0;
			if ((r = winner()) >= 0) {
				static char msg[256];
				int i, n = sprintf(msg, "Победила команда %d!\n\n", r + 1);
				for (i = 0; i < set_teams; i++)
					n += sprintf(msg + n, "Команда %d: %d\n", i + 1, score[i]);
				r = ui_message("Alias", msg);
				break;
			}
		}
	}
	words_free();
	return r < 0 ? -1 : 0;
}

/* ---- settings ---- */

static int pick_deck(void) {
	static char files[MAX_DECKS][48], names[MAX_DECKS + 1][64];
	const char *items[MAX_DECKS + 1];
	int n = deck_list(files, names), i, r;
	for (i = 0; i < n; i++) items[i] = names[i];
	items[n] = "Все уровни";
	r = ui_pick("Слова", items, n + 1, 0);
	if (r == -2) return -1;
	if (r >= 0) snprintf(set_deck, sizeof(set_deck), "%s", r < n ? files[r] : "all");
	return 0;
}

/* the name of the chosen deck */
static const char *deck_name(void) {
	static char files[MAX_DECKS][48], names[MAX_DECKS][64];
	int n = deck_list(files, names), i;
	for (i = 0; i < n; i++) if (!strcmp(files[i], set_deck)) return names[i];
	return "все уровни";
}

static int settings(void) {
	static const char *const teams[] = { "2", "3", "4", "5", "6" };
	static const char *const langs[] = { "Английские", "Русские" };
	static char l[6][96];
	int sel = 0;
	for (;;) {
		const char *items[7];
		int r, i, p = 0;
		sprintf(l[0], "Команд: %d", set_teams);
		sprintf(l[1], "Время хода: %d с", set_time);
		sprintf(l[2], "Игра до: %d очков", set_goal);
		snprintf(l[3], sizeof(l[3]), "Слова: %s", deck_name());
		sprintf(l[4], "Язык слов: %s", langs[set_ru]);
		sprintf(l[5], "Штраф за пропуск: %s", set_skip ? "да" : "нет");
		for (i = 0; i < 6; i++) items[i] = l[i];
		items[6] = "Назад";
		r = ui_pick("Настройки Alias", items, 7, sel);
		if (r == -2) return -1;
		if (r < 0 || r == 6) break;
		sel = r;
		if (r == 0 && (p = ui_pick("Команд", teams, 5, set_teams - 2)) >= 0) set_teams = p + 2;
		if (r == 1 && ask_number("Время хода", "Секунд на ход:", 600, &set_time) < 0) return -1;
		if (r == 2 && ask_number("Игра до", "Очков для победы:", 500, &set_goal) < 0) return -1;
		if (r == 3 && pick_deck() < 0) return -1;
		if (r == 4 && (p = ui_pick("Язык слов", langs, 2, set_ru)) >= 0) set_ru = p;
		if (r == 5) set_skip = !set_skip;
		if (p == -2) return -1;
		if (set_time < 5) set_time = 5;
	}
	settings_save();
	return 0;
}

static const char rules[] =
	"Команды ходят по очереди. Один игрок объясняет слово на экране, "
	"не называя его и однокоренных, остальные из его команды угадывают.\n\n"
	"5 - угадали (+1), # - пропуск (-1, если штраф включён), "
	"Л.софт - пауза.\n\n"
	"За 10 секунд до конца хода и в конце - сигнал. Последнее слово можно "
	"доугадать. Потом ход проверяют: 5 меняет отметку слова.\n\n"
	"Когда все команды сходили, побеждает команда, набравшая нужные очки "
	"первой; при равенстве играют ещё круг.";

int alias_main(void) {
	static const char *const items[] = { "Новая игра", "Настройки", "Правила", "Назад" };
	int sel = 0;
	settings_load();
	for (;;) {
		sel = ui_pick("Alias", items, 4, sel);
		if (sel == -2) return -1;
		if (sel < 0 || sel == 3) return 0;
		if (sel == 0 && play() < 0) return -1;
		if (sel == 1 && settings() < 0) return -1;
		if (sel == 2 && ui_message("Правила Alias", rules) < 0) return -1;
	}
}
