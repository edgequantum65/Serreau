#include "chess_engine.h"
#include <stdio.h>
#include <string.h>

// ============================================================
// Constantes bitboards
// ============================================================

const uint64_t FILE_A = 0x0101010101010101ULL;
const uint64_t FILE_B = 0x0202020202020202ULL;
const uint64_t FILE_G = 0x4040404040404040ULL;
const uint64_t FILE_H = 0x8080808080808080ULL;

const uint64_t RANK_1 = 0x00000000000000FFULL;
const uint64_t RANK_2 = 0x000000000000FF00ULL;
const uint64_t RANK_7 = 0x00FF000000000000ULL;
const uint64_t RANK_8 = 0xFF00000000000000ULL;

const uint64_t CENTER = 0x0000001818000000ULL;
const uint64_t KING_SIDE = 0xF0F0F0F0F0F0F0F0ULL;
const uint64_t QUEEN_SIDE = 0x0F0F0F0F0F0F0F0FULL;

// ============================================================
// Tables de décalage (pour le débogage)
// ============================================================

static const char *square_names[] = {
    "a1", "b1", "c1", "d1", "e1", "f1", "g1", "h1",
    "a2", "b2", "c2", "d2", "e2", "f2", "g2", "h2",
    "a3", "b3", "c3", "d3", "e3", "f3", "g3", "h3",
    "a4", "b4", "c4", "d4", "e4", "f4", "g4", "h4",
    "a5", "b5", "c5", "d5", "e5", "f5", "g5", "h5",
    "a6", "b6", "c6", "d6", "e6", "f6", "g6", "h6",
    "a7", "b7", "c7", "d7", "e7", "f7", "g7", "h7",
    "a8", "b8", "c8", "d8", "e8", "f8", "g8", "h8"
};

static const char *piece_names[] = {
    ".", "P", "N", "B", "R", "Q", "K"
};

// ============================================================
// Fonctions de bit manipulation
// ============================================================

int count_bits(uint64_t bb) {
    int count = 0;
    while (bb) {
        count++;
        bb &= bb - 1; // Supprime le bit de poids faible
    }
    return count;
}

Square lsb(uint64_t bb) {
    if (bb == 0) return NO_SQUARE;
    return (Square)__builtin_ctzll(bb);
}

Square pop_lsb(uint64_t *bb) {
    Square sq = lsb(*bb);
    *bb &= *bb - 1;
    return sq;
}

// ============================================================
// Conversion
// ============================================================

Square algebraic_to_square(const char *str) {
    if (str[0] < 'a' || str[0] > 'h' || str[1] < '1' || str[1] > '8') {
        return NO_SQUARE;
    }
    int file = str[0] - 'a';
    int rank = str[1] - '1';
    return (Square)(file + rank * 8);
}

void square_to_algebraic(Square sq, char *str) {
    if (sq == NO_SQUARE) {
        str[0] = '?';
        str[1] = '\0';
        return;
    }
    str[0] = 'a' + (sq % 8);
    str[1] = '1' + (sq / 8);
    str[2] = '\0';
}

// ============================================================
// Affichage
// ============================================================

void print_bitboard(uint64_t bb) {
    printf("\n");
    for (int rank = 7; rank >= 0; rank--) {
        printf("  %d ", rank + 1);
        for (int file = 0; file < 8; file++) {
            Square sq = (Square)(file + rank * 8);
            if (bb & (1ULL << sq)) {
                printf(" 1");
            } else {
                printf(" .");
            }
        }
        printf("\n");
    }
    printf("    a b c d e f g h\n\n");
}

void print_position(const Position *pos) {
    printf("\n");
    for (int rank = 7; rank >= 0; rank--) {
        printf("  %d ", rank + 1);
        for (int file = 0; file < 8; file++) {
            Square sq = (Square)(file + rank * 8);
            Piece piece = EMPTY;
            Color color = WHITE;
            
            // Trouver la pièce
            for (int c = WHITE; c <= BLACK; c++) {
                for (int p = PAWN; p <= KING; p++) {
                    if (pos->piece[c][p] & (1ULL << sq)) {
                        piece = (Piece)p;
                        color = (Color)c;
                        break;
                    }
                }
                if (piece != EMPTY) break;
            }
            
            if (piece == EMPTY) {
                printf(" .");
            } else {
                if (color == WHITE) {
                    printf(" %c", piece_names[piece][0]);
                } else {
                    printf(" %c", piece_names[piece][0] + 32); // Minuscule pour noir
                }
            }
        }
        printf("\n");
    }
    printf("    a b c d e f g h\n\n");
    
    // Informations supplémentaires
    printf("Trait: %s\n", pos->side_to_move == WHITE ? "Blanc" : "Noir");
    printf("Roque: ");
    if (pos->castling & WHITE_KING_CASTLE) printf("K");
    if (pos->castling & WHITE_QUEEN_CASTLE) printf("Q");
    if (pos->castling & BLACK_KING_CASTLE) printf("k");
    if (pos->castling & BLACK_QUEEN_CASTLE) printf("q");
    if (pos->castling == NO_CASTLING) printf("-");
    printf("\n");
    
    if (pos->en_passant != NO_SQUARE) {
        char ep_str[3];
        square_to_algebraic(pos->en_passant, ep_str);
        printf("En passant: %s\n", ep_str);
    } else {
        printf("En passant: -\n");
    }
    
    printf("Demi-coups: %d\n", pos->halfmove_clock);
    printf("Coups complets: %d\n\n", pos->fullmove_number);
}

const char *square_name(Square sq) {
    if (sq == NO_SQUARE) return "?";
    return square_names[sq];
}

const char *piece_name(Piece p) {
    if (p == EMPTY) return ".";
    return piece_names[p];
}
