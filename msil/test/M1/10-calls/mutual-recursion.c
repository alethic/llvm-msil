// Mutual recursion (Milestone 9): functions that call each other before
// their definitions are seen (forward declarations with prototypes), so
// every call is a direct call whose signature must match a method defined
// later in the module. Covered: a recursive-descent parser and evaluator
// for integer expressions (expr -> term -> unary -> primary -> expr, with
// a parser state passed by pointer), Hofstadter's female and male
// sequences, a three-way cycle a -> b -> c -> a that carries a long long
// and a double, and is_even/is_odd 1500 calls deep through static
// functions. Inputs come from a volatile global, so -O2 cannot fold the
// results. Values are printed; MSIL_EXPECT returns 1 + (line % 255) at the
// first failed check; [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

volatile int zero = 0;

struct parser {
  const char *p;
  int errors;
};

NOINLINE long long parse_expr(struct parser *ps);
NOINLINE long long parse_term(struct parser *ps);
NOINLINE long long parse_unary(struct parser *ps);
NOINLINE long long parse_primary(struct parser *ps);

static void skip_spaces(struct parser *ps) {
  while (*ps->p == ' ')
    ps->p++;
}

NOINLINE long long parse_expr(struct parser *ps) {
  long long v = parse_term(ps);
  for (;;) {
    skip_spaces(ps);
    if (*ps->p == '+') {
      ps->p++;
      v += parse_term(ps);
    } else if (*ps->p == '-') {
      ps->p++;
      v -= parse_term(ps);
    } else {
      return v;
    }
  }
}

NOINLINE long long parse_term(struct parser *ps) {
  long long v = parse_unary(ps);
  for (;;) {
    skip_spaces(ps);
    char op = *ps->p;
    if (op != '*' && op != '/' && op != '%')
      return v;
    ps->p++;
    long long r = parse_unary(ps);
    if (op == '*')
      v *= r;
    else if (r == 0)
      ps->errors++;
    else if (op == '/')
      v /= r;
    else
      v %= r;
  }
}

NOINLINE long long parse_unary(struct parser *ps) {
  skip_spaces(ps);
  if (*ps->p == '-') {
    ps->p++;
    return -parse_unary(ps);
  }
  return parse_primary(ps);
}

NOINLINE long long parse_primary(struct parser *ps) {
  skip_spaces(ps);
  if (*ps->p == '(') {
    ps->p++;
    long long v = parse_expr(ps);
    skip_spaces(ps);
    if (*ps->p == ')')
      ps->p++;
    else
      ps->errors++;
    return v;
  }
  long long v = 0;
  if (*ps->p < '0' || *ps->p > '9')
    ps->errors++;
  while (*ps->p >= '0' && *ps->p <= '9')
    v = v * 10 + (*ps->p++ - '0');
  return v;
}

static long long eval(const char *text, int *errors) {
  struct parser ps;
  ps.p = text;
  ps.errors = 0;
  long long v = parse_expr(&ps);
  skip_spaces(&ps);
  if (*ps.p != '\0')
    ps.errors++;
  *errors = ps.errors;
  return v;
}

// Hofstadter: F(0) = 1, M(0) = 0, F(n) = n - M(F(n-1)), M(n) = n - F(M(n-1)).
NOINLINE int hof_m(int n);
NOINLINE int hof_f(int n) { return n == 0 ? 1 : n - hof_m(hof_f(n - 1)); }
NOINLINE int hof_m(int n) { return n == 0 ? 0 : n - hof_f(hof_m(n - 1)); }

// A three-way cycle with mixed argument types.
NOINLINE long long cyc_b(long long acc, double w, int n);
NOINLINE long long cyc_c(long long acc, double w, int n);
NOINLINE long long cyc_a(long long acc, double w, int n) {
  return n == 0 ? acc : cyc_b(acc * 3 + 1, w * 0.5, n - 1);
}
NOINLINE long long cyc_b(long long acc, double w, int n) {
  return n == 0 ? acc + (long long)w
                : cyc_c(acc - (long long)(w * 4), w + 1.0, n - 1);
}
NOINLINE long long cyc_c(long long acc, double w, int n) {
  return n == 0 ? -acc : cyc_a(acc ^ 0x5a5a, w, n - 1) + 1;
}

static NOINLINE int is_odd(unsigned n);
static NOINLINE int is_even(unsigned n) { return n == 0 ? 1 : is_odd(n - 1); }
static NOINLINE int is_odd(unsigned n) { return n == 0 ? 0 : is_even(n - 1); }

int main(void) {
  static const char *const exprs[] = {
      "1 + 2 * 3",
      "(1 + 2) * 3",
      "-(4 - 10) * -(2) % 5",
      "100 / 7 / 2 + 100 % 7",
      "((((((((((42))))))))))",
      "2 * (3 + (4 * (5 + (6 * (7 + 8)))))",
      "1000000 * 1000000 * 1000 - 1",
      "--5 - -5",
      "7 / (3 - 3)",
      "(1 + 2",
      "12 34",
  };
  for (int i = 0; i < (int)(sizeof exprs / sizeof exprs[0]); i++) {
    int errors;
    long long v = eval(exprs[i + zero], &errors);
    put_str(exprs[i]);
    put_kv(errors ? "error" : "value", errors ? errors : v);
  }
  int err;
  MSIL_EXPECT(eval("(1 + 2) * 3", &err) == 9 && err == 0);

  long long f = 0, m = 0;
  for (int n = 0; n < 30; n++) {
    f = f * 3 + hof_f(n + zero);
    m = m * 3 + hof_m(n + zero);
  }
  put_kv("hofstadter F", f);
  put_kv("hofstadter M", m);
  MSIL_EXPECT(hof_f(10 + zero) == 6 && hof_m(10 + zero) == 6);
  MSIL_EXPECT(hof_f(25 + zero) == 16 && hof_m(25 + zero) == 16);

  put_kv("cycle", cyc_a(1, 64.0, 30 + zero));
  put_kv("cycle", cyc_a(-7, 1024.0, 31 + zero));

  MSIL_EXPECT(is_even(1500u + zero) == 1 && is_odd(1501u + zero) == 1);
  MSIL_EXPECT(is_even(777u + zero) == 0);
  return 0;
}
