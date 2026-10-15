// Unions (Milestone 9): every member starts at offset 0, so a store to one
// member and a load of another reinterpret the same bytes (C11 6.5.2.3,
// footnote 95; both x86-64 ABIs are little endian with IEEE floats).
// Covered: float and double bit patterns read through integer members,
// integers read byte by byte and as halves, a tagged union evaluated by a
// small expression interpreter, unions passed and returned by value (an
// eightbyte that mixes integer and float members is INTEGER on SysV, a
// 16-byte union is two eightbytes, a 24-byte one is byval; on Win64 8-byte
// unions are integers and larger ones go by reference), a union holding a
// struct with an array, a union member of a struct, and a global union
// with an initializer for its first member. Values are printed;
// MSIL_EXPECT returns 1 + (line % 255) at the first failed check; [exit 0]
// means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

volatile int zero = 0;

union fbits {
  float f;
  unsigned u;
  unsigned char b[4];
};
union dbits {
  double d;
  unsigned long long u;
  unsigned short h[4];
};
union wide {
  long long ll[2];
  double d[2];
  int i[4];
};
union big {
  struct {
    int tag;
    char name[12];
  } s;
  long long words[3];
};

NOINLINE union fbits next_float(union fbits x) {
  x.u += 1;
  return x;
}
NOINLINE union dbits flip_sign(union dbits x) {
  x.u ^= 1ULL << 63;
  return x;
}
NOINLINE union wide swap_halves(union wide w) {
  long long t = w.ll[0];
  w.ll[0] = w.ll[1];
  w.ll[1] = t;
  return w;
}
NOINLINE long long sum_words(union big b) {
  long long s = 0;
  for (int i = 0; i < 3; i++)
    s = s * 3 + (long long)(b.words[i] & 0xffff);
  b.words[0] = 0; // only the copy
  return s;
}

// A tagged union: an expression tree in a static pool.
enum kind { NUM, NEG, ADD, MUL };
struct expr {
  enum kind kind;
  union {
    long long num;
    const struct expr *arg;
    struct {
      const struct expr *lhs, *rhs;
    } bin;
  } u;
};
static struct expr nodes[16];
static int nnodes;

static const struct expr *num(long long v) {
  struct expr *e = &nodes[nnodes++];
  e->kind = NUM;
  e->u.num = v;
  return e;
}
static const struct expr *neg(const struct expr *a) {
  struct expr *e = &nodes[nnodes++];
  e->kind = NEG;
  e->u.arg = a;
  return e;
}
static const struct expr *bin(enum kind k, const struct expr *l,
                              const struct expr *r) {
  struct expr *e = &nodes[nnodes++];
  e->kind = k;
  e->u.bin.lhs = l;
  e->u.bin.rhs = r;
  return e;
}
NOINLINE long long eval(const struct expr *e) {
  switch (e->kind) {
  case NUM:
    return e->u.num;
  case NEG:
    return -eval(e->u.arg);
  case ADD:
    return eval(e->u.bin.lhs) + eval(e->u.bin.rhs);
  case MUL:
    return eval(e->u.bin.lhs) * eval(e->u.bin.rhs);
  }
  return 0;
}

union dbits g_one = {1.0};

int main(void) {
  union fbits f;
  f.f = -1.5f + zero;
  put_hex(f.u);
  MSIL_EXPECT(f.u == 0xbfc00000u && f.b[3] == 0xbf && f.b[0] == 0);
  union fbits g = next_float(f);
  put_hex(g.u);
  MSIL_EXPECT(g.f != f.f && g.f < -1.5f && f.u == 0xbfc00000u);
  f.u = 0x7f800000u + (unsigned)zero; // +infinity
  MSIL_EXPECT(f.f > 3.0e38f && f.f == f.f + 1.0f);

  union dbits d;
  d.d = 0.1 + zero;
  put_hex(d.u);
  put_hex(d.h[3]);
  union dbits e = flip_sign(d);
  MSIL_EXPECT(e.d == -0.1 && d.d == 0.1 && e.h[3] == 0xbfb9);
  put_hex(g_one.u);
  MSIL_EXPECT(g_one.h[3] == 0x3ff0 && g_one.h[0] == 0);

  union wide w;
  w.ll[0] = 1 + zero;
  w.ll[1] = -2;
  union wide sw = swap_halves(w);
  MSIL_EXPECT(sw.ll[0] == -2 && sw.ll[1] == 1 && w.ll[0] == 1);
  MSIL_EXPECT(sw.i[0] == -2 && sw.i[1] == -1 && sw.i[2] == 1 && sw.i[3] == 0);
  w.d[0] = 2.0;
  w.d[1] = -0.5 + zero;
  sw = swap_halves(w);
  MSIL_EXPECT(sw.d[0] == -0.5 && sw.d[1] == 2.0);
  put_hex((unsigned)sw.i[1]);

  union big b;
  b.words[0] = b.words[1] = b.words[2] = 0;
  b.s.tag = 0x1234 + zero;
  for (int i = 0; i < 11; i++)
    b.s.name[i] = (char)('a' + i);
  b.s.name[11] = 0;
  put_str(b.s.name);
  put_kv("sum_words", sum_words(b));
  MSIL_EXPECT((b.words[0] & 0xffffffff) == 0x1234 && b.s.name[0] == 'a');

  // One node per statement: the order of evaluation of call arguments is
  // unspecified (clang evaluates them right to left for windows-msvc).
  const struct expr *six = num(6);
  const struct expr *seven = num(7);
  const struct expr *left = bin(MUL, six, neg(seven));
  const struct expr *huge = num(1LL << 33);
  const struct expr *sum = bin(ADD, huge, num(zero));
  const struct expr *three = neg(neg(num(3)));
  const struct expr *t = bin(ADD, left, bin(MUL, sum, three));
  put_kv("eval", eval(t));
  MSIL_EXPECT(eval(t) == -42 + 3 * (1LL << 33) && nnodes == 12);
  nodes[0].u.num = 2; // the 6
  put_kv("eval", eval(t));
  return 0;
}
