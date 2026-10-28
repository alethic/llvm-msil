/* Shim of <string.h> for the exit-corpus programs (both builds; -isystem
   ../shim): ../support defines these in the MSIL build, the C runtime in
   the native build. */
#ifndef SHIM_STRING_H
#define SHIM_STRING_H
#include <stddef.h>
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
#endif
