/* Perft: counts the leaf nodes of the legal move tree and compares them with
 * the published reference values (chessprogramming.org/Perft_Results).
 * Any difference means a move generation bug. */
#include "chess_engine.h"
#include "magic.h"
#include <stdio.h>
#include <string.h>

static uint64_t perft(Position *pos, int depth) {
    if (depth == 0) return 1;
    MoveList list;
    generate_all_moves(pos, &list);
    uint64_t nodes = 0;
    for (int i = 0; i < list.count; i++) {
        if (!is_legal_move(pos, list.moves[i])) continue;
        make_move(pos, list.moves[i]);
        nodes += perft(pos, depth - 1);
        unmake_move(pos, list.moves[i]);
    }
    return nodes;
}

typedef struct { const char *name, *fen; int depth; uint64_t expected; } Case;

static const Case CASES[] = {
    {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 1, 20},
    {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 2, 400},
    {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3, 8902},
    {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 197281},
    {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 1, 48},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 2, 2039},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97862},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603},
    {"position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624},
    {"position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333},
    {"position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487},
    {"position 6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594},
};

int main(void) {
    init_bitboards();
    init_zobrist();
    init_magic();
    int failed = 0;
    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        Position pos;
        memset(&pos, 0, sizeof pos);
        set_fen(&pos, CASES[i].fen);
        uint64_t got = perft(&pos, CASES[i].depth);
        int ok = got == CASES[i].expected;
        failed += !ok;
        printf("[%s] %-10s depth %d: expected %llu, got %llu\n", ok ? "ok  " : "FAIL",
               CASES[i].name, CASES[i].depth,
               (unsigned long long)CASES[i].expected, (unsigned long long)got);
    }
    printf("%d failure(s)\n", failed);
    return failed ? 1 : 0;
}
