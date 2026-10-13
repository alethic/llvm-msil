// e05b: correctly-rounded uint64 -> float64 / float32 without conv.r.un
//   if ((int64)v >= 0) r = (fp)v  else { t = (v >>u 1) | (v & 1); r = (fp)t; r = r + r; }
#include "common.il"
.assembly e05b_u64fp {}
.module e05b_u64fp.dll

.method assembly static float64 'u64_to_f64'(int64 v) cil managed noinlining
{
  .maxstack 3
  ldarg.0
  ldc.i8 0
  blt.s NEG
  ldarg.0
  conv.r8
  ret
NEG:
  ldarg.0
  ldc.i4.1
  shr.un
  ldarg.0
  ldc.i8 1
  and
  or
  conv.r8
  dup
  add
  ret
}
.method assembly static float32 'u64_to_f32'(int64 v) cil managed noinlining
{
  .maxstack 3
  ldarg.0
  ldc.i8 0
  blt.s NEG
  ldarg.0
  conv.r4
  ret
NEG:
  ldarg.0
  ldc.i4.1
  shr.un
  ldarg.0
  ldc.i8 1
  and
  or
  conv.r4
  dup
  add
  conv.r4
  ret
}
.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 4
  ldstr "u64->f64 2^63+2^10+1"  ldc.i8 0x8000000000000401  call float64 'u64_to_f64'(int64)  ldc.r8 9223372036854777856.0  call void 'check.r8'(string, float64, float64)
  ldstr "u64->f64 max"          ldc.i8 -1                  call float64 'u64_to_f64'(int64)  ldc.r8 18446744073709551616.0  call void 'check.r8'(string, float64, float64)
  ldstr "u64->f64 small"        ldc.i8 12345               call float64 'u64_to_f64'(int64)  ldc.r8 12345.0  call void 'check.r8'(string, float64, float64)
  ldstr "u64->f32 2^60+2^36+1"  ldc.i8 0x1000001000000001  call float32 'u64_to_f32'(int64)  conv.r8  ldc.r8 1152921642045800448.0  call void 'check.r8'(string, float64, float64)
  ldstr "u64->f32 2^63+2^39+1"  ldc.i8 0x8000008000000001  call float32 'u64_to_f32'(int64)  conv.r8  ldc.r8 9223373136366403584.0  call void 'check.r8'(string, float64, float64)
  ldstr "u64->f32 max"          ldc.i8 -1                  call float32 'u64_to_f32'(int64)  conv.r8  ldc.r8 18446744073709551616.0  call void 'check.r8'(string, float64, float64)
  call int32 'report'()
  ret
}
