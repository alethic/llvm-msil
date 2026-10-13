// native int + int64: the x64 JIT accepts it silently, but it is a stack-type
// error per ECMA-335. PEVerify reports it ('Unexpected type on the stack'), so
// msil-verify.py must fail on Windows. The backend never emits int64 (plan D2);
// this is the mistake the PEVerify leg exists to catch. It still runs: exit 5.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'ptr-int64-mix' { .ver 0:0:0:0 }

.method public static native int 'mix'(native int, int64) cil managed
{
  .maxstack 2
  ldarg.0
  ldarg.1
  add
  ret
} // end of method 'mix'

.method public static int32 'main'() cil managed
{
  .maxstack 2
  ldc.i4.0
  conv.i
  ldc.i8 5
  call native int 'mix'(native int, int64)
  conv.i4
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'
