#include "chess_engine.h"
#include "magic.h"
#include "nnue.h"
#include <stdio.h>

int main(void) {
    init_bitboards();
    init_zobrist();
    init_magic();
    nnue_init();
    
    Position pos;
    set_fen(&pos, "r4r1k/1ppb1pp1/3p1q1p/4p2N/pnPbP2n/2Q4P/PP1PBPPR/R1B1K1N1 b Q - 5 18");
    
    printf("=== Position FEN ===\n");
    print_position(&pos);
    
    printf("\n=== Queen on f6 (square 45) ===\n");
    uint64_t queen_bb = pos.piece[BLACK][QUEEN];
    printf("Black queen BB: 0x%016llx\n", (unsigned long long)queen_bb);
    printf("Queen on f6: %s\n", (queen_bb & (1ULL << 45)) ? "YES" : "NO");
    
    printf("\n=== Queen attacks from f6 ===\n");
    uint64_t occ = pos.occupancy[2];
    uint64_t attacks = queen_attacks(45, occ);
    printf("f2 (13) in attacks: %s\n", (attacks & (1ULL << 13)) ? "YES" : "NO");
    
    printf("\n=== All pseudo-legal moves for Black ===\n");
    MoveList moves;
    generate_all_moves(&pos, &moves);
    printf("Total moves generated: %d\n", moves.count);
    
    bool found_f6f2 = false;
    for (int i = 0; i < moves.count; i++) {
        char from_str[3], to_str[3];
        square_to_algebraic(moves.moves[i].from, from_str);
        square_to_algebraic(moves.moves[i].to, to_str);
        printf("  %s%s (type=%d, promo=%d)\n", from_str, to_str, moves.moves[i].type, moves.moves[i].promo_piece);
        if (moves.moves[i].from == 45 && moves.moves[i].to == 13) {
            found_f6f2 = true;
        }
    }
    printf("f6f2 found: %s\n", found_f6f2 ? "YES" : "NO");
    
    // Also check rook attacks only
    printf("\n=== Rook attacks from f6 ===\n");
    attacks = rook_attacks(45, occ);
    printf("f2 (13) in rook attacks: %s\n", (attacks & (1ULL << 13)) ? "YES" : "NO");
    printf("All rook targets from f6:\n");
    uint64_t bb = attacks;
    while (bb) {
        Square sq = pop_lsb(&bb);
        char sq_str[3];
        square_to_algebraic(sq, sq_str);
        printf("  %s\n", sq_str);
    }
    
    // Check bishop attacks from f6
    printf("\n=== Bishop attacks from f6 ===\n");
    attacks = bishop_attacks(45, occ);
    printf("f2 (13) in bishop attacks: %s\n", (attacks & (1ULL << 13)) ? "YES" : "NO");
    bb = attacks;
    while (bb) {
        Square sq = pop_lsb(&bb);
        char sq_str[3];
        square_to_algebraic(sq, sq_str);
        printf("  %s\n", sq_str);
    }
    
    // Check occupancy details
    printf("\n=== Occupancy ===\n");
    printf("f-file occupancy:\n");
    for (int r = 0; r < 8; r++) {
        Square sq = (Square)(5 + r * 8); // f-file
        if (occ & (1ULL << sq)) {
            // Find which piece
            for (int c = 0; c < 2; c++) {
                for (int p = PAWN; p <= KING; p++) {
                    if (pos.piece[c][p] & (1ULL << sq)) {
                        char sq_str[3];
                        square_to_algebraic(sq, sq_str);
                        printf("  %s: %s %s\n", sq_str, c == WHITE ? "White" : "Black", piece_name(p));
                    }
                }
            }
        }
    }
    
    // Check f2 occupancy
    printf("\nf2 (13) occupancy: %s\n", (occ & (1ULL << 13)) ? "OCCUPIED" : "EMPTY");
    for (int c = 0; c < 2; c++) {
        for (int p = PAWN; p <= KING; p++) {
            if (pos.piece[c][p] & (1ULL << 13)) {
                printf("f2 has: %s %s\n", c == WHITE ? "White" : "Black", piece_name(p));
            }
        }
    }
    
    return 0;
}
