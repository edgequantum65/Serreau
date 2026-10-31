#!/usr/bin/env python3
"""
Build a standard Polyglot opening book (book.bin) from book_merged.txt.

Input format: FEN MOVE SCORE  (space-separated, one per line)
Output format: standard Polyglot binary (16 bytes per entry)
  - uint64 key (big-endian)
  - uint16 move (big-endian): from_sq | (to_sq << 6) | (promotion << 12)
  - uint16 weight (big-endian)
  - uint32 learn (big-endian)
"""

import chess
import chess.polyglot
import struct
import sys
import os

def polyglot_key(board):
    """Cle Polyglot standard (identique a python-chess et au moteur C depuis le 03/10/2026).

    Ancienne version : couleurs inversees et case en passant toujours ajoutee, alors que le
    moteur ne l'ajoute que si une prise en passant est possible -> le livre ne trouvait plus
    rien apres chaque poussee de pion de deux cases.
    """
    return chess.polyglot.zobrist_hash(board)


def encode_polyglot_move(move_str, board):
    """Code un coup ('e2e4', 'e7e8q', 'e1g1') au format Polyglot standard.

    Bits 0-5 : case d'arrivee, bits 6-11 : case de depart, bits 12-14 : promotion.
    Cases numerotees A1=0 ... H8=63 (comme python-chess et le moteur).
    Le roque s'ecrit "le roi prend sa tour" (e1h1, e1a1), comme l'exige Polyglot.
    """
    move = chess.Move.from_uci(move_str)
    to_sq = move.to_square
    if board.is_castling(move):
        rank = chess.square_rank(move.from_square)
        to_sq = chess.square(7 if chess.square_file(move.to_square) > chess.square_file(move.from_square) else 0, rank)
    promo = {chess.KNIGHT: 1, chess.BISHOP: 2, chess.ROOK: 3, chess.QUEEN: 4}.get(move.promotion, 0)
    return to_sq | (move.from_square << 6) | (promo << 12)


def main():
    input_path = sys.argv[1] if len(sys.argv) > 1 else r'E:\ChessEngine\books\book_merged.txt'
    output_path = sys.argv[2] if len(sys.argv) > 2 else r'E:\ChessEngine\books\book.bin'

    if not os.path.exists(input_path):
        print(f"ERROR: Input file not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    print(f"Reading {input_path}...", file=sys.stderr)

    entries = []
    count = 0
    errors = 0

    with open(input_path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n\r')
            if not line:
                continue

            # Split on LAST two spaces: FEN MOVE SCORE
            # FEN can contain spaces, so we split from the right
            parts = line.rsplit(' ', 2)
            if len(parts) != 3:
                errors += 1
                if errors <= 10:
                    print(f"WARN: Cannot parse line: {line[:80]}", file=sys.stderr)
                continue

            fen, move_str, score_str = parts

            if len(move_str) < 4 or len(move_str) > 5:
                errors += 1
                if errors <= 10:
                    print(f"WARN: Invalid move string '{move_str}' on: {line[:80]}", file=sys.stderr)
                continue

            try:
                board = chess.Board(fen)
                key = polyglot_key(board)
                move_encoded = encode_polyglot_move(move_str, board)
                weight = max(1, min(65535, abs(int(score_str))))
                if weight < 1:
                    weight = 1

                entries.append((key, move_encoded, weight, 0))
                count += 1

                if count % 200000 == 0:
                    print(f"  Processed {count} entries...", file=sys.stderr)

            except Exception as e:
                errors += 1
                if errors <= 10:
                    print(f"ERROR: {e} on line: {line[:80]}", file=sys.stderr)
                continue

    print(f"Processed {count} valid entries, {errors} errors.", file=sys.stderr)

    if count == 0:
        print("ERROR: No entries generated!", file=sys.stderr)
        sys.exit(1)

    # Sort by key (required for binary search)
    print(f"Sorting {len(entries)} entries by key...", file=sys.stderr)
    entries.sort(key=lambda x: x[0])

    # Ne rien ecraser : l'ancien livre est conserve sous un autre nom
    if os.path.exists(output_path):
        import datetime
        sauvegarde = output_path[:-4] + '_ancien_' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '.bin'
        os.rename(output_path, sauvegarde)
        print(f"Ancien livre conserve : {sauvegarde}", file=sys.stderr)

    # Write binary Polyglot book
    print(f"Writing {output_path}...", file=sys.stderr)
    with open(output_path, 'wb') as f:
        for key, move, weight, learn in entries:
            f.write(struct.pack('>QHHI', key, move, weight, learn))

    file_size = os.path.getsize(output_path)
    print(f"Done! {len(entries)} entries written.", file=sys.stderr)
    print(f"File size: {file_size} bytes ({file_size // 16} entries).", file=sys.stderr)


if __name__ == '__main__':
    main()
