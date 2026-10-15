// Nested aggregates (Milestone 9): structs that contain arrays of structs
// that contain unions and arrays, copied, passed and returned whole.
// Covered: a global inventory with nested designated initializers, string
// pointers (relocations) and zero-filled tails; the same shape built in a
// local from a brace initializer (a constant copied in with memcpy) and
// field by field; whole-object copies followed by changes on one side; an
// element of a nested array passed by value (32 bytes: byval on SysV
// x86-64, a pointer to a copy on Win64) and a whole 272-byte object passed
// and returned by value; a function returning a nested struct whose result
// is indexed directly; pointers into nested members and pointer differences
// between them; and arrays of arrays of structs. Values are printed;
// MSIL_EXPECT returns 1 + (line % 255) at the first failed check; [exit 0]
// means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

volatile int zero = 0;

struct qty {
  int kind; // 0: count, 1: weight in grams, 2: label
  union {
    long long count;
    double grams;
    const char *label;
  } v;
};

struct item {
  const char *name;
  short code[4];
  struct qty q;
};

struct inventory {
  int n;
  struct item items[8];
  struct {
    int year, month;
  } stamp;
};

struct inventory g_inv = {
    .n = 3,
    .items =
        {
            {"bolts", {1, 2, 3, 4}, {0, {.count = 500}}},
            {"flour", {9}, {1, {.grams = 2500.5}}},
            [2] = {.name = "tag", .q = {.kind = 2, .v.label = "fragile"}},
        },
    .stamp = {2026, 10},
};

static long long item_score(const struct item *it) {
  long long s =
      it->code[0] * 1000 + it->code[1] * 100 + it->code[2] * 10 + it->code[3];
  if (it->q.kind == 0)
    s += it->q.v.count * 7;
  else if (it->q.kind == 1)
    s += (long long)(it->q.v.grams * 2);
  else
    s += it->q.v.label[0];
  return s;
}

NOINLINE long long item_by_value(struct item it) {
  long long s = item_score(&it);
  it.code[0] = -1; // only the copy
  it.q.kind = 3;
  return s * 3 + it.code[0] + it.q.kind;
}

NOINLINE long long inventory_score(struct inventory inv) {
  long long s = inv.stamp.year * 100 + inv.stamp.month;
  for (int i = 0; i < inv.n; i++)
    s = s * 5 + item_score(&inv.items[i]);
  inv.n = 0;
  return s;
}

NOINLINE struct inventory restock(struct inventory inv, int extra) {
  for (int i = 0; i < inv.n; i++)
    if (inv.items[i].q.kind == 0)
      inv.items[i].q.v.count += extra;
  if (inv.n < COUNT(inv.items)) {
    struct item *it = &inv.items[inv.n++];
    it->name = "nails";
    for (int k = 0; k < 4; k++)
      it->code[k] = (short)(extra + k);
    it->q.kind = 0;
    it->q.v.count = extra * 10;
  }
  inv.stamp.month++;
  return inv;
}

static void print_inventory(const struct inventory *inv) {
  for (int i = 0; i < inv->n; i++) {
    put_str(inv->items[i].name);
    put_kv("score", item_score(&inv->items[i]));
  }
}

struct grid {
  struct cell {
    unsigned char r, g, b;
    short weight;
  } c[3][4];
};

NOINLINE struct grid make_grid(int seed) {
  struct grid g;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 4; j++) {
      g.c[i][j].r = (unsigned char)(seed + i);
      g.c[i][j].g = (unsigned char)(seed + j);
      g.c[i][j].b = (unsigned char)(i * j);
      g.c[i][j].weight = (short)(seed * 10 - i * 4 - j);
    }
  return g;
}

int main(void) {
  print_inventory(&g_inv);
  MSIL_EXPECT(g_inv.items[1].code[1] == 0 && g_inv.items[2].code[3] == 0);
  MSIL_EXPECT(g_inv.items[3].name == 0 && g_inv.items[7].q.v.count == 0);
  put_kv("inventory", inventory_score(g_inv));
  MSIL_EXPECT(g_inv.n == 3);

  // A local copy of the global, changed independently.
  struct inventory a = g_inv;
  a.items[0].q.v.count = 1 + zero;
  a.items[1].code[0] = 8;
  MSIL_EXPECT(g_inv.items[0].q.v.count == 500 && g_inv.items[1].code[0] == 9);
  put_kv("copy", inventory_score(a));

  // Brace-initialized local of the same shape.
  struct inventory b = {
      1, {{"rope", {5, 5, 5, 5}, {1, {.grams = 0.25}}}}, {1999, 12}};
  b = restock(b, 3 + zero);
  print_inventory(&b);
  MSIL_EXPECT(b.n == 2 && b.stamp.month == 13 && b.items[1].q.v.count == 30);

  struct inventory c = restock(restock(g_inv, 10), 20);
  print_inventory(&c);
  MSIL_EXPECT(c.n == 5 && c.items[0].q.v.count == 530 && g_inv.n == 3);
  MSIL_EXPECT(c.items[3].q.v.count == 120 && c.items[4].q.v.count == 200);

  put_kv("by value", item_by_value(c.items[1]));
  put_kv("by value", item_by_value(g_inv.items[2]));
  MSIL_EXPECT(c.items[1].code[0] == 9 && c.items[1].q.kind == 1);

  // Pointers into nested members.
  short *p = &c.items[2].code[1];
  short *q = &c.items[4].code[3];
  put_kv("distance", q - p);
  MSIL_EXPECT((char *)&c.items[3] - (char *)&c.items[0] ==
              3 * (long long)sizeof(struct item));
  MSIL_EXPECT(&c.items[2].q.v.label == (const char **)&c.items[2].q.v);

  struct grid gr = make_grid(5 + zero);
  long long gs = 0;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 4; j++)
      gs = gs * 3 + gr.c[i][j].r + gr.c[i][j].g * 2 + gr.c[i][j].b * 4 +
           gr.c[i][j].weight;
  put_kv("grid", gs);
  MSIL_EXPECT(make_grid(7).c[2][3].weight == 70 - 8 - 3);
  MSIL_EXPECT(make_grid(zero).c[1][2].b == 2);
  struct cell row[4];
  for (int j = 0; j < 4; j++)
    row[j] = gr.c[2][j];
  gr.c[2][0].r = 0;
  MSIL_EXPECT(row[0].r == 7 && row[3].weight == 50 - 8 - 3);
  return 0;
}
