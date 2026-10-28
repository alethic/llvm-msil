/* memcmp for the MSIL build only (the native build uses the C runtime's). */
#include "sup.h"
#ifdef __MSIL__
int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a, *y = b;
  for (size_t i = 0; i < n; ++i)
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}
#endif
