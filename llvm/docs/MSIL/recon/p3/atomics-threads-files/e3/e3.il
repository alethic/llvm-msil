// E3: managed refs from unmanaged pointers for Interlocked and Volatile.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly e3 { }
.module e3.exe
.class public auto ansi abstract sealed E3 extends [mscorlib]System.Object {
  // (a) native int passed where int32& is expected
  .method public static int32 add_nint(native int p, int32 v) cil managed {
    ldarg.0
    ldarg.1
    call int32 [mscorlib]System.Threading.Interlocked::Add(int32&, int32)
    ret
  }
  .method public static int32 cas_nint(native int p, int32 e, int32 n) cil managed {
    ldarg.0
    ldarg.2
    ldarg.1
    call int32 [mscorlib]System.Threading.Interlocked::CompareExchange(int32&, int32, int32)
    ret
  }
  .method public static int32 vread_nint(native int p) cil managed {
    ldarg.0
    call int32 [mscorlib]System.Threading.Volatile::Read(int32&)
    ret
  }
  .method public static void vwrite_nint(native int p, int32 v) cil managed {
    ldarg.0
    ldarg.1
    call void [mscorlib]System.Threading.Volatile::Write(int32&, int32)
    ret
  }
  // (b) i64 on a 64-bit triple: native int values (plan D2)
  .method public static native int cas64_nint(native int p, native int e, native int n) cil managed {
    ldarg.0
    ldarg.2
    ldarg.1
    call native int [mscorlib]System.Threading.Interlocked::CompareExchange(native int&, native int, native int)
    ret
  }
  .method public static native int add64_conv(native int p, native int v) cil managed {
    ldarg.0
    ldarg.1
    conv.i8
    call int64 [mscorlib]System.Threading.Interlocked::Add(int64&, int64)
    conv.i
    ret
  }
  .method public static native int add64_mixed(native int p, native int v) cil managed {
    ldarg.0
    ldarg.1
    call int64 [mscorlib]System.Threading.Interlocked::Add(int64&, int64)
    ret
  }
  .method public static native int vread64_nint(native int p) cil managed {
    ldarg.0
    call native int [mscorlib]System.Threading.Volatile::Read(native int&)
    ret
  }
  // (c) an unmanaged pointer type (int32*) where int32& is expected
  .method public static int32 add_ptr(int32* p, int32 v) cil managed {
    ldarg.0
    ldarg.1
    call int32 [mscorlib]System.Threading.Interlocked::Add(int32&, int32)
    ret
  }
  // (d) through a byref local
  .method public static int32 add_byref_local(native int p, int32 v) cil managed {
    .locals init (int32& r)
    ldarg.0
    stloc.0
    ldloc.0
    ldarg.1
    call int32 [mscorlib]System.Threading.Interlocked::Add(int32&, int32)
    ret
  }
  // (e) today's shape: volatile. ldind/stind through a native int
  .method public static int32 vol_ldind(native int p) cil managed {
    ldarg.0
    volatile.
    ldind.i4
    ret
  }
  .method public static void vol_stind(native int p, int32 v) cil managed {
    ldarg.0
    ldarg.1
    volatile.
    stind.i4
    ret
  }
  .method public static int32 Main() cil managed {
    .entrypoint
    .locals init (native int p, int32 bad)
    ldc.i4 64
    conv.i
    call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
    stloc.0
    ldloc.0  ldc.i4.0  stind.i4
    ldloc.0  ldc.i4.8  conv.i  add  ldc.i4.0  conv.i  stind.i
    // add_nint(p, 5) -> 0, then *p == 5
    ldloc.0 ldc.i4.5 call int32 E3::add_nint(native int, int32)
    ldc.i4.5 beq OK1
    ldloc.1 ldc.i4.1 or stloc.1
OK1:
    ldloc.0 ldc.i4.5 ldc.i4.7 call int32 E3::cas_nint(native int, int32, int32) pop
    ldloc.0 call int32 E3::vread_nint(native int) ldc.i4.7 beq OK2
    ldloc.1 ldc.i4.2 or stloc.1
OK2:
    ldloc.0 ldc.i4.s 11 call void E3::vwrite_nint(native int, int32)
    ldloc.0 ldind.i4 ldc.i4.s 11 beq OK3
    ldloc.1 ldc.i4.4 or stloc.1
OK3:
    ldloc.0 ldc.i4.8 conv.i add  ldc.i4.0 conv.i  ldc.i4 0x10000 conv.i call native int E3::cas64_nint(native int, native int, native int) pop
    ldloc.0 ldc.i4.8 conv.i add  ldc.i4 0x10000 conv.i call native int E3::add64_conv(native int, native int) pop
    ldloc.0 ldc.i4.8 conv.i add  ldc.i4.1 conv.i call native int E3::add64_mixed(native int, native int) pop
    ldloc.0 ldc.i4.8 conv.i add call native int E3::vread64_nint(native int) ldc.i4 0x20001 conv.i beq OK4
    ldloc.1 ldc.i4.8 or stloc.1
OK4:
    ldloc.0 ldc.i4.1 call int32 E3::add_ptr(int32*, int32) pop
    ldloc.0 ldc.i4.1 call int32 E3::add_byref_local(native int, int32) pop
    ldloc.0 call int32 E3::vol_ldind(native int) ldc.i4.s 13 beq OK5
    ldloc.1 ldc.i4.s 16 or stloc.1
OK5:
    ldloc.0 ldc.i4.0 call void E3::vol_stind(native int, int32)
    ldstr "e3 bad="
    ldloca.s 1
    call instance string [mscorlib]System.Int32::ToString()
    call string [mscorlib]System.String::Concat(string, string)
    call void [mscorlib]System.Console::WriteLine(string)
    ldloc.1
    ret
  }
}
