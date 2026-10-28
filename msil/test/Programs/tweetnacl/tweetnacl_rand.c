/* Deterministic randombytes for tweetnacl.c, in a separate translation unit
   (tweetnacl.c declares it extern and calls it for every key pair). */
typedef unsigned char u8;
typedef unsigned long long u64;
static unsigned long long state = 0x123456789abcdef1ull;
void randombytes(u8 *p, u64 n) {
  for (u64 i = 0; i < n; ++i) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    p[i] = (u8)(state >> 24);
  }
}
