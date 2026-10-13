// A construct msil-verify.py must reject: a managed calli whose argument has
// the wrong stack type, an int64 passed for the int32 parameter of
// 'calli int32(int32)' (plan D2: int64 is never emitted). ILVerify cannot
// verify calli ('ImportCalli not implemented') and PEVerify only says
// 'Instruction cannot be verified' (allowed), so neither checks the
// arguments; msil-verify.py's neutralized pass rewrites the calli into a call
// with the same signature, which they do check. Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'calli-wrong-arg' { .ver 0:0:0:0 }

.method public static int32 'id'(int32) cil managed
{
  .maxstack 1
  ldarg.0
  ret
} // end of method 'id'

.method public static int32 'wrongarg'() cil managed
{
  .maxstack 2
  ldc.i8 7
  ldftn int32 'id'(int32)
  calli int32(int32)
  ret
} // end of method 'wrongarg'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'
