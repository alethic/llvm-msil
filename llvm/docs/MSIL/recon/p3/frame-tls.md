# Phase 3 recon: frames, dynamic stack allocation and thread-local storage (P3-A)

Key: `frame-tls`. Branch `msil` at `cc4c79787d6a` (M23). Scratch: `C:\llvm-msil-scratch\p3\frame-tls`.
Supporting files (sources, IL, scripts, trimmed logs): [frame-tls-files/](frame-tls-files/).

## 0. Summary

**What exists.** The static frame is one `localloc` per function (D13), a 2 GiB cap, no epilogue
(`llvm/lib/Target/MSIL/MSILFrameLowering.cpp:74-162`). Dynamic allocas (VLAs, `alloca(n)`), `llvm.stacksave`/
`llvm.stackrestore`, `llvm.frameaddress` and `llvm.stackaddress` are diagnosed in ISel
(`MSILISelLowering.cpp:293-321`, `:595-612`, `:1232-1249`). `thread_local` is an error in `MSILPrepareIR`
(`MSILPrepareIR.cpp:2320-2325`), with backstops in ISel (`:655-663`) and the AsmPrinter (`MSILAsmPrinter.cpp:1918-1922`).
Phase 2 already reserved most of the TLS plumbing: the link-info record v1 has the `ThreadLocal` symbol flag (0x100), the
`SegTLS` segment flag (0x2) and the `TLSOffset32/64` relocations (`llvm/include/llvm/BinaryFormat/MSILLink.h:110-179`).
The library symbol table has `LibraryDataSymbol::ThreadLocal` (0x2). `MSILDataSegment` already names `.tdata`/`.tbss` and
sets `SegTLS` (`MSILDataSegment.cpp:147-148, 187-192`). msil-ld already checks `TLS attribute mismatch`
(`lld/MSIL/SymbolTable.cpp:272, 693`). Every consumer stops with `thread-local storage is not supported yet`.

**What was measured** (Framework 4.8, .NET 8.0.33 and .NET 10.0.14 on Windows; .NET 8.0.31 and 10.0.13 under WSL;
native clang on both OSes):

- **Option A for VLAs (one `localloc` per dynamic alloca) breaks real loops.** `localloc` memory is freed only at
  return. Each iteration grows the CLR stack by the size rounded up to 16 bytes (1 -> 16, 24 -> 32, 100 -> 112;
  identical on all runtimes). A 64-byte VLA in a loop overflows after 23,552 iterations under `dotnet` on Windows
  (1.5 MiB main thread), after 8,192-16,384 on Framework (1 MiB `.exe` reserve), and after 130,048 under WSL (8 MiB).
  Each overflow kills the process (`0xC00000FD`, or SIGABRT 134). Native clang reuses the same address for 1,000,000
  iterations (E1).
- **Option A for TLS (one `[ThreadStatic]` field per variable) is impossible.** `&tlsvar` must be a stable unmanaged
  address. On .NET 10 even primitive `[ThreadStatic]` fields move on every compacting GC (20/20 moves for `int`, `long` and
  `IntPtr`). On every runtime a non-primitive value-type thread static (a C array or struct) moves (E3).
- **The `[ThreadStatic]` pointer to an unmanaged per-thread block (Design.md 4.4) works on every runtime and OS.** The IL
  prototype `proto_tls.il` (template copy, relocations applied once, `.tbss` zeroed, lazy first use, finalizable holder)
  runs identically on all five combinations. It gives distinct per-thread addresses, the right values and four of four
  blocks freed. Its verifier output has only the allow-listed categories (E11).
- **Cost of one `[ThreadStatic]` read with the null check** (ns, best of 5 under 100% host load): Framework 32-38,
  **.NET 8 69-153 (Windows) and 127-262 (WSL)**, .NET 9 9.5, .NET 10 10-25. The .NET 8 figure is a .NET 8 property
  (`ThreadLocal<T>`, which uses CoreLib's thread statics, takes 25-30 ns there). It favours one block lookup per function
  (hoisted) over one per access: four accesses cost 99 vs 377 ns on .NET 8, 64 vs 137 on Framework and 37 vs 59 on .NET
  10 (E2).
- **Thread exit: a finalizable holder is the only BCL hook, and it works.** It freed 2000 of 2000 blocks for managed
  threads and for native threads (`CreateThread`, `pthread_create`) on all runtimes (E5). Without it, each thread leaks
  the block plus about 2-4 KiB. .NET Framework finalizes *reachable* holders at process exit while background threads
  still run (`HasShutdownStarted=True`), so the finalizer must not free then (E10). Native exit hooks are no portable
  alternative: an `FlsAlloc` callback into managed code **kills .NET 10** ("Attempt to execute managed code after the
  .NET runtime thread state has been destroyed"). `pthread_key_create` destructors work on Linux, but on .NET 8 the
  thread's `[ThreadStatic]` state is already gone when they run (E7).
- **Native threads become managed threads on the reverse P/Invoke.** `[ThreadStatic]` is fresh and distinct on 2000 of
  2000 `CreateThread`/`pthread_create` threads (E5). `errno` read through `__errno_location()`/`_errno()` was never
  clobbered: first call, 2,000,000 calls under GC pressure, and two threads with different errnos all gave 0 mismatches
  (E6: 2,000,000 iterations under WSL, 100,000 on Windows). `ExitThread`/`pthread_exit` called from managed frames
  end the thread cleanly but skip `finally` (E8).
- **A chunked shadow stack works.** The C# model of the proposed helpers (E9) ran 1,000,000 VLA iterations of
  1..4096 bytes with one 64 KiB chunk, and an 8 MiB recursion with 6 chunk allocations (1 on the second run).
  After a longjmp-like exception, the setjmp-site restore brings `Sp` back. The cost per call
  (save + alloca + restore, one block lookup) is 41-45 ns on .NET 10, 63 on Framework and 95-174 on .NET 8. A
  `localloc` costs 11-23 ns.

**Recommendations** (section 8 has the decisions):

1. **Dynamic alloca: hybrid H.** An alloca that lies on no CFG cycle stays a `localloc`. It needs no runtime support,
   is freed at return, survives `longjmp` and exceptions, and its memory bound equals the static frame's. An alloca on a
   cycle goes to a per-thread, per-assembly **chunked shadow stack** whose state is the header of the per-thread block.
   `llvm.stacksave`/`stackrestore` become shadow-stack save/restore in functions that need them and no-ops elsewhere. An
   IR pass, `MSILLowerDynamicAlloca`, modeled on `SafeStack::moveDynamicAllocasToUnsafeStack`, does the rewrite before
   ISel. The `setjmp` sites and P3-B's `fault` clauses restore `Sp` (sjlj recon S7).
2. **thread_local: option B.** Each output assembly has one per-thread block: a 64-byte header, then a copy of the TLS
   template (`.tdata` then `.tbss`). A `[ThreadStatic] native int` on a private, `beforefieldinit` class `'<msil>Thread'`
   without a `.cctor` points to it. `'<msil>tls'()` (aggressive inlining) allocates it on first use. A finalizable
   `'<msil>Thread'` instance in a second `[ThreadStatic]` field frees it, and skips the free when `HasShutdownStarted`.
   The IR pass `MSILLowerThreadLocal` computes `TP = '<msil>tls'()` once per function, at the nearest common dominator of
   the uses and outside loops. A definition's address is `TP + ldsfld <offset field>`. A declaration's address is a call
   of a per-symbol accessor `[msil.extern]'<msil>TLSAddr'::'<name>'()`, which msil-ld binds to a synthesized local
   accessor or to a library's `'<msil>addr_<name>'()`.
3. **No codegen switch is needed.** All APIs are mscorlib-safe: `ThreadStaticAttribute`, `Marshal.AllocHGlobal`/
   `FreeHGlobal`, `Environment.HasShutdownStarted`, `Object::.ctor`. `ThreadLocal<T>` is not worth a switch (E2).
4. **TLS models collapse.** General-dynamic, local-dynamic, initial-exec and local-exec all lower the same way.
   `-shared` libraries export TLS through accessors that return the calling thread's address (plan section 12 '-' row
   of 2026-10-27).
5. **Running C/Rust thread-exit destructors on the exiting thread is not possible in general.** Recommend: free memory
   by finalizer always. Run registered destructors only on threads that start through the toolchain's thread-start
   thunks (P3-B) and on the main thread at exit. Put `FlsAlloc`, `__cxa_thread_atexit_impl` and `__cxa_atexit` on the
   callback list now.
6. **Matrix:** `p5-tls` flips to `transcript: 12` on obj and lto for both triples. Proposed new rows: `tls-xtu-init`,
   `tls-shared-export`, `tls-weak-undef`, and with P3-B threads `tls-threads`. Proposed new tests: `vla_cases.c`,
   `tls_threads.c`, the llc/lld tests of section 9. The tests that pin today's texts are listed in 2.4.

---

## 1. Method and caveats

- Every CLR claim was measured with a C# or IL program, compiled once against mscorlib 4.0 (`build-jitall.py --csharp
  --refs net48 --unsafe`, or Framework ilasm 4.8 for IL). It ran as the `.exe` on .NET Framework 4.8.9363 and with
  `dotnet exec --runtimeconfig rc{8,10}.json` on .NET 8.0.33 and .NET 10.0.14 (Windows). Under WSL Ubuntu (glibc 2.43)
  it ran on the private .NET 8.0.31 and 10.0.13 (`$HOME/.dotnet-msil`). Scripts: `frame-tls-files/scripts/`.
- C behaviour was compared with native clang from `D:\llvm-msil\build\bin`: `x86_64-pc-windows-msvc` in the VS dev
  shell, and `x86_64-unknown-linux-gnu` with ld.lld against `C:\llvm-msil-scratch\glibc-sysroot` (crt1/crti/crtn,
  `-nodefaultlibs -lc`), run under WSL.
- **The host was at 100% CPU from other work** for the whole session (`Win32_Processor.LoadPercentage` = 100). Every
  timing is the best of 5-7 interleaved rounds. Absolute nanoseconds are inflated, perhaps 2-3x. Ratios between variants
  in one run are the meaningful numbers.
- No tracked file was modified. No compiler was rebuilt.
- Installs: none.

---

## 2. Today's frame model and the Phase 3 diagnostics (as built at `cc4c79787d6a`)

### 2.1 Static frames (D13)

- `MSILFrameLowering::needsFrame` (`MSILFrameLowering.cpp:42-45`): a frame exists when `getStackSize() != 0 ||
  isFrameAddressTaken()`. `hasFPImpl` returns the same. `hasReservedCallFrame` is always true, because arguments travel
  on the evaluation stack (`:51-56`).
- `getLocallocAlign` returns 8 (`:65-72`). A constant-size `localloc` of 32 bytes or less is only 8-aligned on Framework
  and in .NET 8 tier-0 code (D13). Variable-size `localloc` results were 16-aligned on every runtime (cil-runtime e20).
- `emitFrameBase` (`:74-138`) emits `ldc.i4 Size; localloc` as the first IL after the `ARGUMENT_*` pseudos. When
  `MaxAlign > 8` it over-allocates `MaxAlign - 8` and aligns up. A frame above `INT32_MAX` is diagnosed ("stack frame
  of N bytes is larger than the 2 GiB that MSIL supports") and clamped. There is no epilogue (`:159-162`): CIL frees
  `localloc` memory when the method returns.
- The `LOCALLOC` record is `defm LOCALLOC : I<(outs I64:$p), (ins I32:$size), ...>` (`MSILInstrFrame.td:19`). The size
  is **I32**, so a dynamic size needs a `native int` form (section 3.6).
- CIL rule: `localloc` needs an otherwise empty evaluation stack. `MSILRegStackify.cpp:850-854` never stackifies a
  `LOCALLOC` def. `MSILStackCheck.cpp:185-186` fails a `LOCALLOC` at depth != 1. ECMA-335 III.3.47 also forbids
  `localloc` in a filter, catch, finally or fault block. A `try` block is allowed.
- Constant-size allocas (C locals, byval copies from M12's byval slot pool, vararg buffers per the variadics recon,
  DAG temporaries) are all frame objects in this one block. `.locals` is never `init`, so frames are not zeroed.
- Measured localloc rounding (E1): the CLR stack grows by `alignTo(n, 16)` per `localloc` of n bytes (n = 1, 24, 64, 100,
  4096 gave 16, 32, 64, 112, 4096) on Framework, .NET 8 and .NET 10.

### 2.2 What Phase 3 features report today (find with `grep -n "phase 3\|not supported" MSILPrepareIR.cpp MSILISelLowering.cpp lld/MSIL/*.cpp`)

| Construct | Where | Text |
|---|---|---|
| alloca with a variable count, or outside the entry block | `MSILISelLowering.cpp:304` (Custom), `:595-598` | `dynamic alloca / VLA is not supported for MSIL yet (phase 3 shadow stack)` |
| `llvm.stacksave` / `llvm.stackrestore` with no dynamic alloca in the function | `:305-306`, `:600-602`, `LowerStackSaveRestore` `:1232-1249` | `llvm.stacksave is not supported for MSIL yet (phase 3 shadow stack)` (and `llvm.stackrestore ...`). In a function with a dynamic alloca they are silent (`hasVarSizedObjects()`), so one VLA gives one error |
| `llvm.frameaddress` | `:307-310`, `:603-607` | `llvm.frameaddress is not supported for MSIL yet (phase 3 shadow stack)` |
| `llvm.stackaddress` | `:319-321`, `:608-612` | `llvm.stackaddress is not supported for MSIL yet (phase 3 shadow stack)` |
| returnaddress, addressofreturnaddress, sponentry, trampolines, eh.dwarf.cfa, eh.return | `:613-643` | `... is not supported for MSIL` (permanent) |
| `llvm.eh.sjlj.setjmp/longjmp` | `:629-638` | `... (phase 3 setjmp/longjmp)` (sjlj recon) |
| `thread_local` global | `MSILPrepareIR.cpp:2320-2325` (error, `replaceThreadLocalAddressCalls`, `setThreadLocal(false)`) | `thread-local variable @<name> is not supported for MSIL yet` |
| same, ISel backstop | `MSILISelLowering.cpp:288-289` (GlobalTLSAddress Custom), `:655-663` | same text |
| same, AsmPrinter backstop | `MSILAsmPrinter.cpp:1918-1922`; TLS section class `:956-961` | same text; `thread-local storage is not supported yet: <g> in <file>` for a `.tdata`/`.tbss` section on a non-TLS global |
| `extern_weak` thread-local declaration | `MSILPrepareIR.cpp:1780-1781, 1809` (`lowerExternWeak` leaves it) | the `thread_local` error (row 2026-10-21 M12) |
| thread-local alias | `MSILPrepareIR.cpp:1605-1676` (`replaceAliasThreadLocalAddressCalls`) | lowered to the aliasee plus an offset, then the TLS error |
| `llvm.thread.pointer` (`__builtin_thread_pointer`) | `MSILPrepareIR.cpp:1132-1146` | `thread pointer access ('llvm.thread.pointer') is not supported for MSIL yet (phase 3 TLS)` |
| TLS segment in an object (msil-ld) | `lld/MSIL/OutputSegment.cpp:80-91` | `thread-local storage is not supported yet: <seg> in <file>` |
| TLSOffset32/64 relocation | `OutputSegment.cpp:303-306` | same, naming the symbol |
| `-shared` TLS export candidate | `lld/MSIL/Exports.cpp:212-215` (not a root), `:313-318` | `thread-local storage is not supported yet: <s> in <file>` |
| TLS segment in whole-program layout | `llvm/lib/CLI/ImageLayout.cpp:256-263` (rule 7), text `:200-201` | `thread-local storage is not supported yet: segment ...` |
| TLS vs non-TLS definitions or library export | `SymbolTable.cpp:272-273`, `:683-695` | `TLS attribute mismatch: <s>` (**stays**) |
| native TLS data (`errno@GLIBC_PRIVATE`, a `.so`'s `__thread`) | `NativeLibraries.cpp:152-156`, `:615-651` | `symbol '<name>' in <module> is thread-local data and cannot be imported[; use <errno.h>]` (**stays**) |

### 2.3 TLS groundwork that already exists

- Record v1 (`MSILLink.h`): `SymbolFlags::ThreadLocal = 0x100` (DATA only, never with `Import`; `:110, 120-122, 136`),
  `SegmentFlags::SegTLS = 0x2` (`:151-157`), `RelocType::TLSOffset32 = 5`, `TLSOffset64 = 6` ("the offset of a TLS
  symbol in the output's TLS template + addend (phase 3)", `:164-179`). Plan 5.8.8 rows 2700, 2720, 2749-2752.
- `cli::LibraryDataSymbol::ThreadLocal = 0x2` (`llvm/include/llvm/CLI/SymbolTable.h:67`). Its printer spells
  `Data[ CONST][ TLS][ WEAK]`.
- `cli::SectionClass::TLS` (`ImageLayout.h:136-138`): `.tdata`, `.tbss` and their `.N` forms, plus `.tls`/`.tls$*`
  under COFF rules. It is classified at `ImageLayout.cpp:47, 72`.
- `MSILDataSegment::getSegmentName` names TLS globals `.tdata`/`.tbss` and sets `SegTLS` (`MSILDataSegment.cpp:147-148,
  187-192`). Whole-program mode leaves TLS segments out of the image (`:219-221`). `:480` is the "thread-local
  zerofill" unexpected path.
- msil-ld: `Symbol::tls` from the record (`InputFiles.cpp:530, 804`), the TLS attribute checks above, and `MarkLive.cpp`
  treating `.tdata`/`.tbss` as ordinary data roots (`:96-110`).
- The plan's '-' row of 2026-10-27 fixes the library contract. A TLS export sets `ThreadLocal` and its
  `'<msil>addr_<name>'` accessor returns the calling thread's address. Importers keep calling the accessor.

### 2.4 Tests that pin today's texts (all change in P3-A)

- `llvm/test/CodeGen/MSIL/dynamic-alloca-error.ll`: every frame-operation text, at -O0/-O2 on both x86_64 triples. The
  VLA, alloca, stacksave/restore and (if decided) frameaddress(0) functions become positive tests, and the permanent
  texts stay.
- `llvm/test/CodeGen/MSIL/globals.ll` (split files `tls.ll` 511-535 and `llvmname-tls.ll` 570ff: the IL after the
  error is pinned as `ldsfld native int 'tv'`), `alias.ll` (56, 529), `extern-weak.ll` (332), `object-errors.ll`
  (43, 70, 493-501), `unsupported-ir-errors.ll` (9, 58-62, 563), `section-errors.ll` (18, 88, 177),
  `sections-whole-program.ll` (444, 472), `target-intrinsic-errors.ll` (130, only if `llvm.thread.pointer` changes).
- `lld/test/MSIL/obj-gc-sections.test` (174), `obj-sections-unsupported.test` (15, 65), `shared-export-selection.test`
  (19, 204). `type-mismatch.test` (TLS attribute mismatch) and `native-elf-resolve.test` (errno) stay.
- `msil/test/M2/08-semantics/p5-tls.test` and `matrix.txt:41`. The native result is 12.

---

## 3. Dynamic alloca and `stacksave`/`stackrestore`

### 3.1 What clang gives us

- A VLA scope is bracketed by `llvm.stacksave`/`llvm.stackrestore` at every optimization level. At -O2 the loop below
  becomes a peeled first iteration plus the loop, each with its own save/restore (`c/vla_loop.c`; IR excerpt in
  `results/native-c.txt`). `alloca(n)` (`__builtin_alloca`) has no bracket: its memory lives until return.
- LLVM semantics: `stackrestore(p)` frees every alloca made since the `stacksave` that produced p. A VLA in a loop
  therefore reuses the same bytes: native clang at -O0 gave the same address for all 1,000,000 iterations, and -O2 the
  same after the peeled first one.

### 3.2 Option A: `localloc` per dynamic alloca (measured, E1)

`frame-tls-files/src/E1Loop.cs`: one `stackalloc byte[n]` (= `localloc`) per iteration, n = 64, 1,000,000 iterations.

| Runtime | Thread stack | Last report before the overflow | Result |
|---|---|---|---|
| .NET 8.0.33 / 10.0.14, Windows (`dotnet exec`) | host main thread, about 1.5 MiB | 23,552 iterations = 1,507,328 bytes | `Stack overflow.`, exit `0xC00000FD` |
| .NET Framework 4.8 (`.exe`, csc's 1 MiB reserve) | 1 MiB | 8,192 iterations = 524,288 bytes (16,384 not reached) | `Process is terminated due to StackOverflowException.`, `0xC00000FD` |
| .NET 8.0.31 / 10.0.13, WSL | 8 MiB (`ulimit -s`) | 130,048 iterations = 8,323,072 bytes | `Stack overflow.`, SIGABRT (134) |
| native clang, Windows and Linux | 1 MiB / 8 MiB | 1,000,000, same address | completes |

Option A is correct only where the alloca runs at most once per call. An alloca on no CFG cycle cannot run more than once
per invocation, so its total is bounded by the sum of those allocas' sizes. That is the static frame's bound and
matches the native stack limit (1.5 MiB vs 1 MiB on Windows, 8 MiB vs 8 MiB on Linux). The cost is that of a
`localloc`: 11-23 ns per call with the 64-byte case (E9).

### 3.3 Option B: a per-thread shadow stack

**Model and precedent.**
- WebAssembly keeps a `__stack_pointer` global: `ISD::STACKSAVE/STACKRESTORE/DYNAMIC_STACKALLOC` are `Expand`
  (`WebAssemblyISelLowering.cpp:372-375`). The prologue reads the global or `__wasm_get_stack_pointer()` in
  "libcall thread context" mode, and the epilogue and call frame pseudos write it back
  (`WebAssemblyFrameLowering.cpp:237-256` `writeBackSP`, `:274-350` `emitPrologue`).
- wasm-ld's default stack is 64 KiB (`-z stack-size`, default `WasmDefaultPageSize`; `lld/wasm/Driver.cpp:610-611`).
- Emscripten's `invoke_*` JS wrappers save the stack with `stackSave()` and call `stackRestore(sp)` in their catch
  path, which is how SjLj and C++ EH keep `__stack_pointer` right. Each pthread has its own stack and its own
  `__stack_pointer` value.
- LLVM's SafeStack is the closest IR-level model. `SafeStack::moveDynamicAllocasToUnsafeStack`
  (`llvm/lib/CodeGen/SafeStack.cpp:667-725`) turns each dynamic alloca into `load SP; sub; ptrmask; store`. It turns
  stacksave into a load and stackrestore into a store. `createStackRestorePoints` (`:430-464`) re-stores the pointer
  after every `returns_twice` call and every landing pad (`:384-411`). The pass runs in `TargetPassConfig::addISelPrepare`
  (`TargetPassConfig.cpp:833`).

**Proposed design (`frame-tls-files/src/E9Shadow.cs` is the executable model).**
- State lives in the header of the per-thread block (4.2): `Sp`, `Lo`, `Hi` (current chunk bounds), `Cur` (current
  chunk) and `Spare` (one popped chunk kept for reuse).
- The stack is a list of `Marshal.AllocHGlobal` chunks. The first is 64 KiB, as wasm-ld's default; each next one is at
  least twice the previous and at least the request. A chunk has a 32-byte header {Prev, Size}.
- Fast paths are inline IR:
  - alloca: `if (Sp - Lo >= n + align - 1) Sp = (Sp - n) & -align`;
  - save: `Sp`;
  - restore(p): `if (Lo <= p <= Hi && Cur) Sp = p`.
- Slow paths are private IL helpers that msil-ld or the trailer emits: `'<msil>salloc'(h, n, align)` pushes a chunk,
  reusing the spare when it fits, and `'<msil>srestore'(h, p)` pops chunks. `p == 0` means "before the first chunk":
  pop to the bottom chunk and keep it. **Without that rule a top-level VLA function thrashed: every call took both slow
  paths. Per call with one block lookup, before -> after the fix: Framework 220 -> 63 ns, .NET 8 311 -> 95,
  .NET 10 121 -> 41, WSL .NET 8 548 -> 174, WSL .NET 10 152 -> 45 (E9).**
- Allocation is lazy, on a thread's first shadow alloca. Only threads that run such code pay for the 64 KiB chunk. The
  holder's finalizer frees the chunks with the block.
- "Overflow" is chunk growth. An `AllocHGlobal` failure throws `OutOfMemoryException`, which no C code catches. The
  helper should turn it into `Environment.FailFast("out of memory for the shadow stack (dynamic alloca)")`, the analogue
  of a native stack overflow. No fixed limit and no switch are needed. `-z stack-size` is not proposed.
- Functions with shadow allocas save `Sp` at entry and restore it before every `ret`.
- **setjmp/longjmp** (sjlj recon 3.6, decision S7):
  - each `setjmp` site saves `Sp` and the dispatch path restores it;
  - every function that moves `Sp` gets a `fault` clause that restores its entry `Sp`, so frames that an exception
    abandons (a cross-assembly longjmp, a .NET exception through C frames) give their chunks back.
  - The save at a `setjmp` site must read the block pointer *without* allocating: null means `Sp = 0`.
  - E9 test 3: after an exception through 50 VLA frames, the setjmp-site restore gives `Sp == saved` (7). Without the
    restore, `Sp` stays below the setjmp point (-1).
- Without the `fault` clauses, a leak heals at the next restore of an outer frame on the same thread, because restores
  write absolute saved values. Only a C# loop that calls exported C code which throws each time grows without bound.
- Cost (E9, best of 7, 3M calls, one 64-byte alloca per call):

| | localloc(64) | shadow, block looked up three times | shadow, looked up once (proposed) |
|---|---|---|---|
| Framework 4.8 | 22.6 ns | 148.8 ns | 62.9 ns |
| .NET 8.0.33 Windows | 11.3 | 286.8 | 94.7 |
| .NET 10.0.14 Windows | 12.2 | 68.6 | 40.8 |
| .NET 8.0.31 WSL | 12.4 | 481.9 | 174.3 |
| .NET 10.0.13 WSL | 12.8 | 89.0 | 44.9 |

The shadow cost is one block lookup plus about 10-25 ns. Inside a loop the alloca/restore pair is a few loads and stores
on the header, and the lookup is per function call.

### 3.4 Option C: `localloc` plus reuse after `stackrestore`

There is no way to free `localloc` memory before return. The viable form gives each alloca site on a cycle a
per-site buffer in the function's locals (`buf`, `cap`), reused when `n <= cap`. When `n > cap`, a new `localloc` of
`max(n, 2*cap)` replaces it.
- This is valid only when the site's previous block is dead whenever it runs again. Every path from the alloca back to
  itself must pass a `stackrestore` of a save that dominates it, which is the clang VLA shape.
- Geometric growth bounds the CLR-stack use per site by about 3-4x the largest request.
- No TLS and no runtime support are involved. Shapes it cannot prove (an `alloca(n)` in a loop that accumulates, a
  restore of another save, a pointer kept across iterations) still need option B or a plain growing `localloc`.
- Its CLR-stack pressure (up to 4x the native peak against a 1-1.5 MiB thread stack on Windows) and the extra IR analysis
  make it a worse complement to A than B is. Not recommended. It stays a possible later optimization for hot VLA loops
  if the shadow stack's per-call lookup ever matters.

### 3.5 Recommendation H: `localloc` for acyclic allocas, shadow stack for allocas on a cycle

- An acyclic dynamic alloca is a `localloc` with a `native int` size. That needs no runtime and no TLS; the memory is
  freed at return and is safe under `longjmp`/exceptions, because the CLR frees it during unwinding.
- A dynamic alloca on a cycle uses the shadow stack.
- `stacksave`/`stackrestore` are shadow save/restore in functions that have a shadow alloca or a `setjmp` site.
  Elsewhere they are deleted: they free nothing that the acyclic `localloc`s hold.
- This replaces Design.md 4.2's last bullet ("Dynamic allocas and stacksave/stackrestore use a per-thread shadow
  stack") with the hybrid. That is a Design.md edit, so the user approves it (D-FT1).

### 3.6 Implementation sketch

- **IR pass `MSILLowerDynamicAlloca`** (new file `llvm/lib/Target/MSIL/MSILLowerDynamicAlloca.cpp`, legacy PM). Add it
  in `MSILPassConfig::addIRPasses` (`MSILTargetMachine.cpp:268-296`) after `MSILPrepareIR` and P3-B's setjmp lowering,
  before `LowerInvoke`.
  - Find the allocas with a non-constant count or outside the entry block (`!AI->isStaticAlloca()`).
  - Classify each as acyclic or cyclic: `isPotentiallyReachable(BB, BB)` or `CycleInfo`/`LoopInfo`; a loop containing
    the block suffices.
  - Leave the acyclic ones as allocas: ISel lowers them to `LOCALLOC`.
  - Rewrite the cyclic ones and the function's `stacksave`/`stackrestore` into the shadow-stack fast paths (3.3) plus
    calls to `'<msil>salloc'`/`'<msil>srestore'`. Use `SplitBlockAndInsertIfThen` for the slow paths; no custom
    inserter is needed.
  - Insert `sp0 = Sp` at entry and `restore(sp0)` before each `ret`.
  - The header pointer `h` is the same `'<msil>tls'()` value as TLS's TP (4.6). Compute it once per function.
  - Delete the `stacksave`/`stackrestore` of functions without shadow allocas or setjmp sites.
- **ISel** (`MSILISelLowering.cpp`):
  - `DYNAMIC_STACKALLOC` becomes a `LOCALLOC` with a `native int` size: a new record `LOCALLOC_I64`
    (`(ins I64:$size)`, opcode 0xFE0F), because CIL `localloc` takes `native unsigned int`. Today's `LOCALLOC` takes
    I32, and truncating a 64-bit size would be silent memory corruption.
  - Realign: over-allocate `align - getLocallocAlign()` and mask when the alloca's alignment exceeds 8.
  - `STACKSAVE`/`STACKRESTORE` no longer reach ISel, so keep the current diagnostic as a backstop for a pipeline
    without the pass.
  - `FRAMEADDR` with depth 0 may return a copy of `FP` (`needsFrame` already allocates a frame when
    `isFrameAddressTaken()`). Depth > 0 keeps the error (D-FT8).
  - `STACKADDRESS` could return the same.
- **RegStackify/StackCheck:** the existing `LOCALLOC` root rule (`MSILRegStackify.cpp:850-854`) and the depth-1 check
  (`MSILStackCheck.cpp:185-186`) also cover a mid-function `localloc`. Add the new record to both.
- **Trailer / msil-ld:** emit the two slow-path helpers only when referenced (section 4.6 lists the members).

---

## 4. Thread-local storage

### 4.1 Option A: one `[ThreadStatic]` field per TLS variable (measured, E3)

`frame-tls-files/src/E3Addr.cs` takes `&field` (C# `fixed`) of `[ThreadStatic]` `int`, `long`, `IntPtr` and a
256-byte struct, and of an ordinary static of the same struct. It churns the heap and runs 20 compacting gen-2 GCs on
the main thread and on a new thread.

| Runtime | int / long / IntPtr thread static | 256-byte struct thread static | ordinary static struct |
|---|---|---|---|
| Framework 4.8 | stable | **moved 20/20** | **moved 20/20** (main thread) |
| .NET 8 (Windows and WSL) | stable | **moved 20/20** | stable |
| .NET 10 (Windows and WSL) | **moved 20/20** | **moved 20/20** | stable |

C takes `&tlsvar` and stores it as a plain pointer (it may hand it to another thread, as `p5-tls`-style code and
`tls_threads.c` do), so option A is ruled out. It would also need one field per variable (no contiguous template, no
`.tbss` block zeroing) and a per-variable first-use initializer for non-zero initial values.

### 4.2 Option B: a `[ThreadStatic]` pointer to an unmanaged per-thread block (recommended)

**Block layout** (one per thread per output assembly; 64-bit triples):

| Offset | Field | Use |
|---|---|---|
| 0 | `Sp` | shadow-stack pointer (0 = no chunk yet) |
| 8 | `Lo` | lowest usable byte of the current chunk |
| 16 | `Hi` | top of the current chunk |
| 24 | `Cur` | current chunk (`Prev` at +0, `Size` at +8) |
| 32 | `Spare` | one popped chunk kept for reuse |
| 40 | `Raw` | the `AllocHGlobal` result before alignment (for `FreeHGlobal`) |
| 48 | `Dtors` | reserved: per-thread destructor list (`__cxa_thread_atexit` and similar, D-FT6) |
| 56 | reserved | |
| H = `alignTo(64, TLSAlign)` | TLS data | copy of `.tdata` (relocated template), then `.tbss` zeros |

- Size = H + TLSSize. `AllocHGlobal` returned 16-aligned memory on all runtimes (cil-runtime e20). For
  `TLSAlign > 16`, allocate `Size + TLSAlign - 16` and align up. `tls_threads.c`'s `_Alignas(64) _Thread_local`
  variable checks it.
- An assembly with no TLS but cyclic allocas has a 64-byte block.

**Classes and helpers** (exact IL in `frame-tls-files/il/proto_tls.il`, which runs on all five runtime/OS
combinations):

```
.class private auto ansi sealed beforefieldinit '<msil>Thread' extends [mscorlib]System.Object {
  .field assembly static native int 'cur'                  // [ThreadStatic]: this thread's block
  .custom instance void [mscorlib]System.ThreadStaticAttribute::.ctor() = ( 01 00 00 00 )
  .field assembly static class '<msil>Thread' 'own'        // [ThreadStatic]: the holder
  .custom instance void [mscorlib]System.ThreadStaticAttribute::.ctor() = ( 01 00 00 00 )
  .field assembly native int 'blk'
  .method ... instance void .ctor(native int)              // Object::.ctor; blk = arg
  .method family hidebysig virtual instance void Finalize() // if (!Environment.HasShutdownStarted)
                                                             //   free chunks, FreeHGlobal(Raw)
  .method assembly static native int Init() noinlining     // AllocHGlobal; zero header; cpblk .tdata from the
                                                             // relocated template; initblk .tbss; set cur; own = new
}
.method assembly static native int '<msil>tls'() aggressiveinlining
  { ldsfld native int '<msil>Thread'::'cur'  dup  brtrue.s L1  pop  call native int '<msil>Thread'::Init()  L1: ret }
```

- **Who checks first use:** `'<msil>tls'()`. Its fast path is a thread-static load, a test and a branch; the JIT inlines
  it (`AggressiveInlining` exists since Framework 4.5). `Init` is `noinlining` and runs once per thread per assembly.
- **The holder** is the same class, so the trailer adds one type, not two. Not `<Module>`: E2b found no cost difference
  between a class with and without a `.cctor`, but `<Module>`'s `.cctor` is the module initializer. Keeping the
  thread-static fields off it avoids any interaction with phase 1/phase 2 initialization of libraries (the 2026-10-27
  spilled-`'<msil>Data'` row shows how easily those interact).
- **The template** lives in the data image. Its `.tdata` bytes, including address and function-address relocations, are
  relocated once by phase 1, like any data. `Init` copies `.tdata` with `cpblk` and zeroes `.tbss` with `initblk`. The
  prototype's `tptr = &g` and `tls_threads.c`'s `tfn = twice` are these relocations. Cross-assembly relocations in the
  template (a TLS variable initialized with the address of a library's variable) are applied by phase 2. The module
  initializer runs before any code of the module, so no thread can copy an unrelocated template.
- **Offsets:** each TLS symbol has an `initonly native int` field holding `H + offset in template`. It is set by the
  module initializer (whole-program: `ldc` + `stsfld`, as the prototype does), the same row shape as D12's address
  fields. Tier-1 code on modern .NET folds `initonly` statics of initialized classes into constants (RyuJIT
  behaviour, not measured here).
- **Verification** (E11): PEVerify and ILVerify on `proto_tls.exe` report only `InitLocals`, `StackByRef` and
  `Unverifiable`, which D21's allow-list already has, plus PEVerify's `[MD] 0x8013129d` warnings on global
  fields/methods, which every Phase 1 output has. `[ThreadStatic]`, the finalizer and the holder class add no new
  category.

**Prototype result** (`proto-tls-run.txt`). Four threads with 1000 `bump()` calls each, plus the main thread:

```
--- wsl net10
thread slot 0: &tcount=0x5f187b69e130 tcount=6 *tptr=42 sum(tzero)=0          (main thread: 5 + 1)
thread slot 1: &tcount=0x7c91b4001f00 tcount=1005 *tptr=42 sum(tzero)=0       (each worker: 5 + 1000)
thread slot 2: &tcount=0x7c91b80010b0 tcount=1005 *tptr=42 sum(tzero)=0
thread slot 3: &tcount=0x7c91b4003270 tcount=1005 *tptr=42 sum(tzero)=0
thread slot 4: &tcount=0x7c91b8001340 tcount=1005 *tptr=42 sum(tzero)=0
blocks freed by finalizers: 4 (main thread's block still live: tcount=7)
```

The same on Framework 4.8, .NET 8 and .NET 10 (Windows) and .NET 8 (WSL); the addresses differ per run.
`tzero` was dirtied by each thread after reading, and the next thread still saw zeros. The first version of the
prototype read an uninitialized loop counter; `.locals` are never `init`, which is the backend's rule as well. The
corrected run is in `results/proto-tls-run.txt`.

### 4.3 Costs and where to put the lookup (E2, E2b, E2c)

`frame-tls-files/results/e2cost-run1.txt` (C#, every measured function `NoInlining`, read-modify-write of one `int`,
best of 5 interleaved rounds of 3M calls, ns/call including the call):

| Access | Framework 4.8 | .NET 8 Win | .NET 10 Win | .NET 8 WSL | .NET 10 WSL |
|---|---|---|---|---|---|
| plain global through its address field (Phase 2 data) | 18.5 | 7.1 | 8.3 | 14.0 | 11.4 |
| A: `[ThreadStatic] int` field | 28.4 | 152.2 | 8.9 | 329.5 | 18.1 |
| B: block, inline check (`'<msil>tls'` inlined) | 56.5 | 105.2 | 21.8 | 192.3 | 32.8 |
| B: block, helper call (not inlined) | 56.3 | 80.1 | 20.0 | 198.2 | 27.3 |
| B: block, thread static on a class with a `.cctor` | 48.8 | 96.2 | 15.7 | 171.5 | 31.7 |
| C: `ThreadLocal<IntPtr>.Value` | 54.8 | 28.3 | 16.4 | 33.2 | 25.3 |
| B: 4 variables, **one lookup** (hoisted TP) | 63.7 | 99.5 | 36.7 | 221.1 | 44.4 |
| B: 4 variables, one lookup per access | 137.0 | 376.8 | 59.1 | 803.0 | 93.4 |

`E2c` (one lookup with its null check, including a delegate call of 5-11 ns; two runs; same column order):
`[ThreadStatic] IntPtr` + check 32-38 / 69-85 / 15 / 127-262 / 20-25 ns.
`ThreadLocal<IntPtr>.Value` 26-27 / 25-30 / 17-23 / 34-79 / 22-45. `Thread.CurrentThread` 11-12 / 12-22 / 12-14.
`.NET 9.0.22`: `[ThreadStatic]` 9.5 ns, so .NET 9 already has the fast path. The .NET 8 slowness held with
`TieredCompilation=0`, `TieredPGO=0`, against net8 reference assemblies instead of mscorlib, on new threads and for
classes without a `.cctor` (E2b). It is a .NET 8 runtime property for non-CoreLib thread statics.

Conclusions:
- One lookup per function. The JIT does not CSE thread-static reads (the 4-access rows), so the backend hoists `TP`
  itself.
- The inlined check and the helper call cost the same within noise. `aggressiveinlining` on `'<msil>tls'` costs
  nothing and lets RyuJIT fold the fast path.
- `ThreadLocal<T>` is faster only on .NET 8. That runtime leaves support on 2026-11-10, two weeks after this recon (not
  verified here: the published .NET support policy). A switch is not worth it: generic `TypeSpec` member references in
  the binder, a static instance, no gain on Framework or .NET 9+.

### 4.4 Freeing the block at thread exit (E5, E7, E8, E10)

| Hook | Framework 4.8 | .NET 8 | .NET 10 | Verdict |
|---|---|---|---|---|
| finalizable holder in a `[ThreadStatic]` field (E5) | managed and `CreateThread` threads: 2000/2000 freed | 2000/2000; WSL `pthread_create` 1000/1000 | 2000/2000; WSL 1000/1000 | **use** |
| same, at process exit (E10) | finalizes **reachable** holders, including a background thread's still in use, `HasShutdownStarted=True` | no finalizers at exit | none | guard with `HasShutdownStarted` |
| `FlsAlloc(callback)` into managed code (E7, Windows) | runs; for CLR-created threads the thread statics are already reset (`tsMark=0`, new managed id) | same | **process dies**: `Assert failure ... "Attempt to execute managed code after the .NET runtime thread state has been destroyed."` (managed and native threads) | never |
| `pthread_key_create(dtor)` into managed code (E7, WSL) | n/a | runs; thread statics already gone (`tsMark=0`, new id) | runs; thread statics intact | works, but not portable |
| `ThreadLocal<T>(trackAllValues)` | no per-thread exit callback (`Values` lists live threads only) | same | same | no |
| `ExitThread`/`pthread_exit` from managed frames (E8) | thread ends, `finally` skipped, holder finalized 3/3 | same | same | no wrapper epilogue runs |

Leak without the holder (E5, 64 KiB block, every page touched): +66.4 to +68.4 KiB of private bytes per thread on
Windows. Under WSL, RSS is +74 to +77.5 KiB without and +40 to +43.5 KiB with the holder; glibc keeps freed 64 KiB
chunks in its arenas, while the finalizer count shows that all were freed. The holder's own overhead is one small object
plus a finalization entry, reclaimed by the next GC.

### 4.5 `llvm.threadlocal.address` and the TLS models

- `llvm.threadlocal.address` is `IntrNoMem, IntrSpeculatable` (`llvm/include/llvm/IR/Intrinsics.td:2166-2169`).
  Within a function, the address is a pure function of the thread, so hoisting `TP` is legal (C has no coroutines;
  that intrinsic exists to stop hoisting across coroutine suspension).
- Direct uses of a TLS global without the intrinsic are still valid IR (`Verifier.cpp:7442-7447` only checks the
  intrinsic's operand). SelectionDAG turns any TLS global into `ISD::GlobalTLSAddress` (`SelectionDAG.cpp:1928-1929`).
  The pass must handle constant-expression uses too (`convertUsersOfConstantsToInstructions`).
- clang for both MSIL triples (`c/tlsmodel.c`) gives:
  - `extern _Thread_local int x` -> `@x = external thread_local global` (no `dso_local`);
  - a definition -> `thread_local global`;
  - `tls_model("local-exec")` -> `thread_local(localexec)`.
- **Mapping:**
  - Every model of a *definition* in the module lowers to `TP + offset`.
  - Every model of a *declaration* lowers to the accessor call.
  - `localexec`/`initialexec` on a declaration *may* use `TP + offset` instead (the user promises a definition in the
    same output). msil-ld then reports a library-resolved symbol, like an ELF linker's TLS-model check. Optional.
- `llvm.thread.pointer` (`__builtin_thread_pointer`): keep the error, or return `'<msil>tls'()`, which is per-thread
  unique and stable but is not the native TCB. Keep the error unless a program needs it.
- A non-TLS global initialized with a TLS address (`@p = global ptr @tls`; invalid C, an ELF link error): an error.

### 4.6 Code shape, the IR pass and msil-ld

**IR pass `MSILLowerThreadLocal`** (new file; after `MSILPrepareIR`, whose TLS error goes):

1. Per function, collect the uses of TLS globals (intrinsic calls and direct uses). Materialize
   `%tp = call ptr @"<msil>tls"()` once, at the nearest common dominator of the uses, hoisted to the preheader of the
   outermost loop that contains it (entry block at -O0). Share it with `MSILLowerDynamicAlloca`'s header pointer.
2. A use of a TLS *definition* `@g` becomes `getelementptr i8, ptr %tp, i64 ptrtoint (ptr @g to i64)`. `@g` stays
   `thread_local`. In ISel, `ptrtoint(@g)` of a TLS global is `GlobalTLSAddress`. Custom-lower it to the existing
   `MSILISD::DATA_ADDR` of the symbol, which prints `ldsfld native int 'g'`; that field holds the TLS offset.
   No new opcode and no encoder change.
3. A use of a TLS *declaration* `@x` becomes one call per function of the placeholder
   `[msil.extern]'<msil>TLSAddr'::'x'()` (signature `native int()`), which returns this thread's address of `x`. The
   object writer emits it as a MemberRef, a sibling of `'<msil>FnAddr'` (plan 5.8.3 R2). R5's reservation of
   `<msil>` names for P3 helpers covers it.

IL per function (definitions):

```
call native int '<msil>tls'()      // once
stloc tp
...
ldloc tp
ldsfld native int 'tcount'        // the TLS offset field
add
ldind.i4
```

**msil-ld:**
- **Layout.** `cli::layoutImage` rule 7 (`ImageLayout.cpp:256-263`) becomes the TLS template rule. `.tdata` segments
  are laid out in the image (so phase 1 relocates them), followed in the template by `.tbss` sizes (no image bytes).
  `TLSAlign` is the maximum segment alignment. `TLSSize` is the total, and `H = alignTo(64, TLSAlign)`. This mirrors
  wasm-ld, which merges `.tdata` and `.tbss` into one TLS segment relative to `__tls_base`
  (`lld/wasm/Writer.cpp:1041-1045`) and keeps it for each thread's `__wasm_init_tls` (`:1576-1579`).
- `OutputSegment.cpp:80-91` and `ImageLayout` stop rejecting TLS.
- `TLSOffset32/64` (`OutputSegment.cpp:303-306`) resolve to `H + template offset + addend`. They come only from debug
  information (DWARF TLS locations) and matter in phase 4.
- **Symbols.** A defined TLS symbol's field holds its offset. `'<msil>TLSAddr'::'x'()` binds as follows:
  - defined in the output: a synthesized `aggressiveinlining` method `call '<msil>tls'(); ldsfld 'x'; add; ret`;
  - resolved to a library export: retarget the MemberRef to the library export class's `'<msil>addr_x'()` (same
    signature), as cross-assembly calls bind in M22;
  - weak and unresolved: `'<msil>tls'()` itself. That is non-null, matching native lld, which resolves an undefined
    weak TLS symbol to offset 0 (plan row 2026-10-21 M12: native `&tv != 0` is 1 at -O0 and -O2).
  - `MarkLive` keeps TLS segments by reference, as now (`MarkLive.cpp:96-110`).
- **Libraries (`-shared`).** `Exports.cpp:212-215, 313-318` accept TLS candidates. The accessor `'<msil>addr_<name>'()`
  body is `call '<msil>tls'(); ldsfld '<name>'; add; ret`. The property getter returns that address. The symbol table
  sets `LibraryDataSymbol::ThreadLocal`. Each assembly has its own `'<msil>Thread'` and block (the analogue of ELF's
  per-module TLS block), so a program plus N libraries means at most N + 1 lookups in a function that touches all of
  them. `SymbolTable.cpp:683-695`'s TLS attribute check stays as is.
- **Trailer** (`llvm/lib/CLI/Trailer.{h,cpp}`, shared by `-S` and msil-ld, Q5). It needs:
  - a new kind of synthesized type, a class with instance fields, an instance `.ctor` and a virtual `Finalize`
    override. `TrailerSink` today emits `<Module>` methods and the `'<msil>Data'` class (`Trailer.h:52-80`);
  - `'<msil>tls'`, `Init`, `'<msil>salloc'`, `'<msil>srestore'`;
  - the offset fields;
  - phase 1 must relocate the template before the constructors run (it does: template bytes are image bytes).
- **BCL members** (`llvm/include/llvm/CLI/BCLMembers.def`), all mscorlib-safe:
  - `System.ThreadStaticAttribute::.ctor()`, `Marshal::FreeHGlobal(native int)`,
    `Environment::get_HasShutdownStarted()`, `Object::.ctor()`, `Environment::FailFast(string)` (shadow-stack OOM);
  - `Marshal::AllocHGlobal` is already there;
  - present by name in the .NET 8 reference pack (`System.Runtime.dll`, `System.Runtime.InteropServices.dll`;
    string check only, signatures not verified);
  - `proto_tls.il` ran against mscorlib on all runtimes.
- **Whole-program text** (`-S`, `--lto-emit-asm`): the AsmPrinter's `MSILDataSegment` puts TLS globals into the
  template instead of skipping them (`MSILDataSegment.cpp:219-221`). The trailer text sink prints the same class and
  helpers. Offsets are known, so they can stay fields (same shape as objects) or become `ldc` constants. Keep the fields
  for parity.

### 4.7 TLS and native threads, errno (E5, E6, E7)

- A native thread (`CreateThread`, `pthread_create`) whose start routine is a delegate thunk becomes a managed thread
  on the reverse P/Invoke. Its `[ThreadStatic]` fields start at their defaults, and the values were distinct on 2000 of
  2000 threads on Framework, .NET 8 and .NET 10 (Windows) and 1000 of 1000 under WSL. Lazy `Init` therefore covers
  threads made by native code and by host-libc `pthread_create` alike (Design.md 4.4's last sentence holds).
- `errno` on host-libc triples stays the libc's own TLS, read through `__errno_location()` (glibc) or `_errno()` (UCRT)
  P/Invokes (the existing `<errno.h>` advice, `NativeLibraries.cpp:152-156`). E6 checked:
  - the first call of the accessor right after a failing `open` (IL-stub generation and JIT in between): `ENOENT`
    preserved on all runtimes;
  - 2,000,000 iterations (100,000 on Windows) with an allocating thread forcing thousands of GCs at P/Invoke returns:
    0 mismatches;
  - two threads failing differently at once (`ENOENT` from `open`, `ERANGE` from `strtol`): 0 mismatches each.
  - (`_close(-1)` on UCRT terminates the process through the invalid-parameter handler, natively as well.)

---

## 5. Rust std (HANDOFF issue 15)

The experiment replaced TLS with plain globals (`rust-exp/std/tools/fixir.py`, option `tls`). In the v6_thread IR
(Linux and Windows):

- **`#[thread_local]` statics** (all `thread_local` = general-dynamic, accessed through `llvm.threadlocal.address`; 16
  calls on Linux, 20 on Windows):
  - `panicking::panic_count::LOCAL_PANIC_COUNT`, `thread::current::CURRENT`, `thread::current::id::ID`,
    `thread::spawnhook::SPAWN_HOOKS`;
  - `sys::thread_local::destructors::list::DTORS`;
  - on Windows also `guard::windows::enable::REGISTERED`.
  - They are const-initialized `thread_local!` values whose lazy state lives in TLS. Option B covers them unchanged:
    any size and alignment, stable addresses (Rust keeps `&'static` references to them in `DTORS`), template
    initializers. The `tls` stand-in goes away.
- **Destructor registration** (only for `thread_local!` values that need `Drop`):
  - Linux: `destructors::linux_like::register` calls `__cxa_thread_atexit_impl` (extern_weak) when it and
    `__dso_handle` (extern_weak; crtbegin defines it natively, our toolchain does not) are non-null. Otherwise it falls
    back to the `DTORS` list and `guard::key::enable`: `pthread_key_create(&key, run)` plus
    `pthread_setspecific(key, 1)`. With `__dso_handle` null, Rust std takes the pthread-key path. Per E7 that path runs
    on .NET 8 and .NET 10, but on .NET 8 `run` meets a fresh TLS block (the thread's `[ThreadStatic]` is already
    reset), so the destructors are skipped. That leaks without crashing.
  - Windows: `guard::windows::enable` calls `FlsAlloc(cleanup)`. Per E7, an FLS callback into managed code **kills the
    process on .NET 10** at thread exit. Rust std threads on Windows/.NET 10 need the toolchain to intercept `FlsAlloc`
    with a callback (D-FT6).
- `thread::spawn` needs P3-B's thread-start thunks plus atomics. TLS is not the blocker once P3-A lands.
- `__cxa_thread_atexit_impl`, `__cxa_atexit` and `FlsAlloc` take function pointers, but they are not on msil-ld's
  callback list (`NativeLibraries.cpp:80-125`). Today such a call binds silently and fails at run time (Status.md's
  known gap). Add them.

---

## 6. WebAssembly, Emscripten and SafeStack compared

| | WebAssembly / Emscripten | SafeStack | Proposed MSIL |
|---|---|---|---|
| static frame | linear-memory frame below `__stack_pointer` | unsafe objects on the unsafe stack, safe ones native | CLR `localloc` block (D13) |
| dynamic alloca | `Expand`: SP arithmetic on `__stack_pointer` | SP arithmetic on the TLS unsafe-stack pointer (`:667-725`) | acyclic: `localloc`; cyclic: chunked shadow stack |
| per-thread SP | a wasm global per instance/thread (or `__wasm_get_stack_pointer` libcalls) | `__safestack_unsafe_stack_ptr` (TLS) or `TLI.getSafeStackPointerLocation` | header of the `[ThreadStatic]` block |
| setjmp/exceptions | `invoke_*` wrappers `stackRestore(sp)` in catch | re-store SP after `returns_twice` calls and landing pads (`:430-464`) | setjmp-site save/restore + `fault` clauses (sjlj S7) |
| stack size | `-z stack-size`, default 64 KiB | runtime-chosen | no limit; 64 KiB first chunk, doubling |
| TLS | `.tdata`/`.tbss` merged; `__tls_base` + offset; `__wasm_init_tls` per thread by the thread creator | n/a | template in the image; `TP` + offset field; lazy `Init` on first use (threads are created outside our control) |
| TLS models | only local-exec except Emscripten dynamic linking (`WebAssemblyISelLowering.cpp:2075-2120`) | n/a | definitions `TP + offset`; declarations accessor |

---

## 7. Effects on later goals (not planned here)

- **Portable triple `msil64-unknown-dotnet`:** nothing here depends on a host libc. The block, the shadow stack and the
  finalizer use only `[ThreadStatic]`, `AllocHGlobal`/`FreeHGlobal` and `HasShutdownStarted`. LLVM libc's `errno`
  would become an ordinary `thread_local` in the template, and its `__cxa_thread_atexit`-style destructors would use the
  reserved `Dtors` header slot. This eases the goal.
- **Managed calls from C:** a .NET exception crossing C frames is exactly what the `fault` clauses (3.3) make safe for
  the shadow stack. The accessor `'<msil>addr_<name>'()` already gives C# a per-thread TLS address. Nothing is
  precluded.
- **Inline IL:** operands as locals. A user `localloc` in inline IL would need the same acyclic rule or a diagnostic.
  Nothing is precluded.
- **FeatureIdeas (value-type frames, P4-A):** the hybrid keeps acyclic dynamic allocas as `localloc` even if static
  frames become value-type locals. `localloc` and `ldloca` frames coexist in one method. The `localloc` emptiness rule
  (2.1) stays for the dynamic ones only. "Managed memory/refs" and "escape analysis" are not touched.
- **Multi-triple assemblies:** `'<msil>Thread'` would need a per-triple name, as the helpers-switches recon notes for
  helper holders. Nothing is precluded.

---

## 8. Decisions

**D-FT1. How are dynamic allocas and `stacksave`/`stackrestore` lowered?** (needs the user: it amends Design.md 4.2)
- Options:
  - (A) a `localloc` per alloca;
  - (B) the shadow stack for all;
  - (C) `localloc` with per-site reuse;
  - (H) `localloc` for allocas on no CFG cycle, the shadow stack for allocas on a cycle.
- **Recommend H.**
- Evidence: A overflows after 23,552 x 64 B (Windows .NET 8/10), at most 16,384 (Framework) and 130,048 (WSL) loop
  iterations (E1). B costs one block lookup per call, 41-174 ns against 11-23 ns for `localloc` (E9). H gives acyclic
  VLAs the `localloc` cost and needs runtime support only where a cycle can reclaim memory. C leaves irregular shapes
  unhandled and up to 4x CLR-stack pressure (3.4).

**D-FT2. Shadow-stack storage, size and overflow.**
- Options: a fixed block per thread (wasm-like, `-z stack-size`); a chunked list.
- **Recommend chunked**: 64 KiB first chunk, doubling, one spare, the bottom chunk kept, `FailFast` on `AllocHGlobal`
  failure, no switch.
- Evidence: E9 used 1 chunk for 1M VLA iterations and 6 chunks for 8 MiB of recursion. A fixed block either wastes
  memory per thread or limits deep VLA recursion that native 8 MiB Linux stacks allow.

**D-FT3. How is `thread_local` represented?**
- Options:
  - (A) a `[ThreadStatic]` field per variable;
  - (B) a `[ThreadStatic]` pointer to an unmanaged block initialized from a template;
  - (C) `ThreadLocal<IntPtr>`.
- **Recommend B** (Design.md 4.4 as written).
- Evidence: A's addresses move on .NET 10 (all types) and for structs everywhere (E3). B runs on every runtime (E11).
  C is faster only on .NET 8 and needs generic binding (E2).

**D-FT4. Where do the thread-static fields live?**
- Options: on `<Module>`; on a private `beforefieldinit` class `'<msil>Thread'` without a `.cctor` that is also the
  holder type.
- **Recommend `'<msil>Thread'`.**
- Evidence: no measurable cost difference (E2/E2b). It stays clear of the module initializer and of library two-phase
  initialization. One type serves as both holder and owner.

**D-FT5. Freeing per-thread memory at thread exit.**
- Options: leak; a finalizable holder; native hooks (FLS, pthread keys).
- **Recommend the holder, with an `Environment.HasShutdownStarted` guard.**
- Evidence: 2000/2000 freed on every runtime (E5). Framework finalizes reachable holders at exit (E10). FLS callbacks
  into managed code kill .NET 10 (E7). The leak without it is about 64 KiB + 2-4 KiB per thread with a 64 KiB block (E5).

**D-FT6. Thread-exit destructors registered by C or Rust code** (`__cxa_thread_atexit_impl`, `pthread_key_create`
with a destructor, `tss_create`, `FlsAlloc` with a callback). (needs the user: it is a semantic limit)
- Options:
  - (a) bind them natively through P3-B thunks: Linux works (stale thread statics on .NET 8); Windows `FlsAlloc` is
    fatal on .NET 10;
  - (b) intercept the registration APIs with toolchain helpers that keep a per-thread list in the block's `Dtors` slot
    and run it on threads that start through the toolchain's thread-start thunks (after the start routine returns) and
    on the main thread before exit; other threads' destructors do not run;
  - (c) run them from the holder's finalizer, on the wrong thread.
- **Recommend (b) for `FlsAlloc` and `__cxa_thread_atexit_impl`, (a) for `pthread_key_create`/`tss_create` on
  Linux.** Add `FlsAlloc`, `__cxa_thread_atexit_impl` and `__cxa_atexit` to the callback list now.
- Evidence: E7 and E8. This is P3-B's thread work; P3-A only reserves the slot.

**D-FT7. TLS access code shape.**
- Options: a per-access accessor call; `TP` per access; `TP` hoisted per function with offset fields for definitions
  and accessor calls for declarations.
- **Recommend hoisted `TP` + offset fields + per-symbol `'<msil>TLSAddr'` accessors for declarations**, one call per
  (function, symbol).
- Evidence: 4 accesses cost 99 vs 377 ns (.NET 8), 64 vs 137 (Framework), 37 vs 59 (.NET 10) (E2). Libraries need the
  accessor anyway (plan '-' row of 2026-10-27).

**D-FT8. `__builtin_frame_address(0)` and `__builtin_stack_address()`.**
- Options: keep the errors; return the static frame base.
- **Recommend returning `FP` for depth 0** (`needsFrame` already allocates one) **and keeping the error for depth > 0**.
  Low priority.
- Evidence: no corpus program needs them. Returning `FP` gives a pointer into the current frame, which is what callers
  use it for.

**D-FT9. Codegen switches for this topic.**
- Options: `NativeMemory`, `ThreadLocal<T>`, `SkipLocalsInit`.
- **Recommend none.**
- Evidence: all defaults are mscorlib-safe and run on Framework and modern .NET. `NativeMemory` gives identical
  results (helpers-switches S6). `ThreadLocal` helps only .NET 8 (E2). `localsinit` is never set.

**D-FT10. Ordering with setjmp (contract with P3-B).** The setjmp lowering runs before `MSILLowerDynamicAlloca` and
marks its save/restore as shadow-stack operations. Either it emits `'<msil>ssave'`/`'<msil>srestore'` directly or tags
its `llvm.stacksave`. The save reads the block pointer without allocating. Functions that move `Sp` get `fault`
clauses once P3-B's EH-region emission exists. **Recommend this contract** (sjlj recon S7 agrees).

---

## 9. Work packages, matrix rows and tests

Candidate WPs with disjoint files (sizes are rough):

1. **IR passes** (~700 lines):
   - `llvm/lib/Target/MSIL/MSILLowerThreadLocal.cpp` and `MSILLowerDynamicAlloca.cpp` (new);
   - registration in `MSIL.h` and `MSILTargetMachine.cpp`;
   - `MSILPrepareIR.cpp`: drop the TLS error (`:2320-2325`) and the header text (`:146`); keep the address-space and
     target-extension checks.
2. **ISel/frame** (~250):
   - `MSILISelLowering.{h,cpp}`: `GlobalTLSAddress` -> `DATA_ADDR` of the offset field; `DYNAMIC_STACKALLOC` ->
     `LOCALLOC_I64` with realignment; STACKSAVE/RESTORE backstop; FRAMEADDR(0);
   - `MSILInstrFrame.td` (`LOCALLOC_I64`), `MSILRegStackify.cpp:850-854`, `MSILStackCheck.cpp:185-186`.
3. **lib/CLI** (~900):
   - `Trailer.{h,cpp}` (the `'<msil>Thread'` class kind, `'<msil>tls'`, `Init`, `Finalize`, `'<msil>salloc'`,
     `'<msil>srestore'`, offset fields);
   - `ImageLayout.{h,cpp}` (TLS template rule 7);
   - `BCLMembers.def` (five members).
4. **Backend objects and text** (~500):
   - `MSILDataSegment.cpp` (TLS into the template);
   - `MSILAsmPrinter.cpp` (`:956-961`, `:1918-1922`; offset fields);
   - `MCTargetDesc/MSILObjectWriter.cpp` and the record builder (TLS flags, segments, the `'<msil>TLSAddr'`
     placeholder);
   - `MSILMCInstLower.cpp` (operand class of the accessor).
5. **msil-ld** (~800):
   - `OutputSegment.cpp` (template layout, TLSOffset), `Synthetic.cpp` (accessors, helpers), `Writer.cpp`;
   - `Exports.cpp` (TLS exports), `SymbolTable.cpp` (bind the accessor to library exports);
   - `NativeLibraries.cpp` (callback list additions).
6. **Tests** (~1,500):
   - llc: `tls-lowering.ll` (TP hoisting: entry at -O0, preheader at -O2; definition vs declaration; alias with offset;
     weak; direct use without the intrinsic), `dynamic-alloca.ll` (acyclic `localloc` with `native int` size and
     realignment; cyclic shadow ops; save/restore deletion; setjmp interplay), updated `dynamic-alloca-error.ll` and
     section 2.4's tests;
   - MC: object rows for TLS symbols, segments and the `'<msil>TLSAddr'` MemberRef;
   - lld: TLS template layout, `.tbss` sizes, alignment 64, `TLSOffset`, exports with the `ThreadLocal` flag,
     import-from-library binding, weak undefined TLS, gc-sections of TLS;
   - execution: `vla_cases.c` (`frame-tls-files/c/`, native transcript identical on both OSes: `vla_loop 128493856`,
     `alloca_loop 780`, `vla_align 1`, `vla_rec 1000`) and a single-threaded TLS test now;
   - `tls_threads.c` once P3-B's thread thunks exist. Native transcript identical on Windows and Linux at -O0 and -O2:
     slots 0-4 `tcount=5/1005/2005/3005/4005`, `*tptr+tfn(1)=44`, `sum(tzero)=0`, `taligned%64=0`, distinct
     addresses, main unchanged;
   - all on obj and lto, both triples, the three Windows runtimes and WSL .NET 8/10.

**Matrix** (`msil/test/M2/08-semantics/matrix.txt`):
- `p5-tls` -> `transcript: 12` (obj, lto; linux-gnu, windows-msvc).
- New rows:
  - `tls-xtu-init`: a TLS definition with an address initializer used through an `extern` declaration in another
    object;
  - `tls-shared-export`: a library exports a TLS variable, and the program reads and writes it through the accessor
    from two threads once P3-B lands, one before;
  - `tls-weak-undef`: native 1, as in the 2026-10-21 row;
  - `tls-threads` (P3-B).
- The `TLS attribute mismatch` rows stay.

Gate legs: the frame and TLS execution tests need the Framework and `TieredCompilation=0` legs (D22, D38), because
Tier-0 and optimized code differ in thread-static access paths (E2b).

---

## 10. Risks

- **.NET 8 thread-static cost** (69-330 ns per lookup measured under load): TLS-heavy code on .NET 8 is slow even with
  hoisting. .NET 9 and later are fine. Mitigation: hoisting, and documentation.
- **Framework shutdown:** a `HasShutdownStarted` guard missed in any finalizer frees memory that running threads use.
  Covered by E10's shape as a test.
- **Native thread-exit callbacks into managed code** are fatal on .NET 10 (FLS). Any C program registering FLS or TLS
  callbacks that reach managed code crashes at thread exit on .NET 10 unless D-FT6's interception is in place. This
  includes `.CRT$XL*` TLS callbacks, which stay an unsupported section.
- **Exceptions across C frames without `fault` clauses** leave the shadow `Sp` lowered until an outer restore (3.3).
- **`LOCALLOC` I32 size:** a dynamic size must not be truncated; it needs the new `native int` form.
- **Host load:** every timing here was taken at 100% CPU from other work. Re-measure on an idle host before using the
  numbers for any threshold.

## 11. Not verified

- .NET 8's end-of-support date (taken from the published policy, not measured).
- Default secondary-thread stack sizes (1.5 MiB on Windows x64 and Linux are the documented CoreCLR defaults; only
  main threads were measured).
- Whether UCRT provides C11 `tss_create`, and how it implements its destructors.
- The exact signatures of the five BCL members in the .NET 8/10 reference packs (string presence only; the prototype
  ran against mscorlib).
- Emscripten's `invoke_*` and `stackSave`/`stackRestore` behaviour (from memory, not run here). wasm-ld's default stack
  size and its TLS segment merging were read in this checkout.

## Appendix: experiment index (`frame-tls-files/`)

| Id | Source | Log | What |
|---|---|---|---|
| E1 | `src/E1Loop.cs`, `c/vla_loop.c` | `results/E1Loop.txt` | localloc per iteration: growth and overflow points; native comparison |
| E2 | `src/E2Cost.cs` | `results/e2cost-run1.txt` | dynamic-alloca and TLS access costs |
| E2b, E2c | `src/E2b.cs`, `src/E2c.cs` | `results/E2c-E2b.txt` | thread-static cost in isolation; lookup alternatives |
| E3 | `src/E3Addr.cs` | `results/E3Addr.txt` | `[ThreadStatic]` address stability across compacting GCs |
| E5 | `src/E5Exit.cs` | `results/e5-win.txt`, `results/e5-wsl.txt` | thread-exit freeing, leak per thread, native threads |
| E6 | `src/E6Errno.cs` | `results/E6Errno.txt` | errno through P/Invoke |
| E7 | `src/E7Dtor.cs` | `results/E7Dtor.txt` | FLS and pthread-key destructors into managed code |
| E8 | `src/E8Exit.cs` | `results/E8Exit.txt` | `ExitThread`/`pthread_exit` from managed frames |
| E9 | `src/E9Shadow.cs` | `results/e9-run.txt` | chunked shadow-stack model: loops, recursion, longjmp, cost |
| E10 | `src/E10Shutdown.cs` | `results/e10-*.log` | Framework finalization of reachable holders at exit |
| E11 | `il/proto_tls.il` | `results/proto-tls-run.txt`, `results/proto-tls-verify.txt` | the proposed TLS IL on five runtime/OS combinations; verifier categories |
| C | `c/tls_threads.c`, `c/vla_cases.c`, `c/tlsmodel.c` | `results/native-c.txt` | native transcripts (Windows and Linux, -O0/-O2); clang's TLS IR |
