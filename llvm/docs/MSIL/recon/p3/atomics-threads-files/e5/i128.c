#include <stdatomic.h>
#include <stdio.h>
_Atomic __int128 a;
struct s24 { long long x, y, z; };
_Atomic struct s24 b;
int main(void) {
  atomic_fetch_add(&a, 5);
  __int128 e = 5;
  atomic_compare_exchange_strong(&a, &e, ((__int128)1 << 100) | 7);
  struct s24 v = {1, 2, 3};
  atomic_store(&b, v);
  struct s24 w = atomic_load(&b);
  printf("%d %d lf16=%d lf24=%d always16=%d\n", (int)(atomic_load(&a) >> 100), (int)w.z,
         (int)atomic_is_lock_free(&a), (int)atomic_is_lock_free(&b), (int)__atomic_always_lock_free(16, 0));
  return 0;
}
