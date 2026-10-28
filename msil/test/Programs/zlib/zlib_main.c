/* zlib 1.3.2 (Z_SOLO) round trips: deflate at levels 1, 6 and 9 with the
   default, filtered, Huffman-only and RLE strategies (12 streams) through
   an arena allocator, inflate each stream back and compare; prints the
   input's Adler-32 and CRC-32, each stream's size and FNV-1a hash, and the
   error count, which is also the exit status (at most 255). */
#include "zlib.h"
#include "sup.h"
#include "gen.h"
static unsigned char in[100000], comp[120000], out[100000];
static voidpf za(voidpf o, uInt n, uInt s) { (void)o; return sup_calloc(n, s); }
static void zf(voidpf o, voidpf p) { (void)o; sup_free(p); }
int main(void) {
  static const int levels[] = {1, 6, 9}, strategies[] = {Z_DEFAULT_STRATEGY, Z_FILTERED, Z_HUFFMAN_ONLY, Z_RLE};
  int errors = 0;
  size_t n = gen_text(in, sizeof in, 7);
  gen_bytes(in + 60000, 4000, 99); /* an incompressible stretch */
  sup_put_u("input", n);
  sup_put_u("adler32", adler32(adler32(0, Z_NULL, 0), in, (uInt)n));
  sup_put_u("crc32", crc32(crc32(0, Z_NULL, 0), in, (uInt)n));
  for (int l = 0; l < 3; ++l)
    for (int s = 0; s < 4; ++s) {
      z_stream d = {0};
      sup_reset();
      d.zalloc = za; d.zfree = zf;
      if (deflateInit2(&d, levels[l], Z_DEFLATED, 15, 8, strategies[s]) != Z_OK) { ++errors; continue; }
      d.next_in = in; d.avail_in = (uInt)n; d.next_out = comp; d.avail_out = sizeof comp;
      if (deflate(&d, Z_FINISH) != Z_STREAM_END) ++errors;
      size_t clen = d.total_out;
      deflateEnd(&d);
      sup_put_u("deflate", clen);
      sup_put_u("  fnv", sup_fnv1a(comp, clen));
      z_stream i = {0};
      i.zalloc = za; i.zfree = zf;
      if (inflateInit2(&i, 15) != Z_OK) { ++errors; continue; }
      i.next_in = comp; i.avail_in = (uInt)clen; i.next_out = out; i.avail_out = sizeof out;
      if (inflate(&i, Z_FINISH) != Z_STREAM_END || i.total_out != n) ++errors;
      inflateEnd(&i);
      for (size_t k = 0; k < n; ++k) if (out[k] != in[k]) { ++errors; break; }
    }
  sup_put_u("errors", errors);
  return errors > 255 ? 255 : errors;
}
