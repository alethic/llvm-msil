#include "sup.h"
#ifdef __MSIL__
size_t strlen(const char *s) { size_t n = 0; while (s[n]) ++n; return n; }
#endif
