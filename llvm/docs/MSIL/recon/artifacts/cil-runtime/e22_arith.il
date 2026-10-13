// e22: bitcasts (portable: through a local's address), shift-amount types, out-of-range shifts,
// division traps, and '.locals' WITHOUT 'init'.
#include "common.il"
.assembly e22_arith {}
.module e22_arith.dll

.method assembly static int32 'f2i'(float32 f) cil managed noinlining
{ .maxstack 1  ldarga.s f  ldind.i4  ret }                       // bitcast float -> i32
.method assembly static float32 'i2f'(int32 i) cil managed noinlining
{ .maxstack 1  ldarga.s i  ldind.r4  ret }                       // bitcast i32 -> float
.method assembly static int64 'd2l'(float64 d) cil managed noinlining
{ .maxstack 1  .locals (float64 t)  ldarg.0  stloc.0  ldloca.s t  ldind.i8  ret }   // via a local, no 'init'
.method assembly static int64 'shl64_by_i64'(int64 v, int64 n) cil managed noinlining
{ .maxstack 2  ldarg.0  ldarg.1  shl  ret }                      // int64 shift amount (ECMA: int32/native int)
.method assembly static int64 'shl64_by_i32'(int64 v, int32 n) cil managed noinlining
{ .maxstack 2  ldarg.0  ldarg.1  shl  ret }
.method assembly static int32 'shl32'(int32 v, int32 n) cil managed noinlining
{ .maxstack 2  ldarg.0  ldarg.1  shl  ret }
.method assembly static int32 'shr_un32'(int32 v, int32 n) cil managed noinlining
{ .maxstack 2  ldarg.0  ldarg.1  shr.un  ret }
.method assembly static int64 'sar64'(int64 v, int32 n) cil managed noinlining
{ .maxstack 2  ldarg.0  ldarg.1  shr  ret }
.method assembly static int32 'div32'(int32 a, int32 b) cil managed noinlining
{ .maxstack 2  ldarg.0  ldarg.1  div  ret }
.method assembly static int32 'divun32'(int32 a, int32 b) cil managed noinlining
{ .maxstack 2  ldarg.0  ldarg.1  div.un  ret }
.method assembly static int32 'remun32'(int32 a, int32 b) cil managed noinlining
{ .maxstack 2  ldarg.0  ldarg.1  rem.un  ret }
.method assembly static float64 'frem'(float64 a, float64 b) cil managed noinlining
{ .maxstack 2  ldarg.0  ldarg.1  rem  ret }

.method assembly static void 'trydiv'(string name, int32 a, int32 b) cil managed
{
  .maxstack 3
  .try
  {
    ldstr "OBSERVE "  ldarg.0  call string [mscorlib]System.String::Concat(string, string)  call void [mscorlib]System.Console::Write(string)
    ldarg.1  ldarg.2  call int32 'div32'(int32, int32)
    ldstr " = "  call void [mscorlib]System.Console::Write(string)
    call void [mscorlib]System.Console::WriteLine(int32)
    leave.s DONE
  }
  catch [mscorlib]System.Exception
  {
    ldstr " -> "  call void [mscorlib]System.Console::Write(string)
    callvirt instance class [mscorlib]System.Type [mscorlib]System.Object::GetType()
    call void [mscorlib]System.Console::WriteLine(object)
    leave.s DONE
  }
DONE:
  ret
}

.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 4
  ldstr "bitcast f32->i32 (ldarga+ldind.i4)"  ldc.r4 1.5  call int32 'f2i'(float32)  conv.i8  ldc.i8 0x3FC00000  call void 'check.i8'(string, int64, int64)
  ldstr "bitcast i32->f32 (ldarga+ldind.r4)"  ldc.i4 0xC0200000  call float32 'i2f'(int32)  conv.r8  ldc.r8 -2.5  call void 'check.r8'(string, float64, float64)
  ldstr "bitcast f64->i64 via local (no init)"  ldc.r8 -0.0  call int64 'd2l'(float64)  ldc.i8 0x8000000000000000  call void 'check.i8'(string, int64, int64)
  ldstr "bitcast f32 NaN payload preserved"  ldc.i4 0x7FC00123  call float32 'i2f'(int32)  call int32 'f2i'(float32)  conv.i8  ldc.i8 0x7FC00123  call void 'check.i8'(string, int64, int64)
  ldstr "shl int64 by int64 amount"  ldc.i8 1  ldc.i8 40  call int64 'shl64_by_i64'(int64, int64)  ldc.i8 0x10000000000  call void 'check.i8'(string, int64, int64)
  ldstr "shl int64 by int32 amount"  ldc.i8 1  ldc.i4 40  call int64 'shl64_by_i32'(int64, int32)  ldc.i8 0x10000000000  call void 'check.i8'(string, int64, int64)
  ldstr "shr.un int32"  ldc.i4 -1  ldc.i4 28  call int32 'shr_un32'(int32, int32)  conv.i8  ldc.i8 15  call void 'check.i8'(string, int64, int64)
  ldstr "shr int64 (arith)"  ldc.i8 -256  ldc.i4 4  call int64 'sar64'(int64, int32)  ldc.i8 -16  call void 'check.i8'(string, int64, int64)
  ldstr "div.un"  ldc.i4 -2  ldc.i4 3  call int32 'divun32'(int32, int32)  conv.i8  ldc.i8 1431655764  call void 'check.i8'(string, int64, int64)
  ldstr "rem.un"  ldc.i4 -1  ldc.i4 10  call int32 'remun32'(int32, int32)  conv.i8  ldc.i8 5  call void 'check.i8'(string, int64, int64)
  ldstr "frem (fmod semantics)"  ldc.r8 -7.5  ldc.r8 2.0  call float64 'frem'(float64, float64)  ldc.r8 -1.5  call void 'check.r8'(string, float64, float64)
  // out-of-range shifts are poison in LLVM; observe only
  ldstr "OBSERVE shl int32 by 33 = "  call void [mscorlib]System.Console::Write(string)  ldc.i4.1  ldc.i4 33  call int32 'shl32'(int32, int32)  call void [mscorlib]System.Console::WriteLine(int32)
  ldstr "OBSERVE shl int64 by 65 (int64 amt) = "  call void [mscorlib]System.Console::Write(string)  ldc.i8 1  ldc.i8 65  call int64 'shl64_by_i64'(int64, int64)  call void [mscorlib]System.Console::WriteLine(int64)
  // division traps (UB in C)
  ldstr "div INT_MIN / -1"  ldc.i4 0x80000000  ldc.i4.m1  call void 'trydiv'(string, int32, int32)
  ldstr "div 1 / 0"  ldc.i4.1  ldc.i4.0  call void 'trydiv'(string, int32, int32)
  call int32 'report'()
  ret
}
