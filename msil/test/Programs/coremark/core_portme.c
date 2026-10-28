/* CoreMark port layer of the exit corpus (see core_portme.h): the seeds and
   ITERATIONS through volatile variables, a fake timer, one context, and the
   result of each run for the driver. */
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
struct portable_result_s portable_result;
void portable_seeds(ee_s32 seed1, ee_s32 seed2, ee_s32 seed3,
                    ee_s32 iterations) {
  seed1_volatile = seed1;
  seed2_volatile = seed2;
  seed3_volatile = seed3;
  seed4_volatile = iterations;
}
void portable_init(core_portable *p, int *argc, char *argv[]) {
  (void)argc;
  (void)argv;
  p->portable_id = 1;
  portable_result.crclist = 0;
  portable_result.crcmatrix = 0;
  portable_result.crcstate = 0;
  portable_result.crcfinal = 0;
  portable_result.errors = -1;
}
void portable_report(core_portable *p, const core_results *results,
                     int total_errors) {
  p->portable_id = 0;
  portable_result.crclist = results[0].crclist;
  portable_result.crcmatrix = results[0].crcmatrix;
  portable_result.crcstate = results[0].crcstate;
  portable_result.crcfinal = results[0].crc;
  portable_result.errors = total_errors;
}
