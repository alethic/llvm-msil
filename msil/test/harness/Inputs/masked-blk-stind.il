// Operands of the wrong stack type for cpblk, initblk and stind.i, one
// method each: a cpblk whose destination is an int32 ('conv.i4'), an initblk
// whose address is an int32 or an int64, and a stind.i that stores an int32
// or a float64 (plan D2: addresses and pointer-sized values are native int).
// Both verifiers stop at the instruction itself, which is allowed (cpblk and
// initblk: ILVerify Unverifiable, PEVerify 'Instruction cannot be verified';
// stind.i through a native int: StackByRef, 'Expected ByRef'), so they never
// check its operands. msil-verify.py's neutralized pass replaces each of
// them with a call of a stub that takes native int addresses, an int32 size
// and value, and a native int stored value, so every method here must fail
// there. Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'masked-blk-stind' { .ver 0:0:0:0 }

.method public static void 'cpblk_dst_int32'(native int, native int, int32) cil managed
{
  .maxstack 3
  ldarg.0
  conv.i4
  ldarg.1
  ldarg.2
  cpblk
  ret
} // end of method 'cpblk_dst_int32'

.method public static void 'initblk_addr_int32'(int32, int32, int32) cil managed
{
  .maxstack 3
  ldarg.0
  ldarg.1
  ldarg.2
  initblk
  ret
} // end of method 'initblk_addr_int32'

.method public static void 'initblk_addr_int64'(int64, int32, int32) cil managed
{
  .maxstack 3
  ldarg.0
  ldarg.1
  ldarg.2
  initblk
  ret
} // end of method 'initblk_addr_int64'

.method public static void 'stind_i_int32'(native int, int32) cil managed
{
  .maxstack 2
  ldarg.0
  ldarg.1
  stind.i
  ret
} // end of method 'stind_i_int32'

.method public static void 'stind_i_float64'(native int, float64) cil managed
{
  .maxstack 2
  ldarg.0
  ldarg.1
  stind.i
  ret
} // end of method 'stind_i_float64'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'
