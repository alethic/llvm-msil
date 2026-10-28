/* LZ4 1.10.0 block format (lz4.c) and LZ4 HC (lz4hc.c) round trips with
   caller-provided state: lz4.c at accelerations 1, 2, 4 and 8, lz4hc.c at
   levels 3, 6, 9 and 12, each stream decompressed and compared. The memory
   callbacks of LZ4_USER_MEMORY_FUNCTIONS are defined here over the arena,
   but LZ4_FREESTANDING turns off LZ4's heap use, so nothing calls them.
   Prints each stream's size and FNV-1a hash and the error count, which is
   also the exit status (at most 255). */
#define LZ4_STATIC_LINKING_ONLY
#define LZ4_HC_STATIC_LINKING_ONLY
#include "lz4.h"
#include "lz4hc.h"
#include "sup.h"
#include "gen.h"
static unsigned char in[200000], comp[LZ4_COMPRESSBOUND(200000)], out[200000];
static LZ4_stream_t state;
static LZ4_streamHC_t stateHC;
void *LZ4_malloc(size_t s) { return sup_alloc(s); }
void *LZ4_calloc(size_t n, size_t s) { return sup_calloc(n, s); }
void LZ4_free(void *p) { sup_free(p); }
int main(void) {
  int errors = 0;
  int n = (int)gen_text(in, sizeof in, 11);
  gen_bytes(in + 150000, 20000, 5);
  for (int accel = 1; accel <= 8; accel *= 2) {
    int c = LZ4_compress_fast_extState(&state, (const char *)in, (char *)comp, n, (int)sizeof comp, accel);
    int d = LZ4_decompress_safe((const char *)comp, (char *)out, c, (int)sizeof out);
    sup_put_u("fast", (unsigned)c); sup_put_u("  fnv", sup_fnv1a(comp, (size_t)c));
    if (d != n) ++errors;
    for (int k = 0; k < n; ++k) if (out[k] != in[k]) { ++errors; break; }
  }
  for (int level = 3; level <= 12; level += 3) {
    sup_reset();
    int c = LZ4_compress_HC_extStateHC(&stateHC, (const char *)in, (char *)comp, n, (int)sizeof comp, level);
    int d = LZ4_decompress_safe((const char *)comp, (char *)out, c, (int)sizeof out);
    sup_put_u("hc", (unsigned)c); sup_put_u("  fnv", sup_fnv1a(comp, (size_t)c));
    if (d != n) ++errors;
    for (int k = 0; k < n; ++k) if (out[k] != in[k]) { ++errors; break; }
  }
  sup_put_u("errors", errors);
  return errors > 255 ? 255 : errors;
}
