// E6 library in the proposed P2-C shape: export class with functions, data as a
// property whose getter is '<msil>addr_<name>', function addresses through
// '<msil>addr_<name>', the symbol-table attribute, and the two-phase initializer.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'zlibc'
{
  .custom instance void '<msil>SymbolTable'::.ctor(uint8[]) = ( 01 00 08 00 00 00 4D 53 59 4D 01 00 01 00 00 00 )
  .ver 1:0:0:0
}
.class private auto ansi sealed beforefieldinit '<msil>SymbolTable' extends [mscorlib]System.Attribute
{
  .method public hidebysig specialname rtspecialname instance void .ctor(uint8[] table) cil managed
  { .maxstack 1 ldarg.0 call instance void [mscorlib]System.Attribute::.ctor() ret }
}
// phase 1: data segment and canonical function addresses; refers to no other assembly
.class private abstract sealed auto ansi beforefieldinit '<msil>Data' extends [mscorlib]System.Object
{
  .field assembly static initonly native int 'counter'
  .field assembly static initonly native int '<msil>fn_twice'
  .method private hidebysig specialname rtspecialname static void .cctor() cil managed
  {
    .maxstack 3
    ldc.i4.8 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
    dup ldc.i4.5 stind.i4
    stsfld native int '<msil>Data'::'counter'
    ldftn int32 'Contoso.Zlib.Native'::'twice'(int32)
    stsfld native int '<msil>Data'::'<msil>fn_twice'
    ret
  }
}
.class public abstract sealed auto ansi 'Contoso.Zlib.Native' extends [mscorlib]System.Object
{
  .method public static int32 'add'(int32, int32) cil managed { .maxstack 2 ldarg.0 ldarg.1 add ret }
  .method public static int32 'twice'(int32) cil managed { .maxstack 2 ldarg.0 ldc.i4.1 shl ret }
  .method public static int32 'bump'() cil managed
  { .maxstack 3 ldsfld native int '<msil>Data'::'counter' dup ldind.i4 ldc.i4.1 add stind.i4 ldsfld native int '<msil>Data'::'counter' ldind.i4 ret }
  .method public static native int 'get_twice'() cil managed { .maxstack 1 ldsfld native int '<msil>Data'::'<msil>fn_twice' ret }
  // C: size_t length(const char *s)
  .method public static native int 'length'(native int) cil managed
  { .maxstack 2 .locals (native int) ldarg.0 stloc.0
    L: ldloc.0 ldind.u1 brfalse E ldloc.0 ldc.i4.1 add stloc.0 br L
    E: ldloc.0 ldarg.0 sub ret }
  .method public hidebysig specialname static native int '<msil>addr_counter'() cil managed
  { .maxstack 1 ldsfld native int '<msil>Data'::'counter' ret }
  .property native int 'counter'() { .get native int 'Contoso.Zlib.Native'::'<msil>addr_counter'() }
  .method public static native int '<msil>addr_twice'() cil managed
  { .maxstack 1 ldsfld native int '<msil>Data'::'<msil>fn_twice' ret }
}
// phase 2: imports from other assemblies, cross-assembly relocations, C constructors
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{ .maxstack 1 ldstr "zlibc: constructors" call void [mscorlib]System.Console::WriteLine(string) ret }
