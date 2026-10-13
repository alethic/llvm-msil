// The accepted side of byref-*.il: a load and a store through native int
// addresses, the only address type the backend emits (plan D2). ILVerify
// reports StackByRef with '[found Native Int]' and PEVerify '[found Native
// Int] Expected ByRef on the stack.'; both are allowed (plan D21), so this
// module must pass msil-verify.py. Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'byref-native-int' { .ver 0:0:0:0 }

.method public static int32 'load'(native int) cil managed
{
  .maxstack 1
  ldarg.0
  ldind.i4
  ret
} // end of method 'load'

.method public static void 'store'(native int, int32) cil managed
{
  .maxstack 2
  ldarg.0
  ldarg.1
  stind.i4
  ret
} // end of method 'store'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'
