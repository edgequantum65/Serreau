#include "chess_engine.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    init_engine();

    // Test move_to_algebraic on starting position
    Position pos;
    set_starting_position(&pos);

    // Test e2e4
    Move e4 = {E2, E4, DOUBLE_PAWN_PUSH, EMPTY};
    char alg[10];
    move_to_algebraic(&pos, e4, alg);
    printf("e2e4 -> %s\n", alg);
    make_move(&pos, e4);

    // Test e7e5
    Move e5 = {E7, E5, DOUBLE_PAWN_PUSH, EMPTY};
    move_to_algebraic(&pos, e5, alg);
    printf("e7e5 -> %s\n", alg);
    make_move(&pos, e5);

    // Test Ng1f3
    Move nf3 = {G1, F3, QUIET, EMPTY};
    move_to_algebraic(&pos, nf3, alg);
    printf("Ng1f3 -> %s\n", alg);
    make_move(&pos, nf3);

    // Test Nb8c6
    Move nc6 = {B8, C6, QUIET, EMPTY};
    move_to_algebraic(&pos, nc6, alg);
    printf("Nb8c6 -> %s\n", alg);
    make_move(&pos, nc6);

    // Test Bf1b5
    Move bb5 = {F1, B5, QUIET, EMPTY};
    move_to_algebraic(&pos, bb5, alg);
    printf("Bf1b5 -> %s\n", alg);

    // Test export_pgn
    Move moves[] = {e4, e5, nf3, nc6, bb5};
    export_pgn(moves, 5, "*", "White", "Black", "Test", "test_pgn.pgn");
    printf("PGN written to test_pgn.pgn\n");

    return 0;
}
