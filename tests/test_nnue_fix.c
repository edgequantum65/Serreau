/**
 * Test NNUE fix: accumulateur int16→int32
 * Évalue 3 positions et affiche les scores NNUE + HCE pour vérification
 */
#include <stdio.h>
#include <string.h>
#include "chess_engine.h"
#include "nnue.h"

static int nb_identiques = 0;

static void print_score(const char *label, Position *pos) {
    /* evaluate() renvoie le NNUE des qu'un reseau est charge : on coupe le NNUE
     * pour obtenir le vrai HCE (avant le 03/10/2026, les deux colonnes etaient
     * le meme score NNUE). */
    evaluation_set_nnue(0);
    int hce = evaluate(pos);
    evaluation_set_nnue(1);
    int nnue = nnue_evaluate(pos);
    if (hce == nnue) nb_identiques++;
    printf("%-40s | HCE: %+5d cp | NNUE: %+5d cp\n", label, hce, nnue);
}

int main() {
    printf("=== Test NNUE: accumulateur int16→int32 ===\n\n");
    
    nnue_init();
    if (!nnue_load("E:/ChessEngine/books/nnue_weights.bin"))
        nnue_load("books/nnue_weights.bin");  /* depuis la racine du projet (ctest, Linux) */
    
    if (!nnue_is_loaded()) {
        printf("SKIP: poids NNUE absents (books/nnue_weights.bin n'est pas dans le dépôt)\n");
        return 77;
    }
    printf("Poids NNUE chargés OK\n\n");
    
    // Position 1: Position initiale
    Position pos1;
    set_starting_position(&pos1);
    print_score("Départ (rnbqkbnr/pppppppp/8...)", &pos1);
    
    // Position 2: Après e4 (avantage blanc)
    Position pos2;
    set_starting_position(&pos2);
    MoveList moves;
    generate_all_moves(&pos2, &moves);
    // Cherche e2-e4
    Move e4 = {0};
    for (int i = 0; i < moves.count; i++) {
        if (moves.moves[i].from == 12 && moves.moves[i].to == 28) { // e2->e4
            e4 = moves.moves[i];
            break;
        }
    }
    if (e4.from != 0 || e4.to != 0) {
        make_move(&pos2, e4);
        print_score("Après 1.e4", &pos2);
    }
    
    // Position 3: Mat du berger possible (après 1.e4 e5 2.Bc4 Nc6 3.Qh5)
    Position pos3;
    set_starting_position(&pos3);
    // 1.e4
    for (int i = 0; i < moves.count; i++) {
        if (moves.moves[i].from == 12 && moves.moves[i].to == 28) {
            make_move(&pos3, moves.moves[i]);
            break;
        }
    }
    // e7-e5
    generate_all_moves(&pos3, &moves);
    for (int i = 0; i < moves.count; i++) {
        if (moves.moves[i].from == 52 && moves.moves[i].to == 36) {
            make_move(&pos3, moves.moves[i]);
            break;
        }
    }
    // Bf1-c4
    generate_all_moves(&pos3, &moves);
    for (int i = 0; i < moves.count; i++) {
        if (moves.moves[i].from == 5 && moves.moves[i].to == 18) {
            make_move(&pos3, moves.moves[i]);
            break;
        }
    }
    // Nb8-c6
    generate_all_moves(&pos3, &moves);
    for (int i = 0; i < moves.count; i++) {
        if (moves.moves[i].from == 57 && moves.moves[i].to == 42) {
            make_move(&pos3, moves.moves[i]);
            break;
        }
    }
    // Qd1-h5
    generate_all_moves(&pos3, &moves);
    for (int i = 0; i < moves.count; i++) {
        if (moves.moves[i].from == 3 && moves.moves[i].to == 31) {
            make_move(&pos3, moves.moves[i]);
            break;
        }
    }
    print_score("Scholar's Mate setup", &pos3);
    
    printf("\n=== Test terminé ===\n");
    if (nb_identiques == 3) {
        printf("ERREUR: HCE et NNUE identiques sur les 3 positions -> l'un des deux n'est pas mesure\n");
        return 1;
    }
    return 0;
}
