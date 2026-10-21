.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly perobj { }
.module perobj.exe
.field public static initonly native int 'g'
// mc-streamer data model 2: a per-object init method stores the object's address field,
// and the linker-written .cctor only calls it.
.method assembly static void '<msil>init_data$a'() cil managed
{
  .maxstack 1
  ldc.i4 4096
  conv.i
  stsfld native int 'g'
  ret
}
.method private specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 0
  call void '<msil>init_data$a'()
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
