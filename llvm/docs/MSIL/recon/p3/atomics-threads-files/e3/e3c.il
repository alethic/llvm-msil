// E3c: V3' - the asref helper takes native int, so callers are fully typed.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly e3c { }
.module e3c.exe
.class public auto ansi abstract sealed E3c extends [mscorlib]System.Object {
  .method assembly static int32& 'asref_i4'(native int p) cil managed aggressiveinlining {
    ldarg.0
    ret
  }
  .method public static int32 add_v3n(native int p, int32 v) cil managed {
    ldarg.0
    call int32& E3c::'asref_i4'(native int)
    ldarg.1
    call int32 [mscorlib]System.Threading.Interlocked::Add(int32&, int32)
    ret
  }
  .method public static int32 add_v3n_bad(native int p, int64 v) cil managed {
    ldarg.0
    call int32& E3c::'asref_i4'(native int)
    ldarg.1
    call int32 [mscorlib]System.Threading.Interlocked::Add(int32&, int32)
    ret
  }
  .method public static int32 Main() cil managed {
    .entrypoint
    .locals init (native int p)
    ldc.i4 64
    conv.i
    call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
    stloc.0
    ldloc.0 call int32& E3c::'asref_i4'(native int) ldc.i4.0 call void [mscorlib]System.Threading.Volatile::Write(int32&, int32)
    ldloc.0 ldc.i4.5 call int32 E3c::add_v3n(native int, int32) pop
    ldloc.0 call int32& E3c::'asref_i4'(native int) call int32 [mscorlib]System.Threading.Volatile::Read(int32&)
    ret
  }
}
