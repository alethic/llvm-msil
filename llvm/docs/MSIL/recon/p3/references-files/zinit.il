// R-a's first runtime check (2026-11-19): does the runtime zero object-typed locals of a method whose header does
// NOT set localsinit (this compiler never sets it, Design.md:139)? Dirty() fills a frame with non-null references;
// Probe() is called right after from the same caller, reads its own uninitialized object locals, and every 1000th
// call collects first, which would crash on a garbage reference. Exit 42 = every read was null; 1 = a non-null read.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly zinit { .ver 1:0:0:0 }
.module zinit.exe

.class public auto ansi abstract sealed P extends [mscorlib]System.Object
{
  .method public hidebysig static void Dirty() cil managed noinlining
  {
    .maxstack 8
    .locals init (object a, object b, object c, object d, object e, object f, object g, object h, string s, object[] arr)
    newobj instance void [mscorlib]System.Object::.ctor()
    stloc.0
    ldloc.0
    stloc.1
    ldloc.0
    stloc.2
    ldloc.0
    stloc.3
    ldloc.0
    stloc.s 4
    ldloc.0
    stloc.s 5
    ldloc.0
    stloc.s 6
    ldloc.0
    stloc.s 7
    ldstr "dirty"
    stloc.s 8
    ldc.i4.4
    newarr [mscorlib]System.Object
    stloc.s 9
    ldloc.0
    call void [mscorlib]System.GC::KeepAlive(object)
    ldloc.1
    call void [mscorlib]System.GC::KeepAlive(object)
    ldloc.2
    call void [mscorlib]System.GC::KeepAlive(object)
    ldloc.3
    call void [mscorlib]System.GC::KeepAlive(object)
    ldloc.s 4
    call void [mscorlib]System.GC::KeepAlive(object)
    ldloc.s 5
    call void [mscorlib]System.GC::KeepAlive(object)
    ldloc.s 6
    call void [mscorlib]System.GC::KeepAlive(object)
    ldloc.s 7
    call void [mscorlib]System.GC::KeepAlive(object)
    ldloc.s 8
    call void [mscorlib]System.GC::KeepAlive(object)
    ldloc.s 9
    call void [mscorlib]System.GC::KeepAlive(object)
    ret
  }

  .method public hidebysig static int32 Probe(bool collect) cil managed noinlining
  {
    .maxstack 8
    .locals (object a, object b, object c, object d, object e, object f, object g, object h, string s, object[] arr)
    ldarg.0
    brfalse.s READ
    call void [mscorlib]System.GC::Collect()
  READ:
    ldloc.0
    brtrue BAD
    ldloc.1
    brtrue BAD
    ldloc.2
    brtrue BAD
    ldloc.3
    brtrue BAD
    ldloc.s 4
    brtrue BAD
    ldloc.s 5
    brtrue BAD
    ldloc.s 6
    brtrue BAD
    ldloc.s 7
    brtrue BAD
    ldloc.s 8
    brtrue BAD
    ldloc.s 9
    brtrue BAD
    ldc.i4.0
    ret
  BAD:
    ldc.i4.1
    ret
  }

  .method public hidebysig static int32 Main() cil managed
  {
    .entrypoint
    .maxstack 8
    .locals init (int32 i, int32 bad)
  LOOP:
    ldloc.0
    ldc.i4 100000
    bge DONE
    call void P::Dirty()
    ldloc.1
    ldloc.0
    ldc.i4 1000
    rem
    ldc.i4.0
    ceq
    call int32 P::Probe(bool)
    add
    stloc.1
    ldloc.0
    ldc.i4.1
    add
    stloc.0
    br LOOP
  DONE:
    ldloc.1
    brtrue FAIL
    ldc.i4.s 42
    ret
  FAIL:
    ldc.i4.1
    ret
  }
}
