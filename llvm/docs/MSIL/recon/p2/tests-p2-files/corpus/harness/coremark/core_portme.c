/* CoreMark port layer for the tests-p2 recon (see core_portme.h). */
#include "coremark.h"
#include "core_portme.h"
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;
static CORETIMETYPE start_time_val, stop_time_val;
void start_time(void) { start_time_val = 0; }
void stop_time(void) { stop_time_val = 10000; }
CORE_TICKS get_time(void) { return (CORE_TICKS)(stop_time_val - start_time_val); }
secs_ret time_in_secs(CORE_TICKS ticks) { return (secs_ret)ticks / 1000; }
ee_u32 default_num_contexts = 1;
void portable_init(core_portable *p, int *argc, char *argv[]) {
  (void)argc;
  (void)argv;
  p->portable_id = 1;
}
void portable_fini(core_portable *p) { p->portable_id = 0; }
