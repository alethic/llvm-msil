/* TweetNaCl 20140427: box, box_open, sign, sign_open (and a forged message
   rejected), hash, onetimeauth and scalarmult on deterministic keys from
   tweetnacl_rand.c. tweetnacl.c uses 'unsigned long' for u32, which is 32
   bits on windows-msvc and 64 bits on linux-gnu: the transcript is the same
   on both triples. The error count is also the exit status (at most 255). */
#include "tweetnacl.h"
#include "sup.h"
#include "gen.h"
typedef unsigned char u8;
typedef unsigned long long u64;
static u8 m[512], c[512], m2[512], sm[600], m3[600];
int main(void) {
  int errors = 0;
  u8 apk[32], ask[32], bpk[32], bsk[32], n[24], h[64], a[16], q[32];
  u8 spk[32], ssk[64];
  crypto_box_keypair(apk, ask);
  crypto_box_keypair(bpk, bsk);
  sup_put_hex("box-pk", apk, 32);
  gen_bytes(n, 24, 9);
  for (int i = 0; i < 32; ++i) m[i] = 0;
  gen_text(m + 32, 400, 4);
  if (crypto_box(c, m, 432, n, bpk, ask) != 0) ++errors;
  crypto_hash(h, c, 432); sup_put_hex("box", h, 32);
  if (crypto_box_open(m2, c, 432, n, apk, bsk) != 0) ++errors;
  for (int i = 32; i < 432; ++i) errors += m2[i] != m[i];
  crypto_sign_keypair(spk, ssk);
  u64 smlen = 0, mlen = 0;
  crypto_sign(sm, &smlen, m + 32, 200, ssk);
  sup_put_hex("sign", sm, 64);
  if (crypto_sign_open(m3, &mlen, sm, smlen, spk) != 0 || mlen != 200) ++errors;
  sm[70] ^= 1;
  if (crypto_sign_open(m3, &mlen, sm, smlen, spk) == 0) ++errors;
  crypto_hash(h, m, 432); sup_put_hex("sha512", h, 64);
  crypto_onetimeauth(a, m, 300, apk); sup_put_hex("poly1305", a, 16);
  crypto_scalarmult_base(q, bsk); sup_put_hex("scalarmult", q, 32);
  sup_put_u("errors", errors);
  return errors > 255 ? 255 : errors;
}
