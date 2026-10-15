// Frameless memmove and memcpy helpers (Milestone 7, D15, D21): move_bytes,
// move_ret and copy_bytes are noinline functions whose whole body is one
// memmove or variable-length memcpy of their arguments, so at -O2 their
// methods have no localloc frame and their first unverifiable instruction is
// the call of System.Buffer::MemoryCopy(void*, void*, int64, int64) with
// native int addresses. PEVerify reports exactly that call, and
// msil-verify.py lets only that message through ([found Native Int]
// [expected unmanaged pointer] Unexpected type on the stack.), so this is
// the test that exercises the D21 exception: in a method with a frame the
// localloc comes first and hides it. The helpers have external linkage, so
// -O2 cannot specialize them for the arguments main passes. Forward,
// backward and disjoint moves with lengths derived from argc (1 when run
// without arguments) are checked byte by byte. MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; [exit 0] means every check
// passed.
// RUN: %{msil_c}
#include "msil_test.h"

__attribute__((noinline)) void move_bytes(void *dst, const void *src,
                                          unsigned long long n) {
  __builtin_memmove(dst, src, n);
}

__attribute__((noinline)) void *move_ret(void *dst, const void *src,
                                         unsigned n) {
  return __builtin_memmove(dst, src, n);
}

__attribute__((noinline)) void copy_bytes(void *dst, const void *src,
                                          unsigned long long n) {
  __builtin_memcpy(dst, src, n);
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;
  unsigned char buf[256];

  // Forwards by one: buf[1..200] = old buf[0..199].
  for (int i = 0; i < 256; i++)
    buf[i] = (unsigned char)(i * one);
  move_bytes(buf + 1, buf, 200ull * one);
  MSIL_EXPECT(buf[0] == 0 && buf[1] == 0);
  for (int i = 2; i <= 200; i++)
    MSIL_EXPECT(buf[i] == i - 1);
  for (int i = 201; i < 256; i++)
    MSIL_EXPECT(buf[i] == i);

  // Backwards by three: buf[0..149] = old buf[3..152].
  for (int i = 0; i < 256; i++)
    buf[i] = (unsigned char)(255 - i * one);
  unsigned char *r = move_ret(buf, buf + 3, 150u * one);
  MSIL_EXPECT(r == buf);
  for (int i = 0; i < 150; i++)
    MSIL_EXPECT(buf[i] == 252 - i);
  for (int i = 150; i < 256; i++)
    MSIL_EXPECT(buf[i] == 255 - i);

  // Disjoint: buf[128..255] = buf[0..127], through memcpy.
  for (int i = 0; i < 256; i++)
    buf[i] = (unsigned char)(i * 7 * one);
  copy_bytes(buf + 128, buf, 128ull * one);
  for (int i = 0; i < 128; i++) {
    MSIL_EXPECT(buf[i] == (unsigned char)(i * 7));
    MSIL_EXPECT(buf[i + 128] == (unsigned char)(i * 7));
  }

  // Zero lengths change nothing.
  move_bytes(buf + 5, buf, 0ull * one);
  copy_bytes(buf + 9, buf + 200, 0ull * one);
  MSIL_EXPECT(move_ret(buf, buf + 1, 0u * one) == buf);
  for (int i = 0; i < 128; i++)
    MSIL_EXPECT(buf[i] == (unsigned char)(i * 7));

  // Moves between ints, at an element offset.
  int v[16];
  for (int i = 0; i < 16; i++)
    v[i] = -1000 * i * one;
  move_bytes(v + 2, v, 10 * sizeof(int) * one);
  MSIL_EXPECT(v[0] == 0 && v[1] == -1000 && v[2] == 0 && v[11] == -9000);
  MSIL_EXPECT(v[12] == -12000 && v[15] == -15000);
  return 0;
}
