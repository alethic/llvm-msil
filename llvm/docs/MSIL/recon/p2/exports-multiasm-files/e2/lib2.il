// E2 library: exported data (accessor vs public field) and function addresses.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'lib2' { .ver 1:0:0:0 }

.class public abstract sealed auto ansi 'Contoso.Lib2.Native' extends [mscorlib]System.Object
{
  // C: int twice(int);
  .method public static int32 'twice'(int32) cil managed { .maxstack 2 ldarg.0 ldc.i4.1 shl ret }
  // C: int bump(void) { return ++counter; }
  .method public static int32 'bump'() cil managed
  { .maxstack 3 ldsfld native int 'counter' ldsfld native int 'counter' ldind.i4 ldc.i4.1 add stind.i4 ldsfld native int 'counter' ldind.i4 ret }
  // C: int (*get_twice(void))(int) { return twice; }   (reads the library's own '<msil>fn_twice')
  .method public static native int 'get_twice'() cil managed { .maxstack 1 ldsfld native int '<msil>fn_twice' ret }
  // exported data, option A: an address accessor ("&counter")
  .method public static native int '<msil>addr_counter'() cil managed { .maxstack 1 ldsfld native int 'counter' ret }
  // exported function address: the library's own '<msil>fn_' value
  .method public static native int '<msil>addr_twice'() cil managed { .maxstack 1 ldsfld native int '<msil>fn_twice' ret }
  // a library method whose 'ldftn' is compiled late (same module as the target)
  .method public static native int 'ldftn_late'() cil managed noinlining { .maxstack 1 ldftn int32 'Contoso.Lib2.Native'::'twice'(int32) ret }
  // exported data, option B: a public initonly field holding the address
  .field public static initonly native int 'counter'
}

.field assembly static initonly native int 'counter'
.field assembly static initonly native int '<msil>fn_twice'
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 3
  ldc.i4.8 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  dup ldc.i4.5 stind.i4
  dup stsfld native int 'counter'
  stsfld native int 'Contoso.Lib2.Native'::'counter'
  ldftn int32 'Contoso.Lib2.Native'::'twice'(int32)
  stsfld native int '<msil>fn_twice'
  ret
}
