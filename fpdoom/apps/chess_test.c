/* Mac-only engine tests: perft correctness, speed and level matches */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "chess_engine.h"

static double now(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int perft_check(const char *fen, int depth, unsigned long expect) {
	pos_t p;
	unsigned long n;
	ce_set_fen(&p, fen);
	n = ce_perft(&p, depth);
	printf("perft %d = %lu %s\n", depth, n, n == expect ? "ok" : "FAIL");
	return n == expect;
}

/* plays a game between two levels, returns 1 white won, -1 black won, 0 draw */
static int play(int lw, int lb, uint32_t *seed, long *nodes, int *moves) {
	pos_t p;
	ce_init(&p);
	for (*moves = 0; *moves < 300; (*moves)++) {
		undo_t u;
		move_t m;
		int st = ce_status(&p);
		if (st == ST_MATE) return -p.side;
		if (st != ST_PLAYING) return 0;
		m = ce_think(&p, p.side > 0 ? lw : lb, seed, NULL);
		*nodes += ce_nodes;
		ce_make(&p, m, &u);
		p.nhist = p.nhist; /* history is kept in p */
	}
	return 0;
}

int main(int argc, char **argv) {
	int ok = 1;
	if (argc < 2 || !strcmp(argv[1], "perft")) {
		const char *start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
		const char *kiwi = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
		const char *pos3 = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
		const char *pos4 = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
		ok &= perft_check(start, 4, 197281);
		ok &= perft_check(kiwi, 3, 97862);
		ok &= perft_check(pos3, 5, 674624);
		ok &= perft_check(pos4, 4, 422333);
		printf(ok ? "all perft ok\n" : "PERFT FAILED\n");
	}
	if (argc >= 2 && !strcmp(argv[1], "speed")) {
		pos_t p;
		uint32_t seed = 1;
		int l;
		for (l = 0; l < ce_nlevels; l++) {
			double t0 = now();
			char buf[8];
			move_t m;
			ce_init(&p);
			m = ce_think(&p, l, &seed, NULL);
			ce_move_str(m, buf);
			printf("level %d (%s): %s, %ld nodes, %.3f s\n", l, ce_levels[l].name,
					buf, ce_nodes, now() - t0);
		}
	}
	if (argc >= 4 && !strcmp(argv[1], "match")) {
		int a = atoi(argv[2]), b = atoi(argv[3]), games = argc > 4 ? atoi(argv[4]) : 10;
		int g, wa = 0, wb = 0, dr = 0, moves;
		long nodes = 0, total_moves = 0;
		uint32_t seed = 12345;
		double t0 = now();
		for (g = 0; g < games; g++) {
			/* alternate colors */
			int r = g & 1 ? -play(b, a, &seed, &nodes, &moves) : play(a, b, &seed, &nodes, &moves);
			total_moves += moves;
			if (r > 0) wa++; else if (r < 0) wb++; else dr++;
		}
		printf("level %d vs %d: +%d -%d =%d, avg %.0f plies, %.0f nodes/move, %.1f s\n",
				a, b, wa, wb, dr, (double)total_moves / games,
				(double)nodes / total_moves, now() - t0);
	}
	return !ok;
}
