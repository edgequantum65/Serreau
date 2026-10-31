# Ressources Complètes - Chess Engine Development

## 📚 Documentation Principale

### Chess Programming Wiki (CPW)
- **URL**: https://www.chessprogramming.org
- **Description**: LA référence absolue. 4269 articles, 8026 pages
- **Sections clés**:
  - [Getting Started](https://www.chessprogramming.org/Getting_Started)
  - [Board Representation](https://www.chessprogramming.org/Board_Representation)
  - [Move Generation](https://www.chessprogramming.org/Move_Generation)
  - [Search](https://www.chessprogramming.org/Search)
  - [Evaluation](https://www.chessprogramming.org/Evaluation)
  - [UCI](https://www.chessprogramming.org/UCI)
  - [Engine Testing](https://www.chessprogramming.org/Engine_Testing)

### Livres de Référence
1. **Shannon (1949)** - "Programming a Computer for Playing Chess"
   - Le document fondateur
   - PDF: http://www.pi.infn.it/~carosi/chess/shannon.txt

2. **Knuth (1998)** - "The Art of Computer Programming" Vol 3
   - Sorting and Searching, 2nd Edition

3. **Tsvetkov (2012)** - "Little Chess Evaluation Compendium"
   - Guide complet de l'évaluation

4. **Tsvetkov (2017)** - "The Secret of Chess"
   - Techniques avancées d'évaluation

## 🔧 Représentation de l'Échiquier

### Options
| Méthode | Avantages | Inconvénients |
|---------|-----------|---------------|
| **Bitboards** | Performance maximale | Complexité initiale |
| **0x88** | Bon compromis | Moins performant |
| **Mailbox 10x12** | Simple | Lent |
| **Mailbox 8x8** | Très simple | Limité |

### Ressources Bitboards
- [Bitboards](https://www.chessprogramming.org/Bitboards)
- [Magic Bitboards](https://www.chessprogramming.org/Magic_Bitboards)
- [Best Magics](https://www.chessprogramming.org/Best_Magics_so_far)

## 🎯 Génération de Coups

### Fonctionnalités obligatoires
- Coups simples (pièces normales)
- Captures
- Roque (côté dame et roi)
- En passant
- Promotion (dame, tour, fou, cavalier)

### Ressources
- [Move Generation](https://www.chessprogramming.org/Move_Generation)
- [Perft](https://www.chessprogramming.org/Perft) - Tests de validation
- [Perft Results](https://www.chessprogramming.org/Perft_Results) - Résultats de référence

## 🔍 Moteur de Recherche

### Algorithmes fondamentaux
1. **Minimax** - Base de tout
2. **Negamax** - Version symétrique (recommandé)
3. **Alpha-Beta Pruning** - Élagage essentiel
4. **Iterative Deepening** - Pour le time control

### Optimisations avancées
- **Transposition Tables** - Cache des positions
- **Null Move Pruning** - Réduction aggressive
- **LMR (Late Move Reductions)** - Réduction des coups tardifs
- **Aspiration Windows** - Fenêtres de recherche
- **Quiescence Search** - Stabilisation des tactiques

### Ressources
- [Search](https://www.chessprogramming.org/Search)
- [Alpha-Beta](https://www.chessprogramming.org/Alpha-Beta)
- [Iterative Deepening](https://www.chessprogramming.org/Iterative_Deepening)
- [Transposition Table](https://www.chessprogramming.org/Transposition_Table)
- [Null Move Pruning](https://www.chessprogramming.org/Null_Move_Pruning)
- [LMR](https://www.chessprogramming.org/Late_Move_Reductions)

## 🧠 Fonction d'Évaluation

### Composants de base
1. **Matériel** - Valeur des pièces
2. **Tables pièce-position** - Bonus/malus par case
3. **Structure de pions** - Pions doublés, isolés, passés
4. **Sécurité du roi** - shelter, attaques
5. **Mobilité** - Coups disponibles

### Valeurs matérielles standard
```
Pion:      100
Cavalier:  320
Fou:       330
Tour:      500
Dame:      900
Roi:       20000
```

### Ressources
- [Evaluation](https://www.chessprogramming.org/Evaluation)
- [Material](https://www.chessprogramming.org/Material)
- [Piece-Square Tables](https://www.chessprogramming.org/Piece-Square_Tables)
- [Pawn Structure](https://www.chessprogramming.org/Pawn_Structure)
- [King Safety](https://www.chessprogramming.org/King_Safety)
- [Mobility](https://www.chessprogramming.org/Mobility)
- [Simplified Evaluation Function](https://www.chessprogramming.org/Simplified_Evaluation_Function)
- [PeSTO's Evaluation Function](https://www.chessprogramming.org/PeSTO%27s_Evaluation_Function)

## 📡 Protocole UCI

### Spécification officielle
- [UCI Protocol](https://www.shredderchess.com/chess-features/uci-universal-chess-interface.html)
- [UCI WBEC](http://wbec-ridderkerk.nl/html/UCIProtocol.html)

### Commandes essentielles
```
uci            # Identification
isready        # Vérification
position       # Définir la position
go             # Lancer la recherche
stop           # Arrêter
ponderhit      # Coup de pondering
```

### GUIs compatibles
- [Arena](http://www.playwitharena.com) - Gratuit, complet
- [CuteChess](https://github.com/cutechess/cutechess) - Pour les tournois
- [Lucas Chess](https://lucaschess.org) - Éducatif
- [SCID](https://scidvspc.sourceforge.net) - Base de données

## 🧪 Tests et Développement

### Méthodologie
1. **Perft** - Valider la génération de coups
2. **Test Positions** - Positions tactiques
3. **SPRT** - Tests statistiques (recommandé)
4. **Self-play** - Moteur contre lui-même

### Outils de test
- [CuteChess-cli](https://github.com/cutechess/cutechess) - Tournois automatisés
- [OpenBench](https://github.com/AndyGrant/OpenBench) - Framework SPRT
- [Fishtest](https://tests.stockfishfish.org) - Framework Stockfish

### Bases de test
- [WAC (Win at Chess)](https://www.chessprogramming.org/Win_at_Chess)
- [STS (Strategic Test Suite)](https://www.chessprogramming.org/Strategic_Test_Suite)
- [Bratko-Kopec Test](https://www.chessprogramming.org/Bratko-Kopec_Test)

## 📊 Listes de Rating

### Principales listes
| Liste | URL | Focus |
|-------|-----|-------|
| **CCRL** | http://ccrl.chessdom.com | Rating général |
| **CEGT** | http://www.cegt.net | Tournois |
| **SSDF** | http://www.ssdf.se | Suédois |
| **TCEC** | https://tcec-chess.com | Compétition elite |
| **Lichess** | https://lichess.org | En ligne |

## 🏆 Compétitions

### Tournois majeurs
1. **TCEC** (Top Chess Engine Championship)
   - L'élite mondiale
   - https://tcec-chess.com

2. **CCC** (Computer Chess Championship)
   - Chess.com
   - https://www.chess.com/computer-chess-championship

3. **ICGA Computer Olympiad**
   - Compétition scientifique
   - https://icga.org

4. **CCWC** (World Computer Chess Championship)
   - Championnat du monde

### Compétitions en ligne
- **Lichess Engine Arena** - Gratuit, hebdomadaire
- **Chess.com Bots** - Défier les bots
- **Engine vs Engine** - Sur CuteChess

## 🎯 Résolution de Problèmes

### Bases de données
- [EGTB Probe](https://www.chessprogramming.org/Endgame_Tablebases)
- [Syzygy Tables](https://syzygy-tables.info)
- [Nalimov Tables](https://www.chessprogramming.org/Nalimov)
- [Gaviota Tables](https://www.chessprogramming.org/Gaviota)

### Formats de problèmes
- **EPD** (Extended Position Description)
- **FEN** (Forsyth-Edwards Notation)
- **PGN** (Portable Game Notation)

## 🛠️ Outils de Développement

### Compilateurs
- **GCC** (Linux/MinGW)
- **Clang** (Performance)
- **MSVC** (Windows)

### Débogage
- **GDB** (Linux)
- **Valgrind** (Mémoire)
- **ASAN** (Address Sanitizer)

### Performance
- **perf** (Linux)
- **VTune** (Intel)
- **gprof** (Profiling)

## 📈 Métriques de Performance

### Mesures importantes
- **Nœuds/seconde** - Vitesse de recherche
- **Profondeur moyenne** - Qualité de la recherche
- **ELO** - Force de jeu
- **Hit rate** - Efficacité des transposition tables

### Benchmarks
- [Chess Engine Benchmarks](https://www.chessprogramming.org/Engine_Testing)
- [NPS Tests](https://www.chessprogramming.org/Search_Statistics)

## 🤝 Communauté

### Forums
- [TalkChess](http://www.talkchess.com/forum3/) - Le plus actif
- [Computer Chess Forums](https://www.chessprogramming.org/Computer_Chess_Forums)
- [OpenChess](https://open-chess.org)

### Discord
- [Chess Programming Discord](https://discord.gg/chess-programming)
- [Stockfish Discord](https://discord.gg/stockfish)

### Développeurs connus à suivre
- **Marco Costalba** (Stockfish)
- **Tord Romstad** (Stockfish/Joël)
- **Joona Kiiski** (Stockfish)
- **Larry Kaufman** (Komodo)
- **Ed Schröder** (Rebel)

## 📝 Notes pour David

### Ton avantage en tant que joueur
1. **Compréhension positionnelle** - Tu peux créer une eval function "intelligente"
2. **Connaissance des structures de pions** - Critical pour l'eval
3. **Expérience des fins de partie** - Intégrer les tables d'endgame
4. **Analyse stratégique** - Créer un moteur avec du "style"

### Suggestions d'implementation
1. **Commence simple** - Mailbox 8x8, Minimax basique
2. **Teste rigoureusement** - Perft est crucial
3. **Implémente UCI tôt** - Pour les tests avec Arena
4. **Rejoins la communauté** - TalkChess est très accueillant

---

*Dernière mise à jour: 10 Juillet 2026*
