// e18: taking the address of a P/Invoke method (C: 'int (*p)(int) = abs;') with ldftn and
// calling it through 'calli' with the managed signature.
#include "common.il"
.assembly e18_ldftn_pinvoke {}
.method public static pinvokeimpl("ucrtbase.dll" cdecl) int32 'abs'(int32 x) cil managed preservesig {}
.method public static pinvokeimpl("ucrtbase.dll" cdecl) native int 'strlen'(native int s) cil managed preservesig {}
.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 4
  .locals init (native int p, native int buf)
  ldftn int32 'abs'(int32)
  stloc.0
  ldstr "calli managed -> ldftn(pinvoke abs)"  ldc.i4 -17  ldloc.0  calli int32(int32)  conv.i8  ldc.i8 17  call void 'check.i8'(string, int64, int64)
  ldc.i4 8  localloc  stloc.1
  ldloc.1  ldc.i4 0x00434241  stind.i4
  ldstr "calli managed -> ldftn(pinvoke strlen)"  ldloc.1  ldftn native int 'strlen'(native int)  calli native int(native int)  conv.i8  ldc.i8 3  call void 'check.i8'(string, int64, int64)
  call int32 'report'()
  ret
}
