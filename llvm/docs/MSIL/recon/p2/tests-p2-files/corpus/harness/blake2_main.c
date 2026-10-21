/* BLAKE2 reference code from an archive: the keyed known-answer tests of
   blake2b, blake2s, blake2bp and blake2sp (testvectors/blake2-kat.h, read
   from the upstream checkout at build time). memset's address is taken by
   blake2-impl.h (secure_zero_memory), and memcmp is called here: both come
   from the support archive on MSIL. */
#include "blake2.h"
#include "blake2-kat.h"
#include "sup.h"
int memcmp(const void *, const void *, size_t);
int main(void) {
  uint8_t key[64], in[BLAKE2_KAT_LENGTH], out[64];
  int bad_b = 0, bad_s = 0, bad_bp = 0, bad_sp = 0;
  for (int i = 0; i < 64; ++i) key[i] = (uint8_t)i;
  for (int i = 0; i < BLAKE2_KAT_LENGTH; ++i) in[i] = (uint8_t)i;
  for (int i = 0; i < BLAKE2_KAT_LENGTH; ++i) {
    blake2b(out, 64, in, i, key, 64);  bad_b += memcmp(out, blake2b_keyed_kat[i], 64) != 0;
    blake2s(out, 32, in, i, key, 32);  bad_s += memcmp(out, blake2s_keyed_kat[i], 32) != 0;
    blake2bp(out, 64, in, i, key, 64); bad_bp += memcmp(out, blake2bp_keyed_kat[i], 64) != 0;
    blake2sp(out, 32, in, i, key, 32); bad_sp += memcmp(out, blake2sp_keyed_kat[i], 32) != 0;
  }
  blake2b(out, 64, in, 255, key, 64);
  sup_put_hex("blake2b(255)", out, 64);
  sup_put_u("bad blake2b", bad_b);
  sup_put_u("bad blake2s", bad_s);
  sup_put_u("bad blake2bp", bad_bp);
  sup_put_u("bad blake2sp", bad_sp);
  int errors = bad_b + bad_s + bad_bp + bad_sp;
  return errors > 255 ? 255 : errors;
}
