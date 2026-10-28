/* Monocypher 4.0.2 and its optional Ed25519: every primitive family on fixed
   inputs, with self-checks (AEAD lock/unlock, X25519 key agreement, EdDSA
   and Ed25519 sign/check, a forged message rejected) and printed digests;
   crypto_argon2 takes its configuration and inputs as structs by value
   across translation units. The error count is also the exit status (at
   most 255). */
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "sup.h"
#include "gen.h"
static uint8_t msg[3000], buf[3000], work[8 * 1024];
int main(void) {
  int errors = 0;
  uint8_t key[32], nonce[24], hash[64], mac[16], sk[64], pk[32], sig[64];
  uint8_t a_sk[32], b_sk[32], a_pk[32], b_pk[32], s1[32], s2[32];
  gen_bytes(msg, sizeof msg, 1); gen_bytes(key, 32, 2); gen_bytes(nonce, 24, 3);
  crypto_blake2b(hash, 64, msg, sizeof msg);                 sup_put_hex("blake2b", hash, 64);
  crypto_blake2b_keyed(hash, 32, key, 32, msg, 1000);         sup_put_hex("blake2b-keyed", hash, 32);
  crypto_sha512(hash, msg, sizeof msg);                       sup_put_hex("sha512", hash, 64);
  crypto_sha512_hmac(hash, key, 32, msg, 777);                sup_put_hex("hmac-sha512", hash, 64);
  crypto_chacha20_djb(buf, msg, 1000, key, nonce, 7);         crypto_blake2b(hash, 32, buf, 1000); sup_put_hex("chacha20", hash, 32);
  crypto_chacha20_x(buf, msg, 1000, key, nonce, 0);           crypto_blake2b(hash, 32, buf, 1000); sup_put_hex("xchacha20", hash, 32);
  crypto_poly1305(mac, msg, 999, key);                        sup_put_hex("poly1305", mac, 16);
  crypto_aead_lock(buf, mac, key, nonce, msg, 13, msg + 100, 500);
  sup_put_hex("aead-mac", mac, 16);
  errors += crypto_aead_unlock(buf + 1000, mac, key, nonce, msg, 13, buf, 500) != 0;
  for (int i = 0; i < 500; ++i) errors += buf[1000 + i] != msg[100 + i];
  gen_bytes(a_sk, 32, 4); gen_bytes(b_sk, 32, 5);
  crypto_x25519_public_key(a_pk, a_sk); crypto_x25519_public_key(b_pk, b_sk);
  crypto_x25519(s1, a_sk, b_pk); crypto_x25519(s2, b_sk, a_pk);
  sup_put_hex("x25519", s1, 32);
  for (int i = 0; i < 32; ++i) errors += s1[i] != s2[i];
  uint8_t seed[32]; gen_bytes(seed, 32, 6);
  crypto_eddsa_key_pair(sk, pk, seed);
  crypto_eddsa_sign(sig, sk, msg, 200);                       sup_put_hex("eddsa", sig, 64);
  errors += crypto_eddsa_check(sig, pk, msg, 200) != 0;
  errors += crypto_eddsa_check(sig, pk, msg, 201) == 0;
  gen_bytes(seed, 32, 7);
  crypto_ed25519_key_pair(sk, pk, seed);
  crypto_ed25519_sign(sig, sk, msg, 300);                     sup_put_hex("ed25519", sig, 64);
  errors += crypto_ed25519_check(sig, pk, msg, 300) != 0;
  /* Structs passed by value across translation units. */
  crypto_argon2_config cfg = {CRYPTO_ARGON2_ID, 8, 3, 1};
  crypto_argon2_inputs inp = {msg, msg + 64, 32, 16};
  crypto_argon2(hash, 32, work, cfg, inp, crypto_argon2_no_extras);
  sup_put_hex("argon2id", hash, 32);
  sup_put_u("errors", errors);
  return errors > 255 ? 255 : errors;
}
