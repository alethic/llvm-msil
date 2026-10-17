// FNV-1a hashing (Milestone 10 program corpus, Design.md section 12): the
// 32-bit and 64-bit FNV-1a functions over C strings and byte buffers,
// checked against the published test vectors; bytes above 0x7f, which must
// be zero-extended from char, not sign-extended; the 32-bit hash computed a
// second way in 64-bit arithmetic and truncated; incremental hashing through
// a state struct passed by pointer, returned by value and passed by value
// (the callee's copy changes, the caller's does not); the 16-bit xor-fold;
// an open-addressing word-count table keyed by the hash (the text is split
// in place in a global buffer and the table keeps pointers into it); the
// spread of the hashes of the 4-byte encodings of 0..1023 over 64 buckets
// (their top six bits); and the hashes of a 4 KiB global buffer and of its
// tail. Hashes are printed in hex; MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; when every check passes, the
// exit status is a checksum of the printed values folded into 8 bits (D37).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

typedef unsigned u32;
typedef unsigned long long u64;

#define FNV32_BASIS 0x811c9dc5u
#define FNV32_PRIME 0x01000193u
#define FNV64_BASIS 0xcbf29ce484222325ull
#define FNV64_PRIME 0x00000100000001b3ull

volatile int zero = 0;
static u32 check;

static void note(const char *what, u64 v) {
  put_str(what);
  put_hex(v);
  check = check * 31u + (u32)v + (u32)(v >> 32);
}

static NOINLINE u32 fnv1a32(const char *s) {
  u32 h = FNV32_BASIS;
  for (; *s; s++) {
    h ^= (unsigned char)*s;
    h *= FNV32_PRIME;
  }
  return h;
}

// The same function in 64-bit arithmetic, truncated after every multiply.
static NOINLINE u32 fnv1a32_wide(const char *s) {
  u64 h = FNV32_BASIS;
  while (*s) {
    h = ((h ^ (unsigned char)*s++) * FNV32_PRIME) & 0xffffffffull;
  }
  return (u32)h;
}

static NOINLINE u64 fnv1a64(const void *p, int n) {
  const unsigned char *b = (const unsigned char *)p;
  u64 h = FNV64_BASIS;
  for (int i = 0; i < n; i++) {
    h ^= b[i];
    h *= FNV64_PRIME;
  }
  return h;
}

static int slen(const char *s) {
  int n = 0;
  while (s[n])
    n++;
  return n;
}

static int scmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

// Incremental hashing: 16 bytes, so SysV passes it in two registers and
// Win64 through a pointer to a caller-made copy.
struct fnv_state {
  u64 h;
  int bytes;
};

static NOINLINE struct fnv_state fnv_begin(void) {
  struct fnv_state st;
  st.h = FNV64_BASIS + (u64)zero;
  st.bytes = 0;
  return st;
}

static NOINLINE void fnv_feed(struct fnv_state *st, const char *s, int n) {
  for (int i = 0; i < n; i++) {
    st->h ^= (unsigned char)s[i];
    st->h *= FNV64_PRIME;
  }
  st->bytes += n;
}

// Hashes one more byte into its own copy of the state.
static NOINLINE u64 fnv_peek(struct fnv_state st, char c) {
  fnv_feed(&st, &c, 1);
  return st.h;
}

static u32 fold16(u32 h) { return (h >> 16) ^ (h & 0xffffu); }

// Word counts in an open-addressing table indexed by the hash.
#define SLOTS 32
struct entry {
  const char *word;
  u32 hash;
  int count;
};
static struct entry table[SLOTS];
static int probes;
static char text[] = "the cat and the dog and the bird sat on the mat while "
                     "the dog and the cat ran past the bird on the mat";

static NOINLINE struct entry *lookup(const char *w, int insert) {
  u32 h = fnv1a32(w);
  for (u32 i = 0; i < SLOTS; i++) {
    struct entry *e = &table[(h + i) & (SLOTS - 1)];
    probes++;
    if (!e->word) {
      if (!insert)
        return 0;
      e->word = w;
      e->hash = h;
      return e;
    }
    if (e->hash == h && scmp(e->word, w) == 0)
      return e;
  }
  return 0;
}

static unsigned char block[4096];
static int buckets[64];

int main(void) {
  // Published FNV-1a test vectors.
  MSIL_EXPECT(fnv1a32("") == 0x811c9dc5u);
  MSIL_EXPECT(fnv1a32("a") == 0xe40c292cu);
  MSIL_EXPECT(fnv1a32("foobar") == 0xbf9cf968u);
  MSIL_EXPECT(fnv1a64("", 0) == 0xcbf29ce484222325ull);
  MSIL_EXPECT(fnv1a64("a", 1) == 0xaf63dc4c8601ec8cull);
  MSIL_EXPECT(fnv1a64("foobar", 6) == 0x85944171f73967e8ull);

  const char *words[] = {"", "a", "foobar", "hello, world", "\xff\x80\x7f"};
  for (int i = 0; i < 5; i++) {
    u32 h = fnv1a32(words[i] + zero);
    MSIL_EXPECT(h == fnv1a32_wide(words[i]));
    note("fnv1a32", h);
    note("fnv1a64", fnv1a64(words[i], slen(words[i])));
  }
  // A byte above 0x7f: sign extension would xor 0xffffff80 instead of 0x80.
  MSIL_EXPECT(fnv1a32("\x80") == (FNV32_BASIS ^ 0x80u) * FNV32_PRIME);
  note("fold16(foobar)", fold16(fnv1a32("foobar")));

  // Incremental hashing in uneven pieces matches the one-shot hash.
  const char *msg = "The quick brown fox jumps over the lazy dog";
  int n = slen(msg);
  struct fnv_state st = fnv_begin();
  int pos = 0;
  for (int piece = 1; pos < n; piece++) {
    int take = piece < n - pos ? piece : n - pos;
    fnv_feed(&st, msg + pos, take);
    pos += take;
  }
  MSIL_EXPECT(st.bytes == n && st.h == fnv1a64(msg, n));
  note("pangram", st.h);
  u64 before = st.h;
  u64 peeked = fnv_peek(st, '.');
  MSIL_EXPECT(st.h == before && st.bytes == n);
  fnv_feed(&st, ".", 1);
  MSIL_EXPECT(peeked == st.h);
  note("pangram.", peeked);

  // Word counts: split the text in place, count each word.
  int nwords = 0;
  for (char *p = text; *p;) {
    while (*p == ' ')
      p++;
    if (!*p)
      break;
    char *w = p;
    while (*p && *p != ' ')
      p++;
    if (*p)
      *p++ = 0;
    struct entry *e = lookup(w, 1);
    MSIL_EXPECT(e != 0);
    e->count++;
    nwords++;
  }
  int distinct = 0;
  for (int i = 0; i < SLOTS; i++)
    if (table[i].word) {
      put_kv(table[i].word, table[i].count);
      check = check * 7u + (u32)table[i].count * (u32)(i + 1);
      distinct++;
    }
  put_kv("words", nwords);
  put_kv("distinct", distinct);
  MSIL_EXPECT(lookup("the", 0)->count == 8 && nwords == 25 && distinct == 11);
  MSIL_EXPECT(lookup("fox", 0) == 0 && lookup("", 0) == 0);
  put_kv("probes", probes);

  // Spread of the hashes of 0..1023 (little-endian bytes) over 64 buckets
  // by their top six bits (the low bits of these hashes are exactly uniform).
  for (u32 v = 0; v < 1024u + (u32)zero; v++) {
    unsigned char b[4];
    b[0] = (unsigned char)v;
    b[1] = (unsigned char)(v >> 8);
    b[2] = (unsigned char)(v >> 16);
    b[3] = (unsigned char)(v >> 24);
    buckets[fnv1a64(b, 4) >> 58]++;
  }
  int lo = 1 << 30, hi = 0, total = 0;
  for (int i = 0; i < 64; i++) {
    lo = buckets[i] < lo ? buckets[i] : lo;
    hi = buckets[i] > hi ? buckets[i] : hi;
    total += buckets[i];
  }
  MSIL_EXPECT(total == 1024);
  put_kv("bucket min", lo);
  put_kv("bucket max", hi);
  check = check * 3u + (u32)lo * 100u + (u32)hi;

  for (int i = 0; i < 4096; i++)
    block[i] = (unsigned char)(i * 7 + (i >> 8) + zero);
  note("block", fnv1a64(block, 4096));
  note("block tail", fnv1a64(block + 4000, 96));

  return (int)((check ^ check >> 8 ^ check >> 16 ^ check >> 24) & 0xff);
}
