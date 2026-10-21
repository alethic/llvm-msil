// What msil-ld would have to emit to call the C# library from C: MemberRefs with the
// C# signatures (int64, int32*), fed with the native int values that MSIL codegen uses (D2).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern 'cslib' { .ver 0:0:0:0 }
.assembly 'cprog' { .ver 0:0:0:0 }
.method public static void sayi(string, int64) cil managed
{ .maxstack 2 ldarg.0 ldarg.1 box [mscorlib]System.Int64 call string [mscorlib]System.String::Concat(object, object) call void [mscorlib]System.Console::WriteLine(string) ret }
.method public static int32 main() cil managed
{ .entrypoint .maxstack 4 .locals (native int)
  ldstr "  Add(40, 2) [int64 params, native int args] = " ldc.i4 40 conv.i ldc.i4.2 conv.i
  call int64 [cslib]Contoso.Managed.Api::Add(int64, int64) call void sayi(string, int64)
  ldc.i4.s 12 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int) stloc.0
  ldloc.0 ldc.i4.5 stind.i4 ldloc.0 ldc.i4.4 add ldc.i4.6 stind.i4 ldloc.0 ldc.i4.8 add ldc.i4.7 stind.i4
  ldstr "  Sum(p, 3) [int32* param, native int arg] = " ldloc.0 ldc.i4.3
  call int32 [cslib]Contoso.Managed.Api::Sum(int32*, int32) conv.i8 call void sayi(string, int64)
  ldstr "  Twice(21) [native int] = " ldc.i4.s 21 conv.i
  call native int [cslib]Contoso.Managed.Api::Twice(native int) conv.i8 call void sayi(string, int64)
  ldc.i4.0 ret }
