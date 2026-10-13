// e03: global (<Module>) methods vs class methods; scalar signatures.
#include "common.il"
.assembly e03_sigs {}
.module e03_sigs.dll

.method static int32 'add_i32'(int32 a, int32 b) cil managed
{ .maxstack 2  ldarg.0  ldarg.1  add  ret }
.method static int64 'add_i64'(int64 a, int64 b) cil managed
{ .maxstack 2  ldarg.0  ldarg.1  add  ret }
.method static native int 'add_ni'(native int a, native int b) cil managed
{ .maxstack 2  ldarg.0  ldarg.1  add  ret }
.method static float32 'add_f32'(float32 a, float32 b) cil managed
{ .maxstack 2  ldarg.0  ldarg.1  add  ret }
.method static float64 'add_f64'(float64 a, float64 b) cil managed
{ .maxstack 2  ldarg.0  ldarg.1  add  ret }
.method static void 'void_fn'() cil managed
{ .maxstack 1  ret }
// mixed signature, returns native int = a + b + c + (long)d + (long)e
.method static native int 'mixed'(int32 a, int64 b, native int c, float32 d, float64 e) cil managed
{
  .maxstack 3
  ldarg.0
  conv.i          // int32 -> native int (sign extend)
  ldarg.1
  conv.i          // int64 -> native int
  add
  ldarg.2
  add
  ldarg.3
  conv.i8
  conv.i
  add
  ldarg.s e
  conv.i8
  conv.i
  add
  ret
}

.namespace Ns
{
  .class public abstract sealed auto ansi Exports extends [mscorlib]System.Object
  {
    // same simple name as a global method
    .method public static int32 'add_i32'(int32 a, int32 b) cil managed
    {
      .maxstack 2
      ldarg.0
      ldarg.1
      call int32 'add_i32'(int32, int32)        // unqualified: binds to global <Module> method?
      ldc.i4 1000
      add
      ret
    }
  }
}

.class public abstract sealed auto ansi 'Quoted.Dotted.Cls' extends [mscorlib]System.Object
{
  .method public static int32 'seven'() cil managed { .maxstack 1 ldc.i4.7 ret }
}

.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 8
  ldstr "add_i32"   ldc.i4 -5  ldc.i4 3  call int32 'add_i32'(int32, int32)  conv.i8  ldc.i8 -2  call void 'check.i8'(string, int64, int64)
  ldstr "add_i64"   ldc.i8 0x100000000  ldc.i8 -1  call int64 'add_i64'(int64, int64)  ldc.i8 0xFFFFFFFF  call void 'check.i8'(string, int64, int64)
  ldstr "add_ni"    ldc.i4 -1  conv.i  ldc.i4 2  conv.i  call native int 'add_ni'(native int, native int)  conv.i8  ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "add_ni(int32 args, implicit)"  ldc.i4 -1  ldc.i4 2  call native int 'add_ni'(native int, native int)  conv.i8  ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "add_f32"   ldc.r4 1.5  ldc.r4 2.25  call float32 'add_f32'(float32, float32)  conv.r8  ldc.r8 3.75  call void 'check.r8'(string, float64, float64)
  ldstr "add_f64"   ldc.r8 0.1  ldc.r8 0.2  call float64 'add_f64'(float64, float64)  ldc.r8 0.30000000000000004  call void 'check.r8'(string, float64, float64)
  call void 'void_fn'()
  ldstr "mixed"     ldc.i4 1  ldc.i8 2  ldc.i4 3  conv.i  ldc.r4 4.9  ldc.r8 5.9  call native int 'mixed'(int32, int64, native int, float32, float64)  conv.i8  ldc.i8 15  call void 'check.i8'(string, int64, int64)
  ldstr "Ns.Exports::add_i32 -> global add_i32"  ldc.i4 1  ldc.i4 2  call int32 Ns.Exports::'add_i32'(int32, int32)  conv.i8  ldc.i8 1003  call void 'check.i8'(string, int64, int64)
  ldstr "'Quoted.Dotted.Cls'::seven"  call int32 'Quoted.Dotted.Cls'::'seven'()  conv.i8  ldc.i8 7  call void 'check.i8'(string, int64, int64)
  // reflection: how did ilasm split the quoted dotted class name?
  ldstr "typeof(Quoted.Dotted.Cls) Namespace/Name: "
  call void [mscorlib]System.Console::Write(string)
  ldtoken 'Quoted.Dotted.Cls'
  call class [mscorlib]System.Type [mscorlib]System.Type::GetTypeFromHandle(valuetype [mscorlib]System.RuntimeTypeHandle)
  dup
  callvirt instance string [mscorlib]System.Type::get_Namespace()
  call void [mscorlib]System.Console::Write(string)
  ldstr " / "
  call void [mscorlib]System.Console::Write(string)
  callvirt instance string [mscorlib]System.Reflection.MemberInfo::get_Name()
  call void [mscorlib]System.Console::WriteLine(string)
  call int32 'report'()
  ret
}
