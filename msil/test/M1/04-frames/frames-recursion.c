// Frames in recursion (Milestone 7): every call gets its own localloc frame,
// and the frame is released when the method returns. A list linked through a
// local node in each of 101 frames is walked at the bottom, and each level
// checks after the deeper calls that its node is unchanged, counting failures
// in a local of the outermost frame through a pointer passed all the way
// down; a local array per level survives 40 deeper levels; a mutual recursion
// alternates a frame with one address-taken scalar and a frame with a
// 1000-byte array, 200 levels deep; each of 1001 levels stores its result into
// its caller's local; and a function with a 4 KB frame runs 20000 times in a
// loop, which overflows the stack unless every frame is released at return.
// None of the recursions is a tail call that clang -O2 could turn into a loop.
// Inputs are derived from argc (1 when run without arguments). MSIL_EXPECT
// returns 1 + (line % 255) at the first failed check; main returns 0 when
// every check passes.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

struct node {
  int value;
  struct node *next;
};

NOINLINE unsigned fr_list_hash(const struct node *n) {
  unsigned h = 0;
  for (; n; n = n->next)
    h = h * 31u + (unsigned)n->value;
  return h;
}

NOINLINE unsigned fr_hash(const int *a, int n) {
  unsigned h = 0;
  for (int i = 0; i < n; i++)
    h = h * 31u + (unsigned)a[i];
  return h;
}

NOINLINE void fr_flip(unsigned char *p, int n, int stride) {
  for (int i = 0; i < n; i += stride)
    p[i] = (unsigned char)~p[i];
}

NOINLINE void fr_mix(unsigned *p, int n) { *p = *p * 31u + (unsigned)n; }

// The list runs from the deepest node (depth 0) to the outermost one.
static NOINLINE unsigned chain(int depth, struct node *parent, int *bad) {
  struct node n;
  n.value = depth * 7 + 1;
  n.next = parent;
  unsigned r = depth == 0 ? fr_list_hash(&n) : chain(depth - 1, &n, bad);
  *bad += n.value != depth * 7 + 1;
  *bad += n.next != parent;
  return r;
}

// Returns the number of levels whose array changed while deeper levels ran.
static NOINLINE int rec_array(int depth, int seed) {
  int a[16];
  for (int i = 0; i < 16; i++)
    a[i] = seed * 31 + depth * 16 + i;
  unsigned before = fr_hash(a, 16);
  int bad = depth == 0 ? 0 : rec_array(depth - 1, seed);
  return bad + (fr_hash(a, 16) != before) + (a[depth & 15] != seed * 31 +
                                             depth * 16 + (depth & 15));
}

static NOINLINE unsigned pong(int n, unsigned acc);

// A small frame: one address-taken scalar that must survive the deeper calls.
static NOINLINE unsigned ping(int n, unsigned acc) {
  unsigned v = acc * 3u + (unsigned)n;
  fr_mix(&v, n);
  if (n == 0)
    return v;
  unsigned r = pong(n - 1, v);
  return r ^ v;
}

// A large frame: a 1000-byte array written every 100 bytes; bit 31 of the
// result is set when it changed while the deeper calls ran.
static NOINLINE unsigned pong(int n, unsigned acc) {
  unsigned char big[1000];
  for (int i = 0; i < 1000; i += 100)
    big[i] = (unsigned char)(acc + (unsigned)i);
  fr_flip(big, 1000, 100);
  unsigned r = n == 0 ? acc : ping(n - 1, acc + 1);
  fr_flip(big, 1000, 100);
  unsigned bad = 0;
  for (int i = 0; i < 1000; i += 100)
    bad |= big[i] != (unsigned char)(acc + (unsigned)i);
  return r + (bad << 31);
}

// Each level stores into its caller's local the number of levels from there
// down, so the outermost caller's local ends up as n + 1.
static NOINLINE void count_levels(int n, int *in_caller) {
  int below = 0;
  if (n > 0)
    count_levels(n - 1, &below);
  *in_caller = below + 1;
}

// 4 KB of frame per call.
static NOINLINE int big_frame(int seed) {
  unsigned char buf[4096];
  for (int i = 0; i < 4096; i += 512)
    buf[i] = (unsigned char)(seed + i / 512);
  fr_flip(buf, 4096, 512);
  return (unsigned char)~buf[(seed & 7) * 512];
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;

  int bad = 0;
  MSIL_EXPECT(chain(100 * one, 0, &bad) == 0xBC933ADFu);
  MSIL_EXPECT(bad == 0);
  MSIL_EXPECT(chain(0 * one, 0, &bad) == 1);
  MSIL_EXPECT(bad == 0);

  MSIL_EXPECT(rec_array(40 * one, 3 * one) == 0);
  MSIL_EXPECT(rec_array(0 * one, 3 * one) == 0);

  MSIL_EXPECT(ping(199 * one, 5u * one) == 0x560ACEA4u);
  MSIL_EXPECT(ping(0 * one, 5u * one) == 0x1D1u);

  int levels = -1;
  count_levels(1000 * one, &levels);
  MSIL_EXPECT(levels == 1001);

  long long total = 0;
  for (int k = 0; k < 20000 * one; k++)
    total += big_frame(k);
  MSIL_EXPECT(total == 2536544);

  return 0;
}
