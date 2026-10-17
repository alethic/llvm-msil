// N-queens by backtracking (Milestone 1 program corpus): solution counts
// for N = 1..9 from a recursive solver that keeps the placement and the
// column and diagonal occupancy in a local struct of arrays passed down by
// pointer, checked against a bitmask solver (free & -free picks the lowest
// free column) for N = 1..10 and against the known counts. The 92 solutions
// for N = 8 are stored in a global table; the first is printed as a board,
// and the 12 fundamental solutions are found by mapping each solution
// through the 8 symmetries of the square (column mirror, row mirror and
// transpose) and keeping those that are their own lexicographic minimum.
// The largest N is read from a volatile global. MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; on success the exit status is
// the number of 8-queens solutions, 92, which is not 1 + (line % 255) for
// any MSIL_EXPECT line of this file.
// RUN: %{msil_c}
#include "msil_test.h"

#define MAXN 10

volatile int max_n = MAXN;
static unsigned char solutions[92][8];
static int stored;

struct board {
  int n;
  int col_of_row[MAXN];
  unsigned char col_used[MAXN];
  unsigned char diag_sum[2 * MAXN];  // row + col
  unsigned char diag_diff[2 * MAXN]; // row - col + n - 1
};

static int place(struct board *b, int row) {
  if (row == b->n) {
    if (b->n == 8 && stored < 92) {
      for (int r = 0; r < 8; r++)
        solutions[stored][r] = (unsigned char)b->col_of_row[r];
      stored++;
    }
    return 1;
  }
  int count = 0;
  for (int c = 0; c < b->n; c++) {
    int d = row - c + b->n - 1;
    if (b->col_used[c] || b->diag_sum[row + c] || b->diag_diff[d])
      continue;
    b->col_used[c] = b->diag_sum[row + c] = b->diag_diff[d] = 1;
    b->col_of_row[row] = c;
    count += place(b, row + 1);
    b->col_used[c] = b->diag_sum[row + c] = b->diag_diff[d] = 0;
  }
  return count;
}

static int count_array(int n) {
  struct board b = {0};
  b.n = n;
  return place(&b, 0);
}

static int count_bits(unsigned all, unsigned cols, unsigned left,
                      unsigned right) {
  if (cols == all)
    return 1;
  int count = 0;
  unsigned free = all & ~(cols | left | right);
  while (free) {
    unsigned bit = free & (0u - free);
    free ^= bit;
    count += count_bits(all, cols | bit, ((left | bit) << 1) & all,
                        (right | bit) >> 1);
  }
  return count;
}

// Symmetry t (0..7) of the square applied to p (row -> column).
static void transform(const unsigned char *p, int t, unsigned char *out) {
  for (int r = 0; r < 8; r++) {
    int nr = r, nc = p[r];
    if (t & 1)
      nc = 7 - nc;
    if (t & 2)
      nr = 7 - nr;
    if (t & 4) {
      int x = nr;
      nr = nc;
      nc = x;
    }
    out[nr] = (unsigned char)nc;
  }
}

static int compare(const unsigned char *a, const unsigned char *b) {
  for (int i = 0; i < 8; i++)
    if (a[i] != b[i])
      return a[i] < b[i] ? -1 : 1;
  return 0;
}

static int is_canonical(const unsigned char *p) {
  for (int t = 1; t < 8; t++) {
    unsigned char q[8];
    transform(p, t, q);
    if (compare(q, p) < 0)
      return 0;
  }
  return 1;
}

static void print_digits(const unsigned char *p) {
  char s[9];
  for (int i = 0; i < 8; i++)
    s[i] = (char)('1' + p[i]);
  s[8] = 0;
  puts(s);
}

int main(void) {
  static const int known[MAXN + 1] = {0, 1, 0, 0, 2, 10, 4, 40, 92, 352, 724};
  int limit = max_n;
  MSIL_EXPECT(limit >= 8 && limit <= MAXN);
  for (int n = 1; n <= limit; n++) {
    int by_bits = count_bits((1u << n) - 1, 0, 0, 0);
    int by_array = n <= 9 ? count_array(n) : by_bits;
    MSIL_EXPECT(by_bits == by_array && by_bits == known[n]);
    char name[16] = "solutions(";
    int k = 10;
    if (n >= 10)
      name[k++] = (char)('0' + n / 10);
    name[k++] = (char)('0' + n % 10);
    name[k++] = ')';
    name[k] = 0;
    put_kv(name, by_bits);
  }
  MSIL_EXPECT(stored == 92);

  put_str("first solution");
  for (int r = 0; r < 8; r++) {
    char row[9];
    for (int c = 0; c < 8; c++)
      row[c] = solutions[0][r] == c ? 'Q' : '.';
    row[8] = 0;
    puts(row);
  }

  put_str("fundamental solutions");
  int fundamental = 0;
  for (int i = 0; i < stored; i++) {
    MSIL_EXPECT(i == 0 || compare(solutions[i - 1], solutions[i]) < 0);
    if (is_canonical(solutions[i])) {
      fundamental++;
      print_digits(solutions[i]);
    }
  }
  put_kv("fundamental", fundamental);
  MSIL_EXPECT(fundamental == 12);
  return stored;
}
