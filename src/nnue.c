/**
 * nnue.c - Inférence NNUE compatible Stockfish 13 (réseau nn-62ef826d1a6d.nnue)
 *
 * RÉÉCRITURE 30/08/2026 — spécification dérivée du source officiel Stockfish sf_13 :
 *   - Format : raw little-endian (PAS de LEB128 pour ce réseau), prouvé par
 *     comptabilité d'octets exacte (21 022 697) + chaînes de hashs validées :
 *     réseau 0x3E5AA6EE, feature transformer 0x5D69D7B8, couches 0x63337156.
 *   - Features : HalfKP(Friend) — index = 641 * orient(P, roi_P) + code_PS(P, pièce) + orient(P, case)
 *     orient(P, s) = s ^ (P == NOIR ? 63 : 0)  (rotation 180°, PAS un flip de rangée)
 *     codes PS (entrelacés us/them, pas de 128) : P=1/65 N=129/193 B=257/321 R=385/449 Q=513/577
 *   - Architecture : 41024 -> 256x2 (clamp 0..127) -> [512] -> FC32 (int8/int32) -> clamp(>>6, 0..127)
 *     -> FC32 -> clamp(>>6, 0..127) -> FC1 -> Value = z / 16 (FV_SCALE)
 *   - Conversion cp : cp = Value * 100 / 208 (PawnValueEg = 208 dans sf_13)
 *   - Convention de sortie : score relatif au camp au trait (negamax).
 */

#include "nnue.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <emmintrin.h>   // SSE2
#ifdef __AVX2__
#include <immintrin.h>   // AVX2
#endif

/* ======================================================================
 * CONSTANTES DU FORMAT (sf_13 — prouvées par hashs)
 * ===================================================================== */

#define SF_VERSION      0x7AF32F16u
#define NET_HASH        0x3E5AA6EEu
#define FT_HASH         0x5D69D7B8u
#define LAYERS_HASH     0x63337156u

#define HALF_DIM        256
#define FEATURE_COUNT   41024          /* 64 rois x 641 codes */
#define PS_END          641
#define FV_SCALE        16
#define WS_BITS         6              /* kWeightScaleBits */
#define PAWN_VALUE_EG   208            /* sf_13 types.h */

/* ======================================================================
 * DOUBLE CHARGEUR : format SF13 (ci-dessus) OU format DAVID (Python v12/v13/v14)
 *
 * Le format DAVID est repris de l'historique git (commit 18b4875, fichier
 * src/nnue.c) : HalfKP 40960 -> 256 -> 32 -> 32 -> [8 buckets] -> 1, int16,
 * activation SCReLU, sortie (output*100)/256. Les 3 tailles disque de David
 *   20 990 658 o (v12) / 20 990 856 o (v13, +queue WDL 198 o) / 20 991 120 o (v14, 8 buckets)
 * sont détectées par taille ; le format SF13 reste détecté par ses hashs.
 * ===================================================================== */
typedef enum { NNUE_FORMAT_NONE = 0, NNUE_FORMAT_SF13 = 1, NNUE_FORMAT_DAVID = 2 } NNUEFormat;

#define DAVID_INPUT     40960
#define DAVID_HALF      256
#define DAVID_L2        32
#define DAVID_L3        32
#define DAVID_BUCKETS   8

static int16_t *david_ft_weights = NULL;              /* [40960][256] */
static int16_t  david_ft_bias[DAVID_HALF];
static int16_t *david_l1_weights = NULL;              /* [256][32] */
static int16_t  david_l1_bias[DAVID_L2];
static int16_t *david_l2_weights = NULL;              /* [32][32] */
static int16_t  david_l2_bias[DAVID_L3];
static int16_t  david_out_weights[DAVID_BUCKETS * DAVID_L3];
static int16_t  david_out_bias[DAVID_BUCKETS];
static bool     david_allocated = false;

static NNUEFormat g_nnue_format = NNUE_FORMAT_NONE;

static int16_t *ft_weights = NULL;     /* [FEATURE_COUNT][HALF_DIM] int16 */
static int32_t *ft_weights32 = NULL;   /* AXE 2 : même data pré-widened int32 (42 Mo) */
static int16_t  ft_biases[HALF_DIM];

static int32_t  fc0_bias[32];
static int8_t  *fc0_weights = NULL;    /* [32][512] */
static int32_t  fc1_bias[32];
static int8_t   fc1_weights[32 * 32];
static int32_t  fc2_bias;
static int8_t   fc2_weights[32];

static bool nnue_loaded = false;

/* Alias global pour compatibilité (structure obsolète conservée pour le lien) */
static NNUEWeights nnue_weights_storage;
NNUEWeights* const nnue_weights = &nnue_weights_storage;

bool nnue_is_loaded(void) { return nnue_loaded; }

const char *nnue_format_name(void) {
    switch (g_nnue_format) {
        case NNUE_FORMAT_SF13:  return "SF13";
        case NNUE_FORMAT_DAVID: return "DAVID";
        default:                return "NONE";
    }
}

/* ---- Format DAVID : feature index HalfKP (miroir rang 56 pour les Noirs) ---- */
static inline int david_feature_index(Color perspective, Square king_sq,
                                      Color piece_color, Piece piece_type, Square piece_sq) {
    if (piece_type < PAWN || piece_type > QUEEN) return -1;
    if (king_sq >= 64 || piece_sq >= 64) return -1;
    int pt = (int)piece_type - (int)PAWN;      /* 0..4 */
    Square k = (perspective == BLACK) ? (Square)(king_sq ^ 56)  : king_sq;
    Square s = (perspective == BLACK) ? (Square)(piece_sq ^ 56) : piece_sq;
    Color  c = (perspective == BLACK) ? (Color)(piece_color ^ 1) : piece_color;
    return k * 640 + c * 320 + pt * 64 + s;
}

/* ======================================================================
 * LECTURE DU FICHIER (raw little-endian, offsets exacts)
 * ===================================================================== */

static uint32_t rd_u32(const uint8_t *b, size_t *off) {
    uint32_t v = (uint32_t)b[*off] | ((uint32_t)b[*off + 1] << 8) |
                 ((uint32_t)b[*off + 2] << 16) | ((uint32_t)b[*off + 3] << 24);
    *off += 4;
    return v;
}
static int16_t rd_i16(const uint8_t *b, size_t *off) {
    uint16_t v = (uint16_t)b[*off] | ((uint16_t)b[*off + 1] << 8);
    *off += 2;
    return (int16_t)v;
}
static int32_t rd_i32(const uint8_t *b, size_t *off) {
    int32_t v = (int32_t)((uint32_t)b[*off] | ((uint32_t)b[*off + 1] << 8) |
               ((uint32_t)b[*off + 2] << 16) | ((uint32_t)b[*off + 3] << 24));
    *off += 4;
    return v;
}

/* ---- Chargement DAVID (int16 little-endian, AUCUNE en-tête : détection par taille) ---- */
static bool david_load_buf(const uint8_t *buf, size_t fsize) {
    const long v12 = ((long)DAVID_INPUT * DAVID_HALF + DAVID_HALF
                    + (long)DAVID_HALF * DAVID_L2 + DAVID_L2
                    + (long)DAVID_L2 * DAVID_L3 + DAVID_L3 + DAVID_L3 + 1) * 2L;
    const long v14 = v12 + ((long)DAVID_BUCKETS * DAVID_L3 + DAVID_BUCKETS - DAVID_L3 - 1) * 2L;
    if ((long)fsize != v12 && (long)fsize != v12 + 198L && (long)fsize != v14) return false;

    if (!david_allocated) {
        david_ft_weights = (int16_t *)malloc((size_t)DAVID_INPUT * DAVID_HALF * sizeof(int16_t));
        david_l1_weights = (int16_t *)malloc((size_t)DAVID_HALF * DAVID_L2 * sizeof(int16_t));
        david_l2_weights = (int16_t *)malloc((size_t)DAVID_L2 * DAVID_L3 * sizeof(int16_t));
        if (!david_ft_weights || !david_l1_weights || !david_l2_weights) return false;
        david_allocated = true;
    }

    size_t off = 0;
    for (size_t i = 0; i < (size_t)DAVID_INPUT * DAVID_HALF; i++) david_ft_weights[i] = rd_i16(buf, &off);
    for (int i = 0; i < DAVID_HALF; i++) david_ft_bias[i] = rd_i16(buf, &off);
    for (int i = 0; i < DAVID_HALF * DAVID_L2; i++) david_l1_weights[i] = rd_i16(buf, &off);
    for (int i = 0; i < DAVID_L2; i++) david_l1_bias[i] = rd_i16(buf, &off);
    for (int i = 0; i < DAVID_L2 * DAVID_L3; i++) david_l2_weights[i] = rd_i16(buf, &off);
    for (int i = 0; i < DAVID_L3; i++) david_l2_bias[i] = rd_i16(buf, &off);

    if ((long)fsize >= v14) {
        /* v14 : 8 têtes de sortie */
        for (int i = 0; i < DAVID_BUCKETS * DAVID_L3; i++) david_out_weights[i] = rd_i16(buf, &off);
        for (int i = 0; i < DAVID_BUCKETS; i++) david_out_bias[i] = rd_i16(buf, &off);
    } else {
        /* v12/v13 : 1 tête unique répliquée dans les 8 buckets */
        for (int i = 0; i < DAVID_L3; i++) david_out_weights[i] = rd_i16(buf, &off);
        david_out_bias[0] = rd_i16(buf, &off);
        for (int b = 1; b < DAVID_BUCKETS; b++) {
            memcpy(&david_out_weights[b * DAVID_L3], david_out_weights, DAVID_L3 * sizeof(int16_t));
            david_out_bias[b] = david_out_bias[0];
        }
        /* queue WDL v13 (198 o) : ignorée, non utilisée par l'inférence */
    }
    return true;
}

bool nnue_load(const char *path) {
    nnue_init();
    /* 03/10/2026 : tout echec desactive le NNUE. Avant, un fichier introuvable
     * laissait l'ancien reseau actif sans rien dire (ex. reseau SF13 charge au
     * demarrage alors que l'outil croyait tester un candidat). */
    nnue_loaded = false;
    g_nnue_format = NNUE_FORMAT_NONE;

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) { fclose(f); return false; }

    uint8_t *buf = (uint8_t *)malloc((size_t)fsize);
    if (!buf) { fclose(f); return false; }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) { free(buf); fclose(f); return false; }
    fclose(f);

    /* --- 1) SF13 : en-tête + hashs + comptabilité d'octets exacte --- */
    bool sf13_ok = false;
    if ((size_t)fsize >= 16) {
        size_t off = 0;
        uint32_t version = rd_u32(buf, &off);
        uint32_t nhash   = rd_u32(buf, &off);
        uint32_t dlen    = rd_u32(buf, &off);
        if (version == SF_VERSION && nhash == NET_HASH && dlen != 0 &&
            off + dlen <= (size_t)fsize) {
            off += dlen;
            if (rd_u32(buf, &off) == FT_HASH) {
                for (int i = 0; i < HALF_DIM; i++) ft_biases[i] = rd_i16(buf, &off);
                for (size_t i = 0; i < (size_t)HALF_DIM * FEATURE_COUNT; i++) {
                    ft_weights[i] = rd_i16(buf, &off);
                    ft_weights32[i] = ft_weights[i]; /* AXE 2 : pré-widened pour SSE2 */
                }
                if (rd_u32(buf, &off) == LAYERS_HASH) {
                    for (int i = 0; i < 32; i++) fc0_bias[i] = rd_i32(buf, &off);
                    for (int i = 0; i < 32 * 512; i++) fc0_weights[i] = (int8_t)buf[off++];
                    for (int i = 0; i < 32; i++) fc1_bias[i] = rd_i32(buf, &off);
                    for (int i = 0; i < 32 * 32; i++) fc1_weights[i] = (int8_t)buf[off++];
                    fc2_bias = rd_i32(buf, &off);
                    for (int i = 0; i < 32; i++) fc2_weights[i] = (int8_t)buf[off++];
                    if (off == (size_t)fsize) sf13_ok = true;
                }
            }
        }
    }
    if (sf13_ok) {
        g_nnue_format = NNUE_FORMAT_SF13;
        nnue_loaded = true;
        printf("info string NNUE chargé: %s format=SF13 (%ld octets, HalfKP 41024)\n", path, fsize);
        free(buf);
        return true;
    }

    /* --- 2) DAVID : détecté par taille exacte --- */
    if (david_load_buf(buf, (size_t)fsize)) {
        g_nnue_format = NNUE_FORMAT_DAVID;
        nnue_loaded = true;
        printf("info string NNUE chargé: %s format=DAVID (%ld octets, HalfKP 40960)\n", path, fsize);
        free(buf);
        return true;
    }

    /* --- 3) Format inconnu : refus clair, NNUE désactivé (jamais de plantage) --- */
    printf("info string NNUE: format inconnu, refus (%s, %ld octets) — NNUE désactivé\n", path, fsize);
    free(buf);
    return false;
}

/* ======================================================================
 * FEATURES HalfKP(Friend) — formule sf_13 exacte
 * ===================================================================== */

static inline Square orient_sq(Color p, Square s) {
    return (Square)(s ^ (p == BLACK ? 63 : 0));
}

/* Code PS de base : nous -> 1 + 128*(t-1), eux -> 65 + 128*(t-1), t = 1..5 (PAWN..QUEEN) */
static inline int ps_base(Color p, Color piece_color, Piece piece_type) {
    int t = (int)piece_type - (int)PAWN;        /* 0..4 */
    if (piece_color == p) return 1 + 128 * t;
    return 65 + 128 * t;
}

static inline int halfkp_index(Color p, Square ksq_p, Color piece_color, Piece piece_type, Square piece_sq) {
    return PS_END * (int)orient_sq(p, ksq_p) + ps_base(p, piece_color, piece_type) + (int)orient_sq(p, piece_sq);
}

/* ---- Format DAVID : accumulateur + propagation (reprise de 18b4875) ---- */
static void david_compute_side(NNUEAccumulator *acc, const Position *pos, Color side) {
    for (int j = 0; j < DAVID_HALF; j++) acc->values[j] = david_ft_bias[j];
    acc->bucket = nnue_bucket_of(pos);   /* bucket de sortie (format David) */
    if (!pos->piece[side][KING]) return;
    Square ksq = lsb(pos->piece[side][KING]);
    for (Color c = WHITE; c <= BLACK; c++) {
        for (Piece p = PAWN; p <= QUEEN; p++) {
            uint64_t bb = pos->piece[c][p];
            while (bb) {
                Square sq = pop_lsb(&bb);
                int idx = david_feature_index(side, ksq, c, p, sq);
                if (idx < 0) continue;
                const int16_t *col = david_ft_weights + (size_t)DAVID_HALF * idx;
                for (int j = 0; j < DAVID_HALF; j++) acc->values[j] += col[j];
            }
        }
    }
}


static inline void david_acc_add(int32_t *v, int idx) {
    const int16_t *col = david_ft_weights + (size_t)DAVID_HALF * idx;
    for (int j = 0; j < DAVID_HALF; j++) v[j] += col[j];
}
static inline void david_acc_sub(int32_t *v, int idx) {
    const int16_t *col = david_ft_weights + (size_t)DAVID_HALF * idx;
    for (int j = 0; j < DAVID_HALF; j++) v[j] -= col[j];
}
static void david_update_move(NNUEAccumulator next_acc[2], const NNUEAccumulator current_acc[2],
                              const Position *pos, Move move, Piece moved_piece,
                              Piece captured_piece, Color us) {
    Color them = (us == WHITE) ? BLACK : WHITE;
    int bucket = nnue_bucket_of(pos);
    if (moved_piece == KING) {
        /* Roque : la tour bouge aussi ; le plus simple et sûr est de tout recalculer
         * pour notre perspective et, pour l'autre, de traiter roi (non feature) + tour. */
        david_compute_side(&next_acc[us], pos, us);
        if (move.type == KING_CASTLE || move.type == QUEEN_CASTLE ||
            captured_piece != EMPTY) {
            david_compute_side(&next_acc[them], pos, them);
        } else {
            memcpy(&next_acc[them], &current_acc[them], sizeof(NNUEAccumulator));
            next_acc[them].bucket = bucket;
        }
        return;
    }
    Piece final_piece = (move.promo_piece != EMPTY) ? move.promo_piece : moved_piece;
    Square cap_sq = move.to;
    if (move.type == EN_PASSANT) cap_sq = (Square)(move.to + (us == WHITE ? -8 : 8));
    for (int ci = 0; ci < 2; ci++) {
        Color p = (Color)ci;
        Square ksq = lsb(pos->piece[p][KING]);
        memcpy(&next_acc[p], &current_acc[p], sizeof(NNUEAccumulator));
        next_acc[p].bucket = bucket;
        david_acc_sub(next_acc[p].values, david_feature_index(p, ksq, us, moved_piece, move.from));
        if (captured_piece != EMPTY)
            david_acc_sub(next_acc[p].values, david_feature_index(p, ksq, them, captured_piece, cap_sq));
        david_acc_add(next_acc[p].values, david_feature_index(p, ksq, us, final_piece, move.to));
    }
}

/* SCReLU (QA=127) identique au trainer : clamp(x,0,127)^2/127. */
static inline int david_screlu(int x) {
    if (x < 0) x = 0; else if (x > 127) x = 127;
    return (x * x) / 127;
}

/* Propagation DAVID : 256 -> 32 -> 32 -> 1, sortie (output*100)/256. */
static int david_forward(const NNUEAccumulator *acc) {
    if (!david_allocated) return 0;
    int16_t l1_out[DAVID_L2];
    /* Activation calculée une seule fois par entrée (avant : 32 fois), puis produit
     * ligne par ligne (accès mémoire contigus). Mêmes entiers, même résultat. */
    int l0[DAVID_HALF];
    for (int j = 0; j < DAVID_HALF; j++) l0[j] = david_screlu(acc->values[j]);
    int sums[DAVID_L2];
    for (int i = 0; i < DAVID_L2; i++) sums[i] = david_l1_bias[i];
    for (int j = 0; j < DAVID_HALF; j++) {
        const int a = l0[j];
        if (!a) continue;
        const int16_t *w = david_l1_weights + j * DAVID_L2;
        for (int i = 0; i < DAVID_L2; i++) sums[i] += a * w[i];
    }
    for (int i = 0; i < DAVID_L2; i++) l1_out[i] = (int16_t)david_screlu(sums[i] / 64);
    int16_t l2_out[DAVID_L3];
    for (int i = 0; i < DAVID_L3; i++) {
        int sum = david_l2_bias[i];
        for (int j = 0; j < DAVID_L2; j++) sum += l1_out[j] * david_l2_weights[j * DAVID_L3 + i];
        l2_out[i] = (int16_t)david_screlu(sum / 64);
    }
    int bucket = acc->bucket;
    if (bucket < 0) bucket = 0; else if (bucket >= DAVID_BUCKETS) bucket = DAVID_BUCKETS - 1;
    int output = david_out_bias[bucket];
    for (int i = 0; i < DAVID_L3; i++) output += l2_out[i] * david_out_weights[bucket * DAVID_L3 + i];
    return (output * 100) / 256;
}

/* Recalcule l'accumulateur (256 valeurs int32) d'une perspective donnée. */
void nnue_compute_side(NNUEAccumulator *acc, const Position *pos, Color side) {
    if (g_nnue_format == NNUE_FORMAT_DAVID) { david_compute_side(acc, pos, side); return; }
    if (nnue_is_loaded()) {
        for (int j = 0; j < HALF_DIM; j++) acc->values[j] = ft_biases[j];
    } else {
        memset(acc->values, 0, sizeof(int32_t) * HALF_DIM);
        return;
    }

    if (!pos->piece[side][KING]) return; /* pas de roi (position invalide) */
    Square ksq = lsb(pos->piece[side][KING]);

    for (Color c = WHITE; c <= BLACK; c++) {
        for (Piece p = PAWN; p <= QUEEN; p++) {
            uint64_t bb = pos->piece[c][p];
            while (bb) {
                Square sq = pop_lsb(&bb);
                int idx = halfkp_index(side, ksq, c, p, sq);
                const int32_t *col = ft_weights32 + (size_t)HALF_DIM * idx;
#ifdef __AVX2__
                /* AXE 6.1 : AVX2 8-wide — 2× plus large que SSE2 */
                for (int j = 0; j < HALF_DIM; j += 8) {
                    __m256i a = _mm256_loadu_si256((const __m256i *)&acc->values[j]);
                    __m256i w = _mm256_loadu_si256((const __m256i *)&col[j]);
                    _mm256_storeu_si256((__m256i *)&acc->values[j], _mm256_add_epi32(a, w));
                }
#else
                /* SSE2 4-wide (bit-exact) */
                for (int j = 0; j < HALF_DIM; j += 4) {
                    __m128i a = _mm_loadu_si128((const __m128i *)&acc->values[j]);
                    __m128i w = _mm_loadu_si128((const __m128i *)&col[j]);
                    _mm_storeu_si128((__m128i *)&acc->values[j], _mm_add_epi32(a, w));
                }
#endif
            }
        }
    }
}

/* ======================================================================
 * PROPAGATION RÉSEAU (512 -> 32 -> 32 -> 1)
 * ===================================================================== */

static inline uint8_t clip_ft(int32_t v) {
    if (v < 0) return 0;
    if (v > 127) return 127;
    return (uint8_t)v;
}
static inline uint8_t clip_relu(int32_t v) {
    v >>= WS_BITS;                 /* sf_13 : max(0, min(127, in >> 6)) */
    if (v < 0) return 0;
    if (v > 127) return 127;
    return (uint8_t)v;
}

    /* acc[2] : accumulateurs des DEUX perspectives ([blanc], [noir]).
     * stm : camp au trait (sa perspective arrive en moitié 0 du vecteur 512).
     * Retour : score centipawns relatif au camp au trait. */
    int nnue_forward_fast(const NNUEAccumulator acc[2], int stm) {
        if (!nnue_is_loaded()) return 0;
        if (g_nnue_format == NNUE_FORMAT_DAVID) return david_forward(&acc[stm]);

        uint8_t out[512];
        const int order[2] = { stm, 1 - stm };
        const __m128i vzero8  = _mm_setzero_si128();
#ifdef __AVX2__
        const __m256i vclamp_256  = _mm256_set1_epi8(127);
#else
        const __m128i vclamp  = _mm_set1_epi8(127);
#endif
        /* FT clamp 0..127 — packs(packs(int32->int16)) + packus(int16->uint8) + min. */
        for (int k = 0; k < 2; k++) {
            int base = k * HALF_DIM;
            const NNUEAccumulator *a = &acc[order[k]];
#ifdef __AVX2__
            for (int j = 0; j < HALF_DIM; j += 16) {
                __m256i lo = _mm256_loadu_si256((const __m256i *)&a->values[j]);
                __m256i hi = _mm256_loadu_si256((const __m256i *)&a->values[j + 8]);
                __m256i s16 = _mm256_packs_epi32(lo, hi);     /* 16 int16 */
                __m256i u8  = _mm256_packus_epi16(s16, s16);   /* 32 uint8 */
                u8 = _mm256_min_epu8(u8, vclamp_256);          /* clamp 0..127 */
                /* Store lower 16 bytes */
                _mm_storel_epi64((__m128i *)&out[base + j], _mm256_castsi256_si128(u8));
                if (j + 8 < HALF_DIM) {
                    _mm_storel_epi64((__m128i *)&out[base + j + 8], _mm256_extracti128_si256(u8, 1));
                }
            }
#else
            for (int j = 0; j < HALF_DIM; j += 8) {
                __m128i lo = _mm_loadu_si128((const __m128i *)&a->values[j]);
                __m128i hi = _mm_loadu_si128((const __m128i *)&a->values[j + 4]);
                __m128i s16 = _mm_packs_epi32(lo, hi);
                __m128i u8  = _mm_packus_epi16(s16, s16);
                u8 = _mm_min_epu8(u8, vclamp);
                _mm_storel_epi64((__m128i *)&out[base + j], u8);
            }
#endif
        }

    int32_t x[32];
    for (int i = 0; i < 32; i++) {
        const int8_t *row = fc0_weights + i * 512;
        /* AXE 2 : FC0 en _mm_madd_epi16 (4 produits-sommes int32 / instruction).
         * Widen signé des poids int8 : unpack(x,x) + srai(8). Bit-exact :
         * mul-add par paires — même somme, int32 largement suffisant (max ~8.2M).
         * FIX 13h30 : accumulateur à ZÉRO (le splat bias + réduction 4-lanes
         * comptait le bias 4× !). Le bias scalaire est ajouté une fois à la fin. */
        __m128i s = _mm_setzero_si128();
        for (int j = 0; j < 512; j += 8) {
            __m128i w8 = _mm_loadl_epi64((const __m128i *)&row[j]);   /* 8 int8 */
            __m128i o8 = _mm_loadl_epi64((const __m128i *)&out[j]);   /* 8 uint8 */
            __m128i wlo = _mm_srai_epi16(_mm_unpacklo_epi8(w8, w8), 8);
            __m128i whi = _mm_srai_epi16(_mm_unpackhi_epi8(w8, w8), 8);
            __m128i olo = _mm_unpacklo_epi8(o8, vzero8);
            __m128i ohi = _mm_unpackhi_epi8(o8, vzero8);
            s = _mm_add_epi32(s, _mm_madd_epi16(wlo, olo));
            s = _mm_add_epi32(s, _mm_madd_epi16(whi, ohi));
        }
        /* réduction horizontale 4 lanes + bias UNE fois */
        __m128i t1 = _mm_shuffle_epi32(s, _MM_SHUFFLE(0, 0, 3, 2));
        __m128i t2 = _mm_add_epi32(s, t1);
        __m128i t3 = _mm_shuffle_epi32(t2, _MM_SHUFFLE(0, 0, 0, 1));
        __m128i t4 = _mm_add_epi32(t2, t3);
        x[i] = _mm_cvtsi128_si32(t4) + fc0_bias[i];
    }
    uint8_t h1[32];
    for (int i = 0; i < 32; i++) h1[i] = clip_relu(x[i]);

    /* AXE 2 : FC1 vectorisé SSE2 — 32x32 dot products via madd_epi16.
     * Même technique que FC0 : widen int8→int16, uint8→int16, madd. */
    int32_t y[32];
    for (int i = 0; i < 32; i++) {
        const int8_t *row = fc1_weights + i * 32;
        __m128i s = _mm_setzero_si128();
        for (int j = 0; j < 32; j += 8) {
            __m128i w8 = _mm_loadl_epi64((const __m128i *)&row[j]);
            __m128i o8 = _mm_loadl_epi64((const __m128i *)&h1[j]);
            __m128i wlo = _mm_srai_epi16(_mm_unpacklo_epi8(w8, w8), 8);
            __m128i whi = _mm_srai_epi16(_mm_unpackhi_epi8(w8, w8), 8);
            __m128i olo = _mm_unpacklo_epi8(o8, vzero8);
            __m128i ohi = _mm_unpackhi_epi8(o8, vzero8);
            s = _mm_add_epi32(s, _mm_madd_epi16(wlo, olo));
            s = _mm_add_epi32(s, _mm_madd_epi16(whi, ohi));
        }
        /* réduction 4 lanes + bias */
        __m128i t1 = _mm_shuffle_epi32(s, _MM_SHUFFLE(0, 0, 3, 2));
        __m128i t2 = _mm_add_epi32(s, t1);
        __m128i t3 = _mm_shuffle_epi32(t2, _MM_SHUFFLE(0, 0, 0, 1));
        __m128i t4 = _mm_add_epi32(t2, t3);
        y[i] = _mm_cvtsi128_si32(t4) + fc1_bias[i];
    }
    uint8_t h2[32];
    for (int i = 0; i < 32; i++) h2[i] = clip_relu(y[i]);

    /* AXE 2 : FC2 vectorisé SSE2 — dot product 32 int8 × uint8 → int32. */
    __m128i sz = _mm_setzero_si128();
    for (int j = 0; j < 32; j += 8) {
        __m128i w8 = _mm_loadl_epi64((const __m128i *)&fc2_weights[j]);
        __m128i o8 = _mm_loadl_epi64((const __m128i *)&h2[j]);
        __m128i wlo = _mm_srai_epi16(_mm_unpacklo_epi8(w8, w8), 8);
        __m128i whi = _mm_srai_epi16(_mm_unpackhi_epi8(w8, w8), 8);
        __m128i olo = _mm_unpacklo_epi8(o8, vzero8);
        __m128i ohi = _mm_unpackhi_epi8(o8, vzero8);
        sz = _mm_add_epi32(sz, _mm_madd_epi16(wlo, olo));
        sz = _mm_add_epi32(sz, _mm_madd_epi16(whi, ohi));
    }
    __m128i t1 = _mm_shuffle_epi32(sz, _MM_SHUFFLE(0, 0, 3, 2));
    __m128i t2 = _mm_add_epi32(sz, t1);
    __m128i t3 = _mm_shuffle_epi32(t2, _MM_SHUFFLE(0, 0, 0, 1));
    __m128i t4 = _mm_add_epi32(t2, t3);
    int32_t z = _mm_cvtsi128_si32(t4) + fc2_bias;

    int32_t value = z / FV_SCALE;              /* Value interne sf_13 (pion = 208) */
    return (int)((int64_t)value * 100 / PAWN_VALUE_EG);   /* -> centipawns */
}

/* Évaluation complète d'une position (score relatif au camp au trait). */
int nnue_evaluate(const Position *pos) {
    if (!nnue_is_loaded()) return 0;
    if (g_nnue_format == NNUE_FORMAT_DAVID) {
        NNUEAccumulator acc;
        david_compute_side(&acc, pos, pos->side_to_move);
        return david_forward(&acc);
    }
    NNUEAccumulator acc[2];
    nnue_compute_side(&acc[WHITE], pos, WHITE);
    nnue_compute_side(&acc[BLACK], pos, BLACK);
    return nnue_forward_fast(acc, pos->side_to_move);
}

/* ======================================================================
 * MISE A JOUR INCREMENTALE (corrigée : castle, en passant, promotions)
 * ===================================================================== */

/* AXE 2 (30/08 13h15) : SSE2 4-wide, bit-exact (mêmes additions par élément).
 * ft_weights32 = pré-widened au chargement → plus de conversion int16->int32
 * dans la boucle chaude (appelée à chaque nœud de la recherche). */
static void acc_add_col(int32_t *values, int idx) {
    const int32_t *col = ft_weights32 + (size_t)HALF_DIM * idx;
#ifdef __AVX2__
    for (int j = 0; j < HALF_DIM; j += 8) {
        __m256i a = _mm256_loadu_si256((const __m256i *)&values[j]);
        __m256i w = _mm256_loadu_si256((const __m256i *)&col[j]);
        _mm256_storeu_si256((__m256i *)&values[j], _mm256_add_epi32(a, w));
    }
#else
    for (int j = 0; j < HALF_DIM; j += 4) {
        __m128i a = _mm_loadu_si128((const __m128i *)&values[j]);
        __m128i w = _mm_loadu_si128((const __m128i *)&col[j]);
        _mm_storeu_si128((__m128i *)&values[j], _mm_add_epi32(a, w));
    }
#endif
}
static void acc_sub_col(int32_t *values, int idx) {
    const int32_t *col = ft_weights32 + (size_t)HALF_DIM * idx;
#ifdef __AVX2__
    for (int j = 0; j < HALF_DIM; j += 8) {
        __m256i a = _mm256_loadu_si256((const __m256i *)&values[j]);
        __m256i w = _mm256_loadu_si256((const __m256i *)&col[j]);
        _mm256_storeu_si256((__m256i *)&values[j], _mm256_sub_epi32(a, w));
    }
#else
    for (int j = 0; j < HALF_DIM; j += 4) {
        __m128i a = _mm_loadu_si128((const __m128i *)&values[j]);
        __m128i w = _mm_loadu_si128((const __m128i *)&col[j]);
        _mm_storeu_si128((__m128i *)&values[j], _mm_sub_epi32(a, w));
    }
#endif
}

void nnue_update_move(NNUEAccumulator next_acc[2], const NNUEAccumulator current_acc[2],
                      const Position *pos_after_move, Move move, Piece moved_piece,
                      Piece captured_piece, Color side_that_moved) {
    if (g_nnue_format == NNUE_FORMAT_DAVID) {
        /* Format DAVID : mise à jour incrémentale (les rois ne sont pas des features ;
         * si le roi d'une perspective bouge, toutes ses features changent -> recalcul). */
        david_update_move(next_acc, current_acc, pos_after_move, move, moved_piece,
                          captured_piece, side_that_moved);
        return;
    }
    Color us = side_that_moved;
    Color them = (us == WHITE) ? BLACK : WHITE;
    bool castle = (move.type == KING_CASTLE || move.type == QUEEN_CASTLE);

    /* Copie des accumulateurs (base pour les modifs incrémentales). */
    memcpy(&next_acc[WHITE], &current_acc[WHITE], sizeof(NNUEAccumulator));
    memcpy(&next_acc[BLACK], &current_acc[BLACK], sizeof(NNUEAccumulator));

    if (moved_piece == KING) {
        /* Le roi ami a bougé : la perspective du camp qui bouge change de roi de
         * référence -> recalcul complet. Si roque : la tour (de us) bouge aussi,
         * elle est visible par les DEUX perspectives -> recalcul des deux. */
        nnue_compute_side(&next_acc[us], pos_after_move, us);
        if (castle) nnue_compute_side(&next_acc[them], pos_after_move, them);
    } else {
        /* FIX MAJEUR 30/08 13h40 (axe 1 — evalinc a prouvé le drift) :
         * HalfKP(Friend) voit le roi ami + TOUTES les pièces (2 couleurs — cf.
         * nnue_compute_side qui itère c=WHITE..BLACK, et PS_END=641 = codes ami
         * ET ennemi). Donc les DEUX perspectives changent à chaque coup non-roi :
         * la pièce mobile est visible par them (code couleur us), et la pièce
         * capturée (de them) est visible par les deux. L'ancien code original
         * mettait à jour les deux perspectives — c'était CORRECT — mais avec UN
         * bug : la couleur de la pièce capturée dans la perspective them (us au
         * lieu de them, injectant un drift par capture). Le fix intermédiaire de
         * 12h00 avait supprimé TOUTE la maj them (drift conservé, pire) — prouvé
         * par evalinc : roque noir +86, EP +115, promotion -437.
         * Version finale : maj des deux perspectives, couleur capturée corrigée. */
        Square ksq_us = lsb(pos_after_move->piece[us][KING]);
        Square ksq_them = lsb(pos_after_move->piece[them][KING]);
        Piece final_piece = (move.promo_piece != EMPTY) ? move.promo_piece : moved_piece;
        Square cap_sq = move.to;
        if (move.type == EN_PASSANT)
            cap_sq = (Square)(move.to + (us == WHITE ? -8 : 8));

        /* Perspective them : leur roi n'a pas bougé — incrémental.
         * Pièce mobile (de us) : code couleur = us. Pièce capturée (de them) :
         * code couleur = them  <-- LE FIX (l'ancien l.279 passait us). */
        acc_sub_col(next_acc[them].values, halfkp_index(them, ksq_them, us, moved_piece, move.from));
        if (captured_piece != EMPTY)
            acc_sub_col(next_acc[them].values, halfkp_index(them, ksq_them, them, captured_piece, cap_sq));
        acc_add_col(next_acc[them].values, halfkp_index(them, ksq_them, us, final_piece, move.to));

        /* Perspective us : notre roi n'a pas bougé — incrémental. */
        acc_sub_col(next_acc[us].values, halfkp_index(us, ksq_us, us, moved_piece, move.from));
        if (captured_piece != EMPTY)
            acc_sub_col(next_acc[us].values, halfkp_index(us, ksq_us, them, captured_piece, cap_sq));
        acc_add_col(next_acc[us].values, halfkp_index(us, ksq_us, us, final_piece, move.to));
    }
}

/* ======================================================================
 * API COMPATIBILITÉ (stubs / utilitaires)
 * ===================================================================== */

void nnue_init(void) {
    if (ft_weights) return;
    ft_weights = (int16_t *)malloc((size_t)HALF_DIM * FEATURE_COUNT * sizeof(int16_t));
    fc0_weights = (int8_t *)malloc(32 * 512);
    /* AXE 2 : pré-widened int32 pour les additions SSE2 (maj incrémentale chaude) */
    ft_weights32 = (int32_t *)malloc((size_t)HALF_DIM * FEATURE_COUNT * sizeof(int32_t));
    if (!ft_weights || !fc0_weights || !ft_weights32) {
        printf("NNUE SF13: echec allocation (%.1f Mo)\n",
               (double)((size_t)HALF_DIM * FEATURE_COUNT * 6) / 1048576.0);
        exit(1);
    }
}

int nnue_bucket_of(const Position *pos) {
    if (g_nnue_format != NNUE_FORMAT_DAVID) return 0; /* réseau SF13 sans buckets */
    int pc = 0;
    for (Color c = WHITE; c <= BLACK; c++)
        for (Piece p = PAWN; p <= QUEEN; p++) {
            uint64_t bb = pos->piece[c][p];
            while (bb) { pop_lsb(&bb); pc++; }
        }
    return (pc < 1) ? 0 : (pc > 8) ? 7 : pc - 1;   /* min(pc,8)-1 */
}

int nnue_feature_index(Color king_color, Square king_sq, Color piece_color,
                       Piece piece_type, Square piece_sq) {
    return halfkp_index(king_color, king_sq, piece_color, piece_type, piece_sq);
}

bool nnue_save(const char *path) {
    (void)path;
    return false; /* non supporté pour le format SF13 */
}

void nnue_train_generate(const char *output_path, int num_positions) {
    (void)output_path; (void)num_positions;
}

void nnue_update_accumulator(NNUEAccumulator *acc, const Position *pos, Move move) {
    (void)move;
    nnue_compute_side(acc, pos, pos->side_to_move);
}
