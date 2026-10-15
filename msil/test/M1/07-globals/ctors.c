// C constructors (Milestone 8, D12): llvm.global_ctors entries run from
// <Module>::.cctor before main, after the data image is copied, the BSS tail
// is cleared and the relocations are applied, in increasing priority order
// (__attribute__((constructor(N)))); constructors without a priority run last
// (priority 65535), and constructors of equal priority run in the order they
// are defined. The constructors are defined out of priority order, have
// internal and external linkage, print through puts (so none can be
// evaluated at compile time) and record their order in a BSS array. One reads
// an initialized global and one dereferences a relocated pointer, which must
// already hold their final values; one writes a global that main then reads.
// main prints the recorded order. Exit status: the number of constructors
// that ran.
// RUN: %{msil_c}
#include "msil_test.h"

static int order[16];
static int count;
int seed = 1000;
int target = 7;
int *relocated = &target;
int set_by_ctor;

static void record(int id, const char *text) {
  order[count++] = id;
  puts(text);
}

__attribute__((constructor(300))) static void c300(void) {
  record(300, "ctor 300");
}

__attribute__((constructor)) void c_default_1(void) {
  record(65535, "ctor default (first defined)");
}

__attribute__((constructor(101))) static void c101(void) {
  record(101, "ctor 101: reads the data image");
  put_kv("seed", seed);
  seed += 1;
}

__attribute__((constructor(500))) void c500_a(void) {
  record(500, "ctor 500 (first defined)");
}

__attribute__((constructor(150))) static void c150(void) {
  record(150, "ctor 150: reads through a relocation");
  put_kv("*relocated", *relocated);
  *relocated = 70;
}

__attribute__((constructor(500))) static void c500_b(void) {
  record(501, "ctor 500 (second defined)");
}

__attribute__((constructor)) static void c_default_2(void) {
  record(65536, "ctor default (second defined)");
  set_by_ctor = seed * 2;
}

__attribute__((constructor(1000))) void c1000(void) {
  record(1000, "ctor 1000");
}

int main(void) {
  puts("main");
  for (int i = 0; i < count; i++)
    put_i64(order[i]);
  put_kv("seed", seed);
  put_kv("target", target);
  put_kv("set_by_ctor", set_by_ctor);
  return count;
}
