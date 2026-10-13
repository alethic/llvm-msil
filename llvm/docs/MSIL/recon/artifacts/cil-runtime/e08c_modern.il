// e08c: modern-.NET-only interop pieces (net8+):
//   - [assembly: DisableRuntimeMarshalling]
//   - [SuppressGCTransition] on a P/Invoke
//   - NativeLibrary.Load/GetExport + 'calli unmanaged cdecl'
//   - NativeMemory.Alloc/Free
#include "common.il"
.assembly extern System.Runtime { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A) .ver 8:0:0:0 }
.assembly extern System.Runtime.InteropServices { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A) .ver 8:0:0:0 }
.assembly e08c_modern
{
  .custom instance void [System.Runtime]System.Runtime.CompilerServices.DisableRuntimeMarshallingAttribute::.ctor() = (01 00 00 00)
}
.module e08c_modern.dll

.method public static pinvokeimpl("ucrtbase.dll" cdecl) int32 'abs'(int32 x) cil managed preservesig
{
  .custom instance void [System.Runtime]System.Runtime.InteropServices.SuppressGCTransitionAttribute::.ctor() = (01 00 00 00)
}
.method public static pinvokeimpl("ucrtbase.dll" cdecl) native int 'strlen'(native int s) cil managed preservesig {}

.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 8
  .locals init (native int lib, native int fp, native int mem)
  ldstr "abs with SuppressGCTransition"  ldc.i4 -9  call int32 'abs'(int32)  conv.i8  ldc.i8 9  call void 'check.i8'(string, int64, int64)
  // NativeMemory.Alloc(16) -> write "abc\0" -> strlen
  ldc.i4 16  conv.u
  call void* [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeMemory::Alloc(native uint)
  stloc.2
  ldloc.2  ldc.i4 0x00636261  stind.i4
  ldstr "strlen on NativeMemory buffer"  ldloc.2  call native int 'strlen'(native int)  conv.i8  ldc.i8 3  call void 'check.i8'(string, int64, int64)
  // NativeLibrary + calli unmanaged cdecl
  ldstr "ucrtbase.dll"
  call native int [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeLibrary::Load(string)
  stloc.0
  ldloc.0  ldstr "strlen"
  call native int [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeLibrary::GetExport(native int, string)
  stloc.1
  ldstr "calli unmanaged cdecl strlen"  ldloc.2  ldloc.1  calli unmanaged cdecl native int(native int)  conv.i8  ldc.i8 3  call void 'check.i8'(string, int64, int64)
  ldloc.2
  call void [System.Runtime.InteropServices]System.Runtime.InteropServices.NativeMemory::Free(void*)
  call int32 'report'()
  ret
}
