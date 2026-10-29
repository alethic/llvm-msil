#include <stdint.h>
#define NI __attribute__((noinline))
NI unsigned popc32(uint32_t x) { return __builtin_popcount(x); }
NI unsigned popc64(uint64_t x) { return __builtin_popcountll(x); }
NI unsigned clz32(uint32_t x) { return __builtin_clz(x); }
NI unsigned clz64(uint64_t x) { return __builtin_clzll(x); }
NI unsigned ctz32(uint32_t x) { return __builtin_ctz(x); }
NI unsigned ctz64(uint64_t x) { return __builtin_ctzll(x); }
NI uint32_t bswap32(uint32_t x) { return __builtin_bswap32(x); }
NI uint64_t bswap64(uint64_t x) { return __builtin_bswap64(x); }
NI uint32_t rotl32(uint32_t x, int n) { return __builtin_rotateleft32(x, n); }
NI uint64_t rotl64(uint64_t x, int n) { return __builtin_rotateleft64(x, n); }
NI uint32_t rotl32c(uint32_t x) { return (x << 7) | (x >> 25); }
NI uint32_t fshl32(uint32_t a, uint32_t b, int n) { return __builtin_elementwise_fshl(a, b, (uint32_t)n); }
NI double fmin_(double a, double b) { return __builtin_fmin(a, b); }
NI double fmax_(double a, double b) { return __builtin_fmax(a, b); }
NI double fminimum_(double a, double b) { return __builtin_elementwise_minimum(a, b); }
NI float fminf_(float a, float b) { return __builtin_fminf(a, b); }
NI double round_(double a) { return __builtin_round(a); }
NI double trunc_(double a) { return __builtin_trunc(a); }
NI double floor_(double a) { return __builtin_floor(a); }
NI double rint_(double a) { return __builtin_rint(a); }
NI double nearbyint_(double a) { return __builtin_nearbyint(a); }
NI double roundeven_(double a) { return __builtin_roundeven(a); }
NI long lround_(double a) { return __builtin_lround(a); }
NI double copysign_(double a, double b) { return __builtin_copysign(a, b); }
NI float copysignf_(float a, float b) { return __builtin_copysignf(a, b); }
NI double fabs_(double a) { return __builtin_fabs(a); }
NI double fma_(double a, double b, double c) { return __builtin_fma(a, b, c); }
NI double sqrt_(double a) { return __builtin_sqrt(a); }
NI uint64_t d2u(double d) { uint64_t u; __builtin_memcpy(&u, &d, 8); return u; }
NI double u2d(uint64_t u) { double d; __builtin_memcpy(&d, &u, 8); return d; }
NI uint32_t f2u(float d) { uint32_t u; __builtin_memcpy(&u, &d, 4); return u; }
NI float u2f(uint32_t u) { float d; __builtin_memcpy(&d, &u, 4); return d; }
NI int isnan_(double d) { return __builtin_isnan(d); }
NI int isinf_(double d) { return __builtin_isinf(d); }
NI int signbit_(double d) { return __builtin_signbit(d); }
NI int parity32(uint32_t x) { return __builtin_parity(x); }
NI uint32_t bitrev32(uint32_t x) { return __builtin_bitreverse32(x); }
