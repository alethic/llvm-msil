/* E1: every C atomic construct the front end can produce, one function each. */
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>

_Atomic int ai;
_Atomic char ac;
_Atomic short as;
_Atomic long long all;
_Atomic(void *) ap;
_Atomic float af;
_Atomic double ad;
struct s3 { char a, b, c; };
struct s16 { long long a, b; };
struct s24 { long long a, b, c; };
_Atomic struct s3 a3;
_Atomic struct s16 a16;
_Atomic struct s24 a24;
_Atomic __int128 a128;
atomic_flag fl = ATOMIC_FLAG_INIT;
int plain;
volatile int vol;

int  f_load_seq(void)      { return atomic_load(&ai); }
int  f_load_acq(void)      { return atomic_load_explicit(&ai, memory_order_acquire); }
int  f_load_rlx(void)      { return atomic_load_explicit(&ai, memory_order_relaxed); }
void f_store_seq(int v)    { atomic_store(&ai, v); }
void f_store_rel(int v)    { atomic_store_explicit(&ai, v, memory_order_release); }
void f_store_rlx(int v)    { atomic_store_explicit(&ai, v, memory_order_relaxed); }
int  f_xchg(int v)         { return atomic_exchange(&ai, v); }
int  f_add(int v)          { return atomic_fetch_add(&ai, v); }
int  f_sub(int v)          { return atomic_fetch_sub(&ai, v); }
int  f_and(int v)          { return atomic_fetch_and(&ai, v); }
int  f_or(int v)           { return atomic_fetch_or(&ai, v); }
int  f_xor(int v)          { return atomic_fetch_xor(&ai, v); }
int  f_max(int v)          { return __atomic_fetch_max((int *)&ai, v, __ATOMIC_SEQ_CST); }
int  f_umin(unsigned v)    { return __atomic_fetch_min((unsigned *)&ai, v, __ATOMIC_SEQ_CST); }
int  f_nand(int v)         { return __atomic_fetch_nand((int *)&ai, v, __ATOMIC_SEQ_CST); }
int  f_addpost(int v)      { return ai += v; }
bool f_cas(int e, int n)   { return atomic_compare_exchange_strong(&ai, &e, n); }
bool f_casw(int *e, int n) { return atomic_compare_exchange_weak_explicit(&ai, e, n, memory_order_acq_rel, memory_order_acquire); }
char f_c_add(char v)       { return atomic_fetch_add(&ac, v); }
bool f_c_cas(char e, char n){ return atomic_compare_exchange_strong(&ac, &e, n); }
short f_s_or(short v)      { return atomic_fetch_or(&as, v); }
short f_s_xchg(short v)    { return atomic_exchange(&as, v); }
long long f_ll_add(long long v) { return atomic_fetch_add(&all, v); }
long long f_ll_load(void)  { return atomic_load(&all); }
void *f_p_xchg(void *p)    { return atomic_exchange(&ap, p); }
float f_f_add(float v)     { return af += v; }
double f_d_load(void)      { return atomic_load(&ad); }
double f_d_fadd(double v)  { return __atomic_fetch_add((double *)&ad, v, __ATOMIC_SEQ_CST); }
struct s3 f_s3_load(void)  { return atomic_load(&a3); }
struct s16 f_s16_load(void){ return atomic_load(&a16); }
void f_s16_store(struct s16 v){ atomic_store(&a16, v); }
struct s24 f_s24_load(void){ return atomic_load(&a24); }
__int128 f_i128_add(__int128 v){ return atomic_fetch_add(&a128, v); }
bool f_i128_cas(__int128 e, __int128 n){ return atomic_compare_exchange_strong(&a128, &e, n); }
bool f_flag_tas(void)      { return atomic_flag_test_and_set(&fl); }
void f_flag_clear(void)    { atomic_flag_clear(&fl); }
void f_fence_seq(void)     { atomic_thread_fence(memory_order_seq_cst); }
void f_fence_acq(void)     { atomic_thread_fence(memory_order_acquire); }
void f_sigfence(void)      { atomic_signal_fence(memory_order_seq_cst); }
int  f_sync_add(int v)     { return __sync_fetch_and_add(&plain, v); }
int  f_sync_addf(int v)    { return __sync_add_and_fetch(&plain, v); }
int  f_sync_vcas(int e, int n) { return __sync_val_compare_and_swap(&plain, e, n); }
bool f_sync_bcas(int e, int n) { return __sync_bool_compare_and_swap(&plain, e, n); }
int  f_sync_tas(void)      { return __sync_lock_test_and_set(&plain, 1); }
void f_sync_rel(void)      { __sync_lock_release(&plain); }
void f_sync_sync(void)     { __sync_synchronize(); }
int  f_sync_nand(int v)    { return __sync_fetch_and_nand(&plain, v); }
int  f_vol_load(void)      { return vol; }
void f_vol_store(int v)    { vol = v; }
bool f_lockfree_i(void)    { return atomic_is_lock_free(&ai); }
bool f_lockfree_16(void)   { return atomic_is_lock_free(&a16); }
bool f_lockfree_128(void)  { return atomic_is_lock_free(&a128); }
bool f_lockfree_24(void)   { return atomic_is_lock_free(&a24); }
bool f_lockfree_rt(int n, void *p) { return __atomic_is_lock_free(n, p); }
int  f_always16(void)      { return __atomic_always_lock_free(16, 0); }
int  f_always8(void)       { return __atomic_always_lock_free(8, 0); }
int  f_macros(void)        { return ATOMIC_INT_LOCK_FREE * 100 + ATOMIC_LLONG_LOCK_FREE * 10 + ATOMIC_POINTER_LOCK_FREE; }
int  f_c11_add(_Atomic int *p, int v) { return __c11_atomic_fetch_add(p, v, __ATOMIC_RELAXED); }
