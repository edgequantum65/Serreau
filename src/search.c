#include "chess_engine.h"
#include "nnue.h"
#include "magic.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================
// Formatage score UCI conforme protocole
// Les scores proches de MATE_SCORE sont des distances-mat :
// ils DOIVENT sortir en "score mate N" (jamais "score cp").
// ============================================================
static void format_uci_score(int score, char *buf, size_t bufsz) {
    const int MATE_BOUND = MATE_SCORE - 512;
    if (score >= MATE_BOUND) {
        int plies = MATE_SCORE - score;
        snprintf(buf, bufsz, "score mate %d", (plies + 1) / 2);
    } else if (score <= -MATE_BOUND) {
        int plies = MATE_SCORE + score;
        snprintf(buf, bufsz, "score mate -%d", (plies + 1) / 2);
    } else {
        snprintf(buf, bufsz, "score cp %d", score);
    }
}

// ============================================================
// Move Ordering
// ============================================================

#define MAX_KILLERS 2
#define MAX_PLY 64

/* Syzygy WDL score: large CP value, NOT mate. WDL doesn't give distance-to-mate,
 * so we use 29000 (< MATE_BOUND=31488) to avoid "score mate 0" in UCI output.
 * Ply subtraction gives slight preference to quicker wins in search. */
#define TB_SCORE 29000

static Move killer_moves[MAX_PLY][MAX_KILLERS];
static int history_table[2][64][64]; // [color][from][to]
static int capture_history[2][64][64]; // [color][from][to] — capture performance
static Move countermove_table[64][64]; // [opponent_from][opponent_to] = our best response
MAYBE_UNUSED static Move last_moves[MAX_PLY]; // last quiet move played at each ply (for countermove indexing)
static uint64_t search_nodes = 0;
static int time_limit_ms = 1000;
static uint64_t search_start_time = 0;
static bool time_limit_enabled = false;
/* TIME1 (02/10) : go movetime = limite DURE. Le time management dynamique
 * (×1.5 sur score instable) NE DOIT PAS s'appliquer dans ce mode. */
static bool search_movetime_hard = false;
/* TIME1 : meilleur coup partiel de la racine, mémorisé dès qu'un coup améliore
 * best_score à ply==0, pour ne JAMAIS rendre 0000 si l'itération est coupée. */
static Move root_partial_best = {NO_SQUARE, NO_SQUARE, QUIET, EMPTY};
static bool root_partial_found = false;
bool search_stopped = false;
int search_side = WHITE;
int multipv_count = 1;
int search_seldepth = 0;
static NNUEAccumulator nnue_stack[MAX_PLY][2];
static uint64_t rep_stack[512];
static int rep_sp = 0;

static bool is_repetition(uint64_t key) {
    int count = 0;
    for (int i = 0; i < rep_sp; i++) {
        if (rep_stack[i] == key) {
            count++;
            if (count >= 2) return true;
        }
    }
    return false;
}

static int get_static_eval(const Position *pos, int ply) {
    if (evaluation_get_nnue() && nnue_is_loaded()) {
        int eval = nnue_forward_fast(nnue_stack[ply], pos->side_to_move);
        return eval;
    }
    return evaluate(pos);
}

static void clear_history(void) {
    memset(killer_moves, 0, sizeof(killer_moves));
    memset(history_table, 0, sizeof(history_table));
    memset(capture_history, 0, sizeof(capture_history));
    memset(countermove_table, 0, sizeof(countermove_table));
}

/* AXE search : decay history entre itérations (divise par 2).
 * Évite que les vieux scores history dominent le tri en profondeur. */
static void decay_history(void) {
    for (int c = 0; c < 2; c++)
        for (int f = 0; f < 64; f++)
            for (int t = 0; t < 64; t++) {
                history_table[c][f][t] /= 2;
                capture_history[c][f][t] /= 2;
            }
}

// Forward declarations
static int score_capture(const Position *pos, Move move, Color side_to_move);

void clear_search_state(void) {
    clear_history();
    rep_sp = 0;
}

static void store_killer(Move move, int ply) {
    if (ply < MAX_PLY) {
        // Décaler les killers
        if (killer_moves[ply][0].from != move.from || killer_moves[ply][0].to != move.to) {
            killer_moves[ply][1] = killer_moves[ply][0];
            killer_moves[ply][0] = move;
        }
    }
}

static bool is_killer(Move move, int ply) {
    if (ply >= MAX_PLY) return false;
    return (killer_moves[ply][0].from == move.from && killer_moves[ply][0].to == move.to) ||
           (killer_moves[ply][1].from == move.from && killer_moves[ply][1].to == move.to);
}

static void store_history(Move move, Color color, int depth) {
    if (move.from < 64 && move.to < 64) {
        history_table[color][move.from][move.to] += depth * depth;
    }
}

static int get_history(Move move, Color color) {
    if (move.from < 64 && move.to < 64) {
        return history_table[color][move.from][move.to];
    }
    return 0;
}

// Score un coup pour le tri
static int score_move(const Position *pos, Move move, Move tt_move, int ply,
                      Move opp_last_move) {
    // TT move = score le plus élevé
    if (move.from == tt_move.from && move.to == tt_move.to) {
        return 1000000;
    }
    
    // Captures: utiliser SEE + capture history pour le tri
    if (move.type == CAPTURE || move.type >= KNIGHT_PROMO_CAP) {
        return score_capture(pos, move, pos->side_to_move);
    }
    
    // Promotions
    if (move.promo_piece != EMPTY) {
        return 90000 + move.promo_piece;
    }
    
    // Killer moves
    if (is_killer(move, ply)) {
        return 80000;
    }
    
    // Countermove heuristic : le coup qui a causé un cutoff face au dernier coup adverse
    if (opp_last_move.from != NO_SQUARE &&
        countermove_table[opp_last_move.from][opp_last_move.to].from == move.from &&
        countermove_table[opp_last_move.from][opp_last_move.to].to == move.to) {
        return 70000;
    }
    
    // History heuristic
    return get_history(move, pos->side_to_move);
}

// Trier les coups
static void sort_moves(const Position *pos, MoveList *list, Move tt_move, int ply,
                       Move opp_last_move) {
    int scores[MAX_MOVES];
    
    for (int i = 0; i < list->count; i++) {
        scores[i] = score_move(pos, list->moves[i], tt_move, ply, opp_last_move);
    }
    
    // Insertion sort (assez bon pour <= 256 coups)
    for (int i = 1; i < list->count; i++) {
        int key = scores[i];
        Move move_key = list->moves[i];
        int j = i - 1;
        
        while (j >= 0 && scores[j] < key) {
            scores[j + 1] = scores[j];
            list->moves[j + 1] = list->moves[j];
            j--;
        }
        
        scores[j + 1] = key;
        list->moves[j + 1] = move_key;
    }
}

// ============================================================
// Tables de transposition (basiques)
// ============================================================

#define TT_SIZE (1 << 22) // 4M entrées (64 Mo — plus de collisions en blitz)

typedef struct {
    uint64_t key;
    Move best_move;
    int score;
    int depth;
    TTFlag flag;
    uint8_t age;
    uint8_t pad[3];
} TTEntry;

static TTEntry transposition_table[TT_SIZE];
static uint8_t tt_current_age = 0;

void tt_clear(void) {
    memset(transposition_table, 0, sizeof(transposition_table));
    tt_current_age = 0;
}

void tt_new_search(void) {
    tt_current_age++;
}

void tt_store(uint64_t key, Move best_move, int score, int depth, TTFlag flag) {
    int idx = key % TT_SIZE;
    TTEntry *entry = &transposition_table[idx];
    
    // Remplacement intelligent (Stockfish-style) :
    // 1. Même clé : depth-preferred, mais écrase si stale (age different)
    // 2. Clé différente : écrase si stale OU depth >= entry depth
    if (entry->key != 0) {
        if (entry->key == key) {
            if (entry->depth > depth && entry->age == tt_current_age) {
                return;
            }
        } else {
            if (entry->age != tt_current_age && entry->depth > depth + 2) {
                return;
            }
        }
    }
    
    entry->key = key;
    entry->best_move = best_move;
    entry->score = score;
    entry->depth = depth;
    entry->flag = flag;
    entry->age = tt_current_age;
}

bool tt_probe(uint64_t key, int depth, int alpha, int beta, int *score, Move *best_move) {
    int idx = key % TT_SIZE;
    TTEntry *entry = &transposition_table[idx];
    
    if (entry->key != key) {
        return false;
    }
    
    if (entry->depth >= depth) {
        if (entry->flag == TT_EXACT) {
            *score = entry->score;
            *best_move = entry->best_move;
            return true;
        } else if (entry->flag == TT_ALPHA && entry->score <= alpha) {
            *score = alpha;
            return true;
        } else if (entry->flag == TT_BETA && entry->score >= beta) {
            *score = beta;
            return true;
        }
    }
    
    *score = entry->score;  // used by singular extensions, never as a cutoff
    *best_move = entry->best_move;
    return false;
}

int tt_hashfull(void) {
    int used = 0;
    int sample = 1024;
    if (sample > TT_SIZE) sample = TT_SIZE;
    for (int i = 0; i < sample; i++) {
        if (transposition_table[i].key != 0) used++;
    }
    return (used * 1000) / sample;
}

void tt_resize(int mb) {
    (void)mb;
}

// ============================================================
// Quiescence Search
// ============================================================

// Score de capture pour le tri MVV-LVA (fallback)
MAYBE_UNUSED static int score_capture_raw(const Position *pos, Move move) {
    Color enemy = pos->side_to_move == WHITE ? BLACK : WHITE;
    Piece victim = EMPTY;
    Piece attacker = EMPTY;
    
    for (int p = PAWN; p <= KING; p++) {
        if (pos->piece[enemy][p] & (1ULL << move.to)) {
            victim = (Piece)p;
            break;
        }
    }
    for (int p = PAWN; p <= KING; p++) {
        if (pos->piece[pos->side_to_move][p] & (1ULL << move.from)) {
            attacker = (Piece)p;
            break;
        }
    }
    
    static const int piece_values[] = {0, 100, 320, 330, 500, 900, 0};
    return piece_values[victim] * 10 - piece_values[attacker];
}

// ============================================================
// Static Exchange Evaluation (SEE)
// ============================================================

static const int see_piece_val[] = {0, 100, 300, 300, 500, 900, 20000};

// Vérifie si une pièce donnée sur la case 'from' attaque la case 'to'
static bool piece_attacks_square(Square from, Square to, Piece p, uint64_t occ) {
    int ft = from % 8, tt = to % 8;
    int fr = from / 8, tr = to / 8;
    
    switch (p) {
        case PAWN: {
            // Les pions capturent en diagonale (dépend de la couleur, mais on teste les deux)
            int diff = to - from;
            return diff == 7 || diff == 9 || diff == -7 || diff == -9;
        }
        case KNIGHT: {
            int df = abs(ft - tt);
            int dr = abs(fr - tr);
            return (df == 1 && dr == 2) || (df == 2 && dr == 1);
        }
        case BISHOP:
            return (bishop_attacks(from, occ) & (1ULL << to)) != 0;
        case ROOK:
            return (rook_attacks(from, occ) & (1ULL << to)) != 0;
        case QUEEN:
            return (queen_attacks(from, occ) & (1ULL << to)) != 0;
        case KING: {
            int df = abs(ft - tt);
            int dr = abs(fr - tr);
            return df <= 1 && dr <= 1 && (df + dr > 0);
        }
        default:
            break;
    }
    return false;
}

// Trouve la plus petite pièce qui attaque 'to' pour 'side'
static bool get_smallest_attacker(const Position *pos, Square to, Color side,
                                   uint64_t occ, uint64_t side_occ,
                                   Piece *out_piece, Square *out_sq) {
    static const Piece order[] = {PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING};
    
    for (int i = 0; i < 6; i++) {
        Piece p = order[i];
        uint64_t pieces = pos->piece[side][p] & side_occ;
        while (pieces) {
            Square sq = pop_lsb(&pieces);
            if (piece_attacks_square(sq, to, p, occ)) {
                *out_piece = p;
                *out_sq = sq;
                return true;
            }
        }
    }
    return false;
}

// Static Exchange Evaluation : évalue le gain net d'une capture
// Retourne >= 0 si la capture est gagnante ou égale
int see_capture(const Position *pos, Move move) {
    Square to = move.to;
    Color us = pos->side_to_move;
    Color them = us == WHITE ? BLACK : WHITE;
    
    // Identifier la victime
    Piece victim = EMPTY;
    for (int p = PAWN; p <= KING; p++) {
        if (pos->piece[them][p] & (1ULL << to)) {
            victim = (Piece)p;
            break;
        }
    }
    
    // Identifier l'attaquant initial
    Piece attacker = EMPTY;
    for (int p = PAWN; p <= KING; p++) {
        if (pos->piece[us][p] & (1ULL << move.from)) {
            attacker = (Piece)p;
            break;
        }
    }
    (void)attacker;
    
    int gain[32];
    int d = 0;
    
    // Gain initial
    gain[d] = see_piece_val[victim];
    if (move.type >= KNIGHT_PROMO_CAP) {
        gain[d] += see_piece_val[QUEEN] - see_piece_val[PAWN];
    }
    
    // Copie modifiable des bitboards (on ne touche pas à la position réelle)
    uint64_t occ = pos->occupancy[2];
    uint64_t us_occ = pos->occupancy[us];
    uint64_t them_occ = pos->occupancy[them];
    
    // Enlever l'attaquant initial
    us_occ &= ~(1ULL << move.from);
    occ &= ~(1ULL << move.from);
    
    // Enlever la victime (sauf en passant : la victime est sur une autre case)
    if (move.type == EN_PASSANT) {
        // En passant : le pion capturé est derrière 'to'
        Square ep_victim = to + (us == WHITE ? -8 : 8);
        them_occ &= ~(1ULL << ep_victim);
        occ &= ~(1ULL << ep_victim);
    }
    // Pour les captures normales, ne pas enlever la victime ici car
    // la case to peut contenir la pièce qu'on vient de déplacer
    // (dans le cas où to == from, mais ça n'arrive pas pour les captures)
    them_occ &= ~(1ULL << to);
    occ &= ~(1ULL << to);
    
    // Alterner : l'adversaire capture maintenant
    Color side = them;
    uint64_t side_occ = them_occ;
    
    while (d < 31) {
        d++;
        
        Piece min_attacker;
        Square att_sq;
        if (!get_smallest_attacker(pos, to, side, occ, side_occ, &min_attacker, &att_sq)) {
            break;
        }
        
        // Mettre à jour le gain
        gain[d] = see_piece_val[min_attacker] - gain[d-1];
        
        // Enlever cet attaquant
        side_occ &= ~(1ULL << att_sq);
        occ &= ~(1ULL << att_sq);
        
        // Changer de côté
        side = (side == WHITE) ? BLACK : WHITE;
        side_occ = (side == us) ? us_occ : them_occ;
    }
    
    // Remonter pour trouver le meilleur gain (negamax)
    while (--d > 0) {
        if (gain[d] > -gain[d-1]) {
            gain[d-1] = -gain[d];
        }
    }
    
    return gain[0];
}

// Score de capture avec SEE + capture history
static int score_capture(const Position *pos, Move move, Color side_to_move) {
    int see = see_capture(pos, move);
    // Capture history : bonus pour les captures qui ont causé des cutoffs
    int hist_bonus = 0;
    if (move.from < 64 && move.to < 64) {
        hist_bonus = capture_history[side_to_move][move.from][move.to] / 1024;
    }
    // Bonne capture (SEE >= 0) = score élevé, ordonné par gain + history
    if (see >= 0) {
        return 100000 + see + hist_bonus;
    }
    // Mauvaise capture = score bas, mais au-dessus des coups tranquilles
    return see + hist_bonus; // peut devenir positif si history très bon
}

int quiescence(Position *pos, int alpha, int beta, int ply) {
    search_nodes++;
    
    /* TIME1 : la quiescence n'avait AUCUN contrôle du temps (elle pouvait
     * dépasser la limite dure). Vérification tous les 256 nœuds + sortie
     * immédiate si la recherche a déjà été stoppée plus haut. */
    if (search_stopped) return alpha;
    if (search_nodes % 256 == 0 && search_is_time_up()) {
        search_stopped = true;
        return alpha;
    }
    
    if (ply >= 8) return get_static_eval(pos, ply);
    
    // FIX #3 : Gestion des échecs dans la QS
    bool in_check = is_in_check(pos, pos->side_to_move);
    
    if (in_check) {
        // Évasion d'échec : PAS de stand-pat, générer TOUS les coups
        MoveList evasions;
        generate_all_moves(pos, &evasions);
        int best = -INF_SCORE;
        for (int i = 0; i < evasions.count; i++) {
            if (!is_legal_move(pos, evasions.moves[i])) continue;
            
            Piece moved_piece = EMPTY;
            for (int p = PAWN; p <= KING; p++) {
                if (pos->piece[pos->side_to_move][p] & (1ULL << evasions.moves[i].from)) {
                    moved_piece = (Piece)p;
                    break;
                }
            }
            
            make_move(pos, evasions.moves[i]);
            Color side_moved = pos->side_to_move == WHITE ? BLACK : WHITE;
            Piece cap_piece = pos->stack[pos->sp - 1].captured_piece;
            nnue_update_move(nnue_stack[ply + 1], nnue_stack[ply], pos, evasions.moves[i], moved_piece, cap_piece, side_moved);
            
            int score = -quiescence(pos, -beta, -alpha, ply + 1);
            unmake_move(pos, evasions.moves[i]);
            
            if (score > best) best = score;
            if (score > alpha) alpha = score;
            if (alpha >= beta) return beta;
        }
        if (best == -INF_SCORE) return -MATE_SCORE + ply;
        return best;
    }
    
    // Stand-pat (position calme)
    int stand_pat = get_static_eval(pos, ply);
    
    if (stand_pat >= beta) {
        return beta;
    }
    
    if (stand_pat > alpha) {
        alpha = stand_pat;
    }
    
    // Générer les captures
    MoveList captures;
    generate_captures(pos, &captures);
    
    // Trier les captures par MVV-LVA
    int qscores[MAX_MOVES];
    for (int i = 0; i < captures.count; i++) {
        qscores[i] = score_capture(pos, captures.moves[i], pos->side_to_move);
    }
    for (int i = 1; i < captures.count; i++) {
        int key = qscores[i];
        Move move_key = captures.moves[i];
        int j = i - 1;
        while (j >= 0 && qscores[j] < key) {
            qscores[j + 1] = qscores[j];
            captures.moves[j + 1] = captures.moves[j];
            j--;
        }
        qscores[j + 1] = key;
        captures.moves[j + 1] = move_key;
    }
    
    for (int i = 0; i < captures.count; i++) {
        if (!is_legal_move(pos, captures.moves[i])) {
            continue;
        }
        
        Piece moved_piece = EMPTY;
        for (int p = PAWN; p <= KING; p++) {
            if (pos->piece[pos->side_to_move][p] & (1ULL << captures.moves[i].from)) {
                moved_piece = (Piece)p;
                break;
            }
        }
        
        // SEE pruning + futility
        int see_val = see_capture(pos, captures.moves[i]);
        
        // Skip les très mauvaises captures (perdantes)
        if (see_val < -200 && captures.moves[i].type != QUEEN_PROMO_CAP) {
            continue;
        }
        
        int futility = stand_pat + see_val + 200;
        if (futility <= alpha && captures.moves[i].type != QUEEN_PROMO_CAP) {
            continue;
        }
        
        make_move(pos, captures.moves[i]);
        
        Color side_moved = pos->side_to_move == WHITE ? BLACK : WHITE;
        Piece cap_piece = pos->stack[pos->sp - 1].captured_piece;
        nnue_update_move(nnue_stack[ply + 1], nnue_stack[ply], pos, captures.moves[i], moved_piece, cap_piece, side_moved);
        
        if (is_in_check(pos, pos->side_to_move)) {
            unmake_move(pos, captures.moves[i]);
            continue;
        }
        
        int score = -quiescence(pos, -beta, -alpha, ply + 1);
        
        unmake_move(pos, captures.moves[i]);
        
        if (score >= beta) {
            return beta;
        }
        
        if (score > alpha) {
            alpha = score;
        }
    }
    
    // Coups d'échec en QS (tous les plis, avec limites pour éviter explosion)
    // Limite: 6 checks max par ply, et seulement si stand_pat est intéressant
    if (ply < 6) {
        MoveList checks;
        generate_all_moves(pos, &checks);
        int checks_explored = 0;
        const int MAX_CHECKS_PER_PLY = 6;
        for (int i = 0; i < checks.count && checks_explored < MAX_CHECKS_PER_PLY; i++) {
            // Ignorer les captures déjà explorées
            if (checks.moves[i].type == CAPTURE || checks.moves[i].type == EN_PASSANT ||
                checks.moves[i].type >= KNIGHT_PROMO_CAP) {
                continue;
            }
            if (!is_legal_move(pos, checks.moves[i])) continue;

            // Tester si le coup donne échec
            Piece moved_piece = EMPTY;
            for (int p = PAWN; p <= KING; p++) {
                if (pos->piece[pos->side_to_move][p] & (1ULL << checks.moves[i].from)) {
                    moved_piece = (Piece)p;
                    break;
                }
            }

            make_move(pos, checks.moves[i]);
            bool gives_check = is_in_check(pos, pos->side_to_move);
            if (!gives_check) {
                unmake_move(pos, checks.moves[i]);
                continue;
            }

            Color chk_side = pos->side_to_move == WHITE ? BLACK : WHITE;
            Piece chk_cap = pos->stack[pos->sp - 1].captured_piece;
            nnue_update_move(nnue_stack[ply + 1], nnue_stack[ply], pos, checks.moves[i], moved_piece, chk_cap, chk_side);

            // Delta pruning pour les checks (plus agressif)
            if (stand_pat + 200 < alpha) {
                unmake_move(pos, checks.moves[i]);
                continue;
            }

            int score = -quiescence(pos, -beta, -alpha, ply + 1);
            unmake_move(pos, checks.moves[i]);
            checks_explored++;

            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        }
    }
    
    return alpha;
}

// ============================================================
// Alpha-Beta Search
// ============================================================

int alpha_beta(Position *pos, int depth, int alpha, int beta, bool is_pv, int ply,
               Move opp_last_move) {
    search_nodes++;
    if (ply > search_seldepth) search_seldepth = ply;
    
    if (ply >= MAX_PLY - 1) return get_static_eval(pos, ply);
    
    // Vérifier le temps (tous les 2048 nœuds)
    if (search_nodes % 256 == 0) {
        if (search_is_time_up()) {
            search_stopped = true;
            return alpha;
        }
    }
    
    if (pos->halfmove_clock >= 100) return 0;
    
    if (is_repetition(pos->zobrist)) return 0;
    
    // Tablebase probe (WDL only — no distance-to-mate info)
    // tb result: 2=WIN, 0=DRAW, -1=LOSS (from tb.c encoding)
    if (count_bits(pos->occupancy[2]) <= 4) {
        int tb_result = tb_probe_wdl(pos);
        if (tb_result == 2)  return  TB_SCORE - ply;  // WIN (not mate — WDL has no distance)
        if (tb_result == 0)  return  0;                // DRAW
        if (tb_result == -1) return -(TB_SCORE - ply); // LOSS
        // tb_result == -2 (FAILED) → fall through to normal search
    }
    
    // Transposition table probe
    // FIX 30/08 12h30 : les scores de mat stockés sont déjà absolus
    // (MATE_SCORE - ply_absolu, cf. les retours l.457/l.799) — aucun ajustement
    // ±ply à la lecture (l'ancien +ply/-ply combiné au store -ply corrompait
    // la distance de mat entre nœuds de ply différents).
    Move tt_move = {NO_SQUARE, NO_SQUARE, QUIET, EMPTY};
    int tt_score = 0;
    if (tt_probe(pos->zobrist, depth, alpha, beta, &tt_score, &tt_move)) {
        return tt_score;
    }
    
    if (depth >= 4 && tt_move.from == NO_SQUARE && is_pv) {
        alpha_beta(pos, depth - 2, alpha, beta, false, ply,
                   (Move){NO_SQUARE, NO_SQUARE, QUIET, EMPTY});
        tt_probe(pos->zobrist, depth - 2, alpha, beta, &tt_score, &tt_move);
    }
    
    if (depth <= 0) return quiescence(pos, alpha, beta, ply);
    
    bool in_check = is_in_check(pos, pos->side_to_move);
    if (in_check) depth++;
    
    if (!is_pv && !in_check && depth >= 3) {
        int nm_static = get_static_eval(pos, ply);
        if (nm_static >= beta) {
            // Zugzwang detection: skip null-move if few non-pawn pieces
            int non_pawn = count_bits(pos->occupancy[pos->side_to_move])
                         - count_bits(pos->piece[pos->side_to_move][PAWN]);
            if (non_pawn <= 2) goto skip_nullmove;
            
            Position null_pos = *pos;
        null_pos.side_to_move = pos->side_to_move == WHITE ? BLACK : WHITE;
        null_pos.en_passant = NO_SQUARE;
        null_pos.zobrist ^= zobrist_side;
        null_pos.zobrist ^= zobrist_en_passant[pos->en_passant];
        
        memcpy(&nnue_stack[ply + 1][0], &nnue_stack[ply][0], sizeof(NNUEAccumulator));
        memcpy(&nnue_stack[ply + 1][1], &nnue_stack[ply][1], sizeof(NNUEAccumulator));
        
        // Null move R dynamique. FIX 30/08 12h35 (pruning "une fois pour toute") :
        // l'ancien R = 3 + depth/4 + margin atteignait R=7-8 à depth 12 — le null
        // move sautait des défenses profondes, le moteur calculait -313 pour un sac
        // de dame réellement -1452 (game_01 coup 33, mesuré). R standard : 2-4 max.
        int margin = (nm_static - beta) / 300;
        if (margin < 0) margin = 0;
        if (margin > 1) margin = 1;
        int R = 2 + depth / 6 + margin;
        if (R > 4) R = 4;
        int score = -alpha_beta(&null_pos, depth - R - 1, -beta, -beta + 1, false, ply + 1,
                               (Move){NO_SQUARE, NO_SQUARE, QUIET, EMPTY});
        
        if (search_stopped) return alpha;
        if (score >= beta) return beta;
        }
    }
    skip_nullmove:;
    
    // FIX 30/08 12h35 (razoring) : l'ancien margin 300+100d (jusqu'à 600cp à d3)
    // et depth<=3 écrêtait trop : quiescence sur fenêtre alpha-margin = double
    // punition. Fix : depth<=2, margin plus étroit, quiescence full-window.
    if (!is_pv && !in_check && depth <= 2) {
        int razor_margin = 200 + 80 * depth;
        int static_eval = get_static_eval(pos, ply);
        if (static_eval + razor_margin < alpha) {
            int qs = quiescence(pos, alpha - razor_margin, beta, ply);
            if (qs + razor_margin < alpha) return qs;
        }
    }

    // RFP 60*depth — atomique 2/5 (remplace stash 80*depth → 60 plus sûr NNUE)
    // Non-PV uniquement, depth<=6, évite score mate (TB_SCORE 29000 < MATE_BOUND)
    if (!is_pv && !in_check && depth <= 6 && abs(beta) < MATE_SCORE - MAX_PLY) {
        int rfp_margin = 60 * depth;
        int rfp_eval = get_static_eval(pos, ply);
        if (rfp_eval - rfp_margin >= beta) {
            return rfp_eval - rfp_margin;
        }
    }
    
    MoveList moves;
    generate_all_moves(pos, &moves);
    sort_moves(pos, &moves, tt_move, ply, opp_last_move);
    
    // AXE 1.3 : Singular Extensions — après tri des coups
    bool singular_ext = false;
    if (depth >= 5 && tt_move.from != NO_SQUARE && !in_check &&
        abs(beta) < MATE_SCORE - MAX_PLY) {
        int margin = 50 + depth * 5;
        int se_alpha = tt_score - margin;
        int se_beta = se_alpha + 1;
        int se_best = -INF_SCORE;
        for (int i = 0; i < moves.count; i++) {
            if (!is_legal_move(pos, moves.moves[i])) continue;
            if (moves.moves[i].from == tt_move.from && moves.moves[i].to == tt_move.to) continue;
            Piece mp = EMPTY;
            for (int p = PAWN; p <= KING; p++) {
                if (pos->piece[pos->side_to_move][p] & (1ULL << moves.moves[i].from)) {
                    mp = (Piece)p; break;
                }
            }
            make_move(pos, moves.moves[i]);
            rep_stack[rep_sp++] = pos->zobrist;
            Color sm = pos->side_to_move == WHITE ? BLACK : WHITE;
            Piece cp = pos->stack[pos->sp - 1].captured_piece;
            nnue_update_move(nnue_stack[ply + 1], nnue_stack[ply], pos, moves.moves[i], mp, cp, sm);
            int s = -alpha_beta(pos, depth - 3, -se_beta, -se_alpha, false, ply + 1, moves.moves[i]);
            rep_sp--;
            unmake_move(pos, moves.moves[i]);
            if (search_stopped) return alpha;
            if (s > se_best) se_best = s;
        }
        if (se_best < se_alpha) singular_ext = true;
    }
    
    Move best_move = {NO_SQUARE, NO_SQUARE, QUIET, EMPTY};
    int best_score = -INF_SCORE;
    int moves_searched = 0;
    
    // FIX 30/08 (bug TT majeur) : le drapeau TT doit être décidé avec l'alpha
    // D'ORIGINE du nœud, pas l'alpha mis à jour par la boucle. Avant ce fix,
    // alpha était élevé à best_score dans la boucle, donc (best_score <= alpha)
    // était toujours vrai -> flag TOUJOURS TT_ALPHA -> TT_EXACT/TT_BETA jamais
    // stockés -> tt_probe renvoyait des fail-low périmés -> sous-arbres jamais
    // cherchés (ex: hxg5 +400 jamais vu à d8). Effet mesuré 30/08 : évals quasi
    // constantes avec la depth (d6=-35, d8=-41) alors que la vérité est +400.
    int alpha_orig = alpha;
    
    // FIX 30/08 12h35 (futility) : l'ancien margin 150*depth (450cp à d3 !) sautait
    // des coups tranquilles qui préparent des tactiques. Margin fixe modéré standard :
    // un coup tranquille peut rarement gagner plus d'un mineur dans le reste de la
    // depth. Le coup 1 (moves_searched==0) reste toujours cherché.
    int futility_eval = 0;
    bool do_futility = (!is_pv && !in_check && depth <= 3);
    if (do_futility) {
        futility_eval = get_static_eval(pos, ply);
    }
    
    for (int i = 0; i < moves.count; i++) {
        if (!is_legal_move(pos, moves.moves[i])) continue;

        // LMP 8/12/20/30 depth<=3 — atomique 3/5 (remplace stash 5/10/16/24 depth<=4 trop agressif)
        if (!is_pv && !in_check && depth <= 3 && moves_searched > 0 &&
            moves.moves[i].type != CAPTURE && moves.moves[i].type != EN_PASSANT &&
            moves.moves[i].type < KNIGHT_PROMO_CAP && moves.moves[i].promo_piece == EMPTY) {
            static const int lmp_threshold[] = {0, 8, 12, 20, 30};
            if (moves_searched >= lmp_threshold[depth]) {
                continue;
            }
        }
        
        // Futility pruning: skip quiet moves if we're too far below alpha
        if (do_futility && moves_searched > 0 &&
            moves.moves[i].type != CAPTURE && moves.moves[i].type != EN_PASSANT &&
            moves.moves[i].type < KNIGHT_PROMO_CAP && moves.moves[i].promo_piece == EMPTY) {
            int futility_margin = 120;
            if (futility_eval + futility_margin <= alpha) {
                continue;
            }
        }
        
        Piece moved_piece = EMPTY;
        for (int p = PAWN; p <= KING; p++) {
            if (pos->piece[pos->side_to_move][p] & (1ULL << moves.moves[i].from)) {
                moved_piece = (Piece)p;
                break;
            }
        }
        
        make_move(pos, moves.moves[i]);
        
        rep_stack[rep_sp++] = pos->zobrist;
        
        Color side_moved = pos->side_to_move == WHITE ? BLACK : WHITE;
        Piece cap_piece = pos->stack[pos->sp - 1].captured_piece;
        nnue_update_move(nnue_stack[ply + 1], nnue_stack[ply], pos, moves.moves[i], moved_piece, cap_piece, side_moved);
        
        int score;
        
        // AXE 1.3 : singular extension — étendre le TT move si singular
        bool is_tt_move = (moves.moves[i].from == tt_move.from && moves.moves[i].to == tt_move.to);
        int new_depth = depth - 1 + (is_tt_move && singular_ext ? 1 : 0);
        
        if (moves_searched >= 4 && depth >= 3 && !in_check &&
            moves.moves[i].type != CAPTURE && moves.moves[i].type != QUEEN_PROMO_CAP) {
            /* AXE search : LMR affiné — formule depth + moves_searched.
             * Plus on a exploré de coups et plus on est profond, plus on réduit.
             * Bornes : R ∈ [1, depth-2] (jamais réduire à zéro ou en-dessous). */
            int R = 1 + depth / 5 + moves_searched / 12;
            if (moves_searched > 16) R += 1;
            
            // Killer moves: moins de réduction (ils sont souvent bons)
            if (is_killer(moves.moves[i], ply)) {
                R = (R > 1) ? R - 1 : 0;
            }
            
            // History: bons coups = moins de réduction, mauvais = plus
            int hist = get_history(moves.moves[i], pos->side_to_move);
            if (hist > 500) R = (R > 1) ? R - 1 : 0;
            else if (hist < 10 && R > 0) R += 1;
            
            if (depth - R - 1 < 1) R = depth - 2;
            if (R < 0) R = 0;
            score = -alpha_beta(pos, new_depth - R, -alpha - 1, -alpha, false, ply + 1,
                               moves.moves[i]);
            if (score > alpha) {
                score = -alpha_beta(pos, new_depth, -alpha - 1, -alpha, false, ply + 1,
                                   moves.moves[i]);
            }
        } else {
            if (moves_searched == 0) {
                score = -alpha_beta(pos, new_depth, -beta, -alpha, true, ply + 1,
                                   moves.moves[i]);
            } else {
                score = -alpha_beta(pos, new_depth, -alpha - 1, -alpha, false, ply + 1,
                                   moves.moves[i]);
                if (score > alpha && score < beta) {
                    score = -alpha_beta(pos, new_depth, -beta, -alpha, true, ply + 1,
                                       moves.moves[i]);
                }
            }
        }
        
        rep_sp--;
        unmake_move(pos, moves.moves[i]);
        if (search_stopped) return alpha;
        
        moves_searched++;
        
        if (score > best_score) {
            best_score = score;
            best_move = moves.moves[i];
            /* TIME1 : coup partiel racine (si l'itération est coupée). */
            if (ply == 0) {
                root_partial_best = moves.moves[i];
                root_partial_found = true;
            }
        }
        
        if (score > alpha) alpha = score;
        
        if (alpha >= beta) {
            if (moves.moves[i].type != CAPTURE && moves.moves[i].type != EN_PASSANT && moves.moves[i].type < KNIGHT_PROMO_CAP) {
                store_killer(moves.moves[i], ply);
                store_history(moves.moves[i], pos->side_to_move, depth);
                // Countermove heuristic : stocker le coup qui a causé le cutoff
                // comme réponse au dernier coup adverse (si disponible)
                if (opp_last_move.from != NO_SQUARE) {
                    countermove_table[opp_last_move.from][opp_last_move.to] = moves.moves[i];
                }
            } else if (moves.moves[i].type == CAPTURE || moves.moves[i].type >= KNIGHT_PROMO_CAP) {
                // Capture history : bonus pour les captures qui causent des cutoffs
                if (moves.moves[i].from < 64 && moves.moves[i].to < 64) {
                    capture_history[pos->side_to_move][moves.moves[i].from][moves.moves[i].to]
                        += depth * depth;
                }
            }
            break;
        }
    }
    
    if (moves_searched == 0) {
        if (in_check) return -MATE_SCORE + ply;
        return 0;
    }
    
    TTFlag flag = (best_score <= alpha_orig) ? TT_ALPHA : ((best_score >= beta) ? TT_BETA : TT_EXACT);
    
    // FIX 30/08 12h30 (bug TT critique — "une fois pour toute") :
    // Si la recherche a été stoppée par le temps, best_score est un score PARTIEL
    // (tronqué en pleine itération). Le stocker en TT pollue TOUTES les recherches
    // suivantes (coups suivants en blitz — aucun ucinewgame entre les coups) avec
    // des scores garbage. Mesuré 30/08 : mean_cp -796/-998, défaites 0W-2L-0D avec
    // le moteur qui "voit" -300 et joue quand même. On ne stocke JAMAIS un résultat
    // incomplet. Les scores de mat étant déjà absolus (MATE_SCORE - ply_absolu),
    // aucun ajustement ±ply n'est nécessaire (l'ancien double-ajustement corrompait
    // les scores de mat de ±(p2-p) plies).
    if (search_stopped) return best_score;
    
    tt_store(pos->zobrist, best_move, best_score, depth, flag);
    
    return best_score;
}

// ============================================================
// PV Extraction from TT
// ============================================================

static int extract_pv(Position *pos, Move *pv, int max_len) {
    int count = 0;
    Position temp = *pos;
    uint64_t seen[16];
    int seen_count = 0;
    
    for (int i = 0; i < max_len; i++) {
        Move tt_move = {NO_SQUARE, NO_SQUARE, QUIET, EMPTY};
        int tt_score;
        if (!tt_probe(temp.zobrist, 1, -INF_SCORE, INF_SCORE, &tt_score, &tt_move)) break;
        if (tt_move.from == NO_SQUARE) break;
        
        // Détection de boucle
        bool loop = false;
        for (int j = 0; j < seen_count; j++) {
            if (seen[j] == temp.zobrist) { loop = true; break; }
        }
        if (loop) break;
        if (seen_count < 16) seen[seen_count++] = temp.zobrist;
        
        pv[count++] = tt_move;
        
        // Vérifier légalité
        MoveList moves;
        generate_all_moves(&temp, &moves);
        bool legal = false;
        for (int j = 0; j < moves.count; j++) {
            if (moves.moves[j].from == tt_move.from && moves.moves[j].to == tt_move.to) {
                legal = true;
                break;
            }
        }
        if (!legal) break;
        
        make_move(&temp, tt_move);
    }
    
    return count;
}

// ============================================================
// Iterative Deepening
// ============================================================

SearchResult search(Position *pos, int depth) {
    SearchResult result = {{NO_SQUARE, NO_SQUARE, QUIET, EMPTY}, {NO_SQUARE, NO_SQUARE, QUIET, EMPTY}, 0, 0, 0};
    
    clear_history();
    search_nodes = 0;
    search_stopped = false;
    root_partial_found = false;
    search_seldepth = 0;
    rep_sp = 0;
    rep_stack[rep_sp++] = pos->zobrist;
    
    nnue_compute_side(&nnue_stack[0][WHITE], pos, WHITE);
    nnue_compute_side(&nnue_stack[0][BLACK], pos, BLACK);
    
    // FIX: Verifier conditions de nulle AVANT la recherche
    // pour empecher les parties infinies (shuffling)
    if (pos->halfmove_clock >= 100) {
        UCI_PRINTF("info depth 0 score cp 0 string 50-move-rule\n");
        return result;  // best_move = NO_SQUARE -> "bestmove 0000"
    }
    if (is_insufficient_material(pos)) {
        UCI_PRINTF("info depth 0 score cp 0 string insufficient-material\n");
        return result;
    }

    // Syzygy root probe — atomique 4/5 : si TB couvre la position, retourner directement le meilleur coup
    // via probe des enfants WDL (pas de DTZ). Évite de chercher 4 pièces au search profond.
    if (count_bits(pos->occupancy[2]) <= 4) {
        int root_tb = tb_probe_wdl(pos);
        if (root_tb == 2 || root_tb == 0 || root_tb == -1) {
            MoveList root_moves;
            generate_all_moves(pos, &root_moves);
            Move best_move = {NO_SQUARE, NO_SQUARE, QUIET, EMPTY};
            int best_child_tb = -2;
            for (int i = 0; i < root_moves.count; i++) {
                if (!is_legal_move(pos, root_moves.moves[i])) continue;
                Position child = *pos;
                make_move(&child, root_moves.moves[i]);
                int child_tb = tb_probe_wdl(&child);
                if (child_tb > best_child_tb) {
                    best_child_tb = child_tb;
                    best_move = root_moves.moves[i];
                }
            }
            if (best_move.from != NO_SQUARE) {
                result.best_move = best_move;
                result.score = (root_tb == 2) ? TB_SCORE : (root_tb == 0 ? 0 : -TB_SCORE);
                char score_buf[32];
                format_uci_score(result.score, score_buf, sizeof(score_buf));
                UCI_PRINTF("info depth 1 seldepth 1 multipv 1 %s nodes 0 nps 0 hashfull 0 time 0 string syzygy\n", score_buf);
                fflush(stdout);
                return result;
            }
        }
    }
    
    int alpha = -INF_SCORE;
    int beta = INF_SCORE;
    const int MATE_BOUND_SCORE = MATE_SCORE - 512; // seuil "proche de mat" (cohérent format_uci_score)
    
    // TT age : invalider les entries des recherches précédentes
    tt_new_search();
    
    for (int d = 1; d <= depth; d++) {
        if (time_limit_enabled && search_is_time_up()) break;
        /* TIME1 (limite souple) : ne pas démarrer une nouvelle itération s'il
         * reste moins de ~50 % du budget (evite le cas mesuré : itération
         * lancée à 8,3 s sur un budget de 10 s). */
        if (time_limit_enabled && d > 1 &&
            (get_time_ms() - search_start_time) * 2 >= (uint64_t)time_limit_ms) break;
        
        /* AXE search : decay history à chaque nouvelle itération (après le 1er depth).
         * Les scores history des itérations précédentes deviennent moins influents,
         * les coups vraiment bons restent dominants. */
        if (d > 1) decay_history();
        
        // FIX 30/08 12h30 (aspiration window — "une fois pour toute") :
        // Ancien window = 15 + 5d → 35cp à d4. En blitz, si la vraie eval dérive
        // de >35cp entre deux itérations (capture tactique), fail-low systématique.
        // Mesuré 30/08 : le moteur rendait eval -300/-1500 et jouait le coup du
        // fail-low. Fix : (1) fenêtre élargie 30 + 8d ; (2) jamais de fenêtre
        // serrée autour d'un score de mat (range déraisonnable → full window) ;
        // (3) le re-search full-window ci-dessous reste la garantie : son résultat
        // est complet, et le TT probe racine (plus bas) récupère son VRAI best_move.
        if (d >= 4 && result.score > -MATE_BOUND_SCORE && result.score < MATE_BOUND_SCORE) {
            int window = 30 + d * 8;
            alpha = result.score - window;
            beta = result.score + window;
        } else {
            alpha = -INF_SCORE;
            beta = INF_SCORE;
        }
        
        int score = alpha_beta(pos, d, alpha, beta, true, 0,
                              (Move){NO_SQUARE, NO_SQUARE, QUIET, EMPTY});
        
        if (search_stopped) break;
        
        if (score <= alpha || score >= beta) {
            // AXE 1.4 : re-search progressif (fenêtre intermédiaire avant full).
            // Au lieu de full-window directement, on essaie une fenêtre intermédiaire
            // (décalée dans la direction du fail). Économise un full search dans
            // ~60% des cas (score légèrement hors fenêtre).
            int delta = beta - alpha;
            int ia = alpha, ib = beta;
            if (score <= alpha) {
                ia = alpha - delta;
                ib = alpha;
            } else {
                ia = beta;
                ib = beta + delta;
            }
            score = alpha_beta(pos, d, ia, ib, true, 0,
                              (Move){NO_SQUARE, NO_SQUARE, QUIET, EMPTY});
            if (search_stopped) break;

            if (score <= ia || score >= ib) {
                // Échec intermédiaire → full window (garantie)
                score = alpha_beta(pos, d, -INF_SCORE, INF_SCORE, true, 0,
                                  (Move){NO_SQUARE, NO_SQUARE, QUIET, EMPTY});
                if (search_stopped) break;
            }
        }
        
        // AXE 1.5 : Time Management dynamique — si le score est instable
        // (changement >50cp entre deux itérations), on alloue plus de temps
        // (×1.5) car la position est tactiquement complexe.
        /* TIME1 : en mode movetime (limite DURE), l'extension dynamique ×1.5
         * est désactivée — c'est elle qui faisait dépasser 10 s (15,1 s / 22,6 s). */
        if (d >= 2 && time_limit_enabled && !search_movetime_hard && result.score != 0) {
            int score_delta = abs(score - result.score);
            if (score_delta > 50) {
                time_limit_ms += time_limit_ms / 2; // ×1.5
            }
        }
        
        result.score = score;
        result.depth = d;
        result.nodes = search_nodes;
        
        // Compute stats
        uint64_t elapsed = get_time_ms() - search_start_time;
        if (elapsed < 1) elapsed = 1;
        int nps = (int)(result.nodes * 1000 / elapsed);
        int hashfull = tt_hashfull();
        
        Move tt_best = {NO_SQUARE, NO_SQUARE, QUIET, EMPTY};
        int tt_sc;
        tt_probe(pos->zobrist, d, -INF_SCORE, INF_SCORE, &tt_sc, &tt_best);
        if (tt_best.from != NO_SQUARE) {
            result.best_move = tt_best;
        }
        
        Move pv[MAX_PV_LENGTH];
        int pv_len = extract_pv(pos, pv, MAX_PV_LENGTH);
        
        // Store ponder move from PV
        if (pv_len >= 2) {
            result.ponder_move = pv[1];
        } else {
            result.ponder_move.from = NO_SQUARE;
            result.ponder_move.to = NO_SQUARE;
        }
        
        // Print main info line
        char score_buf[32];
        format_uci_score(score, score_buf, sizeof(score_buf));
        UCI_PRINTF("info depth %d seldepth %d multipv 1 %s nodes %llu nps %d hashfull %d time %llu",
               d, search_seldepth, score_buf, (unsigned long long)result.nodes, nps, hashfull, (unsigned long long)elapsed);
        if (pv_len > 0) {
            UCI_PRINTF(" pv");
            for (int i = 0; i < pv_len; i++) {
                char move_str[6];
                square_to_algebraic(pv[i].from, move_str);
                square_to_algebraic(pv[i].to, move_str + 2);
                if (pv[i].promo_piece != EMPTY) {
                    switch (pv[i].promo_piece) {
                        case QUEEN: move_str[4] = 'q'; break;
                        case ROOK: move_str[4] = 'r'; break;
                        case BISHOP: move_str[4] = 'b'; break;
                        case KNIGHT: move_str[4] = 'n'; break;
                        default: break;
                    }
                    move_str[5] = '\0';
                }
                UCI_PRINTF(" %s", move_str);
            }
        }
        UCI_PRINTF("\n");
        fflush(stdout);
        
        // ---- MultiPV: collect additional best lines from TT ----
        if (multipv_count > 1) {
            MoveList root_moves;
            generate_all_moves(pos, &root_moves);
            
            PvLine multi_lines[MAX_MULTIPV];
            int multi_count = 0;
            
            for (int i = 0; i < root_moves.count && multi_count < MAX_MULTIPV; i++) {
                if (!is_legal_move(pos, root_moves.moves[i])) continue;
                
                // Skip the main best move (it's already multipv 1)
                if (result.best_move.from == root_moves.moves[i].from &&
                    result.best_move.to == root_moves.moves[i].to) {
                    continue;
                }
                
                Position temp = *pos;
                make_move(&temp, root_moves.moves[i]);
                
                int line_score;
                bool got_score = false;
                
                // Try to get score from TT
                Move tt_mv = {NO_SQUARE, NO_SQUARE, QUIET, EMPTY};
                int tt_sc2;
                if (tt_probe(temp.zobrist, 0, -INF_SCORE, INF_SCORE, &tt_sc2, &tt_mv)) {
                    line_score = -tt_sc2;
                    got_score = true;
                } else {
                    // Fallback: static eval
                    line_score = -get_static_eval(&temp, 0);
                }
                
                multi_lines[multi_count].valid = true;
                multi_lines[multi_count].depth = d;
                multi_lines[multi_count].seldepth = search_seldepth;
                multi_lines[multi_count].score_cp = line_score;
                multi_lines[multi_count].is_mate = (got_score && abs(tt_sc2) > MATE_SCORE - MAX_PLY);
                multi_lines[multi_count].pv[0] = root_moves.moves[i];
                multi_lines[multi_count].pv_length = 1;
                
                // Extract full PV for this line from TT
                if (got_score && tt_mv.from != NO_SQUARE) {
                    Move mpv[MAX_PV_LENGTH];
                    int mpv_len = extract_pv(&temp, mpv, MAX_PV_LENGTH);
                    for (int j = 0; j < mpv_len && j < MAX_PV_LENGTH - 1; j++) {
                        multi_lines[multi_count].pv[j + 1] = mpv[j];
                    }
                    multi_lines[multi_count].pv_length = mpv_len + 1;
                }
                
                multi_count++;
            }
            
            // Sort by score descending (simple insertion sort)
            for (int i = 1; i < multi_count; i++) {
                PvLine key = multi_lines[i];
                int j = i - 1;
                while (j >= 0 && multi_lines[j].score_cp < key.score_cp) {
                    multi_lines[j + 1] = multi_lines[j];
                    j--;
                }
                multi_lines[j + 1] = key;
            }
            
            // Output top N lines (limit to multipv_count - 1 since multipv 1 is already printed)
            int out_count = multi_count < multipv_count - 1 ? multi_count : multipv_count - 1;
            for (int k = 0; k < out_count; k++) {
                char mscore_buf[32];
                format_uci_score(multi_lines[k].score_cp, mscore_buf, sizeof(mscore_buf));
                UCI_PRINTF("info depth %d seldepth %d multipv %d %s nodes %llu nps %d hashfull %d time %llu pv",
                       multi_lines[k].depth, multi_lines[k].seldepth, k + 2,
                       mscore_buf, (unsigned long long)result.nodes, nps, hashfull, (unsigned long long)elapsed);
                for (int j = 0; j < multi_lines[k].pv_length; j++) {
                    char move_str[6];
                    square_to_algebraic(multi_lines[k].pv[j].from, move_str);
                    square_to_algebraic(multi_lines[k].pv[j].to, move_str + 2);
                    if (multi_lines[k].pv[j].promo_piece != EMPTY) {
                        switch (multi_lines[k].pv[j].promo_piece) {
                            case QUEEN: move_str[4] = 'q'; break;
                            case ROOK: move_str[4] = 'r'; break;
                            case BISHOP: move_str[4] = 'b'; break;
                            case KNIGHT: move_str[4] = 'n'; break;
                            default: break;
                        }
                        move_str[5] = '\0';
                    }
                    UCI_PRINTF(" %s", move_str);
                }
                UCI_PRINTF("\n");
            }
            fflush(stdout);
        }
    }
    
    // FIX 29/08 soir (run 3 : 5/20 défaites resign_0000 confirmées avec coups légaux
    // restants [verify_0000.py]) : si la recherche a été stoppée par le temps avant
    // la fin de la première itération, result.best_move reste vide -> "bestmove 0000"
    // = forfait à tort. Fallback : meilleur coup TT racine s'il est légal, sinon
    // premier coup légal. JAMAIS 0000 tant qu'un coup légal existe.
    if (result.best_move.from == NO_SQUARE) {
        MoveList root_fb;
        generate_all_moves(pos, &root_fb);
        Move tt_fb = {NO_SQUARE, NO_SQUARE, QUIET, EMPTY};
        int tt_fb_sc;
        if (root_partial_found && is_legal_move(pos, root_partial_best)) {
            /* TIME1 : coup partiel racine (itération coupée) — jamais 0000. */
            result.best_move = root_partial_best;
        } else if (tt_probe(pos->zobrist, 0, -INF_SCORE, INF_SCORE, &tt_fb_sc, &tt_fb)
            && tt_fb.from != NO_SQUARE && is_legal_move(pos, tt_fb)) {
            result.best_move = tt_fb;
        } else {
            for (int i = 0; i < root_fb.count; i++) {
                if (is_legal_move(pos, root_fb.moves[i])) {
                    result.best_move = root_fb.moves[i];
                    break;
                }
            }
        }
    }
    
    return result;
}

// ============================================================
// Time Management
// ============================================================

void search_set_time(int wtime, int btime, int winc, int binc, int movestogo) {
    // Calculer le temps alloué pour ce coup
    int my_time = (search_side == WHITE) ? wtime : btime;
    int my_inc = (search_side == WHITE) ? winc : binc;
    
    if (movestogo > 0) {
        // On sait combien de coups il reste
        time_limit_ms = my_time / movestogo + my_inc;
    } else {
        // AXE 4 (30/08 13h22) : my_time/32 (au lieu de /40) + inc complète.
        // Mesuré en blitz60+1s : ~2s/coup → depth 10-12. 1/32 + inc = ~2.4s/coup
        // (+20% de réflexion) sans risque de flag (cap my_time/2 ci-dessous).
        time_limit_ms = my_time / 32 + my_inc;
    }
    
    // FIX 29/08 soir (run 3 : 5/20 défaites resign_0000, coups légaux restants) :
    // le cap max DOIT précéder le min. Ordre inversé : quand my_time < 200ms,
    // my_time/2 < 100ms écrasait le min -> budget 0ms -> stop immédiat -> 0000.
    if (time_limit_ms > my_time / 2) time_limit_ms = my_time / 2;
    if (time_limit_ms < 100) time_limit_ms = 100; // Min 100ms (prévaut sur le cap)
    
    search_start_time = get_time_ms();
    time_limit_enabled = true;
    search_movetime_hard = false;
}

bool search_is_time_up(void) {
    if (!time_limit_enabled) return false;
    return get_time_ms() >= search_start_time + time_limit_ms;
}

SearchResult search_time(Position *pos, int time_ms) {
    search_start_time = get_time_ms();
    time_limit_ms = time_ms;
    time_limit_enabled = true;
    search_movetime_hard = false;
    return search(pos, 64);
}

void search_set_movetime(int ms) {
    /* TIME1 : go movetime est une limite DURE. On retire une marge (~30 ms)
     * pour que la mesure réelle reste sous movetime, y compris le temps de
     * parse/affichage du bestmove. Le time management dynamique est neutralisé. */
    search_start_time = get_time_ms();
    time_limit_ms = (ms > 50) ? (ms - 30) : ms;
    if (time_limit_ms < 1) time_limit_ms = 1;
    time_limit_enabled = true;
    search_movetime_hard = true;
}

void search_set_infinite(void) {
    time_limit_enabled = false;
    search_movetime_hard = false;
}
