<div align="center">

# Serreau

**Un moteur d'échecs UCI en C — conçu par un Maître International ICCF.**

[English](README.md) · [Français](README_FR.md)

</div>

---

## Présentation

Serreau est un moteur d'échecs écrit de zéro : bitboards, génération de coups magique, recherche alpha-bêta avec les raffinements modernes, et deux évaluations commutables à l'exécution — une évaluation classique et un réseau de neurones **NNUE**.

Il est écrit par [David Serreau](https://www.iccf.com/player?id=181914), joueur d'échecs par correspondance (ICCF). En correspondance, les moteurs font partie du jeu : le métier consiste à les diriger et à vérifier ce qu'ils affirment. Ce projet vient de cette pratique : comprendre un moteur assez bien pour en construire un, le mesurer honnêtement, puis l'améliorer.

## Points forts

**Recherche** : negamax / alpha-bêta, approfondissement itératif, fenêtres d'aspiration, table de transposition, élagage par coup nul, réductions et élagage des coups tardifs, razoring / futility, extensions singulières, recherche de quiétude avec SEE, heuristiques killers / history / countermove.

**Évaluation** : **NNUE** de type HalfKP, avec accumulateurs incrémentaux, capable de charger des réseaux au format Stockfish 13 comme le format propre au moteur ; évaluation classique en complément. L'architecture du réseau est celle définie dans `src/nnue.h` ; les poids entraînés ne sont pas fournis.

**Moteur** : génération de coups par magic bitboards, validée par perft (13 comptes de référence de chessprogramming.org, environ 15,7 millions de positions, tous exacts, vérifiés à chaque push), options UCI `Hash`, `Ponder`, `MultiPV`, `UseNNUE`, `EvalFile`, `OwnBook`, livres d'ouvertures Polyglot. Les points d'entrée Syzygy existent ; la lecture est désactivée dans cette version.

## Rust

Une piste Rust avance en parallèle : un moteur UCI en Rust et une **passe avant NNUE `no_std`** pensée pour les microcontrôleurs. Ces crates ne sont pas encore publiées ici ; le moteur en C reste la référence.

## Démarrage rapide

```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
mingw32-make -j4
./chess_engine uci
```

Le moteur fonctionne avec l'évaluation classique dès l'installation. Pour activer le NNUE : option `UseNNUE` et `EvalFile` vers un réseau compatible (les poids ne sont pas fournis dans ce dépôt).

## État du projet

Projet de recherche actif. La force de jeu est en cours de mesure par des matchs calibrés ; aucun classement n'est annoncé tant que les chiffres ne sont pas reproductibles.

## L'auteur

David Serreau joue aux échecs par correspondance (ICCF n° [181914](https://www.iccf.com/player?id=181914)) : **Maître International ICCF (IM)**, titre obtenu en octobre 2026 après trois normes (Ferreira Memorial B en mars 2025, 2nd Dobri Semov Memorial Open en novembre 2025, Oleksandr Vaisman Memorial en octobre 2026) ; Maître de correspondance (CCM) depuis février 2025 et Expert (CCE) depuis 2024 ; classement ICCF 2379 (octobre 2026) ; demi-finaliste de la Coupe du monde ICCF (classique et Chess960), membre de l'équipe de France dans des matchs internationaux et premier échiquier au championnat de France par équipes.

Projet [Edge Quantum 65](https://edgequantum65.fr).

## Licence

MIT — voir [`LICENSE`](LICENSE).
