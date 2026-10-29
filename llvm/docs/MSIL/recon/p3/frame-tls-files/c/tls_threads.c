/* tls_threads.c: what Phase 3's thread_local must do (a proposed transcript test).
   Each thread sees its own copy, initialized from the template (.tdata values, relocations to a
   global and to a function, .tbss zeros, an over-aligned variable); &tv differs per thread and is
   stable for the thread's life; the main thread's copy is unchanged by the workers. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
typedef HANDLE thr_t;
#else
#include <pthread.h>
typedef pthread_t thr_t;
#endif

int g = 42;
static int twice(int x) { return 2 * x; }
_Thread_local int tcount = 5;                 /* .tdata */
_Thread_local int *tptr = &g;                 /* .tdata + address relocation */
_Thread_local int (*tfn)(int) = twice;        /* .tdata + function-address relocation */
_Thread_local char tzero[100];                /* .tbss */
_Alignas(64) _Thread_local char taligned[8];  /* over-aligned */

#define N 4
static uintptr_t addr[N + 1];
static int seen[N + 1][4];

static void work(int slot) {
  int z = 0;
  for (int i = 0; i < 100; i++) z += tzero[i];
  for (int i = 0; i < 1000 * slot; i++) tcount++;
  memset(tzero, slot, sizeof tzero);
  addr[slot] = (uintptr_t)&tcount;
  seen[slot][0] = tcount;
  seen[slot][1] = *tptr + tfn(1);
  seen[slot][2] = z;
  seen[slot][3] = (int)((uintptr_t)taligned % 64);
}
#ifdef _WIN32
static unsigned __stdcall start(void *p) { work((int)(intptr_t)p); return 0; }
#else
static void *start(void *p) { work((int)(intptr_t)p); return 0; }
#endif

int main(void) {
  thr_t t[N + 1];
  work(0);
  for (int k = 1; k <= N; k++) {
#ifdef _WIN32
    t[k] = (HANDLE)_beginthreadex(0, 0, start, (void *)(intptr_t)k, 0, 0);
#else
    pthread_create(&t[k], 0, start, (void *)(intptr_t)k);
#endif
  }
  for (int k = 1; k <= N; k++) {
#ifdef _WIN32
    WaitForSingleObject(t[k], INFINITE); CloseHandle(t[k]);
#else
    pthread_join(t[k], 0);
#endif
  }
  int distinct = 1;
  for (int a = 0; a <= N; a++)
    for (int b = a + 1; b <= N; b++)
      if (addr[a] == addr[b]) distinct = 0;
  for (int k = 0; k <= N; k++)
    printf("slot %d: tcount=%d *tptr+tfn(1)=%d sum(tzero)=%d taligned%%64=%d\n", k, seen[k][0], seen[k][1], seen[k][2], seen[k][3]);
  printf("distinct addresses: %d; main thread after the workers: tcount=%d tzero[0]=%d\n", distinct, tcount, tzero[0]);
  return 0;
}
