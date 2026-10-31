"""
Extract Lichess PGN by streaming (no full decompression in memory)
Processes in small batches to avoid timeout
"""
import pyzstd
import chess
import chess.pgn
import io
import time
import os

PGN_ZST = r'E:\ChessEngine\books\lichess_pgn\lichess_db_standard_rated_2023-01.pgn.zst'
BOOK_TXT = r'E:\ChessEngine\books\book.txt'

def stream_extract(pgn_zst, book_path, max_games=2500):
    """Stream decompress and extract positions"""
    print(f"Streaming {pgn_zst}...")
    t0 = time.time()
    
    # Count existing
    existing = set()
    if os.path.exists(book_path):
        with open(book_path, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith('#'):
                    parts = line.split()
                    if len(parts) >= 3:
                        existing.add(' '.join(parts[:-2]))
    print(f"Existing book: {len(existing)} positions")
    
    # Stream decompress and parse
    new_positions = []
    games = 0
    buffer = ""
    
    with pyzstd.open(pgn_zst, 'rt', encoding='utf-8', errors='replace') as f:
        while games < max_games:
            line = f.readline()
            if not line:
                break
            buffer += line
            
            # Process complete games in buffer
            while games < max_games:
                game = chess.pgn.read_game(io.StringIO(buffer))
                if game is None:
                    break
                
                # Remove processed game from buffer
                str_game = str(game)
                idx = buffer.find(str_game)
                if idx >= 0:
                    buffer = buffer[idx + len(str_game):]
                
                games += 1
                board = game.board()
                
                for move in game.mainline_moves():
                    fen = board.board_fen()
                    turn = 'w' if board.turn == chess.WHITE else 'b'
                    castling = board.castling_xfen()
                    ep = board.ep_square
                    ep_str = chess.SQUARE_NAMES[ep] if ep else '-'
                    hm = board.halfmove_clock
                    fn = board.fullmove_number
                    full_fen = f'{fen} {turn} {castling} {ep_str} {hm} {fn}'
                    
                    if full_fen not in existing:
                        new_positions.append((full_fen, move.uci()))
                        existing.add(full_fen)
                    
                    board.push(move)
                
                if games % 500 == 0:
                    elapsed = time.time() - t0
                    rate = games / elapsed if elapsed > 0 else 0
                    print(f"  {games} games, {len(new_positions)} new pos, {rate:.0f} games/s")
    
    # Append to book
    with open(book_path, 'a', encoding='utf-8') as f:
        for fen, move in new_positions:
            f.write(f'{fen} {move} 100\n')
    
    elapsed = time.time() - t0
    print(f"\nDone: {games} games, {len(new_positions)} new positions in {elapsed:.0f}s")
    print(f"Book total: {len(existing)} positions")
    return games, len(new_positions)

if __name__ == '__main__':
    stream_extract(PGN_ZST, BOOK_TXT, max_games=2500)
