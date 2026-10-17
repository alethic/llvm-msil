// Double-precision matrix multiplication (Milestone 1 program corpus),
// printed as integers scaled by 1000 or 10^6: 6x6 matrices in global
// structs multiplied by functions that return them by value (A*B, the
// associativity (A*B)*C == A*(B*C), A*I == A and the transpose identity
// (A*B)^T == B^T*A^T), powers of the Fibonacci matrix [[1,1],[1,0]] by
// repeated squaring (every entry stays an integer below 2^53 up to the 76th
// power, so they are exact), and a 5x5 Hilbert matrix times a vector,
// solved back by Gaussian elimination, whose inexact results are the same
// under IEEE 754 everywhere. The 6x6 inputs are multiples of 1/4 derived
// from a volatile global, so their products are exact and can be compared
// with ==. Every multiply and add is a separate statement, so no compiler
// may contract them into a fused multiply-add (clang's default
// -ffp-contract=on works within one expression only). MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; on success the exit status is
// 8 * trace(A*B) modulo 256 (re-check that it is not 1 + (line % 255) for
// an MSIL_EXPECT line after edits that move lines).
// RUN: %{msil_c}
#include "msil_test.h"

#define N 6
#define H 5

typedef struct {
  double m[N][N];
} mat;

volatile int salt = 3;
static mat A, B, C, I;

static mat mul(const mat *x, const mat *y) {
  mat r;
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++) {
      double sum = 0.0;
      for (int k = 0; k < N; k++) {
        double p = x->m[i][k] * y->m[k][j];
        sum += p;
      }
      r.m[i][j] = sum;
    }
  return r;
}

static mat transpose(mat x) {
  mat r;
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++)
      r.m[i][j] = x.m[j][i];
  return r;
}

static int same(const mat *x, const mat *y) {
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++)
      if (x->m[i][j] != y->m[i][j])
        return 0;
  return 1;
}

// v * scale rounded to the nearest integer, halves away from zero.
static long long scaled(double v, double scale) {
  double s = v * scale;
  if (s < 0) {
    double t = 0.5 - s;
    return -(long long)t;
  }
  double t = s + 0.5;
  return (long long)t;
}

static char line[160];
static int len;

static void add_int(long long v) {
  char buf[24];
  buf[23] = 0;
  char *p = msil_fmt_u64(v < 0 ? 0ull - (unsigned long long)v
                               : (unsigned long long)v,
                         buf + 23);
  if (len)
    line[len++] = ' ';
  if (v < 0)
    line[len++] = '-';
  while (*p && len < 150)
    line[len++] = *p++;
}

static void flush(void) {
  line[len] = 0;
  puts(line);
  len = 0;
}

static void print_mat(const char *name, const mat *x) {
  put_str(name);
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++)
      add_int(scaled(x->m[i][j], 1000.0));
    flush();
  }
}

// f = f * g for 2x2 matrices.
static void mul2(double f[2][2], double g[2][2]) {
  double r[2][2];
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++) {
      double a = f[i][0] * g[0][j];
      double b = f[i][1] * g[1][j];
      r[i][j] = a + b;
    }
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++)
      f[i][j] = r[i][j];
}

static double fib_power(int n) { // [[1,1],[1,0]]^n = [[F(n+1),F(n)],...]
  double r[2][2] = {{1, 0}, {0, 1}};
  double p[2][2] = {{1, 1}, {1, 0}};
  while (n) {
    if (n & 1)
      mul2(r, p);
    n >>= 1;
    if (n)
      mul2(p, p);
  }
  return r[0][1];
}

int main(void) {
  int s = salt;
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++) {
      A.m[i][j] = ((i * 7 + j * 3 + s) % 9 - 4) * 0.25;
      B.m[i][j] = ((i * 5 + j * 2 + s) % 7 - 3) * 0.5;
      C.m[i][j] = ((i + 2 * j + s) % 5 - 2) * 0.25;
      I.m[i][j] = i == j;
    }
  mat ab = mul(&A, &B);
  mat bc = mul(&B, &C);
  mat ab_c = mul(&ab, &C);
  mat a_bc = mul(&A, &bc);
  mat ai = mul(&A, &I);
  mat bt = transpose(B);
  mat at = transpose(A);
  mat btat = mul(&bt, &at);
  mat abt = transpose(ab);
  MSIL_EXPECT(same(&ab_c, &a_bc));
  MSIL_EXPECT(same(&ai, &A));
  MSIL_EXPECT(same(&abt, &btat));
  MSIL_EXPECT(!same(&ab, &btat));
  print_mat("A", &A);
  print_mat("A*B", &ab);
  print_mat("(A*B)*C", &ab_c);
  double trace = 0;
  for (int i = 0; i < N; i++)
    trace += ab.m[i][i];
  put_kv("trace(A*B)*1000", scaled(trace, 1000.0));

  put_str("fibonacci by matrix powers");
  unsigned long long f0 = 0, f1 = 1;
  for (int n = 1; n <= 76; n++) {
    unsigned long long t = f0 + f1;
    f0 = f1;
    f1 = t; // f0 = F(n)
    double f = fib_power(n);
    MSIL_EXPECT((unsigned long long)f == f0);
    if (n % 15 == 0 || n == 76)
      put_u64((unsigned long long)f);
  }

  double hm[H][H], x[H], y[H];
  for (int i = 0; i < H; i++) {
    x[i] = i % 2 ? -(i + 1) : i + 1;
    for (int j = 0; j < H; j++)
      hm[i][j] = 1.0 / (i + j + 1);
  }
  put_str("hilbert*x*10^6");
  for (int i = 0; i < H; i++) {
    double sum = 0.0;
    for (int j = 0; j < H; j++) {
      double p = hm[i][j] * x[j];
      sum += p;
    }
    y[i] = sum;
    add_int(scaled(sum, 1e6));
  }
  flush();
  // Solve hm * z = y in place (no pivoting: hm is positive definite).
  for (int k = 0; k < H; k++)
    for (int i = k + 1; i < H; i++) {
      double f = hm[i][k] / hm[k][k];
      for (int j = k; j < H; j++) {
        double p = f * hm[k][j];
        hm[i][j] -= p;
      }
      double q = f * y[k];
      y[i] -= q;
    }
  put_str("solved*10^6");
  double z[H];
  for (int i = H - 1; i >= 0; i--) {
    double sum = y[i];
    for (int j = i + 1; j < H; j++) {
      double p = hm[i][j] * z[j];
      sum -= p;
    }
    z[i] = sum / hm[i][i];
  }
  for (int i = 0; i < H; i++) {
    add_int(scaled(z[i], 1e6));
    MSIL_EXPECT(scaled(z[i], 1000.0) == scaled(x[i], 1000.0));
  }
  flush();
  return (int)((unsigned long long)scaled(trace, 8.0) & 0xff);
}
