// e08L: P/Invoke on Linux (glibc): "libc" vs "libc.so.6"; puts / strlen / fflush / getpid.
#include "common.il"
.assembly e08L_linux {}
.module e08L_linux.dll
.method public static pinvokeimpl("libc" cdecl) int32 'puts'(native int s) cil managed preservesig {}
.method public static pinvokeimpl("libc.so.6" as "puts" cdecl) int32 'puts6'(native int s) cil managed preservesig {}
.method public static pinvokeimpl("libc" cdecl) native int 'strlen'(native int s) cil managed preservesig {}
.method public static pinvokeimpl("libc" cdecl) int32 'fflush'(native int f) cil managed preservesig {}
.method public static pinvokeimpl("libc" cdecl) int32 'getpid'() cil managed preservesig {}
.class private explicit ansi sealed 'blob6' extends [mscorlib]System.ValueType { .pack 1 .size 6 }
.field static assembly valuetype 'blob6' 'str.hello' at D_HELLO
.data D_HELLO = bytearray (68 65 6C 6C 6F 00)
.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 8
  ldstr "[Console] before native puts"  call void [mscorlib]System.Console::WriteLine(string)
  ldsflda valuetype 'blob6' 'str.hello'  conv.u  call int32 'puts'(native int)  pop
  ldsflda valuetype 'blob6' 'str.hello'  conv.u  call int32 'puts6'(native int)  pop
  ldstr "[Console] after native puts"   call void [mscorlib]System.Console::WriteLine(string)
  ldstr "strlen"  ldsflda valuetype 'blob6' 'str.hello'  conv.u  call native int 'strlen'(native int)  conv.i8  ldc.i8 5  call void 'check.i8'(string, int64, int64)
  ldstr "getpid>0"  call int32 'getpid'()  ldc.i4.0  cgt  conv.i8  ldc.i8 1  call void 'check.i8'(string, int64, int64)
  call int32 'report'()
  ldc.i4 300
  add                       // exit code 300 -> observe truncation on Linux
  ret
}
