#ifndef NNUE_H
#define NNUE_H

#include "chess_engine.h"

// NNUE — Inférence compatible Stockfish 13 (nn-62ef826d1a6d.nnue)
// Spécification prouvée 30/08/2026 (hashes + comptabilité d'octets + source sf_13) :
//   Features : HalfKP(Friend) — 41024 indices (64 rois x 641 codes PS)
//   Réseau   : 41024 -> 256x2 -> [512] -> 32 -> 32 -> 1, sorties clampées 0..127
//   Quantif. : FT int16 (clamp direct 0..127), FC int8 (décalage >>6), sorties int32
//   Échelle  : Value = z / 16 ; cp = Value * 100 / 208 (PawnValueEg sf_13)
//   Convention : score relatif au camp au trait (negamax).

#define NNUE_INPUT_SIZE  41024   /* indices de features HalfKP */
#define NNUE_L1_SIZE     256     /* dimension d'un accumulateur (une perspective) */
#define NNUE_NUM_BUCKETS 1       /* le réseau SF13 n'a pas de buckets */

// Accumulateur incrémental d'UNE perspective (256 valeurs int32)
// + bucket de sortie (format DAVID uniquement ; ignoré en SF13)
typedef struct {
    int32_t values[NNUE_L1_SIZE];
    int bucket;
} NNUEAccumulator;

// Structure historique (conservée pour compatibilité de lien, non utilisée
// par la nouvelle inférence SF13)
typedef struct {
    int16_t *feature_weights;
    int16_t *feature_bias;
    int16_t *l1_weights;
    int16_t *l1_bias;
    int16_t *l2_weights;
    int16_t *l2_bias;
    int16_t *output_weights;
    int16_t *output_bias;
} NNUEWeights;

// Initialize allocations (appeler AVANT nnue_load)
void nnue_init(void);

// Réseau chargé et validé ?
bool nnue_is_loaded(void);

// Charge un réseau : détecte le format (SF13 par en-tête/hashs, DAVID par taille).
// Format inconnu => refus, nnue_is_loaded()=false, NNUE désactivé.
bool nnue_load(const char *path);
bool nnue_save(const char *path);

// Nom du format actuellement chargé : "SF13" | "DAVID" | "NONE"
const char *nnue_format_name(void);

// Évaluation complète (recalcule les deux perspectives)
// Retour : centipawns relatif au camp au trait
int nnue_evaluate(const Position *pos);

// Forward pass rapide : acc[2] = accumulateurs [BLANC], [NOIR] ;
// stm = camp au trait (sa perspective arrive en moitié 0 du vecteur 512).
int nnue_forward_fast(const NNUEAccumulator acc[2], int stm);

// Index de feature HalfKP (debug/validation)
int nnue_feature_index(Color king_color, Square king_sq, Color piece_color, Piece piece_type, Square piece_sq);

// Recalcule l'accumulateur d'une perspective
void nnue_compute_side(NNUEAccumulator *acc, const Position *pos, Color side);

// Mise à jour incrémentale des deux perspectives après un coup
void nnue_update_move(NNUEAccumulator next_acc[2], const NNUEAccumulator current_acc[2],
                      const Position *pos_after_move, Move move, Piece moved_piece,
                      Piece captured_piece, Color side_that_moved);

// Compat (sans effet : réseau SF13 sans buckets)
int nnue_bucket_of(const Position *pos);
void nnue_update_accumulator(NNUEAccumulator *acc, const Position *pos, Move move);
void nnue_train_generate(const char *output_path, int num_positions);

#endif // NNUE_H
