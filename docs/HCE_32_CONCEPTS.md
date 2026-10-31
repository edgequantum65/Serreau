# HCE — 32 Concepts d'Évaluation (vérifié dans evaluation.c)

**Date** : 27/07/2026 — source: `src/evaluation.c` (1 611 lignes)

---

## Fonctions principales (32)

| # | Fonction | Concept | Ligne |
|---|---|---|---|
| 1 | `evaluate_material` | Matériel (P/N/B/R/Q = 100/300/300/500/900) | 111 |
| 2 | `evaluate_piece_position` | Tables positionnelles (PSQT) | 135 |
| 3 | `evaluate_pawn_structure` | Pions doublés + isolés | 148 |
| 4 | `evaluate_mobility` | Mobilité simple (pseudo-coups) | 197 |
| 5 | `evaluate_bishop_pair` | Paire de fous (+40 MG, +80 EG) | 223 |
| 6 | `evaluate_rook_files` | Tours sur colonnes ouvertes/demi-ouvertes | 286 |
| 7 | `game_phase` | Phase de jeu (0=opening, 256=endgame) | 330 |
| 8 | `evaluate_development` | Développement (cavaliers/fous pas sortis, roi centre) | 341 |
| 9 | `evaluate_outposts` | Avant-postes (cavaliers/fous soutenus case avancée) | 369 |
| 10 | `evaluate_rook_seventh` | Tours en 7e rangée | 407 |
| 11 | `evaluate_trapped_pieces` | Pièces piégées | 426 |
| 12 | `evaluate_king_safety_improved` | Sécurité du roi (Weiss-style) | 477 |
| 13 | `evaluate_connectivity` | Connectivité entre pièces | 570 |
| 14 | `evaluate_patterns` | Patterns positionnels | 629 |
| 15 | `evaluate_piece_mobility` | Mobilité détaillée par type de pièce | 655 |
| 16 | `evaluate_threats` | Menaces tactiques | 796 |
| 17 | `evaluate_king_flank` | Attaque de flanc roi | 909 |
| 18 | `evaluate_king_attack` | Attaque du roi (pondéré par type) | 961 |
| 19 | `evaluate_space` | Contrôle d'espace | 1013 |
| 20 | `evaluate_closedness` | Fermeture de position | 1031 |
| 21 | `evaluate_passed_pawns` | Pions passés (free/blocked/rook behind) | 1054 |
| 22 | `evaluate_pawn_phalanx` | Phalanx de pions | 1125 |
| 23 | `evaluate_backward_pawns` | Pions arriérés | 1141 |
| 24 | `evaluate_rook_forward` | Tours avancées | 1168 |
| 25 | `evaluate_bishop_quality` | Bon/mauvais fou (mobilité pions) | 1199 |
| 26 | `evaluate_pins` | Clouage (pin detection) | 1220 |
| 27 | `evaluate_weak_squares` | Cases faibles (contrôlables par l'adversaire) | 1268 |
| 28 | `evaluate_pawn_majority` | Majorité de pions (attaques flanc) | 1293 |
| 29 | `evaluate_blockade` | Blocus (bloquer pions passés) | 1318 |
| 30 | `evaluate_suspended_pawns` | Pions suspendus (non protégés) | 1342 |
| 31 | `evaluate_pawn_breaks` | Leviers (breaks) | 1366 |
| 32 | `evaluate_worst_piece` | Pire pièce (activer la moins bonne) | 1394 |

---

## Comment ça s'articule

```
evaluate() [ligne 1407]
├── Matériel + PSQT + Pions (immédiat)
├── Paire de fous + Tours + Développement
├── Outposts + 7e rangée + Pièces piégées
├── Sécurité roi + Connectivité + Patterns
├── Mobilité par type + Menaces
├── Roi flanc + Attaque roi
├── Espace + Fermeture
├── Pions passés + Phalanx + Arriérés
├── Tour avancée + Qualité fou
├── Pin + Cases faibles + Majorité
├── Blocus + Suspendus + Breaks + Pire pièce
└── Tapered evaluation (MG/EG mix)
```

**Le bridge HCE→NNUE (corrélation 0.6794) a ENFIN appris ces 32 concepts à NNUE.**
