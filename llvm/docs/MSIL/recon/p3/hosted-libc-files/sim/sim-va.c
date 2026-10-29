/* hosted-libc recon: expanded-to-fixed adapters for native variadic functions whose
   address SQLite stores in its aSyscall table (see tools/fix.py 'vaaddr'). */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stddef.h>
#include <fcntl.h>
#include <sys/mman.h>
int msil_sim_va_fcntl64(int fd, int cmd, ...) {
  va_list ap; va_start(ap, cmd); void *arg = va_arg(ap, void *); va_end(ap);
  return fcntl(fd, cmd, arg); /* a fixed-arity call (the prototype's fixed-arity list) */
}
void *msil_sim_va_mremap(void *old, size_t olds, size_t news, int flags, ...) {
  void *na = 0;
  if (flags & MREMAP_FIXED) { va_list ap; va_start(ap, flags); na = va_arg(ap, void *); va_end(ap);
    return mremap(old, olds, news, flags, na); }
  return mremap(old, olds, news, flags);
}
