#include <string.h>
#include "chess_engine.h"

#define INF 32000
#define MATE 30000
#define MAX_PLY 64

/* strength levels, the ratings are rough estimates */
const level_t ce_levels[] = {
	{ "Новичок",     300, 1, 0, 150, 30, 900,  20000 },
	{ "Начинающий",  450, 1, 1, 120, 25, 700,  20000 },
	{ "Ученик",      600, 2, 1, 100, 18, 600,  20000 },
	{ "Любитель",    700, 2, 1,  80, 13, 500,  30000 },
	{ "Игрок",       800, 2, 1,  60,  9, 400,  30000 },
	{ "Крепкий",     900, 3, 1,  60,  8, 400,  60000 },
	{ "Опытный",    1000, 3, 1,  40,  5, 300,  60000 },
	{ "Разрядник",  1150, 3, 1,  25,  3, 250,  80000 },
	{ "Сильный",    1300, 4, 1,  30,  2, 250, 120000 },
	{ "Кандидат",   1400, 4, 1,  10,  1, 150, 120000 },
	{ "Мастер",     1600, 6, 1,  10,  0,   0, 150000 },
};
const int ce_nlevels = sizeof(ce_levels) / sizeof(*ce_levels);

long ce_nodes;

static const int8_t knight_d[8] = { 33, 31, 18, 14, -33, -31, -18, -14 };
static const int8_t king_d[8] = { 1, -1, 16, -16, 17, 15, -17, -15 };
static const int8_t bishop_d[4] = { 17, 15, -17, -15 };
static const int8_t rook_d[4] = { 1, -1, 16, -16 };

static const int piece_val[7] = { 0, 100, 320, 330, 500, 900, 0 };

/* zobrist keys for repetition detection */
static uint32_t zob_piece[13][128], zob_castle[16], zob_ep[8], zob_side;
static int zob_ready;

static uint32_t xorshift(uint32_t *s) {
	uint32_t x = *s;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	return *s = x;
}

static void zob_init(void) {
	uint32_t s = 0x9e3779b9;
	int i, j;
	if (zob_ready) return;
	for (i = 0; i < 13; i++)
		for (j = 0; j < 128; j++) zob_piece[i][j] = xorshift(&s);
	for (i = 0; i < 16; i++) zob_castle[i] = xorshift(&s);
	for (i = 0; i < 8; i++) zob_ep[i] = xorshift(&s);
	zob_side = xorshift(&s);
	zob_ready = 1;
}

#define ZP(pc, sq) zob_piece[(pc) + 6][sq]

static uint32_t full_hash(const pos_t *p) {
	uint32_t h = zob_castle[p->castle];
	int sq;
	for (sq = 0; sq < 128; sq++)
		if (!(sq & 0x88) && p->b[sq]) h ^= ZP(p->b[sq], sq);
	if (p->ep >= 0) h ^= zob_ep[SQ_FILE(p->ep)];
	if (p->side < 0) h ^= zob_side;
	return h;
}

int ce_set_fen(pos_t *p, const char *fen) {
	static const char pieces[] = "pnbrqk";
	int rank = 7, file = 0;
	zob_init();
	memset(p, 0, sizeof(*p));
	p->ep = -1;
	for (; *fen && *fen != ' '; fen++) {
		const char *s;
		if (*fen == '/') { rank--; file = 0; continue; }
		if (*fen >= '1' && *fen <= '8') { file += *fen - '0'; continue; }
		s = strchr(pieces, *fen | 0x20);
		if (!s || file > 7 || rank < 0) return -1;
		p->b[SQ(file, rank)] = (s - pieces + 1) * (*fen & 0x20 ? -1 : 1);
		if ((*fen | 0x20) == 'k') p->king[*fen & 0x20 ? 1 : 0] = SQ(file, rank);
		file++;
	}
	while (*fen == ' ') fen++;
	p->side = *fen == 'b' ? -1 : 1;
	if (*fen) fen++;
	while (*fen == ' ') fen++;
	for (; *fen && *fen != ' '; fen++) {
		if (*fen == 'K') p->castle |= 1;
		if (*fen == 'Q') p->castle |= 2;
		if (*fen == 'k') p->castle |= 4;
		if (*fen == 'q') p->castle |= 8;
	}
	while (*fen == ' ') fen++;
	if (*fen >= 'a' && *fen <= 'h' && fen[1] >= '1' && fen[1] <= '8')
		p->ep = SQ(*fen - 'a', fen[1] - '1');
	p->fullmove = 1;
	p->hash = full_hash(p);
	return 0;
}

void ce_init(pos_t *p) {
	ce_set_fen(p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

/* is square attacked by side "by" */
static int attacked(const pos_t *p, int sq, int by) {
	const int8_t *b = p->b;
	int i, t, pd = by > 0 ? -16 : 16;

	t = sq + pd - 1; if (!(t & 0x88) && b[t] == PAWN * by) return 1;
	t = sq + pd + 1; if (!(t & 0x88) && b[t] == PAWN * by) return 1;
	for (i = 0; i < 8; i++) {
		t = sq + knight_d[i];
		if (!(t & 0x88) && b[t] == KNIGHT * by) return 1;
		t = sq + king_d[i];
		if (!(t & 0x88) && b[t] == KING * by) return 1;
	}
	for (i = 0; i < 4; i++) {
		for (t = sq + bishop_d[i]; !(t & 0x88); t += bishop_d[i]) {
			if (!b[t]) continue;
			if (b[t] == BISHOP * by || b[t] == QUEEN * by) return 1;
			break;
		}
		for (t = sq + rook_d[i]; !(t & 0x88); t += rook_d[i]) {
			if (!b[t]) continue;
			if (b[t] == ROOK * by || b[t] == QUEEN * by) return 1;
			break;
		}
	}
	return 0;
}

int ce_in_check(const pos_t *p) {
	return attacked(p, p->king[p->side < 0], -p->side);
}

static int add_pawn_move(move_t *list, int n, int from, int to, int flags, int promo_rank, int quiet) {
	if (SQ_RANK(to) == promo_rank) {
		list[n++] = MV(from, to, QUEEN, flags);
		if (quiet) {
			list[n++] = MV(from, to, KNIGHT, flags);
			list[n++] = MV(from, to, ROOK, flags);
			list[n++] = MV(from, to, BISHOP, flags);
		}
	} else if (quiet || flags & MF_CAPTURE) {
		list[n++] = MV(from, to, 0, flags);
	}
	return n;
}

/* pseudo-legal moves, quiet = 0 gives captures and queen promotions only */
static int gen_pseudo(const pos_t *p, move_t *list, int quiet) {
	const int8_t *b = p->b;
	int n = 0, s = p->side, sq, i, t;

	for (sq = 0; sq < 128; sq++) {
		int pc;
		if (sq & 0x88) { sq += 7; continue; }
		pc = b[sq] * s;
		if (pc <= 0) continue;
		switch (pc) {
		case PAWN: {
			int fwd = 16 * s, start = s > 0 ? 1 : 6, promo = s > 0 ? 7 : 0;
			t = sq + fwd;
			if (!b[t]) {
				n = add_pawn_move(list, n, sq, t, 0, promo, quiet);
				if (quiet && SQ_RANK(sq) == start && !b[t + fwd])
					list[n++] = MV(sq, t + fwd, 0, MF_DOUBLE);
			}
			for (i = -1; i <= 1; i += 2) {
				t = sq + fwd + i;
				if (t & 0x88) continue;
				if (b[t] * s < 0)
					n = add_pawn_move(list, n, sq, t, MF_CAPTURE, promo, quiet);
				else if (t == p->ep)
					list[n++] = MV(sq, t, 0, MF_CAPTURE | MF_EP);
			}
			break;
		}
		case KNIGHT: case KING: {
			const int8_t *d = pc == KNIGHT ? knight_d : king_d;
			for (i = 0; i < 8; i++) {
				t = sq + d[i];
				if (t & 0x88) continue;
				if (!b[t]) { if (quiet) list[n++] = MV(sq, t, 0, 0); }
				else if (b[t] * s < 0) list[n++] = MV(sq, t, 0, MF_CAPTURE);
			}
			break;
		}
		default: {
			int k;
			for (k = 0; k < 8; k++) {
				int d;
				if (k < 4) { if (pc == ROOK) continue; d = bishop_d[k]; }
				else { if (pc == BISHOP) continue; d = rook_d[k - 4]; }
				for (t = sq + d; !(t & 0x88); t += d) {
					if (!b[t]) { if (quiet) list[n++] = MV(sq, t, 0, 0); continue; }
					if (b[t] * s < 0) list[n++] = MV(sq, t, 0, MF_CAPTURE);
					break;
				}
			}
		}
		}
	}

	if (quiet) {
		int r = s > 0 ? 0 : 112, kr = s > 0 ? 1 : 4, qr = s > 0 ? 2 : 8;
		if (p->castle & kr && b[r + 4] == KING * s && b[r + 7] == ROOK * s &&
				!b[r + 5] && !b[r + 6] &&
				!attacked(p, r + 4, -s) && !attacked(p, r + 5, -s) && !attacked(p, r + 6, -s))
			list[n++] = MV(r + 4, r + 6, 0, MF_CASTLE);
		if (p->castle & qr && b[r + 4] == KING * s && b[r] == ROOK * s &&
				!b[r + 1] && !b[r + 2] && !b[r + 3] &&
				!attacked(p, r + 4, -s) && !attacked(p, r + 3, -s) && !attacked(p, r + 2, -s))
			list[n++] = MV(r + 4, r + 2, 0, MF_CASTLE);
	}
	return n;
}

static uint8_t castle_mask(int sq) {
	switch (sq) {
	case 0: return ~2;
	case 7: return ~1;
	case 4: return ~3;
	case 112: return ~8;
	case 119: return ~4;
	case 116: return ~12;
	}
	return 0xff;
}

void ce_make(pos_t *p, move_t m, undo_t *u) {
	int from = MV_FROM(m), to = MV_TO(m), fl = MV_FLAGS(m), promo = MV_PROMO(m);
	int s = p->side, pc = p->b[from], newpc = promo ? promo * s : pc;
	uint32_t h = p->hash;

	u->m = m;
	u->captured = p->b[to];
	u->castle = p->castle;
	u->ep = p->ep;
	u->half = p->half;
	u->hash = h;
	if (p->nhist < MAX_HIST) p->hist[p->nhist] = h;
	p->nhist++;

	h ^= ZP(pc, from) ^ ZP(newpc, to);
	if (fl & MF_EP) {
		int cap = to - 16 * s;
		u->captured = p->b[cap];
		h ^= ZP(p->b[cap], cap);
		p->b[cap] = 0;
	} else if (u->captured) {
		h ^= ZP(u->captured, to);
	}
	p->b[to] = newpc;
	p->b[from] = 0;

	if (fl & MF_CASTLE) {
		int rf = to > from ? from + 3 : from - 4, rt = to > from ? from + 1 : from - 1;
		p->b[rt] = p->b[rf];
		p->b[rf] = 0;
		h ^= ZP(ROOK * s, rf) ^ ZP(ROOK * s, rt);
	}
	if (pc == KING * s) p->king[s < 0] = to;

	h ^= zob_castle[p->castle];
	p->castle &= castle_mask(from) & castle_mask(to);
	h ^= zob_castle[p->castle];

	if (p->ep >= 0) h ^= zob_ep[SQ_FILE(p->ep)];
	p->ep = fl & MF_DOUBLE ? from + 16 * s : -1;
	if (p->ep >= 0) h ^= zob_ep[SQ_FILE(p->ep)];

	p->half = pc == PAWN * s || u->captured ? 0 : p->half + 1;
	if (s < 0) p->fullmove++;
	p->side = -s;
	p->hash = h ^ zob_side;
}

void ce_unmake(pos_t *p, const undo_t *u) {
	move_t m = u->m;
	int from = MV_FROM(m), to = MV_TO(m), fl = MV_FLAGS(m);
	int s = -p->side, pc = MV_PROMO(m) ? PAWN * s : p->b[to];

	p->side = s;
	p->b[from] = pc;
	if (fl & MF_EP) {
		p->b[to] = 0;
		p->b[to - 16 * s] = u->captured;
	} else {
		p->b[to] = u->captured;
	}
	if (fl & MF_CASTLE) {
		int rf = to > from ? from + 3 : from - 4, rt = to > from ? from + 1 : from - 1;
		p->b[rf] = p->b[rt];
		p->b[rt] = 0;
	}
	if (pc == KING * s) p->king[s < 0] = from;
	p->castle = u->castle;
	p->ep = u->ep;
	p->half = u->half;
	p->hash = u->hash;
	p->nhist--;
	if (s < 0) p->fullmove--;
}

int ce_gen_legal(pos_t *p, move_t *list) {
	move_t tmp[MAX_MOVES];
	int i, n = gen_pseudo(p, tmp, 1), k = 0;
	for (i = 0; i < n; i++) {
		undo_t u;
		ce_make(p, tmp[i], &u);
		if (!attacked(p, p->king[p->side > 0], p->side)) list[k++] = tmp[i];
		ce_unmake(p, &u);
	}
	return k;
}

unsigned long ce_perft(pos_t *p, int depth) {
	move_t list[MAX_MOVES];
	unsigned long sum = 0;
	int i, n = ce_gen_legal(p, list);
	if (depth <= 1) return n;
	for (i = 0; i < n; i++) {
		undo_t u;
		ce_make(p, list[i], &u);
		sum += ce_perft(p, depth - 1);
		ce_unmake(p, &u);
	}
	return sum;
}

/* number of earlier occurrences of the current position */
static int repetitions(const pos_t *p) {
	int i, n = 0, lim = p->nhist - p->half;
	if (lim < 0) lim = 0;
	for (i = p->nhist - 2; i >= lim; i -= 2)
		if (i < MAX_HIST && p->hist[i] == p->hash) n++;
	return n;
}

static int insufficient_material(const pos_t *p) {
	int sq, minors = 0;
	for (sq = 0; sq < 128; sq++) {
		int pc;
		if (sq & 0x88) { sq += 7; continue; }
		pc = p->b[sq] < 0 ? -p->b[sq] : p->b[sq];
		if (pc == PAWN || pc == ROOK || pc == QUEEN) return 0;
		if (pc == KNIGHT || pc == BISHOP) minors++;
	}
	return minors <= 1;
}

int ce_status(pos_t *p) {
	move_t list[MAX_MOVES];
	if (!ce_gen_legal(p, list)) return ce_in_check(p) ? ST_MATE : ST_STALEMATE;
	if (p->half >= 100) return ST_DRAW_50;
	if (repetitions(p) >= 2) return ST_DRAW_REP;
	if (insufficient_material(p)) return ST_DRAW_MATERIAL;
	return ST_PLAYING;
}

/* piece-square tables, a8..h8 first, from white's point of view */
static const int8_t pst[7][64] = {
	{ 0 },
	{ /* pawn */
	  0,  0,  0,  0,  0,  0,  0,  0,
	 50, 50, 50, 50, 50, 50, 50, 50,
	 10, 10, 20, 30, 30, 20, 10, 10,
	  5,  5, 10, 25, 25, 10,  5,  5,
	  0,  0,  0, 20, 20,  0,  0,  0,
	  5, -5,-10,  0,  0,-10, -5,  5,
	  5, 10, 10,-20,-20, 10, 10,  5,
	  0,  0,  0,  0,  0,  0,  0,  0 },
	{ /* knight */
	-50,-40,-30,-30,-30,-30,-40,-50,
	-40,-20,  0,  0,  0,  0,-20,-40,
	-30,  0, 10, 15, 15, 10,  0,-30,
	-30,  5, 15, 20, 20, 15,  5,-30,
	-30,  0, 15, 20, 20, 15,  0,-30,
	-30,  5, 10, 15, 15, 10,  5,-30,
	-40,-20,  0,  5,  5,  0,-20,-40,
	-50,-40,-30,-30,-30,-30,-40,-50 },
	{ /* bishop */
	-20,-10,-10,-10,-10,-10,-10,-20,
	-10,  0,  0,  0,  0,  0,  0,-10,
	-10,  0,  5, 10, 10,  5,  0,-10,
	-10,  5,  5, 10, 10,  5,  5,-10,
	-10,  0, 10, 10, 10, 10,  0,-10,
	-10, 10, 10, 10, 10, 10, 10,-10,
	-10,  5,  0,  0,  0,  0,  5,-10,
	-20,-10,-10,-10,-10,-10,-10,-20 },
	{ /* rook */
	  0,  0,  0,  0,  0,  0,  0,  0,
	  5, 10, 10, 10, 10, 10, 10,  5,
	 -5,  0,  0,  0,  0,  0,  0, -5,
	 -5,  0,  0,  0,  0,  0,  0, -5,
	 -5,  0,  0,  0,  0,  0,  0, -5,
	 -5,  0,  0,  0,  0,  0,  0, -5,
	 -5,  0,  0,  0,  0,  0,  0, -5,
	  0,  0,  0,  5,  5,  0,  0,  0 },
	{ /* queen */
	-20,-10,-10, -5, -5,-10,-10,-20,
	-10,  0,  0,  0,  0,  0,  0,-10,
	-10,  0,  5,  5,  5,  5,  0,-10,
	 -5,  0,  5,  5,  5,  5,  0, -5,
	  0,  0,  5,  5,  5,  5,  0, -5,
	-10,  5,  5,  5,  5,  5,  0,-10,
	-10,  0,  5,  0,  0,  0,  0,-10,
	-20,-10,-10, -5, -5,-10,-10,-20 },
	{ /* king, middlegame */
	-30,-40,-40,-50,-50,-40,-40,-30,
	-30,-40,-40,-50,-50,-40,-40,-30,
	-30,-40,-40,-50,-50,-40,-40,-30,
	-30,-40,-40,-50,-50,-40,-40,-30,
	-20,-30,-30,-40,-40,-30,-30,-20,
	-10,-20,-20,-20,-20,-20,-20,-10,
	 20, 20,  0,  0,  0,  0, 20, 20,
	 20, 30, 10,  0,  0, 10, 30, 20 },
};

static const int8_t king_end[64] = {
	-50,-40,-30,-20,-20,-30,-40,-50,
	-30,-20,-10,  0,  0,-10,-20,-30,
	-30,-10, 20, 30, 30, 20,-10,-30,
	-30,-10, 30, 40, 40, 30,-10,-30,
	-30,-10, 30, 40, 40, 30,-10,-30,
	-30,-10, 20, 30, 30, 20,-10,-30,
	-30,-30,  0,  0,  0,  0,-30,-30,
	-50,-30,-30,-30,-30,-30,-30,-50,
};

static int center_dist(int sq) {
	int f = SQ_FILE(sq), r = SQ_RANK(sq);
	int df = f < 4 ? 3 - f : f - 4, dr = r < 4 ? 3 - r : r - 4;
	return df + dr;
}

int ce_eval(const pos_t *p) {
	int sq, mat[2] = { 0, 0 }, pos[2] = { 0, 0 }, kmid[2] = { 0, 0 }, kend[2] = { 0, 0 };
	int bishops[2] = { 0, 0 }, phase = 0, score;

	for (sq = 0; sq < 128; sq++) {
		int pc, c, idx;
		if (sq & 0x88) { sq += 7; continue; }
		if (!(pc = p->b[sq])) continue;
		c = pc < 0;
		if (c) pc = -pc;
		/* white reads the table upside down */
		idx = c ? SQ_RANK(sq) * 8 + SQ_FILE(sq) : (7 - SQ_RANK(sq)) * 8 + SQ_FILE(sq);
		mat[c] += piece_val[pc];
		if (pc == KING) {
			kmid[c] = pst[KING][idx];
			kend[c] = king_end[idx];
			continue;
		}
		pos[c] += pst[pc][idx];
		if (pc == BISHOP) bishops[c]++;
		if (pc == KNIGHT || pc == BISHOP) phase += 1;
		else if (pc == ROOK) phase += 2;
		else if (pc == QUEEN) phase += 4;
	}
	if (phase > 24) phase = 24;
	score = mat[0] - mat[1] + pos[0] - pos[1];
	score += ((kmid[0] - kmid[1]) * phase + (kend[0] - kend[1]) * (24 - phase)) / 24;
	if (bishops[0] >= 2) score += 30;
	if (bishops[1] >= 2) score -= 30;

	/* endgame: drive the lonely king to the edge to actually checkmate */
	if (phase <= 8 && (score > 300 || score < -300)) {
		int win = score > 0 ? 0 : 1;
		int wk = p->king[win], lk = p->king[!win];
		int dist = (SQ_FILE(wk) > SQ_FILE(lk) ? SQ_FILE(wk) - SQ_FILE(lk) : SQ_FILE(lk) - SQ_FILE(wk)) +
				(SQ_RANK(wk) > SQ_RANK(lk) ? SQ_RANK(wk) - SQ_RANK(lk) : SQ_RANK(lk) - SQ_RANK(wk));
		int bonus = center_dist(lk) * 10 + (14 - dist) * 4;
		score += win ? -bonus : bonus;
	}
	return score * p->side;
}

/* ---- search ---- */

static long node_limit;
static int stop_search, use_quiesce;
static move_t killers[MAX_PLY][2];

static int move_order(const pos_t *p, move_t m, int ply) {
	int fl = MV_FLAGS(m);
	if (fl & MF_CAPTURE) {
		int victim = fl & MF_EP ? PAWN : p->b[MV_TO(m)];
		int attacker = p->b[MV_FROM(m)];
		if (victim < 0) victim = -victim;
		if (attacker < 0) attacker = -attacker;
		return 10000 + victim * 10 - attacker + (MV_PROMO(m) ? 900 : 0);
	}
	if (MV_PROMO(m)) return 9500 + MV_PROMO(m);
	if (ply < MAX_PLY) {
		if (m == killers[ply][0]) return 9000;
		if (m == killers[ply][1]) return 8000;
	}
	return 0;
}

static void sort_moves(const pos_t *p, move_t *list, int n, int ply) {
	int score[MAX_MOVES], i, j;
	for (i = 0; i < n; i++) score[i] = move_order(p, list[i], ply);
	for (i = 1; i < n; i++) {
		move_t m = list[i]; int s = score[i];
		for (j = i - 1; j >= 0 && score[j] < s; j--) {
			list[j + 1] = list[j];
			score[j + 1] = score[j];
		}
		list[j + 1] = m;
		score[j + 1] = s;
	}
}

static int quiesce(pos_t *p, int alpha, int beta, int ply) {
	move_t list[MAX_MOVES];
	int i, n, stand;

	ce_nodes++;
	if (ce_nodes >= node_limit) stop_search = 1;
	stand = ce_eval(p);
	if (stand >= beta) return stand;
	if (stand > alpha) alpha = stand;
	if (ply >= MAX_PLY) return stand;

	n = gen_pseudo(p, list, 0);
	sort_moves(p, list, n, MAX_PLY);
	for (i = 0; i < n; i++) {
		undo_t u; int score;
		ce_make(p, list[i], &u);
		if (attacked(p, p->king[p->side > 0], p->side)) {
			ce_unmake(p, &u);
			continue;
		}
		score = -quiesce(p, -beta, -alpha, ply + 1);
		ce_unmake(p, &u);
		if (stop_search) return 0;
		if (score >= beta) return score;
		if (score > alpha) alpha = score;
	}
	return alpha;
}

static int search(pos_t *p, int depth, int alpha, int beta, int ply) {
	move_t list[MAX_MOVES];
	int i, n, legal = 0, best = -INF, in_check;

	if (ply && (p->half >= 100 || repetitions(p))) return 0;
	in_check = ce_in_check(p);
	if (in_check && ply < MAX_PLY - 8) depth++;
	if (depth <= 0)
		return use_quiesce ? quiesce(p, alpha, beta, ply) : (ce_nodes++, ce_eval(p));

	ce_nodes++;
	if (ce_nodes >= node_limit) stop_search = 1;

	n = gen_pseudo(p, list, 1);
	sort_moves(p, list, n, ply);
	for (i = 0; i < n; i++) {
		undo_t u; int score;
		ce_make(p, list[i], &u);
		if (attacked(p, p->king[p->side > 0], p->side)) {
			ce_unmake(p, &u);
			continue;
		}
		legal++;
		score = -search(p, depth - 1, -beta, -alpha, ply + 1);
		ce_unmake(p, &u);
		if (stop_search) return 0;
		if (score > best) best = score;
		if (score > alpha) alpha = score;
		if (score >= beta) {
			if (!(MV_FLAGS(list[i]) & MF_CAPTURE) && ply < MAX_PLY &&
					killers[ply][0] != list[i]) {
				killers[ply][1] = killers[ply][0];
				killers[ply][0] = list[i];
			}
			return score;
		}
	}
	if (!legal) return in_check ? -MATE + ply : 0;
	return best;
}

move_t ce_think(pos_t *p, int level, uint32_t *seed, int *score_out) {
	const level_t *lv = &ce_levels[level];
	move_t list[MAX_MOVES];
	int score[MAX_MOVES], done[MAX_MOVES], n, i, d, best_i = 0;
	int full_window = lv->noise || lv->blunder;

	n = ce_gen_legal(p, list);
	if (!n) return 0;
	if (score_out) *score_out = 0;
	if (n == 1) return list[0];

	ce_nodes = 0;
	node_limit = lv->nodes;
	stop_search = 0;
	use_quiesce = lv->quiesce;
	memset(killers, 0, sizeof(killers));
	sort_moves(p, list, n, 0);
	for (i = 0; i < n; i++) score[i] = 0;

	/* iterative deepening, every root move gets its own score */
	for (d = 1; d <= lv->depth; d++) {
		int alpha = -INF;
		for (i = 0; i < n; i++) {
			undo_t u;
			ce_make(p, list[i], &u);
			if (full_window || i == 0) {
				done[i] = -search(p, d - 1, -INF, INF, 1);
			} else {
				/* plain alpha-beta: only the best move matters */
				done[i] = -search(p, d - 1, -INF, -alpha, 1);
			}
			ce_unmake(p, &u);
			if (stop_search) break;
			if (done[i] > alpha) alpha = done[i];
		}
		if (stop_search) {
			/* out of nodes: keep the previous depth, or what depth 1 managed */
			if (d == 1) {
				int k;
				for (k = 0; k < n; k++) score[k] = k < i ? done[k] : -INF;
			}
			break;
		}
		for (i = 0; i < n; i++) score[i] = done[i];
		/* best moves first for the next iteration */
		{
			int j;
			for (i = 1; i < n; i++) {
				move_t m = list[i]; int s = score[i];
				for (j = i - 1; j >= 0 && score[j] < s; j--) {
					list[j + 1] = list[j];
					score[j + 1] = score[j];
				}
				list[j + 1] = m;
				score[j + 1] = s;
			}
		}
		if (score[0] >= MATE - 100) break;
	}

	for (i = 1; i < n; i++)
		if (score[i] > score[best_i]) best_i = i;

	/* weaker levels: sometimes play a clearly worse move */
	if (score[best_i] < MATE - 100 || level == 0) {
		int best = score[best_i];
		if (lv->blunder && (int)(xorshift(seed) % 100) < lv->blunder) {
			int cand[MAX_MOVES], k = 0;
			for (i = 0; i < n; i++)
				if (score[i] < best - 50 && score[i] >= best - lv->blunder_max)
					cand[k++] = i;
			if (k) {
				best_i = cand[xorshift(seed) % k];
				if (score_out) *score_out = score[best_i];
				return list[best_i];
			}
		}
		if (lv->noise) {
			int top = -INF * 2;
			for (i = 0; i < n; i++) {
				int r = (int)(xorshift(seed) % (2 * lv->noise + 1)) - lv->noise;
				r += (int)(xorshift(seed) % (2 * lv->noise + 1)) - lv->noise;
				if (score[i] + r / 2 > top) { top = score[i] + r / 2; best_i = i; }
			}
		}
	}
	if (score_out) *score_out = score[best_i];
	return list[best_i];
}

void ce_move_str(move_t m, char *buf) {
	static const char promo[] = " nbrq";
	int from = MV_FROM(m), to = MV_TO(m);
	buf[0] = 'a' + SQ_FILE(from);
	buf[1] = '1' + SQ_RANK(from);
	buf[2] = 'a' + SQ_FILE(to);
	buf[3] = '1' + SQ_RANK(to);
	buf[4] = MV_PROMO(m) ? promo[MV_PROMO(m) - 1] : 0;
	buf[5] = 0;
}
