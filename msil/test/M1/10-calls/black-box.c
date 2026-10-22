// Empty asm statements with operands, the C forms of Rust's black_box and of
// Google Benchmark's DoNotOptimize and ClobberMemory (Milestone 12, backlog
// 2.3 and the section 12 row 'D36 amended'): clang emits them on the MSIL
// triples exactly as on the native ones, and MSILPrepareIR replaces each by
// the D36 compiler fence, an llvm.fake.use of each non-constant input and
// llvm.sideeffect, with every output tied to an input replaced by that input.
// Covered, from backlog bb.c: an "r" input with a "memory" clobber, an "r,m"
// input (one operand, two alternatives), a "+r" read-write output, a "+m"
// read-write memory operand, an output tied to input "0", a "g" input, an "i"
// immediate and the operand-free barrier; a value whose only use is the asm,
// a pointer laundered through a tied output and then written through, two
// tied outputs in one statement, 64-bit and pointer operands, and a sum kept
// through a DoNotOptimize-style asm inside a loop at -O2, where the asm
// hides the sum from the optimizer but must not change it. Also the ties
// whose output and input types differ (section 12 row 'D36 row of 2026-10-21
// (...) clarified'): a _Bool, true and false, through "+r" (an i1 input tied
// to an i8 output) in a call and in main, and Linux's RELOC_HIDE, a pointer
// laundered into an unsigned long long through "=r" tied to "0" and, plus an
// offset, back into a pointer the same way; the addresses are compared as
// integers, never printed. And the outputs tied to "0" that clang emits
// indirect because they also allow memory (section 12 row 'D36 rows of
// 2026-10-21 clarified (review-r2)'): the launder idiom through "=g" into an
// uninitialized int, "=rm" into an int that held another value, and "=rm"
// into a long long tied to an int input (clang zero-extends the input), plus
// "=r,r" tied to "0,0" (a tie written with alternatives); native x86 takes
// the register alternative, so each output holds the input afterwards, never
// its old value. Natively the asm emits nothing, so every value passes
// through unchanged. Inputs are derived from argc (1 when run without
// arguments). Results are printed; MSIL_EXPECT returns 1 + (line % 255) at the
// first failed check; main returns 42 (41 + 1 through the asm statements, as
// bb.c) when every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

static inline void do_not_optimize_in(int v) {
  __asm__ volatile("" : : "r"(v) : "memory");
}
static inline void do_not_optimize_rm(int *p) {
  __asm__ volatile("" : : "r,m"(p) : "memory");
}
static inline void do_not_optimize_inout(int *v) {
  __asm__ volatile("" : "+r"(*v) : : "memory");
}
static inline void do_not_optimize_mem(int *v) {
  __asm__ volatile("" : "+m"(*v) : : "memory");
}
static inline void clobber_memory(void) { __asm__ volatile("" : : : "memory"); }

// Returns v through an output tied to input "0" (not volatile).
static inline int launder(int v) {
  int r;
  __asm__("" : "=r"(r) : "0"(v));
  return r;
}

static inline void *launder_ptr(void *p) {
  __asm__("" : "+r"(p));
  return p;
}

static inline void use_g(int v) { __asm__ volatile("" : : "g"(v)); }

static inline void use_imm(void) { __asm__ volatile("" : : "i"(42)); }

// Two tied outputs of different types in one statement.
NOINLINE long long swap_halves(int a, long long b) {
  __asm__ volatile("" : "+r"(a), "+r"(b));
  return b * 1000 + a;
}

// The sum of i * i for i = 1..n, with the running sum hidden from the
// optimizer in every iteration (the benchmark-loop pattern).
NOINLINE int sum_squares(int n) {
  int sum = 0;
  for (int i = 1; i <= n; i++) {
    sum += i * i;
    do_not_optimize_inout(&sum);
  }
  return sum;
}

// A value whose only use is the asm: computed and dropped.
NOINLINE int unused_product(int a, int b) {
  int p = a * b + 7;
  do_not_optimize_in(p);
  use_g(p ^ a);
  return a + b;
}

// A _Bool through "+r": clang ties its i1 value to an i8 output.
NOINLINE int launder_bool(_Bool b) {
  __asm__ volatile("" : "+r"(b));
  return b;
}

// RELOC_HIDE's two ties: a pointer input "0" tied to an integer output of its
// width, and an integer input "0" tied to a pointer output.
NOINLINE unsigned long long hide_ptr(int *p) {
  unsigned long long u;
  __asm__("" : "=r"(u) : "0"(p));
  return u;
}

NOINLINE int *unhide_ptr(unsigned long long u) {
  int *p;
  __asm__("" : "=r"(p) : "0"(u));
  return p;
}

// Indirect outputs tied to input "0" ("=*imr,0" and "=*rm,0" in the IR): the
// output gets the input, not what its memory held before.
NOINLINE int launder_g(int v) {
  int r;
  __asm__("" : "=g"(r) : "0"(v));
  return r;
}

NOINLINE int tie_rm(int y) {
  int x = 55;
  __asm__ volatile("" : "=rm"(x) : "0"(y));
  return x;
}

NOINLINE long long tie_rm_wide(int y) {
  long long x = 55;
  __asm__ volatile("" : "=rm"(x) : "0"(y));
  return x;
}

// A direct output tied with alternatives ("=r|r,0|0").
NOINLINE int tie_alt(int y) {
  int x = 55;
  __asm__ volatile("" : "=r,r"(x) : "0,0"(y));
  return x;
}

int buffer[4];

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;
  int x = 41 * one, y = 1 * one;
  do_not_optimize_in(x);
  do_not_optimize_rm(&y);
  do_not_optimize_inout(&x);
  do_not_optimize_mem(&y);
  clobber_memory();
  use_imm();
  MSIL_EXPECT(x == 41 && y == 1);

  int t = launder(x * 2);
  put_kv("launder", t);
  MSIL_EXPECT(t == 82);

  // Stores through a laundered pointer are seen through the original array.
  int *p = launder_ptr(&buffer[1]);
  p[0] = 5 * one;
  p[1] = 6 * one;
  do_not_optimize_mem(&buffer[2]);
  put_kv("buffer[1]", buffer[1]);
  put_kv("buffer[2]", buffer[2]);
  MSIL_EXPECT(buffer[1] == 5 && buffer[2] == 6);

  long long s = swap_halves(7 * one, 9 * one);
  put_kv("swap_halves", s);
  MSIL_EXPECT(s == 9007);

  long long wide = 0x123456789abcdefll * one;
  __asm__ volatile("" : "+r"(wide) : : "memory");
  put_hex((unsigned long long)wide);
  MSIL_EXPECT(wide == 0x123456789abcdefll);

  int sq = sum_squares(100 * one);
  put_kv("sum_squares(100)", sq);
  MSIL_EXPECT(sq == 338350);

  int u = unused_product(6 * one, 7 * one);
  put_kv("unused_product", u);
  MSIL_EXPECT(u == 13);

  int bt = launder_bool(one == 1), bf = launder_bool(one == 2);
  _Bool lt = one > 0, lf = one < 0;
  __asm__ volatile("" : "+r"(lt));
  __asm__ volatile("" : "+r"(lf));
  put_kv("launder_bool", bt * 1000 + bf * 100 + lt * 10 + lf);
  MSIL_EXPECT(bt == 1 && bf == 0 && lt == 1 && lf == 0);

  // RELOC_HIDE(&buffer[0], 3 * sizeof(int)), then a store through the result.
  unsigned long long addr = hide_ptr(&buffer[0]);
  MSIL_EXPECT(addr == (unsigned long long)&buffer[0]);
  int *q = unhide_ptr(addr + 3 * sizeof(int));
  MSIL_EXPECT((unsigned long long)q == (unsigned long long)&buffer[3]);
  *q = 8 * one;
  put_kv("buffer[3]", buffer[3]);
  MSIL_EXPECT(buffer[3] == 8);

  int g = launder_g(77 * one), rm = tie_rm(77 * one);
  put_kv("launder_g", g);
  put_kv("tie_rm", rm);
  MSIL_EXPECT(g == 77 && rm == 77);
  long long w = tie_rm_wide(99 * one), wn = tie_rm_wide(-2 * one);
  put_kv("tie_rm_wide", w);
  put_hex((unsigned long long)wn);
  MSIL_EXPECT(w == 99 && wn == 0xfffffffell);
  int alt = tie_alt(-77 * one);
  put_kv("tie_alt", alt);
  MSIL_EXPECT(alt == -77);

  return x + y;
}
