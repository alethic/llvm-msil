#include <stddef.h>
#include <stdalign.h>
int a_maxalign = alignof(max_align_t);
int s_maxalign = sizeof(max_align_t);
int a_ld = alignof(long double);
struct S { char c; long double x; };
int s_S = sizeof(struct S);
