"""build_book.py — Génère un opening book pour HCE depuis les sources disponibles.

Format attendu par le HCE : FEN MOVE SCORE (score = fréquence)
Sources : book_weighted.txt (FEN MOVE weight threshold), book_freq.txt, eco_openings.txt

Usage :
    python build_book.py
"""
import os
from collections import defaultdict

BOOKS_DIR = r"E:\ChessEngine\books"
OUTPUT = os.path.join(BOOKS_DIR, "book_merged.txt")

def convert_weighted():
    """Convertit book_weighted.txt (5 colonnes) en FEN MOVE SCORE (3 col)."""
    path = os.path.join(BOOKS_DIR, "book_weighted.txt")
    if not os.path.exists(path):
        print(f"  {path} introuvable")
        return []
    lines = []
    with open(path, "r", encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) < 4:
                continue
            # Format: FEN... MOVE WEIGHT THRESHOLD
            # On prend le MOVE (avant-dernier token dans weighted)
            # Pour le HCE on veut: FEN MOVE SCORE
            score = int(parts[-2])
            move = parts[-3]
            fen = " ".join(parts[:-3])
            if len(move) >= 4 and all(c in "abcdefgh12345678" for c in move[:4]):
                lines.append(f"{fen} {move} {score}")
    print(f"  book_weighted.txt → {len(lines):,} lignes converties")
    return lines

def convert_freq():
    """Lit book_freq.txt (FEN MOVE COUNT) → identique au format cible."""
    path = os.path.join(BOOKS_DIR, "book_freq.txt")
    if not os.path.exists(path):
        return []
    lines = []
    with open(path, "r", encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            if line:
                lines.append(line)
    print(f"  book_freq.txt → {len(lines):,} lignes")
    return lines

def convert_eco():
    """Lit eco_openings.txt (FEN MOVE SCORE avec commentaires #)."""
    path = os.path.join(BOOKS_DIR, "eco_openings.txt")
    if not os.path.exists(path):
        return []
    lines = []
    with open(path, "r", encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) >= 3:
                score = parts[-1]
                move = parts[-2]
                fen = " ".join(parts[:-2])
                if move and score.isdigit():
                    lines.append(f"{fen} {move} {score}")
    print(f"  eco_openings.txt → {len(lines):,} lignes")
    return lines

def merge_and_dedup(all_lines):
    """Fusionne toutes les sources, déduplique par (FEN, MOVE) en sommant les scores."""
    merged = defaultdict(int)
    for line in all_lines:
        line = line.strip()
        if not line:
            continue
        # Chaque ligne: FEN MOVE SCORE
        parts = line.split()
        if len(parts) < 3:
            continue
        score = int(parts[-1])
        move = parts[-2]
        fen = " ".join(parts[:-2])
        key = (fen, move)
        merged[key] += score

    # Trier par FEN puis score décroissant
    result = sorted(
        [(fen, move, score) for (fen, move), score in merged.items()],
        key=lambda x: (x[0], -x[2])
    )
    return result

def write_book(entries, output_path):
    with open(output_path, "w", encoding="utf-8") as f:
        for fen, move, score in entries:
            f.write(f"{fen} {move} {score}\n")
    print(f"\n✅ Book écrit: {output_path}")
    print(f"   {len(entries):,} entrées ({os.path.getsize(output_path)/1024/1024:.0f} MB)")

def main():
    print("=== Construction du opening book HCE ===\n")
    
    # Sources
    sources = []
    sources.extend(convert_weighted())
    sources.extend(convert_freq())
    sources.extend(convert_eco())
    
    if not sources:
        print("Aucune source trouvée !")
        return
    
    print(f"\nSources brutes: {len(sources):,} lignes")
    
    # Fusion + déduplication
    entries = merge_and_dedup(sources)
    print(f"Après dédup: {len(entries):,} entrées uniques")
    
    # Backup de l'ancien book
    if os.path.exists(OUTPUT):
        bak = OUTPUT + ".bak"
        os.replace(OUTPUT, bak)
        print(f"Backup: {bak}")
    
    write_book(entries, OUTPUT)
    
    print("\n✅ HCE utilisera ce book au prochain démarrage.")

if __name__ == "__main__":
    main()
