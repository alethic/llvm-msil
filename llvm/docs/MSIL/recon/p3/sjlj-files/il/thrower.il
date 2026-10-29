// Assembly B: throws the longjmp object. No type is shared with assembly A except LjSub (perf baseline only).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly thrower { .ver 1:0:0:0 }
.module thrower.dll
.class public auto ansi beforefieldinit LjSub extends [mscorlib]System.Exception {
  .field public native int buf
  .method public hidebysig specialname rtspecialname instance void .ctor(native int b) cil managed {
    ldarg.0
    call instance void [mscorlib]System.Exception::.ctor()
    ldarg.0
    ldarg.1
    stfld native int LjSub::buf
    ret
  }
}
.class public abstract sealed auto ansi T extends [mscorlib]System.Object {
  .field public static class LjSub cached
  .method public static void ThrowBoxed(native int b) cil managed noinlining {
    ldarg.0
    box [mscorlib]System.IntPtr
    throw
  }
  .method public static void ThrowSub(native int b) cil managed noinlining {
    ldarg.0
    newobj instance void LjSub::.ctor(native int)
    throw
  }
  .method public static void ThrowExt(native int b) cil managed noinlining {
    ldstr "longjmp"
    ldarg.0
    conv.i4
    newobj instance void [mscorlib]System.Runtime.InteropServices.ExternalException::.ctor(string, int32)
    throw
  }
  .method public static void ThrowAoore(native int b) cil managed noinlining {
    ldstr "msil.longjmp"
    ldarg.0
    box [mscorlib]System.IntPtr
    ldstr "longjmp"
    newobj instance void [mscorlib]System.ArgumentOutOfRangeException::.ctor(string, object, string)
    throw
  }
  .method public static void ThrowCached(native int b) cil managed noinlining {
    ldsfld class LjSub T::cached
    dup
    brtrue.s HAVE
    pop
    ldarg.0
    newobj instance void LjSub::.ctor(native int)
    dup
    stsfld class LjSub T::cached
    throw
  HAVE:
    dup
    ldarg.0
    stfld native int LjSub::buf
    throw
  }
  // Recurse d levels, then throw variant k. Not a tail call (result + 1).
  .method public static int32 Deep(int32 d, native int b, int32 k) cil managed noinlining {
    .maxstack 4
    ldarg.0
    brtrue.s REC
    ldarg.2
    switch (K0, K1, K2, K3, K4)
    ldc.i4.0
    ret
  K0: ldarg.1
    call void T::ThrowBoxed(native int)
    ldc.i4.0
    ret
  K1: ldarg.1
    call void T::ThrowSub(native int)
    ldc.i4.0
    ret
  K2: ldarg.1
    call void T::ThrowExt(native int)
    ldc.i4.0
    ret
  K3: ldarg.1
    call void T::ThrowAoore(native int)
    ldc.i4.0
    ret
  K4: ldarg.1
    call void T::ThrowCached(native int)
    ldc.i4.0
    ret
  REC:
    ldarg.0
    ldc.i4.1
    sub
    ldarg.1
    ldarg.2
    call int32 T::Deep(int32, native int, int32)
    ldc.i4.1
    add
    ret
  }
}
