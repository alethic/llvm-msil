// By-value copies of a 64-byte struct in shared frame slots (Milestone 12,
// backlog 2.6 and the section 12 row 'M9.a byval amended'): on the SysV
// x86-64 triples (x86_64-unknown-linux-gnu-msil) a struct larger than 16
// bytes is a byval argument, and LowerCall copies it into the caller's frame
// before every call; since M12 the K-th byval argument of every call in a
// function reuses the function's K-th byval slot (MSILFunctionInfo::
// getByvalSlot) instead of a new frame object per call, so a function with
// ten such calls allocates one 64-byte slot, not 640 bytes. Win64
// (x86_64-pc-windows-msvc-msil) passes the struct by reference to a copy that
// clang makes in the caller, so the same program checks the same C semantics
// there. Covered: ten calls in a row with ten different structs and a loop of
// calls, each to a callee that overwrites its copy (and reads it back); one
// call with two byval arguments, including the same struct twice, where the
// callee writes the first and must still see the original second (two slots,
// never one); an argument produced by a call that itself takes the struct by
// value (bump(s) inside take(...) and inside take2(..., ...)); a callee that
// passes its own parameter on by value twice; and recursion, where every level
// passes its modified copy down and checks after the call that its own copy
// is unchanged. After every call the caller checks that its structs kept
// their values. Inputs are derived from argc (1 when run without arguments),
// so -O2 cannot fold the calls. Results are printed; MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; [exit 0] means every check
// passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

// 64 bytes, integers only: MEMORY class on SysV, byval in the IR.
struct S {
  int v[16];
};

static void fill(struct S *s, int base) {
  for (int i = 0; i < 16; i++)
    s->v[i] = base + i * 3;
}

// 0 when s still holds fill(s, base).
static int differs(const struct S *s, int base) {
  for (int i = 0; i < 16; i++)
    if (s->v[i] != base + i * 3)
      return 1;
  return 0;
}

// A 16-bit hash of the contents (unsigned arithmetic: no overflow).
static int weigh(const struct S *s) {
  unsigned h = 0;
  for (int i = 0; i < 16; i++)
    h = h * 7 + (unsigned)s->v[i] * (unsigned)(i + 1);
  return (int)(h & 0xffff);
}

// Weighs its copy, overwrites every element and reads the copy back.
NOINLINE int take(struct S s, int k) {
  int h = weigh(&s);
  for (int i = 0; i < 16; i++)
    s.v[i] = -k - i;
  int back = 0;
  for (int i = 0; i < 16; i++)
    back += s.v[i];
  return h + (back == -16 * k - 120 ? 0 : 100000);
}

// Overwrites a, then weighs b: b must be a separate copy even when the caller
// passed the same struct twice.
NOINLINE int take2(struct S a, struct S b) {
  for (int i = 0; i < 16; i++)
    a.v[i] = 1000 + i;
  return weigh(&b) * 3 + weigh(&a);
}

// Returns its copy with every element incremented (an sret result).
NOINLINE struct S bump(struct S s) {
  for (int i = 0; i < 16; i++)
    s.v[i] += 1;
  return s;
}

// Passes its own parameter on by value twice, then checks it is unchanged.
NOINLINE int forward(struct S s, int base) {
  int r = take(s, 5) + take(s, 6);
  return differs(&s, base) ? -1 : r;
}

// Every level stores minus its depth in its own copy, passes the copy down
// and checks afterwards that the deeper levels did not change it: elements at
// and above the depth hold minus their index, the others their fill() value.
NOINLINE int rec(struct S s, int depth, int base) {
  s.v[depth] = -depth;
  int below = depth > 0 ? rec(s, depth - 1, base) : 0;
  if (below < 0)
    return -1;
  for (int i = 0; i < 16; i++)
    if (s.v[i] != (i >= depth ? -i : base + i * 3))
      return -1;
  return below + weigh(&s) % 101;
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;
  struct S a[10];
  for (int i = 0; i < 10; i++)
    fill(&a[i], (i + 1) * 100 * one);

  // Ten calls in a row, each with a different struct.
  int r = 0;
  r += take(a[0], 1);
  r += take(a[1], 2);
  r += take(a[2], 3);
  r += take(a[3], 4);
  r += take(a[4], 5);
  r += take(a[5], 6);
  r += take(a[6], 7);
  r += take(a[7], 8);
  r += take(a[8], 9);
  r += take(a[9], 10);
  put_kv("ten calls", r);
  int sum = 0;
  for (int i = 0; i < 10; i++) {
    MSIL_EXPECT(!differs(&a[i], (i + 1) * 100 * one));
    sum += weigh(&a[i]);
  }
  MSIL_EXPECT(r == sum);

  // The same calls in a loop.
  int l = 0;
  for (int i = 0; i < 10; i++)
    l += take(a[i], i + 1);
  put_kv("loop", l);
  MSIL_EXPECT(l == r);

  // Two byval arguments in one call: different structs, then the same one.
  // take2 overwrites its first copy with w's contents, so a shared copy
  // would make it weigh w twice.
  struct S w;
  for (int i = 0; i < 16; i++)
    w.v[i] = 1000 + i;
  int t2 = take2(a[0], a[1]);
  put_kv("take2(a0, a1)", t2);
  MSIL_EXPECT(t2 == weigh(&a[1]) * 3 + weigh(&w));
  int t2same = take2(a[2], a[2]);
  put_kv("take2(a2, a2)", t2same);
  MSIL_EXPECT(t2same == weigh(&a[2]) * 3 + weigh(&w));
  MSIL_EXPECT(!differs(&a[0], 100 * one) && !differs(&a[1], 200 * one));
  MSIL_EXPECT(!differs(&a[2], 300 * one) && !differs(&a[3], 400 * one));

  // Arguments produced by calls that take the struct by value.
  int nb = take(bump(a[4]), 11);
  put_kv("take(bump(a4))", nb);
  int nb2 = take2(bump(a[5]), bump(bump(a[6])));
  put_kv("take2(bump(a5), bump(bump(a6)))", nb2);
  struct S b4 = bump(a[4]), b5 = bump(a[5]), b6 = bump(bump(a[6]));
  MSIL_EXPECT(b5.v[0] == a[5].v[0] + 1 && b5.v[15] == a[5].v[15] + 1);
  MSIL_EXPECT(b6.v[0] == a[6].v[0] + 2 && b6.v[15] == a[6].v[15] + 2);
  MSIL_EXPECT(nb == weigh(&b4));
  MSIL_EXPECT(nb2 == weigh(&b6) * 3 + weigh(&w));
  MSIL_EXPECT(!differs(&a[4], 500 * one) && !differs(&a[5], 600 * one));
  MSIL_EXPECT(!differs(&a[6], 700 * one));

  // A callee that passes its parameter on.
  int f = forward(a[7], 800 * one);
  put_kv("forward(a7)", f);
  MSIL_EXPECT(f >= 0);
  MSIL_EXPECT(!differs(&a[7], 800 * one));

  // Recursion through 16 levels, each with its own copy.
  int rr = rec(a[8], 15, 900 * one);
  put_kv("rec(a8)", rr);
  MSIL_EXPECT(rr >= 0);
  MSIL_EXPECT(!differs(&a[8], 900 * one));
  return 0;
}
