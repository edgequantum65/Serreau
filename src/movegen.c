#include "chess_engine.h"
#include "magic.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================
// Tables de décalage pour les mouvements
// ============================================================

// Décalages pour les pions
static const int pawn_push[2] = { 8, -8 };
static const int pawn_double_push[2] = { 16, -16 };
static const int pawn_capture_left[2] = { 7, -9 };
static const int pawn_capture_right[2] = { 9, -7 };

// Décalages pour les cavaliers
static const int knight_offsets[8] = {
    17, 15, 10, 6, -6, -10, -15, -17
};

// Décalages pour les fous
MAYBE_UNUSED static const int bishop_offsets[4] = { 7, 9, -7, -9 };

// Décalages pour les tours
MAYBE_UNUSED static const int rook_offsets[4] = { 8, 1, -8, -1 };

// Décalages pour les dames (fous + tours)
MAYBE_UNUSED static const int queen_offsets[8] = { 7, 9, -7, -9, 8, 1, -8, -1 };

// Décalages pour les rois
static const int king_offsets[8] = {
    9, 8, 7, 1, -1, -7, -8, -9
};

// ============================================================
// Vérification d'attaque
// ============================================================

bool is_square_attacked(const Position *pos, Square sq, Color by_color) {
    if (sq >= 64) return false;
    uint64_t occupancy = pos->occupancy[2];
    
    // Attaque par les pions
    uint64_t pawn_attacks;
    if (by_color == WHITE) {
        pawn_attacks = ((1ULL << sq) >> 7) & ~FILE_A;
        pawn_attacks |= ((1ULL << sq) >> 9) & ~FILE_H;
    } else {
        pawn_attacks = ((1ULL << sq) << 7) & ~FILE_H;
        pawn_attacks |= ((1ULL << sq) << 9) & ~FILE_A;
    }
    if (pawn_attacks & pos->piece[by_color][PAWN]) return true;
    
    // Attaque par les cavaliers
    uint64_t knight_attacks = 0;
    for (int i = 0; i < 8; i++) {
        int target = sq + knight_offsets[i];
        if (target >= 0 && target < 64) {
            int from_file = sq % 8;
            int to_file = target % 8;
            if (abs(from_file - to_file) <= 2) {
                knight_attacks |= (1ULL << target);
            }
        }
    }
    if (knight_attacks & pos->piece[by_color][KNIGHT]) return true;
    
    // Attaque par les fous (Magic Bitboards)
    if (bishop_attacks(sq, occupancy) & pos->piece[by_color][BISHOP]) return true;
    
    // Attaque par les tours (Magic Bitboards)
    if (rook_attacks(sq, occupancy) & pos->piece[by_color][ROOK]) return true;
    
    // Attaque par les dames (Magic Bitboards)
    if (queen_attacks(sq, occupancy) & pos->piece[by_color][QUEEN]) return true;
    
    // Attaque par le roi
    uint64_t king_attacks = 0;
    for (int i = 0; i < 8; i++) {
        int target = sq + king_offsets[i];
        if (target >= 0 && target < 64) {
            int from_file = sq % 8;
            int to_file = target % 8;
            if (abs(from_file - to_file) <= 1) {
                king_attacks |= (1ULL << target);
            }
        }
    }
    if (king_attacks & pos->piece[by_color][KING]) return true;
    
    return false;
}

bool is_in_check(const Position *pos, Color color) {
    Color enemy = color == WHITE ? BLACK : WHITE;
    Square king_sq = lsb(pos->piece[color][KING]);
    return is_square_attacked(pos, king_sq, enemy);
}

// ============================================================
// Génération de coups
// ============================================================

static void add_move(MoveList *list, Square from, Square to, MoveType type, Piece promo) {
    if (list->count >= MAX_MOVES) return;
    
    list->moves[list->count].from = from;
    list->moves[list->count].to = to;
    list->moves[list->count].type = type;
    list->moves[list->count].promo_piece = promo;
    list->count++;
}

static void generate_pawn_moves(const Position *pos, MoveList *list, bool captures_only) {
    Color color = pos->side_to_move;
    uint64_t pawns = pos->piece[color][PAWN];
    uint64_t occupancy = pos->occupancy[2];
    uint64_t enemy = pos->occupancy[color == WHITE ? BLACK : WHITE];
    
    // Direction du pion
    int push = pawn_push[color];
    int double_push = pawn_double_push[color];
    int rank_promo = color == WHITE ? 7 : 0;
    int rank_double = color == WHITE ? 1 : 6;
    
    uint64_t bb = pawns;
    while (bb) {
        Square from = pop_lsb(&bb);
        int rank = from / 8;
        
        // Poussée simple
        if (!captures_only) {
            int to = from + push;
            if (to >= 0 && to < 64 && !(occupancy & (1ULL << to))) {
                if (to / 8 == rank_promo) {
                    add_move(list, from, (Square)to, QUIET, QUEEN);
                    add_move(list, from, (Square)to, QUIET, ROOK);
                    add_move(list, from, (Square)to, QUIET, BISHOP);
                    add_move(list, from, (Square)to, QUIET, KNIGHT);
                } else {
                    add_move(list, from, (Square)to, QUIET, EMPTY);
                }
                
                // Double poussée
                if (rank == rank_double) {
                    int to2 = from + double_push;
                    if (!(occupancy & (1ULL << to2))) {
                        add_move(list, from, (Square)to2, DOUBLE_PAWN_PUSH, EMPTY);
                    }
                }
            }
        }
        
        // Captures
        for (int i = 0; i < 2; i++) {
            int capture_offset = i == 0 ? pawn_capture_left[color] : pawn_capture_right[color];
            int to = from + capture_offset;
            
            if (to < 0 || to >= 64) continue;
            
            int from_file = from % 8;
            int to_file = to % 8;
            if (abs(from_file - to_file) != 1) continue;
            
            if (enemy & (1ULL << to)) {
                if (to / 8 == rank_promo) {
                    add_move(list, from, (Square)to, QUEEN_PROMO_CAP, QUEEN);
                    add_move(list, from, (Square)to, ROOK_PROMO_CAP, ROOK);
                    add_move(list, from, (Square)to, BISHOP_PROMO_CAP, BISHOP);
                    add_move(list, from, (Square)to, KNIGHT_PROMO_CAP, KNIGHT);
                } else {
                    add_move(list, from, (Square)to, CAPTURE, EMPTY);
                }
            }
            
            // En passant
            if (to == (int)pos->en_passant) {
                add_move(list, from, (Square)to, EN_PASSANT, EMPTY);
            }
        }
    }
}

static void generate_knight_moves(const Position *pos, MoveList *list, bool captures_only) {
    Color color = pos->side_to_move;
    uint64_t knights = pos->piece[color][KNIGHT];
    uint64_t own = pos->occupancy[color];
    uint64_t enemy = pos->occupancy[color == WHITE ? BLACK : WHITE];
    
    uint64_t bb = knights;
    while (bb) {
        Square from = pop_lsb(&bb);
        
        for (int i = 0; i < 8; i++) {
            int to = from + knight_offsets[i];
            if (to < 0 || to >= 64) continue;
            
            int from_file = from % 8;
            int to_file = to % 8;
            if (abs(from_file - to_file) > 2) continue;
            
            if (own & (1ULL << to)) continue;
            
            if (enemy & (1ULL << to)) {
                add_move(list, from, (Square)to, CAPTURE, EMPTY);
            } else if (!captures_only) {
                add_move(list, from, (Square)to, QUIET, EMPTY);
            }
        }
    }
}

static void generate_sliding_moves(const Position *pos, MoveList *list, bool captures_only, Piece piece) {
    Color color = pos->side_to_move;
    uint64_t pieces = pos->piece[color][piece];
    uint64_t own = pos->occupancy[color];
    uint64_t enemy = pos->occupancy[color == WHITE ? BLACK : WHITE];
    uint64_t occ = pos->occupancy[2];
    
    uint64_t bb = pieces;
    while (bb) {
        Square from = pop_lsb(&bb);
        uint64_t attacks;
        if (piece == BISHOP) attacks = bishop_attacks(from, occ);
        else if (piece == ROOK) attacks = rook_attacks(from, occ);
        else attacks = queen_attacks(from, occ);
        
        uint64_t targets = attacks & ~own;
        uint64_t captures = targets & enemy;
        uint64_t quiets = targets & ~enemy;
        
        while (captures) {
            Square to = pop_lsb(&captures);
            add_move(list, from, to, CAPTURE, EMPTY);
        }
        if (!captures_only) {
            while (quiets) {
                Square to = pop_lsb(&quiets);
                add_move(list, from, to, QUIET, EMPTY);
            }
        }
    }
}

static void generate_king_moves(const Position *pos, MoveList *list, bool captures_only) {
    Color color = pos->side_to_move;
    Square from = lsb(pos->piece[color][KING]);
    uint64_t own = pos->occupancy[color];
    uint64_t enemy = pos->occupancy[color == WHITE ? BLACK : WHITE];
    
    for (int i = 0; i < 8; i++) {
        int to = from + king_offsets[i];
        if (to < 0 || to >= 64) continue;
        
        int from_file = from % 8;
        int to_file = to % 8;
        if (abs(from_file - to_file) > 1) continue;
        
        if (own & (1ULL << to)) continue;
        
        if (enemy & (1ULL << to)) {
            add_move(list, from, (Square)to, CAPTURE, EMPTY);
        } else if (!captures_only) {
            add_move(list, from, (Square)to, QUIET, EMPTY);
        }
    }
    
    // Roque
    if (!captures_only) {
        Color enemy_color = color == WHITE ? BLACK : WHITE;
        int king_rank = color == WHITE ? 0 : 7;
        Square king_sq = (Square)(4 + king_rank * 8);
        
        // Petit roque
        if (pos->castling & (color == WHITE ? WHITE_KING_CASTLE : BLACK_KING_CASTLE)) {
            Square f1 = (Square)(5 + king_rank * 8);
            Square g1 = (Square)(6 + king_rank * 8);
            
            if (!(pos->occupancy[2] & (1ULL << f1)) &&
                !(pos->occupancy[2] & (1ULL << g1)) &&
                !is_square_attacked(pos, king_sq, enemy_color) &&
                !is_square_attacked(pos, f1, enemy_color) &&
                !is_square_attacked(pos, g1, enemy_color)) {
                add_move(list, king_sq, g1, KING_CASTLE, EMPTY);
            }
        }
        
        // Grand roque
        if (pos->castling & (color == WHITE ? WHITE_QUEEN_CASTLE : BLACK_QUEEN_CASTLE)) {
            Square d1 = (Square)(3 + king_rank * 8);
            Square c1 = (Square)(2 + king_rank * 8);
            Square b1 = (Square)(1 + king_rank * 8);
            
            if (!(pos->occupancy[2] & (1ULL << d1)) &&
                !(pos->occupancy[2] & (1ULL << c1)) &&
                !(pos->occupancy[2] & (1ULL << b1)) &&
                !is_square_attacked(pos, king_sq, enemy_color) &&
                !is_square_attacked(pos, d1, enemy_color) &&
                !is_square_attacked(pos, c1, enemy_color)) {
                add_move(list, king_sq, c1, QUEEN_CASTLE, EMPTY);
            }
        }
    }
}

void generate_all_moves(const Position *pos, MoveList *list) {
    list->count = 0;
    
    generate_pawn_moves(pos, list, false);
    generate_knight_moves(pos, list, false);
    generate_sliding_moves(pos, list, false, BISHOP);
    generate_sliding_moves(pos, list, false, ROOK);
    generate_sliding_moves(pos, list, false, QUEEN);
    generate_king_moves(pos, list, false);
}

void generate_captures(const Position *pos, MoveList *list) {
    list->count = 0;
    
    generate_pawn_moves(pos, list, true);
    generate_knight_moves(pos, list, true);
    generate_sliding_moves(pos, list, true, BISHOP);
    generate_sliding_moves(pos, list, true, ROOK);
    generate_sliding_moves(pos, list, true, QUEEN);
    generate_king_moves(pos, list, true);
}

void generate_quiet_moves(const Position *pos, MoveList *list) {
    list->count = 0;
    
    generate_pawn_moves(pos, list, false);
    generate_knight_moves(pos, list, false);
    generate_sliding_moves(pos, list, false, BISHOP);
    generate_sliding_moves(pos, list, false, ROOK);
    generate_sliding_moves(pos, list, false, QUEEN);
    generate_king_moves(pos, list, false);
}

// ============================================================
// Validation de coups
// ============================================================

bool is_legal_move(const Position *pos, Move move) {
    // Vérifier que la pièce existe sur la case de départ
    bool has_piece = false;
    for (int p = PAWN; p <= KING; p++) {
        if (pos->piece[pos->side_to_move][p] & (1ULL << move.from)) {
            has_piece = true;
            break;
        }
    }
    if (!has_piece) return false;
    
    Position test_pos = *pos;
    make_move(&test_pos, move);
    
    // Vérifier que le roi n'est pas en échec après le coup
    return !is_in_check(&test_pos, pos->side_to_move);
}

// ============================================================
// Application de coups (basique)
// ============================================================

void make_move(Position *pos, Move move) {
    // Sauvegarder l'état pour unmake_move
    UndoInfo *undo = &pos->stack[pos->sp];
    undo->en_passant = pos->en_passant;
    undo->castling = pos->castling;
    undo->halfmove_clock = pos->halfmove_clock;
    undo->zobrist = pos->zobrist;
    undo->captured_piece = EMPTY;
    pos->sp++;
    
    Color color = pos->side_to_move;
    Color enemy = color == WHITE ? BLACK : WHITE;
    
    // Supprimer la pièce de la case d'origine
    Piece piece = EMPTY;
    for (int p = PAWN; p <= KING; p++) {
        if (pos->piece[color][p] & (1ULL << move.from)) {
            piece = (Piece)p;
            pos->piece[color][p] &= ~(1ULL << move.from);
            break;
        }
    }
    pos->zobrist ^= zobrist_piece[color][piece][move.from];
    
    // Capturer la pièce adverse si nécessaire
    if (move.type == CAPTURE || move.type >= KNIGHT_PROMO_CAP) {
        for (int p = PAWN; p <= KING; p++) {
            if (pos->piece[enemy][p] & (1ULL << move.to)) {
                pos->piece[enemy][p] &= ~(1ULL << move.to);
                undo->captured_piece = (Piece)p;
                pos->zobrist ^= zobrist_piece[enemy][p][move.to];
                break;
            }
        }
    }
    
    // En passant
    if (move.type == EN_PASSANT) {
        Square captured_pawn = move.to + (color == WHITE ? -8 : 8);
        pos->piece[enemy][PAWN] &= ~(1ULL << captured_pawn);
        undo->captured_piece = PAWN;
        pos->zobrist ^= zobrist_piece[enemy][PAWN][captured_pawn];
    }
    
    // Roque
    if (move.type == KING_CASTLE) {
        int rank = color == WHITE ? 0 : 7;
        Square rook_from = (Square)(7 + rank * 8);
        Square rook_to = (Square)(5 + rank * 8);
        pos->piece[color][ROOK] &= ~(1ULL << rook_from);
        pos->piece[color][ROOK] |= (1ULL << rook_to);
        pos->zobrist ^= zobrist_piece[color][ROOK][rook_from];
        pos->zobrist ^= zobrist_piece[color][ROOK][rook_to];
    } else if (move.type == QUEEN_CASTLE) {
        int rank = color == WHITE ? 0 : 7;
        Square rook_from = (Square)(0 + rank * 8);
        Square rook_to = (Square)(3 + rank * 8);
        pos->piece[color][ROOK] &= ~(1ULL << rook_from);
        pos->piece[color][ROOK] |= (1ULL << rook_to);
        pos->zobrist ^= zobrist_piece[color][ROOK][rook_from];
        pos->zobrist ^= zobrist_piece[color][ROOK][rook_to];
    }
    
    // Placer la pièce (ou promotion)
    if (move.promo_piece != EMPTY) {
        pos->piece[color][move.promo_piece] |= (1ULL << move.to);
        pos->zobrist ^= zobrist_piece[color][move.promo_piece][move.to];
    } else {
        pos->piece[color][piece] |= (1ULL << move.to);
        pos->zobrist ^= zobrist_piece[color][piece][move.to];
    }
    
    // Mettre à jour en passant
    pos->zobrist ^= zobrist_en_passant[pos->en_passant];
    if (move.type == DOUBLE_PAWN_PUSH) {
        pos->en_passant = (Square)(move.from + (color == WHITE ? 8 : -8));
    } else {
        pos->en_passant = NO_SQUARE;
    }
    pos->zobrist ^= zobrist_en_passant[pos->en_passant];
    
    // Mettre à jour les droits de roque
    int old_castling = pos->castling;
    if (piece == KING) {
        if (color == WHITE) {
            pos->castling &= ~(WHITE_KING_CASTLE | WHITE_QUEEN_CASTLE);
        } else {
            pos->castling &= ~(BLACK_KING_CASTLE | BLACK_QUEEN_CASTLE);
        }
    }
    
    if (piece == ROOK) {
        if (color == WHITE) {
            if (move.from == A1) pos->castling &= ~WHITE_QUEEN_CASTLE;
            if (move.from == H1) pos->castling &= ~WHITE_KING_CASTLE;
        } else {
            if (move.from == A8) pos->castling &= ~BLACK_QUEEN_CASTLE;
            if (move.from == H8) pos->castling &= ~BLACK_KING_CASTLE;
        }
    }
    
    // Perte des droits si une tour adverse sur sa case d'origine est capturée
    if (move.to == A1) pos->castling &= ~WHITE_QUEEN_CASTLE;
    if (move.to == H1) pos->castling &= ~WHITE_KING_CASTLE;
    if (move.to == A8) pos->castling &= ~BLACK_QUEEN_CASTLE;
    if (move.to == H8) pos->castling &= ~BLACK_KING_CASTLE;
    
    if (pos->castling != old_castling) {
        pos->zobrist ^= zobrist_castling[old_castling];
        pos->zobrist ^= zobrist_castling[pos->castling];
    }
    
    // Mettre à jour l'occupancy
    pos->occupancy[WHITE] = 0;
    pos->occupancy[BLACK] = 0;
    for (int p = PAWN; p <= KING; p++) {
        pos->occupancy[WHITE] |= pos->piece[WHITE][p];
        pos->occupancy[BLACK] |= pos->piece[BLACK][p];
    }
    pos->occupancy[2] = pos->occupancy[WHITE] | pos->occupancy[BLACK];
    
    // Changer le trait
    pos->side_to_move = enemy;
    pos->zobrist ^= zobrist_side;
    
    // Mettre à jour le compteur
    if (piece == PAWN || move.type == CAPTURE || move.type == EN_PASSANT) {
        pos->halfmove_clock = 0;
    } else {
        pos->halfmove_clock++;
    }
    
    if (color == BLACK) {
        pos->fullmove_number++;
    }
}

void unmake_move(Position *pos, Move move) {
    // Restaurer l'état sauvegardé
    pos->sp--;
    UndoInfo *undo = &pos->stack[pos->sp];
    
    Color color = pos->side_to_move == WHITE ? BLACK : WHITE; // Le trait était à l'autre joueur
    Color enemy = pos->side_to_move;
    
    // Restaurer les états simples
    pos->en_passant = undo->en_passant;
    pos->castling = undo->castling;
    pos->halfmove_clock = undo->halfmove_clock;
    pos->zobrist = undo->zobrist;
    
    // Retirer la pièce de la case d'arrivée
    Piece piece = EMPTY;
    if (move.promo_piece != EMPTY) {
        // Promotion: retirer la pièce promue
        pos->piece[color][move.promo_piece] &= ~(1ULL << move.to);
        piece = PAWN; // La pièce d'origine était un pion
    } else {
        // Trouver la pièce sur la case d'arrivée
        for (int p = PAWN; p <= KING; p++) {
            if (pos->piece[color][p] & (1ULL << move.to)) {
                piece = (Piece)p;
                pos->piece[color][p] &= ~(1ULL << move.to);
                break;
            }
        }
    }
    
    // Remettre la pièce sur la case d'origine
    if (piece != EMPTY) {
        pos->piece[color][piece] |= (1ULL << move.from);
    }
    
    // Remettre la pièce capturée
    if (move.type == CAPTURE || move.type >= KNIGHT_PROMO_CAP) {
        if (undo->captured_piece != EMPTY) {
            pos->piece[enemy][undo->captured_piece] |= (1ULL << move.to);
        }
    }
    
    // En passant: remettre le pion capturé
    if (move.type == EN_PASSANT) {
        Square captured_pawn = move.to + (color == WHITE ? -8 : 8);
        pos->piece[enemy][PAWN] |= (1ULL << captured_pawn);
    }
    
    // Roque: remettre la tour
    if (move.type == KING_CASTLE) {
        int rank = color == WHITE ? 0 : 7;
        Square rook_from = (Square)(7 + rank * 8);
        Square rook_to = (Square)(5 + rank * 8);
        pos->piece[color][ROOK] &= ~(1ULL << rook_to);
        pos->piece[color][ROOK] |= (1ULL << rook_from);
    } else if (move.type == QUEEN_CASTLE) {
        int rank = color == WHITE ? 0 : 7;
        Square rook_from = (Square)(0 + rank * 8);
        Square rook_to = (Square)(3 + rank * 8);
        pos->piece[color][ROOK] &= ~(1ULL << rook_to);
        pos->piece[color][ROOK] |= (1ULL << rook_from);
    }
    
    // Restaurer l'occupancy
    pos->occupancy[WHITE] = 0;
    pos->occupancy[BLACK] = 0;
    for (int p = PAWN; p <= KING; p++) {
        pos->occupancy[WHITE] |= pos->piece[WHITE][p];
        pos->occupancy[BLACK] |= pos->piece[BLACK][p];
    }
    pos->occupancy[2] = pos->occupancy[WHITE] | pos->occupancy[BLACK];
    
    // Restaurer le trait
    pos->side_to_move = color;
    
    // Restaurer le numéro de coup
    if (color == BLACK) {
        pos->fullmove_number--;
    }
}

