# Phase 3 recon: setjmp/longjmp (P3-B)

- Date: 2026-10-28. Checkout: branch `msil` at `cc4c79787d6a` (M23). Key: `sjlj`.
- Scope: what the backend does with `setjmp`/`longjmp` today, the design of Design.md 4.6 (a BCL exception thrown
  by `longjmp`, caught by an IL exception filter in the `setjmp` caller, no exception type shared between
  assemblies), the IR pass that rewrites `setjmp` callers (from LLVM's Emscripten SjLj lowering), the IL shapes,
  the `jmp_buf` layout, cross-assembly behaviour, native frames, cost, the programs that need it, and tests.
- Method: every CLR behaviour relied on below was measured with small IL or C# programs on .NET Framework 4.8,
  .NET 8 and .NET 10 on Windows and on .NET 8 and .NET 10 under WSL; every C behaviour was compared with a native
  clang build. The recommended design (E1) was prototyped end to end: a C program written in the shape the pass
  would produce, compiled by the real MSIL backend (`clang -O2 -S`), with the two generated methods replaced by the
  IL the backend would emit (a 90-line script), assembled with ilasm 4.8 and run on all five runtimes.
- Supporting files: `sjlj-files/` (sources, IL, scripts, `results.txt` with every raw number). Scratch:
  `C:\llvm-msil-scratch\p3\sjlj`. The host was shared and under load (CPU 100 % during the timings): absolute times
  are +-30 %, ratios are meaningful.
- No tracked file was changed; no build of LLVM was made. Downloads (sources only, into the scratch `dl\`):
  `lua-5.4.7.tar.gz` (lua.org), `libpng-1.6.44.tar.gz` (sourceforge), `libjpeg-turbo-3.0.4.tar.gz` (GitHub),
  `sqlite-amalgamation-3470000.zip` (sqlite.org). No tool was installed.

---

## 0. Summary and recommendation

1. **Today** (section 1): `setjmp` is not diagnosed as such. On the windows-msvc triples every `setjmp` fails to
   compile on `llvm.frameaddress` (`_setjmp(buf, frameaddress(0))`, aarch64: `_setjmpex(buf, sponentry())`). On
   the Linux triple `clang -S` reports `_setjmp`/`longjmp` as "not a Phase-1 import", but the default pipeline
   (`clang -c` and msil-ld, objects by default since M23) compiles **silently** and msil-ld binds `_setjmp`,
   `__sigsetjmp`, `longjmp`, `_longjmp`, `siglongjmp` and `__longjmp_chk` to the native glibc. The native
   `longjmp` then jumps across managed frames and **works by accident** in the measured cases (1M and 30M round
   trips, .NET 8 and .NET 10). Status.md section 3 calls `setjmp`/`longjmp` "error": that is wrong for the Linux
   triple's object pipeline. `__builtin_setjmp`/`__builtin_longjmp` are diagnosed (M7.b texts).
2. **Recommended design E1** (sections 3-5): a new IR pass `MSILLowerSjLj`, adapted from the SjLj half of
   `WebAssemblyLowerEmscriptenEHSjLj.cpp` in its Emscripten (JavaScript-style "invoke wrapper") form, not its
   Wasm-EH form:
   - every `setjmp` call site stores `{magic 'MSJ1', site label, address of a status block in the caller's frame}`
     into the first 16 bytes of the `jmp_buf` and becomes a block split with a `phi` for its result;
   - every call after a `setjmp` that may `longjmp` is routed through a generated **wrapper function** whose whole
     body the backend puts in an IL `.try` with a fixed **filter**; after the call the caller tests the status
     block and branches to one dispatch block that `switch`es to the post-`setjmp` blocks; SSA is rebuilt
     (`SSAUpdaterBulk`), so every value has an explicit edge and no local needs demotion;
   - `longjmp` becomes `'<msil>longjmp'(buf, val)`: it checks the magic, **arms** the target status block
     (`label`, `val`) and throws **`new System.Runtime.InteropServices.ExternalException(null, (int)status)`**, a
     BCL type present in mscorlib (Framework) and System.Runtime (.NET 8/10 references);
   - the filter accepts the exception only if it is an `ExternalException`, its `HResult` equals the low 32 bits
     of the wrapper's status-block address and that status block is armed. No type is shared between
     assemblies: the protocol is the BCL type, the `HResult` and the 16-byte `jmp_buf` prefix.
3. **Measured** (section 6): the CLR semantics hold on all five runtimes (filters may write locals and frame
   memory, locals written in a `.try` survive into the handler, a C# `catch`/`finally` in between behaves, other
   frames decline). 1M round trips: native glibc ~65 ns each, native UCRT x64 ~2 us (its `longjmp` unwinds with
   `RtlUnwindEx`), E1 on Framework ~27 us, .NET 8 Windows ~26 us, .NET 10 Windows ~6.6 us, .NET 8 Linux ~44 us,
   .NET 10 Linux ~8.5 us. The normal path costs ~1-3 ns per wrapped call on .NET 8/10 and ~5-8 ns on Framework.
4. **Native frames** (section 7): an exception thrown in a callback below native `qsort` is caught normally on
   Framework, .NET 8 and .NET 10 on Windows (delegate and `UnmanagedCallersOnly` thunks alike), and **aborts the
   process** (SIGABRT, exit 134) on .NET 8 and .NET 10 under Linux. Even with no native frame in between, a
   managed `calli unmanaged` of a thunk whose target throws aborts on .NET 8 Linux (it works on .NET 10 Linux).
   So `longjmp` out of a callback works on Windows only, and a cross-assembly call of `&longjmp` (libpng's
   `png_ptr->longjmp_fn`) needs the indirect-call fast path to reach other assemblies of this toolchain.
5. **Programs** (section 8): Lua (`luaD_rawrunprotected`, one site, one indirect call), libpng (takes
   `longjmp`'s address; `pngtest` has 140 calls after `setjmp`), libjpeg/libjpeg-turbo (17 sites in
   `turbojpeg.c`), zlib contrib `puff`/`blast` (already in the corpus cache). SQLite and c-testsuite: none.
6. **Backend work** (section 9): an "attributed whole-body `.try`" capability (text, object writer, streamer
   event), RETURN as `leave`, a fixed filter and handler, an AsmPrinter-written body for the throw helper, and
   two BCLMembers.def entries. The CLI library (`ILBuilder::addExceptionClause`, `MethodBodyInfo::Clauses`) and
   msil-ld (`lld/MSIL/Writer.cpp:993`, `TokenMap.cpp:136`) already carry EH clauses.

---

## 1. Today

### 1.1 What clang emits per triple (verified, `sjlj-files/today/`)

| Source | x86_64-unknown-linux-gnu-msil (glibc 2.43 headers) | x86_64-pc-windows-msvc-msil | aarch64-pc-windows-msvc-msil |
|---|---|---|---|
| `setjmp(b)` | `call i32 @_setjmp(ptr)` (glibc's macro) | `%f = call ptr @llvm.frameaddress.p0(i32 0)`; `call i32 @_setjmp(ptr, ptr %f)` | `%f = call ptr @llvm.sponentry.p0()`; `call i32 @_setjmpex(ptr, ptr %f)` |
| `_setjmp(b)` | `@_setjmp(ptr)` | as `setjmp` | as `setjmp` |
| `sigsetjmp(b, m)` | `@__sigsetjmp(ptr, i32 m)` | n/a | n/a |
| `longjmp`, `_longjmp`, `siglongjmp` | `@longjmp`, `@_longjmp`, `@siglongjmp` `(ptr, i32)` | `@longjmp(ptr, i32)` | `@longjmp(ptr, i32)` |
| any of them with `-D_FORTIFY_SOURCE=2` | `@__longjmp_chk(ptr, i32)` | | |
| `sizeof`/`_Alignof(jmp_buf)` | 200 / 8 (`sigjmp_buf` 200) | 256 / 16 | 192 / 8 |

The `setjmp` family is declared `returns_twice nounwind`, the `longjmp` family `noreturn nounwind`; call sites
carry the same attributes. The MSVC forms come from `EmitMSVCRTSetJmp`
(`clang/lib/CodeGen/CGBuiltin.cpp:1762-1800`; `_setjmp` builtin at `:6177-6185`, `_setjmpex` at `:6172-6176`).
musl (no sysroot here; from its headers, unverified in this run) declares real functions `setjmp`, `_setjmp`,
`sigsetjmp`, `longjmp`, `_longjmp`, `siglongjmp`. MinGW (`x86_64-w64-windows-gnu-msil`) is unverified: no MinGW
headers on this host; its `setjmp.h` is believed to use `_setjmp(buf, __builtin_frame_address(0))` or
`__mingw_setjmp`/`_setjmp3` spellings, which the pass must be checked against when MinGW headers are available.
aarch64 glibc's `jmp_buf` (believed 312 bytes) is unverified: no aarch64 sysroot.

### 1.2 What the backend and msil-ld do with it

- `__builtin_setjmp`/`__builtin_longjmp` (`clang/lib/CodeGen/CGBuiltin.cpp:5213-5250`: stores of
  `llvm.frameaddress` at `buf[0]` and `llvm.stacksave` at `buf[2]`, then `llvm.eh.sjlj.setjmp(buf)`;
  `llvm.eh.sjlj.longjmp(buf)` + `unreachable`) are diagnosed in ISel: `MSILISelLowering.cpp:318-321` makes
  `EH_SJLJ_SETJMP`/`EH_SJLJ_LONGJMP`/`EH_SJLJ_SETUP_DISPATCH` Custom, `:628-641` reports
  `llvm.eh.sjlj.setjmp is not supported for MSIL yet (phase 3 setjmp/longjmp)`, the same for `longjmp`, and
  `llvm.eh.sjlj.setup.dispatch is not supported for MSIL`; the stores add the `llvm.stacksave` and
  `llvm.frameaddress` "phase 3 shadow stack" errors. Pinned by `llvm/test/CodeGen/MSIL/dynamic-alloca-error.ll:206-218`.
- Library `setjmp`/`longjmp` are ordinary external calls. Nothing in `MSILPrepareIR.cpp` or `lld/MSIL` names them.
  - windows-msvc: `clang -c`/`-S` stop at `error: llvm.frameaddress is not supported for MSIL yet (phase 3 shadow
    stack)`. Forcing a one-argument `_setjmp` with an asm label (`bench3.c`) reaches msil-ld, which reports
    `undefined symbol: _setjmp` and `undefined symbol: longjmp` (vcruntime is not a default library,
    `clang/lib/Driver/ToolChains/MSIL.cpp:125-132`, and `--ucrtbase-imports` lists only the memory and string
    functions).
  - Linux, `clang -S` or llc text: `undefined function '_setjmp' is not defined in this module and is not a
    Phase-1 import (...)` from `MSILISelLoweringCall.cpp:309`.
  - **Linux, `clang -c` and msil-ld (the default pipeline): no diagnostic.** `llvm-nm` shows `U _setjmp`,
    `U __sigsetjmp`, `U siglongjmp`; msil-ld emits `.pinvokeimpl 'libc.so.6' entry '_setjmp'` and `'longjmp'`.
    `sj4.c` prints `ok` on WSL .NET 8. `bench.c` (global `jmp_buf`) and `bench2.c` (`jmp_buf` in a localloc
    frame, as in Lua) pass 1M and 30M round trips at ~95 ns (.NET 8) and ~43 ns (.NET 10) each. This relies on
    the JIT inlining the P/Invoke transition into the `setjmp` caller and on the abandoned frames holding nothing
    the runtime walks; it is undefined and must not ship. **Recommendation (decision S10):** until the pass lands,
    msil-ld refuses to bind the `setjmp` family natively (`_setjmp`, `setjmp`, `__sigsetjmp`, `sigsetjmp`,
    `_setjmpex`, `_setjmp3`, `longjmp`, `_longjmp`, `siglongjmp`, `__longjmp_chk`) with a phase-3 text, and
    Status.md section 3's row is corrected. After the pass, the same check stays: a CIL object that still
    references a native `setjmp` was compiled without the lowering.
- `MSILRegColoring.cpp:324-330` skips coloring when `MF.exposesReturnsTwice()` (copied from WebAssembly, with a
  TODO). After the recommended pass no `returns_twice` call is left and coloring runs normally, which is correct
  because every edge is explicit in the IR (section 3.4).

---

## 2. LLVM's Emscripten SjLj lowering (what to take)

`llvm/lib/Target/WebAssembly/WebAssemblyLowerEmscriptenEHSjLj.cpp` (1,901 lines in this checkout):

| Part | Lines | What it does | For MSIL |
|---|---|---|---|
| File comment | 1-280 | Both schemes: Emscripten SjLj (every longjmpable call through a JS `__invoke_SIG` wrapper with JS `try/catch`, then a `__THREW__` test and a `switch` after each call) and Wasm SjLj (all longjmpable calls become `invoke`s to one `catchswitch`, `catchret` to a `setjmp.dispatch` block at the top) | E1 is the Emscripten scheme with IL wrappers; E2 is the Wasm scheme |
| `canLongjmp` | 613-689 | Every call is longjmpable except intrinsics, inline asm, `setjmp`/`malloc`/`free` and the runtime's own helpers | Adapt; add `willreturn` callees and libc functions without callbacks (section 3.5) |
| `wrapInvoke`, `getInvokeWrapper` | 528-611 | Builds `__invoke_SIG(fnptr, args...)`, shifts argument attributes by one, fixes `allocsize`, **removes `noreturn`** from the wrapper call (572) | Reuse the attribute handling; MSIL wrappers are per callee for direct calls (no function pointer, so no thunk on the call) and per function type for indirect calls |
| `rebuildSSA` | 789-843 | `SSAUpdaterBulk` over all values whose uses are no longer dominated; drops lifetime markers that lose dominance | Reuse verbatim |
| `replaceLongjmpWith` | 845-878 | Calls of `longjmp` to `emscripten_longjmp`/`__wasm_longjmp`; other uses (address taken) to the new function's address | Reuse: `&longjmp` must become `&'<msil>longjmp'` (libpng) |
| `nullifySetjmp` | 893-915 | A function whose only calls are `setjmp`s gets `0` for each | Reuse, but still store the 16-byte prefix (section 4.1) |
| `runOnModule` | 917-1141 | Lowers invokes when there is no EH model; maps `_setjmp`/`_longjmp` to `setjmp`/`longjmp`; indirect use of `setjmp` is `report_fatal_error` | Adapt: more names (1.1); a diagnostic instead of a fatal error. Line 979 creates the `_longjmp` replacement under the name `"setjmp"` (an upstream naming slip, harmless there because later code compares pointers): do not copy it |
| `runSjLjOnFunction` | 1335-1440 | Splits the entry block, keeps static allocas in it, adds the `functionInvocationId` alloca, splits after each `setjmp`, adds the `setjmp.ret` phi, numbers the sites from 1 | Reuse; the status block replaces `functionInvocationId` |
| `handleLongjmpableCallsForEmscriptenSjLj` | 1449-1617 | Wraps each call, tests `__THREW__`/`__threwValue`, calls `__wasm_setjmp_test`, `switch`es to the post-setjmp blocks | Simplify: one status-block load per call and one shared dispatch block |
| `handleLongjmpableCallsForWasmSjLj` | 1630-1880 | Personality, `catchswitch`/`catchpad`, `wasm.catch`, `catchret` to `setjmp.dispatch`, every longjmpable call to `invoke`, strips `nounwind` from the calls and callees (1770-1776) | Not used (needs EH IR in the backend); its "dispatch at the top" shape is E2 |

Locals that must be preserved: the Emscripten scheme needs nothing special, because the post-call test and the
`switch` make every path explicit and `rebuildSSA` inserts the phis: a value defined before a `setjmp` and used
after it reaches the use along the call -> dispatch -> post-setjmp edges. The Wasm scheme gets the same through
the `invoke` unwind edges. Only a scheme whose catch edges are invisible to the IR (E2 below) would have to demote
values to memory, as LLVM's generic `SjLjEHPrepare` does.

---

## 3. The design (E1): IR pass `MSILLowerSjLj`

### 3.1 Placement

New module pass, legacy pass manager like the other MSIL IR passes (D23), in `MSILPassConfig::addIRPasses`
(`llvm/lib/Target/MSIL/MSILTargetMachine.cpp:268-296`):

```
MSILPrepareIR (271) -> MSILAddMissingPrototypes (274) -> [P3-B variadic lowering]
  -> MSILLowerSjLj (new) -> MSILFixFunctionBitcasts (278) -> MSILOptimizeReturned (282)
  -> LowerInvoke (289) -> UnreachableBlockElim (290) -> IndirectBrExpand (294) -> TargetPassConfig::addIRPasses
```

- After MSILPrepareIR: its `diagnoseReservedNames` (`MSILPrepareIR.cpp:1911`) rejects input symbols starting with
  `<msil>`, so the pass may create `<msil>` names afterwards.
- After AddMissingPrototypes: wrappers copy the call-site function type.
- After the variadic lowering of P3-B: a call of our own variadic function after a `setjmp` (`die(fmt, ...)` that
  longjmps is a common pattern) must already be a fixed-arity call when it is wrapped.
- Before FixFunctionBitcasts: the wrapper's inner call keeps the original call-site type, which FixFunctionBitcasts
  then reconciles with the callee's definition.
- In the codegen pipeline, it runs after the optimizer in `-c`, in llc and in msil-ld's LTO, and on stock-clang
  bitcode (Q12) too. No inliner or dead-argument elimination runs after it, so wrappers and their status-block
  argument survive (the C stand-in of the prototype lost the argument to DAE at -O2 until it was used, see
  `proto/proto.c`).
- The pass first turns `invoke`s in `setjmp` callers into calls (as the Emscripten pass does at 932-950, before its SjLj work), so it
  never meets EH IR.

### 3.2 Names it recognizes

`setjmp` sites: calls of `setjmp`, `_setjmp` (one or two arguments), `__sigsetjmp`, `sigsetjmp`, `_setjmpex`,
`_setjmp3`; for the two-argument MSVC forms the second argument's `llvm.frameaddress`/`llvm.sponentry` call is
erased when it has no other use (else the ISel shadow-stack error would fire). Any other call of a `returns_twice`
function (`vfork`, `getcontext`, `savectx`) and any non-call use of a `setjmp` name get a diagnostic,
`'<name>' is not supported for MSIL` / `taking the address of 'setjmp' is not supported for MSIL`.
`longjmp` calls: `longjmp`, `_longjmp`, `siglongjmp`, `__longjmp_chk`, and every other use of these
declarations (an address taken) becomes `'<msil>longjmp'`. Only declarations are intercepted: a module that
defines a function named `longjmp` keeps it. `__builtin_setjmp`/`__builtin_longjmp` keep their M7.b
diagnostics in P3 (decision S8).

### 3.3 The transformation of one function

Source (Lua 5.4.7, `ldo.c` with `LUA_USE_LINUX`; the IR below is clang's -O2 output on the Linux triple, trimmed):

```llvm
define i32 @luaD_rawrunprotected(ptr %L, ptr %f, ptr %ud) {
entry:
  %lj = alloca %struct.lua_longjmp, align 8
  %0 = load i32, ptr %nCcalls                ; oldnCcalls: defined before setjmp, used after
  ...
  %b = getelementptr inbounds nuw i8, ptr %lj, i64 8
  %call = call i32 @_setjmp(ptr %b)          ; returns_twice
  %cmp = icmp eq i32 %call, 0
  br i1 %cmp, label %if.then, label %if.end
if.then:
  call void %f(ptr %L, ptr %ud)              ; may longjmp
  br label %if.end
if.end:
  ...
  store i32 %0, ptr %nCcalls
  %3 = load volatile i32, ptr %status
  ret i32 %3
}
```

After `MSILLowerSjLj` (the IR the pass should produce; `'MSJ1'` = `0x314A534D`):

```llvm
define i32 @luaD_rawrunprotected(ptr %L, ptr %f, ptr %ud) {
entry:
  %sjlj.st = alloca { i32, i32 }, align 8     ; status block {label, val}; its address is the frame identity
  store i32 0, ptr %sjlj.st
  %lj = alloca %struct.lua_longjmp, align 8
  %0 = load i32, ptr %nCcalls
  ...
  %b = getelementptr inbounds nuw i8, ptr %lj, i64 8
  store i32 822891341, ptr %b                ; setjmp site 1: magic 'MSJ1'
  %b.lbl = getelementptr inbounds i8, ptr %b, i64 4
  store i32 1, ptr %b.lbl                    ; site label
  %b.st = getelementptr inbounds i8, ptr %b, i64 8
  store ptr %sjlj.st, ptr %b.st              ; the frame that owns this setjmp
  %ssp.1 = call ptr @llvm.stacksave.p0()     ; P3-A shadow stack pointer at the setjmp (3.6)
  br label %setjmp.cont.1
setjmp.cont.1:
  %call = phi i32 [ 0, %entry ], [ %sjlj.val, %sjlj.restore.1 ]
  %cmp = icmp eq i32 %call, 0
  br i1 %cmp, label %if.then, label %if.end
if.then:
  call void @"<msil>sjlj.invoke.ind.v_pp"(ptr %f, ptr %L, ptr %ud, ptr %sjlj.st)
  %sjlj.lbl = load i32, ptr %sjlj.st
  %sjlj.jumped = icmp ne i32 %sjlj.lbl, 0
  br i1 %sjlj.jumped, label %sjlj.dispatch, label %if.then.cont
if.then.cont:
  br label %if.end
sjlj.dispatch:                               ; one per function; phi over every wrapped call
  %sjlj.l = phi i32 [ %sjlj.lbl, %if.then ]
  %sjlj.valp = getelementptr inbounds i8, ptr %sjlj.st, i64 4
  %sjlj.val = load i32, ptr %sjlj.valp
  store i32 0, ptr %sjlj.st                  ; disarm
  switch i32 %sjlj.l, label %sjlj.bad [ i32 1, label %sjlj.restore.1 ]
sjlj.restore.1:
  call void @llvm.stackrestore.p0(ptr %ssp.1)
  br label %setjmp.cont.1
sjlj.bad:
  unreachable                                ; TrapUnreachable: ldnull; throw
if.end:
  ...                                        ; %0 reaches here on every path: rebuildSSA adds the phis
}

; Generated, internal, one per (indirect function type) or per direct callee; "msil-sjlj-invoke"="N"
; names the status-block argument. The backend puts the whole body in .try (section 4.2).
define internal void @"<msil>sjlj.invoke.ind.v_pp"(ptr %fn, ptr %a0, ptr %a1, ptr %st) #0 {
  call void %fn(ptr %a0, ptr %a1)
  ret void
}
attributes #0 = { noinline "msil-sjlj-invoke"="3" }

; Generated once per module (linkonce_odr hidden: one copy per assembly, address identity kept).
define linkonce_odr hidden void @"<msil>longjmp"(ptr %buf, i32 %v) noreturn {
entry:
  %magic = load i32, ptr %buf
  %ok = icmp eq i32 %magic, 822891341
  br i1 %ok, label %arm, label %nobuf
arm:
  %stp = getelementptr inbounds i8, ptr %buf, i64 8
  %st = load ptr, ptr %stp
  %iszero = icmp eq i32 %v, 0
  %v1 = select i1 %iszero, i32 1, i32 %v      ; C17 7.13.2.1p4
  %valp = getelementptr inbounds i8, ptr %st, i64 4
  store i32 %v1, ptr %valp
  %lblp = getelementptr inbounds i8, ptr %buf, i64 4
  %lbl = load i32, ptr %lblp
  store i32 %lbl, ptr %st                     ; arm the target frame
  call void @"<msil>sjlj.throw"(ptr %st)
  unreachable
nobuf:                                        ; not set by our setjmp (native, zeroed, corrupt)
  call void @"<msil>sjlj.throw"(ptr null)     ; HResult 0 never matches: unhandled, process ends
  unreachable
}
; Body written by the AsmPrinter (section 4.3).
define internal void @"<msil>sjlj.throw"(ptr %st) noreturn "msil-sjlj-throw" { unreachable }
```

Notes:

- One dispatch block per function, reached from every wrapped call, instead of Emscripten's `switch` after each
  call: smaller IL; `rebuildSSA` handles both.
- Several `setjmp` sites number 1..N; a site re-executed in a loop rewrites the prefix (c06 of the test suite).
- `sigsetjmp(b, m)` with `m != 0` also saves the signal mask (decision S9); `siglongjmp` restores it in
  `'<msil>longjmp'` before the throw, as glibc does.
- A function whose `setjmp` has no longjmpable call after it gets `0` for the result (Emscripten's
  `nullifySetjmp`), but the prefix is still stored, so a later `longjmp` to that buffer ends in the "no active
  setjmp" exception instead of reading garbage.
- `noreturn` is removed from wrapper calls (`wrapInvoke`, line 572); the `unreachable` that follows a wrapped
  `longjmp` stays on the normal-return path.

### 3.4 Why no local needs demotion, volatile and non-volatile locals

The setjmp caller is never unwound: the exception is caught in the wrapper (its callee), which returns normally,
and the caller branches on the status block. All control flow is explicit IR, so every later pass (CodeGenPrepare,
SelectionDAG, RegStackify, RegColoring) is correct by construction and nothing needs `volatile`. C semantics
(C17 7.13.2.1p3): volatile locals and locals not modified after the `setjmp` must keep their values; non-volatile
locals modified between `setjmp` and `longjmp` are indeterminate. E1 gives every local its latest value (what
GCC and clang give at -O0; native -O2 may restore older register values), which satisfies both rules. Tests may
check only volatile and unmodified locals (`tests/sjlj-semantics.c`, case c03). For the E2 alternative the CLR
fact that matters was measured too: an IL local written inside a `.try` keeps its value in the handler and after
it (`il/sem.il` test 1, `a = 22`, all five runtimes).

### 3.5 Which calls are wrapped

A call is wrapped when its block is reachable from a block holding a `setjmp` call (or follows the `setjmp` in
that block) and the callee may longjmp. A call cannot longjmp when the callee is an intrinsic, inline asm, one of
the pass's own helpers, a `willreturn` function (LangRef.md:2523-2529: it "comes back ... at a point in the
existing call stack that includes the current invocation"), or a TargetLibraryInfo `LibFunc` that takes no
function pointer. The library functions that take callbacks (`qsort`, `bsearch`, `lfind`, `lsearch`, `tsearch`,
`twalk`, `tdelete`, `ftw`, `nftw`, `scandir`, `glob`, `signal`, `sigaction`, `raise`, `atexit`, `at_quick_exit`,
`exit`, `pthread_once`, `pthread_create`, `bsearch_s`, `qsort_s`) stay wrapped, because on Windows a `longjmp` out
of their callbacks works (section 7). Emscripten wraps every call in the function; the reachability rule is
cheap and strictly better. Measured candidates (`scan/results.txt`, `scan/sjscan.py`):

| Function | setjmp sites | calls after setjmp | indirect | calls in function |
|---|---|---|---|---|
| Lua `luaD_rawrunprotected` | 1 | 1 | 1 | 1 |
| zlib contrib `puff` | 1 | 17 | 0 | 17 |
| zlib contrib `blast` | 1 | 21 | 9 | 21 |
| libpng `png_create_png_struct` | 1 | 2 | 0 | 4 |
| libpng `png_safe_execute` | 1 | 2 | 1 | 2 |
| libpng `pngtest.c` `test_one_file` | 2 | 140 (81 distinct callees) | 0 | 155 |

An interprocedural "may longjmp" analysis (a function may longjmp when it calls a `longjmp` name, an unknown
external or an indirect callee, or a function that may) would prune little in these programs: their callees all
reach `png_error`/`bits()`-style longjmp paths. It is optional.

### 3.6 Shadow stack (coordination with P3-A)

A `longjmp` abandons frames that may have bumped the P3-A shadow stack (VLAs, `alloca`). The pass saves
`llvm.stacksave` at each `setjmp` site and calls `llvm.stackrestore` on the dispatch path to that site, so the
setjmp caller's assembly's shadow stack pointer returns to its value at the `setjmp` (what native `longjmp` does
with `rsp`). The shadow stack is per assembly (no shared runtime), so a `longjmp` from assembly B to a `setjmp` in
assembly A leaks the shadow stack that B's abandoned frames used. Decision S7: P3-A gives every function that
moves the shadow stack pointer a `fault` clause that restores it (the same "whole body in `.try`" capability as
4.2, zero cost on the normal path); that also covers .NET exceptions passing through C frames. Without it, the
cross-assembly leak is a documented limit.

---

## 4. IL shapes

### 4.1 The `jmp_buf` (16 bytes used, any triple)

| Offset | Size | Field | Written by |
|---|---|---|---|
| 0 | 4 | magic `0x314A534D` ('MSJ1'; the last byte is a version) | `setjmp` site |
| 4 | 4 | site label (1..N within the owning function) | `setjmp` site |
| 8 | 8 (4 on a future 32-bit triple) | address of the owner's status block `{i32 label; i32 val}` | `setjmp` site |
| 16.. | | unused; `sigsetjmp` with a non-zero mask uses `+64 i32 mask_saved`, `+72 128-byte sigset` (glibc's and musl's offsets, inside the 200 bytes) | `sigsetjmp` |

- Fits every buffer: glibc 200, UCRT x64 256 (16-aligned), UCRT arm64 192, and `__builtin_setjmp`'s 5 words (40),
  should S8 be revisited. 8-byte alignment holds on every triple (1.1).
- No pointer into the buffer itself and nothing keyed by its address, so a `memcpy`'d `jmp_buf` works (case c07;
  libraries that save and restore an outer handler by copying do this).
- The value of `longjmp` travels in the status block (armed by `'<msil>longjmp'`), not in the buffer and not in
  the exception: the exception carries only 32 bits (4.4).

### 4.2 The wrapper (validated: `proto/proto.win.inline.il`, method `__msil_sjlj_invoke_thrower`)

```
.method assembly static void '<msil>sjlj.invoke.thrower'(int32, native int) cil managed
{
  .maxstack 4                                  // max(body, 3): the filter needs 3
  .try {
    ldarg.0
    call void 'thrower'(int32)                 // the normal call lowering, any ABI shape
    leave SJ_DONE                              // was 'ret'; a value-returning wrapper: stloc R; leave
  } filter {                                   // stack: object
    isinst [mscorlib]System.Runtime.InteropServices.ExternalException
    dup
    brtrue.s SJ_M
    pop
    ldc.i4.0
    br.s SJ_E
  SJ_M:
    callvirt instance int32 [mscorlib]System.Exception::get_HResult()
    ldarg.1                                    // the status block ("msil-sjlj-invoke"="1")
    conv.i4
    bne.un.s SJ_N
    ldarg.1
    ldind.i4                                   // armed? (label != 0)
    ldc.i4.0
    cgt.un
    br.s SJ_E
  SJ_N:
    ldc.i4.0
  SJ_E:
    endfilter
  } {
    pop                                        // handler: nothing to do, the status block is armed
    leave SJ_DONE
  }
SJ_DONE:
  ret                                          // value-returning: ldloc R; ret (R uninitialized after a catch, unused)
}
```

- The try starts at the first instruction (the stack is empty at method entry); `localloc` inside a `.try` is
  allowed and was measured on Framework, .NET 8 and .NET 10 (`il2`, 42), so a wrapper with byval copies may keep
  its frame setup inside the try. ECMA-335 forbids `localloc` only in filter, catch, finally and fault blocks.
- The non-exceptional cost is one extra managed call (methods with EH are not inlined): measured 1-2 ns on
  .NET 10, 2-3.5 ns on .NET 8, 5-8 ns on Framework per call (`proto/proto2.c`, 1e9 calls).
- ILVerify 8.0 on the prototype: the wrapper reports only `StackByRef` on the `ldind.i4` of a native int (allowed
  code, D21). Both ilasm 4.8 and the `scoped .try { } filter { } { }` text form assemble.
- Per direct callee one wrapper per module (a direct `call`, no function pointer and no thunk); per function type
  one wrapper for indirect calls, which takes the pointer as its first argument and uses the normal indirect-call
  lowering (fast path or `calli unmanaged`, Design.md 4.5).

### 4.3 The throw helper (AsmPrinter-written body)

```
.method assembly static void '<msil>sjlj.throw'(native int) cil managed
{
  .maxstack 3
  ldnull                                       // message: null (ldstr needs user strings in objects, 9.2)
  ldarg.0
  conv.i4                                      // HResult = low 32 bits of the target status block
  newobj instance void [mscorlib]System.Runtime.InteropServices.ExternalException::.ctor(string, int32)
  throw
}
```

An ISel pseudo cannot express this: `newobj` needs the string before the int, and the int is the pseudo's stack
operand (MSILMCInstLower.cpp:620-640 expands `TRAP`, `MEMMOVE` and `MEMORY_BARRIER` after their operands). The
helper is therefore an IR function with a marker attribute whose body the AsmPrinter writes, as it will write the
wrappers' filters. Unhandled, it prints `Unhandled exception. System.Runtime.InteropServices.ExternalException
(0x5F3FE9B0)` and the stack (`'<msil>longjmp'`, the caller of `longjmp`, ...); exit `0xE0434352` on Windows,
SIGABRT (134) on Linux.

### 4.4 The exception object and cross-assembly matching

Assembly B's `'<msil>longjmp'` arms A's status block through the buffer and throws a BCL `ExternalException`; A's
wrapper filter needs no type of B. A frame accepts only when all three hold: the type test, `HResult ==
(int)status` and an armed status block.

- Low 32 bits are enough: status blocks live in localloc frames on the current thread's stack, and a thread stack
  is far below 4 GB, so live frames differ in their low 32 bits. The armed check alone already excludes every
  frame but the target; the `HResult` test excludes a stale arm (an earlier longjmp swallowed by foreign code).
- A COMException or other `ExternalException` from interop passing through a wrapper fails the `HResult` or the
  armed test; any other .NET exception fails the type test. Filters never catch and rethrow, so foreign exceptions
  keep their stack and first-pass semantics.
- Arming before the throw writes 8 bytes into the target frame. A `longjmp` to a buffer whose frame has returned
  (undefined in C) therefore writes into dead stack memory before the process dies on the unhandled exception;
  native `longjmp` to a dead frame jumps into it, which is worse.
- Binding: `ExternalException` and its `.ctor(string, int32)` are in mscorlib on Framework and in
  `System.Runtime.dll` of the .NET 8 and .NET 10 reference packs (verified with llvm-cil-dump: `.class
  'System.Runtime.InteropServices.ExternalException' ... extends 'System.SystemException'`);
  `Exception::get_HResult()` is public on Framework 4.5+ and on .NET (the measured runs call it). Add to
  `llvm/include/llvm/CLI/BCLMembers.def`: `CLI_BCL_TYPE(ExternalException, "System.Runtime.InteropServices",
  "ExternalException", true)`, `CLI_BCL_TYPE(Exception, "System", "Exception", true)`,
  `CLI_BCL_METHOD(ExternalException_ctor, ExternalException, ".ctor", true, "void", "string, int32", Required, "",
  true)` and `CLI_BCL_METHOD(Exception_get_HResult, Exception, "get_HResult", true, "int32", "", Required, "", true)`.
- The Phase 2 recon proposed `Exception.Data` (`recon/p2/backlog.md:458`, `recon/p2/bcl-binding.md:146`). It works
  but allocates a dictionary per `longjmp`, and on Framework `Data`'s setter checks `IsSerializable`; the
  `HResult` protocol costs nothing extra and was measured among the fastest (table 6.2).

---

## 5. Alternatives considered

| | E1 (recommended) | E2 | E3 |
|---|---|---|---|
| Where the try lives | per-callee wrappers (Emscripten JS scheme) | the whole setjmp caller in one `.try`, dispatch `switch` as the try's first instruction, handler `leave`s to it (Wasm SjLj shape, which `catchret`s to `setjmp.dispatch`) | real EH IR: `invoke` + `catchswitch`/`catchpad` funclets, an exception model in the backend |
| IR correctness | explicit edges, `rebuildSSA` | the handler -> dispatch edge is invisible to IR and MIR: every value live into a post-setjmp block must be demoted to frame memory (`DemoteRegToStack`, as `SjLjEHPrepare` does), `MSILRegColoring` must stay off, block placement must keep the try contiguous | full |
| Normal-path cost | 1-8 ns per wrapped call | none per call; demoted values cost loads and stores everywhere in the function | none |
| Backend work | whole-body `.try` for attributed functions; fixed filter/handler; RETURN -> `leave` | the same plus a try that starts after the prologue, RETURN -> `leave` in arbitrary functions, demotion | EH pads in SelectionDAG, PHI elimination on EH edges, EH-clause layout: large |
| Verdict | smallest, validated end to end | a later optimization for hot call loops after `setjmp` if profiles ask | out of scope |

Exception object (measured, table 6.2): `ExternalException(null, hr)` (recommended); a boxed `System.IntPtr`
thrown as a non-Exception object (assemblies without `RuntimeCompatibility(WrapNonExceptionThrows)` see the raw
object; C# sees `RuntimeWrappedException`; 50-70 % slower on Linux .NET 8; the unhandled text says nothing about
`longjmp`; and it is not an exception type, which the user's decision names); `ArgumentOutOfRangeException` with a
boxed address in `ActualValue` (misleading type, commonly caught); a private `Exception` subclass per assembly
(not a BCL type, and other assemblies could only match it through base-class members anyway); `Exception.Data`
(4.4).

Helper placement: per-module IR functions (recommended; works for `-S`, llc, `-c` and LTO alike, no archive
needed, `'<msil>longjmp'` is `linkonce_odr hidden` so msil-ld keeps one per assembly and `&longjmp` compares equal
across objects); the P3-A static C helper archive (`recon/p2/native-imports.md:586` foresaw `setjmp` there:
`setjmp` cannot be an archive function because it returns twice, and the archive is not seen by `-S`); trailer
helpers synthesized by `cli::buildTrailer` (would need a new "linker-provided method symbol" for objects).

---

## 6. Measurements

### 6.1 CLR semantics (all five runtimes identical; `il/sem.il`, `il/thrower.il`, `il/Mid.cs`)

- Filter in assembly A, throw in assembly B, no shared type: matched. A filter may write IL locals of its frame
  (`flt = 1`) and its localloc memory (`*st = 85`); an IL local written in the `.try` before the throw keeps its
  value (`a = 22`) in the handler and after it.
- A C# frame in between with `catch (Exception e) when (...)` that declines: pass 1 runs the C# filter (it sees
  `RuntimeWrappedException` for a raw object), then ours; pass 2 runs the C# `finally`, then our handler.
- A C# `catch (Exception) { throw; }` in between: our filter still receives the original object.
- Nested wrappers of two frames: the inner (other frame) declines, the outer catches; a non-longjmp exception
  passes both and reaches an outer `catch`.

### 6.2 Exception object cost (`sem.exe perf`, ns per throw-filter-catch round trip)

| Variant, depth 1 / 10 | Framework 4.8 | .NET 8 Win | .NET 10 Win | .NET 8 Linux | .NET 10 Linux |
|---|---|---|---|---|---|
| boxed IntPtr | 32,214 / 54,710 | 25,412 / 57,838 | 7,473 / 15,525 | 71,967 (68,188) / 112,965 | 16,261 / 26,080 |
| Exception subclass | 25,964 / 44,589 | 23,670 / 44,931 | 6,938 / 14,213 | 39,207 (46,447) / 64,996 | 14,303 / 27,572 |
| **ExternalException(hr)** | **24,205 / 44,214** | **21,922 / 47,878** | **6,378 / 12,920** | **39,676 (42,523) / 78,539** | **15,426 / 29,729** |
| AOORE(ActualValue) | 23,809 / - | 21,398 / - | 7,929 / - | 43,753 (44,635) / - | 15,901 / - |
| cached subclass object | 24,167 / 47,424 | 20,141 / 49,586 | 6,436 / 14,924 | 65,479 (43,365) / 78,295 | 15,102 / 29,565 |

The exception dispatch dominates; the object's type changes the cost by 10-25 % except the raw boxed object on
Linux .NET 8. Reusing a cached exception object saves nothing measurable. .NET 10's managed exception handling is
3-4x faster than .NET 8's.

### 6.3 1M setjmp/longjmp round trips (`bench/bench.c`; `proto/proto.c` is the same program in E1 shape)

| Build | Time per round trip |
|---|---|
| native clang -O2, glibc (WSL) | ~65 ns (100M in 6.96 s - 0.49 s startup) |
| native clang -O2, UCRT x64 | ~1.9-2.7 us (UCRT's x64 `longjmp` unwinds the frames) |
| today's accidental native binding, .NET 8 / .NET 10 Linux | ~95 ns / ~43 ns (undefined, 1.2) |
| **E1 prototype, .NET Framework 4.8** | **~27 us** (28.07 s - 0.75 s) |
| **E1 prototype, .NET 8 Windows** | **~26 us** (26.54 s - 0.30 s) |
| E1 prototype, .NET 10 Windows | ~6.6 us |
| E1 prototype, .NET 8 Linux | ~44 us |
| E1 prototype, .NET 10 Linux | ~8.5 us |

So a `longjmp` costs 400-700x a glibc one on .NET 8/Framework (100-130x on .NET 10) and 3-13x a UCRT x64 one.
Programs that `longjmp` per error (Lua's `error`/`pcall`, libpng/libjpeg error paths, puff's end-of-input) pay
this per error, not per normal call. Decision S12 asks the user to accept it.

---

## 7. `longjmp` out of a callback called by native code

`qsort/Program.cs` (C#, net48/net8.0/net10.0): a comparator called by the native `qsort` (ucrtbase.dll /
libc.so.6) throws on its 5th call; a C# `catch ... when` below `qsort` should catch it, then the program sorts
again and runs `GC.Collect()`. Variants call the comparator pointer from managed code with `calli unmanaged`
(no native frame in between).

| | Framework | .NET 8 Win | .NET 10 Win | .NET 8 Linux | .NET 10 Linux |
|---|---|---|---|---|---|
| native `qsort`, delegate thunk (`GetFunctionPointerForDelegate`) | caught | caught | caught | **abort 134** | **abort 134** |
| native `qsort`, `[UnmanagedCallersOnly]` | n/a | caught | caught | **abort 134** | **abort 134** |
| `calli unmanaged` of the delegate thunk | caught | caught | caught | **abort 134** | caught |
| `calli unmanaged` of the UCO thunk | n/a | caught | caught | **abort 134** | caught |

"abort 134": `Unhandled exception. LjEx: longjmp` and SIGABRT although a matching handler exists. Consequences:

- Windows: `longjmp` out of a callback through native frames works on every runtime (the native frames are
  skipped as native MSVC `longjmp` skips them; their `__finally` blocks run in pass 2). Nothing to do.
- Linux: impossible through native frames on both .NET 8 and .NET 10 (the runtime cannot unwind native frames).
  Decision S5: document it; on Linux triples the native-callable thunks may wrap their managed call in a filter
  for `ExternalException` that calls `abort()` after printing `longjmp out of a callback called by native code is
  not supported on this platform` (better than the runtime's misleading "Unhandled exception").
- Cross-assembly function pointers (decision S6): libpng's `png_jmpbuf` macro passes `longjmp`'s **address**
  (`png.h:951-952`: `png_set_longjmp_fn((png_ptr), longjmp, (sizeof (jmp_buf)))`) and `png_longjmp` calls it
  indirectly. With libpng in another assembly, that indirect call is `calli unmanaged` of the program's thunk, and
  the throw inside aborts on .NET 8 Linux. Design.md 4.5's fast path checks only "this assembly's thunk table";
  it should also recognize the thunk ranges of the assemblies msil-ld linked (each library can export its range
  through the export class), which also makes such calls cheaper. Otherwise cross-assembly `longjmp` through a
  function pointer works only on Windows and on .NET 10+ Linux.
- Native code that calls the native `longjmp` on a buffer our `setjmp` filled (a natively built library given a
  program's `jmp_buf`) cannot work: the buffer does not hold registers. Our `'<msil>longjmp'` on a buffer a native
  `setjmp` filled fails the magic test and ends in the unhandled exception (no fallback to the native `longjmp`).

---

## 8. Programs that need setjmp/longjmp

| Program | Use | Notes |
|---|---|---|
| Lua 5.4.7 (Phase 3 conformance target) | `ldo.c:65-75`: `LUAI_THROW`/`LUAI_TRY` = `_longjmp`/`_setjmp` with `LUA_USE_POSIX`, `longjmp`/`setjmp` otherwise; `luaD_rawrunprotected` (`ldo.c:138-150`) is the only setjmp function, with one indirect call `(*f)(L, ud)`; `luaD_throw` (`ldo.c:115-136`) is the only longjmp; `status` is `volatile` (`ldo.c:87`) | every `error`/`pcall` failure and every C `lua_yield` is one longjmp; also needs P3-B variadics (`lua_pushfstring`, `printf`) |
| libpng 1.6.44 | `png_jmpbuf` takes `longjmp`'s address; `png_longjmp` calls `png_ptr->longjmp_fn` indirectly; library setjmps in `png_create_png_struct` (`png.c:282`), `png_free_jmpbuf` (`pngerror.c:688`), `png_safe_execute` (`pngerror.c:944`); the application's setjmp in `pngtest.c:942,964` | address-taken `longjmp` (3.2); cross-assembly via function pointer (S6); 140 wrapped calls in `test_one_file` |
| libjpeg / libjpeg-turbo 3.0.4 | the application's `error_exit` longjmps (`example.c`, 5 sites); `turbojpeg.c` has 17 sites (one per API function), `turbojpeg-mp.c` 6, `cjpeg.c` 2, `jcstest.c` 2 | the classic API: library frames between the application's callback and its setjmp (cross-assembly, no function pointer to `longjmp`) |
| zlib 1.3.2 contrib (in the corpus cache) | `puff/puff.c` (`setjmp` at 815, `longjmp` at 134 and 299), `blast/blast.c` (407; 80, 164), `minizip/zip.c:472`, `examples/gzlog.c` | the first real-program tests: small, already fetched (`pufftest.c`, blast's `TEST` main) |
| tinf (corpus) | `test/greatest.h` only with `-DGREATEST_USE_LONGJMP=1` (default 0) | optional |
| SQLite 3.47.0 | none (amalgamation `sqlite3.c`, `shell.c`) | |
| c-testsuite (cache, 879 files) | none | the 70 excluded tests are blocked by `printf`, not by setjmp |

---

## 9. Backend and linker work

### 9.1 What exists

- `llvm/lib/CLI`: `ILBuilder::addExceptionClause` (`ILBuilder.cpp:275-284`, filter kind included, unit-tested in
  `llvm/unittests/CLI/ILBuilderTest.cpp:649-664`), `MethodBodyInfo::Clauses` and the fat-header EH section
  (`llvm/include/llvm/CLI/MethodBody.h:58-68`, `MethodBody.cpp:65-120`), the dumper (`Dump.cpp:1692-1712`).
- msil-ld copies EH clauses of object methods into the image, remapping catch class tokens (filter clauses have
  none): `lld/MSIL/Writer.cpp:993-1022`; the token check reads them (`lld/MSIL/TokenMap.cpp:136-139`).
- `MSILObjectWriter.cpp:606-610` fills `MethodBodyInfo` without clauses; the streamer interface
  (`MCTargetDesc/MSILTargetStreamer.h:60-130`) has no clause event.

### 9.2 What to add

1. **IR pass** `llvm/lib/Target/MSIL/MSILLowerSjLj.cpp` (new; ADAPT of the Emscripten pass per D30, about 700-900
   lines), `MSIL.h` (create function, initialize), `MSILTargetMachine.cpp` (placement 3.1, pass registration),
   `CMakeLists.txt`. Pass name for tests: `msil-lower-sjlj`.
2. **Whole-body try for attributed functions** (`"msil-sjlj-invoke"="<status arg>"`): the AsmPrinter
   (`MSILAsmPrinter.cpp`) opens the try before the first instruction, prints RETURN as `leave` (plus `stloc R` and
   one extra local of the return type), and after the body prints the fixed filter and handler of 4.2 and the
   epilogue; `.maxstack` = max(MSILStackCheck's value, 3). In text, the scoped `.try { } filter { } { }` form; in
   objects, a new `MSILTargetStreamer::emitExceptionClause(kind, try begin/end, filter begin, handler begin/end)`
   event with MCSymbol labels that `MSILTargetObjectStreamer` records and `MSILObjectWriter` turns into
   `cli::ExceptionClause`s (label offsets as for branch targets). The same capability serves P3-A's `fault` clause
   (3.6), Linux thunks (S5) and E2 later.
3. **Throw helper body** (`"msil-sjlj-throw"`): the AsmPrinter writes 4.3. `ldnull` for the message: `ldstr`
   exists only in trailer methods today (`MSILAsmPrinter.cpp:450` `addUserString`), objects have no user-string
   path; a message (`longjmp: no setjmp is active for this jmp_buf`) can follow when objects get user strings.
4. **BCLMembers.def** entries (4.4).
5. **msil-ld**: the native-binding guard for the setjmp family (1.2, S10); nothing else (clauses are carried).
6. No target intrinsic: an `IntrinsicsMSIL.td` would regenerate `IntrinsicEnums.inc` and rebuild most of LLVM;
   marker attributes on generated functions (as `msil-import-module` today) are enough.

---

## 10. Tests

1. `llvm/test/CodeGen/MSIL/sjlj-lower.ll` (llc `-stop-after=msil-lower-sjlj`, as `add-prototypes.ll` does):
   per triple the recognized names (1.1, 3.2) including `_setjmp(buf, frameaddress)` with the `frameaddress`
   erased and aarch64 `_setjmpex(buf, sponentry)`, `__sigsetjmp`, `__longjmp_chk`; the 16-byte prefix stores; the
   phi; one dispatch block; wrappers per direct callee and per indirect type; reachability (a call before the
   `setjmp` not wrapped, one in a loop back to it wrapped); `willreturn` and `strlen` not wrapped, `qsort` wrapped;
   address-taken `longjmp`; nullified setjmp keeps its stores; `rebuildSSA` phis for a value defined before and
   used after.
2. `llvm/test/CodeGen/MSIL/sjlj-diag.ll`: `&setjmp`, `vfork`/`getcontext`, `__builtin_setjmp` texts.
3. `llvm/test/CodeGen/MSIL/sjlj-eh.ll`: IL text of a wrapper (void and value-returning, byval and sret callees,
   indirect) and of `'<msil>sjlj.throw'`, an `%if ilasm` round trip, `llvm-cil-dump` of the object's clause table
   (`.try ... filter ... handler`).
4. Execution, both x86_64 triples, Framework (Windows triple), .NET 8 and .NET 10, WSL: `tests/sjlj-semantics.c`
   (14 cases: basic, `longjmp(b, 0)`, locals, past an inner setjmp frame, recursion, re-armed buffer, copied
   buffer, two sites, `longjmp` through a pointer, out of an indirect call, `switch (setjmp(...))`, Lua-style
   chained handlers, 1000 round trips from varying depth, `sigsetjmp`/`_setjmp`), expected output from the native
   builds (`sjlj-semantics.linux.expected`, `.windows.expected`; -O0 and -O2 agree natively).
5. Cross-assembly (M2 `04-shared` style): a `-shared` library that longjmps to the program's setjmp, directly and
   through a function pointer the program passed (libpng's shape), and a program `jmp_buf` filled in one assembly
   and longjmp'd in another; a link-semantics matrix row `sjlj-xasm`.
6. Native callback: `longjmp` out of a `qsort` comparator: expected to work on the Windows triple; on the Linux
   triple pinned to the S5 diagnostic or exit 134.
7. Programs: zlib `contrib/puff` (pufftest) and `contrib/blast` from the corpus cache once variadics exist; Lua's
   test suite with error-heavy scripts in the P3 conformance gate; libpng `pngtest` and libjpeg-turbo `tjunittest`
   later.
8. The today-behaviour guard (S10): `clang -c` + msil-ld of `sj4.c` on the Linux triple ends in the link error.

---

## 11. Late goals

- **Portable triple `msil64-unknown-dotnet`:** nothing depends on the host libc beyond the recognized names and the
  `jmp_buf` size (16 bytes needed). LLVM libc's `setjmp` is per-architecture assembly and unusable either way; the
  portable libc only needs a `jmp_buf` of at least 16 bytes, 8-aligned, and the names of 3.2. Eases it.
- **Managed calls from C:** .NET exceptions thrown by called .NET code pass the wrapper filters untouched (type
  test); a C `longjmp` with no target surfaces to C# callers as an `ExternalException`. No conflict.
- **Inline IL:** the whole-body `.try` capability and the clause event are a first step towards user-written EH
  regions.
- **FeatureIdeas** (class placement, enclosed instance model, multi-triple assemblies): the design keeps no global
  or thread-static state (only the frame's status block and P3-A's shadow stack pointer through
  `stacksave`/`stackrestore`), and the 16-byte prefix is identical on every triple. Nothing precluded.

---

## 12. Decisions

- **S1. Where does the try region live?** Options: E1 per-callee wrappers with a whole-body `.try` (Emscripten JS
  scheme); E2 the setjmp caller's body in one `.try` with values demoted (Wasm SjLj shape); E3 real EH IR in the
  backend. **Recommend E1.** Evidence: end-to-end prototype correct on five runtimes; 1-8 ns per wrapped call;
  explicit edges keep every pass valid; the CLI library and msil-ld already carry clauses.
- **S2. Which object does `longjmp` throw?** Options: `ExternalException(null, (int)status)`; boxed `IntPtr`;
  `ArgumentOutOfRangeException` with a boxed address; private subclass per assembly; `Exception.Data` payload.
  **Recommend ExternalException.** Evidence: a BCL type in mscorlib and System.Runtime; among the fastest
  (6.2); C# sees a real exception; boxed raw objects are 50-70 % slower on Linux .NET 8 and print
  "RuntimeWrappedException".
- **S3. What the `jmp_buf` holds and how the value travels.** Options: 16-byte prefix `{magic, label, status
  address}` with the target armed by `longjmp` and a 32-bit `HResult` match; the buffer address carried in the
  exception (needs an object payload); a per-frame registry of buffers with a pending flag. **Recommend the
  prefix and arming.** Evidence: fits 40-256-byte buffers, survives `memcpy`, O(1) filters, no allocation beyond
  the exception.
- **S4. Where the helpers live.** Options: per-module IR functions (`'<msil>longjmp'` linkonce_odr hidden,
  internal wrappers, AsmPrinter-written throw body); the P3-A static C helper archive; trailer helpers.
  **Recommend per-module IR functions.** Evidence: works for `-S`, llc, `-c`, LTO; no new linker mechanism.
- **S5. `longjmp` out of callbacks through native frames.** Windows: works on every runtime (measured). Linux:
  aborts on .NET 8 and .NET 10. Options: document only; document plus a Linux thunk filter that aborts with a clear
  message. **Recommend document plus the message.** Needs the user to accept the Linux limit.
- **S6. Cross-assembly calls of `&longjmp` (libpng).** Options: extend Design.md 4.5's indirect-call fast path to
  the thunk ranges of linked toolchain assemblies; accept that it works only on Windows and .NET 10+ Linux.
  **Recommend extending the fast path** (a thunk-WP item). Evidence: table 7.
- **S7. Shadow stack after `longjmp`.** Options: `stacksave`/`stackrestore` in the setjmp caller plus P3-A `fault`
  clauses in shadow-stack functions; the setjmp caller only (cross-assembly leak documented). **Recommend both
  parts.**
- **S8. `__builtin_setjmp`/`__builtin_longjmp`.** Options: keep the M7.b diagnostics; support them in the pass
  (erase clang's `frameaddress`/`stacksave` stores, treat the intrinsics as a site and a `longjmp(buf, 1)`).
  **Recommend keeping the diagnostics in P3** (rare in C; revisit on demand).
- **S9. `sigsetjmp` signal mask.** Options: honor a non-zero `savemask` with the host libc's `pthread_sigmask` at
  the site and in `siglongjmp`; ignore it. **Recommend honoring it on Linux triples.**
- **S10. Today's silent native binding on the Linux triple.** **Recommend** an msil-ld link error for the native
  setjmp family now (first P3-B WP), kept after the pass as a guard, and correcting Status.md section 3.
- **S11. Which calls are wrapped.** Options: every call (Emscripten); calls reachable from a setjmp minus
  intrinsics, `willreturn` callees, helpers and callback-free libc functions; plus an interprocedural may-longjmp
  analysis. **Recommend the reachability rule; the analysis is optional.**
- **S12. Cost acceptance.** A `longjmp` costs ~26-44 us on .NET 8/Framework and ~7-9 us on .NET 10 against ~65 ns
  with glibc and ~2 us with UCRT x64; normal code is unaffected. Needs the user to accept it (no faster CLR
  mechanism exists for unwinding managed frames).

## 13. Open items (unverified in this run)

- MinGW (`x86_64-w64-windows-gnu-msil`) `setjmp` spelling, musl and aarch64-glibc headers (no sysroots here).
- Mono/Unity behaviour of filters and of `ExternalException` (Design.md 7.2, phase 4 check).
- Debugger experience: every `longjmp` is a first-chance exception (noisy in a debugger that breaks on thrown
  CLR exceptions); hosts with `AppDomain.FirstChanceException` handlers see each one.
- PEVerify could not be run meaningfully on the ad hoc ilasm images (it reported metadata internal errors on every
  token of the prototype, unrelated to EH); the gates' msil-verify.py path should be used when the backend emits
  clauses.
