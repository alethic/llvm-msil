// Zero-initialized globals (Milestone 8, D12): tentative definitions, explicit
// '= 0' and '= {0}' initializers, a 64 KiB char array, int, long long, double
// and pointer arrays, a struct with padding, static (assembly-field) ones, and
// _Alignas(16), _Alignas(32) and _Alignas(64) objects. They form the BSS tail
// that <Module>::.cctor places after the data image and clears with initblk
// (AllocHGlobal memory is not zeroed). Every byte is checked to read zero,
// the required alignments are checked, then every BSS object is filled with a
// pattern and the initialized globals around them are checked to be intact:
// BSS must not overlap the image or another BSS object. The fill pattern is
// read back. MSIL_EXPECT returns 1 + (line % 255) at the first failed check;
// [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))
#define TRY(call)                                                              \
  do {                                                                         \
    int r_ = (call);                                                           \
    if (r_ != 0)                                                               \
      return r_;                                                               \
  } while (0)

int before = 0x11111111;
int zeros[1000];
static long long s_zeros[512];
char big_bss[1 << 16];
int explicit_zero = 0;
double dzeros[100] = {0};
int *pzeros[64];
struct mixed {
  char c;
  long long ll;
  short s;
  double d;
  char tail[13];
} zstruct;
int middle = 0x22222222;
_Alignas(16) char align16[5];
_Alignas(32) static int align32[3];
_Alignas(64) char align64[100];
static char s_byte;
short zshort;
long long after = 0x3333333333333333ll;

volatile int one = 1;

struct object {
  const char *name;
  unsigned char *p;
  unsigned size;
};

#define OBJ(x) {#x, (unsigned char *)&(x), sizeof(x)}
static struct object objects[] = {
    OBJ(zeros),   OBJ(s_zeros), OBJ(big_bss), OBJ(explicit_zero),
    OBJ(dzeros),  OBJ(pzeros),  OBJ(zstruct), OBJ(align16),
    OBJ(align32), OBJ(align64), OBJ(s_byte),  OBJ(zshort),
};

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

static int aligned(const void *p, unsigned a) {
  return ((__UINTPTR_TYPE__)p & (a - 1)) == 0;
}

static int check_initialized(void) {
  MSIL_EXPECT(before == 0x11111111);
  MSIL_EXPECT(middle == 0x22222222);
  MSIL_EXPECT(after == 0x3333333333333333ll);
  return 0;
}

static int check_zero(void) {
  // Typed reads first, then every byte.
  long long sum = 0;
  for (int i = 0; i < COUNT(zeros); i++)
    sum += zeros[i];
  for (int i = 0; i < COUNT(s_zeros); i++)
    sum += s_zeros[i];
  for (int i = 0; i < COUNT(big_bss); i++)
    sum += big_bss[i];
  for (int i = 0; i < COUNT(dzeros); i++)
    MSIL_EXPECT(dzeros[i] == 0.0);
  for (int i = 0; i < COUNT(pzeros); i++)
    MSIL_EXPECT(pzeros[i] == 0);
  MSIL_EXPECT(sum == 0);
  MSIL_EXPECT(explicit_zero == 0 && zshort == 0 && s_byte == 0);
  MSIL_EXPECT(zstruct.c == 0 && zstruct.ll == 0 && zstruct.s == 0);
  MSIL_EXPECT(zstruct.d == 0.0 && zstruct.tail[12] == 0);
  for (int i = 0; i < COUNT(objects); i++)
    MSIL_EXPECT(all_zero(objects[i].p, objects[i].size));
  return 0;
}

int main(void) {
  TRY(check_initialized());
  TRY(check_zero());
  put_str("bss reads zero");

  MSIL_EXPECT(aligned(align16, 16));
  MSIL_EXPECT(aligned(align32, 32));
  MSIL_EXPECT(aligned(align64, 64));
  MSIL_EXPECT(aligned(s_zeros, 8) && aligned(&zstruct, 8));
  MSIL_EXPECT(aligned(dzeros, 8) && aligned(pzeros, sizeof(int *)));
  put_str("alignment ok");

  for (int i = 0; i < COUNT(objects); i++)
    fill(objects[i].p, objects[i].size, (unsigned char)(i * 16 + one));
  TRY(check_initialized());
  for (int i = 0; i < COUNT(objects); i++) {
    if (!filled(objects[i].p, objects[i].size, (unsigned char)(i * 16 + one))) {
      put_str(objects[i].name);
      return 200;
    }
  }
  put_str("fill pattern intact");

  // Typed stores into the cleared objects, read back through their fields.
  zeros[999] = -one;
  s_zeros[511] = 0x123456789abcdefll * one;
  big_bss[(1 << 16) - 1] = (char)(42 * one);
  zstruct.ll = -2 * one;
  zstruct.tail[12] = 'z';
  align64[99] = (char)(64 * one);
  put_kv("zeros[999]", zeros[999]);
  put_hex((unsigned long long)s_zeros[511]);
  put_kv("big_bss[last]", big_bss[(1 << 16) - 1]);
  put_kv("zstruct.ll", zstruct.ll);
  put_kv("zstruct.tail[12]", zstruct.tail[12]);
  put_kv("align64[99]", align64[99]);
  TRY(check_initialized());
  return 0;
}
