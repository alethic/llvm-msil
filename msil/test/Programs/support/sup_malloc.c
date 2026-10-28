/* malloc, calloc, realloc, free and abort over the arena, for the MSIL build
   only (the native build uses the C runtime's): a size header before each
   block lets realloc copy. */
#include "sup.h"
#ifdef __MSIL__
void *malloc(size_t n) {
  size_t *p = sup_alloc(n + 16);
  if (!p) return p;
  *p = n;
  return (unsigned char *)p + 16;
}
void *calloc(size_t n, size_t s) {
  unsigned char *p = malloc(n * s);
  if (p) for (size_t i = 0; i < n * s; ++i) p[i] = 0;
  return p;
}
void free(void *p) { (void)p; }
void *realloc(void *old, size_t n) {
  unsigned char *p = malloc(n);
  if (p && old) {
    size_t m = *(size_t *)((unsigned char *)old - 16);
    for (size_t i = 0; i < (m < n ? m : n); ++i) p[i] = ((unsigned char *)old)[i];
  }
  return p;
}
void abort(void) { __builtin_trap(); }
#endif
