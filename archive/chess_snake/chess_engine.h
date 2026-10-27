#ifndef CHESS_ENGINE_H
#define CHESS_ENGINE_H

#include <stdint.h>

/* move: from | to << 8 | promo << 16 | flags << 24, squares are 0x88 */
typedef uint32_t move_t;
#define MV(from, to, promo, flags) \
	((move_t)(from) | (move_t)(to) << 8 | (move_t)(promo) << 16 | (move_t)(flags) << 24)
#define MV_FROM(m) ((m) & 0x7f)
#define MV_TO(m) ((m) >> 8 & 0x7f)
#define MV_PROMO(m) ((m) >> 16 & 7)
#define MV_FLAGS(m) ((m) >> 24)

enum { EMPTY, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum { MF_CAPTURE = 1, MF_EP = 2, MF_CASTLE = 4, MF_DOUBLE = 8 };

#define SQ(file, rank) ((rank) * 16 + (file))
#define SQ_FILE(sq) ((sq) & 7)
#define SQ_RANK(sq) ((sq) >> 4)

#define MAX_MOVES 256
#define MAX_HIST 1024

typedef struct {
	int8_t b[128];   /* +piece for white, -piece for black */
	int8_t side;     /* 1 = white, -1 = black */
	uint8_t castle;  /* 1 = K, 2 = Q, 4 = k, 8 = q */
	int8_t ep;       /* en passant target square or -1 */
	uint8_t king[2]; /* [0] = white, [1] = black */
	int half, fullmove;
	uint32_t hash;
	int nhist;
	uint32_t hist[MAX_HIST];
} pos_t;

typedef struct {
	move_t m;
	int8_t captured, ep;
	uint8_t castle;
	int half;
	uint32_t hash;
} undo_t;

enum {
	ST_PLAYING, ST_MATE, ST_STALEMATE,
	ST_DRAW_50, ST_DRAW_REP, ST_DRAW_MATERIAL
};

typedef struct {
	const char *name;
	int rating;      /* rough chess.com-like strength */
	int depth;       /* max search depth */
	int quiesce;     /* 0: doesn't see recaptures */
	int noise;       /* random +-centipawns added to move scores */
	int blunder;     /* chance in % to play a clearly worse move */
	int blunder_max; /* how much worse such a move can be */
	long nodes;      /* node budget per move */
} level_t;

extern const level_t ce_levels[];
extern const int ce_nlevels;
extern long ce_nodes;

void ce_init(pos_t *p);
int ce_set_fen(pos_t *p, const char *fen);
int ce_gen_legal(pos_t *p, move_t *list);
void ce_make(pos_t *p, move_t m, undo_t *u);
void ce_unmake(pos_t *p, const undo_t *u);
int ce_in_check(const pos_t *p);
int ce_status(pos_t *p);
int ce_eval(const pos_t *p); /* from the side to move */
move_t ce_think(pos_t *p, int level, uint32_t *seed, int *score);
void ce_move_str(move_t m, char *buf); /* "e2e4", "e7e8q" */
unsigned long ce_perft(pos_t *p, int depth);

#endif
