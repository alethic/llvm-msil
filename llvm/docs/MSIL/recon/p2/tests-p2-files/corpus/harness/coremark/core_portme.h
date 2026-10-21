/* CoreMark port for the tests-p2 recon (freestanding, single context, static
   memory, no printf: ee_printf expands to nothing, so the result is the exit
   status of coremark_main: the number of CRC mismatches against the known
   values for the seeds below). The timer is fake (10 s), because the run is
   a correctness test, not a benchmark. */
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
#define COMPILER_FLAGS "see the harness"
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
#define MAIN_RETURN_VAL (total_errors != 0 ? (total_errors > 0 ? total_errors : 255) : 0)
extern ee_u32 default_num_contexts;
typedef struct CORE_PORTABLE_S {
  ee_u8 portable_id;
} core_portable;
void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);
#define PERFORMANCE_RUN 1
#define ee_printf(...) ((void)0)
#endif
