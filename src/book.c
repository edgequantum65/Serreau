#include "book.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BOOK_SIZE 7000000

static BookEntry book_table[BOOK_SIZE];
static int book_count = 0;
static bool book_sorted = false;

// Fonction de comparaison pour le tri du livre
static int book_compare(const void *a, const void *b) {
    uint64_t ha = ((const BookEntry*)a)->hash;
    uint64_t hb = ((const BookEntry*)b)->hash;
    if (ha < hb) return -1;
    if (ha > hb) return 1;
    return 0;
}

void book_init(void) {
    book_count = 0;
    book_sorted = false;
    memset(book_table, 0, sizeof(book_table));
}

void book_add_position(const char *fen, const char *move_str, int score) {
    if (book_count >= BOOK_SIZE) return;
    
    Position temp;
    set_fen(&temp, fen);
    
    uint64_t hash = temp.zobrist;
    if (temp.en_passant != NO_SQUARE) {
        hash ^= zobrist_en_passant[temp.en_passant];
        hash ^= zobrist_en_passant[NO_SQUARE];
    }
    
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
    
    Move move = {from, to, QUIET, promo};
    
    book_table[book_count].hash = hash;
    book_table[book_count].move = move;
    book_table[book_count].score = score;
    book_table[book_count].weight = 100;
    book_count++;
}

bool book_probe(const Position *pos, Move *best_move) {
    if (book_count == 0) return false;
    
    uint64_t hash = pos->zobrist;
    if (pos->en_passant != NO_SQUARE) {
        hash ^= zobrist_en_passant[pos->en_passant];
        hash ^= zobrist_en_passant[NO_SQUARE];
    }
    
    // Binary search to find first match
    int lo = 0, hi = book_count - 1;
    int first = -1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (book_table[mid].hash < hash) {
            lo = mid + 1;
        } else if (book_table[mid].hash > hash) {
            hi = mid - 1;
        } else {
            first = mid;
            hi = mid - 1; // Continue searching left for first match
        }
    }
    if (first < 0) return false;
    
    // Collect all moves with this hash (and their total weight)
    Move candidates[16];
    int weights[16];
    int count = 0;
    for (int i = first; i < book_count && book_table[i].hash == hash && count < 16; i++) {
        bool found = false;
        for (int j = 0; j < count; j++) {
            if (candidates[j].from == book_table[i].move.from &&
                candidates[j].to == book_table[i].move.to &&
                candidates[j].promo_piece == book_table[i].move.promo_piece) {
                weights[j] += book_table[i].score;
                found = true;
                break;
            }
        }
        if (!found) {
            candidates[count] = book_table[i].move;
            weights[count] = book_table[i].score;
            count++;
        }
    }
    
    if (count == 0) return false;
    
    // Pick weighted random move (best moves more likely)
    int total_weight = 0;
    for (int i = 0; i < count; i++) total_weight += weights[i];
    if (total_weight <= 0) {
        *best_move = candidates[rand() % count];
        return true;
    }
    int pick = rand() % total_weight;
    for (int i = 0; i < count; i++) {
        pick -= weights[i];
        if (pick < 0) {
            *best_move = candidates[i];
            return true;
        }
    }
    *best_move = candidates[count - 1];
    return true;
}

// Load EPD file (Stockfish format: FEN bm MOVE1;)
MAYBE_UNUSED static int load_epd(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    
    char line[512];
    int loaded = 0;
    
    while (fgets(line, sizeof(line), f) && loaded < BOOK_SIZE) {
        if (line[0] == '\n' || line[0] == '\r') continue;
        
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        nl = strchr(line, '\r');
        if (nl) *nl = '\0';
        
        // Find " bm " in the line
        char *bm = strstr(line, " bm ");
        if (!bm) continue;
        
        *bm = '\0'; // Terminate FEN here
        char *fen = line;
        char *moves = bm + 4;
        
        // Parse moves: "MOVE1 MOVE2; id ..."
        while (*moves && *moves != ';' && *moves != ' ') {
            char move_str[8];
            int mi = 0;
            while (*moves && *moves != ' ' && *moves != ';' && *moves != '\0' && mi < 6) {
                move_str[mi++] = *moves++;
            }
            move_str[mi] = '\0';
            
            if (strlen(move_str) >= 4) {
                book_add_position(fen, move_str, 100);
                loaded++;
            }
            
            while (*moves == ' ') moves++;
        }
    }
    
    fclose(f);
    return loaded;
}

// Load text book file (format: FEN MOVE SCORE)
int load_text_book(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    
    char line[512];
    int loaded = 0;
    
    while (fgets(line, sizeof(line), f) && loaded < BOOK_SIZE) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        nl = strchr(line, '\r');
        if (nl) *nl = '\0';
        
        int len = (int)strlen(line);
        if (len < 6) continue;
        
        char *p = line + len - 1;
        while (p > line && *p != ' ') p--;
        if (p <= line) continue;
        int score = atoi(p + 1);
        *p = '\0';
        
        p--;
        while (p > line && *p != ' ') p--;
        if (p <= line) continue;
        char *move_str = p + 1;
        *p = '\0';
        
        if (strlen(move_str) < 4) continue;
        
        book_add_position(line, move_str, score);
        loaded++;
    }
    
    fclose(f);
    return loaded;
}

void book_sort(void) {
    if (book_count > 0) {
        qsort(book_table, book_count, sizeof(BookEntry), book_compare);
        book_sorted = true;
    }
}

void book_load_builtin(void) {
    if (book_count > 0) return;
    
    // Fallback: built-in book
    book_add_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4", 200);
    book_add_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "d2d4", 200);
    book_add_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "c2c4", 150);
    book_add_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "g1f3", 150);
    
    fprintf(stderr, "Book chargé: %d positions (built-in)\n", book_count);
}
