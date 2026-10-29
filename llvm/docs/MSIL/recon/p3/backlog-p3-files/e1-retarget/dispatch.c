#include <immintrin.h>
static unsigned soft(unsigned c, unsigned x) { for (int i = 0; i < 32; ++i) c = (c >> 1) ^ (0x82F63B78u & -((c ^ (x >> i)) & 1)); return c; }
__attribute__((target("sse4.2"))) static unsigned hw(unsigned c, unsigned x) { return _mm_crc32_u32(c, x); }
unsigned crc(unsigned c, unsigned x) { if (__builtin_cpu_supports("sse4.2")) return hw(c, x); return soft(c, x); }
int main(void) { return (int)(crc(0, 42) & 0x7f); }
