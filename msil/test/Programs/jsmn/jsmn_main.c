/* jsmn split over two translation units (JSMN_HEADER here, the definitions
   in jsmn_impl.c): five JSON documents, one of them unterminated, parsed
   into a token array; prints each token count (a negative error code for
   the unterminated one) and an FNV-1a hash of every token's type, bounds,
   size and parent link. */
#define JSMN_HEADER
#include "jsmn.h"
#include "sup.h"
static const char *const docs[] = {
    "{\"a\": 1, \"b\": [true, false, null], \"c\": {\"d\": \"e\\\"f\"}}",
    "[1, 2.5, -3e10, \"x\", [], {}]",
    "{\"name\": \"msil-ld\", \"inputs\": [\"a.o\", \"libz.a\"], \"shared\": false}",
    "{\"unterminated\": [1, 2",
    "[[[[[[[[[[1]]]]]]]]]]",
};
static jsmntok_t tok[64];
int main(void) {
  unsigned h = 2166136261u;
  for (unsigned d = 0; d < sizeof docs / sizeof *docs; ++d) {
    jsmn_parser p;
    jsmn_init(&p);
    size_t len = 0;
    while (docs[d][len]) ++len;
    int r = jsmn_parse(&p, docs[d], len, tok, 64);
    sup_put_i("tokens", r);
    for (int i = 0; i < r; ++i) {
      h = (h ^ (unsigned)tok[i].type) * 16777619u;
      h = (h ^ (unsigned)tok[i].start) * 16777619u;
      h = (h ^ (unsigned)tok[i].end) * 16777619u;
      h = (h ^ (unsigned)tok[i].size) * 16777619u;
#ifdef JSMN_PARENT_LINKS
      h = (h ^ (unsigned)tok[i].parent) * 16777619u;
#endif
    }
  }
  sup_put_u("hash", h);
  return 0;
}
