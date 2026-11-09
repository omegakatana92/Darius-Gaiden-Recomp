/* registry of the statically recompiled programs (one per edition), generated into gen/<prefix>/ */
#include "core.h"
typedef uint32_t (*rc_chunk_fn)(uint32_t pc);
extern rc_chunk_fn rc_chunk_table_dg[2048], rc_chunk_table_dgx[2048];
extern const uint32_t rc_stats_native_dg, rc_stats_fallback_dg, rc_stats_native_dgx, rc_stats_fallback_dgx;
static rc_program_t progs[2];
const rc_program_t *rc_program_for(int edition) {
    progs[0].table = rc_chunk_table_dg;  progs[0].native = rc_stats_native_dg;  progs[0].fallback = rc_stats_fallback_dg;
    progs[1].table = rc_chunk_table_dgx; progs[1].native = rc_stats_native_dgx; progs[1].fallback = rc_stats_fallback_dgx;
    return edition == ED_ORIGINAL ? &progs[0] : edition == ED_EXTRA ? &progs[1] : 0;
}
