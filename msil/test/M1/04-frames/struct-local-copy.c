// Local structs copied by assignment (Milestone 7): struct assignment and
// initialization from another struct become llvm.memcpy of a constant size
// (cpblk) between frame objects: a 32-byte record with fields of every width,
// a 7-byte struct with alignment 1 (copied to and from odd offsets inside an
// array), a 112-byte struct that nests an array of records, a 600-byte struct,
// a struct holding a pointer to another local, an array of records sorted by
// swapping elements through a temporary, and a copy from a callee's local into
// its caller's frame. After every copy the source is changed and both sides
// are checked, so a copy that is too short, too long or aliased shows up.
// Structs are only passed by pointer (byval and sret arguments are M9) and are
// filled field by field: brace initializers become constant globals (M8).
// The helpers have external linkage so that the optimizer keeps their pointer
// parameters; inputs are derived from argc (1 when run without arguments).
// MSIL_EXPECT returns 1 + (line % 255) at the first failed check; main
// returns 0 when every check passes.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

struct rec {
  signed char tag;
  short s;
  int i;
  long long ll;
  double d;
  unsigned char bytes[5];
}; // size 32, alignment 8

struct odd7 {
  unsigned char b[7];
}; // size 7, alignment 1

struct outer {
  struct rec inner[3];
  int n;
  struct odd7 tail;
}; // size 112

struct big {
  int v[150];
};

struct ref {
  int *p;
  int k;
};

NOINLINE void sc_init(struct rec *r, int seed) {
  r->tag = (signed char)(seed - 100);
  r->s = (short)(seed * 100 - 30000);
  r->i = seed * 100000;
  r->ll = (long long)seed << 40 | (unsigned)seed;
  r->d = seed * 0.25;
  for (int k = 0; k < 5; k++)
    r->bytes[k] = (unsigned char)(seed * 3 + k);
}

NOINLINE unsigned sc_hash(const struct rec *r) {
  unsigned h = (unsigned)r->tag;
  h = h * 31u + (unsigned)r->s;
  h = h * 31u + (unsigned)r->i;
  h = h * 31u + (unsigned)r->ll;
  h = h * 31u + (unsigned)(r->ll >> 32);
  h = h * 31u + (unsigned)(int)(r->d * 4.0);
  for (int k = 0; k < 5; k++)
    h = h * 31u + r->bytes[k];
  return h;
}

// The hash of a record that sc_init(seed) filled.
static NOINLINE unsigned ref_hash(int seed) {
  struct rec r;
  sc_init(&r, seed);
  return sc_hash(&r);
}

NOINLINE void sc_fill7(struct odd7 *o, int seed) {
  for (int k = 0; k < 7; k++)
    o->b[k] = (unsigned char)(seed * 11 + k);
}

NOINLINE unsigned sc_hash7(const struct odd7 *o) {
  unsigned h = 0;
  for (int k = 0; k < 7; k++)
    h = h * 31u + o->b[k];
  return h;
}

NOINLINE void sc_swap(struct rec *x, struct rec *y) {
  struct rec t = *x;
  *x = *y;
  *y = t;
}

// Copies a record built in this frame into the caller's frame.
NOINLINE void sc_copy_out(struct rec *dst, int seed) {
  struct rec local;
  sc_init(&local, seed);
  *dst = local;
  local.i = -1; // the copy must not alias this frame
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;

  // A copy, then a change to the copy: the source keeps its values.
  struct rec a, b;
  sc_init(&a, 7 * one);
  b = a;
  MSIL_EXPECT(sc_hash(&b) == ref_hash(7));
  MSIL_EXPECT(b.tag == -93 && b.s == -29300 && b.i == 700000);
  MSIL_EXPECT(b.ll == 0x70000000007LL && b.d == 1.75 && b.bytes[4] == 25);
  b.i = 12345 * one;
  b.bytes[0] = 0;
  MSIL_EXPECT(sc_hash(&a) == ref_hash(7) && a.i == 700000 && b.i == 12345);

  // Initialization from another struct, and a swap through a temporary.
  struct rec c = a;
  sc_init(&b, 9 * one);
  sc_swap(&b, &c);
  MSIL_EXPECT(sc_hash(&b) == ref_hash(7) && sc_hash(&c) == ref_hash(9));
  MSIL_EXPECT(sc_hash(&a) == ref_hash(7));

  // A struct of 7 bytes with alignment 1, at odd offsets inside an array.
  struct odd7 o7[4];
  for (int k = 0; k < 4; k++)
    sc_fill7(&o7[k], k + one);
  o7[1] = o7[3];
  o7[3 * one] = o7[0];
  struct odd7 lone = o7[2];
  sc_fill7(&o7[2], 100 * one);
  MSIL_EXPECT(sc_hash7(&o7[1]) != sc_hash7(&o7[3]));
  MSIL_EXPECT(o7[1].b[0] == 44 && o7[1].b[6] == 50);
  MSIL_EXPECT(o7[3].b[0] == 11 && o7[3].b[6] == 17);
  MSIL_EXPECT(o7[0].b[3] == 14 && lone.b[0] == 33 && lone.b[6] == 39);
  MSIL_EXPECT(o7[2].b[0] == (unsigned char)1100 && o7[2].b[6] == 82);

  // A nested struct copied whole, then one of its elements.
  struct outer x, y;
  for (int k = 0; k < 3; k++)
    sc_init(&x.inner[k], 20 + k * one);
  x.n = -5 * one;
  sc_fill7(&x.tail, 3 * one);
  y = x;
  sc_init(&x.inner[1], 50 * one);
  x.n = 99;
  x.tail.b[6] = 0;
  MSIL_EXPECT(sc_hash(&y.inner[0]) == ref_hash(20));
  MSIL_EXPECT(sc_hash(&y.inner[1]) == ref_hash(21));
  MSIL_EXPECT(sc_hash(&y.inner[2]) == ref_hash(22));
  MSIL_EXPECT(y.n == -5 && y.tail.b[6] == 39 && y.tail.b[0] == 33);
  y.inner[1] = x.inner[2 * one];
  MSIL_EXPECT(sc_hash(&y.inner[1]) == ref_hash(22));
  MSIL_EXPECT(sc_hash(&x.inner[1]) == ref_hash(50) && x.n == 99);

  // 600 bytes.
  struct big g1, g2;
  for (int k = 0; k < 150; k++)
    g1.v[k] = k * k * one;
  g2 = g1;
  g1.v[149] = 0;
  g1.v[0] = 7;
  long long sum = 0;
  for (int k = 0; k < 150; k++)
    sum += g2.v[k];
  MSIL_EXPECT(sum == 1113775 && g2.v[0] == 0 && g2.v[149] == 22201);

  // A copied pointer still names the original local.
  int target = 10 * one;
  struct ref r1, r2;
  r1.p = &target;
  r1.k = 3;
  r2 = r1;
  *r2.p += r2.k;
  MSIL_EXPECT(target == 13 && r1.p == r2.p);

  // Sort records by i through swaps of whole elements; seeds 0..7 scrambled.
  struct rec arr[8];
  for (int k = 0; k < 8; k++)
    sc_init(&arr[k], (k * 5 + 3) % 8 * one);
  for (int k = 0; k < 8; k++) {
    int m = k;
    for (int j = k + 1; j < 8; j++)
      if (arr[j].i < arr[m].i)
        m = j;
    if (m != k)
      sc_swap(&arr[k], &arr[m]);
  }
  int sorted = 1;
  for (int k = 0; k < 8; k++)
    sorted &= sc_hash(&arr[k]) == ref_hash(k);
  MSIL_EXPECT(sorted);

  // A callee's local copied into this frame.
  struct rec out;
  sc_copy_out(&out, 33 * one);
  MSIL_EXPECT(sc_hash(&out) == ref_hash(33) && out.i == 3300000);

  return 0;
}
