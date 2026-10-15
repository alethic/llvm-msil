// Basic struct use from C (Milestone 9): layouts follow the native ABI of
// the triple (clang's native TargetInfo), and every access is a load or
// store at an offset. Covered: sizeof, alignment and offsetof for structs
// of mixed member widths, with padding and a trailing array (values that
// are the same on SysV x86-64 and Win64); member access through '.', '->'
// and array elements; struct assignment; designated and positional
// initializers in globals (data image) and locals; compound literals;
// anonymous struct and union members (C11); a self-referential list built
// in an array; a flexible array member in a static buffer; and comparison
// of structs member by member. Results are printed; MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; [exit 0] means every check
// passed.
// RUN: %{msil_c}
#include "msil_test.h"

#include <stddef.h>

volatile int zero = 0;

struct mixed {
  char c;
  long long ll;
  short s;
  int i;
  unsigned char tail[3];
  double d;
};

struct small {
  char a;
  short b;
  char c;
};

struct with_anon {
  int kind;
  union {
    int as_int;
    float as_float;
  };
  struct {
    short lo, hi;
  };
};

struct node {
  int value;
  struct node *next;
};

struct packet {
  int len;
  unsigned char data[];
};

struct point {
  int x, y;
};
struct rect {
  struct point min, max;
  const char *label;
};

struct rect g_rects[] = {
    {{0, 0}, {10, 5}, "wide"},
    {.label = "tall", .max = {.y = 30, .x = 2}, .min.x = -2},
    [3] = {{1, 1}, {4, 4}, "square"},
};
const struct point g_origin = {0, 0};
struct mixed g_mixed = {'m', -1234567890123LL, -300, 70000, {1, 2, 3}, 0.5};

static int area(const struct rect *r) {
  return (r->max.x - r->min.x) * (r->max.y - r->min.y);
}

static int same_point(struct point a, struct point b) {
  return a.x == b.x && a.y == b.y;
}

int main(void) {
  put_kv("sizeof mixed", sizeof(struct mixed));
  put_kv("align mixed", _Alignof(struct mixed));
  put_kv("off ll", offsetof(struct mixed, ll));
  put_kv("off s", offsetof(struct mixed, s));
  put_kv("off i", offsetof(struct mixed, i));
  put_kv("off tail", offsetof(struct mixed, tail));
  put_kv("off d", offsetof(struct mixed, d));
  put_kv("sizeof small", sizeof(struct small));
  put_kv("sizeof with_anon", sizeof(struct with_anon));
  put_kv("off hi", offsetof(struct with_anon, hi));
  put_kv("sizeof packet", sizeof(struct packet));
  put_kv("sizeof rect", sizeof(struct rect));
  MSIL_EXPECT(sizeof(struct mixed) == 40 && offsetof(struct mixed, d) == 32);

  // Globals with designated initializers.
  for (int i = 0; i < 4; i++) {
    const struct rect *r = &g_rects[i + zero];
    put_str(r->label ? r->label : "(null)");
    put_kv("area", area(r));
  }
  MSIL_EXPECT(g_rects[1].min.x == -2 && g_rects[1].min.y == 0);
  MSIL_EXPECT(g_rects[1].max.x == 2 && g_rects[2].label == 0);
  MSIL_EXPECT(g_mixed.c == 'm' && g_mixed.ll == -1234567890123LL);
  MSIL_EXPECT(g_mixed.s == -300 && g_mixed.i == 70000 && g_mixed.d == 0.5);
  MSIL_EXPECT(g_mixed.tail[0] + g_mixed.tail[1] * 10 + g_mixed.tail[2] * 100 ==
              321);

  // Locals: member stores and loads, assignment, compound literals.
  struct mixed m;
  m.c = (char)(zero - 1);
  m.ll = (1LL << 62) + zero;
  m.s = (short)(zero + 32767);
  m.i = zero - 2147483647;
  m.tail[0] = 255;
  m.tail[1] = (unsigned char)(zero + 256 + 7);
  m.tail[2] = 9;
  m.d = -0.125;
  struct mixed n = m;
  m.ll = 0;
  m.tail[2] = 0;
  MSIL_EXPECT(n.c == -1 && n.ll == 1LL << 62 && n.s == 32767);
  MSIL_EXPECT(n.i == -2147483647 && n.tail[0] == 255 && n.tail[1] == 7);
  MSIL_EXPECT(n.tail[2] == 9 && n.d == -0.125 && m.ll == 0);

  struct rect local = {{-1, -1}, {zero + 3, 3}, "local"};
  struct rect copy = local;
  copy.max.x = 9;
  put_kv(local.label, area(&local));
  put_kv("copy", area(&copy));
  struct point p = (struct point){zero + 5, 6};
  MSIL_EXPECT(same_point(p, (struct point){5, 6}));
  MSIL_EXPECT(!same_point(p, g_origin) && same_point(g_origin, g_rects[0].min));
  MSIL_EXPECT(area(&(struct rect){{0, 0}, {7, zero + 8}, 0}) == 56);

  // Anonymous members.
  struct with_anon wa;
  wa.kind = 1;
  wa.as_float = 1.0f;
  wa.lo = (short)(zero - 2);
  wa.hi = 3;
  put_hex((unsigned)wa.as_int);
  MSIL_EXPECT(wa.lo == -2 && wa.hi == 3 && wa.as_int == 0x3f800000);

  // A list linked through an array; reversed in place.
  struct node nodes[6];
  for (int i = 0; i < 6; i++) {
    nodes[i].value = (i + 1) * 11 + zero;
    nodes[i].next = i < 5 ? &nodes[i + 1] : 0;
  }
  struct node *head = &nodes[0], *prev = 0;
  while (head) {
    struct node *next = head->next;
    head->next = prev;
    prev = head;
    head = next;
  }
  long long seq = 0;
  for (struct node *it = prev; it; it = it->next)
    seq = seq * 100 + it->value;
  put_kv("reversed", seq);
  MSIL_EXPECT(prev == &nodes[5] && nodes[0].next == 0);

  // A flexible array member over a static buffer.
  static union {
    struct packet pk;
    unsigned char bytes[64];
  } buf;
  struct packet *pk = &buf.pk;
  pk->len = 20 + zero;
  for (int i = 0; i < pk->len; i++)
    pk->data[i] = (unsigned char)(i * 13);
  unsigned sum = 0;
  for (int i = 0; i < pk->len; i++)
    sum += pk->data[i];
  put_kv("packet sum", sum);
  MSIL_EXPECT(buf.bytes[sizeof(struct packet) + 19] ==
              (unsigned char)(19 * 13));
  return 0;
}
