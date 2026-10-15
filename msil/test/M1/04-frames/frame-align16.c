// 16-byte alignment in frames of 32 bytes or less (Milestone 7, plan D13): a
// constant-size localloc of 32 bytes or less is only 8-byte aligned on .NET
// Framework 4.8 and in .NET 8 tiered code, so a frame that holds an
// _Alignas(16) char array or an __int128 must be over-allocated and aligned
// up even when it is that small. The frame* functions have no parameters, no
// other locals and a single return, so at -O0 and -O2 alike their whole frame
// is the 16 or 32 bytes of the objects named in their comments; each object's
// address is checked with (uintptr_t)&x % 16 == 0 and its contents after the
// stores (an object placed partly outside the frame, or two objects sharing
// bytes, changes them). They run from main, from several stack depths through
// frames of other sizes, and 200 times in a loop. The alignment checks run in
// a callee with external linkage: in the function that owns the object the
// compiler knows its alignment and folds the remainder to 0 at -O2. Each
// function returns a bit mask of its failed checks; MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; main returns 0 when every check
// passes.
// RUN: %{msil_c}
#include <stdint.h>

#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

NOINLINE unsigned f16_misalign(const void *p) {
  return (unsigned)((uintptr_t)p % 16);
}

NOINLINE void f16_fill(unsigned char *p, int n) {
  for (int i = 0; i < n; i++)
    p[i] = (unsigned char)(0x10 + i * 0x11);
}

// The hash of the 16 bytes f16_fill stores is 0x29217F88.
NOINLINE unsigned f16_hash(const unsigned char *p, int n) {
  unsigned h = 0;
  for (int i = 0; i < n; i++)
    h = h * 31u + p[i];
  return h;
}

// Stores (0x0123456789ABCDEF + k) * 2^64 + 0xFEDCBA9876543210.
NOINLINE void f16_fill_wide(__int128 *w, unsigned k) {
  unsigned __int128 v = 0x0123456789ABCDEFull + k;
  *w = (__int128)(v << 64 | 0xFEDCBA9876543210ull);
}

// Bit mask of the failed checks of one __int128 filled with k.
#define WIDE_BAD(w, k, bit)                                                    \
  (((f16_misalign(&(w)) != 0) |                                                \
    ((unsigned long long)((w) >> 64) != 0x0123456789ABCDEFull + (k)) |         \
    ((unsigned long long)(w) != 0xFEDCBA9876543210ull))                        \
   << (bit))

// Bit mask of the failed checks of one 16-byte array.
#define BYTES_BAD(b, bit)                                                      \
  (((f16_misalign(b) != 0) | (f16_hash(b, 16) != 0x29217F88u)) << (bit))

// Frame: _Alignas(16) unsigned char[16] + __int128 = 32 bytes.
static NOINLINE unsigned frame32(void) {
  _Alignas(16) unsigned char bytes[16];
  __int128 wide;
  f16_fill(bytes, 16);
  f16_fill_wide(&wide, 1);
  return BYTES_BAD(bytes, 0) | WIDE_BAD(wide, 1, 1);
}

// Frame: two __int128 = 32 bytes.
static NOINLINE unsigned frame32w(void) {
  __int128 lo;
  __int128 hi;
  f16_fill_wide(&lo, 2);
  f16_fill_wide(&hi, 3);
  return WIDE_BAD(lo, 2, 2) | WIDE_BAD(hi, 3, 3);
}

// Frame: one __int128 = 16 bytes.
static NOINLINE unsigned frame16w(void) {
  __int128 wide;
  f16_fill_wide(&wide, 4);
  return WIDE_BAD(wide, 4, 4);
}

// Frame: _Alignas(16) unsigned char[16] = 16 bytes.
static NOINLINE unsigned frame16b(void) {
  _Alignas(16) unsigned char bytes[16];
  f16_fill(bytes, 16);
  return BYTES_BAD(bytes, 5);
}

static NOINLINE unsigned all_small(void) {
  return frame32() | frame32w() | frame16w() | frame16b();
}

static NOINLINE unsigned at_depth(int depth);

// Frames of other sizes between the levels, so that the small frames are
// allocated at different distances from the start of the stack; bit 6 is set
// when a pad changes while the deeper levels run.
static NOINLINE unsigned via12(int depth) {
  unsigned char pad[12];
  f16_fill(pad, 12);
  unsigned before = f16_hash(pad, 12);
  unsigned r = at_depth(depth);
  return r | (f16_hash(pad, 12) != before) << 6;
}

static NOINLINE unsigned via40(int depth) {
  unsigned char pad[40];
  f16_fill(pad, 40);
  unsigned before = f16_hash(pad, 40);
  unsigned r = at_depth(depth);
  return r | (f16_hash(pad, 40) != before) << 6;
}

static NOINLINE unsigned at_depth(int depth) {
  unsigned r = all_small();
  if (depth > 0)
    r |= depth % 3 == 0 ? at_depth(depth - 1)
         : depth % 3 == 1 ? via12(depth - 1)
                          : via40(depth - 1);
  return r;
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;
  __int128 m;
  f16_fill_wide(&m, 5);

  MSIL_EXPECT(frame32() == 0);
  MSIL_EXPECT(frame32w() == 0);
  MSIL_EXPECT(frame16w() == 0);
  MSIL_EXPECT(frame16b() == 0);
  MSIL_EXPECT(at_depth(12 * one) == 0);
  unsigned r = 0;
  for (int i = 0; i < 200 * one; i++)
    r |= all_small();
  MSIL_EXPECT(r == 0);
  MSIL_EXPECT(WIDE_BAD(m, 5, 0) == 0);
  return 0;
}
