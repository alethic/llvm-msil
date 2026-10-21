/* i128 division, remainder and FP<->i128 conversions. Prints with puts only
   (the Phase-1 import table), so values are formatted by hand. */
int puts(const char *);
typedef unsigned __int128 u128;
typedef __int128 i128;

static void put_hex(u128 v) {
  char buf[40];
  int i = 0;
  buf[i++] = '0'; buf[i++] = 'x';
  for (int s = 124; s >= 0; s -= 4) {
    unsigned d = (unsigned)(v >> s) & 15;
    buf[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
  }
  buf[i] = 0;
  puts(buf);
}

static u128 seed = ((u128)0x9E3779B97F4A7C15ull << 64) | 0xD1B54A32D192ED03ull;
static u128 next(void) {
  seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
  return seed;
}

__attribute__((noinline)) u128 udiv(u128 a, u128 b) { return a / b; }
__attribute__((noinline)) u128 urem(u128 a, u128 b) { return a % b; }
__attribute__((noinline)) i128 sdiv(i128 a, i128 b) { return a / b; }
__attribute__((noinline)) i128 srem(i128 a, i128 b) { return a % b; }
__attribute__((noinline)) double u2d(u128 a) { return (double)a; }
__attribute__((noinline)) float i2f(i128 a) { return (float)a; }
__attribute__((noinline)) i128 d2i(double d) { return (i128)d; }
__attribute__((noinline)) u128 f2u(float f) { return (u128)f; }

int main(void) {
  u128 acc = 0;
  for (int i = 0; i < 2000; i++) {
    u128 a = next(), b = next();
    int sh = i % 120;
    b >>= sh;                 /* divisors of every width */
    if (b == 0) b = 3;
    acc = acc * 31 + udiv(a, b);
    acc = acc * 31 + urem(a, b);
    acc = acc * 31 + (u128)sdiv((i128)a, (i128)b);
    acc = acc * 31 + (u128)srem((i128)a, (i128)b);
    acc = acc * 31 + (u128)sdiv((i128)a >> (i % 128), -(i128)(b | 1));
    double d = u2d(a >> (i % 128));
    union { double d; unsigned long long u; } ud = { d };
    acc = acc * 31 + ud.u;
    union { float f; unsigned u; } uf = { i2f((i128)a >> (i % 128)) };
    acc = acc * 31 + uf.u;
    acc = acc * 31 + (u128)d2i(d / 3.0 - 1e20);
    acc = acc * 31 + f2u((float)(i % 97) * 1.5e30f);
  }
  put_hex(acc);
  put_hex(udiv(~(u128)0, 7));
  put_hex((u128)sdiv(-(i128)1000000000000000000ll * 1000000, 7));
  return (int)(acc & 0x7f);
}
