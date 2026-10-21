// E1 library: default-visibility C functions are public static methods of the
// export class Contoso.Zlib.Native; hidden ones stay assembly methods of <Module>.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'zlibx' { .ver 1:2:3:4 }

.class public abstract sealed auto ansi 'Contoso.Zlib.Native' extends [mscorlib]System.Object
{
  .method public static int32 'add'(int32, int32) cil managed
  {
    .maxstack 2
    ldarg.0
    ldarg.1
    add
    ret
  }
  // Reads C data through the <Module> address field, and calls a hidden helper in <Module>.
  .method public static int32 'bump'() cil managed
  {
    .maxstack 3
    ldsfld native int 'counter'
    ldsfld native int 'counter'
    ldind.i4
    ldc.i4.1
    add
    stind.i4
    ldsfld native int 'counter'
    ldind.i4
    call int32 'helper'(int32)
    ret
  }
}

.method assembly static int32 'helper'(int32) cil managed
{
  .maxstack 2
  ldarg.0
  ldc.i4.s 10
  mul
  ret
}

.field public static initonly native int 'counter'
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 3
  ldstr "zlibx: module initializer"
  call void [mscorlib]System.Console::WriteLine(string)
  ldc.i4.8
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  dup
  ldc.i4 100
  stind.i4
  stsfld native int 'counter'
  ret
}
