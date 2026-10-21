// E2b: ldftn identity on .NET Framework: global (<Module>) method vs class method,
// ldftn compiled before vs after the target first ran.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'ident' { .ver 0:0:0:0 }
.class public abstract sealed auto ansi 'K' extends [mscorlib]System.Object
{ .method public static int32 'add1'(int32) cil managed { .maxstack 2 ldarg.0 ldc.i4.1 add ret } }
.method public static int32 'add1'(int32) cil managed { .maxstack 2 ldarg.0 ldc.i4.1 add ret }
.method public static native int g_early() cil managed noinlining { .maxstack 1 ldftn int32 'add1'(int32) ret }
.method public static native int g_late() cil managed noinlining { .maxstack 1 ldftn int32 'add1'(int32) ret }
.method public static native int c_early() cil managed noinlining { .maxstack 1 ldftn int32 K::'add1'(int32) ret }
.method public static native int c_late() cil managed noinlining { .maxstack 1 ldftn int32 K::'add1'(int32) ret }
.method public static void cmp(string, native int, native int) cil managed
{ .maxstack 3 ldarg.0 call void [mscorlib]System.Console::Write(string)
  ldarg.1 ldarg.2 ceq brtrue EQ ldstr " DIFFER" call void [mscorlib]System.Console::WriteLine(string) ret
EQ: ldstr " equal" call void [mscorlib]System.Console::WriteLine(string) ret }
.method public static int32 main() cil managed
{ .entrypoint .maxstack 3 .locals (native int ge, native int ce)
  call native int g_early() stloc.0
  call native int c_early() stloc.1
  ldc.i4.1 call int32 'add1'(int32) pop
  ldc.i4.1 call int32 K::'add1'(int32) pop
  ldstr "  global method: early vs late ldftn:" ldloc.0 call native int g_late() call void cmp(string, native int, native int)
  ldstr "  class method:  early vs late ldftn:" ldloc.1 call native int c_late() call void cmp(string, native int, native int)
  ldc.i4.0 ret }
