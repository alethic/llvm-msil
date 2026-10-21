/* Exercises every BCL member Phase 1 emits: the data segment (AllocHGlobal
   and the RVA image type extending System.ValueType), argv
   (GetCommandLineArgs, AllocHGlobal, StringToHGlobalAnsi), a variable-length
   memmove (Buffer::MemoryCopy) and a fence (Interlocked::MemoryBarrier). */
#include <stddef.h>
int puts(const char *);
static char buf[64] = "hello";
const char *table[] = {buf, "world"};
int main(int argc, char **argv) {
  size_t n = (size_t)argc + 3;
  __builtin_memmove(buf + 1, buf, n);
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  puts(table[1]);
  return argv[0][0] != 0 ? 42 : 1;
}
