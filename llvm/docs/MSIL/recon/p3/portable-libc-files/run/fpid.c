#include <stddef.h>
size_t strlen(const char *);
typedef size_t (*fp_t)(const char *);
fp_t probe_lib_strlen_addr(void);
static fp_t a(void) { return &strlen; }
__attribute__((noinline)) fp_t b(void) { return &strlen; }
int main(void) {
  int r = 0;
  if (a() != b()) r |= 1;
  if (a() != probe_lib_strlen_addr()) r |= 2;
  if (probe_lib_strlen_addr()("abcd") != 4) r |= 4;
  return r ? r : 42;
}
