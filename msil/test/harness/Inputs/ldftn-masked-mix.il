// native int + int64 (plan D2), which only PEVerify reports, later in a
// method whose first PEVerify error is the use of an ldftn value as a native
// int (stored in a native int local). PEVerify stops at that first error, and
// msil-verify.py accepts PEVerify's errors in a method with ldftn only
// because its neutralized pass (pass 2) verifies the method again with every
// ldftn replaced, which must then find the mix. Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'ldftn-masked-mix' { .ver 0:0:0:0 }

.method public static int32 'twice'(int32) cil managed
{
  .maxstack 2
  ldarg.0
  ldc.i4.2
  mul
  ret
} // end of method 'twice'

.method public static native int 'mix'(native int) cil managed
{
  .maxstack 2
  .locals init (native int)
  ldftn int32 'twice'(int32)
  stloc.0
  ldarg.0
  ldc.i8 1
  add
  ret
} // end of method 'mix'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'
