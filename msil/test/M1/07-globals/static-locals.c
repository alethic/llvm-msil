// Static local variables (Milestone 8, D12): function-scope statics are
// globals with internal linkage (assembly fields named after the function,
// such as 'counter.n'), initialized once from the data image and kept across
// calls. Covered: zero-initialized and initialized counters, two functions
// whose statics have the same name, a static array returned by pointer and
// modified by the caller, a memo table filled by a recursive function, a
// static pointer initialized to the address of another static (a
// relocation), a static string table cycled through, a static in a block
// scope, and a recursion-depth tracker. Calls are made in loops whose
// counts come from a volatile global. Results are printed; the exit status is
// the final value of a counter, modulo 256.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

volatile int one = 1;

static NOINLINE int counter(void) {
  static int n;
  return ++n;
}

static NOINLINE int other_counter(void) {
  static int n = 100;
  n += 10;
  return n;
}

static NOINLINE int *history(void) {
  static int h[4] = {1, 2, 3, 4};
  return h;
}

static NOINLINE long long fib(int k) {
  static long long memo[91];
  if (k < 2)
    return k;
  if (memo[k] == 0)
    memo[k] = fib(k - 1) + fib(k - 2);
  return memo[k];
}

static NOINLINE int *linked(void) {
  static int target = 5;
  static int *p = &target;
  ++*p;
  return p;
}

static NOINLINE const char *next_word(void) {
  static const char *const words[] = {"red", "green", "blue"};
  static unsigned i;
  return words[i++ % 3];
}

static NOINLINE int depth_tracker(int n) {
  static int depth, max_depth;
  depth++;
  if (depth > max_depth)
    max_depth = depth;
  if (n > 0)
    depth_tracker(n - 1);
  depth--;
  return max_depth;
}

static NOINLINE int block_static(int k) {
  int total = 0;
  for (int i = 0; i < k; i++) {
    static int seen;
    seen += i;
    total = seen;
  }
  return total;
}

int main(void) {
  int c = 0;
  for (int i = 0; i < 5 * one; i++)
    c = counter();
  put_kv("counter", c);
  put_kv("other_counter", other_counter());
  put_kv("other_counter", other_counter());
  put_kv("counter", counter());

  // Run-time indexes keep the static array an array at -O2.
  int *h = history();
  put_kv("history[3]", h[3 * one]);
  for (int i = 0; i < 4 * one; i++)
    h[i] *= 10 * one;
  int *h2 = history();
  put_kv("same array", h == h2);
  put_kv("history[0]", h2[0 * one]);
  put_kv("history[3]", h2[3 * one]);

  put_kv("fib(10)", fib(10 * one));
  put_kv("fib(50)", fib(50 * one));
  put_kv("fib(90)", fib(90 * one));
  put_kv("fib(90) again", fib(90 * one));

  int *t = linked();
  put_kv("linked", *t);
  put_kv("linked", *linked());
  put_kv("linked same", t == linked());
  put_kv("linked", *t);

  for (int i = 0; i < 4 * one; i++)
    put_str(next_word());

  put_kv("depth(3)", depth_tracker(3 * one));
  put_kv("depth(1)", depth_tracker(1 * one));
  put_kv("depth(7)", depth_tracker(7 * one));

  put_kv("block_static(4)", block_static(4 * one));
  put_kv("block_static(3)", block_static(3 * one));

  for (int i = 0; i < 200 * one; i++)
    c = counter();
  put_kv("counter", c);
  return c & 0xff;
}
