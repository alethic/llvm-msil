.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly fadd { }
.module fadd.exe
.class public auto ansi abstract sealed F extends [mscorlib]System.Object {
  // atomicrmw add returning the old value: Interlocked.Add returns the new one
  .method public static int32 fetch_add(native int p, int32 v) cil managed noinlining {
    ldarg.0 ldarg.1 call int32 [mscorlib]System.Threading.Interlocked::Add(int32&, int32) ldarg.1 sub ret }
  .method public static native int fetch_add64(native int p, native int v) cil managed noinlining {
    ldarg.0 ldarg.1 conv.i8 call int64 [mscorlib]System.Threading.Interlocked::Add(int64&, int64) ldarg.1 conv.i8 sub conv.i ret }
  .method public static int32 cas(native int p, int32 e, int32 n) cil managed noinlining {
    ldarg.0 ldarg.2 ldarg.1 call int32 [mscorlib]System.Threading.Interlocked::CompareExchange(int32&, int32, int32) ldarg.1 ceq ret }
  .method public static int32 Main() cil managed {
    .entrypoint
    .locals init (native int p)
    ldc.i4 64 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int) stloc.0
    ldloc.0 ldc.i4.0 stind.i4
    ldloc.0 ldc.i4.5 call int32 F::fetch_add(native int, int32) pop
    ldloc.0 ldc.i4.5 conv.i call native int F::fetch_add64(native int, native int) pop
    ldloc.0 ldc.i4.0 ldc.i4.1 call int32 F::cas(native int, int32, int32) ret }
}
