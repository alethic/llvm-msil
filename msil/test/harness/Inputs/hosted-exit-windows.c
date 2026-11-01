// The program of ../windows-hosted-oracle.test (plan 5.17.3): an atexit
// handler and stdio output through the C runtime, then a non-zero status.
#include <stdio.h>
#include <stdlib.h>

static void handler(void) { puts("atexit handler"); }

int main(void) {
  atexit(handler);
  puts("main returns");
  return 7;
}
