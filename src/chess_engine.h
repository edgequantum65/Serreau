#ifndef CHESS_ENGINE_H
#define CHESS_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// ============================================================
// Types de base
// ============================================================

// Couleurs
typedef enum {
    WHITE = 0,
    BLACK = 1
} Color;

// Pièces
typedef enum {
    EMPTY = 0,
    PAWN = 1,
    KNIGHT = 2,
    BISHOP = 3,
    ROOK = 4,
    QUEEN = 5,
    KING = 6
} Piece;

// Cases (a1 = 0, h8 = 63)
typedef enum {
    A1 = 0,  B1 = 1,  C1 = 2,  D1 = 3,  E1 = 4,  F1 = 5,  G1 = 6,  H1 = 7,
    A2 = 8,  B2 = 9,  C2 = 10, D2 = 11, E2 = 12, F2 = 13, G2 = 14, H2 = 15,
    A3 = 16, B3 = 17, C3 = 18, D3 = 19, E3 = 20, F3 = 21, G3 = 22, H3 = 23,
    A4 = 24, B4 = 25, C4 = 26, D4 = 27, E4 = 28, F4 = 29, G4 = 30, H4 = 31,
    A5 = 32, B5 = 33, C5 = 34, D5 = 35, E5 = 36, F5 = 37, G5 = 38, H5 = 39,
    A6 = 40, B6 = 41, C6 = 42, D6 = 43, E6 = 44, F6 = 45, G6 = 46, H6 = 47,
    A7 = 48, B7 = 49, C7 = 50, D7 = 51, E7 = 52, F7 = 53, G7 = 54, H7 = 55,
    A8 = 56, B8 = 57, C8 = 58, D8 = 59, E8 = 60, F8 = 61, G8 = 62, H8 = 63,
    NO_SQUARE = 64
} Square;

// Mouvements
typedef enum {
    QUIET = 0,
    DOUBLE_PAWN_PUSH = 1,
    KING_CASTLE = 2,
    QUEEN_CASTLE = 3,
    CAPTURE = 4,
    EN_PASSANT = 5,
    KNIGHT_PROMO = 6,
    BISHOP_PROMO = 7,
    ROOK_PROMO = 8,
    QUEEN_PROMO = 9,
    KNIGHT_PROMO_CAP = 10,
    BISHOP_PROMO_CAP = 11,
    ROOK_PROMO_CAP = 12,
    QUEEN_PROMO_CAP = 13
} MoveType;

// Droits de roque
typedef enum {
    NO_CASTLING = 0,
    WHITE_KING_CASTLE = 1,
    WHITE_QUEEN_CASTLE = 2,
    BLACK_KING_CASTLE = 4,
    BLACK_QUEEN_CASTLE = 8,
    ALL_CASTLING = 15
} CastlingRight;

// ============================================================
// Structures
// ============================================================

// Structure de mouvement
typedef struct {
    Square from;
    Square to;
    MoveType type;
    Piece promo_piece;
} Move;

// État sauvegardé pour unmake_move
typedef struct {
    Square en_passant;
    int castling;
    int halfmove_clock;
    uint64_t zobrist;
    Piece captured_piece; // Pièce capturée (pour restauration)
} UndoInfo;

// Position complète
typedef struct {
    // Bitboards: 1 bit par case (64 bits)
    uint64_t piece[2][7];  // [couleur][pièce]
    uint64_t occupancy[3]; // [blancs][noirs][tous]
    
    // État du jeu
    Color side_to_move;
    Square en_passant;
    int castling;
    int halfmove_clock;
    int fullmove_number;
    
    // Zobrist hash (pour les transpositions)
    uint64_t zobrist;
    
    // Pile pour unmake_move
    UndoInfo stack[512];
    int sp; // stack pointer
} Position;

// ============================================================
// Constantes
// ============================================================

#if defined(__GNUC__)
#define MAYBE_UNUSED __attribute__((unused))
#else
#define MAYBE_UNUSED
#endif

#define MAX_MULTIPV 256
#define MAX_PV_LENGTH 64

// Bitboards utiles
extern const uint64_t FILE_A;
extern const uint64_t FILE_B;
extern const uint64_t FILE_G;
extern const uint64_t FILE_H;
extern const uint64_t RANK_1;
extern const uint64_t RANK_2;
extern const uint64_t RANK_7;
extern const uint64_t RANK_8;
extern const uint64_t CENTER;
extern const uint64_t KING_SIDE;
extern const uint64_t QUEEN_SIDE;

// ============================================================
// Fonctions de base
// ============================================================

// Initialisation
void init_engine(void);
void init_bitboards(void);
void init_zobrist(void);
uint64_t polyglot_key(const Position *pos);

// Position
void set_starting_position(Position *pos);
void set_fen(Position *pos, const char *fen);
void get_fen(const Position *pos, char *fen, int max_len);

// Affichage
void print_bitboard(uint64_t bb);
void print_position(const Position *pos);
const char *square_name(Square sq);
const char *piece_name(Piece p);

// Conversion
Square algebraic_to_square(const char *str);
void square_to_algebraic(Square sq, char *str);

// ============================================================
// Génération de coups
// ============================================================

// Nombre maximum de coups par position
#define MAX_MOVES 256

// Liste de coups
typedef struct {
    Move moves[MAX_MOVES];
    int count;
} MoveList;

// Génération
void generate_all_moves(const Position *pos, MoveList *list);
void generate_captures(const Position *pos, MoveList *list);
void generate_quiet_moves(const Position *pos, MoveList *list);

// Validation
bool is_legal_move(const Position *pos, Move move);
bool is_in_check(const Position *pos, Color color);
bool is_square_attacked(const Position *pos, Square sq, Color by_color);

// Application de coups
void make_move(Position *pos, Move move);
void unmake_move(Position *pos, Move move);

// ============================================================
// Recherche
// ============================================================

// Nœud de recherche
typedef struct {
    Move best_move;
    Move ponder_move;
    int score;
    int depth;
    uint64_t nodes;
} SearchResult;

// Ligne MultiPV
typedef struct {
    int depth;
    int seldepth;
    int score_cp;
    bool is_mate;
    Move pv[MAX_PV_LENGTH];
    int pv_length;
    bool valid;
} PvLine;

// Fonctions de recherche
SearchResult search(Position *pos, int depth);
SearchResult search_time(Position *pos, int time_ms);
int alpha_beta(Position *pos, int depth, int alpha, int beta, bool is_pv, int ply,
               Move opp_last_move);
int quiescence(Position *pos, int alpha, int beta, int ply);

// Time management
void search_set_time(int wtime, int btime, int winc, int binc, int movestogo);
void search_set_movetime(int ms);
void search_set_infinite(void);
bool search_is_time_up(void);
extern int search_side;
extern int multipv_count;
extern int search_seldepth;
extern bool search_stopped;

// Table de transposition
typedef enum {
    TT_EXACT,
    TT_ALPHA,
    TT_BETA
} TTFlag;

void tt_clear(void);
void tt_new_search(void);
void clear_search_state(void);
void tt_resize(int mb);
void tt_store(uint64_t key, Move best_move, int score, int depth, TTFlag flag);
bool tt_probe(uint64_t key, int depth, int alpha, int beta, int *score, Move *best_move);
int tt_hashfull(void);

// SEE (Static Exchange Evaluation)
int see_capture(const Position *pos, Move move);

// ============================================================
// Évaluation
// ============================================================

// Scores
#define INF_SCORE 32001
#define MATE_SCORE 32000
#define DRAW_SCORE 0

// Valeurs matérielles
#define PAWN_VALUE   100
#define KNIGHT_VALUE 300
#define BISHOP_VALUE 300
#define ROOK_VALUE   500
#define QUEEN_VALUE  900
#define KING_VALUE   20000

// Fonctions d'évaluation
int evaluate(const Position *pos);
int evaluate_material(const Position *pos);
int evaluate_pawn_structure(const Position *pos);
int evaluate_king_safety(const Position *pos);
int evaluate_mobility(const Position *pos);
bool is_insufficient_material(const Position *pos);
void evaluation_set_nnue(int enabled);
int evaluation_get_nnue(void);

// ============================================================
// UCI
// ============================================================

// Interface UCI
void uci_loop(void);
void uci_loop_ex(FILE *in, FILE *out);
void uci_command(const char *command);
void uci_command_ex(const char *command, FILE *out);
extern FILE *g_uci_out;  // NULL = stdout; set by DLL to pipe FILE*
#define UCI_PRINTF(...) fprintf(g_uci_out ? g_uci_out : stdout, __VA_ARGS__)

// Commandes UCI
void uci_uci(void);
void uci_isready(void);
void uci_position(const char *args);
void uci_go(const char *args);
void uci_stop(void);
void uci_ponderhit(void);

// ============================================================
// Utilitaires
// ============================================================

// Bit manipulation
int count_bits(uint64_t bb);
Square lsb(uint64_t bb);
Square pop_lsb(uint64_t *bb);

// Time
uint64_t get_time_ms(void);

// Random
uint64_t random_uint64(void);

// Zobrist tables (defined in board.c)
extern uint64_t zobrist_piece[2][7][64];
extern uint64_t zobrist_castling[16];
extern uint64_t zobrist_en_passant[65];
extern uint64_t zobrist_side;

// Polyglot book
int polyglot_book_load(const char *path);
bool polyglot_book_probe(const Position *pos, Move *best_move);

// Tablebases
void tb_init_syzygy(void);
int tb_probe_wdl(const Position *pos);
bool tb_probe_move(const Position *pos, Move *best_move);

// PGN Export
void move_to_algebraic(const Position *pos, Move move, char *str);
void export_pgn(const Move *moves, int move_count, const char *result,
                const char *white_name, const char *black_name,
                const char *event_name, const char *filename);

#endif // CHESS_ENGINE_H
