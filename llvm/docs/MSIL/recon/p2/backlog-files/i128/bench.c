int puts(const char *);
typedef unsigned __int128 u128;
static u128 seed = ((u128)0x9E3779B97F4A7C15ull << 64) | 0xD1B54A32D192ED03ull;
static inline u128 next(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
__attribute__((noinline)) u128 udiv(u128 a, u128 b) { return a / b; }
int main(void) {
  u128 acc = 0;
  for (int i = 0; i < 3000000; i++) {
    u128 a = next(), b = next() >> (i % 100);
    if (b == 0) b = 7;
    acc += udiv(a, b);
  }
  char buf[33]; for (int i = 0; i < 32; i++) { unsigned d = (unsigned)(acc >> (124 - 4*i)) & 15; buf[i] = d < 10 ? '0'+d : 'a'+d-10; } buf[32] = 0;
  puts(buf);
  return 0;
}
