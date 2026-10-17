// String routines written in C (Milestone 10 program corpus, Design.md
// section 12): own strlen, strcmp (unsigned char order, so "\x80" sorts
// after "a"), strncmp, strcpy, strcat, strchr, strrchr, a naive strstr, an
// in-place reverse, itoa in bases 2..36 (INT_MIN included, negated in
// unsigned arithmetic) and atoi (round trip through itoa), upper-casing,
// reversing the words of a sentence in place, a palindrome test that skips
// punctuation, run-length encoding and decoding, and ROT13 applied twice.
// Strings live in global arrays, string literals, local arrays and a global
// table of pointers to literals (relocations). Results are printed;
// MSIL_EXPECT returns 1 + (line % 255) at the first failed check; when every
// check passes, the exit status is a checksum of the printed strings folded
// into 8 bits (D37).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

volatile int zero = 0;
static unsigned check;

static void show(const char *s) {
  put_str(s);
  for (; *s; s++)
    check = check * 33u + (unsigned char)*s;
  check = check * 33u + '\n';
}

static NOINLINE int s_len(const char *s) {
  const char *p = s;
  while (*p)
    p++;
  return (int)(p - s);
}

static NOINLINE int s_cmp(const char *a, const char *b) {
  const unsigned char *x = (const unsigned char *)a;
  const unsigned char *y = (const unsigned char *)b;
  while (*x && *x == *y) {
    x++;
    y++;
  }
  return (*x > *y) - (*x < *y);
}

static NOINLINE int s_ncmp(const char *a, const char *b, int n) {
  for (int i = 0; i < n; i++) {
    unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
    if (x != y)
      return x < y ? -1 : 1;
    if (!x)
      break;
  }
  return 0;
}

static NOINLINE char *s_cpy(char *d, const char *s) {
  char *r = d;
  while ((*d++ = *s++))
    ;
  return r;
}

static NOINLINE char *s_cat(char *d, const char *s) {
  s_cpy(d + s_len(d), s);
  return d;
}

static NOINLINE const char *s_chr(const char *s, int c) {
  for (;; s++) {
    if (*s == (char)c)
      return s;
    if (!*s)
      return 0;
  }
}

static NOINLINE const char *s_rchr(const char *s, int c) {
  const char *last = 0;
  do {
    if (*s == (char)c)
      last = s;
  } while (*s++);
  return last;
}

static NOINLINE const char *s_str(const char *h, const char *n) {
  int m = s_len(n);
  for (; *h; h++)
    if (s_ncmp(h, n, m) == 0)
      return h;
  return m == 0 ? h : 0;
}

static NOINLINE char *s_rev_n(char *s, int n) {
  for (int i = 0, j = n - 1; i < j; i++, j--) {
    char t = s[i];
    s[i] = s[j];
    s[j] = t;
  }
  return s;
}

static char *s_rev(char *s) { return s_rev_n(s, s_len(s)); }

static const char digits36[] = "0123456789abcdefghijklmnopqrstuvwxyz";

// Appends s to d with bytes outside printable ASCII as \xNN, so the
// transcript stays ASCII.
static NOINLINE char *s_cat_escaped(char *d, const char *s) {
  char *o = d + s_len(d);
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c >= 0x20 && c < 0x7f) {
      *o++ = (char)c;
    } else {
      *o++ = '\\';
      *o++ = 'x';
      *o++ = digits36[c >> 4];
      *o++ = digits36[c & 15];
    }
  }
  *o = 0;
  return d;
}

// Base 10 is signed; other bases print the unsigned bit pattern.
static NOINLINE char *s_itoa(int v, char *buf, int base) {
  int neg = base == 10 && v < 0;
  unsigned m = neg ? 0u - (unsigned)v : (unsigned)v;
  int i = 0;
  do {
    buf[i++] = digits36[m % (unsigned)base];
    m /= (unsigned)base;
  } while (m);
  if (neg)
    buf[i++] = '-';
  buf[i] = 0;
  return s_rev(buf);
}

static NOINLINE int s_atoi(const char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  int neg = *s == '-';
  if (*s == '-' || *s == '+')
    s++;
  unsigned m = 0;
  while (*s >= '0' && *s <= '9')
    m = m * 10u + (unsigned)(*s++ - '0');
  // m <= 2147483648 here; no out-of-range conversion.
  return neg ? (m ? -(int)(m - 1u) - 1 : 0) : (int)m;
}

static NOINLINE char *s_upper(char *s) {
  for (char *p = s; *p; p++)
    if (*p >= 'a' && *p <= 'z')
      *p = (char)(*p - 'a' + 'A');
  return s;
}

// Reverse the whole string, then every word.
static NOINLINE char *reverse_words(char *s) {
  s_rev(s);
  for (char *p = s; *p;) {
    while (*p == ' ')
      p++;
    char *w = p;
    while (*p && *p != ' ')
      p++;
    s_rev_n(w, (int)(p - w));
  }
  return s;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }
static int is_alnum(int c) {
  c = lower(c);
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

static NOINLINE int palindrome(const char *s) {
  const char *e = s + s_len(s);
  while (s < e) {
    if (!is_alnum((unsigned char)*s)) {
      s++;
    } else if (!is_alnum((unsigned char)e[-1])) {
      e--;
    } else if (lower((unsigned char)*s++) != lower((unsigned char)*--e)) {
      return 0;
    }
  }
  return 1;
}

static NOINLINE char *rle_encode(const char *s, char *out) {
  char *o = out;
  while (*s) {
    char c = *s;
    int n = 0;
    while (*s == c) {
      s++;
      n++;
    }
    *o++ = c;
    s_itoa(n, o, 10);
    o += s_len(o);
  }
  *o = 0;
  return out;
}

static NOINLINE char *rle_decode(const char *s, char *out) {
  char *o = out;
  while (*s) {
    char c = *s++;
    int n = s_atoi(s);
    while (*s >= '0' && *s <= '9')
      s++;
    while (n-- > 0)
      *o++ = c;
  }
  *o = 0;
  return out;
}

static NOINLINE char *rot13(char *s) {
  for (char *p = s; *p; p++) {
    char c = *p;
    if (c >= 'a' && c <= 'z')
      *p = (char)('a' + (c - 'a' + 13) % 26);
    else if (c >= 'A' && c <= 'Z')
      *p = (char)('A' + (c - 'A' + 13) % 26);
  }
  return s;
}

static const char *const samples[] = {"", "a", "abc", "abd", "ab",
                                      "\x80", "hello, world"};
#define NSAMPLES (int)(sizeof(samples) / sizeof(samples[0]))
static char line[256];
static char sentence[] = "the quick brown fox jumps over the lazy dog";

int main(void) {
  char num[40];
  for (int i = 0; i < NSAMPLES; i++) {
    s_cpy(line, "len \"");
    s_cat(s_cat_escaped(line, samples[i]), "\" = ");
    show(s_cat(line, s_itoa(s_len(samples[i] + zero), num, 10)));
  }
  MSIL_EXPECT(s_len(samples[6]) == 12 && s_len("") == 0);

  // Comparison signs for every ordered pair.
  for (int i = 0; i < NSAMPLES; i++) {
    char *p = line;
    for (int j = 0; j < NSAMPLES; j++) {
      int c = s_cmp(samples[i], samples[j]);
      *p++ = c < 0 ? '<' : c > 0 ? '>' : '=';
      MSIL_EXPECT(c == -s_cmp(samples[j], samples[i]));
    }
    *p = 0;
    show(line);
  }
  MSIL_EXPECT(s_cmp("\x80", "a") > 0 && s_cmp("a", "\x80") < 0);
  MSIL_EXPECT(s_ncmp("abcx", "abcy", 3) == 0 && s_ncmp("abcx", "abcy", 4) < 0);

  // Copy, concatenate, search.
  char buf[64];
  s_cpy(buf, "hello");
  s_cat(s_cat(buf, ", "), "world");
  show(buf);
  MSIL_EXPECT(s_cmp(buf, samples[6]) == 0);
  MSIL_EXPECT(s_chr(buf, 'o') - buf == 4 && s_rchr(buf, 'o') - buf == 8);
  MSIL_EXPECT(s_chr(buf, 'z') == 0 && s_chr(buf, 0) == buf + 12);
  MSIL_EXPECT(s_str(buf, "wor") - buf == 7 && s_str(buf, "word") == 0);
  MSIL_EXPECT(s_str(buf, "") == buf);
  put_kv("strstr(\"lo, w\")", s_str(buf, "lo, w") - buf);

  // itoa in several bases, atoi round trip.
  static const int values[] = {0,          7,           -7,         42,
                               255,        -1000,       123456789,  2147483647,
                               -2147483647 - 1};
  static const int bases[] = {10, 2, 8, 16, 36};
  for (int i = 0; i < 9; i++) {
    int v = values[i] + zero;
    s_itoa(v, line, 10);
    MSIL_EXPECT(s_atoi(line) == v);
    for (int b = 1; b < 5; b++) {
      s_cat(line, " ");
      s_cat(line, s_itoa(v, num, bases[b]));
    }
    show(line);
  }
  MSIL_EXPECT(s_atoi("  -00042xyz") == -42 && s_atoi("+17") == 17);
  MSIL_EXPECT(s_atoi("-2147483648") == -2147483647 - 1 && s_atoi("-0") == 0);

  // Upper case, reversed words, palindromes.
  show(s_upper(s_cpy(buf, "Mixed Case 123 text")));
  show(reverse_words(sentence));
  show(reverse_words(sentence));
  static const char *const pals[] = {"A man, a plan, a canal: Panama!",
                                     "Was it a car or a cat I saw?",
                                     "never odd or even", "palindrome", "",
                                     "ab"};
  int npal = 0;
  for (int i = 0; i < 6; i++)
    npal = npal * 2 + palindrome(pals[i]);
  put_kv("palindromes", npal);
  MSIL_EXPECT(npal == 0x3a);

  // Run-length encoding and ROT13 round trips.
  char enc[64], dec[64];
  const char *runs = "aaaabbbcccccccccccdeffffffffffffffffffff";
  show(rle_encode(runs, enc));
  show(rle_decode(enc, dec));
  MSIL_EXPECT(s_cmp(dec, runs) == 0);
  s_cpy(buf, "Hello, World! Why did the chicken cross the road?");
  show(rot13(buf));
  show(rot13(buf));
  MSIL_EXPECT(s_ncmp(buf, "Hello, World!", 13) == 0);

  return (int)((check ^ check >> 8 ^ check >> 16 ^ check >> 24) & 0xff);
}
