// Constant-size localloc (as the M7 prologue emits it) observed under full opts:
// OR of the low 4 address bits over many calls at varying stack depths.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly align16 {}
.method assembly static int32 'f16'() cil managed noinlining
{ .maxstack 2  ldc.i4 16 localloc conv.i4 ldc.i4 15 and ret }
.method assembly static int32 'f32'() cil managed noinlining
{ .maxstack 2  ldc.i4 32 localloc conv.i4 ldc.i4 15 and ret }
.method assembly static int32 'f8'() cil managed noinlining
{ .maxstack 2  ldc.i4 8 localloc conv.i4 ldc.i4 15 and ret }
.method assembly static int32 'f48'() cil managed noinlining
{ .maxstack 2  ldc.i4 48 localloc conv.i4 ldc.i4 15 and ret }
// recurse to vary depth, OR results
.method assembly static int32 'rec'(int32 d, int32 which) cil managed noinlining
{
  .maxstack 3
  .locals (int32 r)
  ldarg.0 brtrue.s DEEP
  ldarg.1 ldc.i4.0 bne.un.s W1
  call int32 'f8'() ret
W1: ldarg.1 ldc.i4.1 bne.un.s W2
  call int32 'f16'() ret
W2: ldarg.1 ldc.i4.2 bne.un.s W3
  call int32 'f32'() ret
W3: call int32 'f48'() ret
DEEP:
  ldarg.0 ldc.i4.1 sub ldarg.1 call int32 'rec'(int32, int32)
  ldarg.0 ldarg.1 call int32 'rec2'(int32, int32) or
  ret
}
.method assembly static int32 'rec2'(int32 d, int32 which) cil managed noinlining
{ .maxstack 3 .locals (int64 pad) ldarg.0 ldc.i4.1 sub dup ldc.i4.0 bge.s OK pop ldc.i4.0 OK: ldarg.1 call int32 'rec'(int32, int32) ret }
.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 4
  .locals init (int32 w, int32 acc, int32 iter)
LW:
  ldc.i4.0 stloc.1
  ldc.i4.0 stloc.2
LI:
  ldloc.1 ldc.i4 6 ldloc.0 call int32 'rec'(int32, int32) or stloc.1
  ldloc.2 ldc.i4.1 add dup stloc.2 ldc.i4 300 blt.s LI
  ldstr "localloc size idx "  call void [mscorlib]System.Console::Write(string)
  ldloc.0 call void [mscorlib]System.Console::Write(int32)
  ldstr " (8,16,32,48): OR low4 = "  call void [mscorlib]System.Console::Write(string)
  ldloc.1 call void [mscorlib]System.Console::WriteLine(int32)
  ldloc.0 ldc.i4.1 add dup stloc.0 ldc.i4.4 blt.s LW
  ldc.i4.0
  ret
}
