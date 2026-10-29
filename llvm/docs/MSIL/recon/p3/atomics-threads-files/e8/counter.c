/* E8: a counter incremented by NTHREADS threads with atomic_fetch_add (recon atomics-threads). */
#include <stdatomic.h>
#ifdef PROTO
#include "proto-atomic.h"
#define IMPORT(m) __attribute__((import_module(m)))
#else
#define IMPORT(m)
#endif
#define NTHREADS 4
#ifndef ITERS
#define ITERS 2000000
#endif
typedef __SIZE_TYPE__ size_t;
IMPORT(LIBC) int puts(const char *);
_Atomic long long counter;
_Atomic int started;
long long plain_counter; /* incremented without atomics: shows that the test races */

static void work(void) {
  atomic_fetch_add(&started, 1);
  while (atomic_load(&started) < NTHREADS) { } /* start together */
  for (int i = 0; i < ITERS; i++) {
    atomic_fetch_add_explicit(&counter, 1, memory_order_relaxed);
    plain_counter++;
  }
}
#ifdef _WIN32
typedef unsigned long DWORD;
typedef void *HANDLE;
IMPORT("kernel32.dll") HANDLE CreateThread(void *, size_t, DWORD (*)(void *), void *, DWORD, DWORD *);
IMPORT("kernel32.dll") DWORD WaitForSingleObject(HANDLE, DWORD);
IMPORT("kernel32.dll") int CloseHandle(HANDLE);
static DWORD start(void *arg) { (void)arg; work(); return 0; }
#else
typedef unsigned long pthread_t;
IMPORT("libc.so.6") int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
IMPORT("libc.so.6") int pthread_join(pthread_t, void **);
static void *start(void *arg) { (void)arg; work(); return 0; }
#endif
#ifdef _WIN32
IMPORT("kernel32.dll") int QueryPerformanceCounter(long long *);
IMPORT("kernel32.dll") int QueryPerformanceFrequency(long long *);
static long long now_ns(void) { long long c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (long long)((double)c * 1e9 / (double)f); }
#else
struct timespec_ { long tv_sec, tv_nsec; };
IMPORT("libc.so.6") int clock_gettime(int, struct timespec_ *);
static long long now_ns(void) { struct timespec_ t; clock_gettime(1, &t); return t.tv_sec * 1000000000LL + t.tv_nsec; }
#endif
static void put_num(const char *label, long long v) {
  char buf[64]; char *p = buf + sizeof buf - 1; *p = 0;
  do { *--p = (char)('0' + v % 10); v /= 10; } while (v);
  puts(label); puts(p);
}
int main(void) {
  long long t0 = now_ns();
#ifdef _WIN32
  HANDLE h[NTHREADS];
  for (int i = 0; i < NTHREADS; i++) h[i] = CreateThread(0, 0, start, 0, 0, 0);
  for (int i = 0; i < NTHREADS; i++) { WaitForSingleObject(h[i], 0xFFFFFFFF); CloseHandle(h[i]); }
#else
  pthread_t t[NTHREADS];
  for (int i = 0; i < NTHREADS; i++) pthread_create(&t[i], 0, start, 0);
  for (int i = 0; i < NTHREADS; i++) pthread_join(t[i], 0);
#endif
  long long t1 = now_ns();
  long long c = atomic_load(&counter);
  put_num("ns per increment (4 threads, wall / total increments, x1000)", (t1 - t0) * 1000 / ((long long)NTHREADS * ITERS));
  put_num("counter", c);
  puts(plain_counter == c ? "plain counter equal (no race seen)" : "plain counter lost updates (race seen, as expected)");
  return c == (long long)NTHREADS * ITERS ? 0 : 1;
}
