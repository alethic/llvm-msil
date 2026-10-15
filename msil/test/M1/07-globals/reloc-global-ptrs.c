// Pointers in global initializers (Milestone 8, D12): every one is a
// relocation that <Module>::.cctor applies with stind.i after copying the data
// image. Covered: pointers to scalars, to array elements (positive addends,
// one past the end), to struct members, to other pointers (two and three
// levels), a statically linked list and a cycle (a node pointing to itself),
// a struct table of pointers into another table, pointers to static
// (assembly-field) globals, pointers to zero-initialized (BSS) globals,
// pointers to functions of the module and to the puts and putchar P/Invokes
// (putchar is referenced only from data, so its P/Invoke exists only for the
// relocation). A function relocation stores the value of the function's
// address field ('<msil>fn_<name>'), which <Module>::.cctor sets once with
// ldftn. Function pointers are compared with the addresses taken in code, not
// called; calls through function pointers are covered by
// msil/test/M1/08-fnptr. Indexes come from a volatile global, so
// pointers are loaded at -O2 too. Writes through relocated pointers are read
// back through the globals they point to. MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; [exit 0] means every check
// passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

int putchar(int c);

int x = 10;
int arr[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
long long wide[3] = {-1, -2, -3};
static int s_hidden = 77;
int bss_target[4];

int *px = &x;
int *parr3 = &arr[3];
int *parr_end = &arr[10];
long long *pwide2 = &wide[2];
int *phidden = &s_hidden;
int *pbss = &bss_target[2];
int **ppx = &px;
int ***pppx = &ppx;
char *pbyte = (char *)&arr[1] + 2;

struct point {
  int x;
  long long y;
  char name[6];
};
struct point origin = {1, 2, "orig"};
long long *py = &origin.y;
char *pname = &origin.name[2];

struct node {
  int value;
  struct node *next;
};
struct node n3 = {3, 0};
struct node n2 = {2, &n3};
struct node n1 = {1, &n2};
struct node *list = &n1;
struct node ring = {42, &ring};

// Ranges into arr: begin and end pointers per row.
struct range {
  const char *label;
  int *begin;
  int *end;
};
struct range ranges[] = {
    {"low", &arr[0], &arr[3]},
    {"mid", &arr[3], &arr[7]},
    {"high", &arr[7], &arr[10]},
    {"empty", &arr[5], &arr[5]},
};

int *mixed[] = {&x, 0, &arr[9], &s_hidden, &bss_target[0]};

static NOINLINE int twice(int v) { return 2 * v; }
static NOINLINE int thrice(int v) { return 3 * v; }
int (*fns[])(int) = {twice, thrice, twice};
int (*out_puts)(const char *) = puts;
int (*out_putchar)(int) = putchar;

volatile int zero = 0;

static NOINLINE int range_sum(const struct range *r) {
  int s = 0;
  for (const int *p = r->begin; p != r->end; p++)
    s += *p;
  return s;
}

static int check_scalars(void) {
  MSIL_EXPECT(*px == 10 && px == &x);
  MSIL_EXPECT(*parr3 == 3 && parr3 == &arr[3]);
  MSIL_EXPECT(parr_end - parr3 == 7 && parr_end[-1] == 9);
  MSIL_EXPECT(*pwide2 == -3 && pwide2 == &wide[2]);
  MSIL_EXPECT(*phidden == 77);
  MSIL_EXPECT(pbss == &bss_target[2] && *pbss == 0);
  MSIL_EXPECT(**ppx == 10 && *ppx == px && ***pppx == 10);
  MSIL_EXPECT(pbyte == (char *)arr + 6);
  MSIL_EXPECT(*py == 2 && py == &origin.y);
  MSIL_EXPECT(pname[0] == 'i' && pname[1] == 'g' && pname[2] == '\0');
  return 0;
}

static int check_writes(void) {
  // Writes through relocated pointers land in the globals they point to.
  *px = 11 + zero;
  MSIL_EXPECT(x == 11);
  **ppx += 1;
  MSIL_EXPECT(x == 12 && *px == 12);
  *parr3 = 300 + zero;
  MSIL_EXPECT(arr[3] == 300);
  *phidden = -77 + zero;
  MSIL_EXPECT(s_hidden == -77);
  pbss[1] = 5 + zero;
  MSIL_EXPECT(bss_target[3] == 5);
  *py = 1ll << 40;
  MSIL_EXPECT(origin.y == 1ll << 40 && origin.x == 1);
  // Retarget a pointer at run time; the old target keeps its value.
  px = &arr[9];
  MSIL_EXPECT(**ppx == 9 && x == 12);
  return 0;
}

static int check_list(void) {
  int sum = 0, n = 0;
  for (const struct node *p = list; p; p = p->next) {
    sum = sum * 10 + p->value;
    n++;
  }
  MSIL_EXPECT(n == 3 && sum == 123);
  MSIL_EXPECT(ring.next == &ring && ring.next->next->next->value == 42);
  n3.next = &n1; // close the list into a cycle
  const struct node *p = list;
  for (int i = 0; i < 7 + zero; i++)
    p = p->next;
  MSIL_EXPECT(p == &n2 && p->value == 2);
  return 0;
}

static int check_tables(void) {
  // arr[3] is 300 after check_writes.
  MSIL_EXPECT(range_sum(&ranges[0 + zero]) == 0 + 1 + 2);
  MSIL_EXPECT(range_sum(&ranges[1 + zero]) == 300 + 4 + 5 + 6);
  MSIL_EXPECT(range_sum(&ranges[2 + zero]) == 7 + 8 + 9);
  MSIL_EXPECT(range_sum(&ranges[3 + zero]) == 0);
  MSIL_EXPECT(ranges[2 + zero].end == parr_end);
  MSIL_EXPECT(mixed[0 + zero] == &x && mixed[1 + zero] == 0);
  MSIL_EXPECT(*mixed[2 + zero] == 9 && *mixed[3 + zero] == -77);
  MSIL_EXPECT(mixed[4 + zero] == bss_target);
  return 0;
}

static int check_functions(void) {
  MSIL_EXPECT(fns[0 + zero] == twice && fns[1 + zero] == thrice);
  MSIL_EXPECT(fns[2 + zero] == fns[0 + zero] && fns[0 + zero] != fns[1 + zero]);
  MSIL_EXPECT(out_puts == puts);
  MSIL_EXPECT(out_putchar != 0);
  // The functions themselves still work when called directly.
  MSIL_EXPECT(twice(4 + zero) + thrice(5 + zero) == 23);
  return 0;
}

int main(void) {
  int r;
  if ((r = check_scalars()) != 0)
    return r;
  put_str("scalars ok");
  if ((r = check_writes()) != 0)
    return r;
  put_str("writes ok");
  if ((r = check_list()) != 0)
    return r;
  put_str("list ok");
  if ((r = check_tables()) != 0)
    return r;
  for (int i = 0; i < 4; i++) {
    put_str(ranges[i + zero].label);
    put_i64(range_sum(&ranges[i + zero]));
  }
  put_str("tables ok");
  if ((r = check_functions()) != 0)
    return r;
  put_str("functions ok");
  return 0;
}
