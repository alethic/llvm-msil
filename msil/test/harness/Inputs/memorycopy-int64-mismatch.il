// The other side of the plan D21 exception: the last length argument of
// Buffer::MemoryCopy(void*, void*, int64, int64) is a native int, not an
// int64. PEVerify reports '[found Native Int][expected Long] Unexpected type on
// the stack.', which msil-verify.py must still treat as fatal. Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'memorycopy-int64-mismatch' { .ver 0:0:0:0 }

.method public static void 'move'(native int, native int, native int) cil managed
{
  .maxstack 4
  ldarg.1
  ldarg.0
  ldarg.2
  conv.i8
  ldarg.2
  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64)
  ret
} // end of method 'move'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'
