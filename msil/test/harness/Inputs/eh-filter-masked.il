// eh-filter.il's wrapper shape (plan 5.17.16, M27) with native int + int64
// inside the protected block: the body first loads through the native int
// status argument ('ldind.i4', an allowed first error of plan D21, at which
// PEVerify stops for the method), then adds the int64 argument to the
// native int address without 'conv.i'. Only the neutralized pass of
// msil-verify.py (pass 2), which keeps the clause and replaces the load,
// can see the mix, so msil-verify.py must fail from pass 2 and pass without
// it. Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'eh-filter-masked' { .ver 0:0:0:0 }

.method public static int32 'step'(native int) cil managed noinlining
{
  .maxstack 1
  ldc.i4.7
  ret
} // end of method 'step'

// The wrapper of a call 'step'(p) with p = status + off; its status
// parameter is argument 0 ("msil-sjlj-invoke"="0").
.method assembly static int32 '__msil_sjlj_invoke.0'(native int, int64) cil managed noinlining
{
  .maxstack 3
  .locals (int32)
  .try {
@EH1_0:
  ldarg.0
  ldind.i4
  pop
  ldarg.0
  ldarg.1
  add
  call int32 'step'(native int)
  stloc.0
  leave @EH1_6
  } filter {
@EH1_1:
  isinst [mscorlib]System.Runtime.InteropServices.ExternalException
  dup
  brtrue.s @EH1_2
  pop
  ldc.i4.0
  br.s @EH1_4
@EH1_2:
  callvirt instance int32 [mscorlib]System.Exception::get_HResult()
  ldarg.0
  conv.i4
  bne.un.s @EH1_3
  ldarg.0
  ldind.i4
  ldc.i4.0
  cgt.un
  br.s @EH1_4
@EH1_3:
  ldc.i4.0
@EH1_4:
  endfilter
  } {
@EH1_5:
  pop
  leave.s @EH1_6
  }
@EH1_6:
  ldloc.0
  ret
} // end of method '__msil_sjlj_invoke.0'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'
