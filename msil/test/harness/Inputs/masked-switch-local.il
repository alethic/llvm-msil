// A construct msil-verify.py must reject, 'switch' on a native int (ILVerify
// StackUnexpected; plan D35), after an allowed error that comes first in the
// same method: a read of a local of a .locals without init (plan D13: never
// 'init') and a 'br' to the next block. Both verifiers stop at 'initlocals
// must be set' (ILVerify InitLocals, allowed). Neither verifier reports the
// construct while that error is in front of it, so msil-verify.py has to
// look past the allowed error. Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'masked-switch-local' { .ver 0:0:0:0 }

.method public static int32 'select'(native int) cil managed
{
  .maxstack 2
  .locals (int32)
  ldloc.0
  pop
  br @BB0_1
@BB0_1:
  ldarg.0
  switch (@BB0_3, @BB0_4)
  br @BB0_5
@BB0_3:
  ldc.i4.s 10
  ret
@BB0_4:
  ldc.i4.s 11
  ret
@BB0_5:
  ldc.i4.s 99
  ret
} // end of method 'select'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'
