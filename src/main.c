#include "chess_engine.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
// Linux / sessions cloud : equivalents des deux fonctions Windows utilisees
#include <unistd.h>
static unsigned long long GetTickCount64(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000 + (unsigned long long)t.tv_nsec / 1000000;
}
static unsigned int GetCurrentProcessId(void) { return (unsigned int)getpid(); }
#endif

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    srand((unsigned int)(GetTickCount64() ^ (GetCurrentProcessId() * 1103515245U)));
    unsigned int saved_seed = (unsigned int)(GetTickCount64() ^ (GetCurrentProcessId() * 1103515245U));
    
    // Initialiser le moteur
    init_engine();
    
    // Restaurer la graine aléatoire (init_magic() l'écrase avec srand(42))
    srand(saved_seed);
    
    // Mode UCI par défaut (compatible Arena, CuteChess, stable_match.py)
    if (argc > 1 && strcmp(argv[1], "test") == 0) {
        printf("Running tests...\n");
    } else {
        uci_loop();
    }
    return 0;

    if (0) {
        // Mode interactif (désactivé, utiliser UCI)
        Position pos;
        set_starting_position(&pos);
        
        printf("Mode interactif\n");
        printf("Tapez 'help' pour les commandes\n\n");
        
        char input[256];
        while (1) {
            printf("chess> ");
            if (fgets(input, sizeof(input), stdin) == NULL) {
                break;
            }
            
            // Supprimer le newline
            input[strcspn(input, "\n")] = '\0';
            
            if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
                break;
            } else if (strcmp(input, "help") == 0) {
                printf("Commandes:\n");
                printf("  display  - Afficher la position\n");
                printf("  moves    - Lister les coups possibles\n");
                printf("  play <coup> - Jouer un coup\n");
                printf("  undo     - Annuler le dernier coup\n");
                printf("  new      - Nouvelle partie\n");
                printf("  fen      - Afficher le FEN\n");
                printf("  quit     - Quitter\n");
            } else if (strcmp(input, "display") == 0) {
                print_position(&pos);
            } else if (strcmp(input, "moves") == 0) {
                MoveList list;
                generate_all_moves(&pos, &list);
                printf("Coups possibles (%d):\n", list.count);
                for (int i = 0; i < list.count; i++) {
                    char from_str[3], to_str[3];
                    square_to_algebraic(list.moves[i].from, from_str);
                    square_to_algebraic(list.moves[i].to, to_str);
                    printf("  %s%s", from_str, to_str);
                    if (list.moves[i].promo_piece != EMPTY) {
                        printf("=%c", piece_name(list.moves[i].promo_piece)[0]);
                    }
                }
                printf("\n");
            } else if (strcmp(input, "fen") == 0) {
                char fen[256];
                get_fen(&pos, fen, sizeof(fen));
                printf("%s\n", fen);
            } else if (strncmp(input, "play ", 5) == 0) {
                // TODO: Implémenter le parsing de coup
                printf("Commande 'play' pas encore implémentée\n");
            } else if (strcmp(input, "new") == 0) {
                set_starting_position(&pos);
                printf("Nouvelle partie\n");
            } else if (strlen(input) > 0) {
                printf("Commande inconnue: %s\n", input);
            }
        }
    }
    
    return 0;
}
