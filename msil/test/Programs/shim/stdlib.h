/* Shim of <stdlib.h> for the exit-corpus programs (both builds; -isystem
   ../shim): the allocator, which ../support/sup_malloc.c defines in the MSIL
   build and the C runtime in the native build, and abort. */
#ifndef SHIM_STDLIB_H
#define SHIM_STDLIB_H
#include <stddef.h>
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
void abort(void);
#endif
