/*===-- blast_main.c - zlib's contrib/blast on in-memory PKWare streams ---===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/
/* The driver of blast.test (plan 5.17.15, M27). blast() (zlib 1.3.2,
 * contrib/blast/blast.c, unmodified) decompresses the PKWare DCL format
 * through an input and an output function; when the input function returns
 * no more bytes, bits() and decode() longjmp to the setjmp in blast(), which
 * then returns 2. Both functions are this file's, so every read and write
 * is a call through a C function pointer inside the assembly. The driver
 * runs blast() on:
 *   - the example of blast.c's header comment, 00 04 82 24 25 8f 80 7f
 *     ("AIAIAIAIAIAIA"): whole, in 1-byte chunks, with 3 bytes passed in
 *     through blast()'s left/in arguments and the rest through the input
 *     function, with 2 bytes of trailing input (returned through left), and
 *     truncated after each of its first 7 bytes (each runs out of input);
 *   - headers blast() rejects (-1, -2), an output function that fails (1);
 *   - 24 pseudo-random streams of 1 to 70 bytes with valid headers, which
 *     end in any of blast()'s return codes.
 * It prints each return code with the output's length and an FNV-1a hash
 * (the output itself when it is short and printable), the number of input
 * calls and the unused input. The exit status is 0; the transcript carries
 * the results. */
#include <stdio.h>
#include <string.h>

#include "blast.h"

struct input {
  unsigned char data[128];
  unsigned len, pos, chunk, calls;
};

struct output {
  unsigned char data[8192];
  unsigned len;
  int fail;
};

static unsigned get(void *how, unsigned char **buf) {
  struct input *in = how;
  unsigned n = in->len - in->pos;
  ++in->calls;
  if (n > in->chunk)
    n = in->chunk;
  *buf = in->data + in->pos;
  in->pos += n;
  return n;
}

static int put(void *how, unsigned char *buf, unsigned len) {
  struct output *out = how;
  if (out->fail)
    return 1;
  if (len > sizeof out->data - out->len)
    len = sizeof out->data - out->len;
  memcpy(out->data + out->len, buf, len);
  out->len += len;
  return 0;
}

static unsigned long hash(const unsigned char *p, unsigned n) {
  unsigned long h = 2166136261UL;
  unsigned i;
  for (i = 0; i < n; ++i)
    h = ((h ^ p[i]) * 16777619UL) & 0xffffffffUL;
  return h;
}

static const unsigned char example[] = {0x00, 0x04, 0x82, 0x24,
                                        0x25, 0x8f, 0x80, 0x7f};

static struct input in;
static struct output out;

static void setup(const unsigned char *data, unsigned len, unsigned chunk) {
  memset(&in, 0, sizeof in);
  memset(&out, 0, sizeof out);
  memcpy(in.data, data, len);
  in.len = len;
  in.chunk = chunk;
}

static void report(const char *name, int ret, unsigned left) {
  unsigned i;
  int text = out.len <= 40;
  for (i = 0; text && i < out.len; ++i)
    text = out.data[i] >= 0x20 && out.data[i] < 0x7f;
  printf("%s: %d, %u bytes, hash %08lx, %u calls, %u left", name, ret, out.len,
         hash(out.data, out.len), in.calls, left);
  if (text && out.len)
    printf(", \"%.*s\"", (int)out.len, (const char *)out.data);
  printf("\n");
}

static void run(const char *name, const unsigned char *data, unsigned len,
                unsigned chunk) {
  unsigned left = 0;
  unsigned char *next = NULL;
  int ret;
  setup(data, len, chunk);
  ret = blast(get, &in, put, &out, &left, &next);
  report(name, ret, left);
}

int main(void) {
  unsigned char buf[80];
  unsigned char *next;
  unsigned left, k, i;
  unsigned long seed = 12345;
  char name[40];
  int ret;

  run("example", example, sizeof example, sizeof example);
  run("example in 1-byte chunks", example, sizeof example, 1);

  /* 3 bytes through left/in, the rest through the input function. */
  setup(example + 3, sizeof example - 3, 2);
  memcpy(buf, example, 3);
  left = 3;
  next = buf;
  ret = blast(get, &in, put, &out, &left, &next);
  report("example, 3 bytes through left", ret, left);

  memcpy(buf, example, sizeof example);
  buf[sizeof example] = 0xaa;
  buf[sizeof example + 1] = 0x55;
  run("example with 2 trailing bytes", buf, sizeof example + 2,
      sizeof example + 2);

  for (k = 0; k < sizeof example; ++k) {
    sprintf(name, "example cut to %u bytes", k);
    run(name, example, k, 4);
  }

  buf[0] = 2;
  buf[1] = 4;
  run("literal flag 2", buf, 2, 2);
  buf[0] = 0;
  buf[1] = 7;
  run("dictionary 7", buf, 2, 2);

  setup(example, sizeof example, sizeof example);
  out.fail = 1;
  left = 0;
  ret = blast(get, &in, put, &out, &left, NULL);
  report("failing output", ret, left);

  for (i = 0; i < 24; ++i) {
    unsigned len = 1 + i * 3, j;
    buf[0] = (unsigned char)(i & 1);
    buf[1] = (unsigned char)(4 + i % 3);
    for (j = 2; j < len; ++j) {
      seed = (seed * 1103515245UL + 12345UL) & 0x7fffffffUL;
      buf[j] = (unsigned char)(seed >> 16);
    }
    sprintf(name, "random %u (%u bytes)", i, len);
    run(name, buf, len, 1 + i % 5);
  }
  return 0;
}
