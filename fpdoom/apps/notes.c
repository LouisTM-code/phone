#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "apps.h"
#include "log.h"

/* state/notes.txt: a note per line, newest first, rewritten whole on every change */
#define NOTES_FILE "state/notes.txt"
#define MAX_NOTES 200
#define NOTE_MAX 512
/* the boot launcher's config, shown read-only: make update overwrites it */
#define CONFIG_FILE "fpbin/config.txt"

static char *notes[MAX_NOTES];
static int nnotes;
static int broken;  /* the file was not read whole: not saved, so it is not lost */

static void notes_load(void) {
	char *buf, *pos, *line;
	FILE *fp;
	nnotes = broken = 0;
	if (!(buf = file_load(NOTES_FILE, NULL))) {
		/* there but unreadable: saving now would lose the notes */
		if ((fp = fopen(NOTES_FILE, "rb"))) fclose(fp), broken = 1;
		return;
	}
	for (pos = buf; (line = next_line(&pos)); ) {
		if (!*line) continue;
		if (nnotes == MAX_NOTES) { broken = 1; break; }
		if (!(notes[nnotes] = malloc(strlen(line) + 1))) { broken = 1; break; }
		strcpy(notes[nnotes++], line);
	}
	free(buf);
}

static void notes_free(void) {
	while (nnotes) free(notes[--nnotes]);
}

static void notes_save(void) {
	FILE *fp;
	int i;
	LOG_T0(t0);
	if (broken || !(fp = fopen(NOTES_FILE, "wb"))) {
		ui_note("Не удалось записать заметки");
		return;
	}
	for (i = 0; i < nnotes; i++) fprintf(fp, "%s\n", notes[i]);
	fclose(fp);
	LOG_OP(t0, "notes_save %d", nnotes);
}

/* puts text at i (a new note) or over note i, returns -1 without memory */
static int notes_set(int i, const char *text, int add) {
	char *t;
	if (add && nnotes == MAX_NOTES) return -1;
	if (!(t = malloc(strlen(text) + 1))) return -1;
	strcpy(t, text);
	if (add) {
		memmove(notes + i + 1, notes + i, (nnotes - i) * sizeof(*notes));
		nnotes++;
	} else {
		free(notes[i]);
	}
	notes[i] = t;
	return 0;
}

static void notes_remove(int i) {
	free(notes[i]);
	memmove(notes + i, notes + i + 1, (nnotes - i - 1) * sizeof(*notes));
	nnotes--;
}

/* types note i, or a new one at the top if add: -1 on quit */
static int edit_note(int i, int add) {
	static char buf[NOTE_MAX];
	int len;
	if (add) buf[0] = 0;
	else snprintf(buf, sizeof(buf), "%s", notes[i]);
	len = ui_input(add ? "Новая заметка" : "Заметка", NULL, buf, sizeof(buf), 1);
	if (len == -2) return -1;
	if (len < 0) return 0;
	if (notes_set(i, buf, add) < 0) ui_note("Заметок слишком много");
	else notes_save();
	return 0;
}

int notes_main(void) {
	static char lines[MAX_NOTES + 1][64];
	const char *items[MAX_NOTES + 2];
	int i, sel = 0, ret = 0;
	notes_load();
	if (broken) ui_note("Заметки не прочитаны: новые не сохранятся");
	for (;;) {
		items[0] = "+ Новая заметка";
		for (i = 0; i < nnotes; i++) {
			snprintf(lines[i + 1], sizeof(lines[i + 1]), "%s", notes[i]);
			utf8_trim(lines[i + 1]);
			items[i + 1] = lines[i + 1];
		}
		items[nnotes + 1] = CONFIG_FILE;
		sel = ui_pick("Заметки", items, nnotes + 2, sel <= nnotes + 1 ? sel : nnotes + 1);
		if (sel == -2) { ret = -1; break; }
		if (sel < 0) break;
		if (sel == nnotes + 1) {
			char *text = file_load(CONFIG_FILE, NULL);
			int r = ui_message(CONFIG_FILE, text ? text : "Файл не прочитан");
			free(text);
			if (r < 0) { ret = -1; break; }
			continue;
		}
		if (sel == 0) {
			if (edit_note(0, 1) < 0) { ret = -1; break; }
			continue;
		}
		{
			const char *acts[3] = { "Показать", "Изменить", "Удалить" };
			int a = ui_pick("Заметка", acts, 3, 0), k = sel - 1;
			if (a == -2) { ret = -1; break; }
			if (a == 0 && ui_message("Заметка", notes[k]) < 0) { ret = -1; break; }
			if (a == 1 && edit_note(k, 0) < 0) { ret = -1; break; }
			if (a == 2) {
				notes_remove(k);
				notes_save();
				ui_note("Заметка удалена");
			}
		}
	}
	notes_free();
	return ret;
}
