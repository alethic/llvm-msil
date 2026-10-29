/* E4: byte and halfword atomics as a CAS loop on the containing aligned 32-bit word (what LLVM's
   AtomicExpand builds when MinCmpXchgSizeInBits is 32): adjacent bytes updated by other threads,
   atomically or with plain stores, must never be lost. */
#include <stdatomic.h>
#include <stdint.h>
#ifdef PROTO
#include "../e8-threads/proto-atomic.h"
#define IMPORT(m) __attribute__((import_module(m)))
#else
#define IMPORT(m)
#endif
typedef __SIZE_TYPE__ size_t;
IMPORT(LIBC) int puts(const char *);
#define N 1000003
static uint8_t add_u8(uint8_t *p, uint8_t v) {
  _Atomic uint32_t *w = (_Atomic uint32_t *)((uintptr_t)p & ~(uintptr_t)3);
  unsigned sh = (unsigned)((uintptr_t)p & 3) * 8, mask = 0xFFu << sh;
  uint32_t old = atomic_load_explicit(w, memory_order_relaxed), nw;
  do nw = (old & ~mask) | ((((old >> sh) + v) & 0xFFu) << sh);
  while (!atomic_compare_exchange_weak(w, &old, nw));
  return (uint8_t)(old >> sh);
}
static uint16_t add_u16(uint16_t *p, uint16_t v) {
  _Atomic uint32_t *w = (_Atomic uint32_t *)((uintptr_t)p & ~(uintptr_t)3);
  unsigned sh = (unsigned)((uintptr_t)p & 2) * 8, mask = 0xFFFFu << sh;
  uint32_t old = atomic_load_explicit(w, memory_order_relaxed), nw;
  do nw = (old & ~mask) | ((((old >> sh) + v) & 0xFFFFu) << sh);
  while (!atomic_compare_exchange_weak(w, &old, nw));
  return (uint16_t)(old >> sh);
}
_Alignas(4) uint8_t bytes[8];     /* bytes 0,1: atomic adds; bytes 2,3: plain stores; 4..7: halfwords */
_Atomic int started;
_Atomic int plain_errors;
static void sync_start(void) { atomic_fetch_add(&started, 1); while (atomic_load(&started) < 4) { } }
static void worker(int k) {
  sync_start();
  if (k < 2) {
    for (int i = 0; i < N; i++) add_u8(&bytes[k], 1);
    for (int i = 0; i < N; i++) add_u16((uint16_t *)&bytes[4 + 2 * k], 3);
  } else {
    volatile uint8_t *b = &bytes[k];
    int errs = 0;
    for (int i = 1; i <= 2 * N; i++) {
      *b = (uint8_t)i;
      for (int j = 0; j < 3; j++) (void)bytes[0];
      if (*b != (uint8_t)i) errs++;         /* a stale word written back by a CAS would show here */
    }
    atomic_fetch_add(&plain_errors, errs);
  }
}
#ifdef _WIN32
typedef unsigned long DWORD; typedef void *HANDLE;
IMPORT("kernel32.dll") HANDLE CreateThread(void *, size_t, DWORD (*)(void *), void *, DWORD, DWORD *);
IMPORT("kernel32.dll") DWORD WaitForSingleObject(HANDLE, DWORD);
static DWORD start(void *arg) { worker((int)(intptr_t)arg); return 0; }
#else
typedef unsigned long pthread_t;
IMPORT("libc.so.6") int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
IMPORT("libc.so.6") int pthread_join(pthread_t, void **);
static void *start(void *arg) { worker((int)(intptr_t)arg); return 0; }
#endif
int main(void) {
  int bad = 0;
#ifdef _WIN32
  HANDLE h[4];
  for (int i = 0; i < 4; i++) h[i] = CreateThread(0, 0, start, (void *)(intptr_t)i, 0, 0);
  for (int i = 0; i < 4; i++) WaitForSingleObject(h[i], 0xFFFFFFFF);
#else
  pthread_t t[4];
  for (int i = 0; i < 4; i++) pthread_create(&t[i], 0, start, (void *)(intptr_t)i);
  for (int i = 0; i < 4; i++) pthread_join(t[i], 0);
#endif
  if (bytes[0] != (uint8_t)N || bytes[1] != (uint8_t)N) { puts("lost byte update"); bad |= 1; }
  if (bytes[2] != (uint8_t)(2 * N) || bytes[3] != (uint8_t)(2 * N)) { puts("lost plain store (final)"); bad |= 2; }
  if (*(uint16_t *)&bytes[4] != (uint16_t)(3 * N) || *(uint16_t *)&bytes[6] != (uint16_t)(3 * N)) { puts("lost halfword update"); bad |= 4; }
  if (atomic_load(&plain_errors)) { puts("plain store overwritten during the run"); bad |= 8; }
  puts(bad ? "FAIL" : "subword: all updates kept");
  return bad;
}
