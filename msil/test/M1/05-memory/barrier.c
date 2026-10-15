// Compiler barriers and fences from C (Milestone 7, D36): stores and loads
// separated by asm volatile("" ::: "memory") and the other empty asm forms
// (clang emits a singlethread fence for each, which selects the comment-only
// MEMBARRIER) and by __atomic_signal_fence / __c11_atomic_signal_fence (also
// singlethread), and by __atomic_thread_fence with seq_cst, acq_rel, acquire
// and release, __c11_atomic_thread_fence and __sync_synchronize (system
// fences: a call of System.Threading.Interlocked::MemoryBarrier()); a relaxed
// fence emits nothing. Covered: values stored before a barrier are seen
// after it, stores after it overwrite them, barriers in loops, in branches
// and at the start and end of blocks, and helpers whose whole body is one
// barrier or fence (a method that is only the MEMBARRIER comment and ret, or
// only the call and ret). Values are derived from argc (1 when run without
// arguments) and pass through noinline helpers, so clang cannot fold them at
// -O2. MSIL_EXPECT returns 1 + (line % 255) at the first failed check;
// [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define TRY(call)                                                              \
  do {                                                                         \
    int r_ = (call);                                                           \
    if (r_ != 0)                                                               \
      return r_;                                                               \
  } while (0)

#define BARRIER() __asm__ __volatile__("" ::: "memory")

static NOINLINE void only_barrier(void) { BARRIER(); }
static NOINLINE void only_signal_fence(void) {
  __atomic_signal_fence(__ATOMIC_SEQ_CST);
}
static NOINLINE void only_thread_fence(void) {
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
static NOINLINE void only_sync(void) { __sync_synchronize(); }

// Stores v, fences, reads back: each fence kind once.
static NOINLINE int store_barrier_load(int *p, int v) {
  *p = v;
  BARRIER();
  return *p + 1;
}
static NOINLINE int store_fence_load(int *p, int v) {
  *p = v;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  return *p + 2;
}
static NOINLINE long long release_acquire(long long *p, long long v) {
  *p = v;
  __atomic_thread_fence(__ATOMIC_RELEASE);
  long long a = *p;
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  *p = a * 3;
  __atomic_thread_fence(__ATOMIC_ACQ_REL);
  return *p + a;
}
static NOINLINE double c11_fences(double *p, double v) {
  *p = v;
  __c11_atomic_thread_fence(__ATOMIC_SEQ_CST);
  *p *= 2.0;
  __c11_atomic_signal_fence(__ATOMIC_SEQ_CST);
  double r = *p;
  __atomic_thread_fence(__ATOMIC_RELAXED);
  return r + 0.5;
}

static NOINLINE int check_straight_line(int one) {
  int x = 0;
  MSIL_EXPECT(store_barrier_load(&x, 41 * one) == 42 && x == 41);
  MSIL_EXPECT(store_fence_load(&x, 40 * one) == 42 && x == 40);
  long long l = 0;
  MSIL_EXPECT(release_acquire(&l, 1000000000000ll * one) == 4000000000000ll);
  MSIL_EXPECT(l == 3000000000000ll);
  double d = 0;
  MSIL_EXPECT(c11_fences(&d, 1.25 * one) == 3.0 && d == 2.5);

  only_barrier();
  only_signal_fence();
  only_thread_fence();
  only_sync();

  // A sequence of stores to one location with every kind of barrier between
  // them; each load sees the last store.
  int v[4];
  v[0] = one;
  BARRIER();
  v[1] = v[0] + 1;
  __asm__ __volatile__("");
  v[2] = v[1] * 10;
  __asm__("");
  v[3] = v[2] - v[0];
  __asm__("" ::: "memory");
  v[0] = 7;
  __atomic_signal_fence(__ATOMIC_ACQ_REL);
  v[1] += v[0];
  __sync_synchronize();
  v[2] = -v[2];
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  MSIL_EXPECT(v[0] == 7 && v[1] == 9 && v[2] == -20 && v[3] == 19);
  return 0;
}

static NOINLINE int check_control_flow(int one) {
  int buf[32];
  // Barriers inside a loop body, at its start and at its end.
  for (int i = 0; i < 32; i++) {
    BARRIER();
    buf[i] = i * 3 * one;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    buf[i] += 1;
    BARRIER();
  }
  int sum = 0;
  for (int i = 0; i < 32; i++) {
    sum += buf[i];
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
  }
  MSIL_EXPECT(sum == 3 * 496 + 32);

  // Barriers in both arms of a branch and after the join.
  int odd = 0, even = 0;
  for (int i = 0; i < 10 * one; i++) {
    if (i & 1) {
      odd += i;
      BARRIER();
    } else {
      __atomic_thread_fence(__ATOMIC_SEQ_CST);
      even += i;
    }
    BARRIER();
  }
  MSIL_EXPECT(odd == 25 && even == 20);

  // A loop whose condition reads memory written behind a barrier.
  int flag = 5 * one;
  int steps = 0;
  while (flag > 0) {
    flag--;
    BARRIER();
    steps++;
  }
  MSIL_EXPECT(steps == 5 && flag == 0);
  return 0;
}

int main(int argc, char **argv) {
  (void)argv;
  TRY(check_straight_line(argc));
  TRY(check_control_flow(argc));
  return 0;
}
