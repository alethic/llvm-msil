.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly sw {}
.method public static int32 'sel'(native int) cil managed noinlining
{
  .maxstack 1
  ldarg.0
  switch (L0, L1)
  br LD
L0: ldc.i4 10 ret
L1: ldc.i4 11 ret
LD: ldc.i4 99 ret
}
.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 2
  ldc.i8 0x100000001 conv.i call int32 'sel'(native int) ret
}
