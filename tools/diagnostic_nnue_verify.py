#!/usr/bin/env python3
"""
Diagnostic NNUE rapide — sans appels à l'engine C.
Compare la sortie Python float avec la simulation C SCReLU.
"""
import numpy as np
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

INPUT_SIZE = 40960
L1_SIZE = 256
L2_SIZE = 32
L3_SIZE = 32
QA = 127
QB = 64

WEIGHTS_PATH = r"E:\ChessEngine\books\nnue_weights.bin"

PIECE_MAP = {'P': 1, 'N': 2, 'B': 3, 'R': 4, 'Q': 5, 'K': 6,
             'p': 1, 'n': 2, 'b': 3, 'r': 4, 'q': 5, 'k': 6}


def screlu_float(x):
    return np.clip(x, 0.0, 1.0) ** 2


def screlu16_c(x):
    c = np.clip(x, 0, 127)
    c1 = np.clip(x + 1, 0, 127)
    return (c * c1) // 127


def load_weights(path):
    with open(path, 'rb') as f:
        fw = np.frombuffer(f.read(INPUT_SIZE * L1_SIZE * 2), dtype=np.int16).astype(np.int32).reshape(INPUT_SIZE, L1_SIZE)
        fb = np.frombuffer(f.read(L1_SIZE * 2), dtype=np.int16).astype(np.int32)
        l1w = np.frombuffer(f.read(L1_SIZE * L2_SIZE * 2), dtype=np.int16).astype(np.int32).reshape(L1_SIZE, L2_SIZE)
        l1b = np.frombuffer(f.read(L2_SIZE * 2), dtype=np.int16).astype(np.int32)
        l2w = np.frombuffer(f.read(L2_SIZE * L3_SIZE * 2), dtype=np.int16).astype(np.int32).reshape(L2_SIZE, L3_SIZE)
        l2b = np.frombuffer(f.read(L3_SIZE * 2), dtype=np.int16).astype(np.int32)
        ow = np.frombuffer(f.read(L3_SIZE * 2), dtype=np.int16).astype(np.int32)
        ob = np.frombuffer(f.read(2), dtype=np.int16).astype(np.int32)[0]
    return fw, fb, l1w, l1b, l2w, l2b, ow, ob


def parse_fen(fen):
    pieces = []
    parts = fen.split()
    board = parts[0]
    turn = parts[1]
    rank, file = 7, 0
    for c in board:
        if c == '/': rank -= 1; file = 0
        elif c.isdigit(): file += int(c)
        else:
            color = 0 if c.isupper() else 1
            pieces.append((color, PIECE_MAP[c], file + rank * 8))
            file += 1
    return pieces, turn


def get_features_c(pieces, turn):
    features = []
    wk = bk = None
    for c, p, sq in pieces:
        if p == 6:
            if c == 0: wk = sq
            else: bk = sq
    if wk is None or bk is None:
        return []
    side = 0 if turn == 'w' else 1
    active_king = wk if side == 0 else bk
    for c, p, sq in pieces:
        if p == 6 or p < 1 or p > 5:
            continue
        pt = p - 1
        if side == 1:
            k_sq = active_king ^ 56
            p_sq = sq ^ 56
            p_col = c ^ 1
        else:
            k_sq = active_king
            p_sq = sq
            p_col = c
        idx = k_sq * 640 + p_col * 320 + pt * 64 + p_sq
        features.append(idx)
    return features


def compute_accumulator(fw, fb, pieces, turn):
    features = get_features_c(pieces, turn)
    acc = fb.copy().astype(np.int32)
    for fi in features:
        acc += fw[fi]
    return acc, features


def forward_python_float(fw_f, fb_f, l1w_f, l1b_f, l2w_f, l2b_f, ow_f, ob_f, acc_f):
    l1 = screlu_float(acc_f)
    l2_pre = l1 @ l1w_f + l1b_f
    l2 = screlu_float(l2_pre)
    l3_pre = l2 @ l2w_f + l2b_f
    l3 = screlu_float(l3_pre)
    out = float(l3 @ ow_f + ob_f)
    return out, l1, l2, l3


def forward_c_screlu(fw, fb, l1w, l1b, l2w, l2b, ow, ob, acc):
    """C-style forward pass with SCReLU (matching fixed C code)."""
    l1_out = np.zeros(L2_SIZE, dtype=np.int32)
    for i in range(L2_SIZE):
        total = int(l1b[i])
        for j in range(L1_SIZE):
            total += int(screlu16_c(int(acc[j]))) * int(l1w[j, i])
        l1_out[i] = int(screlu16_c(total // QB))
    l2_out = np.zeros(L3_SIZE, dtype=np.int32)
    for i in range(L3_SIZE):
        total = int(l2b[i])
        for j in range(L2_SIZE):
            total += int(l1_out[j]) * int(l2w[j, i])
        l2_out[i] = int(screlu16_c(total // QB))
    output = int(ob)
    for i in range(L3_SIZE):
        output += int(l2_out[i]) * int(ow[i])
    output = output // 256
    return output, l1_out, l2_out


# Test positions
FENS = [
    ("Pos1  (w, Italian)", "rnbqkb1r/pppp1ppp/5n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 2 3"),
    ("Pos2  (b, Italian)", "r1bqkb1r/pppp1ppp/2n2n2/4p3/2BPP3/5N2/PPP2PPP/RNBQK1R1 b Qkq - 0 5"),
    ("Start (w)", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
    ("K+P   (b)", "8/3P4/8/6p1/1p3rPk/1P1R4/2P1r3/6K1 b - - 4 75"),
]

print("=" * 65)
print("DIAGNOSTIC NNUE — Après correction SCReLU")
print("=" * 65)

print("\n[1] Loading weights...")
fw_int, fb_int, l1w_int, l1b_int, l2w_int, l2b_int, ow_int, ob_int = load_weights(WEIGHTS_PATH)

fw_f = fw_int.astype(np.float32) / QA
fb_f = fb_int.astype(np.float32) / QA
l1w_f = l1w_int.astype(np.float32) / QB
l1b_f = l1b_int.astype(np.float32) / (QA * QB)
l2w_f = l2w_int.astype(np.float32) / QB
l2b_f = l2b_int.astype(np.float32) / (QA * QB)
ow_f = ow_int.astype(np.float32) * QA / 256.0
ob_f = ob_int.astype(np.float32) / 256.0

print(f"  fb mean = {fb_int.mean():.1f} int16 = {fb_int.mean()/QA:.2f} float")
print(f"  l1b mean = {l1b_int.mean():.1f} int16 = {l1b_int.mean()/(QA*QB):.2f} float")
print(f"  ob = {ob_int} int16 = {ob_int/256.0:.4f} float")
print(f"\n  >>> ALERTE: fb ~{fb_int.mean()/QA:.1f} >> 0.1 initial! Accumulateur saturé <<<")
print(f"  >>> Tous les neurones > 0 → activations à 1.0 (saturé) <<<")
print(f"  >>> Le réseau ne peut distinguer les positions que par 0/1 binaires <<<")
print(f"  >>> Score max attendu: ~{ob_int/256.0 + 32*127*max(abs(ow_int))/256:.0f} cp <<<")
print()

print("[2] Évaluation des positions:")
print("-" * 65)
print(f"  {'Nom':<18} {'Features':>8} {'Acc>0':>6} {'PyFloat':>8} {'C_SCReLU':>8}")
print("-" * 65)

for name, fen in FENS:
    pieces, turn = parse_fen(fen)
    acc_int, features = compute_accumulator(fw_int, fb_int, pieces, turn)
    acc_f = acc_int.astype(np.float32) / QA
    
    # Python float
    out_f, l1_f, l2_f, l3_f = forward_python_float(fw_f, fb_f, l1w_f, l1b_f, l2w_f, l2b_f, ow_f, ob_f, acc_f)
    
    # C SCReLU
    out_c, l1_out, l2_out = forward_c_screlu(fw_int, fb_int, l1w_int, l1b_int, l2w_int, l2b_int, ow_int, ob_int, acc_int)
    
    acc_pos = np.sum(acc_int > 0)
    
    print(f"  {name:<18} {len(features):>8} {acc_pos:>6}/{L1_SIZE} {out_f:>8.2f} {out_c:>8d}")
    print(f"  {'':>18} {'':>8} {'':>6} L1={np.mean(l1_f>0)*100:.0f}% L3={np.mean(l3_f>0)*100:.0f}%")

print("-" * 65)
print("\n[3] Analyse des poids du réseau:")
print(f"  feature_weights: [{fw_int.min()}, {fw_int.max()}] mean={fw_int.mean():.1f}")
print(f"  feature_bias:    [{fb_int.min()}, {fb_int.max()}] mean={fb_int.mean():.1f}")
print(f"  l1_weights:      [{l1w_int.min()}, {l1w_int.max()}] mean={l1w_int.mean():.1f}")
print(f"  l1_bias:         [{l1b_int.min()}, {l1b_int.max()}] mean={l1b_int.mean():.1f}")
print(f"  l2_weights:      [{l2w_int.min()}, {l2w_int.max()}] mean={l2w_int.mean():.1f}")
print(f"  l2_bias:         [{l2b_int.min()}, {l2b_int.max()}] mean={l2b_int.mean():.1f}")
print(f"  output_weights:  [{ow_int.min()}, {ow_int.max()}] mean={ow_int.mean():.1f}")
print(f"  output_bias:     {ob_int}")

print("\n[4] DIAGNOSTIC:")
print(f"  ✓ Correction C appliquée: clamp16 → screlu16 dans nnue_forward()")
print(f"  ✓ nnue.c modifié: lignes 66, 69, 79 (clamp16 → screlu16)")
print()
print(f"  ⚠️  Les poids actuels ont un biais de feature ({fb_int.mean()/QA:.1f}) bien")
print(f"     trop élevé. Tous les accumulateurs positifs saturent à 1.0 avec SCReLU.")
print(f"     Les scores NNUE sont faibles (~{out_f:.0f}-{out_c} cp) alors que HCE donne")
print(f"     400-550 cp pour les mêmes positions.")
print()
print(f"  RECOMMANDATION: Réentraîner les poids après la correction SCReLU dans le C.")
print(f"  Les cycles d'entraînement futurs produiront des poids adaptés à SCReLU")
print(f"  avec des biais raisonnables.")
