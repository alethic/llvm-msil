/* Support library for the tests-p2 corpus drivers: an arena allocator, the
   libc functions a freestanding MSIL build lacks (memcmp, memset, memcpy,
   memmove, strlen: defined only when __MSIL__, so native oracle builds keep
   the C runtime's), and puts-based output. Each function is its own
   translation unit so that an archive of them exercises lazy extraction. */
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
