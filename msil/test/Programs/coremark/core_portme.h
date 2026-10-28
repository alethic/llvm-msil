/* CoreMark port of the exit corpus: freestanding, a single context, static
   memory, no printf (ee_printf expands to nothing) and a fake timer (10 s),
   because the run is a correctness test, not a benchmark.
   coremark.h defines MAIN_RETURN_VAL as 0 after it includes this header, so
   core_main.c's main always returns 0. CoreMark's result reaches the driver
   (coremark_driver.c) through main's last call instead: the macro
   portable_fini below turns portable_fini(&(results[0].port)) into a call of
   portable_report that also passes main's locals results and total_errors,
   CoreMark's error count (the list, matrix and state CRCs that differ from
   the known values of the run's seeds, or -1 for seeds without known values,
   plus the data-type and run-time checks). portable_report records them in
   portable_result. */
#ifndef CORE_PORTME_H
#define CORE_PORTME_H
#include <stddef.h>
#include <stdint.h>
#define HAS_FLOAT 1
#define HAS_TIME_H 0
#define USE_CLOCK 0
#define HAS_STDIO 0
#define HAS_PRINTF 0
#define COMPILER_VERSION "clang"
#define COMPILER_FLAGS "see coremark.test"
#define MEM_LOCATION "STATIC"
typedef signed short ee_s16;
typedef unsigned short ee_u16;
typedef signed int ee_s32;
typedef double ee_f32;
typedef unsigned char ee_u8;
typedef unsigned int ee_u32;
typedef uintptr_t ee_ptr_int; /* not ee_u32: MSIL pointers are 64-bit */
typedef size_t ee_size_t;
#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x)-1) & ~3))
#define CORETIMETYPE ee_u32
typedef ee_u32 CORE_TICKS;
#define SEED_METHOD SEED_VOLATILE
#define MEM_METHOD MEM_STATIC
#define MULTITHREAD 1
#define USE_PTHREAD 0
#define USE_FORK 0
#define USE_SOCKET 0
#define MAIN_HAS_NOARGC 0
#define MAIN_HAS_NORETURN 0
extern ee_u32 default_num_contexts;
typedef struct CORE_PORTABLE_S {
  ee_u8 portable_id;
} core_portable;
void portable_init(core_portable *p, int *argc, char *argv[]);
struct RESULTS_S; /* core_results (coremark.h) */
void portable_report(core_portable *p, const struct RESULTS_S *results,
                     int total_errors);
/* Expanded only in core_main.c's main, where results and total_errors are
   locals. */
#define portable_fini(p) portable_report((p), results, total_errors)
/* What portable_report recorded of the last run: context 0's CRCs and
   CoreMark's error count. portable_init resets it, so errors stays -1 when
   main returns before portable_fini. */
struct portable_result_s {
  ee_u16 crclist, crcmatrix, crcstate, crcfinal;
  int errors;
};
extern struct portable_result_s portable_result;
/* The seeds and the iteration count of the next run (SEED_VOLATILE); until
   a call, those of the performance run: 0, 0, 0x66 and ITERATIONS. */
void portable_seeds(ee_s32 seed1, ee_s32 seed2, ee_s32 seed3,
                    ee_s32 iterations);
#define PERFORMANCE_RUN 1
#define ee_printf(...) ((void)0)
#endif
