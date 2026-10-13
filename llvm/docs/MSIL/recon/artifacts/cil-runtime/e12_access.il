// e12: accessibility of global (<Module>) members.
//   private global method/field used from a class in the same module, from the entrypoint, and via ldftn;
//   compiler-controlled (no access keyword => 'privatescope') vs explicit 'assembly'.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly e12_access {}
.module e12_access.dll
.field private static int32 'priv_field'
.method private static int32 'priv_fn'(int32 x) cil managed { .maxstack 2 ldarg.0 ldc.i4.1 add ret }
.method assembly static int32 'asm_fn'(int32 x) cil managed { .maxstack 2 ldarg.0 ldc.i4.2 add ret }
.method static int32 'ps_fn'(int32 x) cil managed { .maxstack 2 ldarg.0 ldc.i4.3 add ret }   // privatescope
.class public abstract sealed auto ansi Exports extends [mscorlib]System.Object
{
  .method public static int32 'viaClass'(int32 x) cil managed
  {
    .maxstack 3
    ldarg.0
    call int32 'priv_fn'(int32)          // private global method from another type in the module
    call int32 'asm_fn'(int32)
    call int32 'ps_fn'(int32)
    dup
    stsfld int32 'priv_field'            // private global field from another type
    ldc.i4.0
    conv.i
    pop
    ldftn int32 'priv_fn'(int32)
    calli int32(int32)
    ret
  }
}
.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 2
  ldc.i4 30
  call int32 Exports::'viaClass'(int32)     // 30+1+2+3+1 = 37
  ldsfld int32 'priv_field'                 // 36
  add                                       // 73
  ret
}
