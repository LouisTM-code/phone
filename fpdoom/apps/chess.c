#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "apps.h"
#include "chess_engine.h"
#include "pieces.h"

#define PIECE_N 26

#define COL_BG      RGB(28, 26, 24)
#define COL_BAR     RGB(48, 44, 40)
#define COL_TEXT    RGB(255, 255, 255)
#define COL_DIM     RGB(170, 165, 155)
#define COL_ACCENT  RGB(255, 210, 80)
#define COL_LIGHT   RGB(238, 216, 180)
#define COL_DARK    RGB(181, 136, 99)
#define COL_LAST_L  RGB(246, 236, 140)
#define COL_LAST_D  RGB(214, 196, 90)
#define COL_SEL     RGB(120, 170, 80)
#define COL_HINT    RGB(110, 160, 230)
#define COL_CHECK   RGB(230, 80, 70)
#define COL_CURSOR  RGB(30, 110, 255)
#define COL_DOT     RGB(70, 110, 50)
#define COL_WPIECE  RGB(250, 248, 240)
#define COL_BPIECE  RGB(30, 30, 30)

#define RECOMMENDED_LEVEL 4

#define MAX_GAME_PLY 600

static pos_t pos;
static undo_t game[MAX_GAME_PLY];
static int game_ply;
static int user_side, level = RECOMMENDED_LEVEL;
static int cur_file, cur_rank, sel_sq;
static move_t legal[MAX_MOVES];
static int nlegal;
static move_t last_move, hint_move;
static uint32_t seed;
static int sq_size, board_x, board_y;

static int is_light(int sq) { return (SQ_FILE(sq) + SQ_RANK(sq)) & 1; }

static void sq_to_screen(int sq, int *x, int *y) {
	int f = SQ_FILE(sq), r = SQ_RANK(sq);
	if (user_side < 0) { f = 7 - f; r = 7 - r; }
	*x = board_x + f * sq_size;
	*y = board_y + (7 - r) * sq_size;
}

static void draw_mask(int x, int y, const uint32_t *rows, uint16_t c) {
	int i, j;
	for (j = 0; j < PIECE_N; j++) {
		uint32_t bits = rows[j];
		if (!bits) continue;
		for (i = 0; i < PIECE_N; i++)
			if (bits >> (PIECE_N - 1 - i) & 1) {
				int k = i;
				/* draw runs of pixels at once */
				while (k + 1 < PIECE_N && bits >> (PIECE_N - 2 - k) & 1) k++;
				fill_rect(x + i, y + j, k - i + 1, 1, c);
				i = k;
			}
	}
}

static void draw_piece(int x, int y, int pc) {
	int t = pc < 0 ? -pc : pc;
	x += (sq_size - PIECE_N) / 2;
	y += (sq_size - PIECE_N) / 2;
	if (pc > 0) {
		draw_mask(x, y, piece_sil[t], COL_WPIECE);
		draw_mask(x, y, piece_edge[t], COL_BPIECE);
		draw_mask(x, y, piece_detail[t], COL_BPIECE);
	} else {
		draw_mask(x, y, piece_sil[t], COL_BPIECE);
		draw_mask(x, y, piece_edge[t], RGB(0, 0, 0));
		draw_mask(x, y, piece_detail[t], RGB(170, 170, 170));
	}
}

static int is_legal_dest(int sq) {
	int i;
	for (i = 0; i < nlegal; i++)
		if (MV_FROM(legal[i]) == sel_sq && MV_TO(legal[i]) == sq) return 1;
	return 0;
}

static void draw_board(void) {
	int sq, check_sq = ce_in_check(&pos) ? pos.king[pos.side < 0] : -1;
	int cursor = SQ(cur_file, cur_rank);

	for (sq = 0; sq < 128; sq++) {
		int x, y;
		uint16_t c;
		if (sq & 0x88) { sq += 7; continue; }
		sq_to_screen(sq, &x, &y);
		c = is_light(sq) ? COL_LIGHT : COL_DARK;
		if (last_move && (sq == MV_FROM(last_move) || sq == MV_TO(last_move)))
			c = is_light(sq) ? COL_LAST_L : COL_LAST_D;
		if (hint_move && (sq == MV_FROM(hint_move) || sq == MV_TO(hint_move)))
			c = COL_HINT;
		if (sq == sel_sq) c = COL_SEL;
		if (sq == check_sq) c = COL_CHECK;
		fill_rect(x, y, sq_size, sq_size, c);
		if (pos.b[sq]) draw_piece(x, y, pos.b[sq]);
		if (sel_sq >= 0 && is_legal_dest(sq)) {
			int d = sq_size / 5;
			if (pos.b[sq]) {
				/* corner marks on capturable pieces */
				fill_rect(x, y, d, 3, COL_DOT); fill_rect(x, y, 3, d, COL_DOT);
				fill_rect(x + sq_size - d, y, d, 3, COL_DOT); fill_rect(x + sq_size - 3, y, 3, d, COL_DOT);
				fill_rect(x, y + sq_size - 3, d, 3, COL_DOT); fill_rect(x, y + sq_size - d, 3, d, COL_DOT);
				fill_rect(x + sq_size - d, y + sq_size - 3, d, 3, COL_DOT);
				fill_rect(x + sq_size - 3, y + sq_size - d, 3, d, COL_DOT);
			} else {
				fill_rect(x + (sq_size - d) / 2, y + (sq_size - d) / 2, d, d, COL_DOT);
			}
		}
		if (sq == cursor) {
			fill_rect(x, y, sq_size, 2, COL_CURSOR);
			fill_rect(x, y + sq_size - 2, sq_size, 2, COL_CURSOR);
			fill_rect(x, y, 2, sq_size, COL_CURSOR);
			fill_rect(x + sq_size - 2, y, 2, sq_size, COL_CURSOR);
		}
	}
}

static int material(void) {
	static const int val[7] = { 0, 1, 3, 3, 5, 9, 0 };
	int sq, sum = 0;
	for (sq = 0; sq < 128; sq++) {
		int pc;
		if (sq & 0x88) { sq += 7; continue; }
		pc = pos.b[sq];
		sum += pc > 0 ? val[pc] : -val[-pc];
	}
	return sum * user_side;
}

static const char *status_text(int st) {
	switch (st) {
	case ST_MATE: return pos.side == user_side ? "Мат. Вы проиграли" : "Мат! Вы победили!";
	case ST_STALEMATE: return "Пат. Ничья";
	case ST_DRAW_50: return "Ничья: 50 ходов";
	case ST_DRAW_REP: return "Ничья: повторение";
	case ST_DRAW_MATERIAL: return "Ничья: мало фигур";
	}
	if (pos.side != user_side) return "Компьютер думает...";
	return ce_in_check(&pos) ? "Шах! Ваш ход" : "Ваш ход";
}

static void draw_screen(const char *status, const char *footer) {
	char buf[64], mv[8];
	int y = board_y + 8 * sq_size + 4, m = material();

	fill_rect(0, 0, scr_w, scr_h, COL_BG);
	fill_rect(0, 0, scr_w, board_y, COL_BAR);
	sprintf(buf, "%s ~%d", ce_levels[level].name, ce_levels[level].rating);
	draw_text(4, (board_y - FONT_H) / 2, 1, buf, COL_DIM);
	sprintf(buf, "Ход %d", pos.fullmove);
	draw_text(scr_w - 4 - text_len(buf) * FONT_W, (board_y - FONT_H) / 2, 1, buf, COL_DIM);
	draw_board();

	draw_text(4, y, 1, status, COL_ACCENT);
	if (m) {
		sprintf(buf, "%+d", m);
		draw_text(scr_w - 4 - text_len(buf) * FONT_W, y, 1, buf, m > 0 ? RGB(120, 220, 120) : RGB(240, 120, 110));
	}
	if (last_move) {
		ce_move_str(last_move, mv);
		sprintf(buf, "Последний ход: %c%c-%c%c", mv[0], mv[1], mv[2], mv[3]);
		draw_text(4, y + FONT_H + 2, 1, buf, COL_DIM);
	}
	fill_rect(0, scr_h - FONT_H - 4, scr_w, FONT_H + 4, COL_BAR);
	draw_text(4, scr_h - FONT_H - 2, 1, footer, COL_DIM);
	gfx_present();
}

static void refresh_legal(void) {
	nlegal = ce_gen_legal(&pos, legal);
}

static void do_move(move_t m) {
	if (game_ply >= MAX_GAME_PLY) return;
	ce_make(&pos, m, &game[game_ply++]);
	last_move = m;
	hint_move = 0;
	sel_sq = -1;
	refresh_legal();
}

static void undo_move(void) {
	if (!game_ply) return;
	ce_unmake(&pos, &game[--game_ply]);
	last_move = game_ply ? game[game_ply - 1].m : 0;
	hint_move = 0;
	sel_sq = -1;
	refresh_legal();
}

/* generic list picker drawn over the screen; returns index or -1 */
static int pick(const char *title, const char *const *items, int n, int sel, const char *note) {
	int item_h = FONT_H + 6, h = item_h * n + FONT_H * 2 + 16;
	int top = 0, visible = n;
	if (h > (int)scr_h - 8) {
		visible = (scr_h - 8 - FONT_H * 2 - 16) / item_h;
		h = item_h * visible + FONT_H * 2 + 16;
	}
	for (;;) {
		int i, y0 = (scr_h - h) / 2, key;
		if (sel < top) top = sel;
		if (sel >= top + visible) top = sel - visible + 1;
		fill_rect(8, y0, scr_w - 16, h, COL_BAR);
		fill_rect(8, y0, scr_w - 16, 2, COL_ACCENT);
		draw_text(16, y0 + 6, 1, title, COL_ACCENT);
		for (i = 0; i < visible && top + i < n; i++) {
			int y = y0 + FONT_H + 12 + i * item_h;
			if (top + i == sel) fill_rect(12, y - 3, scr_w - 24, item_h, RGB(80, 110, 190));
			draw_text(18, y, 1, items[top + i], COL_TEXT);
		}
		if (note) draw_text(16, y0 + h - FONT_H - 4, 1, note, COL_DIM);
		gfx_present();
		key = wait_key();
		if (key < 0) return -2;
		if (key == KEYPAD_UP || key == KEYPAD_2) sel = (sel + n - 1) % n;
		else if (key == KEYPAD_DOWN || key == KEYPAD_8) sel = (sel + 1) % n;
		else if (key == KEYPAD_CENTER || key == KEYPAD_5 || key == KEYPAD_LSOFT) return sel;
		else if (key == KEYPAD_RSOFT) return -1;
	}
}

/* returns 0 to start, -1 to leave, -2 on quit */
static int setup_game(void) {
	static char names[16][40];
	const char *items[16];
	static const char *const colors[] = { "Белые", "Чёрные", "Случайно" };
	int i, r;

	fill_rect(0, 0, scr_w, scr_h, COL_BG);
	for (i = 0; i < ce_nlevels && i < 16; i++) {
		sprintf(names[i], "%2d. %s ~%d%s", i + 1, ce_levels[i].name, ce_levels[i].rating,
				i == RECOMMENDED_LEVEL ? " *" : "");
		items[i] = names[i];
	}
	r = pick("Уровень компьютера", items, ce_nlevels, level, "* под ваши ~750");
	if (r < 0) return r;
	level = r;

	fill_rect(0, 0, scr_w, scr_h, COL_BG);
	r = pick("Играть за", colors, 3, 0, NULL);
	if (r < 0) return r;
	user_side = r == 0 ? 1 : r == 1 ? -1 : (sys_timer_ms() & 1 ? 1 : -1);

	ce_init(&pos);
	game_ply = 0;
	last_move = hint_move = 0;
	sel_sq = -1;
	cur_file = 4;
	cur_rank = user_side > 0 ? 1 : 6;
	refresh_legal();
	return 0;
}

static int ask_promotion(void) {
	static const char *const items[] = { "Ферзь", "Ладья", "Слон", "Конь" };
	static const int promo[] = { QUEEN, ROOK, BISHOP, KNIGHT };
	int r = pick("Превратить пешку в", items, 4, 0, NULL);
	return r < 0 ? (r == -2 ? -1 : 0) : promo[r];
}

static void computer_move(void) {
	move_t m;
	uint32_t t0 = sys_timer_ms();
	draw_screen("Компьютер думает...", "");
	m = ce_think(&pos, level, &seed, NULL);
	printf("chess: level %d, %ld nodes, %u ms\n", level, ce_nodes, (unsigned)(sys_timer_ms() - t0));
	if (m) do_move(m);
}

/* user pressed select on the cursor square; returns -1 on quit */
static int select_square(void) {
	int sq = SQ(cur_file, cur_rank), i;
	if (sel_sq >= 0 && is_legal_dest(sq)) {
		move_t m = 0;
		int promo = 0;
		for (i = 0; i < nlegal; i++)
			if (MV_FROM(legal[i]) == sel_sq && MV_TO(legal[i]) == sq) {
				if (MV_PROMO(legal[i]) && !promo) {
					promo = ask_promotion();
					if (promo < 0) return -1;
					if (!promo) { sel_sq = -1; return 0; }
				}
				if (!MV_PROMO(legal[i]) || MV_PROMO(legal[i]) == promo) { m = legal[i]; break; }
			}
		if (m) do_move(m);
		return 0;
	}
	sel_sq = -1;
	if (pos.b[sq] * user_side > 0)
		for (i = 0; i < nlegal; i++)
			if (MV_FROM(legal[i]) == sq) { sel_sq = sq; break; }
	return 0;
}

/* returns 0 = continue, 1 = new game, 2 = leave, -1 = quit */
static int game_menu(int over) {
	static const char *const items[] = {
		"Продолжить", "Отменить ход", "Подсказка", "Новая игра", "Выйти в меню"
	};
	static const char *const items_over[] = { "Новая игра", "Отменить ход", "Выйти в меню" };
	int r;
	if (over) {
		r = pick("Партия окончена", items_over, 3, 0, NULL);
		if (r == -2) return -1;
		if (r == 0) return 1;
		if (r == 2 || r < 0) return r < 0 ? 0 : 2;
		r = 1;
	} else {
		r = pick("Меню", items, 5, 0, NULL);
		if (r == -2) return -1;
	}
	switch (r) {
	case 1:
		/* take back the computer's reply and the user's move */
		undo_move();
		if (pos.side != user_side) undo_move();
		return 0;
	case 2: {
		uint32_t s = seed;
		draw_screen("Ищу хороший ход...", "");
		hint_move = ce_think(&pos, ce_nlevels - 1, &s, NULL);
		sel_sq = -1;
		return 0;
	}
	case 3: return 1;
	case 4: return 2;
	}
	return 0;
}

int chess_main(void) {
	sq_size = scr_w / 8;
	if (sq_size * 8 > (int)scr_h - 60) sq_size = (scr_h - 60) / 8;
	board_x = (scr_w - sq_size * 8) / 2;
	board_y = FONT_H + 4;
	seed = sys_timer_ms() | 1;

	for (;;) {
		int r = setup_game();
		if (r == -2) return -1;
		if (r < 0) return 0;

		for (;;) {
			int st = ce_status(&pos), key, dir_f = 0, dir_r = 0;
			if (st == ST_PLAYING && pos.side != user_side) {
				computer_move();
				continue;
			}
			draw_screen(status_text(st), st == ST_PLAYING ?
					"5-ход  Л-отмена  П-меню" : "П.софт - меню партии");
			key = wait_key();
			if (key < 0) return -1;
			switch (key) {
			case KEYPAD_UP: case KEYPAD_2: dir_r = 1; break;
			case KEYPAD_DOWN: case KEYPAD_8: dir_r = -1; break;
			case KEYPAD_LEFT: case KEYPAD_4: dir_f = -1; break;
			case KEYPAD_RIGHT: case KEYPAD_6: dir_f = 1; break;
			case KEYPAD_CENTER: case KEYPAD_5:
				if (st == ST_PLAYING && select_square() < 0) return -1;
				break;
			case KEYPAD_LSOFT:
				undo_move();
				if (pos.side != user_side) undo_move();
				break;
			case KEYPAD_RSOFT:
				r = game_menu(st != ST_PLAYING);
				if (r < 0) return -1;
				if (r == 2) return 0;
				if (r == 1) goto new_game;
				break;
			}
			/* the board is flipped when playing black */
			if (user_side < 0) { dir_f = -dir_f; dir_r = -dir_r; }
			cur_file = (cur_file + dir_f + 8) & 7;
			cur_rank = (cur_rank + dir_r + 8) & 7;
		}
new_game:;
	}
}
