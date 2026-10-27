/* Boot launcher: picks a program by the keys held at power on.
   Runs as fpbin/fpmain.bin from sdboot. Menu items of fpbin/config.txt
   (fpmenu syntax) are named after keys: "|4|", "|4+2|", "|default|".
   The first held key is the sdboot key (LSOFT, 1 or 4), more keys may follow.
   Chain loading is taken from ../fpmenu (main.c run_binary and loop_end). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "syscode.h"
#include "sdio.h"
#include "fatfile.h"

#define DBG_LOG(...) (void)0

extern uint8_t __image_start[];

typedef void (*readbin_t)(unsigned clust, unsigned size, uint8_t *ram,
		fatdata_t *fatdata);

#include "readconf.h"

/* time to see the extra keys, they are reported a few ms after the first */
#define KEY_WAIT_MS 100
/* smaller files are surely broken (an interrupted copy) */
#define MIN_BIN_SIZE 0x100

static const char *key_name(unsigned code) {
	switch (code) {
#define X(num, name) case num: return #name;
	KEYPAD_ENUM(X)
#undef X
	}
	return NULL;
}

/* key codes held now, at most 4 (the keypad reports 4 slots) */
static unsigned held_keys(unsigned *keys) {
	uint32_t st;
	unsigned i, n = 0;

	sys_wait_ms(KEY_WAIT_MS);
	st = KEYPAD_BASE->key_status;
	for (i = 0; i < 4; i++) {
		unsigned k = st >> (i * 8) & 0xff;
		if (!(k & 0x80)) continue;
		k = sys_data.keytrn[0][(k & 0x70) >> 1 | (k & 7)];
		if (key_name(k)) keys[n++] = k;
	}
	return n;
}

/* "4+2" matches when exactly these keys are held, in any order:
   the slot order follows the keypad scan, not the order of pressing */
static int caption_match(const char *cap, const unsigned *keys, unsigned nk) {
	unsigned i, n = 0, used = 0;
	while (*cap) {
		const char *end = strchr(cap, '+');
		size_t len = end ? (size_t)(end - cap) : strlen(cap);
		for (i = 0; i < nk; i++) {
			const char *name = key_name(keys[i]);
			if (!(used >> i & 1) && strlen(name) == len && !memcmp(name, cap, len)) break;
		}
		if (i == nk) return 0;
		used |= 1 << i;
		n++;
		cap += len;
		if (*cap) cap++;
	}
	return n && n == nk;
}

/* args of the first item whose caption is name (when not NULL)
   or matches the keys, NULL if none */
static char *find_item(char *menu, const char *name, const unsigned *keys, unsigned nk) {
	char *p = menu;
	for (;;) {
		uint32_t next = *(uint32_t*)p;
		char *cap;
		if (!next) return NULL;
		p += next;
		cap = p + 4;
		if (name ? !strcmp(cap, name) : caption_match(cap, keys, nk)) {
			while (*cap++);
			return cap;
		}
	}
}

static int run(char *args) {
	char *d = (char*)CHIPRAM_ADDR, *name;
	extract_args_t x = { 0, 1, d + 0x1000 };
	fat_entry_t *p;
	uint8_t *ram, *dst;
	uint32_t size;
	unsigned clust, fsize;

	name = get_first_arg(args);
	if (!name) return 1;
	if (!extract_args(0, &x, args, d + 6)) return 1;
	*(short*)(d + 4) = x.argc;
	p = fat_find_path(&fatdata_glob, name);
	if (!p || (p->entry.attr & FAT_ATTR_DIR)) return 1;
	clust = fat_entry_clust(p);
	fsize = p->entry.size;
	if (fsize < MIN_BIN_SIZE || fsize > (2 << 20) - 0x10000) return 1;

	/* readbin.c is linked behind the image, copy it out and let it load
	   the binary over this program */
	ram = (uint8_t*)((intptr_t)__image_start & 0xfc000000);
	size = *(uint32_t*)(ram + 4) - 8;
	dst = (uint8_t*)CHIPRAM_ADDR + 0x4000;
	memcpy(dst, ram + 8, size);
	clean_dcache();
	invalidate_icache();
	((readbin_t)dst)(clust, fsize, ram, &fatdata_glob);
	return 1;
}

int main(int argc, char **argv) {
	unsigned keys[4], nk, i;
	char *menu, *args;
	FILE *fi;
	(void)argc; (void)argv;

	nk = held_keys(keys);
	fi = fopen(FPBIN_DIR "config.txt", "rb");
	if (!fi) return 1;
	menu = parse_config(fi, 0x10000);
	fclose(fi);
	if (!menu) return 1;

	args = find_item(menu, NULL, keys, nk);
	/* unknown combination: each key alone, the sdboot key first */
	for (i = 0; !args && i < nk; i++) args = find_item(menu, NULL, keys + i, 1);
	if (!args) args = find_item(menu, "default", NULL, 0);
	if (!args) return 1;
	return run(args);
}

/* keys are read from the registers, events are not used */
void keytrn_init(void) {
	uint8_t keymap[64];
	int i;
	sys_getkeymap(keymap);
	for (i = 0; i < 64; i++) {
		sys_data.keytrn[0][i] = keymap[i];
		sys_data.keytrn[1][i] = 0;
	}
}

void lcd_appinit(void) {
	struct sys_display *disp = &sys_data.display;
	disp->w2 = disp->w1;
	disp->h2 = disp->h1;
}
