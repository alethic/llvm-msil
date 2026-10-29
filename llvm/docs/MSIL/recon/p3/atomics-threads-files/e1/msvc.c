#include <intrin.h>
long f_inc(volatile long *p) { return _InterlockedIncrement(p); }
long f_cas(volatile long *p, long e, long n) { return _InterlockedCompareExchange(p, n, e); }
__int64 f_add64(volatile __int64 *p, __int64 v) { return _InterlockedExchangeAdd64(p, v); }
char f_xchg8(volatile char *p, char v) { return _InterlockedExchange8(p, v); }
short f_or16(volatile short *p, short v) { return _InterlockedOr16(p, v); }
unsigned char f_cas128(volatile __int64 *p, __int64 hi, __int64 lo, __int64 *cmp) { return _InterlockedCompareExchange128(p, hi, lo, cmp); }
void f_rwb(void) { _ReadWriteBarrier(); }
void f_fsf(void) { __faststorefence(); }
void f_mfence(void) { _mm_mfence(); }
void f_pause(void) { _mm_pause(); }
void *f_xchgp(void * volatile *p, void *v) { return _InterlockedExchangePointer(p, v); }
unsigned char f_bts(volatile long *p) { return _interlockedbittestandset(p, 3); }
long f_vload(volatile long *p) { return *p; }
