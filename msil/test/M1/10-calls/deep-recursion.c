// Deep recursion (Milestone 9): 2,500 nested calls, each with its own
// static frame (a localloc at entry, D13) because a local's address
// escapes. The .NET 8 main thread on Windows has about 1.5 MB of stack:
// the review experiment llvm/docs/MSIL/recon/artifacts/review-cil/deep.il
// overflowed near 19,000 frames of an -O0-shaped 16-byte frame, and the
// -O0 build of down() below overflows near 12,000 frames on .NET 8 Tier 0
// (measured with the function-pointer part removed; .NET Framework and the
// optimizing JIT went deeper), so 2,500 keeps more than 4x headroom (a
// native build has 1 MB on Windows and 8 MB on Linux). None of the
// recursions is a tail call that clang could turn into a loop. Covered:
// direct recursion with an address-taken local, mutual recursion between
// two functions, recursion through a function pointer, and a recursive walk
// of a 2,500-node list in a static pool; every level contributes to the
// result, so a lost or repeated level shows. Values are printed;
// MSIL_EXPECT returns 1 + (line % 255) at the first failed check; [exit 0]
// means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define DEPTH 2500

volatile int zero = 0;
static unsigned long long touched;

NOINLINE void touch(long long *p) {
  touched += (unsigned long long)*p;
  *p += 1;
}

NOINLINE long long down(int n) {
  long long local = n * 3LL;
  touch(&local); // local is now 3n + 1
  if (n == 0)
    return local;
  return down(n - 1) * 2 % 1000003 + local;
}

NOINLINE long long pong(int n, long long acc);
NOINLINE long long ping(int n, long long acc) {
  long long local = acc + n;
  touch(&local);
  return n == 0 ? local : pong(n - 1, local % 999983) + 1;
}
NOINLINE long long pong(int n, long long acc) {
  long long local = acc * 3;
  touch(&local);
  return n == 0 ? local : ping(n - 1, local % 999983) - 2;
}

static long long (*volatile self)(int);
NOINLINE long long via_pointer(int n) {
  long long local = n;
  touch(&local);
  return n == 0 ? 0 : self(n - 1) + (local & 15);
}

struct node {
  int value;
  struct node *next;
};
static struct node pool[DEPTH];

NOINLINE long long walk(const struct node *p, int depth) {
  if (!p)
    return depth;
  long long local = p->value;
  touch(&local);
  return walk(p->next, depth + 1) * 7 % 1000000007 + local;
}

int main(void) {
  long long d = down(DEPTH + zero);
  put_kv("down", d);
  MSIL_EXPECT(d == down(DEPTH));

  long long m = ping(DEPTH + zero, 1);
  put_kv("ping-pong", m);

  self = via_pointer;
  long long v = self(DEPTH + zero);
  put_kv("via pointer", v);

  for (int i = 0; i < DEPTH; i++) {
    pool[i].value = (i * 37 + zero) % 101;
    pool[i].next = i + 1 < DEPTH ? &pool[i + 1] : 0;
  }
  long long w = walk(&pool[0], 0);
  put_kv("walk", w);

  put_kv("touched", (long long)(touched % 1000000007));
  return 0;
}
