// Structs returned by value (Milestone 9). SysV x86-64 (linux) returns a
// struct of up to 16 bytes in registers, as one value (i8 .. i64, float,
// double, <2 x float>) or as two ({ i64, i64 }, { double, double },
// { <2 x float>, float }, { i64, double }, ...); a two-value result does not
// fit one CIL return value, so LLVM demotes it to a hidden pointer to a
// slot in the caller's frame. Larger structs use clang's sret pointer.
// Win64 returns 1, 2, 4 and 8 byte structs as integers and everything else
// through sret. Covered, for every shape: a result built in the callee, a
// result that is the callee's by-value parameter, a result assigned to the
// object passed as the argument (s = f(s), so the sret slot and the
// argument must not overlap), results passed straight into another call, a
// result that is ignored, a member of a result, results selected by a
// conditional, and mixed member types. Hashes are printed; MSIL_EXPECT
// returns 1 + (line % 255) at the first failed check; [exit 0] means every
// check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

typedef unsigned long long u64;

volatile int zero = 0;
static int made;

#define ARRAY_STRUCT(name, T, N)                                               \
  struct name {                                                                \
    T v[N];                                                                    \
  };                                                                           \
  static u64 hash_##name(struct name s) {                                      \
    u64 h = N;                                                                 \
    for (int i = 0; i < N; i++)                                                \
      h = h * 1000003u + (u64)(long long)(s.v[i] * 4);                         \
    return h;                                                                  \
  }                                                                            \
  NOINLINE struct name make_##name(int seed) {                                 \
    struct name s;                                                             \
    made++;                                                                    \
    for (int i = 0; i < N; i++)                                                \
      s.v[i] = (T)(seed - 2 * i);                                              \
    return s;                                                                  \
  }                                                                            \
  NOINLINE struct name add_##name(struct name a, struct name b) {              \
    for (int i = 0; i < N; i++)                                                \
      a.v[i] = (T)(a.v[i] + b.v[i]);                                           \
    return a;                                                                  \
  }                                                                            \
  NOINLINE struct name rev_##name(struct name s) {                             \
    struct name r;                                                             \
    for (int i = 0; i < N; i++)                                                \
      r.v[i] = s.v[N - 1 - i];                                                 \
    return r;                                                                  \
  }                                                                            \
  static int check_##name(int seed) {                                          \
    struct name a = make_##name(seed + zero);                                  \
    MSIL_EXPECT(a.v[0] == (T)seed && a.v[N - 1] == (T)(seed - 2 * (N - 1)));   \
    struct name b = add_##name(a, make_##name(1));                             \
    MSIL_EXPECT(b.v[0] == (T)(seed + 1) && a.v[0] == (T)seed);                 \
    a = add_##name(a, a);                                                      \
    MSIL_EXPECT(a.v[0] == (T)(2 * seed) &&                                     \
                a.v[N - 1] == (T)(2 * (seed - 2 * (N - 1))));                  \
    a = rev_##name(a);                                                         \
    MSIL_EXPECT(a.v[N - 1] == (T)(2 * seed));                                  \
    b = rev_##name(rev_##name(b));                                             \
    MSIL_EXPECT(b.v[0] == (T)(seed + 1));                                      \
    int before = made;                                                         \
    (void)make_##name(seed);                                                   \
    make_##name(seed + 1);                                                     \
    MSIL_EXPECT(made == before + 2);                                           \
    MSIL_EXPECT(make_##name(seed + 5).v[N - 1] ==                              \
                (T)(seed + 5 - 2 * (N - 1)));                                  \
    struct name c = zero ? make_##name(100) : add_##name(b, b);                \
    put_str(#name);                                                            \
    put_u64(hash_##name(a) * 31u + hash_##name(b) * 7u + hash_##name(c));      \
    return 0;                                                                  \
  }

ARRAY_STRUCT(c1, signed char, 1)
ARRAY_STRUCT(c2, signed char, 2)
ARRAY_STRUCT(c3, signed char, 3)
ARRAY_STRUCT(s3, short, 3)
ARRAY_STRUCT(c7, signed char, 7)
ARRAY_STRUCT(f1, float, 1)
ARRAY_STRUCT(i2, int, 2)
ARRAY_STRUCT(f2, float, 2)
ARRAY_STRUCT(d1, double, 1)
ARRAY_STRUCT(ll1, long long, 1)
ARRAY_STRUCT(c9, signed char, 9)
ARRAY_STRUCT(i3, int, 3)
ARRAY_STRUCT(f3, float, 3)
ARRAY_STRUCT(d2, double, 2)
ARRAY_STRUCT(ll2, long long, 2)
ARRAY_STRUCT(f4, float, 4)
ARRAY_STRUCT(d3, double, 3)
ARRAY_STRUCT(big, int, 25)

struct ld {
  long long a;
  double d;
};
struct di {
  double d;
  int i;
};
struct if2 {
  int i;
  float f;
  float g;
};
struct pc {
  const char *p;
  char c;
};

NOINLINE struct ld make_ld(long long a, double d) {
  struct ld r;
  r.a = a;
  r.d = d;
  return r;
}
NOINLINE struct di swap_di(struct ld x) {
  struct di r;
  r.d = (double)x.a;
  r.i = (int)x.d;
  return r;
}
NOINLINE struct if2 make_if2(int i) {
  struct if2 r;
  r.i = i;
  r.f = (float)i * 0.5f;
  r.g = -(float)i;
  return r;
}
NOINLINE struct pc make_pc(const char *s, int k) {
  struct pc r;
  r.p = s + k;
  r.c = s[k];
  return r;
}

int main(void) {
  int seed = 3;
  MSIL_EXPECT(check_c1(seed++) == 0);
  MSIL_EXPECT(check_c2(seed++) == 0);
  MSIL_EXPECT(check_c3(seed++) == 0);
  MSIL_EXPECT(check_s3(seed++) == 0);
  MSIL_EXPECT(check_c7(seed++) == 0);
  MSIL_EXPECT(check_f1(seed++) == 0);
  MSIL_EXPECT(check_i2(seed++) == 0);
  MSIL_EXPECT(check_f2(seed++) == 0);
  MSIL_EXPECT(check_d1(seed++) == 0);
  MSIL_EXPECT(check_ll1(seed++) == 0);
  MSIL_EXPECT(check_c9(seed++) == 0);
  MSIL_EXPECT(check_i3(seed++) == 0);
  MSIL_EXPECT(check_f3(seed++) == 0);
  MSIL_EXPECT(check_d2(seed++) == 0);
  MSIL_EXPECT(check_ll2(seed++) == 0);
  MSIL_EXPECT(check_f4(seed++) == 0);
  MSIL_EXPECT(check_d3(seed++) == 0);
  MSIL_EXPECT(check_big(seed++) == 0);
  put_kv("made", made);

  struct ld x = make_ld((1LL << 40) + zero, 6.75);
  MSIL_EXPECT(x.a == 1LL << 40 && x.d == 6.75);
  struct di y = swap_di(make_ld(-12 + zero, 2.5));
  MSIL_EXPECT(y.d == -12.0 && y.i == 2);
  put_kv("di", (long long)(swap_di(x).d / 1024.0) + swap_di(x).i);
  struct if2 z = make_if2(9 + zero);
  MSIL_EXPECT(z.i == 9 && z.f == 4.5f && z.g == -9.0f);
  z = make_if2(z.i * 2);
  put_kv("if2", z.i * 100 + (long long)(z.f * 10) + (long long)z.g);
  struct pc w = make_pc("returned", 2 + zero);
  put_str(w.p);
  MSIL_EXPECT(w.c == 't' && make_pc(w.p, 3).c == 'n');
  return 0;
}
