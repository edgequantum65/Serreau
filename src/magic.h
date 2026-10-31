#ifndef MAGIC_H
#define MAGIC_H

#include "chess_engine.h"

void init_magic(void);
uint64_t rook_attacks(Square sq, uint64_t occupancy);
uint64_t bishop_attacks(Square sq, uint64_t occupancy);
uint64_t queen_attacks(Square sq, uint64_t occupancy);

#endif
