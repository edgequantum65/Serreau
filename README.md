<div align="center">

# Serreau

**A UCI chess engine in C — built by an ICCF International Master.**

![Language: C](https://img.shields.io/badge/language-C-555555?style=flat-square)
![Protocol: UCI](https://img.shields.io/badge/protocol-UCI-2d6a4f?style=flat-square)
![Evaluation: HCE + NNUE](https://img.shields.io/badge/evaluation-HCE%20%2B%20NNUE-1d4e89?style=flat-square)
![Rust track](https://img.shields.io/badge/Rust-port%20in%20progress-b7410e?style=flat-square)
![License: MIT](https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square)

[English](README.md) · [Français](README_FR.md)

</div>

---

## About

Serreau is a from-scratch chess engine: bitboards, magic move generation, an alpha-beta search with the usual modern refinements, and two evaluations you can switch between at runtime — a hand-crafted one and an **NNUE** neural network.

It was written by [David Serreau](https://www.iccf.com/player?id=181914), an ICCF International Master in correspondence chess. In correspondence chess engines are part of the game, and the craft is to steer them and to check what they say. This project comes from that practice: understanding an engine well enough to build one, measure it honestly, and improve it.

## Highlights

**Search**
- Negamax with alpha-beta, iterative deepening, aspiration windows
- Transposition table with ageing
- Null-move pruning, late-move reductions, late-move pruning
- Razoring / futility / reverse futility pruning
- Singular extensions
- Quiescence search with static exchange evaluation (SEE)
- Killer moves, history, capture history and countermove heuristics

**Evaluation**
- **NNUE** (HalfKP-type, incremental accumulators), able to load Stockfish 13–format networks as well as the engine's own format. The network layout is the one defined in `src/nnue.h`; trained weights are not provided.
- A classical hand-crafted evaluation (HCE), used out of the box and as a reference

**Engine**
- Magic-bitboard move generation, validated with perft: 13 reference counts (start position to depth 5, Kiwipete to depth 4, positions 3–6 of chessprogramming.org), about 15.7 million leaf nodes, all exact (`tests/test_perft_counts.c`, run on every push)
- UCI: `Hash`, `Ponder`, `MultiPV`, `UseNNUE`, `EvalFile`, `OwnBook`
- Polyglot opening-book support
- Syzygy hooks are in place; probing is disabled in this snapshot
- Python tooling around the engine: PGN → FEN extraction, book building, scale calibration, match scripts

## Rust

A Rust track runs alongside the C engine:

- a UCI engine in Rust built on the same ideas, and
- a `no_std` **NNUE forward pass** meant for microcontrollers, so the same network can run on tiny hardware.

These crates are not published here yet. The C engine is the reference implementation.

## Quick start

**CMake (MinGW or any C toolchain)**

```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles"     # or: cmake ..
mingw32-make -j4                  # or: make -j4
./chess_engine uci
```

**Plain GCC (tested on x86-64)**

```bash
gcc -O2 -mavx2 -mbmi2 -Isrc -Isrc/endgame -o serreau \
    $(ls src/*.c | grep -v engine_dll.c) $(find src/endgame -name '*.c') -lm
./serreau
```

Then type `uci`, `isready`, `position startpos`, `go depth 8`. Or load the binary in any UCI GUI (Arena, CuteChess, …).

The engine runs with the classical evaluation out of the box. To enable NNUE, set `UseNNUE` and point `EvalFile` to a compatible network — weights are not shipped in this repository.

## Repository layout

| Path | Content |
|---|---|
| `src/` | the engine (search, evaluation, NNUE, movegen, UCI, books) |
| `tests/` | perft, move generation, NNUE and hashing tests |
| `tools/`, `scripts/` | Python helpers (books, calibration, matches) |
| `docs/` | notes on the evaluation, time management and the NNUE roadmap |

## Status

Serreau is an active research project. Playing strength is being measured with calibrated matches; no rating is claimed here until the numbers are reproducible.

## About the author

David Serreau plays ICCF correspondence chess (ICCF ID [181914](https://www.iccf.com/player?id=181914)):

- **ICCF International Master (IM)** — title earned in October 2026 after three IM norms: Ferreira Memorial B (March 2025), 2nd Dobri Semov Memorial Open (November 2025) and Oleksandr Vaisman Memorial (October 2026)
- Correspondence Chess Master (CCM) since February 2025, Correspondence Chess Expert (CCE) since 2024
- ICCF rating 2379 (October 2026)
- Semi-finalist of the ICCF World Cup, classical and Chess960
- Member of the French team in international matches, and first board in the French team championship

Part of the [Edge Quantum 65](https://edgequantum65.fr) project.

## License

MIT — see [`LICENSE`](LICENSE).
