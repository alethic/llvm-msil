// Struct-heavy code (Milestone 9, Design.md section 12 "struct-heavy
// code"): small vector and matrix types passed and returned by value
// through many calls, and a linked list of records kept in a static pool.
// vec3 (three doubles, 24 bytes) is byval and sret on SysV x86-64 and a
// pointer to a caller-made copy plus sret on Win64; mat3 is 72 bytes. The
// arithmetic uses integers and halves only, so every double is exact and
// the printed values (scaled to integers) are the same everywhere. Covered:
// vector add/sub/scale/dot/cross and a reflection, matrix-vector and
// matrix-matrix products by value, a transpose identity check, a particle
// simulation that updates an array of structs through value-returning
// helpers, and a pool-allocated list with sorted insertion, removal, reuse
// of freed nodes and an aggregate fold over the list. Exit status: a
// checksum of the printed values, modulo 256.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

volatile int zero = 0;
static unsigned check;

static void show(const char *name, long long v) {
  check = check * 31u + (unsigned)v;
  put_kv(name, v);
}

typedef struct {
  double x, y, z;
} vec3;

typedef struct {
  vec3 row[3];
} mat3;

NOINLINE vec3 v3(double x, double y, double z) {
  vec3 r = {x, y, z};
  return r;
}
NOINLINE vec3 vadd(vec3 a, vec3 b) {
  a.x += b.x;
  a.y += b.y;
  a.z += b.z;
  return a;
}
NOINLINE vec3 vsub(vec3 a, vec3 b) {
  return v3(a.x - b.x, a.y - b.y, a.z - b.z);
}
NOINLINE vec3 vscale(vec3 a, double k) {
  vec3 r;
  r.x = a.x * k;
  r.y = a.y * k;
  r.z = a.z * k;
  return r;
}
NOINLINE double vdot(vec3 a, vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
NOINLINE vec3 vcross(vec3 a, vec3 b) {
  return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x);
}
// Reflection of d about a unit-axis normal n: d - 2 (d.n) n.
NOINLINE vec3 vreflect(vec3 d, vec3 n) {
  return vsub(d, vscale(n, 2.0 * vdot(d, n)));
}

static long long vkey(vec3 v) {
  return (long long)(v.x * 2) * 1000000 + (long long)(v.y * 2) * 1000 +
         (long long)(v.z * 2);
}

NOINLINE vec3 mulmv(mat3 m, vec3 v) {
  return v3(vdot(m.row[0], v), vdot(m.row[1], v), vdot(m.row[2], v));
}
NOINLINE mat3 transpose(mat3 m) {
  mat3 t;
  t.row[0] = v3(m.row[0].x, m.row[1].x, m.row[2].x);
  t.row[1] = v3(m.row[0].y, m.row[1].y, m.row[2].y);
  t.row[2] = v3(m.row[0].z, m.row[1].z, m.row[2].z);
  return t;
}
NOINLINE mat3 mulmm(mat3 a, mat3 b) {
  mat3 bt = transpose(b);
  mat3 r;
  for (int i = 0; i < 3; i++)
    r.row[i] = v3(vdot(a.row[i], bt.row[0]), vdot(a.row[i], bt.row[1]),
                  vdot(a.row[i], bt.row[2]));
  return r;
}
static long long mkey(mat3 m) {
  long long k = 0;
  for (int i = 0; i < 3; i++)
    k = k * 7 + vkey(m.row[i]);
  return k;
}

// Particles: position and velocity by value; the step returns a new state.
struct particle {
  vec3 pos, vel;
  int id;
};

NOINLINE struct particle step(struct particle p, vec3 gravity) {
  p.vel = vadd(p.vel, gravity);
  p.pos = vadd(p.pos, p.vel);
  if (p.pos.y < 0) { // bounce off the floor
    p.pos.y = -p.pos.y;
    p.vel = vreflect(p.vel, v3(0, 1, 0));
  }
  return p;
}

// A sorted list of records in a static pool, with a free list.
struct rec {
  vec3 v;
  long long key;
  int next; // index into pool, -1 ends the list
};
static struct rec pool[16];
static int free_head = -1, used;

static int alloc_rec(void) {
  if (free_head >= 0) {
    int i = free_head;
    free_head = pool[i].next;
    return i;
  }
  return used < 16 ? used++ : -1;
}

NOINLINE int insert(int head, vec3 v) {
  int n = alloc_rec();
  pool[n].v = v;
  pool[n].key = vkey(v);
  int *link = &head;
  while (*link >= 0 && pool[*link].key < pool[n].key)
    link = &pool[*link].next;
  pool[n].next = *link;
  *link = n;
  return head;
}

NOINLINE int remove_key(int head, long long key) {
  int *link = &head;
  while (*link >= 0 && pool[*link].key != key)
    link = &pool[*link].next;
  if (*link >= 0) {
    int dead = *link;
    *link = pool[dead].next;
    pool[dead].next = free_head;
    free_head = dead;
  }
  return head;
}

NOINLINE vec3 fold(int head) {
  vec3 sum = v3(0, 0, 0);
  for (int i = head; i >= 0; i = pool[i].next)
    sum = vadd(vscale(sum, 0.5), pool[i].v);
  return sum;
}

int main(void) {
  vec3 a = v3(1 + zero, 2, 3), b = v3(-4, 0.5, 6);
  show("add", vkey(vadd(a, b)));
  show("sub", vkey(vsub(a, b)));
  show("scale", vkey(vscale(b, -3)));
  show("dot", (long long)(vdot(a, b) * 2));
  vec3 c = vcross(a, b);
  show("cross", vkey(c));
  MSIL_EXPECT(vdot(c, a) == 0 && vdot(c, b) == 0);
  show("reflect", vkey(vreflect(v3(3, -4, 5), v3(0, 1, 0))));
  a = vadd(a, a);
  MSIL_EXPECT(a.x == 2 && a.y == 4 && a.z == 6 && b.x == -4);

  mat3 m;
  m.row[0] = v3(1, 2, 0);
  m.row[1] = v3(0, 1 + zero, 3);
  m.row[2] = v3(-1, 0, 1);
  mat3 rot; // 90 degrees about z
  rot.row[0] = v3(0, -1, 0);
  rot.row[1] = v3(1, 0, 0);
  rot.row[2] = v3(0, 0, 1);
  show("m*v", vkey(mulmv(m, v3(1, 1, 1))));
  show("m*m", mkey(mulmm(m, m)));
  show("rot^4", mkey(mulmm(mulmm(rot, rot), mulmm(rot, rot))));
  MSIL_EXPECT(mkey(transpose(transpose(m))) == mkey(m));
  MSIL_EXPECT(mkey(mulmm(rot, transpose(rot))) ==
              mkey(mulmm(transpose(rot), rot)));
  mat3 m2 = m;
  m = mulmm(m, rot);
  MSIL_EXPECT(mkey(m2) != mkey(m) && m2.row[1].y == 1);

  struct particle ps[4];
  for (int i = 0; i < 4; i++) {
    ps[i].pos = v3(i, 10 + i, 0);
    ps[i].vel = v3(1, i - 1.5, 0.5 * i);
    ps[i].id = i + zero;
  }
  vec3 g = v3(0, -1, 0);
  for (int t = 0; t < 12; t++)
    for (int i = 0; i < 4; i++)
      ps[i] = step(ps[i], g);
  for (int i = 0; i < 4; i++) {
    show("pos", vkey(ps[i].pos));
    show("vel", vkey(ps[i].vel));
    MSIL_EXPECT(ps[i].id == i);
  }

  int head = -1;
  for (int i = 0; i < 10; i++)
    head = insert(head, v3((i * 7) % 10, i, -i + zero));
  head = remove_key(head, vkey(v3(4, 2, -2)));
  head = remove_key(head, vkey(v3(1, 3, -3)));
  head = remove_key(head, vkey(v3(5, 5, 5))); // not in the list
  head = insert(head, v3(0.5, 0.5, 0.5));
  head = insert(head, v3(9, 9, 9));
  int n = 0;
  for (int i = head; i >= 0; i = pool[i].next) {
    show("list", pool[i].key);
    n++;
  }
  MSIL_EXPECT(n == 10 && used == 10 && free_head == -1);
  show("fold", vkey(fold(head)));
  return (int)(check & 0xff);
}
