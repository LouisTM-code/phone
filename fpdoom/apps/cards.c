#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "apps.h"
#include "cards.h"
#include "dictfile.h"
#include "log.h"

#define CARDS_FILE "cards.txt"
#define MAX_CARDS 4000
/* the settings' defaults: new words a day, and how many more "Ещё" adds */
#define NEW_PER_DAY 20
#define MORE_NEW 10
/* "Уже знаю": the longest interval, the card doesn't come back */
#define KNOWN_IVL 3650
/* a word is learned from this interval on: a deck's progress counts such words */
#define LEARNED_IVL 21
/* a save takes ~0.3 s: write the file every few answers and when the session stops */
#define SAVE_EVERY 10

/* the decks: "my" (the words added from the dictionary and books) and the
   files of decks/index.txt (tools/mkdecks.py); a card has a bit per deck it is in */
#define DECK_INDEX "decks/index.txt"
#define MAX_DECKS 32
#define DECK_MY 0
typedef struct {
	char id[24], file[40], name[48];
	int words;  /* in its file */
	int own;    /* meanings of its own (a topic), not the dictionary's */
	int on;     /* new words come from it */
} deck_t;
static deck_t decks[MAX_DECKS];
static int ndecks;

/* a card's flags: a deck brought a new meaning of a learned word, shown once;
   a new word to learn first ("Учить сейчас" of its deck's list) */
#define F_MEANING 1
#define F_NOW 2

typedef struct {
	char *front, *back;
	int due, ivl, ease, reps, lapses;
	char *ex;  /* an example sentence "English. - Русский.", may be "" */
	uint32_t decks;
	int flags;
} card_t;

static card_t *cards;
static int ncards, loaded, dirty;
/* the header line of cards.txt: settings, today's counts (they start again on
   a new day: day=), the streak of days with answers */
static int stat_day, new_today, extra_today, ans_today, ok_today, warm_today;
static int set_new = NEW_PER_DAY, set_more = MORE_NEW, streak, best_streak, last_day;
static const struct { const char *name; int *val; } head[] = {
	{ " day=", &stat_day }, { " new=", &new_today }, { " extra=", &extra_today },
	{ " ans=", &ans_today }, { " ok=", &ok_today }, { " warm=", &warm_today },
	{ " newmax=", &set_new }, { " more=", &set_more },
	{ " streak=", &streak }, { " best=", &best_streak }, { " last=", &last_day },
};
#define HEAD_N (int)(sizeof(head) / sizeof(*head))

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

static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int same_word(const char *a, const char *b) {
	for (; *a && *b; a++, b++)
		if (lower(*a) != lower(*b)) return 0;
	return *a == *b;
}

/* the cards by word: indexes + 1 in an open-addressing table, 0 is empty */
#define HASH_N 8192
static short hash_tab[HASH_N];

static unsigned word_hash(const char *s) {
	unsigned h = 2166136261u;
	for (; *s; s++) h = (h ^ (unsigned char)lower(*s)) * 16777619u;
	return h & (HASH_N - 1);
}

static void hash_add(int i) {
	unsigned h = word_hash(cards[i].front);
	while (hash_tab[h]) h = (h + 1) & (HASH_N - 1);
	hash_tab[h] = i + 1;
}

/* the card of a word, -1 if none */
static int find_card(const char *word) {
	unsigned h = word_hash(word);
	for (; hash_tab[h]; h = (h + 1) & (HASH_N - 1))
		if (same_word(cards[hash_tab[h] - 1].front, word)) return hash_tab[h] - 1;
	return -1;
}

static int deck_by_id(const char *id, int len) {
	int d;
	for (d = 0; d < ndecks; d++)
		if ((int)strlen(decks[d].id) == len && !strncmp(decks[d].id, id, len)) return d;
	return -1;
}

/* "my" and the decks of decks/index.txt: "decks/A1.txt<TAB>name<TAB>group<TAB>words<TAB>kind" */
static void decks_load(void) {
	char *buf = file_load(DECK_INDEX, NULL), *pos, *line;
	strcpy(decks[0].id, "my");
	strcpy(decks[0].name, "Мои слова");
	decks[0].on = 1;
	ndecks = 1;
	if (!buf) return;
	for (pos = buf; ndecks < MAX_DECKS && (line = next_line(&pos)); ) {
		char *p = line, *file = next_field(&p), *name = next_field(&p), *s, *e;
		deck_t *d = &decks[ndecks];
		next_field(&p);  /* the group: the index lists its decks by groups */
		d->words = atoi(next_field(&p));
		d->own = !strcmp(next_field(&p), "own");
		if (!name[0]) continue;
		/* the id is the file's name: "decks/A1.txt" */
		for (s = e = file; *e; e++) if (*e == '/') s = e + 1;
		if (e - s > 4 && !strcmp(e - 4, ".txt")) e -= 4;
		snprintf(d->id, sizeof(d->id), "%.*s", (int)(e - s), s);
		snprintf(d->file, sizeof(d->file), "%s", file);
		snprintf(d->name, sizeof(d->name), "%s", name);
		utf8_trim(d->name);
		ndecks++;
	}
	free(buf);
}

/* "A1,phrasal" -> the deck bits */
static uint32_t deck_bits(const char *s) {
	uint32_t bits = 0;
	while (*s) {
		const char *e = s;
		int d;
		while (*e && *e != ',' && *e != ' ') e++;
		if ((d = deck_by_id(s, e - s)) >= 0) bits |= 1u << d;
		s = *e ? e + 1 : e;
	}
	return bits;
}

/* the deck bits as "A1,phrasal" (sep ',') or "A1 phrasal" (sep ' ') at p, returns the end */
static char *put_decks(char *p, uint32_t bits, char sep) {
	int d, first = 1;
	for (d = 0; d < ndecks; d++)
		if (bits >> d & 1) {
			if (!first) *p++ = sep;
			p += sprintf(p, "%s", decks[d].id);
			first = 0;
		}
	*p = 0;
	return p;
}

int cards_load(void) {
	char *buf, *pos, *line;
	LOG_T0(t0);
	if (loaded) return 0;
	cards = malloc(MAX_CARDS * sizeof(*cards));
	if (!cards) return -1;
	loaded = 1;
	decks_load();
	buf = file_load(CARDS_FILE, NULL);
	if (!buf) return 0;
	for (pos = buf; (line = next_line(&pos)); ) {
		char *f[11];
		card_t *c = &cards[ncards];
		int n;
		if (!strncmp(line, "#decks", 6)) {
			uint32_t on = deck_bits(line + 6 + (line[6] == ' '));
			int d;
			for (d = 0; d < ndecks; d++) decks[d].on = on >> d & 1;
			continue;
		}
		if (line[0] == '#') {
			int k;
			for (k = 0; k < HEAD_N; k++) {
				const char *p = str_find(line, head[k].name);
				if (p) *head[k].val = atoi(p + strlen(head[k].name));
			}
			continue;
		}
		if (!line[0] || ncards >= MAX_CARDS) continue;
		n = split_tabs(line, f, 10);
		if (n < 2) continue;
		c->front = dup_str(f[0], strlen(f[0]));
		c->back = dup_str(f[1], strlen(f[1]));
		c->due = n > 2 ? atoi(f[2]) : 0;
		c->ivl = n > 3 ? atoi(f[3]) : 0;
		c->ease = n > 4 ? atoi(f[4]) : 2500;
		c->reps = n > 5 ? atoi(f[5]) : 0;
		c->lapses = n > 6 ? atoi(f[6]) : 0;
		c->ex = n > 7 && f[7][0] ? dup_str(f[7], strlen(f[7])) : "";
		c->decks = n > 8 ? deck_bits(f[8]) : 0;
		c->flags = n > 9 ? (str_find(f[9], "m") ? F_MEANING : 0) | (str_find(f[9], "n") ? F_NOW : 0) : 0;
		if (c->ease < 1300) c->ease = 2500;
		/* a card of no deck there is the user's own */
		if (!c->decks) c->decks = 1u << DECK_MY;
		if (!c->front || !c->back || !c->ex) continue;
		hash_add(ncards);
		ncards++;
	}
	free(buf);
	LOG_OP(t0, "cards_load %d", ncards);
	return 0;
}

int cards_save(void) {
	FILE *fp;
	char *buf, *p;
	int i, ids = 0;
	size_t size = 64;
	LOG_T0(t0);
	if (!dirty) return 0;
	/* one big write: every fprintf would be a separate USB request */
	for (i = 0; i < ndecks; i++) ids += strlen(decks[i].id) + 1;
	for (i = 0; i < ncards; i++)
		size += strlen(cards[i].front) + strlen(cards[i].back) + strlen(cards[i].ex) + 64 + ids;
	if (!(buf = malloc(size + HEAD_N * 24 + ids + 16))) return -1;
	p = buf + sprintf(buf, "#cards v2");
	for (i = 0; i < HEAD_N; i++) p += sprintf(p, "%s%d", head[i].name, *head[i].val);
	p += sprintf(p, "\n#decks ");
	for (i = 0; i < ndecks; i++)
		if (decks[i].on) p += sprintf(p, "%s ", decks[i].id);
	p[-1] = '\n';
	for (i = 0; i < ncards; i++) {
		card_t *c = &cards[i];
		p += sprintf(p, "%s\t%s\t%d\t%d\t%d\t%d\t%d\t%s\t", c->front, c->back,
				c->due, c->ivl, c->ease, c->reps, c->lapses, c->ex);
		p = put_decks(p, c->decks, ',');
		if (c->flags) p += sprintf(p, "\t%s%s", c->flags & F_MEANING ? "m" : "", c->flags & F_NOW ? "n" : "");
		*p++ = '\n';
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

/* a card for a new word of deck d, its index; for a word already there the deck
   is added and an example fills its empty one (an older set had none); -1 when full */
static int add_card(const char *front, const char *back, const char *ex, int d) {
	int i = find_card(front);
	card_t *c;
	if (i >= 0) {
		c = &cards[i];
		if (ex && ex[0] && !c->ex[0] && (c->ex = clean_field(ex, 300))) dirty = 1;
		if (!c->ex) c->ex = "";
		if (!(c->decks >> d & 1)) c->decks |= 1u << d, dirty = 1;
		return i;
	}
	if (ncards >= MAX_CARDS) return -1;
	c = &cards[ncards];
	c->front = clean_field(front, 100);
	c->back = clean_field(back, 600);
	c->ex = ex && ex[0] ? clean_field(ex, 300) : "";
	if (!c->front || !c->back || !c->ex) return -1;
	c->due = c->ivl = c->reps = c->lapses = c->flags = 0;
	c->ease = 2500;
	c->decks = 1u << d;
	hash_add(ncards);
	dirty = 1;
	return ncards++;
}

int cards_has(const char *front) {
	if (cards_load() < 0) return 0;
	return find_card(front) >= 0;
}

int cards_put(const char *front, const char *back, const char *ex) {
	int had;
	if (cards_load() < 0) return -1;
	had = find_card(front) >= 0;
	if (add_card(front, back, ex, DECK_MY) < 0) return -1;
	return !had;
}

int cards_add(const char *front, const char *back, const char *ex) {
	int r = cards_put(front, back, ex);
	if (r > 0 && cards_save() < 0) return -1;
	return r;
}

static int is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int prefix_eq(const char *s, const char *w, int n) {
	while (n--) if (lower(*s++) != lower(*w++)) return 0;
	return 1;
}

/* 1 if the token s..t is a form of the word of wl letters ("runs", "making" for
   run, make; "went" for go through the dictionary when it is open) */
static int is_form(const char *word, int wl, const char *s, const char *t) {
	static const char *const ends[] = { "", "s", "es", "e", "ed", "d", "ing", "er", "ers",
		"est", "ies", "ied", "y", "ying", "ly", NULL };
	int stem = wl, i;
	const char *rest;
	char tok[32], base[32];
	if (wl > 3 && (word[wl - 1] == 'e' || word[wl - 1] == 'y')) stem--;
	if (t - s >= stem && prefix_eq(s, word, stem)) {
		rest = s + stem;
		/* running, stopped */
		if (rest < t && stem > 2 && lower(*rest) == lower(word[stem - 1]) && stem == wl) rest++;
		for (i = 0; ends[i]; i++)
			if ((int)strlen(ends[i]) == t - rest && prefix_eq(rest, ends[i], t - rest)) return 1;
	}
	if (t - s >= (int)sizeof(tok)) return 0;
	memcpy(tok, s, t - s);
	tok[t - s] = 0;
	return dict_lookup(tok, base, sizeof(base)) >= 0 && (int)strlen(base) == wl &&
			prefix_eq(base, word, wl);
}

/* the next token (letters) from s: its start, *t its end; NULL at the end */
static const char *next_token(const char *s, const char **t) {
	while (*s && !is_letter(*s)) s++;
	if (!*s) return NULL;
	for (*t = s; is_letter(**t); (*t)++);
	return s;
}

/* at most so many words between the words of a phrase: "gave it up" (tools/mkdecks.py) */
#define PHRASE_GAP 3

int cards_cloze(const char *word, const char *text, int *len) {
	const char *s, *t, *w;
	int fl;
	for (fl = 0; word[fl] && word[fl] != ' '; fl++);
	for (s = next_token(text, &t); s; s = next_token(t, &t)) {
		const char *end = t;
		if (!is_form(word, fl, s, t)) continue;
		/* a phrase: its other words follow in order, a few words apart at most */
		for (w = word + fl; *w == ' '; ) {
			const char *q, *qt = end;
			int wl, gap = 0;
			for (w++, wl = 0; w[wl] && w[wl] != ' '; wl++);
			for (q = next_token(end, &qt); q && gap <= PHRASE_GAP; q = next_token(qt, &qt), gap++)
				if (qt - q == wl && prefix_eq(q, w, wl)) break;
			if (!q || gap > PHRASE_GAP) break;
			end = qt;
			w += wl;
		}
		if (*w) continue;
		*len = end - s;
		return s - text;
	}
	return -1;
}

/* ---- review session ---- */

enum { ANS_AGAIN = 1, ANS_HARD, ANS_GOOD, ANS_EASY, ANS_UNDO, ANS_KNOWN };

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
		new_today = extra_today = ans_today = ok_today = warm_today = 0;
		dirty = 1;
	}
}

/* new words still allowed today */
static int new_left(void) {
	int n = set_new + extra_today - new_today;
	return n > 0 ? n : 0;
}

/* an answer today: the streak goes on from yesterday or starts again */
static void studied(void) {
	if (last_day != app_today) {
		streak = last_day == app_today - 1 ? streak + 1 : 1;
		last_day = app_today;
		if (streak > best_streak) best_streak = streak;
		dirty = 1;
	}
}

/* what a word is to the learner */
enum { ST_NEW, ST_LEARN, ST_LEARNED, ST_KNOWN, ST_ALL };
static const char *const st_names[] = { "новое", "учу", "выучено", "знаю", "все" };

static int card_status(const card_t *c) {
	if (!c->reps) return ST_NEW;
	if (c->ivl >= KNOWN_IVL) return ST_KNOWN;
	return c->ivl >= LEARNED_IVL ? ST_LEARNED : ST_LEARN;
}

static uint32_t on_bits(void) {
	uint32_t bits = 0;
	int d;
	for (d = 0; d < ndecks; d++) if (decks[d].on) bits |= 1u << d;
	return bits;
}

static void reset_card(card_t *c) {
	c->reps = c->ivl = c->due = c->lapses = c->flags = 0;
	c->ease = 2500;
	dirty = 1;
}

static void first_meaning(const char *back, char *buf, int size, int max);

/* a deck of its own brings a meaning the card hasn't got: it is added, and a
   learned card comes once today to show it */
static void add_meaning(card_t *c, const char *back, int d) {
	char m[64], *s, *t;
	first_meaning(back, m, sizeof(m), 40);
	if (!m[0] || str_find(c->back, m)) return;
	if (!(s = malloc(strlen(c->back) + strlen(back) + strlen(decks[d].name) + 16))) return;
	sprintf(s, "%s / + %s (%s)", c->back, back, decks[d].name);
	if ((t = clean_field(s, 600))) {
		free(c->back);
		c->back = t;
	}
	free(s);
	if (card_status(c) >= ST_LEARNED) {
		c->flags |= F_MEANING;
		c->due = app_today;
	}
	dirty = 1;
}

/* a word of a deck file: "word<TAB>meaning[<TAB>example]" */
typedef struct { const char *front, *back, *ex; } entry_t;

/* deck d's words into *ents (free() it and *buf), their count, -1 if missing.
   The cards of the words get the deck (a word learned elsewhere counts in its
   progress), a deck of its own adds its meanings to them */
static int deck_read(int d, char **buf, entry_t **ents) {
	char *pos, *line;
	int n = 1, i;
	LOG_T0(t0);
	*ents = NULL;
	if (!(*buf = file_load(decks[d].file, NULL))) return -1;
	for (pos = *buf; *pos; pos++) if (*pos == '\n') n++;
	if (!(*ents = malloc(n * sizeof(**ents)))) { free(*buf); *buf = NULL; return -1; }
	for (n = 0, pos = *buf; (line = next_line(&pos)); ) {
		entry_t *e = &(*ents)[n];
		char *p = line;
		e->front = next_field(&p);
		e->back = next_field(&p);
		e->ex = next_field(&p);
		if (!e->back[0]) continue;
		if ((i = find_card(e->front)) >= 0 && !(cards[i].decks >> d & 1)) {
			cards[i].decks |= 1u << d;
			if (decks[d].own) add_meaning(&cards[i], e->back, d);
			dirty = 1;
		}
		n++;
	}
	LOG_OP(t0, "deck_read %s %d", decks[d].id, n);
	return n;
}

/* the cards in deck d, how many of them are learned (or known) */
static int deck_learned(int d, int *in_box) {
	int i, n = 0;
	*in_box = 0;
	for (i = 0; i < ncards; i++)
		if (cards[i].decks >> d & 1) {
			(*in_box)++;
			if (card_status(&cards[i]) >= ST_LEARNED) n++;
		}
	return n;
}

/* the words of deck d: its file's, the cards' for "my" */
static int deck_size(int d, int in_box) {
	return d == DECK_MY || in_box > decks[d].words ? in_box : decks[d].words;
}

/* new words the decks that are on can give: the new cards of the box, and the
   words of their files not in the box yet */
static int new_avail(void) {
	static int box[MAX_DECKS];
	uint32_t on = on_bits();
	int i, d, n = 0;
	memset(box, 0, sizeof(box));
	for (i = 0; i < ncards; i++) {
		card_t *c = &cards[i];
		if (!c->reps && (c->decks & on || c->flags & F_NOW)) n++;
		for (d = 1; d < ndecks; d++) if (c->decks >> d & 1) box[d]++;
	}
	for (d = 1; d < ndecks; d++)
		if (decks[d].on && decks[d].words > box[d]) n += decks[d].words - box[d];
	return n;
}

static int count_due(int *nreview, int *nnew) {
	int i;
	*nreview = 0;
	for (i = 0; i < ncards; i++)
		if (cards[i].reps && cards[i].due <= app_today) (*nreview)++;
	*nnew = new_avail();
	return *nreview + (*nnew < new_left() ? *nnew : new_left());
}

int cards_today(int *nreview, int *nnew) {
	if (cards_load() < 0) return -1;
	new_day();
	count_due(nreview, nnew);
	if (*nnew > new_left()) *nnew = new_left();
	return 0;
}

/* today's new words into queue: the new cards of the box (the ones to learn now
   first, then the ones of the decks that are on), then by turns the words of
   the decks' files, made cards; their count */
static int take_new(int *queue, int want) {
	static char *bufs[MAX_DECKS];
	static entry_t *ents[MAX_DECKS];
	static int cnt[MAX_DECKS], at[MAX_DECKS];
	uint32_t on = on_bits();
	int i, d, got = 0, more, pass;
	for (pass = 0; pass < 2; pass++)
		for (i = 0; i < ncards && got < want; i++)
			if (!cards[i].reps && (pass ? !(cards[i].flags & F_NOW) && cards[i].decks & on :
					cards[i].flags & F_NOW)) queue[got++] = i;
	if (got == want) return got;
	ui_header("Карточки");
	draw_text_center(scr_h / 2 - FONT_H / 2, 1, "Выбираю новые слова...", UI_DIM);
	gfx_present();
	for (d = 1; d < ndecks; d++) {
		bufs[d] = NULL;
		ents[d] = NULL;
		cnt[d] = at[d] = 0;
		if (decks[d].on && (cnt[d] = deck_read(d, &bufs[d], &ents[d])) < 0) cnt[d] = 0;
	}
	do {
		more = 0;
		for (d = 1; d < ndecks && got < want; d++) {
			entry_t *e;
			while (at[d] < cnt[d] && find_card(ents[d][at[d]].front) >= 0) at[d]++;
			if (at[d] == cnt[d]) continue;
			e = &ents[d][at[d]++];
			if ((i = add_card(e->front, e->back, e->ex, d)) < 0) break;
			queue[got++] = i;
			more = 1;
		}
	} while (more && got < want);
	for (d = 1; d < ndecks; d++) {
		free(bufs[d]);
		free(ents[d]);
	}
	return got;
}

/* how a session asks: the word, or the word typed for its meaning; both grade the card */
enum { MODE_FLIP, MODE_TYPE };

static const uint16_t rate_colors[4] = {
	RGB(240, 120, 110), RGB(240, 170, 80), UI_OK, RGB(120, 180, 250) };

/* the card under the header, down to above the buttons */
static void draw_panel(void) {
	fill_rect(4, UI_BAR_H + 4, scr_w - 8, UI_BTN_TOP - FONT_H - 12 - UI_BAR_H, UI_CARD);
}

/* draws text wrapped at y in at most max lines, returns the y below it */
static int draw_lines(int y, const char *text, int max, uint16_t col) {
	static int starts[24], lens[24];
	int n = wrap_text(text, (scr_w - 16) / FONT_W, starts, lens, 24), i;
	for (i = 0; i < n && i < max; i++, y += FONT_H + 2)
		draw_text_n(8, y, 1, text + starts[i], lens[i], col);
	return y;
}

/* shows one card, returns the answer 1..4, ANS_UNDO, 0 to stop, -1 on quit */
static int review_card(card_t *c, int left, int can_undo, int mode) {
	static char typed[64];
	const char *verdict = NULL;
	int shown = 0, def = ANS_GOOD, ok;
	char title[64], ipa[96];
	sprintf(title, "Карточки  осталось %d", left);
	dict_ipa(c->front, ipa, sizeof(ipa));
	if (mode == MODE_TYPE) {
		int r, n;
		typed[0] = 0;
		r = ui_input(title, c->back, typed, sizeof(typed), 0);
		if (r == -2) return -1;
		if (r < 0) return 0;
		for (n = strlen(typed); n && typed[n - 1] == ' '; ) typed[--n] = 0;
		ok = same_word(typed, c->front);
		verdict = ok ? "Верно!" : "Неверно:";
		if (!ok) def = ANS_AGAIN;
		shown = 1;
		can_undo = 0;  /* 0 is a space while typing */
	}
	for (;;) {
		char buf[96], a[24], b[24], d[24], e[24];
		int y, key, s;
		ui_header(title);
		draw_panel();
		s = text_len(c->front) * FONT_W * 2 <= (int)scr_w - 16 ? 2 : 1;
		y = draw_text_wrap(8, UI_BAR_H + 12, scr_w - 16, s, c->front, UI_TEXT);
		if (ipa[0]) {
			draw_text(8, y, 1, ipa, UI_IPA);
			y += FONT_H + 2;
		}
		if (!c->reps) draw_text(8, y, 1, "новая", UI_SEL);
		else if (c->flags & F_MEANING) draw_text(8, y, 1, "новое значение", UI_ACCENT);
		y += FONT_H + 6;
		fill_rect(8, y, scr_w - 16, 1, UI_LINE);
		y += 8;
		if (shown) {
			int max_lines = (UI_BTN_TOP - FONT_H - 14 - y) / (FONT_H + 2);
			if (verdict) {
				draw_text(8, y, 1, verdict, def == ANS_GOOD ? UI_OK : rate_colors[0]);
				if (def != ANS_GOOD) draw_text(8 + 9 * FONT_W, y, 1, typed, UI_DIM);
				y += FONT_H + 6;
				max_lines--;
			}
			y = draw_lines(y, c->back, max_lines, UI_TEXT);
			max_lines = (UI_BTN_TOP - FONT_H - 18 - y) / (FONT_H + 2);
			if (c->ex[0] && max_lines > 0) draw_lines(y + 4, c->ex, max_lines, UI_DIM);
			if (c->flags & F_MEANING) {
				/* any answer keeps the interval */
				ivl_text(c->ivl, a);
				sprintf(buf, "Интервал останется: %s", a);
				draw_text(8, UI_BTN_TOP, 1, buf, UI_DIM);
				ui_footer("5 - дальше  П.софт - стоп");
				gfx_present();
				key = wait_key();
				if (key < 0) return -1;
				if (key == KEYPAD_RSOFT) return 0;
				if (key == KEYPAD_0 && can_undo) return ANS_UNDO;
				if (key == KEYPAD_CENTER || key == KEYPAD_5) return ANS_GOOD;
				continue;
			}
			ivl_text(next_ivl(c, 1, NULL), a);
			ivl_text(next_ivl(c, 2, NULL), b);
			ivl_text(next_ivl(c, 3, NULL), d);
			ivl_text(next_ivl(c, 4, NULL), e);
			draw_text(8, UI_BTN_TOP - FONT_H - 6, 1, "* Уже знаю", UI_DIM);
			sprintf(buf, "1 Снова %s", a); ui_button(0, buf, rate_colors[0], def == ANS_AGAIN);
			sprintf(buf, "2 Трудно %s", b); ui_button(1, buf, rate_colors[1], 0);
			sprintf(buf, "3 Хорошо %s", d); ui_button(2, buf, rate_colors[2], def == ANS_GOOD);
			sprintf(buf, "4 Легко %s", e); ui_button(3, buf, rate_colors[3], 0);
			if (def == ANS_AGAIN) ui_footer("5 = Снова  П.софт - стоп");
			else ui_footer(can_undo ? "5=Хорошо 0-отмена П-стоп" : "5 = Хорошо  П.софт - стоп");
		} else {
			if (mode == MODE_FLIP) draw_text_center(y + 16, 1, "Вспомните перевод", UI_DIM);
			ui_footer(can_undo ? "5-ответ  0-отменить" : "5 - показать ответ");
		}
		gfx_present();
		key = wait_key();
		if (key < 0) return -1;
		if (key == KEYPAD_RSOFT) return 0;
		if (key == KEYPAD_0 && can_undo) return ANS_UNDO;
		if (!shown) {
			if (key == KEYPAD_CENTER || key == KEYPAD_5) shown = 1;
			continue;
		}
		if (key >= KEYPAD_1 && key <= KEYPAD_4) return key - KEYPAD_0;
		if (key == KEYPAD_STAR) return ANS_KNOWN;
		if (key == KEYPAD_CENTER || key == KEYPAD_5) return def;
	}
}

static int run_session(int mode) {
	static int queue[MAX_CARDS + 64];
	int n = 0, i, qpos = 0, answers = 0;
	/* the last answer, to undo it: the card before it and where it was put again */
	card_t prev;
	int prev_q = -1, prev_new = 0, prev_at = -1, prev_ans = 0, prev_ok = 0;

	new_day();
	for (i = 0; i < ncards; i++)
		if (cards[i].reps && cards[i].due <= app_today) queue[n++] = i;
	n += take_new(queue + n, new_left());
	if (!n) return ui_message("Карточки", "На сегодня всё повторено! Приходите завтра.\n\n"
			"Новые слова берутся из колод, которые включены в пункте \"Колоды\", "
			"и из словаря и книг кнопкой 1.");

	while (qpos < n) {
		card_t *c = &cards[queue[qpos]];
		int ans = review_card(c, n - qpos, prev_q >= 0, mode), e, ivl;
		if (ans <= 0) { cards_save(); return ans; }
		if (ans == ANS_UNDO) {
			qpos = prev_q;
			cards[queue[qpos]] = prev;
			new_today = prev_new;
			ans_today = prev_ans;
			ok_today = prev_ok;
			if (prev_at >= 0) {
				memmove(queue + prev_at, queue + prev_at + 1, (n - prev_at - 1) * sizeof(*queue));
				n--;
			}
			prev_q = -1;
			dirty = 1;
			continue;
		}
		prev = *c;
		prev_q = qpos;
		prev_new = new_today;
		prev_ans = ans_today;
		prev_ok = ok_today;
		prev_at = -1;
		studied();
		ans_today++;
		if (ans != ANS_AGAIN) ok_today++;
		if (!c->reps) new_today++;
		c->flags &= ~F_NOW;
		if (c->flags & F_MEANING) {
			/* the new meaning is seen: the card goes on as it was */
			c->flags &= ~F_MEANING;
			c->due = app_today + (c->ivl ? c->ivl : 1);
			c->reps++;
			dirty = 1;
			qpos++;
			if (++answers % SAVE_EVERY == 0) cards_save();
			continue;
		}
		if (ans == ANS_KNOWN) {
			c->ivl = KNOWN_IVL;
			c->due = app_today + KNOWN_IVL;
			c->reps++;
			dirty = 1;
			qpos++;
			if (++answers % SAVE_EVERY == 0) cards_save();
			continue;
		}
		ivl = next_ivl(c, ans, &e);
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
			prev_at = at;
		}
		qpos++;
		if (++answers % SAVE_EVERY == 0) cards_save();
	}
	cards_save();
	return ui_message("Карточки", "Отлично! Сессия закончена.");
}

/* ---- warm-ups on learned words: they don't change the schedule ---- */

static uint32_t rng_state;

static unsigned rng(unsigned n) {
	rng_state = rng_state * 1103515245 + 12345;
	return (rng_state >> 16) % n;
}

/* the learned cards into list, the count; the random order differs each day and run */
static int learned(int *list) {
	static int runs;
	int i, n = 0;
	rng_state = app_today * 7919 + ++runs;
	for (i = 0; i < ncards; i++)
		if (cards[i].reps) list[n++] = i;
	for (i = n - 1; i > 0; i--) {
		int j = rng(i + 1), t = list[i];
		list[i] = list[j];
		list[j] = t;
	}
	return n;
}

/* the first meaning of a card, "бутылка" of "n. бутылка; флакон / v. ...",
   at most max letters */
static void first_meaning(const char *back, char *buf, int size, int max) {
	const char *s = back, *t, *e;
	int n = 0;
	if (!strncmp(s, "modal ", 6)) s += 6;
	for (;;) {
		/* "n. ", "phr. v. " */
		for (t = s; *t >= 'a' && *t <= 'z'; t++);
		if (t == s || t[0] != '.' || t[1] != ' ') break;
		s = t + 2;
	}
	for (e = s; *e && *e != ';' && strncmp(e, " / ", 3) && n < max; n++) utf8_next(&e);
	snprintf(buf, size, "%.*s", (int)(e - s), s);
	utf8_trim(buf);
}

/* the rows of the keypad: words on its left keys, meanings on its right keys,
   each pair of keys on the row of the screen it picks */
#define PAIRS 4
static const int pair_keys[2][PAIRS] = {
	{ KEYPAD_1, KEYPAD_4, KEYPAD_7, KEYPAD_STAR },
	{ KEYPAD_3, KEYPAD_6, KEYPAD_9, KEYPAD_HASH } };

static int pairs_game(void) {
	static int list[MAX_CARDS];
	static char msg[96], text[2][PAIRS][64];
	static const char *const labels[2][PAIRS] = { { "1", "4", "7", "*" }, { "3", "6", "9", "#" } };
	int n = learned(list), pos = 0, rounds = 0, total = 0, errors = 0;
	if (n < PAIRS) return ui_message("Подбор пар", "Нужно хотя бы 4 изученных слова: "
			"сначала поучите их в обычном режиме.");
	for (;;) {
		/* order: the word of each meaning row; side, row: the cell picked first */
		int order[PAIRS], done[PAIRS] = { 0 }, side = -1, row = 0, i, matched = 0;
		const char *note = "Слово слева, перевод справа";
		if (pos + PAIRS > n) pos = 0;
		for (i = 0; i < PAIRS; i++) order[i] = i;
		for (i = PAIRS - 1; i > 0; i--) {
			int j = rng(i + 1), t = order[i];
			order[i] = order[j];
			order[j] = t;
		}
		for (i = 0; i < PAIRS; i++) {
			card_t *c = &cards[list[pos + i]];
			snprintf(text[0][i], sizeof(text[0][i]), "%s", c->front);
			utf8_trim(text[0][i]);
			first_meaning(cards[list[pos + order[i]]].back, text[1][i], sizeof(text[1][i]), 30);
		}
		pos += PAIRS;
		rounds++;
		while (matched < PAIRS) {
			int key, k, h = (scr_h - 2 * UI_BAR_H - 8) / PAIRS, half = scr_w / 2;
			char title[64];
			sprintf(title, "Пары  раунд %d  ошибок %d", rounds, errors);
			ui_header(title);
			for (i = 0; i < PAIRS; i++)
				for (k = 0; k < 2; k++) {
					int x = k * half + 2, y = UI_BAR_H + 4 + i * h;
					int ok = done[k ? order[i] : i];
					int s = side == k && row == i;
					fill_rect(x, y, half - 4, h - 4, s ? UI_SEL : ok ? RGB(26, 40, 34) : UI_CARD);
					draw_text(x + 4, y + 4, 1, labels[k][i], s ? UI_INK : ok ? UI_DIM : UI_SEL);
					draw_text_wrap(x + 4 + 2 * FONT_W, y + 4, half - 12 - 2 * FONT_W, 1, text[k][i],
							s ? UI_INK : ok ? UI_OK : UI_TEXT);
				}
			ui_footer(note);
			gfx_present();
			key = wait_key();
			if (key < 0) return -1;
			if (key == KEYPAD_RSOFT) {
				sprintf(msg, "Собрано пар: %d, ошибок: %d.", total, errors);
				return ui_message("Подбор пар", msg);
			}
			for (k = 0; k < 2; k++)
				for (i = 0; i < PAIRS; i++)
					if (key == pair_keys[k][i] && !done[k ? order[i] : i]) {
						if (side < 0 || side == k) {
							side = k;
							row = i;
							note = k ? "Теперь слово: 1 4 7 *" : "Теперь перевод: 3 6 9 #";
						} else {
							int word = k ? row : i, mean = k ? i : row;
							studied();
							warm_today++;
							dirty = 1;
							if (order[mean] == word) {
								done[word] = 1;
								matched++;
								total++;
								note = "Пара!";
							} else {
								errors++;
								note = "Не пара";
							}
							side = -1;
						}
					}
		}
	}
}

#define SPEED_MS 60000

static int speed_game(void) {
	static int list[MAX_CARDS];
	static char word[64], mean[64], fb[160], msg[96];
	int n = learned(list), pos = 0, right = 0, asked = 0, ok = 0, fb_ok = 0;
	uint32_t t0;
	if (n < 2) return ui_message("Верно или нет", "Нужно хотя бы 2 изученных слова: "
			"сначала поучите их в обычном режиме.");
	fb[0] = 0;
	t0 = sys_timer_ms();
	for (;;) {
		card_t *c = &cards[list[pos]];
		int key = 0;
		snprintf(word, sizeof(word), "%s", c->front);
		utf8_trim(word);
		first_meaning(c->back, mean, sizeof(mean), 28);
		ok = rng(2);
		if (!ok) {
			/* a meaning of another word */
			card_t *o = &cards[list[(pos + 1 + rng(n - 1)) % n]];
			first_meaning(o->back, mean, sizeof(mean), 28);
			if (same_word(o->back, c->back)) ok = 1;
		}
		while (!key) {
			int left = SPEED_MS - (int)(sys_timer_ms() - t0), y, s;
			char title[48];
			if (left <= 0) {
				sprintf(msg, "Верных ответов: %d из %d.", right, asked);
				return ui_message("Верно или нет", msg);
			}
			sprintf(title, "Верно или нет  %d с", (left + 999) / 1000);
			ui_header(title);
			draw_panel();
			s = text_len(word) * FONT_W * 2 <= (int)scr_w - 16 ? 2 : 1;
			y = draw_text_wrap(8, UI_BAR_H + 20, scr_w - 16, s, word, UI_TEXT) + 12;
			draw_text_wrap(8, y, scr_w - 16, 1, mean, UI_TEXT);
			y = UI_BTN_TOP - FONT_H - 6;
			if (fb[0]) draw_text_wrap(8, y - 2 * (FONT_H + 2) - 4, scr_w - 16, 1, fb,
					fb_ok ? UI_OK : rate_colors[0]);
			sprintf(msg, "Счёт: %d из %d", right, asked);
			draw_text_center(UI_BTN_TOP + 5, 1, msg, UI_DIM);
			ui_button(2, "1 Верно", UI_OK, 0);
			ui_button(3, "3 Неверно", rate_colors[0], 0);
			ui_footer("П.софт - закончить");
			gfx_present();
			key = wait_key_ms(left % 1000 + 1);
			if (key < 0) return -1;
			if (key == KEYPAD_RSOFT) {
				sprintf(msg, "Верных ответов: %d из %d.", right, asked);
				return ui_message("Верно или нет", msg);
			}
			if (key != KEYPAD_1 && key != KEYPAD_3) key = 0;
		}
		asked++;
		studied();
		warm_today++;
		dirty = 1;
		fb_ok = (key == KEYPAD_1) == ok;
		if (fb_ok) {
			right++;
			strcpy(fb, "Верно!");
		} else {
			char m[64];
			first_meaning(c->back, m, sizeof(m), 20);
			snprintf(fb, sizeof(fb), "Нет: %s - %s", word, m);
			utf8_trim(fb);
		}
		pos = (pos + 1) % n;
	}
}

/* the ways to learn; returns -1 on quit */
static int pick_mode(void) {
	static const char *const items[] = {
		"Обычный: вспомнить перевод",
		"Ввод: напечатать слово",
		"Подбор пар (разминка)",
		"Верно или нет (разминка)",
	};
	static int sel;
	int r = ui_pick("Как учить", items, 4, sel);
	if (r == -2) return -1;
	if (r < 0) return 0;
	sel = r;
	if (r == 2) return pairs_game();
	if (r == 3) return speed_game();
	return run_session(r == 1 ? MODE_TYPE : MODE_FLIP);
}

static int word_key(int key, void *ctx) {
	if (key != KEYPAD_STAR && key != KEYPAD_HASH && key != KEYPAD_1) return 0;
	*(int*)ctx = key;
	return 1;
}

/* a word of deck d: * marks it known, 1 puts it into today's session, # makes it
   new again; a word not in the box yet becomes a card */
static int word_screen(int d, const entry_t *e) {
	static char text[1400];
	char ivl[16];
	int key = 0, i = find_card(e->front);
	card_t *c = i >= 0 ? &cards[i] : NULL;
	if (c) {
		ivl_text(c->ivl, ivl);
		snprintf(text, sizeof(text), "%s\n\n%s\n\n%s%sСтатус: %s\nПовторений: %d, ошибок: %d\n"
				"Интервал: %s", c->front, c->back, c->ex, c->ex[0] ? "\n\n" : "",
				st_names[card_status(c)], c->reps, c->lapses, c->reps ? ivl : "-");
	} else snprintf(text, sizeof(text), "%s\n\n%s\n\n%s%sСтатус: новое", e->front, e->back,
				e->ex, e->ex[0] ? "\n\n" : "");
	utf8_trim(text);
	if (text_view("Слово", text, "*знаю 1учить #сброс П-назад", word_key, &key) < 0) return -1;
	if (!key) return 0;
	if (!c) {
		if ((i = add_card(e->front, e->back, e->ex, d)) < 0)
			return ui_message("Слово", "Не хватает места для карточек.");
		c = &cards[i];
	}
	if (key == KEYPAD_STAR) {
		c->ivl = KNOWN_IVL;
		c->due = app_today + KNOWN_IVL;
		if (!c->reps) c->reps = 1;
		c->flags = 0;
		ui_note("Отмечено: знаю");
	} else if (key == KEYPAD_1) {
		/* a new word comes first among the new ones, a started one today */
		if (!c->reps) c->flags |= F_NOW;
		else {
			if (c->ivl >= KNOWN_IVL) c->ivl = 1;
			c->due = app_today;
		}
		ui_note("Будет в сегодняшней сессии");
	} else {
		reset_card(c);
		ui_note("Слово снова новое");
	}
	dirty = 1;
	return 0;
}

/* the words of deck d with their status: # picks the status to show, 0 makes
   the whole deck new again */
static int deck_words(int d) {
	static const int keys[] = { KEYPAD_HASH, KEYPAD_0, 0 };
	static const char *const yes_no[] = { "Нет", "Да, все слова снова новые" };
	static char title[96];
	char *buf = NULL, **lines;
	const char **items;
	entry_t *ents = NULL;
	int *map, n, i, sel = 0, filter = ST_ALL, ret = 0;
	if (d == DECK_MY) {
		/* the cards of "my", they stay where they are while the list is open */
		for (n = i = 0; i < ncards; i++) n += cards[i].decks >> DECK_MY & 1;
		if ((ents = malloc((n + 1) * sizeof(*ents))))
			for (n = i = 0; i < ncards; i++)
				if (cards[i].decks >> DECK_MY & 1) {
					ents[n].front = cards[i].front;
					ents[n].back = cards[i].back;
					ents[n++].ex = cards[i].ex;
				}
	} else {
		ui_header(decks[d].name);
		draw_text_center(scr_h / 2 - FONT_H / 2, 1, "Читаю колоду...", UI_DIM);
		gfx_present();
		n = deck_read(d, &buf, &ents);
	}
	if (n < 0 || !ents) return ui_message(decks[d].name, "Не удалось прочитать колоду.");
	map = malloc((n + 1) * sizeof(*map));
	items = malloc((n + 1) * sizeof(*items));
	lines = calloc(n + 1, sizeof(*lines));
	if (!map || !items || !lines) {
		ret = ui_message(decks[d].name, "Не хватает памяти.");
		goto done;
	}
	for (;;) {
		int m = 0, r, key;
		for (i = 0; i < n; i++) {
			int c = find_card(ents[i].front), st = c < 0 ? ST_NEW : card_status(&cards[c]);
			if (filter != ST_ALL && st != filter) continue;
			free(lines[m]);
			if (!(lines[m] = malloc(strlen(ents[i].front) + 24))) break;
			sprintf(lines[m], st == ST_NEW ? "%s" : "%s  (%s)", ents[i].front, st_names[st]);
			items[m] = lines[m];
			map[m++] = i;
		}
		snprintf(title, sizeof(title), "%s: %s (%d)", decks[d].name, st_names[filter], m);
		utf8_trim(title);
		r = ui_pick_keys(title, items, m, sel, "5-слово #-показать 0-сброс", keys, &key);
		if (r == -2) { ret = -1; break; }
		if (key == KEYPAD_HASH) {
			filter = (filter + 1) % (ST_ALL + 1);
			sel = 0;
			continue;
		}
		if (key == KEYPAD_0) {
			int k = ui_pick("Сбросить колоду?", yes_no, 2, 0);
			if (k == -2) { ret = -1; break; }
			if (k == 1) {
				for (i = 0; i < ncards; i++) if (cards[i].decks >> d & 1) reset_card(&cards[i]);
				ui_note("Колода снова новая");
			}
			continue;
		}
		if (r < 0) break;
		sel = r;
		if (word_screen(d, &ents[map[r]]) < 0) { ret = -1; break; }
	}
done:
	if (lines) for (i = 0; i <= n; i++) free(lines[i]);
	free(lines);
	free(items);
	free(map);
	free(ents);
	free(buf);
	cards_save();
	return ret;
}

/* the decks with their progress: 5 turns one on or off, > shows its words */
static int decks_screen(void) {
	static const int keys[] = { KEYPAD_RIGHT, 0 };
	static char lines[MAX_DECKS][96];
	const char *items[MAX_DECKS];
	int sel = 0;
	for (;;) {
		int d, r, key;
		for (d = 0; d < ndecks; d++) {
			int box, done = deck_learned(d, &box), room, k;
			const char *p = decks[d].name;
			char count[24];
			snprintf(count, sizeof(count), "  %d/%d", done, deck_size(d, box));
			/* a long name is cut, not the count: "[x] Vocabulary in Use: 2. P..  0/2128" */
			room = (scr_w - 20) / FONT_W - 4 - text_len(count);
			if (text_len(p) > room)
				for (k = 0; k < room - 2 && *p; k++) utf8_next(&p);
			else
				p += strlen(p);
			snprintf(lines[d], sizeof(lines[d]), "%s %.*s%s%s", decks[d].on ? "[x]" : "[ ]",
					(int)(p - decks[d].name), decks[d].name, *p ? ".." : "", count);
			items[d] = lines[d];
		}
		r = ui_pick_keys("Колоды", items, ndecks, sel, "5-вкл/выкл >-слова П-назад", keys, &key);
		if (r == -2) return -1;
		if (r < 0) break;
		sel = r;
		if (key) {
			if (deck_words(r) < 0) return -1;
			continue;
		}
		decks[r].on = !decks[r].on;
		dirty = 1;
		if (decks[r].on && r != DECK_MY) {
			/* its words learned in other decks count at once */
			char *buf;
			entry_t *ents;
			ui_header("Колоды");
			draw_text_center(scr_h / 2 - FONT_H / 2, 1, "Читаю колоду...", UI_DIM);
			gfx_present();
			if (deck_read(r, &buf, &ents) >= 0) {
				free(buf);
				free(ents);
			}
		}
	}
	cards_save();
	return 0;
}

static int show_stats(void) {
	static char text[800];
	int i, learning = 0, learned = 0, known = 0, fresh = 0, tomorrow = 0, week = 0;
	for (i = 0; i < ncards; i++) {
		card_t *c = &cards[i];
		int st = card_status(c);
		if (st == ST_NEW) fresh++;
		else if (st == ST_KNOWN) known++;
		else {
			if (st == ST_LEARNED) learned++;
			else learning++;
			if (c->due <= app_today + 1) tomorrow++;
			if (c->due <= app_today + 7) week++;
		}
	}
	snprintf(text, sizeof(text),
			"Сегодня\n"
			"  ответов: %d, верных: %d%%\n"
			"  новых слов: %d\n"
			"  в разминках: %d\n\n"
			"Дней подряд: %d\n"
			"  лучшая серия: %d\n\n"
			"Карточек: %d\n"
			"  учу: %d, выучено: %d\n"
			"  знаю: %d\n"
			"  не начато: %d\n"
			"Повторений к завтра: %d\n"
			"  за неделю: %d",
			ans_today, ans_today ? ok_today * 100 / ans_today : 0, new_today, warm_today,
			last_day >= app_today - 1 ? streak : 0, best_streak,
			ncards, learning, learned, known, fresh, tomorrow, week);
	return ui_message("Статистика", text);
}

/* a number setting; returns -1 on quit */
static int ask_number(const char *title, const char *hint, int *val) {
	int r = ui_number(title, hint, 500);
	if (r == -2) return -1;
	if (r > 0) { *val = r; dirty = 1; }
	return 0;
}

static int settings(void) {
	int sel = 0;
	for (;;) {
		static char l1[64], l2[64];
		const char *items[3];
		int r;
		sprintf(l1, "Новых слов в день: %d", set_new);
		sprintf(l2, "Порция \"Ещё\": %d", set_more);
		items[0] = l1; items[1] = l2; items[2] = "Назад";
		r = ui_pick("Настройки", items, 3, sel);
		if (r == -2) return -1;
		if (r < 0 || r == 2) break;
		sel = r;
		if (r == 0 && ask_number("Новых в день", "Сколько новых слов в день:", &set_new) < 0) return -1;
		if (r == 1 && ask_number("Порция \"Ещё\"", "Сколько добавить за раз:", &set_more) < 0) return -1;
	}
	cards_save();
	return 0;
}

int cards_main(void) {
	if (cards_load() < 0) return ui_message("Карточки", "Не хватает памяти.");
	/* the transcriptions under the words (no dictionary: no transcriptions) */
	dict_open(DICT_FILE);
	new_day();
	for (;;) {
		static char title[96], learn[96], more[96], deck[96];
		/* the menu: what each line does */
		enum { M_LEARN, M_MORE, M_DECKS, M_STATS, M_SET, M_BACK };
		const char *items[6];
		int ids[6], n = 0, nrev, nnew, r, d, on = 0;
		count_due(&nrev, &nnew);
		sprintf(learn, "Учить (%d повт. + %d нов.)", nrev, nnew < new_left() ? nnew : new_left());
		ids[n] = M_LEARN; items[n++] = learn;
		if (nnew > new_left()) {
			sprintf(more, "Ещё %d новых слов", set_more);
			ids[n] = M_MORE; items[n++] = more;
		}
		for (d = 0; d < ndecks; d++) on += decks[d].on;
		sprintf(deck, "Колоды (включено %d)", on);
		ids[n] = M_DECKS; items[n++] = deck;
		ids[n] = M_STATS; items[n++] = "Статистика";
		ids[n] = M_SET; items[n++] = "Настройки";
		ids[n] = M_BACK; items[n++] = "Назад";
		if (app_today) {
			char date[16];
			date_text(app_today, date);
			sprintf(title, "Карточки %s (%d)", date, ncards);
		} else sprintf(title, "Карточки  (%d)", ncards);
		r = ui_pick(title, items, n, nrev || nnew ? 0 : n - 4);
		if (r == -2) { cards_save(); return -1; }
		if (r < 0 || ids[r] == M_BACK) break;
		r = ids[r];
		if (r == M_MORE) {
			extra_today += set_more;
			dirty = 1;
		}
		if ((r == M_LEARN || r == M_MORE) && pick_mode() < 0) return -1;
		if (r == M_DECKS && decks_screen() < 0) return -1;
		if (r == M_STATS && show_stats() < 0) return -1;
		if (r == M_SET && settings() < 0) return -1;
	}
	cards_save();
	return 0;
}
