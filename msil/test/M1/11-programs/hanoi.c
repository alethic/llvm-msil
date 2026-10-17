// Towers of Hanoi (Milestone 1 program corpus): the recursive solution for
// four discs printed move by move, then a simulation on three global peg
// stacks (a two-dimensional global array) that checks every move (the
// source peg is not empty and no disc lands on a smaller one) for 1..16
// discs, once with the recursive solver and once with the iterative one (on
// odd steps the smallest disc moves one peg along a fixed cycle, on even
// steps the only other legal move is made). Both must leave the whole tower
// on the target peg and give the same FNV-1a hash of the move sequence.
// Prints the move counts (2^n - 1) and the hashes. The largest disc count is
// read from a volatile global. MSIL_EXPECT returns 1 + (line % 255) at the
// first failed check; on success the exit status is the number of moves for
// eight discs, 255, which is not 1 + (line % 255) for any MSIL_EXPECT line
// of this file.
// RUN: %{msil_c}
#include "msil_test.h"

#define MAXD 16

volatile int max_discs = MAXD;
static int peg[3][MAXD];
static int height[3];
static unsigned moves, hash, illegal;
static int printing;

static void reset(int n) {
  height[0] = height[1] = height[2] = 0;
  for (int d = n; d >= 1; d--)
    peg[0][height[0]++] = d;
  moves = 0;
  hash = 2166136261u;
  illegal = 0;
}

static int top(int p) { return height[p] ? peg[p][height[p] - 1] : MAXD + 1; }

static void move(int from, int to) {
  if (height[from] == 0 || top(to) < top(from) || height[to] >= MAXD) {
    illegal++;
    return;
  }
  int disc = peg[from][--height[from]];
  peg[to][height[to]++] = disc;
  moves++;
  hash = (hash ^ (unsigned)(from * 3 + to)) * 16777619u;
  if (printing) {
    char text[] = "disc 0: A -> A";
    text[5] = (char)('0' + disc);
    text[8] = (char)('A' + from);
    text[13] = (char)('A' + to);
    puts(text);
  }
}

static void solve(int n, int from, int to, int via) {
  if (n == 0)
    return;
  solve(n - 1, from, via, to);
  move(from, to);
  solve(n - 1, via, to, from);
}

static void solve_iterative(int n) {
  int small = 0;                // the peg that holds disc 1
  int step = n % 2 == 0 ? 1 : 2; // even n: A->B->C->A, odd n: A->C->B->A
  unsigned total = (1u << n) - 1;
  for (unsigned i = 1; i <= total; i++) {
    if (i % 2 == 1) {
      int next = (small + step) % 3;
      move(small, next);
      small = next;
    } else {
      int a = (small + 1) % 3, b = (small + 2) % 3;
      if (top(a) < top(b))
        move(a, b);
      else
        move(b, a);
    }
  }
}

static char line[96];
static int len;

static void add_str(const char *s) {
  while (*s && len < 90)
    line[len++] = *s++;
}

static void add_u64(unsigned long long v) {
  char buf[24];
  buf[23] = 0;
  add_str(msil_fmt_u64(v, buf + 23));
}

static void add_hex(unsigned v) {
  char buf[9];
  for (int i = 7; i >= 0; i--) {
    buf[i] = "0123456789abcdef"[v & 15];
    v >>= 4;
  }
  buf[8] = 0;
  add_str(buf);
}

int main(void) {
  int limit = max_discs;
  MSIL_EXPECT(limit >= 8 && limit <= MAXD);

  put_str("four discs");
  reset(4);
  printing = 1;
  solve(4, 0, 2, 1);
  printing = 0;
  MSIL_EXPECT(moves == 15 && illegal == 0 && height[2] == 4);

  unsigned moves8 = 0;
  for (int n = 1; n <= limit; n++) {
    reset(n);
    solve(n, 0, 2, 1);
    unsigned recursive_moves = moves, recursive_hash = hash;
    MSIL_EXPECT(illegal == 0 && height[2] == n && moves == (1u << n) - 1);
    for (int i = 0; i < n; i++)
      MSIL_EXPECT(peg[2][i] == n - i);

    reset(n);
    solve_iterative(n);
    MSIL_EXPECT(illegal == 0 && height[2] == n);
    MSIL_EXPECT(moves == recursive_moves && hash == recursive_hash);
    if (n == 8)
      moves8 = moves;

    add_u64((unsigned)n);
    add_str(n == 1 ? " disc: " : " discs: ");
    add_u64(moves);
    add_str(" moves, hash ");
    add_hex(hash);
    line[len] = 0;
    puts(line);
    len = 0;
  }

  // Wrong moves are refused and counted.
  reset(3);
  move(1, 0);
  move(0, 1);
  move(0, 1);
  MSIL_EXPECT(illegal == 2 && moves == 1);
  put_kv("illegal moves refused", illegal);
  return (int)moves8;
}
