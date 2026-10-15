// A module whose only data is zero-initialized (Milestone 8, D12): this test
// pins the BSS-only shape of <Module>::.cctor. There is no initialized global,
// no string literal and none of the msil_test.h print helpers (only
// MSIL_EXPECT), so the data segment has no image: the .cctor has no
// '<msil>image' RVA field and no cpblk, only the AllocHGlobal of the segment
// size plus the alignment slack, the align-up, an initblk that clears the
// whole segment (AllocHGlobal memory is not zeroed) and the address fields.
// The globals have external linkage or are volatile, and noinline functions
// write them, so -O2 keeps every one (an internal global that only main
// touches could become a local). Covered: int, long long, double, pointer,
// struct and char arrays (one of 64 KiB), a _Alignas(64) array (the segment's
// largest alignment, so the align-up matters) and static volatile objects
// (assembly fields). Every object reads zero, the alignment holds, the
// objects do not overlap (each is filled with its own byte pattern, then all
// patterns are checked), and values written by noinline functions are read
// back, also through pointers stored at run time. An object that does not
// read zero returns 101 + its index in FOR_EACH_OBJECT, one whose pattern was
// overwritten 111 + its index, and MSIL_EXPECT 1 + (line % 255) at the first
// other failed check; otherwise the exit status is a checksum of the values
// read back, modulo 256.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

int counter;
long long wide[32];
double reals[8];
int *pointers[4];
struct mixed {
  char c;
  long long ll;
  short s;
} records[3];
unsigned char big[1 << 16];
_Alignas(64) int aligned64[5];
static volatile short v_short;
static volatile int v_ints[3];

static NOINLINE int all_zero(const unsigned char *p, unsigned n) {
  unsigned char acc = 0;
  for (unsigned i = 0; i < n; i++)
    acc |= p[i];
  return acc == 0;
}

static NOINLINE void fill(unsigned char *p, unsigned n, unsigned char v) {
  for (unsigned i = 0; i < n; i++)
    p[i] = (unsigned char)(v + i * 7);
}

static NOINLINE int filled(const unsigned char *p, unsigned n,
                           unsigned char v) {
  for (unsigned i = 0; i < n; i++)
    if (p[i] != (unsigned char)(v + i * 7))
      return 0;
  return 1;
}

// The objects that are checked byte by byte, with the index that names them
// in the exit status; the static volatile ones are checked by typed reads.
#define FOR_EACH_OBJECT(X)                                                     \
  X(0, counter)                                                                \
  X(1, wide)                                                                   \
  X(2, reals)                                                                  \
  X(3, pointers)                                                               \
  X(4, records)                                                                \
  X(5, big)                                                                    \
  X(6, aligned64)

static int check_zero(void) {
#define ZERO(i, x)                                                             \
  if (!all_zero((const unsigned char *)&(x), sizeof(x)))                       \
    return 101 + (i);
  FOR_EACH_OBJECT(ZERO)
#undef ZERO
  MSIL_EXPECT(v_short == 0);
  for (int i = 0; i < 3; i++)
    MSIL_EXPECT(v_ints[i] == 0);
  return 0;
}

static int check_no_overlap(void) {
#define FILL(i, x)                                                             \
  fill((unsigned char *)&(x), sizeof(x), (unsigned char)((i) * 37));
  FOR_EACH_OBJECT(FILL)
#undef FILL
  v_short = -2;
  v_ints[2] = 3;
#define FILLED(i, x)                                                           \
  if (!filled((const unsigned char *)&(x), sizeof(x),                          \
              (unsigned char)((i) * 37)))                                      \
    return 111 + (i);
  FOR_EACH_OBJECT(FILLED)
#undef FILLED
  MSIL_EXPECT(v_short == -2 && v_ints[0] == 0 && v_ints[1] == 0);
  MSIL_EXPECT(v_ints[2] == 3);
  return 0;
}

static NOINLINE void bump(int by) { counter += by; }

static NOINLINE void store(int i, long long v) {
  wide[i] = v;
  reals[i & 7] = (double)v * 0.5;
  records[i % 3].c = (char)i;
  records[i % 3].ll = v;
  records[i % 3].s = (short)-i;
  big[(i * 2053) & 0xffff] = (unsigned char)v;
  aligned64[i % 5] = (int)v;
}

static NOINLINE void point(int i, int *p) { pointers[i] = p; }

// Out of line: inlined, the check would fold, because the compiler knows the
// alignment of a global.
static NOINLINE int aligned(const void *p, unsigned a) {
  return ((__UINTPTR_TYPE__)p & (a - 1)) == 0;
}

static NOINLINE void bump_volatiles(int by) {
  v_short = (short)(v_short + by);
  v_ints[by % 3] += by;
}

int main(void) {
  int r = check_zero();
  if (r != 0)
    return r;
  MSIL_EXPECT(aligned(aligned64, 64));
  MSIL_EXPECT(aligned(wide, 8));
  r = check_no_overlap();
  if (r != 0)
    return r;

  // Typed values written by the noinline functions and read back.
  counter = 0;
  bump(5);
  bump(7);
  MSIL_EXPECT(counter == 12);
  for (int i = 0; i < 32; i++)
    store(i, 1000ll * i - 7);
  MSIL_EXPECT(wide[31] == 30993 && wide[0] == -7);
  MSIL_EXPECT(reals[7] == 15496.5 && reals[0] == 11996.5);
  MSIL_EXPECT(records[1].c == 31 && records[1].ll == 30993);
  MSIL_EXPECT(records[1].s == -31);
  MSIL_EXPECT(big[(5 * 2053) & 0xffff] == (unsigned char)4993);
  MSIL_EXPECT(aligned64[1] == 30993);
  point(0, &counter);
  point(1, &aligned64[4]);
  point(2, &aligned64[0]);
  point(3, &counter);
  *pointers[0] += 1;
  *pointers[2] = *pointers[1] + 1;
  MSIL_EXPECT(counter == 13 && *pointers[3] == 13);
  MSIL_EXPECT(aligned64[0] == 28994 && aligned64[4] == 28993);
  bump_volatiles(4);
  bump_volatiles(9);
  MSIL_EXPECT(v_short == 11 && v_ints[1] == 4 && v_ints[0] == 9);

  unsigned sum = (unsigned)counter + (unsigned)wide[17] +
                 (unsigned)records[2].ll + (unsigned)big[(30 * 2053) & 0xffff] +
                 (unsigned)aligned64[3] + (unsigned)v_short +
                 (unsigned)v_ints[0] + (unsigned)(reals[3] * 2.0);
  return (int)(sum & 0xff);
}
