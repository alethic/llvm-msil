/* bench.c by hand in the shape MSILLowerSjLj would give it (design E1 of the sjlj recon).
 * Three functions are stubs that patch-proto.py replaces with IL:
 *   __msil_sjlj_throw          -> throw new ExternalException(msg, (int)st)
 *   __msil_sjlj_invoke$thrower -> the body inside .try, a filter calling __msil_sjlj_match
 *   (main is unchanged).  Everything else is the real MSIL backend's output. */
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>   /* only for sizeof(jmp_buf) */
#include <stdio.h>

#define MSIL_SJLJ_MAGIC 0x314A534Du /* 'MSJ1' */
struct msil_sjlj_status { int label; int val; };         /* in the setjmp caller's frame */
struct msil_jmp_buf { uint32_t magic; uint32_t label; struct msil_sjlj_status *st; };
_Static_assert(sizeof(struct msil_jmp_buf) <= sizeof(jmp_buf), "layout fits");

static volatile int sink;
static volatile intptr_t stub_sink;

/* IL helper (generated): throws. */
__attribute__((noinline)) void __msil_sjlj_throw(struct msil_sjlj_status *st) { stub_sink = (intptr_t)st; }

/* C helper (static C helper archive): longjmp. */
__attribute__((noinline)) void __msil_longjmp(void *buf, int val) {
  struct msil_jmp_buf *b = buf;
  struct msil_sjlj_status *st = b->st;
  st->val = val ? val : 1;
  st->label = (int)b->label;
  __msil_sjlj_throw(st);
}

/* C helper: called by every wrapper's filter after the BCL type test. */
__attribute__((noinline)) int __msil_sjlj_match(int hresult, struct msil_sjlj_status *st) {
  return hresult == (int)(intptr_t)st && st->label != 0;
}

static jmp_buf env;

/* source: static void thrower(int v) { sink++; longjmp(env, v); } */
__attribute__((noinline)) static void thrower(int v) { sink++; __msil_longjmp(env, v); }

/* generated wrapper (IR function; the backend wraps its body in .try/filter) */
__attribute__((noinline)) static void __msil_sjlj_invoke_thrower(int v, struct msil_sjlj_status *st) {
  stub_sink = (intptr_t)st; /* keeps st alive in this C stand-in; the real wrapper's filter uses it */
  thrower(v);
}

/* source:
 * static int one(int i) { int r = setjmp(env); if (r == 0) { thrower((i & 7) + 1); return -1; } return r; } */
__attribute__((noinline)) static int one(int i) {
  struct msil_sjlj_status st;
  st.label = 0;
  /* setjmp(env), site 1 */
  struct msil_jmp_buf *b = (struct msil_jmp_buf *)env;
  b->magic = MSIL_SJLJ_MAGIC;
  b->label = 1;
  b->st = &st;
  int r = 0;                         /* setjmp.ret = phi [0, entry], [val, dispatch] */
post1:
  if (r == 0) {
    __msil_sjlj_invoke_thrower((i & 7) + 1, &st);
    if (st.label != 0) {             /* dispatch */
      int l = st.label;
      st.label = 0;
      r = st.val;
      switch (l) { case 1: goto post1; }
      __builtin_trap();
    }
    return -1;
  }
  return r;
}

static long parse(const char *s) { long v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }

int main(int argc, char **argv) {
  long n = argc > 1 ? parse(argv[1]) : 1000000, s = 0;
  for (long i = 0; i < n; i++) s += one((int)i);
  puts(s == n / 8 * 36 ? "ok" : "bad");
  return s == n / 8 * 36 ? 0 : 1;
}
