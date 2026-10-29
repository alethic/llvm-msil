#include <stdatomic.h>
int puts(const char *);
_Atomic int counter;
_Atomic long long big;
int main(void) {
  atomic_fetch_add(&counter, 5);
  atomic_fetch_add_explicit(&big, 7, memory_order_relaxed);
  int e = 5;
  atomic_compare_exchange_strong(&counter, &e, 9);
  puts(atomic_load(&counter) == 9 && atomic_load(&big) == 7 ? "ok" : "bad");
  return atomic_load(&counter);
}
