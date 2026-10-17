// The Mandelbrot set in fixed-point arithmetic (Milestone 10 program
// corpus, Design.md section 12): the escape-time iteration in Q24 fixed
// point on 64-bit integers (native int multiplies and arithmetic right
// shifts of negative products) and in Q12 fixed point on 32-bit integers
// (int32 multiplies; the ranges below keep every product inside int32, so
// there is no overflow), rendered as ASCII art from a global palette
// string. Printed: the 64x24 picture of the whole set, the number of points
// inside, the total iteration count and an iteration histogram for each
// precision, the number of points where the two precisions disagree, and a
// 40x12 zoom into the seahorse valley with a higher iteration limit. No
// floating point. MSIL_EXPECT returns 1 + (line % 255) at the first failed
// check; when every check passes, the exit status is a checksum of the
// iteration counts folded into 8 bits (D37).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

typedef long long fx64; // Q24
#define S64 24
#define ONE64 (1LL << S64)
typedef int fx32; // Q12
#define S32 12
#define ONE32 (1 << S32)

#define COLS 64
#define ROWS 24
#define MAXIT 60

volatile int zero = 0;
static unsigned check;
static const char palette[] = ".,-:;=+*";
static unsigned char counts64[ROWS][COLS], counts32[ROWS][COLS];

// An iteration starts with |z| <= 2, so |z^2| <= 4, and |c| < 2.6 in every
// region below, so |x| and |y| stay below 6.6: 6.6^2 * 2^48 < 2^63 and
// 6.6^2 * 2^24 < 2^31.
static NOINLINE int iterate64(fx64 cx, fx64 cy, int maxit) {
  fx64 x = 0, y = 0;
  for (int i = 0; i < maxit; i++) {
    fx64 x2 = (x * x) >> S64, y2 = (y * y) >> S64;
    if (x2 + y2 > 4 * ONE64)
      return i;
    fx64 twoxy = (x * y) >> (S64 - 1);
    x = x2 - y2 + cx;
    y = twoxy + cy;
  }
  return maxit;
}

static NOINLINE int iterate32(fx32 cx, fx32 cy, int maxit) {
  fx32 x = 0, y = 0;
  for (int i = 0; i < maxit; i++) {
    fx32 x2 = (x * x) >> S32, y2 = (y * y) >> S32;
    if (x2 + y2 > 4 * ONE32)
      return i;
    fx32 twoxy = (x * y) >> (S32 - 1);
    x = x2 - y2 + cx;
    y = twoxy + cy;
  }
  return maxit;
}

static char shade(int it, int maxit) {
  return it >= maxit ? '@' : palette[it * 8 / maxit];
}

struct summary {
  int inside;
  int total;
  int hist[6];
};

static NOINLINE struct summary summarize(unsigned char (*c)[COLS]) {
  struct summary s;
  s.inside = 0;
  s.total = 0;
  for (int i = 0; i < 6; i++)
    s.hist[i] = 0;
  for (int r = 0; r < ROWS; r++)
    for (int k = 0; k < COLS; k++) {
      int it = c[r][k];
      s.inside += it == MAXIT;
      s.total += it;
      s.hist[it * 6 / (MAXIT + 1)]++;
    }
  return s;
}

static void print_summary(const char *name, struct summary s) {
  put_str(name);
  put_kv("inside", s.inside);
  put_kv("iterations", s.total);
  char line[80];
  int n = 0;
  for (int i = 0; i < 6; i++) {
    char num[12];
    int len = 0, v = s.hist[i];
    do {
      num[len++] = (char)('0' + v % 10);
      v /= 10;
    } while (v);
    line[n++] = i ? ' ' : '[';
    while (len)
      line[n++] = num[--len];
  }
  line[n++] = ']';
  line[n] = 0;
  put_str(line);
  check = check * 31u + (unsigned)s.total * 7u + (unsigned)s.inside;
}

int main(void) {
  // The whole set: re in [-2.25, 0.75), im in [-1.25, 1.25).
  fx64 dx64 = 3 * ONE64 / COLS, dy64 = 5 * ONE64 / 2 / ROWS;
  fx32 dx32 = 3 * ONE32 / COLS, dy32 = 5 * ONE32 / 2 / ROWS;
  char line[COLS + 1];
  for (int r = 0; r < ROWS; r++) {
    fx64 cy64 = -5 * ONE64 / 4 + r * dy64 + dy64 / 2;
    fx32 cy32 = -5 * ONE32 / 4 + r * dy32 + dy32 / 2;
    for (int k = 0; k < COLS; k++) {
      fx64 cx64 = -9 * ONE64 / 4 + k * dx64 + zero;
      fx32 cx32 = -9 * ONE32 / 4 + k * dx32 + zero;
      counts64[r][k] = (unsigned char)iterate64(cx64, cy64, MAXIT);
      counts32[r][k] = (unsigned char)iterate32(cx32, cy32, MAXIT);
      line[k] = shade(counts64[r][k], MAXIT);
    }
    line[COLS] = 0;
    put_str(line);
  }
  struct summary s64 = summarize(counts64);
  struct summary s32 = summarize(counts32);
  print_summary("Q24", s64);
  print_summary("Q12", s32);
  int differ = 0;
  for (int r = 0; r < ROWS; r++)
    for (int k = 0; k < COLS; k++)
      differ += counts64[r][k] != counts32[r][k];
  put_kv("points where Q24 and Q12 differ", differ);
  // c = 0 is inside, c = 1 escapes after 3 iterations, c = -2 stays on the
  // boundary (0, -2, 2, 2, ...), c = -1 cycles, c = 0.5 + 0.5i escapes.
  MSIL_EXPECT(iterate64(0, 0, MAXIT) == MAXIT);
  MSIL_EXPECT(iterate64(ONE64, 0, MAXIT) == 3);
  MSIL_EXPECT(iterate32(ONE32, 0, MAXIT) == 3);
  MSIL_EXPECT(iterate64(-2 * ONE64, 0, MAXIT) == MAXIT);
  MSIL_EXPECT(iterate32(-2 * ONE32, 0, MAXIT) == MAXIT);
  MSIL_EXPECT(iterate64(-ONE64, 0, MAXIT) == MAXIT);
  MSIL_EXPECT(iterate64(ONE64 / 2, ONE64 / 2, MAXIT) < 10);
  MSIL_EXPECT(s64.inside > 0 && s64.inside < ROWS * COLS / 2);

  // The seahorse valley: re in [-0.8, -0.7), im in [0.05, 0.2).
  fx64 zx = ONE64 / 400, zy = ONE64 / 80; // 0.1 / 40 and 0.15 / 12
  int total = 0;
  char zline[41];
  for (int r = 0; r < 12; r++) {
    fx64 cy = ONE64 / 20 + r * zy;
    for (int k = 0; k < 40; k++) {
      int it = iterate64(-4 * ONE64 / 5 + k * zx + zero, cy, 200);
      total += it;
      zline[k] = shade(it, 200);
    }
    zline[40] = 0;
    put_str(zline);
  }
  put_kv("seahorse iterations", total);
  check = check * 31u + (unsigned)total;
  return (int)((check ^ check >> 8 ^ check >> 16 ^ check >> 24) & 0xff);
}
