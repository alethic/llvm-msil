/* threads.c: deterministic multi-threaded test shapes (plan input for Phase 3 thread tests).
   usage: threads <mode> [threads] [iterations]
   modes: atomic    N threads do atomic_fetch_add; prints the exact total (deterministic)
          plain     the same with a non-atomic increment; prints how many updates were lost
          tls       each thread bumps a _Thread_local counter; main checks per-thread values
          pingpong  two threads hand a token back and forth through a seq_cst atomic
   Every deterministic mode prints only after joining, from main. */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
typedef HANDLE thr_t;
#define THR_RET unsigned __stdcall
#define THR_ARG void *
static void thr_start(thr_t *t, unsigned (__stdcall *f)(void *), void *a) {
  *t = (HANDLE)_beginthreadex(NULL, 0, f, a, 0, NULL);
}
static void thr_join(thr_t t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
#define THR_RETURN return 0
#else
#include <pthread.h>
typedef pthread_t thr_t;
#define THR_RET void *
#define THR_ARG void *
static void thr_start(thr_t *t, void *(*f)(void *), void *a) { pthread_create(t, NULL, f, a); }
static void thr_join(thr_t t) { pthread_join(t, NULL); }
#define THR_RETURN return NULL
#endif

enum { MAXT = 16 };
static long iters;
static atomic_long acounter;
static volatile long pcounter;
static _Thread_local long tl = 7;
static long tl_result[MAXT];
static long *tl_addr[MAXT];
static atomic_int token;
static long pp_rounds;
static atomic_int go;

static THR_RET atomic_worker(THR_ARG a) {
  (void)a;
  while (!atomic_load(&go)) {}
  for (long i = 0; i < iters; ++i) atomic_fetch_add_explicit(&acounter, 1, memory_order_relaxed);
  THR_RETURN;
}
static THR_RET plain_worker(THR_ARG a) {
  (void)a;
  while (!atomic_load(&go)) {}
  for (long i = 0; i < iters; ++i) pcounter = pcounter + 1;
  THR_RETURN;
}
static atomic_int arrived;
static int nthreads;
static THR_RET tls_worker(THR_ARG a) {
  long id = (long)(size_t)a;
  tl_addr[id] = (long *)&tl;
  for (long i = 0; i < iters; ++i) tl += 1;
  tl_result[id] = tl - 7;
  /* Barrier: no thread exits before every thread has recorded its address, so the runtime
     cannot reuse one thread's TLS block for the next (the -O0 failure without it). */
  atomic_fetch_add(&arrived, 1);
  while (atomic_load(&arrived) < nthreads) {}
  THR_RETURN;
}
static THR_RET pp_worker(THR_ARG a) {
  int me = (int)(size_t)a;
  for (long r = 0; r < pp_rounds; ++r) {
    while (atomic_load(&token) != me) {}
    atomic_store(&token, 1 - me);
  }
  THR_RETURN;
}

int main(int argc, char **argv) {
  const char *mode = argc > 1 ? argv[1] : "atomic";
  int n = argc > 2 ? atoi(argv[2]) : 4;
  iters = argc > 3 ? atol(argv[3]) : 1000000;
  thr_t t[MAXT];
  if (n > MAXT) n = MAXT;
  if (!strcmp(mode, "atomic") || !strcmp(mode, "plain")) {
    int plain = !strcmp(mode, "plain");
    for (int i = 0; i < n; ++i) thr_start(&t[i], plain ? plain_worker : atomic_worker, NULL);
    atomic_store(&go, 1);
    for (int i = 0; i < n; ++i) thr_join(t[i]);
    long total = plain ? pcounter : atomic_load(&acounter);
    if (plain) printf("plain: lost %ld of %ld\n", (long)n * iters - total, (long)n * iters);
    else printf("atomic: %ld (expected %ld)\n", total, (long)n * iters);
    return plain ? 0 : total != (long)n * iters;
  }
  if (!strcmp(mode, "tls") || !strcmp(mode, "tls-nobarrier")) {
    nthreads = !strcmp(mode, "tls") ? n : 1;
    for (int i = 0; i < n; ++i) thr_start(&t[i], tls_worker, (void *)(size_t)i);
    for (int i = 0; i < n; ++i) thr_join(t[i]);
    int bad = 0, distinct = 1;
    for (int i = 0; i < n; ++i) {
      if (tl_result[i] != iters) bad++;
      for (int j = 0; j < i; ++j) if (tl_addr[i] == tl_addr[j]) distinct = 0;
      if (tl_addr[i] == &tl) distinct = 0;
    }
    printf("tls: %d threads, %d wrong, addresses distinct %d, main's tl %ld\n", n, bad, distinct, tl);
    return bad != 0 || !distinct || tl != 7;
  }
  if (!strcmp(mode, "pingpong")) {
    pp_rounds = iters;
    thr_start(&t[0], pp_worker, (void *)0);
    thr_start(&t[1], pp_worker, (void *)1);
    thr_join(t[0]); thr_join(t[1]);
    printf("pingpong: %ld rounds, token %d\n", pp_rounds, atomic_load(&token));
    return 0;
  }
  return 2;
}
