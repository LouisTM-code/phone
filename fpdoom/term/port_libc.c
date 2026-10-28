/* libc functions the fpdoom libc declares but has not, for the ports */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>
#include "syscode.h"
#if LIBC_SDIO
#include "fatfile.h"
#endif

char *fgets(char *s, int size, FILE *f) {
	int i = 0, c = 0;
	while (i < size - 1 && (c = fgetc(f)) != EOF) {
		s[i++] = c;
		if (c == '\n') break;
	}
	if (!i) return NULL;
	s[i] = 0;
	return s;
}

char *strstr(const char *s, const char *sub) {
	size_t n = strlen(sub);
	for (; *s; s++) if (!strncmp(s, sub, n)) return (char*)s;
	return n ? NULL : (char*)s;
}

/* no RTC: seconds since power on */
time_t time(time_t *t) {
	time_t r = sys_timer_ms() / 1000;
	if (t) *t = r;
	return r;
}

int ferror(FILE *f) {
	(void)f;
	return 0;
}

/* no push back in the fpdoom FILE */
int ungetc(int c, FILE *f) {
	(void)c; (void)f;
	return EOF;
}

int remove(const char *path) {
#if LIBC_SDIO
	fatdata_t *fatdata = &fatdata_glob;
	fat_entry_t *p = fat_find_path(fatdata, path);
	if (!p || (p->entry.attr & FAT_ATTR_DIR)) return -1;
	fat_delete_entry(fatdata, p);
	fat_flush_buf1(fatdata);
	fat_flush_buf2(fatdata);
	return 0;
#else
	(void)path;
	return -1;
#endif
}

int putc(int c, FILE *f) {
	return fputc(c, f);
}

void rewind(FILE *f) {
	fseek(f, 0, SEEK_SET);
}

char *strerror(int err) {
	(void)err;
	return "error";
}

/* the FAT driver has no rename: copy and delete */
int rename(const char *from, const char *to) {
	char buf[512];
	size_t n;
	FILE *fi = fopen(from, "rb"), *fo;
	if (!fi) return -1;
	fo = fopen(to, "wb");
	if (!fo) { fclose(fi); return -1; }
	while ((n = fread(buf, 1, sizeof(buf), fi)) > 0)
		if (fwrite(buf, 1, n, fo) != n) { fclose(fi); fclose(fo); return -1; }
	fclose(fi);
	fclose(fo);
	return remove(from);
}

/* scanf: %d %i %u %x %c %s %[...] %n with widths, h/l sizes and * */

typedef struct {
	const char *s; /* string input, or NULL for f */
	FILE *f;
	int last; /* char read back by unget, -2 = none */
} scan_src;

static int scan_get(scan_src *src) {
	int c;
	if (src->last != -2) { c = src->last; src->last = -2; return c; }
	if (src->s) return *src->s ? (unsigned char)*src->s++ : EOF;
	return fgetc(src->f);
}

static void scan_unget(scan_src *src, int c) {
	if (c != EOF) src->last = c;
}

static int vscan(scan_src *src, const char *fmt, va_list va) {
	int n = 0, count = 0, c;
	for (; *fmt; fmt++) {
		int skip = 0, width = 0, size = 0;
		if (isspace((unsigned char)*fmt)) {
			while ((c = scan_get(src)) != EOF && isspace(c)) count++;
			scan_unget(src, c);
			continue;
		}
		if (*fmt != '%' || fmt[1] == '%') {
			if (*fmt == '%') fmt++;
			c = scan_get(src);
			if (c != (unsigned char)*fmt) { scan_unget(src, c); break; }
			count++;
			continue;
		}
		fmt++;
		if (*fmt == '*') { skip = 1; fmt++; }
		while (isdigit((unsigned char)*fmt)) width = width * 10 + *fmt++ - '0';
		while (*fmt == 'h' || *fmt == 'l') size = *fmt++;
		if (*fmt == 'n') {
			if (!skip) *va_arg(va, int*) = count;
			continue;
		}
		if (*fmt == 'd' || *fmt == 'i' || *fmt == 'u' || *fmt == 'x') {
			int base = *fmt == 'x' ? 16 : 10, neg = 0, digits = 0;
			unsigned long v = 0;
			while ((c = scan_get(src)) != EOF && isspace(c)) count++;
			if (!width) width = 64;
			if (c == '-' || c == '+') { neg = c == '-'; c = scan_get(src); count++; width--; }
			for (; c != EOF && width > 0; c = scan_get(src), width--) {
				int d = isdigit(c) ? c - '0' : base == 16 && (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1;
				if (d < 0) break;
				v = v * base + d;
				digits++;
				count++;
			}
			scan_unget(src, c);
			if (!digits) break;
			if (neg) v = -v;
			if (!skip) {
				if (size == 'l') *va_arg(va, long*) = v;
				else if (size == 'h') *va_arg(va, short*) = v;
				else *va_arg(va, int*) = v;
				n++;
			}
			continue;
		}
		if (*fmt == 'c') {
			char *p = skip ? NULL : va_arg(va, char*);
			if (!width) width = 1;
			while (width--) {
				if ((c = scan_get(src)) == EOF) return n ? n : EOF;
				if (p) *p++ = c;
				count++;
			}
			if (!skip) n++;
			continue;
		}
		if (*fmt == 's' || *fmt == '[') {
			char set[256], *p = skip ? NULL : va_arg(va, char*);
			int got = 0;
			if (*fmt == 's') {
				int i;
				for (i = 0; i < 256; i++) set[i] = !isspace(i);
				set[0] = 0;
				while ((c = scan_get(src)) != EOF && isspace(c)) count++;
				scan_unget(src, c);
			} else {
				int inv = 0;
				fmt++;
				if (*fmt == '^') { inv = 1; fmt++; }
				memset(set, inv, sizeof(set));
				if (*fmt == ']') set[(unsigned char)*fmt++] = !inv;
				while (*fmt && *fmt != ']') set[(unsigned char)*fmt++] = !inv;
				set[0] = 0;
			}
			if (!width) width = 0x7fffffff;
			while (width-- && (c = scan_get(src)) != EOF && set[c]) {
				if (p) *p++ = c;
				got++;
				count++;
			}
			if (width >= 0) scan_unget(src, c);
			if (!got) break;
			if (p) { *p = 0; n++; }
			continue;
		}
		break;
	}
	if (src->f && src->last != -2) fseek(src->f, -1, SEEK_CUR);
	return n;
}

int vsscanf(const char *s, const char *fmt, va_list va) {
	scan_src src = { s, NULL, -2 };
	if (!*s) return EOF;
	return vscan(&src, fmt, va);
}

int sscanf(const char *s, const char *fmt, ...) {
	va_list va;
	int r;
	va_start(va, fmt);
	r = vsscanf(s, fmt, va);
	va_end(va);
	return r;
}

int fscanf(FILE *f, const char *fmt, ...) {
	scan_src src = { NULL, f, -2 };
	va_list va;
	int r;
	va_start(va, fmt);
	r = vscan(&src, fmt, va);
	va_end(va);
	return r;
}

size_t strspn(const char *s, const char *accept) {
	size_t n = 0;
	while (s[n] && strchr(accept, s[n])) n++;
	return n;
}

size_t strcspn(const char *s, const char *reject) {
	size_t n = 0;
	while (s[n] && !strchr(reject, s[n])) n++;
	return n;
}
