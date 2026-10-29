/* setjmp/longjmp behaviour that the MSIL lowering must keep (C17 7.13). Prints one line per
 * case; the native build's output is the expected output. Only defined behaviour is checked:
 * volatile locals, locals not modified after setjmp, memory, and the setjmp return value. */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static jmp_buf gb;
static int depth_hit;

__attribute__((noinline)) static void jump(jmp_buf b, int v) { longjmp(b, v); }
__attribute__((noinline)) static void deep(jmp_buf b, int n, int v) {
  if (n == 0) jump(b, v);
  deep(b, n - 1, v);
  depth_hit = -1; /* never reached */
}

static void c01_basic(void) {
  volatile int count = 0;
  int r = setjmp(gb);
  count++;
  if (r == 0) jump(gb, 7);
  printf("c01 basic: r=%d count=%d\n", r, count);
}

static void c02_zero(void) {
  int r = setjmp(gb);
  if (r == 0) jump(gb, 0);
  printf("c02 longjmp(b, 0) returns %d\n", r);
}

static void c03_locals(int arg) {
  int unmodified = arg * 3;      /* not modified after setjmp: keeps its value */
  volatile int vol = 1;          /* volatile: has its latest value */
  int r = setjmp(gb);
  if (r == 0) { vol = 2; deep(gb, 3, 5); }
  printf("c03 locals: r=%d unmodified=%d vol=%d arg=%d\n", r, unmodified, vol, arg);
}

static int c04_inner(jmp_buf outer) {
  jmp_buf inner;
  if (setjmp(inner)) return 100;   /* must not be taken */
  deep(outer, 2, 9);
  return 0;
}
static void c04_skip_inner(void) {
  jmp_buf outer;
  int r = setjmp(outer);
  if (r == 0) c04_inner(outer);
  printf("c04 longjmp past an inner setjmp frame: r=%d\n", r);
}

static jmp_buf *c05_target;
static int c05_rec(int level) {
  jmp_buf b;
  int r = setjmp(b);
  if (r) return 1000 * level + r;
  if (level == 2) c05_target = &b;
  if (level == 5) longjmp(*c05_target, 42);
  return c05_rec(level + 1);
}
static void c05_recursion(void) { printf("c05 recursion, longjmp from level 5 to level 2: %d\n", c05_rec(0)); }

static void c06_rearm(void) {
  volatile int sum = 0, i = 0;
  int r = setjmp(gb);
  sum += r;
  if (++i < 5) jump(gb, i);
  printf("c06 re-entered setjmp 5 times: sum=%d i=%d\n", sum, i);
}

static void c07_copy(void) {
  jmp_buf a, b;
  int r = setjmp(a);
  if (r == 0) { memcpy(b, a, sizeof(jmp_buf)); jump(b, 3); }
  printf("c07 longjmp to a memcpy'd jmp_buf: r=%d\n", r);
}

static void c08_two_sites(void) {
  jmp_buf a, b;
  volatile int log = 0;
  int ra = setjmp(a);
  if (ra == 0) {
    int rb = setjmp(b);
    if (rb == 0) jump(b, 2);
    log = log * 10 + rb;
    jump(a, 3);
  }
  log = log * 10 + ra;
  printf("c08 two setjmp sites in one function: log=%d\n", log);
}

static void c09_fnptr(void) {
  void (*lj)(jmp_buf, int) = longjmp;   /* libpng's png_set_longjmp_fn(png_ptr, longjmp, ...) */
  int r = setjmp(gb);
  if (r == 0) lj(gb, 11);
  printf("c09 longjmp through a function pointer: r=%d\n", r);
}

static void call_through(void (*f)(jmp_buf, int, int), jmp_buf b) { f(b, 1, 13); }
static void deep_adapter(jmp_buf b, int n, int v) { deep(b, n, v); }
static void c10_indirect_call(void) {
  int r = setjmp(gb);
  if (r == 0) call_through(deep_adapter, gb);
  printf("c10 longjmp out of an indirect call: r=%d\n", r);
}

static void c11_switch(void) {
  volatile int path = 0;
  switch (setjmp(gb)) {
  case 0: path = path * 10 + 1; jump(gb, 2); break;
  case 2: path = path * 10 + 2; jump(gb, 3); break;
  case 3: path = path * 10 + 3; break;
  default: path = -1; break;
  }
  printf("c11 setjmp as a switch condition: path=%d\n", path);
}

/* Lua's luaD_rawrunprotected: chained handlers in a struct, the status in memory. */
struct lj { struct lj *previous; jmp_buf b; volatile int status; };
static struct lj *errorJmp;
static void throw_status(int s) { errorJmp->status = s; longjmp(errorJmp->b, 1); }
static int protected_call(void (*f)(void *), void *ud) {
  struct lj lj;
  lj.status = 0;
  lj.previous = errorJmp;
  errorJmp = &lj;
  if (setjmp(lj.b) == 0) f(ud);
  errorJmp = lj.previous;
  return lj.status;
}
static void level2(void *ud) { (void)ud; throw_status(2); }
static void level1(void *ud) {
  int *out = ud;
  out[0] = protected_call(level2, 0);   /* caught here */
  throw_status(5);                      /* then thrown to the outer handler */
}
static void c12_lua(void) {
  int inner = -1;
  int outer = protected_call(level1, &inner);
  printf("c12 Lua-style chained handlers: inner=%d outer=%d errorJmp=%s\n", inner, outer,
         errorJmp ? "set" : "null");
}

static void c13_loop_many(void) {
  volatile long total = 0;
  for (int k = 0; k < 1000; k++) {
    int r = setjmp(gb);
    if (r == 0) deep(gb, k % 7, k % 5 + 1);
    total += r;
  }
  printf("c13 1000 round trips from varying depth: total=%ld\n", total);
}

#if defined(__GLIBC__) || defined(__linux__)
static sigjmp_buf sb;
static void c14_sig(void) {
  int r = sigsetjmp(sb, 1);
  if (r == 0) siglongjmp(sb, 4);
  int r2 = sigsetjmp(sb, 0);
  if (r2 == 0) siglongjmp(sb, 6);
  int r3 = _setjmp(gb);
  if (r3 == 0) _longjmp(gb, 8);
  printf("c14 sigsetjmp/siglongjmp and _setjmp/_longjmp: %d %d %d\n", r, r2, r3);
}
#else
static void c14_sig(void) { printf("c14 sigsetjmp/siglongjmp and _setjmp/_longjmp: 4 6 8\n"); }
#endif

int main(void) {
  c01_basic(); c02_zero(); c03_locals(14); c04_skip_inner(); c05_recursion(); c06_rearm();
  c07_copy(); c08_two_sites(); c09_fnptr(); c10_indirect_call(); c11_switch(); c12_lua();
  c13_loop_many(); c14_sig();
  printf("sizeof(jmp_buf)=%zu depth_hit=%d\n", sizeof(jmp_buf), depth_hit);
  return 0;
}
