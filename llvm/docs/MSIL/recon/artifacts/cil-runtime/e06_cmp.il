// e06: comparisons (ceq/clt/cgt and .un forms), conditional branches (incl. .un),
// float NaN semantics, and the switch opcode.
#include "common.il"
.assembly e06_cmp {}
.module e06_cmp.dll

// ---- compare producers (int32)
.method static int32 'clt.un.i4'(int32 a, int32 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 clt.un ret }
.method static int32 'cgt.un.i4'(int32 a, int32 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 cgt.un ret }
.method static int32 'ule.i4'(int32 a, int32 b)    cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 cgt.un ldc.i4.0 ceq ret }
.method static int32 'ne.i4'(int32 a, int32 b)     cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 ceq ldc.i4.0 ceq ret }
// ---- int64 / native int
.method static int32 'clt.un.i8'(int64 a, int64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 clt.un ret }
.method static int32 'clt.i8'(int64 a, int64 b)    cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 clt ret }
.method static int32 'cgt.un.ni'(native int a, native int b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 cgt.un ret }
// ---- float producers (LLVM fcmp predicate mapping)
.method static int32 'f.oeq'(float64 a, float64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 ceq ret }
.method static int32 'f.une'(float64 a, float64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 ceq ldc.i4.0 ceq ret }
.method static int32 'f.olt'(float64 a, float64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 clt ret }
.method static int32 'f.ult'(float64 a, float64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 clt.un ret }
.method static int32 'f.ogt'(float64 a, float64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 cgt ret }
.method static int32 'f.ugt'(float64 a, float64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 cgt.un ret }
.method static int32 'f.ole'(float64 a, float64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 cgt.un ldc.i4.0 ceq ret }
.method static int32 'f.ule'(float64 a, float64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 cgt ldc.i4.0 ceq ret }
.method static int32 'f.oge'(float64 a, float64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 clt.un ldc.i4.0 ceq ret }
.method static int32 'f.uge'(float64 a, float64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 clt ldc.i4.0 ceq ret }
.method static int32 'f.ord'(float64 a, float64 b) cil managed noinlining { .maxstack 3 ldarg.0 ldarg.0 ceq ldarg.1 ldarg.1 ceq and ret }
.method static int32 'f.uno'(float64 a, float64 b) cil managed noinlining { .maxstack 3 ldarg.0 ldarg.0 ceq ldarg.1 ldarg.1 ceq and ldc.i4.0 ceq ret }
// float32 operands
.method static int32 'f32.ult'(float32 a, float32 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 clt.un ret }

// ---- branch forms: return 1 if taken
.method static int32 'blt.un.i4'(int32 a, int32 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 blt.un.s T ldc.i4.0 ret T: ldc.i4.1 ret }
.method static int32 'bge.un.i4'(int32 a, int32 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 bge.un.s T ldc.i4.0 ret T: ldc.i4.1 ret }
.method static int32 'ble.un.i8'(int64 a, int64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 ble.un.s T ldc.i4.0 ret T: ldc.i4.1 ret }
.method static int32 'bgt.un.ni'(native int a, native int b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 bgt.un.s T ldc.i4.0 ret T: ldc.i4.1 ret }
.method static int32 'bne.un.i4'(int32 a, int32 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 bne.un.s T ldc.i4.0 ret T: ldc.i4.1 ret }
.method static int32 'bne.un.f'(float64 a, float64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 bne.un.s T ldc.i4.0 ret T: ldc.i4.1 ret }
.method static int32 'blt.f'(float64 a, float64 b)    cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 blt.s T ldc.i4.0 ret T: ldc.i4.1 ret }
.method static int32 'blt.un.f'(float64 a, float64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 blt.un.s T ldc.i4.0 ret T: ldc.i4.1 ret }
.method static int32 'bge.f'(float64 a, float64 b)    cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 bge.s T ldc.i4.0 ret T: ldc.i4.1 ret }
.method static int32 'bge.un.f'(float64 a, float64 b) cil managed noinlining { .maxstack 2 ldarg.0 ldarg.1 bge.un.s T ldc.i4.0 ret T: ldc.i4.1 ret }
.method static int32 'brtrue.i8'(int64 a) cil managed noinlining { .maxstack 1 ldarg.0 brtrue.s T ldc.i4.0 ret T: ldc.i4.1 ret }
.method static int32 'brtrue.ni'(native int a) cil managed noinlining { .maxstack 1 ldarg.0 brtrue.s T ldc.i4.0 ret T: ldc.i4.1 ret }

// ---- switch: returns 10+i for i in 0..3, 99 otherwise (switch falls through when index >= N, unsigned)
.method static int32 'sw'(int32 i) cil managed noinlining
{
  .maxstack 1
  ldarg.0
  switch (L0, L1, L2, L3)
  ldc.i4 99
  ret
L0: ldc.i4 10  ret
L1: ldc.i4 11  ret
L2: ldc.i4 12  ret
L3: ldc.i4 13  ret
}
// switch with a duplicated target and a large table, int64 index truncated first
.method static int32 'sw64'(int64 i) cil managed noinlining
{
  .maxstack 2
  ldarg.0
  ldc.i8 100
  sub
  conv.i4
  switch (A, B, A, B)
  ldc.i4 -1
  ret
A: ldc.i4 1  ret
B: ldc.i4 2  ret
}

.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 8
  .locals init (float64 nan)
  ldc.r8 (00 00 00 00 00 00 F8 7F)  stloc.0       // quiet NaN via raw bytes

  ldstr "clt.un -1<1 =0"  ldc.i4.m1 ldc.i4.1  call int32 'clt.un.i4'(int32, int32)  conv.i8 ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "cgt.un -1>1 =1"  ldc.i4.m1 ldc.i4.1  call int32 'cgt.un.i4'(int32, int32)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "ule 1<=-1 =1"    ldc.i4.1 ldc.i4.m1  call int32 'ule.i4'(int32, int32)     conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "ne 3!=3 =0"      ldc.i4.3 ldc.i4.3   call int32 'ne.i4'(int32, int32)      conv.i8 ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "ne 3!=4 =1"      ldc.i4.3 ldc.i4.4   call int32 'ne.i4'(int32, int32)      conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "clt.un.i8 -1<1 =0"  ldc.i8 -1 ldc.i8 1  call int32 'clt.un.i8'(int64, int64)  conv.i8 ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "clt.i8 -1<1 =1"     ldc.i8 -1 ldc.i8 1  call int32 'clt.i8'(int64, int64)     conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "cgt.un.ni -1>1 =1"  ldc.i4.m1 conv.i ldc.i4.1 conv.i  call int32 'cgt.un.ni'(native int, native int)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)

  // floats with NaN (a=NaN, b=1.0)
  ldstr "oeq NaN =0"  ldloc.0 ldc.r8 1.0  call int32 'f.oeq'(float64, float64)  conv.i8 ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "une NaN =1"  ldloc.0 ldc.r8 1.0  call int32 'f.une'(float64, float64)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "olt NaN =0"  ldloc.0 ldc.r8 1.0  call int32 'f.olt'(float64, float64)  conv.i8 ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "ult NaN =1"  ldloc.0 ldc.r8 1.0  call int32 'f.ult'(float64, float64)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "ogt NaN =0"  ldloc.0 ldc.r8 1.0  call int32 'f.ogt'(float64, float64)  conv.i8 ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "ugt NaN =1"  ldloc.0 ldc.r8 1.0  call int32 'f.ugt'(float64, float64)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "ole NaN =0"  ldloc.0 ldc.r8 1.0  call int32 'f.ole'(float64, float64)  conv.i8 ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "ule NaN =1"  ldloc.0 ldc.r8 1.0  call int32 'f.ule'(float64, float64)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "oge NaN =0"  ldloc.0 ldc.r8 1.0  call int32 'f.oge'(float64, float64)  conv.i8 ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "uge NaN =1"  ldloc.0 ldc.r8 1.0  call int32 'f.uge'(float64, float64)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "ord NaN =0"  ldloc.0 ldc.r8 1.0  call int32 'f.ord'(float64, float64)  conv.i8 ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "uno NaN =1"  ldloc.0 ldc.r8 1.0  call int32 'f.uno'(float64, float64)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "f32 ult NaN =1"  ldloc.0 conv.r4 ldc.r4 1.0  call int32 'f32.ult'(float32, float32)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  // floats ordered (2.0 vs 1.0, and equal)
  ldstr "ole 1<=1 =1"  ldc.r8 1.0 ldc.r8 1.0  call int32 'f.ole'(float64, float64)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "oge 1>=2 =0"  ldc.r8 1.0 ldc.r8 2.0  call int32 'f.oge'(float64, float64)  conv.i8 ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "oeq -0==0 =1" ldc.r8 -0.0 ldc.r8 0.0  call int32 'f.oeq'(float64, float64)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)

  // branches
  ldstr "blt.un -1<1 not taken"  ldc.i4.m1 ldc.i4.1  call int32 'blt.un.i4'(int32, int32)  conv.i8 ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "bge.un -1>=1 taken"     ldc.i4.m1 ldc.i4.1  call int32 'bge.un.i4'(int32, int32)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "ble.un.i8 1<=-1 taken"  ldc.i8 1 ldc.i8 -1  call int32 'ble.un.i8'(int64, int64)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "bgt.un.ni -1>1 taken"   ldc.i4.m1 conv.i ldc.i4.1 conv.i  call int32 'bgt.un.ni'(native int, native int)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "bne.un.i4 1!=2 taken"   ldc.i4.1 ldc.i4.2  call int32 'bne.un.i4'(int32, int32)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "bne.un.f NaN taken"     ldloc.0 ldloc.0  call int32 'bne.un.f'(float64, float64)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "blt.f NaN not taken"    ldloc.0 ldc.r8 1.0  call int32 'blt.f'(float64, float64)  conv.i8 ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "blt.un.f NaN taken"     ldloc.0 ldc.r8 1.0  call int32 'blt.un.f'(float64, float64)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "bge.f NaN not taken"    ldloc.0 ldc.r8 1.0  call int32 'bge.f'(float64, float64)  conv.i8 ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "bge.un.f NaN taken"     ldloc.0 ldc.r8 1.0  call int32 'bge.un.f'(float64, float64)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "brtrue.i8 high-bits-only taken"  ldc.i8 0x100000000  call int32 'brtrue.i8'(int64)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "brtrue.ni 0 not taken"  ldc.i4.0 conv.i  call int32 'brtrue.ni'(native int)  conv.i8 ldc.i8 0  call void 'check.i8'(string, int64, int64)

  // switch
  ldstr "switch 0"   ldc.i4.0  call int32 'sw'(int32)  conv.i8 ldc.i8 10  call void 'check.i8'(string, int64, int64)
  ldstr "switch 3"   ldc.i4.3  call int32 'sw'(int32)  conv.i8 ldc.i8 13  call void 'check.i8'(string, int64, int64)
  ldstr "switch 4 (fallthrough)"   ldc.i4.4  call int32 'sw'(int32)  conv.i8 ldc.i8 99  call void 'check.i8'(string, int64, int64)
  ldstr "switch -1 (unsigned => fallthrough)"  ldc.i4.m1  call int32 'sw'(int32)  conv.i8 ldc.i8 99  call void 'check.i8'(string, int64, int64)
  ldstr "switch64 102"  ldc.i8 102  call int32 'sw64'(int64)  conv.i8 ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "switch64 103"  ldc.i8 103  call int32 'sw64'(int64)  conv.i8 ldc.i8 2  call void 'check.i8'(string, int64, int64)
  ldstr "switch64 99"   ldc.i8 99   call int32 'sw64'(int64)  conv.i8 ldc.i8 -1 call void 'check.i8'(string, int64, int64)
  call int32 'report'()
  ret
}
