/* Support library of the exit-corpus programs (msil/test/Programs): an arena
   allocator, the C library functions a freestanding MSIL build lacks
   (malloc, calloc, realloc, free, abort, memcmp, memset, memcpy, memmove and
   strlen: defined only when __MSIL__, so the native oracle build keeps the C
   runtime's), and output through puts. Each function group is its own
   translation unit, so the archive 'sup' of these files exercises lazy
   extraction. Both builds of a program use these files and the shim
   headers of ../shim. */
#ifndef SUP_H
#define SUP_H
#include <stddef.h>
int puts(const char *s);
void *sup_alloc(size_t n);
void *sup_calloc(size_t n, size_t size);
void sup_free(void *p);
void sup_reset(void);
size_t sup_used(void);
void sup_put_hex(const char *label, const void *p, size_t n);
void sup_put_u(const char *label, unsigned long long v);
void sup_put_i(const char *label, long long v);
unsigned sup_fnv1a(const void *p, size_t n);
#endif
