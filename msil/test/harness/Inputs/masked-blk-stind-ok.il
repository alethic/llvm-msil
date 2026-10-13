// The control for masked-blk-stind.il: cpblk, initblk and stind.i with
// operands of the right stack types (native int addresses, int32 sizes and
// fill value, a native int stored value). msil-verify.py must accept it, and
// it runs: 'main' fills 8 bytes with 7, copies them 8 bytes further, stores a
// pointer over the first 8 and returns the last byte, 7.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'masked-blk-stind-ok' { .ver 0:0:0:0 }

.method public static void 'copy'(native int, native int, int32) cil managed
{
  .maxstack 3
  ldarg.0
  ldarg.1
  ldarg.2
  cpblk
  ret
} // end of method 'copy'

.method public static void 'fill'(native int, int32, int32) cil managed
{
  .maxstack 3
  ldarg.0
  ldarg.1
  ldarg.2
  initblk
  ret
} // end of method 'fill'

.method public static void 'store'(native int, native int) cil managed
{
  .maxstack 2
  ldarg.0
  ldarg.1
  stind.i
  ret
} // end of method 'store'

.method public static int32 'main'() cil managed
{
  .maxstack 3
  .locals init (native int)
  ldc.i4.s 16
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  stloc.0
  ldloc.0
  ldc.i4.7
  ldc.i4.8
  call void 'fill'(native int, int32, int32)
  ldloc.0
  ldc.i4.8
  conv.i
  add
  ldloc.0
  ldc.i4.8
  call void 'copy'(native int, native int, int32)
  ldloc.0
  ldloc.0
  call void 'store'(native int, native int)
  ldloc.0
  ldc.i4.s 15
  conv.i
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
