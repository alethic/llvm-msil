// Dispatch through global tables of function pointers (Milestone 9): the
// tables are data whose elements <Module>::.cctor sets from the functions'
// address fields, and every call through an element is a managed calli.
// Covered: a const table of long long binary operators indexed by values
// read at run time, its parallel table of names, a two-dimensional table, a
// writable table of void handlers changed at run time with a null entry
// that is skipped, a table of structs pairing a name, a function and an
// argument (looked up by name), and a small bytecode interpreter whose
// opcode handlers are called through a const table and update the machine
// state through a pointer (factorial, sum of 1..100 and gcd programs).
// Indexes come from a volatile global, so -O2 keeps the indirect calls.
// Exit status: a checksum of the results, modulo 256.
// RUN: %{msil_c}
#include "msil_test.h"

#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

volatile int zero = 0;
static unsigned check;

static void note(const char *name, long long v) {
  check = check * 31u + (unsigned)v;
  put_kv(name, v);
}

typedef long long (*binop)(long long, long long);

static long long op_add(long long a, long long b) { return a + b; }
static long long op_sub(long long a, long long b) { return a - b; }
static long long op_mul(long long a, long long b) { return a * b; }
static long long op_div(long long a, long long b) { return b ? a / b : 0; }
static long long op_mod(long long a, long long b) { return b ? a % b : 0; }
static long long op_min(long long a, long long b) { return a < b ? a : b; }
static long long op_max(long long a, long long b) { return a > b ? a : b; }
long long op_shl(long long a, long long b) {
  return (long long)((unsigned long long)a << (b & 63));
}

static binop const ops[] = {op_add, op_sub, op_mul, op_div,
                            op_mod, op_min, op_max, op_shl};
static const char *const op_names[] = {"add", "sub", "mul", "div",
                                       "mod", "min", "max", "shl"};
static binop const grid[2][3] = {{op_add, op_sub, op_mul},
                                 {op_max, op_min, op_shl}};

// Writable table of handlers; entry 2 starts out null.
static int events;
static void on_a(int v) { events = events * 10 + v; }
static void on_b(int v) { events = events * 10 + 2 * v; }
static void on_c(int v) { events -= v; }
void (*handlers[4])(int) = {on_a, on_b, 0, on_c};

static void fire_all(int v) {
  for (int i = 0; i < COUNT(handlers); i++)
    if (handlers[i + zero])
      handlers[i + zero](v);
}

// A table of structs mixing data and function pointers.
static int sq(int x) { return x * x; }
static int cube(int x) { return x * x * x; }
static int neg(int x) { return -x; }
struct command {
  const char *name;
  int (*run)(int);
  int arg;
};
struct command commands[] = {
    {"square", sq, 12}, {"cube", cube, -5}, {"negate", neg, 77}, {"sq", sq, 3}};

static int same(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

static long long run_command(const char *name) {
  for (int i = 0; i < COUNT(commands); i++)
    if (same(commands[i + zero].name, name))
      return commands[i + zero].run(commands[i + zero].arg);
  return -1;
}

// A bytecode interpreter: each handler returns the next pc, or -1 to halt.
enum { PUSH, ADD, SUB, MUL, MOD, DUP, OVER, SWAP, DROP, JNZ, OUT, HALT };
struct vm {
  long long stack[16];
  int sp;
  const int *code;
  long long out[4];
  int nout;
};
typedef int (*handler)(struct vm *, int);

static int h_push(struct vm *m, int pc) {
  m->stack[m->sp++] = m->code[pc + 1];
  return pc + 2;
}
static int h_arith(struct vm *m, int pc) {
  long long b = m->stack[--m->sp];
  long long a = m->stack[m->sp - 1];
  int op = m->code[pc];
  m->stack[m->sp - 1] = op == ADD   ? a + b
                        : op == SUB ? a - b
                        : op == MUL ? a * b
                                    : a % b;
  return pc + 1;
}
static int h_dup(struct vm *m, int pc) {
  m->stack[m->sp] = m->stack[m->sp - 1];
  m->sp++;
  return pc + 1;
}
static int h_over(struct vm *m, int pc) {
  m->stack[m->sp] = m->stack[m->sp - 2];
  m->sp++;
  return pc + 1;
}
static int h_swap(struct vm *m, int pc) {
  long long t = m->stack[m->sp - 1];
  m->stack[m->sp - 1] = m->stack[m->sp - 2];
  m->stack[m->sp - 2] = t;
  return pc + 1;
}
static int h_drop(struct vm *m, int pc) {
  m->sp--;
  return pc + 1;
}
static int h_jnz(struct vm *m, int pc) {
  return m->stack[--m->sp] != 0 ? m->code[pc + 1] : pc + 2;
}
static int h_out(struct vm *m, int pc) {
  m->out[m->nout++] = m->stack[--m->sp];
  return pc + 1;
}
static int h_halt(struct vm *m, int pc) {
  (void)m;
  (void)pc;
  return -1;
}

static handler const dispatch[] = {h_push,  h_arith, h_arith, h_arith,
                                   h_arith, h_dup,   h_over,  h_swap,
                                   h_drop,  h_jnz,   h_out,   h_halt};

static long long run(const int *code, int *steps) {
  struct vm m;
  m.sp = 0;
  m.nout = 0;
  m.code = code;
  int pc = 0;
  *steps = 0;
  while (pc >= 0) {
    pc = dispatch[code[pc] + zero](&m, pc);
    ++*steps;
  }
  return m.nout ? m.out[0] : -1;
}

// acc = 1; n = 15; do { acc *= n; n -= 1; } while (n); out(acc)
static const int prog_fact[] = {PUSH, 1,    PUSH, 15,  SWAP, OVER,
                                MUL,  SWAP, PUSH, 1,   SUB,  DUP,
                                JNZ,  4,    DROP, OUT, HALT};
// sum = 0; n = 100; do { sum += n; n -= 1; } while (n); out(sum)
static const int prog_sum[] = {PUSH, 0,    PUSH, 100, DUP,  JNZ,  8,
                               HALT, SWAP, OVER, ADD, SWAP, PUSH, 1,
                               SUB,  DUP,  JNZ,  8,   DROP, OUT,  HALT};
// gcd(1071, 462) by repeated remainder: a b -> b (a % b) until b == 0
static const int prog_gcd[] = {PUSH, 1071, PUSH, 462,  SWAP, OVER, MOD,
                               DUP,  JNZ,  4,    DROP, OUT,  HALT};

int main(void) {
  static const long long xs[] = {1000, -7, 1LL << 40, 3};
  static const long long ys[] = {7, 1000, 9, 70};
  for (int i = 0; i < COUNT(ops); i++)
    for (int j = 0; j < COUNT(xs); j++)
      check = check * 7u + (unsigned)ops[i + zero](xs[j], ys[j]);
  for (int i = 0; i < COUNT(ops); i++)
    note(op_names[i + zero], ops[i + zero](xs[i % 4], ys[(i + 1) % 4]));
  MSIL_EXPECT(ops[zero + 7](1, 62) == 1LL << 62);
  MSIL_EXPECT(ops[zero + 3](-1000, 7) == -142 && ops[zero + 4](-1000, 7) == -6);

  long long g = 0;
  for (int r = 0; r < 2; r++)
    for (int c = 0; c < 3; c++)
      g = g * 100 + grid[r + zero][c + zero](6 + r, 2 + c) % 100;
  note("grid", g);

  events = 0;
  fire_all(3);
  note("events", events);
  handlers[2] = on_a;
  handlers[3] = 0;
  fire_all(4);
  note("events", events);
  handlers[zero] = handlers[3 + zero];
  fire_all(1);
  note("events", events);
  MSIL_EXPECT(handlers[0] == 0 && handlers[2] == on_a && handlers[1] == on_b);

  note("square", run_command("square"));
  note("cube", run_command("cube"));
  note("negate", run_command("negate"));
  note("sq", run_command("sq"));
  note("missing", run_command("squares"));
  commands[1].run = sq;
  note("cube->sq", run_command("cube"));

  int steps;
  note("fact", run(prog_fact, &steps));
  note("fact steps", steps);
  MSIL_EXPECT(run(prog_fact, &steps) == 1307674368000LL);
  note("sum", run(prog_sum, &steps));
  note("sum steps", steps);
  note("gcd", run(prog_gcd, &steps));
  note("gcd steps", steps);

  check ^= check >> 16;
  check ^= check >> 8;
  return (int)(check & 0xff);
}
