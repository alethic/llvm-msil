// Function pointers stored in structs (Milestone 9): calls through members
// are managed calli. Covered: a hand-written vtable (a const struct of
// function pointers in data, set from address fields) shared by objects that
// point to it, with methods taking the object pointer, returning long long
// and void; objects of two "classes" in one array dispatched through their
// vtables; overriding one entry by copying a vtable into a writable local
// struct; an object whose methods are members of the object itself, filled
// at run time; a struct holding a callback and its argument passed by value
// and returned by value (16 bytes: two eightbytes on SysV x86-64, a hidden
// pointer on Win64), then called through the copy; and an array of
// callbacks inside a struct in a static pool, chained like a pipeline.
// MSIL_EXPECT returns 1 + (line % 255) at the first failed check; [exit 0]
// means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

volatile int zero = 0;

struct shape;
struct shape_ops {
  const char *name;
  long long (*area)(const struct shape *);
  long long (*perimeter)(const struct shape *);
  void (*scale)(struct shape *, int);
};

struct shape {
  const struct shape_ops *ops;
  int w, h; // a rectangle's sides, or a right triangle's legs
};

static long long rect_area(const struct shape *s) {
  return (long long)s->w * s->h;
}
static long long rect_perimeter(const struct shape *s) {
  return 2LL * (s->w + s->h);
}
static void rect_scale(struct shape *s, int k) {
  s->w *= k;
  s->h *= k;
}
// Twice the area keeps the result integral.
static long long tri_area2(const struct shape *s) {
  return (long long)s->w * s->h;
}
static long long tri_legs(const struct shape *s) { return s->w + s->h; }
static void tri_scale(struct shape *s, int k) { s->w *= k; }

static const struct shape_ops rect_ops = {"rect", rect_area, rect_perimeter,
                                          rect_scale};
static const struct shape_ops tri_ops = {"tri", tri_area2, tri_legs, tri_scale};

NOINLINE long long total_area(const struct shape *v, int n) {
  long long t = 0;
  for (int i = 0; i < n; i++)
    t += v[i].ops->area(&v[i]);
  return t;
}

static long long square_perimeter(const struct shape *s) { return 4LL * s->w; }

// An object that carries its own methods.
struct counter {
  int value;
  void (*inc)(struct counter *);
  int (*get)(const struct counter *);
};
static void inc_one(struct counter *c) { c->value += 1; }
static void inc_ten(struct counter *c) { c->value += 10; }
static int get_value(const struct counter *c) { return c->value; }
static int get_negated(const struct counter *c) { return -c->value; }

// A callback with its argument, passed and returned by value.
struct thunk {
  long long (*fn)(long long);
  long long arg;
};
static long long cube(long long x) { return x * x * x; }
static long long minus(long long x) { return -x; }

NOINLINE long long call_thunk(struct thunk t) { return t.fn(t.arg); }
NOINLINE struct thunk make_thunk(int which, long long arg) {
  struct thunk t;
  t.fn = which ? cube : minus;
  t.arg = arg;
  return t;
}
NOINLINE struct thunk bump_thunk(struct thunk t) {
  t.arg += 1;
  return t;
}

// A pipeline of stages in a static pool.
struct pipeline {
  int (*stage[4])(int);
  int n;
};
static int p_add3(int x) { return x + 3; }
static int p_mul5(int x) { return x * 5; }
static int p_sub7(int x) { return x - 7; }
static int p_sq(int x) { return x * x; }
static struct pipeline pool[3];

static int run_pipeline(const struct pipeline *p, int x) {
  for (int i = 0; i < p->n; i++)
    x = p->stage[i](x);
  return x;
}

int main(void) {
  struct shape shapes[4];
  shapes[0].ops = &rect_ops;
  shapes[0].w = 3 + zero;
  shapes[0].h = 4;
  shapes[1].ops = &tri_ops;
  shapes[1].w = 6;
  shapes[1].h = 5 + zero;
  shapes[2].ops = &rect_ops;
  shapes[2].w = 10;
  shapes[2].h = 10;
  shapes[3].ops = &tri_ops;
  shapes[3].w = 7;
  shapes[3].h = 2;
  for (int i = 0; i < COUNT(shapes); i++) {
    const struct shape *s = &shapes[i + zero];
    put_str(s->ops->name);
    put_kv("area", s->ops->area(s));
    put_kv("perimeter", s->ops->perimeter(s));
  }
  MSIL_EXPECT(total_area(shapes, 4) == 12 + 30 + 100 + 14);
  for (int i = 0; i < COUNT(shapes); i++)
    shapes[i].ops->scale(&shapes[i], i + 2);
  put_kv("scaled area", total_area(shapes, 4));
  MSIL_EXPECT(shapes[0].w == 6 && shapes[0].h == 8 && shapes[1].w == 18);
  MSIL_EXPECT(shapes[1].h == 5 && shapes[3].w == 35 && shapes[3].h == 2);

  // Override one entry in a writable copy of a vtable.
  struct shape_ops square_ops = rect_ops;
  square_ops.name = "square";
  square_ops.perimeter = square_perimeter;
  struct shape sq;
  sq.ops = &square_ops;
  sq.w = sq.h = 9 + zero;
  MSIL_EXPECT(sq.ops->area(&sq) == 81 && sq.ops->perimeter(&sq) == 36);
  MSIL_EXPECT(sq.ops->scale == rect_scale && square_ops.area == rect_area);
  MSIL_EXPECT(square_ops.perimeter != rect_ops.perimeter);
  put_str(sq.ops->name);

  struct counter c;
  c.value = 0;
  c.inc = zero ? inc_ten : inc_one;
  c.get = get_value;
  for (int i = 0; i < 5; i++)
    c.inc(&c);
  MSIL_EXPECT(c.get(&c) == 5);
  c.inc = inc_ten;
  c.inc(&c);
  c.get = get_negated;
  MSIL_EXPECT(c.get(&c) == -15);
  put_kv("counter", c.get(&c));

  struct thunk t = make_thunk(1 + zero, 11);
  MSIL_EXPECT(t.fn == cube && t.arg == 11);
  MSIL_EXPECT(call_thunk(t) == 1331);
  struct thunk u = bump_thunk(t);
  MSIL_EXPECT(t.arg == 11 && u.arg == 12 && u.fn == cube);
  put_kv("thunk", call_thunk(u));
  put_kv("thunk", call_thunk(make_thunk(zero, 1LL << 40)));
  put_kv("thunk", u.fn(call_thunk(bump_thunk(make_thunk(zero, -3)))));

  pool[0].stage[0] = p_add3;
  pool[0].stage[1] = p_mul5;
  pool[0].n = 2;
  pool[1].stage[0] = p_sq;
  pool[1].stage[1] = p_sub7;
  pool[1].stage[2] = p_mul5;
  pool[1].stage[3] = p_add3;
  pool[1].n = 4;
  pool[2] = pool[1];
  pool[2].stage[0] = pool[0].stage[1];
  pool[2].n = 3;
  for (int i = 0; i < COUNT(pool); i++)
    put_kv("pipeline", run_pipeline(&pool[i + zero], 4 + i));
  MSIL_EXPECT(run_pipeline(&pool[zero], 1) == 20);
  MSIL_EXPECT(pool[2].stage[0] == p_mul5 && pool[2].stage[1] == p_sub7);
  return 0;
}
