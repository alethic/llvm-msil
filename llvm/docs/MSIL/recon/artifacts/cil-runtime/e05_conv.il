// e05: conversions conv.i1..conv.u8, conv.i/conv.u, conv.r4/r8, conv.r.un, float->int.
#include "common.il"
.assembly e05_conv {}
.module e05_conv.dll

// Helpers that take args so the JIT cannot constant-fold.
.method static int32 'id32'(int32 x) cil managed noinlining { .maxstack 1 ldarg.0 ret }
.method static int64 'id64'(int64 x) cil managed noinlining { .maxstack 1 ldarg.0 ret }
.method static float64 'idf'(float64 x) cil managed noinlining { .maxstack 1 ldarg.0 ret }

// print "OBSERVE <name> = 0x<hex64>"
.method static void 'observe.i8'(string name, int64 v) cil managed
{
  .maxstack 3
  ldstr "OBSERVE "  call void [mscorlib]System.Console::Write(string)
  ldarg.0           call void [mscorlib]System.Console::Write(string)
  ldstr " = 0x"     call void [mscorlib]System.Console::Write(string)
  ldarga.s v  ldstr "X16"  call instance string [mscorlib]System.Int64::ToString(string)
  call void [mscorlib]System.Console::WriteLine(string)
  ret
}

.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 8
  // ---- integer narrowing / widening from int32 0x1234_F680 (low byte 0x80, low half 0xF680)
  ldstr "conv.i1"  ldc.i4 0x1234F680  call int32 'id32'(int32)  conv.i1  conv.i8  ldc.i8 -128      call void 'check.i8'(string, int64, int64)
  ldstr "conv.u1"  ldc.i4 0x1234F680  call int32 'id32'(int32)  conv.u1  conv.i8  ldc.i8 0x80      call void 'check.i8'(string, int64, int64)
  ldstr "conv.i2"  ldc.i4 0x1234F680  call int32 'id32'(int32)  conv.i2  conv.i8  ldc.i8 0xFFFFFFFFFFFFF680  call void 'check.i8'(string, int64, int64)
  ldstr "conv.u2"  ldc.i4 0x1234F680  call int32 'id32'(int32)  conv.u2  conv.i8  ldc.i8 0xF680    call void 'check.i8'(string, int64, int64)
  ldstr "conv.i8 from int32 (sext)"  ldc.i4 -1  call int32 'id32'(int32)  conv.i8  ldc.i8 -1      call void 'check.i8'(string, int64, int64)
  ldstr "conv.u8 from int32 (zext)"  ldc.i4 -1  call int32 'id32'(int32)  conv.u8  ldc.i8 0xFFFFFFFF  call void 'check.i8'(string, int64, int64)
  ldstr "conv.i from int32 (sext)"   ldc.i4 -1  call int32 'id32'(int32)  conv.i   conv.i8  ldc.i8 -1      call void 'check.i8'(string, int64, int64)
  ldstr "conv.u from int32 (zext)"   ldc.i4 -1  call int32 'id32'(int32)  conv.u   conv.u8  ldc.i8 0xFFFFFFFF  call void 'check.i8'(string, int64, int64)
  // ---- from int64
  ldstr "conv.i4 from int64 (trunc)"  ldc.i8 0x1122334480000000  call int64 'id64'(int64)  conv.i4  conv.i8  ldc.i8 0xFFFFFFFF80000000  call void 'check.i8'(string, int64, int64)
  ldstr "conv.u4 from int64 then conv.u8"  ldc.i8 0x1122334480000000  call int64 'id64'(int64)  conv.u4  conv.u8  ldc.i8 0x80000000  call void 'check.i8'(string, int64, int64)
  ldstr "conv.u4 from int64 then conv.i8 (stack int32 => sext)"  ldc.i8 0x1122334480000000  call int64 'id64'(int64)  conv.u4  conv.i8  ldc.i8 0xFFFFFFFF80000000  call void 'check.i8'(string, int64, int64)
  ldstr "conv.i1 from int64"  ldc.i8 0x11223344556677FF  call int64 'id64'(int64)  conv.i1  conv.i8  ldc.i8 -1  call void 'check.i8'(string, int64, int64)
  ldstr "conv.u2 from int64"  ldc.i8 0x112233445566FFFF  call int64 'id64'(int64)  conv.u2  conv.i8  ldc.i8 0xFFFF  call void 'check.i8'(string, int64, int64)
  ldstr "conv.i from int64"   ldc.i8 0x8000000000000001  call int64 'id64'(int64)  conv.i   conv.i8  ldc.i8 0x8000000000000001  call void 'check.i8'(string, int64, int64)

  // ---- int -> float
  ldstr "conv.r8 int32 min"      ldc.i4 0x80000000  call int32 'id32'(int32)  conv.r8  ldc.r8 -2147483648.0  call void 'check.r8'(string, float64, float64)
  ldstr "conv.r.un int32 0x80000000"  ldc.i4 0x80000000  call int32 'id32'(int32)  conv.r.un  conv.r8  ldc.r8 2147483648.0  call void 'check.r8'(string, float64, float64)
  ldstr "conv.r.un int32 -1"     ldc.i4 -1  call int32 'id32'(int32)  conv.r.un  conv.r8  ldc.r8 4294967295.0  call void 'check.r8'(string, float64, float64)
  ldstr "conv.r.un int64 -1"     ldc.i8 -1  call int64 'id64'(int64)  conv.r.un  conv.r8  ldc.r8 18446744073709551616.0  call void 'check.r8'(string, float64, float64)
  ldstr "conv.r8 int64 -1"       ldc.i8 -1  call int64 'id64'(int64)  conv.r8  ldc.r8 -1.0  call void 'check.r8'(string, float64, float64)
  // i64 -> f32 single rounding: v = 2^60 + 2^36 + 1 must round UP to 2^60 + 2^37 (0x5D800001);
  // via double it would double-round to 2^60 (0x5D800000).
  ldstr "conv.r4 int64 (single rounding?)"  ldc.i8 0x1000001000000001  call int64 'id64'(int64)  conv.r4  conv.r8  ldc.r8 1152921642045800448.0  call void 'check.r8'(string, float64, float64)
  ldstr "conv.r.un+conv.r4 uint64 (double rounding?)"  ldc.i8 0x1000001000000001  call int64 'id64'(int64)  conv.r.un  conv.r4  conv.r8  ldc.r8 1152921642045800448.0  call void 'check.r8'(string, float64, float64)
  // unsigned with top bit set: v=2^63+2^39+1 must round UP to 2^63+2^40 (double rounding gives 2^63)
  ldstr "conv.r.un+conv.r4 uint64 top-bit-set"  ldc.i8 0x8000008000000001  call int64 'id64'(int64)  conv.r.un  conv.r4  conv.r8  ldc.r8 9223373136366403584.0  call void 'check.r8'(string, float64, float64)
  // u64->f64: v=2^63+2^10+1 must round UP to 2^63+2^11
  ldstr "conv.r.un uint64 -> f64 rounding"  ldc.i8 0x8000000000000401  call int64 'id64'(int64)  conv.r.un  conv.r8  ldc.r8 9223372036854777856.0  call void 'check.r8'(string, float64, float64)

  // ---- float -> int (in range)
  ldstr "conv.i4 2.9"   ldc.r8 2.9   call float64 'idf'(float64)  conv.i4  conv.i8  ldc.i8 2   call void 'check.i8'(string, int64, int64)
  ldstr "conv.i4 -2.9"  ldc.r8 -2.9  call float64 'idf'(float64)  conv.i4  conv.i8  ldc.i8 -2  call void 'check.i8'(string, int64, int64)
  ldstr "conv.i8 -1.5"  ldc.r8 -1.5  call float64 'idf'(float64)  conv.i8  ldc.i8 -1  call void 'check.i8'(string, int64, int64)
  ldstr "conv.u4 3e9 (valid C uint32)"  ldc.r8 3000000000.0  call float64 'idf'(float64)  conv.u4  conv.u8  ldc.i8 3000000000  call void 'check.i8'(string, int64, int64)
  ldstr "conv.u8 1e19 (valid C uint64)" ldc.r8 10000000000000000000.0  call float64 'idf'(float64)  conv.u8  ldc.i8 0x8AC7230489E80000  call void 'check.i8'(string, int64, int64)
  ldstr "conv.u2 65535.5"  ldc.r8 65535.5  call float64 'idf'(float64)  conv.u2  conv.i8  ldc.i8 65535  call void 'check.i8'(string, int64, int64)
  ldstr "conv.i1 -128.7"   ldc.r8 -128.7   call float64 'idf'(float64)  conv.i1  conv.i8  ldc.i8 -128  call void 'check.i8'(string, int64, int64)
  // float32 source
  ldstr "conv.i4 from float32"  ldc.r4 -7.75  conv.r4  conv.i4  conv.i8  ldc.i8 -7  call void 'check.i8'(string, int64, int64)
  // ---- float <-> float
  ldstr "conv.r4 rounding of 0.1"  ldc.r8 0.1  call float64 'idf'(float64)  conv.r4  conv.r8  ldc.r8 0.10000000149011612  call void 'check.r8'(string, float64, float64)

  // ---- float -> int OUT OF RANGE (UB in C; behaviour differs by runtime version). Observe only.
  ldstr "conv.i4 3e9"       ldc.r8 3000000000.0  call float64 'idf'(float64)  conv.i4  conv.i8  call void 'observe.i8'(string, int64)
  ldstr "conv.i4 NaN"       ldc.r8 (00 00 00 00 00 00 F8 7F)  call float64 'idf'(float64)  conv.i4  conv.i8  call void 'observe.i8'(string, int64)
  ldstr "conv.i8 1e19"      ldc.r8 10000000000000000000.0  call float64 'idf'(float64)  conv.i8  call void 'observe.i8'(string, int64)
  ldstr "conv.u4 -1.0"      ldc.r8 -1.0  call float64 'idf'(float64)  conv.u4  conv.u8  call void 'observe.i8'(string, int64)
  ldstr "conv.u8 -1.0"      ldc.r8 -1.0  call float64 'idf'(float64)  conv.u8  call void 'observe.i8'(string, int64)
  ldstr "conv.u1 300.0"     ldc.r8 300.0  call float64 'idf'(float64)  conv.u1  conv.i8  call void 'observe.i8'(string, int64)
  ldstr "conv.i4 -3e9"      ldc.r8 -3000000000.0  call float64 'idf'(float64)  conv.i4  conv.i8  call void 'observe.i8'(string, int64)
  call int32 'report'()
  ret
}
