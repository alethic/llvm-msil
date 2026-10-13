// Infinities and NaNs as the backend prints them ('ldc.r4 float32(0x...)',
// 'ldc.r8 float64(0x...)', MSILInstPrinter). The .NET Framework 4.8 ildasm
// prints several of them with the C runtime's text, which ilasm cannot parse,
// so msil-verify.py's neutralized copy must not keep it (the column shows the
// ildasm text; ilasm quiets every float32 signaling NaN on the way in):
//   'pinf64'   0x7FF0000000000000  inf
//   'ninf64'   0xFFF0000000000000  -inf
//   'pinf32'   0x7F800000          inf
//   'ninf32'   0xFF800000          -inf
//   'nnan64'   0xFFF8000000000000  -nan(ind)
//   'snan64'   0x7FF0000000000001  nan(snan)
//   'nsnan64'  0xFFF0000000000001  -nan(snan)
//   'qnan64'   0x7FF8000000000123  (23 01 00 00 00 00 F8 7F), a payload qNaN
//   'maxnan64' 0x7FFFFFFFFFFFFFFF  nan
//   'snan32'   0x7F800001          (01 00 C0 7F), stored as 0x7FC00001
//   'maxsnan32' 0x7FBFFFFF         nan, stored as 0x7FFFFFFF
//   'nsnan32'  0xFFBFFFFF          -nan, stored as 0xFFFFFFFF
//   'nnan32'   0xFFC00000          -nan(ind)
// The byte forms reassemble as they are. The module verifies, force-JITs and
// runs: 'main' returns 42.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'fp-special-constants' { .ver 0:0:0:0 }

.method public static float64 'pinf64'() cil managed
{
  .maxstack 1
  ldc.r8 float64(0x7FF0000000000000)
  ret
} // end of method 'pinf64'

.method public static float64 'ninf64'() cil managed
{
  .maxstack 1
  ldc.r8 float64(0xFFF0000000000000)
  ret
} // end of method 'ninf64'

.method public static float32 'pinf32'() cil managed
{
  .maxstack 1
  ldc.r4 float32(0x7F800000)
  ret
} // end of method 'pinf32'

.method public static float32 'ninf32'() cil managed
{
  .maxstack 1
  ldc.r4 float32(0xFF800000)
  ret
} // end of method 'ninf32'

.method public static float64 'nnan64'() cil managed
{
  .maxstack 1
  ldc.r8 float64(0xFFF8000000000000)
  ret
} // end of method 'nnan64'

.method public static float64 'snan64'() cil managed
{
  .maxstack 1
  ldc.r8 float64(0x7FF0000000000001)
  ret
} // end of method 'snan64'

.method public static float64 'nsnan64'() cil managed
{
  .maxstack 1
  ldc.r8 float64(0xFFF0000000000001)
  ret
} // end of method 'nsnan64'

.method public static float64 'qnan64'() cil managed
{
  .maxstack 1
  ldc.r8 float64(0x7FF8000000000123)
  ret
} // end of method 'qnan64'

.method public static float64 'maxnan64'() cil managed
{
  .maxstack 1
  ldc.r8 float64(0x7FFFFFFFFFFFFFFF)
  ret
} // end of method 'maxnan64'

.method public static float32 'snan32'() cil managed
{
  .maxstack 1
  ldc.r4 float32(0x7F800001)
  ret
} // end of method 'snan32'

.method public static float32 'maxsnan32'() cil managed
{
  .maxstack 1
  ldc.r4 float32(0x7FBFFFFF)
  ret
} // end of method 'maxsnan32'

.method public static float32 'nsnan32'() cil managed
{
  .maxstack 1
  ldc.r4 float32(0xFFBFFFFF)
  ret
} // end of method 'nsnan32'

.method public static float32 'nnan32'() cil managed
{
  .maxstack 1
  ldc.r4 float32(0xFFC00000)
  ret
} // end of method 'nnan32'

.method public static int32 'main'() cil managed
{
  .maxstack 1
  ldc.i4.s 42
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'
