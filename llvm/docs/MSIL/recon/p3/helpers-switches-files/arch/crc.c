// Scalar fallback for llvm.x86.sse42.crc32.32.32 (CRC32C, reflected 0x82F63B78, no inversion),
// as a helper archive member would implement it; checks against Sse42.Crc32's a64de79c.
#include <stdint.h>
int puts(const char *);
__attribute__((import_module("kernel32.dll"), import_name("QueryPerformanceCounter"))) int qpc(int64_t *);
__attribute__((import_module("kernel32.dll"), import_name("QueryPerformanceFrequency"))) int qpf(int64_t *);
static uint32_t T[256];
static void init(void) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0x82F63B78u : c >> 1; T[i] = c; } }
__attribute__((noinline)) uint32_t crc32c_u32(uint32_t c, uint32_t v) {
  c ^= v;
  c = (c >> 8) ^ T[c & 255]; c = (c >> 8) ^ T[c & 255]; c = (c >> 8) ^ T[c & 255]; c = (c >> 8) ^ T[c & 255];
  return c;
}
__attribute__((noinline)) uint32_t crc32c_u32_bitwise(uint32_t c, uint32_t v) {
  c ^= v; for (int k = 0; k < 32; k++) c = (c >> 1) ^ (0x82F63B78u & -(c & 1)); return c;
}
volatile uint32_t vn = 2000000;
static char buf[64];
static void out(const char *n, uint32_t c, int64_t t, int64_t f, uint32_t N) {
  int k = 0; while (*n) buf[k++] = *n++; buf[k++] = ' ';
  for (int i = 7; i >= 0; --i) buf[k++] = "0123456789abcdef"[(c >> (4 * i)) & 15];
  buf[k++] = ' '; uint64_t ps = (uint64_t)t * 1000000ull / (uint64_t)f * 1000000ull / N; char s[24]; int m = 0; do s[m++] = '0' + ps % 10; while (ps /= 10); while (m) buf[k++] = s[--m];
  buf[k++] = ' '; buf[k++] = 'p'; buf[k++] = 's'; buf[k] = 0; puts(buf);
}
int main(void) {
  init(); int64_t a, b, f; qpf(&f);
  for (int r = 0; r < 3; r++) {
    uint32_t N = vn, c = 0; qpc(&a); for (uint32_t i = 0; i < N; i++) c = crc32c_u32(c, i); qpc(&b); out("table", c, b - a, f, N);
    c = 0; qpc(&a); for (uint32_t i = 0; i < N; i++) c = crc32c_u32_bitwise(c, i); qpc(&b); out("bitwise", c, b - a, f, N);
  }
  return 0;
}
