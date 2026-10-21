// Per-object init method (mc-streamer 5.9 model) storing an initonly
// address field outside the type initializer, versus the same store inside
// <Module>::.cctor (linker-synthesized model).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly initchk { .ver 0:0:0:0 }
.module initchk.exe
.field public static initonly native int 'g_obj'
.field public static initonly native int 'g_link'
.method private static void '<msil>init_data$a.o'() cil managed
{
  .maxstack 1
  ldc.i4.s 42
  conv.i
  stsfld native int 'g_obj'
  ret
}
.method private specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 1
  call void '<msil>init_data$a.o'()
  ldc.i4.7
  conv.i
  stsfld native int 'g_link'
  ret
}
.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 2
  ldsfld native int 'g_obj'
  ldsfld native int 'g_link'
  add
  conv.i4
  ret
}
