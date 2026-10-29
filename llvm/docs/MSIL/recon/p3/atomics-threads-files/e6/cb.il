.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern System { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly cb { }
.module cb.exe
.class public auto ansi abstract sealed B extends [mscorlib]System.Object {
  .method assembly static void '<msil>compiler_barrier'() cil managed noinlining { ret }
  .method public static int64 'b_none'(native int p, int32 n) cil managed noinlining {
    .locals init (int32 i, int64 t0)
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() stloc.1
    ldc.i4.0 stloc.0 br.s C
  L: ldarg.0 ldarg.0 ldind.i4 ldc.i4.1 add stind.i4
    ldloc.0 ldc.i4.1 add stloc.0
  C: ldloc.0 ldarg.1 blt.s L
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() ldloc.1 sub ret }
  .method public static int64 'b_noinl_call'(native int p, int32 n) cil managed noinlining {
    .locals init (int32 i, int64 t0)
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() stloc.1
    ldc.i4.0 stloc.0 br.s C
  L: ldarg.0 ldarg.0 ldind.i4 ldc.i4.1 add stind.i4 call void B::'<msil>compiler_barrier'()
    ldloc.0 ldc.i4.1 add stloc.0
  C: ldloc.0 ldarg.1 blt.s L
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() ldloc.1 sub ret }
  .method public static int64 'b_membarrier'(native int p, int32 n) cil managed noinlining {
    .locals init (int32 i, int64 t0)
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() stloc.1
    ldc.i4.0 stloc.0 br.s C
  L: ldarg.0 ldarg.0 ldind.i4 ldc.i4.1 add stind.i4 call void [mscorlib]System.Threading.Interlocked::MemoryBarrier()
    ldloc.0 ldc.i4.1 add stloc.0
  C: ldloc.0 ldarg.1 blt.s L
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() ldloc.1 sub ret }
  .method public static int32 Main() cil managed {
    .entrypoint
    .locals init (native int p, int32 n, int32 r)
    ldc.i4 64 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int) stloc.0
    ldc.i4 100000000 stloc.1
  R:
    ldstr "none " call void [mscorlib]System.Console::Write(string)
    ldloc.0 ldloc.1 call int64 B::'b_none'(native int, int32) conv.r8 ldc.r8 1e9 mul ldsfld int64 [System]System.Diagnostics.Stopwatch::Frequency conv.r8 div ldloc.1 conv.r8 div call void [mscorlib]System.Console::WriteLine(float64)
    ldstr "noinl_call " call void [mscorlib]System.Console::Write(string)
    ldloc.0 ldloc.1 call int64 B::'b_noinl_call'(native int, int32) conv.r8 ldc.r8 1e9 mul ldsfld int64 [System]System.Diagnostics.Stopwatch::Frequency conv.r8 div ldloc.1 conv.r8 div call void [mscorlib]System.Console::WriteLine(float64)
    ldstr "membarrier " call void [mscorlib]System.Console::Write(string)
    ldloc.0 ldloc.1 call int64 B::'b_membarrier'(native int, int32) conv.r8 ldc.r8 1e9 mul ldsfld int64 [System]System.Diagnostics.Stopwatch::Frequency conv.r8 div ldloc.1 conv.r8 div call void [mscorlib]System.Console::WriteLine(float64)
    ldloc.2 ldc.i4.1 add dup stloc.2 ldc.i4.2 blt R
    ldc.i4.0 ret }
}