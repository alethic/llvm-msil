// e19: overlapping copies. cpblk with a NON-constant size (helper path) vs Buffer.MemoryCopy (memmove).
#include "common.il"
.assembly e19_memmove {}
.method assembly static void 'cp'(native int d, native int s, int32 n) cil managed noinlining
{ .maxstack 3  ldarg.0 ldarg.1 ldarg.2 cpblk ret }
.method assembly static void 'fill'(native int p, int32 n) cil managed noinlining
{
  .maxstack 3
  .locals init (int32 i)
L: ldloc.0 ldarg.1 bge.s E
   ldarg.0 ldloc.0 add ldloc.0 conv.u1 stind.i1
   ldloc.0 ldc.i4.1 add stloc.0 br.s L
E: ret
}
// checksum: sum of p[i]*(i+1) for i < n
.method assembly static int64 'sum'(native int p, int32 n) cil managed noinlining
{
  .maxstack 4
  .locals init (int32 i, int64 s)
L: ldloc.0 ldarg.1 bge.s E
   ldloc.1 ldarg.0 ldloc.0 add ldind.u1 conv.u8 ldloc.0 ldc.i4.1 add conv.i8 mul add stloc.1
   ldloc.0 ldc.i4.1 add stloc.0 br.s L
E: ldloc.1 ret
}
.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 6
  .locals init (native int p)
  ldc.i4 4096  conv.i  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)  stloc.0
  // memmove semantics expected: dst = p+1, src = p, n = 1000 => p[1..1000] = 0..999 (mod 256)
  ldloc.0 ldc.i4 1100 call void 'fill'(native int, int32)
  ldloc.0 ldc.i4.1 add  ldloc.0  ldc.i4 1000  call void 'cp'(native int, native int, int32)
  ldstr "OBSERVE cpblk(p+1, p, 1000) [non-const size] checksum = "  call void [mscorlib]System.Console::Write(string)
  ldloc.0 ldc.i4 1001 call int64 'sum'(native int, int32)  call void [mscorlib]System.Console::WriteLine(int64)
  // reference result via Buffer.MemoryCopy (documented to handle overlap)
  ldloc.0 ldc.i4 1100 call void 'fill'(native int, int32)
  ldloc.0  ldloc.0 ldc.i4.1 add  ldc.i8 1000  ldc.i8 1000
  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64)
  ldstr "OBSERVE Buffer.MemoryCopy(src=p, dst=p+1, 1000) checksum = "  call void [mscorlib]System.Console::Write(string)
  ldloc.0 ldc.i4 1001 call int64 'sum'(native int, int32)  call void [mscorlib]System.Console::WriteLine(int64)
  // backward overlap: dst = p, src = p+1
  ldloc.0 ldc.i4 1100 call void 'fill'(native int, int32)
  ldloc.0  ldloc.0 ldc.i4.1 add  ldc.i4 1000  call void 'cp'(native int, native int, int32)
  ldstr "OBSERVE cpblk(p, p+1, 1000) checksum = "  call void [mscorlib]System.Console::Write(string)
  ldloc.0 ldc.i4 1001 call int64 'sum'(native int, int32)  call void [mscorlib]System.Console::WriteLine(int64)
  ldc.i4.0
  ret
}
