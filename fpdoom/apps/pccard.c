/* PC build only: the limits of the phone's SD card code (fpdoom/microfat.c)
   and the cost of reading it. fopen allows "r", "w" and "+" only, and a new
   file needs an 8.3 name; rename, remove and folders are not there at all.
   Reads and the heap are counted and printed by pc_shot_hook() at
   each screenshot of a test script (fpdoom/pctest/syscode.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

FILE *__real_fopen(const char *name, const char *mode);
size_t __real_fread(void *p, size_t size, size_t n, FILE *f);
void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t size);
void *__real_realloc(void *p, size_t n);
void __real_free(void *p);

static unsigned long io_read, heap_now, heap_peak, heap_base;

static int short_name(const char *path) {
	const char *name = strrchr(path, '/'), *dot;
	unsigned base;
	name = name ? name + 1 : path;
	dot = strchr(name, '.');
	base = dot ? (unsigned)(dot - name) : strlen(name);
	if (!base || base > 8 || (dot && (strlen(dot + 1) > 3 || strchr(dot + 1, '.')))) return 0;
	for (; *name; name++)
		if ((unsigned char)*name >= 127 || strchr(" +,;=[]\"*/:<>?\\|", *name)) return 0;
	return 1;
}

FILE *__wrap_fopen(const char *name, const char *mode) {
	const char *shots = getenv("FP_SHOTS");
	/* screenshots of the test script are not on the card */
	if (shots && !strncmp(name, shots, strlen(shots))) return __real_fopen(name, mode);
	if ((mode[0] != 'r' && mode[0] != 'w') || strchr(mode, 'a')) {
		fprintf(stderr, "!!! card: fopen mode \"%s\" is not supported\n", mode);
		abort();
	}
	if (mode[0] == 'w' && access(name, F_OK) && !short_name(name)) {
		fprintf(stderr, "!!! card: can't create \"%s\", not an 8.3 name\n", name);
		return NULL;
	}
	return __real_fopen(name, mode);
}

size_t __wrap_fread(void *p, size_t size, size_t n, FILE *f) {
	size_t r = __real_fread(p, size, n, f);
	io_read += r * size;
	return r;
}

/* the size is kept in front of each block */
#define HDR 16

static void *track(char *p, size_t n) {
	if (!p) return NULL;
	*(size_t*)p = n;
	heap_now += n;
	if (heap_now > heap_peak) heap_peak = heap_now;
	return p + HDR;
}

void *__wrap_malloc(size_t n) { return track(__real_malloc(n + HDR), n); }

void *__wrap_calloc(size_t n, size_t size) {
	return track(__real_calloc(1, n * size + HDR), n * size);
}

void __wrap_free(void *p) {
	if (!p) return;
	p = (char*)p - HDR;
	heap_now -= *(size_t*)p;
	__real_free(p);
}

void *__wrap_realloc(void *p, size_t n) {
	char *q;
	size_t old = 0;
	if (p) {
		p = (char*)p - HDR;
		old = *(size_t*)p;
	}
	q = __real_realloc(p, n + HDR);
	if (!q) return NULL;
	heap_now -= old;
	return track(q, n);
}

void pc_shot_hook(const char *name) {
	/* peak: the most the heap grew over its size at the previous screenshot */
	printf("io %s read=%lu heap=%lu peak=%lu\n", name, io_read, heap_now,
			heap_peak - heap_base);
	io_read = 0;
	heap_peak = heap_base = heap_now;
}
