// A construct msil-verify.py must reject, a join reached with two values on
// one path and one on the other (ILVerify PathStackDepth), after an allowed
// error that comes first in the same method: a load through a native int
// argument ('ldarg.0; ldind.i4; pop'). Both verifiers stop at '[found Native
// Int] Expected ByRef on the stack' (ILVerify StackByRef, allowed). Neither
// verifier reports the construct while that error is in front of it, so
// msil-verify.py has to look past the allowed error. Never run or force-JIT
// it: the .NET 10 JIT was seen growing without bound on this join
// (cil-runtime recon, e15).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'masked-join-ldind' { .ver 0:0:0:0 }

.method public static int32 'join'(native int, int32) cil managed
{
  .maxstack 2
  ldarg.0
  ldind.i4
  pop
  ldarg.1
  brtrue @BB0_3
  ldc.i4.1
  ldc.i4.2
  br @BB0_4
@BB0_3:
  ldc.i4.3
@BB0_4:
  ret
} // end of method 'join'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'
