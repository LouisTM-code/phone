#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "apps.h"
#include "cards.h"
#include "log.h"

#define CARDS_FILE "cards.txt"
#define MAX_CARDS 4000
#define NEW_PER_DAY 20

typedef struct {
	char *front, *back;
	int due, ivl, ease, reps, lapses;
} card_t;

static card_t *cards;
static int ncards, loaded, dirty;
static int stat_day, new_today;

int cards_count(void) { return ncards; }

static char *dup_str(const char *s, int len) {
	char *p = malloc(len + 1);
	if (p) { memcpy(p, s, len); p[len] = 0; }
	return p;
}

/* splits a tab-separated line in place */
static int split_tabs(char *s, char **f, int max) {
	int n = 0;
	f[n++] = s;
	while (*s && n < max) {
		if (*s == '\t') { *s = 0; f[n++] = s + 1; }
		s++;
	}
	return n;
}

int cards_load(void) {
	char *buf, *pos, *line;
	LOG_T0(t0);
	if (loaded) return 0;
	cards = malloc(MAX_CARDS * sizeof(*cards));
	if (!cards) return -1;
	loaded = 1;
	buf = file_load(CARDS_FILE, NULL);
	if (!buf) return 0;
	for (pos = buf; (line = next_line(&pos)); ) {
		char *f[8];
		int n;
		if (line[0] == '#') {
			const char *p = str_find(line, "day=");
			if (p) stat_day = atoi(p + 4);
			if ((p = str_find(line, "new=")) != NULL) new_today = atoi(p + 4);
			continue;
		}
		if (!line[0] || ncards >= MAX_CARDS) continue;
		n = split_tabs(line, f, 7);
		if (n < 2) continue;
		cards[ncards].front = dup_str(f[0], strlen(f[0]));
		cards[ncards].back = dup_str(f[1], strlen(f[1]));
		cards[ncards].due = n > 2 ? atoi(f[2]) : 0;
		cards[ncards].ivl = n > 3 ? atoi(f[3]) : 0;
		cards[ncards].ease = n > 4 ? atoi(f[4]) : 2500;
		cards[ncards].reps = n > 5 ? atoi(f[5]) : 0;
		cards[ncards].lapses = n > 6 ? atoi(f[6]) : 0;
		if (cards[ncards].ease < 1300) cards[ncards].ease = 2500;
		ncards++;
	}
	free(buf);
	LOG_OP(t0, "cards_load %d", ncards);
	return 0;
}

int cards_save(void) {
	FILE *fp;
	char *buf, *p;
	int i;
	size_t size = 64;
	LOG_T0(t0);
	if (!dirty) return 0;
	/* one big write: every fprintf would be a separate USB request */
	for (i = 0; i < ncards; i++)
		size += strlen(cards[i].front) + strlen(cards[i].back) + 64;
	if (!(buf = malloc(size))) return -1;
	p = buf + sprintf(buf, "#cards v1 day=%d new=%d\n", stat_day, new_today);
	for (i = 0; i < ncards; i++) {
		card_t *c = &cards[i];
		p += sprintf(p, "%s\t%s\t%d\t%d\t%d\t%d\t%d\n", c->front, c->back,
				c->due, c->ivl, c->ease, c->reps, c->lapses);
	}
	fp = fopen(CARDS_FILE, "wb");
	if (!fp) { free(buf); return -1; }
	fwrite(buf, 1, p - buf, fp);
	fclose(fp);
	LOG_OP(t0, "cards_save %d %uK", ncards, (unsigned)(p - buf) >> 10);
	free(buf);
	dirty = 0;
	return 0;
}

static int same_word(const char *a, const char *b) {
	for (; *a && *b; a++, b++) {
		char x = *a, y = *b;
		if (x >= 'A' && x <= 'Z') x += 32;
		if (y >= 'A' && y <= 'Z') y += 32;
		if (x != y) return 0;
	}
	return *a == *b;
}

/* tabs and newlines would break the file format */
static char *clean_field(const char *s, int max) {
	int len = strlen(s), i;
	char *p;
	if (len > max) {
		len = max;
		while (len && ((unsigned char)s[len] & 0xc0) == 0x80) len--;
	}
	p = dup_str(s, len);
	if (p) for (i = 0; p[i]; i++) if (p[i] == '\t' || p[i] == '\n' || p[i] == '\r') p[i] = ' ';
	return p;
}

static int add_card(const char *front, const char *back) {
	int i;
	for (i = 0; i < ncards; i++)
		if (same_word(cards[i].front, front)) return 0;
	if (ncards >= MAX_CARDS) return -1;
	cards[ncards].front = clean_field(front, 100);
	cards[ncards].back = clean_field(back, 600);
	cards[ncards].due = 0;
	cards[ncards].ivl = 0;
	cards[ncards].ease = 2500;
	cards[ncards].reps = 0;
	cards[ncards].lapses = 0;
	ncards++;
	dirty = 1;
	return 1;
}

int cards_add(const char *front, const char *back) {
	int r;
	if (cards_load() < 0) return -1;
	r = add_card(front, back);
	if (r > 0 && cards_save() < 0) return -1;
	return r;
}

int cards_import(const char *path) {
	char *buf, *pos, *line;
	int added = 0;
	if (cards_load() < 0 || !(buf = file_load(path, NULL))) return -1;
	for (pos = buf; (line = next_line(&pos)); ) {
		char *tab = strchr(line, '\t');
		if (!tab || line[0] == '#') continue;
		*tab = 0;
		if (add_card(line, tab + 1) > 0) added++;
	}
	free(buf);
	cards_save();
	return added;
}

/* ---- review session ---- */

enum { ANS_AGAIN = 1, ANS_HARD, ANS_GOOD, ANS_EASY };

/* the interval in days an answer would give, 0 = see again today */
static int next_ivl(const card_t *c, int ans, int *ease) {
	int e = c->ease, ivl;
	if (!c->ivl) {
		/* new or relearning card */
		if (ans == ANS_AGAIN || ans == ANS_HARD) ivl = 0;
		else if (ans == ANS_GOOD) ivl = 1;
		else ivl = 4;
	} else switch (ans) {
	case ANS_AGAIN: ivl = 0; e -= 200; break;
	case ANS_HARD: ivl = c->ivl * 12 / 10; e -= 150; break;
	case ANS_GOOD: ivl = c->ivl * e / 1000; break;
	default: ivl = c->ivl * e / 1000 * 13 / 10; e += 150; break;
	}
	if (c->ivl && ans != ANS_AGAIN && ivl <= c->ivl) ivl = c->ivl + 1;
	if (ivl > 3650) ivl = 3650;
	if (e < 1300) e = 1300;
	if (ease) *ease = e;
	return ivl;
}

static void ivl_text(int ivl, char *buf) {
	if (!ivl) strcpy(buf, "<1д");
	else if (ivl < 31) sprintf(buf, "%dд", ivl);
	else if (ivl < 365) sprintf(buf, "%dмес", ivl / 30);
	else sprintf(buf, "%dг", ivl / 365);
}

static void new_day(void) {
	if (stat_day != app_today) {
		stat_day = app_today;
		new_today = 0;
		dirty = 1;
	}
}

static int count_due(int *nreview, int *nnew) {
	int i;
	*nreview = *nnew = 0;
	for (i = 0; i < ncards; i++) {
		if (!cards[i].reps) (*nnew)++;
		else if (cards[i].due <= app_today) (*nreview)++;
	}
	return *nreview + (*nnew < NEW_PER_DAY - new_today ? *nnew : NEW_PER_DAY - new_today);
}

/* shows one card, returns the answer 1..4, 0 to stop, -1 on quit */
static int review_card(card_t *c, int left) {
	int shown = 0;
	for (;;) {
		char title[64], buf[96], a[24], b[24], d[24], e[24];
		int y, key, s;
		sprintf(title, "Карточки  осталось %d", left);
		ui_header(title);
		s = text_len(c->front) * FONT_W * 2 <= (int)scr_w - 16 ? 2 : 1;
		y = draw_text_wrap(8, UI_BAR_H + 16, scr_w - 16, s, c->front, UI_ACCENT);
		if (!c->reps) draw_text(8, y, 1, "новая", UI_DIM);
		y += FONT_H + 6;
		fill_rect(8, y, scr_w - 16, 1, UI_DIM);
		if (shown) {
			static int starts[24], lens[24];
			int n = wrap_text(c->back, (scr_w - 16) / FONT_W, starts, lens, 24), i;
			int max_lines = (scr_h - y - FONT_H * 5 - 16) / (FONT_H + 2);
			for (i = 0; i < n && i < max_lines; i++)
				draw_text_n(8, y + 8 + i * (FONT_H + 2), 1, c->back + starts[i], lens[i], UI_TEXT);
			ivl_text(next_ivl(c, 1, NULL), a);
			ivl_text(next_ivl(c, 2, NULL), b);
			ivl_text(next_ivl(c, 3, NULL), d);
			ivl_text(next_ivl(c, 4, NULL), e);
			y = scr_h - FONT_H * 4 - 14;
			sprintf(buf, "1 Снова %s", a); draw_text(8, y, 1, buf, RGB(240, 120, 110));
			sprintf(buf, "2 Трудно %s", b); draw_text(128, y, 1, buf, RGB(240, 190, 100));
			sprintf(buf, "3 Хорошо %s", d); draw_text(8, y + FONT_H + 4, 1, buf, UI_OK);
			sprintf(buf, "4 Легко %s", e); draw_text(128, y + FONT_H + 4, 1, buf, RGB(120, 180, 250));
			ui_footer("5 = Хорошо  П.софт - стоп");
		} else {
			draw_text_center(y + 24, 1, "Вспомните перевод", UI_DIM);
			ui_footer("5 - показать ответ");
		}
		gfx_present();
		key = wait_key();
		if (key < 0) return -1;
		if (key == KEYPAD_RSOFT) return 0;
		if (!shown) {
			if (key == KEYPAD_CENTER || key == KEYPAD_5) shown = 1;
			continue;
		}
		if (key >= KEYPAD_1 && key <= KEYPAD_4) return key - KEYPAD_0;
		if (key == KEYPAD_CENTER || key == KEYPAD_5) return ANS_GOOD;
	}
}

static int run_session(void) {
	static int queue[MAX_CARDS + 64];
	int n = 0, i, qpos = 0, nnew = 0;

	new_day();
	for (i = 0; i < ncards; i++)
		if (cards[i].reps && cards[i].due <= app_today) queue[n++] = i;
	for (i = 0; i < ncards && nnew < NEW_PER_DAY - new_today; i++)
		if (!cards[i].reps) { queue[n++] = i; nnew++; }
	if (!n) return ui_message("Карточки", "На сегодня всё повторено! Приходите завтра.\n\n"
			"Новые слова добавляются из словаря и из книг кнопкой 1.");

	while (qpos < n) {
		card_t *c = &cards[queue[qpos]];
		int ans = review_card(c, n - qpos), e, ivl;
		if (ans <= 0) { cards_save(); return ans; }
		ivl = next_ivl(c, ans, &e);
		if (!c->reps) new_today++;
		if (ans == ANS_AGAIN && c->ivl) c->lapses++;
		c->ease = e;
		c->ivl = ivl;
		c->reps++;
		c->due = app_today + ivl;
		dirty = 1;
		if (!ivl && n < MAX_CARDS + 60) {
			/* see it again a few cards later */
			int at = qpos + 4 < n ? qpos + 4 : n;
			memmove(queue + at + 1, queue + at, (n - at) * sizeof(*queue));
			queue[at] = queue[qpos];
			n++;
		}
		qpos++;
		/* the phone may lose power any moment, keep the file current */
		cards_save();
	}
	cards_save();
	return ui_message("Карточки", "Отлично! Сессия закончена.");
}

#define DECK_INDEX "decks/index.txt"

static int add_deck(void) {
	static char files[16][48], names[16][64], msg[128];
	const char *items[16];
	char *buf = file_load(DECK_INDEX, NULL), *pos, *line;
	int n = 0, r;
	if (!buf) return ui_message("Наборы слов", "Наборов пока нет.");
	for (pos = buf; n < 16 && (line = next_line(&pos)); ) {
		char *tab = strchr(line, '\t');
		if (!tab) continue;
		*tab = 0;
		snprintf(files[n], sizeof(files[n]), "%s", line);
		snprintf(names[n], sizeof(names[n]), "%s", tab + 1);
		items[n] = names[n];
		n++;
	}
	free(buf);
	if (!n) return ui_message("Наборы слов", "Наборов пока нет.");
	r = ui_pick("Добавить слова уровня", items, n, 0);
	if (r == -2) return -1;
	if (r < 0) return 0;
	ui_header("Наборы слов");
	draw_text_center(scr_h / 2 - FONT_H / 2, 1, "Добавляю...", UI_DIM);
	gfx_present();
	r = cards_import(files[r]);
	if (r < 0) return ui_message("Наборы слов", "Не удалось прочитать набор.");
	snprintf(msg, sizeof(msg), "Добавлено новых слов: %d.\n\n"
			"Каждый день будет %d новых слов плюс повторения.", r, NEW_PER_DAY);
	return ui_message("Наборы слов", msg);
}

static int list_cards(void) {
	static const char **items;
	int i, r, sel = 0;
	if (!ncards) return ui_message("Все карточки", "Карточек пока нет.");
	items = malloc(ncards * sizeof(*items));
	if (!items) return 0;
	for (i = 0; i < ncards; i++) items[i] = cards[i].front;
	for (;;) {
		r = ui_pick("Все карточки", items, ncards, sel);
		if (r < 0) break;
		sel = r;
		{
			static char text[800];
			card_t *c = &cards[r];
			char ivl[16];
			ivl_text(c->ivl, ivl);
			snprintf(text, sizeof(text), "%s\n\n%s\n\nПовторений: %d, ошибок: %d\nИнтервал: %s",
					c->front, c->back, c->reps, c->lapses, c->reps ? ivl : "новая");
			if (ui_message("Карточка", text) < 0) { r = -2; break; }
		}
	}
	free(items);
	return r == -2 ? -1 : 0;
}

int cards_main(void) {
	if (cards_load() < 0) return ui_message("Карточки", "Не хватает памяти.");
	new_day();
	for (;;) {
		static char title[96], line1[96], line2[96], line3[96], line4[96];
		const char *items[4];
		int nrev, nnew, r;
		count_due(&nrev, &nnew);
		sprintf(line1, "Учить (%d повт. + %d нов.)", nrev,
				nnew < NEW_PER_DAY - new_today ? nnew : NEW_PER_DAY - new_today);
		sprintf(line2, "Все карточки (%d)", ncards);
		strcpy(line3, "Добавить слова уровня...");
		strcpy(line4, "Назад");
		items[0] = line1; items[1] = line2; items[2] = line3; items[3] = line4;
		sprintf(title, "Карточки  (%d)", ncards);
		r = ui_pick(title, items, 4, ncards ? 0 : 2);
		if (r == -2) { cards_save(); return -1; }
		if (r < 0 || r == 3) break;
		if (r == 0 && run_session() < 0) return -1;
		if (r == 1 && list_cards() < 0) return -1;
		if (r == 2 && add_deck() < 0) return -1;
	}
	cards_save();
	return 0;
}
