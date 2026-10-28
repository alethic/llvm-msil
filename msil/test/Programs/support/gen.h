/* Deterministic test input shared by the exit-corpus drivers: pseudo-text
   from a small word list chosen by a 32-bit LCG (compressible, but not
   trivially), and xorshift bytes. Header-only: each driver gets its own
   static copies. */
#ifndef GEN_H
#define GEN_H
#include <stddef.h>
static size_t gen_text(unsigned char *out, size_t n, unsigned seed) {
  static const char *const words[] = {"the ", "quick ", "brown ", "fox ", "jumps ", "over ",
    "lazy ", "dog ", "msil ", "assembly ", "linker ", "object ", "archive ", "\n", "0123 ", "zz "};
  size_t k = 0;
  unsigned s = seed;
  while (k < n) {
    s = s * 1103515245u + 12345u;
    const char *w = words[(s >> 16) & 15];
    while (*w && k < n) out[k++] = (unsigned char)*w++;
  }
  return k;
}
static void gen_bytes(unsigned char *out, size_t n, unsigned seed) {
  unsigned s = seed;
  for (size_t i = 0; i < n; ++i) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; out[i] = (unsigned char)(s >> 8); }
}
#endif
