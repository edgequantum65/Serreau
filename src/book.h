#ifndef BOOK_H
#define BOOK_H

#include "chess_engine.h"

// Structure d'une entrée de livre
typedef struct {
    uint64_t hash;
    Move move;
    int score;
    int weight;
} BookEntry;

// Fonctions du livre d'ouvertures
void book_init(void);
bool book_probe(const Position *pos, Move *best_move);
void book_add_position(const char *fen, const char *move_str, int score);
int load_text_book(const char *path);
void book_sort(void);

// Livre intégré (ouvertures principales)
void book_load_builtin(void);

#endif // BOOK_H
