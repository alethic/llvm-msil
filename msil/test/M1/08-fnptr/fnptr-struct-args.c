// Indirect calls with structs passed and returned by value (Milestone 9):
// the calli signature is built from the lowered call, so it must agree with
// the callee's method signature after clang's ABI lowering, which differs by
// triple. SysV x86-64 (linux) passes structs of up to 16 bytes as one or
// two scalars (i24, i64, <2 x float>, double pairs, { i64, double }) and
// returns them the same way, larger ones byval (copied by the caller) with
// an sret result pointer; Win64 passes 1, 2, 4 and 8 byte structs as
// integers and everything else as a pointer to a caller-made copy. Covered:
// each shape called through a volatile pointer and through a table; a
// callee that changes its by-value parameter (the caller's object must not
// change); several aggregates and scalars mixed in one call; a struct
// result fed straight into another indirect call; and a result assigned to
// the object passed as the argument. MSIL_EXPECT returns 1 + (line % 255)
// at the first failed check; [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

volatile int zero = 0;

struct s3 {
  char a, b, c;
};
struct ii {
  int a, b;
};
struct ff {
  float x, y;
};
struct dd {
  double x, y;
};
struct mix {
  long long a;
  double d;
};
struct big {
  int v[10];
};

NOINLINE struct s3 rot3(struct s3 s, int k) {
  struct s3 r;
  for (int i = 0; i < k % 3; i++) {
    r.a = s.b;
    r.b = s.c;
    r.c = s.a;
    s = r;
  }
  return s;
}
NOINLINE struct ii swap_ii(struct ii p) {
  int t = p.a;
  p.a = p.b;
  p.b = t;
  return p;
}
NOINLINE struct ff swap_ff(struct ff p) {
  struct ff r;
  r.x = p.y;
  r.y = p.x;
  return r;
}
NOINLINE struct dd scale_dd(struct dd p, double k) {
  p.x *= k;
  p.y *= k;
  return p;
}
NOINLINE struct mix mix_add(struct mix a, struct mix b) {
  a.a += b.a;
  a.d += b.d;
  return a;
}
NOINLINE struct big big_rev(struct big b) {
  for (int i = 0; i < 5; i++) {
    int t = b.v[i];
    b.v[i] = b.v[9 - i];
    b.v[9 - i] = t;
  }
  return b;
}
NOINLINE long long digest(int bias, struct big b, struct dd d, struct s3 s,
                          struct ff f, long long tail) {
  long long h = bias;
  for (int i = 0; i < 10; i++)
    h = h * 3 + b.v[i];
  h = h * 1000 + (long long)(d.x * 2.0) - (long long)(d.y * 4.0);
  h = h * 100 + s.a - 2 * s.b + 3 * s.c;
  h = h * 100 + (long long)(f.x * 8.0f) - (long long)f.y;
  b.v[0] = -1; // only the callee's copy
  return h * 7 + tail;
}

static long long big_hash(struct big b) {
  long long h = 0;
  for (int i = 0; i < 10; i++)
    h = h * 10 + b.v[i];
  return h;
}

int main(void) {
  struct s3 (*volatile p3)(struct s3, int) = rot3;
  struct ii (*volatile pii)(struct ii) = swap_ii;
  struct ff (*volatile pff)(struct ff) = swap_ff;
  struct dd (*volatile pdd)(struct dd, double) = scale_dd;
  struct mix (*volatile pmix)(struct mix, struct mix) = mix_add;
  struct big (*volatile pbig)(struct big) = big_rev;
  long long (*volatile pdig)(int, struct big, struct dd, struct s3, struct ff,
                             long long) = digest;

  struct s3 s;
  s.a = 'x';
  s.b = 'y';
  s.c = (char)('z' + zero);
  struct s3 r = p3(s, 1);
  MSIL_EXPECT(r.a == 'y' && r.b == 'z' && r.c == 'x');
  r = p3(r, 2 + zero);
  MSIL_EXPECT(r.a == 'x' && r.b == 'y' && r.c == 'z');
  MSIL_EXPECT(s.a == 'x' && s.c == 'z');

  struct ii q;
  q.a = -7 + zero;
  q.b = 1 << 30;
  q = pii(q);
  MSIL_EXPECT(q.a == 1 << 30 && q.b == -7);
  q = pii(pii(q));
  MSIL_EXPECT(q.a == 1 << 30 && q.b == -7);

  struct ff f;
  f.x = 1.5f;
  f.y = -2.25f + zero;
  struct ff g = pff(f);
  MSIL_EXPECT(g.x == -2.25f && g.y == 1.5f && f.x == 1.5f);

  struct dd d;
  d.x = 3.5;
  d.y = -0.75 + zero;
  struct dd e = pdd(d, 4.0);
  MSIL_EXPECT(e.x == 14.0 && e.y == -3.0 && d.x == 3.5 && d.y == -0.75);
  d = pdd(d, -2.0);
  MSIL_EXPECT(d.x == -7.0 && d.y == 1.5);

  struct mix m1, m2;
  m1.a = 1LL << 40;
  m1.d = 0.5;
  m2.a = -(1LL << 39) + zero;
  m2.d = 2.25;
  struct mix m3 = pmix(m1, m2);
  MSIL_EXPECT(m3.a == 1LL << 39 && m3.d == 2.75 && m1.a == 1LL << 40);
  m1 = pmix(m1, m1);
  MSIL_EXPECT(m1.a == 1LL << 41 && m1.d == 1.0);

  struct big b;
  for (int i = 0; i < 10; i++)
    b.v[i] = i + zero;
  struct big rb = pbig(b);
  MSIL_EXPECT(big_hash(b) == 123456789 && big_hash(rb) == 9876543210LL);
  MSIL_EXPECT(big_hash(pbig(pbig(b))) == 123456789);

  long long h1 = pdig(5, b, d, s, f, 11);
  MSIL_EXPECT(h1 == digest(5, b, d, s, f, 11) && b.v[0] == 0);
  put_kv("digest", h1);
  put_kv("digest", pdig(-1, rb, pdd(e, 0.5), p3(s, 1), pff(g), 1LL << 50));

  // The same functions through a table of differently typed pointers.
  struct {
    struct ii (*swap)(struct ii);
    struct dd (*scale)(struct dd, double);
    struct big (*rev)(struct big);
  } table = {swap_ii, scale_dd, big_rev};
  struct ii t = table.swap(q);
  struct dd u = table.scale(e, 0.25);
  struct big w = table.rev(rb);
  put_kv("table ii", (long long)t.a * 1000 + t.b);
  put_kv("table dd", (long long)(u.x * 1000.0) + (long long)(u.y * 1000000.0));
  put_kv("table big", big_hash(w));
  return 0;
}
