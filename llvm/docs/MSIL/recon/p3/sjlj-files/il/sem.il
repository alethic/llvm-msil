// Assembly A: the setjmp side. Filters match the thrown object without a shared type.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern System { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern thrower { .ver 1:0:0:0 }
.assembly extern mid { .ver 1:0:0:0 }
.assembly sem { .ver 1:0:0:0 }
.module sem.exe
.class private abstract sealed auto ansi P extends [mscorlib]System.Object {

  // kind 0 boxed IntPtr, 1 LjSub (shared subclass, baseline), 2 ExternalException(HResult),
  // 3 ArgumentOutOfRangeException(ActualValue), 4 cached LjSub
  .method static int32 Match(object e, native int st, int32 kind) cil managed noinlining {
    .maxstack 4
    ldarg.2
    switch (B, S, X, A, S)
    ldc.i4.0
    ret
  B: ldarg.0
    isinst [mscorlib]System.IntPtr
    dup
    brfalse.s ZB
    unbox.any native int
    ldarg.1
    ceq
    ret
  ZB: pop
    ldc.i4.0
    ret
  S: ldarg.0
    isinst [thrower]LjSub
    dup
    brfalse.s ZS
    ldfld native int [thrower]LjSub::buf
    ldarg.1
    ceq
    ret
  ZS: pop
    ldc.i4.0
    ret
  X: ldarg.0
    isinst [mscorlib]System.Runtime.InteropServices.ExternalException
    dup
    brfalse.s ZX
    callvirt instance int32 [mscorlib]System.Exception::get_HResult()
    ldarg.1
    conv.i4
    ceq
    ret
  ZX: pop
    ldc.i4.0
    ret
  A: ldarg.0
    isinst [mscorlib]System.ArgumentOutOfRangeException
    dup
    brfalse.s ZA
    callvirt instance object [mscorlib]System.ArgumentOutOfRangeException::get_ActualValue()
    unbox.any native int
    ldarg.1
    ceq
    ret
  ZA: pop
    ldc.i4.0
    ret
  }

  // The invoke wrapper shape: the whole body is a try; the filter asks Match; the handler returns 1.
  .method static int32 Wrap(native int st, int32 depth, int32 kind) cil managed noinlining {
    .maxstack 4
    .locals (int32 r)
    .try {
      ldarg.1
      ldarg.0
      ldarg.2
      call int32 [thrower]T::Deep(int32, native int, int32)
      pop
      ldc.i4.0
      stloc.0
      leave.s DONE
    } filter {
      ldarg.0
      ldarg.2
      call int32 P::Match(object, native int, int32)
      endfilter
    } {
      pop
      ldc.i4.1
      stloc.0
      leave.s DONE
    }
  DONE:
    ldloc.0
    ret
  }

  .method static void Bench(int32 kind, int32 depth, int32 n, string name) cil managed noinlining {
    .maxstack 8
    .locals (native int st, int32 i, int64 t0, int32 caught, int64 t1)
    ldc.i4.s 16
    localloc
    stloc.0
    ldc.i4.0
    stloc.1
  W: ldloc.0
    ldarg.1
    ldarg.0
    call int32 P::Wrap(native int, int32, int32)
    pop
    ldloc.1
    ldc.i4.1
    add
    dup
    stloc.1
    ldc.i4 2000
    blt.s W
    ldc.i4.0
    stloc.1
    ldc.i4.0
    stloc.3
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp()
    stloc.2
  L: ldloc.3
    ldloc.0
    ldarg.1
    ldarg.0
    call int32 P::Wrap(native int, int32, int32)
    add
    stloc.3
    ldloc.1
    ldc.i4.1
    add
    dup
    stloc.1
    ldarg.2
    blt.s L
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp()
    stloc.s t1
    ldstr "{0,-26} depth {1,2}: {2,8:F0} ns per round trip (caught {3})"
    ldc.i4.4
    newarr [mscorlib]System.Object
    dup
    ldc.i4.0
    ldarg.3
    stelem.ref
    dup
    ldc.i4.1
    ldarg.1
    box [mscorlib]System.Int32
    stelem.ref
    dup
    ldc.i4.2
    ldloc.s t1
    ldloc.2
    sub
    conv.r8
    ldc.r8 1000000000.0
    mul
    ldsfld int64 [System]System.Diagnostics.Stopwatch::Frequency
    conv.r8
    div
    ldarg.2
    conv.r8
    div
    box [mscorlib]System.Double
    stelem.ref
    dup
    ldc.i4.3
    ldloc.3
    box [mscorlib]System.Int32
    stelem.ref
    call void [mscorlib]System.Console::WriteLine(string, object[])
    ret
  }

  .method static void PrintI(string s, int32 v) cil managed {
    ldarg.0
    ldarg.1
    box [mscorlib]System.Int32
    call void [mscorlib]System.Console::WriteLine(string, object)
    ret
  }

  // Test 1-3: locals and filter-written state survive into the handler and after it.
  .method static void Locals(int32 via) cil managed noinlining {
    .maxstack 4
    .locals (int32 a, native int st, int32 flt, int32 caught)
    ldc.i4.s 11
    stloc.0
    ldc.i4.s 16
    localloc
    stloc.1
    ldloc.1
    ldc.i4.0
    stind.i4
    ldc.i4.0
    stloc.2
    ldc.i4.0
    stloc.3
    .try {
      ldc.i4.s 22
      stloc.0
      ldarg.0
      switch (V0, V1, V2)
    V0:
      ldc.i4.1
      ldloc.1
      ldc.i4.0
      call int32 [thrower]T::Deep(int32, native int, int32)
      pop
      leave.s AFTER
    V1:
      ldloc.1
      ldc.i4.0
      call int32 [mid]Mid::CallThrough(native int, int32)
      pop
      leave.s AFTER
    V2:
      ldloc.1
      ldc.i4.0
      call int32 [mid]Mid::CatchRethrow(native int, int32)
      pop
      leave.s AFTER
    } filter {
      isinst [mscorlib]System.IntPtr
      dup
      brtrue.s M
      pop
      ldstr "  IL filter: not a boxed IntPtr"
      call void [mscorlib]System.Console::WriteLine(string)
      ldc.i4.0
      br.s E
    M: unbox.any native int
      ldloc.1
      ceq
      dup
      stloc.2
      ldloc.1
      ldc.i4.s 85
      stind.i4
      ldstr "  IL filter matched the boxed IntPtr"
      call void [mscorlib]System.Console::WriteLine(string)
    E: endfilter
    } {
      pop
      ldc.i4.1
      stloc.3
      leave.s AFTER
    }
  AFTER:
    ldstr "  a (set to 22 in the try) = {0}"
    ldloc.0
    call void P::PrintI(string, int32)
    ldstr "  flt (local written by the filter) = {0}"
    ldloc.2
    call void P::PrintI(string, int32)
    ldstr "  *st (frame memory written by the filter) = {0}"
    ldloc.1
    ldind.i4
    call void P::PrintI(string, int32)
    ldstr "  caught = {0}"
    ldloc.3
    call void P::PrintI(string, int32)
    ret
  }

  // Test 5: a non-matching exception passes the filter (filter returns 0) and is caught outside.
  // Test 6: a boxed IntPtr for another frame passes an inner wrapper and is caught by the outer one.
  .method static void Passing() cil managed noinlining {
    .maxstack 4
    .locals (native int st1, native int st2, int32 r)
    ldc.i4.s 16
    localloc
    stloc.0
    ldc.i4.s 16
    localloc
    stloc.1
    .try {
      .try {
        ldc.i4.1
        ldloc.0
        ldc.i4.1
        call int32 [thrower]T::Deep(int32, native int, int32)
        pop
        leave.s IN
      } filter {
        ldloc.0
        ldc.i4.0
        call int32 P::Match(object, native int, int32)
        endfilter
      } {
        pop
        ldstr "  WRONG: inner caught a non-longjmp exception"
        call void [mscorlib]System.Console::WriteLine(string)
        leave.s IN
      }
    IN: leave.s OUT
    } catch [mscorlib]System.Exception {
      callvirt instance class [mscorlib]System.Type [mscorlib]System.Object::GetType()
      callvirt instance string [mscorlib]System.Reflection.MemberInfo::get_Name()
      ldstr " <- type the outer catch got; the inner filter declined"
      call string [mscorlib]System.String::Concat(string, string)
      call void [mscorlib]System.Console::WriteLine(string)
      leave.s OUT
    }
  OUT:
    .try {
      .try {
        ldc.i4.1
        ldloc.0
        ldc.i4.0
        call int32 [thrower]T::Deep(int32, native int, int32)
        pop
        leave.s IN2
      } filter {
        ldloc.1
        ldc.i4.0
        call int32 P::Match(object, native int, int32)
        endfilter
      } {
        pop
        ldstr "  WRONG: inner (st2) caught a longjmp for st1"
        call void [mscorlib]System.Console::WriteLine(string)
        leave.s IN2
      }
    IN2: leave.s OUT2
    } filter {
      ldloc.0
      ldc.i4.0
      call int32 P::Match(object, native int, int32)
      endfilter
    } {
      pop
      ldstr "  outer (st1) caught its longjmp; inner (st2) declined"
      call void [mscorlib]System.Console::WriteLine(string)
      leave.s OUT2
    }
  OUT2:
    ret
  }

  .method static int32 Main(string[] args) cil managed {
    .entrypoint
    .maxstack 8
    .locals (string mode, int32 n)
    ldstr "CLR {0}, {1}-bit"
    call class [mscorlib]System.Version [mscorlib]System.Environment::get_Version()
    call int32 [mscorlib]System.IntPtr::get_Size()
    ldc.i4.8
    mul
    box [mscorlib]System.Int32
    call void [mscorlib]System.Console::WriteLine(string, object, object)
    ldarg.0
    ldc.i4.0
    ldelem.ref
    stloc.0
    ldloc.0
    ldstr "sem"
    call bool [mscorlib]System.String::op_Equality(string, string)
    brfalse PERF
    ldstr "test 1: IL frame, throw two frames deeper"
    call void [mscorlib]System.Console::WriteLine(string)
    ldc.i4.0
    call void P::Locals(int32)
    ldstr "test 2: a C# frame in between (filter declining, finally)"
    call void [mscorlib]System.Console::WriteLine(string)
    ldc.i4.1
    call void P::Locals(int32)
    ldstr "test 3: a C# catch(Exception) + throw; in between"
    call void [mscorlib]System.Console::WriteLine(string)
    ldc.i4.2
    call void P::Locals(int32)
    ldstr "tests 5-6: non-matching exceptions pass"
    call void [mscorlib]System.Console::WriteLine(string)
    call void P::Passing()
    ldc.i4.0
    ret
  PERF:
    ldloc.0
    ldstr "perf"
    call bool [mscorlib]System.String::op_Equality(string, string)
    brfalse UNH
    ldarg.0
    ldc.i4.1
    ldelem.ref
    call int32 [mscorlib]System.Int32::Parse(string)
    stloc.1
    ldc.i4.0
    ldc.i4.1
    ldloc.1
    ldstr "boxed IntPtr"
    call void P::Bench(int32, int32, int32, string)
    ldc.i4.1
    ldc.i4.1
    ldloc.1
    ldstr "Exception subclass"
    call void P::Bench(int32, int32, int32, string)
    ldc.i4.2
    ldc.i4.1
    ldloc.1
    ldstr "ExternalException(HResult)"
    call void P::Bench(int32, int32, int32, string)
    ldc.i4.3
    ldc.i4.1
    ldloc.1
    ldstr "AOORE(ActualValue)"
    call void P::Bench(int32, int32, int32, string)
    ldc.i4.4
    ldc.i4.1
    ldloc.1
    ldstr "cached subclass object"
    call void P::Bench(int32, int32, int32, string)
    ldc.i4.0
    ldc.i4.s 10
    ldloc.1
    ldstr "boxed IntPtr"
    call void P::Bench(int32, int32, int32, string)
    ldc.i4.1
    ldc.i4.s 10
    ldloc.1
    ldstr "Exception subclass"
    call void P::Bench(int32, int32, int32, string)
    ldc.i4.2
    ldc.i4.s 10
    ldloc.1
    ldstr "ExternalException(HResult)"
    call void P::Bench(int32, int32, int32, string)
    ldc.i4.4
    ldc.i4.s 10
    ldloc.1
    ldstr "cached subclass object"
    call void P::Bench(int32, int32, int32, string)
    ldc.i4.0
    ret
  UNH:
    ldstr "unhandled: throwing a boxed IntPtr with no handler"
    call void [mscorlib]System.Console::WriteLine(string)
    ldc.i4 4660
    conv.i
    call void [thrower]T::ThrowBoxed(native int)
    ldc.i4.0
    ret
  }
}
