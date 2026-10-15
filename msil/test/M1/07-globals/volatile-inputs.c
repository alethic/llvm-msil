// Inputs read from volatile globals (Milestone 8, D12, D24): every read of a
// volatile global is a load with the 'volatile.' prefix through its address
// field, so clang cannot fold the arithmetic at -O2 and the program computes
// at run time what a native build computes. Covered: signed and unsigned
// char, short, int and long long inputs (sign and zero extension of narrow
// volatile loads), division and remainder with negative operands, shifts,
// comparisons, int <-> FP conversions of float and double inputs, a volatile
// array, a volatile struct, volatile stores to output globals read back,
// read-modify-write of volatile globals, and a loop whose bound is read again
// on every iteration. Results are printed; the exit status is a checksum of
// them, modulo 256.
// RUN: %{msil_c}
#include "msil_test.h"

volatile signed char v_schar = -7;
volatile unsigned char v_uchar = 250;
volatile short v_short = -300;
volatile unsigned short v_ushort = 65000;
volatile int v_int = -123456;
volatile unsigned v_uint = 0xfffffff0u;
volatile long long v_ll = -5000000000ll;
volatile unsigned long long v_ull = 0x8000000000000001ull;
volatile float v_float = -0.75f;
volatile double v_double = 2.5;
volatile _Bool v_bool = 1;
volatile int v_arr[5] = {3, 1, 4, 1, 5};
volatile struct {
  char c;
  int i;
  long long ll;
} v_struct = {'q', 17, -18};
volatile int v_limit = 6;

volatile int out_int;
volatile long long out_ll;
volatile double out_double;

static unsigned checksum;

static void report(const char *name, long long v) {
  put_kv(name, v);
  checksum = checksum * 31u + (unsigned)v;
}

int main(void) {
  put_str("narrow loads");
  report("schar", v_schar);
  report("uchar", v_uchar);
  report("short", v_short);
  report("ushort", v_ushort);
  report("schar+uchar", v_schar + v_uchar);
  report("short*ushort", (long long)v_short * v_ushort);
  int b = v_bool; // one access per expression: two unsequenced volatile
                  // reads of one object would be undefined
  report("bool", b + b);

  put_str("int arithmetic");
  report("int/7", v_int / 7);
  report("int%7", v_int % 7);
  report("int>>3", v_int >> 3);
  report("uint/7", v_uint / 7u);
  report("uint%7", v_uint % 7u);
  report("uint>>4", v_uint >> 4);
  report("int<uint", v_int < 0 && v_uint > 0x7fffffffu);
  long long vi = v_int;
  report("int*int", vi * vi);

  put_str("long long arithmetic");
  report("ll/1000", v_ll / 1000);
  report("ll%1000", v_ll % 1000);
  report("ll%-7", v_ll % -7);
  report("ll>>12", v_ll >> 12);
  report("ll+int", v_ll + v_int);
  put_hex(v_ull);
  put_hex(v_ull >> 63);
  put_hex(v_ull * 3);
  put_hex(v_ull / 3);
  report("ull<ll", v_ull > (unsigned long long)v_ll);

  put_str("conversions");
  report("float*1000", (long long)(v_float * 1000.0f));
  report("double*1000", (long long)(v_double * 1000.0));
  report("(int)double", (int)v_double);
  report("(int)-double", (int)-v_double);
  report("(int)float", (int)v_float);
  report("int->double", (long long)((double)v_int * 0.5));
  report("ll->double", (long long)((double)v_ll / 4.0));
  report("uchar->float", (long long)((float)v_uchar * 0.25f));
  report("double<float", v_double < v_float);

  put_str("aggregates");
  long long s = 0;
  for (int i = 0; i < 5; i++)
    s = s * 10 + v_arr[i];
  report("array", s);
  report("struct.c", v_struct.c);
  report("struct.i", v_struct.i);
  report("struct.ll", v_struct.ll);

  put_str("stores");
  out_int = v_int * 2;
  out_ll = v_ll * -3;
  out_double = v_double * v_float;
  report("out_int", out_int);
  report("out_ll", out_ll);
  report("out_double*1000", (long long)(out_double * 1000.0));
  v_arr[2] += 10;
  v_struct.i *= -1;
  v_ull ^= 1;
  report("v_arr[2]", v_arr[2]);
  report("v_struct.i", v_struct.i);
  put_hex(v_ull);

  put_str("loop");
  int n = 0;
  for (int i = 0; i < v_limit; i++) {
    n += i;
    if (i == 2)
      v_limit = 4; // the bound is read again on every iteration
  }
  report("loop", n);
  report("v_limit", v_limit);
  return (int)(checksum & 0xff);
}
