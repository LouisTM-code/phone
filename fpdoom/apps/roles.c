#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "party.h"

/* Hidden-roles games: the phone goes round, each player secretly sees a card,
   the odd ones (spies, offenders) see another card or none. Then the players
   talk against the clock, vote and open the answer.
   Spy: a place or a word the spies don't know (each player may get a limit on
   the answers too), or two close words. Odd one: a rule of behaviour or a task,
   the odd ones get a similar one.
   The lists are party/<name>.txt, a card per line; in a pair "card<TAB>close
   card" either may go to the majority. */

#define MAX_PLAYERS 12
#define MAX_ODD 3
#define MAX_CARDS 512
#define MAX_TIME 30      /* minutes */
#define WARN_SEC 60      /* the signal before the end of the talk */
#define LIMITS_FILE "party/limits.txt"

struct mode {
	const char *name;      /* in the settings */
	const char *file;
	int pairs;             /* the odd ones see the close card, not knowing they are odd */
	int limits;            /* each player gets a limit from LIMITS_FILE */
	const char *label;     /* the card in the answer */
	const char *odd_label; /* the close card in the answer */
	const char *note;      /* under the majority's card */
	const char *odd_note;  /* under "Вы шпион" when there is no close card */
	const char *talk;      /* how to talk, %d: the first player */
};

struct game {
	const char *name, *settings_file;
	const char *odd_big;           /* the odd ones' card without a pair */
	const char *odd_who;           /* "шпион": who to find */
	const char *odd_one, *odd_many; /* the answer: "Шпион: игрок", "Шпионы: игроки" */
	const char *odd_count;         /* in the settings: "Шпионов" */
	const char *rules;
	const struct mode *modes;
	int nmodes;
	/* the settings: state/<game>.txt, "key value" lines */
	int players, odd, time, mode;
};

static void settings_check(struct game *g) {
	if (g->players < 3 || g->players > MAX_PLAYERS) g->players = 4;
	if (g->odd < 1 || g->odd > MAX_ODD) g->odd = 1;
	if (g->odd >= g->players) g->odd = g->players - 1;
	if (g->time < 1 || g->time > MAX_TIME) g->time = 5;
	if (g->mode < 0 || g->mode >= g->nmodes) g->mode = 0;
}

static void settings_load(struct game *g) {
	char *buf = file_load(g->settings_file, NULL), *pos, *line;
	if (!buf) return;
	for (pos = buf; (line = next_line(&pos)); ) {
		char *v = strchr(line, ' ');
		if (!v) continue;
		*v++ = 0;
		if (!strcmp(line, "players")) g->players = atoi(v);
		else if (!strcmp(line, "odd")) g->odd = atoi(v);
		else if (!strcmp(line, "time")) g->time = atoi(v);
		else if (!strcmp(line, "mode")) g->mode = atoi(v);
	}
	free(buf);
	settings_check(g);
}

static void settings_save(struct game *g) {
	FILE *fp = fopen(g->settings_file, "wb");
	if (!fp) { ui_note("Не удалось записать настройки"); return; }
	fprintf(fp, "players %d\nodd %d\ntime %d\nmode %d\n", g->players, g->odd, g->time, g->mode);
	fclose(fp);
}

/* the talk on the clock: 1 to vote, 0 to end the game, -1 on quit */
static int talk(struct game *g, int first) {
	static const char *const no_yes[] = { "Нет, играть дальше", "Да, закончить" };
	uint32_t end = sys_timer_ms() + g->time * 60000;
	int warned = g->time * 60 <= WARN_SEC;
	char title[48], note[256];
	sprintf(title, "%s: обсуждение", g->name);
	snprintf(note, sizeof(note), g->modes[g->mode].talk, first + 1);
	for (;;) {
		char clock[16];
		int left = (int)(end - sys_timer_ms()), sec = (left + 999) / 1000, key;
		if (left <= 0) break;
		if (!warned && sec <= WARN_SEC) { warned = 1; buzz(1); }
		sprintf(clock, "%d:%02d", sec / 60, sec % 60);
		draw_card(title, clock, sec <= WARN_SEC ? UI_ACCENT : UI_TEXT, note,
				"5 голосовать  П.софт выход");
		key = wait_key_ms(left - (sec - 1) * 1000);
		if (key < 0) return -1;
		if (key == KEYPAD_5 || key == KEYPAD_CENTER) return 1;
		if (key == KEYPAD_RSOFT) {
			uint32_t t0 = sys_timer_ms();
			int r = ui_pick("Закончить игру?", no_yes, 2, 0);
			if (r == -2) return -1;
			if (r == 1) return 0;
			end += sys_timer_ms() - t0;
		}
	}
	buzz(3);
	snprintf(note, sizeof(note), "Договоритесь, кто %s, и откройте ответ.", g->odd_who);
	draw_card(g->name, "Время!", UI_ACCENT, note, "5 открыть ответ");
	return wait_go();
}

static int play(struct game *g) {
	static const char *cards[MAX_CARDS], *limits[MAX_CARDS];
	static char title[48], big[32], note[320], msg[512];
	const struct mode *m = &g->modes[g->mode];
	const char *card, *odd_card, *limit[MAX_PLAYERS];
	char *buf, *lbuf = NULL, *line;
	int odd[MAX_PLAYERS], n, nl = 0, i, r;
	n = list_load(m->file, &buf, cards, MAX_CARDS);
	if (m->limits) nl = list_load(LIMITS_FILE, &lbuf, limits, MAX_CARDS);
	if (!n || (m->limits && !nl)) {
		sprintf(msg, "Нет списка: на карте нет %s.", n ? LIMITS_FILE : m->file);
		r = ui_message(g->name, msg);
		goto done;
	}
	rng_start();
	line = (char *)cards[rng(n)];
	card = next_field(&line);
	odd_card = next_field(&line);
	if (m->pairs && !*odd_card) odd_card = card;
	if (m->pairs && rng(2)) {
		const char *t = card;
		card = odd_card;
		odd_card = t;
	}
	memset(odd, 0, sizeof(odd));
	for (i = 0; i < g->odd; ) {
		int p = rng(g->players);
		if (!odd[p]) odd[p] = 1, i++;
	}
	for (i = 0; i < g->players; i++) limit[i] = nl ? limits[rng(nl)] : NULL;
	for (i = 0; i < g->players; i++) {
		const char *c = card, *t = m->note;
		uint16_t color = UI_TEXT;
		sprintf(title, "%s: роль %d из %d", g->name, i + 1, g->players);
		sprintf(big, "Игрок %d", i + 1);
		draw_card(title, big, UI_TEXT, "Возьмите телефон так, чтобы никто не видел экран.",
				"5 показать  П.софт выход");
		if ((r = wait_go()) <= 0) goto done;
		if (odd[i] && m->pairs) c = odd_card;
		else if (odd[i]) c = g->odd_big, t = m->odd_note, color = UI_ACCENT;
		if (limit[i]) snprintf(note, sizeof(note), "%s Ваше ограничение: %s.", t, limit[i]);
		else snprintf(note, sizeof(note), "%s", t);
		draw_card(title, c, color, note, "5 скрыть");
		if ((r = wait_go()) <= 0) goto done;
	}
	draw_card(g->name, "Все знают роли", UI_TEXT, "Положите телефон на стол.",
			"5 запустить таймер");
	if ((r = wait_go()) <= 0) goto done;
	if ((r = talk(g, rng(g->players))) <= 0) goto done;
	n = snprintf(msg, sizeof(msg), "%s: %s\n", m->label, card);
	if (m->pairs) n += snprintf(msg + n, sizeof(msg) - n, "%s: %s\n", m->odd_label, odd_card);
	n += snprintf(msg + n, sizeof(msg) - n, "\n%s", g->odd > 1 ? g->odd_many : g->odd_one);
	for (r = 0, i = 0; i < g->players; i++)
		if (odd[i]) n += snprintf(msg + n, sizeof(msg) - n, "%s %d", r++ ? "," : "", i + 1);
	sprintf(title, "%s: ответ", g->name);
	r = ui_message(title, msg);
done:
	free(buf);
	free(lbuf);
	return r < 0 ? -1 : 0;
}

static int settings(struct game *g) {
	static const char *const odd[] = { "1", "2", "3" };
	static char l[4][96], title[48];
	const char *modes[8];
	int sel = 0, i;
	for (i = 0; i < g->nmodes; i++) modes[i] = g->modes[i].name;
	sprintf(title, "Настройки: %s", g->name);
	for (;;) {
		const char *items[5];
		int r, p = 0;
		sprintf(l[0], "Режим: %s", g->modes[g->mode].name);
		sprintf(l[1], "Игроков: %d", g->players);
		sprintf(l[2], "%s: %d", g->odd_count, g->odd);
		sprintf(l[3], "Обсуждение: %d мин", g->time);
		for (i = 0; i < 4; i++) items[i] = l[i];
		items[4] = "Назад";
		r = ui_pick(title, items, 5, sel);
		if (r == -2) return -1;
		if (r < 0 || r == 4) break;
		sel = r;
		if (r == 0 && (p = ui_pick("Режим", modes, g->nmodes, g->mode)) >= 0) g->mode = p;
		if (r == 1 && ask_number("Игроков", "Игроков (3-12):", MAX_PLAYERS, &g->players) < 0) return -1;
		if (r == 2 && (p = ui_pick(g->odd_count, odd, MAX_ODD, g->odd - 1)) >= 0) g->odd = p + 1;
		if (r == 3 && ask_number("Обсуждение", "Минут на обсуждение:", MAX_TIME, &g->time) < 0) return -1;
		if (p == -2) return -1;
		if (g->players < 3) g->players = 3;
		if (g->odd >= g->players) g->odd = g->players - 1;
	}
	settings_save(g);
	return 0;
}

static int game_main(struct game *g) {
	static const char *const items[] = { "Новая игра", "Настройки", "Правила", "Назад" };
	char title[48];
	int sel = 0;
	settings_load(g);
	sprintf(title, "Правила: %s", g->name);
	for (;;) {
		sel = ui_pick(g->name, items, 4, sel);
		if (sel == -2) return -1;
		if (sel < 0 || sel == 3) return 0;
		if (sel == 0 && play(g) < 0) return -1;
		if (sel == 1 && settings(g) < 0) return -1;
		if (sel == 2 && ui_message(title, g->rules) < 0) return -1;
	}
}

/* ---- the games ---- */

static const struct mode spy_modes[] = {
	{ "Место", "party/spy.txt", 0, 0, "Место", NULL,
		"Это место. Среди вас шпион, который его не знает.",
		"Узнайте место по вопросам и не выдайте себя.",
		"Задавайте друг другу вопросы о месте, первым спрашивает игрок %d. "
		"Шпион может остановить игру и назвать место." },
	{ "Два слова", "party/words.txt", 1, 0, "Слово", "Слово шпиона",
		"Ваше слово. У шпиона другое, похожее, и он об этом не знает.", NULL,
		"По очереди называйте ассоциацию к своему слову, начинает игрок %d. "
		"Найдите того, у кого слово другое." },
	{ "Без слова", "party/words.txt", 0, 0, "Слово", NULL,
		"Ваше слово. Шпион его не знает.",
		"Угадайте слово по ассоциациям остальных и не выдайте себя.",
		"По очереди называйте ассоциацию к слову, начинает игрок %d. "
		"Шпион может остановить игру и назвать слово." },
	{ "С ограничением", "party/words.txt", 0, 1, "Слово", NULL,
		"Ваше слово. Шпион его не знает.",
		"Угадайте слово по ассоциациям остальных и не выдайте себя.",
		"По очереди называйте ассоциацию к слову, соблюдая своё ограничение, "
		"начинает игрок %d. Шпион может остановить игру и назвать слово." },
};

static struct game spy = {
	"Шпион", "state/spy.txt", "Вы шпион", "шпион", "Шпион: игрок", "Шпионы: игроки", "Шпионов",
	"Телефон идёт по кругу. Каждый игрок тайно смотрит свою роль, шпион видит "
	"«Вы шпион». Потом запускается таймер: игроки говорят по очереди и ищут "
	"шпиона, шпион старается не выдать себя. Когда время выйдет или все готовы "
	"голосовать (5), голосуют и открывают ответ.\n\n"
	"Режимы:\n"
	"Место - у всех одно место, шпион его не знает. Задавайте друг другу "
	"вопросы о месте. Шпион выигрывает, если угадает место или его не вычислят.\n\n"
	"Два слова - у всех одно слово, у шпиона похожее, и он не знает, что он "
	"шпион. Называйте ассоциации к своему слову.\n\n"
	"Без слова - у всех одно слово, шпион его не знает и угадывает по "
	"ассоциациям остальных.\n\n"
	"С ограничением - как «Без слова», но у каждого своё ограничение на "
	"ответы: например, нельзя называть цвета.",
	spy_modes, sizeof(spy_modes) / sizeof(*spy_modes), 4, 1, 5, 0
};

static const struct mode odd_modes[] = {
	{ "Правило", "party/rules.txt", 1, 0, "Правило", "Правило нарушителя",
		"Ваше правило: соблюдайте его, пока разговариваете. У кого-то из вас другое.", NULL,
		"Разговаривайте о чём угодно, соблюдая своё правило, начинает игрок %d. "
		"Найдите того, кто ведёт себя иначе." },
	{ "Задание", "party/tasks.txt", 1, 0, "Задание", "Задание нарушителя",
		"Ваше задание: ответьте на него вслух, когда придёт очередь. У кого-то из вас другое.", NULL,
		"По очереди отвечайте вслух на своё задание, начинает игрок %d. "
		"Найдите того, чей ответ не подходит." },
};

static struct game odd_one = {
	"Нарушитель", "state/odd.txt", NULL, "нарушитель", "Нарушитель: игрок", "Нарушители: игроки",
	"Нарушителей",
	"Телефон идёт по кругу. Каждый игрок тайно смотрит свою карточку. У всех она "
	"одна, а у нарушителя похожая, но другая, и он не знает, что он нарушитель.\n\n"
	"Режимы:\n"
	"Правило - правило поведения в разговоре: например, начинать каждую фразу "
	"со слова «ну». Разговаривайте и следите, кто ведёт себя иначе.\n\n"
	"Задание - вопрос, на который каждый по очереди отвечает вслух. Ищите ответ, "
	"который не подходит к вопросу большинства.\n\n"
	"Когда время выйдет или все готовы голосовать (5), голосуют и открывают ответ.",
	odd_modes, sizeof(odd_modes) / sizeof(*odd_modes), 4, 1, 5, 0
};

int spy_main(void) { return game_main(&spy); }
int odd_main(void) { return game_main(&odd_one); }
