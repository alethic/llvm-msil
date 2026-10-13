// e14: does ilverify keep checking a method after an ignored (-g) unverifiable-pointer error?
// m1: StackByRef (ldind via native int) first, then a real stack-shape error (int64 stored to int32 local).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly e14_ilv {}
.module e14_ilv.dll
.method assembly static int32 'sq'(int32 x) cil managed { .maxstack 2 ldarg.0 ldarg.0 mul ret }
.method assembly static int32 'm1'(native int p) cil managed
{
  .maxstack 2
  .locals init (int32 r)
  ldarg.0
  ldind.i4          // StackByRef (expected for C code)
  pop
  ldc.i8 1
  stloc.0           // StackUnexpected: int64 stored to an int32 local (a real type-shape bug)
  ldc.i4.0
  ret
}
.method assembly static int32 'm2'(native int p) cil managed
{
  .maxstack 2
  ldc.i4.3
  ldarg.0
  calli int32(int32)   // ImportCalli not implemented
  pop
  ldc.i8 1             // StackUnexpected after the calli: returns int64 from int32 method
  ret
}
.method public static int32 'main'() cil managed { .entrypoint .maxstack 1 ldc.i4.0 ret }
