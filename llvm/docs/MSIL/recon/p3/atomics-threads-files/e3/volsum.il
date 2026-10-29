.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly volsum { }
.module volsum.dll
.class public auto ansi abstract sealed V extends [mscorlib]System.Object {
  // sum of volatile (acquire) loads: the value is used
  .method public static int32 vsum(native int p, int32 n) cil managed noinlining {
    .locals init (int32 i, int32 s)
    br.s C
  L: ldloc.1 ldarg.0 volatile. ldind.i4 add stloc.1 ldloc.0 ldc.i4.1 add stloc.0
  C: ldloc.0 ldarg.1 blt.s L
    ldloc.1 ret }
  // seq_cst store as proposed (volatile. stind + MemoryBarrier) followed by a seq_cst load (Dekker shape)
  .method public static int32 dekker(native int x, native int y) cil managed noinlining {
    ldarg.0 ldc.i4.1 volatile. stind.i4 call void [mscorlib]System.Threading.Interlocked::MemoryBarrier()
    ldarg.1 volatile. ldind.i4 ret }
  // byte loads and stores (subword atomics need no CAS for plain loads/stores)
  .method public static int32 bload(native int p) cil managed noinlining { ldarg.0 volatile. ldind.u1 ret }
  .method public static void bstore(native int p, int32 v) cil managed noinlining { ldarg.0 ldarg.1 volatile. stind.i1 ret }
  .method public static native int load64(native int p) cil managed noinlining { ldarg.0 volatile. ldind.i ret }
}
