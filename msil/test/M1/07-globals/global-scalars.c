// Global scalar variables (Milestone 8, D12): initialized globals of every
// scalar type (char, signed and unsigned char, short, unsigned short, int,
// unsigned, long long, unsigned long long, float, double, _Bool, a pointer)
// with external linkage (public address fields) and with internal linkage
// (assembly fields), zero-initialized ones (the BSS tail), const ones, and
// small globals declared next to wider ones, so a load or store of the wrong
// width or at the wrong offset clobbers a neighbour. The initial values come
// from the data image copied by <Module>::.cctor; dump() prints every global,
// mutate() changes every one with values derived from a volatile global, and
// dump() prints them again, so the image, the address fields and stores into
// the data segment are all observed. Each global is used by two functions,
// so -O2 neither folds the initial values nor turns a global into a local of
// main. Natural alignment of the wider globals and the alignment of
// _Alignas(32) and _Alignas(64) initialized globals are checked too. FP
// values are printed scaled to integers. Exit status 0.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

char g_char = 'A';
signed char g_schar = -100;
unsigned char g_uchar = 200;
short g_short = -12345;
unsigned short g_ushort = 54321;
int g_int = -123456789;
unsigned g_uint = 4000000000u;
long long g_ll = -1234567890123456789ll;
unsigned long long g_ull = 0xfedcba9876543210ull;
float g_float = 1.5f;
double g_double = -2.25;
_Bool g_bool = 1;
int *g_ptr = 0;

// Narrow globals between wider ones.
char g_pad1 = 1;
long long g_after_pad1 = 0x0102030405060708ll;
short g_pad2 = 2;
double g_after_pad2 = 6.5;
unsigned char g_pad3 = 3;

static int s_int = 42;
static long long s_ll = -9;
static double s_double = 0.125;
static unsigned short s_ushort = 65535;
static signed char s_schar = -128;

int z_int;
static long long z_ll;
double z_double;
static char z_char;

const int c_int = 77;
const long long c_ll = -77777777777ll;

// Over-aligned initialized globals: the image itself must be placed at an
// address aligned to the largest alignment.
_Alignas(32) int g_aligned32[3] = {7, 8, 9};
_Alignas(64) static char s_aligned64[3] = "ab";

volatile int one = 1;

static int aligned(const void *p, unsigned a) {
  return ((__UINTPTR_TYPE__)p & (a - 1)) == 0;
}

static NOINLINE void dump(const char *title) {
  put_str(title);
  put_kv("g_char", g_char);
  put_kv("g_schar", g_schar);
  put_kv("g_uchar", g_uchar);
  put_kv("g_short", g_short);
  put_kv("g_ushort", g_ushort);
  put_kv("g_int", g_int);
  put_kv("g_uint", g_uint);
  put_kv("g_ll", g_ll);
  put_hex(g_ull);
  put_kv("g_float*1000", (long long)(g_float * 1000.0f));
  put_kv("g_double*1000", (long long)(g_double * 1000.0));
  put_kv("g_bool", g_bool);
  put_kv("g_ptr_is_null", g_ptr == 0);
  put_kv("g_ptr_target", g_ptr ? *g_ptr : -1);
  put_kv("g_pad1", g_pad1);
  put_hex((unsigned long long)g_after_pad1);
  put_kv("g_pad2", g_pad2);
  put_kv("g_after_pad2*10", (long long)(g_after_pad2 * 10.0));
  put_kv("g_pad3", g_pad3);
  put_kv("s_int", s_int);
  put_kv("s_ll", s_ll);
  put_kv("s_double*1000", (long long)(s_double * 1000.0));
  put_kv("s_ushort", s_ushort);
  put_kv("s_schar", s_schar);
  put_kv("z_int", z_int);
  put_kv("z_ll", z_ll);
  put_kv("z_double*1000", (long long)(z_double * 1000.0));
  put_kv("z_char", z_char);
}

// Every global gets a new value; one is 1 at run time.
static NOINLINE void mutate(void) {
  g_char = (char)(g_char + one);
  g_schar = (signed char)(g_schar - 28 * one);
  g_uchar = (unsigned char)(g_uchar + 100 * one);
  g_short = (short)(g_short * 2 * one);
  g_ushort = (unsigned short)(g_ushort + 20000 * one);
  g_int = g_int / (3 * one);
  g_uint = g_uint + 300000000u * (unsigned)one;
  g_ll = g_ll * -one - 1;
  g_ull = ~g_ull + (unsigned)one;
  g_float = g_float * -4.0f * (float)one;
  g_double = g_double + 0.5 * one;
  g_bool = !g_bool;
  g_ptr = one ? &s_int : &z_int;
  g_pad1 = (char)(-g_pad1 * one);
  g_after_pad1 = g_after_pad1 << (8 * one);
  g_pad2 = (short)(g_pad2 * -1000 * one);
  g_after_pad2 = g_after_pad2 * 3.0 * one;
  g_pad3 = (unsigned char)(g_pad3 - 4 * one);
  s_int = s_int + 58 * one;
  s_ll = s_ll * 1000000000000ll * one;
  s_double = s_double * 8.0 * one;
  s_ushort = (unsigned short)(s_ushort + one);
  s_schar = (signed char)(s_schar + 255 * one);
  z_int = -5 * one;
  z_ll = 0x7fffffffffffffffll * one;
  z_double = 1e-3 * one;
  z_char = (char)(90 * one);
}

int main(void) {
  dump("initial");
  mutate();
  dump("mutated");
  // s_int was 42 + 58 = 100; g_ptr now points to it.
  *g_ptr += one;
  put_kv("s_int via g_ptr", s_int);
  put_kv("const", c_int + (long long)c_ll * one);
  put_kv("aligned_ll", aligned(&g_ll, 8));
  put_kv("aligned_ull", aligned(&g_ull, 8));
  put_kv("aligned_double", aligned(&g_double, 8));
  put_kv("aligned_after_pad1", aligned(&g_after_pad1, 8));
  put_kv("aligned_after_pad2", aligned(&g_after_pad2, 8));
  put_kv("aligned_z_ll", aligned(&z_ll, 8));
  put_kv("aligned_int", aligned(&g_int, 4));
  put_kv("aligned_short", aligned(&g_short, 2));
  put_kv("aligned_ptr", aligned(&g_ptr, sizeof(int *)));
  put_kv("aligned32", aligned(g_aligned32, 32));
  put_kv("aligned64", aligned(s_aligned64, 64));
  put_kv("g_aligned32[2]", g_aligned32[2 * one]);
  put_str(s_aligned64);
  return 0;
}
