#include <immintrin.h>
int spin(volatile int *f) { while (!*f) _mm_pause(); return *f; }
int main(void) { volatile int f = 1; return spin(&f) - 1; }
