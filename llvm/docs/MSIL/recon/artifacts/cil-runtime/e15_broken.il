// e15: deliberately malformed method bodies -- which ones does the JIT (via jitall) reject,
// and which does ilverify catch? (ilasm accepts all of them.)
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly e15_broken {}
.module e15_broken.dll
// b1: .maxstack too small
.method assembly static int32 'b1_maxstack'() cil managed { .maxstack 1  ldc.i4.1 ldc.i4.2 add ret }
// b2: stack underflow
.method assembly static int32 'b2_underflow'() cil managed { .maxstack 2  ldc.i4.1 add ret }
// b3: different stack depth at a join point
.method assembly static int32 'b3_join_depth'(int32 c) cil managed
{ .maxstack 2  ldc.i4.1  ldarg.0  brtrue.s J  ldc.i4.2  J: ret }
// b4: different stack types at a join point (int32 vs float64)
.method assembly static int32 'b4_join_type'(int32 c) cil managed
{ .maxstack 2  ldarg.0  brtrue.s A  ldc.r8 1.0  br.s J  A: ldc.i4.1  J: pop  ldc.i4.0  ret }
// b5: int64 stored into an int32 local (silent truncation?)
.method assembly static int32 'b5_i8_to_i4_local'() cil managed
{ .maxstack 1  .locals init (int32 x)  ldc.i8 0x100000005  stloc.0  ldloc.0  ret }
// b6: extra item left on the stack at ret
.method assembly static int32 'b6_extra_at_ret'() cil managed { .maxstack 2  ldc.i4.1 ldc.i4.2 ret }
// b7: float64 stored into an int32 local
.method assembly static int32 'b7_f8_to_i4_local'() cil managed
{ .maxstack 1  .locals init (int32 x)  ldc.r8 1.5  stloc.0  ldloc.0  ret }
// b8: localloc with another value on the stack
.method assembly static int32 'b8_localloc_nonempty'() cil managed
{ .maxstack 2  ldc.i4.7  ldc.i4 16  localloc  pop  ret }
// b9: int32 + float64
.method assembly static int32 'b9_i4_plus_f8'() cil managed { .maxstack 2  ldc.i4.1 ldc.r8 1.0 add conv.i4 ret }
// b10: return int64 from int32 method
.method assembly static int32 'b10_ret_i8_as_i4'() cil managed { .maxstack 1  ldc.i8 0x100000007  ret }
// b11: non-empty stack across a backward branch target
.method assembly static int32 'b11_loop_stack'() cil managed
{ .maxstack 3  ldc.i4.0  L: ldc.i4.1  add  dup  ldc.i4.5  blt.s L  ret }
// b12: missing ret (falls off the end)
.method assembly static void 'b12_fall_off_end'() cil managed { .maxstack 1  nop }
.method public static int32 'main'() cil managed { .entrypoint .maxstack 1 ldc.i4.0 ret }
