#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "marks.h"
#include "log.h"

/* state/<id>.txt: "b" bookmark or "q" quote lines:
   "kind<TAB>block<TAB>char<TAB>text", rewritten whole on every change */

static void path(char *buf, int size, const char *id) {
	snprintf(buf, size, "state/%s.txt", id);
}

static char *field(char **p) {
	char *s = *p, *t;
	if (!s) return "";
	if ((t = strchr(s, '\t'))) *t = 0, *p = t + 1;
	else *p = NULL;
	return s;
}

void marks_free(marks_t *m) {
	int i;
	for (i = 0; i < m->n; i++) free(m->list[i].text);
	m->n = 0;
}

void marks_load(marks_t *m, const char *id) {
	char name[32], *buf, *pos, *line;
	m->n = 0;
	path(name, sizeof(name), id);
	if (!(buf = file_load(name, NULL))) return;
	for (pos = buf; m->n < MAX_MARKS && (line = next_line(&pos)); ) {
		mark_t *k = &m->list[m->n];
		char *kind = field(&line);
		if (kind[0] != 'b' && kind[0] != 'q') continue;
		k->kind = kind[0];
		k->pos.blk = strtoul(field(&line), NULL, 10);
		k->pos.ch = strtoul(field(&line), NULL, 10);
		if (!(k->text = malloc(strlen(line ? line : "") + 1))) break;
		strcpy(k->text, line ? line : "");
		m->n++;
	}
	free(buf);
}

int marks_save(marks_t *m, const char *id) {
	char name[32];
	FILE *fp;
	int i;
	LOG_T0(t0);
	path(name, sizeof(name), id);
	if (!(fp = fopen(name, "wb"))) return -1;
	for (i = 0; i < m->n; i++)
		fprintf(fp, "%c\t%u\t%u\t%s\n", m->list[i].kind, (unsigned)m->list[i].pos.blk,
				(unsigned)m->list[i].pos.ch, m->list[i].text);
	fclose(fp);
	LOG_OP(t0, "marks_save %d", m->n);
	return 0;
}

/* kept in book order */
int marks_add(marks_t *m, int kind, book_anchor_t pos, const char *text) {
	int i, len = strlen(text);
	char *t;
	if (m->n >= MAX_MARKS || !(t = malloc(len + 1))) return -1;
	/* the text is one line in the file */
	for (i = 0; i < len; i++) t[i] = text[i] == '\n' || text[i] == '\t' ? ' ' : text[i];
	t[len] = 0;
	for (i = m->n; i > 0; i--) {
		book_anchor_t a = m->list[i - 1].pos;
		if (a.blk < pos.blk || (a.blk == pos.blk && a.ch <= pos.ch)) break;
		m->list[i] = m->list[i - 1];
	}
	m->list[i].kind = kind;
	m->list[i].pos = pos;
	m->list[i].text = t;
	m->n++;
	return i;
}

void marks_remove(marks_t *m, int i) {
	free(m->list[i].text);
	memmove(m->list + i, m->list + i + 1, (m->n - i - 1) * sizeof(*m->list));
	m->n--;
}
