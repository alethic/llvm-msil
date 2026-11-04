// The EH-filter shape of plan 5.17.16 (D21's filter part, M27): a setjmp
// wrapper of M27.a, whose whole body is one .try with the fixed filter that
// accepts an ExternalException only when its HResult is the low 32 bits of
// the wrapper's status-block address and the block is armed (read through
// 'ldind.i4' from the native int status argument), plus the throw helper
// that raises it. The wrapper returns a value, so each return of its body
// stores the local R and leaves to the 'ldloc R; ret' tail: M27.a writes
// 'leave' (the long form), and one return here uses 'leave.s', as the
// handler does, so the neutralized copy of msil-verify.py's pass 2 must turn
// both into 'leave'. '<msil>Main' keeps the status block in a D13 frame
// (localloc): a call that returns gives 7, then a call whose callee arms the
// block with 3 and throws is caught by the filter, so the exit status is
// 7 * 10 + 3 = 73.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'eh-filter' { .ver 0:0:0:0 }

// __msil_sjlj_throw(status): plan 5.15.4's throw body.
.method assembly static void '__msil_sjlj_throw'(native int) cil managed noinlining
{
  .maxstack 3
  ldnull
  ldarg.0
  conv.i4
  newobj instance void [mscorlib]System.Runtime.InteropServices.ExternalException::.ctor(string, int32)
  throw
} // end of method '__msil_sjlj_throw'

// step(x, status): 0 returns 7 directly, 1 returns 9 through the 'leave.s'
// return of the wrapper, any other x arms the block with x and throws (a
// longjmp to the wrapper's caller).
.method public static int32 'step'(int32, native int) cil managed noinlining
{
  .maxstack 2
  ldarg.0
  brtrue.s @BB1_1
  ldc.i4.7
  ret
@BB1_1:
  ldarg.0
  ldc.i4.1
  bne.un.s @BB1_2
  ldc.i4.s 9
  ret
@BB1_2:
  ldarg.1
  ldarg.0
  stind.i4
  ldarg.1
  call void '__msil_sjlj_throw'(native int)
  ldc.i4.0
  ret
} // end of method 'step'

// The wrapper of the call 'step'(x, status); its status parameter is
// argument 1 ("msil-sjlj-invoke"="1").
.method assembly static int32 '__msil_sjlj_invoke.0'(int32, native int) cil managed noinlining
{
  .maxstack 3
  .locals (int32)
  .try {
@EH2_0:
  ldarg.0
  ldc.i4.1
  beq @BB2_1
  ldarg.0
  ldarg.1
  call int32 'step'(int32, native int)
  stloc.0
  leave @EH2_6
@BB2_1:
  ldarg.0
  ldarg.1
  call int32 'step'(int32, native int)
  stloc.0
  leave.s @EH2_6
  } filter {
@EH2_1:
  isinst [mscorlib]System.Runtime.InteropServices.ExternalException
  dup
  brtrue.s @EH2_2
  pop
  ldc.i4.0
  br.s @EH2_4
@EH2_2:
  callvirt instance int32 [mscorlib]System.Exception::get_HResult()
  ldarg.1
  conv.i4
  bne.un.s @EH2_3
  ldarg.1
  ldind.i4
  ldc.i4.0
  cgt.un
  br.s @EH2_4
@EH2_3:
  ldc.i4.0
@EH2_4:
  endfilter
  } {
@EH2_5:
  pop
  leave.s @EH2_6
  }
@EH2_6:
  ldloc.0
  ret
} // end of method '__msil_sjlj_invoke.0'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 3
  .locals (native int, int32)
  ldc.i4.s 16
  localloc
  stloc.0
  ldloc.0
  ldc.i4.0
  stind.i4
  ldc.i4.0
  ldloc.0
  call int32 '__msil_sjlj_invoke.0'(int32, native int)
  ldc.i4.s 10
  mul
  stloc.1
  ldc.i4.1
  ldloc.0
  call int32 '__msil_sjlj_invoke.0'(int32, native int)
  ldc.i4.s 9
  bne.un @BB3_1
  ldc.i4.3
  ldloc.0
  call int32 '__msil_sjlj_invoke.0'(int32, native int)
  pop
  ldloc.1
  ldloc.0
  ldind.i4
  add
  ret
@BB3_1:
  ldc.i4.1
  ret
} // end of method '<msil>Main'
