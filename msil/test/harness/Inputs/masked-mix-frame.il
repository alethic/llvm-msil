// A construct msil-verify.py must reject, native int + int64, which only
// PEVerify reports ('[found Native Int][expected Long] Unexpected type on
// the stack.'; plan D2), after an allowed error that comes first in the same
// method: the static frame of plan D13 ('ldc.i4 N; localloc; stloc FP' into
// a native int local of a .locals without init) and a 'br' to the next
// block. Both verifiers stop at the localloc ('Instruction cannot be
// verified', allowed). Neither verifier reports the construct while that
// error is in front of it, so msil-verify.py has to look past the allowed
// error. Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'masked-mix-frame' { .ver 0:0:0:0 }

.method public static native int 'mix'(native int, int64) cil managed
{
  .maxstack 2
  .locals (native int)
  ldc.i4.s 16
  localloc
  stloc.0
  br @BB0_1
@BB0_1:
  ldarg.0
  ldarg.1
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
