// cb.il: native-callable function pointer experiments that run on .NET Framework 4.8,
// .NET 8 and .NET 10 (mscorlib reference only). One test per process: cb.exe <test> [n].
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern System { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly cb { .ver 0:0:0:0 }
.module cb.exe
.imagebase 0x00400000
.subsystem 0x0003
.corflags 0x00000001

// The delegate types the backend would emit, one per IL signature: .ctor and Invoke only,
// with UnmanagedFunctionPointer(Cdecl) (CallingConvention.Cdecl = 2).
.class private auto ansi sealed D_i_i extends [mscorlib]System.MulticastDelegate
{
  .custom instance void [mscorlib]System.Runtime.InteropServices.UnmanagedFunctionPointerAttribute::.ctor(valuetype [mscorlib]System.Runtime.InteropServices.CallingConvention) = ( 01 00 02 00 00 00 00 00 )
  .method public hidebysig specialname rtspecialname instance void .ctor(object 'object', native int 'method') runtime managed {}
  .method public hidebysig newslot virtual instance int32 Invoke(int32 x) runtime managed {}
}
// The same without the attribute (Winapi default: cdecl on x64 Windows and on Linux).
.class private auto ansi sealed D_i_i_noattr extends [mscorlib]System.MulticastDelegate
{
  .method public hidebysig specialname rtspecialname instance void .ctor(object 'object', native int 'method') runtime managed {}
  .method public hidebysig newslot virtual instance int32 Invoke(int32 x) runtime managed {}
}
.class private auto ansi sealed D_p_p extends [mscorlib]System.MulticastDelegate
{
  .custom instance void [mscorlib]System.Runtime.InteropServices.UnmanagedFunctionPointerAttribute::.ctor(valuetype [mscorlib]System.Runtime.InteropServices.CallingConvention) = ( 01 00 02 00 00 00 00 00 )
  .method public hidebysig specialname rtspecialname instance void .ctor(object 'object', native int 'method') runtime managed {}
  .method public hidebysig newslot virtual instance native int Invoke(native int a) runtime managed {}
}
.class private auto ansi sealed D_cmp extends [mscorlib]System.MulticastDelegate
{
  .custom instance void [mscorlib]System.Runtime.InteropServices.UnmanagedFunctionPointerAttribute::.ctor(valuetype [mscorlib]System.Runtime.InteropServices.CallingConvention) = ( 01 00 02 00 00 00 00 00 )
  .method public hidebysig specialname rtspecialname instance void .ctor(object 'object', native int 'method') runtime managed {}
  .method public hidebysig newslot virtual instance int32 Invoke(native int a, native int b) runtime managed {}
}

.class private abstract auto ansi sealed beforefieldinit Prog extends [mscorlib]System.Object
{
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int32 call_cb(native int f, int32 x) cil managed preservesig {}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int32 call_cb_twice(native int f, int32 x) cil managed preservesig {}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int64 loop_cb(native int f, int32 n) cil managed preservesig {}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) native int get_native_fn() cil managed preservesig {}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int64 loop_native(int32 n) cil managed preservesig {}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) void do_qsort(native int b, native int n, native int sz, native int cmp) cil managed preservesig {}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int32 call_ops(native int o, int32 x) cil managed preservesig {}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) int32 run_thread(native int s, native int a, native int r) cil managed preservesig {}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) native int native_thread_id() cil managed preservesig {}
  .method assembly static pinvokeimpl("cbnative" nomangle cdecl) native int get_loop_native() cil managed preservesig {}
  .field static int64 churned

  .field static class [mscorlib]System.Delegate[] keep
  .field static native int fp_del
  .field static native int fp_ldftn
  .field static int32 tsv
  .custom instance void [mscorlib]System.ThreadStaticAttribute::.ctor() = ( 01 00 00 00 )

  // ---------------------------------------------------------------- C functions
  .method static int32 add1(int32 x) cil managed { .maxstack 2 ldarg.0 ldc.i4.1 add ret }
  .method static int32 add1_cold(int32 x) cil managed { .maxstack 2 ldarg.0 ldc.i4.1 add ret }
  .method static int32 add1_gc(int32 x) cil managed
  {
    .maxstack 2
    ldc.i4 1000 newarr [mscorlib]System.Object pop
    call void [mscorlib]System.GC::Collect()
    ldarg.0 ldc.i4.1 add ret
  }
  .method static int32 add1_throw(int32 x) cil managed
  {
    .maxstack 2
    ldarg.0 ldc.i4.5 blt.s OK
    ldstr "thrown in callback" newobj instance void [mscorlib]System.InvalidOperationException::.ctor(string) throw
OK: ldarg.0 ldc.i4.1 add ret
  }
  .method static int32 cmp_int(native int a, native int b) cil managed
  {
    .maxstack 3
    ldarg.0 ldind.i4 ldarg.1 ldind.i4 cgt
    ldarg.0 ldind.i4 ldarg.1 ldind.i4 clt
    sub ret
  }
  .method static native int thread_start(native int arg) cil managed
  {
    .maxstack 4
    ldstr "  in thread: tsv before={0} managedId={1} isBackground={2}"
    ldsfld int32 Prog::tsv box [mscorlib]System.Int32
    call class [mscorlib]System.Threading.Thread [mscorlib]System.Threading.Thread::get_CurrentThread()
    callvirt instance int32 [mscorlib]System.Threading.Thread::get_ManagedThreadId() box [mscorlib]System.Int32
    call class [mscorlib]System.Threading.Thread [mscorlib]System.Threading.Thread::get_CurrentThread()
    callvirt instance bool [mscorlib]System.Threading.Thread::get_IsBackground() box [mscorlib]System.Boolean
    call void [mscorlib]System.Console::WriteLine(string, object, object, object)
    ldc.i4 77 stsfld int32 Prog::tsv
    ldarg.0 ldc.i4.1 add ret
  }
  .field static bool stop
  .field static object sink
  .method static void churn() cil managed
  {
    // A managed thread that allocates without pause, so collections run while the callback executes.
    .maxstack 3
LOOP:
    ldc.i4 64 newarr [mscorlib]System.Byte stsfld object Prog::sink
    ldsfld int64 Prog::churned ldc.i4.1 conv.i8 add stsfld int64 Prog::churned
    ldsfld bool Prog::stop brfalse.s LOOP
    ret
  }
  .method static int32 add1_refs(int32 x) cil managed
  {
    // Holds object references in locals across allocations and checks them afterwards.
    .maxstack 4
    .locals (string a, object[] arr, int32 i)
    ldarg.0 box [mscorlib]System.Int32 callvirt instance string [mscorlib]System.Object::ToString() stloc.0
    ldc.i4.s 32 newarr [mscorlib]System.Object stloc.1
    ldc.i4.0 stloc.2
FILL: ldloc.1 ldloc.2 ldloc.2 box [mscorlib]System.Int32 stelem.ref
    ldloc.2 ldc.i4.1 add dup stloc.2 ldc.i4.s 32 blt.s FILL
    ldc.i4.0 stloc.2
CHK: ldloc.1 ldloc.2 ldelem.ref unbox.any [mscorlib]System.Int32 ldloc.2 beq.s NEXT
    ldstr "CORRUPTION: boxed value changed" call void [mscorlib]System.Console::WriteLine(string) ldc.i4 -1000000 ret
NEXT: ldloc.2 ldc.i4.1 add dup stloc.2 ldc.i4.s 32 blt.s CHK
    ldloc.0 call int32 [mscorlib]System.Int32::Parse(string) ldc.i4.1 add ret
  }
  .method static native int thread_start_throw(native int arg) cil managed
  {
    .maxstack 2
    ldstr "thrown on native thread" newobj instance void [mscorlib]System.InvalidOperationException::.ctor(string) throw
  }

  // ---------------------------------------------------------------- helpers
  .method static bool is(string a, string b) cil managed { .maxstack 2 ldarg.0 ldarg.1 call bool [mscorlib]System.String::op_Equality(string, string) ret }
  .method static void say(int64 v, string s) cil managed
  {
    .maxstack 3
    ldarg.1 ldstr ": " ldarg.0 box [mscorlib]System.Int64 call string [mscorlib]System.String::Concat(object, object, object)
    call void [mscorlib]System.Console::WriteLine(string) ret
  }
  .method static native int keepalive(class [mscorlib]System.Delegate d) cil managed
  {
    // One kept-alive delegate per function: a static array holds it, and its native-callable pointer is returned.
    .maxstack 4
    ldsfld class [mscorlib]System.Delegate[] Prog::keep brtrue.s HAVE
    ldc.i4 16 newarr [mscorlib]System.Delegate stsfld class [mscorlib]System.Delegate[] Prog::keep
HAVE:
    ldsfld class [mscorlib]System.Delegate[] Prog::keep ldsfld int32 Prog::nkeep ldarg.0 stelem.ref
    ldsfld int32 Prog::nkeep ldc.i4.1 add stsfld int32 Prog::nkeep
    ldarg.0 call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate)
    ret
  }
  .method static native int mkdel_i_i(native int m) cil managed
  {
    .maxstack 3
    ldnull ldarg.0 newobj instance void D_i_i::.ctor(object, native int) call native int Prog::keepalive(class [mscorlib]System.Delegate) ret
  }
  .field static int32 nkeep
  .method static void bench(string name, int64 ticks, int32 n) cil managed
  {
    .maxstack 4
    ldstr "{0,-44} {1,8:F2} ns/call"
    ldarg.0
    ldarg.1 conv.r8 ldc.r8 1e9 mul ldsfld int64 [System]System.Diagnostics.Stopwatch::Frequency conv.r8 div ldarg.2 conv.r8 div box [mscorlib]System.Double
    call void [mscorlib]System.Console::WriteLine(string, object, object)
    ret
  }

  // ---------------------------------------------------------------- Main
  .method static int32 Main(string[] args) cil managed
  {
    .entrypoint
    .maxstack 8
    .locals (string t, native int p, int64 s, int32 n, int32 i, class [System]System.Diagnostics.Stopwatch sw, native int q, native int mem, int32 r, int32 rep, int64 c0)
    ldarg.0 ldc.i4.0 ldelem.ref stloc.0
    ldc.i4 20000000 stloc.3
    ldarg.0 ldlen conv.i4 ldc.i4.2 blt.s NODEF
    ldarg.0 ldc.i4.1 ldelem.ref call int32 [mscorlib]System.Int32::Parse(string) stloc.3
NODEF:
    ldstr "runtime" call class [mscorlib]System.Version [mscorlib]System.Environment::get_Version() callvirt instance string [mscorlib]System.Object::ToString()
    call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string)

    ldloc.0 ldstr "ldftn" call bool Prog::is(string, string) brtrue T_LDFTN
    ldloc.0 ldstr "ldftn-cold" call bool Prog::is(string, string) brtrue T_LDFTN_COLD
    ldloc.0 ldstr "ldftn-loop" call bool Prog::is(string, string) brtrue T_LDFTN_LOOP
    ldloc.0 ldstr "ldftn-gc" call bool Prog::is(string, string) brtrue T_LDFTN_GC
    ldloc.0 ldstr "ldftn-thread" call bool Prog::is(string, string) brtrue T_LDFTN_THREAD
    ldloc.0 ldstr "del" call bool Prog::is(string, string) brtrue T_DEL
    ldloc.0 ldstr "calli-managed-native-gc" call bool Prog::is(string, string) brtrue T_CMN_GC
    ldloc.0 ldstr "calli-unmanaged-native-gc" call bool Prog::is(string, string) brtrue T_CUN_GC
    ldloc.0 ldstr "ldftn-gcstress" call bool Prog::is(string, string) brtrue T_LDFTN_GCSTRESS
    ldloc.0 ldstr "del-gcstress" call bool Prog::is(string, string) brtrue T_DEL_GCSTRESS
    ldloc.0 ldstr "del-noattr" call bool Prog::is(string, string) brtrue T_DEL_NOATTR
    ldloc.0 ldstr "del-gc" call bool Prog::is(string, string) brtrue T_DEL_GC
    ldloc.0 ldstr "del-collected" call bool Prog::is(string, string) brtrue T_DEL_COLLECTED
    ldloc.0 ldstr "del-identity" call bool Prog::is(string, string) brtrue T_DEL_ID
    ldloc.0 ldstr "del-thread" call bool Prog::is(string, string) brtrue T_DEL_THREAD
    ldloc.0 ldstr "del-thread-throw" call bool Prog::is(string, string) brtrue T_DEL_THREAD_THROW
    ldloc.0 ldstr "del-throw" call bool Prog::is(string, string) brtrue T_DEL_THROW
    ldloc.0 ldstr "del-ops" call bool Prog::is(string, string) brtrue T_DEL_OPS
    ldloc.0 ldstr "del-qsort" call bool Prog::is(string, string) brtrue T_DEL_QSORT
    ldloc.0 ldstr "calli-native" call bool Prog::is(string, string) brtrue T_CALLI_NATIVE
    ldloc.0 ldstr "calli-del" call bool Prog::is(string, string) brtrue T_CALLI_DEL
    ldloc.0 ldstr "calli-unmanaged-on-ldftn" call bool Prog::is(string, string) brtrue T_CU_LDFTN
    ldloc.0 ldstr "calli-managed-on-native" call bool Prog::is(string, string) brtrue T_CM_NATIVE
    ldloc.0 ldstr "bench" call bool Prog::is(string, string) brtrue T_BENCH
    ldstr "unknown test" call void [mscorlib]System.Console::WriteLine(string)
    ldc.i4 99 ret

T_LDFTN:
    // An ldftn value (what '<msil>fn_' holds today) handed to native code, which calls it.
    ldc.i4.1 call int32 Prog::add1(int32) pop        // add1 has run once (compiled code, not precode)
    ldftn int32 Prog::add1(int32) ldc.i4 41 call int32 Prog::call_cb(native int, int32) conv.i8
    ldstr "native call of ldftn value" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_LDFTN_COLD:
    // ldftn of a method that never ran: on CoreCLR the value can be a precode stub.
    ldftn int32 Prog::add1_cold(int32) ldc.i4 41 call int32 Prog::call_cb(native int, int32) conv.i8
    ldstr "native call of cold ldftn value" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_LDFTN_LOOP:
    ldftn int32 Prog::add1_cold(int32) ldloc.3 call int64 Prog::loop_cb(native int, int32)
    ldstr "native loop over ldftn value" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_LDFTN_GC:
    // The callback allocates and collects while the thread is in preemptive mode.
    ldc.i4.0 stloc.s rep
LGC: ldftn int32 Prog::add1_gc(int32) ldc.i4 41 call int32 Prog::call_cb(native int, int32) pop
    ldloc.s rep ldc.i4.1 add dup stloc.s rep ldc.i4 200 blt.s LGC
    ldftn int32 Prog::add1_gc(int32) ldc.i4 41 call int32 Prog::call_cb(native int, int32) conv.i8
    ldstr "native call of allocating/collecting ldftn value x200" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_LDFTN_THREAD:
    ldftn native int Prog::thread_start(native int) ldc.i4 41 conv.i ldloca.s q conv.u call int32 Prog::run_thread(native int, native int, native int) pop
    ldloc.s q conv.i8 ldstr "native thread on ldftn start routine returned" call void Prog::say(int64, string)
    ldc.i4.0 ret

T_LDFTN_GCSTRESS:
    ldftn int32 Prog::add1_refs(int32) stloc.1
    br.s GCS
T_DEL_GCSTRESS:
    ldftn int32 Prog::add1_refs(int32) call native int Prog::mkdel_i_i(native int) stloc.1
GCS:
    ldnull ldftn void Prog::churn() newobj instance void [mscorlib]System.Threading.ThreadStart::.ctor(object, native int)
    newobj instance void [mscorlib]System.Threading.Thread::.ctor(class [mscorlib]System.Threading.ThreadStart) dup ldc.i4.1 callvirt instance void [mscorlib]System.Threading.Thread::set_IsBackground(bool)
    callvirt instance void [mscorlib]System.Threading.Thread::Start()
    ldloc.1 ldloc.3 call int64 Prog::loop_cb(native int, int32)
    ldstr "loop with allocating callback under concurrent GC (expect n(n+1)/2)" call void Prog::say(int64, string)
    ldc.i4.1 stsfld bool Prog::stop
    ldc.i4.0 call int32 [mscorlib]System.GC::CollectionCount(int32) conv.i8 ldstr "gen0 collections" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_CMN_GC:
    ldc.i4.0 stloc.s r br.s CMNGC
T_CUN_GC:
    ldc.i4.1 stloc.s r
CMNGC:
    // A long native call made with a managed calli (cooperative mode) or calli unmanaged (preemptive),
    // while a managed thread allocates: does the GC wait for it?
    ldnull ldftn void Prog::churn() newobj instance void [mscorlib]System.Threading.ThreadStart::.ctor(object, native int)
    newobj instance void [mscorlib]System.Threading.Thread::.ctor(class [mscorlib]System.Threading.ThreadStart) dup ldc.i4.1 callvirt instance void [mscorlib]System.Threading.Thread::set_IsBackground(bool)
    callvirt instance void [mscorlib]System.Threading.Thread::Start()
    ldc.i4 200 call void [mscorlib]System.Threading.Thread::Sleep(int32)
    call native int Prog::get_loop_native() stloc.1
    ldc.i4.0 call int32 [mscorlib]System.GC::CollectionCount(int32) conv.i8 stloc.2
    ldsfld int64 Prog::churned stloc.s c0
    newobj instance void [System]System.Diagnostics.Stopwatch::.ctor() stloc.s sw ldloc.s sw callvirt instance void [System]System.Diagnostics.Stopwatch::Start()
    ldloc.s r brtrue.s CUN
    ldloc.3 ldloc.1 calli int64(int32) pop br.s CDONE
CUN: ldloc.3 ldloc.1 calli unmanaged cdecl int64(int32) pop
CDONE:
    ldloc.s sw callvirt instance void [System]System.Diagnostics.Stopwatch::Stop()
    ldloc.s sw callvirt instance int64 [System]System.Diagnostics.Stopwatch::get_ElapsedMilliseconds() ldstr "native call ms" call void Prog::say(int64, string)
    ldc.i4.0 call int32 [mscorlib]System.GC::CollectionCount(int32) conv.i8 ldloc.2 sub ldstr "gen0 collections during the native call" call void Prog::say(int64, string)
    ldsfld int64 Prog::churned ldloc.s c0 sub ldstr "allocations by the other thread during the native call" call void Prog::say(int64, string)
    ldc.i4.1 stsfld bool Prog::stop
    ldc.i4.0 ret
T_DEL:
    ldftn int32 Prog::add1(int32) call native int Prog::mkdel_i_i(native int) stloc.1
    ldloc.1 ldc.i4 41 call int32 Prog::call_cb(native int, int32) conv.i8
    ldstr "native call of delegate thunk" call void Prog::say(int64, string)
    ldloc.1 ldloc.3 call int64 Prog::loop_cb(native int, int32)
    ldstr "native loop over delegate thunk" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_DEL_NOATTR:
    ldnull ldftn int32 Prog::add1(int32) newobj instance void D_i_i_noattr::.ctor(object, native int)
    call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate)
    ldc.i4 41 call int32 Prog::call_cb(native int, int32) conv.i8
    ldstr "native call of delegate thunk without UnmanagedFunctionPointer" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_DEL_GC:
    ldftn int32 Prog::add1_gc(int32) call native int Prog::mkdel_i_i(native int) stloc.1
    ldc.i4.0 stloc.s rep
DGC: ldloc.1 ldc.i4 41 call int32 Prog::call_cb(native int, int32) pop
    ldloc.s rep ldc.i4.1 add dup stloc.s rep ldc.i4 200 blt.s DGC
    ldloc.1 ldc.i4 41 call int32 Prog::call_cb(native int, int32) conv.i8
    ldstr "native call of allocating/collecting delegate thunk x200" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_DEL_COLLECTED:
    // Not kept alive: only the native int survives; collect, then call.
    ldnull ldftn int32 Prog::add1(int32) newobj instance void D_i_i::.ctor(object, native int)
    call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) stloc.1
    call void [mscorlib]System.GC::Collect() call void [mscorlib]System.GC::WaitForPendingFinalizers() call void [mscorlib]System.GC::Collect()
    ldloc.1 ldc.i4 41 call int32 Prog::call_cb(native int, int32) conv.i8
    ldstr "native call of collected delegate's thunk" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_DEL_ID:
    ldftn int32 Prog::add1(int32) call native int Prog::mkdel_i_i(native int) stloc.1
    ldsfld class [mscorlib]System.Delegate[] Prog::keep ldc.i4.0 ldelem.ref call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) stloc.s q
    ldloc.1 ldloc.s q ceq conv.i8 ldstr "same delegate, two GetFunctionPointerForDelegate equal" call void Prog::say(int64, string)
    ldftn int32 Prog::add1(int32) call native int Prog::mkdel_i_i(native int) ldloc.1 ceq conv.i8
    ldstr "two delegates of one method equal" call void Prog::say(int64, string)
    ldloc.1 ldftn int32 Prog::add1(int32) ceq conv.i8 ldstr "thunk equals ldftn" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_DEL_THREAD:
    call native int Prog::native_thread_id() conv.i8 ldstr "main native thread id" call void Prog::say(int64, string)
    ldc.i4 5 stsfld int32 Prog::tsv
    ldnull ldftn native int Prog::thread_start(native int) newobj instance void D_p_p::.ctor(object, native int)
    call native int Prog::keepalive(class [mscorlib]System.Delegate) stloc.1
    ldloc.1 ldc.i4 41 conv.i ldloca.s q conv.u call int32 Prog::run_thread(native int, native int, native int) conv.i8
    ldstr "run_thread status" call void Prog::say(int64, string)
    ldloc.s q conv.i8 ldstr "native thread on delegate start routine returned" call void Prog::say(int64, string)
    ldsfld int32 Prog::tsv conv.i8 ldstr "main thread tsv after" call void Prog::say(int64, string)
    ldloc.1 ldc.i4 50 conv.i ldloca.s q conv.u call int32 Prog::run_thread(native int, native int, native int) pop
    ldloc.s q conv.i8 ldstr "second native thread returned" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_DEL_THREAD_THROW:
    ldnull ldftn native int Prog::thread_start_throw(native int) newobj instance void D_p_p::.ctor(object, native int)
    call native int [mscorlib]System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class [mscorlib]System.Delegate) stloc.1
    .try {
      ldloc.1 ldc.i4 41 conv.i ldloca.s q conv.u call int32 Prog::run_thread(native int, native int, native int) pop
      leave.s TTDONE
    } catch [mscorlib]System.Exception {
      callvirt instance string [mscorlib]System.Exception::get_Message() ldstr "caught on main thread: " call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string)
      leave.s TTDONE
    }
TTDONE:
    ldstr "after thread with throwing start routine" call void [mscorlib]System.Console::WriteLine(string)
    ldc.i4.0 ret
T_DEL_THROW:
    // An exception thrown in a callback, with native frames between it and a managed catch.
    ldftn int32 Prog::add1_throw(int32) call native int Prog::mkdel_i_i(native int) stloc.1
    .try {
      ldloc.1 ldc.i4 41 call int32 Prog::call_cb_twice(native int, int32) conv.i8
      ldstr "no exception?" call void Prog::say(int64, string)
      leave.s THDONE
    } catch [mscorlib]System.InvalidOperationException {
      callvirt instance string [mscorlib]System.Exception::get_Message() ldstr "caught across native frames: " call string [mscorlib]System.String::Concat(string, string) call void [mscorlib]System.Console::WriteLine(string)
      leave.s THDONE
    }
THDONE:
    ldloc.1 ldc.i4 1 call int32 Prog::call_cb_twice(native int, int32) conv.i8
    ldstr "callback still usable after" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_DEL_OPS:
    // A thunk stored in a struct that native code reads (sigaction, z_stream, WNDCLASS shape).
    ldftn int32 Prog::add1(int32) call native int Prog::mkdel_i_i(native int) stloc.1
    ldc.i4.s 16 call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(int32) stloc.s mem
    ldloc.s mem ldloc.1 stind.i
    ldloc.s mem ldc.i4.8 add ldc.i4 1000 stind.i4
    ldloc.s mem ldc.i4 41 call int32 Prog::call_ops(native int, int32) conv.i8
    ldstr "native call through struct field" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_DEL_QSORT:
    ldnull ldftn int32 Prog::cmp_int(native int, native int) newobj instance void D_cmp::.ctor(object, native int)
    call native int Prog::keepalive(class [mscorlib]System.Delegate) stloc.1
    // 10 ints, descending, sorted by native qsort with the managed comparator.
    ldc.i4.s 40 call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(int32) stloc.s mem
    ldc.i4.0 stloc.s i
QF: ldloc.s mem ldloc.s i ldc.i4.4 mul add ldc.i4.s 10 ldloc.s i sub stind.i4
    ldloc.s i ldc.i4.1 add dup stloc.s i ldc.i4.s 10 blt.s QF
    ldloc.s mem ldc.i4.s 10 conv.i ldc.i4.4 conv.i ldloc.1 call void Prog::do_qsort(native int, native int, native int, native int)
    ldc.i4.0 stloc.s i ldc.i4.0 conv.i8 stloc.2
QP: ldloc.2 ldc.i4.s 10 conv.i8 mul ldloc.s mem ldloc.s i ldc.i4.4 mul add ldind.i4 conv.i8 add stloc.2
    ldloc.s i ldc.i4.1 add dup stloc.s i ldc.i4.s 10 blt.s QP
    ldloc.2 ldstr "qsort result digits (expect 12345678910 folded)" call void Prog::say(int64, string)
    ldc.i4.0 ret

T_CALLI_NATIVE:
    // A native function pointer (dlsym/GetProcAddress shape) called from managed code.
    call native int Prog::get_native_fn() stloc.1
    ldc.i4 41 ldloc.1 calli unmanaged cdecl int32(int32) conv.i8
    ldstr "calli unmanaged cdecl of native pointer" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_CALLI_DEL:
    ldftn int32 Prog::add1(int32) call native int Prog::mkdel_i_i(native int) stloc.1
    ldc.i4 41 ldloc.1 calli unmanaged cdecl int32(int32) conv.i8
    ldstr "calli unmanaged cdecl of delegate thunk" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_CU_LDFTN:
    // Wrong kind 1: an unmanaged calli of a managed (ldftn) pointer.
    ldc.i4 41 ldftn int32 Prog::add1(int32) calli unmanaged cdecl int32(int32) conv.i8
    ldstr "calli unmanaged cdecl of ldftn value" call void Prog::say(int64, string)
    ldc.i4.0 ret
T_CM_NATIVE:
    // Wrong kind 2: a managed calli of a native pointer.
    call native int Prog::get_native_fn() stloc.1
    ldc.i4 41 ldloc.1 calli int32(int32) conv.i8
    ldstr "managed calli of native pointer" call void Prog::say(int64, string)
    ldc.i4.0 ret

T_BENCH:
    ldftn int32 Prog::add1(int32) stsfld native int Prog::fp_ldftn
    ldftn int32 Prog::add1(int32) call native int Prog::mkdel_i_i(native int) stsfld native int Prog::fp_del
    call native int Prog::get_native_fn() stloc.s q
    ldc.i4.0 stloc.s rep
BREP:
    // B0: direct call
    newobj instance void [System]System.Diagnostics.Stopwatch::.ctor() stloc.s sw ldloc.s sw callvirt instance void [System]System.Diagnostics.Stopwatch::Start()
    ldc.i4.0 conv.i8 stloc.2 ldc.i4.0 stloc.s i
B0: ldloc.2 ldloc.s i call int32 Prog::add1(int32) conv.i8 add stloc.2
    ldloc.s i ldc.i4.1 add dup stloc.s i ldloc.3 blt.s B0
    ldloc.s sw callvirt instance void [System]System.Diagnostics.Stopwatch::Stop()
    ldstr "direct call (managed)" ldloc.s sw callvirt instance int64 [System]System.Diagnostics.Stopwatch::get_ElapsedTicks() ldloc.3 call void Prog::bench(string, int64, int32)
    // B1: managed calli through ldftn value (today's C indirect call)
    newobj instance void [System]System.Diagnostics.Stopwatch::.ctor() stloc.s sw ldloc.s sw callvirt instance void [System]System.Diagnostics.Stopwatch::Start()
    ldc.i4.0 conv.i8 stloc.2 ldc.i4.0 stloc.s i
B1: ldloc.2 ldloc.s i ldsfld native int Prog::fp_ldftn calli int32(int32) conv.i8 add stloc.2
    ldloc.s i ldc.i4.1 add dup stloc.s i ldloc.3 blt.s B1
    ldloc.s sw callvirt instance void [System]System.Diagnostics.Stopwatch::Stop()
    ldstr "managed calli (ldftn value)" ldloc.s sw callvirt instance int64 [System]System.Diagnostics.Stopwatch::get_ElapsedTicks() ldloc.3 call void Prog::bench(string, int64, int32)
    // B2: calli unmanaged cdecl through the delegate thunk (managed -> native -> managed)
    newobj instance void [System]System.Diagnostics.Stopwatch::.ctor() stloc.s sw ldloc.s sw callvirt instance void [System]System.Diagnostics.Stopwatch::Start()
    ldc.i4.0 conv.i8 stloc.2 ldc.i4.0 stloc.s i
B2: ldloc.2 ldloc.s i ldsfld native int Prog::fp_del calli unmanaged cdecl int32(int32) conv.i8 add stloc.2
    ldloc.s i ldc.i4.1 add dup stloc.s i ldloc.3 blt.s B2
    ldloc.s sw callvirt instance void [System]System.Diagnostics.Stopwatch::Stop()
    ldstr "calli unmanaged -> delegate thunk" ldloc.s sw callvirt instance int64 [System]System.Diagnostics.Stopwatch::get_ElapsedTicks() ldloc.3 call void Prog::bench(string, int64, int32)
    // B3: calli unmanaged cdecl through a native pointer (managed -> native)
    newobj instance void [System]System.Diagnostics.Stopwatch::.ctor() stloc.s sw ldloc.s sw callvirt instance void [System]System.Diagnostics.Stopwatch::Start()
    ldc.i4.0 conv.i8 stloc.2 ldc.i4.0 stloc.s i
B3: ldloc.2 ldloc.s i ldloc.s q calli unmanaged cdecl int32(int32) conv.i8 add stloc.2
    ldloc.s i ldc.i4.1 add dup stloc.s i ldloc.3 blt.s B3
    ldloc.s sw callvirt instance void [System]System.Diagnostics.Stopwatch::Stop()
    ldstr "calli unmanaged -> native function" ldloc.s sw callvirt instance int64 [System]System.Diagnostics.Stopwatch::get_ElapsedTicks() ldloc.3 call void Prog::bench(string, int64, int32)
    // B4: native loop over a native function (baseline: what native qsort costs per callback)
    newobj instance void [System]System.Diagnostics.Stopwatch::.ctor() stloc.s sw ldloc.s sw callvirt instance void [System]System.Diagnostics.Stopwatch::Start()
    ldloc.3 call int64 Prog::loop_native(int32) pop
    ldloc.s sw callvirt instance void [System]System.Diagnostics.Stopwatch::Stop()
    ldstr "native loop -> native function" ldloc.s sw callvirt instance int64 [System]System.Diagnostics.Stopwatch::get_ElapsedTicks() ldloc.3 call void Prog::bench(string, int64, int32)
    // B5: native loop over the delegate thunk (native -> managed)
    newobj instance void [System]System.Diagnostics.Stopwatch::.ctor() stloc.s sw ldloc.s sw callvirt instance void [System]System.Diagnostics.Stopwatch::Start()
    ldsfld native int Prog::fp_del ldloc.3 call int64 Prog::loop_cb(native int, int32) pop
    ldloc.s sw callvirt instance void [System]System.Diagnostics.Stopwatch::Stop()
    ldstr "native loop -> delegate thunk" ldloc.s sw callvirt instance int64 [System]System.Diagnostics.Stopwatch::get_ElapsedTicks() ldloc.3 call void Prog::bench(string, int64, int32)
    ldloc.s rep ldc.i4.1 add dup stloc.s rep ldc.i4.2 blt BREP
    ldc.i4.0 ret
  }
}
