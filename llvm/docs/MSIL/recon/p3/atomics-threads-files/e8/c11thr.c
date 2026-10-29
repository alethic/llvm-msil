/* E8c: C11 <threads.h>: thrd_create/thrd_join, mtx_lock/mtx_unlock and call_once (a second callback). */
#include <threads.h>
#include <stdatomic.h>
#ifdef PROTO
#include "proto-atomic.h"
#define IMPORT(m) __attribute__((import_module(m)))
IMPORT(THRLIB) int thrd_create(thrd_t *, thrd_start_t, void *);
IMPORT(THRLIB) int thrd_join(thrd_t, int *);
IMPORT(THRLIB) int mtx_init(mtx_t *, int);
IMPORT(THRLIB) int mtx_lock(mtx_t *);
IMPORT(THRLIB) int mtx_unlock(mtx_t *);
IMPORT(THRLIB) void call_once(once_flag *, void (*)(void));
IMPORT(LIBC) int puts(const char *);
#else
int puts(const char *);
#endif
#define ITERS 200000
static mtx_t m;
static long long guarded;
static _Atomic int once_runs;
static once_flag flag = ONCE_FLAG_INIT;
static void init_once(void) { atomic_fetch_add(&once_runs, 1); }
static int start(void *arg) {
  call_once(&flag, init_once);
  for (int i = 0; i < ITERS; i++) { mtx_lock(&m); guarded++; mtx_unlock(&m); }
  return (int)(long)arg;
}
int main(void) {
  thrd_t t[4]; int sum = 0;
  mtx_init(&m, mtx_plain);
  for (int i = 0; i < 4; i++) thrd_create(&t[i], start, (void *)(long)(i + 1));
  for (int i = 0; i < 4; i++) { int r = 0; thrd_join(t[i], &r); sum += r; }
  puts(guarded == 4LL * ITERS ? "mutex: counter exact" : "mutex: counter WRONG");
  puts(atomic_load(&once_runs) == 1 ? "call_once: ran once" : "call_once: WRONG");
  puts(sum == 10 ? "thrd_join: results ok" : "thrd_join: WRONG");
  return 0;
}
