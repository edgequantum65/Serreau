#include "chess_engine.h"
#include <stdio.h>
#include <stdlib.h>

// Zobrist tables (definitions)
uint64_t zobrist_piece[2][7][64];
uint64_t zobrist_castling[16];
uint64_t zobrist_en_passant[65];
uint64_t zobrist_side;

// Polyglot standard random array (781 values, pre-computed)
// Source: python-chess polyglot module — exact same table used by Stockfish, Crafty, etc.
#include "polyglot_keys.inc"

void init_zobrist(void) {
    // Polyglot standard random array for book probing
    static const int pg_map[2][7] = {
        {0, 0, 2, 4, 6, 8, 10},  // WHITE: _, P, N, B, R, Q, K
        {0, 1, 3, 5, 7, 9, 11}   // BLACK: _, P, N, B, R, Q, K
    };

    for (int c = 0; c < 2; c++) {
        for (int p = 1; p <= 6; p++) {
            int pg_idx = pg_map[c][p];
            for (int s = 0; s < 64; s++) {
                zobrist_piece[c][p][s] = POLYGLOT_RANDOM[pg_idx * 64 + s];
            }
        }
    }

    // Internal Zobrist tables for engine (make/unmake, set_fen, etc.)
    // These don't need to match Polyglot — just internally consistent
    for (int i = 0; i < 16; i++) {
        zobrist_castling[i] = POLYGLOT_RANDOM[768 + (i % 4)] ^ (uint64_t)i * 0x9E3779B97F4A7C15ULL;
    }
    for (int i = 0; i < 64; i++) {
        zobrist_en_passant[i] = POLYGLOT_RANDOM[772 + (i % 8)] ^ (uint64_t)i * 0xBF58476D1CE4E5B9ULL;
    }
    zobrist_en_passant[NO_SQUARE] = 0;
    zobrist_side = POLYGLOT_RANDOM[780];
}

uint64_t polyglot_key(const Position *pos) {
    uint64_t key = 0;

    // Pieces: même logique que python-chess ZobristHasher.hash_board
    for (int c = 0; c < 2; c++) {
        for (int p = PAWN; p <= KING; p++) {
            uint64_t bb = pos->piece[c][p];
            while (bb) {
                Square sq = pop_lsb(&bb);
                // Polyglot standard : pion noir = 0, pion blanc = 1, cavalier noir = 2...
                // (WHITE vaut 0 dans le moteur, d'ou le 1 - c)
                int piece_index = (p - 1) * 2 + (1 - c);
                key ^= POLYGLOT_RANDOM[64 * piece_index + sq];
            }
        }
    }

    // Side to move: Polyglot XORs on WHITE (index 780)
    if (pos->side_to_move == WHITE) key ^= POLYGLOT_RANDOM[780];

    // Castling: pure Polyglot standard keys (indices 768-771)
    if (pos->castling & 1) key ^= POLYGLOT_RANDOM[768]; // White K-side
    if (pos->castling & 2) key ^= POLYGLOT_RANDOM[769]; // White Q-side
    if (pos->castling & 4) key ^= POLYGLOT_RANDOM[770]; // Black K-side
    if (pos->castling & 8) key ^= POLYGLOT_RANDOM[771]; // Black Q-side

    // En passant: pure Polyglot standard (index 772+file)
    if (pos->en_passant != NO_SQUARE) {
        int ep_file = pos->en_passant % 8;
        int ep_rank = pos->en_passant / 8;
        // Les pions qui capturent sont sur le rang adjacent, pas sur le rang ep
        int pawn_rank = (pos->side_to_move == WHITE) ? ep_rank - 1 : ep_rank + 1;
        if (pawn_rank >= 0 && pawn_rank <= 7) {
            uint64_t adjacent = 0;
            if (ep_file > 0) adjacent |= 1ULL << (pawn_rank * 8 + ep_file - 1);
            if (ep_file < 7) adjacent |= 1ULL << (pawn_rank * 8 + ep_file + 1);
            if (adjacent & pos->piece[pos->side_to_move][PAWN]) {
                key ^= POLYGLOT_RANDOM[772 + ep_file];
            }
        }
    }

    return key;
}

// ============================================================
// Polyglot Book Reader
// ============================================================

#define POLYGLOT_BOOK_MAX 2000000

typedef struct {
    uint64_t key;
    uint16_t move;
    uint16_t weight;
    uint32_t learn;
} PolyglotEntry;

static PolyglotEntry *polyglot_book = NULL;
static int polyglot_book_count = 0;

static int polyglot_cmp(const void *a, const void *b) {
    const PolyglotEntry *ea = (const PolyglotEntry *)a;
    const PolyglotEntry *eb = (const PolyglotEntry *)b;
    if (ea->key < eb->key) return -1;
    if (ea->key > eb->key) return 1;
    return 0;
}

static void unpack_move(uint16_t packed, Square *from, Square *to, Piece *promo, int *move_type) {
    // Polyglot standard : bits 0-5 = arrivee, bits 6-11 = depart, cases A1=0 comme le moteur
    *to = (Square)(packed & 0x3F);
    *from = (Square)((packed >> 6) & 0x3F);
    int p = (packed >> 12) & 7;
    *promo = EMPTY;
    *move_type = QUIET;
    if (p == 1) { *promo = KNIGHT; *move_type = KNIGHT_PROMO; }
    else if (p == 2) { *promo = BISHOP; *move_type = BISHOP_PROMO; }
    else if (p == 3) { *promo = ROOK; *move_type = ROOK_PROMO; }
    else if (p == 4) { *promo = QUEEN; *move_type = QUEEN_PROMO; }
}

// Polyglot code le roque comme "le roi prend sa tour" (e1h1, e1a1) :
// on le convertit en coup de roi de deux cases (e1g1, e1c1) comme le moteur.
static void fix_castling(const Position *pos, Square from, Square *to) {
    if (!(pos->piece[pos->side_to_move][KING] & (1ULL << from))) return;
    int df = (*to % 8) - (from % 8);
    if (df > 1) *to = (Square)(from + 2);
    else if (df < -1) *to = (Square)(from - 2);
}

int polyglot_book_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    if (polyglot_book) { free(polyglot_book); polyglot_book = NULL; }
    polyglot_book_count = 0;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    int n = file_size / 16;
    if (n <= 0 || n > POLYGLOT_BOOK_MAX) { fclose(f); return 0; }

    polyglot_book = (PolyglotEntry *)malloc((size_t)n * sizeof(PolyglotEntry));
    if (!polyglot_book) { fclose(f); return 0; }

    size_t r = fread(polyglot_book, 16, n, f);
    fclose(f);
    if (r != (size_t)n) { free(polyglot_book); polyglot_book = NULL; return 0; }

    for (int i = 0; i < n; i++) {
        uint8_t *raw = (uint8_t *)&polyglot_book[i];
        polyglot_book[i].key    = ((uint64_t)raw[0] << 56) | ((uint64_t)raw[1] << 48) | ((uint64_t)raw[2] << 40) | ((uint64_t)raw[3] << 32) | ((uint64_t)raw[4] << 24) | ((uint64_t)raw[5] << 16) | ((uint64_t)raw[6] << 8) | (uint64_t)raw[7];
        polyglot_book[i].move   = ((uint16_t)raw[8] << 8) | raw[9];
        polyglot_book[i].weight = ((uint16_t)raw[10] << 8) | raw[11];
        polyglot_book[i].learn  = ((uint32_t)raw[12] << 24) | ((uint32_t)raw[13] << 16) | ((uint32_t)raw[14] << 8) | raw[15];
    }

    polyglot_book_count = n;
    qsort(polyglot_book, n, sizeof(PolyglotEntry), polyglot_cmp);
    return n;
}

bool polyglot_book_probe(const Position *pos, Move *best_move) {
    if (!polyglot_book || polyglot_book_count == 0) return false;
    uint64_t key = polyglot_key(pos);
#ifdef DEBUG_BOOK
    fprintf(stderr, "DEBUG polyglot_key=0x%016llx castling=%d ep=%d side=%d\n",
            (unsigned long long)key, pos->castling, pos->en_passant, pos->side_to_move);
#endif

    PolyglotEntry target;
    target.key = key;
    PolyglotEntry *base = (PolyglotEntry *)bsearch(&target, polyglot_book, polyglot_book_count, sizeof(PolyglotEntry), polyglot_cmp);
    if (!base) return false;

    PolyglotEntry *start = base;
    while (start > polyglot_book && (start - 1)->key == key) start--;
    PolyglotEntry *end = base;
    while ((end + 1) < polyglot_book + polyglot_book_count && (end + 1)->key == key) end++;

    int total_weight = 0;
    for (PolyglotEntry *e = start; e <= end; e++) {
        total_weight += e->weight;
    }

    if (total_weight <= 0) {
        int idx = rand() % (int)(end - start + 1);
        Square from, to; Piece promo; int move_type;
        unpack_move(start[idx].move, &from, &to, &promo, &move_type);
        fix_castling(pos, from, &to);
        best_move->from = from; best_move->to = to;
        best_move->type = move_type; best_move->promo_piece = promo;
        return true;
    }

    int pick = rand() % total_weight;
    for (PolyglotEntry *e = start; e <= end; e++) {
        pick -= e->weight;
        if (pick < 0) {
            Square from, to; Piece promo; int move_type;
            unpack_move(e->move, &from, &to, &promo, &move_type);
            fix_castling(pos, from, &to);
            best_move->from = from; best_move->to = to;
            best_move->type = move_type; best_move->promo_piece = promo;
            return true;
        }
    }

    return false;
}
