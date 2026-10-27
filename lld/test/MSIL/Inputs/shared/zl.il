// zl: a library in the shape msil-ld -shared writes (ImplementationPlan.md
// 5.9.15, M22.b item 2; recon p2/exports-multiasm.md 9, e7/zl.il.in), for
// shared-checks.test. msil-ld cannot write the broken variants the test
// needs, so gen.py assembles this source with ilasm, puts the table that
// symtab.py encodes in place of @BLOB@, and converts each image into
// yaml2obj YAML (zl-*.yaml here). What a linked program can use:
//   int counter = 5;            data: '<msil>addr_counter' and property
//                               counter, both in Contoso.Zl.Native
//   int twice(int x);           a function and '<msil>addr_twice'
//   int bump(void);             ++counter, and '<msil>addr_bump'
// '<msil>Data' runs phase 1 (allocation, image copy, address field, the
// '<msil>fn_' fields); there is no constructor, so no <Module>::.cctor.
// The '#define' lines gen.py prepends make the malformed variants:
//   NOTPUBLIC    the export class is not public
//   NOADDR_FN    twice has no '<msil>addr_twice'
//   NOPROP       counter has no property
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly zl
{
  .custom instance void '<msil>SymbolTable'::.ctor(uint8[]) = @BLOB@
  .ver 1:2:3:4
}
.module zl.dll

.class private auto ansi sealed beforefieldinit '<msil>SymbolTable' extends [mscorlib]System.Attribute
{
  .custom instance void [mscorlib]System.AttributeUsageAttribute::.ctor(valuetype [mscorlib]System.AttributeTargets)
    = ( 01 00 01 00 00 00 01 00 54 02 0D 41 6C 6C 6F 77 4D 75 6C 74 69 70 6C 65 01 )
  .method public hidebysig specialname rtspecialname instance void .ctor(uint8[] A_0) cil managed
  {
    .maxstack 1
    ldarg.0
    call instance void [mscorlib]System.Attribute::.ctor()
    ret
  }
}

.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType { .pack 1 .size 4 }
.data cil '<msil>image_d' = bytearray ( 05 00 00 00 )
.field assembly static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'

.class private abstract sealed auto ansi beforefieldinit '<msil>Data' extends [mscorlib]System.Object
{
  .field assembly static initonly native int '<msil>data'
  .field assembly static initonly native int 'counter'
  .field assembly static initonly native int '<msil>fn_twice'
  .field assembly static initonly native int '<msil>fn_bump'
  .method private hidebysig specialname rtspecialname static void .cctor() cil managed
  {
    .maxstack 3
    ldc.i4.s 19
    conv.i
    call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
    ldc.i4.s 15
    conv.i
    add
    ldc.i4.s -16
    conv.i
    and
    stsfld native int '<msil>Data'::'<msil>data'
    ldsfld native int '<msil>Data'::'<msil>data'
    ldsflda valuetype '<msil>image_t' '<msil>image'
    ldc.i4.4
    cpblk
    ldsfld native int '<msil>Data'::'<msil>data'
    stsfld native int '<msil>Data'::'counter'
    ldftn int32 'Contoso.Zl.Native'::'twice'(int32)
    stsfld native int '<msil>Data'::'<msil>fn_twice'
    ldftn int32 'Contoso.Zl.Native'::'bump'()
    stsfld native int '<msil>Data'::'<msil>fn_bump'
    ret
  }
}

#ifdef NOTPUBLIC
.class private abstract sealed auto ansi 'Contoso.Zl.Native' extends [mscorlib]System.Object
#else
.class public abstract sealed auto ansi 'Contoso.Zl.Native' extends [mscorlib]System.Object
#endif
{
  .method public static int32 'twice'(int32 A_0) cil managed
  {
    .maxstack 2
    ldarg.0
    ldc.i4.1
    shl
    ret
  }
  .method public static int32 'bump'() cil managed
  {
    .maxstack 3
    .locals (int32)
    ldsfld native int '<msil>Data'::'counter'
    ldsfld native int '<msil>Data'::'counter'
    ldind.i4
    ldc.i4.1
    add
    dup
    stloc.0
    stind.i4
    ldloc.0
    ret
  }
#ifndef NOADDR_FN
  .method public hidebysig static native int '<msil>addr_twice'() cil managed
  {
    .maxstack 1
    ldsfld native int '<msil>Data'::'<msil>fn_twice'
    ret
  }
#endif
  .method public hidebysig static native int '<msil>addr_bump'() cil managed
  {
    .maxstack 1
    ldsfld native int '<msil>Data'::'<msil>fn_bump'
    ret
  }
  .method public hidebysig specialname static native int '<msil>addr_counter'() cil managed
  {
    .maxstack 1
    ldsfld native int '<msil>Data'::'counter'
    ret
  }
#ifndef NOPROP
  .property native int 'counter'()
  {
    .get native int 'Contoso.Zl.Native'::'<msil>addr_counter'()
  }
#endif
}
