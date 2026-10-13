// A join with different stack depths: one path reaches @BB0_3 with two values
// on the stack, the other with one. ILVerify reports PathStackDepth, which
// msil-verify.py must treat as fatal.
//
// Never run or jitall this file: the .NET 10 JIT was seen growing without
// bound on exactly this shape (cil-runtime recon, e15).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'bad-stack-join' { .ver 0:0:0:0 }

.method public static int32 'join'(int32) cil managed
{
  .maxstack 2
  ldarg.0
  brtrue @BB0_2
  ldc.i4.1
  ldc.i4.2
  br @BB0_3
@BB0_2:
  ldc.i4.3
@BB0_3:
  ret
} // end of method 'join'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'
