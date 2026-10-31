"""pgn_to_fens.py — boucle vivante étape 1 (28/08, demande David).
Le moteur a joué ~231 parties archivées dans games/*.pgn — jamais exploitées.
Chaque position AVANT coup = une position où le NNUE a dû évaluer.
On les extrait toutes (uniques) -> matière première de l'analyse SF pleine
puissance -> extraction des erreurs NNUE -> apprentissage sur ses propres fautes.
Sortie : books/game_fens_all.txt (FENs 6 champs, 1/ligne)
"""
import chess, chess.pgn, os, glob, sys

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')

GAMES = r'E:\ChessEngine\games'
OUT   = r'E:\ChessEngine\books\game_fens_all.txt'

seen = set()
n_games = 0
n_pos = 0
files = sorted(glob.glob(os.path.join(GAMES, 'match_c_vs_sf18_*.pgn')))
with open(OUT, 'w', encoding='utf-8') as out:
    for path in files:
        n_games += 1
        try:
            with open(path, encoding='utf-8', errors='ignore') as f:
                game = chess.pgn.read_game(f)
        except Exception:
            continue
        if game is None:
            continue
        board = game.board()
        for mv in game.mainline_moves():
            fen = board.fen()
            if fen not in seen:
                seen.add(fen)
                out.write(fen + '\n')
                n_pos += 1
            board.push(mv)
        fen = board.fen()
        if fen not in seen:
            seen.add(fen)
            out.write(fen + '\n')
            n_pos += 1

print(f'{n_games} parties -> {n_pos} positions uniques -> {OUT}')
