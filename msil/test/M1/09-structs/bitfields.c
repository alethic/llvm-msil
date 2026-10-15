// Bit-fields (Milestone 9): reads are a load, shifts and a mask or sign
// extension; writes are read-modify-write of the containing unit. All
// members are 'unsigned int' or 'signed int' bit-fields, whose layout is
// the same in the Itanium (SysV) and Microsoft (Win64) rules, so sizes and
// values agree on every triple. Covered: unsigned fields of widths 1 to 32
// packed into one and two units, unsigned arithmetic that wraps modulo
// 2^width, signed fields read back with sign extension (including the most
// negative value), neighbours unchanged by a write, a zero-width field that
// starts a new unit, bit-fields next to ordinary members, a struct of
// bit-fields passed and returned by value, and bit-fields in a global with
// an initializer (data image). Values are printed; MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; [exit 0] means every check
// passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

volatile int zero = 0;

struct flags {
  unsigned int ready : 1;
  unsigned int mode : 3;
  unsigned int level : 5;
  unsigned int count : 7;
  unsigned int code : 12;
  unsigned int top : 4; // fills the first 32-bit unit exactly
  unsigned int wide : 20;
  unsigned int rest : 12;
};

struct sfields {
  signed int a : 4;
  signed int b : 7;
  signed int c : 13;
  signed int d : 8;
};

struct gap {
  unsigned int lo : 3;
  unsigned int : 0; // the next field starts a new unit
  unsigned int hi : 3;
  int plain;
  unsigned int last : 31;
};

struct packed_rgb {
  unsigned int r : 5;
  unsigned int g : 6;
  unsigned int b : 5;
};

struct flags g_flags = {1, 5, 17, 99, 4000, 9, 1000000, 4095};
struct sfields g_s = {-8, 63, -4096, -1};

static unsigned long long pack(struct flags f) {
  return ((unsigned long long)f.ready << 63) ^
         ((unsigned long long)f.mode << 55) ^
         ((unsigned long long)f.level << 45) ^
         ((unsigned long long)f.count << 35) ^
         ((unsigned long long)f.code << 20) ^
         ((unsigned long long)f.top << 14) ^ ((unsigned long long)f.wide << 3) ^
         (unsigned long long)f.rest;
}

NOINLINE struct packed_rgb blend(struct packed_rgb x, struct packed_rgb y) {
  struct packed_rgb r;
  r.r = (x.r + y.r) / 2;
  r.g = (x.g + y.g) / 2;
  r.b = (x.b + y.b) / 2;
  x.r = 0; // only the copy
  return r;
}

NOINLINE struct sfields negate_all(struct sfields s) {
  s.a = -s.a - 1;
  s.b = -s.b;
  s.c = -s.c - 1;
  s.d = -s.d;
  return s;
}

static unsigned rgb565(struct packed_rgb c) {
  return (unsigned)c.r << 11 | (unsigned)c.g << 5 | c.b;
}

int main(void) {
  put_kv("sizeof flags", sizeof(struct flags));
  put_kv("sizeof sfields", sizeof(struct sfields));
  put_kv("sizeof gap", sizeof(struct gap));
  put_kv("sizeof rgb", sizeof(struct packed_rgb));

  put_hex(pack(g_flags));
  MSIL_EXPECT(g_flags.code == 4000 && g_flags.wide == 1000000);
  MSIL_EXPECT(g_s.a == -8 && g_s.b == 63 && g_s.c == -4096 && g_s.d == -1);

  struct flags f = g_flags;
  f.mode = 7;
  f.count = (unsigned)(zero + 200); // 200 mod 128 = 72
  MSIL_EXPECT(f.count == 72 && f.mode == 7 && f.level == 17 && f.code == 4000);
  f.level += 20; // 37 mod 32 = 5
  f.top--;
  f.wide = f.wide * 3 + zero; // 3000000 mod 2^20
  f.rest = 0;
  f.ready ^= 1;
  put_hex(pack(f));
  MSIL_EXPECT(f.level == 5 && f.top == 8 && f.wide == 3000000 % (1 << 20));
  MSIL_EXPECT(f.ready == 0 && f.rest == 0 && f.code == 4000);
  for (int i = 0; i < 40; i++)
    f.mode++; // 7 + 40 = 47 mod 8 = 7
  MSIL_EXPECT(f.mode == 7 && f.count == 72);
  MSIL_EXPECT(g_flags.mode == 5 && g_flags.ready == 1);

  struct sfields s;
  s.a = zero - 8;
  s.b = zero + 63;
  s.c = zero - 1;
  s.d = zero + 127;
  struct sfields t = negate_all(s);
  put_kv("a", t.a);
  put_kv("b", t.b);
  put_kv("c", t.c);
  put_kv("d", t.d);
  MSIL_EXPECT(s.a == -8 && s.b == 63 && s.c == -1 && s.d == 127);
  MSIL_EXPECT(t.a == 7 && t.b == -63 && t.c == 0 && t.d == -127);
  int sum = 0;
  for (int v = -8; v < 8; v++) {
    s.a = v + zero;
    sum += s.a * (v + 9);
    MSIL_EXPECT(s.a == v && s.b == 63);
  }
  put_kv("sign sum", sum);

  struct gap gp;
  gp.lo = 5 + zero;
  gp.hi = 6;
  gp.plain = -100;
  gp.last = 0x7fffffffu;
  gp.hi++;
  gp.lo += 4; // 9 mod 8 = 1
  MSIL_EXPECT(gp.lo == 1 && gp.hi == 7 && gp.plain == -100);
  MSIL_EXPECT(gp.last == 0x7fffffffu);
  gp.last += 2u; // unsigned arithmetic: wraps to 1 (mod 2^31)
  MSIL_EXPECT(gp.last == 1 && gp.plain == -100);

  struct packed_rgb x, y;
  x.r = 31;
  x.g = 63;
  x.b = (unsigned)zero;
  y.r = 1;
  y.g = (unsigned)(zero + 1);
  y.b = 31;
  struct packed_rgb m = blend(x, y);
  put_hex(rgb565(m));
  MSIL_EXPECT(x.r == 31 && m.r == 16 && m.g == 32 && m.b == 15);
  put_hex(rgb565(blend(m, blend(x, m))));
  return 0;
}
