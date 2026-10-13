// 'switch' on a native int: the JIT accepts it, but ILVerify reports
// StackUnexpected (the switch operand must be int32). This is why the jump-table
// index is always int32 (plan D35). Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'switch-native-int' { .ver 0:0:0:0 }

.method public static int32 'select'(native int) cil managed
{
  .maxstack 1
  ldarg.0
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

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'
