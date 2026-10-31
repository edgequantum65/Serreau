#include "chess_engine.h"
#include <stdio.h>
#include <stdint.h>

int main() {
    init_engine();
    
    Position pos;
    set_starting_position(&pos);
    
    uint64_t key = polyglot_key(&pos);
    printf("Polyglot key for startpos: 0x%016llX\n", (unsigned long long)key);
    printf("Expected:                  0x463B96181691FC9C\n");
    
    if (key == 0x463B96181691FC9CULL) {
        printf("MATCH!\n");
    } else {
        printf("MISMATCH! Keys don't match.\n");
        return 1;  // echec visible par ctest et par les scripts
    }
    
    return 0;
}
