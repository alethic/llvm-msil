// Variable-length arrays outside loops: one- and two-dimensional, of structures, over-aligned, of length 0, several in one function, in nested blocks, passed to callees, sizeof at run time, in recursive functions, and released when their function returns.
// Plan 5.15.8 H0 (M26): a dynamic alloca on no cycle of its function's CFG
// stays an alloca, and code generation turns it into a CIL 'localloc' in the
// middle of the function, with a native-int size (LOCALLOC_I64), after the one
// localloc of the static frame at entry. An alignment above 8 over-allocates
// and masks the address up (the frame model assumes 8-byte localloc alignment,
// plan D13), so the over-aligned arrays below print 1 for 'aligned'. clang -O0
// brackets each VLA scope with llvm.stacksave and llvm.stackrestore; in a
// function without a cyclic alloca they are deleted, because a localloc lives
// until its method returns, as the static frame does. A VLA declared inside a
// loop body is cyclic and an error until M32
// (llvm/test/CodeGen/MSIL/dynamic-alloca-error.ll), so every VLA here is
// outside any loop of its function: loops only fill and read the arrays, and
// each function with a VLA is noinline, so no VLA is inlined into a caller's
// loop. 'released' calls a function with a 64 KiB VLA 2000 times from a loop
// (with an argument that changes, so the call cannot leave the loop): without
// the release at return that would need 128 MiB of stack. 'empty(0)' makes a
// VLA of length 0 (a GNU extension; the length comes from a volatile, so the
// compiler cannot see it) and prints 1 when its address is not null, as it is
// natively. A localloc that the CLR cannot satisfy throws
// StackOverflowException, never wrong data. The program calls only puts
// (msil_test.h), so it is a %{msil_c} test (plan 5.17.13) whose standalone -S
// reference also builds under msil_equiv=1. Plan 5.15.8, 5.17.13 (M26,
// M3/00-frames).
// RUN: %{msil_c}
#include <stddef.h>
#include <stdint.h>

#include "msil_test.h"

struct cell {
  double weight;
  int id;
  char tag;
};

__attribute__((noinline)) static long long squares(int n) {
  int a[n];
  for (int i = 0; i < n; i++)
    a[i] = i * i;
  long long s = 0;
  for (int i = n - 1; i >= 0; i--)
    s += a[i];
  return s;
}

__attribute__((noinline)) static long long matrix(int rows, int cols) {
  int m[rows][cols];
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++)
      m[r][c] = r * 100 + c;
  long long s = 0;
  for (int r = 0; r < rows; r++)
    s += (long long)m[r][r % cols] * (r + 1);
  return s * 1000 + (long long)sizeof(m) + (long long)sizeof(m[0]);
}

__attribute__((noinline)) static long long cells(int n) {
  struct cell v[n];
  for (int i = 0; i < n; i++) {
    v[i].weight = i * 0.5;
    v[i].id = 1000 + i;
    v[i].tag = (char)('a' + i % 26);
  }
  long long s = 0;
  for (int i = 0; i < n; i++)
    s += v[i].id + v[i].tag + (long long)(v[i].weight * 2);
  return s + (long long)sizeof(v);
}

__attribute__((noinline)) static int aligned64(int n) {
  _Alignas(64) char a[n];
  for (int i = 0; i < n; i++)
    a[i] = (char)i;
  int sum = 0;
  for (int i = 0; i < n; i++)
    sum += a[i];
  return (((uintptr_t)a & 63) == 0) * 100000 + sum;
}

__attribute__((noinline)) static int aligned256(int n) {
  char a[n] __attribute__((aligned(256)));
  char b[n + 3];
  for (int i = 0; i < n; i++)
    a[i] = 1;
  for (int i = 0; i < n + 3; i++)
    b[i] = 2;
  int sum = 0;
  for (int i = 0; i < n; i++)
    sum += a[i];
  for (int i = 0; i < n + 3; i++)
    sum += b[i];
  return (((uintptr_t)a & 255) == 0) * 100000 + sum;
}

// Several VLAs of different types in one function: each keeps its own
// values after all of them are written.
__attribute__((noinline)) static long long several(int n) {
  char c[n];
  long long l[n + 1];
  short s[2 * n];
  double d[n / 2 + 1];
  for (int i = 0; i < n; i++)
    c[i] = (char)(i + 1);
  for (int i = 0; i < n + 1; i++)
    l[i] = (long long)i << 33;
  for (int i = 0; i < 2 * n; i++)
    s[i] = (short)(-i);
  for (int i = 0; i < n / 2 + 1; i++)
    d[i] = i + 0.25;
  long long acc = 0;
  for (int i = 0; i < n; i++)
    acc += c[i];
  for (int i = 0; i < n + 1; i++)
    acc += l[i] >> 33;
  for (int i = 0; i < 2 * n; i++)
    acc += s[i];
  for (int i = 0; i < n / 2 + 1; i++)
    acc += (long long)(d[i] * 4);
  return acc;
}

// VLAs in nested blocks, in a branch and after a loop (in a block that is
// not on the loop's cycle), each read after the other blocks ran.
__attribute__((noinline)) static long long nested(int n, int pick) {
  long long acc = 0;
  for (int i = 0; i < n; i++)
    acc += i;
  {
    int outer[n];
    for (int i = 0; i < n; i++)
      outer[i] = i + 7;
    if (pick) {
      long long inner[pick * 3];
      for (int i = 0; i < pick * 3; i++)
        inner[i] = outer[i % n] * 2;
      for (int i = 0; i < pick * 3; i++)
        acc += inner[i];
    } else {
      char other[n + 1];
      for (int i = 0; i <= n; i++)
        other[i] = 3;
      for (int i = 0; i <= n; i++)
        acc += other[i];
    }
    for (int i = 0; i < n; i++)
      acc += outer[i];
  }
  return acc;
}

// A VLA parameter type and a VLA typedef, whose sizes are evaluated at run
// time.
static long long fill_rows(int rows, int cols, int m[rows][cols]) {
  long long s = 0;
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++) {
      m[r][c] = r ^ c;
      s += m[r][c];
    }
  return s;
}

__attribute__((noinline)) static long long param(int rows, int cols) {
  typedef int row_t[cols];
  row_t m[rows];
  long long s = fill_rows(rows, cols, m);
  return s * 10000 + (long long)sizeof(row_t) * 100 + (long long)sizeof(m) / (long long)sizeof(row_t);
}

// Each recursive call has its own VLA, which still holds its values after
// the deeper calls return.
__attribute__((noinline)) static long long recurse(int depth) {
  int a[depth + 1];
  for (int i = 0; i <= depth; i++)
    a[i] = depth * 10 + i;
  long long below = depth ? recurse(depth - 1) : 0;
  long long s = 0;
  for (int i = 0; i <= depth; i++)
    s += a[i];
  return below + s;
}

__attribute__((noinline)) static int big(int n) {
  char a[n];
  a[0] = 1;
  a[n - 1] = 2;
  for (int i = 1; i < n - 1; i++)
    a[i] = 0;
  int sum = 0;
  for (int i = 0; i < n; i++)
    sum += a[i];
  return sum;
}

// A size computed in 64-bit arithmetic (size_t), as LOCALLOC_I64 takes it.
__attribute__((noinline)) static long long wide_size(size_t k) {
  unsigned char a[k * 3 + 1];
  for (size_t i = 0; i < k * 3 + 1; i++)
    a[i] = (unsigned char)i;
  long long s = 0;
  for (size_t i = 0; i < k * 3 + 1; i++)
    s += a[i];
  return s + (long long)sizeof(a);
}

// A VLA of length 0, its length unknown to the compiler: its address is
// stored to and read back from a volatile, so the null test cannot fold.
static volatile int zero_len;
static void *volatile vla_addr;

__attribute__((noinline)) static int empty(int n) {
  char a[n];
  vla_addr = a;
  return vla_addr != NULL;
}

int main(void) {
  put_kv("squares(1)", squares(1));
  put_kv("squares(10)", squares(10));
  put_kv("squares(1000)", squares(1000));
  put_kv("squares(30000)", squares(30000));
  put_kv("matrix(5, 3)", matrix(5, 3));
  put_kv("matrix(17, 40)", matrix(17, 40));
  put_kv("cells(3)", cells(3));
  put_kv("cells(100)", cells(100));
  put_kv("aligned64(5)", aligned64(5));
  put_kv("aligned64(200)", aligned64(200));
  put_kv("aligned256(9)", aligned256(9));
  put_kv("aligned256(1000)", aligned256(1000));
  put_kv("several(1)", several(1));
  put_kv("several(25)", several(25));
  put_kv("nested(4, 0)", nested(4, 0));
  put_kv("nested(6, 5)", nested(6, 5));
  put_kv("param(3, 4)", param(3, 4));
  put_kv("param(9, 13)", param(9, 13));
  put_kv("recurse(0)", recurse(0));
  put_kv("recurse(40)", recurse(40));
  put_kv("big(200000)", big(200000));
  put_kv("wide_size(7)", wide_size(7));
  put_kv("wide_size(4000)", wide_size(4000));
  put_kv("empty(0)", empty(zero_len));
  long long released = 0;
  for (int i = 0; i < 2000; i++)
    released += big(65536 + (i & 7));
  put_kv("released", released);
  return 0;
}
