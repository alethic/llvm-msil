// e04: unmanaged pointers, ldind/stind (all widths), sign/zero extension,
// unaligned./volatile. prefixes, localloc (with and without localsinit), initblk/cpblk.
#include "common.il"
.assembly e04_mem {}
.module e04_mem.dll

// Frame via localloc, no .locals at all => InitLocals flag NOT set (memory not zeroed).
.method static int32 'frame_noinit'() cil managed
{
  .maxstack 3
  ldc.i4 256
  localloc
  dup
  ldc.i4 77
  stind.i4
  ldind.i4
  ret
}

// localloc in a method with '.locals init' => buffer is zero-initialised.
.method static int64 'frame_init_sum'() cil managed
{
  .maxstack 4
  .locals init (native int p, int32 i, int64 s)
  ldc.i4 4096
  localloc
  stloc.0
  ldc.i4.0
  stloc.1
LOOP:
  ldloc.2
  ldloc.0
  ldloc.1
  add
  ldind.u1
  conv.u8
  add
  stloc.2
  ldloc.1
  ldc.i4.1
  add
  dup
  stloc.1
  ldc.i4 4096
  blt.s LOOP
  ldloc.2
  ret
}

.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 8
  .locals init (native int p, native int q)
  ldc.i4 128
  localloc
  stloc.0

  // ---- 8-bit
  ldloc.0  ldc.i4 0x180  stind.i1           // truncates to 0x80
  ldstr "ldind.i1 sign-extends"   ldloc.0  ldind.i1  conv.i8  ldc.i8 -128  call void 'check.i8'(string, int64, int64)
  ldstr "ldind.u1 zero-extends"   ldloc.0  ldind.u1  conv.i8  ldc.i8 128   call void 'check.i8'(string, int64, int64)
  // ---- 16-bit
  ldloc.0  ldc.i4 0x18000  stind.i2
  ldstr "ldind.i2 sign-extends"   ldloc.0  ldind.i2  conv.i8  ldc.i8 -32768  call void 'check.i8'(string, int64, int64)
  ldstr "ldind.u2 zero-extends"   ldloc.0  ldind.u2  conv.i8  ldc.i8 32768   call void 'check.i8'(string, int64, int64)
  // ---- 32-bit
  ldloc.0  ldc.i4 0x80000000  stind.i4
  ldstr "ldind.i4 + conv.i8"      ldloc.0  ldind.i4  conv.i8  ldc.i8 0xFFFFFFFF80000000  call void 'check.i8'(string, int64, int64)
  ldstr "ldind.u4 + conv.i8 (sign-extends, stack type is int32)"  ldloc.0  ldind.u4  conv.i8  ldc.i8 0xFFFFFFFF80000000  call void 'check.i8'(string, int64, int64)
  ldstr "ldind.u4 + conv.u8 (zero-extends)"   ldloc.0  ldind.u4  conv.u8  ldc.i8 0x80000000  call void 'check.i8'(string, int64, int64)
  // ---- 64-bit
  ldloc.0  ldc.i8 0x8877665544332211  stind.i8
  ldstr "ldind.i8"   ldloc.0  ldind.i8  ldc.i8 0x8877665544332211  call void 'check.i8'(string, int64, int64)
  ldstr "ldind.u8 (ilasm alias of ldind.i8)"   ldloc.0  ldind.u8  ldc.i8 0x8877665544332211  call void 'check.i8'(string, int64, int64)
  ldstr "little-endian byte 0"  ldloc.0  ldind.u1  conv.u8  ldc.i8 0x11  call void 'check.i8'(string, int64, int64)
  // ---- native int
  ldloc.0  ldc.i4 -2  conv.i  stind.i
  ldstr "ldind.i"    ldloc.0  ldind.i  conv.i8  ldc.i8 -2  call void 'check.i8'(string, int64, int64)
  // ---- float32 / float64
  ldloc.0  ldc.r4 1.5  stind.r4
  ldstr "ldind.r4"   ldloc.0  ldind.r4  conv.r8  ldc.r8 1.5  call void 'check.r8'(string, float64, float64)
  ldstr "r4 bits"    ldloc.0  ldind.u4  conv.u8  ldc.i8 0x3FC00000  call void 'check.i8'(string, int64, int64)
  ldloc.0  ldc.r8 -2.25  stind.r8
  ldstr "ldind.r8"   ldloc.0  ldind.r8  ldc.r8 -2.25  call void 'check.r8'(string, float64, float64)
  // float stored with stind.r4 from a float64 on the stack (implicit narrowing by stind.r4)
  ldloc.0  ldc.r8 0.1  stind.r4
  ldstr "stind.r4 of F narrows"  ldloc.0  ldind.u4  conv.u8  ldc.i8 0x3DCCCCCD  call void 'check.i8'(string, int64, int64)

  // ---- pointer arithmetic
  ldloc.0  ldc.i4 16  add  stloc.1                 // native int + int32 -> native int
  ldstr "ptr diff (native int - native int)"  ldloc.1  ldloc.0  sub  conv.i8  ldc.i8 16  call void 'check.i8'(string, int64, int64)
  ldloc.1  ldc.i4 -8  conv.i  add  ldc.i4 1234  stind.i4      // q[-8] = 1234
  ldstr "negative offset store/load"  ldloc.0  ldc.i4 8  add  ldind.i4  conv.i8  ldc.i8 1234  call void 'check.i8'(string, int64, int64)

  // ---- unaligned. / volatile.
  ldloc.0  ldc.i4 1  add  ldc.i4 0x11223344  unaligned. 1  stind.i4
  ldstr "unaligned.1 ldind.i4"  ldloc.0  ldc.i4 1  add  unaligned. 1  ldind.i4  conv.i8  ldc.i8 0x11223344  call void 'check.i8'(string, int64, int64)
  ldloc.0  ldc.i4 3  add  ldc.i8 0x0102030405060708  unaligned. 1  stind.i8
  ldstr "unaligned.1 ldind.i8"  ldloc.0  ldc.i4 3  add  unaligned. 1  ldind.i8  ldc.i8 0x0102030405060708  call void 'check.i8'(string, int64, int64)
  ldloc.0  ldc.i4 5  add  ldc.r8 3.5  unaligned. 1  stind.r8
  ldstr "unaligned.1 ldind.r8"  ldloc.0  ldc.i4 5  add  unaligned. 1  ldind.r8  ldc.r8 3.5  call void 'check.r8'(string, float64, float64)
  ldloc.0  ldc.i4 2  add  ldc.i4 0x7777  unaligned. 2  stind.i4
  ldstr "unaligned.2 ldind.i4"  ldloc.0  ldc.i4 2  add  unaligned. 2  ldind.i4  conv.i8  ldc.i8 0x7777  call void 'check.i8'(string, int64, int64)
  ldloc.0  ldc.i4 99  volatile.  stind.i4
  ldstr "volatile. ldind.i4"  ldloc.0  volatile.  ldind.i4  conv.i8  ldc.i8 99  call void 'check.i8'(string, int64, int64)
  ldloc.0  ldc.i4 1  add  ldc.i4 -7  unaligned. 1  volatile.  stind.i4
  ldstr "unaligned.1 volatile. ldind.i4"  ldloc.0  ldc.i4 1  add  unaligned. 1  volatile.  ldind.i4  conv.i8  ldc.i8 -7  call void 'check.i8'(string, int64, int64)
  ldloc.0  ldc.i4 1  add  ldc.i4 -9  volatile.  unaligned. 1  stind.i4
  ldstr "volatile. unaligned.1 (other order)"  ldloc.0  ldc.i4 1  add  volatile.  unaligned. 1  ldind.i4  conv.i8  ldc.i8 -9  call void 'check.i8'(string, int64, int64)

  // ---- initblk / cpblk
  ldloc.0  ldc.i4 0xAB  ldc.i4 32  initblk                  // p[0..31] = 0xAB
  ldstr "initblk"  ldloc.0  ldc.i4 24  add  ldind.i8  ldc.i8 0xABABABABABABABAB  call void 'check.i8'(string, int64, int64)
  ldloc.0  ldc.i4 0x1CD  ldc.i4 4  initblk                  // value is truncated to unsigned int8
  ldstr "initblk value truncated to u8"  ldloc.0  ldind.u4  conv.u8  ldc.i8 0xCDCDCDCD  call void 'check.i8'(string, int64, int64)
  ldloc.0  ldc.i8 0x0807060504030201  stind.i8
  ldloc.0  ldc.i4 64  add  ldloc.0  ldc.i4 8  cpblk         // cpblk(dest, src, size)
  ldstr "cpblk"  ldloc.0  ldc.i4 64  add  ldind.i8  ldc.i8 0x0807060504030201  call void 'check.i8'(string, int64, int64)
  ldloc.0  ldc.i4 65  add  ldloc.0  ldc.i4 8  unaligned. 1  cpblk
  ldstr "unaligned. cpblk"  ldloc.0  ldc.i4 65  add  unaligned. 1  ldind.i8  ldc.i8 0x0807060504030201  call void 'check.i8'(string, int64, int64)
  ldloc.0  ldc.i4 72  add  ldc.i4 0  ldc.i4 16  volatile.  initblk
  ldstr "volatile. initblk"  ldloc.0  ldc.i4 72  add  ldind.i8  ldc.i8 0  call void 'check.i8'(string, int64, int64)
  // size as native int
  ldloc.0  ldc.i4 0x11  ldc.i4 8  conv.u  initblk
  ldstr "initblk size native int"  ldloc.0  ldind.i8  ldc.i8 0x1111111111111111  call void 'check.i8'(string, int64, int64)
  // overlapping cpblk (memmove semantics are NOT guaranteed by ECMA; observe only)
  ldloc.0  ldc.i8 0x0807060504030201  stind.i8
  ldloc.0  ldc.i4 1  add  ldloc.0  ldc.i4 7  cpblk
  ldstr "OBSERVE overlapping cpblk dest=src+1 result=0x"  call void [mscorlib]System.Console::Write(string)
  ldloc.0  ldind.i8  conv.i  stloc.1  ldloca.s q  ldstr "X16"  call instance string [mscorlib]System.IntPtr::ToString(string)  call void [mscorlib]System.Console::WriteLine(string)

  // ---- localloc variants
  ldstr "localloc no-init frame"  call int32 'frame_noinit'()  conv.i8  ldc.i8 77  call void 'check.i8'(string, int64, int64)
  ldstr "localloc with .locals init is zeroed (sum of 4096 bytes)"  call int64 'frame_init_sum'()  ldc.i8 0  call void 'check.i8'(string, int64, int64)
  call int32 'report'()
  ret
}
