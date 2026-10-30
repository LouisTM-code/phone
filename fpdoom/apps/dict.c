#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "apps.h"
#include "dictfile.h"
#include "cards.h"

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

/* first line of the article (the one below its transcription), cut to max_chars characters */
static void short_gloss(int i, char *buf, int size, int max_chars) {
	char *p;
	int n = 0;
	dict_read(i, buf, size);
	if (buf[0] == '/' && (p = strchr(buf, '\n'))) memmove(buf, p + 1, strlen(p + 1) + 1);
	for (p = buf; *p && *p != '\n' && n < max_chars; n++) {
		const char *q = p;
		utf8_next(&q);
		p = (char*)q;
	}
	*p = 0;
}

/* appends n bytes of s to buf (size bytes) at *len, whole letters only; 0 when full */
static int put_str(char *buf, int size, int *len, const char *s, int n) {
	if (n < 0) n = strlen(s);
	if (*len + n >= size) {
		n = size - 1 - *len;
		while (n > 0 && ((unsigned char)s[n] & 0xc0) == 0x80) n--;
		memcpy(buf + *len, s, n > 0 ? n : 0);
		*len += n > 0 ? n : 0;
		buf[*len] = 0;
		return 0;
	}
	memcpy(buf + *len, s, n);
	*len += n;
	buf[*len] = 0;
	return 1;
}

/* the answer side of a card: every Russian meaning of a lexicon article by part
   of speech, "n. дом; семья / v. размещать" (as tools/mkdecks.py); "" when the
   article has no numbered meanings */
static void card_back(const char *text, char *buf, int size) {
	const char *s = text, *pos = NULL;
	int len = 0, pos_len = 0, first = 1;
	buf[0] = 0;
	while (*s) {
		const char *e = s, *t;
		for (e = s; *e && *e != '\n'; e++);
		if (!strncmp(s, "---", 3)) break;  /* another dictionary below */
		if (!strncmp(s, "modal v.", 8) || !strncmp(s, "phr. v.", 7)) {
			pos = s, pos_len = s[0] == 'm' ? 8 : 7;
		} else if (*s >= 'a' && *s <= 'z') {
			/* "n. A1", "v. (ran, run) A1"; "phr. run out" has no meanings to take */
			for (t = s; *t >= 'a' && *t <= 'z'; t++);
			if (*t == '.') pos = s, pos_len = t + 1 - s;
		} else if (*s >= '0' && *s <= '9') {
			/* "1. дом - a building where people live" */
			for (t = s; *t >= '0' && *t <= '9'; t++);
			if (*t == '.') {
				const char *m = t + 1, *me;
				while (*m == ' ') m++;
				for (me = m; me < e && strncmp(me, " - ", 3); me++);
				while (me > m && me[-1] == ' ') me--;
				if (me > m) {
					if (pos) {
						if (!put_str(buf, size, &len, first ? "" : " / ", -1) ||
								!put_str(buf, size, &len, pos, pos_len) ||
								!put_str(buf, size, &len, " ", 1)) return;
						pos = NULL;
					} else if (!first && !put_str(buf, size, &len, "; ", 2)) return;
					if (!put_str(buf, size, &len, m, me - m)) return;
					first = 0;
				}
			}
		}
		s = *e ? e + 1 : e;
	}
}

/* all the meanings, or the start of an article without numbered ones */
static void make_back(const char *text, char *back, int size) {
	card_back(text, back, size);
	if (!back[0]) {
		snprintf(back, size, "%s", text);
		utf8_trim(back);
	}
}

/* the first example of the article (an indented line) that has a form of the word */
static void card_example(const char *word, const char *text, char *buf, int size) {
	const char *s = text;
	buf[0] = 0;
	while (*s && strncmp(s, "---", 3)) {
		const char *e, *m;
		int len;
		for (e = s; *e && *e != '\n'; e++);
		if (s[0] == ' ') {
			for (m = s; *m == ' '; m++);
			snprintf(buf, size, "%.*s", (int)(e - m), m);
			utf8_trim(buf);
			if (cards_cloze(word, buf, &len) >= 0) return;
			buf[0] = 0;
		}
		s = *e ? e + 1 : e;
	}
}

/* adds the word with all its meanings as a flashcard, with the sentence it was
   met in (or NULL: an example of the article) */
static int dict_add_card(const char *word, const char *text, const char *context) {
	char back[600], ex[300];
	int r;
	make_back(text, back, sizeof(back));
	if (context && context[0]) snprintf(ex, sizeof(ex), "%s", context);
	else card_example(word, text, ex, sizeof(ex));
	r = cards_add(word, back, ex);
	ui_note(r > 0 ? "Добавлено в карточки" : r == 0 ? "Уже есть в карточках" : "Ошибка записи");
	return r;
}

/* adds word i and the words of its family line, saving once; -1 on a write error */
static int add_family(int i, const char *text) {
	static char other[ARTICLE_MAX];
	char back[600], ex[300], word[64];
	const char *p = str_find(text, FAMILY_LABEL);
	int added = 0, r;
	make_back(text, back, sizeof(back));
	card_example(dict_head(i), text, ex, sizeof(ex));
	if ((r = cards_put(dict_head(i), back, ex)) < 0) goto fail;
	added += r;
	for (p += sizeof(FAMILY_LABEL) - 1; *p && *p != '\n'; ) {
		int len = 0, j;
		while (*p && *p != '\n' && *p != ',' && len < 63) word[len++] = *p++;
		word[len] = 0;
		while (*p == ',' || *p == ' ') p++;
		if ((j = dict_find(word)) < 0) continue;
		dict_read(j, other, sizeof(other));
		make_back(other, back, sizeof(back));
		card_example(word, other, ex, sizeof(ex));
		if ((r = cards_put(word, back, ex)) < 0) goto fail;
		added += r;
	}
	if (cards_save() < 0) goto fail;
	{
		static char msg[48];
		snprintf(msg, sizeof(msg), "Семья в карточках, новых: %d", added);
		ui_note(msg);
	}
	return 0;
fail:
	cards_save();
	ui_note("Ошибка записи");
	return -1;
}

/* ---- the article: scrolling, and a cursor on its English words that opens their articles */

#define ART_LINES 400
#define ART_WORDS 600
#define HISTORY 16

typedef struct { int16_t line, col, off, len; } art_word_t;
static int art_starts[ART_LINES], art_lens[ART_LINES], art_nlines;
static art_word_t art_words[ART_WORDS];
static int art_nwords;

/* the articles opened from words, to go back to */
static struct { int word, top, sel; char title[136]; } history[HISTORY];

static int is_latin(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* "adj.", "pl." and the like are labels, not words to look up */
static int is_label(const char *s, int len) {
	static const char *const labels[] = {
		"adj", "adv", "prep", "conj", "pron", "det", "num", "excl", "phr", "pl", "sb", "sth" };
	unsigned k;
	if (s[len] != '.') return 0;
	for (k = 0; k < sizeof(labels) / sizeof(*labels); k++)
		if ((int)strlen(labels[k]) == len && !strncmp(s, labels[k], len)) return 1;
	return 0;
}

/* wraps the article and finds its English words of two letters or more */
static void art_layout(int cols) {
	int i;
	art_nlines = wrap_text(article, cols, art_starts, art_lens, ART_LINES);
	art_nwords = 0;
	for (i = 0; i < art_nlines; i++) {
		const char *s = article + art_starts[i], *end = s + art_lens[i];
		int col = 0;
		while (s < end) {
			if (is_latin(*s)) {
				const char *w = s;
				int len;
				/* inner apostrophes: don't, it's */
				while (s < end && (is_latin(*s) || (*s == '\'' && s + 1 < end && is_latin(s[1])))) s++;
				len = s - w;
				if (len > 1 && !is_label(w, len) && art_nwords < ART_WORDS) {
					art_word_t *aw = &art_words[art_nwords++];
					aw->line = i, aw->col = col, aw->off = w - article, aw->len = len;
				}
				col += len;
			} else {
				utf8_next(&s);
				col++;
			}
		}
	}
}

/* the word nearest to the column of word cur on the next line with words up
   (dir -1) or down (dir 1), or cur */
static int art_vertical(int cur, int dir) {
	int i, line = -1, best = cur, best_d = 1 << 30;
	for (i = cur + dir; i >= 0 && i < art_nwords; i += dir) {
		int d = art_words[i].col - art_words[cur].col;
		if (art_words[i].line == art_words[cur].line) continue;
		if (line >= 0 && art_words[i].line != line) break;
		line = art_words[i].line;
		if (d < 0) d = -d;
		if (d < best_d) best = i, best_d = d;
	}
	return best;
}

static void art_draw(const char *title, int in_cards, int top, int rows, int sel, const char *footer) {
	int i, line_h = FONT_H + 2, cols = (scr_w - 8) / FONT_W;
	ui_header(title);
	if (in_cards) {
		/* the mark, shortened when the title leaves no room */
		const char *mark = text_len(title) + 14 <= cols ? "√ в карточках" : "√";
		draw_text(scr_w - 4 - text_len(mark) * FONT_W, 3, 1, mark, UI_OK);
	}
	if (sel >= 0) {
		art_word_t *w = &art_words[sel];
		fill_rect(4 + w->col * FONT_W - 1, UI_BAR_H + 4 + (w->line - top) * line_h - 1,
				w->len * FONT_W + 2, FONT_H + 2, UI_SEL);
	}
	for (i = 0; i < rows && top + i < art_nlines; i++)
		draw_text_n(4, UI_BAR_H + 4 + i * line_h, 1, article + art_starts[top + i],
				art_lens[top + i], ui_line_color(article, art_starts[top + i]));
	if (sel >= 0) {
		/* the word under the cursor again, dark on the cursor */
		art_word_t *w = &art_words[sel];
		draw_text_n(4 + w->col * FONT_W, UI_BAR_H + 4 + (w->line - top) * line_h, 1,
				article + w->off, w->len, UI_INK);
	}
	if (art_nlines > rows) {
		/* scrollbar */
		int track = rows * line_h, h = track * rows / art_nlines, y = track * top / art_nlines;
		fill_rect(scr_w - 3, UI_BAR_H + 4, 2, track, UI_LINE);
		fill_rect(scr_w - 3, UI_BAR_H + 4 + y, 2, h < 6 ? 6 : h, ui_color);
	}
	ui_footer(footer);
	gfx_present();
}

/* full article of word i, met in the sentence context (or NULL) that its card
   gets; 0 opens a cursor on its English words, 5 on a word opens that word's
   article, П.софт goes back; returns -1 on quit */
int dict_show(int i, const char *title, const char *context) {
	int cols = (scr_w - 8) / FONT_W, line_h = FONT_H + 2;
	int rows = (scr_h - UI_BAR_H * 2 - 8) / line_h;
	int depth = 0, top = 0, sel = -1, in_cards, family;
	char cur_title[136];

	snprintf(cur_title, sizeof(cur_title), "%s", title ? title : dict_head(i));
	utf8_trim(cur_title);
	dict_read(i, article, sizeof(article));
	art_layout(cols);
	in_cards = cards_has(dict_head(i));
	family = str_find(article, FAMILY_LABEL) != NULL;

	for (;;) {
		int key, open = 0;
		art_draw(cur_title, in_cards, top, rows, sel,
				sel >= 0 ? "5-открыть  0/П-назад" :
				family ? "0-слова 1-карточка 3-семья" : "0-слова 1-в карточки П-назад");
		key = wait_key();
		if (key < 0) return -1;
		if (key == KEYPAD_1) {
			if (dict_add_card(dict_head(i), article, depth ? NULL : context) >= 0) in_cards = 1;
		} else if (key == KEYPAD_3 && family) {
			if (add_family(i, article) >= 0) in_cards = 1;
		} else if (sel < 0) {
			if (key == KEYPAD_UP || key == KEYPAD_2) { if (top > 0) top--; }
			else if (key == KEYPAD_DOWN || key == KEYPAD_8) { if (top + rows < art_nlines) top++; }
			else if (key == KEYPAD_LEFT || key == KEYPAD_4) top = top > rows ? top - rows : 0;
			else if (key == KEYPAD_RIGHT || key == KEYPAD_6) {
				if (top + rows < art_nlines) top += rows;
				if (top + rows > art_nlines) top = art_nlines > rows ? art_nlines - rows : 0;
			} else if (key == KEYPAD_0) {
				/* the first word on the screen */
				for (sel = 0; sel < art_nwords && art_words[sel].line < top; sel++);
				if (sel == art_nwords || art_words[sel].line >= top + rows) {
					sel = -1;
					ui_note("Здесь нет английских слов");
				}
			} else if (key == KEYPAD_CENTER || key == KEYPAD_5) return 0;
			else if (key == KEYPAD_RSOFT) open = -1;
		} else {
			if (key == KEYPAD_LEFT || key == KEYPAD_4) { if (sel > 0) sel--; }
			else if (key == KEYPAD_RIGHT || key == KEYPAD_6) { if (sel + 1 < art_nwords) sel++; }
			else if (key == KEYPAD_UP || key == KEYPAD_2) sel = art_vertical(sel, -1);
			else if (key == KEYPAD_DOWN || key == KEYPAD_8) sel = art_vertical(sel, 1);
			else if (key == KEYPAD_0 || key == KEYPAD_RSOFT) sel = -1;
			else if (key == KEYPAD_CENTER || key == KEYPAD_5) open = 1;
			if (sel >= 0) {
				int line = art_words[sel].line;
				if (line < top) top = line;
				if (line >= top + rows) top = line - rows + 1;
			}
		}

		if (open > 0) {
			char word[64], base[64];
			art_word_t *w = &art_words[sel];
			int j, len = w->len < 63 ? w->len : 63, k;
			memcpy(word, article + w->off, len);
			word[len] = 0;
			j = dict_lookup(word, base, sizeof(base));
			if (j < 0) { ui_note("Этого слова нет в словаре"); continue; }
			if (j == i) { ui_note("Это слово уже открыто"); continue; }
			if (depth == HISTORY) {
				/* the oldest article is forgotten */
				memmove(history, history + 1, sizeof(history) - sizeof(*history));
				depth--;
			}
			history[depth].word = i, history[depth].top = top, history[depth].sel = sel;
			memcpy(history[depth].title, cur_title, sizeof(cur_title));
			depth++;
			/* "went -> go" when the word is a form of the headword */
			for (k = 0; word[k]; k++) if (word[k] >= 'A' && word[k] <= 'Z') word[k] += 32;
			if (strcmp(word, base)) snprintf(cur_title, sizeof(cur_title), "%s -> %s", word, base);
			else snprintf(cur_title, sizeof(cur_title), "%s", base);
			i = j, top = 0, sel = -1;
		} else if (open < 0) {
			if (!depth) return 0;
			depth--;
			i = history[depth].word, top = history[depth].top, sel = history[depth].sel;
			memcpy(cur_title, history[depth].title, sizeof(cur_title));
		} else continue;
		dict_read(i, article, sizeof(article));
		art_layout(cols);
		in_cards = cards_has(dict_head(i));
		family = str_find(article, FAMILY_LABEL) != NULL;
	}
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
		int r = dict_show(rng(dict_count()), "Случайное слово", NULL);
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
	int max_chars = (scr_w - 16) / FONT_W;

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
		fill_rect(4, UI_BAR_H + 2, scr_w - 8, FONT_H + 6, ui_color);
		fill_rect(6, UI_BAR_H + 4, scr_w - 12, FONT_H + 2, UI_CARD);
		sprintf(buf, "Поиск: %s", input);
		i = draw_text(10, UI_BAR_H + 5, 1, buf, UI_TEXT);
		fill_rect(i + 1, UI_BAR_H + 5, 2, FONT_H, ui_color);

		for (i = 0; i < rows_vis && top + i < n; i++) {
			int w = lo + top + i, y = list_y + i * row_h, len;
			const char *head = dict_head(w);
			int match = !strncmp(head, input, input_len);
			int s = top + i == sel;
			if (s) fill_rect(4, y - 2, scr_w - 8, row_h, UI_SEL);
			len = text_len(head);
			/* words that have a card are green */
			draw_text_style(8, y, s ? TEXT_BOLD : 0, head, -1,
					s ? UI_INK : cards_has(head) ? UI_OK : match ? UI_TEXT : UI_DIM);
			if (len + 3 < max_chars) {
				char gloss[160];
				short_gloss(w, gloss, sizeof(gloss), max_chars - len - 2);
				draw_text(8 + (len + 2) * FONT_W, y, 1, gloss, s ? UI_INK : UI_DIM);
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
			if (n && dict_show(lo + sel, NULL, NULL) < 0) return -1;
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
