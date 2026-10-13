// A construct msil-verify.py must reject, a join reached with two values on
// one path and one on the other (ILVerify PathStackDepth), after a managed
// calli in the same method (plan 5.1: 'calli int32(int32)' through an ldftn
// value, M9). ILVerify cannot verify calli ('ImportCalli not implemented')
// and stops there; PEVerify reports 'Instruction cannot be verified' (allowed)
// and stops too. Neither verifier reports the join while the calli is in
// front of it, so msil-verify.py has to look past the calli: its neutralized
// pass rewrites the calli into a call. Never run or force-JIT it: the .NET 10
// JIT was seen growing without bound on this join (cil-runtime recon, e15).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'masked-join-calli' { .ver 0:0:0:0 }

.method public static int32 'id'(int32) cil managed
{
  .maxstack 1
  ldarg.0
  ret
} // end of method 'id'

.method public static int32 'join'(native int, int32) cil managed
{
  .maxstack 2
  ldarg.1
  ldftn int32 'id'(int32)
  calli int32(int32)
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
