"""
Sonde de promotion NNUE — CHAMPION_08030.md règle 2
====================================================
Mesure via le BINAIRE réel (chess_engine_nnue.exe) sur les 3 positions fixes :
  startpos / kiwipete / finale — chaque position vue côté blanc ET côté noir.
Pour chaque réseau (champion puis candidat) : swap temporaire de
books\\nnue_weights.bin avec RESTAURATION GARANTIE (finally).

Sortie : tableau comparatif cp/nœuds + écarts de symétrie |eval_w + eval_b|
Verdict automatique vs champion : symétrie améliorée ? échelle d'éval ?
"""
import subprocess, sys, os, shutil, datetime, json

ROOT = r'E:\ChessEngine'
ENGINE = os.path.join(ROOT, 'chess_engine_nnue.exe')
CHAMPION = os.path.join(ROOT, 'books', 'nnue_weights.bin')
SIZE_OK = (20990658, 20990856, 20990724)

FENS = [
    ("startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
    ("kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"),
    ("finale",   "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"),
]
DEPTH = 6


def flip(fen):
    parts = fen.split()
    parts[1] = 'b' if parts[1] == 'w' else 'w'
    return ' '.join(parts)


def uci_eval(engine, fen, depth=DEPTH, timeout=120):
    """Retourne (cp, nodes) du dernier info du depth demandé."""
    p = subprocess.Popen([engine], cwd=ROOT,
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True,
                         encoding='utf-8', errors='replace', bufsize=1)
    cp, nodes = None, None
    try:
        p.stdin.write('uci\n'); p.stdin.flush()
        # attendre uciok (max ~10 s)
        t0 = datetime.datetime.now()
        while (datetime.datetime.now() - t0).seconds < 10:
            line = p.stdout.readline()
            if not line:
                break
            if 'uciok' in line:
                break
        p.stdin.write('isready\n'); p.stdin.flush()
        p.stdin.write('ucinewgame\n'); p.stdin.flush()
        # Fix 27/08 ter : OwnBook=true par defaut -> sur startpos le moteur
        # repond du book SANS chercher ('info depth 0 score cp 0 string book')
        # => nodes=None legitime qui faisait echouer le critere viability.
        p.stdin.write('setoption name OwnBook value false\n'); p.stdin.flush()
        p.stdin.write(f'position fen {fen}\n'); p.stdin.flush()
        p.stdin.write('isready\n'); p.stdin.flush()
        while True:
            line = p.stdout.readline()
            if not line or 'readyok' in line:
                break
        p.stdin.write(f'go depth {depth}\n'); p.stdin.flush()
        t0 = datetime.datetime.now()
        while (datetime.datetime.now() - t0).seconds < timeout:
            line = p.stdout.readline()
            if not line:
                break
            if line.startswith('info'):
                toks = line.split()
                # Fix 27/08 bis : nodes sur TOUTES lignes info (le score cp 0
                # de startpos sortait sans 'nodes' sur la même ligne -> None
                # -> faux verdict "sonde non viable")
                if 'nodes' in toks:
                    try:
                        nodes = int(toks[toks.index('nodes') + 1])
                    except (ValueError, IndexError):
                        pass
                if 'score cp' in line or 'score mate' in line:
                    try:
                        j = toks.index('score')
                        if toks[j + 1] == 'cp':
                            cp = int(toks[j + 2])
                        elif toks[j + 1] == 'mate':
                            # Fix 27/08 : depuis commit 5256643 le moteur sort
                            # 'score mate N' sur les fins de partie (protocole UCI :
                            # N>0 = le camp au trait mate, N<0 = mat contre).
                            # Sans ce catch, ces positions (ET leurs nodes) étaient
                            # silencieusement exclues -> faux verdict "sonde non viable"
                            # sur sonde_promotion_20260826_221734.json.
                            m = int(toks[j + 2])
                            cp = (32000 - abs(m)) if m > 0 else -(32000 - abs(m))
                    except (ValueError, IndexError):
                        pass
            if line.startswith('bestmove'):
                break
    finally:
        try:
            p.stdin.write('quit\n'); p.stdin.flush()
        except Exception:
            pass
        try:
            p.wait(timeout=5)
        except Exception:
            p.kill()
    return cp, nodes


def probe_net(label, src_bin):
    """Swap src_bin en place, sonde, restaure. Retourne dict résultats."""
    results = {}
    for name, fen in FENS:
        e_w = uci_eval(ENGINE, fen)
        e_b = uci_eval(ENGINE, flip(fen))
        gap = (abs(e_w[0] + e_b[0]) if (e_w[0] is not None and e_b[0] is not None) else None)
        results[name] = {'blanc': e_w, 'noir': e_b, 'gap_symetrie': gap}
        print(f"  [{label}] {name}: blanc={e_w} noir={e_b} |gap|={gap}", flush=True)
    return results


def main():
    cand = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        ROOT, 'books', 'nnue_weights_v14.bin')
    for f in (CHAMPION, cand):
        sz = os.path.getsize(f)
        if sz not in SIZE_OK:
            print(f"[SONDE] REFUS: {f} taille {sz} anormale"); sys.exit(2)
    bak = CHAMPION + f'.bak_sonde_{datetime.datetime.now():%Y%m%d_%H%M%S}'
    shutil.copy2(CHAMPION, bak)
    out = {}
    try:
        print("[SONDE] === CHAMPION ===", flush=True)
        out['champion'] = probe_net('champ', CHAMPION)
        print("[SONDE] === CANDIDAT ===", flush=True)
        shutil.copy2(cand, CHAMPION)
        out['candidat'] = probe_net('v14', CHAMPION)
    finally:
        shutil.copy2(bak, CHAMPION)
        print(f"[SONDE] champion restauré depuis {os.path.basename(bak)}",
              flush=True)

    g_c = sum(v['gap_symetrie'] or 0 for v in out['champion'].values())
    g_n = sum(v['gap_symetrie'] or 0 for v in out['candidat'].values())
    kw_c = out['champion']['kiwipete']['blanc'][0]
    kw_n = out['candidat']['kiwipete']['blanc'][0]
    print("\n===== VERDICT SONDE PROMOTION =====")
    print(f"Symétrie cumulée |gaps| : champion={g_c} candidat={g_n} "
          f"{'AMELIOREE ✅' if g_n < g_c else 'PAS AMELIOREE ❌'}")
    print(f"Échelle kiwipete cp : champion={kw_c} candidat={kw_n}")
    ok_nodes = all((out['candidat'][n]['blanc'][1] or 0) > 1000
                   for n in out['candidat'])
    print(f"Sonde UCI viable (nœuds>1000 depth 6) : {'OUI ✅' if ok_nodes else 'NON ❌'}")
    out['_verdict'] = {'gap_champion': g_c, 'gap_candidat': g_n,
                       'symetrie_amelioree': bool(g_n < g_c),
                       'sonde_viable': bool(ok_nodes)}
    stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    rp = os.path.join(ROOT, 'logs', f'sonde_promotion_{stamp}.json')
    with open(rp, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=2, ensure_ascii=False)
    print(f"[SONDE] rapport -> {rp}")


if __name__ == '__main__':
    main()
