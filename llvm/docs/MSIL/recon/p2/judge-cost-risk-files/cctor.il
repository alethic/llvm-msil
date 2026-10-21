.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly cctor { }
.module cctor.exe
.field public static initonly native int 'g'
// linker-synthesized initializer: the store is inside <Module>::.cctor itself
.method private specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 1
  ldc.i4 4096
  conv.i
  stsfld native int 'g'
  ret
}
.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 2
  ldsfld native int 'g'
  conv.i4
  ldc.i4 4096
  ceq
  ret
}
