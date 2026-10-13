// An int64 ('conv.i8' of a native int) used as the address of 'ldind.i4'.
// ILVerify reports StackByRef with '[found Long] Expected ByRef on the
// stack.', which the D21 allowance must not cover: addresses are native int,
// never int64 (plan D2). Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'byref-int64-addr' { .ver 0:0:0:0 }

.method public static int32 'load'(native int) cil managed
{
  .maxstack 2
  ldarg.0
  conv.i8
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
