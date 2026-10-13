// An ldftn value used where an int32 is expected: a function address is a
// native int (plan D2), so passing it for an int32 parameter is an error that
// msil-verify.py must reject, although PEVerify reports every use of an
// ldftn value and msil-verify.py accepts the uses of a correct module
// (ldftn-native-int.il). Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'ldftn-wrong-type' { .ver 0:0:0:0 }

.method public static int32 'twice'(int32) cil managed
{
  .maxstack 2
  ldarg.0
  ldc.i4.2
  mul
  ret
} // end of method 'twice'

.method public static int32 'wrong'() cil managed
{
  .maxstack 1
  ldftn int32 'twice'(int32)
  call int32 'twice'(int32)
  ret
} // end of method 'wrong'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'
