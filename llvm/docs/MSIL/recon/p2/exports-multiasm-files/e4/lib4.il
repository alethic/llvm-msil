// E4 library: when does the CLR run <Module>::.cctor of a library assembly?
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'lib4' { .ver 1:0:0:0 }

.class public abstract sealed auto ansi 'L.Native' extends [mscorlib]System.Object
{
  .field public static initonly native int 'pub_g'
  .method public static int32 'pure'() cil managed { .maxstack 1 ldc.i4.s 42 ret }
  .method public static void 'pure_print'() cil managed noinlining
  { .maxstack 1 ldstr "  lib: pure_print runs" call void [mscorlib]System.Console::WriteLine(string) ret }
  .method public static int32 'data'() cil managed { .maxstack 1 ldsfld native int 'g' ldind.i4 ret }
  .method public static native int 'addr_g'() cil managed { .maxstack 1 ldsfld native int 'g' ret }
}

.field public static initonly native int 'g'
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 3
  ldstr "  lib: module initializer"
  call void [mscorlib]System.Console::WriteLine(string)
  ldc.i4.8
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  dup
  ldc.i4.7
  stind.i4
  dup
  stsfld native int 'g'
  stsfld native int 'L.Native'::'pub_g'
  ret
}
