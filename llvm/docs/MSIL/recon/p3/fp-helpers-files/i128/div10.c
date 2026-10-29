// Hand-written equivalent of TargetLowering::expandDIVREMByConstant for u128 / 10 and % 10
// (divide by 2, then by 5 using 2^64 == 1 (mod 5): remainder from the sum of the 64-bit halves,
// quotient by multiplying with the inverse of 5 modulo 2^128).
typedef unsigned __int128 u128;
typedef unsigned long long u64;
u128 div10_dag(u128 x) {
  u128 y = x >> 1;
  u64 lo = (u64)y, hi = (u64)(y >> 64);
  u64 sum = lo + hi;
  sum += sum < lo;
  u64 r = sum % 5;
  const u128 inv5 = ((u128)0xCCCCCCCCCCCCCCCCull << 64) | 0xCCCCCCCCCCCCCCCDull;
  return (y - r) * inv5;
}
u128 div10_inline(u128 x) { return x / 10; }
