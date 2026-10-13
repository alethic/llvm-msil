// Two stack-shape errors ilverify must report: a join with different depths
// and falling off the end of a method.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly badstack { }
.method public static int32 badjoin(int32 c) cil managed {
  .maxstack 2
  ldarg.0
  brtrue L1
  ldc.i4.1
  ldc.i4.2
  br L2
L1:
  ldc.i4.3
L2:
  ret
}
.method public static void falloff(int32 c) cil managed {
  .maxstack 1
  ldarg.0
  brtrue L1
  ret
L1:
  nop
}
.method public static int32 Main(string[] args) cil managed {
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
}
