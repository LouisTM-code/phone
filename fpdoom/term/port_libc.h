/* declarations for port_libc.c missing from the fpdoom headers */
#include <stdio.h>
int remove(const char *path);
int putc(int c, FILE *f);
void rewind(FILE *f);
int rename(const char *from, const char *to);
char *strerror(int err);
int ferror(FILE *f);
