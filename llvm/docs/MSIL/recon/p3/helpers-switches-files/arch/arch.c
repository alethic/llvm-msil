#include <immintrin.h>
#include <x86intrin.h>
void f_pause(void) { _mm_pause(); }
unsigned f_crc8(unsigned c, unsigned char v) { return _mm_crc32_u8(c, v); }
unsigned f_crc32(unsigned c, unsigned v) { return _mm_crc32_u32(c, v); }
unsigned long long f_crc64(unsigned long long c, unsigned long long v) { return _mm_crc32_u64(c, v); }
unsigned long long f_rdtsc(void) { return __rdtsc(); }
unsigned long long f_rdtscp(unsigned *a) { return __rdtscp(a); }
void f_mfence(void) { _mm_mfence(); }
void f_lfence(void) { _mm_lfence(); }
void f_sfence(void) { _mm_sfence(); }
void f_prefetch(const char *p) { _mm_prefetch(p, _MM_HINT_T0); }
void f_clflush(const void *p) { _mm_clflush(p); }
int f_popcnt(unsigned x) { return _mm_popcnt_u32(x); }
unsigned f_lzcnt(unsigned x) { return _lzcnt_u32(x); }
unsigned f_tzcnt(unsigned x) { return _tzcnt_u32(x); }
unsigned f_pdep(unsigned a, unsigned m) { return _pdep_u32(a, m); }
unsigned f_bextr(unsigned a, unsigned s, unsigned l) { return _bextr_u32(a, s, l); }
int f_cpu(void) { return __builtin_cpu_supports("sse4.2"); }
void f_cpuinit(void) { __builtin_cpu_init(); }
void f_debugtrap(void) { __builtin_debugtrap(); }
void f_trap(void) { __builtin_trap(); }
unsigned f_rdrand(unsigned *p) { return _rdrand32_step(p); }
__attribute__((target("sse4.2"))) unsigned f_crc_target(unsigned c, unsigned v) { return _mm_crc32_u32(c, v); }
