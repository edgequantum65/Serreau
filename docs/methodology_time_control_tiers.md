# Méthodologie de Test par Paliers de Temps (Cadence/Rythme)

## Problème

Actuellement, tous les matchs utilisent `movetime 3000` (3s/coup) via `match_vs_hermann_d4_full.py`. Cette cadence :
- Ne correspond à **aucun time control standard** (ni blitz, ni rapid, ni classical)
- Favorise le calcul tactique au détriment de la compréhension stratégique
- Ne permet pas de dire « notre moteur est X Elo à Y cadence »
- Masque les faiblesses positionnelles (pas le temps de réfléchir profondément)
- Avantage les moteurs tactiques purs vs les moteurs stratégiques

## Objectif

Proposer **3 paliers de test** avec des time controls standard, des adversaires adaptés, et une estimation du temps nécessaire, pour pouvoir caractériser le niveau du moteur à chaque cadence.

---

## Scripts Disponibles pour les Matchs

### `match_persistent.py` (RECOMMANDÉ — commit ec37b5b)
- **Mécanisme** : Process UCI persistent (book chargé 1 seule fois)
- **Time control supporté** : `--tc "moves/total+inc"` (ex: `--tc "40/15+0"`)
- **Depth supporté** : `--white-depth N --black-depth N`
- **Limitation** : Les deux côtés reçoivent le **même budget temps** (pas de tracking séparé Blanc/Noir)
- **Usage** : `python tools/match_persistent.py --white chess_engine_nnue.exe --black engines/Hermann/Hermann28_32.exe --tc "40/15+0" --moves 200`

### `match_clean.py` (commit 51d373b)
- **Mécanisme** : Subprocess par coup (lent — 19s de chargement book à chaque fois)
- **Time control supporté** : `--tc "moves/total+inc"` (même format)
- **Usage** : `python tools/match_clean.py --white chess_engine_nnue.exe --black engines/Hermann/Hermann28_32.exe --tc "5+0" --moves 200`

### `match_vs_hermann_d4_full.py` (ACTUEL — à remplacer)
- **Mécanisme** : NNUE 3s/move vs Hermann depth 4, pas de limite de coups
- **Problème** : movetime 3000 fixe, pas de time control standard

---

## Adversaires Disponibles par Niveau Estimé

| Adversaire | Elo Estimé (CCRL 40/40) | UCI ? | Statut |
|---|---|---|---|
| **Stockfish 18 AVX2** | ~3650 | ✅ Oui | Trop fort pour comparaison directe |
| **Arasan 25.4** | ~3584 | ✅ Oui | NNUE cassé (écarté) |
| **Rybka 2.3.2a** | ~3185 | ❌ Non (xboard) | Inutilisable |
| **Spike 1.4** | ~3040 | ❌ Non (xboard) | Inutilisable |
| **Ruffian 1.0.5** | ~2612 | ❌ Non (xboard) | Inutilisable |
| **Hermann 2.8** | ~2510 | ✅ Oui | ✅ Fonctionnel |
| **Stockfish (Elo limité)** | 2000-3000 configurable | ✅ Oui | ✅ Fonctionnel via `UCI_Elo` |
| **Notre ChessEngine (NNUE)** | ~2000-2200 (estimé) | ✅ Oui | ✅ |

**Contrainte majeure** : Seuls Hermann 2.8 et Stockfish (Elo limité) sont des adversaires UCI viables. Les autres (Spike, Ruffian, Rybka) sont xboard et ne peuvent pas être utilisés avec nos scripts Python.

---

## Les 3 Paliers Proposés

### Palier 1 — Blitz (5+0)

| Paramètre | Valeur |
|---|---|
| Time control | `5+0` (5 min par side, 0 sec incrément) |
| Durée estimée par game | ~5-10 min |
| Nombre de games recommandé | 6 (3 Blanc + 3 Noir) |
| Temps total session | ~30-60 min |
| Adversaires | Hermann depth 6, Stockfish Elo 2000-2500 |
| Commande | `python tools/match_persistent.py --white chess_engine_nnue.exe --black engines/Hermann/Hermann28_32.exe --tc "5+0" --moves 200` |
| Ce que ça teste | Vitesse tactique, gestion du temps sous pression, qualité des coups en cadence rapide |

**Interprétation** : À 5+0, un moteur ~2000 Elo doit tenir ~50% contre Hermann depth 6 (~2200 Elo effectif en blitz). Si notre moteur fait 50%+, il est probablement >2200 Elo blitz.

### Palier 2 — Rapid (15+10)

| Paramètre | Valeur |
|---|---|
| Time control | `15+10` (15 min par side, 10 sec incrément) |
| Durée estimée par game | ~20-40 min |
| Nombre de games recommandé | 3 (alterner couleurs) |
| Temps total session | ~60-120 min |
| Adversaires | Hermann depth 8, Stockfish Elo 2200-2600 |
| Commande | `python tools/match_persistent.py --white chess_engine_nnue.exe --black engines/Hermann/Hermann28_32.exe --tc "15+10" --moves 200` |
| Ce que ça teste | Profondeur stratégique, évaluation positionnelle, qualité du jeu de milieu de partie |

**Interprétation** : À 15+10, Hermann depth 8 joue ~2600 Elo effectif. Si notre moteur tient 30%+, c'est un bon signe pour le niveau rapid.

### Palier 3 — Classical (40/15+0)

| Paramètre | Valeur |
|---|---|
| Time control | `40/15+0` (40 coups en 15 min, 0 incrément) |
| Durée estimée par game | ~8-15 min |
| Nombre de games recommandé | 3 |
| Temps total session | ~24-45 min |
| Adversaires | Hermann depth 10, Stockfish Elo 2300-2700 |
| Commande | `python tools/match_persistent.py --white chess_engine_nnue.exe --black engines/Hermann/Hermann28_32.exe --tc "40/15+0" --moves 200` |
| Ce que ça teste | Jeu de haute qualité, profondeur de calcul, fin de partie |

**Interprétation** : Le format 40/15+0 est le plus proche du standard CCRL 40/40 (référence Elo des engines). Hermann depth 10 est ~2700 Elo effectif.

---

## Limitation Importante du Time Control dans `match_persistent.py`

Le script actuel **ne tracke pas le temps restant séparément** pour Blanc et Noir. Il calcule :

```python
remaining_moves = max(tc_moves - movenum // 2, 1)
base_time = total_ms // remaining_moves
wtime = btime = base_time + inc_ms
```

Cela signifie que les deux joueurs reçoivent le **même budget temps à chaque coup**, indépendamment du temps réellement consommé. Ce n'est pas un vrai time control — c'est une approximation.

**Impact** : Pour les tests de cadence, cette approximation est acceptable car :
1. Les deux moteurs sont traités symétriquement
2. Le temps total par game est borné
3. L'incrément (quand présent) compense les variations

**Pour un vrai time control** (tracking séparé), il faudrait modifier `match_persistent.py` pour :
- Mesurer le temps réel de chaque coup
- Soustraire du budget du joueur concerné
- Ajouter l'incrément après chaque coup

---

## Plan d'Exécution Recommandé

### Session 1 — Blitz Benchmark (30 min)
```bash
cd /e/ChessEngine

# Game 1-2: NNUE vs Hermann depth 6 (Blanc + Noir)
python tools/match_persistent.py --white chess_engine_nnue.exe --black engines/Hermann/Hermann28_32.exe --tc "5+0" --moves 200
python tools/match_persistent.py --white engines/Hermann/Hermann28_32.exe --black chess_engine_nnue.exe --tc "5+0" --moves 200

# Game 3-4: NNUE vs Stockfish Elo 2200 (Blanc + Noir)
python tools/match_persistent.py --white chess_engine_nnue.exe --black engines/Stockfish/stockfish.exe --tc "5+0" --moves 200
# (ajouter setoption name UCI_LimitStrength value true + setoption name UCI_Elo value 2200 dans le code)
```

### Session 2 — Rapid Benchmark (30-60 min)
```bash
# Game 1: NNUE vs Hermann depth 8
python tools/match_persistent.py --white chess_engine_nnue.exe --black engines/Hermann/Hermann28_32.exe --tc "15+10" --moves 200

# Game 2: NNUE vs Stockfish Elo 2400
python tools/match_persistent.py --white chess_engine_nnue.exe --black engines/Stockfish/stockfish.exe --tc "15+10" --moves 200
```

### Session 3 — Classical Benchmark (30 min)
```bash
# Game 1-2: NNUE vs Hermann depth 10
python tools/match_persistent.py --white chess_engine_nnue.exe --black engines/Hermann/Hermann28_32.exe --tc "40/15+0" --moves 200
python tools/match_persistent.py --white engines/Hermann/Hermann28_32.exe --black chess_engine_nnue.exe --tc "40/15+0" --moves 200
```

---

## Interprétation des Résultats

### Grille d'Évaluation

| Score vs Hermann depth 6 (blitz 5+0) | Niveau Blitz estimé |
|---|---|
| > 70% | >2400 Elo |
| 50-70% | 2200-2400 Elo |
| 30-50% | 2000-2200 Elo |
| < 30% | <2000 Elo |

| Score vs Hermann depth 8 (rapid 15+10) | Niveau Rapid estimé |
|---|---|
| > 50% | >2500 Elo |
| 30-50% | 2300-2500 Elo |
| 15-30% | 2100-2300 Elo |
| < 15% | <2100 Elo |

| Score vs Hermann depth 10 (classical 40/15) | Niveau Classical estimé |
|---|---|
| > 40% | >2600 Elo |
| 20-40% | 2400-2600 Elo |
| 10-20% | 2200-2400 Elo |
| < 10% | <2200 Elo |

### Rapport Type

```
=== RAPPORT DE CADENCE — 2026-07-28 ===

Blitz (5+0) : 4/6 = 66.7% → ~2300 Elo blitz
Rapid (15+10) : 1.5/3 = 50% → ~2400 Elo rapid
Classical (40/15) : 1/3 = 33.3% → ~2450 Elo classical

Verdict : Le moteur est plus fort en cadence lente (+150 Elo),
ce qui indique une bonne évaluation positionnelle mais
une faiblesse relative en tactique rapide.
```

---

## Recommandations pour Améliorer les Tests

### Court Terme
1. **Modifier `match_persistent.py`** pour supporter `UCI_Elo` sur Stockfish (actuellement, le script n'envoie pas `setoption name UCI_LimitStrength value true` avant le match)
2. **Ajouter un tracking du temps réel** dans `match_persistent.py` pour un vrai time control asymétrique
3. **Créer un script de gauntlet multi-cadence** qui enchaîne les 3 paliers automatiquement

### Moyen Terme
1. **Trouver des adversaires UCI supplémentaires** dans la gamme 2200-2800 Elo (Berserk, Igel, Clover sont gratuits et UCI)
2. **Intégrer cutechess-cli** pour des tournois round-robin avec SPRT (test statistique)
3. **Automatiser le rapport de cadence** avec un script qui compile les résultats

### Long Terme
1. **Benchmark CCRL 40/40** complet (le standard de l'industrie)
2. **Profil Elo par phase de jeu** (ouverture, milieu, finale)
3. **Tests de régression** : après chaque entraînement NNUE, rejouer les mêmes positions pour mesurer le progrès

---

## Annexe : Format Time Control Supporté

Les scripts `match_persistent.py` et `match_clean.py` supportent le format :

```
--tc "moves/total+inc"
```

| Exemple | Signification |
|---|---|
| `--tc "5+0"` | 5 min par side, 0 incrément (Blitz) |
| `--tc "3+2"` | 3 min + 2 sec incrément (Blitz) |
| `--tc "15+10"` | 15 min + 10 sec incrément (Rapid) |
| `--tc "40/15+0"` | 40 coups en 15 min, 0 incrément (Classical court) |
| `--tc "40/60+0"` | 40 coups en 60 min, 0 incrément (Classical standard) |

**Note** : Le format `"moves/total+inc"` avec `moves` > 0 signifie « X coups en Y minutes ». Sans `moves` (ex: `"5+0"`), c'est « Y minutes par side, Z sec incrément ».
