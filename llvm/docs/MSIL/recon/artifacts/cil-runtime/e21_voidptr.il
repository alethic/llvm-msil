// e21: unmanaged pointer types (void*, uint8*) in signatures/locals interoperating with native int
#include "common.il"
// (note: an unquoted parameter named off is a syntax error -- 'off' is an ILAsm keyword)
.assembly e21_voidptr {}
.method public static int32 'rd'(void* p, int32 'off') cil managed
{ .maxstack 2  ldarg.0  ldarg.1  add  ldind.i4  ret }
.method public static uint8* 'adv'(uint8* p) cil managed
{ .maxstack 2  ldarg.0  ldc.i4.4  add  ret }
.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 4
  .locals init (native int p, void* q)
  ldc.i4 16  localloc  stloc.0
  ldloc.0  ldc.i4.4  add  ldc.i4 1234  stind.i4
  ldstr "native int passed as void*"  ldloc.0  ldc.i4.4  call int32 'rd'(void*, int32)  conv.i8  ldc.i8 1234  call void 'check.i8'(string, int64, int64)
  ldloc.0  call uint8* 'adv'(uint8*)  stloc.1
  ldstr "uint8* result stored to void* local, read"  ldloc.1  ldind.i4  conv.i8  ldc.i8 1234  call void 'check.i8'(string, int64, int64)
  call int32 'report'()
  ret
}
