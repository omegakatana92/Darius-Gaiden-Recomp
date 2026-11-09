#include "core.h"
/* interpreter-only build (no generated code) */
void rc_run(void) { cpu_interp_run(); }
int rc_has(uint32_t pc) { (void)pc; return 0; }
void rc_select_program(void *table) { (void)table; }
const rc_program_t *rc_program_for(int edition) { (void)edition; return 0; }
