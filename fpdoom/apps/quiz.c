#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "apps.h"

/* Quizzes: a question and four answers to choose from, in rounds, with
   statistics and no schedule (that is the flashcards' way). A set is a table
   on the card, quiz/<name>.txt, listed in quiz/index.txt, its pictures and
   signs in quiz/<name>.bin (both formats: tools/mkquiz.py). A topic of a set
   pairs two columns and is asked both ways, with one statistics per item. A
   set's statistics are state/<name>.qz, the round length and the direction
   state/quiz.txt. */

#define INDEX_FILE "quiz/index.txt"
#define SETTINGS_FILE "state/quiz.txt"
#define MAX_SETS 32
#define MAX_COLS 8
#define MAX_MODES 12
#define MAX_GROUPS 16
#define MAX_ROWS 1000
#define NO_GROUP 255
#define MASTERED 2   /* right answers in a row that make an item learned */
#define MAX_HARD 10  /* items with the most mistakes in the statistics */

#define COL_RIGHT RGB(40, 130, 60)
#define COL_WRONG RGB(160, 45, 45)
#define COL_OPT   UI_CARD

struct mode { const char *name; int a, b; };  /* a topic: asked a -> b, or b -> a */
struct pic { unsigned long off; int w, h, mask; };  /* mask: alpha bytes, else RGB565 */
struct stat { unsigned short right, total; unsigned char streak; };

static const int round_lens[] = { 10, 20, 30, 0 };  /* 0: all */
static int round_len = 10;
static const char *const dir_names[] = { "прямое", "обратное", "вперемешку" };
static int direction = 2;

/* the open set */
static char *set_buf;
static const char *cols[MAX_COLS];
static int ncols, group_col;
static char image_col[MAX_COLS];
static int icon_col[MAX_COLS];       /* the column of signs drawn with a column's text, or -1 */
static int big_h[MAX_COLS], small_h[MAX_COLS], small_w[MAX_COLS];  /* the largest pictures */
static struct mode modes[MAX_MODES];
static int nmodes;
static const char *groups[MAX_GROUPS];
static int ngroups;
static const char *(*rows)[MAX_COLS];
static unsigned char *row_group;
static int nrows;
static struct stat *stats;  /* [row * nmodes + mode] */
static int rounds, best_streak;
static char stats_path[64];
static FILE *pics;
static struct pic *pic;  /* [number * 2 + small] */
static int npics;
static uint8_t *pic_px;

static uint32_t rng_state;

static unsigned rng(unsigned n) {
	rng_state = rng_state * 1103515245 + 12345;
	return (rng_state >> 16) % n;
}

#define STAT(r, m) (&stats[(r) * nmodes + (m)])

static void settings_load(void) {
	char *buf = file_load(SETTINGS_FILE, NULL), *pos, *line;
	if (!buf) return;
	for (pos = buf; (line = next_line(&pos)); )
		if (!strncmp(line, "round ", 6)) round_len = atoi(line + 6);
		else if (!strncmp(line, "dir ", 4)) direction = atoi(line + 4);
	free(buf);
	if (direction < 0 || direction > 2) direction = 2;
}

static void settings_save(void) {
	FILE *fp = fopen(SETTINGS_FILE, "wb");
	if (!fp) { ui_note("Не удалось записать настройки"); return; }
	fprintf(fp, "round %d\ndir %d\n", round_len, direction);
	fclose(fp);
}

static int col_index(const char *name) {
	int i;
	for (i = 0; i < ncols; i++) if (!strcmp(cols[i], name)) return i;
	return -1;
}

static int row_index(const char *key) {
	int i;
	for (i = 0; i < nrows; i++) if (!strcmp(rows[i][0], key)) return i;
	return -1;
}

static void stats_load(void) {
	char *buf = file_load(stats_path, NULL), *pos, *line;
	rounds = best_streak = 0;
	if (!buf) return;
	for (pos = buf; (line = next_line(&pos)); ) {
		char *p = line, *key, *mode, *nums;
		int r, m;
		if (!strncmp(line, "rounds ", 7)) { rounds = atoi(line + 7); continue; }
		if (!strncmp(line, "best ", 5)) { best_streak = atoi(line + 5); continue; }
		key = next_field(&p);
		mode = next_field(&p);
		nums = next_field(&p);
		if ((r = row_index(key)) < 0) continue;
		for (m = 0; m < nmodes && strcmp(modes[m].name, mode); m++);
		if (m == nmodes) continue;
		STAT(r, m)->right = strtol(nums, &nums, 10);
		STAT(r, m)->total = strtol(nums, &nums, 10);
		STAT(r, m)->streak = strtol(nums, &nums, 10);
	}
	free(buf);
}

static void stats_save(void) {
	FILE *fp = fopen(stats_path, "wb");
	int r, m;
	if (!fp) { ui_note("Не удалось записать статистику"); return; }
	fprintf(fp, "rounds %d\nbest %d\n", rounds, best_streak);
	for (r = 0; r < nrows; r++)
		for (m = 0; m < nmodes; m++) {
			struct stat *s = STAT(r, m);
			if (s->total) fprintf(fp, "%s\t%s\t%d %d %d\n", rows[r][0], modes[m].name,
					s->right, s->total, s->streak);
		}
	fclose(fp);
}

static void set_close(void) {
	free(set_buf); set_buf = NULL;
	free(rows); rows = NULL;
	free(row_group); row_group = NULL;
	free(stats); stats = NULL;
	free(pic); pic = NULL;
	free(pic_px); pic_px = NULL;
	if (pics) fclose(pics), pics = NULL;
}

/* "quiz/world.txt" -> dir + "world" + ext, the set's own folder if dir is NULL */
static void path_with(char *buf, int size, const char *path, const char *dir, const char *ext) {
	const char *name = strrchr(path, '/'), *dot;
	name = name ? name + 1 : path;
	dot = strrchr(name, '.');
	snprintf(buf, size, "%.*s%.*s%s", dir ? (int)strlen(dir) : (int)(name - path), dir ? dir : path,
			dot ? (int)(dot - name) : (int)strlen(name), name, ext);
}

static unsigned get16(const uint8_t *p) { return p[0] | p[1] << 8; }

/* the table of the set's pictures and the largest of each column: NULL, or
   what is wrong */
static const char *pics_open(const char *path) {
	uint8_t h[12];
	int i, c, r, most = 0;
	if (!(pics = fopen(path, "rb"))) return "Нет файла картинок набора";
	if (fread(h, 1, 4, pics) != 4) return "Файл картинок испорчен";
	npics = get16(h) | get16(h + 2) << 16;
	if (!(pic = malloc(npics * 2 * sizeof(*pic)))) return "Не хватает памяти";
	for (i = 0; i < npics * 2; i++) {
		if (fread(h, 1, 12, pics) != 12) return "Файл картинок испорчен";
		pic[i].off = get16(h) | (unsigned long)get16(h + 2) << 16;
		pic[i].w = get16(h + 4);
		pic[i].h = get16(h + 6);
		pic[i].mask = get16(h + 8);
		if (pic[i].w > (int)scr_w || pic[i].h > (int)scr_h / 2) return "Картинка больше экрана";
		if (pic[i].w * pic[i].h > most) most = pic[i].w * pic[i].h;
	}
	if (!(pic_px = malloc(most * 2))) return "Не хватает памяти";
	for (c = 0; c < ncols; c++)
		for (r = 0; r < nrows; r++) {
			int n = atoi(rows[r][c]);
			if (!image_col[c] || !*rows[r][c] || n < 0 || n >= npics) continue;
			if (pic[n * 2].h > big_h[c]) big_h[c] = pic[n * 2].h;
			if (pic[n * 2 + 1].h > small_h[c]) small_h[c] = pic[n * 2 + 1].h;
			if (pic[n * 2 + 1].w > small_w[c]) small_w[c] = pic[n * 2 + 1].w;
		}
	return NULL;
}

/* reads a set: NULL, or what is wrong with it */
static const char *set_open(const char *path) {
	const char *ma[MAX_MODES], *mb[MAX_MODES], *images[MAX_COLS], *icons[MAX_COLS][2];
	const char *group = NULL, *err;
	char *pos, *line, pic_path[64];
	int nimages = 0, nicons = 0, i, m;
	ncols = nmodes = ngroups = nrows = npics = 0;
	memset(big_h, 0, sizeof(big_h));
	memset(small_h, 0, sizeof(small_h));
	memset(small_w, 0, sizeof(small_w));
	group_col = -1;
	memset(image_col, 0, sizeof(image_col));
	for (i = 0; i < MAX_COLS; i++) icon_col[i] = -1;
	if (!(set_buf = file_load(path, NULL))) return "Набор не прочитан";
	if (!(rows = malloc(MAX_ROWS * sizeof(*rows))) || !(row_group = malloc(MAX_ROWS)))
		return "Не хватает памяти";
	for (pos = set_buf; (line = next_line(&pos)); ) {
		char *p = line, *f;
		if (!*line || *line == '#') continue;
		if (*line == '!') {
			f = next_field(&p);
			if (!strcmp(f, "!group")) group = next_field(&p);
			else if (!strcmp(f, "!image") && nimages < MAX_COLS) images[nimages++] = next_field(&p);
			else if (!strcmp(f, "!icon") && nicons < MAX_COLS) {
				icons[nicons][0] = next_field(&p);
				icons[nicons++][1] = next_field(&p);
			} else if (!strcmp(f, "!mode") && nmodes < MAX_MODES) {
				modes[nmodes].name = next_field(&p);
				ma[nmodes] = next_field(&p);
				mb[nmodes++] = next_field(&p);
			}
			continue;
		}
		if (!ncols) {
			while (p && ncols < MAX_COLS) cols[ncols++] = next_field(&p);
			continue;
		}
		if (nrows == MAX_ROWS) break;
		for (i = 0; i < ncols; i++) rows[nrows][i] = next_field(&p);
		nrows++;
	}
	if (!ncols || !nrows) return "В наборе нет строк";
	for (m = i = 0; i < nmodes; i++) {
		modes[m] = modes[i];
		modes[m].a = col_index(ma[i]);
		modes[m].b = col_index(mb[i]);
		if (modes[m].a >= 0 && modes[m].b >= 0 && modes[m].a != modes[m].b) m++;
	}
	if (!(nmodes = m)) return "В наборе нет вопросов (!mode)";
	for (i = 0; i < nimages; i++)
		if (col_index(images[i]) >= 0) image_col[col_index(images[i])] = 1;
	for (i = 0; i < nicons; i++) {
		int t = col_index(icons[i][0]), c = col_index(icons[i][1]);
		if (t >= 0 && c >= 0) icon_col[t] = c, image_col[c] = 1, nimages++;
	}
	if (group) group_col = col_index(group);
	for (i = 0; i < nrows; i++) {
		int g;
		row_group[i] = NO_GROUP;
		if (group_col < 0 || !*rows[i][group_col]) continue;
		for (g = 0; g < ngroups && strcmp(groups[g], rows[i][group_col]); g++);
		if (g == ngroups && ngroups < MAX_GROUPS) groups[ngroups++] = rows[i][group_col];
		if (g < ngroups) row_group[i] = g;
	}
	if (!(stats = calloc(nrows * nmodes, sizeof(*stats)))) return "Не хватает памяти";
	path_with(stats_path, sizeof(stats_path), path, "state/", ".qz");
	stats_load();
	path_with(pic_path, sizeof(pic_path), path, NULL, ".bin");
	if (nimages && (err = pics_open(pic_path))) return err;
	return NULL;
}

/* an item can be asked in a topic when it has both sides */
static int askable(int r, int m) {
	return *rows[r][modes[m].a] && *rows[r][modes[m].b];
}

static int mastered(int r, int m) { return STAT(r, m)->streak >= MASTERED; }

static int hard(int r, int m) {
	struct stat *s = STAT(r, m);
	return s->right < s->total && s->streak < MASTERED;
}

/* fg over bg by alpha a (0..255) */
static uint16_t blend(uint16_t fg, uint16_t bg, unsigned a) {
	unsigned r = ((fg >> 11) * a + (bg >> 11) * (255 - a)) >> 8;
	unsigned g = ((fg >> 5 & 63) * a + (bg >> 5 & 63) * (255 - a)) >> 8;
	unsigned b = ((fg & 31) * a + (bg & 31) * (255 - a)) >> 8;
	return r << 11 | g << 5 | b;
}

/* the picture numbered by field, big or small, centred on cx at y, a sign in
   color: its height */
static int draw_pic(const char *field, int small, int cx, int y, uint16_t color) {
	int n = atoi(field), i, j;
	struct pic *p;
	if (!pics || n < 0 || n >= npics) return 0;
	p = &pic[n * 2 + small];
	cx -= p->w / 2;
	if (fseek(pics, p->off, SEEK_SET) ||
			fread(pic_px, p->mask ? 1 : 2, p->w * p->h, pics) != (size_t)(p->w * p->h)) {
		fill_rect(cx, y, p->w, p->h, UI_CARD);
		return p->h;
	}
	for (j = 0; j < p->h && y + j < (int)scr_h; j++) {
		uint16_t *d = framebuf + (y + j) * scr_w + cx;
		if (!p->mask) {
			memcpy(d, (uint16_t*)pic_px + j * p->w, p->w * 2);
			continue;
		}
		for (i = 0; i < p->w; i++) {
			unsigned a = pic_px[j * p->w + i];
			if (a) d[i] = a == 255 ? color : blend(color, d[i], a);
		}
	}
	return p->h;
}

/* ---------------------------------------------------------------- a round */

/* a question: item row of topic m, asked from column q, answered by column a */
struct ask { int m, row, q, a; };

/* item i of a round list: the row and the direction */
#define ITEM(row, rev) ((row) * 2 + (rev))

static struct ask ask_of(int m, int item) {
	struct ask k;
	k.m = m;
	k.row = item / 2;
	k.q = item & 1 ? modes[m].b : modes[m].a;
	k.a = item & 1 ? modes[m].a : modes[m].b;
	return k;
}

/* the answers: opt[0..3], the item among them; the others from its group
   first, each with another answer and another question, so only one is right */
static int pick_options(const struct ask *k, int *opt) {
	static int cand[MAX_ROWS];
	int n, i, j = 1, pass, r = k->row;
	opt[0] = r;
	for (pass = 0; pass < 2 && j < 4; pass++) {
		for (n = i = 0; i < nrows; i++)
			if ((pass || row_group[i] == row_group[r]) && askable(i, k->m) &&
					strcmp(rows[i][k->q], rows[r][k->q]))
				cand[n++] = i;
		while (j < 4 && n) {
			int x = rng(n), c = cand[x];
			cand[x] = cand[--n];
			for (i = 0; i < j && strcmp(rows[opt[i]][k->a], rows[c][k->a]); i++);
			if (i == j) opt[j++] = c;
		}
	}
	i = rng(j);
	opt[0] = opt[i];
	opt[i] = r;
	return j;
}

/* the question at the top, returns the y below it */
static int draw_question(const struct ask *k) {
	const char *s = rows[k->row][k->q];
	char label[80];
	int y = UI_BAR_H + 6, ic = icon_col[k->q];
	if (image_col[k->q]) {
		draw_pic(s, 0, scr_w / 2, y, UI_ACCENT);
		return y + big_h[k->q] + 4;
	}
	if (ic >= 0) {
		draw_pic(rows[k->row][ic], 0, scr_w / 2, y, UI_ACCENT);
		y += big_h[ic] + 6;
	}
	if (text_len(s) * FONT_W * 2 <= (int)scr_w - 8) {
		draw_text_center(y, 2, s, UI_TEXT);
		y += FONT_H * 2 + 4;
	} else {
		y = draw_text_wrap(6, y, scr_w - 12, 1, s, UI_TEXT) + 4;
	}
	snprintf(label, sizeof(label), "%s?", cols[k->a]);
	draw_text_center(y, 1, label, UI_DIM);
	return y + FONT_H + 6;
}

/* the answers below y: the cursor on sel, after the answer (chosen >= 0)
   the right one green and a wrong choice red */
static void draw_options(const struct ask *k, const int *opt, int nopt, int y, int sel, int chosen) {
	int bottom = scr_h - UI_BAR_H - 2, i, ic = icon_col[k->a];
	for (i = 0; i < nopt; i++) {
		uint16_t bg = COL_OPT, fg = UI_TEXT;
		char num[12];
		const char *s = rows[opt[i]][k->a];
		if (chosen >= 0 && opt[i] == k->row) bg = COL_RIGHT;
		else if (chosen == i) bg = COL_WRONG;
		else if (chosen < 0 && i == sel) bg = UI_SEL, fg = UI_INK;
		sprintf(num, "%d", i + 1);
		if (image_col[k->a]) {
			int cw = scr_w / 2, ch = (bottom - y) / 2;
			int x = (i & 1) * cw, cy = y + (i >> 1) * ch, top = cy + FONT_H + 2;
			fill_rect(x + 2, cy + 1, cw - 4, ch - 2, bg);
			draw_text(x + 6, cy + 2, 1, num, bg == COL_OPT ? UI_SEL : fg);
			draw_pic(s, 1, x + cw / 2, top + (small_h[k->a] - pic[atoi(s) * 2 + 1].h) / 2, fg);
		} else {
			static int starts[4], lens[4];
			int rh = (bottom - y) / nopt, x = 28, cy, n, j;
			if (rh > FONT_H * 2 + 10) rh = FONT_H * 2 + 10;
			cy = y + i * rh;
			fill_rect(4, cy + 1, scr_w - 8, rh - 2, bg);
			draw_text_style(10, cy + (rh - FONT_H) / 2, TEXT_BOLD, num, -1, bg == COL_OPT ? UI_SEL : fg);
			if (ic >= 0) {
				const char *f = rows[opt[i]][ic];
				if (*f) draw_pic(f, 1, x + small_w[ic] / 2, cy + (rh - pic[atoi(f) * 2 + 1].h) / 2, fg);
				x += small_w[ic] + 6;
			}
			if (bg == COL_RIGHT) draw_icon(scr_w - 24, cy + (rh - 16) / 2, 1, ICON_CHECK, UI_TEXT);
			n = wrap_text(s, (scr_w - x - 26) / FONT_W, starts, lens, 4);
			if (n > 2) n = 2;
			for (j = 0; j < n; j++)
				draw_text_n(x, cy + (rh - n * FONT_H) / 2 + j * FONT_H, 1, s + starts[j],
						lens[j], fg);
		}
	}
}

static void draw_ask(const struct ask *k, const int *opt, int nopt, int sel, int chosen,
		int i, int n) {
	char title[80], count[16];
	int y;
	snprintf(title, sizeof(title), "%s → %s", cols[k->q], cols[k->a]);
	ui_header(title);
	sprintf(count, "%d/%d", i + 1, n);
	draw_text(scr_w - 4 - text_len(count) * FONT_W, 3, 1, count, UI_DIM);
	y = draw_question(k);
	draw_options(k, opt, nopt, y, sel, chosen);
	if (chosen < 0) ui_footer("1-4 ответ  П.софт - стоп");
	else ui_footer(opt[chosen] == k->row ? "Верно!  5 - дальше" : "Неверно  5 - дальше");
	gfx_present();
}

/* asks question k, number i of n: 1 right, 0 wrong, 2 the round stopped
   before the answer, -1 quit; *stop is set when it stops after the answer */
static int ask(const struct ask *k, int i, int n, int *stop) {
	struct stat *s = STAT(k->row, k->m);
	int opt[4], nopt = pick_options(k, opt), sel = 0, chosen = -1, key;
	int step = image_col[k->a] ? 2 : 1;
	while (chosen < 0) {
		draw_ask(k, opt, nopt, sel, -1, i, n);
		key = wait_key();
		if (key < 0) return -1;
		if (key == KEYPAD_RSOFT) return 2;
		if (key >= KEYPAD_1 && key < KEYPAD_1 + nopt) chosen = key - KEYPAD_1;
		else if (key == KEYPAD_CENTER || key == KEYPAD_5 || key == KEYPAD_LSOFT) chosen = sel;
		else if (key == KEYPAD_UP) sel = (sel + nopt - step) % nopt;
		else if (key == KEYPAD_DOWN) sel = (sel + step) % nopt;
		else if (key == KEYPAD_LEFT) sel = (sel + nopt - 1) % nopt;
		else if (key == KEYPAD_RIGHT) sel = (sel + 1) % nopt;
	}
	if (s->total < 65535) s->total++;
	if (opt[chosen] == k->row) {
		if (s->right < 65535) s->right++;
		if (s->streak < 255) s->streak++;
	} else {
		s->streak = 0;
	}
	draw_ask(k, opt, nopt, sel, chosen, i, n);
	key = wait_key();
	if (key < 0) return -1;
	if (key == KEYPAD_RSOFT) *stop = 1;
	return opt[chosen] == k->row;
}

/* "Париж → Франция", without the picture side */
static void item_line(char *buf, int size, const struct ask *k) {
	const char *q = image_col[k->q] ? NULL : rows[k->row][k->q];
	const char *a = image_col[k->a] ? NULL : rows[k->row][k->a];
	if (q && a) snprintf(buf, size, "%s → %s", q, a);
	else snprintf(buf, size, "%s", q ? q : a ? a : rows[k->row][0]);
}

static int repeat_key(int key, void *ctx) {
	if (key != KEYPAD_CENTER && key != KEYPAD_5) return 0;
	*(int*)ctx = 1;
	return 1;
}

/* asks the items of list (ITEM()) in topic m and shows the result, then may
   repeat the mistakes: 0 back, -1 on quit */
static int play(int m, int *list, int n) {
	static int wrong[MAX_ROWS];
	static char text[4096];
	for (;;) {
		int i, right = 0, nwrong = 0, streak = 0, round_best = 0, stop = 0, again = 0, len;
		for (i = 0; i < n && !stop; i++) {
			struct ask k = ask_of(m, list[i]);
			int a = ask(&k, i, n, &stop);
			if (a < 0) { stats_save(); return -1; }
			if (a == 2) break;
			if (a) {
				right++;
				if (++streak > round_best) round_best = streak;
				if (streak > best_streak) best_streak = streak;
			} else {
				wrong[nwrong++] = list[i];
				streak = 0;
			}
		}
		if (!i) return 0;
		rounds++;
		stats_save();
		len = snprintf(text, sizeof(text), "Верно: %d из %d (%d%%)\nСерия в раунде: %d\n"
				"Рекорд серии: %d\n", right, i, right * 100 / i, round_best, best_streak);
		if (nwrong) len += snprintf(text + len, sizeof(text) - len, "\nОшибки:\n");
		for (i = 0; i < nwrong && len < (int)sizeof(text) - 1; i++) {
			char line[160];
			struct ask k = ask_of(m, wrong[i]);
			item_line(line, sizeof(line), &k);
			len += snprintf(text + len, sizeof(text) - len, "%s\n", line);
		}
		if (text_view("Итог раунда", text, nwrong ? "5 повторить ошибки  П назад" :
				"5 или П.софт - назад", nwrong ? repeat_key : NULL, &again) < 0)
			return -1;
		if (!again) return 0;
		memcpy(list, wrong, nwrong * sizeof(*list));
		n = nwrong;
	}
}

/* ------------------------------------------------------------------ menus */

#define GROUP_ALL  -1
#define GROUP_HARD -2

static int in_group(int r, int g, int m) {
	return g == GROUP_ALL || (g == GROUP_HARD ? hard(r, m) : row_group[r] == g);
}

/* the items of group g in topic m into list (ITEM()), shuffled, in the
   chosen direction, the unlearned first, a question once: the count, at most
   round_len */
static int make_round(int m, int g, int *list) {
	static int all[MAX_ROWS];
	int n = 0, i, j, k = 0, pass;
	for (i = 0; i < nrows; i++)
		if (askable(i, m) && in_group(i, g, m)) all[n++] = i;
	for (i = n - 1; i > 0; i--) {
		int t = all[i];
		j = rng(i + 1);
		all[i] = all[j];
		all[j] = t;
	}
	for (i = j = 0; i < n; i++) {
		int item = ITEM(all[i], direction == 2 ? (int)rng(2) : direction), x;
		struct ask a = ask_of(m, item);
		for (x = 0; x < j; x++) {
			struct ask b = ask_of(m, all[x]);
			if (b.q == a.q && !strcmp(rows[b.row][b.q], rows[a.row][a.q])) break;
		}
		if (x == j) all[j++] = item;
	}
	for (pass = 0; pass < 2; pass++)
		for (i = 0; i < j; i++)
			if (mastered(all[i] / 2, m) == pass) list[k++] = all[i];
	return round_len && k > round_len ? round_len : k;
}

/* learned and all items of group g in topic m */
static void count_group(int m, int g, int *learned, int *total) {
	int r;
	*learned = *total = 0;
	for (r = 0; r < nrows; r++)
		if (askable(r, m) && in_group(r, g, m)) {
			++*total;
			*learned += mastered(r, m);
		}
}

/* a part of the set for topic m, then rounds: 0 back, -1 on quit */
static int pick_group(int m) {
	static char lines[MAX_GROUPS + 2][64];
	static int list[MAX_ROWS];
	const char *items[MAX_GROUPS + 2];
	int gs[MAX_GROUPS + 2], sel = 0;
	for (;;) {
		int n = 0, i, learned, total;
		count_group(m, GROUP_ALL, &learned, &total);
		snprintf(lines[n], sizeof(lines[n]), "Все  %d/%d", learned, total);
		gs[n++] = GROUP_ALL;
		for (i = 0; i < ngroups; i++) {
			count_group(m, i, &learned, &total);
			if (!total) continue;
			snprintf(lines[n], sizeof(lines[n]), "%s  %d/%d", groups[i], learned, total);
			gs[n++] = i;
		}
		count_group(m, GROUP_HARD, &learned, &total);
		if (total) {
			snprintf(lines[n], sizeof(lines[n]), "Трудные  %d", total);
			gs[n++] = GROUP_HARD;
		}
		for (i = 0; i < n; i++) items[i] = lines[i];
		if (n == 1) sel = 0;
		else if ((sel = ui_pick(modes[m].name, items, n, sel < n ? sel : 0)) == -2) return -1;
		else if (sel < 0) return 0;
		rng_state = app_today * 7919 + rounds * 31 + m;
		i = make_round(m, gs[sel], list);
		if (i && play(m, list, i) < 0) return -1;
		if (n == 1) return 0;
	}
}

static int stats_view(const char *title) {
	static char text[8192];
	int len = 0, m, g, r, i, learned, total;
	static int top[MAX_HARD];
	int ntop = 0;
	len += snprintf(text + len, sizeof(text) - len, "Раундов сыграно: %d\nРекорд серии: %d\n",
			rounds, best_streak);
	for (m = 0; m < nmodes; m++) {
		int right = 0, answers = 0;
		count_group(m, GROUP_ALL, &learned, &total);
		for (r = 0; r < nrows; r++) right += STAT(r, m)->right, answers += STAT(r, m)->total;
		if (!answers) {
			len += snprintf(text + len, sizeof(text) - len, "\n%s\n  ещё не играли\n", modes[m].name);
			continue;
		}
		len += snprintf(text + len, sizeof(text) - len,
				"\n%s\n  выучено %d из %d (%d%%)\n  ответов %d, верных %d%%\n",
				modes[m].name, learned, total, total ? learned * 100 / total : 0,
				answers, answers ? right * 100 / answers : 0);
		for (g = 0; g < ngroups && len < (int)sizeof(text) - 1; g++) {
			count_group(m, g, &learned, &total);
			if (total) len += snprintf(text + len, sizeof(text) - len, "  %s %d/%d\n",
					groups[g], learned, total);
		}
	}
	/* the most mistakes in all topics */
	for (r = 0; r < nrows; r++) {
		int w = 0, k;
		for (m = 0; m < nmodes; m++) w += STAT(r, m)->total - STAT(r, m)->right;
		if (!w) continue;
		for (k = ntop; k > 0; k--) {
			int wk = 0;
			for (m = 0; m < nmodes; m++) wk += STAT(top[k - 1], m)->total - STAT(top[k - 1], m)->right;
			if (wk >= w) break;
			if (k < MAX_HARD) top[k] = top[k - 1];
		}
		if (k < MAX_HARD) {
			top[k] = r;
			if (ntop < MAX_HARD) ntop++;
		}
	}
	if (ntop) len += snprintf(text + len, sizeof(text) - len, "\nБольше всего ошибок:\n");
	for (i = 0; i < ntop && len < (int)sizeof(text) - 1; i++) {
		int w = 0;
		for (m = 0; m < nmodes; m++) w += STAT(top[i], m)->total - STAT(top[i], m)->right;
		len += snprintf(text + len, sizeof(text) - len, "  %s: %d\n", rows[top[i]][0], w);
	}
	return ui_message(title, text);
}

/* the topics of a set, the direction, the round length, the statistics:
   0 back, -1 on quit */
static int set_menu(const char *title) {
	static char lines[MAX_MODES + 3][80];
	const char *items[MAX_MODES + 3];
	int sel = 0;
	for (;;) {
		int m, learned, total;
		for (m = 0; m < nmodes; m++) {
			count_group(m, GROUP_ALL, &learned, &total);
			snprintf(lines[m], sizeof(lines[m]), "%3d%% %s", total ? learned * 100 / total : 0,
					modes[m].name);
			items[m] = lines[m];
		}
		snprintf(lines[m], sizeof(lines[m]), "Направление: %s", dir_names[direction]);
		items[m] = lines[m];
		if (round_len) snprintf(lines[m + 1], sizeof(lines[m + 1]), "Вопросов в раунде: %d", round_len);
		else snprintf(lines[m + 1], sizeof(lines[m + 1]), "Вопросов в раунде: все");
		items[m + 1] = lines[m + 1];
		items[m + 2] = "Статистика";
		sel = ui_pick(title, items, nmodes + 3, sel);
		if (sel == -2) return -1;
		if (sel < 0) return 0;
		if (sel < nmodes) {
			if (pick_group(sel) < 0) return -1;
		} else if (sel == nmodes) {
			direction = (direction + 1) % 3;
			settings_save();
		} else if (sel == nmodes + 1) {
			int i, n = sizeof(round_lens) / sizeof(*round_lens);
			for (i = 0; i < n - 1 && round_lens[i] != round_len; i++);
			round_len = round_lens[(i + 1) % n];
			settings_save();
		} else if (stats_view(title) < 0) {
			return -1;
		}
	}
}

int quiz_main(void) {
	static char *paths[MAX_SETS];
	const char *titles[MAX_SETS];
	char *buf = file_load(INDEX_FILE, NULL), *pos, *line;
	int n = 0, sel = 0, ret = 0;
	if (!buf) return ui_message("Викторины", "Нет наборов: quiz/index.txt не найден") < 0 ? -1 : 0;
	for (pos = buf; n < MAX_SETS && (line = next_line(&pos)); ) {
		char *p = line;
		if (!*line || *line == '#') continue;
		paths[n] = next_field(&p);
		titles[n] = *p ? p : paths[n];
		n++;
	}
	settings_load();
	while (n) {
		const char *err;
		sel = ui_pick("Викторины", titles, n, sel);
		if (sel == -2) { ret = -1; break; }
		if (sel < 0) break;
		err = set_open(paths[sel]);
		if (err) ret = ui_message(titles[sel], err);
		else ret = set_menu(titles[sel]);
		set_close();
		if (ret < 0) break;
	}
	free(buf);
	return ret;
}
