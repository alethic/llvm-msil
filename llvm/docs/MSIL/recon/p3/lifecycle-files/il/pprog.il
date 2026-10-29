.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern plib { .ver 1:0:0:0 }
.assembly pprog { .ver 1:0:0:0 }
.module pprog.exe
.field private static string mode

.field private static native int[] '<msil>atexit_fns'
.field private static int32 '<msil>atexit_count'
.field private static object '<msil>exit_lock'

.method assembly static void trace(string s) cil managed {
  .maxstack 4
  call class [mscorlib]System.IO.TextWriter [mscorlib]System.Console::get_Error()
  ldstr "[tid {0}] {1}"
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
    call void [plib]plib::'<msil>fini'()
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
    ldstr "[tid {0}] {1}"
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

.method private static void h1() cil managed { ldstr "atexit h1 (registered 1st)" call void trace(string) ret }
.method private static void h2() cil managed {
  .maxstack 2
  ldstr "atexit h2 (registered 2nd)" call void trace(string)
  ldsfld string mode ldstr "nested" call bool [mscorlib]System.String::op_Equality(string, string) brfalse.s N1
  ldstr "h2 registers h_late" call void trace(string)
  ldftn void h_late() call int32 'atexit'(native int) pop
N1:
  ldsfld string mode ldstr "exitinhandler" call bool [mscorlib]System.String::op_Equality(string, string) brfalse.s N2
  ldstr "h2 calls exit(9)" call void trace(string)
  ldc.i4.s 9 call void 'exit'(int32)
N2:
  ret
}
.method private static void h_late() cil managed { ldstr "atexit h_late (registered during exit)" call void trace(string) ret }
.method private static void dtor() cil managed { ldstr "program destructor" call void trace(string) ret }
.method private static void '<msil>run_dtors'() cil managed { call void dtor() ret }
.method private static void ctor() cil managed {
  .maxstack 2
  ldstr "program constructor" call void trace(string)
  ldstr "PPROG_EXIT_IN_CTOR" call string [mscorlib]System.Environment::GetEnvironmentVariable(string) brfalse.s C1
  ldstr "program constructor calls exit(13)" call void trace(string)
  ldc.i4.s 13 call void 'exit'(int32)
C1:
  ret
}

.method public static int32 main() cil managed {
  .maxstack 2
  ldstr "main" call void trace(string)
  ldftn void h1() call int32 'atexit'(native int) pop
  call void [plib]plib::lib_register()
  ldftn void h2() call int32 'atexit'(native int) pop
  ldsfld string mode ldstr "exit3" call bool [mscorlib]System.String::op_Equality(string, string) brfalse.s M1
  ldc.i4.3 call void 'exit'(int32)
M1:
  ldsfld string mode ldstr "libexit" call bool [mscorlib]System.String::op_Equality(string, string) brfalse.s M2
  ldc.i4.5 call void [plib]plib::lib_exit(int32)
M2:
  ldsfld string mode ldstr "thread" call bool [mscorlib]System.String::op_Equality(string, string) brfalse.s M3
  ldstr "main starts a thread that calls exit(4), then sleeps 5 s"
  call void trace(string)
  ldnull ldftn void exit_thread()
  newobj instance void [mscorlib]System.Threading.ThreadStart::.ctor(object, native int)
  newobj instance void [mscorlib]System.Threading.Thread::.ctor(class [mscorlib]System.Threading.ThreadStart)
  callvirt instance void [mscorlib]System.Threading.Thread::Start()
  ldc.i4 5000 call void [mscorlib]System.Threading.Thread::Sleep(int32)
M3:
  ldstr "main returns 300" call void trace(string)
  ldc.i4 300
  ret
}
.method private static void exit_thread() cil managed { ldc.i4.4 call void 'exit'(int32) ret }

// '<msil>Main': exit(main()); the return is never reached.
.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed {
  .entrypoint
  .maxstack 2
  ldarg.0 ldlen brtrue.s HAVE
  ldstr "ret" stsfld string mode br.s GO
HAVE:
  ldarg.0 ldc.i4.0 ldelem.ref stsfld string mode
GO:
  call int32 main()
  call void 'exit'(int32)
  ldc.i4.0
  ret
}
.method private specialname rtspecialname static void .cctor() cil managed {
  .maxstack 1
  call void '<msil>init_exit'()
  ldtoken [plib]plib
  pop
  call void ctor()
  ldc.i4.1
  volatile. stsfld int32 '<msil>Exit'::initialized
  call void '<msil>subscribe_exit'()
  ret
}
