// Brace-initialized local aggregates (Milestone 8): clang turns most of them
// into a private constant global ('__const.<function>.<name>') copied into the
// frame with llvm.memcpy (cpblk), the case M7's tests had to avoid; larger
// mostly-zero ones become llvm.memset plus stores. Covered: int, long long,
// unsigned char and double arrays, a partially initialized array (zero tail),
// a sparse array with designated initializers, structs with padding, an
// array of structs, a nested aggregate, a char array initialized from a
// string literal, a sized char array with a short literal (zero tail) and a
// const local array indexed at run time. The function that declares them runs
// three times and modifies its copies each time: every call must start from
// the initializers again, so the constant globals must never change. Results
// are printed; the exit status is a checksum of them, modulo 256.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

struct point {
  int x, y;
};

struct record {
  char tag;
  short s;
  int i;
  long long ll;
  double d;
  char name[12];
};

volatile int one = 1;
static unsigned checksum;

static void report(const char *name, long long v) {
  put_kv(name, v);
  checksum = checksum * 31u + (unsigned)v;
}

// A position-dependent hash of the elements.
static NOINLINE long long sum_ints(const int *a, int n) {
  unsigned h = 0;
  for (int i = 0; i < n; i++)
    h = h * 31u + (unsigned)a[i];
  return h;
}

static NOINLINE void show_record(const struct record *r) {
  report("tag", r->tag);
  report("s", r->s);
  report("i", r->i);
  report("ll", r->ll);
  report("d*1000", (long long)(r->d * 1000.0));
  put_str(r->name);
}

static NOINLINE void run(int round) {
  int arr[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  int partial[16] = {9, 8, 7};
  long long lls[4] = {-1, 1ll << 40, 0, 0x7fffffffffffffffll};
  unsigned char bytes[] = {0xff, 0x80, 0x7f, 0x00, 0x01, 0xfe};
  double ds[3] = {0.25, -1.5, 1e10};
  int sparse[200] = {[0] = 5, [100] = 6, [199] = 7};
  struct point pts[3] = {{1, 2}, {3, 4}, {5, 6}};
  struct record rec = {'x', -2, 100000, -5ll, 0.5, "record"};
  struct {
    int a[3];
    struct point p;
    const char *label;
  } nested = {{9, 8, 7}, {6, 5}, "nested"};
  char text[] = "a char array initialized from a string literal";
  char sized[32] = "short";
  const short lookup[6] = {10, 20, 30, 40, 50, 60};

  put_kv("round", round);
  report("arr", sum_ints(arr, COUNT(arr)));
  report("partial", sum_ints(partial, COUNT(partial)));
  report("lls[1]", lls[1]);
  report("lls[3]", lls[3]);
  long long b = 0;
  for (int i = 0; i < COUNT(bytes); i++)
    b = b * 256 + bytes[i];
  report("bytes", b);
  report("ds*1000", (long long)((ds[0] + ds[1]) * 1000.0 + ds[2] / 1e6));
  report("sparse", sum_ints(sparse, COUNT(sparse)));
  report("sparse[100]", sparse[100]);
  report("pts", pts[0].x + pts[1].y * 10 + pts[2].x * 100 + pts[2].y * 1000);
  show_record(&rec);
  report("nested", nested.a[0] * 1000 + nested.a[2] * 100 + nested.p.x * 10 +
                       nested.p.y);
  put_str(nested.label);
  put_str(text);
  report("sizeof text", (long long)sizeof text);
  put_str(sized);
  int zeros = 0;
  for (int i = 5; i < COUNT(sized); i++)
    zeros += sized[i] == 0;
  report("sized zero tail", zeros);
  report("lookup", lookup[(round + 2) % 6] + lookup[5 * one]);

  // Change every copy; the next call must see the initializers again.
  for (int i = 0; i < COUNT(arr); i++)
    arr[i] *= -round * one;
  partial[15] = 100 * one;
  lls[1] = 0;
  bytes[0] = 0;
  ds[2] = 0.0;
  sparse[100] = -one;
  pts[1].y = 0;
  rec.tag = 'y';
  rec.name[0] = 'R';
  nested.a[0] = 0;
  text[0] = 'A';
  sized[5] = '!';
  report("modified arr", sum_ints(arr, COUNT(arr)));
  report("modified partial", sum_ints(partial, COUNT(partial)));
  put_str(text);
  put_str(sized);
  put_str(rec.name);
}

int main(void) {
  for (int round = 1; round <= 3 * one; round++)
    run(round);
  return (int)(checksum & 0xff);
}
