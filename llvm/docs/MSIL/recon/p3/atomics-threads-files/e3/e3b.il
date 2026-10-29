// E3b: the asref helper (V3) and the op helper (V2) shapes.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly e3b { }
.module e3b.exe
.class public auto ansi abstract sealed E3b extends [mscorlib]System.Object {
  // V3: one tiny helper per type turns an unmanaged pointer into a byref
  .method assembly static int32& 'asref_i4'(void* p) cil managed aggressiveinlining {
    ldarg.0
    ret
  }
  .method assembly static native int& 'asref_i'(void* p) cil managed aggressiveinlining {
    ldarg.0
    ret
  }
  .method public static int32 add_v3(native int p, int32 v) cil managed {
    ldarg.0
    call int32& E3b::'asref_i4'(void*)
    ldarg.1
    call int32 [mscorlib]System.Threading.Interlocked::Add(int32&, int32)
    ret
  }
  .method public static native int cas64_v3(native int p, native int e, native int n) cil managed {
    ldarg.0
    call native int& E3b::'asref_i'(void*)
    ldarg.2
    ldarg.1
    call native int [mscorlib]System.Threading.Interlocked::CompareExchange(native int&, native int, native int)
    ret
  }
  // V2: one helper per operation, called with native int
  .method assembly static int32 'atomic_add_4'(native int p, int32 v) cil managed aggressiveinlining {
    ldarg.0
    ldarg.1
    call int32 [mscorlib]System.Threading.Interlocked::Add(int32&, int32)
    ret
  }
  .method public static int32 add_v2(native int p, int32 v) cil managed {
    ldarg.0
    ldarg.1
    call int32 E3b::'atomic_add_4'(native int, int32)
    ret
  }
  // a type error behind the allowed construct: int64 passed as the value
  .method public static int32 add_v3_bad(native int p, int64 v) cil managed {
    ldarg.0
    call int32& E3b::'asref_i4'(void*)
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
    ldloc.0 ldc.i4.0 stind.i4
    ldloc.0 ldc.i4.5 call int32 E3b::add_v3(native int, int32) pop
    ldloc.0 ldc.i4.6 call int32 E3b::add_v2(native int, int32) pop
    ldloc.0 ldind.i4
    call void [mscorlib]System.Console::WriteLine(int32)
    ldloc.0 ldind.i4
    ret
  }
}
