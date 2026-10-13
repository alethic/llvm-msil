// The same body as switch-native-int.il with 'conv.i4' before 'switch', the
// shape the backend prints after LLVM's range check (plan D35). It passes
// msil-verify.py and runs: 'select'(1) takes the second case, exit 11.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'switch-int32' { .ver 0:0:0:0 }

.method public static int32 'select'(native int) cil managed
{
  .maxstack 1
  ldarg.0
  conv.i4
  switch (@BB0_1, @BB0_2)
  br @BB0_3
@BB0_1:
  ldc.i4.s 10
  ret
@BB0_2:
  ldc.i4.s 11
  ret
@BB0_3:
  ldc.i4.s 99
  ret
} // end of method 'select'

.method public static int32 'main'() cil managed
{
  .maxstack 1
  ldc.i4.1
  conv.i
  call int32 'select'(native int)
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'
