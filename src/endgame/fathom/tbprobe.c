/* tbprobe.c — STUB de compilation (02/10/2026, mission NNUE2).
 *
 * Le vrai tbprobe.c (Fathom) était absent du disque et non versionné par git,
 * alors que CMakeLists.txt le référence (commit 62aab4e). Ce stub fournit les
 * symboles attendus par src/endgame/tb.c pour permettre la compilation de
 * build_nnue2. Il désactive Syzygy (tb_init=false, TB_LARGEST=0), ce qui est
 * cohérent avec tb_probe_syzygy() qui retourne déjà -2 (probe désactivé).
 * Aucun impact sur le NNUE (objet de la mission NNUE2).
 */
#include <stdbool.h>
#include <stdint.h>

unsigned TB_LARGEST = 0;

bool tb_init(const char *_path) { (void)_path; return false; }
void tb_free(void) {}

unsigned tb_probe_wdl_impl(
    uint64_t _white, uint64_t _black,
    uint64_t _kings, uint64_t _queens,
    uint64_t _rooks, uint64_t _bishops,
    uint64_t _knights, uint64_t _pawns,
    unsigned _ep, bool _turn) {
    (void)_white; (void)_black; (void)_kings; (void)_queens;
    (void)_rooks; (void)_bishops; (void)_knights; (void)_pawns;
    (void)_ep; (void)_turn;
    return 0xFFFFFFFFu;   /* TB_RESULT_FAILED */
}
