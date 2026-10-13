// e08: P/Invoke declarations (pinvokeimpl) calling native CRT functions on Windows.
//   - puts from ucrtbase.dll, from msvcrt.dll, and from "ucrtbase" (no extension)
//   - 'as "entry"' renaming
//   - C string argument taken directly from an RVA .data blob (address inside the PE image)
//   - interleaving of Console output and CRT stdio, fflush(NULL)
#include "common.il"
.assembly e08b_noflush {}
.module e08b_noflush.dll

.method public static pinvokeimpl("ucrtbase.dll" cdecl) int32 'puts'(native int s) cil managed preservesig {}
.method public static pinvokeimpl("msvcrt.dll" as "puts" cdecl) int32 'msvcrt_puts'(native int s) cil managed preservesig {}
.method public static pinvokeimpl("ucrtbase" as "puts" cdecl) int32 'puts_noext'(native int s) cil managed preservesig {}
.method public static pinvokeimpl("ucrtbase.dll" cdecl) native int 'strlen'(native int s) cil managed preservesig {}
.method public static pinvokeimpl("ucrtbase.dll" cdecl) int32 'fflush'(native int f) cil managed preservesig {}
.method public static pinvokeimpl("ucrtbase.dll" cdecl) int32 'abs'(int32 x) cil managed preservesig {}
.method public static pinvokeimpl("ucrtbase.dll" cdecl) float64 'sqrt'(float64 x) cil managed preservesig {}
.method public static pinvokeimpl("ucrtbase.dll" cdecl) int64 '_abs64'(int64 x) cil managed preservesig {}

// C strings as RVA data (NUL-terminated), each wrapped in an explicit-size value type
.class private explicit ansi sealed 'blob6'  extends [mscorlib]System.ValueType { .pack 1 .size 6 }
.class private explicit ansi sealed 'blob13' extends [mscorlib]System.ValueType { .pack 1 .size 13 }
.field static assembly valuetype 'blob6'  'str.hello' at D_HELLO
.field static assembly valuetype 'blob13' 'str.msvcrt' at D_MSVCRT
.data D_HELLO  = bytearray (68 65 6C 6C 6F 00)                              // "hello"
.data D_MSVCRT = bytearray (66 72 6F 6D 20 6D 73 76 63 72 74 21 00)        // "from msvcrt!"

.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 8
  ldstr "[Console] before native puts"
  call void [mscorlib]System.Console::WriteLine(string)
  ldsflda valuetype 'blob6' 'str.hello'
  conv.u                                     // & -> native int (unmanaged pointer into the image)
  call int32 'puts'(native int)
  pop
  ldsflda valuetype 'blob13' 'str.msvcrt'
  conv.u
  call int32 'msvcrt_puts'(native int)
  pop
  ldsflda valuetype 'blob6' 'str.hello'
  conv.u
  call int32 'puts_noext'(native int)
  pop
  ldstr "[Console] after native puts"
  call void [mscorlib]System.Console::WriteLine(string)
  ldstr "strlen(\"hello\")"  ldsflda valuetype 'blob6' 'str.hello'  conv.u  call native int 'strlen'(native int)  conv.i8  ldc.i8 5  call void 'check.i8'(string, int64, int64)
  ldstr "abs(-5)"    ldc.i4 -5  call int32 'abs'(int32)  conv.i8  ldc.i8 5  call void 'check.i8'(string, int64, int64)
  ldstr "_abs64"     ldc.i8 -0x100000000  call int64 '_abs64'(int64)  ldc.i8 0x100000000  call void 'check.i8'(string, int64, int64)
  ldstr "sqrt(2.25)" ldc.r8 2.25  call float64 'sqrt'(float64)  ldc.r8 1.5  call void 'check.r8'(string, float64, float64)
  // flush CRT stdio so output is not lost when stdout is a pipe/file
  // (fflush removed)
  call int32 'report'()
  ret
}
