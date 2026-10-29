# Recon P3: program and library lifecycle (global destructors, atexit, exit, `.fini_array`, `.CRT$XT`)

Key: `lifecycle`. Phase 3 item P3-A ("global destructors and atexit through AppDomain.ProcessExit"), the
matrix rows `p6-dtors`, `fini-array`, `crt-xt` and `atexit-order`, and the open Phase 2 question "atexit
ordering across several output assemblies, and a C `exit` through the host libc possibly bypassing
ProcessExit" (P3 inputs section 4). HEAD `cc4c79787d6a`. Scratch: `C:\llvm-msil-scratch\p3\lifecycle`.
Supporting files (sources, scripts, logs): [lifecycle-files/](lifecycle-files/).

Everything below marked *measured* was run on this host: native builds with the tree's clang
(`x86_64-pc-windows-msvc` with lld-link and the UCRT, both `-fms-runtime-lib=dll` (/MD) and the default static
CRT (/MT); `x86_64-unknown-linux-gnu` with ld.lld and the glibc 2.43 sysroot's `crt1.o`/`crti.o`/`crtn.o`, run
in WSL Ubuntu, glibc 2.43); C# and ilasm probes on .NET Framework 4.8.9363, .NET 8.0.33 and .NET 10.0.14 on
Windows and on the private .NET 8.0.31 and 10.0.13 under WSL.

## 0. Summary

1. **Today** every destructor form is a named error and `atexit` binds nowhere (section 1); `exit`, `_exit`,
   `_Exit`, `quick_exit`, `abort` and `__cxa_atexit` bind natively by P/Invoke. Record v1 already carries FINI
   entries (`INIT_FINI` Kind 1), so P3-A needs **no record or ABI version change**.
2. **Native semantics** (section 2, measured): glibc runs every `atexit`/`__cxa_atexit` handler (one
   process-wide LIFO list) and then the `.fini_array` entries in reverse sorted order; a handler registered
   during exit runs next; `exit` from a handler continues the list with the new status; `exit` from a
   destructor abandons the remaining destructors; `_Exit`, `quick_exit` and `abort` do not flush stdio. The
   UCRT with the DLL CRT (/MD, which is what the MSIL Windows triple binds to) runs the handlers but **never
   runs `.CRT$XT*` or `__attribute__((destructor))`**, and flushes stdio even on `_Exit`/`quick_exit`; the
   static CRT (/MT) runs `.CRT$XT*` ascending, but after stdio is torn down, so a destructor that prints
   crashes (0xC0000005, judge-c-fidelity J9 explained). Exit codes: Windows 32 bits, Linux low 8 bits.
3. **.NET** (section 3, measured): `AppDomain.ProcessExit` always runs on another thread (the finalizer
   thread), is cut off after about 2.3-2.6 s on .NET Framework, and **`Environment.Exit` called inside
   ProcessExit deadlocks on .NET 8 and .NET 10** (Windows and Linux). A native `exit` reaches ProcessExit on
   Framework only (UCRT hands a managed EXE to `CorExitProcess`, skipping its own atexit table), never on
   .NET 8/10; native `_exit`/`quick_exit` raise ProcessExit on Framework too. `ExitProcess` raises it nowhere.
   Return from `Main` waits for foreground threads; `Environment.Exit` does not. Native stdio is flushed after
   `Environment.Exit` and after return from `Main` on every runtime and OS. Finalizers run at exit only on
   Framework.
4. **Recommendation** (section 5): the toolchain implements the whole C exit protocol as private IL that the
   trailer builder (`llvm/lib/CLI/Trailer.cpp`) emits into each output, for msil-ld and `-S` alike: per-assembly
   handler lists; `atexit`, `__cxa_atexit`, `on_exit`/`_onexit`, `at_quick_exit`, `exit`, `quick_exit` (and
   `_Exit`/`_exit` on Windows triples) defined when the link leaves them undefined; `'<msil>Main'` becomes
   `exit(main(...))`; C `exit` runs this assembly's handlers and destructors **on the calling thread**, then
   those of its libraries in reverse initialization order (an explicit cascade through a `'<msil>fini'`
   member of each library's export class), then calls `Environment.Exit`. `AppDomain.ProcessExit` is only the
   fallback (a library used by a .NET host), subscribed **at the end** of the module initializer and never
   blocking. A complete IL prototype of this design (program + library, mscorlib-only) runs correctly on all
   five runtimes, including the three deadlocks it was built to avoid (section 5.9).
5. **No codegen switch** is needed. Twelve decisions are listed in section 6; two need the user (Windows
   destructor semantics with hand-written expected files, and the Design.md 4.1 wording).

## 1. Today (HEAD `cc4c79787d6a`)

### 1.1 Constructors and startup, as built

| Piece | Where | What it does |
|---|---|---|
| `llvm.global_ctors` in objects | `MSILAsmPrinter.cpp:2696-2757` (in `emitObjectEnd`, which starts at 2292) | INIT entries (Kind 0, priority, symbol, comdat) in array order; a constructor with arguments off the ELF rule is `global constructor '<c>' takes arguments, which MSIL does not pass` |
| `llvm.global_ctors` in whole-program text | `MSILAsmPrinter::emitGlobalVariable` 1882-1887, `collectConstructors` 1137-1227 | INIT entries plus every 8-byte slot of `.init_array*` (ELF rules) or `.CRT$XC*` (COFF rules), keyed by `cli::getConstructorKey`, sorted by `cli::sortConstructors` |
| Linking | `lld/MSIL/OutputSegment.cpp:370-453` (`OutputImage::getConstructors`), `Writer.cpp` ~545 (`Writer::addConstructors`), LTO `LTO.cpp:452-565` | same keys and sort; null `.CRT$XC*` slots skipped (`cli::skipsNullConstructorSlots`, COFF only), a null `.init_array` slot is the error `constructor table entry at <s>+<off> in <file> is not a function address` |
| Keys | `llvm/lib/CLI/ImageLayout.cpp:123-190` | ELF: priority, 65535 -> 65536 (plain `.init_array`); COFF: clang's section names (`.CRT$XCU`, `.CRT$XCA%05u`, ...) compared byte-wise; ties: input order, an object's slots before its INIT entries |
| Trailer | `llvm/lib/CLI/Trailer.cpp`: `emitPhase1` 329, `emitPhase2` 560-637, `buildEntryPoint` 660-688, `buildTrailer` 702-748 | `<Module>::.cctor` = image, address fields, relocations, then (phase 2) one `ldtoken`+`pop` per linked library in command-line order, cross-assembly relocations, constructors (`call`, result popped; argc/argv/envp on ELF triples, row 'M22.b extended'); `'<msil>Main'(string[])` = `call int32 main(...)`, `ret` |
| Libraries | `TrailerInput::TwoPhase` | phase 1 in `'<msil>Data'::.cctor`, phase 2 in `<Module>::.cctor` (omitted when empty) |

A program P linking libraries B then C initializes B, C, P by construction (M22: one `ldtoken` per library in
command-line order, and the JIT runs a library's module initializer when it resolves the token; the IL prototype
of section 5.9 shows the library's constructor before the program's). Native Windows has the same order (B, C, P,
measured); glibc initializes C, B, P (reverse `DT_NEEDED` order, measured).

`'<msil>Main'` of a Phase 2 program that calls `exit` (ildasm of `callexit.c`, *measured*):

```
.method public static int32 main() ... IL_0010: call void exit(int32)
.method assembly hidebysig static pinvokeimpl("api-ms-win-crt-runtime-l1-1-0.dll" nomangle cdecl) void exit(int32)
.method private hidebysig static int32 '<msil>Main'(string[] A_0) { .entrypoint  call int32 main()  ret }
```

`msil-ld --print-imports` lists `exit -> api-ms-win-crt-runtime-l1-1-0.dll!exit [function, -]`; the program exits
300 on .NET 8 and Framework, with its buffered `puts` line flushed.

### 1.2 Destructors, `atexit` and the exit family: every diagnostic

| Construct | Path | Diagnostic (source location) |
|---|---|---|
| non-empty `llvm.global_dtors` | `clang -c`, `-S`, llc (MSILPrepareIR) | `global destructor @<f> is not supported for MSIL yet` per entry, else `global destructors (llvm.global_dtors) are not supported for MSIL yet`; the array is erased (`MSILPrepareIR.cpp:1406-1433` `diagnoseGlobalDtors`, called at 2295) |
| same, without MSILPrepareIR | AsmPrinter backstop | `global destructors (llvm.global_dtors) are not supported for MSIL yet` (`MSILAsmPrinter.cpp:1889-1896`) |
| `.fini_array*` (ELF) / `.CRT$XT*` (COFF) slot in whole-program text | AsmPrinter `checkSections` | `global destructors are not supported yet: <IL name> in <source file>` (`MSILAsmPrinter.cpp:945-951`) |
| FINI entry in an object | msil-ld | `global destructors are not supported yet: <s> in <file>` (`OutputSegment.cpp:55-63`; no Phase 2 writer emits FINI) |
| `.fini_array*` / `.CRT$XT*` segment | msil-ld | same text with the segment (`OutputSegment.cpp:72-75`); `MarkLive.cpp:96-110` keeps such segments live so they are reported, and `MarkLive.cpp:186` already roots INIT and FINI entries |
| `.CRT$XP*`, `.CRT$XI*`, `.CRT$XL*`, `.ctors*`, `.dtors*`, `.preinit_array*` | both | `section '<name>' in <file> is not supported` (`ImageLayout.cpp:45-75` `classifySection`) |
| `atexit` with the glibc sysroot | msil-ld | `symbol 'atexit' is defined only in native object code (<...>libc_nonshared.a(atexit.oS)); MSIL cannot link machine code` (`NativeLibraries.cpp:144`; test `msil/test/M2/09-hosted/glibc-atexit.test`) |
| `atexit` with the E7 sysroot or on Windows | msil-ld | `undefined symbol: atexit` + `>>> referenced by <file>:(main)` (matrix `atexit-order`) |
| `atexit`, `at_quick_exit`, `_onexit`, `on_exit` bound natively | msil-ld | the callback-list error `native function '<name>' (<module>) takes a C function pointer; passing functions to native code needs phase 3 thunks` (`NativeLibraries.cpp:80-115`) |
| `exit`, `_exit`, `_Exit`, `quick_exit`, `abort` | msil-ld | none: bound natively (P/Invoke), as 5.10.10 allows ("`exit` may bind natively in Phase 2") |
| `__cxa_atexit`, `__cxa_at_quick_exit`, `__cxa_thread_atexit_impl` | msil-ld | none: glibc exports them, they are **not** on the callback list, so a direct call hands glibc a managed `ldftn` value it cannot call (not run; same reason as 5.10.10's list). clang emits `__cxa_atexit` only for C++ and with `-fregister-global-dtors-with-atexit` |
| any of these in `-S`/llc | D14 | `... is not a Phase-1 import` |

Tests that pin these texts (all change in P3-A): `msil/test/M2/08-semantics/{p6-dtors,fini-array,crt-xt,atexit-order}.test`
(matrix lines 43, 95-97), `msil/test/M2/09-hosted/glibc-atexit.test`, `llvm/test/CodeGen/MSIL/{object-errors.ll
(DTORS), unsupported-ir-errors.ll (DTORS), section-errors.ll, sections-whole-program.ll, global-ctors.ll (comment)}`,
`lld/test/MSIL/obj-sections-unsupported.test` (DTOR), and four lld tests that use `atexit` merely as *the example*
of a native-only or callback symbol: `native-static-only.test`, `libsearch.test`, `native-elf-script.test`
(libc_nonshared's `atexit.oS`) and `native-callback.test` (TABLE-DAG `atexit`); they must switch to another
libc_nonshared-only symbol (`pthread_atfork`) once `atexit` is synthesized. The il-snapshot baseline
`msil/test/harness/Inputs/il-snapshot/baseline/x86_64-unknown-linux-gnu-msil/O2/llvm/test/CodeGen/MSIL/dtors.ll.err`
changes too.

clang emits `llvm.global_dtors` for `__attribute__((destructor[(N)]))` on all three MSIL triples (linux-gnu,
windows-msvc, windows-gnu; `-fregister-global-dtors-with-atexit` defaults on only for Darwin,
`clang/lib/Driver/ToolChains/Clang.cpp:7647-7650`). Natively, the COFF lowering for MSVC places destructors in
`.CRT$XTX` (priority 65535), `.CRT$XTA%05u` (<200), `.CRT$XTC` (200), `.CRT$XTC%05u` (201-399), `.CRT$XTL`
(400), `.CRT$XTT%05u` (>400) (`TargetLoweringObjectFileImpl.cpp:2049-2101`, `getCOFFStaticStructorSection`
with `IsCtor=false`): note the default is `XTX`, not `XTU`.

## 2. Native semantics to match (measured)

Probe: `lifecycle-files/native/order.c` (constructors 101/default that register handlers, destructors
101/200/300/default, a plain and a `.150` `.fini_array` slot or `.CRT$XTU`/`.CRT$XPU` slots, `__cxa_atexit` on
Linux, `at_quick_exit`, handlers that register or call `exit`). Trace on unbuffered stderr; `stdout:` lines are
buffered `printf`s (stdout is a pipe) whose appearance shows the exit flush. Full logs:
`native/results-linux-glibc.txt`, `native/results-windows.txt`, `native/results-windows-extra.txt`.

### 2.1 glibc (`x86_64-unknown-linux-gnu`, real `crt1.o`)

Return from `main` (identical for `exit(3)`):

```
ctor 101 / ctor default / main start / main end
__cxa_atexit handler (arg ok)                  <- registered last in main
atexit h2 / atexit h1                          <- main's, LIFO
atexit h_ctor / atexit h_ctor101               <- registered by constructors: still before every destructor
dtor default / .fini_array slot (plain) / dtor 300 / dtor 200 / .fini_array.150 slot / dtor 101
stdout: buffered line from main / stdout: from h1 / stdout: from dtor default   <- flushed after all of it
```

- One LIFO list for `atexit` and `__cxa_atexit`; `.fini_array` runs after it because `__libc_start_main`
  registers `_dl_fini` before it runs the constructors. Destructor order = reverse of the sorted array
  (`.fini_array.N` ascending, plain `.fini_array` = 65536 last, within one object the slot before the
  `llvm.global_dtors` entries), i.e. exactly `reverse(sortConstructors(entries))` with the constructor keys.
- `nested`: a handler that registers another (`atexit` returns 0) -> the new one runs next.
- `exitinhandler`: `exit(9)` from a handler continues the remaining handlers and destructors; status 9.
- `exitindtor`: `exit(11)` from the default destructor abandons the remaining destructors (`_dl_fini` is one
  list entry, already consumed); flush; status 11.
- `atexitindtor`: `atexit` from a destructor -> it runs after all destructors.
- `exit` from a constructor (`ORDER_EXIT_IN_CTOR`): the handlers registered so far, then every destructor and
  `.fini_array` slot of the program; status 13.
- `_Exit(5)`, `_exit(5)`: nothing runs, **stdout lost**. `quick_exit(6)`: `q2`, `q1`, **stdout lost**.
  `abort()`: 134, stdout lost. `exit(300)`/`return 300` -> 44; `exit(-1)` -> 255.

### 2.2 UCRT, DLL CRT (/MD: `vcruntime140` + `ucrtbase`, which the MSIL windows-msvc triple binds to)

```
ctor 101 / ctor default / main start / main end
atexit h2 / atexit h1 / atexit h_ctor / atexit h_ctor101
stdout: buffered line from main / stdout: from h1          <- no destructor, no .CRT$XT slot, no .CRT$XP slot ever runs
```

- `nested` and `exitinhandler` behave as on glibc (status 9).
- `_Exit`/`_exit`: no handlers, **stdout flushed** (ExitProcess -> `ucrtbase` `DLL_PROCESS_DETACH` flush).
  `quick_exit(6)`: `q2`, `q1`, stdout (including q2's line) flushed. `abort`: 0xC0000409 (after a WER delay).
- Exit codes: 300 -> 300, -1 -> 0xFFFFFFFF.
- `exit` from a constructor: the handlers registered so far; status 13.

### 2.3 UCRT, static CRT (/MT, clang's default and the harness's Windows oracle)

`exit`/return: handlers (LIFO), then the `.CRT$XP*` pre-terminators (the user's `.CRT$XPU` slot ran; the CRT's own
`.CRT$XP*` entries flush and tear down stdio), then the `.CRT$XT*` terminators **crash** in the first destructor
that touches stdio: `0xC0000005` on every exiting path. With `_exit`/`_Exit`/`quick_exit` (no pre-terminators)
the terminators run in **ascending** section order, `dtor 101` (`XTA00101`), `dtor 200` (`XTC`), `dtor 300`
(`XTC00300`), `.CRT$XTU slot`, `dtor default` (`XTX`), and stdout is **not** flushed. So the COFF destructor order
is ascending priority, the opposite of ELF's.

### 2.4 The P3 matrix cases natively

| Case | glibc (crt1) | E7 oracle `start.c` today | Windows /MD | Windows /MT |
|---|---|---|---|---|
| `fini-array` | `main`, `fini_array entry` | `main` only (no `.fini_array`) | n/a | n/a |
| `p6-dtors` | `BCDMcb` | `BCDM` | `BCDM` | `BCDM`, then 0xC0000005 |
| `crt-xt` | n/a | n/a | `main` (terminator never runs) | `main`, then 0xC0000005 |
| `atexit-order` | `main returns`, `second...`, `first...` | link error (`atexit` not in `libc.so.6`) | same as glibc | same |

The E7 oracle can produce all Linux transcripts with a small extension of `msil/utils/linux-oracle/start.c`,
prototyped as [`native/e7/start2.c`](lifecycle-files/native/e7/start2.c): register a `.fini_array` runner with
`__cxa_atexit` **before** running `.init_array` (as `__libc_start_main` registers `_dl_fini`), and define `atexit`
and `at_quick_exit` the way `libc_nonshared.a` does (`__cxa_atexit(f, 0, __dso_handle)`,
`__cxa_at_quick_exit(f, __dso_handle)`; `libc.so.6` of the E7 sysroot exports both, checked with `llvm-nm -D`).
Measured: `start2` gives byte-identical transcripts to glibc's `crt1.o` for `order.c` in all seven modes
(`ret exit nested exitinhandler exitindtor atexitindtor quick`) and for the three Linux matrix cases
(`native/e7/e7-results.txt`).

### 2.5 Several modules natively

`native/multi/`: program P with `atexit(h1)`, `b_reg()`, `c_reg()`, `atexit(h2)`; libraries B and C (linked in that
order) each with a constructor that registers a handler, a destructor and `*_reg` that registers another.

| | glibc | Windows /MD |
|---|---|---|
| init | `libC ctor`, `libB ctor`, `prog ctor` | `libB ctor`, `libC ctor`, `prog ctor` |
| exit | `h2`, `C (c_reg)`, `B (b_reg)`, `h1` (one global list, registration order), then `prog dtor`, `libB dtor`, `libC dtor` (reverse init), then the two handlers registered by library constructors (they precede `_dl_fini` on the list; with GCC's `crtbeginS.o` they would run at their library's fini through `__cxa_finalize`, so this tail depends on the crt files) | `h2`, `h1` (program's own table), then at `DLL_PROCESS_DETACH` in reverse load order: `libC`'s table (`c_reg`'s, ctor's), `libB`'s table; no destructor runs (/MD) |

Windows keeps **one list per module** and finalizes modules in **reverse initialization order**; glibc keeps one
process list and runs all handlers before any module's fini.

## 3. The .NET side (measured)

Probe: [`dotnet/Program.cs`](lifecycle-files/dotnet/Program.cs) (`net48;net8.0;net10.0`), trace through the host
C library's `_write`/`write` on fd 2 with the managed thread; logs `dotnet/win-results-{1..4}.txt`,
`dotnet/wsl-results-{1,2}.txt`. "fw" = Framework 4.8 (the EXE run directly), "8/10" = `dotnet` on Windows,
"L8/L10" = WSL.

| Scenario | ProcessExit runs | Exit status | Native stdio flushed | Notes |
|---|---|---|---|---|
| `Main` returns 7 | yes, all | 7 (300 -> 300 Windows, 44 Linux) | yes | handlers on another thread (finalizer thread: id 2 on fw/8/L8/L10, id 1 on .NET 10 Windows) |
| `Environment.Exit(7)` (also from a second thread, a native thread via reverse P/Invoke, or inside a native `qsort` comparator) | yes, all | 7 | yes | |
| `Environment.ExitCode = 5`, return 7 (Windows runtimes only) | yes | 7 | | the return value wins |
| `Environment.ExitCode = 9` inside ProcessExit after return 7 | | **9**, all | | a handler can still change the status |
| unhandled exception | **no**, all | 0xE0434352 (Windows), 134 (Linux) | | `UnhandledException` runs |
| native `exit(7)` (`ucrtbase`/`libc.so.6`) | **fw only**; 8/10/L8/L10 no | 7 | yes | fw: the native `_crt_atexit` callback does **not** run (the UCRT's `exit` hands a process whose EXE is managed to `CorExitProcess` before its own table: UCRT source, consistent with this measurement); 8/10: the native callback runs on the main thread; L8/L10: it runs |
| native `_exit(7)` | **fw only** | 7 | Windows yes, Linux **no** | Framework raises ProcessExit even here |
| native `quick_exit(7)` | **fw only** | 7 | Windows yes, Linux no | |
| native `abort()` | no | 0xC0000409 (Windows, cil-runtime e17), 134 (Linux) | no | |
| kernel32 `ExitProcess(7)` | **no**, all three | 7 | yes (`ucrtbase` detach) | native `_crt_atexit` callback not run |
| kernel32 `TerminateProcess` | no | 7 | **no** | |
| `Main` returns after registering a native atexit callback | yes | 7 | yes | fw: callback not run; 8/10 and Linux: callback runs after ProcessExit |
| `Environment.Exit` after registering it | yes | 7 | yes | Windows 8/10: callback **not** run; Linux: runs after ProcessExit |
| ProcessExit handler sleeps 4 s | | 7 | | **fw: killed at about 2.3-2.6 s** after ProcessExit began (total for all handlers; the second handler was cut too); 8/10/L8/L10: no limit |
| `Environment.Exit(9)` inside ProcessExit | | | | fw: exits 9 at once; **8/10/L8/L10: deadlock** (killed after 60 s, one ran 35 minutes) |
| native `exit(9)` inside ProcessExit | | 9, all | | |
| `Main` returns while a foreground thread sleeps 3 s | after the thread ends | 7 | | process lives 3 s longer |
| `Environment.Exit` with that thread | at once | 7 | | |
| a handler subscribed during ProcessExit (Windows runtimes only) | **never runs** | | | the delegate list is a snapshot |
| finalizers at exit | | | | fw runs them (reachable and unreachable objects), 8/10/L8/L10 do not |
| thread start and `Join` inside ProcessExit (Windows runtimes only) | works | | | |

Consequences:

- **ProcessExit cannot be the main mechanism for C handlers.** A C handler running there sees the finalizer
  thread's `thread_local` data and shadow stack, not the exiting thread's (C runs them on the thread that called
  `exit`); Framework silently kills long handlers; and a handler that calls `exit` deadlocks .NET 8/10 if `exit`
  is `Environment.Exit`.
- **A native `exit` is unusable** as C `exit`: on .NET 8/10 it skips ProcessExit (other assemblies' handlers
  never run), on Framework it skips the native table and goes through ProcessExit; `atexit` is not exported by
  either library anyway (`ucrtbase` has `_crt_atexit`, glibc has `__cxa_atexit`; `llvm-nm` on `ucrt.lib` and
  `libc.so.6`). This confirms and extends recon p2/backlog item 8.
- **`Environment.Exit` is a good primitive**: consistent ProcessExit on every runtime, works from any thread and
  from reverse-P/Invoke frames, passes 32 bits on Windows, the host flushes native stdio afterwards (Windows:
  `ucrtbase` detach; Linux: CoreCLR ends with glibc `exit`, which also runs glibc's own atexit list).
- **`ExitProcess` is the right `_Exit` on Windows**: no ProcessExit on any runtime (unlike `ucrtbase!_exit` on
  Framework), and it flushes like native /MD `_Exit`. On Linux glibc's `_exit` is already exact (no ProcessExit,
  no flush, like native).

## 4. Several output assemblies

- **Lists.** Each assembly binds its own references to its own definitions (Q21, `-Bsymbolic`), so a per-assembly
  `atexit` is natural and matches Windows (one table per module, measured). A process-wide list (glibc) would need
  shared state between assemblies (a named `AppDomain` data slot holding BCL objects): a cross-assembly protocol
  that every toolchain version must keep, against the spirit of "no type shared between assemblies".
- **Order.** Subscribing each assembly to ProcessExit gives subscription order, not reverse initialization order,
  and runs on the finalizer thread under Framework's 2-second limit. The program's `exit` therefore finalizes its
  libraries itself: after its own handlers and destructors it calls each directly linked library's
  `'<msil>fini'` in reverse initialization order, and each library cascades to its own libraries. Measured in the
  prototype: `prog h2, h1, prog dtor, plib handler, plib dtor` on the exiting thread, exit status preserved.
- **Library destructors when the program exits** run in that cascade (exiting thread, no time limit). When the
  library is used by a .NET host, they run from the library's own ProcessExit handler (finalizer thread; on
  Framework within the 2-second limit), as native DLL destructors run at process exit.
- **Order within the cascade.** Reverse of the order the module initializers ran: for P linking [B, C] it is
  P, C, B, matching native Windows exactly (P, C, B). glibc gives P, B, C because it initializes C before B; the
  MSIL order follows MSIL's own (Windows-like) initialization order, which M22 fixed. For a diamond (P links
  [B, D], B links D) plain reverse command-line order would finalize D before B; msil-ld knows the libraries'
  AssemblyRefs and should order the calls by the simulated initialization order (D initializes inside B's
  initializer), giving P, B, D.
- **Known deviation (Linux triples only):** a handler that a library registers with `atexit` runs in that
  library's fini, after the program's destructors, instead of interleaved with the program's handlers by
  registration time. Windows triples match native.

## 5. Recommended design

### 5.1 Overview

All of it is private IL that `cli::buildTrailer` emits (lib/CLI, shared by msil-ld's `RowTrailerSink` and the
target's text sink, so `-S`, llc and `--lto-emit-asm` behave like a one-object link and O3 holds). No helper
archive, no BCL member beyond mscorlib-safe ones, no record or ABI change.

1. **FINI entries flow like INIT entries.** MSILPrepareIR stops reporting `llvm.global_dtors`
   (`diagnoseGlobalDtors` goes); object mode writes FINI entries (Kind 1) next to the INIT entries in
   `emitObjectEnd`; whole-program mode collects them (and `.fini_array*`/`.CRT$XT*` slots) as
   `collectConstructors` does. msil-ld replaces the two errors in `OutputImage::layout` with a destructor list
   built like `getConstructors`.
2. **Destructor order** (new `cli::getDestructorKey` and `cli::sortDestructors` beside the constructor ones):
   - ELF rules: key = FINI priority (65535 -> 65536) or the slot section's priority (`.fini_array` = 65536);
     sort exactly as constructors (`sortConstructors`: ties in input order, an object's slots before its
     entries); **run in reverse**. Reproduces glibc for `order.c` and `p6-dtors` (`cb`).
   - COFF rules: key = the name clang gives a destructor of that priority (`.CRT$XTX` for 65535, `.CRT$XTA%05u`,
     `.CRT$XTC`, `.CRT$XTC%05u`, `.CRT$XTL`, `.CRT$XTT%05u`; `IsCtor=false` in
     `getCOFFStaticStructorSection`) or the slot's section name; byte-wise; **run ascending** (the static CRT's
     `_initterm(__xt_a, __xt_z)` order, measured). A null `.CRT$XT*` slot is skipped (`skipsNullConstructorSlots`,
     already decided for COFF tables by row 'M21.b extended'); a null `.fini_array` slot is the error
     `destructor table entry at <section>+<off> in <file> is not a function address` (glibc would call it).
   - FINI entries of a discarded comdat group go with the group, as INIT entries do.
3. **Synthesized C entry points**, defined by the trailer when the link (or the module) references the name and no
   object, archive member or bitcode defines it: msil-ld's late binding (5.9.6) gets a step "toolchain
   definitions" between objects/archives and managed/native libraries. A program or a libc that defines the
   name wins, as with native crt files. The set:

   | Symbol | Triples | IL signature | Behaviour |
   |---|---|---|---|
   | `atexit` | all | `int32(native int)` | push (fn, 0, kind 0); return 0 |
   | `__cxa_atexit` | all | `int32(native int, native int, native int)` | push (fn, arg, kind 1); dso ignored |
   | `on_exit` | non-Windows | `int32(native int, native int)` | push (fn, arg, kind 2): `fn(status, arg)` |
   | `_onexit` | Windows | `native int(native int)` | push (fn, 0, kind 0); return fn |
   | `_crt_atexit` | Windows | `int32(native int)` | as `atexit` (UCRT's internal name; keeps a stray reference off the native table) |
   | `at_quick_exit`, `__cxa_at_quick_exit` (`_crt_at_quick_exit` on Windows) | all | as above | push onto the quick list |
   | `exit` | all | `void(int32)` | section 5.3 |
   | `quick_exit` | all | `void(int32)` | quick list LIFO (this assembly, then the libraries' through `'<msil>fini'(1)`), then `_Exit` |
   | `_Exit`, `_exit` | Windows only | `void(int32)` | P/Invoke `kernel32.dll!ExitProcess(uint32)` |
   | `_Exit`, `_exit` | non-Windows | | **not synthesized**: native glibc `_exit`/`_Exit` is already exact |
   | `abort` | all | | not synthesized: native (0xC0000409 / SIGABRT 134, no handlers, no flush, like native) |
   | `__cxa_finalize` | all | `void(native int)` | optional: no-op (C++ `dlclose` support only) |

   `__cxa_thread_atexit_impl` belongs to the TLS work (thread-exit destructors); `exit` calls the exiting
   thread's TLS destructors first, as glibc's `exit` does, through a hook that the TLS design fills.
4. **`'<msil>Main'`** becomes `call int32 main(...)` / `call void exit(int32)` / `ldc.i4.0` / `ret` (the `exit` the
   link resolves, so a libc's own `exit` is called, as by crt0) **when the output has exit machinery**; otherwise it
   keeps Phase 2's shape byte for byte. An output has exit machinery when it has FINI entries or destructor-table
   slots, references any symbol of the table above (synthesized), or links a library whose export class has
   `'<msil>fini'`.
5. **Libraries** get `public static void '<msil>fini'(int32 kind)` on their (first) export class: kind 0 = exit
   (handlers, destructors, then their own libraries), kind 1 = quick_exit (quick list, then their libraries).
   msil-ld finds it by name in the library's metadata when it links the library (no symbol-table attribute change;
   an M22-built library without it is simply not cascaded and keeps relying on ProcessExit). The name is
   unspeakable in C#.
6. **ProcessExit is the fallback**, subscribed at the **end** of the module initializer (after the constructors;
   for a two-phase library at the end of `<Module>::.cctor`), so a ProcessExit raised while the initializer runs
   never invokes a handler of a module whose initializer is unfinished. Its handler returns at once when this
   assembly's exit has started; otherwise it runs `'<msil>fini'(0)`.

### 5.2 Fields (on `<Module>`; all private static)

```
native int[]  '<msil>atexit'       // entries of 3 slots: fn, arg, kind; grown by doubling (newarr System.IntPtr, Array::Copy)
int32         '<msil>atexit_n'     // number of entries
native int[]  '<msil>quick'        int32 '<msil>quick_n'
object        '<msil>exit_lock'    // created at the start of phase 2 (before constructors, which may call atexit)
int32         '<msil>exit_state'   // 0 running, 1 exiting, 2 finished (volatile reads)
int32         '<msil>exit_thread'  // managed thread id of the exiting thread
int32         '<msil>dtors_run'    // destructor list started (glibc: exit inside a destructor skips the rest)
```

An unmanaged block grown with `Marshal::ReAllocHGlobal` is an equivalent choice; the managed arrays need only
`newarr [mscorlib]System.IntPtr` and `Array::Copy`, which the prototype used. C requires at least 32
registrations; both are unbounded.

### 5.3 IL shapes

```
// C: int atexit(void (*)(void))            (same body with other kinds for __cxa_atexit, on_exit, _onexit)
.method public static int32 atexit(native int fn) {
  Monitor::Enter('<msil>exit_lock', ref taken) ... grow if full; store fn, 0, 0; '<msil>atexit_n'++ ... finally Monitor::Exit
  ldc.i4.0 ret }

// C: void exit(int)
.method public static void exit(int32 code) {
  // another thread already exiting: block forever (the process is ending); this thread: continue (nested exit)
  call '<msil>begin_exit'()                       // state 0 -> 1 and exit_thread = Thread::get_CurrentThread().get_ManagedThreadId()
  [call the exiting thread's TLS destructors]     // hook for the TLS design (glibc order)
  ldc.i4.0 call '<msil>fini'(int32)
  [if this thread is running this assembly's ProcessExit handler: ldarg.0 call Environment::set_ExitCode(int32);
   then the native termination of 5.4, never Environment.Exit (deadlock on .NET 8/10)]
  ldarg.0 call void [mscorlib]System.Environment::Exit(int32)
  ret }

// The fini loop: one entry at a time, popped under the lock and called outside it, so a handler may
// register another (it runs next) or call exit (the nested exit continues the list, then exits with its code).
.method assembly static void '<msil>fini'(int32 kind) {
NEXT:
  lock; if '<msil>atexit_n' > 0: n--; load fn, arg, kind; unlock
        kind 0: ldloc fn calli void()            kind 1: ldloc arg ldloc fn calli void(native int)
        kind 2: <status> ldloc arg ldloc fn calli void(int32, native int)
        br NEXT
  unlock
  if '<msil>dtors_run' == 0: '<msil>dtors_run' = 1; call '<msil>run_dtors'(); br NEXT   // handlers a destructor registers run after it
  call [B]B::'<msil>fini'(int32) ... in reverse initialization order (section 4)
  '<msil>exit_state' = 2
  ret }

.method assembly static void '<msil>run_dtors'() { call d1  call d2 ... (result popped) ret }   // section 5.1 item 2's order

.method assembly static void '<msil>on_process_exit'(object, class [mscorlib]System.EventArgs) {
  volatile. ldsfld int32 '<msil>exit_state'  brtrue.s DONE
  [mark: this thread runs this assembly's ProcessExit handler]
  call '<msil>begin_exit'()  ldc.i4.0 call '<msil>fini'(int32)
DONE: ret }

// <Module>::.cctor: at the start of phase 2
  newobj instance void [mscorlib]System.Object::.ctor()  stsfld object '<msil>exit_lock'
// ... ldtokens, cross-assembly relocations, constructors ... then, last:
  call class [mscorlib]System.AppDomain [mscorlib]System.AppDomain::get_CurrentDomain()
  ldnull  ldftn void '<msil>on_process_exit'(object, class [mscorlib]System.EventArgs)
  newobj instance void [mscorlib]System.EventHandler::.ctor(object, native int)
  callvirt instance void [mscorlib]System.AppDomain::add_ProcessExit(class [mscorlib]System.EventHandler)

// '<msil>Main'
  call int32 main(...)  call void exit(int32)  ldc.i4.0  ret
```

`kind 2`'s status is the `exit` argument, kept in a field by `exit`. The `calli` forms are today's managed
`calli default`; they must follow whatever P3-B decides for C function-pointer values (a native-callable thunk
address needs `calli unmanaged cdecl` or P3-B's in-assembly fast path), so the trailer should emit them through
one shared "call a C function pointer" helper of lib/CLI that P3-B extends, not hand-written `calli`s.
A delegate over a `<Module>` method (`ldftn` of a global method into `EventHandler::.ctor`) works on all five
runtimes (prototype). The handler needs no separate class: once subscription happens at the end of the
initializer, its module is always initialized when it runs.

### 5.4 The other exits

- `quick_exit(code)`: `'<msil>begin_exit'`, the quick list LIFO, libraries' `'<msil>fini'(1)`, then `_Exit(code)`.
- `_Exit`/`_exit` on Windows triples: `ExitProcess(code)` (kernel32 is a default library of the Windows triples, so
  msil-ld binds it; `-S` text needs a `pinvokeimpl("kernel32.dll" nomangle cdecl)` stub, a D14 extension).
  Behaviour on all three runtimes: no ProcessExit, `ucrtbase` flushes stdio (= native /MD `_Exit`).
- `_Exit`/`_exit` on Linux triples: native glibc (exit_group: no ProcessExit, no flush = native glibc).
- `exit` inside this assembly's own ProcessExit handler (a .NET host exits, a C handler of the library calls
  `exit`): run the remaining list, `Environment.ExitCode = code` (measured: effective inside ProcessExit), then
  terminate natively without `Environment.Exit`: Windows `ExitProcess(code)`, Linux glibc `exit(code)` (measured:
  native `exit(9)` inside ProcessExit gives 9 on all runtimes, Linux flushes). `exit` of *another* toolchain
  assembly called from such a handler still deadlocks on .NET 8/10 (it cannot know); a documented limit.
- `abort`: native, unchanged.
- Exit status: `Environment.Exit(int32)` passes 32 bits on Windows (300 -> 300, -1 -> 0xFFFFFFFF) and the low
  8 bits on Linux (300 -> 44), exactly as native.
- stdio: no explicit `fflush(NULL)` is needed: the host flushes after `Environment.Exit` and after ProcessExit,
  i.e. after every C handler and destructor, as C requires (measured on fw, 8, 10, L8, L10, including output
  written inside ProcessExit). This also covers a MinGW program's `msvcrt.dll` buffers (cil-runtime: both CRTs
  flush at exit).

### 5.5 BCL members (BCLMembers.def additions; all mscorlib-safe)

`System.AppDomain` (`get_CurrentDomain()`, instance `add_ProcessExit(class System.EventHandler)`),
`System.EventHandler` (`.ctor(object, native int)`), `System.EventArgs` (type), `System.Environment::Exit(int32)`,
`System.Environment::set_ExitCode(int32)`, `System.Threading.Thread::get_CurrentThread()` and instance
`get_ManagedThreadId()`, `System.Threading.Monitor::Enter(object, bool&)` and `::Exit(object)`,
`System.Object::.ctor()`, `System.IntPtr` (type), `System.Array::Copy(class System.Array, class System.Array,
int32)`, `System.Threading.Thread::Sleep(int32)` (a second exiting thread waits forever). The IL prototype bound
every one it uses to `[mscorlib]` and ran on Framework, .NET 8 and .NET 10 (Windows and WSL); `set_ExitCode` and
`Thread.Sleep` were exercised only from C#, so `msil/utils/check-mscorlib-safe.py` should confirm those two. With the
.NET 8 reference pack, `msil-ld --print-bcl-binding=type:...` binds `AppDomain`, `EventHandler`, `EventArgs`,
`Environment`, `Array`, `IntPtr` to `[System.Runtime]`, `Monitor` to `[System.Threading]` and `Thread` to
`[System.Threading.Thread]` (measured). Required requests (Q11), no fallback needed.

### 5.6 What other Phase 3 work and the later goals see

- **TLS (P3-A):** `exit` calls the exiting thread's TLS destructors first (glibc's order); handlers run on the
  exiting thread, so `thread_local` and the shadow stack are the ones C expects. The ProcessExit fallback runs on
  the finalizer thread, whose TLS block is created lazily (a documented difference for libraries used by .NET
  hosts).
- **Threads (P3-B):** `exit` from any thread works (measured: second thread, native thread through reverse
  P/Invoke, inside a native `qsort` callback). If P3 creates C threads as `System.Threading.Thread`, they must be
  background threads or a program whose `'<msil>Main'` returns (no exit machinery) waits for them (measured: a
  foreground thread keeps the process 3 s after `Main` returns; `Environment.Exit` does not wait).
- **Function pointers (P3-B):** see the `calli` note in 5.3.
- **setjmp/longjmp (P3-B):** `exit` does not unwind; a `longjmp` out of an atexit handler is UB natively and needs
  no support.
- **Helper archive (backlog S3):** not needed here. If P3 decides that list logic should be C in the archive, the
  only IL left is the subscription, `Environment.Exit` and `ExitProcess`; the C/IL split would cost a reserved
  primitive (`__msil_process_exit(int)`).
- **Portable triple (`msil64-unknown-dotnet`, LLVM libc):** not precluded. LLVM libc defines `atexit`,
  `__cxa_atexit`, `exit`, `quick_exit`, `at_quick_exit` (`libc/src/stdlib/`), and its startup runs `.fini_array`
  through `atexit(call_fini_array_callbacks)` registered before `.init_array` (`libc/startup/linux/do_start.cpp:
  74-77, 210-214`), the glibc order. Because the synthesized definitions only fill undefined names, a statically
  linked libc wins; that triple then needs (a) a trailer switch that leaves the destructor tables to the libc, with
  `__init_array_start`/`__fini_array_start`/`_end` defined as ld.lld does, and (b) an OS-layer `_Exit` that calls
  `Environment.Exit`, through managed calls or a reserved toolchain primitive. Both are additive.
- **Managed calls, inline IL, final cleanup:** unaffected. **FeatureIdeas** (enclosed instance model,
  multi-triple assemblies): per-assembly state keeps one exit protocol per assembly; an enclosed-instance model
  would move these fields into the instance, which the per-assembly design eases rather than blocks.
- **Mono/Unity (Design 7.2):** `AppDomain.ProcessExit` and `Environment.Exit` exist there (not run).

### 5.7 Deviations that remain (documented, not bugs)

1. Linux triples: per-assembly lists (section 4).
2. Windows triples run `.CRT$XT*` and `__attribute__((destructor))` (decision L6); native /MD never does.
3. glibc runs a library constructor's handler after the destructors when the library lacks `crtbeginS.o`
   (section 2.5); MSIL runs it in the library's fini.
4. A library used by a .NET host finalizes on the finalizer thread, after the host's own ProcessExit handlers that
   subscribed earlier, within Framework's 2-second limit.
5. `exit` from a library constructor (during the program's initializer) runs only that library's lists
   (prototype `PLIB_EXIT_IN_CTOR`); glibc in that situation runs only atexit handlers, `_dl_fini` not yet being
   registered.

### 5.8 Where the code goes (for work packages)

- `llvm/include/llvm/CLI/ImageLayout.h`, `llvm/lib/CLI/ImageLayout.cpp`: `getDestructorKey(uint32_t, SectionRules)`,
  `getDestructorKey(StringRef, SectionRules)`, `sortDestructors` (ELF reverse, COFF ascending), unit tests.
- `llvm/include/llvm/CLI/Trailer.h`, `llvm/lib/CLI/Trailer.cpp`: `TrailerInput::Dtors` (methods in run order),
  `ExitSymbols` (which C entry points to define and their entities), `LibraryFini` (export-class MemberRefs in
  cascade order), `NativeExit` (the `ExitProcess`/`exit` import for 5.4); `TrailerSink::defineCFunction(name, sig)`
  for C-named methods; the methods of 5.3; `EntryPoint` gains "call exit".
- `llvm/include/llvm/CLI/BCLMembers.def`: section 5.5's members.
- `llvm/lib/Target/MSIL/MSILPrepareIR.cpp`: drop `diagnoseGlobalDtors` (keep the "not a function" check);
  `MSILAsmPrinter.cpp`: FINI entries in `emitObjectEnd` (same checks as INIT, without arguments), whole-program
  `collectDestructors`, `checkSections` stops reporting destructor tables, the trailer's exit symbols replace D14's
  error for those names.
- `lld/MSIL/OutputSegment.cpp` (`getDestructors`), `Writer.cpp` (fill the trailer, find libraries' `'<msil>fini'`),
  `SymbolTable.cpp`/`Symbols.cpp` (the toolchain-definition step of late binding), `Exports.cpp` (`'<msil>fini'`
  on the export class), `MarkLive.cpp` (destructor tables already roots), `LTO.cpp` (FINI under LTO, like
  `getConstructorFunction`), `NativeLibraries.cpp` (callback list: the synthesized names never reach it; keep).
- `msil/utils/linux-oracle/start.c`: the `start2.c` extension (section 2.4).
- `msil/utils/update-expected.py`, `msil/test/M2/08-semantics/Inputs/check-matrix.py`: the hand-written expected
  kind of decision L11.

### 5.9 The IL prototype (evidence for 5.1-5.4)

`lifecycle-files/il/gen.py` writes [`plib.il`](lifecycle-files/il/plib.il) (a library: constructor, destructor,
`atexit`, `exit`, `'<msil>fini'` on the export class `plib`) and [`pprog.il`](lifecycle-files/il/pprog.il) (a
program linking it), both `[mscorlib]`-only, assembled with Framework ilasm and run on fw, 8, 10 (Windows) and
L8, L10 (`il/results-win.txt`, `results-wsl.txt`, `results-wsl-ctor.txt`). Every case gives the intended
transcript and status on all five runtimes:

| Mode | Transcript (all runtimes) | Status |
|---|---|---|
| `ret` (main returns 300) | `h2`, `h1`, program destructor, plib handler, plib destructor (exiting thread); both ProcessExit handlers: "exit already started" | 300 (Linux 44) |
| `exit3` | same | 3 |
| `nested` | `h2`, `h_late` (registered by h2), `h1`, ... | 300 |
| `exitinhandler` | `h2` calls `exit(9)`: `h1`, destructors, plib continue | 9 |
| `thread` | `exit(4)` on a second thread while main sleeps: everything on that thread | 4 |
| `libexit` (plib's own `exit(5)`) | plib handler, plib destructor; then the program's fallback on the finalizer thread: `h2`, `h1`, program destructor | 5 |
| program constructor calls `exit(13)` | program destructor, plib destructor | 13 |
| plib constructor calls `exit(14)` | plib destructor | 14 |

Three hazards were hit on the way and are what shaped 5.1-5.3 (logs `il/results-win-deadlock-v1.txt`,
`results-win-deadlock-v2.txt`):

1. ProcessExit handler blocking on the list lock that the exiting thread still held (nested `exit` inside the
   outer `fini`): Framework survived through its 2-second cut-off, **.NET 8/10 deadlocked**. Fix: the handler
   never takes the lock and returns when `exit_state != 0`.
2. `exit` called from a constructor, i.e. inside `<Module>::.cctor`, with the ProcessExit handler subscribed at
   the start of the initializer: **.NET 8/10 deadlocked** even with the handler and its state moved to a separate
   class without a type initializer, because CoreCLR makes *every* member of a module wait for that module's
   initializer (ECMA-335 II.10.5.3.1). Fix: subscribe at the end of the initializer.
3. PEVerify reports only the prototype's raw `ldftn` passed as `native int` to `atexit` (generated code reads
   `'<msil>fn_'` fields instead, M9); the new methods (`calli`, `Monitor`, `EventHandler` over a global method,
   `newarr`/`ldelem.i`/`stelem.i`, `Environment.Exit`) give no other PEVerify or ILVerify error under
   `msil/utils/msil-verify.py` (ILVerify cannot check `calli`, as always).

Rejected alternative, measured: LLVM's `LowerGlobalDtors` pass (WebAssembly's choice) turns `llvm.global_dtors`
into constructors that register `__cxa_atexit` per priority. For `order.c` it produces
`global_ctors = [c101, cdef, register_call_dtors.101, .200, .300, register_call_dtors]`
(`clang/order-lower-global-dtors.txt`), so `dtor default` would run before `h_ctor` and `d101` before
`h_ctor101`, unlike glibc, and it does not handle `.fini_array` slots at all.

## 6. Decisions

| # | Question | Options | Recommendation | Evidence | User? |
|---|---|---|---|---|---|
| L1 | Where does the C exit sequence run? | (a) inside `AppDomain.ProcessExit` (Design 4.1 as written); (b) on the calling thread in the toolchain's `exit`, then `Environment.Exit`; ProcessExit only as fallback | **(b)** | ProcessExit runs on the finalizer thread (wrong TLS), Framework kills it after about 2.3-2.6 s, `Environment.Exit` inside it deadlocks .NET 8/10 | no |
| L2 | How are `exit`, `atexit` and the family bound? | (a) host libc; (b) IL the trailer defines when undefined; (c) C helpers in the static archive plus IL primitives | **(b)** | `atexit` exported by neither libc; native `exit` skips ProcessExit on .NET 8/10 and skips the native table on Framework; (b) works in `-S` too and needs no archive | no |
| L3 | `_Exit`/`_exit` | (a) native everywhere; (b) Windows `ExitProcess`, Linux native `_exit` | **(b)** | Framework raises ProcessExit on `ucrtbase!_exit` and `quick_exit`; `ExitProcess` raises it nowhere and flushes like /MD | no |
| L4 | Scope of the handler list | per assembly; process-wide via a named AppDomain slot; host libc's list through thunks | **per assembly** | Windows natively keeps one table per module; no shared state between assemblies; host lists are inconsistent across runtimes (section 3) | no |
| L5 | Order across assemblies | (a) ProcessExit subscription order; (b) explicit cascade through `'<msil>fini'` in reverse initialization order, ProcessExit as fallback | **(b)** | matches Windows native (P, C, B); runs on the exiting thread with no time limit; prototype | no |
| L6 | Windows destructor semantics (`llvm.global_dtors`, `.CRT$XT*`) | (a) static-CRT order (after the handlers, ascending section names), run before the stdio flush; (b) /MD fidelity: never run them (diagnose or drop); (c) ELF order everywhere | **(a)** | /MD never runs them, /MT runs them after stdio is torn down and crashes; (a) is what a user of `__attribute__((destructor))` expects and keeps the native COFF order | **yes** (it chooses a behaviour no native build shows) |
| L7 | `'<msil>Main'` | (a) always `exit(main())`; (b) only with exit machinery, else Phase 2's `ret` | **(b)** | keeps every Phase 2 output and test byte-identical; return and `Environment.Exit` give the same status and flush; only foreground threads differ (5.6) | no |
| L8 | When does an assembly subscribe ProcessExit? | start or end of its module initializer | **end** | deadlock of hazard 2 on .NET 8/10 | no |
| L9 | Lowering of `llvm.global_dtors` | (a) `LowerGlobalDtors` to `__cxa_atexit`; (b) FINI entries to the linker | **(b)** | (a) interleaves destructors with ctor-registered handlers and ignores slots; record v1 already has FINI | no |
| L10 | Linux native oracle for destructors and `atexit` | (a) extend E7's `start.c` (`start2.c`); (b) glibc sysroot `crt1.o` + `libc_nonshared.a` | **(a)** | `start2` equals `crt1.o` in 7 modes and 3 matrix cases; no new sysroot dependency | no |
| L11 | Windows expected transcripts for `p6-dtors` and `crt-xt` | (a) hand-written per L6 with a marker that `update-expected.py` keeps and `check-matrix.py` accepts as a new native kind (`rule: <why>`); (b) Windows rows stay Linux-only; (c) restrict them to /MD's `BCDM`/`main` (if L6 = b) | **(a)** | no native build prints them (section 2.4); it amends condition 5's rule ("every case prints the native transcript or a named diagnostic") | **yes** |
| L12 | `exit` inside the assembly's own ProcessExit handler | `Environment.Exit` (deadlock on .NET 8/10); set `ExitCode` and terminate natively | **native termination** | measured: `ExitCode` set inside ProcessExit wins; native `exit` inside ProcessExit gives its status | no |
| L13 | Library cascade order with diamonds | reverse command line; msil-ld simulates initialization through the libraries' AssemblyRefs | **simulate** | section 4 | no |
| L14 | `.CRT$XP*` pre-terminators | keep `section ... is not supported`; run them between handlers and `.CRT$XT*` | **keep unsupported** | user code does not use them; the CRT's own are its stdio teardown | no |
| L15 | `__dso_handle` | define per assembly on ELF triples (data symbol in the image, as ld.lld defines it hidden) | **define** (low priority) | clang's C++ `__cxa_atexit(dtor, obj, &__dso_handle)`; harmless for C | no |
| L16 | Design.md 4.1 and section 6 wording | keep "Destructors and atexit handlers run at process exit (AppDomain.ProcessExit)"; replace | **replace** with: "Destructors (`llvm.global_dtors`, `.fini_array*`, `.CRT$XT*`) and `atexit` handlers run in the C library's order when the program calls `exit` or returns from `main`: the toolchain's `exit`, emitted into each output, runs the assembly's handlers and destructors on the exiting thread, then those of the libraries it links, and calls `Environment.Exit`; `AppDomain.ProcessExit` runs them for an assembly whose exit did not run, such as a library used by a .NET program." Section 6's "`AppDomain.ProcessExit` for global destructors and `atexit`" changes likewise | **yes** (Design.md is the user's) |

**Codegen switches:** none. The design uses only APIs that exist in mscorlib on Framework 4.8 and modern .NET
alike, and its behaviour does not depend on the runtime (the only runtime-specific behaviour left is
Framework's 2-second ProcessExit limit on the fallback path, which no switch could change). clang's existing
`-fregister-global-dtors-with-atexit` keeps working (it calls the synthesized `atexit`).

## 7. Tests

### 7.1 Matrix rows to flip (P3-A)

| Row | Phase 3 result | Native source |
|---|---|---|
| `p6-dtors` | Linux `BCDMcb`; Windows `BCDMbc` (`.CRT$XTA00120` then `.CRT$XTA00150`) | Linux: E7 `start2`; Windows: hand-written (L11) |
| `fini-array` (linux-gnu) | `main`, `fini_array entry` | E7 `start2` |
| `crt-xt` (windows-msvc) | `main`, `CRT$XT entry` | hand-written (L11) |
| `atexit-order` | `main returns`, `second registered, runs first`, `first registered, runs last` | Windows /MT and /MD natively; Linux E7 `start2` (defines `atexit`) |
| `09-hosted/glibc-atexit.test` | runs: `hello`, `goodbye` | glibc |

### 7.2 New cases

- `exit-order` (08-semantics, both triples, obj and lto, every runtime): `order.c`'s modes `ret`, `exit`,
  `nested`, `exitinhandler`, `exitindtor`, `atexitindtor`, `exitinctor`; Linux from `start2`; Windows the
  /MD transcript plus L6's destructors (hand-written destructor tail).
- `quick-exit` and `underscore-exit`: `at_quick_exit` order; stdout flushed on Windows, lost on Linux (both
  native).
- `exit-status` (not a transcript case; D37 limits transcripts to 0-255): `exit(300)` and `return 300` give 300 on
  Windows and 44 on Linux, `exit(-1)` 0xFFFFFFFF/255; a lit test with explicit per-OS status checks.
- `p2c-exit-order` (04-shared): program + libraries B, C with handlers, destructors and a library `exit`; Windows
  handler order from the native /MD oracle (identical to L5), destructors hand-written; Linux hand-written per
  section 4. Plus a diamond (L13).
- .NET host: a C# program (csc, `msil_require_csc`) using a `-shared` library with a destructor and an `atexit`
  handler; both run at the host's exit; on Framework the handler stays under 2 s.
- Deadlock regressions, each with a timeout: `exit` from a program constructor, from a library constructor, from
  a handler, from a destructor, from a second thread, and `exit` inside a library's ProcessExit fallback (L12);
  on all five runtimes.
- `abort`: Linux only (status 134); on Windows WER may hold the process for minutes (measured in this run), so no
  Windows execution test.
- llc/FileCheck: `llvm/test/CodeGen/MSIL/dtors-*.ll` for FINI in objects (`llvm-cil-dump --link-info` shows
  `Fini` entries), whole-program `'<msil>fini'`, `'<msil>run_dtors'` order on both rule sets, and the synthesized
  C entry points in `-S` text.
- lld/test/MSIL: `obj-dtors.test` (ELF and COFF order, null `.CRT$XT` slot skipped, null `.fini_array` slot error,
  comdat-discarded FINI entry), `obj-exit-synth.test` (definitions only when undefined; an object's `exit` wins;
  `--print-imports` no longer lists `exit`/`atexit`; `_Exit` binds `kernel32!ExitProcess` on windows-msvc and
  `libc.so.6!_exit` on linux-gnu), `shared-fini.test` (`'<msil>fini'` on the export class and the cascade order);
  update `obj-sections-unsupported.test`, `native-static-only.test`, `libsearch.test`, `native-elf-script.test`,
  `native-callback.test` (section 1.2).
- Unit tests for `getDestructorKey`/`sortDestructors`.

## 8. Risks and open points

- The fallback path (ProcessExit) cannot avoid Framework's 2-second limit or the finalizer thread; only libraries
  used by .NET hosts take it.
- A toolchain assembly whose code is called from another toolchain assembly's ProcessExit handler and calls
  `exit` there still deadlocks .NET 8/10 (5.4); rare, documented.
- A native library that calls `exit` itself bypasses the toolchain's protocol on .NET 8/10 (no ProcessExit) and
  takes Framework's `CorExitProcess` path; outside the toolchain's control.
- The Windows destructor behaviour (L6) and the hand-written expected files (L11) are user decisions.
- The MinGW triple (`x86_64-w64-windows-gnu-msil`) follows the COFF rules here; native MinGW runs `.dtors`
  through `atexit(__do_global_dtors)` from `__main`, a different interleaving. Not an execution target before P4.
- Size: lib/CLI about 500-700 lines (keys, trailer methods, BCL rows), the backend about 150, msil-ld about 300,
  the oracle 30, tests about 40 files.

## 9. Files

All under [lifecycle-files/](lifecycle-files/):

- `native/order.c`, `build-linux.sh`, `build-win.ps1`, `run-linux.sh`, `run-win.ps1`, `run-ctor-linux.sh`;
  results `results-linux-glibc.txt`, `results-windows.txt` (/MT and /MD, 13 modes), `results-windows-extra.txt`
  (exit from a constructor, several modules, matrix cases).
- `native/multi/` (`libpb.c`, `libpc.c`, `main.c`, `results-linux.txt`; Windows results in
  `results-windows-extra.txt`), `native/e7/start2.c` with `e7-results.txt`, `native/matrix/` (builds and Linux
  results of the four matrix cases).
- `dotnet/Program.cs`, `lifecycle.csproj`, `run-win.ps1`, `run-wsl.sh`, results `win-results-{1..4}.txt`,
  `wsl-results-{1,2}.txt`.
- `il/gen.py`, `plib.il`, `pprog.il`, `run-ctor.ps1`, `run-win.ps1`, `run-wsl.sh`, `run-wsl-ctor.sh`, results
  `results-win.txt`, `results-wsl.txt`, `results-wsl-ctor.txt`, and the two deadlock logs.
- `clang/dtor.c`, `clang/order-lower-global-dtors.txt`.
