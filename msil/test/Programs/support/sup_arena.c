/* The arena allocator of the support library: 16 MB of zero-initialized
   data (in an MSIL program part of the data segment), handed out in 16-byte
   aligned blocks; sup_reset starts over. */
#include "sup.h"
static _Alignas(16) unsigned char heap[16u << 20];
static size_t top;
void *sup_alloc(size_t n) {
  size_t start = (top + 15) & ~(size_t)15;
  if (start + n > sizeof heap) return (void *)0;
  top = start + n;
  return heap + start;
}
void *sup_calloc(size_t n, size_t size) {
  unsigned char *p = sup_alloc(n * size);
  if (p) for (size_t i = 0; i < n * size; ++i) p[i] = 0;
  return p;
}
void sup_free(void *p) { (void)p; }
void sup_reset(void) { top = 0; }
size_t sup_used(void) { return top; }
