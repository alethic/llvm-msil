// E7 program main.exe in the proposed P2-C shape (hand-written from main.c).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern 'zl' { .ver 1:0:0:0 }
.assembly 'main' { .ver 0:0:0:0 }

.class private abstract sealed auto ansi beforefieldinit '<msil>Data' extends [mscorlib]System.Object
{
  .field assembly static initonly native int '<msil>data'
  .field assembly static initonly native int 'pc'              // +0
  .field assembly static initonly native int 'ft'              // +8
  .field assembly static initonly native int '<msil>fn_add1'
  .method private hidebysig specialname rtspecialname static void .cctor() cil managed
  {
    .maxstack 3
    ldc.i4 31 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
    ldc.i4.s 15 conv.i add ldc.i4.s -16 conv.i and stsfld native int '<msil>Data'::'<msil>data'
    ldsfld native int '<msil>Data'::'<msil>data' ldc.i4.0 ldc.i4 16 initblk
    ldsfld native int '<msil>Data'::'<msil>data' stsfld native int '<msil>Data'::'pc'
    ldsfld native int '<msil>Data'::'<msil>data' ldc.i4 8 add stsfld native int '<msil>Data'::'ft'
    ldftn int32 'add1'(int32) stsfld native int '<msil>Data'::'<msil>fn_add1'
    ret
  }
}
// phase 2: initialize the linked libraries (in link order), cross-assembly relocations, constructors
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 2
  ldtoken [zl]'Contoso.Zl.Native' pop
  // int *pc = &counter;
  ldsfld native int '<msil>Data'::'pc' call native int [zl]'Contoso.Zl.Native'::'<msil>addr_counter'() stind.i
  // int (*ft)(int) = twice;
  ldsfld native int '<msil>Data'::'ft' call native int [zl]'Contoso.Zl.Native'::'<msil>addr_twice'() stind.i
  ret
}
.method assembly static int32 'add1'(int32) cil managed { .maxstack 2 ldarg.0 ldc.i4.1 add ret }
.method public static int32 'main'() cil managed
{
  .maxstack 4
  .locals (int32 fails)
  ldc.i4.0 stloc.0
  // (counter != 100) << 0
  ldloc.0 call native int [zl]'Contoso.Zl.Native'::'<msil>addr_counter'() ldind.i4 ldc.i4 100 ceq ldc.i4.0 ceq or stloc.0
  // (pc != &counter || pcounter != &counter) << 1
  ldsfld native int '<msil>Data'::'pc' ldind.i call native int [zl]'Contoso.Zl.Native'::'<msil>addr_counter'() ceq
  call native int [zl]'Contoso.Zl.Native'::'<msil>addr_pcounter'() ldind.i call native int [zl]'Contoso.Zl.Native'::'<msil>addr_counter'() ceq and
  ldc.i4.0 ceq ldc.i4.1 shl ldloc.0 or stloc.0
  // (ft != get_twice() || ft != &twice) << 2
  ldsfld native int '<msil>Data'::'ft' ldind.i call native int [zl]'Contoso.Zl.Native'::'get_twice'() ceq
  ldsfld native int '<msil>Data'::'ft' ldind.i call native int [zl]'Contoso.Zl.Native'::'<msil>addr_twice'() ceq and
  ldc.i4.0 ceq ldc.i4.2 shl ldloc.0 or stloc.0
  // (ft(21) != 42) << 3
  ldc.i4.s 21 ldsfld native int '<msil>Data'::'ft' ldind.i calli int32(int32) ldc.i4.s 42 ceq ldc.i4.0 ceq ldc.i4.3 shl ldloc.0 or stloc.0
  // (apply(add1, 41) != 42) << 4
  ldsfld native int '<msil>Data'::'<msil>fn_add1' ldc.i4.s 41 call int32 [zl]'Contoso.Zl.Native'::'apply'(native int, int32) ldc.i4.s 42 ceq ldc.i4.0 ceq ldc.i4.4 shl ldloc.0 or stloc.0
  // *pc = 7; (bump() != 8) << 5
  ldsfld native int '<msil>Data'::'pc' ldind.i ldc.i4.7 stind.i4
  call int32 [zl]'Contoso.Zl.Native'::'bump'() ldc.i4.8 ceq ldc.i4.0 ceq ldc.i4.5 shl ldloc.0 or stloc.0
  ldloc.0 ret
}
.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{ .entrypoint .maxstack 1 call int32 'main'() ret }
