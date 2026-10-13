/*===-- msil_test.h - Helpers for the MSIL execution tests -------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* Milestone-1 programs are freestanding C: the only import is puts          *|
|* (Design.md section 12), so all output goes through the put_* helpers, one  *|
|* value per line. Everything is static inline so each test stays a single   *|
|* translation unit without unused-function warnings. The same source is     *|
|* also built natively (the oracle), so nothing here may depend on the MSIL  *|
|* target.                                                                    *|
|*                                                                            *|
|* Exit statuses must stay in 0..255 on every OS (plan D37): Linux keeps only *|
|* the low 8 bits, and msil-run.py rejects anything else.                     *|
|*                                                                            *|
|* Capabilities (plan 5.5): the put_* helpers need loops, local arrays and    *|
|* string literals, so tests may use them from M8 on; MSIL_EXPECT needs      *|
|* branches (M6); MSIL_CHECK is branch-free and works from M4.                *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef MSIL_TEST_H
#define MSIL_TEST_H

int puts(const char *s);

/* Branch-free result accumulation for tests written before control flow
 * exists (M4, M5): sets bit (bit & 7) of acc when cond is false. A test ends
 * with 'return acc;', so the exit status is a bitmask of the failed checks
 * (0 = all passed) and never leaves 0..255. Each check needs its own bit. */
#define MSIL_CHECK(acc, bit, c) ((acc) |= (unsigned)!(c) << ((bit) & 7))

/* Self-checking tests from M6 on: on failure, return 1 + the line number
 * modulo 255 (1..255), so a nonzero exit status names the failing line. */
#define MSIL_EXPECT(c)                                                         \
  do {                                                                         \
    if (!(c))                                                                  \
      return 1 + (__LINE__ % 255);                                             \
  } while (0)

/* Writes the decimal digits of v ending just before end; returns the start. */
static inline char *msil_fmt_u64(unsigned long long v, char *end) {
  do {
    *--end = (char)('0' + (int)(v % 10));
    v /= 10;
  } while (v);
  return end;
}

static inline void put_str(const char *s) { puts(s); }

static inline void put_u64(unsigned long long v) {
  char buf[24];
  buf[23] = 0;
  puts(msil_fmt_u64(v, buf + 23));
}

static inline void put_i64(long long v) {
  char buf[24];
  buf[23] = 0;
  /* Negate in unsigned arithmetic so LLONG_MIN works. */
  unsigned long long m =
      v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
  char *p = msil_fmt_u64(m, buf + 23);
  if (v < 0)
    *--p = '-';
  puts(p);
}

/* 0x and lowercase hex digits, no leading zeros: put_hex(255) is 0xff. */
static inline void put_hex(unsigned long long v) {
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

/* "name=value" on one line; name is truncated to 64 characters. */
static inline void put_kv(const char *name, long long v) {
  char buf[96];
  char num[24];
  int n = 0;
  while (*name && n < 64)
    buf[n++] = *name++;
  buf[n++] = '=';
  num[23] = 0;
  unsigned long long m =
      v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
  char *p = msil_fmt_u64(m, num + 23);
  if (v < 0)
    buf[n++] = '-';
  while (*p)
    buf[n++] = *p++;
  buf[n] = 0;
  puts(buf);
}

#endif /* MSIL_TEST_H */
