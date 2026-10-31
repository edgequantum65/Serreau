/**
 * tb_syzygy.c - Adaptateur Syzygy via Fathom pour le moteur Chess
 *
 * Remplace l'ancien tb.c (fake). Appelle tb_init_impl() au démarrage,
 * tb_probe_wdl_impl() pendant la recherche.
 *
 * On n'inclut PAS tbprobe.h (déclare des inline qui clash avec notre tb_probe_wdl).
 * On déclare les fonctions _impl directement.
 */

#include "chess_engine.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* Fathom internal functions (from tbprobe.c) */
extern bool tb_init(const char *_path);
extern void tb_free(void);
extern unsigned tb_probe_wdl_impl(
    uint64_t _white, uint64_t _black,
    uint64_t _kings, uint64_t _queens,
    uint64_t _rooks, uint64_t _bishops,
    uint64_t _knights, uint64_t _pawns,
    unsigned _ep, bool _turn);
extern unsigned TB_LARGEST;

/* TB result codes */
#define TB_LOSS             0
#define TB_BLESSED_LOSS     1
#define TB_DRAW             2
#define TB_CURSED_WIN       3
#define TB_WIN              4
#define TB_RESULT_FAILED    0xFFFFFFFF

/* Path to Syzygy tablebase files — searched in order */
static const char *syzygy_paths[] = {
    "E:\\ChessEngine\\syzygy",
    "syzygy",
    NULL
};

static bool syzygy_initialized = false;
static unsigned syzygy_largest = 0;

/* ======================================================================
 * Initialisation — appelée une seule fois au démarrage
 * ===================================================================== */
void tb_init_syzygy(void) {
    if (syzygy_initialized) return;
    
    for (const char **path = syzygy_paths; *path; path++) {
        if (tb_init(*path)) {
            syzygy_largest = TB_LARGEST;
            syzygy_initialized = true;
            printf("Syzygy tablebases: OK (%u-man, path=%s)\n", syzygy_largest, *path);
            return;
        }
    }
    
    printf("Syzygy tablebases: not found\n");
    syzygy_largest = 0;
}

/* ======================================================================
 * Probe WDL pendant la recherche (thread-safe)
 * Retourne : 2=WIN, 0=DRAW, -1=LOSS, -2=FAILED
 * ===================================================================== */
int tb_probe_syzygy(const Position *pos) {
    if (!syzygy_initialized || syzygy_largest == 0) return -2;
    
    /* Compter les pièces */
    int piece_count = count_bits(pos->occupancy[2]);
    if (piece_count > (int)syzygy_largest) return -2;
    if (piece_count < 2) return -2; /* insufficient pieces */
    
    /* Castling: Fathom échoue si castling != 0, on skip */
    if (pos->castling != 0) return -2;
    
    /* Halfmove clock: Fathom WDL needs rule50=0 for accuracy.
     * With non-zero rule50, skip probing to avoid incorrect results. */
    if (pos->halfmove_clock > 0) return -2;
    
    /* NOTE: Fathom probing disabled pending debug of internal loop issue.
     * tb_init still validates that files are loadable (syzygy_largest > 0).
     * TODO: debug the lsb() infinite loop in Fathom for KQK+ positions. */
    return -2;
    
    /* Construire les bitboards au format Fathom */
    uint64_t white   = pos->occupancy[WHITE];
    uint64_t black   = pos->occupancy[BLACK];
    uint64_t kings   = pos->piece[WHITE][KING]   | pos->piece[BLACK][KING];
    uint64_t queens  = pos->piece[WHITE][QUEEN]  | pos->piece[BLACK][QUEEN];
    uint64_t rooks   = pos->piece[WHITE][ROOK]   | pos->piece[BLACK][ROOK];
    uint64_t bishops = pos->piece[WHITE][BISHOP] | pos->piece[BLACK][BISHOP];
    uint64_t knights = pos->piece[WHITE][KNIGHT] | pos->piece[BLACK][KNIGHT];
    uint64_t pawns   = pos->piece[WHITE][PAWN]   | pos->piece[BLACK][PAWN];
    
    /* En passant */
    unsigned ep = 0;
    if (pos->en_passant != NO_SQUARE) {
        ep = (unsigned)pos->en_passant;
    }
    
    /* Side to move */
    bool turn = (pos->side_to_move == WHITE);
    
    /* Appel Fathom */
    unsigned result = tb_probe_wdl_impl(white, black, kings, queens, rooks,
                                        bishops, knights, pawns, ep, turn);
    
    if (result == TB_RESULT_FAILED) return -2;
    
    /* Convertir au format moteur : 2=WIN, 0=DRAW, -1=LOSS */
    switch (result) {
        case TB_WIN:          return 2;
        case TB_CURSED_WIN:   return 2;  /* win (even if cursed) */
        case TB_DRAW:         return 0;
        case TB_BLESSED_LOSS: return -1; /* loss (even if blessed) */
        case TB_LOSS:         return -1;
        default:              return -2;
    }
}

/* ======================================================================
 * Probe WDL original (compatibilité avec search.c)
 * ===================================================================== */
int tb_probe_wdl(const Position *pos) {
    return tb_probe_syzygy(pos);
}

/* ======================================================================
 * Probe move (compatibilité — WDL-only, pas de move)
 * ===================================================================== */
bool tb_probe_move(const Position *pos, Move *best_move) {
    (void)pos;
    (void)best_move;
    return false;
}

/* ======================================================================
 * Cleanup
 * ===================================================================== */
void tb_free_syzygy(void) {
    if (syzygy_initialized) {
        tb_free();
        syzygy_initialized = false;
    }
}
