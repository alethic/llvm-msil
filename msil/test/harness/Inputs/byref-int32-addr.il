// An int32 used as the address of 'ldind.i4'. ILVerify reports StackByRef
// with '[found Int32] Expected ByRef on the stack.', which the D21 allowance
// must not cover: it exists for native int addresses only (plan D2). Not
// run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'byref-int32-addr' { .ver 0:0:0:0 }

.method public static int32 'load'(int32) cil managed
{
  .maxstack 2
  ldarg.0
  ldind.i4
  ret
} // end of method 'load'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'
