// __attribute__((alias)) in a whole program (Milestone 12, backlog 2.1 and the
// section 12 row 'D26 amended (whole-program mode, ...)'): MSILPrepareIR
// replaces every use of an alias (calls, function pointers, initializers,
// llvm.global_ctors, other aliases) by its aliasee and erases the alias; a
// function alias with external or weak linkage also gets a forwarding method
// under its own name that calls the aliasee. Before M12 every alias was the
// error 'alias @<name> is not supported for MSIL'. Covered, from backlog
// calias.c: an external, a static and a weak function alias, and an external
// alias of an array, read and written through the alias and seen through the
// original; also an alias of an alias, an alias of a static variable, aliases
// in a function-pointer table and in initializers of data pointers, and a
// constructor that calls an alias before main runs. No alias is compared with
// its aliasee by address: clang folds that comparison at compile time. Values
// are derived from argc (1 when run without arguments); results are printed;
// MSIL_EXPECT returns 1 + (line % 255) at the first failed check; [exit 0]
// means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

int impl(int x) { return x + 1; }
int pub_alias(int x) __attribute__((alias("impl")));
static int priv_alias(int x) __attribute__((alias("impl")));
__attribute__((weak, alias("impl"))) int weak_alias(int);
int chain_alias(int x) __attribute__((alias("pub_alias")));

int table[4] = {1, 2, 3, 4};
extern int table_alias[4] __attribute__((alias("table")));

static int counter = 10;
extern int counter_alias __attribute__((alias("counter")));

// Function aliases in a table of function pointers, and a data alias in the
// initializer of a pointer.
static int (*const fns[])(int) = {impl, pub_alias, priv_alias, weak_alias,
                                  chain_alias};
int *const table_ptr = &table_alias[2];
int (*fn_ptr)(int) = weak_alias;

static int from_ctor;

// The puts keeps -O2 from evaluating the constructor at compile time.
__attribute__((constructor)) static void init(void) {
  puts("init");
  from_ctor = pub_alias(41);
  counter_alias += 5;
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;

  // From calias.c: 2 + 3 + 4 + 4 - 13 = 0.
  int r = pub_alias(1 * one) + priv_alias(2 * one) + weak_alias(3 * one) +
          table_alias[3] - 13;
  put_kv("calias", r);
  MSIL_EXPECT(r == 0);

  int c = chain_alias(99 * one);
  put_kv("chain_alias(99)", c);
  MSIL_EXPECT(c == 100);

  int sum = 0;
  for (int i = 0; i < 5; i++)
    sum += fns[i](i * 10 * one);
  put_kv("table of aliases", sum);
  MSIL_EXPECT(sum == 105);
  put_kv("fn_ptr(7)", fn_ptr(7 * one));
  MSIL_EXPECT(fn_ptr(7 * one) == 8);

  // Writes through the alias are seen through the array and the other way.
  table_alias[1] = 20 * one;
  table[2] = 30 * one;
  put_kv("table[1]", table[1]);
  put_kv("table_alias[2]", table_alias[2]);
  put_kv("*table_ptr", *table_ptr);
  MSIL_EXPECT(table[1] == 20 && table_alias[2] == 30 && *table_ptr == 30);

  put_kv("from_ctor", from_ctor);
  MSIL_EXPECT(from_ctor == 42);
  put_kv("counter", counter);
  MSIL_EXPECT(counter == 15);
  counter_alias *= 2 * one;
  MSIL_EXPECT(counter == 30);
  return 0;
}
