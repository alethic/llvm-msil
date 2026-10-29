// Loops whose operation is today's lowering (expansion or libm P/Invoke) or a call
// that patch.py turns into a BCL member call. Timing via QueryPerformanceCounter.
#include <stdint.h>
#define IMP(n) __attribute__((import_module("bcl"), import_name(#n)))
IMP(popc) uint32_t bcl_popc(uint32_t);
IMP(clz) uint32_t bcl_clz(uint32_t);
IMP(rotl) uint32_t bcl_rotl(uint32_t, int);
IMP(bswap) uint32_t bcl_bswap(uint32_t);
IMP(d2u) int64_t bcl_d2u(double);
IMP(fabs) double bcl_fabs(double);
IMP(floor) double bcl_floor(double);
IMP(sqrt) double bcl_sqrt(double);
IMP(sin) double bcl_sin(double);
IMP(fma) double bcl_fma(double, double, double);
__attribute__((import_module("kernel32.dll"), import_name("QueryPerformanceCounter"))) int qpc(int64_t *);
__attribute__((import_module("kernel32.dll"), import_name("QueryPerformanceFrequency"))) int qpf(int64_t *);
int puts(const char *);
double floor(double); double sin(double); double sqrt(double); double fma(double, double, double);

volatile uint32_t vn = 20000000;
#define LOOPU(name, expr) __attribute__((noinline)) uint32_t name(uint32_t n0) { uint32_t n = vn, s = 0; for (uint32_t i = 0; i < n; i++) { uint32_t x = i * 2654435761u | 1; s += (expr); } return s; }
#define LOOPD(name, expr) __attribute__((noinline)) double name(uint32_t n0) { uint32_t n = vn; double s = 0; for (uint32_t i = 0; i < n; i++) { double x = (double)(int)i * 0.37 - 1e6; s += (expr); } return s; }
LOOPU(popc_exp, __builtin_popcount(x))  LOOPU(popc_bcl, bcl_popc(x))
LOOPU(clz_exp, __builtin_clz(x))        LOOPU(clz_bcl, bcl_clz(x))
LOOPU(rotl_exp, __builtin_rotateleft32(x, (int)(i & 31))) LOOPU(rotl_bcl, bcl_rotl(x, (int)(i & 31)))
LOOPU(bswap_exp, __builtin_bswap32(x))  LOOPU(bswap_bcl, bcl_bswap(x))
LOOPD(d2u_exp, ({ uint64_t u; __builtin_memcpy(&u, &x, 8); (double)(u >> 40); })) LOOPD(d2u_bcl, (double)((uint64_t)bcl_d2u(x) >> 40))
LOOPD(fabs_exp, __builtin_fabs(x))      LOOPD(fabs_bcl, bcl_fabs(x))
LOOPD(floor_pinv, floor(x))             LOOPD(floor_bcl, bcl_floor(x))
LOOPD(sqrt_pinv, sqrt(x + 2e6))         LOOPD(sqrt_bcl, bcl_sqrt(x + 2e6))
LOOPD(sin_pinv, sin(x))                 LOOPD(sin_bcl, bcl_sin(x))
LOOPD(fma_pinv, fma(x, 1.0000001, s))   LOOPD(fma_bcl, bcl_fma(x, 1.0000001, s))

static char buf[128];
static void report(const char *name, int64_t ticks, int64_t freq, uint32_t n, uint64_t chk) {
  int k = 0; while (*name) buf[k++] = *name++; buf[k++] = ' ';
  uint64_t ps = (uint64_t)ticks * 1000000000000ull / (uint64_t)freq / n; // picoseconds per iteration
  char t[24]; int m = 0; do t[m++] = '0' + ps % 10; while (ps /= 10); while (m) buf[k++] = t[--m];
  buf[k++] = ' '; buf[k++] = 'p'; buf[k++] = 's'; buf[k++] = ' ';
  for (int i = 15; i >= 0; --i) buf[k++] = "0123456789abcdef"[(chk >> (4 * i)) & 15];
  buf[k] = 0; puts(buf);
}
typedef uint32_t (*fu)(uint32_t); typedef double (*fd)(uint32_t);
static void runu(const char *n, fu f, uint32_t iters) { int64_t a, b, fr, best = -1; uint32_t r = 0; qpf(&fr); for (int k = 0; k < 5; k++) { qpc(&a); r = f(iters); qpc(&b); if (best < 0 || b - a < best) best = b - a; } report(n, best, fr, iters, r); }
static void rund(const char *n, fd f, uint32_t iters) { int64_t a, b, fr, best = -1; double r = 0; qpf(&fr); for (int k = 0; k < 5; k++) { qpc(&a); r = f(iters); qpc(&b); if (best < 0 || b - a < best) best = b - a; } uint64_t u; __builtin_memcpy(&u, &r, 8); report(n, best, fr, iters, u); }
int main(int argc, char **argv) {
  uint32_t N = 20000000; int modern = argc > 1;
  runu("popc_exp", popc_exp, N); if (modern) runu("popc_bcl", popc_bcl, N);
  runu("clz_exp", clz_exp, N); if (modern) runu("clz_bcl", clz_bcl, N);
  runu("rotl_exp", rotl_exp, N); if (modern) runu("rotl_bcl", rotl_bcl, N);
  runu("bswap_exp", bswap_exp, N); if (modern) runu("bswap_bcl", bswap_bcl, N);
  rund("d2u_exp", d2u_exp, N); rund("d2u_bcl", d2u_bcl, N);
  rund("fabs_exp", fabs_exp, N); rund("fabs_bcl", fabs_bcl, N);
  rund("floor_pinv", floor_pinv, N); rund("floor_bcl", floor_bcl, N);
  rund("sqrt_pinv", sqrt_pinv, N); rund("sqrt_bcl", sqrt_bcl, N);
  rund("sin_pinv", sin_pinv, N); rund("sin_bcl", sin_bcl, N);
  rund("fma_pinv", fma_pinv, N); if (modern) rund("fma_bcl", fma_bcl, N);
  return 0;
}
