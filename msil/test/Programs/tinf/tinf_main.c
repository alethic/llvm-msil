/* Two libraries in one program: zlib (Z_SOLO) compresses, tinf decompresses
   the raw deflate and the zlib-wrapped stream; zlib's and tinf's Adler-32
   and CRC-32 must agree. libz.a and libtinf.a both have members named
   adler32.o and crc32.o. Prints each result, size and checksum and the error
   count, which is also the exit status (at most 255). */
#include "zlib.h"
#include "tinf.h"
#include "sup.h"
#include "gen.h"
static unsigned char in[50000], comp[60000], out[50000];
static voidpf za(voidpf o, uInt n, uInt s) { (void)o; return sup_calloc(n, s); }
static void zf(voidpf o, voidpf p) { (void)o; sup_free(p); }
int main(void) {
  int errors = 0;
  size_t n = gen_text(in, sizeof in, 3);
  tinf_init();
  for (int wbits = -15; wbits <= 15; wbits += 30) { /* raw deflate, then zlib wrapper */
    z_stream d = {0};
    sup_reset();
    d.zalloc = za; d.zfree = zf;
    if (deflateInit2(&d, 9, Z_DEFLATED, wbits, 9, Z_DEFAULT_STRATEGY) != Z_OK) { ++errors; continue; }
    d.next_in = in; d.avail_in = (uInt)n; d.next_out = comp; d.avail_out = sizeof comp;
    if (deflate(&d, Z_FINISH) != Z_STREAM_END) ++errors;
    unsigned clen = (unsigned)d.total_out;
    deflateEnd(&d);
    unsigned olen = sizeof out;
    int r = wbits < 0 ? tinf_uncompress(out, &olen, comp, clen) : tinf_zlib_uncompress(out, &olen, comp, clen);
    sup_put_i("tinf result", r);
    sup_put_u("compressed", clen);
    sup_put_u("decompressed", olen);
    if (r != TINF_OK || olen != n) ++errors;
    for (size_t k = 0; k < n; ++k) if (out[k] != in[k]) { ++errors; break; }
  }
  unsigned a1 = (unsigned)adler32(1, in, (uInt)n), a2 = tinf_adler32(in, (unsigned)n);
  unsigned c1 = (unsigned)crc32(0, in, (uInt)n), c2 = tinf_crc32(in, (unsigned)n);
  sup_put_u("adler32", a1); sup_put_u("crc32", c1);
  if (a1 != a2 || c1 != c2) ++errors;
  sup_put_u("errors", errors);
  return errors > 255 ? 255 : errors;
}
