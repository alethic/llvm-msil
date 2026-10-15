// Structs passed by value (Milestone 9): clang lowers them with the native
// ABI of the triple, and the backend must give the callee its own copy.
// SysV x86-64 (linux) passes a struct of up to 16 bytes as one or two
// scalars (i8, i16, i24, i48, i56, i64, float, double, <2 x float>,
// { i64, i32 }, { <2 x float>, float }, { i64, double }, { double, i32 },
// { i64, i8 }, { ptr, i32 }), and a larger one, or one that no longer fits
// in the remaining argument registers, byval: the caller copies it into its
// own frame (M9.a). Win64 passes 1, 2, 4 and 8 byte structs as integers and
// everything else as a pointer to a copy that clang makes in the caller.
// Every callee hashes its parameter, then overwrites it through a pointer
// that escapes (so -O2 keeps the writes), and the caller checks that its own
// object did not change. Also covered: the same object passed twice in one
// call, structs after six integer and eight double arguments, and a 16-byte
// aligned struct whose copy must be 16-byte aligned. Hashes are printed;
// MSIL_EXPECT returns 1 + (line % 255) at the first failed check; [exit 0]
// means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#include <stdint.h>

#define NOINLINE __attribute__((noinline))

typedef unsigned long long u64;

volatile int zero = 0;

// A struct of N elements of type T, with a hash, an overwrite, a by-value
// consumer, a two-argument consumer, and a filler.
#define ARRAY_STRUCT(name, T, N)                                               \
  struct name {                                                                \
    T v[N];                                                                    \
  };                                                                           \
  NOINLINE u64 hash_##name(const struct name *s) {                             \
    u64 h = N;                                                                 \
    for (int i = 0; i < N; i++)                                                \
      h = h * 1000003u + (u64)(long long)(s->v[i] * 4);                        \
    return h;                                                                  \
  }                                                                            \
  NOINLINE void wipe_##name(struct name *s) {                                  \
    for (int i = 0; i < N; i++)                                                \
      s->v[i] = (T)(-1 - i);                                                   \
  }                                                                            \
  NOINLINE u64 take_##name(struct name s) {                                    \
    u64 h = hash_##name(&s);                                                   \
    wipe_##name(&s);                                                           \
    return h * 7u + hash_##name(&s);                                           \
  }                                                                            \
  NOINLINE u64 take2_##name(struct name a, int k, struct name b) {             \
    wipe_##name(&a);                                                           \
    return hash_##name(&a) * (u64)k + hash_##name(&b);                         \
  }                                                                            \
  static void fill_##name(struct name *s, int seed) {                          \
    for (int i = 0; i < N; i++)                                                \
      s->v[i] = (T)(seed + 3 * i + zero);                                      \
  }                                                                            \
  static int check_##name(int seed) {                                          \
    struct name s, wiped;                                                      \
    fill_##name(&s, seed);                                                     \
    u64 before = hash_##name(&s);                                              \
    wiped = s;                                                                 \
    wipe_##name(&wiped);                                                       \
    u64 r = take_##name(s);                                                    \
    MSIL_EXPECT(r == before * 7u + hash_##name(&wiped));                       \
    MSIL_EXPECT(hash_##name(&s) == before);                                    \
    u64 r2 = take2_##name(s, 5, s);                                            \
    MSIL_EXPECT(r2 == hash_##name(&wiped) * 5u + before);                      \
    MSIL_EXPECT(hash_##name(&s) == before);                                    \
    put_str(#name);                                                            \
    put_u64(r);                                                                \
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

// Mixed members: the SysV classes merge per eightbyte.
struct fi {
  float f;
  int i;
};
struct if2 {
  int i;
  float f;
  float g;
};
struct ld {
  long long a;
  double d;
};
struct di {
  double d;
  int i;
};
struct pi {
  const char *p;
  int i;
};
struct nest {
  struct {
    float x;
  } a;
  float y;
  double z;
};
union u {
  int i;
  float f;
  unsigned char b[4];
};

NOINLINE void wipe_int(int *p) { *p = -1; }
NOINLINE void wipe_ll(long long *p) { *p = -1; }
NOINLINE void wipe_float(float *p) { *p = -1.0f; }

NOINLINE u64 take_mixed(struct fi a, struct if2 b, struct ld c, struct di d,
                        struct pi e, struct nest f, union u g) {
  u64 h = (u64)(long long)(a.f * 4) * 3 + (u64)a.i;
  h = h * 31 + (u64)b.i * 5 + (u64)(long long)(b.f * 4) +
      (u64)(long long)(b.g * 8);
  h = h * 31 + (u64)c.a + (u64)(long long)(c.d * 2);
  h = h * 31 + (u64)(long long)(d.d * 2) + (u64)d.i;
  h = h * 31 + (u64)(e.p[0] + e.p[1]) + (u64)e.i;
  h = h * 31 + (u64)(long long)(f.a.x * 4 + f.y * 8 + f.z * 16);
  h = h * 31 + (u64)g.i;
  // Overwrite the copies through escaping pointers.
  wipe_int(&a.i);
  wipe_int(&b.i);
  wipe_ll(&c.a);
  wipe_int(&d.i);
  wipe_int(&e.i);
  wipe_float(&f.a.x);
  wipe_int(&g.i);
  return h + (u64)(a.i + b.i + d.i + e.i + g.i) + (u64)c.a +
         (u64)(long long)f.a.x;
}

// Structs after the argument registers are used up (SysV: six integer and
// eight SSE registers), where small structs also go byval.
NOINLINE u64 late(int a, int b, int c, int d, int e, int f, struct ll2 s,
                  double g1, double g2, double g3, double g4, double g5,
                  double g6, double g7, double g8, struct d2 t, struct i2 u,
                  struct f2 v, int tail) {
  u64 h = (u64)(a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f);
  h = h * 1000003u + hash_ll2(&s);
  h = h * 1000003u + (u64)(long long)(g1 + 2 * g2 + 3 * g3 + 4 * g4 + 5 * g5 +
                                      6 * g6 + 7 * g7 + 8 * g8);
  h = h * 1000003u + hash_d2(&t);
  h = h * 1000003u + hash_i2(&u);
  h = h * 1000003u + hash_f2(&v);
  wipe_ll2(&s);
  wipe_i2(&u);
  return h * 31u + (u64)tail + hash_ll2(&s) + hash_i2(&u);
}

struct a16 {
  _Alignas(16) long long a;
  long long b;
  long long c;
};

NOINLINE int take_a16(struct a16 s, struct a16 *orig) {
  int ok = (uintptr_t)&s % 16 == 0;
  ok &= s.a == orig->a && s.b == orig->b && s.c == orig->c;
  wipe_ll(&s.b);
  wipe_ll(&s.c);
  // The copy changed (read back, so -O2 keeps the writes); the caller's
  // object did not.
  ok &= s.b == -1 && s.c == -1 && s.a == orig->a;
  return ok + 2 * (orig->b != -1) + 4 * (orig->c != -1);
}

int main(void) {
  int seed = 1;
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

  struct fi a;
  a.f = 2.5f;
  a.i = 7 + zero;
  struct if2 b;
  b.i = -3;
  b.f = 0.25f;
  b.g = 1.125f + zero;
  struct ld c;
  c.a = 1LL << 50;
  c.d = -8.5 + zero;
  struct di d;
  d.d = 3.5;
  d.i = 1000 + zero;
  struct pi e;
  e.p = "ok";
  e.i = 12 + zero;
  struct nest f;
  f.a.x = 0.75f;
  f.y = 0.5f + zero;
  f.z = 2.0;
  union u g;
  g.i = 0x01020304 + zero;
  u64 hm = take_mixed(a, b, c, d, e, f, g);
  put_str("mixed");
  put_u64(hm);
  MSIL_EXPECT(a.i == 7 && b.i == -3 && c.a == 1LL << 50 && d.i == 1000);
  MSIL_EXPECT(e.i == 12 && f.a.x == 0.75f && g.i == 0x01020304);
  MSIL_EXPECT(g.b[0] == 4 && g.b[3] == 1); // little endian on every triple

  struct ll2 s;
  fill_ll2(&s, 1 << 20);
  struct d2 t;
  fill_d2(&t, 9);
  struct i2 u;
  fill_i2(&u, -40);
  struct f2 v;
  fill_f2(&v, 6);
  u64 hl = late(1, 2, 3, 4, 5, 6 + zero, s, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5,
                8.5, t, u, v, 99);
  put_str("late");
  put_u64(hl);
  MSIL_EXPECT(s.v[0] == 1 << 20 && s.v[1] == (1 << 20) + 3);
  MSIL_EXPECT(u.v[0] == -40 && u.v[1] == -37);

  struct a16 al;
  al.a = 1 + zero;
  al.b = 2;
  al.c = 3;
  MSIL_EXPECT(take_a16(al, &al) == 7);
  MSIL_EXPECT(al.b == 2 && al.c == 3);
  return 0;
}
