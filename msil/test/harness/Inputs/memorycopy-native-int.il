// The MEMMOVE sequence of plan D15: the addresses are native int and the length
// is widened with 'conv.i8' and duplicated for both int64 parameters of
// Buffer::MemoryCopy(void*, void*, int64, int64). PEVerify reports the native
// int addresses as '[found Native Int][expected unmanaged pointer] Unexpected
// type on the stack.', the one 'Unexpected type' message msil-verify.py allows
// (plan D21), so this module must pass.
//
// 'main' moves bytes 1 2 3 4 one byte up inside 1 2 3 4 5: memmove gives
// 1 1 2 3 4, so byte 4 is 4 (a forward byte-by-byte copy would give 1).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'memorycopy-native-int' { .ver 0:0:0:0 }

// memmove(dst = arg 0, src = arg 1, len = arg 2)
.method public static void 'move'(native int, native int, native int) cil managed
{
  .maxstack 4
  ldarg.1
  ldarg.0
  ldarg.2
  conv.i8
  dup
  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64)
  ret
} // end of method 'move'

.method public static int32 'main'() cil managed
{
  .maxstack 3
  .locals (native int)
  ldc.i4.8
  localloc
  stloc.0
  ldloc.0
  ldc.i4 0x04030201
  stind.i4
  ldloc.0
  ldc.i4.4
  add
  ldc.i4.5
  stind.i4
  ldloc.0
  ldc.i4.1
  add
  ldloc.0
  ldc.i4.4
  conv.i
  call void 'move'(native int, native int, native int)
  ldloc.0
  ldc.i4.4
  add
  ldind.u1
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'
