// e07: function pointers inside the module: ldftn + calli (managed convention),
// pointer tables in unmanaged memory, pointer identity, and stability across tier-up.
#include "common.il"
.assembly e07_fnptr {}
.module e07_fnptr.dll

.method static int32 'sq'(int32 x) cil managed { .maxstack 2 ldarg.0 ldarg.0 mul ret }
.method static int32 'neg'(int32 x) cil managed { .maxstack 1 ldarg.0 neg ret }
.method static int64 'mix'(int64 a, float64 b, native int c) cil managed
{ .maxstack 2 ldarg.0 ldarg.1 conv.i8 add ldarg.2 conv.i8 add ret }
.method static float64 'half'(float64 x) cil managed { .maxstack 2 ldarg.0 ldc.r8 0.5 mul ret }
.field static int32 'counter'
.method static void 'bump'() cil managed { .maxstack 2 ldsfld int32 'counter' ldc.i4.1 add stsfld int32 'counter' ret }

// int32 apply(native int fp, int32 x) { return ((int(*)(int))fp)(x); }
.method static int32 'apply'(native int fp, int32 x) cil managed noinlining
{
  .maxstack 2
  ldarg.1
  ldarg.0
  calli int32(int32)
  ret
}

// recursive factorial through a function pointer stored in a static field
.field static native int 'fact_ptr'
.method static int64 'fact'(int64 n) cil managed
{
  .maxstack 3
  ldarg.0
  ldc.i8 1
  bgt.s REC
  ldc.i8 1
  ret
REC:
  ldarg.0
  ldarg.0
  ldc.i8 1
  sub
  ldsfld native int 'fact_ptr'
  calli int64(int64)
  mul
  ret
}

.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 8
  .locals init (native int tbl, native int p1, native int p2, int32 i)
  // dispatch table of 2 entries in unmanaged (localloc) memory
  ldc.i4 16
  localloc
  stloc.0
  ldloc.0                ldftn int32 'sq'(int32)    stind.i
  ldloc.0  ldc.i4.8 add  ldftn int32 'neg'(int32)   stind.i
  ldstr "table[0](7) = sq"   ldloc.0  ldind.i  ldc.i4.7  call int32 'apply'(native int, int32)  conv.i8  ldc.i8 49  call void 'check.i8'(string, int64, int64)
  ldstr "table[1](7) = neg"  ldc.i4.7  ldloc.0  ldc.i4.8 add  ldind.i  calli int32(int32)  conv.i8  ldc.i8 -7  call void 'check.i8'(string, int64, int64)
  ldstr "calli int64(int64,float64,native int)"  ldc.i8 40  ldc.r8 1.9  ldc.i4.1 conv.i  ldftn int64 'mix'(int64, float64, native int)  calli int64(int64, float64, native int)  ldc.i8 42  call void 'check.i8'(string, int64, int64)
  ldstr "calli float64(float64)"  ldc.r8 5.0  ldftn float64 'half'(float64)  calli float64(float64)  ldc.r8 2.5  call void 'check.r8'(string, float64, float64)
  ldftn void 'bump'()  calli void()
  ldstr "calli void()"  ldsfld int32 'counter'  conv.i8  ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldftn int64 'fact'(int64)  stsfld native int 'fact_ptr'
  ldstr "recursive calli fact(20)"  ldc.i8 20  call int64 'fact'(int64)  ldc.i8 2432902008176640000  call void 'check.i8'(string, int64, int64)

  // identity: two ldftn of the same method compare equal; different methods differ
  ldftn int32 'sq'(int32)  stloc.1
  ldstr "ldftn same method twice equal"  ldloc.1  ldftn int32 'sq'(int32)  ceq  conv.i8  ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "ldftn different methods differ" ldloc.1  ldftn int32 'neg'(int32)  ceq  conv.i8  ldc.i8 0  call void 'check.i8'(string, int64, int64)
  // hammer 'sq' so tiered compilation promotes it, then compare ldftn again
  ldc.i4.0 stloc.3
HOT:
  ldloc.1  ldloc.3  call int32 'apply'(native int, int32)  pop
  ldc.i4.7  call int32 'sq'(int32)  pop
  ldloc.3  ldc.i4.1  add  dup  stloc.3
  ldc.i4 200000
  blt.s HOT
  ldc.i4 500
  call void [mscorlib]System.Threading.Thread::Sleep(int32)
  ldc.i4.0 stloc.3
HOT2:
  ldc.i4.7  call int32 'sq'(int32)  pop
  ldloc.3  ldc.i4.1  add  dup  stloc.3
  ldc.i4 200000
  blt.s HOT2
  ldftn int32 'sq'(int32)  stloc.2
  ldstr "ldftn stable across tier-up"  ldloc.1  ldloc.2  ceq  conv.i8  ldc.i8 1  call void 'check.i8'(string, int64, int64)
  ldstr "old pointer still callable after tier-up"  ldloc.1  ldc.i4.s 9  call int32 'apply'(native int, int32)  conv.i8  ldc.i8 81  call void 'check.i8'(string, int64, int64)
  call int32 'report'()
  ret
}
