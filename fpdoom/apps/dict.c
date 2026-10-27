#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "apps.h"
#include "dictfile.h"
#include "cards.h"

#define DICT_FILE "dict.bin"
#define MAX_INPUT 24
#define MULTITAP_MS 900
#define ARTICLE_MAX 3200

static const char *const tap_letters[10] = {
	" ", "-'", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"
};

static char input[MAX_INPUT + 1];
static int input_len;
static int last_tap_key = -1, tap_index;
static uint32_t last_tap_time;
static char article[ARTICLE_MAX];

static uint32_t rng_state;

static unsigned rng(unsigned n) {
	rng_state = rng_state * 1103515245 + 12345;
	return (rng_state >> 16) % n;
}

/* first line of the article, cut to max_chars characters */
static void short_gloss(int i, char *buf, int size, int max_chars) {
	char *p;
	int n = 0;
	dict_read(i, buf, size);
	for (p = buf; *p && *p != '\n' && n < max_chars; n++) {
		const char *q = p;
		utf8_next(&q);
		p = (char*)q;
	}
	*p = 0;
}

/* adds the word with the first lines of its article as a flashcard */
int dict_add_card(const char *word, const char *text) {
	char back[400];
	int len = strlen(text), r;
	if (len > (int)sizeof(back) - 1) len = sizeof(back) - 1;
	memcpy(back, text, len);
	back[len] = 0;
	while (len && ((unsigned char)back[len] & 0xc0) == 0x80) back[len--] = 0;
	r = cards_add(word, back);
	ui_note(r > 0 ? "Добавлено в карточки" : r == 0 ? "Уже есть в карточках" : "Ошибка записи");
	return r;
}

typedef struct { const char *word; } article_ctx;

static int article_key(int key, void *p) {
	article_ctx *ctx = p;
	if (key == KEYPAD_1) dict_add_card(ctx->word, article);
	else if (key == KEYPAD_CENTER || key == KEYPAD_5) return 1;
	return 0;
}

/* full article of word i; returns -1 on quit */
int dict_show(int i, const char *title) {
	article_ctx ctx;
	ctx.word = dict_head(i);
	dict_read(i, article, sizeof(article));
	return text_view(title ? title : ctx.word, article, "1 - в карточки  П.софт - назад",
			article_key, &ctx);
}

static void type_key(int digit) {
	const char *set = tap_letters[digit];
	uint32_t now = sys_timer_ms();
	int n = strlen(set);
	if (!n) return;
	if (digit == last_tap_key && now - last_tap_time < MULTITAP_MS && input_len) {
		tap_index = (tap_index + 1) % n;
		input[input_len - 1] = set[tap_index];
	} else if (input_len < MAX_INPUT) {
		tap_index = 0;
		input[input_len++] = set[0];
		input[input_len] = 0;
	}
	last_tap_key = digit;
	last_tap_time = now;
}

static int random_words(void) {
	for (;;) {
		int r = dict_show(rng(dict_count()), "Случайное слово");
		if (r < 0) return -1;
		ui_header("Случайное слово");
		draw_text_center(scr_h / 2 - FONT_H, 1, "5 - ещё слово", UI_TEXT);
		draw_text_center(scr_h / 2 + 4, 1, "П.софт - к поиску", UI_DIM);
		gfx_present();
		r = wait_key();
		if (r < 0) return -1;
		if (r != KEYPAD_CENTER && r != KEYPAD_5) return 0;
	}
}

int dict_main(void) {
	int sel = 0, top = 0;
	int row_h = FONT_H + 4;
	int list_y = UI_BAR_H * 2 + 4;
	int rows_vis = (scr_h - list_y - UI_BAR_H - 4) / row_h;
	int max_chars = (scr_w - 8) / FONT_W;

	if (dict_open(DICT_FILE) < 0)
		return ui_message("Словарь", "Не найден файл словаря dict.bin.\n\n"
				"Его делает на Mac команда tools/mkdict.py.");
	rng_state = sys_timer_ms();
	input_len = 0;
	input[0] = 0;

	for (;;) {
		int lo, n, i, key;
		char buf[64];

		lo = dict_lower_bound(input);
		/* everything from the first match to the end of the dictionary is scrollable */
		n = dict_count() - lo;
		if (sel >= n) sel = n ? n - 1 : 0;
		if (sel < top) top = sel;
		if (sel >= top + rows_vis) top = sel - rows_vis + 1;

		sprintf(buf, "Словарь  (%d слов)", dict_count());
		ui_header(buf);
		fill_rect(0, UI_BAR_H, scr_w, FONT_H + 8, RGB(30, 36, 60));
		sprintf(buf, "Поиск: %s_", input);
		draw_text(4, UI_BAR_H + 4, 1, buf, UI_ACCENT);

		for (i = 0; i < rows_vis && top + i < n; i++) {
			int w = lo + top + i, y = list_y + i * row_h, len;
			const char *head = dict_head(w);
			int match = !strncmp(head, input, input_len);
			if (top + i == sel) fill_rect(0, y - 2, scr_w, row_h, UI_SEL);
			len = text_len(head);
			draw_text(4, y, 1, head, match ? UI_TEXT : UI_DIM);
			if (len + 3 < max_chars) {
				char gloss[160];
				short_gloss(w, gloss, sizeof(gloss), max_chars - len - 2);
				draw_text(4 + (len + 2) * FONT_W, y, 1, gloss, top + i == sel ? UI_TEXT : UI_DIM);
			}
		}
		ui_footer("2-9 буквы #стереть *случайное");
		gfx_present();

		key = wait_key();
		if (key < 0) return -1;
		switch (key) {
		case KEYPAD_UP: if (sel > 0) sel--; break;
		case KEYPAD_DOWN: if (sel < n - 1) sel++; break;
		case KEYPAD_LEFT: sel = sel > rows_vis ? sel - rows_vis : 0; break;
		case KEYPAD_RIGHT: sel = sel + rows_vis < n ? sel + rows_vis : (n ? n - 1 : 0); break;
		case KEYPAD_CENTER:
			if (n && dict_show(lo + sel, NULL) < 0) return -1;
			break;
		case KEYPAD_STAR:
			if (random_words() < 0) return -1;
			break;
		case KEYPAD_HASH:
			if (input_len) input[--input_len] = 0;
			last_tap_key = -1;
			sel = top = 0;
			break;
		case KEYPAD_RSOFT:
			if (!input_len) return 0;
			input[--input_len] = 0;
			last_tap_key = -1;
			sel = top = 0;
			break;
		default:
			if (key >= KEYPAD_0 && key <= KEYPAD_9) {
				type_key(key - KEYPAD_0);
				sel = top = 0;
			}
		}
	}
}
