/* memset for the MSIL build only (the native build uses the C runtime's). */
#include "sup.h"
#ifdef __MSIL__
void *memset(void *d, int c, size_t n) {
  unsigned char *p = d;
  for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)c;
  return d;
}
#endif
