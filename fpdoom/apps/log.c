#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "log.h"

#define LOG_FILE "log.txt"
#define STACK_MARK 0x5354434bu
/* the stack is chip RAM 0x40000000..0x40009000 (sc6531e_fdl.ld), its first
   4KB hold the program arguments (fpdoom/entry.c). The linker symbols can't be
   used: the relocation of the loaded image shifts them too. */
#define STACK_LOW ((uint32_t*)0x40001000)
#define STACK_END ((uint32_t*)0x40009000)

/* free list of fpdoom's malloc (fpdoom/libc/malloc.h), chunk size just before */
struct mfree { struct mfree *next, *prev; };
extern struct mfree *_mfree0;

static const char *cur_app;
static uint32_t t_start, t_app;
static unsigned min_free = ~0u;
static int done;

static void heap_stat(unsigned *total, unsigned *big) {
	struct mfree *f;
	*total = *big = 0;
	for (f = _mfree0; f; f = f->next) {
		unsigned n = ((intptr_t*)f)[-1];
		*total += n;
		if (n > *big) *big = n;
	}
	if (*total < min_free) min_free = *total;
}

static void stack_paint(void) {
	uint32_t *p = STACK_LOW, *sp = (uint32_t*)&p - 64;
	while (p < sp) *p++ = STACK_MARK;
}

static unsigned stack_used(void) {
	uint32_t *p = STACK_LOW;
	while (p < STACK_END && *p == STACK_MARK) p++;
	return (char*)STACK_END - (char*)p;
}

void log_msg(const char *fmt, ...) {
	char buf[160];
	unsigned t = (sys_timer_ms() - t_start) / 1000;
	int n = sprintf(buf, "%3u:%02u ", t / 60, t % 60);
	va_list va;
	va_start(va, fmt);
	n += vsnprintf(buf + n, sizeof(buf) - n, fmt, va);
	va_end(va);
	if (n > (int)sizeof(buf) - 2) n = sizeof(buf) - 2;
	buf[n++] = '\n';
	buf[n] = 0;
	printf("%s", buf);
#if LIBC_SDIO
	{
		/* microfat has no append mode; the size is stored on fclose */
		FILE *f = fopen(LOG_FILE, "rb+");
		if (!f) f = fopen(LOG_FILE, "wb");
		if (f) {
			fseek(f, 0, SEEK_END);
			fwrite(buf, 1, n, f);
			fclose(f);
		}
	}
#endif
}

/* runs from exit(): malloc failure, bad free, assert */
static void log_crash(void) {
	unsigned total, big;
	if (done) return;
	done = 1;
	heap_stat(&total, &big);
	log_msg("exit crash app=%s stack=%uB free=%uK big=%uK", cur_app ? cur_app : "menu",
			stack_used(), total >> 10, big >> 10);
}

void log_start(void) {
	unsigned total, big;
	t_start = sys_timer_ms();
	stack_paint();
	atexit(log_crash);
	heap_stat(&total, &big);
	log_msg("start free=%uK", total >> 10);
}

void log_app(const char *name) {
	unsigned total, big;
	heap_stat(&total, &big);
	if (name) {
		t_app = sys_timer_ms();
		log_msg("app %s in free=%uK big=%uK", name, total >> 10, big >> 10);
	} else if (cur_app) {
		log_msg("app %s out %us free=%uK min=%uK", cur_app,
				(unsigned)(sys_timer_ms() - t_app) / 1000, total >> 10, min_free >> 10);
	}
	cur_app = name;
}

void log_end(void) {
	unsigned total, big;
	heap_stat(&total, &big);
	log_msg("exit ok stack=%uB free=%uK min=%uK", stack_used(), total >> 10, min_free >> 10);
	done = 1;
}
