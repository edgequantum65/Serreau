#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
baseline_honnete.py — mesure de base (2026-10-01)
Mesure le niveau reel du moteur classique HCE (chess_engine_hce.exe).

Pourquoi un nouveau script (R004) : les outils existants
(continuous_match_pipeline.py, match_c_vs_sf18.py) imposent une profondeur
fixe, un seul adversaire, pas d'ouvertures, pas de movetime, pas de PGN
complet (raison de fin) ni de statistiques Wilson. Aucun ne convient tel quel.

Contraintes mission :
  - 5 configurations adverses x 20 parties = 100 parties
      Ruffian 1.0.5, Hermann 2.8, Spike 1.4, Stockfish Skill 1, Stockfish Skill 2
  - 10 ouvertures de 4 coups UCI, jouees avec les 2 couleurs
  - 0,1 s/coup (go movetime 100) pour les deux camps, 1 thread, Hash 256
  - PGN par partie dans games/baseline_20261001/ (jamais ecraser)
  - stderr de chaque moteur capture
  - watchdog par partie ; parties interrompues comptees a part
  - score %, ecart Elo, intervalle de Wilson 95 %
N'ecrit QUE dans games/baseline_20261001/ (PGN, stderr, log, summary).
"""
import subprocess, time, os, sys, json, math, threading
import chess, chess.pgn
from datetime import datetime

BASE      = r'E:\ChessEngine'
HCE_EXE   = os.path.join(BASE, 'chess_engine_hce.exe')
ENG_DIR   = os.path.join(BASE, 'engines')
GAMES_DIR = os.path.join(BASE, 'games', 'baseline_20261001')
LOG_FILE  = os.path.join(GAMES_DIR, 'baseline_run.log')
SUMMARY   = os.path.join(GAMES_DIR, 'summary.json')

MOVETIME_MS      = 100
HASH_MB          = 256
MAX_PLIES        = 400
GAME_WATCHDOG_S  = 240
QUERY_TIMEOUT_S  = 30
HCE_NAME         = 'ChessEngine HCE 1.0.0'

OPENINGS = [
    ['e2e4', 'e7e5', 'g1f3', 'b8c6'],
    ['d2d4', 'd7d5', 'c2c4', 'e7e6'],
    ['e2e4', 'c7c5', 'g1f3', 'd7d6'],
    ['d2d4', 'g8f6', 'c2c4', 'g7g6'],
    ['e2e4', 'e7e6', 'd2d4', 'd7d5'],
    ['d2d4', 'g8f6', 'g1f3', 'e7e6'],
    ['e2e4', 'c7c6', 'd2d4', 'd7d5'],
    ['d2d4', 'd7d5', 'g1f3', 'g8f6'],
    ['e2e4', 'e7e5', 'f1c4', 'g8f6'],
    ['d2d4', 'e7e6', 'e2e4', 'd7d5'],
]

OPPONENTS = [
    {'key': 'Ruffian', 'exe': os.path.join(ENG_DIR, 'Ruffian', 'Ruffian_105.exe'),
     'name': 'Ruffian 1.0.5', 'elo': 2500,
     'opts': ['setoption name Hash value %d' % HASH_MB]},
    {'key': 'Hermann', 'exe': os.path.join(ENG_DIR, 'Hermann', 'Hermann28_64.exe'),
     'name': 'Hermann 2.8', 'elo': 2510,
     'opts': ['setoption name Hash value %d' % HASH_MB,
              'setoption name OwnBook value false',
              'setoption name NumberOfProcessors value 1']},
    {'key': 'Spike', 'exe': os.path.join(ENG_DIR, 'Spike', 'Spike1.4.exe'),
     'name': 'Spike 1.4', 'elo': 2700,
     'opts': ['setoption name Hash value %d' % HASH_MB,
              'setoption name OwnBook value false',
              'setoption name CPUs value 1']},
    {'key': 'SF_skill1', 'exe': os.path.join(ENG_DIR, 'Stockfish', 'stockfish-avx2.exe'),
     'name': 'Stockfish 18 (Skill 1)', 'elo': None,
     'opts': ['setoption name Hash value %d' % HASH_MB,
              'setoption name Threads value 1',
              'setoption name Skill Level value 1']},
    {'key': 'SF_skill2', 'exe': os.path.join(ENG_DIR, 'Stockfish', 'stockfish-avx2.exe'),
     'name': 'Stockfish 18 (Skill 2)', 'elo': None,
     'opts': ['setoption name Hash value %d' % HASH_MB,
              'setoption name Threads value 1',
              'setoption name Skill Level value 2']},
]


def log(msg):
    ts = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    line = '[%s] %s' % (ts, msg)
    print(line, flush=True)
    with open(LOG_FILE, 'a', encoding='utf-8') as f:
        f.write(line + '\n')


def read_until(proc, marker, timeout):
    holder = {'lines': [], 'found': False}

    def rd():
        while True:
            line = proc.stdout.readline()
            if not line:
                break
            holder['lines'].append(line)
            if marker in line:
                holder['found'] = True
                break
    t = threading.Thread(target=rd, daemon=True)
    t.start()
    t.join(timeout)
    return holder


def start_engine(exe, opts, stderr_handle):
    p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=stderr_handle, text=True, errors='replace',
                         bufsize=1, cwd=os.path.dirname(exe))
    p.stdin.write('uci\n')
    p.stdin.flush()
    r = read_until(p, 'uciok', 15)
    if not r['found']:
        log('WARN: pas de uciok de %s' % exe)
    for o in opts:
        p.stdin.write(o + '\n')
    p.stdin.write('isready\n')
    p.stdin.flush()
    r = read_until(p, 'readyok', 15)
    if not r['found']:
        log('WARN: pas de readyok de %s' % exe)
    return p


def get_move(proc):
    r = read_until(proc, 'bestmove', QUERY_TIMEOUT_S)
    if not r['found']:
        return None
    for line in r['lines']:
        if line.strip().startswith('bestmove'):
            parts = line.split()
            return parts[1] if len(parts) > 1 else None
    return None


def play_game(opp, opening, hce_is_white, holder):
    """Joue une partie. Remplit holder avec procs/result."""
    board = chess.Board()
    moves = []
    for m in opening:
        board.push(chess.Move.from_uci(m))
        moves.append(m)

    err_hce = open(os.path.join(GAMES_DIR, 'stderr_HCE_%s.txt' % opp['key']),
                   'a', encoding='utf-8', errors='replace')
    err_opp = open(os.path.join(GAMES_DIR, 'stderr_%s.txt' % opp['key']),
                   'a', encoding='utf-8', errors='replace')
    hdr = '\n===== game %s (%s) %s =====\n' % (
        opp['key'], 'HCE_white' if hce_is_white else 'HCE_black',
        datetime.now().isoformat())
    err_hce.write(hdr)
    err_opp.write(hdr)
    err_hce.flush()
    err_opp.flush()

    hce = start_engine(HCE_EXE, [
        'setoption name OwnBook value false',
        'setoption name Hash value %d' % HASH_MB,
        'setoption name UseNNUE value false'], err_hce)
    o = start_engine(opp['exe'], opp['opts'], err_opp)
    holder['procs'] = [hce, o]

    result = None
    reason = None
    anomaly = None

    try:
        while not board.is_game_over(claim_draw=True):
            if len(moves) >= MAX_PLIES:
                result = '1/2-1/2'
                reason = 'adjudicated_draw_move_limit_%d' % MAX_PLIES
                break
            mover_is_white = board.turn == chess.WHITE
            hce_to_move = (mover_is_white == hce_is_white)
            eng = hce if hce_to_move else o
            eng.stdin.write('position startpos moves %s\n' % ' '.join(moves))
            eng.stdin.write('go movetime %d\n' % MOVETIME_MS)
            eng.stdin.flush()
            mv_uci = get_move(eng)

            if mv_uci is None or mv_uci == '0000' or mv_uci == '(none)':
                if board.is_checkmate():
                    result = '0-1' if board.turn == chess.WHITE else '1-0'
                    reason = 'checkmate'
                elif board.is_stalemate():
                    result = '1/2-1/2'
                    reason = 'stalemate'
                else:
                    result = '*'
                    reason = 'engine_no_move'
                    anomaly = 'moteur %s sans coup (stdout vide/0000) position non terminale' % (
                        'HCE' if hce_to_move else opp['key'])
                break
            try:
                mv = chess.Move.from_uci(mv_uci)
            except Exception:
                result = '*'
                reason = 'illegal_move'
                anomaly = 'coup illisible %r par %s' % (
                    mv_uci, 'HCE' if hce_to_move else opp['key'])
                break
            if mv not in board.legal_moves:
                result = '*'
                reason = 'illegal_move'
                anomaly = 'coup illegal %s par %s' % (
                    mv_uci, 'HCE' if hce_to_move else opp['key'])
                break
            board.push(mv)
            moves.append(mv_uci)

        if result is None:
            if board.is_checkmate():
                result = '0-1' if board.turn == chess.WHITE else '1-0'
                reason = 'checkmate'
            elif board.is_stalemate():
                result = '1/2-1/2'
                reason = 'stalemate'
            elif board.is_insufficient_material():
                result = '1/2-1/2'
                reason = 'insufficient_material'
            elif board.is_seventyfive_moves():
                result = '1/2-1/2'
                reason = '75_moves'
            elif board.is_fivefold_repetition():
                result = '1/2-1/2'
                reason = 'fivefold_repetition'
            elif board.is_fifty_moves():
                result = '1/2-1/2'
                reason = '50_moves'
            elif board.can_claim_threefold_repetition():
                result = '1/2-1/2'
                reason = 'threefold_repetition'
            else:
                result = '1/2-1/2'
                reason = 'game_over'
    finally:
        for p in holder['procs']:
            try:
                p.kill()
            except Exception:
                pass
        err_hce.close()
        err_opp.close()

    holder['result'] = (result, reason, anomaly, list(moves), hce_is_white, list(opening))


def play_game_watchdog(opp, opening, hce_is_white):
    holder = {'procs': [], 'result': None, 'abandoned': False}
    lock = threading.Lock()

    def runner():
        try:
            play_game(opp, opening, hce_is_white, holder)
        except Exception as e:
            with lock:
                if not holder['abandoned']:
                    holder['result'] = ('*', 'exception', str(e), [], hce_is_white, list(opening))

    t = threading.Thread(target=runner, daemon=True)
    t.start()
    t.join(GAME_WATCHDOG_S)
    if t.is_alive():
        with lock:
            holder['abandoned'] = True
        log('WATCHDOG: partie %s > %ds -> kill moteurs' % (opp['key'], GAME_WATCHDOG_S))
        for p in holder['procs']:
            try:
                p.kill()
            except Exception:
                pass
        t.join(10)
        return ('*', 'watchdog_timeout', 'partie > %ds' % GAME_WATCHDOG_S, [], hce_is_white, list(opening))
    with lock:
        res = holder['result']
    return res


def write_pgn(path, opp, opening, hce_is_white, result, reason, moves, anomaly):
    game = chess.pgn.Game()
    game.headers['Event'] = 'BASE1 baseline 2026-10-01'
    game.headers['Site'] = 'ChessEngine'
    game.headers['Date'] = time.strftime('%Y.%m.%d')
    game.headers['Round'] = '1'
    game.headers['White'] = HCE_NAME if hce_is_white else opp['name']
    game.headers['Black'] = opp['name'] if hce_is_white else HCE_NAME
    game.headers['Result'] = result if result in ('1-0', '0-1', '1/2-1/2') else '*'
    game.headers['Termination'] = reason or 'unknown'
    game.headers['TimeControl'] = 'movetime 100ms'
    game.headers['Opening'] = ' '.join(opening)
    game.headers['HceColor'] = 'white' if hce_is_white else 'black'
    if anomaly:
        game.headers['Anomaly'] = anomaly.replace('\n', ' ')[:200]
    node = game
    for m in moves:
        node = node.add_main_variation(chess.Move.from_uci(m))
    with open(path, 'w', encoding='utf-8') as f:
        f.write(str(game) + '\n')


def wilson(p, n, z=1.96):
    if n == 0:
        return (0.0, 0.0)
    denom = 1 + z * z / n
    center = (p + z * z / (2 * n)) / denom
    half = (z / denom) * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))
    return (max(0.0, center - half), min(1.0, center + half))


def elo_diff(score):
    if score <= 0.0 or score >= 1.0:
        return None
    return -400.0 * math.log10(1.0 / score - 1.0)


def scan_stats():
    """Relit tous les PGN du dossier et calcule les stats (source de verite)."""
    per_opp = {}
    total = {'games': 0, 'wins': 0, 'draws': 0, 'losses': 0,
             'interrupted': 0, 'games_played': 0}
    interrupted_list = []
    anomalies = []
    for key in [o['key'] for o in OPPONENTS]:
        per_opp[key] = {'games': 0, 'wins': 0, 'draws': 0, 'losses': 0,
                        'interrupted': 0}
    if not os.path.isdir(GAMES_DIR):
        return per_opp, total, interrupted_list, anomalies
    for fn in sorted(os.listdir(GAMES_DIR)):
        if not fn.endswith('.pgn'):
            continue
        path = os.path.join(GAMES_DIR, fn)
        with open(path, encoding='utf-8', errors='replace') as f:
            game = chess.pgn.read_game(f)
        if game is None:
            continue
        key = fn.split('_')[1] if fn.count('_') >= 2 and fn.startswith('base1_') else None
        if key not in per_opp:
            continue
        res = game.headers.get('Result', '*')
        term = game.headers.get('Termination', '')
        hce_color = game.headers.get('HceColor', '')
        anomaly = game.headers.get('Anomaly', '')
        total['games'] += 1
        per_opp[key]['games'] += 1
        if anomaly:
            anomalies.append('%s: %s' % (fn, anomaly))
        if res == '*':
            total['interrupted'] += 1
            per_opp[key]['interrupted'] += 1
            interrupted_list.append('%s (termination=%s)' % (fn, term))
            continue
        total['games_played'] += 1
        hce_white = (hce_color == 'white')
        hce_win = (res == '1-0' and hce_white) or (res == '0-1' and not hce_white)
        hce_loss = (res == '0-1' and hce_white) or (res == '1-0' and not hce_white)
        if hce_win:
            total['wins'] += 1
            per_opp[key]['wins'] += 1
        elif hce_loss:
            total['losses'] += 1
            per_opp[key]['losses'] += 1
        else:
            total['draws'] += 1
            per_opp[key]['draws'] += 1
    return per_opp, total, interrupted_list, anomalies


def opp_elo(key):
    for o in OPPONENTS:
        if o['key'] == key:
            return o['elo']
    return None


def build_summary():
    per_opp, total, interrupted, anomalies = scan_stats()
    out = {'generated': datetime.now().isoformat(), 'per_opponent': {},
           'total': {}, 'interrupted': interrupted, 'anomalies': anomalies}
    for key, s in per_opp.items():
        played = s['games_played']
        if played > 0:
            score = (s['wins'] + 0.5 * s['draws']) / played
            lo, hi = wilson(score, played)
            diff = elo_diff(score)
            elo = opp_elo(key)
            est = None
            est_lo = None
            est_hi = None
            if elo is not None and diff is not None:
                est = elo + diff
                est_lo = elo + (elo_diff(lo) if elo_diff(lo) is not None else -9999)
                est_hi = elo + (elo_diff(hi) if elo_diff(hi) is not None else 9999)
            entry = {
                'games': s['games'], 'played': played,
                'wins': s['wins'], 'draws': s['draws'], 'losses': s['losses'],
                'interrupted': s['interrupted'],
                'score_pct': round(score * 100, 2),
                'wilson_low_pct': round(lo * 100, 2),
                'wilson_high_pct': round(hi * 100, 2),
                'elo_diff': None if diff is None else round(diff, 1),
                'opp_elo': elo,
                'elo_est': None if est is None else round(est, 1),
                'elo_est_low': None if est_lo is None else round(est_lo, 1),
                'elo_est_high': None if est_hi is None else round(est_hi, 1),
            }
        else:
            entry = {'games': s['games'], 'played': 0, 'wins': s['wins'],
                     'draws': s['draws'], 'losses': s['losses'],
                     'interrupted': s['interrupted']}
        out['per_opponent'][key] = entry
    tp = total['games_played']
    if tp > 0:
        score = (total['wins'] + 0.5 * total['draws']) / tp
        lo, hi = wilson(score, tp)
        out['total'] = {
            'games': total['games'], 'played': tp,
            'wins': total['wins'], 'draws': total['draws'],
            'losses': total['losses'], 'interrupted': total['interrupted'],
            'score_pct': round(score * 100, 2),
            'wilson_low_pct': round(lo * 100, 2),
            'wilson_high_pct': round(hi * 100, 2),
            'elo_diff_all': None if elo_diff(score) is None else round(elo_diff(score), 1),
        }
    else:
        out['total'] = {'games': total['games'], 'played': 0,
                        'wins': total['wins'], 'draws': total['draws'],
                        'losses': total['losses'],
                        'interrupted': total['interrupted']}
    with open(SUMMARY, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=2, ensure_ascii=False)
    return out


def main():
    os.makedirs(GAMES_DIR, exist_ok=True)
    log('=== BASE1 baseline_honnete demarre ===')
    log('HCE: %s (sha256 premier 16 = %s)' % (
        HCE_EXE, _sha16(HCE_EXE)))
    log('movetime=%dms Hash=%dMB MAX_PLIES=%d watchdog=%ds' % (
        MOVETIME_MS, HASH_MB, MAX_PLIES, GAME_WATCHDOG_S))

    for opp in OPPONENTS:
        log('--- Adversaire %s (%s, Elo=%s) ---' % (
            opp['key'], opp['name'], opp['elo']))
        games = []
        for i, op in enumerate(OPENINGS):
            games.append((op, i, True))   # HCE blancs
            games.append((op, i, False))  # HCE noirs
        for n, (op, oi, hce_white) in enumerate(games, 1):
            color = 'W' if hce_white else 'B'
            pgn = os.path.join(GAMES_DIR, 'base1_%s_op%02d_%s.pgn' % (opp['key'], oi + 1, color))
            if os.path.exists(pgn):
                log('SKIP (existe deja): %s' % os.path.basename(pgn))
                continue
            t0 = time.time()
            res = play_game_watchdog(opp, op, hce_white)
            result, reason, anomaly, moves, _, _ = res
            write_pgn(pgn, opp, op, hce_white, result, reason, moves, anomaly)
            dt = time.time() - t0
            log('%s %s op%02d HCE_%s -> %s (%s) %d plies %.1fs%s' % (
                opp['key'], 'game%02d' % n, oi + 1,
                'white' if hce_white else 'black',
                result, reason, len(moves), dt,
                (' ANOMALIE: %s' % anomaly) if anomaly else ''))
        summ = build_summary()
        log('APRES %s: %s' % (opp['key'], json.dumps(summ['per_opponent'][opp['key']], ensure_ascii=False)))
        log('TOTAL cumule: %s' % json.dumps(summ['total'], ensure_ascii=False))

    summ = build_summary()
    log('=== BASE1 termine. TOTAL: %s ===' % json.dumps(summ['total'], ensure_ascii=False))
    log('Interrompues: %s' % (summ['interrupted'] if summ['interrupted'] else 'aucune'))
    log('Anomalies: %s' % (summ['anomalies'] if summ['anomalies'] else 'aucune'))


def _sha16(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()[:16]


if __name__ == '__main__':
    main()
