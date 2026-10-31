#!/usr/bin/env python3
"""quick_analysis.py — Test rapide NNUE vs Stockfish.
Échantillonne N positions, évalue avec NNUE puis Stockfish, calcule corrélation.
Usage : python tools/quick_analysis.py [--pos 200] [--depth 2]
"""
import argparse, math, os, random, re, subprocess, sys

SF_PATH = r"E:\ChessEngine\engines\Stockfish\stockfish.exe"
NNUE_PATH = r"chess_engine_nnue.exe"
DATASET = r"books\nnue_training_v13_final.txt"

SCORE_RE = re.compile(r"score cp (-?\d+)")

def eval_position(exe, fen, options):
    """Évalue une position, retourne le score cp ou None."""
    opts = "\n".join(f"setoption name {k} value {v}" for k, v in options.items())
    cmd = f"uci\n{opts}\nisready\nposition fen {fen}\ngo depth {DEPTH}\nquit\n"
    try:
        r = subprocess.run([exe], input=cmd, capture_output=True, text=True, timeout=10)
    except:
        return None
    for line in r.stdout.split("\n"):
        m = SCORE_RE.search(line)
        if m:
            return int(m.group(1))
    return None

def main():
    global DEPTH
    parser = argparse.ArgumentParser()
    parser.add_argument("--pos", type=int, default=200)
    parser.add_argument("--depth", type=int, default=2)
    args = parser.parse_args()
    DEPTH = args.depth

    print("=" * 60)
    print(f"  QUICK ANALYSIS — NNUE vs Stockfish")
    print(f"  {args.pos} positions, depth {DEPTH}")
    print("=" * 60)

    # Échantillonner
    with open(DATASET, "r") as f:
        lines = f.readlines()
    random.seed(42)
    sample = random.sample(lines, min(args.pos, len(lines)))
    print(f"  Dataset: {len(lines)} positions")
    print(f"  Échantillon: {len(sample)} positions\n")

    ours_scores = []
    sf_scores = []

    for i, line in enumerate(sample):
        fen = " ".join(line.split()[:-1])

        our = eval_position(NNUE_PATH, fen, {"UseNNUE": "true", "OwnBook": "false"})
        sf = eval_position(SF_PATH, fen, {})

        if our is not None and sf is not None:
            ours_scores.append(our)
            sf_scores.append(sf)

        if (i + 1) % 50 == 0:
            print(f"  {i+1}/{len(sample)} positions...", flush=True)

    n = len(ours_scores)
    if n < 10:
        print(f"\n  ❌ Seulement {n} positions valides (<10). Pas significatif.")
        return

    # Corrélation
    mean_o = sum(ours_scores) / n
    mean_s = sum(sf_scores) / n
    cov = sum((ours_scores[i] - mean_o) * (sf_scores[i] - mean_s) for i in range(n))
    var_o = sum((x - mean_o) ** 2 for x in ours_scores)
    var_s = sum((x - mean_s) ** 2 for x in sf_scores)
    denom = math.sqrt(var_o * var_s)
    corr = cov / denom if denom > 0 else 0

    rmse_val = math.sqrt(sum((ours_scores[i] - sf_scores[i])**2 for i in range(n)) / n)

    # Qui est proche
    nnue_closer = sum(1 for o, s in zip(ours_scores, sf_scores)
                      if abs(o - s) < abs(0 - s))

    print("\n" + "=" * 60)
    print(f"  RÉSULTATS (n={n})")
    print("=" * 60)
    print(f"  Corrélation NNUE ↔ Stockfish : {corr:.4f}")
    print(f"  RMSE : {rmse_val:.1f} cp")
    print(f"  Score moyen NNUE : {mean_o:.1f} cp")
    print(f"  Score moyen SF   : {mean_s:.1f} cp")
    print(f"  Std dev NNUE     : {math.sqrt(var_o/(n-1)):.1f} cp")
    print(f"  Std dev SF       : {math.sqrt(var_s/(n-1)):.1f} cp")
    print(f"  NNUE plus proche que neutre : {100*nnue_closer/n:.1f}%")
    print("=" * 60)

    # Qualification simple
    if corr >= 0.7:
        qual = "✅ FORTE — bon poids"
    elif corr >= 0.5:
        qual = "🟡 MODÉRÉE — utilisable mais sous-entraîné"
    elif corr >= 0.3:
        qual = "⚠️ FAIBLE — réseau pas encore calibré"
    else:
        qual = "❌ TRÈS FAIBLE — pas d'apprentissage significatif"
    print(f"  Verdict : {qual}")
    print("=" * 60)

if __name__ == "__main__":
    main()
