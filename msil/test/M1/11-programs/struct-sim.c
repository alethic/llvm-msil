// A particle simulation on structs passed and returned by value (Milestone
// 10 program corpus, Design.md section 12 "struct-heavy code"): 2-D vectors
// (8 bytes: one integer register on SysV x86-64 and Win64), particles (24
// bytes: byval and sret on SysV, a pointer to a caller-made copy and sret on
// Win64), boxes (16 bytes: two integer registers on SysV), pairs of
// particles (48 bytes) and statistics records (40 bytes) all go in and out
// of noinline functions by value; a centre of mass is returned as a struct
// of two doubles (two SSE registers on SysV; the sums are exact and the
// quotient is rounded once, so the printed results are the same on every
// runtime); a struct holding an array of
// four vectors and an 8-byte struct holding a char array are returned by
// value. The world is a global array of structs with a brace initializer;
// eight particles fall under gravity, bounce off the walls of a box with
// damping and collide pairwise (integer elastic exchange with truncating
// division) for 300 steps. Statistics measured over the whole array and
// merged from two halves must agree; a callee's changes to its by-value
// copy must not reach the caller. Positions are in 1/256 units; values are
// printed; MSIL_EXPECT returns 1 + (line % 255) at the first failed check;
// when every check passes, the exit status is a checksum of the printed
// values folded into 8 bits (D37).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

volatile int zero = 0;
static unsigned check;
static int collisions, bounces;

struct vec2 {
  int x, y;
};
struct particle {
  struct vec2 pos, vel;
  int mass;
  char tag;
};
struct box {
  struct vec2 lo, hi;
};
struct pair {
  struct particle a, b;
};
struct stats {
  int count, mass;
  long long px, py, kinetic;
  struct vec2 lo;
};
struct vec2d {
  double x, y;
};
struct quad {
  struct vec2 corner[4];
};
struct label {
  char text[8];
};

static NOINLINE struct vec2 v_add(struct vec2 a, struct vec2 b) {
  a.x += b.x;
  a.y += b.y;
  return a;
}

static NOINLINE struct vec2 v_sub(struct vec2 a, struct vec2 b) {
  struct vec2 r = {a.x - b.x, a.y - b.y};
  return r;
}

static NOINLINE long long v_dot(struct vec2 a, struct vec2 b) {
  return (long long)a.x * b.x + (long long)a.y * b.y;
}

static NOINLINE struct particle p_step(struct particle p, struct vec2 accel) {
  p.vel = v_add(p.vel, accel);
  p.pos = v_add(p.pos, p.vel);
  return p;
}

// Reflects the position at each wall and damps that velocity component.
static NOINLINE struct particle p_bounce(struct particle p, struct box b) {
  if (p.pos.x < b.lo.x || p.pos.x > b.hi.x) {
    p.pos.x = p.pos.x < b.lo.x ? 2 * b.lo.x - p.pos.x : 2 * b.hi.x - p.pos.x;
    p.vel.x = -p.vel.x * 7 / 8;
    bounces++;
  }
  if (p.pos.y < b.lo.y || p.pos.y > b.hi.y) {
    p.pos.y = p.pos.y < b.lo.y ? 2 * b.lo.y - p.pos.y : 2 * b.hi.y - p.pos.y;
    p.vel.y = -p.vel.y * 7 / 8;
    bounces++;
  }
  return p;
}

static int exchange(int m1, int u1, int m2, int u2) {
  return ((m1 - m2) * u1 + 2 * m2 * u2) / (m1 + m2);
}

// Two particles closer than radius and approaching exchange momentum.
static NOINLINE struct pair p_collide(struct pair pr, int radius) {
  struct vec2 d = v_sub(pr.b.pos, pr.a.pos);
  if (v_dot(d, d) > (long long)radius * radius)
    return pr;
  if (v_dot(d, v_sub(pr.b.vel, pr.a.vel)) >= 0)
    return pr;
  struct particle a = pr.a, b = pr.b;
  pr.a.vel.x = exchange(a.mass, a.vel.x, b.mass, b.vel.x);
  pr.a.vel.y = exchange(a.mass, a.vel.y, b.mass, b.vel.y);
  pr.b.vel.x = exchange(b.mass, b.vel.x, a.mass, a.vel.x);
  pr.b.vel.y = exchange(b.mass, b.vel.y, a.mass, a.vel.y);
  collisions++;
  return pr;
}

static NOINLINE struct stats measure(const struct particle *ps, int n) {
  struct stats s = {0, 0, 0, 0, 0, {1 << 30, 1 << 30}};
  for (int i = 0; i < n; i++) {
    struct particle p = ps[i];
    s.count++;
    s.mass += p.mass;
    s.px += (long long)p.mass * p.vel.x;
    s.py += (long long)p.mass * p.vel.y;
    s.kinetic += p.mass * v_dot(p.vel, p.vel) / 2;
    s.lo.x = p.pos.x < s.lo.x ? p.pos.x : s.lo.x;
    s.lo.y = p.pos.y < s.lo.y ? p.pos.y : s.lo.y;
  }
  return s;
}

static NOINLINE struct stats merge(struct stats a, struct stats b) {
  a.count += b.count;
  a.mass += b.mass;
  a.px += b.px;
  a.py += b.py;
  a.kinetic += b.kinetic;
  a.lo.x = b.lo.x < a.lo.x ? b.lo.x : a.lo.x;
  a.lo.y = b.lo.y < a.lo.y ? b.lo.y : a.lo.y;
  return a;
}

static int same_stats(struct stats a, struct stats b) {
  return a.count == b.count && a.mass == b.mass && a.px == b.px &&
         a.py == b.py && a.kinetic == b.kinetic && a.lo.x == b.lo.x &&
         a.lo.y == b.lo.y;
}

// Every coordinate is a multiple of 1/256 and every sum stays far below
// 2^53, so the sums are exact; the quotient is rounded once (IEEE).
static NOINLINE struct vec2d centre(const struct particle *ps, int n) {
  double mx = 0, my = 0, m = 0;
  for (int i = 0; i < n; i++) {
    mx += (double)ps[i].mass * ps[i].pos.x;
    my += (double)ps[i].mass * ps[i].pos.y;
    m += ps[i].mass;
  }
  struct vec2d c = {mx / m, my / m};
  return c;
}

static NOINLINE struct quad corners(struct box b) {
  struct quad q;
  q.corner[0] = b.lo;
  q.corner[1].x = b.hi.x;
  q.corner[1].y = b.lo.y;
  q.corner[2] = b.hi;
  q.corner[3].x = b.lo.x;
  q.corner[3].y = b.hi.y;
  return q;
}

static NOINLINE struct label make_label(char tag, int index) {
  struct label l;
  l.text[0] = tag;
  l.text[1] = '#';
  l.text[2] = (char)('0' + index / 10);
  l.text[3] = (char)('0' + index % 10);
  for (int i = 4; i < 8; i++)
    l.text[i] = 0;
  return l;
}

#define N 8
static struct particle world[N] = {
    {{20 * 256, 100 * 256}, {3 * 256, 0}, 1, 'a'},
    {{60 * 256, 90 * 256}, {-2 * 256, 256}, 2, 'b'},
    {{100 * 256, 80 * 256}, {256, -256}, 3, 'c'},
    {{140 * 256, 70 * 256}, {-3 * 256, 2 * 256}, 1, 'd'},
    {{180 * 256, 60 * 256}, {-256, -3 * 256}, 2, 'e'},
    {{40 * 256, 30 * 256}, {2 * 256, 3 * 256}, 4, 'f'},
    {{120 * 256, 20 * 256}, {-256 / 2, 4 * 256}, 1, 'g'},
    {{160 * 256, 40 * 256}, {256 / 4, 256}, 5, 'h'},
};
static const struct box arena = {{0, 0}, {200 * 256, 120 * 256}};
static const struct vec2 gravity = {0, -24};

static void print_stats(int t, struct stats s) {
  put_kv("step", t);
  put_kv("momentum x", s.px);
  put_kv("momentum y", s.py);
  put_kv("kinetic", s.kinetic);
  put_kv("lowest y", s.lo.y);
  check = check * 31u + (unsigned)s.px + (unsigned)s.py * 3u +
          (unsigned)s.kinetic + (unsigned)s.lo.y;
}

int main(void) {
  // A by-value parameter is the callee's own copy.
  struct particle q = world[1];
  struct particle r = p_step(q, gravity);
  MSIL_EXPECT(q.pos.x == 60 * 256 && q.vel.y == 256 && r.vel.y == 256 - 24);
  MSIL_EXPECT(r.pos.x == 58 * 256 && r.pos.y == 90 * 256 + 256 - 24);
  MSIL_EXPECT(r.mass == 2 && r.tag == 'b');

  struct quad c = corners(arena);
  MSIL_EXPECT(c.corner[1].x == 200 * 256 && c.corner[3].y == 120 * 256);
  long long area = (long long)(c.corner[2].x - c.corner[0].x) *
                   (c.corner[2].y - c.corner[0].y) / (256 * 256);
  put_kv("arena area", area);

  struct stats s0 = measure(world, N);
  print_stats(0, s0);
  for (int t = 1; t <= 300; t++) {
    for (int i = 0; i < N; i++)
      world[i] = p_bounce(p_step(world[i], gravity), arena);
    for (int i = 0; i < N; i++)
      for (int j = i + 1; j < N; j++) {
        struct pair pr = {world[i], world[j]};
        pr = p_collide(pr, 6 * 256 + zero);
        world[i] = pr.a;
        world[j] = pr.b;
      }
    if (t % 100 == 0) {
      struct stats all = measure(world, N);
      struct stats halves = merge(measure(world, N / 2),
                                  measure(world + N / 2, N - N / 2));
      MSIL_EXPECT(same_stats(all, halves) && all.mass == s0.mass);
      print_stats(t, all);
    }
  }
  put_kv("collisions", collisions);
  put_kv("bounces", bounces);
  check = check * 31u + (unsigned)collisions * 5u + (unsigned)bounces;

  for (int i = 0; i < N; i++) {
    struct label l = make_label(world[i].tag, i);
    put_str(l.text);
    put_kv("x", world[i].pos.x);
    put_kv("y", world[i].pos.y);
    MSIL_EXPECT(world[i].pos.x >= 0 && world[i].pos.x <= 200 * 256);
    MSIL_EXPECT(world[i].pos.y >= 0 && world[i].pos.y <= 120 * 256);
    check = check * 31u + (unsigned)world[i].pos.x + (unsigned)world[i].pos.y;
  }
  struct vec2d cm = centre(world, N);
  put_kv("centre x * 16", (long long)(cm.x * 16));
  put_kv("centre y * 16", (long long)(cm.y * 16));
  check = check * 31u + (unsigned)(long long)(cm.x * 16);
  return (int)((check ^ check >> 8 ^ check >> 16 ^ check >> 24) & 0xff);
}
