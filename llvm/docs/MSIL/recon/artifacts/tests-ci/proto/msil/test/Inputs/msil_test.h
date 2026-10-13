/* Helpers shared by the MSIL execution tests.
 *
 * Milestone 1 programs are freestanding: the only import is puts (Design.md
 * section 12), so all output goes through these helpers, one value per line.
 * Everything is static so each test stays a single translation unit. The same
 * source is also built natively (the oracle), so nothing here may depend on
 * the MSIL target.
 */
#ifndef MSIL_TEST_H
#define MSIL_TEST_H

int puts(const char *s);

/* Writes the decimal digits of v ending just before end; returns the start. */
static char *msil_fmt_u64(unsigned long long v, char *end) {
  do {
    *--end = (char)('0' + (int)(v % 10));
    v /= 10;
  } while (v);
  return end;
}

static void put_str(const char *s) { puts(s); }

static void put_u64(unsigned long long v) {
  char buf[24];
  buf[23] = 0;
  puts(msil_fmt_u64(v, buf + 23));
}

static void put_i64(long long v) {
  char buf[24];
  buf[23] = 0;
  /* Negate in unsigned arithmetic so LLONG_MIN works. */
  unsigned long long m = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
  char *p = msil_fmt_u64(m, buf + 23);
  if (v < 0)
    *--p = '-';
  puts(p);
}

static void put_hex(unsigned long long v) {
  static const char digits[] = "0123456789abcdef";
  char buf[20];
  char *p = buf + 19;
  *p = 0;
  do {
    *--p = digits[v & 15];
    v >>= 4;
  } while (v);
  *--p = 'x';
  *--p = '0';
  puts(p);
}

/* "name=value" on one line. */
static void put_kv(const char *name, long long v) {
  char buf[96];
  char num[24];
  int n = 0;
  while (*name && n < 64)
    buf[n++] = *name++;
  buf[n++] = '=';
  num[23] = 0;
  unsigned long long m = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
  char *p = msil_fmt_u64(m, num + 23);
  if (v < 0)
    buf[n++] = '-';
  while (*p)
    buf[n++] = *p++;
  buf[n] = 0;
  puts(buf);
}

#endif /* MSIL_TEST_H */
