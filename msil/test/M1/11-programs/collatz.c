// Collatz stopping times with a memo table (Milestone 10 program corpus,
// Design.md section 12): total stopping times of every n below 20000 with
// memoization in a global unsigned short table, the path of not yet known
// values kept on an explicit global stack and filled in backwards (no
// recursion), checked against a plain loop on a sample; the records (each n
// whose stopping time exceeds that of every smaller n), the longest chain,
// the highest value reached and the first n reaching it, a histogram of the
// stopping times, and the glide records (steps until the value first drops
// below n). Values are unsigned 64-bit. 03-control/collatz.c covers the
// plain loops and the recursion; this is the program-sized version.
// MSIL_EXPECT returns 1 + (line % 255) at the first failed check; when
// every check passes, the exit status is a checksum of the printed values
// folded into 8 bits (D37).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

typedef unsigned long long u64;

#define LIMIT 20000
#define PATH_MAX_LEN 1024

volatile int zero = 0;
static unsigned check;
static unsigned short memo[LIMIT]; // 0 = not known yet (memo[1] is 0 too)
static u64 path[PATH_MAX_LEN];
static int path_overflow;
static u64 peak;
static unsigned peak_start;
static int memo_hits;

static u64 next(u64 v) { return (v & 1) ? 3 * v + 1 : v >> 1; }

static NOINLINE int stopping_time(unsigned n) {
  int len = 0;
  u64 v = n;
  while (v != 1 && (v >= LIMIT || memo[v] == 0)) {
    if (len == PATH_MAX_LEN) {
      path_overflow = 1;
      return 0;
    }
    path[len++] = v;
    v = next(v);
    if (v > peak) {
      peak = v;
      peak_start = n;
    }
  }
  int t = 0;
  if (v != 1) {
    t = memo[v];
    memo_hits++;
  }
  while (len > 0) {
    u64 p = path[--len];
    t++;
    if (p < LIMIT)
      memo[p] = (unsigned short)t;
  }
  return t;
}

static NOINLINE int plain_steps(u64 v) {
  int t = 0;
  for (; v != 1; t++)
    v = next(v);
  return t;
}

static NOINLINE int glide(u64 n) {
  int t = 0;
  for (u64 v = n; v >= n; t++)
    v = next(v);
  return t;
}

// Appends ' ' and the decimal digits of v to a line.
static char line[256];
static int line_len;
static void append(unsigned v) {
  char tmp[12];
  int n = 0;
  do {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v);
  if (line_len)
    line[line_len++] = ' ';
  while (n)
    line[line_len++] = tmp[--n];
  line[line_len] = 0;
}

static void flush(const char *title) {
  put_str(title);
  put_str(line);
  for (int i = 0; i < line_len; i++)
    check = check * 33u + (unsigned char)line[i];
  line_len = 0;
  line[0] = 0;
}

static int hist[16];

int main(void) {
  int best = 0;
  unsigned best_n = 1;
  for (unsigned n = 1; n < LIMIT + (unsigned)zero; n++) {
    int t = stopping_time(n);
    if (t > best) {
      best = t;
      best_n = n;
      append(n);
    }
    hist[t / 25]++;
  }
  flush("records:");
  MSIL_EXPECT(!path_overflow);
  put_kv("longest chain start", best_n);
  put_kv("longest chain steps", best);
  put_kv("highest value", (long long)peak);
  put_kv("first reached from", peak_start);
  put_kv("memo hits", memo_hits);
  check = check * 31u + (unsigned)best + (unsigned)peak + peak_start;

  // The memoized times agree with a plain loop.
  for (unsigned n = 2; n < LIMIT; n += 997)
    MSIL_EXPECT(memo[n] == plain_steps(n));
  MSIL_EXPECT(memo[27] == 111 && plain_steps(27) == 111);
  MSIL_EXPECT(memo[9] == 19 && memo[2] == 1 && stopping_time(1) == 0);
  // Beyond the table: computed on the fly from the memo.
  MSIL_EXPECT(stopping_time(77031) == plain_steps(77031));
  put_kv("steps(77031)", stopping_time(77031 + (unsigned)zero));

  for (int b = 0; b < 16; b++)
    if (hist[b])
      append((unsigned)hist[b]);
  flush("histogram (buckets of 25 steps):");

  int best_glide = 0;
  for (u64 n = 2; n < LIMIT; n++) {
    int g = glide(n);
    if (g > best_glide) {
      best_glide = g;
      append((unsigned)n);
      append((unsigned)g);
    }
  }
  flush("glide records (n, glide):");
  return (int)((check ^ check >> 8 ^ check >> 16 ^ check >> 24) & 0xff);
}
