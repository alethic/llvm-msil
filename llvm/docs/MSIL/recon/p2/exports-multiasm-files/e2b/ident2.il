// E2b-2: as ident.il, but the target first runs through calli (as in fnptr-call.c),
// and the early value comes from <Module>::.cctor.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'ident2' { .ver 0:0:0:0 }
.class public abstract sealed auto ansi 'K' extends [mscorlib]System.Object
{ .method public static int32 'add1'(int32) cil managed { .maxstack 2 ldarg.0 ldc.i4.1 add ret } }
.method public static int32 'add1'(int32) cil managed { .maxstack 2 ldarg.0 ldc.i4.1 add ret }
.field static native int 'gf'
.field static native int 'cf'
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{ .maxstack 1 ldftn int32 'add1'(int32) stsfld native int 'gf' ldftn int32 K::'add1'(int32) stsfld native int 'cf' ret }
.method public static native int g_late() cil managed noinlining { .maxstack 1 ldftn int32 'add1'(int32) ret }
.method public static native int c_late() cil managed noinlining { .maxstack 1 ldftn int32 K::'add1'(int32) ret }
.method public static void cmp(string, native int, native int) cil managed
{ .maxstack 3 ldarg.0 call void [mscorlib]System.Console::Write(string)
  ldarg.1 ldarg.2 ceq brtrue EQ ldstr " DIFFER" call void [mscorlib]System.Console::WriteLine(string) ret
EQ: ldstr " equal" call void [mscorlib]System.Console::WriteLine(string) ret }
.method public static int32 main() cil managed
{ .entrypoint .maxstack 3
  ldc.i4.1 ldsfld native int 'gf' calli int32(int32) pop
  ldc.i4.1 ldsfld native int 'cf' calli int32(int32) pop
  ldstr "  global method: .cctor vs late ldftn (target ran via calli):" ldsfld native int 'gf' call native int g_late() call void cmp(string, native int, native int)
  ldstr "  class method:  .cctor vs late ldftn (target ran via calli):" ldsfld native int 'cf' call native int c_late() call void cmp(string, native int, native int)
  ldc.i4.0 ret }
