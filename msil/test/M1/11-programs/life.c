// Conway's Game of Life on a global toroidal grid (Milestone 10 program
// corpus, Design.md section 12): two global 16x32 grids reached through
// global pointers initialized to their addresses (relocations in the data
// segment) and swapped after every generation; patterns described by a
// global table of structs that point to arrays of pointers to string
// literals (nested relocations); the neighbour count computed with modular
// wrap-around and, as a cross-check, with precomputed neighbour index
// tables; a bounding box returned by value. Checks: a block is still, a
// blinker has period 2, a glider moves by (1, 1) every 4 generations and is
// back where it started after 128 generations on this torus. Then a world of
// five patterns runs for 100 generations, printing the population every 10
// generations and the grid at the start and the end. MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; when every check passes, the
// exit status is a checksum of the printed values folded into 8 bits (D37).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

#define W 32
#define H 16

typedef unsigned char row_t[W];

volatile int zero = 0;
static unsigned check;

static unsigned char grids[2][H][W];
static row_t *cur = grids[0];
static row_t *nxt = grids[1];
static int xprev[W], xnext[W], yprev[H], ynext[H];
static int mismatches;

struct pattern {
  const char *name;
  int rows;
  const char *const *cells;
};
static const char *const glider[] = {".#.", "..#", "###"};
static const char *const blinker[] = {"###"};
static const char *const block[] = {"##", "##"};
static const char *const rpentomino[] = {".##", "##.", ".#."};
static const char *const toad[] = {".###", "###."};
static const struct pattern patterns[] = {
    {"glider", 3, glider},         {"blinker", 1, blinker},
    {"block", 2, block},           {"r-pentomino", 3, rpentomino},
    {"toad", 2, toad},
};
enum { GLIDER, BLINKER, BLOCK, RPENTOMINO, TOAD };

struct box {
  int x0, y0, x1, y1;
};

static void clear(void) {
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      cur[y][x] = 0;
}

static NOINLINE void place(int which, int x, int y) {
  const struct pattern *p = &patterns[which];
  for (int r = 0; r < p->rows; r++)
    for (const char *c = p->cells[r]; *c; c++)
      if (*c == '#')
        cur[(y + r) % H][(x + (int)(c - p->cells[r])) % W] = 1;
}

static NOINLINE int neighbours(row_t *g, int x, int y) {
  int n = 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if (dx || dy)
        n += g[(y + dy + H) % H][(x + dx + W) % W];
  return n;
}

static NOINLINE int neighbours_table(row_t *g, int x, int y) {
  const unsigned char *up = g[yprev[y]], *mid = g[y], *down = g[ynext[y]];
  return up[xprev[x]] + up[x] + up[xnext[x]] + mid[xprev[x]] + mid[xnext[x]] +
         down[xprev[x]] + down[x] + down[xnext[x]];
}

static NOINLINE void step(void) {
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int n = neighbours(cur, x, y);
      mismatches += n != neighbours_table(cur, x, y);
      nxt[y][x] = (unsigned char)(n == 3 || (n == 2 && cur[y][x]));
    }
  row_t *t = cur;
  cur = nxt;
  nxt = t;
}

static int population(void) {
  int n = 0;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      n += cur[y][x];
  return n;
}

static NOINLINE unsigned grid_hash(void) {
  unsigned h = 2166136261u;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      h = (h ^ cur[y][x]) * 16777619u;
  return h;
}

// Bounding box of the live cells (assumes they do not wrap).
static NOINLINE struct box bounds(void) {
  struct box b = {W, H, -1, -1};
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      if (cur[y][x]) {
        b.x0 = x < b.x0 ? x : b.x0;
        b.y0 = y < b.y0 ? y : b.y0;
        b.x1 = x > b.x1 ? x : b.x1;
        b.y1 = y > b.y1 ? y : b.y1;
      }
  return b;
}

static void show(const char *title) {
  put_str(title);
  char line[W + 1];
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++)
      line[x] = cur[y][x] ? '#' : '.';
    line[W] = 0;
    put_str(line);
  }
  check = check * 31u + grid_hash();
}

// Places a pattern and prints its name and position as name=x*100+y.
static void add(int which, int x, int y) {
  place(which, x, y);
  put_kv(patterns[which].name, x * 100 + y);
}

static int run_alone(int which, int gens) {
  clear();
  place(which, 10 + zero, 6);
  for (int g = 0; g < gens; g++)
    step();
  return population();
}

int main(void) {
  for (int x = 0; x < W; x++) {
    xprev[x] = (x + W - 1) % W;
    xnext[x] = (x + 1) % W;
  }
  for (int y = 0; y < H; y++) {
    yprev[y] = (y + H - 1) % H;
    ynext[y] = (y + 1) % H;
  }

  // Still life, oscillators and the glider, each alone on the torus.
  run_alone(BLOCK, 0);
  unsigned h0 = grid_hash();
  MSIL_EXPECT(run_alone(BLOCK, 5) == 4 && grid_hash() == h0);
  run_alone(BLINKER, 0);
  h0 = grid_hash();
  run_alone(BLINKER, 1);
  unsigned h1 = grid_hash();
  MSIL_EXPECT(run_alone(BLINKER, 2) == 3 && grid_hash() == h0 && h1 != h0);
  MSIL_EXPECT(run_alone(TOAD, 2) == 6);
  run_alone(GLIDER, 0);
  struct box b0 = bounds();
  h0 = grid_hash();
  MSIL_EXPECT(run_alone(GLIDER, 4) == 5);
  struct box b4 = bounds();
  MSIL_EXPECT(b4.x0 == b0.x0 + 1 && b4.y0 == b0.y0 + 1);
  MSIL_EXPECT(b4.x1 == b0.x1 + 1 && b4.y1 == b0.y1 + 1);
  MSIL_EXPECT(run_alone(GLIDER, 128) == 5 && grid_hash() == h0);
  put_kv("glider x0", b4.x0);
  put_kv("glider y0", b4.y0);

  // A world of five patterns.
  clear();
  add(GLIDER, 1, 1);
  add(BLINKER, 8, 3);
  add(BLOCK, 14, 12);
  add(RPENTOMINO, 22, 6);
  add(TOAD, 26, 13);
  show("generation 0");
  for (int g = 1; g <= 100; g++) {
    step();
    if (g % 10 == 0) {
      int pop = population();
      put_kv("population", pop);
      check = check * 31u + (unsigned)pop;
    }
  }
  show("generation 100");
  MSIL_EXPECT(mismatches == 0);
  return (int)((check ^ check >> 8 ^ check >> 16 ^ check >> 24) & 0xff);
}
