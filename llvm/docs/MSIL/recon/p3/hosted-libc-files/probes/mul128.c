#include <stdio.h>
typedef unsigned long long u64; typedef unsigned int u32;
__attribute__((noinline)) static u64 m128(u64 a, u64 b, u64 *lo) { __uint128_t r = (__uint128_t)a * b; *lo = (u64)r; return (u64)(r >> 64); }
__attribute__((noinline)) static u64 m160(u64 a, u32 aLo, u64 b, u32 *pLo) {
  __uint128_t r = (__uint128_t)a * b; r += ((__uint128_t)aLo * b) >> 32; *pLo = (r >> 32) & 0xffffffff; return r >> 64; }
int main(void) {
  u64 x = 0x9E3779B97F4A7C15ull, h = 0, acc = 0; u32 accl = 0;
  for (int i = 0; i < 100000; i++) {
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    u64 y = x * 0xD1B54A32D192ED03ull, lo; u32 l2;
    h ^= m128(x, y, &lo) + (lo >> 1); acc += m160(x, (u32)y, y ^ (y >> 3), &l2); accl ^= l2;
    if (i < 3) printf("%016llx %016llx -> %016llx %016llx\n", x, y, m128(x, y, &lo), lo);
  }
  printf("h=%016llx acc=%016llx accl=%08x\n", h, acc, accl);
  return 0;
}
