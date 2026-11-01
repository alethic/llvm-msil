// The program of ../../linux-hosted-oracle.test (plan 5.17.3): a pthread,
// two atexit handlers and a .fini_array entry. glibc's exit runs the
// handlers (last registered first), then the dynamic loader's _dl_fini,
// which __libc_start_main registered before main, runs the main program's
// .fini_array; stdout is flushed last, so the transcript shows that order.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static void handler1(void) { puts("atexit handler 1"); }
static void handler2(void) { puts("atexit handler 2"); }
static void fini(void) { puts("fini_array entry"); }

__attribute__((used, section(".fini_array"))) static void (*fini_entry)(void) = fini;

static void *thread_main(void *arg) {
  puts("thread ran");
  return arg;
}

int main(void) {
  pthread_t thread;
  if (pthread_create(&thread, NULL, thread_main, NULL) != 0)
    return 1;
  if (pthread_join(thread, NULL) != 0)
    return 2;
  atexit(handler1);
  atexit(handler2);
  puts("main returns");
  return 3;
}
