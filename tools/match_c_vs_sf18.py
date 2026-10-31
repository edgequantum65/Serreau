#!/usr/bin/env python
"""Match C engine (60M weights) vs Stockfish 18 bridled (Skill Level 5)
C engine depth 8, Stockfish Skill 5 depth 6. 1 match, no move limit.
"""
import subprocess, time, os, sys

C_ENGINE = r'E:\ChessEngine\chess_engine_nnue.exe'
SF18     = r'E:\ChessEngine\engines\Stockfish\stockfish-avx2.exe'

LABEL       = sys.argv[1] if len(sys.argv) > 1 else "C-Engine(60M)"
SKILL_LEVEL = sys.argv[2] if len(sys.argv) > 2 else "5"
C_DEPTH     = sys.argv[3] if len(sys.argv) > 3 else "8"
SF_DEPTH    = sys.argv[4] if len(sys.argv) > 4 else "6"

def start_engine(exe, cwd=None, extra_cmds=None):
    """Start a UCI engine, send initialisation commands."""
    p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True,
                         cwd=cwd or os.path.dirname(exe))
    p.stdin.write('uci\n'); p.stdin.flush()
    for _ in range(100):
        if 'uciok' in p.stdout.readline(): break
    if extra_cmds:
        for cmd in extra_cmds:
            p.stdin.write(cmd + '\n'); p.stdin.flush()
    p.stdin.write('isready\n'); p.stdin.flush()
    for _ in range(100):
        if 'readyok' in p.stdout.readline(): break
    return p

def start_c():
    """C engine — disable own book for fair match."""
    return start_engine(C_ENGINE, extra_cmds=[
        'setoption name OwnBook value false',
        'setoption name Hash value 256',
    ])

def start_sf18(skill_level):
    """Stockfish 18 AVX2 bridled to skill_level."""
    return start_engine(SF18, extra_cmds=[
        f'setoption name Skill Level value {skill_level}',
        'setoption name Hash value 256',
    ])

# --- Match ---
c_eng = start_c()
sf    = start_sf18(SKILL_LEVEL)
print(f'Match: {LABEL} vs SF18(Skill={SKILL_LEVEL}) — C d{C_DEPTH}, SF d{SF_DEPTH}')
print(f'C engine: {C_ENGINE}')
print(f'Stockfish: {SF18}')
print('=' * 60)

moves = []; t0 = time.time()
result = None  # '1-0', '0-1', '1/2-1/2'

for i in range(500):  # 500 moves max (generous)
    is_c = (i % 2 == 0)
    side = 'C' if is_c else 'S'
    eng = c_eng if is_c else sf
    depth = C_DEPTH if is_c else SF_DEPTH

    eng.stdin.write(f'position startpos moves {" ".join(moves)}\n')
    eng.stdin.write(f'go depth {depth}\n')
    eng.stdin.flush()

    move = None
    info_lines = []
    for _ in range(5000):
        line = eng.stdout.readline()
        if not line:
            break
        info_lines.append(line.strip())
        if 'bestmove' in line:
            parts = line.split()
            move = parts[1] if len(parts) > 1 else None
            break

    if not move or move == '0000' or move == '(none)':
        # Check for checkmate/stalemate in info
        reason = move or 'none'
        print(f'{i+1}.{side}: RESIGN ({reason})')
        # Determine result: if C resigned, SF wins; if SF resigned, C wins
        if is_c:
            result = '0-1'
        else:
            result = '1-0'
        break

    moves.append(move)

    # Print every move with some info
    score_info = ''
    for line in info_lines:
        if 'score' in line and 'cp' in line:
            parts = line.split()
            for j, p in enumerate(parts):
                if p == 'score' and j+2 < len(parts) and parts[j+1] == 'cp':
                    score_info = f' cp={parts[j+2]}'
                    break
            if score_info:
                break
        if 'score' in line and 'mate' in line:
            parts = line.split()
            for j, p in enumerate(parts):
                if p == 'score' and j+2 < len(parts) and parts[j+1] == 'mate':
                    score_info = f' mate={parts[j+2]}'
                    break
            if score_info:
                break

    print(f'{i+1:3d}.{side}: {move:6s}{score_info}')

t = time.time() - t0

# If we exhausted moves without a result, determine from last position
if result is None:
    # Check if last move was checkmate by looking at the engine output
    # We'll do a quick analysis
    print(f'\nMatch ended after {len(moves)} moves (move limit)')
    result = 'unknown'

print('=' * 60)
print(f'Résultat: {len(moves)} coups, {int(t)}s ({t/60:.1f} min)')
print(f'Gagnant: {result}')
if result == '1-0':
    print('Vainqueur: C Engine (Blancs)')
elif result == '0-1':
    print('Vainqueur: Stockfish 18 (Noirs)')
elif result == '1/2-1/2':
    print('Match nul')
else:
    print('Résultat indéterminé')

# Save PGN
pgn_path = os.path.join(os.path.dirname(C_ENGINE), 'games', f'match_c_vs_sf18_{int(t0)}.pgn')
os.makedirs(os.path.dirname(pgn_path), exist_ok=True)
with open(pgn_path, 'w') as f:
    f.write(f'[Event "C Engine (60M) vs Stockfish 18"]\n')
    f.write(f'[Site "ChessEngine"]\n')
    f.write(f'[Date "{time.strftime("%Y.%m.%d")}"]\n')
    f.write(f'[Round "1"]\n')
    f.write(f'[White "C Engine (60M)"]\n')
    f.write(f'[Black "Stockfish 18 (Skill {SKILL_LEVEL})"]\n')
    f.write(f'[Result "{result}"]\n')
    f.write(f'[WhiteDepth "{C_DEPTH}"]\n')
    f.write(f'[BlackDepth "{SF_DEPTH}"]\n')
    f.write(f'[PlyCount "{len(moves)}"]\n')
    f.write(f'\n')
    for j, m in enumerate(moves):
        if j % 2 == 0:
            f.write(f'{j//2+1}. {m}')
        else:
            f.write(f' {m} ')
    f.write(f' {result}\n')

print(f'PGN sauvegardé: {pgn_path}')

c_eng.terminate()
sf.terminate()
