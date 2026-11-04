/*===-- puff_main.c - zlib's contrib/puff on in-memory deflate streams ----===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/
/* The driver of puff.test (plan 5.17.15, M27). puff() (zlib 1.3.2,
 * contrib/puff/puff.c, unmodified) decodes deflate data from memory; when a
 * read would pass the end of its input, bits() and decode() longjmp to the
 * setjmp in puff(), which then returns 2. The driver runs puff() as
 * pufftest.c does (first with no output buffer to size the output, then, in
 * the modes 'w' and 'f', again into a buffer, which mode 'f' makes half as
 * large, so puff() returns 1) on:
 *   - the streams of contrib/puff/Makefile's 'cov' target, the coverage test
 *     whose expected exit statuses are puff()'s return codes (2, 254 for -2,
 *     246 for -10, ...), and the file zeros.raw of the same target, named by
 *     the first argument (the test copies it from the corpus cache next to
 *     the program);
 *   - streams the driver builds: a stored block followed by a final block of
 *     fixed Huffman codes with literals below and above 144, matches of
 *     lengths 3 to 10 and 258 and distances 1 to 8, and a stream of stored
 *     blocks only; every proper prefix of both, so that each byte boundary
 *     runs out of input (a stored length that does not fit returns 2 without
 *     a longjmp, bits read past the end longjmp); a final fixed block whose
 *     first match reaches back before the output (-11).
 * It prints each return code, the output and input lengths of a successful
 * call and an FNV-1a hash of the output. The exit status is 0; the
 * transcript carries the results. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "puff.h"

static unsigned long hash(const unsigned char *p, unsigned long n) {
  unsigned long h = 2166136261UL;
  unsigned long i;
  for (i = 0; i < n; ++i)
    h = ((h ^ p[i]) * 16777619UL) & 0xffffffffUL;
  return h;
}

/* pufftest.c's main on memory: mode 0 only sizes the output, 'w' also
 * decodes into a buffer of that size, 'f' into one of half the size. */
static void run(const char *name, const unsigned char *src, unsigned long len,
                int mode) {
  unsigned long destlen = 0, sourcelen = len;
  int ret = puff(NULL, &destlen, src, &sourcelen);
  printf("%s: %d", name, ret);
  if (ret == 0)
    printf(", %lu bytes from %lu of %lu", destlen, sourcelen, len);
  if (ret == 0 && mode) {
    unsigned long size = mode == 'f' ? destlen >> 1 : destlen;
    unsigned long outlen = size;
    unsigned char *dest = malloc(size ? size : 1);
    if (dest == NULL) {
      printf(": out of memory\n");
      return;
    }
    sourcelen = len;
    ret = puff(dest, &outlen, src, &sourcelen);
    printf("; -%c: %d, %lu bytes, hash %08lx", mode, ret, outlen,
           hash(dest, ret == 0 ? outlen : 0));
    free(dest);
  }
  printf("\n");
}

static unsigned hexval(int c) {
  return c <= '9' ? (unsigned)(c - '0') : (unsigned)(c - 'a' + 10);
}

static unsigned long from_hex(const char *hex, unsigned char *out) {
  unsigned long n = 0;
  while (*hex) {
    if (*hex == ' ') {
      ++hex;
      continue;
    }
    out[n++] = (unsigned char)(hexval(hex[0]) << 4 | hexval(hex[1]));
    hex += 2;
  }
  return n;
}

/* The streams of the 'cov' target in its order, with pufftest's mode. */
static const struct {
  const char *hex;
  int mode;
} cov[] = {
    {"04", 0},
    {"00", 0},
    {"00 00 00 00 00", 0},
    {"00 01 00 fe ff", 0},
    {"01 01 00 fe ff 0a", 'f'},
    {"02 7e ff ff", 0},
    {"02", 0},
    {"04 80 49 92 24 49 92 24 0f b4 ff ff c3 04", 0},
    {"04 80 49 92 24 49 92 24 71 ff ff 93 11 00", 0},
    {"04 c0 81 08 00 00 00 00 20 7f eb 0b 00 00", 0},
    {"0b 00 00", 'f'},
    {"1a 07", 0},
    {"0c c0 81 00 00 00 00 00 90 ff 6b 04", 0},
    {"fc 00 00", 0},
    {"04 00 fe ff", 0},
    {"04 00 24 49", 0},
    {"04 80 49 92 24 49 92 24 0f b4 ff ff c3 84", 0},
    {"04 00 24 e9 ff ff", 0},
    {"04 00 24 e9 ff 6d", 0},
};

/* A deflate bit writer: bits go out least significant first, Huffman codes
 * most significant bit first (RFC 1951 3.1.1). */
struct writer {
  unsigned char buf[1024];
  unsigned long n;
  unsigned bits, count;
};

static void put_bits(struct writer *w, unsigned value, unsigned count) {
  while (count--) {
    w->bits |= (value & 1) << w->count;
    value >>= 1;
    if (++w->count == 8) {
      w->buf[w->n++] = (unsigned char)w->bits;
      w->bits = w->count = 0;
    }
  }
}

static void put_code(struct writer *w, unsigned code, unsigned len) {
  while (len--)
    put_bits(w, code >> len, 1);
}

static void flush_byte(struct writer *w) {
  if (w->count)
    put_bits(w, 0, 8 - w->count);
}

static void put_stored(struct writer *w, int last, const char *text) {
  unsigned len = (unsigned)strlen(text);
  put_bits(w, (unsigned)last, 1);
  put_bits(w, 0, 2);
  flush_byte(w);
  put_bits(w, len, 16);
  put_bits(w, ~len & 0xffff, 16);
  memcpy(w->buf + w->n, text, len);
  w->n += len;
}

/* Fixed Huffman codes (RFC 1951 3.2.6). */
static void put_symbol(struct writer *w, unsigned sym) {
  if (sym < 144)
    put_code(w, 0x30 + sym, 8);
  else if (sym < 256)
    put_code(w, 0x190 + (sym - 144), 9);
  else if (sym < 280)
    put_code(w, sym - 256, 7);
  else
    put_code(w, 0xc0 + (sym - 280), 8);
}

static void put_literals(struct writer *w, const char *text) {
  while (*text)
    put_symbol(w, (unsigned char)*text++);
}

/* A match of length 3..10 or 258 at distance 1..8. */
static void put_match(struct writer *w, unsigned len, unsigned dist) {
  put_symbol(w, len == 258 ? 285 : 254 + len);
  if (dist <= 4) {
    put_code(w, dist - 1, 5);
  } else {
    put_code(w, dist <= 6 ? 4 : 5, 5);
    put_bits(w, (dist - 1) & 1, 1);
  }
}

static void prefixes(const char *name, const unsigned char *src,
                     unsigned long len) {
  unsigned long k;
  printf("%s, prefixes:", name);
  for (k = 0; k < len; ++k) {
    unsigned long destlen = 0, sourcelen = k;
    printf(" %d", puff(NULL, &destlen, src, &sourcelen));
  }
  printf("\n");
}

int main(int argc, char **argv) {
  static unsigned char data[4096];
  static struct writer w;
  unsigned long i, n;
  char name[32];

  for (i = 0; i < sizeof cov / sizeof cov[0]; ++i) {
    n = from_hex(cov[i].hex, data);
    sprintf(name, "cov %lu", i + 1);
    run(name, data, n, cov[i].mode);
  }

  if (argc > 1) {
    FILE *f = fopen(argv[1], "rb");
    if (f == NULL) {
      printf("%s: cannot open\n", argv[1]);
      return 1;
    }
    n = (unsigned long)fread(data, 1, sizeof data, f);
    fclose(f);
    run(argv[1], data, n, 'w');
    run(argv[1], data, n, 'f');
  }

  memset(&w, 0, sizeof w);
  put_stored(&w, 0, "stored, then fixed: ");
  put_bits(&w, 1, 1); /* the last block */
  put_bits(&w, 1, 2); /* fixed Huffman codes */
  put_literals(&w, "puff and blast");
  put_match(&w, 3, 4);
  put_match(&w, 10, 8);
  put_literals(&w, " \xe9\xff\x90 ");
  put_match(&w, 258, 1);
  put_match(&w, 5, 2);
  put_match(&w, 7, 5);
  put_match(&w, 4, 7);
  put_literals(&w, "!\n");
  put_symbol(&w, 256);
  flush_byte(&w);
  run("built mixed", w.buf, w.n, 'w');
  run("built mixed", w.buf, w.n, 'f');
  prefixes("built mixed", w.buf, w.n);

  memset(&w, 0, sizeof w);
  put_stored(&w, 0, "first ");
  put_stored(&w, 0, "");
  put_stored(&w, 1, "last");
  run("built stored", w.buf, w.n, 'w');
  prefixes("built stored", w.buf, w.n);

  memset(&w, 0, sizeof w);
  put_bits(&w, 1, 1);
  put_bits(&w, 1, 2);
  put_match(&w, 3, 1);
  put_symbol(&w, 256);
  flush_byte(&w);
  run("built too far", w.buf, w.n, 0);
  return 0;
}
