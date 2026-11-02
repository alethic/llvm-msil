// alloca outside loops: memory that lives until its function returns (across blocks), over-aligned alloca, a zero-size alloca, several allocas beside address-taken locals, a buffer handed to callees, and release at return.
// Plan 5.15.8 H0 (M26): __builtin_alloca and __builtin_alloca_with_align with a
// run-time size are dynamic allocas; on no cycle of the function's CFG they
// become a CIL 'localloc' with a native-int size (LOCALLOC_I64) in the middle
// of the function, after the static frame's localloc at entry, and an alignment
// above 8 over-allocates and masks. Unlike a VLA, an alloca made inside a block
// stays valid after the block ends, until the function returns, which a
// localloc does too (in_branch). An alloca in a loop is cyclic and an error
// until M32 (llvm/test/CodeGen/MSIL/dynamic-alloca-error.ll), so every alloca
// here is outside any loop of its function, and each function that calls alloca
// is noinline, so none is inlined into a caller's loop. with_locals has
// address-taken locals in the static frame next to two allocas, and each keeps
// its values. 'released' calls a function that allocates 32 KiB 4000 times from
// a loop (with an argument that changes, so the call cannot leave the loop):
// without the release at return that would need 128 MiB of stack.
// 'empty_aligned(0)' makes an alloca of size 0 at alignment 8 (the size comes
// from a volatile, so the compiler cannot see it), in a function of its own so
// that no other alloca raises its alignment, and prints 1 when the address is
// not null, as it is natively. The program calls only puts (msil_test.h), so it
// is a %{msil_c} test (plan 5.17.13). Plan 5.15.8, 5.17.13 (M26, M3/00-frames).
// RUN: %{msil_c}
#include <stddef.h>
#include <stdint.h>

#include "msil_test.h"

static void fill(unsigned char *p, size_t n, unsigned char seed) {
  for (size_t i = 0; i < n; i++)
    p[i] = (unsigned char)(seed + i * 3);
}

static long long total(const unsigned char *p, size_t n) {
  long long s = 0;
  for (size_t i = 0; i < n; i++)
    s += p[i];
  return s;
}

__attribute__((noinline)) static long long simple(size_t n) {
  unsigned char *p = __builtin_alloca(n);
  fill(p, n, 5);
  return total(p, n);
}

// Allocated in one branch, used after the branch.
__attribute__((noinline)) static long long in_branch(int n, int which) {
  int *p;
  if (which) {
    p = __builtin_alloca((size_t)n * sizeof(int));
    for (int i = 0; i < n; i++)
      p[i] = i * which;
  } else {
    p = __builtin_alloca((size_t)(n + 1) * sizeof(int));
    for (int i = 0; i < n; i++)
      p[i] = -i;
  }
  long long s = 0;
  for (int i = 0; i < n; i++)
    s += p[i];
  return s;
}

__attribute__((noinline)) static long long aligned(size_t n) {
  unsigned char *a = __builtin_alloca_with_align(n, 32 * 8);
  unsigned char *b = __builtin_alloca(n + 1);
  unsigned char *c = __builtin_alloca_with_align(n + 2, 128 * 8);
  fill(a, n, 1);
  fill(b, n + 1, 2);
  fill(c, n + 2, 3);
  long long ok = (((uintptr_t)a & 31) == 0) + (((uintptr_t)c & 127) == 0) * 10;
  return ok * 1000000 + total(a, n) + total(b, n + 1) + total(c, n + 2);
}

// Address-taken locals in the static frame beside two allocas.
static void bump(int *x, long long *y) {
  *x += 3;
  *y *= 2;
}

__attribute__((noinline)) static long long with_locals(int n) {
  int x = n;
  long long y = 7;
  int arr[4] = {1, 2, 3, 4};
  bump(&x, &y);
  long long *p = __builtin_alloca((size_t)n * sizeof(long long));
  short *q = __builtin_alloca((size_t)n * sizeof(short));
  for (int i = 0; i < n; i++) {
    p[i] = (long long)i * 1000000007LL;
    q[i] = (short)(i * 3);
  }
  bump(&x, &y);
  long long s = 0;
  for (int i = 0; i < n; i++)
    s += p[i] / 1000000007LL + q[i];
  return s * 1000 + x + y + arr[0] + arr[1] + arr[2] + arr[3];
}

// One alloca per recursive call; each still holds its values after the
// deeper calls return.
__attribute__((noinline)) static long long recurse(int depth) {
  unsigned char *p = __builtin_alloca((size_t)depth + 16);
  fill(p, (size_t)depth + 16, (unsigned char)depth);
  long long below = depth ? recurse(depth - 1) : 0;
  return below + total(p, (size_t)depth + 16);
}

__attribute__((noinline)) static int chunk(size_t n) {
  unsigned char *p = __builtin_alloca(n);
  p[0] = 1;
  p[n - 1] = 2;
  return p[0] + p[n - 1];
}

// An alloca of size 0 at alignment 8 (64 bits), its size unknown to the
// compiler: its address is stored to and read back from a volatile, so the
// null test cannot fold.
static volatile size_t zero_size;
static void *volatile alloca_addr;

__attribute__((noinline)) static int empty_aligned(size_t n) {
  alloca_addr = __builtin_alloca_with_align(n, 8 * 8);
  return alloca_addr != NULL;
}

int main(void) {
  put_kv("simple(1)", simple(1));
  put_kv("simple(100)", simple(100));
  put_kv("simple(150000)", simple(150000));
  put_kv("in_branch(10, 1)", in_branch(10, 1));
  put_kv("in_branch(10, 0)", in_branch(10, 0));
  put_kv("in_branch(500, 3)", in_branch(500, 3));
  put_kv("aligned(1)", aligned(1));
  put_kv("aligned(77)", aligned(77));
  put_kv("aligned(4096)", aligned(4096));
  put_kv("with_locals(1)", with_locals(1));
  put_kv("with_locals(300)", with_locals(300));
  put_kv("recurse(0)", recurse(0));
  put_kv("recurse(60)", recurse(60));
  put_kv("empty_aligned(0)", empty_aligned(zero_size));
  long long released = 0;
  for (int i = 0; i < 4000; i++)
    released += chunk(32768 + (size_t)(i & 15));
  put_kv("released", released);
  return 0;
}
