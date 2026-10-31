#include "chess_engine.h"
#include "book.h"
#include "nnue.h"   // AXE 1 : evalinc (nnue_update_move / nnue_forward_fast / nnue_evaluate)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

// ============================================================
// Variables globales
// ============================================================

static Position current_position;
static bool engine_running = true;
static bool search_running = false;
static int uci_own_book = 1;
static char g_last_position_cmd[2048] = "startpos"; // dernière commande position (pour evalinc)
FILE *g_uci_out = NULL;

// ============================================================
// Fonctions utilitaires
// ============================================================

uint64_t get_time_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (uint64_t)(counter.QuadPart * 1000 / frequency.QuadPart);
#else
    // Linux / sessions cloud : horloge monotone
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
#endif
}

// ============================================================
// AXE 1 (30/08 13h10) — evalinc : rejeu INCREMENTAL de la dernière position
// (via nnue_update_move, exactement comme le search.c), comparé à l'ORACLE
// (nnue_evaluate = recompute complet). drift != 0 => bug incrémental résiduel.
// Couvre tous les types de coups (simple, capture, EP, promotion, roque)
// et les deux couleurs. Lecture seule, sans search.
// ============================================================
static void uci_evalinc(void) {
    const char *ptr = g_last_position_cmd;
    static Position pos;

    if (strncmp(ptr, "startpos", 8) == 0) {
        set_starting_position(&pos);
        ptr += 8;
    } else if (strncmp(ptr, "fen", 3) == 0) {
        ptr += 3;
        while (*ptr == ' ') ptr++;
        char fen[256];
        int i = 0, spaces = 0;
        while (*ptr && i < 255) {
            if (*ptr == ' ') {
                spaces++;
                if (spaces >= 5) break;
            }
            fen[i++] = *ptr++;
        }
        fen[i] = '\0';
        set_fen(&pos, fen);
    } else {
        UCI_PRINTF("info string evalinc error no_position\n");
        return;
    }

    // Accumulateurs oracle de départ
    NNUEAccumulator acc[2];
    nnue_compute_side(&acc[WHITE], &pos, WHITE);
    nnue_compute_side(&acc[BLACK], &pos, BLACK);

    // Rejouer les coups avec la MÊME chaîne incrémentale que le search
    while (*ptr) {
        while (*ptr == ' ') ptr++;
        if (strncmp(ptr, "moves", 5) == 0) {
            ptr += 5;
            while (*ptr) {
                while (*ptr == ' ') ptr++;
                if (!*ptr) break;
                char move_str[6] = {0};
                int i = 0;
                while (*ptr && *ptr != ' ' && i < 5) move_str[i++] = *ptr++;

                if (strlen(move_str) >= 4) {
                    Square from = algebraic_to_square(move_str);
                    Square to = algebraic_to_square(move_str + 2);
                    Piece promo = EMPTY;
                    if (strlen(move_str) == 5) {
                        switch (move_str[4]) {
                            case 'q': promo = QUEEN; break;
                            case 'r': promo = ROOK; break;
                            case 'b': promo = BISHOP; break;
                            case 'n': promo = KNIGHT; break;
                        }
                    }
                    MoveList moves;
                    generate_all_moves(&pos, &moves);
                    for (int m = 0; m < moves.count; m++) {
                        if (moves.moves[m].from == from && moves.moves[m].to == to &&
                            (promo == EMPTY || moves.moves[m].promo_piece == promo)) {
                            // moved_piece lu AVANT make_move (comme search.c l.727)
                            Piece moved_piece = EMPTY;
                            for (int p = PAWN; p <= KING; p++) {
                                if (pos.piece[pos.side_to_move][p] & (1ULL << moves.moves[m].from)) {
                                    moved_piece = (Piece)p;
                                    break;
                                }
                            }
                            Color side_moved = pos.side_to_move;
                            make_move(&pos, moves.moves[m]);
                            Piece cap_piece = pos.stack[pos.sp - 1].captured_piece;
                            nnue_update_move(acc, acc, &pos, moves.moves[m], moved_piece, cap_piece, side_moved);
                            break;
                        }
                    }
                }
            }
            break;
        }
        ptr++;
    }

    int inc = nnue_forward_fast(acc, pos.side_to_move);
    int oracle = nnue_evaluate(&pos);
    UCI_PRINTF("info string evalinc stm=%d inc=%d oracle=%d diff=%d %s\n",
               (int)pos.side_to_move, inc, oracle, inc - oracle,
               (inc == oracle) ? "OK" : "DRIFT!");
}

// ============================================================
// Commandes UCI
// ============================================================

void uci_uci(void) {
    UCI_PRINTF("id name ChessEngine 1.0.0\n");
    UCI_PRINTF("id author David Serreau\n");
    UCI_PRINTF("\n");
    UCI_PRINTF("option name Hash type spin default 128 min 1 max 1024\n");
    UCI_PRINTF("option name Ponder type check default false\n");
    UCI_PRINTF("option name MultiPV type spin default 1 min 1 max 256\n");
    UCI_PRINTF("option name UseNNUE type check default true\n");
    UCI_PRINTF("option name EvalFile type string default <empty>\n");
    UCI_PRINTF("option name OwnBook type check default true\n");
    UCI_PRINTF("uciok\n");
}

void uci_isready(void) {
    UCI_PRINTF("readyok\n");
}

void uci_position(const char *args) {
    // AXE 1 (30/08 13h10) : mémoriser la commande pour rejeu incrémental (evalinc)
    snprintf(g_last_position_cmd, sizeof(g_last_position_cmd), "%s", args);
    // Parser les arguments
    const char *ptr = args;
    
    // Vérifier si c'est "startpos" ou "fen"
    if (strncmp(ptr, "startpos", 8) == 0) {
        set_starting_position(&current_position);
        ptr += 8;
    } else if (strncmp(ptr, "fen", 3) == 0) {
        ptr += 3;
        while (*ptr == ' ') ptr++;
        
        char fen[256];
        int i = 0;
        int spaces = 0;
        while (*ptr && i < 255) {
            if (*ptr == ' ') {
                spaces++;
                if (spaces >= 5) break;
            }
            fen[i++] = *ptr++;
        }
        fen[i] = '\0';
        
        set_fen(&current_position, fen);
    }
    
    // Vérifier s'il y a des coups à jouer
    while (*ptr) {
        while (*ptr == ' ') ptr++;
        
        if (strncmp(ptr, "moves", 5) == 0) {
            ptr += 5;
            
            // Jouer les coups
            while (*ptr) {
                while (*ptr == ' ') ptr++;
                if (!*ptr) break;
                
                // FIX: Augmentation à 6 pour accueillir les coups de promotion (ex: e7e8q)
                char move_str[6] = {0};
                int i = 0;
                while (*ptr && *ptr != ' ' && i < 5) {
                    move_str[i++] = *ptr++;
                }
                
                // Parser le coup
                if (strlen(move_str) >= 4) {
                    Square from = algebraic_to_square(move_str);
                    Square to = algebraic_to_square(move_str + 2);
                    Piece promo = EMPTY;
                    
                    if (strlen(move_str) == 5) {
                        switch (move_str[4]) {
                            case 'q': promo = QUEEN; break;
                            case 'r': promo = ROOK; break;
                            case 'b': promo = BISHOP; break;
                            case 'n': promo = KNIGHT; break;
                        }
                    }
                    
                    // Trouver le coup dans la liste des coups légaux
                    MoveList moves;
                    generate_all_moves(&current_position, &moves);
                    
                    bool move_found = false;
                    for (int i = 0; i < moves.count; i++) {
                        if (moves.moves[i].from == from && moves.moves[i].to == to) {
                            if (promo == EMPTY || moves.moves[i].promo_piece == promo) {
                                make_move(&current_position, moves.moves[i]);
                                move_found = true;
                                break;
                            }
                        }
                    }
                    if (!move_found) {
                        fprintf(stderr, "WARNING: move %s NOT FOUND (side=%d from=%d to=%d count=%d)\n",
                                move_str, current_position.side_to_move, from, to, moves.count);
                    }
                }
            }
            break;
        }
        ptr++;
    }
}

void uci_go(const char *args) {
    int wtime = 60000, btime = 60000, winc = 0, binc = 0, movestogo = 0;
    int depth = 64; 
    bool has_movetime = false;
    
    const char *ptr = args;
    while (*ptr) {
        while (*ptr == ' ') ptr++;
        
        if (strncmp(ptr, "wtime", 5) == 0) {
            ptr += 5;
            while (*ptr == ' ') ptr++;
            wtime = atoi(ptr);
            while (*ptr && *ptr != ' ') ptr++;
        } else if (strncmp(ptr, "btime", 5) == 0) {
            ptr += 5;
            while (*ptr == ' ') ptr++;
            btime = atoi(ptr);
            while (*ptr && *ptr != ' ') ptr++;
        } else if (strncmp(ptr, "winc", 4) == 0) {
            ptr += 4;
            while (*ptr == ' ') ptr++;
            winc = atoi(ptr);
            while (*ptr && *ptr != ' ') ptr++;
        } else if (strncmp(ptr, "binc", 4) == 0) {
            ptr += 4;
            while (*ptr == ' ') ptr++;
            binc = atoi(ptr);
            while (*ptr && *ptr != ' ') ptr++;
        } else if (strncmp(ptr, "movestogo", 9) == 0) {
            ptr += 9;
            while (*ptr == ' ') ptr++;
            movestogo = atoi(ptr);
            while (*ptr && *ptr != ' ') ptr++;
        } else if (strncmp(ptr, "depth", 5) == 0) {
            ptr += 5;
            while (*ptr == ' ') ptr++;
            depth = atoi(ptr);
            while (*ptr && *ptr != ' ') ptr++;
        } else if (strncmp(ptr, "movetime", 8) == 0) {
            ptr += 8;
            while (*ptr == ' ') ptr++;
            int mt = atoi(ptr);
            search_side = current_position.side_to_move;
            search_set_movetime(mt);
            depth = 64;
            has_movetime = true;
            while (*ptr && *ptr != ' ') ptr++;
        } else if (strncmp(ptr, "infinite", 8) == 0) {
            depth = 64;
            search_set_infinite();
            ptr += 8;
        } else {
            ptr++;
        }
    }
    
    search_running = true;
    
    // Vérifier le livre d'ouvertures (Polyglot prioritaire, fallback text)
    Move book_move = {NO_SQUARE, NO_SQUARE, QUIET, EMPTY};
    bool book_found = false;
    if (uci_own_book) {
        book_found = polyglot_book_probe(&current_position, &book_move);
        if (!book_found) {
            book_found = book_probe(&current_position, &book_move);
        }
    }
    Move best_book_move = {NO_SQUARE, NO_SQUARE, QUIET, EMPTY};
    bool book_move_valid = false;
    
    if (book_found) {
        // FIX : On génère d'abord les coups légaux pour s'assurer que le coup du livre appartient bien à la couleur active
        MoveList moves;
        generate_all_moves(&current_position, &moves);
        
        for (int i = 0; i < moves.count; i++) {
            if (moves.moves[i].from == book_move.from && moves.moves[i].to == book_move.to) {
                if (book_move.promo_piece == EMPTY || moves.moves[i].promo_piece == book_move.promo_piece) {
                    best_book_move = moves.moves[i];
                    book_move_valid = is_legal_move(&current_position, best_book_move);
                    break;
                }
            }
        }
    }
    
    // SI LE BOOK A UN COUP VALIDE → l'utiliser DIRECTEMENT, pas de search()
    // Le book contient 1.1M positions de parties maîtres (2500-3000 Elo)
    // C'est bien meilleur que la recherche à depth 8-10 en ouverture
    if (book_move_valid) {
        char move_str[6] = {0};
        move_str[0] = 'a' + (best_book_move.from % 8);
        move_str[1] = '1' + (best_book_move.from / 8);
        move_str[2] = 'a' + (best_book_move.to % 8);
        move_str[3] = '1' + (best_book_move.to / 8);
        if (best_book_move.promo_piece != EMPTY) {
            switch (best_book_move.promo_piece) {
                case QUEEN: move_str[4] = 'q'; break;
                case ROOK: move_str[4] = 'r'; break;
                case BISHOP: move_str[4] = 'b'; break;
                case KNIGHT: move_str[4] = 'n'; break;
                default: break;
            }
        }
        UCI_PRINTF("info depth 0 score cp 0 string book\n");
        UCI_PRINTF("bestmove %s\n", move_str);
        fflush(stdout);
        search_running = false;
        return;
    }
    
    // Configurer le time management
    search_side = current_position.side_to_move;
    if (!has_movetime) {
        search_set_time(wtime, btime, winc, binc, movestogo);
    }
    
    // Lancer la recherche (uniquement si le book n'a pas de coup)
    SearchResult result = search(&current_position, depth);
    
    // Fallback : si la recherche n'a pas trouvé de coup, utiliser le livre
    if ((result.best_move.from == NO_SQUARE || result.best_move.to == NO_SQUARE) && book_move_valid) {
        result.best_move = best_book_move;
    }
    
    // ---- VALIDATION du best_move ----
    if (result.best_move.from != NO_SQUARE && result.best_move.to != NO_SQUARE) {
        MoveList legal_moves;
        generate_all_moves(&current_position, &legal_moves);
        bool found = false;
        for (int i = 0; i < legal_moves.count; i++) {
            if (legal_moves.moves[i].from == result.best_move.from &&
                legal_moves.moves[i].to   == result.best_move.to &&
                legal_moves.moves[i].promo_piece == result.best_move.promo_piece) {
                found = true;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "WARNING: illegal best_move %c%d%c%d — fallback to first legal\n",
                    'a'+(result.best_move.from%8), (result.best_move.from/8)+1,
                    'a'+(result.best_move.to%8),   (result.best_move.to/8)+1);
            result.best_move.from = NO_SQUARE;
            result.best_move.to   = NO_SQUARE;
            // Fallback : premier coup légal
            for (int i = 0; i < legal_moves.count; i++) {
                if (is_legal_move(&current_position, legal_moves.moves[i])) {
                    result.best_move = legal_moves.moves[i];
                    break;
                }
            }
        }
    }
    
    if (result.best_move.from == NO_SQUARE || result.best_move.to == NO_SQUARE) {
        UCI_PRINTF("bestmove 0000\n");
        fflush(stdout);
        search_running = false;
        return;
    }
    
    // Afficher le meilleur coup
    char move_str[6] = {0};
    move_str[0] = 'a' + (result.best_move.from % 8);
    move_str[1] = '1' + (result.best_move.from / 8);
    move_str[2] = 'a' + (result.best_move.to % 8);
    move_str[3] = '1' + (result.best_move.to / 8);
    
    if (result.best_move.promo_piece != EMPTY) {
        switch (result.best_move.promo_piece) {
            case QUEEN: move_str[4] = 'q'; break;
            case ROOK: move_str[4] = 'r'; break;
            case BISHOP: move_str[4] = 'b'; break;
            case KNIGHT: move_str[4] = 'n'; break;
            default: break;
        }
    }
    
    // Ajouter le coup ponder si disponible
    if (result.ponder_move.from != NO_SQUARE && result.ponder_move.to != NO_SQUARE) {
        char ponder_str[6] = {0};
        ponder_str[0] = 'a' + (result.ponder_move.from % 8);
        ponder_str[1] = '1' + (result.ponder_move.from / 8);
        ponder_str[2] = 'a' + (result.ponder_move.to % 8);
        ponder_str[3] = '1' + (result.ponder_move.to / 8);
        if (result.ponder_move.promo_piece != EMPTY) {
            switch (result.ponder_move.promo_piece) {
                case QUEEN: ponder_str[4] = 'q'; break;
                case ROOK: ponder_str[4] = 'r'; break;
                case BISHOP: ponder_str[4] = 'b'; break;
                case KNIGHT: ponder_str[4] = 'n'; break;
                default: break;
            }
        }
        UCI_PRINTF("bestmove %s ponder %s\n", move_str, ponder_str);
    } else {
        UCI_PRINTF("bestmove %s\n", move_str);
    }
    fflush(stdout);
    
    search_running = false;
}

void uci_stop(void) {
    search_stopped = true;
}

void uci_ponderhit(void) {
    // Pondering
}

// ============================================================
// Boucle UCI principale
// ============================================================

void uci_command(const char *command) {
    if (strncmp(command, "uci", 3) == 0) {
        uci_uci();
    } else if (strncmp(command, "isready", 7) == 0) {
        uci_isready();
    } else if (strncmp(command, "position", 8) == 0) {
        uci_position(command + 9);
    } else if (strncmp(command, "go", 2) == 0) {
        uci_go(command + 3);
    } else if (strncmp(command, "stop", 4) == 0) {
        uci_stop();
    } else if (strncmp(command, "evalinc", 7) == 0) {
        // AXE 1 : test incrémental vs oracle (AVANT "eval" : préfixe commun)
        uci_evalinc();
    } else if (strncmp(command, "eval", 4) == 0) {
        // DEBUG: évaluation brute de la position courante (HCE ou NNUE selon option)
        int e = evaluate(&current_position);
        UCI_PRINTF("info string eval stm=%d value=%d\n", (int)current_position.side_to_move, e);
    } else if (strncmp(command, "ponderhit", 9) == 0) {
        uci_ponderhit();
    } else if (strncmp(command, "quit", 4) == 0) {
        engine_running = false;
    } else if (strncmp(command, "ucinewgame", 10) == 0) {
        tt_clear();
        clear_search_state();
        set_starting_position(&current_position);
    } else if (strncmp(command, "setoption", 9) == 0) {
        // Parse: setoption name <name> value <value>
        const char *ptr = command + 9;
        while (*ptr == ' ') ptr++;
        if (strncmp(ptr, "name", 4) == 0) {
            ptr += 4;
            while (*ptr == ' ') ptr++;
            // Extract option name
            char opt_name[64] = {0};
            int i = 0;
            while (*ptr && *ptr != ' ' && i < 63) {
                opt_name[i++] = *ptr++;
            }
            while (*ptr == ' ') ptr++;
            if (strncmp(ptr, "value", 5) == 0) {
                ptr += 5;
                while (*ptr == ' ') ptr++;
                if (strcmp(opt_name, "UseNNUE") == 0) {
                    if (strncmp(ptr, "true", 4) == 0) {
                        evaluation_set_nnue(1);
                    } else {
                        evaluation_set_nnue(0);
                    }
                } else if (strcmp(opt_name, "EvalFile") == 0) {
                    /* Charge le réseau au changement (<empty> = chemin vide, ignoré). */
                    char eval_path[512];
                    snprintf(eval_path, sizeof(eval_path), "%s", ptr);
                    size_t elen = strlen(eval_path);
                    while (elen > 0 && (eval_path[elen - 1] == ' ' || eval_path[elen - 1] == '\n' || eval_path[elen - 1] == '\r'))
                        eval_path[--elen] = '\0';
                    if (elen > 0 && strcmp(eval_path, "<empty>") != 0) {
                        if (!nnue_load(eval_path)) {
                            UCI_PRINTF("info string ERREUR EvalFile non charge : %s -> NNUE desactive, le moteur joue en HCE\n",
                                       eval_path);
                        }
                    }
                } else if (strcmp(opt_name, "OwnBook") == 0) {
                    if (strncmp(ptr, "true", 4) == 0) {
                        uci_own_book = 1;
                    } else {
                        uci_own_book = 0;
                    }
                } else if (strcmp(opt_name, "Hash") == 0) {
                    int mb = atoi(ptr);
                    if (mb >= 1 && mb <= 1024) {
                        tt_resize(mb);
                    }
                } else if (strcmp(opt_name, "Ponder") == 0) {
                    // Ponder option acknowledged but no special handling needed
                } else if (strcmp(opt_name, "MultiPV") == 0) {
                    multipv_count = atoi(ptr);
                    if (multipv_count < 1) multipv_count = 1;
                    if (multipv_count > MAX_MULTIPV) multipv_count = MAX_MULTIPV;
                }
            }
        }
    }
}

void uci_loop(void) {
    uci_loop_ex(stdin, stdout);
}

void uci_loop_ex(FILE *in, FILE *out) {
    char command[4096];
    
    while (engine_running) {
        if (fgets(command, sizeof(command), in) == NULL) {
            break;
        }
        
        command[strcspn(command, "\n")] = '\0';
        
        if (strlen(command) == 0) {
            continue;
        }
        
        uci_command_ex(command, out);
        fflush(out);
    }
}

void uci_command_ex(const char *command, FILE *out) {
    FILE *saved = g_uci_out;
    g_uci_out = out;
    uci_command(command);
    g_uci_out = saved;
}