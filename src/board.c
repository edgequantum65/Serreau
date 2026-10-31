#include "chess_engine.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

// ============================================================
// Initialisation du moteur
// ============================================================

#include "book.h"
#include "nnue.h"
#include "magic.h"

void init_engine(void) {
    init_bitboards();
    init_zobrist();
    init_magic();
    // Polyglot book (chargement instantané), fallback text book
    const char *polyglot_paths[] = {"E:/ChessEngine/books/book.bin", "books/book.bin", "../books/book.bin", NULL};
    int pg_loaded = 0;
    for (int i = 0; polyglot_paths[i]; i++) {
        pg_loaded = polyglot_book_load(polyglot_paths[i]);
        if (pg_loaded > 0) {
            fprintf(stderr, "Polyglot book chargé: %s (%d positions)\n", polyglot_paths[i], pg_loaded);
            break;
        }
    }
    // Fallback text book si Polyglot pas trouvé
    if (pg_loaded == 0) {
        const char *text_paths[] = {"E:/ChessEngine/books/book_merged.txt", "books/book_merged.txt", "../books/book_merged.txt", NULL};
        for (int i = 0; text_paths[i]; i++) {
            if (load_text_book(text_paths[i]) > 0) {
                fprintf(stderr, "Text book chargé: %s\n", text_paths[i]);
                book_sort();
                break;
            }
        }
    }
    nnue_init();
    tb_init_syzygy();
    /* S38 16/10/2026 (objectif David : « je veux que le nnue fonctionne — ça a toujours été mon
       objectif ») : charger le réseau ENTRAÎNÉ DE DAVID (books/nnue_weights.bin, format DAVID)
       PAR DÉFAUT. Le réseau SF13 nn-62ef826d1a6d.nnue n'est plus qu'un DERNIER recours.
       nnue_load() détecte le format : DAVID par taille, SF13 par en-tête/hashs. */
    const char *nnue_paths[] = {
        "E:/ChessEngine/books/nnue_weights.bin",    // réseau de David (format DAVID) — DÉFAUT
        "books/nnue_weights.bin",                   // même réseau, chemin relatif
        "E:/tmp/sf-networks/nn-62ef826d1a6d.nnue",  // dernier recours : SF13 (déprécié)
        NULL
    };
    for (int i = 0; nnue_paths[i]; i++) {
        if (nnue_load(nnue_paths[i])) {
            fprintf(stderr, "NNUE chargé: %s\n", nnue_paths[i]);
            break;
        }
    }
}

void init_bitboards(void) {
    // Les bitboards sont déjà initialisés comme constantes
    // Rien à faire ici pour l'instant
}

// ============================================================
// Position de départ
// ============================================================

void set_starting_position(Position *pos) {
    set_fen(pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

// ============================================================
// Parsing FEN
// ============================================================

void set_fen(Position *pos, const char *fen) {
    // Réinitialiser la position
    memset(pos, 0, sizeof(Position));
    
    // Parser la position des pièces
    int square = 56; // Commencer par a8
    
    while (*fen && *fen != ' ') {
        if (*fen == '/') {
            square -= 16; // Ligne suivante
        } else if (*fen >= '1' && *fen <= '8') {
            square += *fen - '0';
        } else {
            Color color = islower(*fen) ? BLACK : WHITE;
            Piece piece = EMPTY;
            
            switch (tolower(*fen)) {
                case 'p': piece = PAWN; break;
                case 'n': piece = KNIGHT; break;
                case 'b': piece = BISHOP; break;
                case 'r': piece = ROOK; break;
                case 'q': piece = QUEEN; break;
                case 'k': piece = KING; break;
            }
            
            if (piece != EMPTY) {
                pos->piece[color][piece] |= (1ULL << square);
            }
            square++;
        }
        fen++;
    }
    
    // Mettre à jour l'occupancy
    pos->occupancy[WHITE] = 0;
    pos->occupancy[BLACK] = 0;
    for (int p = PAWN; p <= KING; p++) {
        pos->occupancy[WHITE] |= pos->piece[WHITE][p];
        pos->occupancy[BLACK] |= pos->piece[BLACK][p];
    }
    pos->occupancy[2] = pos->occupancy[WHITE] | pos->occupancy[BLACK];
    
    // Parser le trait
    fen++;
    if (*fen == 'w') {
        pos->side_to_move = WHITE;
    } else {
        pos->side_to_move = BLACK;
    }
    
    // Parser les droits de roque
    fen += 2;
    pos->castling = NO_CASTLING;
    while (*fen && *fen != ' ') {
        switch (*fen) {
            case 'K': pos->castling |= WHITE_KING_CASTLE; break;
            case 'Q': pos->castling |= WHITE_QUEEN_CASTLE; break;
            case 'k': pos->castling |= BLACK_KING_CASTLE; break;
            case 'q': pos->castling |= BLACK_QUEEN_CASTLE; break;
            case '-': break;
        }
        fen++;
    }
    
    // Parser en passant
    fen++;
    if (*fen == '-') {
        pos->en_passant = NO_SQUARE;
        fen++;
    } else {
        pos->en_passant = algebraic_to_square(fen);
        fen += 2;
    }
    
    // Parser le demi-coups
    fen++;
    pos->halfmove_clock = 0;
    while (*fen && *fen != ' ') {
        pos->halfmove_clock = pos->halfmove_clock * 10 + (*fen - '0');
        fen++;
    }
    
    // Parser le numéro de coup
    fen++;
    pos->fullmove_number = 0;
    while (*fen && *fen != '\0') {
        pos->fullmove_number = pos->fullmove_number * 10 + (*fen - '0');
        fen++;
    }
    if (pos->fullmove_number == 0) pos->fullmove_number = 1; // Minimum 1
    
    // Calculer le hash Zobrist
    pos->zobrist = 0;
    for (int c = 0; c < 2; c++) {
        for (int p = 1; p <= 6; p++) {
            uint64_t bb = pos->piece[c][p];
            while (bb) {
                Square sq = pop_lsb(&bb);
                pos->zobrist ^= zobrist_piece[c][p][sq];
            }
        }
    }
    pos->zobrist ^= zobrist_castling[pos->castling];
    pos->zobrist ^= zobrist_en_passant[pos->en_passant];
    if (pos->side_to_move == BLACK) {
        pos->zobrist ^= zobrist_side;
    }
}

// ============================================================
// Génération FEN
// ============================================================

void get_fen(const Position *pos, char *fen, int max_len) {
    (void)max_len;
    int idx = 0;
    
    // Position des pièces
    for (int rank = 7; rank >= 0; rank--) {
        int empty = 0;
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
                empty++;
            } else {
                if (empty > 0) {
                    fen[idx++] = '0' + empty;
                    empty = 0;
                }
                char c = piece_name(piece)[0];
                if (color == BLACK) {
                    c = tolower(c);
                }
                fen[idx++] = c;
            }
        }
        if (empty > 0) {
            fen[idx++] = '0' + empty;
        }
        if (rank > 0) {
            fen[idx++] = '/';
        }
    }
    
    // Trait
    fen[idx++] = ' ';
    fen[idx++] = pos->side_to_move == WHITE ? 'w' : 'b';
    
    // Droits de roque
    fen[idx++] = ' ';
    if (pos->castling == NO_CASTLING) {
        fen[idx++] = '-';
    } else {
        if (pos->castling & WHITE_KING_CASTLE) fen[idx++] = 'K';
        if (pos->castling & WHITE_QUEEN_CASTLE) fen[idx++] = 'Q';
        if (pos->castling & BLACK_KING_CASTLE) fen[idx++] = 'k';
        if (pos->castling & BLACK_QUEEN_CASTLE) fen[idx++] = 'q';
    }
    
    // En passant
    fen[idx++] = ' ';
    if (pos->en_passant == NO_SQUARE) {
        fen[idx++] = '-';
    } else {
        square_to_algebraic(pos->en_passant, &fen[idx]);
        idx += 2;
    }
    
    // Demi-coups
    fen[idx++] = ' ';
    idx += sprintf(&fen[idx], "%d", pos->halfmove_clock);
    
    // Numéro de coup
    fen[idx++] = ' ';
    idx += sprintf(&fen[idx], "%d", pos->fullmove_number);
    
    fen[idx] = '\0';
}

// ============================================================
// Conversion move → notation algébrique standard
// ============================================================

void move_to_algebraic(const Position *pos, Move move, char *str) {
    Piece piece = EMPTY;
    Color color = pos->side_to_move;

    // Trouver la pièce sur la case de départ
    for (int p = PAWN; p <= KING; p++) {
        if (pos->piece[color][p] & (1ULL << move.from)) {
            piece = (Piece)p;
            break;
        }
    }

    if (piece == EMPTY) {
        strcpy(str, "??");
        return;
    }

    // Roque
    if (move.type == KING_CASTLE) {
        strcpy(str, "O-O");
        return;
    }
    if (move.type == QUEEN_CASTLE) {
        strcpy(str, "O-O-O");
        return;
    }

    char from_str[3], to_str[3];
    square_to_algebraic(move.from, from_str);
    square_to_algebraic(move.to, to_str);

    // Déterminer si c'est une capture
    bool is_capture = (move.type == CAPTURE || move.type == EN_PASSANT ||
                       move.type >= KNIGHT_PROMO_CAP);
    if (!is_capture) {
        // Vérifier s'il y a une pièce ennemie sur la case d'arrivée
        Color enemy = 1 - color;
        for (int p = PAWN; p <= KING; p++) {
            if (pos->piece[enemy][p] & (1ULL << move.to)) {
                is_capture = true;
                break;
            }
        }
    }

    int idx = 0;

    if (piece == PAWN) {
        // Pions
        if (is_capture) {
            str[idx++] = from_str[0]; // colonne de départ
            str[idx++] = 'x';
        }
        str[idx++] = to_str[0];
        str[idx++] = to_str[1];

        // Promotion
        if (move.promo_piece != EMPTY) {
            str[idx++] = '=';
            str[idx++] = piece_name(move.promo_piece)[0];
        }
    } else {
        // Pièces
        str[idx++] = piece_name(piece)[0];

        // Désambiguïsation : vérifier si une autre pièce du même type
        // peut aussi aller sur la case d'arrivée
        MoveList moves;
        generate_all_moves(pos, &moves);
        bool need_file = false;
        bool need_rank = false;

        for (int i = 0; i < moves.count; i++) {
            if (moves.moves[i].to == move.to &&
                moves.moves[i].from != move.from) {
                // Une autre pièce peut aller sur la même case
                Piece other_piece = EMPTY;
                for (int p = PAWN; p <= KING; p++) {
                    if (pos->piece[color][p] & (1ULL << moves.moves[i].from)) {
                        other_piece = (Piece)p;
                        break;
                    }
                }
                if (other_piece == piece) {
                    if ((move.from % 8) == (moves.moves[i].from % 8)) {
                        need_rank = true;  // même colonne → besoin rangée
                    } else {
                        need_file = true;  // colonnes différentes → besoin colonne
                    }
                }
            }
        }

        if (need_file) {
            str[idx++] = from_str[0];
        }
        if (need_rank) {
            str[idx++] = from_str[1];
        }

        if (is_capture) {
            str[idx++] = 'x';
        }

        str[idx++] = to_str[0];
        str[idx++] = to_str[1];
    }

    str[idx] = '\0';
}

// ============================================================
// Export PGN
// ============================================================

void export_pgn(const Move *moves, int move_count, const char *result,
                const char *white_name, const char *black_name,
                const char *event_name, const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "export_pgn: cannot open %s\n", filename);
        return;
    }

    // En-têtes PGN
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char date_str[11];
    strftime(date_str, sizeof(date_str), "%Y.%m.%d", tm_info);

    fprintf(f, "[Event \"%s\"]\n", event_name ? event_name : "ChessEngine Game");
    fprintf(f, "[Site \"Local\"]\n");
    fprintf(f, "[Date \"%s\"]\n", date_str);
    fprintf(f, "[Round \"?\"]\n");
    fprintf(f, "[White \"%s\"]\n", white_name ? white_name : "White");
    fprintf(f, "[Black \"%s\"]\n", black_name ? black_name : "Black");
    fprintf(f, "[Result \"%s\"]\n", result ? result : "*");
    fprintf(f, "\n");

    // Rejouer les coups pour générer la notation algébrique
    Position pos;
    set_starting_position(&pos);

    int line_len = 0;
    for (int i = 0; i < move_count; i++) {
        // Numéro de coup pour les blancs
        if (i % 2 == 0) {
            if (i > 0) fprintf(f, " ");
            fprintf(f, "%d.", i / 2 + 1);
            line_len += 4;
        }

        char alg[10];
        move_to_algebraic(&pos, moves[i], alg);
        fprintf(f, " %s", alg);
        line_len += (int)strlen(alg) + 1;

        // Appliquer le coup pour la position suivante
        make_move(&pos, moves[i]);

        // Retour à la ligne si la ligne est trop longue
        if (line_len > 70 && i + 1 < move_count) {
            fprintf(f, "\n");
            line_len = 0;
        }
    }

    // Résultat
    fprintf(f, " %s\n\n", result ? result : "*");
    fclose(f);
}
