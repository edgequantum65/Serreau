"""
engine_dll.py — Wrapper Python ctypes pour chess_engine_dll.dll

Contourne WDAC : la DLL est chargée dans le process Python via ctypes,
pas de subprocess → pas de WinError 4551.

Interface identique à PersistentEngine (match_persistent.py) pour
être un drop-in replacement.
"""
import ctypes
import os
import queue
import threading
import time
import sys

DLL_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DLL_PATH = os.path.join(DLL_DIR, "build", "chess_engine_dll.dll")

if not os.path.exists(DLL_PATH):
    DLL_PATH = os.path.join(DLL_DIR, "chess_engine_dll.dll")


class DllEngine:
    """Wrapper UCI via DLL chargée en ctypes (pas de subprocess)."""

    def __init__(self, path=None, name="engine"):
        self.name = name
        self._dll = None
        self._out_queue = queue.Queue()
        self._reader_thread = None
        self._running = False
        self._start()

    def _start(self):
        dll_path = DLL_PATH
        if not os.path.exists(dll_path):
            alt = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "chess_engine_dll.dll")
            if os.path.exists(alt):
                dll_path = alt

        if not os.path.exists(dll_path):
            raise FileNotFoundError(
                f"DLL not found: {dll_path}\n"
                f"Build it with: cd build && mingw32-make chess_engine_dll"
            )

        self._dll = ctypes.CDLL(dll_path)

        self._dll.engine_start.restype = ctypes.c_bool
        self._dll.engine_is_running.restype = ctypes.c_bool
        self._dll.engine_read.restype = ctypes.c_int
        self._dll.engine_read.argtypes = [ctypes.c_char_p, ctypes.c_int]
        self._dll.engine_write.argtypes = [ctypes.c_char_p]

        t0 = time.time()
        if not self._dll.engine_start():
            raise RuntimeError(f"{self.name}: engine_start() failed")

        self._running = True

        self._reader_thread = threading.Thread(target=self._reader, daemon=True)
        self._reader_thread.start()

        self._send("uci")
        if not self._wait_for("uciok", 60):
            raise RuntimeError(f"{self.name}: timeout waiting for uciok")
        self._send("isready")
        if not self._wait_for("readyok", 60):
            raise RuntimeError(f"{self.name}: timeout waiting for readyok")

        elapsed = time.time() - t0
        print(f"  [{self.name}] ready in {elapsed:.1f}s (DLL mode)", flush=True)

    def _reader(self):
        buf = ctypes.create_string_buffer(65536)
        while self._running:
            n = self._dll.engine_read(buf, 65536)
            if n > 0:
                text = buf.value.decode('utf-8', errors='replace')
                for line in text.split('\n'):
                    line = line.strip()
                    if line:
                        self._out_queue.put(line)
            else:
                time.sleep(0.01)

    def _send(self, cmd):
        if not self._running:
            return
        self._dll.engine_write(cmd.encode('utf-8'))

    def _wait_for(self, target, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self._out_queue.get(timeout=0.3)
                if target in line:
                    return line
            except queue.Empty:
                if not self._dll.engine_is_running():
                    return None
        return None

    def _read_until(self, target, timeout):
        lines = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self._out_queue.get(timeout=0.3)
                lines.append(line)
                if target in line:
                    return lines
            except queue.Empty:
                if not self._dll.engine_is_running():
                    break
        return lines

    def new_game(self):
        self._send("ucinewgame")
        self._send("isready")
        self._wait_for("readyok", 10)

    def get_move(self, moves, depth=None, tc=None, movenum=0):
        while not self._out_queue.empty():
            try:
                self._out_queue.get_nowait()
            except queue.Empty:
                break

        if moves:
            mstr = " ".join(moves)
            self._send(f"position startpos moves {mstr}")
        else:
            self._send("position startpos")

        if tc is not None:
            parts = tc.split("/")
            tc_moves = int(parts[0]) if len(parts) >= 2 else 40
            rest = parts[1] if len(parts) >= 2 else parts[0]
            inc_part = rest.split("+")
            total_min = float(inc_part[0])
            inc_sec = int(inc_part[1]) if len(inc_part) >= 2 else 0
            total_ms = int(total_min * 60 * 1000)
            inc_ms = inc_sec * 1000
            remaining_moves = max(tc_moves - movenum // 2, 1)
            base_time = total_ms // remaining_moves
            wtime = btime = base_time + inc_ms
            self._send(f"go wtime {wtime} btime {btime} winc {inc_ms} binc {inc_ms}")
        else:
            self._send(f"go depth {depth}")

        lines = self._read_until("bestmove", 60)
        for l in lines:
            if l.startswith("bestmove"):
                parts = l.split()
                if len(parts) >= 2:
                    bestmove = parts[1]
                    is_draw = bestmove in ("(none)", "0000")
                    return bestmove, is_draw
        return None, False

    def close(self):
        self._running = False
        try:
            self._dll.engine_stop()
        except Exception:
            pass


def engine_factory(path, name="engine"):
    """
    Factory qui choisit automatiquement DLL ou subprocess.
    Priorité DLL (contourne WDAC), fallback subprocess.
    """
    dll_path = DLL_PATH
    if not os.path.exists(dll_path):
        alt = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "chess_engine_dll.dll")
        if os.path.exists(alt):
            dll_path = alt

    if os.path.exists(dll_path) and path.lower().endswith('.exe'):
        base = os.path.splitext(os.path.basename(path))[0]
        if base.startswith('chess_engine'):
            try:
                return DllEngine(name=name)
            except Exception as e:
                print(f"  DLL mode failed ({e}), falling back to subprocess...", flush=True)

    from tools.match_persistent import PersistentEngine
    return PersistentEngine(path, name=name)


if __name__ == "__main__":
    print("Testing DLL engine...", flush=True)
    eng = DllEngine(name="test")
    eng._send("uci")
    lines = eng._read_until("uciok", 5)
    for l in lines:
        print(f"  {l}")
    eng.close()
    print("OK", flush=True)
