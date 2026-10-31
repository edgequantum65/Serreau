# Algorithmes d'Inspiration Quantique pour Moteur d'Échecs

> Document pratique — 3 pistes concrètes, pas de philosophie.
> Projet : ChessEngine (NNUE, bientôt Rust). Objectif : startup Edge AI embarquée.

---

## Table des matières

1. [Piste 1 — QUBO pour l'optimisation du Move Ordering (Champion vs Challenger)](#piste-1--qubo-pour-loptimisation-du-move-ordering-champion-vs-challenger)
2. [Piste 2 — Effet Tunnel Quantique pour la Recherche (Simulated Quantum Annealing)](#piste-2--effet-tunnel-quantique-pour-la-recherche-simulated-quantum-annealing)
3. [Piste 3 — QUBO pour l'Évaluation de Position](#piste-3--qubo-pour-lévaluation-de-position)
4. [Recommandation : par où commencer](#recommandation--par-où-commencer)

---

## Piste 1 — QUBO pour l'optimisation du Move Ordering (Champion vs Challenger)

### Concept

Formuler le problème d'optimisation du **move ordering** (l'ordre dans lequel les coups sont explorés dans alpha-beta) comme un problème QUBO. L'idée : chaque coup candidat est une variable binaire `x_i ∈ {0,1}` indiquant s'il doit être priorisé. La fonction objectif QUBO pénalise les mauvais ordres et récompense les coups qui produisent des cutoffs rapides.

### Formulation QUBO

Soit `n` coups légaux dans une position. On définit :

- `x_i = 1` si le coup `i` est dans le top-k de l'ordre de recherche, `0` sinon
- `Q_ii` = coefficient linéaire = score d'urgence du coup (gain matériel attendu, check, etc.)
- `Q_ij` = coefficient quadratique = pénalité si deux coups de même type sont classés ensemble (redondance)

**Fonction objectif :**

```
min f(x) = x^T Q x
         = Σ_i Q_ii x_i + Σ_i Σ_{j>i} Q_ij x_i x_j
```

Avec contrainte de cardinalité (exactement `k` coups sélectionnés) encodée comme pénalité :

```
P · (Σ_i x_i - k)²
```

### Références

| Référence | Lien |
|-----------|------|
| **QUBO Tutorial (Glover et al., 2019)** — Article fondateur sur la formulation QUBO, 112 problèmes répertoriés | [arXiv:1811.11538](https://arxiv.org/abs/1811.11538) |
| **List of QUBO Formulations** — 112 problèmes avec formulations QUBO complètes | [blog.xa0.de](https://blog.xa0.de/post/List-of-QUBO-formulations) |
| **pasqal-io/qubo-solver** — Bibliothèque Python QUBO avec recuit simulé, tabu-search, algorithmes quantiques | [GitHub](https://github.com/pasqal-io/qubo-solver) |
| **Five Starter Problems (CMU, 2024)** — Tutoriel QUBO avec notebooks Jupyter | [arXiv:2401.08989](https://arxiv.org/html/2401.08989v1) |

### Complexité

- **Nombre de variables** : `n` = nombre de coups légaux (max ~40 en position complexe)
- **Taille de la matrice Q** : `n × n` (1600 entrées max — trivial)
- **Solveur classique** : recuit simulé standard, convergence en `O(k·n²)` par itération
- **Solveur quantique** : compatible D-Wave (5000+ qubits) si on veut hardware réel

### Temps estimé pour un POC

| Phase | Durée |
|-------|-------|
| Implémentation du formateur QUBO en Python | 1-2 jours |
| Intégration avec le moteur (export des coups + scores) | 2-3 jours |
| Benchmark : QUBO vs move ordering classique (MVV-LVA, killers) | 2 jours |
| **Total POC** | **~1 semaine** |

### Code existant

```python
# Exemple d'utilisation de pasqal-io/qubo-solver
from qubosolver.annealing import SimulatedAnnealing
import numpy as np

# Matrice Q (triangulaire supérieure)
Q = np.array([
    [10, -2,  0,  1],
    [ 0,  8, -1,  0],
    [ 0,  0,  5, -3],
    [ 0,  0,  0,  7]
])

solver = SimulatedAnnealing()
result = solver.solve(Q, num_reads=100)
print(f"Meilleur vecteur x: {result.x}")
print(f"Énergie: {result.energy}")
```

---

## Piste 2 — Effet Tunnel Quantique pour la Recherche (Simulated Quantum Annealing)

### Concept

L'**effet tunnel quantique** permet à une particule de traverser une barrière d'énergie au lieu de devoir la franchir par le haut. Dans le contexte d'un moteur d'échecs, cela se traduit par un algorithme de recherche capable de "traverser" des positions défavorables temporaires (barrières locales) pour trouver des coups gagnants plus profonds — atténuant directement l'**horizon effect**.

L'algorithme concret est le **Simulated Quantum Annealing (SQA)** via **Path Integral Monte Carlo (PIMC)**. C'est un algorithme 100% classique qui simule le tunneling quantique sur CPU/GPU.

### Algorithme SQA-PIMC (pseudocode)

```
1. Initialiser M répliques (tranches imaginaires) du système
2. Pour chaque réplique r ∈ [1..M] :
     - Assigner une configuration de spins/coups initiale
3. Pour t = 1..max_iter :
     a. Température T = schedule(t)  # décroît avec le temps
     b. Champ transverse Γ = schedule_Γ(t)  # décroît aussi
     c. Pour chaque réplique r :
          - Proposer un flip de spin (changer l'ordre des coups)
          - Calculer ΔE = variation d'énergie
          - Accepter avec probabilité min(1, exp(-ΔE/T))
     d. Pour chaque paire de répliques voisines (r, r+1) :
          - Proposer un échange de configuration (tunnel)
          - Accepter avec probabilité min(1, exp(-ΔE_tunnel))
4. Retourner la meilleure configuration trouvée
```

Le paramètre clé est le **champ transverse `Γ`** qui contrôle la force du tunneling. Quand `Γ` est élevé, le système traverse facilement les barrières (exploration). Quand `Γ` → 0, le système se comporte comme du recuit simulé classique (exploitation).

### Application à la recherche alpha-beta

Au lieu d'une recherche purement gloutonne, on peut intercaler des phases SQA pour explorer des alternatives que alpha-beta aurait élaguées :

```
fonction recherche_avec_tunnel(profondeur, alpha, beta):
    si profondeur == 0:
        retourner evaluer()
    
    coups = generer_coups()
    
    # Phase de tunneling : SQA sur l'ordre des coups
    meilleur_ordre = SQA_optimize(coups, temperature_initiale)
    
    pour coup dans meilleur_ordre:
        score = -recherche_avec_tunnel(profondeur-1, -beta, -alpha)
        alpha = max(alpha, score)
        si alpha >= beta: break
    
    retourner alpha
```

### Références

| Référence | Lien |
|-----------|------|
| **Simulated Quantum Annealing (Crosson & Harrow)** — Preuve que SQA peut être exponentiellement plus rapide que SA classique | [arXiv:2301.04278](https://arxiv.org/abs/2301.04278) |
| **SQA via Path Integral Monte Carlo (Martoňák et al., 2002)** — Article séminal sur PIMC pour l'optimisation | [Phys. Rev. E 71, 066707](https://link.aps.org/doi/10.1103/PhysRevE.71.066707) |
| **Performance of QAIA (Communications Physics, 2024)** — Comparaison des algorithmes d'inspiration quantique | [Nature Comms Phys](https://www.nature.com/articles/s42005-024-01705-7) |
| **Quantum-Inspired Annealing (McCaffrey, MS Research, 2022)** — Tutoriel pratique C#/Python avec `pct_tunnel` | [Visual Studio Magazine](https://visualstudiomagazine.com/articles/2022/01/20/quantum-inspired-annealing.aspx) |

### Implémentations existantes

| Projet | Description | Langage |
|--------|-------------|---------|
| **[therooler/piqmc](https://github.com/therooler/piqmc)** | SA + SQA avec PIMC, prêt à l'emploi | Python |
| **[dtoconnor/MonteCarloSolvers](https://github.com/dtoconnor/MonteCarloSolvers)** | PIMC pour Ising Hamiltonians | Python |
| **[fs-bullard/pimc](https://github.com/fs-bullard/pimc)** | Comparaison SA vs QA avec PIMC | Python |
| **[bqth29/simulated-bifurcation-algorithm](https://github.com/bqth29/simulated-bifurcation-algorithm)** | Simulated Bifurcation (variante plus récente que SQA, 165★) | Python (CPU/GPU) |

### Complexité

- **M** (répliques) : typiquement 10-30 pour un bon compromis précision/vitesse
- **Coût par itération** : `O(M · n²)` où `n` = nombre de coups
- **Convergence** : typiquement 1000-10000 itérations pour une position complexe
- **Parallélisable** : chaque réplique peut tourner sur un thread/core séparé

### Temps estimé pour un POC

| Phase | Durée |
|-------|-------|
| Implémentation SQA-PIMC en Python (validation sur QUBO benchmark) | 3-4 jours |
| Adaptation au contexte échecs (espace des coups → espace des spins) | 2-3 jours |
| Intégration dans le moteur (callback de recherche) | 3-4 jours |
| Benchmark : horizon effect avant/après | 2 jours |
| **Total POC** | **~2 semaines** |

---

## Piste 3 — QUBO pour l'Évaluation de Position

### Concept

Formuler l'évaluation statique d'une position d'échecs comme un problème QUBO. Chaque pièce est une variable binaire, et les interactions entre pièces sont les coefficients quadratiques. L'avantage : l'évaluation devient un produit matriciel massivement parallélisable, compatible QPU.

### Formulation

Soit une position avec `n` pièces (max 32). On définit :

- `x_i ∈ {0,1}` : présence de la pièce `i` sur la case `s_i`
- `Q_ii` = valeur de la pièce `i` sur sa case (table PST — Piece Square Table)
- `Q_ij` = interaction entre pièce `i` et pièce `j` (attaque, défense, clouage, etc.)

**Évaluation :**

```
eval(position) = x^T Q x
```

### Exemple concret (position avec 4 pièces)

```
Pièces : Roi_blanc (x1), Tour_blanche (x2), Roi_noir (x3), Cavalier_noir (x4)

Q = [
    [500,  20,   0, -10],   # Roi_blanc : valeur 500, bonus avec Tour, malus si Cavalier noir proche
    [  0, 500, -30,   5],   # Tour_blanche : valeur 500, bonus si attaque Roi noir, bonus avec Cavalier
    [  0,   0, 500,  15],   # Roi_noir : valeur 500, bonus avec son Cavalier
    [  0,   0,   0, 300]    # Cavalier_noir : valeur 300
]

eval = 500x1 + 500x2 + 500x3 + 300x4
     + 20·x1x2 - 10·x1x4 - 30·x2x3 + 5·x2x4 + 15·x3x4
```

### Avantages

1. **Parallélisable** : le produit `x^T Q x` se calcule en `O(n²)` avec des opérations matricelles (BLAS, GPU)
2. **Apprentissage** : les coefficients `Q_ij` peuvent être appris par rétropropagation (comme les poids NNUE)
3. **Compatibilité QPU** : la même matrice Q peut être soumise à un quantum annealer D-Wave
4. **Interprétable** : chaque coefficient a un sens physique (interaction entre deux pièces)

### Références

| Référence | Lien |
|-----------|------|
| **QUBO Formulations Tutorial (Glover et al., 2019)** — Base mathématique complète | [arXiv:1811.11538](https://arxiv.org/abs/1811.11538) |
| **PennyLane QUBO Demo** — Implémentation QUBO + QAOA + D-Wave | [pennylane.ai](https://pennylane.ai/demos/tutorial_QUBO) |
| **Simulated Bifurcation (bqth29)** — Solveur QUBO GPU massivement parallèle, 165★ | [GitHub](https://github.com/bqth29/simulated-bifurcation-algorithm) |
| **QUBO.jl** — Package Julia pour QUBO end-to-end | [YouTube](https://www.youtube.com/watch?v=gPapX51j_KQ) |

### Complexité

- **Matrice Q** : `32 × 32` = 1024 coefficients (taille fixe, indépendante de la position)
- **Évaluation** : `O(n²)` = 1024 multiplications-additions — **~100× plus rapide qu'un NNUE complet**
- **Apprentissage** : rétropropagation standard sur les `Q_ij` (gradient descent)

### Limitation importante

L'évaluation QUBO pure (quadratique) ne capture que les interactions **par paires**. Les interactions à 3+ corps (fourchette, clouage avec 3 pièces) nécessitent des termes d'ordre supérieur (HUBO — Higher-order Unconstrained Binary Optimization). Solution : utiliser la **décomposition de Grover-mixer QAOA** pour HUBO.

### Temps estimé pour un POC

| Phase | Durée |
|-------|-------|
| Construction de la matrice Q à partir des PST existantes | 1 jour |
| Ajout des termes d'interaction (attaques/défenses) | 2 jours |
| Implémentation de l'évaluateur QUBO en Rust | 2-3 jours |
| Benchmark : corrélation QUBO vs NNUE sur positions réelles | 2 jours |
| **Total POC** | **~1 semaine** |

---

## Recommandation : par où commencer

### Priorité 1 : Piste 1 (QUBO Move Ordering) — **1 semaine**

**Pourquoi en premier :**
- Faible risque : le QUBO est petit (≤40 variables), solveur classique (recuit simulé) suffit
- Impact immédiat : un meilleur move ordering = plus de cutoffs = profondeur effective accrue
- Code existant : `pasqal-io/qubo-solver` et `simulated-bifurcation-algorithm` sont prêts à l'emploi
- Pas besoin de hardware quantique

**Plan d'action :**
1. Installer `pasqal-io/qubo-solver` (ou `simulated-bifurcation`)
2. Écrire un script Python qui prend les coups + scores du moteur et construit Q
3. Comparer le taux de cutoffs avec/sans optimisation QUBO
4. Si concluant, intégrer en Rust via FFI ou réimplémentation

### Priorité 2 : Piste 3 (QUBO Évaluation) — **1 semaine**

**Pourquoi en deuxième :**
- Complément naturel de la Piste 1 (même formalisme QUBO)
- Parallélisable sur GPU (important pour Edge AI)
- Peut servir d'évaluation de secours légère (poids < 1 Ko vs NNUE ~50 Mo)

**Attention :** Ne pas remplacer NNUE — l'évaluation QUBO est moins précise. L'utiliser comme **évaluation rapide** pour les nœuds de faible profondeur ou comme **pré-filtre** avant NNUE.

### Priorité 3 : Piste 2 (SQA-Tunneling) — **2 semaines**

**Pourquoi en dernier :**
- Plus complexe à implémenter et à tuner (paramètres M, Γ, schedule)
- Bénéfice potentiel élevé (atténuation horizon effect) mais difficile à mesurer
- Nécessite une intégration plus profonde dans le moteur de recherche

**Quand l'envisager :**
- Après avoir validé les Pistes 1 et 3
- Quand le moteur aura une base de test solide (benchmark positions avec horizon effect)
- Si le passage en Rust est fait (performance nécessaire pour SQA temps réel)

---

## Résumé des outils et bibliothèques

| Outil | Utilité | Lien |
|------|--------|------|
| **pasqal-io/qubo-solver** | Solveur QUBO classique + quantique (Python) | [GitHub](https://github.com/pasqal-io/qubo-solver) |
| **simulated-bifurcation** | Solveur QUBO GPU massivement parallèle (Python) | [GitHub](https://github.com/bqth29/simulated-bifurcation-algorithm) |
| **piqmc (therooler)** | SQA + PIMC prêt à l'emploi | [GitHub](https://github.com/therooler/piqmc) |
| **MonteCarloSolvers** | PIMC pour Ising Hamiltonians | [GitHub](https://github.com/dtoconnor/MonteCarloSolvers) |
| **PennyLane QUBO** | Tutoriel QUBO + QAOA + D-Wave | [pennylane.ai](https://pennylane.ai/demos/tutorial_QUBO) |
| **find-qubo (LANL)** | Construction automatique de QUBO à partir d'une table de vérité | [GitHub](https://github.com/lanl/find-qubo) |
| **QuantumAnnealing.jl** | Simulation d'annealing quantique en Julia | [GitHub](https://github.com/lanl-ansi/QuantumAnnealing.jl) |

---

*Document rédigé le 28 juillet 2026. Sources : arXiv, GitHub, Chessprogramming wiki, Visual Studio Magazine, Nature Communications Physics.*
