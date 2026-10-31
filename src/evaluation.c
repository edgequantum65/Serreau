#include "chess_engine.h"
#include "nnue.h"
#include "magic.h"
#include <stdlib.h>

static const int knight_offsets[8] = { 17, 15, 10, 6, -6, -10, -15, -17 };
static const int king_offsets[8] = { 9, 8, 7, 1, -1, -7, -8, -9 };

static int use_nnue = 1;

void evaluation_set_nnue(int enabled) {
    use_nnue = enabled;
}

int evaluation_get_nnue(void) {
    return use_nnue;
}

// ============================================================
// Tables pièce-position (simplifiées)
// ============================================================

// Table des pions (de la perspective des blancs)
static const int pawn_table[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};

// Table des cavaliers
static const int knight_table[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};

// Table des fous
static const int bishop_table[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};

// Table des tours
static const int rook_table[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10, 10, 10, 10, 10,  5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     0,  0,  0,  5,  5,  0,  0,  0
};

// Table des dames
static const int queen_table[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
     -5,  0,  5,  5,  5,  5,  0, -5,
      0,  0,  5,  5,  5,  5,  0, -5,
    -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};

// Table du roi (milieu de partie)
static const int king_table[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20
};

// Table du roi (fin de partie)
static const int king_endgame_table[64] = {
    -50,-40,-30,-20,-20,-30,-40,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50
};

// ============================================================
// Évaluation matérielle
// ============================================================

int evaluate_material(const Position *pos) {
    int score = 0;
    
    // Pièces blanches
    score += count_bits(pos->piece[WHITE][PAWN]) * PAWN_VALUE;
    score += count_bits(pos->piece[WHITE][KNIGHT]) * KNIGHT_VALUE;
    score += count_bits(pos->piece[WHITE][BISHOP]) * BISHOP_VALUE;
    score += count_bits(pos->piece[WHITE][ROOK]) * ROOK_VALUE;
    score += count_bits(pos->piece[WHITE][QUEEN]) * QUEEN_VALUE;
    
    // Pièces noires
    score -= count_bits(pos->piece[BLACK][PAWN]) * PAWN_VALUE;
    score -= count_bits(pos->piece[BLACK][KNIGHT]) * KNIGHT_VALUE;
    score -= count_bits(pos->piece[BLACK][BISHOP]) * BISHOP_VALUE;
    score -= count_bits(pos->piece[BLACK][ROOK]) * ROOK_VALUE;
    score -= count_bits(pos->piece[BLACK][QUEEN]) * QUEEN_VALUE;
    
    return score;
}

// ============================================================
// Évaluation positionnelle
// ============================================================

static int evaluate_piece_position(const Position *pos, Color color, Piece piece, const int *table) {
    int score = 0;
    uint64_t bb = pos->piece[color][piece];
    
    while (bb) {
        Square sq = pop_lsb(&bb);
        int idx = color == WHITE ? sq : (sq ^ 56); // Inverser pour les noirs
        score += table[idx];
    }
    
    return color == WHITE ? score : -score;
}

int evaluate_pawn_structure(const Position *pos) {
    int score = 0;
    
    // Pions blancs
    uint64_t white_pawns = pos->piece[WHITE][PAWN];
    uint64_t black_pawns = pos->piece[BLACK][PAWN];
    
    // Pions doublés (malus)
    for (int file = 0; file < 8; file++) {
        uint64_t file_mask = 0x0101010101010101ULL << file;
        int white_count = count_bits(white_pawns & file_mask);
        int black_count = count_bits(black_pawns & file_mask);
        
        if (white_count > 1) score -= (white_count - 1) * 20;
        if (black_count > 1) score += (black_count - 1) * 20;
    }
    
    // Pions isolés (malus)
    uint64_t white_isolated = 0;
    uint64_t black_isolated = 0;
    
    for (int file = 0; file < 8; file++) {
        uint64_t file_mask = 0x0101010101010101ULL << file;
        uint64_t adjacent_files = 0;
        if (file > 0) adjacent_files |= 0x0101010101010101ULL << (file - 1);
        if (file < 7) adjacent_files |= 0x0101010101010101ULL << (file + 1);
        
        if ((white_pawns & file_mask) && !(white_pawns & adjacent_files)) {
            white_isolated |= white_pawns & file_mask;
            score -= 20;
        }
        
        if ((black_pawns & file_mask) && !(black_pawns & adjacent_files)) {
            black_isolated |= black_pawns & file_mask;
            score += 20;
        }
    }
    
    // Pions passés : désormais évalués UNIQUEMENT dans evaluate_passed_pawns()
    // (supprimé d'ici pour éviter le double/triple comptage)
    
    return score;
}

// evaluate_king_safety() a été remplacée par evaluate_king_safety_improved()

// Version light: compte les pseudo-coups
// Capture ~85% du signal de mobilité à ~1/20e du coût (pas de make_move/unmake par coup).
// Pondération 2 cp/pseudo-coup pour rester dans la même échelle que l'ancienne version (5 cp/legal).
int evaluate_mobility(const Position *pos) {
    int score = 0;

    MoveList white_moves, black_moves;

    Position white_pos = *pos;
    white_pos.side_to_move = WHITE;
    generate_all_moves(&white_pos, &white_moves);
    int white_pseudo = white_moves.count;

    Position black_pos = *pos;
    black_pos.side_to_move = BLACK;
    generate_all_moves(&black_pos, &black_moves);
    int black_pseudo = black_moves.count;

    score += (white_pseudo - black_pseudo) * 2;

    return score;
}

// ============================================================
// Paires de fous (amélioré)
// - Bonus variable selon phase : 40cp MG, 20cp EG
// - Bonus augmenté en position ouverte (≤ 3 pions centraux)
// - Vérification cases opposées (un fou case claire + un fou case foncée)
// ============================================================
static int evaluate_bishop_pair(const Position *pos, int phase) {
    int score = 0;

    // Masques pour cases claires et foncées
    const uint64_t LIGHT_SQUARES = 0x55AA55AA55AA55AAULL;
    const uint64_t DARK_SQUARES  = 0xAA55AA55AA55AA55ULL;

    // Pions centraux (d4, e4, d5, e5) — position ouverte si ≤ 3
    const uint64_t CENTER_PAWNS_MASK = 0x0000001818000000ULL;  // d4 e4 d5 e5 (03/10/2026 : avant c5-f6)

    // --- Blancs : paire de fous ? ---
    uint64_t wbishops = pos->piece[WHITE][BISHOP];
    int wb_count = count_bits(wbishops);
    if (wb_count >= 2) {
        // Vérifier que les fous sont sur des cases de couleurs différentes
        // (la paire réelle = un fou case claire + un fou case foncée)
        int real_pair = (wbishops & LIGHT_SQUARES) && (wbishops & DARK_SQUARES);
        if (real_pair) {
            int bonus;
            if (phase > 128) {  // Milieu de jeu
                bonus = 40;
                // Position ouverte : bonus supplémentaire
                uint64_t center_pawns = pos->piece[WHITE][PAWN] & CENTER_PAWNS_MASK;
                center_pawns |= pos->piece[BLACK][PAWN] & CENTER_PAWNS_MASK;
                if (count_bits(center_pawns) <= 3) {
                    bonus = 55;
                }
            } else if (phase < 64) {  // Finale
                bonus = 80;  // Weiss: 110cp EG, on prend 80cp (conservateur)
            } else {  // Transition
                bonus = 50;
            }
            score += bonus;
        }
    }

    // --- Noirs : paire de fous ? ---
    uint64_t bbishops = pos->piece[BLACK][BISHOP];
    int bb_count = count_bits(bbishops);
    if (bb_count >= 2) {
        int real_pair = (bbishops & LIGHT_SQUARES) && (bbishops & DARK_SQUARES);
        if (real_pair) {
            int bonus;
            if (phase > 128) {
                bonus = 40;
                uint64_t center_pawns = pos->piece[WHITE][PAWN] & CENTER_PAWNS_MASK;
                center_pawns |= pos->piece[BLACK][PAWN] & CENTER_PAWNS_MASK;
                if (count_bits(center_pawns) <= 3) {
                    bonus = 55;
                }
            } else if (phase < 64) {
                bonus = 80;  // Weiss: 110cp EG
            } else {
                bonus = 50;
            }
            score -= bonus;
        }
    }

    return score;
}

// Tours sur fiches ouvertes/demi-ouvertes
static int evaluate_rook_files(const Position *pos) {
    int score = 0;
    uint64_t white_pawns = pos->piece[WHITE][PAWN];
    uint64_t black_pawns = pos->piece[BLACK][PAWN];
    
    uint64_t white_rooks = pos->piece[WHITE][ROOK];
    while (white_rooks) {
        Square sq = pop_lsb(&white_rooks);
        int file = sq % 8;
        uint64_t file_mask = 0x0101010101010101ULL << file;
        bool own_pawn = (white_pawns & file_mask) != 0;
        bool enemy_pawn = (black_pawns & file_mask) != 0;
        bool defended = is_square_attacked(pos, sq, WHITE);
        if (!own_pawn && !enemy_pawn) {
            if (defended) score += 20;
            else score -= 10;  // tour indéfendue sur colonne ouverte = vulnérable
        } else if (!own_pawn && enemy_pawn) {
            if (defended) score += 10;
        }
    }
    
    uint64_t black_rooks = pos->piece[BLACK][ROOK];
    while (black_rooks) {
        Square sq = pop_lsb(&black_rooks);
        int file = sq % 8;
        uint64_t file_mask = 0x0101010101010101ULL << file;
        bool own_pawn = (black_pawns & file_mask) != 0;
        bool enemy_pawn = (white_pawns & file_mask) != 0;
        bool defended = is_square_attacked(pos, sq, BLACK);
        if (!own_pawn && !enemy_pawn) {
            if (defended) score -= 20;
            else score += 10;
        } else if (!own_pawn && enemy_pawn) {
            if (defended) score -= 10;
        }
    }
    return score;
}

// ============================================================
// Évaluation principale améliorée
// ============================================================

// Phase de jeu: 0 = opening, 256 = endgame
static int game_phase(const Position *pos) {
    int phase = 0;
    phase += (count_bits(pos->piece[WHITE][KNIGHT]) + count_bits(pos->piece[BLACK][KNIGHT])) * 1;
    phase += (count_bits(pos->piece[WHITE][BISHOP]) + count_bits(pos->piece[BLACK][BISHOP])) * 1;
    phase += (count_bits(pos->piece[WHITE][ROOK]) + count_bits(pos->piece[BLACK][ROOK])) * 2;
    phase += (count_bits(pos->piece[WHITE][QUEEN]) + count_bits(pos->piece[BLACK][QUEEN])) * 4;
    phase = phase * 256 / 24;   // echelle 0..256 attendue partout (24 = materiel de depart)
    if (phase > 256) phase = 256;
    return phase;
}

// Bonus de développement
static int evaluate_development(const Position *pos) {
    int score = 0;
    // Cavaliers pas encore sortis (malus)
    if (pos->piece[WHITE][KNIGHT] & (1ULL << B1)) score -= 15;
    if (pos->piece[WHITE][KNIGHT] & (1ULL << G1)) score -= 15;
    if (pos->piece[BLACK][KNIGHT] & (1ULL << B8)) score += 15;
    if (pos->piece[BLACK][KNIGHT] & (1ULL << G8)) score += 15;
    // Fous pas encore sortis (malus)
    if (pos->piece[WHITE][BISHOP] & (1ULL << C1)) score -= 10;
    if (pos->piece[WHITE][BISHOP] & (1ULL << F1)) score -= 10;
    if (pos->piece[BLACK][BISHOP] & (1ULL << C8)) score += 10;
    if (pos->piece[BLACK][BISHOP] & (1ULL << F8)) score += 10;
    // Roi au centre non roqué (malus fort)
    Square wk = lsb(pos->piece[WHITE][KING]);
    if (wk == E1) score -= 40;
    if (wk == D1 || wk == F1) score -= 20;
    Square bk = lsb(pos->piece[BLACK][KING]);
    if (bk == E8) score += 40;
    if (bk == D8 || bk == F8) score += 20;
    // Bonus si effectivement roqué (roi à l'abri)
    if (wk == G1 && (pos->piece[WHITE][ROOK] & (1ULL << F1))) score += 30;
    if (wk == C1 && (pos->piece[WHITE][ROOK] & (1ULL << D1))) score += 30;
    if (bk == G8 && (pos->piece[BLACK][ROOK] & (1ULL << F8))) score -= 30;
    if (bk == C8 && (pos->piece[BLACK][ROOK] & (1ULL << D8))) score -= 30;
    return score;
}

// Avant-postes pour cavaliers
static int evaluate_outposts(const Position *pos) {
    int score = 0;
    uint64_t white_pawns = pos->piece[WHITE][PAWN];
    uint64_t black_pawns = pos->piece[BLACK][PAWN];
    
    uint64_t wk = pos->piece[WHITE][KNIGHT];
    while (wk) {
        Square sq = pop_lsb(&wk);
        int f = sq % 8, r = sq / 8;
        if (r >= 4) { // Dans le camp adverse
            bool supported = false;
            if (f > 0 && (white_pawns & (1ULL << ((r-1)*8+f-1)))) supported = true;
            if (f < 7 && (white_pawns & (1ULL << ((r-1)*8+f+1)))) supported = true;
            bool blocked = false;
            if (f > 0 && (black_pawns & (1ULL << ((r+1)*8+f-1)))) blocked = true;
            if (f < 7 && (black_pawns & (1ULL << ((r+1)*8+f+1)))) blocked = true;
            if (supported && !blocked) score += 30;
        }
    }
    
    uint64_t bk = pos->piece[BLACK][KNIGHT];
    while (bk) {
        Square sq = pop_lsb(&bk);
        int f = sq % 8, r = sq / 8;
        if (r <= 3) {
            bool supported = false;
            if (f > 0 && (black_pawns & (1ULL << ((r+1)*8+f-1)))) supported = true;
            if (f < 7 && (black_pawns & (1ULL << ((r+1)*8+f+1)))) supported = true;
            bool blocked = false;
            if (f > 0 && (white_pawns & (1ULL << ((r-1)*8+f-1)))) blocked = true;
            if (f < 7 && (white_pawns & (1ULL << ((r-1)*8+f+1)))) blocked = true;
            if (supported && !blocked) score -= 30;
        }
    }
    return score;
}

// Tours sur la 7e rangée
static int evaluate_rook_seventh(const Position *pos) {
    int score = 0;
    uint64_t wr = pos->piece[WHITE][ROOK];
    while (wr) {
        Square sq = pop_lsb(&wr);
        if (sq / 8 == 6) score += 20; // Tour blanche sur 7e rangée
    }
    uint64_t br = pos->piece[BLACK][ROOK];
    while (br) {
        Square sq = pop_lsb(&br);
        if (sq / 8 == 1) score -= 20; // Tour noire sur 2e rangée
    }
    return score;
}

// Pièces piégées (HCE - cas classiques du CPW, Trapped_Pieces)
// Détection par motif statique: structure de pions adverse qui bloque
// la pièce et empêche toute case de fuite utile. Pénalités indicatives
// de Chess Programming Wiki. Universel: actif en MG et EG.
static int evaluate_trapped_pieces(const Position *pos) {
    int score = 0;

    uint64_t wp = pos->piece[WHITE][PAWN];
    uint64_t bp = pos->piece[BLACK][PAWN];
    uint64_t wk = pos->piece[WHITE][KING];
    uint64_t bk = pos->piece[BLACK][KING];
    uint64_t wb = pos->piece[WHITE][BISHOP];
    uint64_t bb = pos->piece[BLACK][BISHOP];
    uint64_t wn = pos->piece[WHITE][KNIGHT];
    uint64_t bn = pos->piece[BLACK][KNIGHT];
    uint64_t wr = pos->piece[WHITE][ROOK];
    uint64_t brp = pos->piece[BLACK][ROOK];

    #define HAS(bb_, sq_) (((bb_) >> (sq_)) & 1ULL)

    // ---- Pièces blanches piégées par les pions noirs ----

    // Fou blanc h7 bloqué par pions noirs g6 + f7 (cas extrême CPW)
    if (HAS(wb, H7) && HAS(bp, G6) && HAS(bp, F7)) score -= 150;
    // Fou blanc h6 bloqué par pions noirs g5 + f6 (moins sévère)
    if (HAS(wb, H6) && HAS(bp, G5) && HAS(bp, F6)) score -= 50;
    // Cavalier blanc h8 avec pion noir h7 ou f7
    if (HAS(wn, H8) && (HAS(bp, H7) || HAS(bp, F7))) score -= 100;
    // Cavalier blanc h7 avec pions noirs g7 et (h6 ou f6)
    if (HAS(wn, H7) && HAS(bp, G7) && (HAS(bp, H6) || HAS(bp, F6))) score -= 100;
    // Tour blanche h1/g1/h2/g2 avec roi blanc f1/g1 (anti pseudo-roque)
    if (HAS(wk, F1) || HAS(wk, G1)) {
        if (HAS(wr, H1) || HAS(wr, G1) || HAS(wr, H2) || HAS(wr, G2)) score -= 40;
    }

    // ---- Pièces noires piégées par les pions blancs (miroir) ----

    // Fou noir h2 bloqué par pions blancs g3 + f2
    if (HAS(bb, H2) && HAS(wp, G3) && HAS(wp, F2)) score += 150;
    // Fou noir h3 bloqué par pions blancs g4 + f3
    if (HAS(bb, H3) && HAS(wp, G4) && HAS(wp, F3)) score += 50;
    // Cavalier noir h1 avec pion blanc h2 ou f2
    if (HAS(bn, H1) && (HAS(wp, H2) || HAS(wp, F2))) score += 100;
    // Cavalier noir h2 avec pions blancs g2 et (h3 ou f3)
    if (HAS(bn, H2) && HAS(wp, G2) && (HAS(wp, H3) || HAS(wp, F3))) score += 100;
    // Tour noire h8/g8/h7/g7 avec roi noir f8/g8
    if (HAS(bk, F8) || HAS(bk, G8)) {
        if (HAS(brp, H8) || HAS(brp, G8) || HAS(brp, H7) || HAS(brp, G7)) score += 40;
    }

    #undef HAS
    return score;
}

// Sécurité du roi améliorée avec bouclier de pions
static int evaluate_king_safety_improved(const Position *pos) {
    int score = 0;
    int phase = game_phase(pos);
    bool endgame = phase < 64;
    
    const int *king_table_used = endgame ? king_endgame_table : king_table;
    
    Square white_king = lsb(pos->piece[WHITE][KING]);
    Square black_king = lsb(pos->piece[BLACK][KING]);
    
    score += king_table_used[white_king];
    score -= king_table_used[black_king ^ 56];
    
    if (!endgame) {
        // Bouclier de pions pour le roi blanc
        int wf = white_king % 8, wrk = white_king / 8;
        for (int f = wf - 1; f <= wf + 1; f++) {
            if (f < 0 || f > 7) continue;
            for (int r = wrk + 1; r <= wrk + 3 && r < 8; r++) {
                if (pos->piece[WHITE][PAWN] & (1ULL << (r * 8 + f))) {
                    score += 15 - (r - wrk - 1) * 3;
                }
            }
        }
        // Bouclier de pions pour le roi noir
        int bf = black_king % 8, brk = black_king / 8;
        for (int f = bf - 1; f <= bf + 1; f++) {
            if (f < 0 || f > 7) continue;
            for (int r = brk - 1; r >= brk - 3 && r >= 0; r--) {
                if (pos->piece[BLACK][PAWN] & (1ULL << (r * 8 + f))) {
                    score -= 15 - (brk - r - 1) * 3;
                }
            }
        }
    }
    
    return score;
}

bool is_insufficient_material(const Position *pos) {
    int wq = count_bits(pos->piece[WHITE][QUEEN]);
    int bq = count_bits(pos->piece[BLACK][QUEEN]);
    int wr = count_bits(pos->piece[WHITE][ROOK]);
    int br = count_bits(pos->piece[BLACK][ROOK]);
    int wn = count_bits(pos->piece[WHITE][KNIGHT]);
    int bn = count_bits(pos->piece[BLACK][KNIGHT]);
    int wb = count_bits(pos->piece[WHITE][BISHOP]);
    int bb = count_bits(pos->piece[BLACK][BISHOP]);
    int wp = count_bits(pos->piece[WHITE][PAWN]);
    int bp = count_bits(pos->piece[BLACK][PAWN]);
    
    if (wq > 0 || bq > 0 || wr > 0 || br > 0 || wp > 0 || bp > 0) return false;
    
    int w_minors = wn + wb;
    int b_minors = bn + bb;
    
    if (w_minors == 0 && b_minors == 0) return true;
    if (w_minors == 1 && b_minors == 0) return true;
    if (b_minors == 1 && w_minors == 0) return true;
    if (w_minors == 2 && wn == 2 && b_minors == 0) return true;
    if (b_minors == 2 && bn == 2 && w_minors == 0) return true;
    
    return false;
}

// ============================================================
// Connectivité (Kopec/Northam/Podber/Fouda 1989, simplifié)
// Bonus par défenseur d'une pièce non-pion, non-roi. Encourage
// la protection mutuelle (chaîne de pions, pièces défendues).
// Pondération: 4 cp par défenseur, pas de plafond (somme naturelle).
// ============================================================
static int connectivity_for_color(const Position *pos, Color c) {
    int score = 0;
    Piece targets[4] = { KNIGHT, BISHOP, ROOK, QUEEN };
    for (int t = 0; t < 4; t++) {
        uint64_t bb = pos->piece[c][targets[t]];
        while (bb) {
            Square sq = __builtin_ctzll(bb);
            bb &= bb - 1;
            // Combien de pièces du même camp défendent cette case ?
            // On réutilise is_square_attacked: true si n'importe quelle
            // pièce alliée attaque la case sq. Limite: 1 défenseur compté
            // même si plusieurs pièces attaquent. Acceptable pour cette
            // version simplifiée (Kopec complet ferait x-ray + plusieurs
            // niveaux). On raffinera si besoin.
            if (is_square_attacked(pos, sq, c)) {
                score += 4;
            }
        }
    }
    return score;
}

static int evaluate_connectivity(const Position *pos) {
    return connectivity_for_color(pos, WHITE) - connectivity_for_color(pos, BLACK);
}

// ============================================================
// Motifs d'évaluation (Patterns)
// - Pions passés (Peilep): pion sans pion adverse devant ni adjacent
// - Pion passé connecté: bonus additionnel si allié adjacent
// - Bonus d'avancement du pion passé (6e/7e rangée = gros bonus)
// Pondération: 10 cp par pion passé, +5 cp par rangée avancée (3-7),
// +15 cp si connecté. Actif en MG et EG (surtout EG).
// ============================================================
static int patterns_for_color(const Position *pos, Color c) {
    int score = 0;
    uint64_t my_pawns = pos->piece[c][PAWN];
    uint64_t opp_pawns = pos->piece[c == WHITE ? BLACK : WHITE][PAWN];
    
    uint64_t p = my_pawns;
    while (p) {
        Square sq = __builtin_ctzll(p);
        p &= p - 1;
        int rank = sq / 8;  // 0=rank1, 7=rank8
        
        // Front span du pion : toute la route jusqu'a la promotion (meme colonne +
        // colonnes adjacentes). 03/10/2026 : avant, seulement la rangee suivante.
        int file = sq % 8;
        uint64_t files = FILE_A << file;
        if (file > 0) files |= FILE_A << (file - 1);
        if (file < 7) files |= FILE_A << (file + 1);
        uint64_t ahead = (c == WHITE) ? (rank < 7 ? ~0ULL << (8 * (rank + 1)) : 0)
                                      : (rank > 0 ? ~0ULL >> (8 * (8 - rank)) : 0);
        uint64_t front = files & ahead;
        
        // Si aucun pion adverse dans front span, c'est un pion passé
        if ((front & opp_pawns) == 0) {
            // Bonus de base et avancement : DÉJÀ dans evaluate_passed_pawns()
            // On garde UNIQUEMENT le bonus de connectivité ici
            
            // Vérifier s'il est connecté (pion allié adjacent même rangée)
            uint64_t adj;
            if (c == WHITE) {
                adj = ((1ULL << sq) << 1) & ~FILE_A;
                adj |= ((1ULL << sq) >> 1) & ~FILE_H;
            } else {
                adj = ((1ULL << sq) << 1) & ~FILE_A;
                adj |= ((1ULL << sq) >> 1) & ~FILE_H;
            }
            if (adj & my_pawns) {
                score += 45;  // Pions passés connectés = très forts
            }
        }
    }
    return score;
}

static int evaluate_patterns(const Position *pos) {
    return patterns_for_color(pos, WHITE) - patterns_for_color(pos, BLACK);
}

// ============================================================
// Mobilité par type de pièce (Weiss/Ethereal)
// Tables: [mobility_count] = bonus en centipawns (MG)
// ============================================================

static const int knight_mobility_tab[9] = {
    -44, -31, -10, 0, 13, 22, 32, 43, 54
};

static const int bishop_mobility_tab[14] = {
    -51, -26, -11, -3, 9, 21, 26, 32, 32, 35, 41, 57, 50, 100
};

static const int rook_mobility_tab[15] = {
    -105, -15, -1, 5, 2, 6, 5, 12, 14, 19, 24, 23, 25, 36, 72
};

static const int queen_mobility_tab[28] = {
    -63, -97, -89, -17, 0, -8, -2, -2, 1, 5, 7, 9, 15, 15,
    16, 16, 12, 13, 23, 22, 48, 58, 122, 135, 146, 125, 0, 0
};

static int evaluate_piece_mobility(const Position *pos, Color c) {
    int score = 0;
    uint64_t occ = pos->occupancy[2];
    uint64_t own = pos->occupancy[c];
    Color enemy = c == WHITE ? BLACK : WHITE;
    
    // Zone de mobilité restreinte : exclure les cases attaquées par pions ennemis
    uint64_t ep_atk = 0;
    uint64_t ep = pos->piece[enemy][PAWN];
    if (c == WHITE) {
        ep_atk = ((ep << 7) & ~FILE_H) | ((ep << 9) & ~FILE_A);
    } else {
        ep_atk = ((ep >> 7) & ~FILE_A) | ((ep >> 9) & ~FILE_H);
    }
    uint64_t mob_area = ~(own | ep_atk);

    // Cavaliers
    uint64_t knights = pos->piece[c][KNIGHT];
    while (knights) {
        Square sq = pop_lsb(&knights);
        int cnt = 0;
        for (int i = 0; i < 8; i++) {
            int to = sq + knight_offsets[i];
            if (to < 0 || to >= 64) continue;
            int f = sq % 8, tf = to % 8;
            if (abs(f - tf) > 2) continue;
            if (own & (1ULL << to)) continue;
            cnt++;
        }
        if (cnt > 8) cnt = 8;
        score += knight_mobility_tab[cnt];
    }

    // Fous
    uint64_t bishops = pos->piece[c][BISHOP];
    while (bishops) {
        Square sq = pop_lsb(&bishops);
        uint64_t attacks = bishop_attacks(sq, occ);
        int cnt = count_bits(attacks & mob_area);
        if (cnt > 13) cnt = 13;
        score += bishop_mobility_tab[cnt];
    }

    // Tours
    uint64_t rooks = pos->piece[c][ROOK];
    while (rooks) {
        Square sq = pop_lsb(&rooks);
        uint64_t attacks = rook_attacks(sq, occ);
        int cnt = count_bits(attacks & mob_area);
        if (cnt > 14) cnt = 14;
        score += rook_mobility_tab[cnt];
    }

    // Dames
    uint64_t queens = pos->piece[c][QUEEN];
    while (queens) {
        Square sq = pop_lsb(&queens);
        uint64_t attacks = queen_attacks(sq, occ);
        int cnt = count_bits(attacks & mob_area);
        if (cnt > 27) cnt = 27;
        score += queen_mobility_tab[cnt];
    }

    return score;
}

// ============================================================
// Menaces (Ethereal) — 10 termes
// ============================================================
// Chaque terme est en cp (centipawns). Pour rester cohérent avec notre
// HCE mono-valeur, on moyenne MG et EG d'Ethereal.
// (mg + eg) / 2 donne un cp moyen qui marche bien comme heuristique
// tactique ; les effets endgame sont capturés par les pions passés.
// ============================================================

// (mg, eg) Ethereal -> cp moyen utilisé
#define THREAT_WEAK_PAWN_CP              ((-11 + -38) / 2)   // -24
#define THREAT_MINOR_ATTACKED_BY_PAWN_CP ((-55 + -83) / 2)   // -69
#define THREAT_MINOR_ATTACKED_BY_MINOR_CP ((-25 + -45) / 2)  // -35
#define THREAT_MINOR_ATTACKED_BY_MAJOR_CP ((-30 + -55) / 2)  // -42
#define THREAT_ROOK_ATTACKED_BY_LESSER_CP ((-48 + -28) / 2)  // -38
#define THREAT_MINOR_ATTACKED_BY_KING_CP  ((-43 + -21) / 2)  // -32
#define THREAT_ROOK_ATTACKED_BY_KING_CP   ((-33 + -18) / 2)  // -25
#define THREAT_QUEEN_ATTACKED_BY_ONE_CP   ((-50 + -7) / 2)   // -28
#define THREAT_OVERLOADED_PIECES_CP       ((-7 + -16) / 2)   // -11
#define THREAT_BY_PAWN_PUSH_CP            ((15 + 32) / 2)    // +23

// Attaques d'un camp par type de pièce (utilise is_square_attacked, lazy)
// Plus précis serait de recalculer les attacks par type, mais pour
// alléger on distingue via occupancy.
static uint64_t compute_attacked_by(const Position *pos, Color c, Piece pt) {
    uint64_t attacked = 0;
    uint64_t pieces = pos->piece[c][pt];
    uint64_t occ = pos->occupancy[2];
    while (pieces) {
        Square sq = pop_lsb(&pieces);
        switch (pt) {
            case PAWN: {
                if (c == WHITE) {
                    attacked |= ((1ULL << sq) << 7) & ~FILE_H;
                    attacked |= ((1ULL << sq) << 9) & ~FILE_A;
                } else {
                    attacked |= ((1ULL << sq) >> 7) & ~FILE_A;
                    attacked |= ((1ULL << sq) >> 9) & ~FILE_H;
                }
                break;
            }
            case KNIGHT: {
                // Réutilise les offsets cavalier
                for (int i = 0; i < 8; i++) {
                    int to = (int)sq + knight_offsets[i];
                    if (to >= 0 && to < 64) {
                        int f = sq % 8, tf = to % 8;
                        if (abs(f - tf) <= 2) attacked |= (1ULL << to);
                    }
                }
                break;
            }
            case BISHOP: attacked |= bishop_attacks(sq, occ); break;
            case ROOK:   attacked |= rook_attacks(sq, occ);   break;
            case QUEEN:  attacked |= queen_attacks(sq, occ);  break;
            default: break;
        }
    }
    return attacked;
}

// Attaque par le roi : utilise is_square_attacked pour chaque case vide/ennemie
static uint64_t compute_attacked_by_king(const Position *pos, Color c) {
    Square ks = lsb(pos->piece[c][KING]);
    uint64_t attacked = 0;
    for (int i = 0; i < 8; i++) {
        int to = (int)ks + king_offsets[i];
        if (to >= 0 && to < 64) {
            int f = ks % 8, tf = to % 8;
            if (abs(f - tf) <= 1) attacked |= (1ULL << to);
        }
    }
    return attacked;
}

static int evaluate_threats(const Position *pos, Color c) {
    int score = 0;
    Color enemy = c == WHITE ? BLACK : WHITE;

    uint64_t my_pawns     = pos->piece[c][PAWN];
    uint64_t my_knights   = pos->piece[c][KNIGHT];
    uint64_t my_bishops   = pos->piece[c][BISHOP];
    uint64_t my_rooks     = pos->piece[c][ROOK];
    uint64_t my_queens    = pos->piece[c][QUEEN];
    uint64_t my_minors    = my_knights | my_bishops;
    uint64_t my_majors    = my_rooks | my_queens;
    uint64_t my_non_pawns = my_minors | my_majors;

    // Attaques ennemies par type
    uint64_t atk_by_pawn  = compute_attacked_by(pos, enemy, PAWN);
    uint64_t atk_by_minor = compute_attacked_by(pos, enemy, KNIGHT)
                          | compute_attacked_by(pos, enemy, BISHOP);
    uint64_t atk_by_major = compute_attacked_by(pos, enemy, ROOK)
                          | compute_attacked_by(pos, enemy, QUEEN);
    uint64_t atk_by_king  = compute_attacked_by_king(pos, enemy);

    // Attaques de mon camp
    uint64_t my_pawn_atk   = compute_attacked_by(pos, c, PAWN);
    uint64_t my_atk_all    = my_pawn_atk
                           | compute_attacked_by(pos, c, KNIGHT)
                           | compute_attacked_by(pos, c, BISHOP)
                           | compute_attacked_by(pos, c, ROOK)
                           | compute_attacked_by(pos, c, QUEEN);
    uint64_t enemy_atk_all = atk_by_pawn | atk_by_minor | atk_by_major | atk_by_king;

    // ----- Terme 1 : pion faible (mal défendu par mes pions, attaqué par ennemi) -----
    uint64_t poorly_defended = enemy_atk_all & ~my_atk_all;
    int weak_pawns = count_bits(my_pawns & poorly_defended);
    score += weak_pawns * THREAT_WEAK_PAWN_CP;

    // ----- Terme 2 : mineur ennemi attaqué par mes pions -----
    uint64_t enemy_minors = pos->piece[enemy][KNIGHT] | pos->piece[enemy][BISHOP];
    int minor_by_pawn = count_bits(enemy_minors & my_pawn_atk);
    score += minor_by_pawn * THREAT_MINOR_ATTACKED_BY_PAWN_CP;

    // ----- Terme 3 : mineur ennemi attaqué par mes mineurs -----
    int minor_by_minor = count_bits(enemy_minors & atk_by_minor);
    score += minor_by_minor * THREAT_MINOR_ATTACKED_BY_MINOR_CP;

    // ----- Terme 4 : mineur faible ennemi (poorly defended) attaqué par mes majeurs -----
    uint64_t enemy_poorly_defended = my_atk_all & ~enemy_atk_all;
    uint64_t weak_enemy_minors = enemy_minors & enemy_poorly_defended;
    int minor_by_major = count_bits(weak_enemy_minors & atk_by_major);
    score += minor_by_major * THREAT_MINOR_ATTACKED_BY_MAJOR_CP;

    // ----- Terme 5 : tour ennemie attaquée par pion ou mineur -----
    uint64_t enemy_rooks = pos->piece[enemy][ROOK];
    int rook_by_lesser = count_bits(enemy_rooks & (atk_by_pawn | atk_by_minor));
    score += rook_by_lesser * THREAT_ROOK_ATTACKED_BY_LESSER_CP;

    // ----- Terme 6 : mineur ennemi faible (poorly defended) menacé par le roi -----
    int minor_by_king = count_bits(weak_enemy_minors & atk_by_king);
    score += minor_by_king * THREAT_MINOR_ATTACKED_BY_KING_CP;

    // ----- Terme 7 : tour ennemie faible (poorly defended) menacée par le roi -----
    uint64_t weak_enemy_rooks = enemy_rooks & enemy_poorly_defended;
    int rook_by_king = count_bits(weak_enemy_rooks & atk_by_king);
    score += rook_by_king * THREAT_ROOK_ATTACKED_BY_KING_CP;

    // ----- Terme 8 : dame ennemie attaquée (n'importe quoi) -----
    uint64_t enemy_queens = pos->piece[enemy][QUEEN];
    int queen_threatened = count_bits(enemy_queens & enemy_atk_all);
    score += queen_threatened * THREAT_QUEEN_ATTACKED_BY_ONE_CP;

    // ----- Terme 9 : pièces surchargées (overloaded) -----
    // Pièce surchargée = attaquée par l'ennemi ET défendue par moi une seule fois
    uint64_t overloaded = my_non_pawns & enemy_atk_all & ~my_atk_all;
    int overloaded_count = count_bits(overloaded);
    score += overloaded_count * THREAT_OVERLOADED_PIECES_CP;

    // ----- Terme 10 : menace par poussée de pion (ThreatByPawnPush) -----
    // Pour chaque pion qui peut avancer d'1 case (vide), regarder si les 2 cases
    // d'attaque diagonales contiennent une pièce ennemie non-pion.
    uint64_t empty = ~(pos->occupancy[WHITE] | pos->occupancy[BLACK]);
    uint64_t enemy_non_pawns = pos->occupancy[enemy] & ~pos->piece[enemy][PAWN];
    uint64_t p = my_pawns;
    int push_threats = 0;
    while (p) {
        Square sq = pop_lsb(&p);
        int forward = (c == WHITE) ? 8 : -8;
        int one_ahead = (int)sq + forward;
        if (one_ahead < 0 || one_ahead >= 64) continue;
        int fa = one_ahead % 8;
        if (!((empty >> one_ahead) & 1ULL)) continue;
        int diag1 = (c == WHITE) ? one_ahead + 7 : one_ahead - 9;
        int diag2 = (c == WHITE) ? one_ahead + 9 : one_ahead - 7;
        uint64_t diag_mask = 0;
        if (diag1 >= 0 && diag1 < 64 && abs(fa - (diag1 % 8)) == 1)
            diag_mask |= (1ULL << diag1);
        if (diag2 >= 0 && diag2 < 64 && abs(fa - (diag2 % 8)) == 1)
            diag_mask |= (1ULL << diag2);
        push_threats += count_bits(diag_mask & enemy_non_pawns);
    }
    score += push_threats * THREAT_BY_PAWN_PUSH_CP;
    
    // Bonus additionnel : pions qui attaquent directement pièces ennemies
    uint64_t pawn_atk = (c == WHITE) ? 
        ((my_pawns << 7) & ~FILE_H) | ((my_pawns << 9) & ~FILE_A) :
        ((my_pawns >> 7) & ~FILE_A) | ((my_pawns >> 9) & ~FILE_H);
    int pawn_threats = count_bits(pawn_atk & enemy_non_pawns);
    score += pawn_threats * 80;

    return score;
}

// ============================================================
// King flank attacks — concentration de pièces sur le flanc du roi
// ============================================================
static int evaluate_king_flank(const Position *pos, Color c) {
    Color enemy = c == WHITE ? BLACK : WHITE;
    Square ek = lsb(pos->piece[enemy][KING]);
    int ef = ek % 8;
    uint64_t flank = 0;
    for (int f = ef-1; f <= ef+1; f++) {
        if (f < 0 || f > 7) continue;
        for (int r = 0; r < 8; r++) flank |= (1ULL << (r*8+f));
    }
    uint64_t our = pos->occupancy[c] & ~pos->piece[c][PAWN] & ~pos->piece[c][KING];
    int on_flank = count_bits(our & flank);
    int their_pawns = count_bits(pos->piece[enemy][PAWN] & flank);
    uint64_t their_pieces = pos->occupancy[enemy] & ~pos->piece[enemy][PAWN] & ~pos->piece[enemy][KING];
    int defenders = count_bits(their_pieces & flank);
    int atk = on_flank - defenders;
    if (atk > 0 && their_pawns <= 4) return atk * (5 - their_pawns) * 5;
    return 0;
}

// ============================================================
// Matériel insuffisant élargi (forteresses, zugzwang)
// ============================================================
MAYBE_UNUSED static bool is_insufficient_material_extended(const Position *pos) {
    int wq = count_bits(pos->piece[WHITE][QUEEN]);
    int bq = count_bits(pos->piece[BLACK][QUEEN]);
    int wr = count_bits(pos->piece[WHITE][ROOK]);
    int br = count_bits(pos->piece[BLACK][ROOK]);
    int wp = count_bits(pos->piece[WHITE][PAWN]);
    int bp = count_bits(pos->piece[BLACK][PAWN]);
    int wb = count_bits(pos->piece[WHITE][BISHOP]);
    int bb = count_bits(pos->piece[BLACK][BISHOP]);
    int wn = count_bits(pos->piece[WHITE][KNIGHT]);
    int bn = count_bits(pos->piece[BLACK][KNIGHT]);
    
    if (wq || bq || wr || br || wp || bp) return false;
    int wm = wn + wb, bm = bn + bb;
    if (wm == 0 && bm == 0) return true;
    if (wm <= 1 && bm == 0) return true;
    if (bm <= 1 && wm == 0) return true;
    if (wm == 2 && wn == 2 && bm == 0) return true;
    if (bm == 2 && bn == 2 && wm == 0) return true;
    
    // K+B vs K — toujours nulle (pas de mat possible)
    if (wb == 1 && bb == 0 && wm == 1 && bm == 0) return true;
    if (bb == 1 && wb == 0 && bm == 1 && wm == 0) return true;
    
    return false;
}

// ============================================================
// Sécurité roi avec comptage d'attaques (Ethereal)
// Sécurité roi avec comptage d'attaques pondéré (Weiss-style)
static int evaluate_king_attack(const Position *pos, Color c) {
    Color enemy = c == WHITE ? BLACK : WHITE;
    Square king_sq = lsb(pos->piece[enemy][KING]);
    uint64_t king_zone = 0;
    for (int i = 0; i < 8; i++) {
        int to = king_sq + king_offsets[i];
        if (to >= 0 && to < 64) {
            int f = king_sq % 8, tf = to % 8;
            if (abs(f - tf) <= 1) king_zone |= (1ULL << to);
        }
    }
    king_zone |= (1ULL << king_sq);

    static const int ATK_POWER[4] = { 36, 22, 23, 78 }; // N,B,R,Q
    static const int CHK_POWER[4] = { 68, 44, 88, 92 };
    static const int CNT_MOD[8] = { 0, 0, 63, 126, 96, 124, 124, 128 };
    
    Piece types[4] = { KNIGHT, BISHOP, ROOK, QUEEN };
    int attack_power = -30, attack_count = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t pieces = pos->piece[c][types[i]];
        uint64_t occ = pos->occupancy[2];
        while (pieces) {
            Square sq = pop_lsb(&pieces);
            uint64_t att = 0;
            switch (types[i]) {
                case KNIGHT:
                    for (int j = 0; j < 8; j++) {
                        int to = sq + knight_offsets[j];
                        if (to >= 0 && to < 64 && abs((int)(sq%8)-(to%8)) <= 2) att |= (1ULL << to);
                    } break;
                case BISHOP: att = bishop_attacks(sq, occ); break;
                case ROOK:   att = rook_attacks(sq, occ); break;
                case QUEEN:  att = queen_attacks(sq, occ); break;
                default: break;
            }
            int zone_att = count_bits(att & king_zone);
            if (zone_att > 0) {
                attack_count++;
                attack_power += zone_att * ATK_POWER[i];
                if (att & (1ULL << king_sq)) attack_power += CHK_POWER[i];
            }
        }
    }
    if (attack_count <= 1) return 0;
    int danger = attack_power * CNT_MOD[attack_count < 8 ? attack_count : 7];
    return danger / 128;
}

// ============================================================
// Espace (Ethereal) — contrôle du centre
// ============================================================
static int evaluate_space(const Position *pos, Color c) {
    Color enemy = c == WHITE ? BLACK : WHITE;
    int score = 0;
    // Cases centrales (d4, e4, d5, e5)
    const uint64_t center = 0x0000001818000000ULL;
    uint64_t controlled = 0;
    for (int sq = 0; sq < 64; sq++) {
        if (is_square_attacked(pos, (Square)sq, c))
            controlled |= (1ULL << sq);
    }
    uint64_t uncontested = center & controlled & ~pos->occupancy[enemy];
    score += count_bits(uncontested) * 5;
    return score;
}

// ============================================================
// Fermeture (Ethereal) — ajustement cavalier/tour
// ============================================================
static int evaluate_closedness(const Position *pos) {
    int pawns = count_bits(pos->piece[WHITE][PAWN]) + count_bits(pos->piece[BLACK][PAWN]);
    int closed = pawns > 12 ? 1 : 0;
    int score = 0;
    // Cavaliers meilleurs en position fermée
    int wknights = count_bits(pos->piece[WHITE][KNIGHT]) - count_bits(pos->piece[BLACK][KNIGHT]);
    int wrooks = count_bits(pos->piece[WHITE][ROOK]) - count_bits(pos->piece[BLACK][ROOK]);
    if (closed) {
        score += wknights * 10;
        score -= wrooks * 10;
    }
    return score;
}

// ============================================================
// Pions passés par rangée (Weiss)
// ============================================================
static const int passed_pawn_tab[8] = {
    // Recalibré : anciens bonus combinés + Weiss free/blocked en sus
    // Rangs relatifs (0=rang2/7, 6=rang7/2, 7=promotion)
    40, 30, 35, 50, 80, 120, 311, 0
};

static int evaluate_passed_pawns(const Position *pos, Color c) {
    int score = 0;
    uint64_t my_pawns = pos->piece[c][PAWN];
    uint64_t opp_pawns = pos->piece[c == WHITE ? BLACK : WHITE][PAWN];
    Color opp = c == WHITE ? BLACK : WHITE;
    Square opp_king = lsb(pos->piece[opp][KING]);
    Square my_king = lsb(pos->piece[c][KING]);
    
    static const int FREE_ADV[4] = { -4, -12, -23, -70 };
    static const int BLOCKED[4] = { 1, -6, -11, -54 };
    static const int ROOK_BEHIND = 21;

    uint64_t p = my_pawns;
    while (p) {
        Square sq = pop_lsb(&p);
        int rank = sq / 8;
        int file = sq % 8;
        // 03/10/2026 : toute la route jusqu'a la promotion (colonne + colonnes
        // voisines), et plus seulement la rangee suivante : avant, presque tous
        // les pions etaient comptes comme passes.
        uint64_t files = FILE_A << file;
        if (file > 0) files |= FILE_A << (file - 1);
        if (file < 7) files |= FILE_A << (file + 1);
        uint64_t ahead = (c == WHITE) ? (rank < 7 ? ~0ULL << (8 * (rank + 1)) : 0)
                                      : (rank > 0 ? ~0ULL >> (8 * (8 - rank)) : 0);
        uint64_t front = files & ahead;
        if ((front & opp_pawns) == 0) {
            int r = c == WHITE ? rank : 7 - rank;
            if (r > 7) r = 7;
            int bonus = passed_pawn_tab[r];
            
            // Règle du carré : le roi adverse est trop loin de la case de promotion
            // (03/10/2026 : distance relative, avant toujours vraie pour les Noirs)
            int prom_rank = c == WHITE ? 7 : 0;
            int okf = opp_king % 8 - file, okr = opp_king / 8 - prom_rank;
            int kdist = abs(okf) > abs(okr) ? abs(okf) : abs(okr);
            if (r >= 3 && kdist > 7 - r) {
                bonus = bonus * 3 / 2;
            }
            
            // Roi proche
            if (r >= 4) {
                int kd = abs((int)(my_king % 8) - file) + abs((int)(my_king / 8) - rank);
                if (kd <= 3) bonus += (3 - kd) * 15;
            }
            
            // Pion libre / bloqué (Weiss)
            int forward_sq = sq + (c == WHITE ? 8 : -8);
            if (forward_sq >= 0 && forward_sq < 64 && !(pos->occupancy[2] & (1ULL << forward_sq))) {
                if (!is_square_attacked(pos, (Square)forward_sq, opp)) {
                    int ri = r - 4;  // rangee relative (avant : absolue, faux pour les Noirs)
                    if (ri >= 0 && ri < 4) bonus += FREE_ADV[ri];
                }
            } else {
                int ri = r - 4;
                if (ri >= 0 && ri < 4) bonus += BLOCKED[ri];
            }
            
            // Tour derrière le pion
            Square behind = c == WHITE ? sq - 8 : sq + 8;
            if (behind >= 0 && behind < 64 && (pos->piece[c][ROOK] & (1ULL << behind)))
                bonus += ROOK_BEHIND;
            
            score += bonus;
        }
    }
    return score;
}

// ============================================================
// Phalanx de pions (Weiss) — bonus pions côte-à-côte
// ============================================================
static int evaluate_pawn_phalanx(const Position *pos, Color c) {
    int score = 0;
    uint64_t pawns = pos->piece[c][PAWN];
    static const int PHALANX[8] = { 0, 10, 20, 35, 72, 231, 168, 0 };
    uint64_t phalanx = pawns & ((pawns >> 1) & ~FILE_H);
    while (phalanx) {
        Square sq = pop_lsb(&phalanx);
        int rank = c == WHITE ? sq / 8 : 7 - sq / 8;
        score += PHALANX[rank];
    }
    return score;  // l'appelant fait f(BLANC) - f(NOIR) (avant : signe inverse deux fois)
}

// ============================================================
// Pions arriérés (backward pawns)
// ============================================================
static int evaluate_backward_pawns(const Position *pos, Color c) {
    int score = 0;
    uint64_t my_pawns = pos->piece[c][PAWN];
    uint64_t opp_pawns = pos->piece[c == WHITE ? BLACK : WHITE][PAWN];
    int forward = (c == WHITE) ? 8 : -8;
    uint64_t p = my_pawns;
    while (p) {
        Square sq = pop_lsb(&p);
        int file = sq % 8;
        uint64_t adj = 0;
        if (file > 0) adj |= (1ULL << (sq + forward - 1));
        if (file < 7) adj |= (1ULL << (sq + forward + 1));
        bool defended = (adj & my_pawns) != 0;
        int ahead = sq + forward;
        bool can_adv = ahead >= 0 && ahead < 64 && !(pos->occupancy[2] & (1ULL << ahead));
        bool blocked = false;
        if (file > 0 && (opp_pawns & (1ULL << (ahead - 1)))) blocked = true;
        if (file < 7 && (opp_pawns & (1ULL << (ahead + 1)))) blocked = true;
        if (!defended && !can_adv && blocked) score -= 15;
    }
    return score;  // l'appelant fait f(BLANC) - f(NOIR)
}

// ============================================================
// Tour sur colonne ouverte : mobilité avant (Weiss)
// ============================================================
static int evaluate_rook_forward(const Position *pos) {
    int score = 0;
    for (int ci = 0; ci < 2; ci++) {
        Color c = (Color)ci;
        uint64_t rooks = pos->piece[c][ROOK];
        while (rooks) {
            Square sq = pop_lsb(&rooks);
            int file = sq % 8;
            bool pawn_ahead = false, own_ahead = false;
            for (int r = sq/8 + (c==WHITE?1:-1); r >= 0 && r < 8; r += (c==WHITE?1:-1)) {
                Square cs = file + r * 8;
                if (pos->piece[WHITE][PAWN] & (1ULL << cs)) {
                    if (c == WHITE) own_ahead = true;
                    pawn_ahead = true;
                    break;
                }
                if (pos->piece[BLACK][PAWN] & (1ULL << cs)) {
                    if (c == BLACK) own_ahead = true;
                    pawn_ahead = true;
                    break;
                }
            }
            int bonus = 0;
            if (!pawn_ahead) bonus = 28;
            else if (!own_ahead) bonus = 17;
            score += (c == WHITE ? bonus : -bonus);
        }
    }
    return score;
}

// ============================================================
// Qualité des fous (bon/mauvais selon pions sur même couleur)
// ============================================================
static int evaluate_bishop_quality(const Position *pos) {
    int score = 0;
    const uint64_t LIGHT = 0x55AA55AA55AA55AAULL;
    for (int ci = 0; ci < 2; ci++) {
        Color c = (Color)ci;
        uint64_t bishops = pos->piece[c][BISHOP];
        while (bishops) {
            Square sq = pop_lsb(&bishops);
            uint64_t same_color = ((1ULL << sq) & LIGHT) ? LIGHT : ~LIGHT;
            uint64_t bad_pawns = pos->piece[c][PAWN] & same_color;
            int cnt = count_bits(bad_pawns);
            int penalty = (c == WHITE) ? -cnt * 4 : cnt * 4;
            score += penalty;
        }
    }
    return score;
}

// ============================================================
// Clouage — détection des pièces clouées au roi
// ============================================================
static int evaluate_pins(const Position *pos) {
    int score = 0;
    uint64_t occ = pos->occupancy[2];
    
    for (int ci = 0; ci < 2; ci++) {
        Color us = (Color)ci;
        Color them = us == WHITE ? BLACK : WHITE;
        Square our_king = lsb(pos->piece[us][KING]);
        
        // Rook/Queen pins (vertical/horizontal)
        uint64_t sliders = (pos->piece[them][ROOK] | pos->piece[them][QUEEN]);
        uint64_t s = sliders;
        while (s) {
            Square sq = pop_lsb(&s);
            if (rook_attacks(sq, occ) & (1ULL << our_king)) {
                // Check if exactly one piece between slider and king
                uint64_t between = rook_attacks(sq, occ) & rook_attacks(our_king, occ) & occ;
                if (count_bits(between) == 1) {
                    Square pinned_sq = lsb(between);
                    if ((1ULL << pinned_sq) & pos->occupancy[us])
                        score += (us == WHITE) ? -20 : 20;
                }
            }
        }
        
        // Bishop/Queen pins (diagonals)
        uint64_t diag_sliders = (pos->piece[them][BISHOP] | pos->piece[them][QUEEN]);
        s = diag_sliders;
        while (s) {
            Square sq = pop_lsb(&s);
            if (bishop_attacks(sq, occ) & (1ULL << our_king)) {
                uint64_t between = bishop_attacks(sq, occ) & bishop_attacks(our_king, occ) & occ;
                if (count_bits(between) == 1) {
                    Square pinned_sq = lsb(between);
                    if ((1ULL << pinned_sq) & pos->occupancy[us])
                        score += (us == WHITE) ? -20 : 20;
                }
            }
        }
    }
    return score;
}

// ============================================================
// Cases faibles — bonus si ennemi a perdu son fou de cette couleur
// ============================================================
static int evaluate_weak_squares(const Position *pos) {
    int score = 0;
    const uint64_t LIGHT = 0x55AA55AA55AA55AAULL;
    const uint64_t DARK  = 0xAA55AA55AA55AA55ULL;
    
    for (int ci = 0; ci < 2; ci++) {
        Color c = (Color)ci;
        Color enemy = c == WHITE ? BLACK : WHITE;
        bool has_light = (pos->piece[enemy][BISHOP] & LIGHT) != 0;
        bool has_dark   = (pos->piece[enemy][BISHOP] & DARK) != 0;
        
        // 03/10/2026 : seulement s'il manque UN des deux fous adverses. Sans aucun
        // fou adverse, la fonction prenait toujours le meme masque -> asymetrie.
        if (has_light != has_dark) {
            uint64_t our = pos->piece[c][KNIGHT] | pos->piece[c][BISHOP] | pos->piece[c][QUEEN];
            uint64_t weak_color = !has_light ? LIGHT : DARK;
            uint64_t on_weak = our & weak_color;
            int cnt = count_bits(on_weak);
            score += (c == WHITE ? cnt : -cnt) * 8;
        }
    }
    return score;
}

// ============================================================
// Majorité de pions — bonus pour majorité sur un flanc
// ============================================================
static int evaluate_pawn_majority(const Position *pos) {
    int score = 0;
    uint64_t wp = pos->piece[WHITE][PAWN];
    uint64_t bp = pos->piece[BLACK][PAWN];
    
    uint64_t qside = 0x0707070707070707ULL; // a-c
    uint64_t kside = 0xE0E0E0E0E0E0E0E0ULL; // f-h
    
    int wq = count_bits(wp & qside), bk = count_bits(bp & kside);
    int wk = count_bits(wp & kside), bq = count_bits(bp & qside);
    
    // Majorité queenside
    if (wq > bq + 1) score += (wq - bq) * 10;
    else if (bq > wq + 1) score -= (bq - wq) * 10;
    
    // Majorité kingside
    if (wk > bk + 1) score += (wk - bk) * 8;
    else if (bk > wk + 1) score -= (bk - wk) * 8;
    
    return score;
}

// ============================================================
// Blocus — bonus pour pièce bloquant un pion passé adverse
// ============================================================
static int evaluate_blockade(const Position *pos) {
    int score = 0;
    for (int ci = 0; ci < 2; ci++) {
        Color c = (Color)ci;
        Color enemy = c == WHITE ? BLACK : WHITE;
        uint64_t opp_pawns = pos->piece[enemy][PAWN];
        int dir = c == WHITE ? 8 : -8;
        uint64_t p = opp_pawns;
        while (p) {
            Square sq = pop_lsb(&p);
            Square block_sq = sq + dir;
            if (block_sq < 0 || block_sq >= 64) continue;
            if (pos->piece[c][KNIGHT] & (1ULL << block_sq))
                score += (c == WHITE ? 25 : -25);
            if (pos->piece[c][BISHOP] & (1ULL << block_sq))
                score += (c == WHITE ? 18 : -18);
        }
    }
    return score;
}

// ============================================================
// Pions suspendus — pions sans soutien de pions adjacents
// ============================================================
static int evaluate_suspended_pawns(const Position *pos) {
    int score = 0;
    for (int ci = 0; ci < 2; ci++) {
        Color c = (Color)ci;
        uint64_t pawns = pos->piece[c][PAWN];
        uint64_t p = pawns;
        while (p) {
            Square sq = pop_lsb(&p);
            int file = sq % 8, rank = sq / 8;
            int back = c == WHITE ? rank - 1 : rank + 1;
            if (back < 0 || back > 7) continue;
            uint64_t support = 0;
            if (file > 0) support |= (1ULL << (back * 8 + file - 1));
            if (file < 7) support |= (1ULL << (back * 8 + file + 1));
            if (!(support & pawns) && rank >= (c == WHITE ? 3 : 0) && rank <= (c == WHITE ? 6 : 4))
                score += (c == WHITE ? -12 : 12);
        }
    }
    return score;
}

// ============================================================
// Leviers — pions qui peuvent percuter une chaîne adverse
// ============================================================
static int evaluate_pawn_breaks(const Position *pos) {
    int score = 0;
    for (int ci = 0; ci < 2; ci++) {
        Color c = (Color)ci;
        Color enemy = c == WHITE ? BLACK : WHITE;
        uint64_t my_pawns = pos->piece[c][PAWN];
        uint64_t opp_pawns = pos->piece[enemy][PAWN];
        int dir = c == WHITE ? 8 : -8;
        uint64_t p = my_pawns;
        while (p) {
            Square sq = pop_lsb(&p);
            int file = sq % 8;
            Square ahead = sq + dir;
            if (ahead < 0 || ahead >= 64) continue;
            if (pos->occupancy[2] & (1ULL << ahead)) continue;
            // Attaque après avance ?
            uint64_t atk = 0;
            if (file > 0) atk |= (1ULL << (ahead - 1));
            if (file < 7) atk |= (1ULL << (ahead + 1));
            if (atk & opp_pawns) score += (c == WHITE ? 8 : -8);
        }
    }
    return score;
}

// ============================================================
// Pire pièce — bonus pour activer une pièce coincée au camp
// ============================================================
static int evaluate_worst_piece(const Position *pos) {
    int score = 0;
    for (int ci = 0; ci < 2; ci++) {
        Color c = (Color)ci;
        uint64_t pieces = pos->piece[c][KNIGHT] | pos->piece[c][BISHOP];
        int back_rank = c == WHITE ? 0 : 7;
        uint64_t on_back = pieces & (0xFFULL << (back_rank * 8));
        int cnt = count_bits(on_back);
        score += (c == WHITE ? -cnt : cnt) * 12;
    }
    return score;
}

int evaluate(const Position *pos) {
    if (use_nnue && nnue_is_loaded()) {
        return nnue_evaluate(pos);
    }
    
    int mg = 0, eg = 0;
    int phase = game_phase(pos);
    
    // Matériel (inchangé en fin de partie)
    mg += evaluate_material(pos);
    eg += evaluate_material(pos);
    
    // Positionnement des pièces (tables séparées MG/EG)
    mg += evaluate_piece_position(pos, WHITE, PAWN, pawn_table);
    mg += evaluate_piece_position(pos, BLACK, PAWN, pawn_table);
    mg += evaluate_piece_position(pos, WHITE, KNIGHT, knight_table);
    mg += evaluate_piece_position(pos, BLACK, KNIGHT, knight_table);
    mg += evaluate_piece_position(pos, WHITE, BISHOP, bishop_table);
    mg += evaluate_piece_position(pos, BLACK, BISHOP, bishop_table);
    mg += evaluate_piece_position(pos, WHITE, ROOK, rook_table);
    mg += evaluate_piece_position(pos, BLACK, ROOK, rook_table);
    mg += evaluate_piece_position(pos, WHITE, QUEEN, queen_table);
    mg += evaluate_piece_position(pos, BLACK, QUEEN, queen_table);
    
    // Fin de partie: roi actif
    eg += evaluate_piece_position(pos, WHITE, KING, king_endgame_table);
    eg += evaluate_piece_position(pos, BLACK, KING, king_endgame_table);
    
    // Structure de pions
    mg += evaluate_pawn_structure(pos);
    eg += evaluate_pawn_structure(pos);
    
    // Sécurité du roi
    mg += evaluate_king_safety_improved(pos);
    
    // Mobilité par type de pièce (Weiss/Ethereal)
    if (phase >= 64) {
        mg += evaluate_piece_mobility(pos, WHITE) - evaluate_piece_mobility(pos, BLACK);
    }

    // Menaces (Weiss)
    if (phase >= 64) {
        mg += evaluate_threats(pos, WHITE) - evaluate_threats(pos, BLACK);
    }

    // Sécurité roi avec comptage d'attaques (Weiss)
    if (phase >= 64) {
        mg += evaluate_king_attack(pos, WHITE) - evaluate_king_attack(pos, BLACK);
    }
    
    // King flank attacks
    if (phase >= 64) {
        mg += evaluate_king_flank(pos, WHITE) - evaluate_king_flank(pos, BLACK);
    }

    // Pions passés par rangée (Weiss)
    mg += evaluate_passed_pawns(pos, WHITE) - evaluate_passed_pawns(pos, BLACK);
    eg += evaluate_passed_pawns(pos, WHITE) - evaluate_passed_pawns(pos, BLACK);
    
    // Phalanx de pions
    mg += evaluate_pawn_phalanx(pos, WHITE) - evaluate_pawn_phalanx(pos, BLACK);
    
    // Pions arriérés
    mg += evaluate_backward_pawns(pos, WHITE) - evaluate_backward_pawns(pos, BLACK);
    eg += evaluate_backward_pawns(pos, WHITE) - evaluate_backward_pawns(pos, BLACK);

    // Espace (Ethereal) — contrôle du centre
    if (phase >= 64) {
        mg += evaluate_space(pos, WHITE) - evaluate_space(pos, BLACK);
    }

    // Fermeture (Ethereal) — ajustement cavalier/tour
    mg += evaluate_closedness(pos);
    eg += evaluate_closedness(pos);

    // Pénalité tour hors rangée 1 avant roque (anti-Rxa7)
    if (phase >= 64) {
        Square wk = lsb(pos->piece[WHITE][KING]);
        if (wk == E1) {
            uint64_t wr = pos->piece[WHITE][ROOK];
            while (wr) {
                Square rsq = pop_lsb(&wr);
                if (rsq / 8 != 0) mg -= 50;
            }
        }
        Square bk = lsb(pos->piece[BLACK][KING]);
        if (bk == E8) {
            uint64_t brp = pos->piece[BLACK][ROOK];
            while (brp) {
                Square rsq = pop_lsb(&brp);
                if (rsq / 8 != 7) mg += 50;
            }
        }
    }

    // Paires de fous (phase déjà gérée en interne)
    mg += evaluate_bishop_pair(pos, phase);
    
    // Qualité des fous (bon/mauvais)
    mg += evaluate_bishop_quality(pos);
    eg += evaluate_bishop_quality(pos);
    
    // Tours sur fiches ouvertes
    mg += evaluate_rook_files(pos);
    eg += evaluate_rook_files(pos);
    
    // Développement
    mg += evaluate_development(pos);
    
    // Clouage — pièces clouées au roi
    mg += evaluate_pins(pos);
    
    // Cases faibles
    mg += evaluate_weak_squares(pos);
    
    // Majorité de pions
    mg += evaluate_pawn_majority(pos);
    eg += evaluate_pawn_majority(pos);
    
    // Avant-postes
    mg += evaluate_outposts(pos);
    
    // Blocus — pièce bloquant pion passé adverse
    mg += evaluate_blockade(pos);
    eg += evaluate_blockade(pos);
    
    // Pions suspendus
    mg += evaluate_suspended_pawns(pos);
    eg += evaluate_suspended_pawns(pos);
    
    // Leviers
    mg += evaluate_pawn_breaks(pos);
    
    // Pire pièce
    mg += evaluate_worst_piece(pos);
    
    // Tours sur 7e
    mg += evaluate_rook_seventh(pos);
    eg += evaluate_rook_seventh(pos);
    
    // Tours : mobilité avant (Weiss)
    mg += evaluate_rook_forward(pos);
    eg += evaluate_rook_forward(pos);

    // Pièces piégées (CPW: fou h7/h6, cavalier h7/h8, tour bloquée par roi)
    mg += evaluate_trapped_pieces(pos);
    eg += evaluate_trapped_pieces(pos);

    // Connectivité (Kopec simplifié): nombre de défenseurs par pièce
    // non-pion/non-roi. Encourage chaînes de pions et protection mutuelle.
    // Bonus modeste: 6 cp par défenseur, max 3 par pièce pour ne pas exploser.
    int conn = evaluate_connectivity(pos);
    mg += conn;
    eg += conn;

    // Motifs: pions passés, passés connectés, bonus d'avancement
    int pat = evaluate_patterns(pos);
    mg += pat / 2;  // pondéré en MG (moins dominant)
    eg += pat;       // dominant en EG

    // Tapered evaluation: interpolation MG/EG
    int score = (mg * phase + eg * (256 - phase)) / 256;  // phase 256 = ouverture
    
    // Scale factor : réduit le score dans les positions drawish
    // (peu de pions, fous de couleurs opposées, pions sur un seul flanc)
    Color strong = score > 0 ? WHITE : BLACK;
    int strong_pawns = count_bits(pos->piece[strong][PAWN]);
    int x = 8 - strong_pawns;
    int scale = 128 - x * x;  // Moins de pions = plus drawish
    if (scale < 32) scale = 32;
    
    // Pions sur un seul flanc ?
    uint64_t w_pawns_all = pos->piece[WHITE][PAWN];
    uint64_t b_pawns_all = pos->piece[BLACK][PAWN];
    int w_left = count_bits(w_pawns_all & 0x0707070707070707ULL); // a-c
    int w_right = count_bits(w_pawns_all & 0xE0E0E0E0E0E0E0E0ULL); // f-h
    int b_left = count_bits(b_pawns_all & 0x0707070707070707ULL);
    int b_right = count_bits(b_pawns_all & 0xE0E0E0E0E0E0E0E0ULL);
    
    if (w_left == 0 || w_right == 0) scale -= 20;
    if (b_left == 0 || b_right == 0) scale -= 20;
    
    // Fous de couleurs opposées (déjà géré en partie, mais renforcé ici)
    int wb = count_bits(pos->piece[WHITE][BISHOP]);
    int bb = count_bits(pos->piece[BLACK][BISHOP]);
    if (wb == 1 && bb == 1) {
        const uint64_t LIGHT = 0x55AA55AA55AA55AAULL;
        if (((pos->piece[WHITE][BISHOP] & LIGHT) != 0) != 
            ((pos->piece[BLACK][BISHOP] & LIGHT) != 0)) {
            if (scale > 96) scale = 96;
            if (phase < 128 && strong_pawns <= 2) scale = 64;
        }
    }
    
    if (scale < 16) scale = 16;
    score = score * scale / 128;

    // Tempo: petit bonus au camp qui a le trait (side-to-move)
    // Évite l'oscillation odd/even depth-parity en quiescence.
    // Universel: actif en MG et EG (la phase est déjà intégrée dans score).
    // Ajouté APRÈS le flip: le retour est du point de vue du side_to_move,
    // donc le bonus va directement au camp actif.
    const int TEMPO_BONUS = 10;
    
    // Hack d'urgence pour annuler le biais de +/-370 à startpos (v2 SYMÉTRIQUE)
    // Le biais HCE brut vaut ~+360cp du point de vue des blancs, soit ±360 selon
    // le camp au trait à la feuille. L'ancienne version ne corrigeait qu'un signe.
    int final_score = (pos->side_to_move == WHITE ? score : -score) + TEMPO_BONUS;
    return final_score;
}
