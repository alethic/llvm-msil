/* memcpy and memmove for the MSIL build only (the native build uses the C
   runtime's). */
#include "sup.h"
#ifdef __MSIL__
void *memcpy(void *d, const void *s, size_t n) {
  unsigned char *p = d; const unsigned char *q = s;
  for (size_t i = 0; i < n; ++i) p[i] = q[i];
  return d;
}
void *memmove(void *d, const void *s, size_t n) {
  unsigned char *p = d; const unsigned char *q = s;
  if (p < q) for (size_t i = 0; i < n; ++i) p[i] = q[i];
  else for (size_t i = n; i-- > 0;) p[i] = q[i];
  return d;
}
#endif
