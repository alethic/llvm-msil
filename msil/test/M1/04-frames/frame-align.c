// Over-aligned locals (Milestone 7, plan D13): _Alignas(32) and _Alignas(64)
// locals, a struct with a 32-aligned member and a 16-aligned array share one
// frame with 1-, 2- and 8-byte objects; every address is checked with
// (unsigned long long)(uintptr_t)&x % N. The backend assumes only 8-byte
// alignment from localloc, so these frames are over-allocated and their base
// aligned up: each object is then filled with its own byte pattern and checked
// afterwards, so an object that overlaps another or ends outside the frame
// shows up as a changed pattern. A recursion through frames of three sizes
// checks the alignment at many stack depths and that every level's object
// survives the deeper calls, and a frame of 8-aligned objects only (no
// realignment) is checked too. The alignment checks run in callees with
// external linkage: in the function that owns the object the compiler knows
// its alignment and folds the remainder to 0 at -O2. Inputs are derived from
// argc (1 when run without arguments). MSIL_EXPECT returns 1 + (line % 255) at
// the first failed check; main returns 0 when every check passes.
// RUN: %{msil_c}
#include <stdint.h>

#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

NOINLINE unsigned fa_misalign(const void *p, unsigned n) {
  return (unsigned)((unsigned long long)(uintptr_t)p % n);
}

NOINLINE void fa_fill(unsigned char *p, int n, int seed) {
  for (int i = 0; i < n; i++)
    p[i] = (unsigned char)(seed + i * 7);
}

NOINLINE unsigned fa_hash(const unsigned char *p, int n) {
  unsigned h = 0;
  for (int i = 0; i < n; i++)
    h = h * 31u + p[i];
  return h;
}

// The hash fa_fill(p, n, seed) leaves in n <= 64 bytes.
static NOINLINE unsigned ref_hash(int n, int seed) {
  unsigned char ref[64];
  fa_fill(ref, n, seed);
  return fa_hash(ref, n);
}

struct over32 {
  char tag;
  _Alignas(32) int v[3];
  char tail;
}; // alignment 32, size 64

#define FILL(obj, k)                                                           \
  fa_fill((unsigned char *)&(obj), (int)sizeof(obj), seed + (k) * 16)
#define INTACT(obj, k)                                                         \
  (fa_hash((unsigned char *)&(obj), (int)sizeof(obj)) ==                       \
   ref_hash((int)sizeof(obj), seed + (k) * 16))

// Objects of every alignment from 1 to 64 in one frame. Returns the number of
// misaligned objects plus 100 times the number of changed ones.
static NOINLINE int mixed(int seed) {
  char c1;
  _Alignas(32) int x;
  short s;
  _Alignas(32) unsigned char buf[40];
  char c2;
  _Alignas(64) double d[3];
  long long ll;
  _Alignas(16) unsigned char b16[24];
  struct over32 o;
  FILL(c1, 0);
  FILL(x, 1);
  FILL(s, 2);
  FILL(buf, 3);
  FILL(c2, 4);
  FILL(d, 5);
  FILL(ll, 6);
  FILL(b16, 7);
  FILL(o, 8);
  int bad = 0;
  bad += fa_misalign(&x, 32) != 0;
  bad += fa_misalign(buf, 32) != 0;
  bad += fa_misalign(d, 64) != 0;
  bad += fa_misalign(&ll, 8) != 0;
  bad += fa_misalign(&s, 2) != 0;
  bad += fa_misalign(b16, 16) != 0;
  bad += fa_misalign(&o, 32) != 0;
  bad += fa_misalign(o.v, 32) != 0;
  bad += 100 * !INTACT(c1, 0);
  bad += 100 * !INTACT(x, 1);
  bad += 100 * !INTACT(s, 2);
  bad += 100 * !INTACT(buf, 3);
  bad += 100 * !INTACT(c2, 4);
  bad += 100 * !INTACT(d, 5);
  bad += 100 * !INTACT(ll, 6);
  bad += 100 * !INTACT(b16, 7);
  bad += 100 * !INTACT(o, 8);
  return bad;
}

static NOINLINE int aligned_rec(int depth, int seed);

// Frames of other sizes between the levels, so that the over-aligned arrays
// are allocated at different distances from the start of the stack.
static NOINLINE int shim8(int depth, int seed) {
  unsigned char pad[8];
  fa_fill(pad, 8, seed);
  int bad = aligned_rec(depth, seed);
  return bad + 100 * (fa_hash(pad, 8) != ref_hash(8, seed));
}

static NOINLINE int shim40(int depth, int seed) {
  unsigned char pad[40];
  fa_fill(pad, 40, seed);
  int bad = aligned_rec(depth, seed);
  return bad + 100 * (fa_hash(pad, 40) != ref_hash(40, seed));
}

// Each level checks its arrays' alignment, fills them, recurses, and checks
// after the deeper levels have returned that the contents are unchanged.
static NOINLINE int aligned_rec(int depth, int seed) {
  _Alignas(32) unsigned char a[32];
  _Alignas(64) unsigned char b[8];
  int bad = (fa_misalign(a, 32) != 0) + (fa_misalign(b, 64) != 0);
  fa_fill(a, 32, seed + depth);
  fa_fill(b, 8, seed - depth);
  if (depth > 0)
    bad += depth % 3 == 0   ? aligned_rec(depth - 1, seed)
           : depth % 3 == 1 ? shim8(depth - 1, seed)
                            : shim40(depth - 1, seed);
  bad += 100 * (fa_hash(a, 32) != ref_hash(32, seed + depth));
  bad += 100 * (fa_hash(b, 8) != ref_hash(8, seed - depth));
  return bad;
}

// Only 8-byte alignment: no realignment needed.
static NOINLINE int only8(int seed) {
  long long ll;
  double d;
  char c;
  unsigned long long ull;
  FILL(ll, 0);
  FILL(d, 1);
  FILL(c, 2);
  FILL(ull, 3);
  int bad = (fa_misalign(&ll, 8) != 0) + (fa_misalign(&d, 8) != 0) +
            (fa_misalign(&ull, 8) != 0);
  bad += 100 * !INTACT(ll, 0);
  bad += 100 * !INTACT(d, 1);
  bad += 100 * !INTACT(c, 2);
  bad += 100 * !INTACT(ull, 3);
  return bad;
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;
  _Alignas(32) int x = 5 * one;

  MSIL_EXPECT((unsigned long long)(uintptr_t)&x % 32 == 0);
  MSIL_EXPECT(fa_misalign(&x, 32) == 0);
  MSIL_EXPECT(mixed(3 * one) == 0);
  MSIL_EXPECT(mixed(200 * one) == 0);
  MSIL_EXPECT(aligned_rec(0 * one, 1) == 0);
  MSIL_EXPECT(aligned_rec(30 * one, 9 * one) == 0);
  MSIL_EXPECT(only8(77 * one) == 0);
  MSIL_EXPECT(x == 5);
  return 0;
}
