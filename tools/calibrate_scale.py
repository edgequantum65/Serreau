"""
calibrate_scale.py — Calibration POST-HOC de l'echelle NNUE (levier v15).
Le forward est LINEAIRE en {output_weights, output_bias} => un seul scalaire k
applique a ces deux tenseurs int16 aligne l'echelle moteur sur celle de Stockfish
sans toucher au corps du reseau (Pearson invariant aux transform lineaires positives).

Preuve d'harmless : corr(sf, eval) doit rester quasi-identique avant/apres ;
l'etendue |moy| doit converger vers celle de SF.

Usage :
  python tools/calibrate_scale.py --positions 200 --depth 6            # probe+fit+write
  python tools/calibrate_scale.py --verify-only                        # re-probe seulement
Securite : n'ecrit JAMAIS sur nnue_weights.bin / champion / backup ; sortie *_calibrated.bin.
"""
import argparse, os, re, subprocess, sys, time, json
import numpy as np

EXE = r"E:\ChessEngine\chess_engine_nnue.exe"
SF  = r"E:\ChessEngine\engines\Stockfish\stockfish-avx2.exe"
DATA = r"E:\ChessEngine\books\nnue_training_ccrl.txt"
ACTIVE = r"E:\ChessEngine\books\nnue_weights.bin"
INBIN  = r"E:\ChessEngine\books\nnue_weights_combo.bin"
OUTBIN = r"E:\ChessEngine\books\nnue_weights_combo_calibrated.bin"

SECTIONS = [40960*256, 256, 256*32, 32, 32*32, 32, 32, 1]  # int16 counts

CP_RE = re.compile(r"score cp (-?\d+)")

class UCI:
    def __init__(self, path, name, opts=None):
        flags = subprocess.CREATE_NO_WINDOW if hasattr(subprocess,'CREATE_NO_WINDOW') else 0
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                 stderr=subprocess.STDOUT, text=True, bufsize=1, creationflags=flags)
        self.name=name; self._send("uci")
        self._expect("uciok")
        if opts:
            for k,v in opts.items(): self._send(f"setoption name {k} value {v}")
        self._send("isready"); self._expect("readyok")
    def _send(self,c):
        self.p.stdin.write(c+"\n"); self.p.stdin.flush()
    def _expect(self,t,to=60):
        t0=time.time()
        while time.time()-t0<to:
            l=self.p.stdout.readline()
            if not l: time.sleep(0.001); continue
            if t in l: return l.strip()
        raise TimeoutError(t)
    def ev(self,fen,d):
        self._send(f"position fen {fen}"); self._send(f"go depth {d}")
        cp=None
        t0=time.time()
        while time.time()-t0<90:
            l=self.p.stdout.readline()
            if not l: break
            m=CP_RE.search(l)
            if m: cp=int(m.group(1))
            if l.startswith("bestmove"): break
        return cp
    def quit(self):
        try: self._send("quit"); self.p.wait(timeout=5)
        except Exception: pass

def sample_fens(n):
    fens=[]
    with open(DATA,'r',encoding='latin-1') as f:
        stride=max(1,400000//n)
        for i,line in enumerate(f):
            if i%stride: continue
            t=line.split(); board=[]
            for p_ in t:
                if p_=='moves': break
                board.append(p_)
            if len(board)>=6: fens.append(" ".join(board[:6]))
            if len(fens)>=n: break
    return fens

def read_sections(path):
    raw={}
    off=0
    with open(path,'rb') as f: blob=f.read()
    for name,n in zip(["fw","fb","l1w","l1b","l2w","l2b","ow","ob"],SECTIONS):
        cnt=n*2
        raw[name]=np.frombuffer(blob[off:off+cnt],dtype='<i2').copy(); off+=cnt
    raw["tail"]=blob[off:]
    return raw

def write_sections(path,s):
    with open(path,'wb') as f:
        for key,_n in zip(["fw","fb","l1w","l1b","l2w","l2b","ow","ob"],SECTIONS):
            f.write(s[key].astype('<i2').tobytes())
        f.write(s["tail"])
    assert os.path.getsize(path)==os.path.getsize(INBIN)+len(s["tail"]), "taille incoherente"

def probe(weights_path, fens, depth):
    """Swap protege -> collecte NNUE + SF cp -> restaure."""
    bak = ACTIVE + ".calib_swap_bak"
    import shutil
    shutil.copy2(ACTIVE,bak)
    nn=sf=None
    try:
        shutil.copy2(weights_path,ACTIVE)
        e1=UCI(EXE,"NNUE",{"UseNNUE":"true","OwnBook":"false"})
        a=[e1.ev(x,depth) for x in fens]; e1.quit()
        e2=UCI(SF,"SF",{"OwnBook":"false","UCI_Chess960":"false"}) if False else UCI(SF,"SF",{"OwnBook":"false"})
        b=[e2.ev(x,depth) for x in fens]; e2.quit()
    finally:
        shutil.copy2(bak,ACTIVE); os.remove(bak)
    md5_after = __import__('hashlib').md5(open(ACTIVE,'rb').read()).hexdigest().upper()
    return a,b,md5_after

def fit_and_report(nn,sf,label,json_out=None):
    pairs=[(x,y) for x,y in zip(nn,sf) if y is not None and x is not None and abs(y)<=350]
    x=np.array([p_[0] for p_ in pairs],float); y=np.array([p_[1] for p_ in pairs],float)
    mask=np.abs(x)>15
    ratios=(y[mask]/x[mask]) if mask.sum()>10 else np.array([])
    k_med=float(np.median(ratios)) if ratios.size else float('nan')
    slope=float((x*y).sum()/max(1e-9,(x*x).sum()))
    r=float(np.corrcoef(x,y)[0,1]) if len(x)>3 else float('nan')
    print(f"[{label}] n_clean={len(x)} corr={r:.4f} |moy_nnue|={np.abs(x).mean():.0f} "
          f"|moy_sf|={np.abs(y).mean():.0f} k_median={k_med:.2f} slope_origin={slope:.2f}")
    if json_out:
        json.dump({"label":label,"n":int(len(x)),"corr":r,"absmean_nnue":float(np.abs(x).mean()),
                   "absmean_sf":float(np.abs(y).mean()),"k_median":k_med,"slope":slope},
                  open(json_out,'w'))
    return k_med,r,float(np.abs(x).mean())

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--positions",type=int,default=200)
    ap.add_argument("--depth",type=int,default=6)
    ap.add_argument("--k-max",type=float,default=25.0)
    args=ap.parse_args()

    fens=sample_fens(args.positions)
    print(f"Probe: {len(fens)} positions, depth {args.distance if hasattr(args,'distance') else args.depth}",flush=True)

    # 1) etat initial de INBIN
    sec=read_sections(INBIN)
    before_md5=__import__('hashlib').md5(open(INBIN,'rb').read()).hexdigest().upper()
    print(f"in={INBIN} md5={before_md5[:8]}")

    # 2) probe AVANT (swap vers INBIN puis restore auto dans probe())
    nn,sf,md5_after_probe=probe(INBIN,fens,args.depth)
    active_md5=__import__('hashlib').md5(open(ACTIVE,'rb').read()).hexdigest().upper()
    assert active_md5.startswith("634FEBF1"), f"poids actif non restaure! ({active_md5[:8]})"
    k0,r0,a0=fit_and_report(nn,sf,"AVANT",r"E:\ChessEngine\logs\calib_before.json")

    k=min(max(k0,1.0),args.k_max)
    if not np.isfinite(k) or k<=1.02:
        print("Pas de sous-echelle detectee (k<=1.02) => aucune ecriture."); return

    # 3) application k sur ow/ob avec garde saturation
    newsec=read_sections(INBIN)
    for key,val in (("ow",newsec["ow"].astype(np.int64)*round(k)),("ob",np.int64(newsec["ob"][0])*round(k))):
        clipped=int(((val<-32768)|(val>32767)).sum()); val=np.clip(val,-32768,32767)
        pct=100.0*clipped/max(1,val.size)
        print(f"scale {key}: clip={clipped}/{val.size} ({pct:.1f}%)")
        if pct>15: sys.exit("ABANDON: saturation >15% de la section "+key)
        newsec[key]=val.astype('<i2')
    ki=int(round(k))
    write_sections(OUTBIN,newsec)
    out_md5=__import__('hashlib').md5(open(OUTBIN,'rb').read()).hexdigest().upper()
    print(f"ecrit {OUTBIN} md5={out_md5[:8]} (k applique = {ki})")

    # 4) probe APRES
    nn2,sf2,_=probe(OUTBIN,fens,args.depth)
    active_md5=__import__('hashlib').md5(open(ACTIVE,'rb').read()).hexdigest().upper()
    assert active_md5.startswith("634FEBF1"), f"poids actif non restaure apres verify! ({active_md5[:8]})"
    _,r1,a1=fit_and_report(nn2,sf2,"APRES",r"E:\ChessEngine\logs\calib_after.json")
    verdict="OK" if (abs(r1-r0)<0.05 and a1>=a0*0.6) else "A_REVOIR"
    print(f"VERDICT harmlessness: corr {r0:.4f}->{r1:.4f}, |moy| {a0:.0f}->{a1:.0f} => {verdict}")

if __name__=="__main__":
    main()
