// A bytecode virtual machine that dispatches through a global table of
// function pointers (Design.md section 12): 'optable' is a writable global
// array of 18 handler addresses, so its elements are relocations to
// functions that <Module>::.cctor sets, and every instruction is a call
// through it. Programs are assembled from a source form with numbered labels
// (two passes) into a global code array and run on a machine with a data
// stack, a return stack for CALL/RET and global memory cells: the primes
// below 60 by trial division, Fibonacci numbers from a recursive bytecode
// subroutine, and Euclid's gcd. At run time the OUT handler is then replaced
// by one that records instead of printing (the recorded values must match),
// and a null MOD entry must stop the machine with a fault. Prints the
// outputs and how often each opcode ran. MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; on success the exit status is
// a checksum of the outputs and the opcode counts, modulo 256 (re-check that
// it is not 1 + (line % 255) for an MSIL_EXPECT line after edits that move
// lines).
// RUN: %{msil_c}
#include "msil_test.h"

#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

enum {
  PUSH, LOAD, STORE, ADD, SUB, MUL, MOD, LT, DUP, DROP, SWAP, OVER,
  JMP, JZ, CALL, RET, OUT, HALT, NOPS,
  LABEL = 100 // assembler pseudo-op: LABEL n defines label n
};

struct vm {
  int pc, sp, rsp, running, fault;
  int stack[64];
  int rstack[32];
};

typedef void (*handler)(struct vm *);

static int code[256];
static int code_len;
static int mem[8];
static unsigned long long counts[NOPS];
static unsigned check = 1;
static int recorded[32];
static int nrecorded;

static void fault(struct vm *m) {
  m->fault = 1;
  m->running = 0;
}

static int operand(struct vm *m) {
  if (m->pc < code_len)
    return code[m->pc++];
  fault(m);
  return 0;
}

static void push(struct vm *m, int v) {
  if (m->sp < COUNT(m->stack))
    m->stack[m->sp++] = v;
  else
    fault(m);
}

static int pop(struct vm *m) {
  if (m->sp > 0)
    return m->stack[--m->sp];
  fault(m);
  return 0;
}

static void op_push(struct vm *m) { push(m, operand(m)); }
static void op_load(struct vm *m) { push(m, mem[operand(m) & 7]); }
static void op_store(struct vm *m) {
  int a = operand(m);
  mem[a & 7] = pop(m);
}
static void op_add(struct vm *m) {
  unsigned b = (unsigned)pop(m), a = (unsigned)pop(m);
  push(m, (int)(a + b));
}
static void op_sub(struct vm *m) {
  unsigned b = (unsigned)pop(m), a = (unsigned)pop(m);
  push(m, (int)(a - b));
}
static void op_mul(struct vm *m) {
  unsigned b = (unsigned)pop(m), a = (unsigned)pop(m);
  push(m, (int)(a * b));
}
static void op_mod(struct vm *m) {
  int b = pop(m), a = pop(m);
  if (b > 0)
    push(m, a % b);
  else
    fault(m);
}
static void op_lt(struct vm *m) {
  int b = pop(m), a = pop(m);
  push(m, a < b);
}
static void op_dup(struct vm *m) {
  int a = pop(m);
  push(m, a);
  push(m, a);
}
static void op_drop(struct vm *m) { pop(m); }
static void op_swap(struct vm *m) {
  int b = pop(m), a = pop(m);
  push(m, b);
  push(m, a);
}
static void op_over(struct vm *m) {
  int b = pop(m), a = pop(m);
  push(m, a);
  push(m, b);
  push(m, a);
}
static void op_jmp(struct vm *m) { m->pc = operand(m); }
static void op_jz(struct vm *m) {
  int target = operand(m);
  if (pop(m) == 0)
    m->pc = target;
}
static void op_call(struct vm *m) {
  int target = operand(m);
  if (m->rsp < COUNT(m->rstack)) {
    m->rstack[m->rsp++] = m->pc;
    m->pc = target;
  } else {
    fault(m);
  }
}
static void op_ret(struct vm *m) {
  if (m->rsp > 0)
    m->pc = m->rstack[--m->rsp];
  else
    fault(m);
}
static void op_out(struct vm *m) {
  int v = pop(m);
  put_i64(v);
  check = check * 31u + (unsigned)v;
}
static void op_out_record(struct vm *m) {
  int v = pop(m);
  if (nrecorded < COUNT(recorded))
    recorded[nrecorded++] = v;
}
static void op_halt(struct vm *m) { m->running = 0; }

handler optable[NOPS] = {op_push, op_load, op_store, op_add,  op_sub,
                         op_mul,  op_mod,  op_lt,    op_dup,  op_drop,
                         op_swap, op_over, op_jmp,   op_jz,   op_call,
                         op_ret,  op_out,  op_halt};
static const char *const opnames[NOPS] = {
    "PUSH", "LOAD", "STORE", "ADD", "SUB", "MUL",  "MOD", "LT",  "DUP",
    "DROP", "SWAP", "OVER",  "JMP", "JZ",  "CALL", "RET", "OUT", "HALT"};

static int has_operand(int op) {
  return op == PUSH || op == LOAD || op == STORE || op == JMP || op == JZ ||
         op == CALL || op == LABEL;
}

// Two passes: label addresses first, then the code with resolved targets.
static int assemble(const int *src, int n) {
  int labels[16];
  for (int i = 0; i < 16; i++)
    labels[i] = -1;
  int pc = 0;
  for (int i = 0; i < n; i += has_operand(src[i]) ? 2 : 1) {
    if (src[i] == LABEL)
      labels[src[i + 1] & 15] = pc;
    else
      pc += has_operand(src[i]) ? 2 : 1;
  }
  code_len = 0;
  for (int i = 0; i < n; i += has_operand(src[i]) ? 2 : 1) {
    int op = src[i];
    if (op == LABEL)
      continue;
    code[code_len++] = op;
    if (has_operand(op)) {
      int v = src[i + 1];
      if (op == JMP || op == JZ || op == CALL) {
        v = labels[v & 15];
        if (v < 0)
          return -1;
      }
      code[code_len++] = v;
    }
  }
  return code_len;
}

// Returns the number of instructions executed, or -1 after a fault.
static int run(void) {
  struct vm m;
  m.pc = m.sp = m.rsp = m.fault = 0;
  m.running = 1;
  int steps = 0;
  while (m.running) {
    if (m.pc < 0 || m.pc >= code_len || ++steps > 100000) {
      fault(&m);
      break;
    }
    int op = code[m.pc++];
    if (op < 0 || op >= NOPS || !optable[op]) {
      fault(&m);
      break;
    }
    counts[op]++;
    optable[op](&m);
  }
  return m.fault ? -1 : steps;
}

// mem[0] = n, mem[1] = d; print n if no d with d * d <= n divides it.
static const int primes[] = {
    PUSH, 2, STORE, 0,
    LABEL, 1, LOAD, 0, PUSH, 60, LT, JZ, 9,
    PUSH, 2, STORE, 1,
    LABEL, 2, LOAD, 0, LOAD, 1, DUP, MUL, LT, JZ, 3,
    LOAD, 0, OUT, JMP, 4,
    LABEL, 3, LOAD, 0, LOAD, 1, MOD, JZ, 4,
    LOAD, 1, PUSH, 1, ADD, STORE, 1, JMP, 2,
    LABEL, 4, LOAD, 0, PUSH, 1, ADD, STORE, 0, JMP, 1,
    LABEL, 9, HALT};

// for (n = 0; n < 16; n++) out(fib(n)), fib being a recursive subroutine
// (n -- fib(n)).
static const int fibs[] = {
    PUSH, 0, STORE, 0,
    LABEL, 1, LOAD, 0, PUSH, 16, LT, JZ, 9,
    LOAD, 0, CALL, 5, OUT,
    LOAD, 0, PUSH, 1, ADD, STORE, 0, JMP, 1,
    LABEL, 9, HALT,
    LABEL, 5, DUP, PUSH, 2, LT, JZ, 6, RET,
    LABEL, 6, DUP, PUSH, 1, SUB, CALL, 5,
    SWAP, PUSH, 2, SUB, CALL, 5, ADD, RET};

// gcd (a b -- g): while (b) { a, b = b, a % b; }
static const int gcds[] = {
    PUSH, 1071, PUSH, 462, CALL, 7, OUT,
    PUSH, 270, PUSH, 192, CALL, 7, OUT,
    PUSH, 17, PUSH, 5, CALL, 7, OUT,
    PUSH, 832040, PUSH, 514229, CALL, 7, OUT,
    HALT,
    LABEL, 7, DUP, JZ, 8, SWAP, OVER, MOD, JMP, 7,
    LABEL, 8, DROP, RET};

int main(void) {
  put_str("primes");
  MSIL_EXPECT(assemble(primes, COUNT(primes)) > 0);
  int steps = run();
  MSIL_EXPECT(steps > 0);
  put_kv("steps", steps);

  put_str("fibonacci");
  MSIL_EXPECT(assemble(fibs, COUNT(fibs)) > 0);
  steps = run();
  MSIL_EXPECT(steps > 0);
  put_kv("steps", steps);

  put_str("gcd");
  MSIL_EXPECT(assemble(gcds, COUNT(gcds)) > 0);
  steps = run();
  MSIL_EXPECT(steps > 0);
  put_kv("steps", steps);

  // Swap the OUT handler at run time: the outputs are recorded instead.
  optable[OUT] = op_out_record;
  MSIL_EXPECT(assemble(fibs, COUNT(fibs)) > 0 && run() > 0);
  optable[OUT] = op_out;
  MSIL_EXPECT(nrecorded == 16);
  for (int i = 0, a = 0, b = 1; i < 16; i++) {
    MSIL_EXPECT(recorded[i] == a);
    int t = a + b;
    a = b;
    b = t;
  }

  // A null entry faults instead of calling through it (the primes 2 and 3
  // are printed before the first MOD).
  put_str("null MOD handler");
  optable[MOD] = 0;
  MSIL_EXPECT(assemble(primes, COUNT(primes)) > 0 && run() < 0);
  optable[MOD] = op_mod;

  put_str("opcode counts");
  for (int op = 0; op < NOPS; op++) {
    put_kv(opnames[op], (long long)counts[op]);
    check = check * 31u + (unsigned)counts[op];
  }
  check ^= check >> 16;
  check ^= check >> 8;
  return (int)(check & 0xff);
}
