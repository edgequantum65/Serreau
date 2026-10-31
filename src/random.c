#include "chess_engine.h"
#include <stdio.h>

uint64_t random_uint64(void) {
    static uint64_t seed = 0x1234567890ABCDEF;
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
}
