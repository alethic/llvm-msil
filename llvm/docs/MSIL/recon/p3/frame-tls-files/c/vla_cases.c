/* vla_cases.c: dynamic allocas a Phase 3 shadow stack must handle (a proposed transcript test). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <malloc.h>
#define alloca _alloca
#else
#include <alloca.h>
#endif
static volatile int vn = 64;

/* 1. A VLA in a loop: 1,000,000 iterations, the size changing (stacksave/stackrestore per iteration). */
static unsigned vla_loop(void) {
  unsigned s = 0;
  for (int i = 0; i < 1000000; i++) {
    int n = vn + (i % 4000);
    unsigned char b[n];
    b[0] = (unsigned char)i; b[n - 1] = 1;
    s += b[0] + b[n - 1];
  }
  return s;
}
/* 2. alloca in a loop: memory accumulates until return (64 KiB * 40 = 2.5 MiB: more than the
   1.5 MiB CLR main-thread stack under dotnet on Windows, less than a native 8 MiB Linux stack;
   the Windows native build uses /STACK:4194304). */
static unsigned alloca_loop(void) {
  unsigned char *p[40]; unsigned s = 0;
  for (int i = 0; i < 40; i++) { p[i] = alloca(65536); memset(p[i], i, 65536); }
  for (int i = 0; i < 40; i++) s += p[i][i * 1000];
  return s;
}
/* 3. Over-aligned VLA elements. */
typedef struct { _Alignas(64) char c[64]; } A64;
static int vla_align(int n) { A64 a[n]; int ok = 1; for (int i = 0; i < n; i++) ok &= ((uintptr_t)&a[i] % 64) == 0; return ok; }
/* 4. Recursion with a VLA per frame. */
static unsigned vla_rec(int d) { int n = vn; char b[n]; b[0] = (char)d; b[n - 1] = 1; return d ? vla_rec(d - 1) + b[n - 1] : b[0]; }

int main(void) {
  printf("vla_loop %u\n", vla_loop());
  printf("alloca_loop %u\n", alloca_loop());
  printf("vla_align %d\n", vla_align(vn / 8));
  printf("vla_rec %u\n", vla_rec(1000));
  return 0;
}
