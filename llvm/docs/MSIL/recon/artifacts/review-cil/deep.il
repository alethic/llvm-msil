// -O0-shaped recursion: every frame does 'ldc.i4 16; localloc' and spills its arg there.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly deep {}
.method public static int32 'rec'(int32) cil managed
{
  .maxstack 3
  .locals (native int)
  ldc.i4 16 localloc stloc.0
  ldloc.0 ldarg.0 stind.i4
  ldloc.0 ldind.i4 brtrue L
  ldc.i4.0 ret
L:
  ldloc.0 ldind.i4 ldc.i4.1 sub call int32 'rec'(int32)
  ldc.i4.1 add ret
}
.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 2
  ldarg.0 ldlen conv.i4 brtrue USE
  ldc.i4 10000 call int32 'rec'(int32) ldc.i4 255 and ret
USE:
  ldarg.0 ldc.i4.0 ldelem.ref call int32 [mscorlib]System.Int32::Parse(string) call int32 'rec'(int32) ldc.i4 255 and ret
}
