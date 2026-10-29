.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly plib { .ver 1:0:0:0 }
.module plib.dll

.field private static native int[] '<msil>atexit_fns'
.field private static int32 '<msil>atexit_count'
.field private static object '<msil>exit_lock'

.method assembly static void trace(string s) cil managed {
  .maxstack 4
  call class [mscorlib]System.IO.TextWriter [mscorlib]System.Console::get_Error()
  ldstr "[tid {0}] plib: {1}"
  call class [mscorlib]System.Threading.Thread [mscorlib]System.Threading.Thread::get_CurrentThread()
  callvirt instance int32 [mscorlib]System.Threading.Thread::get_ManagedThreadId()
  box [mscorlib]System.Int32
  ldarg.0
  callvirt instance void [mscorlib]System.IO.TextWriter::WriteLine(string, object, object)
  ret
}

// C: int atexit(void (*)(void)) -- registers into this assembly's list (grown by doubling); thread-safe.
.method public static int32 'atexit'(native int fn) cil managed {
  .maxstack 4
  .locals (native int[] old, native int[] n, bool taken)
  ldc.i4.0
  stloc.2
  .try {
    ldsfld object '<msil>exit_lock'
    ldloca.s taken
    call void [mscorlib]System.Threading.Monitor::Enter(object, bool&)
    ldsfld native int[] '<msil>atexit_fns'
    stloc.0
    ldloc.0
    brfalse.s GROW
    ldsfld int32 '<msil>atexit_count'
    ldloc.0
    ldlen
    conv.i4
    blt.s STORE
  GROW:
    ldsfld int32 '<msil>atexit_count'
    ldc.i4.2
    mul
    ldc.i4.s 32
    add
    newarr [mscorlib]System.IntPtr
    stloc.1
    ldloc.0
    brfalse.s NOCOPY
    ldloc.0
    ldloc.1
    ldsfld int32 '<msil>atexit_count'
    call void [mscorlib]System.Array::Copy(class [mscorlib]System.Array, class [mscorlib]System.Array, int32)
  NOCOPY:
    ldloc.1
    stsfld native int[] '<msil>atexit_fns'
  STORE:
    ldsfld native int[] '<msil>atexit_fns'
    ldsfld int32 '<msil>atexit_count'
    ldarg.0
    stelem.i
    ldsfld int32 '<msil>atexit_count'
    ldc.i4.1
    add
    stsfld int32 '<msil>atexit_count'
    leave.s DONE
  } finally {
    ldloc.2
    brfalse.s ENDF
    ldsfld object '<msil>exit_lock'
    call void [mscorlib]System.Threading.Monitor::Exit(object)
  ENDF:
    endfinally
  }
DONE:
  ldc.i4.0
  ret
}

// Runs the handlers (last registered first, one at a time, so that a handler that registers another or calls
// exit continues the list), then the destructors, then the linked libraries' fini; once per assembly.
.method public static void '<msil>fini'() cil managed {
  .maxstack 3
  .locals (bool taken, native int fn)
  ldc.i4.0
  stloc.0
  .try {
    ldsfld object '<msil>exit_lock'
    ldloca.s taken
    call void [mscorlib]System.Threading.Monitor::Enter(object, bool&)
    ldsfld int32 '<msil>Exit'::state
    ldc.i4.2
    beq.s OUT
    ldc.i4.1
    stsfld int32 '<msil>Exit'::state
  NEXT:
    ldsfld int32 '<msil>atexit_count'
    ldc.i4.0
    ble.s DTORS
    ldsfld int32 '<msil>atexit_count'
    ldc.i4.1
    sub
    stsfld int32 '<msil>atexit_count'
    ldsfld native int[] '<msil>atexit_fns'
    ldsfld int32 '<msil>atexit_count'
    ldelem.i
    stloc.1
    ldloc.1
    calli void()
    br.s NEXT
  DTORS:
    call void '<msil>run_dtors'()
    ldc.i4.2
    stsfld int32 '<msil>Exit'::state

  OUT:
    leave.s DONE
  } finally {
    ldloc.0
    brfalse.s ENDF
    ldsfld object '<msil>exit_lock'
    call void [mscorlib]System.Threading.Monitor::Exit(object)
  ENDF:
    endfinally
  }
DONE:
  ret
}

// '<msil>Exit' has no type initializer, so the ProcessExit handler never waits for <Module>'s .cctor (a
// constructor that calls exit runs inside it). It does nothing when an exit of this assembly has started (on any
// thread, possibly the one now waiting in Environment.Exit while it holds the lock) or when the module's
// initialization has not finished.
.class private auto ansi abstract sealed '<msil>Exit' extends [mscorlib]System.Object {
  .field assembly static int32 state
  .field assembly static int32 initialized
  .method assembly static void trace(string s) cil managed {
    .maxstack 4
    call class [mscorlib]System.IO.TextWriter [mscorlib]System.Console::get_Error()
    ldstr "[tid {0}] plib: {1}"
    call class [mscorlib]System.Threading.Thread [mscorlib]System.Threading.Thread::get_CurrentThread()
    callvirt instance int32 [mscorlib]System.Threading.Thread::get_ManagedThreadId()
    box [mscorlib]System.Int32
    ldarg.0
    callvirt instance void [mscorlib]System.IO.TextWriter::WriteLine(string, object, object)
    ret
  }
  .method assembly static void on_process_exit(object s, class [mscorlib]System.EventArgs e) cil managed {
    .maxstack 2
    volatile. ldsfld int32 '<msil>Exit'::state
    brtrue.s SKIP
    volatile. ldsfld int32 '<msil>Exit'::initialized
    brfalse.s SKIP2
    ldstr "ProcessExit handler -> fini"
    call void '<msil>Exit'::trace(string)
    call void '<msil>fini'()
    ret
  SKIP:
    ldstr "ProcessExit handler: exit already started, nothing to do"
    call void '<msil>Exit'::trace(string)
    ret
  SKIP2:
    ldstr "ProcessExit handler: module initialization did not finish, nothing to do"
    call void '<msil>Exit'::trace(string)
    ret
  }
}

// The start of phase 2 of the module initializer: the lock, then the ProcessExit subscription.
.method assembly static void '<msil>init_exit'() cil managed {
  .maxstack 1
  newobj instance void [mscorlib]System.Object::.ctor()
  stsfld object '<msil>exit_lock'
  ret
}
// The end of the module initializer: subscribe only once the module is initialized, so that a ProcessExit that
// starts while the initializer runs (a constructor that calls exit) never invokes a handler of this module, whose
// every member CoreCLR would make wait for the initializer (ECMA-335 II.10.5.3.1): a deadlock.
.method assembly static void '<msil>subscribe_exit'() cil managed {
  .maxstack 4
  call class [mscorlib]System.AppDomain [mscorlib]System.AppDomain::get_CurrentDomain()
  ldnull
  ldftn void '<msil>Exit'::on_process_exit(object, class [mscorlib]System.EventArgs)
  newobj instance void [mscorlib]System.EventHandler::.ctor(object, native int)
  callvirt instance void [mscorlib]System.AppDomain::add_ProcessExit(class [mscorlib]System.EventHandler)
  ret
}

// C: void exit(int) -- this assembly's handlers and destructors (and its libraries'), then Environment.Exit.
.method public static void 'exit'(int32 code) cil managed {
  .maxstack 1
  ldstr "exit called"
  call void trace(string)
  call void '<msil>fini'()
  ldarg.0
  call void [mscorlib]System.Environment::Exit(int32)
  ret
}

.method assembly static void lib_h() cil managed { ldstr "plib atexit handler"  call void trace(string) ret }
.method private static void lib_dtor() cil managed { ldstr "plib destructor" call void trace(string) ret }
.method private static void '<msil>run_dtors'() cil managed { call void lib_dtor() ret }
.method private static void lib_ctor() cil managed {
  .maxstack 2
  ldstr "plib constructor" call void trace(string)
  ldstr "PLIB_EXIT_IN_CTOR" call string [mscorlib]System.Environment::GetEnvironmentVariable(string) brfalse.s C1
  ldstr "plib constructor calls exit(14)" call void trace(string)
  ldc.i4.s 14 call void 'exit'(int32)
C1:
  ret
}
.method private specialname rtspecialname static void .cctor() cil managed {
  .maxstack 1
  call void '<msil>init_exit'()
  call void lib_ctor()
  ldc.i4.1
  volatile. stsfld int32 '<msil>Exit'::initialized
  call void '<msil>subscribe_exit'()
  ret
}
.class public auto ansi abstract sealed beforefieldinit plib extends [mscorlib]System.Object {
  .method public static void lib_register() cil managed {
    .maxstack 1
    ldstr "plib: lib_register calls atexit(lib_h)"
    call void trace(string)
    ldftn void lib_h()
    call int32 'atexit'(native int)
    pop
    ret
  }
  .method public static void lib_exit(int32 c) cil managed { ldarg.0 call void 'exit'(int32) ret }
  .method public static void '<msil>fini'() cil managed { call void '<msil>fini'() ret }
}
