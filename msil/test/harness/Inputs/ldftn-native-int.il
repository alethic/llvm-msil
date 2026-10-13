// A function address used as a native int (plan D2, D18; M8.a prints
// FUNC_ADDR as 'ldftn <member-ref>'), once per kind of use, each in a method
// of its own so that PEVerify, which stops at the first error of a method,
// reports every one of them: a call argument ('use_arg'), a native int local
// ('use_local'), a return value ('use_ret'), a compare with NULL in both
// operand orders ('cmp_ceq', 'cmp_ceq_rev') and as a branch in both orders
// ('cmp_beq', 'cmp_beq_rev'), arithmetic ('use_add') and a calli
// ('use_calli'). PEVerify types an ldftn value as a method pointer and rejects
// these uses; ILVerify and the JIT accept them, and msil-verify.py must too
// (plan section 12, 'D21 and M0.b item (7) refined', part 2). The program
// runs: 'main' returns 10 + 6 + 2 + 0 + 0 + 1 + 1 + 4 + 16 = 40.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'ldftn-native-int' { .ver 0:0:0:0 }

.method public static int32 'twice'(int32) cil managed
{
  .maxstack 2
  ldarg.0
  ldc.i4.2
  mul
  ret
} // end of method 'twice'

// apply(f, x) = f(x), a callee that takes a function pointer.
.method public static int32 'apply'(native int, int32) cil managed
{
  .maxstack 2
  ldarg.1
  ldarg.0
  calli int32(int32)
  ret
} // end of method 'apply'

.method public static int32 'use_arg'(int32) cil managed
{
  .maxstack 2
  ldftn int32 'twice'(int32)
  ldarg.0
  call int32 'apply'(native int, int32)
  ret
} // end of method 'use_arg'

.method public static native int 'use_local'() cil managed
{
  .maxstack 1
  .locals init (native int)
  ldftn int32 'twice'(int32)
  stloc.0
  ldloc.0
  ret
} // end of method 'use_local'

.method public static native int 'use_ret'() cil managed
{
  .maxstack 1
  ldftn int32 'twice'(int32)
  ret
} // end of method 'use_ret'

.method public static int32 'cmp_ceq'() cil managed
{
  .maxstack 2
  ldftn int32 'twice'(int32)
  ldc.i4.0
  conv.i
  ceq
  ret
} // end of method 'cmp_ceq'

.method public static int32 'cmp_ceq_rev'() cil managed
{
  .maxstack 2
  ldc.i4.0
  conv.i
  ldftn int32 'twice'(int32)
  ceq
  ret
} // end of method 'cmp_ceq_rev'

.method public static int32 'cmp_beq'() cil managed
{
  .maxstack 2
  ldftn int32 'twice'(int32)
  ldc.i4.0
  conv.i
  beq @BB0_2
  ldc.i4.1
  ret
@BB0_2:
  ldc.i4.0
  ret
} // end of method 'cmp_beq'

.method public static int32 'cmp_beq_rev'() cil managed
{
  .maxstack 2
  ldc.i4.0
  conv.i
  ldftn int32 'twice'(int32)
  beq @BB0_2
  ldc.i4.1
  ret
@BB0_2:
  ldc.i4.0
  ret
} // end of method 'cmp_beq_rev'

.method public static native int 'use_add'() cil managed
{
  .maxstack 2
  ldftn int32 'twice'(int32)
  ldc.i4.0
  conv.i
  add
  ret
} // end of method 'use_add'

.method public static int32 'use_calli'(int32) cil managed
{
  .maxstack 2
  ldarg.0
  ldftn int32 'twice'(int32)
  calli int32(int32)
  ret
} // end of method 'use_calli'

.method public static int32 'main'() cil managed
{
  .maxstack 3
  ldc.i4.5
  call int32 'use_arg'(int32)
  call native int 'use_local'()
  ldc.i4.3
  call int32 'apply'(native int, int32)
  add
  call native int 'use_ret'()
  ldc.i4.1
  call int32 'apply'(native int, int32)
  add
  call int32 'cmp_ceq'()
  add
  call int32 'cmp_ceq_rev'()
  add
  call int32 'cmp_beq'()
  add
  call int32 'cmp_beq_rev'()
  add
  call native int 'use_add'()
  ldc.i4.2
  call int32 'apply'(native int, int32)
  add
  ldc.i4.8
  call int32 'use_calli'(int32)
  add
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'
