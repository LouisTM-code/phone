#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dictfile.h"
#include "ui.h"

static FILE *dict_fp;
static int nwords;
static char *head_blob;
static const char **heads;
static uint32_t *offsets;
static long defs_base;

static uint32_t rd32(const uint8_t *p) {
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

void dict_close(void) {
	if (dict_fp) fclose(dict_fp);
	free(head_blob); free(heads); free(offsets);
	dict_fp = NULL; head_blob = NULL; heads = NULL; offsets = NULL;
	nwords = 0;
}

int dict_open(const char *path) {
	uint8_t hdr[16];
	uint32_t head_size, i;
	char *p;

	if (dict_fp) return 0;
	dict_fp = fopen(path, "rb");
	if (dict_fp) {
		ui_header("Словарь");
		draw_text_center(scr_h / 2 - FONT_H / 2, 1, "Загружаю словарь...", UI_DIM);
		gfx_present();
	}
	if (!dict_fp) return -1;
	if (fread(hdr, 1, 16, dict_fp) != 16 || memcmp(hdr, "DIC1", 4)) goto err;
	nwords = rd32(hdr + 4);
	head_size = rd32(hdr + 8);
	head_blob = malloc(head_size);
	heads = malloc(nwords * sizeof(*heads));
	offsets = malloc((nwords + 1) * 4);
	if (!head_blob || !heads || !offsets) goto err;
	if (fread(head_blob, 1, head_size, dict_fp) != head_size) goto err;
	if (fread(offsets, 4, nwords + 1, dict_fp) != (size_t)nwords + 1) goto err;
	for (i = 0; i <= (uint32_t)nwords; i++) offsets[i] = rd32((uint8_t*)&offsets[i]);
	for (p = head_blob, i = 0; i < (uint32_t)nwords; i++) {
		heads[i] = p;
		p += strlen(p) + 1;
	}
	defs_base = 16 + head_size + (nwords + 1) * 4;
	return 0;
err:
	dict_close();
	return -1;
}

int dict_count(void) { return nwords; }
const char *dict_head(int i) { return heads[i]; }

int dict_lower_bound(const char *prefix) {
	int a = 0, b = nwords;
	while (a < b) {
		int m = (a + b) / 2;
		if (strcmp(heads[m], prefix) < 0) a = m + 1;
		else b = m;
	}
	return a;
}

int dict_find(const char *word) {
	int i = dict_lower_bound(word);
	return i < nwords && !strcmp(heads[i], word) ? i : -1;
}

int dict_read(int i, char *buf, int size) {
	uint32_t len = offsets[i + 1] - offsets[i];
	if (len > (uint32_t)size - 1) len = size - 1;
	fseek(dict_fp, defs_base + offsets[i], SEEK_SET);
	len = fread(buf, 1, len, dict_fp);
	buf[len] = 0;
	/* don't leave half of a UTF-8 character at the end */
	{
		uint32_t k = len, need;
		while (k && ((uint8_t)buf[k - 1] & 0xc0) == 0x80) k--;
		if (k) {
			uint8_t lead = buf[k - 1];
			need = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
			if (len - (k - 1) < need) len = k - 1;
		}
	}
	buf[len] = 0;
	return len;
}

/* irregular forms: "form base" pairs */
static const char irregular[] =
	"am be|is be|are be|was be|were be|been be|being be|has have|had have|"
	"does do|did do|done do|went go|gone go|goes go|came come|saw see|seen see|"
	"took take|taken take|gave give|given give|got get|gotten get|made make|"
	"said say|knew know|known know|thought think|told tell|found find|felt feel|"
	"left leave|kept keep|began begin|begun begin|brought bring|bought buy|"
	"caught catch|taught teach|fought fight|sought seek|built build|sent send|"
	"spent spend|meant mean|met meet|led lead|fed feed|held hold|stood stand|"
	"understood understand|sat sit|lay lie|lain lie|laid lay|paid pay|sold sell|"
	"heard hear|lost lose|ran run|won win|wrote write|written write|rode ride|"
	"ridden ride|drove drive|driven drive|rose rise|risen rise|spoke speak|"
	"spoken speak|broke break|broken break|chose choose|chosen choose|froze freeze|"
	"frozen freeze|stole steal|stolen steal|woke wake|woken wake|wore wear|worn wear|"
	"tore tear|torn tear|bore bear|born bear|swore swear|sworn swear|ate eat|"
	"eaten eat|fell fall|fallen fall|forgot forget|forgotten forget|got get|"
	"hid hide|hidden hide|bit bite|bitten bite|drew draw|drawn draw|grew grow|"
	"grown grow|threw throw|thrown throw|flew fly|flown fly|blew blow|blown blow|"
	"shook shake|shaken shake|sang sing|sung sing|rang ring|rung ring|drank drink|"
	"drunk drink|swam swim|swum swim|sank sink|sunk sink|began begin|shot shoot|"
	"slept sleep|swept sweep|wept weep|crept creep|dealt deal|dreamt dream|"
	"lent lend|bent bend|built build|burnt burn|learnt learn|spelt spell|"
	"stuck stick|struck strike|hung hang|dug dig|spun spin|won win|shone shine|"
	"fled flee|slid slide|lit light|bred breed|bled bleed|sped speed|forgave forgive|"
	"forgiven forgive|became become|overcame overcome|withdrew withdraw|"
	"children child|men man|women woman|feet foot|teeth tooth|mice mouse|geese goose|"
	"people person|lives life|wives wife|knives knife|leaves leaf|halves half|"
	"selves self|wolves wolf|better good|best good|worse bad|worst bad|"
	"more much|most much|less little|least little|further far|farther far|"
	"him he|her she|them they|us we|me i|my i|his he|their they|our we|your you|an a|"
	"mrs mr|ms mr|mr mr|colour color|favourite favorite|centre center|theatre theater|";

static int try_word(const char *w, char *base, int base_size) {
	int i = dict_find(w);
	if (i >= 0 && base) {
		strncpy(base, w, base_size - 1);
		base[base_size - 1] = 0;
	}
	return i;
}

int dict_lookup(const char *word, char *base, int base_size) {
	char w[64], t[72];
	int n = 0, i, len;
	const char *p;

	/* lowercase, letters and inner apostrophes/hyphens only */
	for (p = word; *p && n < 60; p++) {
		char c = *p;
		if (c >= 'A' && c <= 'Z') c += 32;
		if ((c >= 'a' && c <= 'z') || ((c == '\'' || c == '-') && n)) w[n++] = c;
	}
	while (n && (w[n - 1] == '\'' || w[n - 1] == '-')) n--;
	w[n] = 0;
	if (!n) return -1;
	if ((i = try_word(w, base, base_size)) >= 0) return i;

	/* contractions: didn't, can't, won't, I'll, you're, we've, I'm, he'd, Tom's */
	{
		static const char *const full[][2] = {
			{ "won't", "will" }, { "can't", "can" }, { "shan't", "shall" }, { "ain't", "be" },
			{ "i'm", "be" }, { "let's", "let" },
		};
		static const char *const tails[][2] = {
			{ "n't", "" }, { "'ll", "will" }, { "'re", "be" }, { "'ve", "have" },
			{ "'d", "would" }, { "'s", "" }, { "'", "" },
		};
		unsigned k;
		for (k = 0; k < sizeof(full) / sizeof(*full); k++)
			if (!strcmp(w, full[k][0])) return try_word(full[k][1], base, base_size);
		for (k = 0; k < sizeof(tails) / sizeof(*tails); k++) {
			int tl = strlen(tails[k][0]);
			if (n > tl && !strcmp(w + n - tl, tails[k][0])) {
				/* "didn't" -> "did" -> do; "you'll" -> the word itself first */
				w[n - tl] = 0;
				if ((i = dict_lookup(w, base, base_size)) >= 0) return i;
				if (*tails[k][1]) return try_word(tails[k][1], base, base_size);
				return -1;
			}
		}
	}

	/* irregular forms */
	len = strlen(w);
	for (p = irregular; *p; ) {
		const char *sp = strchr(p, ' '), *bar = strchr(p, '|');
		if ((int)(sp - p) == len && !memcmp(p, w, len)) {
			memcpy(t, sp + 1, bar - sp - 1);
			t[bar - sp - 1] = 0;
			if ((i = try_word(t, base, base_size)) >= 0) return i;
		}
		p = bar + 1;
	}

#define ENDS(s) (len > (int)sizeof(s) && !strcmp(w + len - (sizeof(s) - 1), s))
#define TRY(cut, add) do { \
		memcpy(t, w, len - (cut)); strcpy(t + len - (cut), add); \
		if ((i = try_word(t, base, base_size)) >= 0) return i; \
	} while (0)

	if (ENDS("ies")) TRY(3, "y");
	if (ENDS("ves")) { TRY(3, "f"); TRY(3, "fe"); }
	if (ENDS("es")) TRY(2, "");
	if (ENDS("s") && !ENDS("ss")) TRY(1, "");
	if (ENDS("ied")) TRY(3, "y");
	if (ENDS("ed")) {
		TRY(2, ""); TRY(1, "");
		if (len > 4 && w[len - 3] == w[len - 4]) TRY(3, "");
	}
	if (ENDS("ing")) {
		TRY(3, ""); TRY(3, "e");
		if (len > 5 && w[len - 4] == w[len - 5]) TRY(4, "");
		if (ENDS("ying")) TRY(4, "ie");
	}
	if (ENDS("ier")) TRY(3, "y");
	if (ENDS("iest")) TRY(4, "y");
	if (ENDS("er")) {
		TRY(2, ""); TRY(1, "");
		if (len > 4 && w[len - 3] == w[len - 4]) TRY(3, "");
	}
	if (ENDS("est")) {
		TRY(3, ""); TRY(2, "");
		if (len > 5 && w[len - 4] == w[len - 5]) TRY(4, "");
	}
	if (ENDS("ily")) TRY(3, "y");
	if (ENDS("ly")) { TRY(2, ""); TRY(2, "le"); }
	/* British -ise spelling of -ize words */
	if (ENDS("ised")) { TRY(4, "ize"); TRY(4, "ise"); }
	if (ENDS("ising")) { TRY(5, "ize"); TRY(5, "ise"); }
	if (ENDS("ise")) TRY(3, "ize");
	if (ENDS("ises")) TRY(4, "ize");
	if (ENDS("ness")) TRY(4, "");
	if (ENDS("ment")) TRY(4, "");
	if (ENDS("ful")) TRY(3, "");
#undef TRY
#undef ENDS
	return -1;
}
