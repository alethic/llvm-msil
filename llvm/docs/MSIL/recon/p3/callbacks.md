# Phase 3 recon: native-callable function pointers (callbacks)

Key: `callbacks`. Topic: P3-B's native-callable function pointers (plan section 7, P3-B; Design.md 4.5, 4.4,
7.1; matrix row `native-callback`). Base: branch `msil`, HEAD `cc4c79787d6a` (M23). Scratch:
`C:\llvm-msil-scratch\p3\callbacks` (native test library, IL and C programs, logs). Supporting files (sources,
generators, scrubbed logs): [callbacks-files/](callbacks-files/).

Runtimes measured: .NET Framework 4.8 (`4.0.30319.42000`, column **fx**), .NET 8.0.33 (**n8**) and .NET 10.0.14
(**n10**) on Windows 11 x64, .NET 8.0.31 (**w8**) and .NET 10.0.13 (**w10**) under WSL Ubuntu (the harness's
private runtimes, `msil/utils/msil-wsl-dotnet.sh`). Every experiment is one IL program assembled by Framework
ilasm 4.8 (`-exe -pe64 -x64`) against `[mscorlib]` only, unless it says otherwise, so the same image ran on all
five. Native side: `callbacks-files/native/cbnative.c`, built as `cbnative.dll` (clang-cl, `/MD`) and
`libcbnative.so` (`build\bin\clang --target=x86_64-unknown-linux-gnu --sysroot=<glibc sysroot>` + `ld.lld`).
The host is shared and was loaded during the runs: timings are given as the minimum over three in-process
rounds (median in parentheses where it matters), and their spread is large.

---

## 0. Summary

1. **Today a C function pointer is the managed `ldftn` value** stored once in `'<msil>fn_<f>'` (M9) and
   called with a managed `calli`. Native code that calls such a value enters managed code without a
   reverse-P/Invoke transition. It *appears* to work in simple cases on every runtime (qsort with a C
   comparator through an explicit `import_module` sorts correctly today on all five runtimes), but it is
   unsafe: with a second thread allocating, 300,000 native calls of an allocating `ldftn` callback crashed
   Framework (SIGSEGV), .NET 8 Windows (`Fatal error. Internal CLR error. (0x80131506)`), .NET 8 WSL
   (SIGSEGV), and **silently corrupted heap data** on .NET 10 Windows; a native thread whose start routine is
   an `ldftn` value crashes Framework. msil-ld's link error for the 46 names of the callback list (plan
   5.10.14) is therefore justified, and the "known gap" (callbacks not on the list) is a real hazard.
2. The reverse direction is broken too: a **native function pointer** (`GetProcAddress`/`dlsym` result)
   called with today's managed `calli` runs native code in cooperative GC mode. It returns the right value,
   but while it runs no other thread can complete a GC: in a 1.8-2.9 s native call, the other thread managed
   1-2 gen0 collections on CoreCLR (vs 755-2,644 with `calli unmanaged cdecl`), and 98 vs 2,304 on Framework.
3. **Kept-alive delegates + `Marshal.GetFunctionPointerForDelegate` work on all five runtimes** for every
   case measured: direct and looped native calls, GC stress, struct fields read by native code, native
   qsort, native threads (CreateThread/pthread_create), pthread key destructors, struct-by-value parameters
   and results (16 and 24 bytes). A private delegate type with only `.ctor` and `Invoke` suffices; the
   `UnmanagedFunctionPointer(Cdecl)` attribute is not needed on x64. One delegate gives one pointer
   (`GetFunctionPointerForDelegate` twice: equal), two delegates of one method give two pointers, so the
   delegate must be **one per function**, created once and kept alive (a collected delegate's pointer
   crashed Framework with a NullReferenceException; CoreCLR happened not to reuse the stub yet).
4. **`[UnmanagedCallersOnly]` + `ldftn` works on .NET 8 and 10** (Windows and Linux): stable identity across
   2M native calls, a GC and tier-up; a direct `call` *or a managed `calli`* of the UCO method is
   `Fatal error. Invalid Program: attempted to call a UnmanagedCallersOnly method from managed code`, so the
   UCO method must be a separate thunk that calls the C function, and the managed fast path must call the C
   function, never the thunk. CoreCLR recognizes the attribute **by name**: a private attribute class of
   the same full name in the output, or a TypeRef to `[mscorlib]System.Runtime.InteropServices.
   UnmanagedCallersOnlyAttribute` (which the .NET 8/10 mscorlib facade does not forward), both work.
   Framework ignores the attribute (the `ldftn` value stays managed), so UCO must stay a switch.
5. **`calli unmanaged cdecl` works on all five runtimes**, for native pointers, delegate thunks and UCO
   thunks. Cost (optimized code): 3.2-4.1 ns to a native function, 17-30 ns round trip to a delegate thunk,
   9-31 ns to a UCO thunk; managed `calli` is 2.8-4.6 ns.
6. **Native-to-managed cost per call**: delegate thunk 17-27 ns (Framework 26.5), UCO 6.6-24 ns, native
   1.6-2.1 ns. Native qsort of 1M ints with a C comparator: 157-252 ns per element with a native
   comparator, 517-793 with a delegate thunk (3-4.5x), 368-510 with UCO (about 2x).
7. **The "one range compare" fast path of Design.md 4.5 is not implementable**: delegate thunks come from a
   runtime-wide stub heap (consecutive thunks 0x40 or 0x18 apart, with gaps, interleaved with other
   allocations) and UCO entry points are precodes interleaved with other methods' precodes. An **exact
   lookup** in a per-assembly open-addressing table (native-callable value -> managed `ldftn` value) costs
   1-3 ns over a managed `calli` on a hit (3.9-6.5 ns total) and makes a miss (a native pointer) cost
   7.5-11 ns.
8. **The fast path is needed for correctness, not only speed**: an exception (P3-B's `longjmp`) thrown in a
   callback crosses the native frames and is caught on Windows (all three runtimes, delegate and UCO), but
   aborts the process on Linux (.NET 8 and 10, both thunk kinds). Calls from C to C through a pointer must
   therefore never pass through a thunk; Lua's `lua_CFunction` calls and `lua_error`'s `longjmp` depend on it.
9. **Threads**: a native thread (CreateThread, pthread_create) entering through either thunk kind gets a
   CLR thread on first entry (background, new ManagedThreadId), fresh `[ThreadStatic]` values, and an
   unhandled exception on it ends the process. pthread key destructors run after the CLR detached the thread
   and re-attach it (new ManagedThreadId). **Asynchronous signals** whose handler is a thunk are fatal on
   Linux when they interrupt managed code (`attempted to call a UnmanagedCallersOnly method from managed
   code`); synchronous `raise` works.
10. **Start-up cost**: creating 1,000 delegate thunks in the module initializer (JIT of the initializer
    included) costs 39-226 ms straight-line, 7-89 ms table-driven (Framework 44-89), against 3-16 ms for
    Phase 2's `ldftn` and 2-12 ms for UCO thunks. Typical programs have few address-taken functions (zlib 5,
    tinf 5, LZ4 2, BLAKE2 1), Lua a few hundred.

**Recommendation.** Keep Design.md 4.5's representation (option A): every `'<msil>fn_<f>'` holds a
native-callable entry point, created eagerly in phase 1 of the module initializer: a kept-alive delegate
thunk by default (runs on Framework 4.8 and modern .NET alike), the `ldftn` of an `[UnmanagedCallersOnly]`
thunk under a link-time switch (`--uco-thunks`, attribute bound from the references). The address of a
native function is its native entry point (the M20 data-import helper). Every C indirect call becomes an
exact dispatch: `m = '<msil>fnmgd'(p)`; a hit is a managed `calli` of the C function, a miss a
`calli unmanaged cdecl` of `p`. Retire the callback-list link error (keep a warning for the `signal` family
on Linux triples). Sections 5 and 6 give the shapes, the code sites and the decisions.

---

## 1. Today (Phase 2 as built)

### 1.1 Representation in one module

- **Address fields.** Every function whose address is used (FUNC_ADDR in code, or the target of a data
  relocation) gets `.field private static initonly native int '<msil>fn_<IL name>'` (prefix
  `MSIL::FnAddrFieldPrefix`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILMCExpr.h:48`; plan section 12, M9 row
  of 2026-10-15). The module initializer stores each field once with `ldftn <method>` and `stsfld`:
  `llvm/lib/CLI/Trailer.cpp:329` `emitPhase1`, lambda `AddFunctionAddresses` at `:338-347`, fed by
  `TrailerInput::FnAddrFields` (`llvm/include/llvm/CLI/Trailer.h:176-182`, pairs (field, method) in
  canonical order). `-S`/llc fill it in `MSILAsmPrinter.cpp:1568-1645` (`FnAddrField`, then
  `In.FnAddrFields.push_back` at `:1645`); msil-ld in `lld/MSIL/Relocations.cpp:388-411`
  (`SymbolBinder::addFnAddrFields`). Both go through the one trailer builder, so `-S` and linked outputs
  agree.
- **Reading an address.** `FUNC_ADDR` (`llvm/lib/Target/MSIL/MSILInstrInfo.td:237-257`) prints
  `ldsfld native int '<msil>fn_f'`; `MSILMCInstLower::lowerFunctionAddress`
  (`MSILMCInstLower.cpp:326-366`) makes the `MSILMemberRefExpr::createFnAddr` operand; in a CIL object it is
  a placeholder MemberRef `native int [msil.extern]'<msil>FnAddr'::'f'` (plan 5.8.3 R2) that msil-ld binds to
  the output's field. Data relocations of kind `FnAddr64`/`FnAddr32` (`llvm/include/llvm/BinaryFormat/
  MSILLink.h:164-176`) store the field's value. The address of a library function by name
  (TargetExternalSymbol) is the error `the address of library function '<name>' is not supported for MSIL`
  (`MSILMCInstLower.cpp:351-364`).
- **Undefined (native) functions.** The field of a function bound to a native library holds the `ldftn` of
  its canonical P/Invoke stub (plan 5.10.11, "one canonical stub per symbol for its `'<msil>fn_<name>'`
  address field"), a managed method. So `&puts` is a managed pointer, and it differs between assemblies
  (exports-multiasm 4.3).
- **Calls through pointers.** `LowerCall` (`MSILISelLoweringCall.cpp:472-480`) keeps the callee in a
  register; `emitCallResults` makes `CALL_INDIRECT` (`:559`; `MSILInstrCall.td:65`, `calli`).
  `MSILMCInstLower::lowerCallIndirectSignature` (`MSILMCInstLower.cpp:423-452`) builds the StandAloneSig
  from the MI's register classes and hard-codes `Sig.CC = MSIL::CallConv::Managed` (`:448`, comment
  "Native callers and unmanaged calli wait for phase 3"). The encoder already supports the unmanaged
  conventions: `MSIL::CallConv::{Managed, UnmanagedCdecl, UnmanagedStdcall}`
  (`MCTargetDesc/MSILMCTypeUtilities.h:42-46`), printed by `MSILAsmPrinter.cpp:281` and encoded as `SIG_C`
  / `SIG_STDCALL` by `encodeSignature(..., IsCallSite)` (`MCTargetDesc/MSILObjectWriter.cpp:87-110`).
  Nothing produces them yet.

### 1.2 Across objects (M20) and libraries (M22)

- msil-ld owns one `'<msil>fn_'` field per resolved method (aliases share their aliasee's), placed in the
  order of the methods' first FUNCTION symbol over the objects in link order; Framework identity
  (`table[1] == table[2]`) holds program-wide because there is one `ldftn` per method (plan 5.8.3 R2,
  5.9.10).
- A `-shared` library publishes, per exported function, `public static native int '<msil>addr_<f>'()`
  returning its `'<msil>fn_'` field (`lld/MSIL/Writer.cpp:364-420`, `addExportMembers`); the fields live in
  the phase-1 class `'<msil>Data'`. An importer never takes the address with its own `ldftn` (which on
  Framework can differ from the exporter's: exports-multiasm E2): code calls the accessor, and data
  relocations against library functions become `CrossReloc`s applied in phase 2
  (`Relocations.cpp:590-629`, `getCrossRelocations`). This is the hook P3-B needs: a native-callable value
  "is a thunk that only the exporter can own" (exports-multiasm 4.3).
- Calls through pointers are not signature-checked across objects (Status.md, M20), as natively.

### 1.3 The diagnostics (matrix row `native-callback`)

- **Callback list:** `lld/MSIL/NativeLibraries.cpp:80-125` (`nativeCallbackFunctions[]`, sorted, 46 names:
  `CreateThread`, `__sysv_signal`, `_beginthread(ex)`, `_lfind(_s)`, `_lsearch(_s)`, `_onexit`,
  `at_quick_exit`, `atexit`, `bsd_signal`, `bsearch(_s)`, `call_once`, `ftw(64)`, `lfind`, `lsearch`,
  `nftw(64)`, `on_exit`, `pthread_atfork`, `pthread_create`, `pthread_key_create`, `pthread_once`,
  `qsort(_r, _s)`, `scandir(64)`, `scandirat(64)`, `sigaction`, `signal`, `sigset`, `ssignal`,
  `sysv_signal`, `tdelete`, `tdestroy`, `tfind`, `thrd_create`, `tsearch`, `tss_create`, `twalk(_r)`),
  `bool lld::msil::isNativeCallbackFunction(StringRef)` at `:127-134`.
- **LTO path:** `lld/MSIL/LTO.cpp:739-749`: a live reference that native lookup binds to a listed name is
  `native function '<name>' (<module>) takes a C function pointer; passing functions to native code needs
  phase 3 thunks`; unreferenced declarations on the list are not bound quietly (`:707-718`).
- **Object path:** `lld/MSIL/Signatures.cpp:123-128`, the same text, unless the declaration has an explicit
  `import_module` (exempt, unchecked; the documented workaround).
- **Tests:** `msil/test/M2/08-semantics/native-callback.test` (qsort with a C comparator, `obj` and `lto`,
  -O0/-O2, both triples) pins the diagnostic; `matrix.txt:78` names the transcript as P3-B's.
- **Known gap** (Status.md section 3): a function address passed to any other native function (a
  library's own callback API, `EnumWindows`, `SetConsoleCtrlHandler`, ...) is not diagnosed. Also not on the
  list: glibc's `__cxa_thread_atexit_impl` (Rust std's TLS destructors, *unverified*), `dl_iterate_phdr`,
  `pthread_cleanup_push` internals, `SetUnhandledExceptionFilter`, every Win32 `*Proc` API.

### 1.4 What today's output does at run time (measured)

The same C sources were compiled by the shared build (`clang --target=<triple>-msil -O2 -ffreestanding`,
explicit `import_module` imports so the callback check does not apply; `callbacks-files/c/`):

| program | native (x64 clang) | fx | n8 | n10 | w8 | w10 |
|---|---|---|---|---|---|---|
| `qs.c`: qsort with a C comparator | `-7 -7 0 3 19 42 256 1000` | same | same | same | same | same |
| `fp.c`: qsort, C->C through data, identity, `abs` from GetProcAddress/dlsym called from C | `-17510 21 1 5` | same | same | same | same | same |
| `thr.c`: CreateThread / pthread_create with a C start routine | `42 43 1` | **SIGSEGV** | same | same | same | same |

So today's shape "works" whenever no GC happens during the callback, which is why it must stay an error
until P3-B: the IL experiments of section 2.1 make the hazard visible.

---

## 2. CLR behaviour (measured)

All logs: `callbacks-files/results/matrix.txt` (sections 2.1-2.5), `extras.txt` (2.6-2.10, 1.4, 5.7),
`bench-summary.txt`/`bench-raw.txt` (2.11), `startup.txt` (2.12). IL: `callbacks-files/il/cb.il` (one test
per process, `cb.exe <test> [n]`), `gen_uco.py` (`uco.il`, `uconame.il`, `ucomscorlib.il`), and the
generators named below.

### 2.1 An `ldftn` value called by native code

| test (`cb.il`) | fx | n8 | n10 | w8 | w10 |
|---|---|---|---|---|---|
| `ldftn`: `call_cb(ldftn add1, 41)` after `add1` ran | 42 | 42 | 42 | 42 | 42 |
| `ldftn-cold`: target never ran (CoreCLR: a precode) | 42 | 42 | 42 | 42 | 42 |
| `ldftn-loop`: 1M native calls | ok | ok | ok | ok | ok |
| `ldftn-gc`: callback allocates and calls `GC.Collect()`, 200 times, single thread | 42 | 42 | 42 | 42 | 42 |
| `ldftn-gcstress`: 300k native calls of a callback that keeps 33 object references in locals across allocations while a second managed thread allocates | **SIGSEGV** | **`Fatal error. Internal CLR error. (0x80131506)`**, SIGSEGV | **`CORRUPTION: boxed value changed`**, wrong sum 44999140416 (expected 45000150000) | **SIGSEGV** | passed (by luck) |
| `ldftn-thread`: a native thread (CreateThread/pthread_create in `cbnative`) runs an `ldftn` start routine | **SIGSEGV** | runs | runs | runs | runs |
| same with a delegate thunk (`del-gcstress`) | correct, 1,212 gen0 GCs | correct, 398 | correct, 396 | correct, 416 | correct, 327-486 |

The managed code runs while the runtime believes the thread is in preemptive mode (it left managed code
through the P/Invoke of `call_cb`), so a GC triggered by another thread neither waits for it nor reports its
frames. On CoreCLR the `ldftn` of a static method is a precode slot (0x18 apart, `extras.txt` "thunk and
ldftn addresses"), on Framework a precode or the compiled code (M9). Neither is a native entry point.

### 2.2 Kept-alive delegates and `Marshal.GetFunctionPointerForDelegate`

Delegate type as the backend would emit it (no `BeginInvoke`/`EndInvoke`):

```
.class private auto ansi sealed D_i_i extends [mscorlib]System.MulticastDelegate
{
  .method public hidebysig specialname rtspecialname instance void .ctor(object 'object', native int 'method') runtime managed {}
  .method public hidebysig newslot virtual instance int32 Invoke(int32 x) runtime managed {}
}
// thunk: ldnull; ldftn int32 add1(int32); newobj instance void D_i_i::.ctor(object, native int);
//        keep it in a static Delegate[]; call native int [mscorlib]System.Runtime.InteropServices.Marshal::
//        GetFunctionPointerForDelegate(class [mscorlib]System.Delegate)
```

| test | fx | n8 | n10 | w8 | w10 |
|---|---|---|---|---|---|
| `del`: one call (42) and 1M native calls | ok | ok | ok | ok | ok |
| `del-noattr`: no `UnmanagedFunctionPointer` attribute (x64) | ok | ok | ok | ok | ok |
| `del-gc`, `del-gcstress` (2.1) | ok | ok | ok | ok | ok |
| `del-collected`: delegate not kept alive, `GC.Collect` x2 + `WaitForPendingFinalizers`, then native call | **NullReferenceException, SIGSEGV** | "ok" | "ok" | "ok" | "ok" |
| `del-identity`: same delegate, two GFPFD | equal | equal | equal | equal | equal |
| two delegates of one method | differ | differ | differ | differ | differ |
| thunk vs `ldftn` of the method | differ | differ | differ | differ | differ |
| `del-ops`: thunk stored in a struct that native code reads (`sigaction`/`z_stream` shape) | 1042 | 1042 | 1042 | 1042 | 1042 |
| `del-qsort`: native qsort, managed comparator | sorted | sorted | sorted | sorted | sorted |

"ok" on CoreCLR after collection is luck: CoreCLR frees the stub of a collected delegate lazily. The
delegate must be kept alive for the life of the process; a static `System.Delegate[]` (or one static field
per thunk) does it, no `GCHandle` is needed. The attribute `UnmanagedFunctionPointer(Cdecl)` (blob
`01 00 02 00 00 00 00 00`) changes nothing on x64 (the default `Winapi` is the platform convention); it
matters for i686 (phase 5).

### 2.3 `[UnmanagedCallersOnly]` + `ldftn` (modern .NET only)

`uco.il` binds `[System.Runtime.InteropServices]System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute`
(8.0.0.0); the thunk is a separate method that calls the C function:

```
.method assembly static int32 '<msil>uco_add1'(int32 x) cil managed
{ .custom instance void [System.Runtime.InteropServices]System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute::.ctor() = ( 01 00 00 00 )
  .maxstack 1 ldarg.0 call int32 Prog::add1(int32) ret }
// value: ldftn int32 Prog::'<msil>uco_add1'(int32)
```

| test | n8 | n10 | w8 | w10 |
|---|---|---|---|---|
| `uco`: one call and 1M native calls | ok | ok | ok | ok |
| `uco-cdecl`: `CallConvs = { typeof(CallConvCdecl) }` (blob of recon/p2 bcl-binding 4.4) | ok | ok | ok | ok |
| `uco-cold`: thunk never ran | ok | ok | ok | ok |
| `uco-identity`: `ldftn` before and after 2M native calls, `GC.Collect`, 200 ms sleep, and in a method JIT-compiled later | equal | equal | equal | equal |
| UCO thunk vs `ldftn` of the C function | differ | differ | differ | differ |
| `uco-direct`: `call '<msil>uco_add1'` from managed code | **Fatal error. Invalid Program: attempted to call a UnmanagedCallersOnly method from managed code** (exit 6 / SIGABRT) | same | same | same |
| `uco-managed-calli`: managed `calli` through the UCO pointer | same fatal error | same | same | same |
| `uco-calli`: `calli unmanaged cdecl` through the UCO pointer | 42 | 42 | 42 | 42 |
| `uco-thread`, `uco-qsort` | ok | ok | ok | ok |

**Binding by name** (`extras.txt`, "UnmanagedCallersOnly bound by name"): `uconame.exe` (references only
`mscorlib`, defines its own `private sealed System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute :
System.Attribute`) and `ucomscorlib.exe` (TypeRef `[mscorlib]System.Runtime.InteropServices.
UnmanagedCallersOnlyAttribute`; `llvm-cil-dump` of the 8.0.33 and 10.0.14 `mscorlib.dll` facades shows
1,381 and 1,386 forwarders, none for it) both behave as the real attribute on all four CoreCLR runs
(`uco-direct` is the fatal error, so the runtime saw the attribute). On Framework both images run
`uco-direct` as an ordinary call: the attribute is ignored and the `ldftn` value stays managed (2.1's
hazard). So UCO cannot be a default for an image that must run on Framework 4.8, and CoreCLR does not need
a reference that defines the attribute (a policy question, decision CB3).

### 2.4 `calli unmanaged cdecl`, and the wrong-kind calls

| test | fx | n8 | n10 | w8 | w10 |
|---|---|---|---|---|---|
| `calli-native`: `calli unmanaged cdecl int32(int32)` of `get_native_fn()` (the dlsym/GetProcAddress shape) | 42 | 42 | 42 | 42 | 42 |
| `calli-del`: same through a delegate thunk | 42 | 42 | 42 | 42 | 42 |
| `calli-unmanaged-on-ldftn` (wrong kind: unmanaged calli of a managed pointer) | "42" | "42" | "42" | "42" | "42" |
| `calli-managed-on-native` (wrong kind: managed calli of a native pointer; what `fp.c`'s `nabs(-5)` does today) | "42" | "42" | "42" | "42" | "42" |
| `calli-managed-native-gc`: managed `calli` of `loop_native(1e9)` (1.8-2.9 s) while another thread allocates: gen0 GCs / allocations of the other thread during the call | 98 / 4.7M | 1 / 25k | 1 / 61k | 2 / 63k | 1 / 56k |
| `calli-unmanaged-native-gc`: the same with `calli unmanaged cdecl` | 2,304 / 109M | 2,644 / 126M | 2,163 / 103M | 755 / 36M | 779 / 37M |

Both wrong kinds return the right value, so only the GC experiments show them: an unmanaged `calli` of a
managed pointer is 2.1's hazard, a managed `calli` of native code blocks every GC for its duration (a
deadlock if the native code waits for another managed thread). `calli unmanaged` itself works on
Framework 4.8 (it has existed since CLR 1.0; cil-runtime.md e07 had only checked CoreCLR).

### 2.5 Exceptions that cross native frames

`del-throw` / `uco-throw`: a managed `try`/`catch` calls native `call_cb_twice`, whose callback throws
`InvalidOperationException` on its second call.

| | fx | n8 | n10 | w8 | w10 |
|---|---|---|---|---|---|
| delegate thunk | caught; callback usable afterwards | caught | caught | **SIGABRT** "Unhandled exception" | **SIGABRT** |
| UCO thunk | - | caught | caught | **SIGABRT** | **SIGABRT** |
| exception on a native thread's start routine (`del-thread-throw`) | process ends (unhandled) | ends | ends | SIGABRT | SIGABRT |

On Windows the exception unwinds through the native frames (SEH); on Linux the runtime cannot and aborts.
Consequences: (a) `longjmp` implemented as a BCL exception (Design.md 4.6) works out of a callback that
native code called only on Windows; (b) **a C-to-C call through a pointer must not go through a thunk**,
or a `longjmp` through it aborts on Linux.

### 2.6 Threads

`del-thread`, `uco-thread` (`cb.il`, `uco.il`) and `thr.c` compiled by the toolchain then rewritten by
`patch_fp.py` (section 5.7):

- A native thread created by `CreateThread` (Windows) or `pthread_create` (Linux) whose start routine is a
  thunk enters managed code; the CLR creates the `Thread` on first entry: `IsBackground = True`, a new
  `ManagedThreadId` per thread (3, 4 on Framework; 4, 5 on CoreCLR), `[ThreadStatic]` fields start at their
  default (0 in the thread, while the main thread's value stayed 5). P3-A's lazily allocated TLS block and
  shadow stack therefore work on such threads unchanged. Background threads do not keep the process alive,
  which matches C (returning from `main` ends the process).
- `thr.c` (`thr-win-delegate.exe`, `thr-lnx-delegate.exe`, `-uco`): `42 43 1` on every runtime, the native
  transcript; Phase 2's shape crashes Framework (1.4).
- **pthread key destructors** (`key.il`, Linux): a destructor registered with `pthread_key_create` as a
  thunk runs at thread exit for a thread that never ran managed code (managed thread 4) and for one whose
  start routine was managed (it ran as managed thread **6**, not 5: the CLR had already detached the thread
  and re-attached it). Destructors work, but `[ThreadStatic]` state set by the start routine is gone by
  then (relevant to `tss_create` destructors and to P3-A's TLS block cleanup).
- An unhandled exception on such a thread ends the process (2.5), as it does on CLR threads.

### 2.7 Signals (Linux)

`sig.il`: `signal(SIGUSR1, <thunk>)`. Synchronous delivery (`raise`, called through a P/Invoke, so the
thread is in preemptive mode): the handler runs, both thunk kinds, .NET 8 and 10. Asynchronous delivery
(`pthread_kill` from a native thread after 200 ms while the main thread runs an allocating managed loop):
**`Fatal error. Invalid Program: attempted to call a UnmanagedCallersOnly method from managed code`**, SIGABRT,
both thunk kinds (CoreCLR implements the delegate's reverse stub as an UnmanagedCallersOnly IL stub too).
A C program that installs a handler for an asynchronous signal (SIGALRM, SIGINT, SIGCHLD, SIGWINCH, ...) and
receives it while the thread runs C code compiled to IL ends the process. Handlers for SIGSEGV/SIGBUS/
SIGFPE would also replace the runtime's own (NullReferenceException handling; *unverified*, not tested).
Windows (UCRT `signal`) runs SIGINT handlers on a new thread and `raise` synchronously (*unverified*, from
the UCRT documentation, not measured).

### 2.8 Struct-by-value callbacks

`gen_sbv.py` (`sbv.il`): native code calls `long long f(struct s16)`, `long long f(struct s24)`,
`struct s16 g(long long)` and `struct s24 g(long long)` (16 and 24 bytes of `long long`) through thunks whose
signatures use explicit-layout value types (`.pack 1 .size 16|24`, `int64` fields). All four give the
native results (105, 1605, 5006, 5006007) on all five runtimes with delegates and on the four CoreCLR runs
with UCO: the runtime applies the native ABI (SysV: 16 bytes in two registers, 24 in memory; result in
rax:rdx or through a hidden pointer; Win64: by reference to a copy, result through a hidden pointer).

### 2.9 Where thunks live (is a range check possible?)

`gen_addr.py` (`addrfx.exe`, `addruco.exe`) prints six consecutive delegate thunks and `ldftn` values:

- Framework: thunks `0x15608CC, 0x156091C, ...` (0x50 apart, 4-byte aligned, low addresses);
  `ldftn` values `0x7FF8A8000488, ...490` (8 apart).
- .NET 8: thunks `0x7FF7F84C3024, ...3064, ...30A4` (0x40 apart, 4-byte aligned); `ldftn` precodes 0x18
  apart; UCO `ldftn` values interleaved with the other methods' precodes.
- .NET 10: thunk f0 at `...3618`, f1 at `...39C0` (a gap), then 0x18 apart.

Thunks come from a runtime-wide stub heap in allocation order, with gaps and other allocations between
them, and are shared by every assembly in the process. No per-assembly range exists, so Design.md 4.5's
"one range compare" cannot identify the assembly's own pointers; an exact table lookup can (2.11).

### 2.10 A process-wide slot without a shared type

`reg.il`: `AppDomain.CurrentDomain.GetData("<msil>fntables")`/`SetData` under
`Monitor.Enter(String.Intern("<msil>fntables"))`, holding a boxed `IntPtr` to unmanaged memory, works with
`[mscorlib]` references on all five runtimes. It is the only BCL-only way found to let every assembly built
by this toolchain find the others' lookup tables (decision CB4).

### 2.11 Cost per call

`gen_bench.py` (`bench.il`, `benchuco.il`), 20M calls per case, three in-process rounds; Framework with
default JIT, CoreCLR with `DOTNET_TieredCompilation=0` (the default-tiering table is in
`bench-summary.txt`: same picture, P/Invoke transitions cost more in Tier-0 code, for example
`calli unmanaged -> native` 10-23 ns). Minimum over rounds (median):

| case (ns per call) | fx | n8 | n10 | w8 | w10 |
|---|---|---|---|---|---|
| direct call | 0.5 (0.6) | 1.3 (1.4) | 1.6 (1.6) | 2.8 (4.2) | 1.8 (1.9) |
| managed `calli` (today) | 3.8 (4.8) | 2.9 (3.1) | 2.8 (3.4) | 4.6 (5.2) | 3.1 (3.1) |
| `calli unmanaged` -> delegate thunk (option A1, every indirect call) | 29.4 (37.1) | 25.3 (28.2) | 16.6 (16.7) | 29.6 (35.6) | 27.1 (28.3) |
| `calli unmanaged` -> UCO thunk (A1 with the switch) | - | 8.9 (10.7) | 9.9 (10.2) | 16.2 (17.1) | 30.8 (32.6) |
| `calli unmanaged` -> native function | 3.6 (4.5) | 3.3 (3.9) | 3.2 (3.6) | 4.1 (4.2) | 4.0 (4.5) |
| A2: inline first table probe, hit, + managed `calli` | 3.9 (4.0) | 6.0 (10.2) | 4.9 (5.2) | 5.1 (8.6) | 6.5 (7.5) |
| A2: inline first probe, miss (native pointer), + `calli unmanaged` | 9.0 (9.4) | 7.5 (7.7) | 8.3 (21.0) | 10.2 (17.7) | 11.2 (11.8) |
| A2: lookup through a helper method, hit | 8.1 (8.2) | 5.5 (5.7) | 5.0 (5.4) | 5.9 (6.8) | 7.6 (7.7) |
| A2: lookup through a helper method, miss | 7.5 (7.9) | 6.6 (8.5) | 6.8 (7.0) | 11.4 (11.5) | 8.2 (10.2) |
| per-call-site cache hit + managed `calli` | 3.3 (3.3) | 4.9 (4.9) | 3.6 (3.8) | 5.0 (6.5) | 4.9 (7.7) |
| native loop -> native function | 1.6 (1.8) | 1.6 (1.8) | 1.8 (1.9) | 1.7 (2.0) | 2.1 (4.0) |
| native loop -> delegate thunk | 26.5 (31.7) | 22.9 (25.0) | 16.6 (21.2) | 24.7 (26.4) | 21.8 (26.8) |
| native loop -> UCO thunk | - | 8.5 (9.3) | 6.6 (7.0) | 11.5 (12.9) | 23.6 (25.9) |
| native loop -> `ldftn` value (unsafe; scale only) | 2.8 (3.0) | 4.1 (4.2) | 2.7 (4.7) | 2.8 (2.9) | 3.1 (4.8) |
| native `qsort` of 1M ints, ns per element: native comparator | 174 (190) | 181 (181) | 157 (209) | 231 (240) | 252 (289) |
| ... delegate-thunk comparator | 793 (839) | 698 (744) | 517 (705) | 735 (960) | 572 (628) |
| ... UCO comparator | - | 413 (525) | 368 (397) | 510 (542) | 770 (784) |

### 2.12 Start-up cost of publishing native-callable addresses

`gen_init.py` (phases inside one warm `Main`; `startup.txt`): per 1,000 functions of one signature,
`newobj` of the delegates 0.07-0.2 ms and `GetFunctionPointerForDelegate` 0.7-1.6 ms; the first native call
through each thunk costs 23-294 ms per 1,000 (the target's JIT plus the reverse stub; paid by any route at
first call); the first `GetFunctionPointerForDelegate` of a new delegate type costs about 0.5 ms on
Framework (stub generated then), while CoreCLR defers the stub to the first call (0.35-0.85 ms per
signature).

`gen_cctor.py` (each initializer method's first call, JIT included; two to seven runs per runtime; ms per
1,000 functions, median of the runs; single runs ranged up to 2-9x the median on the loaded host):

| initializer shape | fx | n8 | n10 | w8 | w10 |
|---|---|---|---|---|---|
| A: `ldftn` + `stsfld` (Phase 2) | 5.8 | 4.3 | 3.0 | 15.5 | 15.5 |
| B: straight-line `ldnull; ldftn; newobj; GetFunctionPointerForDelegate; stsfld` | 128 | 53 | 64 | 100 | 93 |
| E: B split into 20 methods of 50 | 138 | 41 | 94 | 54 | 66 |
| C: `ldftn` into a table, then one loop creating the delegates | 55 | 9.1 | 9.7 | 18.1 | 18.0 |
| U: `ldftn` of UCO thunks + `stsfld` | - | 3.1 | 3.6 | 3.5 | 2.6 |

Address-taken functions in the exit corpus (`'<msil>fn_'` fields in the O2 images built by the phase gate):
zlib 5, tinf 5, LZ4 2, CoreMark 2 at -O0 and 0 at -O2, BLAKE2 1, jsmn/KISS FFT/Monocypher/TweetNaCl 0. Lua registers a few
hundred C functions in `luaL_Reg` tables (data relocations), so its default start-up cost would be a few
to about 25 ms on Framework, 2-5 ms on CoreCLR (estimate from the table, not measured on Lua).

---

## 3. Identity, equality and the fast path

C requires one value per function (C11 6.5.9p6), comparable and storable anywhere, and callable by whoever
holds it. Native code needs a native entry point; the CLR offers two (2.2, 2.3), both distinct from the
`ldftn` value, so C code that calls through a pointer must either always hold one kind or find out which
kind it holds.

**Option A: every function pointer is native-callable** (Design.md 4.5).
- A1, every indirect call is `calli unmanaged cdecl`: correct for native pointers, thunks of any assembly
  and C-to-C calls alike, but each C-to-C indirect call costs a double transition (17-30 ns with delegate
  thunks, 9-31 ns with UCO, vs 2.8-4.6 ns), and a `longjmp` through it aborts on Linux (2.5). Rejected.
- **A2 (recommended): A plus an exact per-assembly lookup** at each indirect call: the module initializer
  records (native-callable value -> managed `ldftn` of the C function) for every address-taken function in
  an open-addressing table; an indirect call looks the pointer up, calls the managed method with a managed
  `calli` on a hit, and `calli unmanaged cdecl` on a miss. Cost 3.9-6.5 ns on a hit (+1-3 ns over today),
  7.5-11 ns on a miss. Correct for pointers stored in structs that native code reads (`del-ops`), for
  `qsort`/`bsearch`/thread start routines, for native pointers called from C (`fp.c`'s `abs`), and keeps
  C-to-C indirect calls free of native frames. Identity: one thunk per function, created once (the
  `'<msil>fn_'` slot already exists for this; Design.md 4.5 "the slot the thunk table fills later").
- A per-call-site inline cache alone (3.3-5.0 ns) is not exact: a miss would fall back to a thunk round
  trip, which breaks `longjmp` on Linux. It can accelerate A2 but cannot replace the lookup.

**Option B: managed pointers internally, converted at the boundary.** A P/Invoke parameter of a
function-pointer type would get a thunk for the pointer passed. Breaks: pointers stored in memory that
native code reads (`struct sigaction`, `z_stream.zalloc`, `WNDCLASS.lpfnWndProc`, callback tables of
native libraries), pointers returned by native code and compared with ours (`signal()`'s previous
handler), and native pointers called from C, which still need a run-time kind test at every indirect
call. Two values per function also break identity (`&f` in C vs what native code returns). Rejected.

**Option C: range check or tagging.** No per-assembly range exists (2.9). Tagging cannot change the value
native code calls; it only helps internal values, which is option B. Rejected; the exact table lookup is
the working version of Design.md 4.5's fast path.

**Across assemblies.** A function pointer obtained from another assembly built by this toolchain (an
exporter's `'<msil>addr_f'()`) misses the importer's table. Two levels: (1) the miss is a
`calli unmanaged`, correct but a double transition, and a `longjmp` through it aborts on Linux; (2) the
miss path also probes the other assemblies' tables through a process-wide registry in an AppDomain data
slot (2.10), so every C-to-C call through a pointer stays managed in the whole process. The Lua case needs
(2) when Lua is a `-shared` library and the program's `lua_CFunction`s raise errors (`lua_error` ->
`longjmp` through the library's indirect call). Decision CB4.

**Native functions.** `&qsort` should be the native entry point (`NativeLibrary.GetExport` or
`GetProcAddress`/`dlsym`, the M20 `'<msil>dataimport'` helper of plan 5.10.12), not the `ldftn` of the
P/Invoke stub: native code that receives it calls `qsort` directly, C code calling it takes the miss path,
and `&qsort` then equals what `dlsym` returns and is the same in every assembly (exports-multiasm 4.3
expected this, *not tested* there; the lookup works, `fp.c`).

---

## 4. Threads

- **Native thread entry** works through both thunk kinds on every runtime (2.6). No extra code is needed for
  thread attach: the CLR attaches on the first reverse transition. P3-A's `[ThreadStatic]`-anchored TLS
  block and shadow stack must be allocated lazily on first use (they will be, for CLR threads too) and see
  default values on a native thread.
- **Thread exit:** nothing runs managed code at a native thread's exit unless the program registered a
  destructor (pthread keys, `tss_create`, `__cxa_thread_atexit_impl`); those run after the CLR detached the
  thread and re-attach it (2.6). P3-A must not expect its `[ThreadStatic]` TLS block in such destructors;
  freeing the per-thread blocks of native threads is P3-A's question (inputs.md section 4: "never freed at
  thread exit").
- **Exceptions:** unhandled exceptions on such threads end the process (as natively a crash would); a
  `longjmp` out of a start routine to a `setjmp` on another thread is undefined in C anyway.
- **GC:** native threads that are outside managed code are in preemptive mode and never block a GC; inside
  a callback they reach safe points like any managed code. The only GC hazards are the wrong-kind calls of
  2.1 and 2.4, which A2 removes.
- **Rust `thread::spawn` (stretch):** not run. It needs P3-A TLS and P3-B atomics besides the thunk (HANDOFF
  issue 15). The thunk part is what `thr.c` and the `del-thread`/`uco-thread` tests cover: Rust's Windows
  `thread_start` is `extern "system" fn(*mut c_void) -> u32` (thunk `int32(native int)`), its Linux one
  `extern "C" fn(*mut c_void) -> *mut c_void`. Rust std on glibc also registers TLS destructors through
  `__cxa_thread_atexit_impl(dtor, obj, &__dso_handle)` (*unverified*, from Rust std's source as remembered):
  that native function takes a callback (not on today's list) and `__dso_handle` is an ELF linker symbol that
  msil-ld would have to define. On Windows Rust registers TLS callbacks in `.CRT$XLB`, which msil-ld
  rejects today (`section '.CRT$XL*' ... is not supported`, Status.md): P3-A's TLS item.

---

## 5. Recommendation

### 5.1 Representation and defaults

- Keep one `'<msil>fn_<f>'` field per address-taken function as the canonical C value (no change to
  FUNC_ADDR, relocations, accessors or objects). P3-B changes what phase 1 of the initializer stores in it:
  - **defined function, default:** `GetFunctionPointerForDelegate` of a delegate of a private delegate type
    `'<msil>d<k>'` (one per distinct thunk signature; `.ctor` and `Invoke` only; no
    `UnmanagedFunctionPointer` attribute on the 64-bit triples, `Cdecl`/`StdCall` on i686 later), kept alive
    in `private static class [mscorlib]System.Delegate[] '<msil>thunks'`, created **eagerly** in phase 1
    (so data relocations can store it), table-driven (2.12, shape C);
  - **defined function, switch `--uco-thunks`:** `ldftn` of `'<msil>uco_<f>'`, an `[UnmanagedCallersOnly]`
    method that forwards its arguments to `f` (no `CallConvs` on 64-bit, so the blob is `01 00 00 00` and
    needs no type-valued argument);
  - **native function:** its native entry point through the M20 data-import helper
    (`'<msil>dataimport'(module, entry)` in `'<msil>init_dataimports'`, same failure behaviour);
  - **function of a linked library:** unchanged (the exporter's accessor), so the exporter owns the thunk.
- Every `(native-callable value, ldftn of the C function)` pair of a defined function goes into the
  assembly's lookup table in phase 1, before relocations and constructors.
- Defaults run on Framework 4.8 and modern .NET alike (delegates). The UCO switch is purely a **link-time**
  choice: objects do not change, only the trailer; `-S`/llc get the same through an llc option.

### 5.2 IL shapes

Phase 1 (one `'<msil>Data'` or `<Module>` `.cctor`), default, for N address-taken defined functions with
thunk signatures grouped by delegate type:

```
  // table: 2^ceil(log2(2N)) entries of 16 bytes {native-callable value, managed ldftn}, zeroed
  ldc.i4 <bytes> conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  dup stsfld native int '<msil>fntab'  ldc.i4.0 ldc.i4 <bytes> initblk
  ldc.i4 <mask> conv.i stsfld native int '<msil>fnmask'
  ldc.i4 N newarr [mscorlib]System.Delegate stsfld class [mscorlib]System.Delegate[] '<msil>thunks'
  // locals: native int[] mgd, native int[] pubs (or one unmanaged scratch block)
  ldc.i4 N newarr [mscorlib]System.IntPtr stloc mgd
  ldc.i4 N newarr [mscorlib]System.IntPtr stloc pubs
  // one ldftn per function (Phase 2's cost)
  ldloc mgd ldc.i4 <i> ldftn int32 'f'(native int, native int) stelem.i
  ...
  // per delegate type '<msil>d<k>' with index range [a, b): one loop
  //   d = new '<msil>d<k>'(null, mgd[i]); thunks[i] = d; pub = GetFunctionPointerForDelegate(d);
  //   '<msil>fnadd'(pub, mgd[i]); pubs[i] = pub
  // then per function: ldloc pubs ldc.i4 <i> ldelem.i stsfld native int '<msil>fn_f'
```

With `--uco-thunks` the loop disappears: per function `ldftn '<msil>uco_f'(sig)`, `ldftn 'f'(sig)`,
`call void '<msil>fnadd'(native int, native int)`, and the `stsfld` of the first value (measured shape U).

Lookup helper (private, one per output; the `bench.il`/`patch_fp.py` body, measured):

```
.method assembly static native int '<msil>fnmgd'(native int p) cil managed
{
  .maxstack 4
  .locals (native int e, native int k)
  ldsfld native int '<msil>fntab' brfalse.s NONE
  ldsfld native int '<msil>fntab' ldarg.0 ldc.i4.3 shr.un ldarg.0 ldc.i4.s 17 shr.un xor
  ldsfld native int '<msil>fnmask' and ldc.i4.4 shl add stloc.0
LOOP: ldloc.0 ldind.i dup stloc.1 ldarg.0 bne.un.s NEXT
  ldloc.0 ldc.i4.8 add ldind.i ret                         // hit: the managed ldftn
NEXT: ldloc.1 brfalse.s NONE                               // empty slot ends the chain
  ldloc.0 ldc.i4.s 16 add stloc.0                          // next slot, wrapping at the end
  ldloc.0 ldsfld native int '<msil>fntab' ldsfld native int '<msil>fnmask' ldc.i4.1 add ldc.i4.4 shl add blt.un.s LOOP
  ldsfld native int '<msil>fntab' stloc.0 br.s LOOP
NONE: ldc.i4.0 conv.i ret                                  // not this assembly's (or the registry's, CB4)
}
```

Thunk values are only 4-byte aligned (2.9) and at least 0x18 apart; `(p >> 3) ^ (p >> 17)` spreads them,
and a collision only lengthens a probe chain. A null `p` hashes to a slot and misses; the
`calli unmanaged` of a null pointer then faults as natively.

An indirect call `r = (*p)(x)` (what ISel produces from 5.3's IR):

```
  ldloc p call native int '<msil>fnmgd'(native int) dup stloc m brfalse NATIVE
  ldloc x ldloc m calli int32(int32) br DONE              // C to C: managed, no native frame
NATIVE:
  ldloc x ldloc p calli unmanaged cdecl int32(int32)      // native pointer (or another assembly's thunk)
DONE:
```

Trap thunk for a function that cannot be called natively (5.4): body
`ldstr "<msil>: native code called '<f>', which MSIL cannot make native-callable (<reason>)"`
`call void [mscorlib]System.Environment::FailFast(string)` and a default return; its value still goes into
the table, so C-to-C calls through the pointer keep working.

### 5.3 Where it goes in the code

- **IR dispatch (backend):** a new step in `MSILPrepareIR.cpp` rewrites every indirect `call` (not inline
  asm, not intrinsics; after `MSILFixFunctionBitcasts`, which only touches direct calls) into
  `%m = call ptr @"<msil>fnmgd"(ptr %p)`, a branch, the managed `call ptr %m(...)` and the native
  `call ptr %p(...) #{"msil-native-call"}`, joined by a phi; a `musttail` call gets a `ret` in each arm.
  Different call-site attributes keep SimplifyCFG from merging the two calls back (*to verify with a test*).
  `@"<msil>fnmgd"` is a reserved scaffolding name (plan 5.8.3 R5) that every output defines; in objects
  it is a placeholder reference like `'<msil>FnAddr'`.
- **ISel/MC:** `MSILISelLoweringCall.cpp` `LowerCall` reads `CLI.CB->hasFnAttr("msil-native-call")` and
  `emitCallResults` (`:559`) picks a new `CALL_INDIRECT_UNMANAGED` (or an immediate on `CALL_INDIRECT`);
  `MSILMCInstLower::lowerCallIndirectSignature` (`:448`) sets `MSIL::CallConv::UnmanagedCdecl`; the encoder
  and the object writer already handle it (1.1). `MSILUtilities.cpp:96` and `MSILMCInstLower.cpp:586` list
  `CALL_INDIRECT` cases that need the twin. msil-ld's token renumbering of StandAloneSig `calli` signatures
  must keep `SIG_C` (*to verify*: `lld/MSIL/TokenMap.cpp`, `llvm/lib/CLI/ILDecoder.cpp`).
- **Trailer (shared by `-S` and msil-ld):** `llvm/include/llvm/CLI/Trailer.h` `TrailerInput::FnAddrFields`
  grows a per-function thunk descriptor (kind: delegate, UCO, native import, trap; the IL signature; the
  ABI descriptor of 5.4); `TrailerSink` grows "declare delegate type for signature", "declare UCO thunk",
  and members; `llvm/lib/CLI/Trailer.cpp` `emitPhase1` (`:329-347`) emits 5.2's table, thunks and
  `'<msil>fnmgd'`/`'<msil>fnadd'`. Producers: `MSILAsmPrinter.cpp:1640-1645` and
  `lld/MSIL/Relocations.cpp:388-411`; msil-ld's writer needs TypeDefs for the delegate types (it already
  writes private classes: `'<msil>Data'`, `'<msil>SymbolTable'`, spill classes).
- **BCL members** (`llvm/include/llvm/CLI/BCLMembers.def`): types `Delegate`, `MulticastDelegate`,
  `IntPtr`; `Marshal::GetFunctionPointerForDelegate(Delegate)` and `Environment::FailFast(string)`
  (mscorlib-safe, recon/p2 bcl-binding); `UnmanagedCallersOnlyAttribute::.ctor()` (not mscorlib-safe,
  requested only under `--uco-thunks`); for CB4's registry `AppDomain::get_CurrentDomain`, `GetData`,
  `SetData`, `String::Intern`, `Monitor::Enter/Exit` (all mscorlib, measured 2.10).
- **Diagnostics:** delete the callback-list errors (`LTO.cpp:739-749`, `Signatures.cpp:123-128`, the quiet
  non-binding at `LTO.cpp:707-718`); keep `isNativeCallbackFunction` only if CB8 keeps a warning for the
  `signal` family; P3-A takes `atexit`, `at_quick_exit`, `on_exit`, `_onexit` into its interception list.
  Status.md's "known gap" disappears.
- **Exports:** `Writer.cpp:addExportMembers` keeps returning `'<msil>fn_'`; see CB6 for export-only
  functions.

### 5.4 Thunk signatures

The thunk's IL signature is the method's, with these exceptions (from the SIGNATURES descriptor that objects
already carry for definitions: `MSILLink::SignatureInfo`, `MSILLink.h:304-323`):

- `Sret` (Win64 results over 8 bytes, SysV over 16): the native caller passes the pointer first and expects
  it back in rax: thunk `native int (native int sret, ...)`, forwards, returns `ldarg.0`.
- `Byval` (SysV structs over 16 bytes): a value-type parameter `.class explicit sealed ... .pack 1
  .size <Size>` (no fields needed for a MEMORY-class struct); the thunk passes `ldarga` to the C function.
- `NativeIncompatible` (two-register struct results `{i64,i64}`/`{double,double}`, i128, vectors, half):
  the value type needs typed fields (2.8 measured int64 fields), which the record does not carry yet; the
  same extension that P/Invoke struct-by-value needs in P3 ("takes or returns a value that a P/Invoke
  cannot pass natively yet"). Until it exists: a trap thunk.
- `VarArg` definitions: native callers pass variadic arguments in registers, which IL cannot read: a trap
  thunk, permanently (decision CB7). A C-to-C call through such a pointer takes the managed path with
  P3-B's argument-buffer convention; a call of a *native* variadic function through a pointer (miss path)
  cannot build native varargs and is a variadics question (the v-variant table does not apply).
- Win64 by-reference copies and SysV structs of at most 16 bytes are already scalars in IR (coerced by
  clang), so the thunk is scalar and matches the native ABI.
- aarch64 HFA/HVA and composite rules: *unverified* (no aarch64 execution).

### 5.5 Diagnostics and documentation after P3-B

- The `native-callback` row's transcript (the sorted array) on both triples, `obj` and `lto`.
- `signal`/`sigaction` with a function of the program on Linux triples: CB8.
- `longjmp` out of a callback that native code called: documented as Windows-only (2.5).
- GettingStarted.md section 8's `native function 'qsort' ... needs phase 3 thunks` row goes away.

### 5.6 Tests

1. `msil/test/M2/08-semantics/native-callback.test`: the transcript on both triples, -O0/-O2, obj and lto,
   all runtimes of the harness (`msil_also_run`).
2. New hosted/native tests (M2/03-native style, both triples): `bsearch`, `lfind`/`tsearch`/`twalk`,
   `qsort_r`/`qsort_s` with a context, `pthread_once`/`call_once`, thread creation (`pthread_create` +
   `pthread_join`; `CreateThread` and `_beginthreadex` + `WaitForSingleObject`), a callback stored in a
   struct read by a test-built native library (`--dep`, like M15's native tests), struct-by-value
   callbacks (16/24-byte params and sret results, 5.4), `pthread_key_create` destructors.
3. Native pointers called from C: `GetProcAddress`/`dlsym` results (`fp.c`'s `abs`), and identity:
   `&qsort == dlsym(RTLD_DEFAULT, "qsort")` / `GetProcAddress(ucrtbase, "qsort")`.
4. Identity and fast path across objects and libraries (extend M22's tests): a library's function pointer
   passed by the program to native `qsort`, compared with the library's own `&f`, called from C in both.
5. `longjmp` out of a comparator (needs P3-B setjmp): Windows triple only (`REQUIRES`), the Linux limitation
   documented, no XFAIL.
6. Backend: IR tests of the dispatch rewrite (`llvm/test/CodeGen/MSIL/`), `calli unmanaged cdecl` encoding
   (llc `-filetype=obj` vs ilasm round trip), `musttail`, and that `-O2` does not merge the two calls.
7. Trailer FileChecks: delegate types per signature, `'<msil>thunks'`, `'<msil>fnmgd'`, the UCO thunks and
   the attribute binding under `--uco-thunks` (error when no reference defines the attribute), native
   import addresses through `'<msil>dataimport'`.
8. **GC churn leg (new harness tool, small C#):** load a test program's assembly, start a thread that
   allocates without pause, and run the entry point; run the callback tests under it. 2.1 shows that only
   such a leg exposes a wrong-kind call (a GCStress leg is the alternative; release runtimes' GCStress
   support is *unverified*).
9. `--uco-thunks` legs of the callback tests on .NET 8 and 10 (Windows and WSL).
10. Stretch at P3-B exit (after P3-A TLS and atomics): Rust `thread::spawn` + `join` on both triples.

### 5.7 Proof of shape on compiler output

`callbacks-files/c/patch_fp.py` rewrites `clang -S` output of `fp.c` and `thr.c` (both triples) into 5.2's
shapes (delegate and UCO variants; a per-signature dispatcher method stands in for 5.3's IR rewrite) and
counts table hits and misses on stderr. Results (`extras.txt`): `fp`: `-17510 21 1 5` (the native transcript),
"1 through the table, 1 native" (the C-to-C call through `pick` and the native `abs`), on all five runtimes
with delegates and the four CoreCLR runs with UCO; `thr`: `42 43 1` everywhere, Framework included.

---

## 6. Decisions

**CB1. Representation of a C function pointer.** Options: (A1) native-callable everywhere, every indirect
call `calli unmanaged`; (A2) native-callable everywhere plus an exact per-assembly lookup at indirect calls;
(B) managed values converted at P/Invoke boundaries; (C) range check or tagging. **Recommend A2.**
Evidence: 2.1 and 2.4 (wrong kinds are unsafe), 2.5 (A1 breaks `longjmp` on Linux), 2.9 (no range), 2.11
(A2 hit 3.9-6.5 ns vs A1 17-31 ns), section 3 (B breaks struct-stored pointers and identity), 5.7.

**CB2. Default thunk kind and when it is created.** Options: (a) kept-alive delegates created eagerly in
phase 1; (b) delegates created lazily on first use of the address (FUNC_ADDR becomes an accessor; fields
lose `initonly`); (c) both kinds emitted and chosen at run time (`Environment.Version.Major >= 5` or the
presence of the attribute type) with a private by-name UCO attribute. **Recommend (a)**, with (b) only for
export-only functions of libraries (CB6). Evidence: 2.2 (works on all five), 2.12 (eager cost 9-55 ms per
1,000 table-driven; programs have few), data relocations need the value at initialization anyway, and lazy
fields cannot be `initonly` (ECMA-335 lets only the type initializer store them; CoreCLR's tier-1 JIT also
treats `static readonly` fields of initialized types as constants, not measured here). (c) would give modern .NET UCO speed by
default but relies on name matching and doubles the thunk metadata; it is the user's call under the rule
"defaults run on Framework 4.8 and modern .NET alike" (it does run on both). needs_user: yes (c is a policy
choice).

**CB3. The UnmanagedCallersOnly switch.** Options: (a) a link-time option (`msil-ld --uco-thunks`, llc
`-msil-uco-thunks` for `-S`), the attribute bound from the references like any BCL member (error when none
defines it); (b) the same, but a private attribute class by name when no reference defines it (measured to
work, 2.3); (c) a module flag/TargetOptions switch recorded in objects. **Recommend (a)**: objects do not
change, the trailer alone does; it follows "an API is usable only if a given reference assembly defines it".
needs_user: yes ((b) bends that rule; how the driver spells the flag belongs to P3-A's switch framework).

**CB4. Indirect calls to another assembly's functions.** Options: (a) the miss path (`calli unmanaged`):
correct, 17-31 ns, `longjmp` across it aborts on Linux; (b) a process-wide registry of lookup tables in an
AppDomain data slot (2.10), probed on a miss; (c) probe only directly linked libraries' tables (misses the
library-calls-program direction, Lua's case). **Recommend (a) in the first P3-B milestone and (b) when
setjmp/longjmp across assemblies is decided** (inputs.md section 4); (b) adds about 300 lines and a few ns
per probed assembly on misses. needs_user: yes (tied to the cross-assembly setjmp question).

**CB5. Address of a native function.** Options: (a) native entry point via the data-import helper; (b) the
`ldftn` of the P/Invoke stub (today, managed, per-assembly); (c) a thunk of the stub. **Recommend (a)**:
native code can call it, it equals `dlsym`/`GetProcAddress` results and is the same in every assembly; the
helper exists (M20) and costs one lookup per address-taken import. Evidence: section 3, 5.7 (`abs`).

**CB6. Thunks of export-only functions of a `-shared` library.** Options: (a) eager like the rest (1,000
exports: 9-55 ms table-driven delegates, 3 ms UCO); (b) lazy in `'<msil>addr_f'()` with
`Interlocked.CompareExchange` on a non-`initonly` field (exports-multiasm 5.5's suggestion). **Recommend
(b) for functions whose address no code or data of the library uses, (a) for the others.** Evidence: 2.12,
exports-multiasm 5.5.

**CB7. Functions a thunk cannot represent** (variadic definitions; `NativeIncompatible` signatures until P3's
aggregate descriptors). Options: (a) a trap thunk (`Environment.FailFast` with a message) that still enters
the table; (b) a link error when their address is taken; (c) a warning plus (a). **Recommend (a)**: taking
such an address for C-to-C use is legitimate and works through the table; only a native call fails, loudly.

**CB8. `signal`/`sigaction` and friends on Linux triples.** Options: (a) allow, document that an
asynchronous signal interrupting managed code ends the process; (b) allow with a link-time warning when
`signal`, `bsd_signal`, `sysv_signal`, `__sysv_signal`, `sigset` or `ssignal` is called (the `sigaction`
handler is in a struct, so only documentation); (c) keep today's error; (d) later, behind a switch,
intercept them and map to `PosixSignalRegistration` (.NET 6+; handlers then run on a .NET thread, not in the
interrupted one). **Recommend (b)**, and (d) as a later idea. Evidence: 2.7. needs_user: yes (semantics
differ from native).

**CB9. Exceptions across native frames.** No design option exists on Linux (2.5). **Recommend:** the setjmp
design must not depend on unwinding through native frames on Linux; document `longjmp` out of a
native-called callback as Windows-only. needs_user: no (informs the setjmp recon).

**CB10. Where the dispatch is introduced.** Options: (a) an IR rewrite in `MSILPrepareIR` (codegen
pipeline, objects and LTO alike); (b) an IR pass early in the optimizer pipeline (`PipelineStartEP`) so LICM
can hoist `'<msil>fnmgd'` out of loops; (c) per-signature dispatcher methods (5.7's stand-in, one call layer
more unless the JIT inlines a method containing `calli`, *unverified*). **Recommend (a)**, (b) as a later
tuning measured on Lua/SQLite. needs_user: no.

**CB11. Lookup structure.** Options: open-addressing hash in unmanaged memory (measured), sorted array plus
binary search, `Dictionary<IntPtr,IntPtr>` (needs generic instantiations, which objects cannot express
before phase 4), per-site caches. **Recommend the open-addressing table**, filled in phase 1, read without
locks; a per-site cache only as a later refinement. needs_user: no.

---

## 7. Interaction with other Phase 3 work and the late goals

- **P3-A** (switches, helpers, TLS, atexit): `atexit`-family interception receives native-callable values
  and calls them through the dispatch (hits); TLS and the shadow stack must initialize lazily on native
  threads (2.6); the UCO switch is one of P3-A's switches; `'<msil>fnmgd'`, `'<msil>fnadd'` and the trap
  thunk are trailer-built IL helpers of the kind P3-A defines.
- **P3-B setjmp/longjmp:** within an assembly, A2 keeps C-to-C indirect calls free of native frames; across
  native frames only Windows unwinds (CB9); across assemblies, CB4.
- **P3-B variadics:** the trap thunk for variadic definitions; calls of native variadic functions through
  pointers have no v-variant (5.4).
- **P3 struct-by-value P/Invokes:** the aggregate descriptor they need also completes the thunks of
  `NativeIncompatible` functions (5.4); 2.8 shows the CLR side works for thunks.
- **Portable triple (`msil64-unknown-dotnet`):** with no native libraries the dispatch could be switched
  off (a "managed-only" function-pointer model where `'<msil>fn_'` holds the `ldftn` and indirect calls are
  managed); keeping the dispatch behind one IR step makes that a mode, not a fork. The thunk machinery is
  triple-independent. Eases.
- **Managed calls from C / FeatureIdeas 3 (delegate-accepting export overloads):** because C function
  pointers are native-callable, an overload can convert a C# delegate with `GetFunctionPointerForDelegate`
  and C# can pass `delegate* unmanaged[Cdecl]` values (UCO methods) directly; C calls them through the miss
  path. A managed-only representation would have precluded this. Eases.
- **Inline IL:** inline IL that calls a C function pointer must use the dispatch (`'<msil>fnmgd'` or
  `calli unmanaged cdecl`), never a managed `calli` of the value: a documentation rule for that goal.
- **FeatureIdeas 6 (enclosed instance, wasm-style table indices)** replaces the model entirely; nothing here
  precludes it. **FeatureIdeas 7 (several triples in one assembly):** delegate types per signature are
  triple-independent; tables per variant. No conflict.
- **Mono/Unity (Design.md 7.2):** delegate thunks are the Mono-compatible kind; IL2CPP needs
  `[MonoPInvokeCallback]` on callback targets (*unverified*). Not precluded.

---

## 8. Risks and open questions

- Native-to-managed callback cost with delegates: 17-27 ns per call (Framework 26.5); a qsort-heavy C
  program is 3-4.5x slower in its comparisons than native, 2x with UCO.
- Start-up cost on Framework with many address-taken functions (about 55 ms per 1,000, table-driven); Lua
  and SQLite should be measured once P3-B exists.
- Code size: each indirect call site grows by about 25-40 bytes of IL (lookup call, branch, second
  `calli`).
- The by-name recognition of `UnmanagedCallersOnlyAttribute` is observed behaviour, not documented (only
  relevant if CB2(c) or CB3(b) is chosen).
- Native code that calls a callback after the CLR started shutting down (native library destructors at
  process exit) is untested.
- `fork()` in a .NET process (and `pthread_atfork` handlers) is unsupported by the runtime; P3-B should not
  promise it.
- aarch64 thunk ABI (HFA/HVA), i686 `__stdcall` callbacks (WNDPROC on x86): later phases.
- Measurements were taken on a loaded shared host; per-call numbers spread by up to 2x between rounds.

---

## 9. Work package ideas (disjoint files)

1. **Backend dispatch** (`MSILPrepareIR.cpp`, `MSILISelLoweringCall.cpp`, `MSILInstrCall.td`,
   `MSILMCInstLower.cpp`, `MSILUtilities.cpp`, `llvm/test/CodeGen/MSIL/`): IR rewrite, unmanaged `calli`,
   tests. About 500-800 lines with tests.
2. **Trailer** (`llvm/include/llvm/CLI/Trailer.h`, `llvm/lib/CLI/Trailer.cpp`, `BCLMembers.def`,
   `MSILAsmPrinter.cpp` sink, unit tests): delegate types, thunks, table, `'<msil>fnmgd'`/`'<msil>fnadd'`,
   trap thunks, import addresses through `'<msil>dataimport'`, UCO thunks. About 900-1,200 lines.
3. **msil-ld** (`lld/MSIL/Relocations.cpp`, `Writer.cpp`, `Synthetic.cpp`, `BCLBinder.cpp`, `Options.td`,
   `Driver.cpp`, `LTO.cpp`, `Signatures.cpp`, `NativeLibraries.cpp`, `lld/test/MSIL/`): thunk descriptors,
   delegate TypeDefs, `--uco-thunks`, attribute binding, lazy export thunks, retiring the callback-list
   errors, the `signal` warning. About 700-1,000 lines.
4. **Tests and harness** (`msil/test/M2/08-semantics/native-callback.test`, new callback tests, the GC churn
   host under `msil/utils/`, `--uco-thunks` legs): about 800 lines.
5. **Docs** (Status.md, GettingStarted.md, Design.md 4.5 update: "exact table lookup" instead of "one range
   compare"; the `signal` and `longjmp` limits): small.
6. **Optional, with the setjmp decision:** the cross-assembly registry (CB4 b). About 300 lines.

---

## Appendix: files

`callbacks-files/`:
- `native/cbnative.c`, `native/build-win.ps1`: the native library (call_cb, loop_cb, do_qsort, call_ops,
  run_thread, struct-by-value callers, signal killer, pthread keys, get_native_fn/get_loop_native).
- `il/cb.il` (delegates, `ldftn`, `calli`, GC stress, threads, exceptions); generators `gen_uco.py`,
  `gen_bench.py`, `gen_init.py`, `gen_cctor.py`, `gen_sbv.py`, `gen_addr.py`; `sig.il`, `key.il`, `reg.il`.
  `gen_uco.py` reads `uco-blob.txt` from `recon/p2/bcl-binding-files/roslyn/` (copy it next to the script).
- `c/qs.c`, `c/fp.c`, `c/thr.c` with their `build*.ps1`, and `c/patch_fp.py` (5.7).
- `scripts/`: `run.sh` (one program on fx/n8/n10/w8/w10), `matrix.sh`, `benchall.sh`, `extras.sh`,
  `summarize2.py`, the runtimeconfig files.
- `results/`: `matrix.txt`, `extras.txt`, `bench-summary.txt`, `bench-raw.txt`, `startup.txt` (user paths
  scrubbed).
