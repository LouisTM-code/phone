#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "apps.h"

#define COL_BG     RGB(16, 24, 16)
#define COL_GRID   RGB(24, 36, 24)
#define COL_WALL   RGB(80, 110, 80)
#define COL_HEAD   RGB(140, 255, 90)
#define COL_BODY   RGB(60, 190, 60)
#define COL_FOOD   RGB(255, 70, 50)
#define COL_TEXT   RGB(255, 255, 255)
#define COL_DIM    RGB(150, 150, 150)

#define MAX_CELLS 1024

enum { DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT };
enum { ST_PLAY, ST_PAUSE, ST_OVER };

/* playfield geometry */
static unsigned cell, cols, rows, field_x, field_y, hud_h;

static struct { uint8_t x, y; } snake[MAX_CELLS];
static unsigned snake_len, head_idx;
static unsigned dir, next_dir;
static unsigned food_x, food_y;
static unsigned score, best, state;
static uint8_t occupied[MAX_CELLS];

static uint32_t rng_state;

static unsigned rng(unsigned n) {
	rng_state = rng_state * 1103515245 + 12345;
	return (rng_state >> 16) % n;
}

static void draw_cell(unsigned x, unsigned y, uint16_t c, int inset) {
	fill_rect(field_x + x * cell + inset, field_y + y * cell + inset,
			cell - inset * 2, cell - inset * 2, c);
}

static void place_food(void) {
	unsigned free_cells = cols * rows - snake_len, n, i;
	if (!free_cells) return;
	n = rng(free_cells);
	for (i = 0; i < cols * rows; i++) {
		if (occupied[i]) continue;
		if (!n--) break;
	}
	food_x = i % cols;
	food_y = i / cols;
}

static void game_reset(void) {
	unsigned i, cx = cols / 2, cy = rows / 2;
	memset(occupied, 0, sizeof(occupied));
	snake_len = 3;
	head_idx = snake_len - 1;
	for (i = 0; i < snake_len; i++) {
		snake[i].x = cx - (snake_len - 1 - i);
		snake[i].y = cy;
		occupied[snake[i].y * cols + snake[i].x] = 1;
	}
	dir = next_dir = DIR_RIGHT;
	score = 0;
	state = ST_PLAY;
	place_food();
}

/* returns 0 when the snake crashed */
static int game_step(void) {
	unsigned hx = snake[head_idx].x, hy = snake[head_idx].y;
	unsigned tail_idx = (head_idx + MAX_CELLS + 1 - snake_len) % MAX_CELLS;
	int grow;

	dir = next_dir;
	switch (dir) {
	case DIR_UP: if (hy == 0) return 0; hy--; break;
	case DIR_DOWN: if (hy == rows - 1) return 0; hy++; break;
	case DIR_LEFT: if (hx == 0) return 0; hx--; break;
	default: if (hx == cols - 1) return 0; hx++; break;
	}

	grow = hx == food_x && hy == food_y;
	/* the tail moves away this turn, so it's legal to step onto it */
	if (!grow) occupied[snake[tail_idx].y * cols + snake[tail_idx].x] = 0;
	if (occupied[hy * cols + hx]) return 0;

	head_idx = (head_idx + 1) % MAX_CELLS;
	snake[head_idx].x = hx;
	snake[head_idx].y = hy;
	occupied[hy * cols + hx] = 1;

	if (grow) {
		snake_len++;
		score++;
		if (score > best) best = score;
		if (snake_len >= cols * rows) return 0;
		place_food();
	}
	return 1;
}

static void render(void) {
	char buf[32];
	unsigned i, x, y;
	int inset = cell > 6 ? 1 : 0;

	fill_rect(0, 0, scr_w, scr_h, COL_WALL);
	fill_rect(0, 0, scr_w, hud_h, COL_BG);
	fill_rect(field_x, field_y, cols * cell, rows * cell, COL_BG);
	for (y = 0; y < rows; y++)
	for (x = (y & 1); x < cols; x += 2)
		draw_cell(x, y, COL_GRID, 0);

	sprintf(buf, "Счёт: %u", score);
	draw_text(4, (hud_h - FONT_H) / 2, 1, buf, COL_TEXT);
	sprintf(buf, "Рекорд: %u", best);
	draw_text(scr_w - 4 - text_len(buf) * FONT_W, (hud_h - FONT_H) / 2, 1, buf, COL_DIM);

	draw_cell(food_x, food_y, COL_FOOD, inset);
	for (i = 0; i < snake_len; i++) {
		unsigned k = (head_idx + MAX_CELLS - i) % MAX_CELLS;
		draw_cell(snake[k].x, snake[k].y, i ? COL_BODY : COL_HEAD, inset);
	}

	if (state != ST_PLAY) {
		int big = scr_w >= 200 ? 2 : 1, mid = field_y + rows * cell / 2;
		fill_rect(0, mid - FONT_H * big - 6, scr_w, FONT_H * (big + 1) + 18, COL_BG);
		if (state == ST_PAUSE)
			draw_text_center(mid - FONT_H * big - 2, big, "ПАУЗА", COL_TEXT);
		else
			draw_text_center(mid - FONT_H * big - 2, big, "КОНЕЦ ИГРЫ", COL_FOOD);
		draw_text_center(mid + 6, 1, "5 - дальше, П.софт - меню", COL_DIM);
	}
	gfx_present();
}

static void set_dir(unsigned d) {
	/* forbid reversing into the neck */
	if ((d ^ dir) == 2) return;
	next_dir = d;
}

/* returns 1 to leave the game */
static int handle_key(int key) {
	switch (key) {
	case KEYPAD_UP: case KEYPAD_2: set_dir(DIR_UP); break;
	case KEYPAD_DOWN: case KEYPAD_8: set_dir(DIR_DOWN); break;
	case KEYPAD_LEFT: case KEYPAD_4: set_dir(DIR_LEFT); break;
	case KEYPAD_RIGHT: case KEYPAD_6: set_dir(DIR_RIGHT); break;
	case KEYPAD_CENTER: case KEYPAD_5: case KEYPAD_LSOFT:
		if (state == ST_OVER) game_reset();
		else state = state == ST_PLAY ? ST_PAUSE : ST_PLAY;
		break;
	case KEYPAD_RSOFT: return 1;
	}
	return 0;
}

static void layout(void) {
	unsigned avail_h;
	hud_h = FONT_H + 6;
	avail_h = scr_h - hud_h;
	cell = (scr_w < avail_h ? scr_w : avail_h) / 16;
	if (cell < 4) cell = 4;
	cols = (scr_w - 2) / cell;
	rows = (avail_h - 2) / cell;
	while (cols * rows > MAX_CELLS) {
		cell++;
		cols = (scr_w - 2) / cell;
		rows = (avail_h - 2) / cell;
	}
	field_x = (scr_w - cols * cell) / 2;
	field_y = hud_h + (avail_h - rows * cell) / 2;
}

int snake_main(void) {
	uint32_t next_tick;

	layout();
	rng_state = sys_timer_ms();
	game_reset();
	render();
	next_tick = sys_timer_ms();

	for (;;) {
		int key;
		while ((key = poll_key())) {
			if (key < 0) return -1;
			if (handle_key(key)) return 0;
		}
		if (state == ST_PLAY) {
			uint32_t now = sys_timer_ms();
			if ((int32_t)(now - next_tick) >= 0) {
				/* speeds up from 180 ms to 70 ms per step */
				unsigned delay = score >= 22 ? 70 : 180 - score * 5;
				next_tick = now + delay;
				if (!game_step()) state = ST_OVER;
			}
		} else {
			next_tick = sys_timer_ms();
		}
		render();
		sys_wait_ms(10);
	}
}
