// check-mscorlib-safe.py: every entry below through [mscorlib]; the
// entry point returns 42 when each token resolved (plan 5.11.7).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'mscorlib-safe-probe' { .ver 0:0:0:0 }
.module 'mscorlib-safe-probe.exe'

// entry 1: type [mscorlib]System.Text.UTF8Encoding
.method public static void 'entry1'() cil managed noinlining
{
  .maxstack 1
  ldtoken [mscorlib]System.Text.UTF8Encoding
  pop
  ret
}

// entry 2: type [mscorlib]System.Text.UTF32Encoding
.method public static void 'entry2'() cil managed noinlining
{
  .maxstack 1
  ldtoken [mscorlib]System.Text.UTF32Encoding
  pop
  ret
}

// entry 3: type [mscorlib]System.Text.DecoderFallbackException
.method public static void 'entry3'() cil managed noinlining
{
  .maxstack 1
  ldtoken [mscorlib]System.Text.DecoderFallbackException
  pop
  ret
}

// entry 4: method instance void [mscorlib]System.Text.UTF8Encoding::.ctor(bool, bool)
.method public static void 'entry4'() cil managed noinlining
{
  .maxstack 1
  ldtoken method instance void [mscorlib]System.Text.UTF8Encoding::.ctor(bool, bool)
  pop
  ret
}

// entry 5: method instance void [mscorlib]System.Text.UTF32Encoding::.ctor(bool, bool, bool)
.method public static void 'entry5'() cil managed noinlining
{
  .maxstack 1
  ldtoken method instance void [mscorlib]System.Text.UTF32Encoding::.ctor(bool, bool, bool)
  pop
  ret
}

// entry 6: method instance void [mscorlib]System.String::.ctor(char*)
.method public static void 'entry6'() cil managed noinlining
{
  .maxstack 1
  ldtoken method instance void [mscorlib]System.String::.ctor(char*)
  pop
  ret
}

// entry 7: method instance object [mscorlib]System.Runtime.InteropServices.GCHandle::get_Target()
.method public static void 'entry7'() cil managed noinlining
{
  .maxstack 1
  ldtoken method instance object [mscorlib]System.Runtime.InteropServices.GCHandle::get_Target()
  pop
  ret
}

// entry 8: method instance void [mscorlib]System.Runtime.InteropServices.GCHandle::Free()
.method public static void 'entry8'() cil managed noinlining
{
  .maxstack 1
  ldtoken method instance void [mscorlib]System.Runtime.InteropServices.GCHandle::Free()
  pop
  ret
}

.method public static int32 'Main'() cil managed
{
  .entrypoint
  .maxstack 2
  .locals init (class [mscorlib]System.Exception V_0)
  .try
  {
    call void 'entry1'()
    leave L1
  }
  catch [mscorlib]System.Exception
  {
    stloc.0
    ldstr "check-mscorlib-safe: entry 1 failed: type [mscorlib]System.Text.UTF8Encoding"
    ldloc.0
    newobj instance void [mscorlib]System.Exception::.ctor(string, class [mscorlib]System.Exception)
    throw
  }
L1:
  .try
  {
    call void 'entry2'()
    leave L2
  }
  catch [mscorlib]System.Exception
  {
    stloc.0
    ldstr "check-mscorlib-safe: entry 2 failed: type [mscorlib]System.Text.UTF32Encoding"
    ldloc.0
    newobj instance void [mscorlib]System.Exception::.ctor(string, class [mscorlib]System.Exception)
    throw
  }
L2:
  .try
  {
    call void 'entry3'()
    leave L3
  }
  catch [mscorlib]System.Exception
  {
    stloc.0
    ldstr "check-mscorlib-safe: entry 3 failed: type [mscorlib]System.Text.DecoderFallbackException"
    ldloc.0
    newobj instance void [mscorlib]System.Exception::.ctor(string, class [mscorlib]System.Exception)
    throw
  }
L3:
  .try
  {
    call void 'entry4'()
    leave L4
  }
  catch [mscorlib]System.Exception
  {
    stloc.0
    ldstr "check-mscorlib-safe: entry 4 failed: method instance void [mscorlib]System.Text.UTF8Encoding::.ctor(bool, bool)"
    ldloc.0
    newobj instance void [mscorlib]System.Exception::.ctor(string, class [mscorlib]System.Exception)
    throw
  }
L4:
  .try
  {
    call void 'entry5'()
    leave L5
  }
  catch [mscorlib]System.Exception
  {
    stloc.0
    ldstr "check-mscorlib-safe: entry 5 failed: method instance void [mscorlib]System.Text.UTF32Encoding::.ctor(bool, bool, bool)"
    ldloc.0
    newobj instance void [mscorlib]System.Exception::.ctor(string, class [mscorlib]System.Exception)
    throw
  }
L5:
  .try
  {
    call void 'entry6'()
    leave L6
  }
  catch [mscorlib]System.Exception
  {
    stloc.0
    ldstr "check-mscorlib-safe: entry 6 failed: method instance void [mscorlib]System.String::.ctor(char*)"
    ldloc.0
    newobj instance void [mscorlib]System.Exception::.ctor(string, class [mscorlib]System.Exception)
    throw
  }
L6:
  .try
  {
    call void 'entry7'()
    leave L7
  }
  catch [mscorlib]System.Exception
  {
    stloc.0
    ldstr "check-mscorlib-safe: entry 7 failed: method instance object [mscorlib]System.Runtime.InteropServices.GCHandle::get_Target()"
    ldloc.0
    newobj instance void [mscorlib]System.Exception::.ctor(string, class [mscorlib]System.Exception)
    throw
  }
L7:
  .try
  {
    call void 'entry8'()
    leave L8
  }
  catch [mscorlib]System.Exception
  {
    stloc.0
    ldstr "check-mscorlib-safe: entry 8 failed: method instance void [mscorlib]System.Runtime.InteropServices.GCHandle::Free()"
    ldloc.0
    newobj instance void [mscorlib]System.Exception::.ctor(string, class [mscorlib]System.Exception)
    throw
  }
L8:
  ldc.i4.s 42
  ret
}
