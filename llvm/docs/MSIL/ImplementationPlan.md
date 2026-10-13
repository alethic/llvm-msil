# MSIL backend: implementation plan for Phase 0 and Phase 1

Status: plan v2, 2026-10-12 (v1 revised after an adversarial review; dispositions in Appendix A). Owner: lead planner (agent farm). Base: branch `msil-milestone1` at `8da03434abcc`.
Inputs: [Design.md](Design.md) (settled decisions) and the recon reports in [recon/](recon/) (`triple-objfmt`, `registry-tm`, `clang`, `wasm-inventory`, `isel`, `passes-frame`, `asmprinter`, `cil-runtime`, `tests-ci`, `build-env`), plus [recon/artifacts/](recon/artifacts/): the experiment sources and logs behind `cil-runtime` and `build-env`, the `tests-ci` prototype, and the review experiments (`review-cil/`). Every API, path and line number the plan relies on was checked in this checkout or comes from a recon report that checked it; anything else is marked *unverified*.

## 0. Summary

Phase 0 + Phase 1 of the MSIL backend as 11 small milestones (M0-M10), each one workflow run (parallel WPs on disjoint files -> one serialized build -> tests -> 3-lens review -> verified fixes -> gate -> one [MSIL] commit). M0 lands the build, plan-check, test and CI infrastructure with no LLVM changes. M1 front-loads every widely-included header edit (Triple.h, MCContext.h, MCObjectFileInfo.h, clang diagnostics) plus the clang TargetInfo wrapper, so the only full rebuild is M1 (a TableGen-only pre-build lets implementers syntax-check first). M2 creates the experimental MSIL target with every Phase-1 source file, final query hooks and stubs only for lowering hooks, so CMakeLists.txt, MSIL.h and the pass pipeline never change again. M3-M9 add capabilities in dependency order (leaf functions and stackification; calls, integers and select; FP; control flow; memory, frames and fences; globals and P/Invoke; function pointers, prototypes and aggregates). Every milestone's tests are written by separate test agents from the spec and only use capabilities that already exist (plan 5.5), and verification is enforced in every gate. M10 is the Design.md section 12 conformance gate (25+ programs at -O0/-O2 vs a native oracle, .NET 8/10/Framework, optimizing-JIT legs and a WSL Linux leg). Phases 2-5 are outlined as future milestones. Plan v2 addresses the adversarial review (Appendix A).

| Milestone | Title | WPs | Rebuild | Runnable gate |
|---|---|---|---|---|
| M0 | Infrastructure: tracked build scripts, plan checks, execution harness, jitall, CI skeleton | 5 | none | harness self-tests (ilasm + dotnet, local and WSL), plan checks |
| M1 | Shared plumbing: msil object format, MC environment, target selection, clang wrapper (all widely-included header edits) | 7 | full (only once) | unit tests + clang frontend tests |
| M2 | MSIL target skeleton: registration, TargetMachine, TableGen core, MC layer, module header | 6 | new target + relink | empty module assembles with ilasm (msvc, linux, MinGW) |
| M3 | First executable: leaf functions, stackification, .maxstack, entry point | 5 | target + relink | `ret 42` (llc -O0/-O2) and `int main(void){return 42;}` (clang -O2) run on .NET 8 / WSL .NET 10 |
| M4 | Direct calls, integer arithmetic and select | 5 | target + relink | integer, select and call programs run |
| M5 | Floating point and conversions | 4 | target + relink | FP programs run on .NET 8/10/Framework, optimizing JIT |
| M6 | Control flow | 4 | target + relink | loops, switch, recursion run |
| M7 | Memory, static frames and fences | 5 | target + relink | C at -O0 runs; frames, memory, fences |
| M8 | Globals, module initializer and P/Invoke output | 5 | target + relink | puts output, globals, relocations, ctors |
| M9 | Function pointers, prototype fix-ups and aggregates | 4 | target + relink | function pointers, K&R calls, structs |
| M10 | Milestone-1 conformance: program corpus, verification legs, both triples, documentation | 5 | target + relink | Design.md 12 exit criteria, all legs |

## 1. How agents use this plan

Each milestone is executed by **one workflow run**, then a human checks the result before the next run starts.

0. **Pre-spawn checks** (orchestrator, M1 on): `python msil/utils/plan-check.py ownership` (owned files are disjoint
   inside every milestone) and `python msil/utils/plan-check.py reads M<n>` (every input a WP is told to read exists).
   M0's inputs were checked by the planning run that wrote `recon/cil-runtime.md`, `recon/build-env.md` and
   `recon/artifacts/`.
1. **Plan and spawn.** The orchestrator gives each work package (WP) of the milestone to its own agent, together with
   sections 1-6 of this plan and the WP's `reads` list. Implementation WPs and test WPs run **in parallel**. A WP whose
   id ends in `.0` (for example M1.0) is a build-agent step: it owns no files and only writes the build tree.
2. **Ownership.** All agents share one working tree (`D:\llvm-msil`) and one build directory (`D:\llvm-msil\build`);
   there are no per-agent worktrees (disk and build time rule them out). A WP may create or edit **only** the files in
   its `owned_files` list (a path ending in `/` means every file under that directory). Ownership is per milestone: a
   later milestone may give the same file to a different WP. If a WP needs a change in a file it does not own, it stops
   and reports the exact change to the orchestrator; it never edits the file. The orchestrator itself may edit only this
   plan (amendments, section 12) and files under `llvm/docs/MSIL/recon/` (reviewer reports).
   - **2a. Dependencies.** `Depends on: X` (X in the same milestone) means: the WP is spawned with the others and
     writes its code against the section 5 contracts at once, but it may syntax-check or TableGen anything that
     includes X's files only after X has handed over, and it hands over only after those checks pass. Dependencies are
     acyclic. A WP that owns a header other WPs include writes the contract declarations first and keeps the header
     compilable after every edit, so a dependent's syntax check never fails on a half-written header.
   - **2b. Generator refresh.** When a hand-over changes TableGen input that a dependent compiles against, the
     generated files are refreshed before the dependent's checks: in M1 the build agent rebuilds the TableGen target
     (M1.0); from M2 on implementers run `msil-tblgen.ps1` into their own scratch directory.
3. **No builds by implementers.** Only the designated build agent runs `cmake` or `ninja`, through
   `msil-configure.ps1` / `msil-build.ps1` (exclusive lock). Implementers check their work with
   `msil-syntax.ps1` (compile one TU with `/Zs`, no outputs, no lock) and, from M2 on, `msil-tblgen.ps1`
   (TableGen into a scratch directory). Scratch files go under
   `C:\Users\JEROME~1\AppData\Local\Temp\claude\D--llvm-msil\fa776849-ce33-4cee-bcc7-05382d063d3e\scratchpad\milestones\<M>\<WP>\`.
4. **Independent tests.** Test WPs are written by different agents than the implementation WPs they test, from the
   specification (Design.md, the recon reports, the contracts in section 5 of this plan), **without reading the
   implementation files of the same milestone**. Section 5.4 pins every structural record's operand list so MIR tests
   can be written from the spec. Tests use only capabilities that exist by their milestone (section 5.5).
   Exec-test `.expected` files are generated from a native build with `msil/utils/update-expected.py`, never
   hand-edited (per-OS overrides excepted, each with a comment).
5. **Serialized build, then tests.** When every WP has handed over, the build agent configures (if the milestone says
   so) and builds the milestone's target set; compile errors go back to the owning WP. Then the gate commands run.
6. **Adversarial review panel** with three distinct lenses, each reading the whole diff plus this plan:
   *CIL/semantic correctness* (does the emitted IL do what C and LLVM IR mean, on every runtime?), *LLVM conventions
   and maintainability* (coding standards, upstream idioms, minimal and mechanical diffs against WebAssembly, no stray
   upstream edits), and *test adequacy vs Design.md* (do the tests prove the milestone's goal, including negative
   cases?). Each milestone lists extra review focus points.
7. **Verified fixes.** A finding is fixed only after it is reproduced (a failing test or a concrete IL counterexample).
   The fix is made by an agent working within the same ownership rules; a test/implementation disagreement is settled
   against the specification, not by weakening the test.
   - **7a. Fix-loop ownership.** A verified fix may need a file that no WP of this milestone owns (for example
     `MSILTargetTransformInfo.cpp`, owned only in M2). The orchestrator may then assign that file to exactly one fix
     agent, after first adding a row to the section 12 amendment log whose Milestone column is `M<n>` and whose Change
     column reads ``fix-loop ownership: `<path>` -> <agent>`` (format in section 12). The scope check accepts such files
     for that milestone only.
8. **Gate and commit.** All gate commands pass (including the format and scope checks), then the orchestrator makes
   **one commit** on `msil-milestone1`: `[MSIL] M<n>: <milestone title>`, a body listing the WPs, the gate results and
   any amendments, and the attribution trailer lines the harness requires. The scope check (`plan-check.py scope
   M<n>`, last gate step) stages everything and accepts exactly: files owned by a WP of the milestone, files in that
   milestone's fix-loop amendment rows, and the allow-list `llvm/docs/MSIL/ImplementationPlan.md` and
   `llvm/docs/MSIL/recon/**`.
9. **Interface changes.** The names in section 5 are contracts. If one must change, the orchestrator records it in the
   amendment log (section 12) before the milestone's commit, so later milestones see it.

## 2. Conventions

- **Code:** [LLVM Coding Standards](../CodingStandards.rst); LLVM file header and license banner on every new file;
  `namespace llvm` with target helpers in `llvm::MSIL`, ISD nodes in `llvm::MSILISD`, TSFlags masks in `llvm::MSILII`;
  classes `MSIL<Name>`; legacy passes `MSIL<Name>Legacy` with `createMSIL<Name>LegacyPass()` and
  `initializeMSIL<Name>LegacyPass(PassRegistry &)`; `DEBUG_TYPE` and `cl::opt` names `msil-<kebab-name>`;
  `LLVM_ABI` on new public API in `llvm/include`. User-facing problems are diagnostics
  (`DiagnosticInfoUnsupported`, `LLVMContext::emitError`, clang `Diag`); `report_fatal_error` /
  `reportFatalInternalError` only for internal invariants (MSILStackCheck) and for stubs that later milestones replace.
- **Stubs vs query hooks (D33).** Only lowering hooks may be stubs: LowerOperation cases, LowerFormalArguments /
  LowerCall / LowerReturn, custom inserters, eliminateFrameIndex and AsmPrinter emit hooks. TargetLowering and TTI
  query hooks (`is*`, `should*`, `allows*`, `get*Ty`, `convert*`, `CanLowerReturn`) get their final bodies in M2,
  because the IR optimizer calls them through TTI at -O1 and above long before the code they describe exists.
- **Diagnostics leave well-formed output.** llc and clang keep running after a `DS_Error` (llc checks `HasErrors` only
  after `PM.run`, `llvm/tools/llc/lib/llcdriver.cpp:881-884`), so every path that diagnoses must hand later passes
  something valid that never reaches a stub: MSILPrepareIR deletes the body of a diagnosed definition and removes or
  neutralizes diagnosed globals; LowerCall for a diagnosed callee builds no call (it returns the chain and UNDEF
  results); other lowering hooks return a well-formed dummy value after the diagnostic. The visible result is the
  diagnostic and exit code 1, never `LLVM ERROR` or a stack dump.
- **Copied WebAssembly files** keep the upstream structure; the first comment after the banner is
  `// Derived from llvm/lib/Target/WebAssembly/<File> at 8da03434abcc.`; renames are mechanical
  (WebAssembly -> MSIL, wasm- -> msil-); wasm-only code is deleted, not commented out (decision D30).
- **Formatting:** clang-format (repo `.clang-format`, VS-bundled clang-format 22) on changed C++ lines; the gate runs
  `git-clang-format --extensions c,cpp,h,hpp,hxx,cxx,inc,cppm,cl --diff HEAD` over `llvm/lib llvm/include
  llvm/unittests clang/lib clang/include` (the extension list of `llvm/utils/git/code-format-helper.py`). `.td` files
  are never clang-formatted: git-clang-format would include them by default, and only `llvm/.clang-format` disables
  TableGen, not the root or `clang/.clang-format`. Tests are not clang-formatted.
- **Commits:** one per milestone on `msil-milestone1`, subject `[MSIL] M<n>: <title>`, body with WPs, gate results and
  any amendments, ending with the attribution lines the harness provides.
- **Tests:** lit tests use kebab-case names after the feature (`switch-i64.ll`, `frame-overalign.ll`); keep the
  WebAssembly name when porting (`reg-stackify.ll`, `function-bitcasts.ll`). llc tests always pass `-mtriple`, run
  both `x86_64-unknown-linux-gnu-msil` and `x86_64-pc-windows-msvc-msil` with a shared `CHECK` prefix (`LNX`/`WIN`
  where they differ), add `-verify-machineinstrs`, use `CHECK-LABEL: .method {{.*}}'<name>'(`, never contain a
  `target datalayout`, and end with `; RUN: %if ilasm %{ llc ... -o %t.il && %ilasm-verify -output=%t.dll %t.il %}`
  when they produce a full module. MIR tests are named after the behaviour they check (`stack-check-errors.mir`,
  `localloc-root.mir`) and use `-run-pass=msil-<pass>` with the operand lists of section 5.4. A test that expects a
  user diagnostic runs `not llc ... 2>&1 | FileCheck %s --implicit-check-not="LLVM ERROR"
  --implicit-check-not="PLEASE submit"`: on Windows `report_fatal_error` ends in `abort()` (exit code 3), which plain
  `not` accepts as a failure, so only the output shows a crash. Internal errors (MSILStackCheck) use `not --crash`.
  Gate commands that expect an error use `msil-expect-error.ps1` for the same reason. clang tests that run the backend
  have `// REQUIRES: msil-registered-target`; frontend-only tests have none. Unit tests live in
  `llvm/unittests/Target/MSIL/MSIL<Topic>Test.cpp` and every new gtest name contains `MSIL`; gates that must prove a
  gtest ran (not skipped) use `msil-gtest.ps1 -NoSkip -MinTests N`. Execution tests follow section 5.5.
- **Installs:** the user pre-approved installing build and test tooling (2026-10-12): NuGet packages, dotnet tools,
  pip packages, WSL apt packages. Prefer what is already installed (section 8); name every install (package and
  version) in the hand-over. Never add a download to a gate or build script without a WP owning that change.
- **Never:** read or copy
  `%APPDATA%\NuGet\NuGet.Config`; edit `llvm/docs/MSIL/Design.md`; run `ninja check-llvm`, `check-clang` or `check-all`;
  modify upstream GitHub workflows.

## 3. Settled decisions (lead planner)

Recon reports disagreed in a few places or left questions open. These are the binding answers for Phase 1. Items that genuinely need the user are in section 10; until the user answers, the default given here applies.

| # | Topic | Decision |
|---|---|---|
| D1 | TableGen form | **One record per instruction** (wasm-inventory 3.3), not the register/stack dual form of isel.md 10.1. Nothing in Phase 1 consumes stack forms; `_S` twins can be added mechanically if phase 2 wants an `.il` parser. WebAssembly record names are kept where semantics match, so copied passes compile unchanged. |
| D2 | 64-bit values | Register class `I64` (pointers and 64-bit integers) is CIL **`native int`** everywhere on 64-bit triples: locals, params, results, evaluation stack. `int64` is never emitted (PEVerify flags native int/int64 mixing). `CONST_I64` prints `ldc.i4 N; conv.i` or `ldc.i8 N; conv.i`. |
| D3 | Triple API | Add exactly `Triple::MSIL`, `isOSBinFormatMSIL()`, `getDefaultObjectFormat()` and `getNativeFormatTriple()`. `computeDataLayout` of an MSIL triple is the native triple's (byte-identical). `getDefaultExceptionHandling()` is None; `getDefaultLongDoubleFormat()` is IEEEdouble. `normalize()` is unchanged (`x86_64-pc-win32-msil` -> `x86_64-pc-windows-msil`). `supportsCOMDAT()` stays true; the backend ignores comdats (D26). |
| D4 | Target selection | Option A of registry-tm.md 1.2: only `TargetRegistry.cpp` changes; the target is named `msil`, never matches an arch, is chosen for every MSIL triple, and `-march=msil` switches the object format. |
| D5 | MC environment | `MCContext::IsMSIL` + header-only `MCSectionMSIL` + `MCContext::getMSILSection` + `MCObjectFileInfo::initMSILMCObjectFileInfo` (triple-objfmt.md 4). |
| D6 | Phase-1 triples | clang: x86_64 and aarch64 on Linux (gnu, musl) and Windows (msvc, gnu). `*-apple-darwin*-msil` and `i686-*-msil` are rejected as unknown triples until phases 4/5 (they need the UserLabelPrefix rework for `m:o`/`m:x`). Backend: x86_64 and aarch64 MSIL triples. Execution: `x86_64-pc-windows-msvc-msil` on Windows, `x86_64-unknown-linux-gnu-msil` under WSL (.NET 10). |
| D7 | Subtarget | One processor `generic`, no features; every native CPU/feature string from clang is accepted silently; native strings are kept on the subtarget for later SIMD mapping. |
| D8 | TTI / codegen options | MSILTTIImpl: 0 vector registers, no lookup tables (`shouldBuildLookupTables()` false: SimplifyCFG would otherwise turn switches of constants into `@switch.table` globals plus loads, `BasicTTIImpl.h:644-648`) and no relative lookup tables, no partial/runtime unroll. `Reloc::Static`, `CodeModel::Small`, `TrapUnreachable = true`, `NoTrapAfterNoreturn = false`, `EmitAddrsig = false`, `ExceptionModel` forced to None whatever the driver passes (clang's MinGW toolchain passes `-exception-model=seh` even for C), `usesPhysRegsForValues() == false`. TargetLowering and TTI query hooks are final in M2 (D33). |
| D9 | Build configuration | Keep the build-env configuration: targets `X86;WebAssembly` + experimental `MSIL`, projects `clang;lld` (lld unused in Phase 1), clang-cl/lld-link, Release + assertions, `LLVM_OPTIMIZED_TABLEGEN=OFF`. No AArch64 backend (MSIL is selected by object format; clang's AArch64 TargetInfo is always built). |
| D10 | Assembly references | Phase 1 emits only `.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }` and uses only mscorlib members (`Marshal::AllocHGlobal`, `Marshal::StringToHGlobalAnsi`, `Environment::GetCommandLineArgs`, `Buffer::MemoryCopy`, `Interlocked::MemoryBarrier`). Verified to run unchanged on .NET 8/9/10 and .NET Framework 4.8 (cil-runtime e01/e01c; isel fence experiment). No `DisableRuntimeMarshalling` (blittable `preservesig` signatures do not need it). The System.Runtime header arrives with the phase-3 `-mdotnet` profile (user confirmation pending, section 10). |
| D11 | Identifiers and labels | Every symbol-derived identifier is single-quoted through `MSIL::printQuotedName` (escapes: `\'`, backslash as `\134`, control bytes as octal, UTF-8 raw plus a BOM when any global name is non-ASCII). Compiler-generated names use the `<msil>` prefix (no dots in type names). Block labels are `@BB<f>_<b>` (`InternalSymbolPrefix = "@"`). Names come from IR names, never from the Mangler. |
| D12 | Globals | One unmanaged data segment per module, allocated and filled by `<Module>::.cctor` from an RVA blob, with straight-line relocations and ctors; one `initonly native int` address field per global; code uses `ldsfld native int 'g'` (+ `ldc.i4 off; add`). Initializer bytes are captured from `AsmPrinter::emitGlobalConstant` through a collector streamer (NVPTX-style walker as fallback). RVA memory is never written and `&(label)` data is never emitted. |
| D13 | Frames | Design.md 4.2: one `localloc` at entry sized for the static frame, FP as a fake physreg turned into a vreg, `.locals` never `init`. The assumed localloc alignment is **8 bytes** (`MSILFrameLowering::getLocallocAlign()`): constant-size localloc of 32 bytes or less is only 8-aligned on .NET Framework 4.8 and in .NET 8 tiered code (review experiment `recon/artifacts/review-cil/align16.il`; the recon had measured only variable sizes), so any frame with MaxAlign > 8 (`__int128`, `_Alignas(16)`, arrays clang aligns to 16) over-allocates MaxAlign - 8 bytes and aligns up. Dynamic alloca, stacksave/stackrestore and frameaddress are diagnosed in Phase 1 (shadow stack in phase 3). The frame model stays behind one helper (user decision on a value-type frame pending, section 10). |
| D14 | P/Invoke (Phase-1 import table) | An undefined function referenced from code or data becomes `pinvokeimpl("<lib>" cdecl) ... preservesig` only if it is a Phase-1 import: `puts` and `putchar` from the C library (linux-gnu/linux-musl `libc`, windows-msvc `ucrtbase`, windows-gnu `msvcrt`); the C math functions clang or the legalizer emit (sqrt, cbrt, floor, ceil, trunc, round, rint, nearbyint, fmod, remainder, fma, fmin, fmax, fdim, sin, cos, tan, asin, acos, atan, atan2, sinh, cosh, tanh, exp, exp2, expm1, log, log2, log10, log1p, pow, ldexp, frexp, modf, hypot, and their `f` variants) from the math library (linux-gnu `libm.so.6`, because glibc keeps libm separate and CoreCLR special-cases only `"libc"`; linux-musl `libc`; windows-msvc `ucrtbase`; windows-gnu `msvcrt`); or any function carrying the `msil-import-module` attribute (`msil-import-name` sets the entry point). Every other undefined function is an error: Phase 1 has no linker, and Design.md 5.2 step 4 makes an unresolved symbol a link error, not a runtime failure. Variadic callees are errors. Library calls whose name starts with `__` (compiler-rt and `__atomic_*` helpers) are errors until phase 3. Phase 2's msil-ld replaces the table with real symbol resolution. |
| D15 | Memory intrinsics | Never libcalls. Constant-length memcpy <= 2^32-1: `cpblk`; memset: `initblk` (variable length truncated with `conv.i4`, documented 4 GiB limit); memmove and variable-length memcpy: `[mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64)` (NativeMemory is net6+ only). |
| D16 | BCL intrinsics | None in Phase 1: ctlz/cttz/ctpop/bswap/rotates use LLVM's generic arithmetic expansions; fabs, copysign and FP<->integer bitcasts use the generic expansions through a stack slot and therefore work from M7 (their tests live in M7). isel.md decision 11 moves to the phase-3 net8.0 profile. |
| D17 | uint64 -> FP | Custom for f32 and f64 results. `LowerUINT_TO_FP` emits `s = srl x, 63; y = or (srl x, trunc s), (and x, s); r = sint_to_fp y; result = fmul r, sint_to_fp(add s, 1)`: compiler-rt's halving with a sticky bit, with no SELECT and no memory. `conv.r4`/`conv.r8` of a non-negative int64 round once on every runtime (cil-runtime e05b) and the multiply by 1 or 2 is exact, so the result is correctly rounded. Never `conv.r.un` for 64-bit inputs (it double-rounds on .NET 8/9 and Framework, e05), and never the generic expansions (BITCAST through a stack slot for f64, SELECT for f32). |
| D18 | Calls | WebAssembly's `CALL_PARAMS`/`CALL_RESULTS` split and custom inserter; signatures from `computeSignatureVTs`; `calli` signatures from the MI; MSILFixFunctionBitcasts required but **without** WebAssembly's main renaming. |
| D19 | Varargs | Variadic definitions and calls are diagnosed in Phase 1 (phase 3 implements Design.md 4.7). |
| D20 | .maxstack | Computed and verified by MSILStackCheck (last machine pass, always on, fatal on violation) with the `StackPeakExtra` TSFlags field. |
| D21 | Verification policy | A test passes verification when MSILStackCheck, `jitall` (force-JIT every method under time and memory limits), ILVerify with a code allow-list (`Unverifiable`, `InitLocals`, `StackByRef`, `UnmanagedPointer`) and, on Windows, PEVerify with its allowed messages are all clean. PEVerify's `[found Native Int][expected unmanaged pointer] Unexpected type on the stack.` is allowed because `Buffer::MemoryCopy(void*, void*, int64, int64)` takes native int addresses (D15; review experiment `ilv2.il`); every other `Unexpected type on the stack` stays fatal. Enforced in every gate from M0 (`msil_require_verify=1`, D38). This replaces the literal 'ilverify reports no stack-shape errors' (ILVerify stops at the first error per method and cannot verify `calli`); user approval pending, section 10. |
| D22 | Runtimes in tests | .NET 8 by default; .NET 10 and .NET Framework 4.8 legs in the gates where runtime behaviour differs (FP, conversions, frames, globals) and in M10; WSL .NET 10 for the Linux triple; optimizing-JIT legs with `DOTNET_TieredCompilation=0` (D38). |
| D23 | Pass manager | Legacy pipeline only in Phase 1. Copied passes drop their new-PM wrapper classes; `MSILPassRegistry.def` and `MSILCodeGenPassBuilder` are phase 4. |
| D24 | Memory flags | A post-ISel `MSILSetMemFlags` pass (modeled on WebAssemblySetP2AlignOperands) fills a memflags immediate (bits 0-2 unaligned N in {1,2,4}, bit 3 volatile), not a root-aware ComplexPattern. |
| D25 | Stack protector | MSILPrepareIR strips `ssp`/`sspstrong`/`sspreq`/`safestack` with a warning (the Linux and MSVC toolchains default to no protector anyway). |
| D26 | Linkage | linkonce/weak/comdat definitions are ordinary definitions in the single-module world (MSVC string literals `??_C@...` included). External global variables, `thread_local`, aliases, ifuncs, non-empty `llvm.global_dtors` and module asm are errors. |
| D27 | Entry point | `'<msil>Main'(string[])` with `.entrypoint`: calls `main()` directly, or builds a NULL-terminated argv from `Environment.GetCommandLineArgs()` (argv[0] = dll path) for `main(int32, native int)`; any other signature is an error. |
| D28 | Assembly name | `-msil-assembly-name`, default the stem of `source_filename`. The harness runs every DLL from a private directory, so same-named assemblies never collide. |
| D29 | Method text | One-line header `.method <public|assembly> static <ret> '<name>'(<params>) cil managed`, `{` on the next line, `.maxstack`, optional `.locals (...)`, body, and `} // end of method '<name>'` (the update_llc_test_checks regex depends on it). |
| D30 | Copied files | Copy only the curated COPY/ADAPT set (wasm-inventory 4.4), never wholesale; derivation line in every copied file; diffs kept mechanical for future merges. |
| D31 | ilasm | Default ilasm on Windows is .NET Framework ilasm 4.8 (`-nologo -quiet -dll -pe64 -x64`); modern ilasm 10.0.12 from the local NuGet cache is a second leg in M10; Linux CI uses the NuGet ilasm. |
| D32 | Early exec tests | Until frames exist (M7), every C execution test uses `%{msil_c_O2}` with the first-line comment '-O0 needs frames (M7)': clang -O0 always puts parameters and locals in allocas, and on windows-msvc even `int main(void){return 42;}` gets a return-value alloca and a store. `.ll` tests through llc carry the -O0 coverage before then. M7.d switches every earlier C test to `%{msil_c}`. |
| D33 | Stubs | M2 creates every Phase-1 `.cpp`/`.h`, so CMakeLists.txt, MSIL.h and the pipeline never change again in Phase 1. Only lowering hooks are stubs (they fail loudly naming the milestone that implements them); query hooks are final in M2 (section 2). |
| D34 | Select (M4) | ISD::SELECT is Legal from M4 through `SELECT_<RC>` pseudos expanded by the custom inserter `emitSelect` into a brtrue triangle with a PHI (isel.md 11.6). Generic legalization emits SELECT long before control flow exists (variable-amount i128 shifts via expandShiftParts, min/max expansions), so it cannot wait for M6. `convertSelectOfConstantsToMath` returns true (final in M2), so sext/zext of a compare stays arithmetic instead of becoming select(setcc, -1, 0) (`DAGCombiner.cpp` ~16088). |
| D35 | Switch index | `getJumpTableRegTy` returns i32 (NVPTX precedent), so every jump table indexes with int32 and only `BR_TABLE_I32` exists: CIL `switch` on a native int fails ILVerify with StackUnexpected (review experiment `sw.il`). SelectionDAGBuilder range-checks the full-width value before truncating, so 64-bit switch values stay correct (they print `conv.i4` before `switch`). LLVM's range check is never removed. |
| D36 | Fences | A singlethread fence (clang's translation of `asm volatile("" ::: "memory")`) selects the generic `MEMBARRIER` pseudo (AArch64 precedent; printed as the comment `// MEMBARRIER`; RegStackify does not move memory operations across it). Any other fence calls `[mscorlib]System.Threading.Interlocked::MemoryBarrier()`. Atomic loads, stores and RMW stay unsupported until phase 3. |
| D37 | Exit codes and checks | Test exit statuses are 0..255 on every OS: Linux truncates to 8 bits, so a larger Windows value would make the WSL leg pass by accident. Bitmask tests use at most 8 bits; programs mask their checksum (`& 0xff`). Before M6 (no branches) C tests accumulate results with the branch-free `MSIL_CHECK(acc, bit, cond)` and `return acc;`; `MSIL_EXPECT` is used from M6 on. `main` always returns explicitly (`-Werror=return-type`, because `-ffreestanding` disables main's implicit `return 0`). The harness and update-expected.py reject out-of-range statuses. |
| D38 | Enforced verification and optimizing-JIT legs | Every gate passes `msil_require_verify=1`, so a missing ILVerify, jitall or (Windows) PEVerify is a configuration error rather than UNSUPPORTED. Because short tests run almost entirely as Tier-0 code, the gates add `msil_dotnet_env=DOTNET_TieredCompilation=0` legs: 02-fp in M5, 04-frames and 05-memory in M7, and all of msil/test/M1 on Windows .NET 8 and WSL .NET 10 in M10. |

## 4. Build configuration and command reference

```text
Host: Windows 11, i7-7700K (4 cores / 8 threads), 64 GB RAM, VS 2026 Professional (clang-cl 22.1.3, lld-link, llvm-lib, CMake 4.3.1 inside the dev env), ninja 1.13.2, Python 3.14.7. Build dir D:/llvm-msil/build (gitignored, already configured by the build-env recon). Until M0 lands the helper scripts live in D:/llvm-msil/build/msil-scripts/ (same names, same arguments); M0 moves them to D:/llvm-msil/msil/utils/build/ and everything below uses that path.

Configure (build agent only):
  M0, M1:   pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-configure.ps1
  M2 on:    pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-configure.ps1 -Experimental MSIL
  (M0 adds -DLLVM_EXTERNAL_PROJECTS=msil -DLLVM_EXTERNAL_MSIL_SOURCE_DIR=D:/llvm-msil/msil to the script's defaults.)
Equivalent raw command, inside the VS x64 dev environment:
  cmake -G Ninja -S D:/llvm-msil/llvm -B D:/llvm-msil/build -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON -DLLVM_ENABLE_PROJECTS="clang;lld" -DLLVM_TARGETS_TO_BUILD="X86;WebAssembly" -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=MSIL -DLLVM_EXTERNAL_PROJECTS=msil -DLLVM_EXTERNAL_MSIL_SOURCE_DIR=D:/llvm-msil/msil -DLLVM_OPTIMIZED_TABLEGEN=OFF -DLLVM_PARALLEL_COMPILE_JOBS=8 -DLLVM_PARALLEL_LINK_JOBS=2 -DLLVM_ENABLE_PDB=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_TESTS=ON -DLLVM_BUILD_TESTS=OFF -DLLVM_APPEND_VC_REV=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_DIA_SDK=OFF -DLLVM_ENABLE_TELEMETRY=OFF -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_ENABLE_OBJC_REWRITER=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DPython3_EXECUTABLE=C:/Users/JeromeHaltom/AppData/Local/Python/pythoncore-3.14-64/python.exe -DCMAKE_C_COMPILER="C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-cl.exe" -DCMAKE_CXX_COMPILER="C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-cl.exe" -DCMAKE_LINKER="C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/lld-link.exe" -DCMAKE_AR="C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/llvm-lib.exe" -DLLVM_ENABLE_LLD=ON
  (Python3_EXECUTABLE must use forward slashes; MSIL goes only in LLVM_EXPERIMENTAL_TARGETS_TO_BUILD, never LLVM_TARGETS_TO_BUILD.)

Build (build agent only; serialized by the orchestrator; long builds run with run_in_background and the agent waits; the script takes an exclusive file lock and throttles -j when RAM is low):
  pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 <targets>
  M1 target set:   llc opt clang FileCheck not count split-file llvm-config TargetParserTests MCTests
  M2+ target set:  llc opt clang FileCheck not count split-file llvm-config MCTests MSILTests
Lit (shared lock, source-tree paths):   pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 [-LitArgs "-sv --param k=v"] <paths>
Run one built tool (shared lock):        pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-run.ps1 llc -mtriple=x86_64-pc-windows-msvc-msil in.ll -o -
Syntax-check C++ without ninja (any agent, lock-free):  pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-syntax.ps1 [-FlagsFrom <tu>] [-ExtraInclude <dirs>] <files>
TableGen check without ninja (any agent, M2+):           pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-tblgen.ps1 -Out <scratch dir>
gtest with skip/count checks (gates):                     pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-gtest.ps1 -Exe <test.exe> -Filter <pattern> [-NoSkip] [-MinTests N]
Expect a clean diagnostic, not a crash (gates):            pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-expect-error.ps1 -Pattern <regex> -- <exe> <args>
Plan checks (orchestrator and gates):                      python D:/llvm-msil/msil/utils/plan-check.py ownership | reads M<n> | scope M<n>
Milestone pre-spawn (orchestrator, M1 on):                 python D:/llvm-msil/msil/utils/plan-check.py ownership && python D:/llvm-msil/msil/utils/plan-check.py reads M<n>
Unit tests: D:/llvm-msil/build/unittests/Target/MSIL/MSILTests.exe; D:/llvm-msil/build/unittests/TargetParser/TargetParserTests.exe; D:/llvm-msil/build/unittests/MC/MCTests.exe
Format check: pwsh -NoProfile -Command "Set-Location D:/llvm-msil; git add -A; python 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/git-clang-format' --binary 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe' --extensions c,cpp,h,hpp,hxx,cxx,inc,cppm,cl --diff HEAD -- llvm/lib llvm/include llvm/unittests clang/lib clang/include"  (must print no diff; same extension list as llvm/utils/git/code-format-helper.py, so .td files are never formatted)
Never: ninja check-llvm / check-clang / check-all (they build every tool); LLVM_TARGETS_TO_BUILD=MSIL (fatal); LLVM_OPTIMIZED_TABLEGEN=ON with assertions (nested native build); a second build tree on D: (34 GB free).
```

Rebuild costs (step counts from `ninja -t commands` in the build-env recon; times are estimates on this shared 4C/8T host): M1 target set 3,462 steps (2,762 C++ TUs, 242 tablegen runs), 1-2.5 h; the whole tree 4,817 steps. After M1 only the MSIL target and relinks are rebuilt (5-30 min). The build script drops to `-j 4` below 16 GB free RAM and `-j 2` below 6 GB, because other agents' .NET experiments have been seen holding 40 GB.

## 5. Shared contracts

### 5.1 ILAsm output contract (Phase 1)

Everything `llc`/`clang -S` prints for an MSIL triple has this shape. Tests are written against it, the `MSILTargetStreamer` and `MSILInstPrinter` produce it, and `update_llc_test_checks.py` parses it.

```text
// MSIL module generated by LLVM
// source_filename = "prog.c"
// target triple   = "x86_64-pc-windows-msvc-msil"
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'prog' { .ver 0:0:0:0 }

.method public static int32 'sum'(native int, int32) cil managed
{
  .maxstack 4
  .locals (int32, int32)
  ldc.i4.0
  stloc.0
  ldarg.1
  ldc.i4.1
  blt @BB0_3
  ...
@BB0_3:
  ldloc.0
  ret
} // end of method 'sum'

// ---- undefined external functions: P/Invoke ----
.method public hidebysig static pinvokeimpl("ucrtbase" cdecl) int32 'puts'(native int) cil managed preservesig {}

// ---- data segment: 76 bytes (72 initialized + 4 zero), align 16 ----
.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
{
  .pack 1
  .size 72
}
.data cil '<msil>image_d' = bytearray (
  05 00 00 00 03 00 00 00 09 00 00 00 01 00 00 00   // 0x0000
  ... )
.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'
.field private static initonly native int '<msil>data'
.field public static initonly native int 'table'        // +0
.field assembly static initonly native int '.L.str'     // +20

.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 3
  ldc.i4 91
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  ...                                                    // align, cpblk image, initblk bss,
  ret                                                    // address fields, relocations, ctors
} // end of method '.cctor'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 4
  ...                                                    // argv from Environment::GetCommandLineArgs()
  call int32 'main'(int32, native int)
  ret
} // end of method '<msil>Main'
```

Rules (each verified by the recon experiments):
- Types: `int32` (class I32), `native int` (class I64, D2), `float32`, `float64`, `void`. Parameters are unnamed.
- Access: `public` for non-local linkage, `assembly` for internal/private (never `private`, never omitted).
- Instructions: one CIL opcode per line, two-space indent. Short forms are chosen by the printer: `ldarg.0-3`,
  `ldarg.s N` (N < 256), `ldarg N`; same for `ldloc`, `stloc`, `starg`; `ldc.i4.m1`, `ldc.i4.0-8`, `ldc.i4.s`
  (-128..127), `ldc.i4`. Branches always use the long forms (`br`, `brtrue`, `brfalse`, `beq`, `bne.un`, `blt`,
  `blt.un`, ...). FP constants are bit-exact: `ldc.r4 float32(0x7FC00000)`, `ldc.r8 float64(0x3FF8000000000000)`.
- Calls: `call int32 'f'(int32, native int)` (the same text as the callee's `.method` signature), `ldftn int32 'f'(int32)`,
  `calli int32(native int)` (managed in Phase 1).
- Globals: `ldsfld native int 'g'`, plus `ldc.i4 <off>` and `add` (or `ldc.i8 <off>; conv.i; add`) when the offset is
  non-zero.
- `switch (@BB0_1, @BB0_2)` is followed by `br @BB0_<default>`; the switch index is always int32 (a 64-bit value is
  range-checked first and converted with `conv.i4`, D35). Traps are `ldnull` + `throw`. Memory prefixes
  `unaligned. N` and `volatile.` are on their own lines before the opcode. Tees print `dup` + the store. A singlethread
  fence prints the comment `// MEMBARRIER`; other fences print
  `call void [mscorlib]System.Threading.Interlocked::MemoryBarrier()` (D36).
- Assembled with `ilasm -nologo -quiet -dll -pe64 -x64 -output=<x>.dll <x>.il`; run with
  `dotnet exec --runtimeconfig <net8 config> <x>.dll` from a private directory.

### 5.2 Target source layout and ownership across milestones

`llvm/lib/Target/MSIL/` in Phase 1 (M2 creates every file; the table lists which WP owns it in each milestone). `CMakeLists.txt` lists every `.cpp` below from M2 on. A file that no WP of the current milestone owns can change only through a fix-loop ownership grant (section 1.7a).

| File | Owned by (WP per milestone) |
|---|---|
| `CMakeLists.txt` | M2.a |
| `MCTargetDesc/CMakeLists.txt` | M2.a |
| `MCTargetDesc/MSILInstPrinter.cpp` | M2.c, M3.d, M4.c, M6.b, M8.c, M9.a |
| `MCTargetDesc/MSILInstPrinter.h` | M2.c, M3.d, M4.c, M6.b, M8.c, M9.a |
| `MCTargetDesc/MSILMCAsmInfo.cpp` | M2.c |
| `MCTargetDesc/MSILMCAsmInfo.h` | M2.c |
| `MCTargetDesc/MSILMCExpr.cpp` | M2.c |
| `MCTargetDesc/MSILMCExpr.h` | M2.c |
| `MCTargetDesc/MSILMCTargetDesc.cpp` | M2.c |
| `MCTargetDesc/MSILMCTargetDesc.h` | M2.b |
| `MCTargetDesc/MSILMCTypeUtilities.cpp` | M2.c |
| `MCTargetDesc/MSILMCTypeUtilities.h` | M2.c |
| `MCTargetDesc/MSILTargetStreamer.cpp` | M2.c, M8.c |
| `MCTargetDesc/MSILTargetStreamer.h` | M2.c, M8.c |
| `MSIL.h` | M2.f |
| `MSIL.td` | M2.b |
| `MSILAddMissingPrototypes.cpp` | M2.a, M9.b |
| `MSILArgumentMove.cpp` | M2.a, M3.b |
| `MSILAsmPrinter.cpp` | M2.d, M3.d, M4.c, M8.c |
| `MSILAsmPrinter.h` | M2.d, M3.d, M4.c, M8.c |
| `MSILCleanCodeAfterTrap.cpp` | M2.a, M3.b |
| `MSILDataSegment.cpp` | M2.a, M8.b |
| `MSILDataSegment.h` | M2.a, M8.b |
| `MSILDebugValueManager.cpp` | M2.a, M3.a |
| `MSILDebugValueManager.h` | M2.a, M3.a |
| `MSILExplicitLocals.cpp` | M2.a, M3.a |
| `MSILFixFunctionBitcasts.cpp` | M2.a, M9.b |
| `MSILFrameLowering.cpp` | M2.f, M7.b |
| `MSILFrameLowering.h` | M2.f, M7.b |
| `MSILISelDAGToDAG.cpp` | M2.f, M3.c, M4.a, M7.b, M9.a |
| `MSILISelLowering.cpp` | M2.f, M3.c, M4.b, M5.a, M6.a, M7.b, M8.a |
| `MSILISelLowering.h` | M2.f, M3.c, M4.b, M5.a, M6.a, M7.b, M8.a |
| `MSILISelLoweringCall.cpp` | M2.f, M3.c, M4.a, M8.a, M9.a |
| `MSILInstrCall.td` | M2.b, M4.a, M9.a |
| `MSILInstrControl.td` | M2.b, M3.c, M4.b, M6.a |
| `MSILInstrConv.td` | M2.b, M4.b, M5.a |
| `MSILInstrFloat.td` | M2.b, M5.a |
| `MSILInstrFormats.td` | M2.b |
| `MSILInstrFrame.td` | M2.b, M7.b |
| `MSILInstrInfo.cpp` | M2.f, M3.c, M6.a |
| `MSILInstrInfo.h` | M2.f, M3.c, M6.a |
| `MSILInstrInfo.td` | M2.b, M3.c, M8.a |
| `MSILInstrInteger.td` | M2.b, M4.b |
| `MSILInstrMemory.td` | M2.b, M7.a |
| `MSILMCInstLower.cpp` | M2.d, M3.d, M4.a, M6.b, M8.c, M9.a |
| `MSILMCInstLower.h` | M2.d, M3.d, M4.a, M6.b, M8.c, M9.a |
| `MSILMachineFunctionInfo.cpp` | M2.f, M3.c |
| `MSILMachineFunctionInfo.h` | M2.f, M3.c |
| `MSILNullifyDebugValueLists.cpp` | M2.a, M3.b |
| `MSILOptimizeLiveIntervals.cpp` | M2.a, M3.a |
| `MSILOptimizeReturned.cpp` | M2.a, M9.b |
| `MSILPrepareIR.cpp` | M2.a |
| `MSILRegColoring.cpp` | M2.a, M3.a |
| `MSILRegNumbering.cpp` | M2.a, M3.a |
| `MSILRegStackify.cpp` | M2.a, M3.a |
| `MSILRegisterInfo.cpp` | M2.f, M7.b |
| `MSILRegisterInfo.h` | M2.f, M7.b |
| `MSILRegisterInfo.td` | M2.b |
| `MSILReplacePhysRegs.cpp` | M2.a, M3.a |
| `MSILSelectionDAGInfo.cpp` | M2.f, M7.a |
| `MSILSelectionDAGInfo.h` | M2.f, M7.a |
| `MSILSetMemFlags.cpp` | M2.a, M7.a |
| `MSILStackCheck.cpp` | M2.a, M3.b |
| `MSILSubtarget.cpp` | M2.f, M4.b |
| `MSILSubtarget.h` | M2.f, M4.b |
| `MSILTargetMachine.cpp` | M2.a |
| `MSILTargetMachine.h` | M2.f |
| `MSILTargetObjectFile.cpp` | M2.a |
| `MSILTargetObjectFile.h` | M2.a |
| `MSILTargetTransformInfo.cpp` | M2.a |
| `MSILTargetTransformInfo.h` | M2.a |
| `MSILUtilities.cpp` | M2.d, M8.a |
| `MSILUtilities.h` | M2.d, M8.a |
| `TargetInfo/CMakeLists.txt` | M2.a |
| `TargetInfo/MSILTargetInfo.cpp` | M2.a |
| `TargetInfo/MSILTargetInfo.h` | M2.a |

### 5.3 Core C++ interfaces (fixed in M2)

Parallel WPs rely on these exact names. Verify override signatures against the headers at HEAD (`TargetLowering.h`, `AsmPrinter.h`, `MCStreamer.h`) and keep the names.

```cpp
// ---- MSIL.h (M2.f): legacy passes, in pipeline order -------------------------
ModulePass   *createMSILPrepareIRLegacyPass();                 // msil-prepare-ir (new, D25/D26)
ModulePass   *createMSILAddMissingPrototypesLegacyPass();      // msil-add-missing-prototypes
ModulePass   *createMSILFixFunctionBitcastsLegacyPass();       // msil-fix-function-bitcasts
FunctionPass *createMSILOptimizeReturnedLegacyPass();          // msil-optimize-returned
FunctionPass *createMSILISelDagLegacyPass(MSILTargetMachine &TM, CodeGenOptLevel OL); // msil-isel
FunctionPass *createMSILArgumentMoveLegacyPass();              // msil-argument-move
FunctionPass *createMSILSetMemFlagsLegacyPass();               // msil-set-mem-flags
FunctionPass *createMSILCleanCodeAfterTrapLegacyPass();        // msil-clean-code-after-trap
FunctionPass *createMSILNullifyDebugValueListsLegacyPass();    // msil-nullify-dbg-value-lists
FunctionPass *createMSILReplacePhysRegsLegacyPass();           // msil-replace-phys-regs
FunctionPass *createMSILOptimizeLiveIntervalsLegacyPass();     // msil-optimize-live-intervals
FunctionPass *createMSILRegStackifyLegacyPass(CodeGenOptLevel OL); // msil-reg-stackify
FunctionPass *createMSILRegColoringLegacyPass();               // msil-reg-coloring
FunctionPass *createMSILExplicitLocalsLegacyPass();            // msil-explicit-locals
FunctionPass *createMSILRegNumberingLegacyPass();              // msil-reg-numbering
FunctionPass *createMSILStackCheckLegacyPass();                // msil-stack-check
// plus initializeMSIL<Name>LegacyPass(PassRegistry &) for each, initializeMSILDAGToDAGISelLegacyPass,
// initializeMSILAsmPrinterPass.

// ---- MSILPassConfig (M2.a), passes-frame.md 4.2 + MSILPrepareIR + MSILSetMemFlags ----------
// addIRPasses:      PrepareIR, AddMissingPrototypes, FixFunctionBitcasts, OptimizeReturned (O1+),
//                   LowerInvoke, UnreachableBlockElim, IndirectBrExpand, base
// addISelPrepare:   AtomicExpand, base
// addInstSelector:  base, ISelDag, ArgumentMove, SetMemFlags, CleanCodeAfterTrap
// addPostRegAlloc:  disable MachineLateInstrsCleanup, MachineCopyPropagation, PostRAMachineSinking,
//                   PostRAScheduler, FuncletLayout, StackMapLiveness, PatchableFunction, ShrinkWrap,
//                   RemoveLoadsIntoFakeUses, MachineBlockPlacement; base
// addPreEmitPass:   base, NullifyDebugValueLists, UnreachableMachineBlockElim, ReplacePhysRegs,
//                   OptimizeLiveIntervals (O1+), RegStackify, RegColoring (O1+), ExplicitLocals, RegNumbering
// addPreEmitPass2:  StackCheck
// createTargetRegisterAllocator -> nullptr; addRegAssignAndRewriteFast/Optimized -> false; addGCPasses -> false

// ---- MSILTargetMachine.h (declared by M2.f, defined in MSILTargetMachine.cpp by M2.a) ----------
class MSILTargetMachine final : public CodeGenTargetMachineImpl {   // WebAssemblyTargetMachine.h shape
public:
  MSILTargetMachine(const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
                    const TargetOptions &Options, std::optional<Reloc::Model> RM,
                    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT);
  ~MSILTargetMachine() override;
  const MSILSubtarget *getSubtargetImpl(StringRef CPU, StringRef FS) const;  // per-function map
  const MSILSubtarget *getSubtargetImpl(const Function &F) const override;
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;
  TargetLoweringObjectFile *getObjFileLowering() const override;
  MachineFunctionInfo *createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                                                 const TargetSubtargetInfo *STI) const override;
  TargetTransformInfo getTargetTransformInfo(const Function &F) const override;
  bool usesPhysRegsForValues() const override { return false; }
  yaml::MachineFunctionInfo *createDefaultFuncInfoYAML() const override;
  yaml::MachineFunctionInfo *convertFuncInfoToYAML(const MachineFunction &MF) const override;
  bool parseMachineFunctionInfo(const yaml::MachineFunctionInfo &, PerFunctionMIParsingState &PFS,
                                SMDiagnostic &Error, SMRange &SourceRange) const override;
  // createMCStreamer override: ObjectFile -> StringError (copy the exact signature from
  // CodeGenTargetMachineImpl.h at HEAD). No new-PM hooks in Phase 1 (D23).
};

// ---- MSILSubtarget.h (M2.f) --------------------------------------------------
class MSILSubtarget final : public MSILGenSubtargetInfo {
public:
  MSILSubtarget(const Triple &TT, StringRef CPU, StringRef TuneCPU, StringRef FS,
                const TargetMachine &TM);
  const MSILInstrInfo *getInstrInfo() const override;
  const MSILRegisterInfo *getRegisterInfo() const override;
  const MSILTargetLowering *getTargetLowering() const override;
  const MSILFrameLowering *getFrameLowering() const override;
  const MSILSelectionDAGInfo *getSelectionDAGInfo() const override;
  StringRef getNativeCPU() const;  StringRef getNativeFeatures() const;  // kept for later SIMD (D7)
  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS); // generated
  void initLibcallLoweringInfo(LibcallLoweringInfo &Info) const override;   // M4 body
};
// Constructors the subtarget calls (M2.f): MSILInstrInfo(const MSILSubtarget &STI);
// MSILRegisterInfo(const Triple &TT); MSILFrameLowering(); MSILSelectionDAGInfo();
// MSILTargetLowering(const TargetMachine &TM, const MSILSubtarget &STI).
// MSILFrameLowering (M7): Align getLocallocAlign(const MachineFunction &MF) const;  // Align(8), D13

// ---- MSILTargetTransformInfo.h (M2.a) ----------------------------------------
class MSILTTIImpl final : public BasicTTIImplBase<MSILTTIImpl> {
  // final in M2: 0 vector registers, vector width 0, scalar width = pointer size,
  // shouldBuildLookupTables() -> false, shouldBuildRelLookupTables() -> false,
  // getUnrollingPreferences: no partial or runtime unroll.
};

// ---- MCTargetDesc/MSILMCTargetDesc.h (M2.b) ---------------------------------
namespace MSILII { enum : uint64_t { StackPeakExtraMask = 0x3, TrapsOnlyOnUB = 0x4 }; }
namespace MSIL {
enum OperandType { OPERAND_LOCAL = MCOI::OPERAND_FIRST_TARGET, OPERAND_ARG, OPERAND_I32IMM,
  OPERAND_I64IMM, OPERAND_F32IMM, OPERAND_F64IMM, OPERAND_METHOD, OPERAND_DATA, OPERAND_SIGNATURE,
  OPERAND_MEMFLAGS };
static const unsigned UnusedReg = -1u;
bool isArgument(unsigned Opc); bool isCopy(unsigned Opc); bool isTee(unsigned Opc);
bool isScalarConst(unsigned Opc); bool isCallDirect(unsigned Opc); bool isCallIndirect(unsigned Opc);
std::string getMSILCPU(StringRef CPU);          // defined in MSILMCTargetDesc.cpp (M2.c): all -> "generic"
std::string filterMSILFeatures(StringRef FS);   // Phase 1: ""
}

// ---- MCTargetDesc/MSILMCTypeUtilities.h (M2.c) ------------------------------
namespace MSIL {
enum class ValType : uint8_t { I32, NativeInt, I64, F32, F64 };
enum class CallConv : uint8_t { Managed, UnmanagedCdecl, UnmanagedStdcall };
struct Signature { SmallVector<ValType, 1> Results; SmallVector<ValType, 4> Params;
                   CallConv CC = CallConv::Managed;
                   bool operator==(const Signature &) const; bool operator<(const Signature &) const; };
ValType toValType(MVT VT, unsigned PointerSizeInBits);   // i32, i64->NativeInt (64-bit), f32, f64
StringRef getValTypeName(ValType T);                     // "int32", "native int", "int64", "float32", "float64"
void printValType(raw_ostream &OS, ValType T);
void printResult(raw_ostream &OS, const Signature &S);    // "void" or the type
void printParamList(raw_ostream &OS, const Signature &S); // "(int32, native int)"
void printQuotedName(raw_ostream &OS, StringRef Name);    // D11 escapes
}

// ---- MCTargetDesc/MSILMCExpr.h (M2.c) --------------------------------------
class MSILMemberRefExpr final : public MCTargetExpr {
public:
  enum Kind : uint8_t { Method, Field, StandAloneSig, BCLMember };
  static const MSILMemberRefExpr *createMethod(StringRef ILName, const MSIL::Signature *S, MCContext &Ctx);
  static const MSILMemberRefExpr *createField(StringRef ILName, int64_t Offset, MCContext &Ctx);
  static const MSILMemberRefExpr *createSig(const MSIL::Signature *S, MCContext &Ctx);
  static const MSILMemberRefExpr *createBCL(StringRef Text, MCContext &Ctx);
  Kind getKind() const; StringRef getName() const; const MSIL::Signature *getSignature() const;
  int64_t getOffset() const;
};

// ---- MCTargetDesc/MSILTargetStreamer.h (M2.c) -------------------------------
struct MSILMethodDecl { StringRef Name; const MSIL::Signature *Sig; bool Public = true; };
class MSILTargetStreamer : public MCTargetStreamer {
public:
  explicit MSILTargetStreamer(MCStreamer &S);
  virtual void emitFileHeader(bool BOM, StringRef SourceFile, StringRef TripleStr) = 0;
  virtual void emitAssemblyRefs() = 0;                         // .assembly extern mscorlib {...}
  virtual void emitAssembly(StringRef Name) = 0;               // .assembly 'Name' { .ver 0:0:0:0 }
  virtual void emitMethodBegin(const MSILMethodDecl &M) = 0;   // .method ... cil managed / {
  virtual void emitEntryPoint() = 0;                           // .entrypoint
  virtual void emitMaxStack(unsigned N) = 0;                   // .maxstack N
  virtual void emitLocals(ArrayRef<MSIL::ValType> Locals) = 0; // .locals (...)  never init; nothing if empty
  virtual void emitMethodEnd(StringRef Name) = 0;              // } // end of method 'Name'
  virtual void emitPInvoke(const MSILMethodDecl &M, StringRef Lib, StringRef Entry) = 0;
  virtual void emitDataImage(StringRef TypeName, StringRef FieldName, StringRef Label,
                             ArrayRef<uint8_t> Bytes) = 0;
  virtual void emitAddressField(StringRef Name, bool Public) = 0;
  virtual void emitILLine(const Twine &Line) = 0;              // synthesized bodies (.cctor, Main)
};
// MSILTargetAsmStreamer (writes through getStreamer().emitRawText) and MSILTargetNullStreamer.

// ---- MSILAsmPrinter.h (M2.d) public API --------------------------------------
class MSILAsmPrinter final : public AsmPrinter {
public:
  MSILTargetStreamer &getTargetStreamer();
  const MSIL::Signature &getSignature(const Function &F);          // computeSignatureVTs + toValType
  const MSIL::Signature &internSignature(const MSIL::Signature &S); // calli
  void noteExternFunction(const Function &F);                       // -> P/Invoke (M8)
  const MSIL::Signature &noteExternalSymbol(StringRef Name, const MSIL::Signature &S);
};
// MSILMCInstLower(MCContext &Ctx, MSILAsmPrinter &Printer); void lower(const MachineInstr *, MCInst &) const;

// ---- MSILUtilities.h (M2.d) -------------------------------------------------
namespace MSIL {
StringRef getILName(const GlobalValue &GV, const TargetMachine &TM);  // IR name, never the Mangler
bool isChild(const MachineInstr &MI, const MSILFunctionInfo &MFI);
bool mayThrow(const MachineInstr &MI);                                 // Phase 1: MI.isCall()
const MachineOperand &getCalleeOp(const MachineInstr &MI);
unsigned getCopyOpcodeForRegClass(const TargetRegisterClass *RC);
// added by M8.a (D14 import table):
std::optional<StringRef> getPInvokeModule(const Triple &TT, StringRef Name, const Function *F = nullptr);
StringRef getPInvokeEntry(StringRef Name, const Function *F);
}

// ---- MSILMachineFunctionInfo.h (M2.f): copy of WebAssemblyFunctionInfo -------
// addParam/getParams, addResult/getResults, clearParamsAndResults, setNumLocals/setLocal/addLocal/
// getLocals (MVTs), stackifyVReg/unstackifyVReg/isVRegStackified, initWARegs/setWAReg/getWAReg,
// setFrameBaseVreg/getFrameBaseVreg/clearFrameBaseVreg/isFrameBaseVirtual, setFrameBaseLocal/
// getFrameBaseLocal, getMaxStack/setMaxStack; yaml::MSILFunctionInfo {params, results, maxStack}.
void computeLegalValueVTs(const MSILTargetLowering &TLI, LLVMContext &Ctx, const DataLayout &DL,
                          Type *Ty, SmallVectorImpl<MVT> &ValueVTs);
void computeLegalValueVTs(const Function &F, const TargetMachine &TM, Type *Ty,
                          SmallVectorImpl<MVT> &ValueVTs);
void computeSignatureVTs(const FunctionType *Ty, const Function *TargetFunc,
                         const Function &ContextFunc, const TargetMachine &TM,
                         SmallVectorImpl<MVT> &Params, SmallVectorImpl<MVT> &Results);

// ---- MSILISelLowering.h (M2.f): complete Phase-1 declaration -----------------
class MSILTargetLowering final : public TargetLowering {
public:
  MSILTargetLowering(const TargetMachine &TM, const MSILSubtarget &STI);
  // Query hooks: final bodies in M2, never stubs (D33).
  MVT getScalarShiftAmountTy(const DataLayout &DL, EVT) const override;          // i32
  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Ctx, EVT VT) const override; // i32
  bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;       // !Function
  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM, Type *Ty,
                             unsigned AS, Instruction *I = nullptr) const override;
      // [reg] only: !AM.BaseGV && AM.BaseOffs == 0 && AM.ScalableOffset == 0 &&
      //             (AM.Scale == 0 || (AM.Scale == 1 && !AM.HasBaseReg))
  bool allowsMisalignedMemoryAccesses(EVT, unsigned AS, Align A, MachineMemOperand::Flags F,
                                      unsigned *Fast) const override;            // true
  bool isIntDivCheap(EVT VT, AttributeList Attr) const override;                 // true
  MVT getJumpTableRegTy(const DataLayout &DL) const override;                    // i32 (D35)
  bool convertSelectOfConstantsToMath(EVT VT) const override;                    // true (D34)
  // Lowering hooks: stubs until the milestone named in the comment.
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  MachineBasicBlock *EmitInstrWithCustomInserter(MachineInstr &MI,
                                                 MachineBasicBlock *BB) const override;
  // MSILISelLoweringCall.cpp
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CC, bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
                               SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const override;
  SDValue LowerCall(CallLoweringInfo &CLI, SmallVectorImpl<SDValue> &InVals) const override;
  bool CanLowerReturn(CallingConv::ID CC, MachineFunction &MF, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Ctx,
                      const Type *RetTy) const override;                         // Outs.size() <= 1
  SDValue LowerReturn(SDValue Chain, CallingConv::ID CC, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;
  MachineBasicBlock *emitCallResults(MachineInstr &MI, MachineBasicBlock *BB) const; // M4
private:
  const MSILSubtarget *Subtarget;
  SDValue LowerFrameIndex(SDValue Op, SelectionDAG &DAG) const;      // M7
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;   // M8
  SDValue LowerExternalSymbol(SDValue Op, SelectionDAG &DAG) const;  // M8
  SDValue LowerBR_JT(SDValue Op, SelectionDAG &DAG) const;           // M6
  SDValue LowerShiftParts(SDValue Op, SelectionDAG &DAG) const;      // M4
  SDValue LowerUINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;      // M5 (D17 sequence)
  SDValue LowerUnsupported(SDValue Op, SelectionDAG &DAG, StringRef What) const; // M7
  MachineBasicBlock *emitSelect(MachineInstr &MI, MachineBasicBlock *BB) const;  // M4 (D34)
};
// MSILSelectionDAGInfo.h: GET_SDNODE_ENUM from MSILGenSDNodeInfo.inc, then
// namespace llvm::MSILISD { enum NodeType : unsigned { CALL = GENERATED_OPCODE_END }; }
```

A later milestone's ISel-core WP may add private helpers to `MSILISelLowering.h` (it owns the file in that milestone), but must not rename or remove anything above.

### 5.4 TableGen conventions and the opcode catalogue

- Single form (D1): `class MSILInst<bits<16> cilop, string asmstr>`; `NI<oops, iops, pattern, asmstr, cilop>`
  sets `Defs = [ARGUMENTS]`; `multiclass I` emits one `def ""`. Asm strings name only immediates and symbols.
- TSFlags: bits 1-0 `StackPeakExtra` (extra transient depth of multi-opcode records), bit 2 `TrapsOnlyOnUB`
  (div/rem: RegStackify may still move them). memflags immediate: bits 0-2 = unaligned N (0, 1, 2 or 4), bit 3 = volatile.
- Each `.td` area defines its own SDNodes (`-gen-sd-node-info` builds the `MSILISD` enum); `MSILISD::CALL` is C++-only.
- Register classes: `I32`, `I64` (FP, I64_0), `F32`, `F64`; fake regs `FP`, `VALUE_STACK`, `ARGUMENTS`;
  `defm : RemapAllTargetPseudoPointerOperands<I64>;`. In MIR the classes are `i32`, `i64`, `f32`, `f64` and the fake
  registers `$fp`, `$value_stack`, `$arguments`.
- Naming: `<MNEMONIC>_<class>`; conversions `CONV_<cil suffix>_<source class>`; `<RC>` below is I32, I64, F32, F64.

Operand definitions (M2.b; the PrintMethod names are what the generated `MSILGenAsmWriter.inc` calls on M2.c's
`MSILInstPrinter`, so they are a contract):

| Operand def | OperandType (`MSIL::`) | PrintMethod |
|---|---|---|
| `local_op` | `OPERAND_LOCAL` | `printLocalOperand` |
| `arg_op` | `OPERAND_ARG` | `printArgOperand` |
| `i32imm_op` | `OPERAND_I32IMM` | `printOperand` |
| `i64imm_op` | `OPERAND_I64IMM` | `printOperand` |
| `f32imm_op` | `OPERAND_F32IMM` | `printF32ImmOperand` |
| `f64imm_op` | `OPERAND_F64IMM` | `printF64ImmOperand` |
| `function_op` | `OPERAND_METHOD` | `printOperand` (a `MSILMemberRefExpr` after MCInstLower) |
| `data_op` | `OPERAND_DATA` | `printOperand` |
| `memflags_op` | `OPERAND_MEMFLAGS` | `printMemFlagsOperand` (prints the prefix lines) |
| `bb_op` (`Operand<OtherVT>`) | (none) | `printOperand` |

| Area (file, first milestone) | Records (CIL text; `+n` = StackPeakExtra) |
|---|---|
| Core (`MSILInstrInfo.td`, M2) | `ARGUMENT_<RC>` (none), `COPY_<RC>`, `TEE_<RC>`, `LOCAL_GET_<RC>` (ldloc), `LOCAL_SET_<RC>` (stloc), `LOCAL_TEE_<RC>` (dup; stloc, +1), `ARG_GET_<RC>` (ldarg), `ARG_SET_<RC>` (starg), `ARG_TEE_<RC>` (dup; starg, +1), `DROP_<RC>` (pop), `CONST_I32`, `CONST_I64` (ldc + conv.i), `CONST_F32`, `CONST_F64`, `GLOBAL_ADDR` (ldsfld [+ ldc.i4; add], +1), `FUNC_ADDR` (ldftn) |
| Control (`MSILInstrControl.td`, M2/M4/M6) | `RETURN` (ret), `TRAP` (ldnull; throw, +1), `BR`, `BR_IF` (brtrue), `BR_UNLESS` (brfalse), `BR_TABLE_I32` (switch (...); br; no I64 form, D35); M4: `SELECT_<RC>` (custom inserter, D34); M6: `B{EQ,NE_UN,LT,LT_UN,LE,LE_UN,GT,GT_UN,GE,GE_UN}_<RC>` |
| Calls (`MSILInstrCall.td`, M2/M4) | `ADJCALLSTACKDOWN`, `ADJCALLSTACKUP`, `CALL_PARAMS`, `CALL_RESULTS`, `CALL` (call <member-ref>), `CALL_INDIRECT` (calli <sig>) |
| Frame (`MSILInstrFrame.td`, M2) | `LOCALLOC` (localloc; never stackified, depth must be 1) |
| Integer (`MSILInstrInteger.td`, M4) | `ADD/SUB/MUL/AND/OR/XOR/SHL/SHR_S/SHR_U_{I32,I64}`, `DIV_S/DIV_U/REM_S/REM_U_{I32,I64}` (TrapsOnlyOnUB), `CEQ/CGT/CGT_UN/CLT/CLT_UN_{I32,I64}` (result I32) |
| Conversions (`MSILInstrConv.td`, M4/M5) | `CONV_I_I32` (conv.i), `CONV_U_I32` (conv.u), `CONV_I4_I64` (conv.i4), `CONV_I1_I32`, `CONV_I2_I32`, `CONV_U1_I32`, `CONV_U2_I32`; M5: `CONV_R4_{I32,I64,F64}`, `CONV_R8_{I32,I64,F32}`, `CONV_R_UN_I32`, `CONV_I4/U4/I/U_{F32,F64}` |
| FP (`MSILInstrFloat.td`, M5) | `ADD/SUB/MUL/DIV/REM_{F32,F64}`, `NEG_{F32,F64}`, `CEQ/CGT/CGT_UN/CLT/CLT_UN_{F32,F64}` |
| Memory (`MSILInstrMemory.td`, M7) | `LDIND_{I1,U1,I2,U2,I4,I,R4,R8}`, `STIND_{I1,I2,I4,I,R4,R8}` (memflags first), `CPBLK`, `INITBLK`, `MEMMOVE` (conv.i8; dup; call Buffer::MemoryCopy, +1), `MEMORY_BARRIER` (call Interlocked::MemoryBarrier); singlethread fences select the generic `MEMBARRIER` (D36) |

Exact operand lists of the structural records (mirroring WebAssembly; MIR tests are written against this table, and no
later milestone may reorder it). Every record defined with `NI`/`I` also carries `implicit-def dead $arguments` in MIR.

| Record | `(outs)` | `(ins)` | Flags and notes |
|---|---|---|---|
| `ARGUMENT_<RC>` | `RC:$res` | `i32imm:$argno` | `Uses = [ARGUMENTS]`, `Defs = []`, hasSideEffects, isCodeGenOnly; MIR `%0:i32 = ARGUMENT_I32 0, implicit $arguments` |
| `COPY_<RC>` | `RC:$res` | `RC:$src` | isAsCheapAsAMove, isCodeGenOnly |
| `TEE_<RC>` | `RC:$res, RC:$also` | `RC:$src` | isAsCheapAsAMove, isCodeGenOnly |
| `LOCAL_GET_<RC>` | `RC:$res` | `local_op:$local` | mayLoad |
| `LOCAL_SET_<RC>` | - | `local_op:$local, RC:$src` | mayStore |
| `LOCAL_TEE_<RC>` | `RC:$res` | `local_op:$local, RC:$src` | mayStore, StackPeakExtra 1 |
| `ARG_GET_<RC>` | `RC:$res` | `arg_op:$arg` | mayLoad |
| `ARG_SET_<RC>` | - | `arg_op:$arg, RC:$src` | mayStore |
| `ARG_TEE_<RC>` | `RC:$res` | `arg_op:$arg, RC:$src` | mayStore, StackPeakExtra 1 |
| `DROP_<RC>` | - | `RC:$src` | |
| `CONST_I32` / `CONST_I64` | `I32:$res` / `I64:$res` | `i32imm_op:$imm` / `i64imm_op:$imm` | isMoveImm, isAsCheapAsAMove, isReMaterializable; patterns `imm` |
| `CONST_F32` / `CONST_F64` | `F32:$res` / `F64:$res` | `f32imm_op:$imm` / `f64imm_op:$imm` | as above; patterns `fpimm` |
| `GLOBAL_ADDR` | `I64:$res` | `data_op:$addr` | isReMaterializable, StackPeakExtra 1 (pattern in M8) |
| `FUNC_ADDR` | `I64:$res` | `function_op:$func` | isReMaterializable (pattern in M8) |
| `RETURN` | - | `variable_ops` | isReturn, isTerminator, isBarrier; MIR `RETURN %1, implicit-def dead $arguments` |
| `TRAP` | - | - | isTrap, isTerminator, isBarrier, hasCtrlDep; pattern `(trap)`; StackPeakExtra 1 |
| `BR` | - | `bb_op:$dst` | isBranch, isTerminator, isBarrier |
| `BR_IF` / `BR_UNLESS` | - | `bb_op:$dst, I32:$cond` | isBranch, isTerminator |
| `BR_TABLE_I32` | - | `I32:$index, variable_ops` | variable operands are MBBs, the last one is the default; isBranch, isTerminator, isBarrier |
| `SELECT_<RC>` (M4) | `RC:$dst` | `I32:$cond, RC:$t, RC:$f` | usesCustomInserter; pattern `(select I32:$cond, RC:$t, RC:$f)` |
| `ADJCALLSTACKDOWN` / `ADJCALLSTACKUP` | - | `i32imm:$amt, i32imm:$amt2` | isCodeGenOnly; callseq patterns |
| `CALL_PARAMS` | - | `function_op:$callee, variable_ops` | isCall; ISel only, recombined by the custom inserter |
| `CALL_RESULTS` | - | `variable_ops` | variadicOpsAreDefs, usesCustomInserter, isPseudo |
| `CALL` | - | `function_op:$callee, variable_ops` | variadicOpsAreDefs, isCall: defs first, then the callee (operand index = number of defs), then the argument uses |
| `CALL_INDIRECT` | - | `variable_ops` | variadicOpsAreDefs, isCall: defs first, then the argument uses, the callee address last (calli order); the signature is built by MCInstLower (M9) |
| `LOCALLOC` | `I64:$p` | `I32:$size` | hasSideEffects; never stackified; stack depth must be 1 when it executes |

Sample MIR (pre-ExplicitLocals, as a test WP writes it; register classes and operand order from the tables above):

```text
---
name:            f
tracksRegLiveness: true
machineFunctionInfo:
  params:  [ i32, i64 ]
  results: [ i32 ]
body: |
  bb.0:
    liveins: $arguments
    %0:i32 = ARGUMENT_I32 0, implicit $arguments
    %1:i64 = ARGUMENT_I64 1, implicit $arguments
    dead %2:i32 = CONST_I32 7, implicit-def dead $arguments
    RETURN %0, implicit-def dead $arguments
...
```

After `-run-pass=msil-explicit-locals` the use of `%0` reads `ARG_GET_I32 0`, the dead `CONST_I32` is followed by
`DROP_I32`, and non-argument values that are not stackified go through `LOCAL_SET_<RC>` / `LOCAL_GET_<RC>` with local
indices counted from 0 (passes-frame 7: unified numbering, params first, then CIL locals).

Stack effect contract (isel.md 9, passes-frame 8.2): after ExplicitLocals every explicit register use pops and every
explicit register def pushes (at most one), in operand order; `StackPeakExtra` covers multi-opcode records. Any record
that breaks this must be fixed, not special-cased in MSILStackCheck.

### 5.5 Test layout and harness conventions

| Tier | Location | Gate |
|---|---|---|
| Triple / registry unit tests | `llvm/unittests/TargetParser/TripleTest.cpp`, `llvm/unittests/MC/TargetRegistry.cpp` | always built |
| Target unit tests | `llvm/unittests/Target/MSIL/` (`MSILTests`) | MSIL built |
| llc / MIR FileCheck | `llvm/test/CodeGen/MSIL/` (`lit.local.cfg`: MSIL gate + optional `ilasm` feature + `%ilasm-verify`) | MSIL built |
| clang | `clang/test/CodeGen/MSIL/`, `clang/test/Preprocessor/init-msil.c`, `clang/test/Driver/msil-toolchain.c` | `REQUIRES: msil-registered-target` only when the backend runs |
| Execution | `msil/test/M1/<NN-area>/` (external project `msil/`) | `msil-exec` feature, else UNSUPPORTED |
| Harness self-tests | `msil/test/harness/` | ilasm + dotnet |

Execution directories: `00-smoke` (M3), `01-int` (M4), `02-fp` (M5), `03-control` (M6), `04-frames`, `05-memory` (M7),
`06-output`, `07-globals` (M8), `08-fnptr`, `09-structs`, `10-calls` (M9), `11-programs` (M10).

Execution-test rules:
- One RUN line, `%{msil_c}` (C) or `%{msil_ll}` (IR through llc), or a per-level form (`%{msil_c_O2}`) with a
  first-line comment saying why (D32). The first comment line says what the test covers.
- `.expected` is stdout (LF) plus `[exit N]` and is generated by `msil/utils/update-expected.py` from a native build.
- Tests are UB-free C11 that behaves identically at -O0 and -O2 natively. `main` always returns explicitly:
  `%{msil_cflags}` has `-Werror=return-type`, because `-ffreestanding` turns off main's implicit `return 0`.
- The exit status is 0..255 (D37): Linux truncates exit statuses to 8 bits, so a larger value that passes on Windows
  would pass on the WSL leg by accident. Bitmasks use at most 8 bits; programs return `checksum & 0xff`. msil-run.py
  and update-expected.py reject anything else.
- No `long`, `long double`, varargs, printing of addresses or floating-point printing; no libc beyond `puts` (through
  `msil/test/Inputs/msil_test.h`).

Capability rules. A test written before the capability it needs exists fails for the wrong reason, so tests (and
their RUN lines) respect these limits by milestone:

| Until | Not available | Write tests like this |
|---|---|---|
| M6 (control flow) | branches, loops, `switch`, `MSIL_EXPECT` (it is `if (!(c)) return ...`) | straight-line code and selects (M4); C tests accumulate with `MSIL_CHECK(acc, bit, cond)` and `return acc;` |
| M7 (frames, memory) | clang -O0 (every parameter and local becomes an alloca; windows-msvc gives even `main` a return-value slot), local arrays, address-taken locals, `fabs`/`copysign`/FP bitcasts (they go through a stack slot) | C at `%{msil_c_O2}` only (D32); produce -0.0 as `0.0 * -1.0`, NaN as `0.0 / 0.0`, test NaN with `fcmp uno` |
| M8 (globals, P/Invoke) | globals, string literals, brace- or string-initialized local arrays and structs (clang emits private `@__const` globals plus `llvm.memcpy`, even at -O0), output | results through the exit code only; initialize aggregates element by element |
| M9 (function pointers, aggregates) | indirect calls, `byval`/`sret` aggregates, K&R calls | direct calls with scalar arguments |

Each level runs compile -> `ilasm -dll` -> `msil-verify` -> `jitall` -> `dotnet exec` (private directory, timeout,
memory cap) -> transcript comparison. Every gate passes `msil_require_verify=1`, so a missing verifier or jitall is a
configuration error, not a silently skipped step (D38). The native-oracle (`msil_native_oracle`), .NET Framework
(`msil_run_netfx=1`), WSL (`msil_runner=wsl`, linux triple, .NET 10) and optimizing-JIT
(`msil_dotnet_env=DOTNET_TieredCompilation=0`) legs are parameters.

## 6. Milestones

### M0. Infrastructure: tracked build scripts, plan checks, execution harness, jitall, CI skeleton

**Goal.** Put the build helpers, the plan-check tool, the lit execution harness (top-level msil/ project wired in as an LLVM external project), the verification tools and the fork CI workflow into the tree, and prove them with harness self-tests that need only ilasm and dotnet. No LLVM or clang source file changes. The milestone commit also adds llvm/docs/MSIL/ImplementationPlan.md and llvm/docs/MSIL/recon/ (every recon report, including cil-runtime.md and build-env.md, and the recon/artifacts/ directory, all written by the planning run before M0 starts).

**Depends on:** nothing.

**Header changes:**
- none

**Work packages**

#### M0.a Tracked build scripts, gate helpers, plan-check tool and CMake cache (infra, ~1000 lines (mostly adapted))

- Owned files: `msil/utils/build/msil-env.ps1`, `msil/utils/build/msil-lock.ps1`, `msil/utils/build/msil-configure.ps1`, `msil/utils/build/msil-build.ps1`, `msil/utils/build/msil-lit.ps1`, `msil/utils/build/msil-run.ps1`, `msil/utils/build/msil-syntax.ps1`, `msil/utils/build/msil-tblgen.ps1`, `msil/utils/build/README.md`, `msil/cmake/caches/MSIL.cmake`, `msil/CMakeLists.txt`, `msil/.gitattributes`, `msil/README.md`, `msil/utils/build/msil-gtest.ps1`, `msil/utils/build/msil-expect-error.ps1`, `msil/utils/plan-check.py`
- Reads: D:/llvm-msil/build/msil-scripts/*.ps1 (tested originals); llvm/docs/MSIL/recon/tests-ci.md Appendix A.1, A.3; llvm/docs/MSIL/recon/build-env.md and llvm/docs/MSIL/recon/artifacts/build-env/ (tested script copies and lock-test logs); this plan sections 1 (ownership, fix-loop amendments, scope allow-list) and 12 (amendment row format)
- Provides: `msil-configure.ps1 [-Experimental MSIL] [-NoMsilProject] [-Fresh] [-BuildDir]`; `msil-build.ps1 <targets> [-Jobs] [-KeepGoing]`; `msil-lit.ps1 [-LitArgs] <paths>`; `msil-run.ps1 <tool> <args>`; `msil-syntax.ps1 [-FlagsFrom <tu>] [-ExtraInclude <dirs>] [-NoPch] <files>`; `msil-tblgen.ps1 -Out <dir> [-Gen <names>]`; `$MsilRepo/$MsilBuild/$MsilPython/$MsilIlasm globals from msil-env.ps1`; `msil-gtest.ps1 -Exe <gtest.exe> -Filter <pattern> [-NoSkip] [-MinTests N]`; `msil-expect-error.ps1 -Pattern <regex> [-ExitCode 1] -- <exe> <args...>`; `python msil/utils/plan-check.py ownership | reads M<n> | scope M<n> | --self-test`

Copy the seven tested scripts from D:/llvm-msil/build/msil-scripts/ into msil/utils/build/ and adapt them: (1) msil-env.ps1 derives $MsilRepo from $PSScriptRoot (three levels up) instead of a hard-coded path, keeps $MsilPython with forward slashes, honours $env:MSIL_PYTHON and $env:MSIL_BUILD_DIR, and keeps the cached VS dev-shell logic unchanged. (2) msil-configure.ps1 adds -DLLVM_EXTERNAL_PROJECTS=msil and -DLLVM_EXTERNAL_MSIL_SOURCE_DIR=<repo>/msil (forward slashes) unless -NoMsilProject is given; every other option and default stays exactly as today (X86;WebAssembly, clang;lld, clang-cl + lld-link, Release + assertions, LLVM_OPTIMIZED_TABLEGEN=OFF, LLVM_BUILD_TESTS=OFF, compile_commands on). (3) msil-syntax.ps1 gains -FlagsFrom <tu> (use the compile command of another TU, e.g. llvm/lib/Target/WebAssembly/WebAssemblyInstrInfo.cpp, for files not yet in compile_commands.json) and -ExtraInclude <dir[]> (adds /I flags; used for llvm/lib/Target/MSIL, build/lib/Target/MSIL and a scratch tblgen output dir). (4) New msil-tblgen.ps1 -Out <dir> [-Gen ...] runs build/bin/llvm-tblgen.exe on llvm/lib/Target/MSIL/MSIL.td for the six backends (-gen-asm-writer, -gen-dag-isel, -gen-instr-info, -gen-register-info, -gen-sd-node-info, -gen-subtarget) with -I llvm/include -I llvm/lib/Target/MSIL, writing MSILGenAsmWriter.inc, MSILGenDAGISel.inc, MSILGenInstrInfo.inc, MSILGenRegisterInfo.inc, MSILGenSDNodeInfo.inc and MSILGenSubtargetInfo.inc into <dir> only (never into the build tree), exit 1 on any tblgen error. It needs no lock. (5) msil/CMakeLists.txt and msil/cmake/caches/MSIL.cmake per tests-ci.md Appendix A.1/A.3 (the cache file is what CI uses: Release, assertions, clang, X86, experimental MSIL, LLVM_EXTERNAL_PROJECTS=msil via CMAKE_CURRENT_LIST_DIR). (6) msil/.gitattributes: '* text=auto eol=lf' (this checkout has core.autocrlf=true; expected files must stay LF). (7) README.md files document each script with one example and the lock rules (exclusive for cmake/ninja, shared for lit/tools; background builds die with the agent's turn). (8) msil-gtest.ps1 -Exe <gtest binary> -Filter <pattern> [-NoSkip] [-MinTests N]: runs the binary under the shared lock with --gtest_filter, captures its output, and fails on a non-zero exit, on any '[  SKIPPED ]' line when -NoSkip is given, or when the '[  PASSED  ] N tests' count is below -MinTests (GTEST_SKIP exits 0, so a plain run cannot tell a skipped test from a passing one). (9) msil-expect-error.ps1 -Pattern <regex> [-ExitCode 1] -- <exe> <args...>: passes only if the process exits with exactly -ExitCode, its combined stdout+stderr matches -Pattern, and the output contains none of 'PLEASE submit a bug report', 'LLVM ERROR', 'Stack dump', 'Exception Code:' (a clean diagnostic, not a crash: on Windows report_fatal_error ends in abort() with exit code 3, and plain 'not' only treats that as a crash with --crash, llvm/utils/not/not.cpp:69-82). (10) msil/utils/plan-check.py (Python 3 standard library only) parses llvm/docs/MSIL/ImplementationPlan.md: '#### M<n>.<x> ' WP headings under '### M<n>. ' milestone headings, the '- Owned files:' line of each WP (backquoted paths; a trailing '/' means the whole directory), the '- Reads:' line ('; '-separated entries), and section 12 amendment rows of the exact form '| <date> | M<n> | fix-loop ownership: `<path>` -> <agent> | <reason> |'. Subcommands: 'ownership' (inside every milestone the owned files are pairwise disjoint and no directory entry contains another WP's file; prints conflicts, exit 1); 'reads M<n>' (the leading token of every Reads entry that looks like a path - contains '/' and starts with llvm/, clang/, lld/, msil/, .github/ or D:/ - exists after brace expansion and globbing, with trailing punctuation and a ':<line>' or ':<line>-<line>' suffix stripped (for example 'clang/lib/CodeGen/Targets/WebAssembly.cpp:122-123'); other entries such as 'this plan section 5' are skipped; prints missing paths, exit 1); 'scope M<n>' (every path of 'git diff --cached --name-only HEAD' is owned by a WP of M<n>, or is the path of an M<n> fix-loop amendment row, or is on the allow-list llvm/docs/MSIL/ImplementationPlan.md and llvm/docs/MSIL/recon/**; prints offenders, exit 1); '--self-test' runs the parser on an embedded two-milestone sample. Verify by running every script once with a harmless argument (configure is run by the build agent at gate time, not by this WP) and plan-check.py ownership / reads M0 / --self-test against the current plan.

#### M0.b lit execution harness (infra, ~1200 lines)

- Owned files: `msil/test/CMakeLists.txt`, `msil/test/lit.cfg.py`, `msil/test/lit.site.cfg.py.in`, `msil/test/README.md`, `msil/test/Inputs/msil_test.h`, `msil/test/M1/lit.local.cfg`, `msil/utils/msil-run.py`, `msil/utils/msil-verify.py`, `msil/utils/ilverify-allowed.txt`, `msil/utils/update-expected.py`
- Depends on: M0.c
- Reads: llvm/docs/MSIL/recon/tests-ci.md sections 1.4, 6, 7, Appendix A.2, A.4-A.10, A.14; llvm/docs/MSIL/recon/artifacts/tests-ci/proto/ (the verified prototype: msil/ tree, fake tools, CI file); llvm/docs/MSIL/recon/asmprinter.md 0.9 (assembly-name collisions); llvm/docs/MSIL/recon/cil-runtime.md (e15 JIT hang hazard, ILVerify summary); this plan section 5.5 and decisions D21, D32, D37, D38
- Provides: lit params: msil_triple, msil_opt_levels (default O0,O2), msil_dotnet_runtime (default 8), msil_native_oracle (0|1|required), msil_run_netfx (0|1), msil_runner (local|wsl), msil_ilasm, msil_dotnet, msil_ilverify, msil_peverify, msil_jitall (dir, default <build>/tools/msil/jitall), msil_require_verify (0|1, default 0; every gate passes 1), msil_dotnet_env ('VAR=VALUE[;VAR=VALUE...]'); lit features: msil-exec, msil-ilasm, msil-dotnet, dotnet-runtime-<N>, msil-ilverify, msil-peverify, msil-netfx, msil-run-netfx, msil-wsl, msil-native-oracle, msil-jitall, msil-triple-windows, msil-triple-linux (from the effective triple; msil_runner=wsl defaults the triple to x86_64-unknown-linux-gnu-msil); substitutions: %msil_triple %clang_msil %llc_msil %clang_native %ilasm %dotnet %msil_exec %msil_verify %msil_jitall %msil_inputs %{msil_cflags} %{msil_c} %{msil_ll} %{msil_c_O0..Oz} %{msil_ll_O0..O3}; transcript format: stdout (LF) + '[exit N]' with 0 <= N <= 255 | '[unhandled exception: T]' | '[timeout]' | '[memory limit]'; any other exit status is reported as an error (D37); `ninja targets: check-msil-exec, check-msil (umbrella), check-msil-m1-<dir>`; msil_test.h: MSIL_CHECK(acc, bit, cond) (branch-free, bits 0-7), MSIL_EXPECT(cond) (needs branches, M6+), put_u64/put_i64/put_hex (puts only)
- Consumes: python msil/utils/jitall/build-jitall.py [--out <dir>] (default <build>/tools/msil/jitall) (M0.c); python msil/utils/msil-jitall.py [--jitall <dir>] [--env VAR=VALUE]... <dll> (M0.c)

Implement tests-ci.md section 6 and Appendix A.2, A.4-A.10 and A.14 (prototype verified on this host), with these required changes: (1) msil-run.py copies the .dll (and .exe for netfx) plus its runtimeconfig.json into a fresh private temporary directory and runs it there, because two DLLs with the same assembly name in one directory silently run the wrong code (asmprinter.md 0.9). (2) Every child process (dotnet, ilasm, ilverify, PEVerify, jitall, native exe) runs under a wall-clock timeout (--timeout, default 60 s) and a memory cap (--mem-limit-mb, default 4096): on Windows a Job Object created with ctypes (CreateJobObjectW, SetInformationJobObject with JOBOBJECT_EXTENDED_LIMIT_INFORMATION / JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, AssignProcessToJobObject), on Linux resource.RLIMIT_AS in preexec_fn; report '[timeout]' / '[memory limit]'. The .NET 10 JIT was seen hanging and allocating 40+ GB on an inconsistent stack depth (cil-runtime recon). (3) --runner wsl: translate Windows paths to /mnt/<drive>/..., run 'wsl.exe -d Ubuntu -- dotnet exec --runtimeconfig <net10 cfg> <dll>' (WSL has only .NET 10.0.12), feature msil-wsl when 'wsl.exe -d Ubuntu -- dotnet --list-runtimes' succeeds; msil_runner=wsl makes the default msil_triple x86_64-unknown-linux-gnu-msil, and the msil-triple-windows / msil-triple-linux features always follow the effective triple (a Windows-only P/Invoke test must REQUIRE msil-triple-windows, not system-windows). (4) Tool discovery as in A.5, plus: modern ilasm is also searched in the NuGet global-packages folder reported by 'dotnet nuget locals global-packages --list' (on this host D:/packages/NuGet, which holds runtime.win-x64/linux-x64.microsoft.netcore.ilasm 10.0.12); the default ilasm on Windows stays Framework ilasm (C:/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe); %ilasm expands to '"<ilasm>" -nologo -quiet -pe64 -x64' for x86_64 triples (callers add -dll/-exe and -output=). Never read %APPDATA%/NuGet/NuGet.Config (it contains credentials). (5) Substitutions: besides %{msil_c} and %{msil_ll} (all levels in msil_opt_levels) define per-level composites %{msil_c_O0}, %{msil_c_O1}, %{msil_c_O2}, %{msil_c_O3}, %{msil_c_Os}, %{msil_c_Oz} and %{msil_ll_O0}..%{msil_ll_O3}; each level runs: compile with -S -> ilasm -dll -> %msil_verify -> %msil_jitall -> %msil_exec --expect %S/%{s:stem} (compares with <stem>.expected or <stem>.<os>.expected), plus the %if msil-native-oracle and %if msil-run-netfx legs of A.5. Per-level outputs use distinct %t.<L> names. Everything must work in lit's internal shell (the external shell is deprecated in this LLVM). (6) %{msil_cflags} = '-ffreestanding -Werror=return-type -I<msil/test/Inputs>' (-ffreestanding turns off main's implicit 'return 0', clang/lib/AST/Decl.cpp FunctionDecl::isMain, so a main that falls off its end must be a compile error, not undef). msil_test.h per A.8 with MSIL_EXPECT, the branch-free '#define MSIL_CHECK(acc, bit, c) ((acc) |= (unsigned)!(c) << ((bit) & 7))' for tests written before control flow exists (M6), and puts-based put_u64/put_i64/put_hex helpers (puts is the only output import). (7) msil-verify.py per A.10: ILVerify with ilverify-allowed.txt (Unverifiable, InitLocals, StackByRef, UnmanagedPointer, each with a comment), PEVerify on Windows with the allowed message list plus exactly the message '[found Native Int][expected unmanaged pointer] Unexpected type on the stack.' (the native int arguments of [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64), D15/D21, with a comment naming MEMMOVE; every other 'Unexpected type on the stack' message stays fatal), 'ImportCalli not implemented' becomes a note; any other error fails. (8) update-expected.py per A.14 (native build with the just-built clang and the same -Werror=return-type; --check mode); it refuses to write, and --check fails on, a native run whose exit status is outside 0..255 (D37). (9) msil/test/CMakeLists.txt: check-msil-exec, add_lit_testsuites for per-directory targets, and the lean umbrella check-msil over llvm/test/CodeGen/MSIL, clang/test/CodeGen/MSIL, clang/test/Preprocessor/init-msil.c, clang/test/Driver/msil-toolchain.c and msil/test with DEPENDS clang llc FileCheck not count split-file llvm-config (targets that do not exist yet must not break configure). (10) msil/test/M1/lit.local.cfg: unsupported unless msil-exec. (11) jitall location: lit.cfg.py runs 'python msil/utils/jitall/build-jitall.py' (default output <build>/tools/msil/jitall/) during configuration; it rebuilds only when jitall.cs is newer than jitall.dll; the msil-jitall feature is set when jitall.dll exists afterwards and %msil_jitall uses that directory. (12) msil_require_verify=1: a missing ILVerify, jitall or (on Windows) PEVerify is lit_config.fatal naming the tool and the parameter that would fix it, instead of silently dropping the step (every gate from M0 on passes it, D38). (13) msil_dotnet_env='VAR=VALUE[;VAR=VALUE...]': msil-run.py and msil-jitall.py accept '--env VAR=VALUE' (repeatable) and set it in the child environment; the WSL runner runs 'wsl.exe -d Ubuntu -- env VAR=VALUE dotnet exec ...'; the .NET Framework leg passes the variables through unchanged (used for DOTNET_TieredCompilation=0, D38). (14) Exit statuses: msil-run.py writes '[exit N]' only for 0 <= N <= 255; any other status (a Windows exit of 300 or -1 that Linux would truncate) is written as '[exit N: outside 0..255]', which never matches an .expected file (D37). Before handing over, run the harness against the M0.d self-tests if they exist, else against a hand-written smoke .il in scratch.

#### M0.c jitall force-JIT tool (infra, ~250 lines)

- Owned files: `msil/utils/jitall/jitall.cs`, `msil/utils/jitall/build-jitall.py`, `msil/utils/msil-jitall.py`
- Reads: llvm/docs/MSIL/recon/artifacts/cil-runtime/jitall.cs and llvm/docs/MSIL/recon/artifacts/cil-runtime/csc.rsp (verified prototype and its reference list); llvm/docs/MSIL/recon/cil-runtime.md (e15: what jitall detects; the .NET 10 hang)
- Provides: python build-jitall.py [--out <dir>] (default <build>/tools/msil/jitall, build dir from --build-dir, $MSIL_BUILD_DIR or <repo>/build) -> jitall.dll + jitall.runtimeconfig.json; python msil-jitall.py [--jitall <dir>] [--timeout S] [--mem-limit-mb N] [--env VAR=VALUE]... <dll>: exit 0 if every method JITs

Port the verified prototype (llvm/docs/MSIL/recon/artifacts/cil-runtime/jitall.cs): load the assembly with Assembly.LoadFrom, force-JIT every method with a body (including <Module> global methods; skip pinvokeimpl/abstract methods and static constructors) with RuntimeHelpers.PrepareMethod, print 'JIT FAIL <type>::<method>: <exception>' per failure and 'jitall: N ok, M failed', exit code = M. build-jitall.py compiles it offline, with no NuGet restore: locate the newest SDK (dotnet --list-sdks), run 'dotnet <sdk>/Roslyn/bincore/csc.dll' with -r: for every *.dll of the newest installed Microsoft.NETCore.App.Ref 8.x pack (C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.31/ref/net8.0 here; generate the list like the prototype csc.rsp), -target:exe, -nullable-, -out:<dir>/jitall.dll, and write jitall.runtimeconfig.json (tfm net8.0, framework Microsoft.NETCore.App 8.0.0, rollForward Major). --out defaults to <build>/tools/msil/jitall (the location lit.cfg.py and msil-jitall.py use); rebuild only when jitall.cs is newer than the output; compile into a temporary sibling directory and rename it into place so two concurrent lit runs never see a half-written jitall.dll. msil-jitall.py (--jitall defaults to the same directory; --env VAR=VALUE sets child environment variables) runs 'dotnet exec --runtimeconfig ... jitall.dll <dll>' from a private copy directory under the same timeout and memory cap as msil-run.py (share the helper by importing msil-run.py's module-level function, or duplicate a 40-line helper if import paths get awkward).

#### M0.d Harness self-tests (test, ~550 lines of .test/.il)

- Owned files: `msil/test/harness/lit.local.cfg`, `msil/test/harness/`, `msil/test/harness/Inputs/`
- Reads: llvm/docs/MSIL/recon/tests-ci.md section 6 and Appendix A.6-A.7; this plan, M0.b and M0.c descriptions and section 5.1 (ILAsm contract); llvm/docs/MSIL/recon/asmprinter.md section 9 (verified module shape); this plan section 5.5 and decisions D21, D35, D37, D38; llvm/docs/MSIL/recon/artifacts/review-cil/sw.il and ilv2.il (switch on native int; MemoryCopy under PEVerify)

Write from the specification (M0.b/M0.c descriptions, tests-ci.md section 6, plan 5.5 and D21/D35/D37/D38), not from the implementation. Hand-written IL in the Phase-1 dialect of plan section 5.1 (mscorlib-only, global methods, '<msil>Main' entry point) assembled with %ilasm. Tests (each < 5 s): exit-code.test ([exit 42]); exit-range.test (a program exiting with 300 produces '[exit 300: outside 0..255]' and fails --expect against an .expected that says [exit 44], checked with 'not'; update-expected.py refuses an out-of-range native result); puts-windows.test (pinvokeimpl("ucrtbase" cdecl) puts, REQUIRES: msil-triple-windows); puts-wsl.test (pinvokeimpl("libc"), REQUIRES: msil-triple-linux, msil-wsl); unhandled-exception.test ([unhandled exception: System.NullReferenceException] from ldnull; throw); verify-rejects-bad-stack.test (join with different stack depths -> msil-verify fails, checked with 'not'); verify-accepts-unverifiable.test (localloc, cpblk, ldind through native int pass); verify-ptr-int64-mix.test (native int + int64 fails under PEVerify; REQUIRES: msil-peverify); verify-memorycopy-native-int.test (REQUIRES: msil-peverify; a call to [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64) with native int addresses and conv.i8 lengths passes, while passing a native int where an int64 parameter is expected still fails: both sides of the D21 exception); verify-rejects-switch-native-int.test ('ldarg.0' of a native int followed by 'switch' fails msil-verify with StackUnexpected, and the same body with 'conv.i4' before 'switch' passes: the reason for D35); jitall-maxstack.test and jitall-falloff.test (.maxstack too small and a method falling off its end are reported by msil-jitall.py without running Main); timeout.test (infinite loop, --timeout 5 -> [timeout]); same-assembly-name.test (two DLLs with the same .assembly name in one directory: each runs its own code); expected-mismatch.test (wrong .expected -> failure with a diff, via 'not'); dotnet-env.test (msil_dotnet_env passthrough: a program that returns the length of Environment::GetEnvironmentVariable("MSIL_TEST_ENV") exits with 3 when run with --env MSIL_TEST_ENV=abc). harness/lit.local.cfg requires msil-ilasm and msil-dotnet only (no clang).

#### M0.e CI workflow and fork hygiene (infra, ~400 lines)

- Owned files: `.github/workflows/msil-ci.yml`, `.github/msil/disable-upstream-workflows.sh`, `msil/utils/tools/msil-tools.proj`
- Reads: llvm/docs/MSIL/recon/tests-ci.md section 9 and Appendix A.11-A.13

Implement tests-ci.md section 9 and Appendix A.11-A.13 with these adjustments: build targets 'clang llc opt FileCheck not count split-file llvm-config TargetParserTests MCTests MSILTests' where MSILTests (and later check-msil content) are skipped when the target does not exist yet (probe with 'ninja -C build -t targets all'); unit-test step runs the gtest binaries with --gtest_filter=*MSIL*:TargetRegistry.*; then 'ninja check-msil' with LIT_OPTS='--param msil_dotnet_runtime=8 --param msil_native_oracle=required' (Windows adds --param msil_run_netfx=1). Linux job: setup-dotnet 8.0.x and 10.0.x, 'dotnet restore msil/utils/tools/msil-tools.proj' for runtime.linux-x64.Microsoft.NETCore.ILAsm and .ILDAsm pinned to 10.0.12 (the version cached locally), chmod +x, 'dotnet tool update -g dotnet-ilverify --version 10.0.12'. Windows job: Framework ilasm, PEVerify, netfx leg, git config core.autocrlf false before checkout. All jobs guarded by github.repository_owner == 'alethic'; third-party actions pinned by SHA exactly as upstream pins them; the actions/setup-dotnet SHA cannot be verified offline, so leave it as an explicit 'TODO(user): pin SHA' comment with the tag. Triggers: workflow_dispatch only (user decision, section 10 item 5); the push (msil-main, msil-milestone*), pull_request (into msil-*) and nightly schedule triggers are written out but commented, with a note that the user enables them after running disable-upstream-workflows.sh. Do not edit or delete any upstream workflow; disable-upstream-workflows.sh (gh workflow disable for every non msil-* workflow, --limit 200) is run by the user, never by an agent. Validate with PyYAML only; the workflow is not executed locally.

**Build targets:** `(none; cmake reconfigure only)`

**Gate commands** (run in order by the gate agent; all must succeed):

```text
 1. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-configure.ps1
 2. python D:/llvm-msil/msil/utils/plan-check.py --self-test
 3. python D:/llvm-msil/msil/utils/plan-check.py ownership
 4. python D:/llvm-msil/msil/utils/jitall/build-jitall.py  (default output D:/llvm-msil/build/tools/msil/jitall; offline, no NuGet restore)
 5. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" msil/test/harness
 6. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_runner=wsl" msil/test/harness
 7. python -c "import yaml; d=yaml.safe_load(open(r'D:/llvm-msil/.github/workflows/msil-ci.yml', encoding='utf-8')); on=d.get('on', d.get(True)); assert list(on)==['workflow_dispatch'], on"  (PyYAML reads the key 'on' as True; only workflow_dispatch may be active, section 10 item 5)
 8. pwsh -NoProfile -Command "git -C D:/llvm-msil status --porcelain -- llvm clang lld"  (must list nothing outside llvm/docs/MSIL)
 9. pwsh -NoProfile -Command "git -C D:/llvm-msil add -A; python D:/llvm-msil/msil/utils/plan-check.py scope M0"  (every staged path is owned by a WP of M0, is a M0 fix-loop amendment in section 12, or is on the allow-list)
```

**Exit criteria**
- Configure with the msil external project succeeds and creates build/tools/msil/test/lit.site.cfg.py.
- plan-check.py passes its self-test and finds no ownership conflict in this plan; 'reads' works for every milestone that has started.
- jitall builds offline into build/tools/msil/jitall and detects .maxstack/fall-off errors without running the program.
- Every harness self-test passes on Windows with msil_require_verify=1; the WSL leg passes (puts-windows is UNSUPPORTED there, puts-wsl runs).
- Timeouts, the memory cap, the 0..255 exit-status rule and the environment passthrough are enforced by msil-run.py and msil-jitall.py.
- No tracked file outside msil/, .github/ and llvm/docs/MSIL/ changed; no download happened.
- ImplementationPlan.md and all recon reports (including cil-runtime.md, build-env.md and recon/artifacts/) are committed.

**Review focus** (in addition to the three standing lenses)
- Harness determinism: private-directory execution, LF transcripts, exit-code mapping on Windows and Linux.
- Safety: timeout + memory cap on every child process; no reading of NuGet.Config; no downloads.
- lit internal-shell portability of every substitution; optional tools degrade to UNSUPPORTED, except that msil_require_verify=1 turns a missing verifier into a configuration error.
- Allow-list minimality in ilverify-allowed.txt (each code justified).
- CI file touches no upstream workflow; pinned actions; guarded by repository owner.
- Scripts stay byte-compatible with the tested originals except for the listed changes.
- plan-check.py parses the real plan exactly (try a deliberately conflicting copy) and the scope allow-list is no wider than section 1 says.
- msil-expect-error.ps1 and msil-gtest.ps1 cannot pass on a crash or a skipped test.

**Rebuild cost.** cmake reconfigure only (~1-2 min); no compilation.

### M1. Shared plumbing: msil object format, MC environment, target selection, clang wrapper (all widely-included header edits)

**Goal.** Teach LLVM and clang the msil object format and every switch over it; select a target named 'msil' by object format (it is not built yet); give MSIL triples the exact native DataLayout and no exception model; add the clang TargetInfo wrapper, inline-asm rejection, the no-prototype attribute and the driver's -c error. All widely-included header edits of Phase 1 land here so that this is the only full rebuild. A TableGen-only pre-build by the build agent (M1.0) runs first, so implementers can syntax-check TUs that include IR, CodeGen and clang headers before the long gate build.

**Depends on:** M0.

**Header changes:**
- llvm/include/llvm/TargetParser/Triple.h: ObjectFormatType::MSIL; isOSBinFormatMSIL(); getDefaultObjectFormat(); getNativeFormatTriple() (full LLVM + clang rebuild)
- llvm/include/llvm/MC/MCContext.h: Environment::IsMSIL; forward decl MCSectionMSIL; MSILAllocator; MSILUniquingMap; getMSILSection(StringRef, SectionKind)
- llvm/include/llvm/MC/MCObjectFileInfo.h: private initMSILMCObjectFileInfo(const Triple &)
- llvm/include/llvm/MC/MCSectionMSIL.h: new header-only file
- llvm/include/llvm/Config/Targets.h.cmake: LLVM_HAS_MSIL_TARGET
- clang/include/clang/Basic/DiagnosticCommonKinds.td: err_msil_asm_unsupported
- clang/include/clang/Basic/DiagnosticDriverKinds.td: err_drv_msil_no_object_output

**Work packages**

#### M1.0 TableGen pre-build for syntax checks (build agent) (infra, no source changes)

- Owned files: (none; build agent, writes only the build tree)
- Reads: llvm/docs/MSIL/recon/build-env.md; this plan section 1 (generator refresh)
- Provides: generated .inc files in D:/llvm-msil/build for intrinsics, OpenMP/OpenACC, ValueTypes, TargetParser, Analysis and every clang TableGen target

Executed by the build agent, not an implementer, before the other M1 WPs may syntax-check (they start coding immediately against their recon patches). It owns no files and writes only the build tree. Step 1: 'pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 llvm-tblgen clang-tblgen intrinsics_gen acc_gen omp_gen vt_gen target_parser_gen analysis_gen clang-tablegen-targets' (about 250 TableGen runs plus the two tblgen tools; a few minutes) and report the elapsed time. Without it the configured tree has no generated .inc (build/include/llvm/IR has no Attributes.inc or Intrinsics*.inc, there are no clang Diagnostic*.inc), so msil-syntax.ps1 fails on every TU that includes IR or clang headers (TargetMachine.cpp, BitcodeWriter.cpp, AddressSanitizer.cpp, every M1.c/M1.d file). Step 2: when M1.c hands over its DiagnosticCommonKinds.td / DiagnosticDriverKinds.td edits, run 'pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 clang-tablegen-targets' again so diag::err_msil_asm_unsupported and err_drv_msil_no_object_output exist for M1.d's checks, then tell M1.d. Step 3: whenever an implementer reports a missing generated file (msil-syntax.ps1 prints the *_gen / *TableGen target to build), build that target. The real build is gate step 1.

#### M1.a Triple, DataLayout, EH and long-double defaults (implement, ~150 lines)

- Owned files: `llvm/include/llvm/TargetParser/Triple.h`, `llvm/lib/TargetParser/Triple.cpp`, `llvm/lib/TargetParser/TargetDataLayout.cpp`
- Depends on: M1.0
- Reads: llvm/docs/MSIL/recon/triple-objfmt.md sections 1, 6, 7.1-7.3; llvm/docs/MSIL/recon/registry-tm.md sections 3.3-3.4
- Provides: `Triple::MSIL`; `bool Triple::isOSBinFormatMSIL() const`; `LLVM_ABI Triple::ObjectFormatType Triple::getDefaultObjectFormat() const`; `LLVM_ABI Triple Triple::getNativeFormatTriple() const`

Apply the verified patch of triple-objfmt.md 7.1-7.3 exactly (175 checks passed in its harness): enumerator MSIL between MachO and SPIRV; isOSBinFormatMSIL() next to isOSBinFormatDXContainer(); getDefaultObjectFormat() (returns the static getDefaultFormat(*this)) and getNativeFormatTriple() (strips a trailing '-msil' or a bare 'msil' from the environment component and rebuilds the 3- or 4-component native triple) after getObjectFormat(). Triple.cpp: getObjectFormatTypeName case MSIL -> "msil"; parseFormat .EndsWith("msil", Triple::MSIL); getDefaultExceptionHandling returns ExceptionHandling::None first when isOSBinFormatMSIL(); getDefaultLongDoubleFormat returns IEEEdouble for MSIL (decision D3). TargetDataLayout.cpp: at the top of Triple::computeDataLayout, 'if (isOSBinFormatMSIL()) return getNativeFormatTriple().computeDataLayout(ABIName);'. normalize() stays unchanged (x86_64-pc-win32-msil normalizes to x86_64-pc-windows-msil, the -elf precedent). Add nothing else to Triple.h: a later rename would cost another full rebuild. Re-verify every line number against HEAD before editing.

#### M1.b MC layer, registry hook, LLVM switch sites, CMake registration (implement, ~350 lines)

- Owned files: `llvm/include/llvm/MC/MCSectionMSIL.h`, `llvm/include/llvm/MC/MCContext.h`, `llvm/lib/MC/MCContext.cpp`, `llvm/include/llvm/MC/MCObjectFileInfo.h`, `llvm/lib/MC/MCObjectFileInfo.cpp`, `llvm/lib/MC/TargetRegistry.cpp`, `llvm/lib/MC/MCParser/AsmParser.cpp`, `llvm/lib/Target/TargetMachine.cpp`, `llvm/lib/Bitcode/Writer/BitcodeWriter.cpp`, `llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp`, `llvm/lib/Target/ARM/AsmParser/ARMAsmParser.cpp`, `lldb/source/Plugins/Process/gdb-remote/GDBRemoteCommunicationClient.cpp`, `llvm/CMakeLists.txt`, `llvm/include/llvm/Config/Targets.h.cmake`
- Depends on: M1.0, M1.a
- Reads: llvm/docs/MSIL/recon/triple-objfmt.md sections 3a, 4, 5, 7.5-7.7; llvm/docs/MSIL/recon/registry-tm.md sections 1.2, 1.5, 2.2
- Provides: `MCContext::IsMSIL`; `MCSectionMSIL *MCContext::getMSILSection(StringRef Name, SectionKind K)`; `class MCSectionMSIL final : public MCSection (header-only)`; TargetRegistry::lookupTarget(const Triple&) returns the target named "msil" for every MSIL triple; `-march=msil keeps arch/OS/env and sets the msil object format`; `LLVM_HAS_MSIL_TARGET`
- Consumes: Triple::MSIL; Triple::isOSBinFormatMSIL()

Implement triple-objfmt.md 7.5-7.7 and registry-tm.md 1.2/1.5/2.2: (1) MCSectionMSIL.h: header-only, final, private ctor with friend MCContext, stores SectionKind. (2) MCContext: append IsMSIL to enum Environment, SpecificBumpPtrAllocator<MCSectionMSIL> MSILAllocator, StringMap<MCSectionMSIL *> MSILUniquingMap, LLVM_ABI MCSectionMSIL *getMSILSection(StringRef Name, SectionKind K) (unique by name, begin symbol nullptr); MCContext.cpp must #include "llvm/MC/MCSectionMSIL.h" (else the allocator fails to compile), ctor switch case Triple::MSIL -> Env = IsMSIL, reset() destroys/clears both, createSymbolImpl case IsMSIL -> plain MCSymbol. (3) MCObjectFileInfo: private initMSILMCObjectFileInfo(const Triple &) creating TextSection/DataSection/BSSSection/ReadOnlySection via getMSILSection(".text", SectionKind::getText()) etc.; case IsMSIL in initMCObjectFileInfo; case Triple::MSIL in the getDwarfComdatSection fatal group. (4) TargetRegistry.cpp: static findTargetByName, constexpr StringLiteral MSILTargetName = "msil"; lookupTarget(const Triple&) returns that target for isOSBinFormatMSIL() before arch matching, else the error 'No available targets are compatible with triple "<T>" (the MSIL target is not built; configure LLVM with -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=MSIL)'; lookupTarget(ArchName, Triple&): -march=msil -> setObjectFormat(Triple::MSIL) if needed and return; any other -march with an MSIL triple -> error; createMCObjectStreamer case Triple::MSIL -> reportFatalUsageError("MSIL object file emission is not implemented"). (5) AsmParser.cpp ctor switch case IsMSIL -> report_fatal_error("assembly parsing is not supported for MSIL"). (6) TargetMachine::shouldAssumeDSOLocal: before the MachO block 'if (TT.isOSBinFormatMSIL()) return !GV->isDeclarationForLinker();' (avoids the assert reachable from DAGCombine). (7) BitcodeWriter getSectionNameForBitcode / getSectionNameForCommandline: case Triple::MSIL llvm_unreachable; AddressSanitizer getGlobalMetadataSection: case MSIL in the fatal group. (8) ARMAsmParser parsePrefix case MCContext::IsMSIL in the unreachable group and lldb GDBRemoteCommunicationClient case llvm::Triple::MSIL in the not-supported group: neither is built here, edit by inspection only and say so in the hand-over. (9) llvm/CMakeLists.txt: add MSIL to LLVM_ALL_EXPERIMENTAL_TARGETS between M68k and Xtensa; Targets.h.cmake: '/* Define if the MSIL target is built in */' + '#cmakedefine01 LLVM_HAS_MSIL_TARGET' between Mips and MSP430. Syntax-check MCContext.cpp, MCObjectFileInfo.cpp, TargetRegistry.cpp and AsmParser.cpp with msil-syntax.ps1 (clang-cl, -Wswitch is on) against the patched headers.

#### M1.c clang TargetInfo wrapper, diagnostics, driver -c error (implement, ~300 lines)

- Owned files: `clang/lib/Basic/Targets/MSIL.h`, `clang/lib/Basic/Targets.cpp`, `clang/include/clang/Basic/DiagnosticCommonKinds.td`, `clang/include/clang/Basic/DiagnosticDriverKinds.td`, `clang/lib/Driver/ToolChains/Clang.cpp`
- Depends on: M1.0, M1.a
- Reads: llvm/docs/MSIL/recon/clang.md sections 2, 3.2 (diagnostic text), 5.4; clang/lib/Basic/Targets.cpp AllocateTarget (lines ~111-700)
- Provides: `template <class Native> class clang::targets::MSILTargetInfo final : public Native`; diag::err_msil_asm_unsupported (%0 = 0 inline assembly | 1 Microsoft-style inline assembly | 2 file-scope assembly, %1 = triple); `diag::err_drv_msil_no_object_output (%0 = triple)`; `predefined macro __MSIL__`
- Consumes: Triple::isOSBinFormatMSIL()

clang.md 2.3 with lead decision D6. clang/lib/Basic/Targets/MSIL.h (header-only): template <class Native> class MSILTargetInfo final : public Native; ctor and adjust() set LongDoubleWidth = DoubleWidth, LongDoubleAlign = DoubleAlign, LongDoubleFormat = &llvm::APFloat::IEEEdouble(); getTargetDefines calls Native's then defines __MSIL__; adjust() reports err_opt_not_valid_on_target for -mlong-double-80/-128. Targets.cpp: #include "Targets/MSIL.h"; static std::unique_ptr<TargetInfo> AllocateMSILTarget(const llvm::Triple &, const TargetOptions &) called first in AllocateTarget when Triple.isOSBinFormatMSIL(). Phase 1 supports exactly: x86_64 Linux (gnu, musl; not Android/OHOS/x32) -> MSILTargetInfo<LinuxTargetInfo<X86_64TargetInfo>>; aarch64 Linux -> MSILTargetInfo<LinuxTargetInfo<AArch64leTargetInfo>>; x86_64 Windows MSVC -> MSILTargetInfo<MicrosoftX86_64TargetInfo>; x86_64 Windows GNU -> MSILTargetInfo<MinGWX86_64TargetInfo>; aarch64 Windows MSVC -> MSILTargetInfo<MicrosoftARM64TargetInfo>; aarch64 Windows GNU -> MSILTargetInfo<MinGWARM64TargetInfo> (class names verified at Targets.cpp:164, 188, 191, 641, 676, 679 - re-check the exact conditions the native dispatch uses). Every other *-msil triple (Darwin, i686, other OS or arch) returns nullptr, which clang reports as 'unknown target triple'. Diagnostics: err_msil_asm_unsupported in the Inline Assembly block of DiagnosticCommonKinds.td: "%select{inline assembly|Microsoft-style inline assembly|file-scope assembly}0 is not supported for the MSIL target '%1'"; err_drv_msil_no_object_output in DiagnosticDriverKinds.td: "emitting object files is not yet supported for MSIL target '%0'; use -S to produce ILAsm text, or -emit-llvm or -flto to produce bitcode". Driver: report it in Clang::ConstructJob's AssembleJobAction branch and at the top of ClangAs::ConstructJob when the effective triple isOSBinFormatMSIL() (clang.md 5.4; re-locate the lines, ~5499 and ~9261); '-c -emit-llvm' and '-flto -c' must keep working. Do not touch IsIntegratedAssemblerDefault.

#### M1.d clang CodeGen, Sema and Parse hooks (implement, ~250 lines)

- Owned files: `clang/lib/CodeGen/CGStmt.cpp`, `clang/lib/CodeGen/CodeGenModule.cpp`, `clang/lib/Parse/ParseStmtAsm.cpp`, `clang/lib/CodeGen/BackendUtil.cpp`, `clang/lib/CodeGen/CGObjCMac.cpp`, `clang/lib/Sema/SemaChecking.cpp`
- Depends on: M1.0, M1.a, M1.c
- Reads: llvm/docs/MSIL/recon/clang.md sections 3, 4; clang/lib/CodeGen/Targets/WebAssembly.cpp:122-123 (no-prototype)
- Provides: IR function attribute "no-prototype" on body-less, prototype-less declarations for MSIL triples (consumed by MSILAddMissingPrototypes, M9)
- Consumes: diag::err_msil_asm_unsupported (M1.c); Triple::isOSBinFormatMSIL()

clang.md 3.2 and 4 plus passes-frame.md section 11: (1) CGStmt.cpp: static emitMSILAsmStmt() called first in CodeGenFunction::EmitAsmStmt when the triple isOSBinFormatMSIL(): an empty GNU asm string with no outputs or inputs whose clobbers include "memory" becomes 'fence syncscope("singlethread") seq_cst'; an empty asm with no operands and no memory clobber emits nothing; everything else (any GNU or MS asm) reports err_msil_asm_unsupported (kind 0 or 1) with the triple string. (2) CodeGenModule.cpp: Decl::FileScopeAsm on MSIL reports kind 2 for a non-empty string and never calls appendModuleInlineAsm; at ~1718 change 'if (getTriple().isOSLinux())' to also require isOSBinFormatELF() (fixes the aarch64-linux-msil assert at ~1722); add case llvm::Triple::MSIL to the CFString section switch (~7509) in the unimplemented group; in SetFunctionAttributes, where setTargetAttributes is called for declarations (~3803), add 'if (getTriple().isOSBinFormatMSIL() && !FD->doesThisDeclarationHaveABody() && !FD->hasPrototype()) F->addFnAttr("no-prototype");' (same rule as WebAssembly.cpp:122-123). (3) ParseStmtAsm.cpp ParseMicrosoftAsmStatement: for MSIL skip lookupTarget and the MC-based parse and fall through to the empty-statement path described in clang.md 3.2, so CodeGen diagnoses it only if the function is emitted. (4) BackendUtil.cpp asanUseGlobalsGC: case Triple::MSIL in the break group. (5) CGObjCMac.cpp GetSectionName: case llvm::Triple::MSIL in the report_fatal_error group. (6) SemaChecking.cpp: add llvm::Triple::MSIL to the __builtin___CFStringMakeConstantString unsupported-format list. Re-verify every line number at HEAD. The diagnostics come from M1.c's .td change: syntax-check CGStmt.cpp, CodeGenModule.cpp and ParseStmtAsm.cpp only after M1.0 has re-run clang-tablegen-targets following M1.c's hand-over.

#### M1.e LLVM unit tests for the triple and the registry (test, ~350 lines)

- Owned files: `llvm/unittests/TargetParser/TripleTest.cpp`, `llvm/unittests/MC/TargetRegistry.cpp`
- Reads: llvm/docs/MSIL/Design.md section 3; llvm/docs/MSIL/recon/triple-objfmt.md sections 1, 6, 7.4 (spec text, not the patch); llvm/docs/MSIL/recon/registry-tm.md sections 1, 3, 8; this plan decisions D3, D4

Write from the spec, not from M1.a/M1.b code. TripleTest.cpp: TEST(TripleTest, MSILObjectFormat) over the 8 Design.md triples (x86_64/aarch64 linux-gnu, x86_64/aarch64 linux-musl, x86_64/aarch64 windows-msvc, x86_64-w64-windows-gnu, aarch64-apple-darwin, each with -msil): native arch/vendor/OS/env plus MSIL, str() and normalize() round trip, getNativeFormatTriple() equals the native string, computeDataLayout() equals the native triple's byte for byte, getDefaultExceptionHandling() == None, Darwin keeps 'msil' in the environment slot; mutators setObjectFormat/setEnvironment/setArch keep the suffix; getObjectFormatTypeName(MSIL) == "msil". TEST(TripleTest, MSILLongDouble): getDefaultLongDoubleFormat() is IEEEdouble. Add lines to the existing FileFormat, DefaultExceptionHandling, NormalizeWindows (x86_64-pc-win32-msil -> x86_64-pc-windows-msil; x86_64-pc-windows-msvc19.40.0-msil unchanged) and isCompatibleWith tests. TargetRegistry.cpp: exempt "msil" in TargetHasArchType (line 35: Name != "cpp" && Name != "msil"); TEST(TargetRegistry, MSILLookupWithoutTarget): when no target named msil is registered, lookupTarget(Triple("x86_64-unknown-linux-gnu-msil")) fails with an error mentioning LLVM_EXPERIMENTAL_TARGETS_TO_BUILD (GTEST_SKIP when msil is registered); TEST(TargetRegistry, MSILLookupByObjectFormat), TEST(TargetRegistry, MSILMarchSetsObjectFormat) and TEST(TargetRegistry, MSILRejectsOtherMarch) GTEST_SKIP unless msil is registered (they activate in M2). Every new test name contains MSIL.

#### M1.f clang frontend and driver tests (test, ~450 lines)

- Owned files: `clang/test/Preprocessor/init-msil.c`, `clang/test/Driver/msil-toolchain.c`, `clang/test/CodeGen/MSIL/long-double.c`, `clang/test/CodeGen/MSIL/inline-asm.c`, `clang/test/CodeGen/MSIL/ms-inline-asm.c`, `clang/test/CodeGen/MSIL/system-headers.c`, `clang/test/CodeGen/MSIL/triples.c`, `clang/test/CodeGen/MSIL/datalayout.c`, `clang/test/CodeGen/MSIL/no-prototype.c`
- Reads: llvm/docs/MSIL/recon/clang.md section 6 (RUN lines); llvm/docs/MSIL/recon/registry-tm.md 8.4; this plan decision D6; this plan section 10 decision 9 (__ELF__)

clang.md 6.1-6.5 and registry-tm.md 8.4, adapted to D6. No REQUIRES lines: the MSIL backend is not built in M1 and no test may run it (-E, -###, -fsyntax-only, -emit-llvm only). init-msil.c: predefine diff native vs -msil for x86_64-linux-gnu, x86_64-windows-msvc and aarch64-linux-gnu with the LDBL/long-double, __ELF__ and __MSIL__ lines filtered, plus direct checks of __MSIL__ 1 and __SIZEOF_LONG_DOUBLE__ 8, and an explicit check of the section 10 decision 9 default: the '-msil' Linux triples do not define __ELF__ (LNX-NOT: #define __ELF__) while the native Linux triples do (clang/lib/Frontend/InitPreprocessor.cpp defines it only for isOSBinFormatELF()). msil-toolchain.c: -### for the three triples (-triple keeps -msil; for MSVC the version is inserted before -msil, match x86_64-pc-windows-msvc{{.*}}-msil), -S accepted, no -faddrsig and no -fdwarf2-cfi-asm, '-c' fails with err_drv_msil_no_object_output, '-c -emit-llvm' works. long-double.c, inline-asm.c (-verify: GNU asm and file-scope asm errors; 'asm volatile("" ::: "memory")' becomes 'fence syncscope("singlethread") seq_cst'; an unused static inline function with asm is accepted; -fsyntax-only gives no diagnostics), ms-inline-asm.c, system-headers.c (#include <intrin.h> and <immintrin.h> on x86_64-pc-windows-msvc-msil with -emit-llvm-only -Werror), triples.c (aarch64-unknown-linux-gnu-msil compiles without the ELF-only aarch64 module flags; ppc64le-msil, *-android-msil, aarch64-apple-darwin-msil and i686-pc-windows-msvc-msil are rejected as unknown target triples in Phase 1), datalayout.c (-emit-llvm datalayout for x86_64-unknown-linux-gnu-msil is the native e-m:e string and for x86_64-pc-windows-msvc-msil the native e-m:w string), no-prototype.c ('int f(); int g(void){return f(1);}' gives the "no-prototype" attribute on f only for msil triples).

**Build targets:** `llc` `opt` `clang` `FileCheck` `not` `count` `split-file` `llvm-config` `TargetParserTests` `MCTests`

**Gate commands** (run in order by the gate agent; all must succeed):

```text
 1. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 llc opt clang FileCheck not count split-file llvm-config TargetParserTests MCTests > D:/llvm-msil/build/msil-M1-build.log 2>&1  (run_in_background; 1-2.5 h)
 2. bash: ! grep -qE "'(IsMSIL|MSIL)' not handled in switch" D:/llvm-msil/build/msil-M1-build.log  (no -Wswitch warning for the new enumerators)
 3. D:/llvm-msil/build/unittests/TargetParser/TargetParserTests.exe
 4. D:/llvm-msil/build/unittests/MC/MCTests.exe
 5. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" clang/test/Preprocessor/init-msil.c clang/test/Driver/msil-toolchain.c clang/test/CodeGen/MSIL
 6. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" clang/test/Preprocessor/init-x86.c clang/test/Preprocessor/init-aarch64.c clang/test/Preprocessor/init.c clang/test/CodeGen/X86/x86_64-arguments.c clang/test/CodeGen/X86/x86_64-arguments-win32.c
 7. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-run.ps1 llc -mtriple=x86_64-unknown-linux-gnu -filetype=null D:/llvm-msil/llvm/test/CodeGen/X86/add.ll  (native triples unaffected)
 8. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-expect-error.ps1 -Pattern 'LLVM_EXPERIMENTAL_TARGETS_TO_BUILD=MSIL' -- D:/llvm-msil/build/bin/clang.exe --target=x86_64-pc-windows-msvc-msil -S D:/llvm-msil/clang/test/CodeGen/MSIL/datalayout.c -o -  (exit code 1, the 'MSIL target is not built' error, no crash marker)
 9. pwsh -NoProfile -Command "Set-Location D:/llvm-msil; git add -A; python 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/git-clang-format' --binary 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe' --extensions c,cpp,h,hpp,hxx,cxx,inc,cppm,cl --diff HEAD -- llvm/lib llvm/include llvm/unittests clang/lib clang/include"  (must print no diff; same extension list as llvm/utils/git/code-format-helper.py, so .td files are never formatted)
10. pwsh -NoProfile -Command "git -C D:/llvm-msil add -A; python D:/llvm-msil/msil/utils/plan-check.py scope M1"  (every staged path is owned by a WP of M1, is a M1 fix-loop amendment in section 12, or is on the allow-list)
```

**Exit criteria**
- TargetParserTests and MCTests pass in full (new MSIL tests run; registry tests that need the target are skipped).
- All clang MSIL frontend/driver tests and the native regression tests listed in the gate pass.
- No -Wswitch warning mentions MSIL or IsMSIL in the build log.
- 'clang --target=x86_64-pc-windows-msvc-msil -S' fails cleanly with the 'MSIL target is not built' error: exit code 1 and no crash marker (checked by msil-expect-error.ps1, not by 'not').
- Native triples produce unchanged output; Triple.h exposes only the four planned additions.
- init-msil.c pins the __ELF__ behaviour of section 10 decision 9 explicitly.

**Review focus** (in addition to the three standing lenses)
- Completeness of the switch-site checklist (triple-objfmt.md 3a): every ObjectFormatType/Environment switch handles MSIL.
- DataLayout byte-identical to native; EH None; normalize/merge pitfalls documented; no API beyond the plan in Triple.h.
- clang wrapper: mirror of AllocateTarget for the Phase-1 triples exactly; Darwin/i686 rejected; long double = double ABI.
- Inline-asm handling: barrier allowance, MS asm parse bypass, file-scope asm; diagnostics wording; no effect on native triples.
- Tests derived from the spec, covering negative cases; upstream-style test conventions.

**Rebuild cost.** Full: ~3,462 ninja steps (2,762 C++ TUs, 242 tablegen runs) for the M1 target set; estimated 1-2.5 h on this shared 4C/8T host. The only full rebuild in Phase 1.

### M2. MSIL target skeleton: registration, TargetMachine, TableGen core, MC layer, module header

**Goal.** An experimental MSIL target that builds, is selected for every *-msil triple, and makes llc and clang -S emit a complete ilasm-assemblable module for IR without function definitions. Every Phase-1 source file exists, so llvm/lib/Target/MSIL/CMakeLists.txt, MSIL.h and the pass pipeline in MSILTargetMachine.cpp never change again in Phase 1. Query hooks (TargetLowering and TTI) get their final bodies now; only lowering hooks are stubs, each failing loudly with the milestone that implements it (D33). The structural TableGen records get their final names and operand lists (plan 5.4). Wave order inside the run: M2.b (TableGen) and M2.e (tests) first; M2.f and M2.c check against M2.b's records; M2.a and M2.d check against M2.f and M2.c (section 1.2a).

**Depends on:** M1.

**Header changes:**
- none (target-local only; Targets.def/AsmPrinters.def/Targets.h are regenerated by configure)

**Work packages**

#### M2.a Target glue: CMake, registration, TargetMachine pipeline, TTI, TLOF, MSILPrepareIR, stub passes (implement, ~1250 lines (incl. ~18 small stubs))

- Owned files: `llvm/lib/Target/MSIL/CMakeLists.txt`, `llvm/lib/Target/MSIL/TargetInfo/CMakeLists.txt`, `llvm/lib/Target/MSIL/TargetInfo/MSILTargetInfo.h`, `llvm/lib/Target/MSIL/TargetInfo/MSILTargetInfo.cpp`, `llvm/lib/Target/MSIL/MCTargetDesc/CMakeLists.txt`, `llvm/lib/Target/MSIL/MSILTargetMachine.cpp`, `llvm/lib/Target/MSIL/MSILTargetTransformInfo.h`, `llvm/lib/Target/MSIL/MSILTargetTransformInfo.cpp`, `llvm/lib/Target/MSIL/MSILTargetObjectFile.h`, `llvm/lib/Target/MSIL/MSILTargetObjectFile.cpp`, `llvm/lib/Target/MSIL/MSILPrepareIR.cpp`, `llvm/lib/Target/MSIL/MSILAddMissingPrototypes.cpp`, `llvm/lib/Target/MSIL/MSILFixFunctionBitcasts.cpp`, `llvm/lib/Target/MSIL/MSILOptimizeReturned.cpp`, `llvm/lib/Target/MSIL/MSILArgumentMove.cpp`, `llvm/lib/Target/MSIL/MSILSetMemFlags.cpp`, `llvm/lib/Target/MSIL/MSILCleanCodeAfterTrap.cpp`, `llvm/lib/Target/MSIL/MSILNullifyDebugValueLists.cpp`, `llvm/lib/Target/MSIL/MSILReplacePhysRegs.cpp`, `llvm/lib/Target/MSIL/MSILOptimizeLiveIntervals.cpp`, `llvm/lib/Target/MSIL/MSILRegStackify.cpp`, `llvm/lib/Target/MSIL/MSILRegColoring.cpp`, `llvm/lib/Target/MSIL/MSILExplicitLocals.cpp`, `llvm/lib/Target/MSIL/MSILRegNumbering.cpp`, `llvm/lib/Target/MSIL/MSILStackCheck.cpp`, `llvm/lib/Target/MSIL/MSILDebugValueManager.h`, `llvm/lib/Target/MSIL/MSILDebugValueManager.cpp`, `llvm/lib/Target/MSIL/MSILDataSegment.h`, `llvm/lib/Target/MSIL/MSILDataSegment.cpp`
- Depends on: M2.b, M2.f
- Reads: llvm/docs/MSIL/recon/registry-tm.md sections 1.3, 2.3-2.4, 4.4, 5.2, 7.3-7.4; llvm/docs/MSIL/recon/passes-frame.md sections 4.1-4.2; llvm/docs/MSIL/recon/wasm-inventory.md section 5; llvm/lib/Target/WebAssembly/WebAssemblyTargetMachine.{h,cpp}; llvm/include/llvm/CodeGen/BasicTTIImpl.h (shouldBuildLookupTables ~644); this plan sections 2, 3 (D8, D14, D25, D26, D33), 5.2-5.3
- Provides: `llvm::getTheMSILTarget()`; `MSILTargetMachine member definitions and MSILPassConfig`; `every pass factory and initializer of plan 5.3 (stub passes)`; `class MSILTTIImpl`; `class MSILTargetObjectFile`
- Consumes: MSIL.h, MSILTargetMachine.h, MSILSubtarget, MSILFunctionInfo + yaml::MSILFunctionInfo (M2.f); MSILGen*.inc from M2.b's .td (checked with msil-tblgen.ps1 into scratch); LLVMInitializeMSILTargetMC (M2.c); MSILAsmPrinter (M2.d)

(1) CMakeLists.txt per registry-tm.md 2.4: add_llvm_component_group(MSIL); the six tablegen runs; add_public_tablegen_target(MSILCommonTableGen); add_llvm_target(MSILCodeGen <every .cpp of plan section 5.2, alphabetical> DEPENDS intrinsics_gen LINK_COMPONENTS Analysis AsmPrinter CodeGen CodeGenTypes Core MC MSILDesc MSILInfo Scalar SelectionDAG Support Target TargetParser TransformUtils ADD_TO_COMPONENT MSIL); add_subdirectory(MCTargetDesc) and (TargetInfo); no GlobalISel, AsmParser, Disassembler or Utils. TargetInfo/ per registry-tm 1.3 (name "msil", description "MSIL (.NET CIL as ILAsm text)", BackendName "MSIL", ArchMatchFn always false, HasJIT=false). MCTargetDesc/CMakeLists.txt: add_llvm_component_library(LLVMMSILDesc MSILInstPrinter.cpp MSILMCAsmInfo.cpp MSILMCExpr.cpp MSILMCTargetDesc.cpp MSILMCTypeUtilities.cpp MSILTargetStreamer.cpp LINK_COMPONENTS CodeGenTypes MC Support TargetParser MSILInfo ADD_TO_COMPONENT MSIL) with the include_directories/add_dependencies(MSILCommonTableGen) WebAssembly uses. (2) MSILTargetMachine.cpp implements the class M2.f declares in MSILTargetMachine.h (registry-tm 7.4, passes-frame 4.1): ctor uses RM.value_or(Reloc::Static) forced to Static, getEffectiveCodeModel(CM, CodeModel::Small); rejects triples that are not MSIL or whose arch is not x86_64/aarch64 with report_fatal_error("MSIL: unsupported target triple '<T>' (Phase 1 supports x86_64 and aarch64)"); Options.TrapUnreachable = true, Options.NoTrapAfterNoreturn = false, Options.EmitAddrsig = false, and this->Options.ExceptionModel = ExceptionHandling::None for every incoming model, before initAsmInfo(): clang's MinGW toolchain passes -exception-model=seh for x86_64/aarch64 even for C (clang/lib/Driver/ToolChains/MinGW.cpp GetExceptionModel, Clang.cpp ~7919-7939) and BackendUtil.cpp:388-389 turns it into WinEH, and C has no unwinding in Phase 1; initAsmInfo(); setEnableTiedFastRegAlloc(false); never setRequiresStructuredCFG; WebAssembly-style per-function subtarget map keyed on the target-cpu and target-features attribute strings; getTargetTransformInfo returns MSILTTIImpl; createMachineFunctionInfo -> MSILFunctionInfo; MIR YAML hooks copied from WebAssembly for yaml::MSILFunctionInfo; createMCStreamer override returns make_error<StringError>("MSIL object file emission is not implemented; use -S (ILAsm text)", inconvertibleErrorCode()) for CodeGenFileType::ObjectFile. MSILPassConfig exactly as passes-frame.md 4.2 (plan 5.3) with MSILPrepareIR first, MSILSetMemFlags after MSILArgumentMove, StackCheck in addPreEmitPass2 unconditionally (no escape hatch), no GlobalISel overrides. LLVMInitializeMSILTarget registers the TM and calls every initializeMSIL*Pass from MSIL.h. (3) MSILTTIImpl : BasicTTIImplBase<MSILTTIImpl> (registry-tm 5.2), final in M2 (query hooks are never stubs, D33): 0 vector registers, vector register width 0, scalar width = pointer size, shouldBuildLookupTables() false and shouldBuildRelLookupTables() false (otherwise SimplifyCFG turns a switch of constants into a private @switch.table global plus GEP and load whenever BR_JT or BRIND is Legal or Custom, BasicTTIImpl.h:644-648, which needs M7 loads and M8 globals and is bigger IL than the CIL switch), no partial/runtime unroll. (4) MSILTargetObjectFile : TargetLoweringObjectFile: SelectSectionForGlobal and getExplicitSectionGlobal return the MSIL text/data/bss/rodata section by SectionKind (explicit sections ignored). (5) MSILPrepareIR (ModulePass, new): strip ssp/sspstrong/sspreq/safestack with a DS_Warning DiagnosticInfoUnsupported ('stack protector is not supported for MSIL; attribute ignored'); DS_Error diagnostics naming the symbol for variadic function definitions ('variadic function definitions are not supported for MSIL yet'), thread_local globals, GlobalAlias, GlobalIFunc and a non-empty llvm.global_dtors. llc and clang keep running after an error (llc checks HasErrors only after PM.run, llvm/tools/llc/lib/llcdriver.cpp:881-884), so after each error PrepareIR neutralizes the construct and later passes never meet it (section 2 diagnostics rule): a diagnosed definition gets F.deleteBody(); an alias or ifunc is replaced by poison of its type in all uses and erased; a thread_local global gets setThreadLocal(false); llvm.global_dtors is erased. Module inline asm is not handled here: the legacy pass manager runs every doInitialization (including the AsmPrinter's) before any pass runs (llvm/lib/IR/LegacyPassManager.cpp MPPassManager::runOnModule), so M2.d diagnoses and clears it. (6) Stubs: each stub pass file defines its legacy pass class (MSIL<X>Legacy), DEBUG_TYPE from section 5.3, INITIALIZE_PASS, the create function, and a run method that returns false, with the first comment line '// STUB: implemented in M<k>'; MSILDebugValueManager.{h,cpp} and MSILDataSegment.{h,cpp} declare empty classes. Syntax-check every .cpp with msil-syntax.ps1 -FlagsFrom llvm/lib/Target/WebAssembly/WebAssemblyTargetMachine.cpp -ExtraInclude llvm/lib/Target/MSIL,<scratch tblgen dir> once M2.b and M2.f have handed over.

#### M2.b TableGen core records, operand definitions and MSILMCTargetDesc.h (implement, ~800 lines)

- Owned files: `llvm/lib/Target/MSIL/MSIL.td`, `llvm/lib/Target/MSIL/MSILRegisterInfo.td`, `llvm/lib/Target/MSIL/MSILInstrFormats.td`, `llvm/lib/Target/MSIL/MSILInstrInfo.td`, `llvm/lib/Target/MSIL/MSILInstrInteger.td`, `llvm/lib/Target/MSIL/MSILInstrFloat.td`, `llvm/lib/Target/MSIL/MSILInstrConv.td`, `llvm/lib/Target/MSIL/MSILInstrMemory.td`, `llvm/lib/Target/MSIL/MSILInstrControl.td`, `llvm/lib/Target/MSIL/MSILInstrCall.td`, `llvm/lib/Target/MSIL/MSILInstrFrame.td`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILMCTargetDesc.h`
- Reads: llvm/docs/MSIL/recon/wasm-inventory.md sections 3.1-3.6; llvm/docs/MSIL/recon/isel.md sections 9, 10; llvm/docs/MSIL/recon/passes-frame.md sections 6.2, 8.2; llvm/lib/Target/WebAssembly/*.td, MCTargetDesc/WebAssemblyMCTargetDesc.h; this plan sections 3 (D1, D35), 5.3-5.4
- Provides: all structural opcode names and operand lists of plan section 5.4 (MSIL::ARGUMENT_I32 ... MSIL::LOCALLOC); BR_TABLE_I32 only (D35); operand defs with their PrintMethods (plan 5.4): local_op/printLocalOperand, arg_op/printArgOperand, f32imm_op/printF32ImmOperand, f64imm_op/printF64ImmOperand, memflags_op/printMemFlagsOperand, plus i32imm_op, i64imm_op, function_op, data_op, bb_op with printOperand; register classes MSIL::I32RegClass, I64RegClass, F32RegClass, F64RegClass; regs MSIL::FP, VALUE_STACK, ARGUMENTS; `MSILII::StackPeakExtraMask, MSILII::TrapsOnlyOnUB`; `enum MSIL::OperandType`; `MSIL::isArgument/isCopy/isTee/isScalarConst/isCallDirect/isCallIndirect, MSIL::UnusedReg`; std::string MSIL::getMSILCPU(StringRef), std::string MSIL::filterMSILFeatures(StringRef) (declared here, defined in M2.c); `SDNodes MSILISD::ARGUMENT, MSILISD::RETURN (generated MSILISD enum via -gen-sd-node-info)`

Single-form TableGen (decision D1, wasm-inventory 3.3-3.5). MSILInstrFormats.td: class MSILInst<bits<16> cilop, string asmstr> : Instruction (Namespace "MSIL", bits<2> StackPeakExtra -> TSFlags{1-0}, bit TrapsOnlyOnUB -> TSFlags{2}); class NI<dag oops, dag iops, list<dag> pattern, string asmstr = "", bits<16> cilop = 0xFFFF> with Defs = [ARGUMENTS]; multiclass I emitting a single def "". Asm strings name only immediate and symbol operands, never registers. MSILRegisterInfo.td per wasm-inventory 3.4. MSIL.td: Target.td, RegisterInfo, InstrInfo, 'defm : RemapAllTargetPseudoPointerOperands<I64>;', ProcessorModel<"generic", NoSchedModel, []>, no features, AsmWriter (AsmWriterClassName "InstPrinter", isMCAsmWriter 1), 'def MSIL : Target' without AssemblyParsers. Operand definitions exactly as the plan 5.4 operand table (the generated MSILGenAsmWriter.inc calls these PrintMethods on M2.c's MSILInstPrinter, so the names are a contract). Structural records with the FINAL names, flags and (outs)/(ins) operand lists of the plan 5.4 record table (later milestones add patterns and records but never rename or reorder these; test WPs write MIR against that table): MSILInstrInfo.td: SDNodes MSILISD::ARGUMENT and MSILISD::RETURN (variadic, chain), ARGUMENT_*, COPY_*, TEE_*, LOCAL_GET/SET/TEE_*, ARG_GET/SET/TEE_*, DROP_*, CONST_I32/I64/F32/F64 (with imm/fpimm patterns), GLOBAL_ADDR, FUNC_ADDR; MSILInstrControl.td: RETURN, TRAP (pattern (trap)), BR, BR_IF, BR_UNLESS, BR_TABLE_I32 (no BR_TABLE_I64: the jump-table index is always i32, decision D35); MSILInstrCall.td: ADJCALLSTACKDOWN/UP (callseq patterns), CALL_PARAMS, CALL_RESULTS (usesCustomInserter), CALL, CALL_INDIRECT (variadicOpsAreDefs, isCall); MSILInstrFrame.td: LOCALLOC; StackPeakExtra=1 on LOCAL_TEE_*, ARG_TEE_*, TRAP, GLOBAL_ADDR; MSILInstrInteger/Float/Conv/Memory.td: header only. Asm strings and CIL opcode values from wasm-inventory 3.6.1 and isel.md 9. MSILMCTargetDesc.h: GET_REGINFO_ENUM / GET_INSTRINFO_ENUM / GET_INSTRINFO_MC_HELPER_DECLS / GET_SUBTARGETINFO_ENUM includes, MSILII masks, MSIL::OperandType, opcode predicates (the subset of WebAssemblyMCTargetDesc.h the copied passes use: isArgument, isCopy, isTee, isScalarConst, isCallDirect, isCallIndirect), 'static const unsigned UnusedReg = -1u', the getMSILCPU/filterMSILFeatures declarations and the create* factory declarations used by M2.c. Run msil-tblgen.ps1 (all six backends must succeed) before hand-over and hand the scratch output directory path to M2.c and M2.f.

#### M2.c MC layer: registration, MCAsmInfo, InstPrinter, target streamer, type utilities, member-ref expression (implement, ~1000 lines)

- Owned files: `llvm/lib/Target/MSIL/MCTargetDesc/MSILMCTargetDesc.cpp`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILMCAsmInfo.h`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILMCAsmInfo.cpp`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILInstPrinter.h`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILInstPrinter.cpp`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILTargetStreamer.h`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILTargetStreamer.cpp`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILMCTypeUtilities.h`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILMCTypeUtilities.cpp`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILMCExpr.h`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILMCExpr.cpp`
- Depends on: M2.b
- Reads: llvm/docs/MSIL/recon/asmprinter.md sections 3, 4, 6.1-6.3, 9; llvm/docs/MSIL/recon/isel.md section 8.1; this plan sections 5.1 and 5.3; llvm/lib/Target/NVPTX/MCTargetDesc/NVPTXMCAsmInfo.cpp, llvm/lib/Target/WebAssembly/MCTargetDesc/WebAssemblyTargetStreamer.{h,cpp}; this plan section 5.4 (operand PrintMethods)
- Provides: `LLVMInitializeMSILTargetMC`; `MSIL::getMSILCPU / MSIL::filterMSILFeatures definitions`; `class MSILMCAsmInfo`; class MSILTargetStreamer / MSILTargetAsmStreamer / MSILTargetNullStreamer (API in plan 5.3); MSIL::ValType, MSIL::CallConv, MSIL::Signature, MSIL::toValType, getValTypeName, printValType, printResult, printParamList, printQuotedName; `class MSILMemberRefExpr (createMethod/createField/createSig/createBCL)`; `class MSILInstPrinter`
- Consumes: MSILMCTargetDesc.h declarations (M2.b)

(1) MSILMCTargetDesc.cpp per asmprinter.md 6.1: register MCAsmInfo, MCInstrInfo, MCRegisterInfo, MCSubtargetInfo (createMSILMCSubtargetInfoImpl(TT, MSIL::getMSILCPU(CPU), MSIL::getMSILCPU(TuneCPU), MSIL::filterMSILFeatures(FS))), MCInstPrinter, asm target streamer and null target streamer; no code emitter, no asm backend. Define getMSILCPU (every CPU name, including "" and the native names x86-64/generic/..., maps to "generic"; "help" is kept) and filterMSILFeatures (returns "" in Phase 1). (2) MSILMCAsmInfo exactly as asmprinter.md 3.2 (CommentString "//", InternalSymbolPrefix "@", AllowAtInName, SupportsQuotedNames=false, no .file/.ident/.type/.size/function alignment, all visibility attrs MCSA_Invalid, GlobalDirective/WeakDirective as comments, SupportsDebugInformation false, ExceptionsType None, UseIntegratedAssembler false). (3) MSILMCTypeUtilities: the API of plan 5.3; printQuotedName implements the verified escapes (always single quotes; ' -> \', backslash -> \134, bytes < 0x20 and 0x7f -> three-digit octal, bytes >= 0x80 raw); getValTypeName: I32 'int32', NativeInt 'native int', I64 'int64', F32 'float32', F64 'float64'; toValType(MVT, PtrBits): i32 -> I32, i64 -> NativeInt when PtrBits == 64 (decision D2), f32, f64, anything else llvm_unreachable. (4) MSILMemberRefExpr : MCTargetExpr per asmprinter.md 6.3 (trivially destructible members; Method prints '<ret> <quoted name>(<params>)', Field prints 'native int <quoted name>', StandAloneSig prints '<ret>(<params>)', BCLMember prints its text). (5) MSILTargetStreamer API of plan 5.3; the Asm implementation writes whole lines with getStreamer().emitRawText() and produces exactly the text of plan 5.1 (header comments, '.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }', ".assembly '<name>' { .ver 0:0:0:0 }", method header/footer, .maxstack, .locals without init, .entrypoint, pinvokeimpl declarations, data image, address fields); the Null implementation does nothing. (6) MSILInstPrinter: TableGen-generated printInstruction; printInst prints the instruction then printAnnotation; printOperand prints immediates in decimal, expressions via MAI.printExpr, and nothing for register operands; custom printers with exactly the PrintMethod names of plan 5.4: printLocalOperand, printArgOperand, printF32ImmOperand ('float32(0x%08X)' of the bit pattern), printF64ImmOperand ('float64(0x%016llX)'), printMemFlagsOperand (prefix lines, M7 defines the encoding: bits 0-2 unaligned N in {1,2,4}, bit 3 volatile). Short forms (ldarg.0, ldc.i4.s, ...) come in M3.

#### M2.d AsmPrinter skeleton, MCInstLower skeleton, IL naming and CodeGen utilities (implement, ~600 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILAsmPrinter.h`, `llvm/lib/Target/MSIL/MSILAsmPrinter.cpp`, `llvm/lib/Target/MSIL/MSILMCInstLower.h`, `llvm/lib/Target/MSIL/MSILMCInstLower.cpp`, `llvm/lib/Target/MSIL/MSILUtilities.h`, `llvm/lib/Target/MSIL/MSILUtilities.cpp`
- Depends on: M2.c, M2.f
- Reads: llvm/docs/MSIL/recon/asmprinter.md sections 3.1, 5, 6.4-6.5; llvm/lib/Target/WebAssembly/WebAssemblyUtilities.{h,cpp}; this plan sections 5.1, 5.3
- Provides: class MSILAsmPrinter with the public API of plan 5.3 (getSignature, internSignature, noteExternFunction, noteExternalSymbol, getTargetStreamer); class MSILMCInstLower { MSILMCInstLower(MCContext&, MSILAsmPrinter&); void lower(const MachineInstr *, MCInst &) const; }; `StringRef MSIL::getILName(const GlobalValue &, const TargetMachine &)`; `MSIL::isChild, MSIL::mayThrow, MSIL::getCalleeOp, MSIL::getCopyOpcodeForRegClass`; `cl::opt<std::string> -msil-assembly-name`
- Consumes: MSILTargetStreamer, MSIL::Signature helpers, MSILMemberRefExpr (M2.c); MSILFunctionInfo, computeSignatureVTs (M2.f)

(1) MSILAsmPrinter final : public AsmPrinter (asmprinter.md 5.1/6.5): pass name "MSIL Assembly Printer"; getTargetStreamer(); the signature services of plan 5.3 implemented now (getSignature uses computeSignatureVTs + MSIL::toValType with the DataLayout pointer size and caches per Function in a DenseMap with signatures in a SpecificBumpPtrAllocator; internSignature uses std::set<MSIL::Signature>; noteExternFunction collects into a MapVector; noteExternalSymbol records name -> signature and asserts agreement); cl::opt<std::string> MSILAssemblyName("msil-assembly-name") with default = sys::path::stem(M.getSourceFileName()), or "module" if empty; doInitialization: if M.getModuleInlineAsm() is non-empty, emit the error 'module-level inline assembly is not supported for MSIL' through M.getContext() and call M.setModuleInlineAsm("") before calling the base, which would otherwise try to emit it (the legacy pass manager initializes the AsmPrinter before any IR pass runs, so MSILPrepareIR cannot remove it first); emitStartOfAsmFile: UTF-8 BOM only if a global value name is non-ASCII, then emitFileHeader, emitAssemblyRefs, emitAssembly; emitLinkage no-op; emitConstantPool asserts the pool is empty; emitJumpTableInfo no-op; emitGlobalVariable: skip llvm.* specials and unused declarations, otherwise emit the error 'global variables are implemented in M8' (temporary); emitGlobalAlias/emitGlobalIFunc diagnose; emitFunctionEntryLabel/emitFunctionBodyStart/emitInstruction: report_fatal_error("MSIL: function bodies are implemented in M3"); runOnMachineFunction: reject prefix/prologue data, call the base, then emitMethodEnd(name); emitEndOfAsmFile: nothing yet; LLVMInitializeMSILAsmPrinter registers RegisterAsmPrinter<MSILAsmPrinter>; INITIALIZE_PASS for initializeMSILAsmPrinterPass. (2) MSILMCInstLower: register operands -> MCOperand::createReg(MCRegister()) placeholders (single form keeps operand positions), immediates, FP immediates as bit patterns (createSFPImm/createDFPImm as WebAssembly), MBB -> MCSymbolRefExpr of MBB->getSymbol(); GlobalAddress/ExternalSymbol -> report_fatal_error naming M4/M8. (3) MSILUtilities: getILName (asmprinter.md 4: IR name via GlobalValue::dropLLVMManglingEscape, never the Mangler, TM.getSymbol name only for unnamed values) plus copies of WebAssemblyUtilities isChild, mayThrow (Phase 1: MI.isCall()), getCalleeOp and getCopyOpcodeForRegClass (COPY_I32/I64/F32/F64), without EH, tables, TLS or multivalue.

#### M2.e Skeleton tests (llc, clang, unit) (test, ~500 lines)

- Owned files: `llvm/test/CodeGen/MSIL/lit.local.cfg`, `llvm/test/CodeGen/MSIL/target-selection.ll`, `llvm/test/CodeGen/MSIL/march-msil.ll`, `llvm/test/CodeGen/MSIL/empty-module.ll`, `llvm/test/CodeGen/MSIL/obj-unsupported.ll`, `llvm/test/CodeGen/MSIL/assembly-name.ll`, `llvm/test/CodeGen/MSIL/unsupported-ir-errors.ll`, `llvm/test/CodeGen/MSIL/stack-protector-warning.ll`, `clang/test/CodeGen/MSIL/empty-tu.c`, `llvm/unittests/Target/MSIL/CMakeLists.txt`, `llvm/unittests/Target/MSIL/MSILNameQuotingTest.cpp`, `llvm/unittests/Target/MSIL/MSILTypeUtilitiesTest.cpp`, `llvm/test/CodeGen/MSIL/tti-hooks.ll`
- Reads: this plan sections 3 (decisions), 5.1 (ILAsm contract), 5.5 (test conventions); llvm/docs/MSIL/recon/tests-ci.md sections 3-4; llvm/docs/MSIL/recon/asmprinter.md sections 4, 12; llvm/docs/MSIL/recon/registry-tm.md section 8; llvm/unittests/Target/WebAssembly/CMakeLists.txt (template); this plan section 5.4 (record operand lists)

From the spec only. lit.local.cfg per tests-ci.md 3.1 (MSIL gate; optional 'ilasm' feature; %ilasm-verify = '"<ilasm>" -nologo -quiet -dll -pe64 -x64'). target-selection.ll: llc with x86_64-unknown-linux-gnu-msil, x86_64-pc-windows-msvc-msil and aarch64-unknown-linux-gnu-msil on a module with x86 'target-cpu'/'target-features' attributes on declarations prints the mscorlib header and no GNU directive (CHECK-NOT regex of asmprinter.md 12.2 excluding '.data cil'), plus 'llc --version' lists msil. march-msil.ll: -march=msil with a native triple selects MSIL; 'not llc -march=x86-64 -mtriple=...-msil' errors. empty-module.ll: exact header lines of plan 5.1 and the '%if ilasm %{ ... %ilasm-verify ... %}' round trip. obj-unsupported.ll: 'not llc -filetype=obj' error text. assembly-name.ll: -msil-assembly-name=foo gives ".assembly 'foo'", default is the source-file stem, a quote in the name is escaped. unsupported-ir-errors.ll (split-file, one 'not llc ... 2>&1 | FileCheck --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"' per case): GlobalAlias (also used by a function), GlobalIFunc, thread_local global, non-empty llvm.global_dtors, variadic definition (also called by another definition), module asm - each diagnostic names the symbol and nothing crashes after it (section 2 diagnostics rule). tti-hooks.ll: 'opt -mtriple=x86_64-pc-windows-msvc-msil -passes="simplifycfg<switch-to-lookup>" -S' on a switch returning constants produces no @switch.table (lookup tables are off, D8), and 'opt -mtriple=x86_64-unknown-linux-gnu-msil -O2 -S' on a loop over an array argument with scaled and offset GEPs completes (TTI getGEPCost and LoopStrengthReduce reach isLegalAddressingMode, which is final since M2, D33). stack-protector-warning.ll: a declaration with sspstrong yields the warning and no error. clang/test/CodeGen/MSIL/empty-tu.c (REQUIRES: msil-registered-target): 'clang --target=<T> -S -o -' on an empty TU contains '.assembly extern mscorlib' for T = x86_64-pc-windows-msvc-msil, x86_64-unknown-linux-gnu-msil and x86_64-w64-windows-gnu-msil (the MinGW driver passes -exception-model=seh; the TargetMachine must force None, D8). Unit tests (add_llvm_target_unittest(MSILTests ...), LLVM_LINK_COMPONENTS CodeGen CodeGenTypes Core MC MIRParser Support Target TargetParser MSILCodeGen MSILDesc MSILInfo): MSILNameQuotingTest (add, int32, value, quote, backslash, tab, 0x01, 0x7f, UTF-8 bytes, '?foo@@YAHXZ', 'a.b$c'); MSILTypeUtilitiesTest (toValType, getValTypeName, printResult/printParamList, Signature equality and ordering).

#### M2.f CodeGen class skeletons: MSIL.h, TargetMachine/Subtarget headers, InstrInfo, RegisterInfo, FrameLowering, ISel, MFI (implement, ~1300 lines (MFI is a mechanical copy))

- Owned files: `llvm/lib/Target/MSIL/MSIL.h`, `llvm/lib/Target/MSIL/MSILTargetMachine.h`, `llvm/lib/Target/MSIL/MSILSubtarget.h`, `llvm/lib/Target/MSIL/MSILSubtarget.cpp`, `llvm/lib/Target/MSIL/MSILInstrInfo.h`, `llvm/lib/Target/MSIL/MSILInstrInfo.cpp`, `llvm/lib/Target/MSIL/MSILRegisterInfo.h`, `llvm/lib/Target/MSIL/MSILRegisterInfo.cpp`, `llvm/lib/Target/MSIL/MSILFrameLowering.h`, `llvm/lib/Target/MSIL/MSILFrameLowering.cpp`, `llvm/lib/Target/MSIL/MSILISelLowering.h`, `llvm/lib/Target/MSIL/MSILISelLowering.cpp`, `llvm/lib/Target/MSIL/MSILISelLoweringCall.cpp`, `llvm/lib/Target/MSIL/MSILISelDAGToDAG.cpp`, `llvm/lib/Target/MSIL/MSILSelectionDAGInfo.h`, `llvm/lib/Target/MSIL/MSILSelectionDAGInfo.cpp`, `llvm/lib/Target/MSIL/MSILMachineFunctionInfo.h`, `llvm/lib/Target/MSIL/MSILMachineFunctionInfo.cpp`
- Depends on: M2.b
- Reads: llvm/docs/MSIL/recon/wasm-inventory.md sections 3.7, 5; llvm/docs/MSIL/recon/isel.md sections 2, 6, 10; llvm/docs/MSIL/recon/passes-frame.md sections 6.2, 8.2; llvm/docs/MSIL/recon/registry-tm.md sections 7.3-7.4; llvm/lib/Target/WebAssembly/WebAssemblyMachineFunctionInfo.{h,cpp}, WebAssemblySubtarget.{h,cpp}, WebAssemblyTargetMachine.h, WebAssemblyISelDAGToDAG.cpp; llvm/lib/Target/NVPTX/NVPTXISelLowering.h (getJumpTableRegTy precedent); this plan sections 2, 3 (D8, D33-D35), 5.3-5.4
- Provides: `MSIL.h (exactly plan 5.3)`; `class MSILTargetMachine declaration (plan 5.3)`; `class MSILSubtarget (plan 5.3)`; `class MSILTargetLowering (full Phase-1 declaration, plan 5.3; query hooks final)`; class MSILInstrInfo, MSILRegisterInfo, MSILFrameLowering, MSILSelectionDAGInfo, MSILISD::CALL (constructors of plan 5.3); class MSILFunctionInfo + yaml::MSILFunctionInfo; computeSignatureVTs; computeLegalValueVTs
- Consumes: MSILGen*.inc from M2.b's .td (msil-tblgen.ps1 output directory); MSILMCTargetDesc.h (M2.b)

(1) MSIL.h: exactly the declarations of plan section 5.3 (legacy PM only, decision D23). MSILTargetMachine.h: the class declaration of plan 5.3 (members are defined by M2.a in MSILTargetMachine.cpp). (2) MSILSubtarget.{h,cpp} (registry-tm 7.3): MSILGenSubtargetInfo(TT, MSIL::getMSILCPU(CPU), MSIL::getMSILCPU(TuneCPU), MSIL::filterMSILFeatures(FS)); keeps NativeCPU/NativeFS; owns MSILInstrInfo, MSILTargetLowering, MSILFrameLowering, MSILSelectionDAGInfo; the getters of plan 5.3; initLibcallLoweringInfo override left empty (M4). (3) MSILInstrInfo: ADJCALLSTACKDOWN/UP, copyPhysReg via COPY_<RC>, isReMaterializableImpl for CONST_*/GLOBAL_ADDR/FUNC_ADDR, 'unsigned getStackPeakExtra(unsigned Opc) const', analyzeBranch returning true (cannot analyze) until M6. MSILRegisterInfo: reserved FP/VALUE_STACK/ARGUMENTS, empty callee-saved list, getFrameRegister returns MSIL::FP, pointer class I64, eliminateFrameIndex stub (report_fatal_error "MSIL: frame indices are implemented in M7"). MSILFrameLowering: StackGrowsDown, Align(16), StackRealignable=true, empty prologue/epilogue, hasFPImpl = stack size != 0 || frame address taken, hasReservedCallFrame true, eliminateCallFramePseudoInstr erases. (4) MSILISelLowering.h: the complete Phase-1 class of plan section 5.3. MSILISelLowering.cpp: ctor (register classes for i32/i64/f32/f64, computeRegisterProperties, ZeroOrOneBooleanContent, setMaxAtomicSizeInBitsSupported(0), ConstantFP Legal). Query hooks are final now and never stubs (D33), because the IR optimizer reaches them through TTI at -O1 and above (getGEPCost calls isLegalAddressingMode, TargetTransformInfoImpl.h ~1423; LoopStrengthReduce and CodeGenPrepare too): getScalarShiftAmountTy -> i32; getSetCCResultType -> i32; isIntDivCheap -> true; allowsMisalignedMemoryAccesses -> true (*Fast = 1); isOffsetFoldingLegal -> !isa<Function>(GA->getGlobal()); isLegalAddressingMode -> '[reg]' only: 'return !AM.BaseGV && AM.BaseOffs == 0 && AM.ScalableOffset == 0 && (AM.Scale == 0 || (AM.Scale == 1 && !AM.HasBaseReg));' (CIL ldind/stind have no offset); getJumpTableRegTy -> MVT::i32 (decision D35, NVPTX precedent NVPTXISelLowering.h:132; SelectionDAGBuilder::visitJumpTableHeader range-checks the full-width value before it truncates, SelectionDAGBuilder.cpp ~3100-3130); convertSelectOfConstantsToMath -> true (decision D34: otherwise DAGCombiner turns sext(setcc) into select(setcc, -1, 0), DAGCombiner.cpp ~16088-16098). Every lowering hook declared in 5.3 (LowerOperation cases, EmitInstrWithCustomInserter, the Lower* helpers) gets a stub body that ends in report_fatal_error("MSIL: <what> is implemented in M<k>"). MSILISelLoweringCall.cpp: stubs for LowerFormalArguments, LowerCall, LowerReturn and emitCallResults; CanLowerReturn final (Outs.size() <= 1). MSILISelDAGToDAG.cpp: MSILDAGToDAGISel + MSILDAGToDAGISelLegacy wrapper + createMSILISelDagLegacyPass, Select() -> SelectCode. MSILSelectionDAGInfo: GET_SDNODE_ENUM from MSILGenSDNodeInfo.inc and 'namespace llvm::MSILISD { enum NodeType : unsigned { CALL = GENERATED_OPCODE_END }; }' (WebAssemblySelectionDAGInfo.h:20-29 pattern). (5) MSILMachineFunctionInfo: copy WebAssemblyMachineFunctionInfo.{h,cpp} as MSILFunctionInfo (Params/Results/Locals as MVT, VRegStackified, WARegs, FrameBaseVreg; drop CFGStackified, VarargVreg, BasePtrVreg, wasm signatures) plus 'unsigned MaxStack' with getMaxStack()/setMaxStack() and YAML field maxStack (YAML keys params, results, maxStack); free functions computeLegalValueVTs (two overloads, the first taking const MSILTargetLowering &) and computeSignatureVTs (wasm signatures; >1 result -> sret pointer param; no varargs); use TM.getSubtarget<MSILSubtarget>(F) so this TU needs only MSILSubtarget.h. Write the headers first and keep them compilable after every edit (section 1.2a); run msil-syntax.ps1 on every .cpp (with -ExtraInclude for M2.b's msil-tblgen output) once M2.b has handed over.

**Build targets:** `(configure -Experimental MSIL)` `llc` `opt` `clang` `FileCheck` `not` `count` `split-file` `llvm-config` `MCTests` `MSILTests`

**Gate commands** (run in order by the gate agent; all must succeed):

```text
 1. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-configure.ps1 -Experimental MSIL
 2. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 llc opt clang FileCheck not count split-file llvm-config MCTests MSILTests
 3. D:/llvm-msil/build/unittests/Target/MSIL/MSILTests.exe
 4. D:/llvm-msil/build/unittests/TargetParser/TargetParserTests.exe --gtest_filter=*MSIL*
 5. D:/llvm-msil/build/unittests/MC/MCTests.exe --gtest_filter=TargetRegistry.*
 6. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" llvm/test/CodeGen/MSIL clang/test/CodeGen/MSIL clang/test/Preprocessor/init-msil.c clang/test/Driver/msil-toolchain.c msil/test
 7. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-gtest.ps1 -Exe D:/llvm-msil/build/unittests/MC/MCTests.exe -Filter 'TargetRegistry.MSIL*' -NoSkip -MinTests 3  (the M1 registry tests now run, none skipped)
 8. pwsh -NoProfile -Command "if (-not ((& D:/llvm-msil/build/bin/llc.exe --version) -match 'msil')) { exit 1 }"  (llc lists msil)
 9. pwsh -NoProfile -Command "Set-Location D:/llvm-msil; git add -A; python 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/git-clang-format' --binary 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe' --extensions c,cpp,h,hpp,hxx,cxx,inc,cppm,cl --diff HEAD -- llvm/lib llvm/include llvm/unittests clang/lib clang/include"  (must print no diff; same extension list as llvm/utils/git/code-format-helper.py, so .td files are never formatted)
10. pwsh -NoProfile -Command "git -C D:/llvm-msil add -A; python D:/llvm-msil/msil/utils/plan-check.py scope M2"  (every staged path is owned by a WP of M2, is a M2 fix-loop amendment in section 12, or is on the allow-list)
```

**Exit criteria**
- The MSIL target configures and builds; llc/opt/clang link with it; llvm-config --targets-built lists MSIL.
- Every *-msil triple selects the MSIL target; -march=msil works; -filetype=obj fails with a clear message.
- An empty module and an empty C translation unit (msvc, linux-gnu and MinGW triples) produce ilasm-assemblable output with exactly the plan 5.1 header.
- No 'is not a recognized processor/feature' warnings for clang-produced x86 attributes.
- All six tablegen backends succeed; every Phase-1 source file exists; query hooks are final (opt -O2 on GEP-heavy IR and simplifycfg on switches work); only lowering hooks are stubs and each fails loudly with its milestone.
- Every unsupported-IR diagnostic ends in exit code 1 with no crash marker.
- M1 registry unit tests now run (none skipped, at least 3) and pass; MSILTests pass.

**Review focus** (in addition to the three standing lenses)
- LLVM target conventions: registration, CMake components, file headers, pass naming, no new-PM leftovers.
- Pipeline order exactly as passes-frame.md 4.2 (plus MSILPrepareIR first, SetMemFlags after ArgumentMove).
- Structural record names, operand lists, PrintMethods and TSFlags match plan 5.4 exactly so later milestones and MIR tests never diverge.
- MCAsmInfo neutralization table (asmprinter.md 3.1): nothing GNU-like can leak; quoting rules exact.
- Subtarget silence for native CPU/features; TTI disables vectorization and both kinds of lookup tables; Reloc::Static; exception model forced to None.
- No query hook is a stub; every diagnostic path leaves well-formed IR/output behind (section 2).
- Tests check the exact contract text and include negative cases.

**Rebuild cost.** Reconfigure (~1 min) + new MSIL target (~80 TUs, 6 tablegen runs) + small TargetSelect users + relink llc/opt/clang/MCTests/MSILTests: ~15-30 min.

### M3. First executable: leaf functions, stackification, .maxstack, entry point

**Goal.** Functions with scalar arguments, constants and returns (no calls or arithmetic yet) compile to correct CIL: RegStackify, ExplicitLocals and MSILStackCheck work, every function prints as a .method with exact .maxstack and .locals, and a generated '<msil>Main' calls main. 'define i32 @main() { ret i32 42 }' through llc -O0 and -O2, and 'int main(void){return 42;}' through clang -O2, run with exit code 42 on .NET 8 (windows-msvc-msil) and on .NET 10 in WSL (linux-gnu-msil). clang -O0 waits for frames (M7): at -O0 windows-msvc gives main a return-value alloca and a store (D32).

**Depends on:** M2.

**Header changes:**
- none

**Work packages**

#### M3.a Stackification passes (copied from WebAssembly) (implement, ~2700 lines copied, ~400 changed)

- Owned files: `llvm/lib/Target/MSIL/MSILRegStackify.cpp`, `llvm/lib/Target/MSIL/MSILExplicitLocals.cpp`, `llvm/lib/Target/MSIL/MSILDebugValueManager.h`, `llvm/lib/Target/MSIL/MSILDebugValueManager.cpp`, `llvm/lib/Target/MSIL/MSILRegColoring.cpp`, `llvm/lib/Target/MSIL/MSILRegNumbering.cpp`, `llvm/lib/Target/MSIL/MSILReplacePhysRegs.cpp`, `llvm/lib/Target/MSIL/MSILOptimizeLiveIntervals.cpp`
- Reads: llvm/docs/MSIL/recon/passes-frame.md sections 3, 5, 7; llvm/docs/MSIL/recon/wasm-inventory.md section 2.2 (COPY rows); llvm/lib/Target/WebAssembly/WebAssembly{RegStackify,ExplicitLocals,DebugValueManager,RegColoring,RegNumbering,ReplacePhysRegs,OptimizeLiveIntervals}.cpp
- Consumes: structural opcodes, MSILII::TrapsOnlyOnUB, MSIL::isArgument/isCopy/isTee/isScalarConst (M2.b); MSIL::isChild, MSIL::getCalleeOp (M2.d); MSILFunctionInfo (M2.f)

Replace the stubs with copies of the WebAssembly files at 8da03434abcc, first comment line 'Derived from llvm/lib/Target/WebAssembly/<File> at 8da03434abcc.' (decision D30), renamed WebAssembly->MSIL, wasm-->msil-, WebAssemblyFunctionInfo->MSILFunctionInfo. Drop the new-PM wrapper classes (D23) and every wasm-only branch (V128, reference types, EXNREF/FUNCREF, GLOBAL_SET/__stack_pointer, Emscripten, EH/catch, multivalue). Keep the rest byte-for-byte where possible. Required MSIL changes: RegStackify never stackifies a def of MSIL::LOCALLOC (next to the ARGUMENT skip, passes-frame 5.5); the DIV/REM/TRUNC opcode list in query() becomes 'MI.getDesc().TSFlags & MSILII::TrapsOnlyOnUB'; tee opcodes are TEE_<RC>. ExplicitLocals: unified numbering with params first (passes-frame 7): when Local < NumParams emit ARG_GET_/ARG_SET_/ARG_TEE_<RC> with the param index, otherwise LOCAL_GET_/LOCAL_SET_/LOCAL_TEE_<RC> with Local - NumParams; MFI Locals holds only CIL locals (MVTs); DROP_<RC> for unused defs; IMPLICIT_DEF handling as WebAssembly (CONST 0). ReplacePhysRegs turns MSIL::FP into a vreg and records FrameBaseVreg. Write the 'msil-*' DEBUG_TYPE names of plan 5.3.

#### M3.b Small MI passes and MSILStackCheck (implement, ~600 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILArgumentMove.cpp`, `llvm/lib/Target/MSIL/MSILCleanCodeAfterTrap.cpp`, `llvm/lib/Target/MSIL/MSILNullifyDebugValueLists.cpp`, `llvm/lib/Target/MSIL/MSILStackCheck.cpp`
- Reads: llvm/docs/MSIL/recon/passes-frame.md sections 8, 9; llvm/docs/MSIL/recon/isel.md section 9 (stack effects); llvm/lib/Target/WebAssembly/WebAssembly{ArgumentMove,CleanCodeAfterTrap,NullifyDebugValueLists}.cpp
- Provides: `MSILFunctionInfo::MaxStack filled for every function`; `fatal internal error 'MSIL stack check failed in '<fn>': <why>: <MI>'`
- Consumes: MSILFunctionInfo::setMaxStack/isVRegStackified/getResults; MSILII::StackPeakExtraMask; MSIL::TRAP, MSIL::LOCALLOC

Copy ArgumentMove, CleanCodeAfterTrap and NullifyDebugValueLists mechanically (D30 header line, no new-PM class). MSILStackCheck exactly as passes-frame.md 8.3: if the last block's last non-debug instruction is not a barrier, append TRAP; per block, depth starts at 0, pops = explicit register uses, pushes = explicit register defs, peak = depth + StackPeakExtra (TSFlags bits 0-1) and max(depth after); fail with reportFatalInternalError on more than one push, underflow, 'ret' with depth != number of results, LOCALLOC with depth != 1, and a non-empty stack at block end; assert every register operand is stackified; store MaxStack in MSILFunctionInfo. Always on (no escape hatch).

#### M3.c ISel for formal arguments, constants and returns (implement, ~700 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILInstrInfo.td`, `llvm/lib/Target/MSIL/MSILInstrControl.td`, `llvm/lib/Target/MSIL/MSILISelLowering.h`, `llvm/lib/Target/MSIL/MSILISelLowering.cpp`, `llvm/lib/Target/MSIL/MSILISelLoweringCall.cpp`, `llvm/lib/Target/MSIL/MSILISelDAGToDAG.cpp`, `llvm/lib/Target/MSIL/MSILInstrInfo.h`, `llvm/lib/Target/MSIL/MSILInstrInfo.cpp`, `llvm/lib/Target/MSIL/MSILMachineFunctionInfo.h`, `llvm/lib/Target/MSIL/MSILMachineFunctionInfo.cpp`
- Reads: llvm/docs/MSIL/recon/isel.md sections 2.1-2.3, 8, 11.1; llvm/docs/MSIL/recon/wasm-inventory.md section 3.7; llvm/lib/Target/WebAssembly/WebAssemblyISelLowering.cpp (LowerFormalArguments ~1645-1711, LowerReturn, CanLowerReturn)
- Provides: MSILFunctionInfo Params/Results recorded for every function (consumed by AsmPrinter signatures)
- Consumes: structural opcodes and MSILISD::ARGUMENT/RETURN (M2.b); computeSignatureVTs (M2.f)

Copy and trim the WebAssembly lowering (isel.md 11.1): LowerFormalArguments creates (MSILISD::ARGUMENT idx) for each In and MFI->addParam(VT), and records results with computeSignatureVTs -> MFI->addResult; isVarArg -> DiagnosticInfoUnsupported 'variadic functions are not supported for MSIL yet' (already caught by MSILPrepareIR; keep as a backstop); byval/inalloca/preallocated/nest params -> DiagnosticInfoUnsupported, after which the parameter is lowered as a plain pointer ARGUMENT so compilation continues to well-formed output (section 2 diagnostics rule; byval arrives in M9); sret is an ordinary pointer param. LowerReturn -> MSILISD::RETURN with at most one value (CanLowerReturn stays Outs.size() <= 1); i1/i8/i16 returns follow the zeroext/signext attributes. Patterns: (MSILISD::RETURN) -> RETURN (variable ops), CONST patterns for imm/fpimm (verify the M2 ones), undef/IMPLICIT_DEF per class. Query hooks are already final since M2 (D33). LowerOperation dispatch skeleton (default report_fatal_error naming the opcode), EmitInstrWithCustomInserter dispatch skeleton. LowerCall keeps a stub message 'calls are implemented in M4'. MSILInstrInfo: copyPhysReg (COPY_<RC>), isReMaterializableImpl. Adjust MSILFunctionInfo only if a field is missing; keep its API (plan 5.3).

#### M3.d Function emission: method headers, instruction printing, entry point (implement, ~800 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILAsmPrinter.h`, `llvm/lib/Target/MSIL/MSILAsmPrinter.cpp`, `llvm/lib/Target/MSIL/MSILMCInstLower.h`, `llvm/lib/Target/MSIL/MSILMCInstLower.cpp`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILInstPrinter.h`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILInstPrinter.cpp`
- Reads: llvm/docs/MSIL/recon/asmprinter.md sections 4, 5, 6.4-6.5, 7.4, 9; this plan section 5.1; llvm/docs/MSIL/recon/cil-runtime.md (e10 startup)
- Consumes: MSILFunctionInfo getParams/getResults/getLocals/getMaxStack; MSILTargetStreamer API (M2.c)

asmprinter.md 5.1/6.5 with the Phase-1 contract of plan 5.1. emitFunctionEntryLabel: TS.emitMethodBegin({getILName(F), &getSignature(F), Public = !F.hasLocalLinkage()}) after asserting the signature equals MFI params/results mapped through toValType; emitFunctionBodyStart: emitMaxStack(MFI->getMaxStack()) and emitLocals(MFI locals mapped through toValType; omitted when empty, never 'init'); emitInstruction: ARGUMENT_* and other non-printing pseudos print nothing, everything else goes through MSILMCInstLower::lower and EmitToStreamer; runOnMachineFunction ends with emitMethodEnd(name). emitEndOfAsmFile: when main is defined emit '<msil>Main'(string[] args) (decision D27): for 'i32 ()' just 'call int32 'main'()' and 'ret'; for 'i32 (i32, ptr)' build argv exactly like asmprinter.md 9 but with [mscorlib]System.Environment::GetCommandLineArgs(), [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int) and ::StringToHGlobalAnsi(string), argv[argc] = NULL, then 'call int32 'main'(int32, native int)'; any other main signature -> error 'unsupported signature for main'. .maxstack of generated bodies is computed by the emitter. MSILInstPrinter::printInst special cases (single place, table-driven): ARG_GET -> ldarg.0-3 / ldarg.s N (N < 256) / ldarg N; ARG_SET -> starg.s / starg; LOCAL_GET/SET -> ldloc/stloc .0-.3 / .s / plain; LOCAL_TEE/ARG_TEE -> 'dup' then the store; DROP -> pop; CONST_I32 -> ldc.i4.m1, ldc.i4.0-8, ldc.i4.s (-128..127), ldc.i4; CONST_I64 -> 'ldc.i4 N' + 'conv.i' when isInt<32>(N) else 'ldc.i8 N' + 'conv.i'; CONST_F32/F64 bit-exact forms; RETURN -> ret; TRAP -> ldnull / throw. Each CIL opcode on its own line, two-space indent.

#### M3.e Tests for M3 (llc, MIR, unit, execution) (test, ~900 lines)

- Owned files: `llvm/test/CodeGen/MSIL/ret.ll`, `llvm/test/CodeGen/MSIL/args.ll`, `llvm/test/CodeGen/MSIL/constants.ll`, `llvm/test/CodeGen/MSIL/maxstack.ll`, `llvm/test/CodeGen/MSIL/entrypoint.ll`, `llvm/test/CodeGen/MSIL/identifier-quoting.ll`, `llvm/test/CodeGen/MSIL/cpu-features-silent.ll`, `llvm/test/CodeGen/MSIL/unused-argument.ll`, `llvm/test/CodeGen/MSIL/explicit-locals.mir`, `llvm/test/CodeGen/MSIL/reg-stackify-basic.mir`, `llvm/test/CodeGen/MSIL/stack-check-falloff.mir`, `llvm/test/CodeGen/MSIL/stack-check-errors.mir`, `llvm/test/CodeGen/MSIL/localloc-root.mir`, `llvm/unittests/Target/MSIL/CMakeLists.txt`, `llvm/unittests/Target/MSIL/MSILStackEffectTest.cpp`, `msil/test/M1/00-smoke/`, `clang/test/CodeGen/MSIL/emit-il.c`
- Reads: this plan sections 3, 5.1, 5.4 (record operand lists and the sample MIR), 5.5; llvm/docs/MSIL/recon/passes-frame.md section 12; llvm/docs/MSIL/recon/isel.md section 9; llvm/test/CodeGen/WebAssembly/explicit-locals.mir, reg-argument.mir, unused-argument.ll (to port)

From the spec only (plan 5.1, 5.4, 5.5 and the recon reports), without reading M3's implementation files. llc tests (both x86_64 triples, -verify-machineinstrs, CHECK-LABEL on '.method {{.*}}'<name>'(', ilasm round trip for full modules): ret.ll (void, i32, i64 -> 'native int' with ldc.i4/ldc.i8 + conv.i, f32/f64 bit-exact incl. -0.0 and NaN payload; i1/i8/i16 returns only as constants, or as arguments that already carry the matching zeroext/signext attribute so no extension instruction is needed: AND and sext_inreg patterns arrive in M4 and M4.d's narrow-ints.ll covers the rest), args.ll (ldarg.0-3, ldarg.s for 5+ params, param types int32/native int/float32/float64, no copies of args into locals), constants.ll (ldc.i4 short forms at all boundaries), maxstack.ll, entrypoint.ll (main() and main(i32, ptr) shapes; 'not llc ... 2>&1 | FileCheck --implicit-check-not="LLVM ERROR"' for an i64 main), identifier-quoting.ll (functions named add, ret, value, int32, a.b$c, ?foo@@YAHXZ, a name with a quote; the ilasm round trip is the main check), cpu-features-silent.ll (functions with clang's x86-64 target-cpu/target-features/tune-cpu attributes: llc stderr is empty), unused-argument.ll (only the WebAssembly cases without calls; M4.d adds the call cases). The WebAssembly implicit-def.ll is all branches and is ported in M6.c. MIR tests (-run-pass=msil-explicit-locals, msil-reg-stackify, msil-stack-check), written against the exact operand lists and the sample of plan 5.4: ARG vs LOCAL numbering, DROP for unused defs, tee, a LOCALLOC def is never stackified (localloc-root.mir), fall-off TRAP appended, and stack-check-errors.mir ('not --crash llc' for an unbalanced block; MSILStackCheck failures are internal errors). Stackified-register state is not serialized in MIR, so StackCheck MIR tests chain '-run-pass=msil-explicit-locals,msil-stack-check' (or start from pre-stackify MIR). MSILStackEffectTest.cpp: for every core opcode, MCInstrDesc register defs <= 1 and StackPeakExtra matches plan 5.4. Execution (msil/test/M1/00-smoke/): return-0.c and return-42.c with %{msil_c_O2} and the first-line comment '-O0 needs frames (M7): at -O0 windows-msvc gives main a return-value alloca (D32)' (M7.d switches them to %{msil_c}); ret-const.ll and ret-argc.ll ('define i32 @main(i32 %argc, ptr %argv)' returns argc -> [exit 1]) with %{msil_ll}, which runs llc at -O0 and -O2; .expected files generated with update-expected.py. clang/test/CodeGen/MSIL/emit-il.c (REQUIRES: msil-registered-target): clang -O2 -ffreestanding -S on 'int main(void){return 42;}' for x86_64-pc-windows-msvc-msil, x86_64-unknown-linux-gnu-msil and x86_64-w64-windows-gnu-msil.

**Build targets:** `llc` `opt` `clang` `FileCheck` `not` `count` `split-file` `llvm-config` `MCTests` `MSILTests`

**Gate commands** (run in order by the gate agent; all must succeed):

```text
 1. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 llc opt clang FileCheck not count split-file llvm-config MCTests MSILTests
 2. D:/llvm-msil/build/unittests/Target/MSIL/MSILTests.exe
 3. D:/llvm-msil/build/unittests/TargetParser/TargetParserTests.exe --gtest_filter=*MSIL*
 4. D:/llvm-msil/build/unittests/MC/MCTests.exe --gtest_filter=TargetRegistry.*
 5. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" llvm/test/CodeGen/MSIL clang/test/CodeGen/MSIL clang/test/Preprocessor/init-msil.c clang/test/Driver/msil-toolchain.c msil/test
 6. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_runner=wsl --param msil_triple=x86_64-unknown-linux-gnu-msil" msil/test/M1
 7. pwsh -NoProfile -Command "Set-Location D:/llvm-msil; git add -A; python 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/git-clang-format' --binary 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe' --extensions c,cpp,h,hpp,hxx,cxx,inc,cppm,cl --diff HEAD -- llvm/lib llvm/include llvm/unittests clang/lib clang/include"  (must print no diff; same extension list as llvm/utils/git/code-format-helper.py, so .td files are never formatted)
 8. pwsh -NoProfile -Command "git -C D:/llvm-msil add -A; python D:/llvm-msil/msil/utils/plan-check.py scope M3"  (every staged path is owned by a WP of M3, is a M3 fix-loop amendment in section 12, or is on the allow-list)
```

**Exit criteria**
- llc and clang -S produce assemblable IL for leaf functions; 00-smoke passes (llc -O0/-O2 for the .ll tests, clang -O2 for the C tests) on .NET 8 (Windows) and .NET 10 (WSL, linux triple).
- jitall and msil-verify (enforced, msil_require_verify=1) pass for every 00-smoke DLL.
- MIR tests prove ARG/LOCAL numbering, the LOCALLOC root rule and every MSILStackCheck failure mode.
- No 'not a recognized processor/feature' warnings for clang-produced attributes; clang -S works for the MinGW triple.
- Copied passes keep the D30 header and differ from WebAssembly only by the listed changes.

**Review focus** (in addition to the three standing lenses)
- CIL correctness: exact .maxstack, ldarg/starg vs ldloc/stloc split, no falling off a method, entry point argv building.
- Diff hygiene of copied passes vs WebAssembly (mechanical renames only plus listed MSIL rules).
- Signature consistency between definition headers and MFI (assertion present), widened param types (narrow-param hazard).
- Tests cover boundaries (short forms), quoting, negative MIR cases; execution on both triples.

**Rebuild cost.** MSIL target TUs only (~20 changed) + relink llc/opt/clang/MSILTests: ~5-15 min.

### M4. Direct calls, integer arithmetic and select

**Goal.** Direct calls with scalar signatures (narrow integers widened to int32, 64-bit values as native int), every integer ALU operation, integer comparisons as values, integer width conversions, SELECT through a branch triangle (D34; generic legalization emits SELECT for variable i128 shifts and min/max), and i128 arithmetic expanded inline (no runtime helpers). Diagnosed calls (variadic, runtime library, mismatched signature) end in exit code 1 without a crash. llc -O2 programs made of noinline helper calls and selects run correctly.

**Depends on:** M3.

**Header changes:**
- none

**Work packages**

#### M4.a Direct calls: LowerCall, CALL split, call results inserter, call-site lowering (implement, ~800 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILInstrCall.td`, `llvm/lib/Target/MSIL/MSILISelLoweringCall.cpp`, `llvm/lib/Target/MSIL/MSILISelDAGToDAG.cpp`, `llvm/lib/Target/MSIL/MSILMCInstLower.h`, `llvm/lib/Target/MSIL/MSILMCInstLower.cpp`
- Reads: llvm/docs/MSIL/recon/isel.md sections 2.4-2.5, 8, 11.1, 11.5; llvm/docs/MSIL/recon/asmprinter.md section 6.4; llvm/lib/Target/WebAssembly/WebAssemblyISelLowering.cpp (LowerCall), WebAssemblyISelDAGToDAG.cpp (CALL split), WebAssemblyMCInstLower.cpp
- Provides: MSILTargetLowering::emitCallResults(MachineInstr&, MachineBasicBlock*) (called by the EmitInstrWithCustomInserter dispatch in M4.b)
- Consumes: MSILAsmPrinter::getSignature / noteExternFunction (M2.d API, made usable by M4.c); MSILISD::CALL (M2.f); MSILMemberRefExpr::createMethod (M2.c)

Copy WebAssembly's LowerCall (isel.md 11.1) without tail calls (IsTailCall = false), Swift, funcref/table or Emscripten paths: build MSILISD::CALL (chain, callee, args...) with TargetGlobalAddress / TargetExternalSymbol callees; variadic calls -> DiagnosticInfoUnsupported 'calls to variadic functions are not supported for MSIL yet' (M9 fixes K&R-typed calls to defined functions before ISel); byval args -> diagnostic (M9); an ExternalSymbol callee -> DiagnosticInfoUnsupported "runtime library call '<name>' is not supported for MSIL yet" (M8 replaces this policy); a direct call whose call-site FunctionType differs from the callee's -> DiagnosticInfoUnsupported 'mismatched call signature' (backstop until FixFunctionBitcasts, M9). After any of these diagnostics LowerCall builds no call node: it returns CLI.Chain and pushes DAG.getUNDEF(In.VT) for every entry of CLI.Ins (section 2 diagnostics rule; the same pattern M7 uses for dynamic alloca), so ISel, the custom inserter and MCInstLower never meet the callee and llc exits 1 without a stack dump. DAGToDAG: split MSILISD::CALL into CALL_PARAMS/CALL_RESULTS exactly as WebAssembly (strip the FUNC_ADDR/EXT_SYM wrappers for direct callees). emitCallResults (custom inserter) recombines into CALL (direct; callee operand index = number of defs) or CALL_INDIRECT (callee moved last; completed in M9). MSILInstrCall.td: callseq patterns and any operand fixes. MSILMCInstLower: MO_GlobalAddress of a Function -> MSILMemberRefExpr::createMethod(getILName(F), &Printer.getSignature(F), Ctx) and Printer.noteExternFunction(F) for declarations; non-function GlobalAddress and ExternalSymbol stay fatal until M8.

#### M4.b Integer ISel, select triangles and legalization (implement, ~1050 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILInstrInteger.td`, `llvm/lib/Target/MSIL/MSILInstrConv.td`, `llvm/lib/Target/MSIL/MSILISelLowering.h`, `llvm/lib/Target/MSIL/MSILISelLowering.cpp`, `llvm/lib/Target/MSIL/MSILSubtarget.h`, `llvm/lib/Target/MSIL/MSILSubtarget.cpp`, `llvm/lib/Target/MSIL/MSILInstrControl.td`
- Reads: llvm/docs/MSIL/recon/wasm-inventory.md sections 3.6.2, 3.6.4 (integer rows), 3.7; llvm/docs/MSIL/recon/isel.md sections 3.1, 5.1, 5.2, 5.4, 6, 7.1, 10.5; llvm/docs/MSIL/recon/isel.md section 11.6 (select triangle)
- Provides: integer opcode names of plan 5.4 (ADD_I32 ... CLT_UN_I64, CONV_I_I32, CONV_U_I32, CONV_I4_I64, CONV_I1_I32, CONV_I2_I32, CONV_U1_I32, CONV_U2_I32); SELECT_I32, SELECT_I64, SELECT_F32, SELECT_F64 ((outs RC:$dst) (ins I32:$cond, RC:$t, RC:$f), usesCustomInserter); MSILTargetLowering::emitSelect
- Consumes: emitCallResults (M4.a)

Records and patterns (single form): ADD/SUB/MUL/AND/OR/XOR/SHL/SHR_S/SHR_U and DIV_S/DIV_U/REM_S/REM_U for I32 and I64 (DIV/REM: hasSideEffects = 1, TrapsOnlyOnUB = 1); shift amounts are I32 for both widths (getScalarShiftAmountTy is i32); keep LLVM's shift masks (never port wasm's mask-stripping patterns); NEG/NOT optional; compares CEQ/CGT/CGT_UN/CLT/CLT_UN for I32 and I64 producing I32, with the negated predicates built from single-opcode MIs at ISel (setne -> CEQ(CEQ(a,b), CONST_I32 0); setle -> CEQ(CGT(a,b),0); and so on, isel.md decision 2); integer conversions in MSILInstrConv.td (integer part only): sext i32->i64 conv.i, zext/anyext conv.u, trunc i64->i32 conv.i4, sext_inreg i8/i16 conv.i1/conv.i2 (I32) and the I64 forms per isel.md 5.4. setOperationAction per isel.md 6 integer rows: i1/i8/i16 promoted; CTLZ/CTTZ/CTPOP/BSWAP/ROTL/ROTR Expand (no BCL intrinsics, decision D16); MULHS/MULHU/SMUL_LOHI/UMUL_LOHI/SDIVREM/UDIVREM Expand; SHL_PARTS/SRA_PARTS/SRL_PARTS Custom via expandShiftParts (LowerShiftParts); SELECT_CC/BR_CC Expand; SETCC Legal for i32/i64; SELECT Legal for i32/i64/f32/f64 through new pseudos in MSILInstrControl.td: SELECT_{I32,I64,F32,F64} with (outs RC:$dst) (ins I32:$cond, RC:$t, RC:$f), pattern (select I32:$cond, RC:$t, RC:$f), usesCustomInserter = 1 (decision D34). emitSelect (isel.md 11.6) splits the block into a triangle: ThisMBB ends with 'BR_IF SinkMBB, $cond' (the taken edge carries $t), an empty FalseMBB falls through to SinkMBB carrying $f, and SinkMBB starts with '$dst = PHI $t, ThisMBB, $f, FalseMBB'; successor lists and the PHI are built explicitly (analyzeBranch still returns true until M6, so nothing may rely on branch analysis). BR_IF and bb_op exist since M2.b, MCInstLower lowers MBB operands since M2.d, and M4.c prints branches and labels. Generic code needs SELECT from here on: expandShiftParts for variable-amount i128 shifts (TargetLowering.cpp ~9449-9457), the SMIN/SMAX/UMIN/UMAX expansions and min/max idioms; convertSelectOfConstantsToMath is true since M2.f, so sext/zext of a compare stays arithmetic. EmitInstrWithCustomInserter dispatches MSIL::CALL_RESULTS to emitCallResults and SELECT_* to emitSelect. MSILSubtarget::initLibcallLoweringInfo marks MUL_I128, MULO_I64 and MULO_I128 unsupported so they expand inline (isel decision 10; confirm the LibcallLoweringInfo API at HEAD).

#### M4.c Call-site printing and signature services (implement, ~300 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILAsmPrinter.h`, `llvm/lib/Target/MSIL/MSILAsmPrinter.cpp`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILInstPrinter.h`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILInstPrinter.cpp`
- Reads: llvm/docs/MSIL/recon/asmprinter.md sections 5.3, 6.3-6.4; this plan section 5.1

Make the M2 signature services production quality: getSignature for declarations and definitions alike, noteExternFunction collecting undefined callees; at emitEndOfAsmFile, if ExternFuncs is non-empty, emit the error "call to external function '<name>' needs P/Invoke support (M8)" (temporary, replaced in M8). InstPrinter: CALL prints 'call <member-ref>' (operand index = number of defs because of variadicOpsAreDefs), CALL_INDIRECT prints 'calli <sig>' (M9 supplies the operand); verify every integer record prints the plan 5.4 mnemonic. Branch printing lands here because select triangles produce the first branches (D34): BR prints 'br <label>', BR_IF 'brtrue <label>', BR_UNLESS 'brfalse <label>', always the long forms; MBB operands print as '@BB<f>_<b>' and the base AsmPrinter's block labels as '@BB<f>_<b>:' (InternalSymbolPrefix '@'); block comments use '//'.

#### M4.d llc tests for calls and integers (test, ~800 lines)

- Owned files: `llvm/test/CodeGen/MSIL/call.ll`, `llvm/test/CodeGen/MSIL/call-signatures.ll`, `llvm/test/CodeGen/MSIL/call-args-order.ll`, `llvm/test/CodeGen/MSIL/int-arith.ll`, `llvm/test/CodeGen/MSIL/int-compare.ll`, `llvm/test/CodeGen/MSIL/int-shifts.ll`, `llvm/test/CodeGen/MSIL/int-conv.ll`, `llvm/test/CodeGen/MSIL/i128.ll`, `llvm/test/CodeGen/MSIL/narrow-ints.ll`, `llvm/test/CodeGen/MSIL/reg-stackify.ll`, `llvm/test/CodeGen/MSIL/div-rem.ll`, `llvm/test/CodeGen/MSIL/call-errors.ll`, `llvm/test/CodeGen/MSIL/maxstack-calls.ll`, `llvm/test/CodeGen/MSIL/select.ll`, `llvm/test/CodeGen/MSIL/unused-argument.ll`
- Reads: this plan sections 2 (diagnostic test convention), 3 (D34), 5.1, 5.4, 5.5; llvm/docs/MSIL/recon/isel.md sections 5.1-5.4, 9, 11.6; llvm/test/CodeGen/WebAssembly/i32.ll, i64.ll, reg-stackify.ll, call.ll, unused-argument.ll (to port)

From the spec only, both x86_64 triples, ilasm round trip for each file. call.ll (direct calls, void calls, result dropped with pop, call tokens equal to the callee's .method signature), call-signatures.ll (i1/i8/i16 widened to int32, i64/ptr as native int, f32/f64), call-args-order.ll, int-arith.ll / int-compare.ll (every predicate, incl. negated forms as 'ceq; ldc.i4.0; ceq'; sext and zext of every compare stay arithmetic, with no brtrue in the output), int-shifts.ll (amount is int32, masks kept), int-conv.ll, i128.ll (add/sub/mul and shifts by constant and by variable amounts inline, no __multi3/__ashlti3/__lshrti3/__ashrti3), select.ll (select of i32/i64/f32/f64 values on integer conditions, since FP compares arrive in M5, and smin/umax become a brtrue triangle with no SELECT pseudo left; both triples; -verify-machineinstrs at -O0 and -O2), unused-argument.ll (add the WebAssembly call cases, e.g. call_something), narrow-ints.ll, reg-stackify.ll (port the WebAssembly test's integer cases: trees, no reordering across side effects, div/rem trap exemption), div-rem.ll, call-errors.ll ('not llc ... 2>&1 | FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"' for a variadic call, a runtime-library call such as i128 sdiv -> __divti3, and a mismatched call signature: each prints its diagnostic and nothing crashes after it), maxstack-calls.ll (12-argument call: .maxstack 12).

#### M4.e Execution tests for calls and integers (test, ~600 lines)

- Owned files: `msil/test/M1/01-int/`
- Reads: this plan section 5.5 and decisions D32, D34, D37; llvm/docs/MSIL/recon/tests-ci.md sections 6.4, 7.1

Straight-line programs plus selects (no branches until M6) with noinline helpers so llc -O2 cannot fold: add-args.ll, int32-ops.ll and int64-ops.ll (each check sets one bit of an 8-bit mask that is returned as the exit code; never more than 8 bits, D37), narrow-ints.ll (signext/zeroext), i128-ops.ll (mul, shifts by constant and by variable amounts, add with carry), select-values.ll (selects of every value type on integer conditions, incl. signed/unsigned min and max), many-args.ll (12 mixed i32/i64 args), call-chain.ll, div-rem.ll (in-range values only; INT_MIN/-1 is UB). C tests at -O2 only (%{msil_c_O2} with the first-line comment '-O0 needs frames (M7)', D32): calls-arith.c using argc-derived inputs, __attribute__((noinline)) helpers and the branch-free 'MSIL_CHECK(acc, bit, cond)' of msil_test.h, ending in 'return acc;' (MSIL_EXPECT is 'if (!(c)) return ...' and needs branches, M6). All with .expected from update-expected.py.

**Build targets:** `llc` `opt` `clang` `FileCheck` `not` `count` `split-file` `llvm-config` `MCTests` `MSILTests`

**Gate commands** (run in order by the gate agent; all must succeed):

```text
 1. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 llc opt clang FileCheck not count split-file llvm-config MCTests MSILTests
 2. D:/llvm-msil/build/unittests/Target/MSIL/MSILTests.exe
 3. D:/llvm-msil/build/unittests/TargetParser/TargetParserTests.exe --gtest_filter=*MSIL*
 4. D:/llvm-msil/build/unittests/MC/MCTests.exe --gtest_filter=TargetRegistry.*
 5. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" llvm/test/CodeGen/MSIL clang/test/CodeGen/MSIL clang/test/Preprocessor/init-msil.c clang/test/Driver/msil-toolchain.c msil/test
 6. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_runner=wsl --param msil_triple=x86_64-unknown-linux-gnu-msil" msil/test/M1
 7. pwsh -NoProfile -Command "Set-Location D:/llvm-msil; git add -A; python 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/git-clang-format' --binary 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe' --extensions c,cpp,h,hpp,hxx,cxx,inc,cppm,cl --diff HEAD -- llvm/lib llvm/include llvm/unittests clang/lib clang/include"  (must print no diff; same extension list as llvm/utils/git/code-format-helper.py, so .td files are never formatted)
 8. pwsh -NoProfile -Command "git -C D:/llvm-msil add -A; python D:/llvm-msil/msil/utils/plan-check.py scope M4"  (every staged path is owned by a WP of M4, is a M4 fix-loop amendment in section 12, or is on the allow-list)
```

**Exit criteria**
- Direct calls, every integer operation and selects compile, assemble, verify and run; 01-int passes on .NET 8 and on WSL .NET 10.
- i128 add/sub/mul and constant and variable shifts never reference a runtime helper.
- Unsupported calls fail with clear diagnostics, exit code 1 and no crash marker.
- sext/zext of a compare stays arithmetic; every select is a brtrue triangle; branches print in long form with '@BB' labels.
- Call-site tokens always equal the callee's method signature.

**Review focus** (in addition to the three standing lenses)
- CIL semantics: sign/zero extension at int32/native int boundaries, shift amount typing, negated compares, div/rem side effects.
- WebAssembly call machinery copied faithfully (CALL_PARAMS/CALL_RESULTS); diagnostics instead of crashes.
- No int64 typing anywhere (decision D2); PEVerify clean except allow-listed codes.
- Test adequacy: boundary values, both triples, -O0/-O2 where possible.
- emitSelect builds correct successor lists and PHIs without relying on analyzeBranch; -verify-machineinstrs clean.

**Rebuild cost.** MSIL target TUs + relink: ~5-15 min.

### M5. Floating point and conversions

**Goal.** f32/f64 arithmetic, FP comparisons as values with exact NaN semantics, int<->fp conversions with correctly rounded uint64->fp on every runtime through a select-free, memory-free sequence (D17), frem and fneg; FP operations that become library calls are diagnosed until M8, and fabs/copysign/bitcasts (through memory) wait for M7. update_llc_test_checks.py learns MSIL.

**Depends on:** M4.

**Header changes:**
- none

**Work packages**

#### M5.a FP and conversion ISel (implement, ~700 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILInstrFloat.td`, `llvm/lib/Target/MSIL/MSILInstrConv.td`, `llvm/lib/Target/MSIL/MSILISelLowering.h`, `llvm/lib/Target/MSIL/MSILISelLowering.cpp`
- Reads: llvm/docs/MSIL/recon/wasm-inventory.md sections 3.6.3-3.6.4; llvm/docs/MSIL/recon/isel.md sections 3.2, 5.2, 5.4, 5.6, 6 (FP rows); llvm/docs/MSIL/recon/cil-runtime.md (e05, e05b conversions); llvm/lib/CodeGen/SelectionDAG/LegalizeDAG.cpp (ExpandLegalINT_TO_FP ~2880-2935) and TargetLowering.cpp (expandUINT_TO_FP ~10340-10383); this plan decisions D16, D17
- Provides: `FP opcode names of plan 5.4 (ADD_F32 ... CONV_U_F64); there is no CONV_R_UN_I64 (D17)`

ADD/SUB/MUL/DIV/REM for F32 and F64 (FREM Legal -> CIL rem, verified equal to fmod), NEG_F32/F64 (fneg -> neg), CEQ/CGT/CGT_UN/CLT/CLT_UN for F32/F64 with the ordered/unordered predicate mapping of isel.md 5.2 (SETO, SETUO, SETONE, SETUEQ Expand); conversions: fpext conv.r8, fpround conv.r4, sint_to_fp from I32/I64 conv.r4/conv.r8, uint_to_fp from I32 via conv.r.un (+ conv.r4 for f32; exact for 32-bit inputs), fp_to_sint/fp_to_uint conv.i4/conv.u4/conv.i/conv.u (out of range is poison), FP_TO_SINT_SAT/FP_TO_UINT_SAT Expand. uint_to_fp from I64 Custom for both f32 and f64 results: LowerUINT_TO_FP builds the sequence of decision D17 and never returns SDValue(): s = (srl x, 63); y = (or (srl x, (trunc s to i32)), (and x, s)); r = (sint_to_fp y); result = (fmul r, (sint_to_fp (add s, 1))). For x < 2^63 this is sint_to_fp x times 1; otherwise y is x/2 with the lost bit kept as a sticky bit and the multiply by 2 is exact, so the single rounding in sint_to_fp gives the correctly rounded result (compiler-rt's algorithm; its branchy form passed on every runtime, cil-runtime e05b). Do not fall back to the generic expansions: for f64 they BITCAST i64 to f64 through a stack slot (TargetLowering.cpp ~10379-10380, a frame index before M7 and a localloc frame in every function after), for f32 they build a SELECT, and conv.r.un double-rounds on .NET 8/9 and .NET Framework (cil-runtime e05). Expand: FMA, FSQRT, FSIN, FCOS, FPOW, FEXP*, FLOG*, FCEIL, FFLOOR, FTRUNC, FRINT, FNEARBYINT, FROUND, FROUNDEVEN, FMINNUM/FMAXNUM/FMINIMUM/FMAXIMUM (library calls: LowerCall diagnoses them and builds no call until M8 makes them math-library P/Invokes, D14); FABS, FCOPYSIGN and BITCAST f32<->i32 / f64<->i64 (generic expansions through a stack slot: they hit the M2 frame-index stub until M7, so M5 tests must not use them, plan 5.5); f16/bf16 are unsupported in Phase 1: their promotions call __extendhfsf2/__truncsfhf2, which LowerCall diagnoses without building a call, so the result is a diagnostic and exit code 1, never a crash.

#### M5.b update_llc_test_checks support for MSIL (infra, ~300 lines)

- Owned files: `llvm/utils/UpdateTestChecks/asm.py`, `llvm/utils/update_llc_test_checks.py`, `llvm/test/tools/UpdateTestChecks/update_llc_test_checks/msil-basic.test`, `llvm/test/tools/UpdateTestChecks/update_llc_test_checks/Inputs/msil-basic.ll`, `llvm/test/tools/UpdateTestChecks/update_llc_test_checks/Inputs/msil-basic.ll.expected`, `msil/utils/run-utc-tests.py`
- Reads: llvm/docs/MSIL/recon/tests-ci.md section 3.4; this plan section 5.1 (method header/footer); llvm/test/tools/UpdateTestChecks/update_llc_test_checks/lit.local.cfg; llvm/test/tools/UpdateTestChecks/lit.local.cfg (the 'llc' probe) and update_llc_test_checks/nvptx-basic.test (template)
- Provides: python msil/utils/run-utc-tests.py --llc <llc.exe> <test-name>...: runs simple update_llc_test_checks .test files directly (exit 1 on any difference)

tests-ci.md 3.4 adapted to the plan 5.1 contract: ASM_FUNCTION_MSIL_RE matches a one-line '.method ... '<name>'(' header through '} // end of method '<name>''; scrub_asm_msil; get_run_handler returns the MSIL handler when the triple ends with '-msil' before the arch-prefix table; add_checks gains an optional check_label_format (default unchanged) and update_llc_test_checks.py passes MSIL_CHECK_LABEL_FORMAT ('.method {{.*}}'<name>'(') for prefixes whose RUN triple ends in -msil. msil-basic.test mirrors nvptx-basic.test: '# REQUIRES: msil-registered-target', 'cp -f %S/Inputs/msil-basic.ll %t.ll && %update_llc_test_checks %t.ll', 'diff -u %S/Inputs/msil-basic.ll.expected %t.ll'. It never runs on this Windows host: llvm/test/tools/UpdateTestChecks/lit.local.cfg sets the llc-binary feature only when a file named exactly 'llc' exists in the tools directory (it is llc.exe here), so the whole directory is UNSUPPORTED; do not change that upstream file. Instead write msil/utils/run-utc-tests.py: for each named test of llvm/test/tools/UpdateTestChecks/update_llc_test_checks it reads the two RUN lines of the 'cp -f %S/Inputs/<f> %t.ll && %update_llc_test_checks %t.ll' / 'diff -u %S/Inputs/<f>.expected %t.ll' form (anything else is an error), copies the input into a scratch directory, runs 'python llvm/utils/update_llc_test_checks.py --llc-binary <llc> --version=1 <copy>' (the arguments lit.local.cfg would add) and compares the result with the .expected file ignoring CR at end of line; it prints a unified diff and exits 1 on any difference or error. The gate runs it on msil-basic and on the upstream x86-basic as the regression check of the asm.py change. No behaviour change for other targets.

#### M5.c llc tests for FP (test, ~600 lines)

- Owned files: `llvm/test/CodeGen/MSIL/fp-arith.ll`, `llvm/test/CodeGen/MSIL/fp-compare.ll`, `llvm/test/CodeGen/MSIL/fp-conv.ll`, `llvm/test/CodeGen/MSIL/u64-to-fp.ll`, `llvm/test/CodeGen/MSIL/fp-const.ll`, `llvm/test/CodeGen/MSIL/frem.ll`, `llvm/test/CodeGen/MSIL/fp-expand.ll`
- Reads: this plan sections 3, 5.1, 5.4; llvm/docs/MSIL/recon/isel.md sections 3.2, 5.2, 5.4, 5.6; llvm/test/CodeGen/WebAssembly/f32.ll, f64.ll, comparisons-f32.ll, conv.ll; this plan sections 2 (diagnostic test convention) and 5.5

From the spec only, both triples, ilasm round trip. Every fcmp predicate as a value (ordered vs unordered forms), every conversion (u64->f32/f64 in u64-to-fp.ll is the D17 sequence: shr.un, and, or, conv.r4/conv.r8, mul; no conv.r.un on a 64-bit operand, no localloc, no brtrue), fneg, frem, FP constants (inf, -inf, NaN payload, -0.0, denormals) printed bit-exact, and fp-expand.ll proving that fma, sqrt, floor, ceil, trunc, rint, round, fmin and fmax become library calls that are diagnosed until M8, and that f16 arithmetic likewise ends in a diagnostic: 'not llc ... 2>&1 | FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"' (fabs, copysign and FP bitcasts go through memory and are tested in M7.c).

#### M5.d Execution tests for FP (test, ~500 lines)

- Owned files: `msil/test/M1/02-fp/`
- Reads: this plan section 5.5 and decisions D17, D37; llvm/docs/MSIL/recon/isel.md section 3.2; llvm/docs/MSIL/recon/cil-runtime.md (e05, e05b, e22)

Straight-line .ll programs with noinline helpers (8-bit exit-code masks, D37) and no bitcast, fabs or copysign (they need frames until M7, plan 5.5: produce -0.0 as 0.0 * -1.0 or 1.0 / -inf, produce NaN as 0.0 / 0.0, and test NaN with 'fcmp uno'): fp-ops.ll, fp-compare-nan.ll (all predicates with NaN operands), conversions.ll (in-range values, plus the unsigned high range that is in range natively: fptoui f64 and f32 to i64 for 2^63 and 1.8e19, and to i32 for 3e9 and 4e9; conv.u/conv.u8/conv.u4 were verified on every runtime, isel.md 3.2), u64-to-fp-rounding.ll (hard cases 0x8000008000000001, 0x8000000000000401, 0xFFFFFFFFFFFFFFFF, 2^53+1 to f64 and to f32; must be correct on .NET 8 and .NET Framework, where conv.r.un double-rounds), fp32-precision.ll (float sums keep single precision), frem.ll (fmod cases incl. -0.0 and inf). Expected files from the native oracle.

**Build targets:** `llc` `opt` `clang` `FileCheck` `not` `count` `split-file` `llvm-config` `MCTests` `MSILTests`

**Gate commands** (run in order by the gate agent; all must succeed):

```text
 1. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 llc opt clang FileCheck not count split-file llvm-config MCTests MSILTests
 2. D:/llvm-msil/build/unittests/Target/MSIL/MSILTests.exe
 3. D:/llvm-msil/build/unittests/TargetParser/TargetParserTests.exe --gtest_filter=*MSIL*
 4. D:/llvm-msil/build/unittests/MC/MCTests.exe --gtest_filter=TargetRegistry.*
 5. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" llvm/test/CodeGen/MSIL clang/test/CodeGen/MSIL clang/test/Preprocessor/init-msil.c clang/test/Driver/msil-toolchain.c msil/test
 6. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_runner=wsl --param msil_triple=x86_64-unknown-linux-gnu-msil" msil/test/M1
 7. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_dotnet_runtime=10" msil/test/M1/02-fp
 8. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_run_netfx=1" msil/test/M1/02-fp
 9. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_dotnet_env=DOTNET_TieredCompilation=0" msil/test/M1/02-fp  (optimizing JIT on .NET 8, D38)
10. python D:/llvm-msil/msil/utils/run-utc-tests.py --llc D:/llvm-msil/build/bin/llc.exe msil-basic x86-basic  (lit reports this directory UNSUPPORTED on Windows)
11. pwsh -NoProfile -Command "Set-Location D:/llvm-msil; git add -A; python 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/git-clang-format' --binary 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe' --extensions c,cpp,h,hpp,hxx,cxx,inc,cppm,cl --diff HEAD -- llvm/lib llvm/include llvm/unittests clang/lib clang/include"  (must print no diff; same extension list as llvm/utils/git/code-format-helper.py, so .td files are never formatted)
12. pwsh -NoProfile -Command "git -C D:/llvm-msil add -A; python D:/llvm-msil/msil/utils/plan-check.py scope M5"  (every staged path is owned by a WP of M5, is a M5 fix-loop amendment in section 12, or is on the allow-list)
```

**Exit criteria**
- FP arithmetic, comparisons and conversions are correct on .NET 8 (tiered and TieredCompilation=0), .NET 10, .NET Framework 4.8 and WSL .NET 10.
- uint64 -> fp is correctly rounded everywhere without conv.r.un on 64-bit inputs, without SELECT and without a frame.
- Library-call FP operations and f16 end in diagnostics (exit code 1, no crash marker).
- run-utc-tests.py regenerates msil-basic.ll.expected and the upstream x86-basic.ll.expected exactly.

**Review focus** (in addition to the three standing lenses)
- NaN/ordered semantics of every predicate; rounding of every conversion; behaviour differences between runtimes.
- No crash path for unsupported FP types or operations (diagnostics only).
- asm.py change is isolated and upstream-style.

**Rebuild cost.** MSIL target TUs + relink: ~5-15 min.

### M6. Control flow

**Goal.** Branches, loops, fused compare-and-branch, switch through the CIL switch opcode on an int32 index (D35), unreachable/trap, and branch analysis for the generic MI passes. First C programs with loops, switch and recursion run at -O2; msil-verify is clean for all of them.

**Depends on:** M5.

**Header changes:**
- none

**Work packages**

#### M6.a Control-flow ISel and branch analysis (implement, ~900 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILInstrControl.td`, `llvm/lib/Target/MSIL/MSILISelLowering.h`, `llvm/lib/Target/MSIL/MSILISelLowering.cpp`, `llvm/lib/Target/MSIL/MSILInstrInfo.h`, `llvm/lib/Target/MSIL/MSILInstrInfo.cpp`
- Reads: llvm/docs/MSIL/recon/isel.md sections 2.8-2.10, 5.3, 10.7, 11.2, 11.6, 11.7; llvm/docs/MSIL/recon/passes-frame.md sections 5.3-5.4; llvm/lib/Target/WebAssembly/WebAssemblyInstrInfo.cpp (analyzeBranch etc.); this plan decisions D34, D35
- Provides: opcodes BEQ_*, BNE_UN_*, BLT_*, BLT_UN_*, BLE_*, BLE_UN_*, BGT_*, BGT_UN_*, BGE_*, BGE_UN_* for I32/I64/F32/F64; `MSILISD::BR_TABLE`

Patterns: br -> BR; brcond on an i32 condition -> BR_IF (brtrue) / BR_UNLESS (brfalse); fused brcond(setcc) -> B{EQ,NE_UN,LT,LT_UN,LE,LE_UN,GT,GT_UN,GE,GE_UN}_{I32,I64,F32,F64} with the verified integer and FP mapping (isel.md 5.3; FP blt = OLT, blt.un = ULT, bne.un = UNE, and so on). BR_JT Custom (LowerBR_JT) -> MSILISD::BR_TABLE(index, MBBs..., default) -> BR_TABLE_I32. The index is already i32 because getJumpTableRegTy returns i32 since M2.f (decision D35): SelectionDAGBuilder range-checks the full-width value and only then truncates, so a 64-bit switch value prints as 'conv.i4' followed by 'switch'. CIL switch on a native int fails ILVerify with StackUnexpected (review experiment artifacts/review-cil/sw.il), which is why there is no BR_TABLE_I64. Never remove LLVM's range check (no WebAssemblyFixBrTableDefaults-style pass); an index outside the table is only possible when the default is unreachable, which is UB. unreachable becomes TRAP (TrapUnreachable is set); debugtrap also TRAP in Phase 1; indirectbr is handled by IndirectBrExpand (already in the pipeline). SELECT_* and emitSelect exist since M4.b; keep them and the CALL_RESULTS dispatch unchanged. MSILInstrInfo: analyzeBranch, insertBranch, removeBranch and reverseBranchCondition as WebAssembly minus the CFGStackified check, extended to the fused branches with the FP-specific inverse table of isel.md 11.7 (return true when an FP condition cannot be reversed exactly).

#### M6.b Control-flow printing (implement, ~250 lines)

- Owned files: `llvm/lib/Target/MSIL/MCTargetDesc/MSILInstPrinter.h`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILInstPrinter.cpp`, `llvm/lib/Target/MSIL/MSILMCInstLower.h`, `llvm/lib/Target/MSIL/MSILMCInstLower.cpp`
- Reads: llvm/docs/MSIL/recon/asmprinter.md sections 4 (labels), 6.4; this plan section 5.1

BR, BR_IF, BR_UNLESS, MBB operands and block labels print since M4.c. Add: BR_TABLE_I32 prints 'switch (@BB0_1, @BB0_2, ...)' then 'br @BB0_<default>' on the next line; every fused branch prints its long form (beq, bne.un, blt, blt.un, ble, ble.un, bgt, bgt.un, bge, bge.un), never '.s'. Check that jump-table MBB operands lower through MSILMCInstLower like other MBB operands and that no jump-table data is ever emitted (emitJumpTableInfo stays a no-op).

#### M6.c llc tests for control flow (test, ~700 lines)

- Owned files: `llvm/test/CodeGen/MSIL/br.ll`, `llvm/test/CodeGen/MSIL/brcond-fused.ll`, `llvm/test/CodeGen/MSIL/brcond-fp.ll`, `llvm/test/CodeGen/MSIL/switch.ll`, `llvm/test/CodeGen/MSIL/switch-i64.ll`, `llvm/test/CodeGen/MSIL/switch-unreachable-default.ll`, `llvm/test/CodeGen/MSIL/unreachable.ll`, `llvm/test/CodeGen/MSIL/irreducible.ll`, `llvm/test/CodeGen/MSIL/indirectbr.ll`, `llvm/test/CodeGen/MSIL/loop-phi.ll`, `llvm/test/CodeGen/MSIL/implicit-def.ll`
- Reads: this plan sections 3 (D35), 5.1, 5.4; llvm/docs/MSIL/recon/isel.md sections 5.3, 11.6-11.7; llvm/test/CodeGen/WebAssembly/switch.ll, switch-unreachable-default.ll, unreachable.ll, implicit-def.ll (to port)

From the spec only, both triples, ilasm round trip. Long branch forms only, '@BB' labels, switch followed by br; switch.ll and switch-i64.ll check that a 64-bit switch value is range-checked at full width and then converted with 'conv.i4' before 'switch', and that an i32 switch value goes straight to 'switch' (D35); switch-unreachable-default.ll keeps a well-formed switch; fused FP branches with NaN-correct mnemonics; unreachable after noreturn calls ends in 'ldnull; throw'; every method ends in ret/br/throw; irreducible CFGs accepted; implicit-def.ll (port of the WebAssembly test, which is all branches); loop-phi.ll; -verify-machineinstrs clean at -O0 and -O2.

#### M6.d Execution tests for control flow (test, ~700 lines)

- Owned files: `msil/test/M1/03-control/`
- Reads: this plan section 5.5 and decisions D32, D35, D37; llvm/docs/MSIL/recon/passes-frame.md section 10 (e2, e3); llvm/docs/MSIL/recon/tests-ci.md section 7.2

.ll: loops.ll, nested-loops.ll, switch-dense.ll, switch-sparse.ll, switch-i64.ll (case 2^32+1 must go to default), irreducible.ll, fp-branches-nan.ll, trap.ll (explicit RUN lines, no oracle, expects '[unhandled exception: System.NullReferenceException]'). C at -O2 only (%{msil_c_O2}, first-line comment '-O0 needs frames (M7)'), inputs derived from argc, MSIL_EXPECT allowed from now on: loops.c, switch.c, recursion.c (fib), ackermann.c, collatz.c. In switch.c every case calls a different __attribute__((noinline)) function or updates a scalar by a different amount, with a comment saying why: a switch that only returns constants is a lookup-table candidate (tables are disabled by TTI, D8, but the test must not depend on that) and needs M7 loads and M8 globals. No local arrays or address-taken locals (frames are M7).

**Build targets:** `llc` `opt` `clang` `FileCheck` `not` `count` `split-file` `llvm-config` `MCTests` `MSILTests`

**Gate commands** (run in order by the gate agent; all must succeed):

```text
 1. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 llc opt clang FileCheck not count split-file llvm-config MCTests MSILTests
 2. D:/llvm-msil/build/unittests/Target/MSIL/MSILTests.exe
 3. D:/llvm-msil/build/unittests/TargetParser/TargetParserTests.exe --gtest_filter=*MSIL*
 4. D:/llvm-msil/build/unittests/MC/MCTests.exe --gtest_filter=TargetRegistry.*
 5. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" llvm/test/CodeGen/MSIL clang/test/CodeGen/MSIL clang/test/Preprocessor/init-msil.c clang/test/Driver/msil-toolchain.c msil/test
 6. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_runner=wsl --param msil_triple=x86_64-unknown-linux-gnu-msil" msil/test/M1
 7. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_run_netfx=1" msil/test/M1/03-control
 8. pwsh -NoProfile -Command "Set-Location D:/llvm-msil; git add -A; python 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/git-clang-format' --binary 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe' --extensions c,cpp,h,hpp,hxx,cxx,inc,cppm,cl --diff HEAD -- llvm/lib llvm/include llvm/unittests clang/lib clang/include"  (must print no diff; same extension list as llvm/utils/git/code-format-helper.py, so .td files are never formatted)
 9. pwsh -NoProfile -Command "git -C D:/llvm-msil add -A; python D:/llvm-msil/msil/utils/plan-check.py scope M6"  (every staged path is owned by a WP of M6, is a M6 fix-loop amendment in section 12, or is on the allow-list)
```

**Exit criteria**
- All control-flow shapes run correctly on .NET 8, WSL .NET 10 and .NET Framework; jitall never reports fall-off or stack errors.
- Every switch prints an int32 index (conv.i4 for 64-bit values) and msil-verify is clean for 03-control at -O2.
- Branch analysis lets the generic passes (branch folding, tail duplication) run with -verify-machineinstrs clean.
- Recursive C programs (fib, ackermann) run at -O2.

**Review focus** (in addition to the three standing lenses)
- Stack empty at every block boundary (RegStackify invariant) and StackCheck coverage.
- FP branch inversion correctness (isel 11.7); switch semantics for negative/large indices; LLVM's range check never removed.
- No structured-CFG assumptions left from WebAssembly.

**Rebuild cost.** MSIL target TUs + relink: ~5-15 min.

### M7. Memory, static frames and fences

**Goal.** Loads and stores of every scalar width with unaligned./volatile. prefixes, the localloc static frame with FP-relative frame indices and over-alignment above 8 bytes (D13), memcpy/memset/memmove lowering, fabs/copysign and bitcasts through memory, and compiler/system fences (D36); dynamic alloca diagnosed. C programs run at -O0, so every earlier C test switches to all optimization levels.

**Depends on:** M6.

**Header changes:**
- none

**Work packages**

#### M7.a Loads, stores, memory flags, memory intrinsics and fences (implement, ~800 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILInstrMemory.td`, `llvm/lib/Target/MSIL/MSILSetMemFlags.cpp`, `llvm/lib/Target/MSIL/MSILSelectionDAGInfo.h`, `llvm/lib/Target/MSIL/MSILSelectionDAGInfo.cpp`
- Reads: llvm/docs/MSIL/recon/wasm-inventory.md section 3.6.5; llvm/docs/MSIL/recon/isel.md sections 2.14-2.15, 5.5, 10.6, 11.3; llvm/docs/MSIL/recon/cil-runtime.md (e04 memory, e19 memmove); llvm/lib/Target/WebAssembly/WebAssemblySetP2AlignOperands.cpp; llvm/lib/Target/AArch64/AArch64InstrAtomics.td:17-19 (atomic_fence patterns); llvm/lib/Target/WebAssembly/WebAssemblyRegStackify.cpp query() ~172-250; this plan decisions D15, D21, D36
- Provides: opcodes LDIND_I1/U1/I2/U2/I4/I/R4/R8, STIND_I1/I2/I4/I/R4/R8, CPBLK, INITBLK, MEMMOVE; memflags encoding (bits 0-2 unaligned N, bit 3 volatile); `MSILISD::CPBLK, MSILISD::INITBLK, MSILISD::MEMMOVE`; MEMORY_BARRIER (call void [mscorlib]System.Threading.Interlocked::MemoryBarrier()); atomic_fence patterns (singlethread -> generic MEMBARRIER)

Records: LDIND_* and STIND_* with a leading memflags_op immediate then the address (I64) [then the value]; no offset operand. Patterns: i32 loads (ldind.i4), sext/zext i8/i16 extloads to i32 (ldind.i1/u1/i2/u2), i64 and pointer loads ldind.i, f32/f64 ldind.r4/r8; stores stind.*, truncstores i32->i8/i16 (stind.i1/i2). i64 extloads, i64 truncstores and f64<-f32 extloads are Expand (set in M7.b's ctor; coordinate by name). MSILSetMemFlags (copy the structure of WebAssemblySetP2AlignOperands): after ISel, for every LDIND/STIND/CPBLK/INITBLK with a MachineMemOperand set the memflags immediate: 'unaligned. N' with N = largest of 1/2/4 not above the access alignment when alignment < access size, and the volatile bit when the memoperand is volatile; the InstPrinter (printMemFlagsOperand from M2.c) prints the prefixes on their own lines. MSILSelectionDAGInfo (decision D15, never return SDValue()): memcpy with a constant length <= UINT32_MAX -> MSILISD::CPBLK(dst, src, len:i32); other memcpy and every memmove -> MSILISD::MEMMOVE(src, dst, len:i64) printed as 'conv.i8', 'dup', 'call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64)' (StackPeakExtra 1); memset -> MSILISD::INITBLK(dst, val:i32, len:i32), a variable i64 length truncated with conv.i4 (documented 4 GiB limit). Atomic loads/stores stay unsupported (max atomic size 0). Fences (decision D36; clang turns 'asm volatile("" ::: "memory")' into 'fence syncscope("singlethread") seq_cst', M1.d, and ISD::ATOMIC_FENCE has no default handling: Legal without a pattern is 'Cannot select', Expand is a __sync_synchronize libcall): in MSILInstrMemory.td add 'def : Pat<(atomic_fence (timm), 0), (MEMBARRIER)>;' (the AArch64 precedent; the generic TargetOpcode::MEMBARRIER has hasSideEffects and no memoperands, so the copied RegStackify query() treats it as Write+Effects and never moves a load or store across it, and the base AsmPrinter prints it as the comment '// MEMBARRIER') and a MEMORY_BARRIER record printing 'call void [mscorlib]System.Threading.Interlocked::MemoryBarrier()' (hasSideEffects, mayLoad, mayStore) selected by 'def : Pat<(atomic_fence (timm), (timm)), (MEMORY_BARRIER)>;' for every other scope; verify the pattern order (the scope-0 pattern must win; add AddedComplexity if needed) with msil-tblgen.ps1.

#### M7.b Frame lowering and frame indices (implement, ~800 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILFrameLowering.h`, `llvm/lib/Target/MSIL/MSILFrameLowering.cpp`, `llvm/lib/Target/MSIL/MSILRegisterInfo.h`, `llvm/lib/Target/MSIL/MSILRegisterInfo.cpp`, `llvm/lib/Target/MSIL/MSILInstrFrame.td`, `llvm/lib/Target/MSIL/MSILISelLowering.h`, `llvm/lib/Target/MSIL/MSILISelLowering.cpp`, `llvm/lib/Target/MSIL/MSILISelDAGToDAG.cpp`
- Reads: llvm/docs/MSIL/recon/passes-frame.md section 6; llvm/docs/MSIL/recon/isel.md section 2.6; llvm/lib/Target/WebAssembly/WebAssemblyFrameLowering.cpp, WebAssemblyRegisterInfo.cpp (eliminateFrameIndex); llvm/docs/MSIL/recon/artifacts/review-cil/align16.il (constant-size localloc alignment) and this plan decision D13
- Consumes: ADD_I64, AND_I64, CONST_I32, CONST_I64 (M4); MSILFunctionInfo::setFrameBaseVreg/getFrameBaseVreg/isFrameBaseVirtual

passes-frame.md 6.1-6.9 with decision D13: FrameIndex -> TargetFrameIndex (LowerFrameIndex) and its selection as an I64 value; emitPrologue inserts, right after the ARGUMENT_* block, 'CONST_I32 <frame size>' and 'FP = LOCALLOC' (the only frame instructions; behind one helper so the frame model can change later), with the verified over-allocate-and-align-up sequence whenever MaxAlign > getLocallocAlign(), which returns Align(8) for every Phase-1 triple (decision D13: constant-size localloc of 32 bytes or less is only 8-byte aligned on .NET Framework 4.8 and in .NET 8 tiered code, and 8 bytes is only 8-aligned on .NET 8/10 with TieredCompilation=0, review experiment artifacts/review-cil/align16.il; the recon measured only variable sizes): over-allocate MaxAlign - 8 bytes and align up, still inside the single prologue helper; emitEpilogue empty; hasFPImpl = stack size != 0 || frame address taken; eliminateFrameIndex: Offset = StackSize + ObjectOffset; FP+0 uses FP directly, otherwise a new vreg = ADD_I64(FP, CONST_I64 off), folding into an existing single-use ADD + CONST where possible; getFrameRegister returns the frame-base vreg once ReplacePhysRegs made it virtual. DYNAMIC_STACKALLOC, STACKSAVE, STACKRESTORE and llvm.frameaddress -> DiagnosticInfoUnsupported ('dynamic alloca / VLA is not supported for MSIL yet (phase 3 shadow stack)') returning a well-formed dummy value. TargetLowering (isLegalAddressingMode is final since M2): load/store actions: i64 extloads Expand, i64 truncstores Expand, f64<-f32 extload Expand. Run the WebAssembly stack-alignment ideas against plan 5.1 output.

#### M7.c llc and MIR tests for memory and frames (test, ~700 lines)

- Owned files: `llvm/test/CodeGen/MSIL/load-store.ll`, `llvm/test/CodeGen/MSIL/load-store-unaligned.ll`, `llvm/test/CodeGen/MSIL/volatile.ll`, `llvm/test/CodeGen/MSIL/ext-loads.ll`, `llvm/test/CodeGen/MSIL/trunc-stores.ll`, `llvm/test/CodeGen/MSIL/memintrinsics.ll`, `llvm/test/CodeGen/MSIL/memmove.ll`, `llvm/test/CodeGen/MSIL/frame-index.ll`, `llvm/test/CodeGen/MSIL/frame-overalign.ll`, `llvm/test/CodeGen/MSIL/frame-address-taken-args.ll`, `llvm/test/CodeGen/MSIL/dynamic-alloca-error.ll`, `llvm/test/CodeGen/MSIL/bitcast.ll`, `llvm/test/CodeGen/MSIL/fabs-copysign.ll`, `llvm/test/CodeGen/MSIL/localloc-prologue.mir`, `llvm/test/CodeGen/MSIL/fence.ll`, `clang/test/CodeGen/MSIL/asm-barrier.c`
- Reads: this plan sections 2, 3 (D13, D15, D16, D36), 5.1, 5.4; llvm/docs/MSIL/recon/passes-frame.md sections 6, 12; llvm/docs/MSIL/recon/isel.md sections 5.5, 11.3

From the spec only, both triples, ilasm round trip. ldind/stind widths, unaligned./volatile. prefixes, extending loads and truncating stores, cpblk/initblk for constant sizes, Buffer::MemoryCopy for memmove and variable memcpy, the localloc prologue (size, single localloc, stored to a native int local, '.locals' without init), FP-relative frame addresses with folded offsets, the over-align sequence for align 16 (frames of 32 bytes or less included) and align 32 and no realignment for align 8 (frame-overalign.ll, D13), address-taken arguments, bitcasts and fabs/copysign through memory (moved here from M5), the dynamic-alloca diagnostic ('not llc ... 2>&1 | FileCheck --implicit-check-not="LLVM ERROR"'), fence.ll (a singlethread fence prints '// MEMBARRIER' and a load is not moved across it; a system fence prints 'call void [mscorlib]System.Threading.Interlocked::MemoryBarrier()'), and clang/test/CodeGen/MSIL/asm-barrier.c (REQUIRES: msil-registered-target: 'clang --target=x86_64-pc-windows-msvc-msil -O2 -S' on a function containing 'asm volatile("" ::: "memory")' between two stores succeeds and the module assembles).

#### M7.d Execution tests for frames; enable -O0 for every earlier C test (test, ~500 lines)

- Owned files: `msil/test/M1/04-frames/`, `msil/test/M1/00-smoke/return-0.c`, `msil/test/M1/00-smoke/return-42.c`, `msil/test/M1/01-int/calls-arith.c`, `msil/test/M1/03-control/loops.c`, `msil/test/M1/03-control/switch.c`, `msil/test/M1/03-control/recursion.c`, `msil/test/M1/03-control/ackermann.c`, `msil/test/M1/03-control/collatz.c`
- Reads: this plan section 5.5 and decisions D13, D32, D37, D38; llvm/docs/MSIL/recon/tests-ci.md section 7.2

04-frames: address-taken-local.c, local-array.c, frame-align.c (_Alignas(32) local checked with (unsigned long long)(uintptr_t)&x % 32), frame-align16.c (a frame of 32 bytes or less holding an _Alignas(16) char array and an __int128 local, each checked with (uintptr_t)&x % 16 == 0: the D13 case where a constant-size localloc only 8-aligns), frames-recursion.c, struct-local-copy.c, many-locals.c. Rule for these tests (plan 5.5, until M8): no brace or string initializers of local arrays or structs and no string literals, because clang emits them as private @__const globals plus llvm.memcpy even at -O0 and globals arrive in M8; initialize element by element. All %{msil_c} (O0 and O2), results through MSIL_EXPECT. Switch every earlier C test from %{msil_c_O2} to %{msil_c} and drop the D32 comment: 00-smoke/return-0.c, return-42.c, 01-int/calls-arith.c and 03-control/loops.c, switch.c, recursion.c, ackermann.c, collatz.c (regenerate their .expected only if update-expected.py --check says so; the .expected files of those tests are not owned here, so report a needed change to the orchestrator).

#### M7.e Execution tests for memory, bit operations and fences (test, ~500 lines)

- Owned files: `msil/test/M1/05-memory/`
- Reads: this plan section 5.5 and decisions D15, D16, D21, D36, D37; llvm/docs/MSIL/recon/tests-ci.md section 7.2; llvm/docs/MSIL/recon/cil-runtime.md (e04, e19)

05-memory: memcpy-memset.c, memmove-overlap.c, memmove-helper.c (an __attribute__((noinline)) function whose only body is a memmove of its pointer arguments, so at -O2 the method is frameless and its first unverifiable instruction is the Buffer::MemoryCopy call: the PEVerify exception of D21 is really exercised), packed-unaligned.c, pointer-arith.c, bit-builtins.c (bswap, popcount, clz, ctz, rotates), bitcast-fp.c (union and memcpy float<->int), fp-bits.c (fabs, copysign and -0.0 through bit patterns; moved here from M5 because they go through memory, D16), volatile-locals.c, barrier.c (stores separated by 'asm volatile("" ::: "memory")' and by __atomic_thread_fence(__ATOMIC_SEQ_CST), results checked, D36). Same initializer rule as M7.d (no aggregate or string initializers before M8). All %{msil_c} (O0 and O2), results through MSIL_EXPECT.

**Build targets:** `llc` `opt` `clang` `FileCheck` `not` `count` `split-file` `llvm-config` `MCTests` `MSILTests`

**Gate commands** (run in order by the gate agent; all must succeed):

```text
 1. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 llc opt clang FileCheck not count split-file llvm-config MCTests MSILTests
 2. D:/llvm-msil/build/unittests/Target/MSIL/MSILTests.exe
 3. D:/llvm-msil/build/unittests/TargetParser/TargetParserTests.exe --gtest_filter=*MSIL*
 4. D:/llvm-msil/build/unittests/MC/MCTests.exe --gtest_filter=TargetRegistry.*
 5. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" llvm/test/CodeGen/MSIL clang/test/CodeGen/MSIL clang/test/Preprocessor/init-msil.c clang/test/Driver/msil-toolchain.c msil/test
 6. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_runner=wsl --param msil_triple=x86_64-unknown-linux-gnu-msil" msil/test/M1
 7. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_run_netfx=1" msil/test/M1/04-frames msil/test/M1/05-memory
 8. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_dotnet_env=DOTNET_TieredCompilation=0" msil/test/M1/04-frames msil/test/M1/05-memory  (optimizing JIT: localloc alignment and CSE across loads, D38)
 9. pwsh -NoProfile -Command "Set-Location D:/llvm-msil; git add -A; python 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/git-clang-format' --binary 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe' --extensions c,cpp,h,hpp,hxx,cxx,inc,cppm,cl --diff HEAD -- llvm/lib llvm/include llvm/unittests clang/lib clang/include"  (must print no diff; same extension list as llvm/utils/git/code-format-helper.py, so .td files are never formatted)
10. pwsh -NoProfile -Command "git -C D:/llvm-msil add -A; python D:/llvm-msil/msil/utils/plan-check.py scope M7"  (every staged path is owned by a WP of M7, is a M7 fix-loop amendment in section 12, or is on the allow-list)
```

**Exit criteria**
- Every C test so far passes at -O0 and -O2 on .NET 8, WSL .NET 10 and .NET Framework; 04-frames and 05-memory also pass with DOTNET_TieredCompilation=0.
- localloc only ever executes with just its size on the stack (StackCheck + jitall clean); 16-byte-aligned objects are 16-aligned in small frames on every runtime.
- memmove semantics hold for overlapping ranges; unaligned/volatile prefixes printed where required; PEVerify accepts the MemoryCopy call and nothing else of its kind.
- Compiler barriers compile (no crash) and system fences call Interlocked::MemoryBarrier.
- Dynamic alloca fails with the documented diagnostic (exit code 1, no crash marker).

**Review focus** (in addition to the three standing lenses)
- Frame layout correctness (offsets, over-alignment, arguments whose address is taken).
- Memory-op typing (native int addresses, no int64), prefix encoding, cpblk/initblk size limits.
- localloc + RegStackify interaction; .locals never 'init' (passes-frame 6.6).
- Fence lowering keeps memory operations ordered through RegStackify; MEMBARRIER really is a comment-only pseudo.

**Rebuild cost.** MSIL target TUs + relink: ~5-15 min.

### M8. Globals, module initializer and P/Invoke output

**Goal.** Global variables live in one unmanaged data segment that <Module>::.cctor allocates and fills from an RVA blob, with relocations and llvm.global_ctors; code addresses globals through per-global initonly fields; undefined functions that are Phase-1 imports (puts, putchar, the C math library, msil-import-module) become per-triple P/Invokes and every other undefined function is a diagnostic (D14), so puts output works end to end (Design.md section 12's hard-coded puts, generalized to a small import table).

**Depends on:** M7.

**Header changes:**
- none

**Work packages**

#### M8.a Global addresses, function addresses, the Phase-1 import table and library-call policy (implement, ~600 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILInstrInfo.td`, `llvm/lib/Target/MSIL/MSILISelLowering.h`, `llvm/lib/Target/MSIL/MSILISelLowering.cpp`, `llvm/lib/Target/MSIL/MSILISelLoweringCall.cpp`, `llvm/lib/Target/MSIL/MSILUtilities.h`, `llvm/lib/Target/MSIL/MSILUtilities.cpp`
- Reads: llvm/docs/MSIL/recon/isel.md sections 2.7, 5.7, 7, 11.2; llvm/docs/MSIL/recon/asmprinter.md section 11 (per-global field form); this plan decision D14
- Provides: `MSILISD::DATA_ADDR, MSILISD::FUNC_ADDR, MSILISD::EXT_SYM`; `GLOBAL_ADDR/FUNC_ADDR patterns`; std::optional<StringRef> MSIL::getPInvokeModule(const Triple &TT, StringRef Name, const Function *F = nullptr); StringRef MSIL::getPInvokeEntry(StringRef Name, const Function *F) (MSILUtilities.h)

LowerGlobalAddress: data -> MSILISD::DATA_ADDR(TargetGlobalAddress with offset) -> GLOBAL_ADDR (offset folding allowed for data); a function used as a value -> MSILISD::FUNC_ADDR -> FUNC_ADDR (ldftn; calls through it come in M9); thread-local globals, GlobalTLSAddress and BlockAddress -> diagnostics. LowerExternalSymbol -> MSILISD::EXT_SYM. Phase-1 import table (decision D14) in MSILUtilities.cpp: MSIL::getPInvokeModule returns the library for puts and putchar (C library: linux-gnu and linux-musl "libc", windows-msvc "ucrtbase", windows-gnu "msvcrt"), for the math functions of D14 and their f-suffixed variants (math library: linux-gnu "libm.so.6" because glibc keeps libm separate and CoreCLR special-cases only "libc"; linux-musl "libc"; windows-msvc "ucrtbase"; windows-gnu "msvcrt"), and for any function with the "msil-import-module" attribute (its value; "msil-import-name" overrides the entry point through getPInvokeEntry); std::nullopt for everything else. Library-call policy in LowerCall for ExternalSymbol callees: names starting with "__" (compiler-rt and __atomic_* helpers such as __divti3, __mulsc3, __extendhfsf2) -> DiagnosticInfoUnsupported "runtime helper '<name>' is not available for MSIL yet (phase 3)"; names getPInvokeModule accepts (sqrt, floor, fmod, fmin, ...) are lowered as calls to the external symbol and become P/Invokes; any other name -> DiagnosticInfoUnsupported "library call '<name>' is not a Phase-1 import". Every diagnosed call builds no call node (M4 rule). memcpy/memset/memmove never reach here (M7).

#### M8.b Data segment capture and layout (implement, ~450 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILDataSegment.h`, `llvm/lib/Target/MSIL/MSILDataSegment.cpp`
- Reads: llvm/docs/MSIL/recon/asmprinter.md sections 7.1-7.2; llvm/include/llvm/MC/MCStreamer.h (pure virtuals at HEAD); llvm/lib/CodeGen/AsmPrinter/AsmPrinter.cpp emitGlobalConstant (~4525-4678)
- Provides: struct MSILReloc, struct MSILDiff, struct MSILDataSegment { Image, Entries, Zero, Relocs, Diffs, MaxAlign, InitSize, TotalSize, addGlobal(...), finalize() }; `class MSILDataCollector : public MCStreamer`

asmprinter.md 7.1-7.2 (decision D12): MSILDataSegment with 'void addGlobal(const GlobalVariable &GV, AsmPrinter &AP, const DenseMap<const MCSymbol *, const GlobalValue *> &SymToGV)' that aligns the image, temporarily swaps AP.OutStreamer with an MSILDataCollector, calls AP.emitGlobalConstant(DL, Init), swaps back and records the entry; zero/undef initializers go to the BSS list; finalize() places BSS after the image (MaxAlign >= 16) and folds symbol-difference relocations into image bytes. MSILDataCollector records bytes (emitBytes, emitFill, integer values) and turns emitValueImpl expressions into absolute bytes, MSILReloc {Site, Target, Addend, Size 4|8} or MSILDiff; implement every pure virtual of MCStreamer at HEAD; errors via LLVMContext::emitError naming the global ('unsupported constant expression', 'function address + offset', 'address of a non-global'). If reviewers reject the OutStreamer swap, the NVPTX-style walker is the drop-in fallback with the same output structs.

#### M8.c Module trailer: P/Invoke declarations, data image, address fields, .cctor, ctors (implement, ~700 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILAsmPrinter.h`, `llvm/lib/Target/MSIL/MSILAsmPrinter.cpp`, `llvm/lib/Target/MSIL/MSILMCInstLower.h`, `llvm/lib/Target/MSIL/MSILMCInstLower.cpp`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILTargetStreamer.h`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILTargetStreamer.cpp`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILInstPrinter.h`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILInstPrinter.cpp`
- Reads: llvm/docs/MSIL/recon/asmprinter.md sections 5.1, 6.5, 7.3, 9; llvm/docs/MSIL/recon/cil-runtime.md (e08 P/Invoke, e09 module initializer); this plan sections 5.1, 3 (D10, D14, D26)
- Consumes: MSILDataSegment API (M8.b); MSIL::getPInvokeModule / getPInvokeEntry (M8.a)

emitGlobalVariable: llvm.global_ctors -> preprocessXXStructorList into Ctors; llvm.used, llvm.compiler.used and section llvm.metadata skipped; declarations that are used -> error "external global variable '<name>' is not supported (phase 2 linker)"; thread_local -> error; every definition (any linkage, comdat ignored, decision D26) -> MSILDataSegment::addGlobal. emitEndOfAsmFile in this order: P/Invoke declarations ('.method public hidebysig static pinvokeimpl("<lib>" [as "<entry>"] cdecl) <ret> '<name>'(<params>) cil managed preservesig {}') for every collected undefined function and external symbol that MSIL::getPInvokeModule accepts (D14), and for every other collected undefined function the error "undefined function '<name>' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker" (no declaration emitted); variadic -> error; the data image ('.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType { .pack 1 .size N }', '.data cil '<msil>image_d' = bytearray (...)' 16 bytes per line with an offset comment, '.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d''), omitted when empty; '.field private static initonly native int '<msil>data'' and one '.field public|assembly static initonly native int '<name>'' per defined global; the .cctor exactly as asmprinter.md 7.3 with [mscorlib] references (AllocHGlobal of TotalSize + MaxAlign - 1, align up, cpblk from ldsflda of the RVA field, initblk of the BSS tail, address fields, relocations with stind.i (4-byte: conv.i4 + stind.i4), function relocations via ldftn <sig> (an undefined function's ldftn targets its P/Invoke declaration), then ctors in priority order, ret; .maxstack computed); then '<msil>Main' (M3). MCInstLower: non-function GlobalAddress -> MSILMemberRefExpr::createField(name, offset); ExternalSymbol -> noteExternalSymbol(name, signature from the MI) + createMethod. InstPrinter: GLOBAL_ADDR prints 'ldsfld native int '<g>'' plus 'ldc.i4 <off>' / 'add' when the offset is non-zero ('ldc.i8 <off>' / 'conv.i' / 'add' when not isInt<32>); FUNC_ADDR prints 'ldftn <member-ref>'. Remove the M4 temporary 'needs P/Invoke support' error and the M2 'global variables' error.

#### M8.d llc tests for globals and P/Invoke (test, ~800 lines)

- Owned files: `llvm/test/CodeGen/MSIL/globals.ll`, `llvm/test/CodeGen/MSIL/global-relocs.ll`, `llvm/test/CodeGen/MSIL/global-offsets.ll`, `llvm/test/CodeGen/MSIL/bss.ll`, `llvm/test/CodeGen/MSIL/module-initializer.ll`, `llvm/test/CodeGen/MSIL/global-ctors.ll`, `llvm/test/CodeGen/MSIL/string-literals.ll`, `llvm/test/CodeGen/MSIL/comdat-linkonce.ll`, `llvm/test/CodeGen/MSIL/pinvoke.ll`, `llvm/test/CodeGen/MSIL/pinvoke-from-data.ll`, `llvm/test/CodeGen/MSIL/pinvoke-errors.ll`, `llvm/test/CodeGen/MSIL/data-diff-reloc.ll`, `llvm/test/CodeGen/MSIL/quoting-globals.ll`, `llvm/test/CodeGen/MSIL/libcall-policy.ll`
- Reads: this plan sections 3, 5.1; llvm/docs/MSIL/recon/asmprinter.md sections 7, 9, 12

From the spec only, both triples (LNX/WIN prefixes where the P/Invoke library differs), ilasm round trip for every module. Data image bytes and layout (alignment, BSS after the image, MaxAlign 16), per-global fields with public/assembly access, the .cctor sequence, relocations to data (+addend), to functions (ldftn) and 4-byte relocations, ctor order by priority, MSVC string literal names ('??_C@...') with linkonce_odr/comdat, a P/Invoke referenced only from data, variadic P/Invoke error, external global error, TLS error, symbol-difference relocations, quoting of global names, and the import table and library-call policy of D14 (libcall-policy.ll: sqrt from C and from an FSQRT libcall -> P/Invoke into the math library, LNX 'libm.so.6', WIN 'ucrtbase'; puts -> LNX 'libc', WIN 'ucrtbase'; a function with msil-import-module/msil-import-name -> that library and entry; __divti3 -> diagnostic; pinvoke-errors.ll: an undefined function that is not an import -> the 'not a Phase-1 import' diagnostic; every diagnostic test uses --implicit-check-not="LLVM ERROR").

#### M8.e Execution tests: output and globals (test, ~800 lines)

- Owned files: `msil/test/M1/06-output/`, `msil/test/M1/07-globals/`
- Reads: this plan section 5.5 and decisions D14, D37; llvm/docs/MSIL/recon/tests-ci.md sections 6.5, 7.2; msil/test/Inputs/msil_test.h

06-output: puts-hello.c, puts-many.c (2000 lines: buffering and flush at exit), print-ints.c (msil_test.h helpers incl. LLONG_MIN and hex). 07-globals: global-scalars.c, global-array-sum.c, bss-zero.c, const-tables.c, string-table.c (const char *names[] relocations), reloc-global-ptrs.c, static-locals.c, volatile-inputs.c (inputs read from volatile globals defeat -O2 folding), ctors.c (__attribute__((constructor(N))) ordering). Also 06-output/libm-sqrt.c (returns (int)__builtin_sqrt of a value read from a volatile global, 7 -> [exit 7]; depending on the triple's math-errno default clang emits either a call to sqrt or llvm.sqrt, which becomes an FSQRT libcall, and both end in the sqrt P/Invoke: "libm.so.6" on the WSL leg, "ucrtbase" on Windows) and 07-globals/local-aggregate-init.c (brace-initialized local arrays and structs and a char array initialized from a string literal: clang emits private @__const globals plus llvm.memcpy, the case M7 had to avoid). All %{msil_c}; .expected from update-expected.py; stdout is compared, so these run on Windows (ucrtbase) and WSL (libc, libm.so.6).

**Build targets:** `llc` `opt` `clang` `FileCheck` `not` `count` `split-file` `llvm-config` `MCTests` `MSILTests`

**Gate commands** (run in order by the gate agent; all must succeed):

```text
 1. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 llc opt clang FileCheck not count split-file llvm-config MCTests MSILTests
 2. D:/llvm-msil/build/unittests/Target/MSIL/MSILTests.exe
 3. D:/llvm-msil/build/unittests/TargetParser/TargetParserTests.exe --gtest_filter=*MSIL*
 4. D:/llvm-msil/build/unittests/MC/MCTests.exe --gtest_filter=TargetRegistry.*
 5. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" llvm/test/CodeGen/MSIL clang/test/CodeGen/MSIL clang/test/Preprocessor/init-msil.c clang/test/Driver/msil-toolchain.c msil/test
 6. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_runner=wsl --param msil_triple=x86_64-unknown-linux-gnu-msil" msil/test/M1
 7. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_run_netfx=1" msil/test/M1/06-output msil/test/M1/07-globals
 8. pwsh -NoProfile -Command "Set-Location D:/llvm-msil; git add -A; python 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/git-clang-format' --binary 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe' --extensions c,cpp,h,hpp,hxx,cxx,inc,cppm,cl --diff HEAD -- llvm/lib llvm/include llvm/unittests clang/lib clang/include"  (must print no diff; same extension list as llvm/utils/git/code-format-helper.py, so .td files are never formatted)
 9. pwsh -NoProfile -Command "git -C D:/llvm-msil add -A; python D:/llvm-msil/msil/utils/plan-check.py scope M8"  (every staged path is owned by a WP of M8, is a M8 fix-loop amendment in section 12, or is on the allow-list)
```

**Exit criteria**
- puts output, a libm call and global data work end to end on .NET 8 (ucrtbase), WSL .NET 10 (libc, libm.so.6) and .NET Framework.
- RVA data is never written; '&(label)' never appears; every global access goes through its initonly field.
- Undefined functions outside the import table, external globals, TLS and other unsupported kinds fail with diagnostics (exit code 1, no crash marker).
- All emitted modules assemble and verify.

**Review focus** (in addition to the three standing lenses)
- Data capture fidelity (padding, relocations, symbol differences), layout alignment, .cctor ordering.
- P/Invoke correctness: blittable signatures, preservesig, per-triple library names, no DisableRuntimeMarshalling in Phase 1.
- Robustness of the OutStreamer swap; fallback readiness.
- Import table matches D14 exactly per triple; no undefined function silently becomes a runtime failure.

**Rebuild cost.** MSIL target TUs + relink: ~5-15 min.

### M9. Function pointers, prototype fix-ups and aggregates

**Goal.** Function pointers within the module (ldftn + managed calli), K&R and mismatched-prototype calls through AddMissingPrototypes/FixFunctionBitcasts, the 'returned' optimization, and byval/sret aggregates as clang emits them for SysV x86-64 and Win64. Calls to variadic functions stay diagnosed (phase 3).

**Depends on:** M8.

**Header changes:**
- none

**Work packages**

#### M9.a Indirect calls, calli signatures and byval arguments (implement, ~600 lines)

- Owned files: `llvm/lib/Target/MSIL/MSILInstrCall.td`, `llvm/lib/Target/MSIL/MSILISelLoweringCall.cpp`, `llvm/lib/Target/MSIL/MSILISelDAGToDAG.cpp`, `llvm/lib/Target/MSIL/MSILMCInstLower.h`, `llvm/lib/Target/MSIL/MSILMCInstLower.cpp`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILInstPrinter.h`, `llvm/lib/Target/MSIL/MCTargetDesc/MSILInstPrinter.cpp`
- Reads: llvm/docs/MSIL/recon/isel.md sections 2.12, 8.2; llvm/docs/MSIL/recon/asmprinter.md section 6.4; llvm/lib/Target/WebAssembly/WebAssemblyISelLowering.cpp (byval in LowerCall), WebAssemblyMCInstLower.cpp:274-301
- Consumes: MSILAsmPrinter::internSignature (M2.d); MSILMemberRefExpr::createSig (M2.c)

Indirect callees (anything but GlobalAddress/ExternalSymbol) -> CALL_INDIRECT with the callee as the last register operand (calli order) and calling convention Managed; MCInstLower builds the MSIL::Signature from the MI (defs -> results, explicit register uses minus the trailing callee -> params, toValType), interns it via Printer.internSignature and emits MSILMemberRefExpr::createSig; the InstPrinter prints 'calli <ret>(<params>)'. byval arguments: copy into a new stack object with an always-inline memcpy as WebAssembly's LowerCall does (uses the M7 frame); sret stays a plain pointer argument; inalloca/preallocated -> diagnostic.

#### M9.b IR fix-up passes (copied) (implement, ~500 lines copied)

- Owned files: `llvm/lib/Target/MSIL/MSILAddMissingPrototypes.cpp`, `llvm/lib/Target/MSIL/MSILFixFunctionBitcasts.cpp`, `llvm/lib/Target/MSIL/MSILOptimizeReturned.cpp`
- Reads: llvm/lib/Target/WebAssembly/WebAssembly{AddMissingPrototypes,FixFunctionBitcasts,OptimizeReturned}.cpp; llvm/docs/MSIL/recon/asmprinter.md section 7.4; this plan decision D18, D27

Copy the three WebAssembly passes (D30 header line, no new-PM class). AddMissingPrototypes acts on the "no-prototype" attribute that clang sets for MSIL triples (M1.d). FixFunctionBitcasts creates wrappers for calls whose call-site type differs from the callee's type, including K&R variadic-typed calls to defined non-variadic functions; REMOVE WebAssembly's main renaming (__original_main and the argc/argv wrapper), because '<msil>Main' calls main with its own signature (D27). OptimizeReturned mechanical. LowerCall's mismatched-signature check (M4) stays as a backstop.

#### M9.c llc tests for function pointers, prototypes and aggregates (test, ~700 lines)

- Owned files: `llvm/test/CodeGen/MSIL/call-indirect.ll`, `llvm/test/CodeGen/MSIL/ldftn-calli.ll`, `llvm/test/CodeGen/MSIL/fnptr-table.ll`, `llvm/test/CodeGen/MSIL/function-bitcasts.ll`, `llvm/test/CodeGen/MSIL/add-prototypes.ll`, `llvm/test/CodeGen/MSIL/returned.ll`, `llvm/test/CodeGen/MSIL/byval.ll`, `llvm/test/CodeGen/MSIL/sret.ll`, `llvm/test/CodeGen/MSIL/struct-args-win64.ll`, `llvm/test/CodeGen/MSIL/struct-args-sysv.ll`, `llvm/test/CodeGen/MSIL/varargs-call-error.ll`
- Reads: this plan sections 3, 5.1; llvm/test/CodeGen/WebAssembly/function-bitcasts.ll, add-prototypes.ll, returned.ll, call-indirect.ll (to port)

From the spec only, both triples, ilasm round trip: calli signatures (incl. void and mixed types), ldftn identity, function-pointer tables in data (ldftn relocations), wrappers created for mismatched calls, prototype repair, 'returned' arguments, byval copies, sret, struct argument shapes clang emits for SysV vs Win64 (the struct-args tests feed clang-shaped IR, not C), and the variadic-call diagnostic.

#### M9.d Execution tests for function pointers, structs and C calls (test, ~900 lines)

- Owned files: `msil/test/M1/08-fnptr/`, `msil/test/M1/09-structs/`, `msil/test/M1/10-calls/`
- Reads: this plan section 5.5 and decision D37; llvm/docs/MSIL/recon/tests-ci.md section 7.2; llvm/docs/MSIL/recon/cil-runtime.md (e07, e18); llvm/docs/MSIL/recon/artifacts/review-cil/deep.il (stack depth)

08-fnptr: fnptr-call.c, dispatch-table.c (global table of function pointers), fnptr-in-struct.c, qsort-callback.c (own qsort with a comparator), fnptr-to-import.c (a global 'int (*const out[])(const char *) = { puts };' called through the pointer: the .cctor writes the pointer with ldftn of the pinvokeimpl declaration and the call is a managed calli, cil-runtime e18). 09-structs: struct-basic.c, struct-byval.c, struct-return.c, struct-heavy.c (vec3 math, linked list in a static pool), bitfields.c, unions.c, nested-aggregates.c. 10-calls: kr-call.c (no-prototype call), many-args.c (12 mixed int/long long/double args), mutual-recursion.c, void-calls.c, deep-recursion.c (2,500 frames: an -O0-shaped frame overflows the .NET 8 Windows main-thread stack, about 1.5 MB, near 19,000 frames in the review experiment artifacts/review-cil/deep.il, and real -O0 frames are bigger, so keep at least 4x headroom). All %{msil_c}, stdout via msil_test.h, .expected from the native oracle. The WSL leg exercises the SysV ABI shapes.

**Build targets:** `llc` `opt` `clang` `FileCheck` `not` `count` `split-file` `llvm-config` `MCTests` `MSILTests`

**Gate commands** (run in order by the gate agent; all must succeed):

```text
 1. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 llc opt clang FileCheck not count split-file llvm-config MCTests MSILTests
 2. D:/llvm-msil/build/unittests/Target/MSIL/MSILTests.exe
 3. D:/llvm-msil/build/unittests/TargetParser/TargetParserTests.exe --gtest_filter=*MSIL*
 4. D:/llvm-msil/build/unittests/MC/MCTests.exe --gtest_filter=TargetRegistry.*
 5. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" llvm/test/CodeGen/MSIL clang/test/CodeGen/MSIL clang/test/Preprocessor/init-msil.c clang/test/Driver/msil-toolchain.c msil/test
 6. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_runner=wsl --param msil_triple=x86_64-unknown-linux-gnu-msil" msil/test/M1
 7. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_run_netfx=1" msil/test/M1/08-fnptr msil/test/M1/09-structs msil/test/M1/10-calls
 8. pwsh -NoProfile -Command "Set-Location D:/llvm-msil; git add -A; python 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/git-clang-format' --binary 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe' --extensions c,cpp,h,hpp,hxx,cxx,inc,cppm,cl --diff HEAD -- llvm/lib llvm/include llvm/unittests clang/lib clang/include"  (must print no diff; same extension list as llvm/utils/git/code-format-helper.py, so .td files are never formatted)
 9. pwsh -NoProfile -Command "git -C D:/llvm-msil add -A; python D:/llvm-msil/msil/utils/plan-check.py scope M9"  (every staged path is owned by a WP of M9, is a M9 fix-loop amendment in section 12, or is on the allow-list)
```

**Exit criteria**
- Function-pointer dispatch, K&R calls and struct passing/returning run on both triples and all runtimes.
- Every direct call token matches its callee's definition (no MissingMethodException); calli signatures match callees.
- Variadic calls fail with the documented diagnostic.

**Review focus** (in addition to the three standing lenses)
- Signature agreement between ldftn targets and calli sites; managed calli only (native thunks are phase 3).
- Correct ABI shapes for SysV and Win64 aggregates; byval copies happen in the caller.
- Fidelity and minimal diffs of the copied IR passes; removal of wasm's main renaming.

**Rebuild cost.** MSIL target TUs + relink: ~5-15 min.

### M10. Milestone-1 conformance: program corpus, verification legs, both triples, documentation

**Goal.** Meet Design.md section 12: at least 25 small C programs (fib, sieve, quicksort over a global array, struct-heavy code, a function-pointer dispatch table, a string table with relocations, ...) compile at -O0 and -O2, run, and match native clang's output, as a lit suite; FileCheck coverage on the emitted .il for both triples; the verification policy (plan D21) clean; legs on .NET 8 (tiered and TieredCompilation=0), .NET 10, .NET Framework 4.8 and WSL .NET 10 for x86_64-unknown-linux-gnu-msil (Design.md's literal triple), including a leg assembled by the Linux ilasm inside WSL; the CI job steps are scripts the local gate executes; documentation.

**Depends on:** M9.

**Header changes:**
- none

**Work packages**

#### M10.a Program corpus A (test, ~900 lines of C)

- Owned files: `msil/test/M1/11-programs/fib.c`, `msil/test/M1/11-programs/sieve.c`, `msil/test/M1/11-programs/quicksort.c`, `msil/test/M1/11-programs/mergesort.c`, `msil/test/M1/11-programs/nqueens.c`, `msil/test/M1/11-programs/hanoi.c`, `msil/test/M1/11-programs/matrix-mult.c`, `msil/test/M1/11-programs/crc32.c`, `msil/test/M1/11-programs/string-table.c`, `msil/test/M1/11-programs/dispatch-vm.c`, `msil/test/M1/11-programs/fib.expected`, `msil/test/M1/11-programs/sieve.expected`, `msil/test/M1/11-programs/quicksort.expected`, `msil/test/M1/11-programs/mergesort.expected`, `msil/test/M1/11-programs/nqueens.expected`, `msil/test/M1/11-programs/hanoi.expected`, `msil/test/M1/11-programs/matrix-mult.expected`, `msil/test/M1/11-programs/crc32.expected`, `msil/test/M1/11-programs/string-table.expected`, `msil/test/M1/11-programs/dispatch-vm.expected`
- Reads: llvm/docs/MSIL/Design.md section 12; this plan section 5.5; llvm/docs/MSIL/recon/tests-ci.md section 7; this plan decision D37 (exit status 0..255)

Ten self-contained, UB-free C11 programs using only msil_test.h output (no long, long double, varargs or libc beyond puts), each printing a deterministic transcript and returning a meaningful exit code masked to 0..255 (for example 'return (int)(checksum & 0xff);', D37): fib, sieve, quicksort over a global array, mergesort, nqueens, hanoi, matrix-mult (double, printed scaled to integers), crc32 (table + bit ops), string-table (const char *table[] with relocations, sorting by strcmp written in C), dispatch-vm (bytecode interpreter dispatching through a global function-pointer table). One RUN line '%{msil_c}' each; .expected from update-expected.py.

#### M10.b Program corpus B (test, ~900 lines of C)

- Owned files: `msil/test/M1/11-programs/fnv1a.c`, `msil/test/M1/11-programs/strings.c`, `msil/test/M1/11-programs/bigint-add.c`, `msil/test/M1/11-programs/life.c`, `msil/test/M1/11-programs/mandelbrot-fixed.c`, `msil/test/M1/11-programs/collatz.c`, `msil/test/M1/11-programs/struct-sim.c`, `msil/test/M1/11-programs/linked-list-pool.c`, `msil/test/M1/11-programs/binary-trees-pool.c`, `msil/test/M1/11-programs/kr-legacy.c`, `msil/test/M1/11-programs/fnv1a.expected`, `msil/test/M1/11-programs/strings.expected`, `msil/test/M1/11-programs/bigint-add.expected`, `msil/test/M1/11-programs/life.expected`, `msil/test/M1/11-programs/mandelbrot-fixed.expected`, `msil/test/M1/11-programs/collatz.expected`, `msil/test/M1/11-programs/struct-sim.expected`, `msil/test/M1/11-programs/linked-list-pool.expected`, `msil/test/M1/11-programs/binary-trees-pool.expected`, `msil/test/M1/11-programs/kr-legacy.expected`
- Reads: llvm/docs/MSIL/Design.md section 12; this plan section 5.5; this plan decision D37 (exit status 0..255)

Ten more programs, same rules as M10.a, written by a different agent: fnv1a, strings (own strlen/strcmp/reverse/itoa), bigint-add (arrays of uint32 limbs), life (global grid), mandelbrot-fixed (fixed-point), collatz, struct-sim (struct-heavy particle simulation passing structs by value and returning them), linked-list-pool (static node pool), binary-trees-pool, kr-legacy (K&R definitions and prototype-less calls). Together with the struct and function-pointer tests of M9 this exceeds Design.md's 'about 25 programs'.

#### M10.c Runtime and assembler legs, CI scripts shared with the local gate, gate automation (infra, ~500 lines)

- Owned files: `msil/test/lit.cfg.py`, `msil/utils/msil-verify.py`, `msil/utils/ilverify-allowed.txt`, `msil/utils/msil-run.py`, `msil/utils/run-m1-gate.ps1`, `msil/utils/ci/`, `.github/workflows/msil-ci.yml`, `msil/test/M1/lit.local.cfg`
- Reads: this plan decisions D21, D31, D38 and section 5.5; llvm/docs/MSIL/recon/tests-ci.md sections 6.6, 9 and E2/E3; llvm/docs/MSIL/recon/cil-runtime.md (verification limits)

Verification is already enforced in every gate (msil_require_verify=1 since M0); keep it, and change ilverify-allowed.txt only with a comment naming the construct. (1) ildasm -> ilasm round-trip leg for 11-programs (Design.md 5.5) using the modern ildasm/ilasm 10.0.12 from the NuGet global-packages folder (Framework ildasm prints 'ldc.r8 inf' that Framework ilasm cannot reassemble), feature msil-roundtrip. (2) Linux assembler leg: lit param msil_wsl_ilasm=<path to the cached runtime.linux-x64.microsoft.netcore.ilasm 10.0.12 ilasm> makes msil-run.py assemble inside WSL (copy the ilasm binary to a WSL temporary directory and chmod +x it, because DrvFs files are not executable; translate paths) before running there, so the Linux CI toolchain path is exercised locally. (3) Move the CI job steps into scripts under msil/utils/ci/ (configure.ps1/.sh, build.ps1/.sh, unit-tests.ps1/.sh, lit-legs.ps1/.sh) that msil-ci.yml calls and that run-m1-gate.ps1 calls in the same order, so the local gate executes exactly the CI commands; msil-ci.yml keeps its triggers, guards and pinned actions. (4) msil/utils/run-m1-gate.ps1 runs every leg of the M10 gate in order (including the DOTNET_TieredCompilation=0 legs and the WSL ilasm leg) and prints a summary table with one row per leg. (5) Update msil-ci.yml target lists and nightly legs to the final Phase-1 shape.

#### M10.d Documentation (docs, ~600 lines)

- Owned files: `llvm/docs/MSIL/GettingStarted.md`, `llvm/docs/MSIL/Status.md`, `msil/README.md`, `msil/test/README.md`
- Reads: llvm/docs/MSIL/Design.md; this plan; every milestone's commit message

GettingStarted.md: build, configure, run the compiler (-S to .il, ilasm flags -dll -pe64 -x64, runtimeconfig.json, dotnet exec), run the tests and the gate script. Status.md: what Phase 1 supports and what it does not (varargs, TLS, dynamic alloca, long double = double, Darwin/i686, object files, BCL intrinsics, net48 profile flag), the verification policy, known runtime differences (.NET 8 vs 10 vs Framework), and a 'Proposed Design.md updates' section (Design.md itself is user-owned and must not be edited by agents) that records at least: the section 12 command 'dotnet prog.dll' needs prog.runtimeconfig.json (or 'dotnet exec --runtimeconfig') in Phase 1, which has no driver to write it (tests-ci E2: rc 131 without it); the verification criterion is D21, not the literal ILVerify sentence; the Phase-1 import table of D14 instead of P/Invoking every undefined function; __ELF__ on Linux MSIL triples (section 10 decision 9); localloc alignment (D13); and the roughly 1.5 MB main-thread stack of .NET versus native as a known difference for deep recursion.

#### M10.e Triple parity and Design-literal FileCheck tests (test, ~400 lines)

- Owned files: `llvm/test/CodeGen/MSIL/triple-parity.ll`, `llvm/test/CodeGen/MSIL/pinvoke-libs.ll`, `llvm/test/CodeGen/MSIL/aarch64-triples.ll`, `clang/test/CodeGen/MSIL/design-milestone1.c`
- Reads: llvm/docs/MSIL/Design.md sections 3, 12; this plan sections 3 (D6, D14), 5.1

triple-parity.ll: the same portable IR gives identical method bodies for x86_64-unknown-linux-gnu-msil and x86_64-pc-windows-msvc-msil (only the P/Invoke library differs); pinvoke-libs.ll: puts from linux-gnu/linux-musl 'libc', windows-msvc 'ucrtbase', windows-gnu 'msvcrt', and sqrt from linux-gnu 'libm.so.6', linux-musl 'libc', windows-msvc 'ucrtbase', windows-gnu 'msvcrt' (D14); aarch64-triples.ll: aarch64 linux and windows MSIL triples compile with native DataLayouts and assemble; design-milestone1.c (REQUIRES: msil-registered-target): Design.md 12's literal 'clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -S prog.c -o prog.il' on fib with FileCheck of the module skeleton and a %if ilasm round trip.

**Build targets:** `llc` `opt` `clang` `FileCheck` `not` `count` `split-file` `llvm-config` `MCTests` `MSILTests`

**Gate commands** (run in order by the gate agent; all must succeed):

```text
 1. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-build.ps1 llc opt clang FileCheck not count split-file llvm-config MCTests MSILTests
 2. D:/llvm-msil/build/unittests/Target/MSIL/MSILTests.exe
 3. D:/llvm-msil/build/unittests/TargetParser/TargetParserTests.exe --gtest_filter=*MSIL*
 4. D:/llvm-msil/build/unittests/MC/MCTests.exe --gtest_filter=TargetRegistry.*
 5. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_native_oracle=required --param msil_run_netfx=1" llvm/test/CodeGen/MSIL clang/test/CodeGen/MSIL clang/test/Preprocessor/init-msil.c clang/test/Driver/msil-toolchain.c msil/test
 6. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_dotnet_runtime=10" msil/test/M1
 7. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_dotnet_env=DOTNET_TieredCompilation=0" msil/test/M1  (Windows .NET 8, optimizing JIT)
 8. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_runner=wsl --param msil_triple=x86_64-unknown-linux-gnu-msil" msil/test/M1
 9. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_runner=wsl --param msil_triple=x86_64-unknown-linux-gnu-msil --param msil_dotnet_env=DOTNET_TieredCompilation=0" msil/test/M1  (WSL .NET 10, optimizing JIT)
10. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_runner=wsl --param msil_triple=x86_64-unknown-linux-gnu-msil --param msil_wsl_ilasm=D:/packages/NuGet/runtime.linux-x64.microsoft.netcore.ilasm/10.0.12/runtimes/linux-x64/native/ilasm" msil/test/M1/11-programs  (Linux ilasm inside WSL)
11. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1 --param msil_ilasm=D:/packages/NuGet/runtime.win-x64.microsoft.netcore.ilasm/10.0.12/runtimes/win-x64/native/ilasm.exe" msil/test/M1/11-programs
12. pwsh -NoProfile -Command ". D:/llvm-msil/msil/utils/build/msil-env.ps1; python D:/llvm-msil/msil/utils/update-expected.py --clang D:/llvm-msil/build/bin/clang.exe --check D:/llvm-msil/msil/test/M1"
13. pwsh -NoProfile -File D:/llvm-msil/msil/utils/run-m1-gate.ps1  (runs the msil/utils/ci scripts in CI order and prints the leg summary)
14. pwsh -NoProfile -Command "Set-Location D:/llvm-msil; git add -A; python 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/git-clang-format' --binary 'C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe' --extensions c,cpp,h,hpp,hxx,cxx,inc,cppm,cl --diff HEAD -- llvm/lib llvm/include llvm/unittests clang/lib clang/include"  (must print no diff; same extension list as llvm/utils/git/code-format-helper.py, so .td files are never formatted)
15. pwsh -NoProfile -Command "git -C D:/llvm-msil add -A; python D:/llvm-msil/msil/utils/plan-check.py scope M10"  (every staged path is owned by a WP of M10, is a M10 fix-loop amendment in section 12, or is on the allow-list)
```

**Exit criteria**
- At least 25 programs (11-programs plus the M9 struct and function-pointer tests) pass at -O0 and -O2 with the native oracle required.
- Legs green: .NET 8 (tiered and TieredCompilation=0), .NET 10, .NET Framework 4.8 (Windows), WSL .NET 10 with x86_64-unknown-linux-gnu-msil (tiered and TieredCompilation=0), Linux ilasm inside WSL, modern Windows ilasm, ildasm round trip.
- msil-verify (ILVerify allow-list + PEVerify), jitall and MSILStackCheck report no stack-shape errors.
- The local gate runs the same msil/utils/ci scripts as msil-ci.yml; enabling the workflow remains the user's decision.
- Documentation committed, including the 'Proposed Design.md updates' list.

**Review focus** (in addition to the three standing lenses)
- Programs are UB-free and meaningful (native -O0/-O2 agree); coverage maps to every Design.md 12 example.
- Verification policy is enforced, not advisory; allow-list unchanged or justified.
- Docs match behaviour; Design.md untouched (proposals only).

**Rebuild cost.** MSIL target TUs + relink if fixes are needed: ~5-15 min; the full lit legs take a few minutes.

## 7. Future milestones (phases 2-5)

Each becomes a sequence of small milestones with the same protocol when its phase starts. Every phase that needs widely-included header edits (MCSymbol kinds, IR intrinsics, RuntimeLibcalls.td, CallingConv.h, clang Attr.td) front-loads them into its first milestone, as M1 does.

### P2-A. CIL object files and the MC object writer

Phase 2 start: an MCSymbolMSIL with signatures, an MSIL MC object streamer and a CIL object format modeled on the wasm object format (method bodies in CIL, data segments, relocations, typed symbol table), the CIL binary encoder (opcode values checked against ECMA-335), and 'clang -c' producing .o. Requires a batched broad-rebuild header commit (MCSymbol kind if needed, MCContext createSymbolImpl, BinaryFormat additions), landed as the first milestone of the phase like M1.

### P2-B. msil-ld: lld flavor, symbol resolution, LTO and the PE writer

An lld flavor modeled on lld/wasm: loads objects and archives, runs LTO on bitcode (accepting stock-clang bitcode for the native triple after retargeting the module triple), resolves symbols to objects, managed assemblies and native libraries, lays out data and writes the PE/ECMA-335 metadata directly; ilasm remains a test oracle (round trip). The clang driver gains MSILToolChain<NativeTC> invoking msil-ld and writing runtimeconfig.json.

### P2-C. Exports and assembly-to-assembly linking

-shared libraries with a public export class (--export-class), the symbol-table custom attribute carrying names, kinds, signatures and the triple, linking against managed assemblies built by this toolchain, and P/Invoke resolution from native export tables / import libraries (DT_SONAME, kernel32.lib style mapping).

### P3-A. Runtime support assembly and the net48/net8.0 profiles

MsilC.Runtime (C#, net48 + net8.0): shadow stack for dynamic alloca and stacksave/restore, TLS blocks for thread_local, global destructors via ProcessExit, process startup; the -mdotnet=net48|net8.0 profile plumbing (module flag or TargetOptions), the net8.0 System.Runtime header with DisableRuntimeMarshalling, and BCL intrinsics for bit ops, rounding, min/max and bitcasts (isel.md decision 11).

### P3-B. Variadics, native-callable function pointers, setjmp/longjmp, atomics

Variadic definitions and calls (native va_list construction over an argument buffer, v-variant table, msil_fixed_arity attribute), native-callable thunks ([UnmanagedCallersOnly] / kept-alive delegates on net48) with the in-assembly fast path, setjmp/longjmp via managed exceptions (from the Emscripten SjLj lowering), C11 atomics mapped to Interlocked/Volatile, compiler-rt-style helpers (i128 division, complex, f16) in the runtime; glibc and UCRT host-libc conformance (zlib, Lua, SQLite).

### P4-A. Conformance and performance

GCC torture tests and llvm-test-suite per triple through the same transcript harness, benchmarks against native clang, IL-size-aware inlining, revisit the frame model (value-type frame local vs localloc, measured 73 vs 4 bytes of prologue), full SIMD (Vector128/256/512 and arch intrinsics), Portable PDB debug info, musl and Darwin triples (UserLabelPrefix fix for m:o), new pass manager pipeline.

### P5-A. Production toolchain

MSBuild SDK and NuGet packaging with per-target runtimes/<rid> assemblies, 32-bit triples (i686-pc-windows-msvc-msil with stdcall/cdecl signatures and the ILP32 typing rule), and static host libcs where viable.

## 8. Downloads and installed tooling

Installs are pre-approved by the user (section 2). Present on this machine:

- Framework ilasm 4.8, modern ilasm/ildasm 10.0.12 (win-x64 and linux-x64, D:/packages/NuGet), .NET 6/8/9/10 runtimes, dotnet-ilverify 10.0.12, PEVerify, VS 2026 toolchain.
- WSL Ubuntu 26.04: .NET SDK 10.0.112 / runtime 10.0.12 (apt `dotnet-sdk-10.0`) and .NET runtime 8.0.31 (Ubuntu 26.04 has no `dotnet-runtime-8.0` package; installed by the orchestrator with `dotnet-install.sh --runtime dotnet --channel 8.0 --install-dir /usr/lib/dotnet`). Both runtimes resolve through `/usr/bin/dotnet`.
- Optional (local): psutil==7.0.0 for 'lit --timeout' (python -m pip install psutil==7.0.0); not needed because msil-run.py enforces its own timeouts.
- CI only, downloaded by GitHub-hosted runners once the user enables msil-ci.yml: actions/setup-dotnet (tag v4.x, SHA to be pinned by the user), NuGet runtime.linux-x64.Microsoft.NETCore.ILAsm 10.0.12 and runtime.linux-x64.Microsoft.NETCore.ILDAsm 10.0.12, dotnet tool dotnet-ilverify 10.0.12, apt packages ninja-build clang lld, choco package ninja, and sccache via hendrikmuhs/ccache-action@f09c25b45002a07be2955cbe52e8cee55643f89d (v1.2.24, same SHA as upstream).

## 9. Risks the plan tracks

- **JIT hangs on bad IL.** The .NET 10 JIT hung and allocated 40+ GB on an inconsistent stack depth at a join
  (cil-runtime e15). Every dotnet/jitall run is under a timeout and a memory cap (M0); MSILStackCheck makes such IL a
  compile-time error.
- **Silent type errors.** The JIT accepts stack-type errors (float64 into an int32 local, int64 returned as int32,
  int64/native int mixing). The single-class-per-CIL-type design (D2), PEVerify on Windows, verification enforced in
  every gate (D38) and execution against a native oracle are the defences.
- **Runtime differences.** `conv.r.un`, out-of-range float-to-int, the alignment of constant-size localloc, and Tier-0
  versus optimized code differ between .NET 8, 9, 10 and Framework. FP, frame and memory gates run on .NET 8 (tiered
  and TieredCompilation=0), .NET 10 and Framework (M5, M7, M10).
- **Capabilities needed before their milestone.** Generic SelectionDAG legalization and the IR optimizer reach
  features early: SELECT from i128 shifts and min/max, BITCAST through memory from fabs/copysign and the generic
  u64->f64 expansion, lookup-table globals from switches, TTI queries at -O2, `@__const` globals from initialized
  local aggregates, frames from every clang -O0 function. Defences: SELECT in M4 (D34), the memory-free D17 sequence,
  lookup tables off (D8), query hooks final in M2 (D33) and the capability table of section 5.5, which the
  test-adequacy reviewer checks in every milestone.
- **Crashes after diagnostics.** llc and clang keep running after an error. MSILPrepareIR neutralizes what it
  diagnoses, LowerCall builds no call for a diagnosed callee, and every diagnostic test rejects `LLVM ERROR` output
  (section 2).
- **Stub drift.** Stubs fail loudly with their milestone; the scope check stops WPs from editing other WPs' files.
- **Upstream merge cost.** Upstream edits are confined to the M1 hooks; copied WebAssembly files carry their source
  commit (D30).
- **Machine contention.** Builds throttle on low RAM; background builds die with the agent's turn, so the build agent
  waits for its build. D: has ~34 GB free; no second build tree there.
- **Shared working tree.** Parallel WPs see each other's files mid-edit. Dependencies only gate checks (1.2a), owners
  of shared headers write the contract declarations first, and the build is serialized; per-agent worktrees are ruled
  out by disk space and build time.
- **Lost inputs.** Everything a WP reads is in the repository (`recon/`, `recon/artifacts/`); `plan-check.py reads`
  runs before every milestone.
- **CRLF checkout.** `msil/.gitattributes` keeps expected files LF regardless of `core.autocrlf=true`.
- **Undefined functions.** With `-ffreestanding` the optimizer introduces no libc calls. An undefined function that is
  not a Phase-1 import is a compile-time error (D14), so a missing helper never turns into a runtime
  EntryPointNotFoundException; compiler-rt helpers are diagnosed until phase 3.

## 10. Decisions that need the user

1. Verification criterion (Design.md section 12, 'ilverify reports no stack-shape errors'): ILVerify stops at the first error per method and cannot verify any method containing calli, so the literal criterion is not checkable on real C code. Proposed restatement (decision D21): MSILStackCheck (always on) + jitall force-JIT under time/memory limits + ILVerify with a code allow-list + PEVerify on Windows (allowing exactly the native-int arguments of Buffer::MemoryCopy), all clean and enforced in every gate. Approve, or specify another criterion. Agents will not edit Design.md.
   **Resolved 2026-10-12: user approved D21 (combined MSILStackCheck + jitall + ILVerify allow-list + PEVerify), enforced in every gate. Refinement for the D12 RVA image copy and ldftn-as-native-int PEVerify reports (section 12, M0) confirmed by the user the same day.**
2. Frame model (Design.md 4.2): keep one localloc per frame in Phase 1 (implemented in M7 behind a single helper), or switch now to an address-taken explicit-layout value-type local, which RyuJIT compiles to a 4-byte prologue versus 73 bytes (GS cookie, rbp frame, probe) for localloc (passes-frame recon). Recommendation: localloc now, measure in phase 4.
   **Resolved 2026-10-12: localloc per frame in Phase 1 behind one helper; measure in phase 4 (D13 stands).**
3. Phase-1 assembly references (decision D10): emit only '.assembly extern mscorlib' (runs unchanged on .NET 8/9/10 and .NET Framework 4.8, verified) and no DisableRuntimeMarshalling (not needed for blittable preservesig signatures), deferring Design.md 5.3/7.1's System.Runtime + DisableRuntimeMarshalling header to the phase-3 -mdotnet profile. Confirm.
   **Resolved 2026-10-12: mscorlib only in Phase 1 (D10 stands); System.Runtime header with the phase-3 -mdotnet profile.**
4. Phase-1 triple scope in clang (decision D6): x86_64/aarch64 on Linux (gnu, musl) and Windows (msvc, gnu) only; aarch64-apple-darwin-msil and i686-pc-windows-msvc-msil are rejected until phases 4/5 (they need the UserLabelPrefix rework for m:o/m:x layouts). Confirm.
   **Resolved 2026-10-12: D6 default accepted (darwin and i686 MSIL triples rejected until phases 4/5).**
5. GitHub/CI: which branch becomes the fork's default (msil-main or msil-milestone1); whether the repository is public (hosted-runner size and cost; a cold build is 1.5-3 h); whether to enable msil-ci.yml during Phase 1; and running .github/msil/disable-upstream-workflows.sh (a repository-settings change only you should make).
   **Resolved 2026-10-12: M0.e commits msil-ci.yml with workflow_dispatch only (no push/PR triggers). Default branch, repository visibility, enabling triggers and running disable-upstream-workflows.sh remain user actions.**
6. This checkout has core.autocrlf=true (CRLF working tree). msil/.gitattributes forces LF under msil/ either way; switching the clone to core.autocrlf=false and renormalizing is your call (upstream recommends false on Windows).
   **Default 2026-10-12: leave core.autocrlf as is; msil/.gitattributes forces LF under msil/.**
7. Security (user action, not a design decision): the cil-runtime recon found plaintext GitHub feed credentials (PATs) in %APPDATA%/NuGet/NuGet.Config. Consider rotating them or moving them to a credential provider. Agents are instructed never to read or copy that file.
8. ~~Optional download: the .NET 8 runtime inside WSL Ubuntu.~~ Resolved 2026-10-12: installs are pre-approved and .NET 8.0.31 is installed in WSL (section 8).
9. __ELF__ on Linux MSIL triples: clang defines __ELF__ only when the object format is ELF (clang/lib/Frontend/InitPreprocessor.cpp), so x86_64-unknown-linux-gnu-msil does not define it, a deviation from Design.md section 3's 'predefines are exactly the native ones'. Default for Phase 1: leave it undefined (M1.f pins this with an explicit test; freestanding tests do not depend on it). Alternative: define it in the MSILTargetInfo wrapper for Linux triples so glibc headers see what they see natively. Choose.
   **Default 2026-10-12: leave __ELF__ undefined in Phase 1 (M1.f pins it); revisit with host-libc headers in phase 3.**

## 11. Notes for the orchestrator

- The planning run saved `llvm/docs/MSIL/recon/cil-runtime.md` and `build-env.md` as indexes reconstructed from the
  recon experiments, and copied the experiment sources, logs, the tests-ci prototype and the review experiments to
  `llvm/docs/MSIL/recon/artifacts/`. The orchestrator appended the original report texts from the workflow journal
  (done 2026-10-12).
- The harness sometimes refuses subagent writes of `.md` files ("return findings as text"). A WP that owns a markdown
  file (for example M10.d) returns the content in its result and the orchestrator writes it, or writes it from Bash.
- Until M0 lands, use `D:/llvm-msil/build/msil-scripts/*.ps1` (same arguments). The build directory is configured but
  not built; the first build is M1.0's TableGen pre-build, the first full build is M1's gate.
- Gate commands that build must run with `run_in_background` and be waited for; a background job dies with the turn.
- Before spawning a milestone (M1 on): `plan-check.py ownership` and `plan-check.py reads M<n>`. During the fix loop,
  assign unowned files only through section 1.7a.
- Spawn every WP of a milestone at once. Dependent WPs wait only for their checks and hand-over (1.2a). M2's check
  order: M2.b and M2.e; then M2.f and M2.c; then M2.a and M2.d.
- The structured plan returned with this document is generated from the same source as this file (scratch
  `recon/plan-revise/plan_rev.py` + `render_plan.py`).

## 12. Amendment log

Rows are `| <date> | <milestone or -> | <change> | <reason> |`. A fix-loop ownership grant (section 1.7a) uses exactly
the change text ``fix-loop ownership: `<path>` -> <agent>``, for example
``| 2026-10-20 | M6 | fix-loop ownership: `llvm/lib/Target/MSIL/MSILTargetTransformInfo.cpp` -> fix-2 | <finding> |``;
`plan-check.py scope` reads these rows.

| Date | Milestone | Change | Reason |
|---|---|---|---|
| 2026-10-12 | - | Plan v1 | Initial plan from recon |
| 2026-10-12 | - | Plan v2: SELECT moves to M4 (D34); D17 becomes a select-free, memory-free sequence; query hooks final in M2 and lookup tables off (D8, D33); i32 switch index (D35); fences (D36); exit-status and capability rules (D37, 5.5); enforced verification and optimizing-JIT legs (D38); Phase-1 import table with libm.so.6 (D14); M2 split into M2.b (TableGen) and M2.f (C++ classes); M1.0 TableGen pre-build; M7.e split from M7.d; plan-check.py, msil-gtest.ps1, msil-expect-error.ps1 and run-utc-tests.py added | Adversarial review, Appendix A |
| 2026-10-12 | M6 | `recon/isel.md` 3.1/5.3 and its `BR_TABLE_I64` record (~line 1232) are superseded: there is no `BR_TABLE_I64`; the jump-table index is i32 (D35) | ILVerify rejects `switch` on a native int (review experiment `sw.il`) |
| 2026-10-12 | M7 | `recon/passes-frame.md` 1 and 6 ('localloc results are 16-aligned; realign only when MaxAlign > 16') are superseded: assume 8 and realign above it (D13) | constant-size localloc is 8-aligned on Framework and in .NET 8 tiered code (review experiment `align16.il`) |
| 2026-10-12 | M5 | `recon/isel.md` decision on uint64->f32 and plan v1 D17 ('return SDValue()') are superseded by the D17 sequence | the generic expansions need SELECT (f32) and BITCAST through memory (f64) |
| 2026-10-12 | - | Tool installs pre-approved (section 2 "Installs", section 8); .NET 8.0.31 installed in WSL; original cil-runtime/build-env reports restored | User approval; planning-run notes (section 11) |
| 2026-10-12 | - | Section 10 decisions resolved: D21 approved, localloc frames (D13), mscorlib-only (D10), D6 triple scope, CI via workflow_dispatch only, autocrlf and __ELF__ defaults | User answers |
| 2026-10-12 | M0 | fix-loop ownership: `.github/msil/.gitattributes` -> fix-1 | M0.e hand-over: a fresh Windows clone with core.autocrlf=true checks disable-upstream-workflows.sh out with CRLF, and WSL bash then stops at 'set: pipefail: invalid option name' (reproduced by triage; Git for Windows bash tolerates CR); msil/.gitattributes covers only msil/ |
| 2026-10-12 | M0 | M0.b/M0.c interfaces as built (they replace the M0.b and M0.c text where the two differ): `%ilasm` runs ilasm through `msil-run.py --tool --timeout T --mem-limit-mb N -- "<ilasm>" -nologo -quiet -pe64 -x64` (aarch64 `-pe64 -arm64`), and callers still add `-dll`/`-exe` and `-output=`; `--runner wsl` runs `wsl.exe -d <distro> --exec env VAR=VALUE... python3 msil-run.py ... -- <dll>`, so the timeout, memory cap and private directory apply inside Linux (WSL needs python3; a failed WSL probe is fatal under `msil_runner=wsl`); the POSIX memory cap is `RLIMIT_DATA`, not `RLIMIT_AS`, and `[memory limit]` is recognized from the runtime's out-of-memory message (msil-run.py and msil-jitall.py); feature `msil-wsl` is set only when `msil_runner=wsl`; extra lit params `msil_timeout` (60), `msil_mem_limit_mb` (4096), `msil_wsl_distro` (Ubuntu), `msil_ilasm=nuget`, plus `$MSIL_<NAME>` tool-path overrides; extra feature `msil-triple=<triple>`; extra transcript lines `[signal N]` and `[no newline at end of output]`, and Windows NTSTATUS/HRESULT exit codes print in hex in `[exit 0x...: outside 0..255]`; `msil-jitall.py` also takes `--dotnet`, `--runtimeconfig` and `--filter`, `build-jitall.py` also takes `--dotnet`, and lit runs jitall and ILVerify on the host runtime with the major of the runtime under test when it is installed (else 8) | Plan 1.9 (interface changes recorded for later milestones). Under a 4 GiB RLIMIT_AS CoreCLR 8.0.31 does not start, because its GC reserves 256 GiB (measured by M0.c); a Job Object around wsl.exe does not limit the Linux dotnet process; triage re-checked the WSL cap (256 MiB gives `[memory limit]`, 8 GiB gives `[exit 0]`) |
| 2026-10-12 | M0 | D21 and M0.b item (7) refined (user confirmation requested at the M0 review, because D21 was user-approved): msil-verify.py also accepts the PEVerify reports that the binding D12 and D2 shapes always produce and that ILVerify and the JIT accept. (1) `Cannot modify an imaged based (RVA) static`, in pass 1 only, and only when the ildasm listing shows `ldsflda valuetype '<msil>image_t' '<msil>image'` at the reported offset (the image copy of section 5.1 and M8.c: `ldsfld native int '<msil>data'; ldsflda ...; ldc.i4 N; cpblk`); pass 2 rewrites that ldsflda to `ldc.i4.0; conv.i`. (2) An ldftn value used as a native int (D2; FUNC_ADDR prints `ldftn <member-ref>`), which PEVerify types as a method pointer and then rejects with a message that depends on the use (`[found <type>::<method>][expected Native Int] Unexpected type on the stack.` as an argument, local or return value; `[found <type>::<method>][expected Native Int] Non-compatible types on the stack.` or `[found Native Int] Non-compatible types on the stack.` in a compare, depending on the operand order; `[found <type>::<method>] Expected numeric type on the stack.` in arithmetic): in pass 1, PEVerify errors in a method that contains ldftn are notes, but only when pass 2 ran; pass 2 rewrites every `ldftn <sig>` to `ldc.i4.0; conv.i` and verifies that method again. Pass 2 keeps no PEVerify allowance except MemoryCopy's, so D21's 'every other Unexpected type on the stack stays fatal' still holds for the neutralized copy, which is what catches errors behind these constructs; without ildasm (no pass 2) neither allowance applies | review-r2 findings 1 and 2, reproduced by the verifiers (scratch `verify-d12-rva`, `verify-fnptr`) and by triage (`triage-r2/rev.il`: compare with the ldftn value second, ldftn plus an offset): PEVerify reports these on every module with an initialized global or a function pointer used as a value, so with `msil_require_verify=1` the M8, M9 and M10 gates could not pass on Windows; no emitted shape avoids them (`ldftn; conv.i` gives 'Expected numeric type on the stack') |
| 2026-10-12 | M0 | Transcript newline rule (section 5.5 '.expected is stdout (LF)'; M0.b): msil-run.py (`--out`, `--expect`) and update-expected.py (write and `--check`) replace every run of CRs before an LF (regex `\r+\n`) by one LF, in program output on every runner and in .expected files alike. The rule is idempotent, so Windows text-mode output (`puts("a\r")` writes `a\r\r\n`) and Linux output (`a\r\n`) give the same transcript `a\n`, which `*.expected text eol=lf` (msil/.gitattributes) can commit | review-r2 finding 4: output was folded once and expected files twice, so a program printing a CR before a newline never matched its own transcript, and the Windows and WSL legs disagreed on the same .expected |
| 2026-10-12 | - | User confirmed the M0 D21 refinement (two-pass verification: pass-1 PEVerify notes only for the D12 RVA image copy and ldftn-as-native-int shapes; pass-2 ildasm round trip that neutralizes them and re-verifies strictly) | User answer at the M0 review |
| 2026-10-12 | M1 | fix-loop ownership: `msil/utils/build/msil-syntax.ps1` -> fix-1 | M1.b hand-over: on exit code 0 the script discards the compiler output, so warnings (the -Wswitch 'enumeration value ... not handled in switch' that M1.b's syntax checks are meant to catch, and every later milestone that adds enumerators) never reach the agent; reproduced by triage (scratch `triage-r1/warnsw.h`: 'OK' with no output, while the same compile with the output printed shows the -Wswitch warning) |
| 2026-10-12 | M1 | fix-loop ownership: `clang/lib/CodeGen/CGBuiltin.cpp` -> fix-2 | review-r1 finding 3: on MSIL triples the MSVC _bittest*/_interlockedbittest* builtins (the x86 asm path at ~1731) and __fastfail (~2018) build llvm::InlineAsm directly and bypass the M1.d asm rejection; verified: x86_64-pc-windows-msvc-msil -emit-llvm emits 'btsl' and 'int $$0x29' with exit 0 |
| 2026-10-12 | M1 | fix-loop ownership: `clang/lib/CodeGen/TargetBuiltins/X86.cpp` -> fix-2 | review-r1 finding 3: __cpuid/__cpuidex (~2921) and __int2c (~3034) build x86 InlineAsm directly on MSIL triples; verified, exit 0 |
| 2026-10-12 | M1 | fix-loop ownership: `clang/lib/Driver/ToolChain.cpp` -> fix-3 | review-r1 finding 4: with -fno-integrated-as the Linux and MinGW toolchains assemble through ToolChain::getAssemble() (the external 'as'), so neither M1.c hook reports err_drv_msil_no_object_output; verified: '-### -fno-integrated-as -c' exits 0 and plans '"as" "--64"' on the ILAsm output |
| 2026-10-12 | M1 | fix-loop ownership: `clang/lib/Sema/SemaDeclAttr.cpp` -> fix-4 | review-r1 finding 7: the 8192-byte cap on aligned/alignas/__declspec(align) (~4862) tests isOSBinFormatCOFF(), so every Windows MSIL triple accepts aligned(16384), which native clang rejects; verified on x86_64 and aarch64, msvc and gnu |
| 2026-10-12 | M1 | D3 amended: `normalize()` keeps the msil format of vendor-less Windows spellings. When the component permutation pushes a component that parses as the msil format to position 4, normalize() parses it, so `x86_64-windows-msvc-msil`, `x86_64-windows-gnu-msil` and `aarch64-windows-msvc-msil` normalize to `<arch>-unknown-windows-<env>-msil`. Only the msil format is affected: native spellings such as `x86_64-windows-msvc-elf` normalize as before. Unchanged: `x86_64-pc-win32-msil` -> `x86_64-pc-windows-msil`, which clang keeps rejecting as an unknown target triple (D6: Windows MSIL triples name msvc or gnu explicitly); TripleTest, triples.c and msil-toolchain.c pin both | review-r1 findings 1 and 8: the Win32 branch of normalize() resized the components to 4 and dropped the never-parsed msil, so clang and llc silently compiled native x86/AArch64 code with exit 0 (verified) |
| 2026-10-12 | M1 | M1.d amended. Item (1): every accepted empty GNU asm statement (empty string; no outputs, inputs or labels; any clobbers; volatile or not) emits `fence syncscope("singlethread") seq_cst` followed by `call void @llvm.sideeffect()`; the rule 'an empty asm with no operands and no memory clobber emits nothing' is withdrawn. D36 is unchanged (SelectionDAG and FastISel drop llvm.sideeffect). Added: CodeGenModule emits no "exception-model" module flag for MSIL triples, because D8's TargetMachine override cannot reach the codegen consumers that read Module::getExceptionModel(); the driver's -exception-model and therefore the native predefines (__SEH__ on MinGW) stay unchanged | review-r1 findings 2 and 6: dropping the empty asm let -O2 delete 'while (!flag) asm volatile("");' and delay loops that native clang keeps, and the fence alone still collapsed a delay loop (verifier, opt -O2), while fence plus llvm.sideeffect matched native; MinGW MSIL modules carried 'exception-model'='wineh' (verified) |
| 2026-10-12 | M1 | M1.b items (5) and (7) amended: the AsmParser IsMSIL case calls reportFatalUsageError("assembly parsing is not supported for MSIL") and its comment names what can still reach it (llvm-mc or cc1as with an MSIL triple, and IR inline asm until M2's MSILPrepareIR diagnoses it); getSectionNameForBitcode and getSectionNameForCommandline call reportFatalUsageError for Triple::MSIL instead of llvm_unreachable. M1.c rejects -fembed-bitcode[=...] for MSIL triples with err_drv_unsupported_opt_for_target | review-r1 findings 3 and 5: '-fembed-bitcode -S -emit-llvm' on an MSIL triple hit 'UNREACHABLE executed' with a stack dump (undefined behaviour in a release build), and the new AsmParser comment wrongly claimed that clang rejects all inline asm for MSIL; report_fatal_error is deprecated and asks for a crash reproducer |
| 2026-10-12 | M2 | M2.a item (5) extended: MSILPrepareIR also reports a DS_Error naming the function for every call or invoke whose callee is an InlineAsm and for every call of llvm.read_register, llvm.read_volatile_register or llvm.write_register, then deletes that function's body (section 2 rule); M2.e adds one case of each to unsupported-ir-errors.ll | review-r1 finding 3: clang builtins (the ARM __emit, __builtin_arm_rsr and _ReadStatusReg family, and any InlineAsm-building builtin M1 does not diagnose) and stock-clang bitcode (Design 3: msil-ld accepts native bitcode) put native machine asm or physical-register accesses into MSIL IR, and no milestone diagnosed them; M1 itself only diagnoses __cpuid/__cpuidex/__int2c/__fastfail, routes _bittest* to the generic lowering and rejects global register variables |
| 2026-10-12 | M1 | M1 gate step 2 amended: in addition to the grep of the step-1 log, run `pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-syntax.ps1 -Quiet <files>` over every .cpp under llvm/lib and clang/lib that differs from HEAD (`git -C D:/llvm-msil diff --name-only HEAD -- llvm/lib clang/lib`), except llvm/lib/Target/ARM/AsmParser/ARMAsmParser.cpp (ARM is not built), with its output saved in the gate's scratch directory; it must exit 0 and its output must not match the step-2 pattern ('MSIL' or 'IsMSIL' not handled in switch) | review-r1 finding 11: step 1 overwrites the log that step 2 greps, so when an earlier build has already compiled every changed TU, step 1 logs only 'ninja: no work to do' and step 2 cannot fail; LLVM_ENABLE_WERROR is OFF, so a missed enumerator only warns (verified with a counterexample TU); a /Zs check of the changed TUs does not depend on build history |
| 2026-10-12 | M1 | fix-loop ownership: `clang/lib/CodeGen/CodeGenAction.cpp` -> fix-5 | review-r1-residual (M1.d hand-over): for IR or bitcode input, CodeGenAction::ExecuteAction (~577-592) adds the "exception-model" module flag from the driver's -exception-model=seh, so 'clang --target=x86_64-w64-windows-gnu-msil -S -emit-llvm -x ir' (and aarch64-w64-windows-gnu-msil) emits !{i32 1, !"exception-model", !"wineh"}, which row 'M1.d amended' rules out for MSIL modules; reproduced by M1.d, confirmed by triage in the code (only the C path in CodeGenModule.cpp is guarded) |
| 2026-10-12 | M2 | M2.a item (5) extended: MSILPrepareIR sets an "exception-model" module flag that the input IR already carries to "none" (Module::setModuleFlag), so every codegen consumer of Module::getExceptionModel(), which reads the flag before TargetOptions, sees None as D8 intends; M2.e pins it in target-selection.ll with a module carrying !"wineh" (llc -stop-after=msil-prepare-ir shows !"none") | review-r1-residual (M1.d hand-over): clang emits no flag for MSIL (row 'M1.d amended' and fix-5's IR-input guard), but IR built for a native MinGW triple carries "wineh" and keeps it when llc -mtriple or clang -x ir retargets it to MSIL (Design 3 accepts native bitcode) |
| 2026-10-12 | M1 | M1.c amended: the driver does not turn on -fms-volatile by default for MSIL triples (the default `Triple.isX86() && IsWindowsMSVC` in Clang::ConstructJob also requires `!Triple.isOSBinFormatMSIL()`); an explicit -fms-volatile (clang-cl /volatile:ms) is still passed. Volatile accesses on x86_64-pc-windows-msvc-msil are therefore plain `load volatile` / `store volatile`, as on every other MSIL triple, and keep the /volatile:ms meaning through the CIL `volatile.` prefix (D24), which has acquire/release semantics (ECMA-335 I.12.6.7); with an explicit -fms-volatile they become atomic accesses, which stay unsupported until phase 3 (D36). msil-toolchain.c pins the default | review-r2 finding 1: the default made every volatile access of up to 8 bytes on the Windows execution triple (D6; lit's default triple on Windows) `load atomic volatile ... acquire` / `store atomic volatile ... release`, which AtomicExpand turns into `__atomic_*` libcalls that D14 rejects, so the planned M7/M8 volatile tests would fail on the Windows leg only (verified with the built clang; the Linux, MinGW and aarch64 MSVC triples give plain volatile accesses) |
| 2026-10-12 | M2 | M2.a item (5) extended: MSILPrepareIR also reports a DS_Error naming the function for every definition that uses a pointer in a non-zero address space, in its signature or in any instruction operand or result (loads, stores, atomics, memory intrinsics, GEPs, addrspacecast, call arguments and results, and constant expressions such as `inttoptr (i32 48 to ptr addrspace(256))`), then deletes that function's body. This covers the x86 segment spaces 256/257/258 (`__seg_gs`, `__seg_fs`, `__readgsqword` and the other MSVC segment builtins, `NtCurrentTeb()` in winnt.h) and the MS 32-bit pointer spaces 270/271 (`__ptr32`, `__sptr`, `__uptr`). A global variable in a non-zero address space is diagnosed naming the global, replaced by poison of its type in all uses and erased, as for an alias. M2.e adds an addrspace(256) load through an inttoptr constant, an addrspace(270) pointer parameter and an addrspacecast case to unsupported-ir-errors.ll | review-r2 finding 2: MSIL triples keep the native X86 address-space map and predefines (MSIL.h), so these constructs reach MSIL IR with exit 0 and no diagnostic (verified with the built clang, including `#include <windows.h>` with NtCurrentTeb()); CIL has no segment-relative or 32-bit pointers (D2), so without this rule the generic LDIND/STIND patterns would read absolute address 0x30 at run time and 32-bit pointers would fail instruction selection with an LLVM ERROR (section 2 diagnostics rule); no milestone diagnosed them |
| 2026-10-12 | M1 | fix-loop ownership: `clang/lib/Driver/ToolChain.cpp` -> fix-6 | review-r2 findings 4 and 6: getSupportedSanitizers offers -fsanitize=kcfi on every triple, and on MSIL triples KCFI's type-id module asm makes '-flto -c' assert in ModuleSymbolTable (verified, exit 74); no hook rejects a link, so '-### -flto m.c -o m.dll' on the Linux MSIL triples exits 0 and plans GNU ld with LLVMgold, crt objects and -lc (verified). fix-6 drops KCFI for MSIL triples and reports err_drv_msil_no_object_output from getLink(), as fix-3 does from getAssemble() |
| 2026-10-12 | M1 | fix-loop ownership: `clang/lib/CodeGen/CodeGenAction.cpp` -> fix-6 | review-r2 finding 4: IR or bitcode input that carries module asm reaches ModuleSymbolTable on MSIL triples under -flto / -flto=thin and asserts (verified with a .ll input, exit 74; the native triple exits 0); D26 makes module asm an error, so CodeGenAction reports err_msil_asm_unsupported (kind 2) and clears the module asm before any pipeline runs |
| 2026-10-12 | M1 | fix-loop ownership: `clang/lib/CodeGen/TargetBuiltins/X86.cpp` -> fix-7 | review-r2 finding 5: on MSIL triples __cpuid, __cpuidex and __int2c report 'inline assembly is not supported' although the source contains no assembly (verified); they report err_builtin_target_unsupported instead, the diagnostic M1.d already uses for __builtin___CFStringMakeConstantString on MSIL |
| 2026-10-12 | M1 | fix-loop ownership: `clang/lib/CodeGen/CGBuiltin.cpp` -> fix-7 | review-r2 finding 5: the same wording change for __fastfail on MSIL triples (verified on aarch64-pc-windows-msvc-msil) |

## Appendix A. Review dispositions (plan v1 -> v2)

Three reviewers (feasibility, agent-farm protocol, CIL fidelity) found 48 issues; several are the same problem seen
through different lenses. Each was checked against the source before it was accepted. "Accepted" means the fix is in
this plan, and the last column says where.

| # | Lens | Sev. | Ms | Finding | Disposition |
|---|---|---|---|---|---|
| F1 | feasibility | blocker | M3 | -O0 smoke tests need frames (`main` gets a return-value alloca on windows-msvc) | Accepted. Re-verified with VS clang 22.1.3: windows-msvc -O0 gives `%1 = alloca i32` / `store i32 0`; linux-gnu -O0 gives only `ret i32 42`. M3 goal and exit criteria now say clang -O2, llc -O0/-O2; M3.e uses `%{msil_c_O2}`; M7.d owns `00-smoke/return-*.c` and switches them; D32 generalized to every C test before M7. |
| F2 | feasibility | blocker | M5 | D17's 'return SDValue()' needs BITCAST via memory (f64) and SELECT (f32); fabs/copysign go through memory too | Accepted, option (c) generalized to both types: D17 is now the select-free, memory-free sequence (verified `TargetLowering.cpp:10379-10380`, `LegalizeDAG.cpp:2884-2934`). fabs/copysign/bitcast tests move to M7.c and M7.e; plan 5.5 forbids them in 02-fp. |
| F3 | feasibility | blocker | M4 | Variable i128 shifts need SELECT | Accepted by moving SELECT to M4 (D34), so i128 variable shifts stay in M4 (verified `TargetLowering.cpp:9452-9457`). |
| F4 | feasibility | blocker | M6 | -O2 switches become `@switch.table` globals + loads | Accepted (re-verified: VS clang -O2 builds `@switch.table.pick` plus GEP and load even with -ffreestanding). `shouldBuildLookupTables()` false in M2.a's TTI, which is final in M2 (verified `BasicTTIImpl.h:644-648`); M6.d's switch.c also avoids constant-only arms; tti-hooks.ll tests it in M2. |
| F5 | feasibility | major | M4 | Generic code emits SELECT from M4 on (sext(setcc), min/max, shifts) | Accepted (D34): SELECT pseudos and emitSelect in M4.b; `convertSelectOfConstantsToMath` true, final in M2.f as a query hook (verified `DAGCombiner.cpp:16088-16098`, `TargetLowering.h:2634`). |
| F6 | feasibility | major | M7 | Initialized local aggregates and string literals become `@__const` globals even at -O0 | Accepted (re-verified: -O0 gives `@__const.sum.a` (align 16), `@__const.sum.s`, `@__const.sum.t` and three `llvm.memcpy`). Capability table in 5.5 and the M7.d/M7.e initializer rule; `07-globals/local-aggregate-init.c` added in M8.e. |
| F7 | feasibility | major | M4 | MSIL_EXPECT needs branches | Accepted: branch-free `MSIL_CHECK` in msil_test.h (M0.b), required before M6 (5.5, D37, M4.e). |
| F8 | feasibility | major | M2 | isLegalAddressingMode is a fatal stub reachable from TTI at -O2 | Accepted: final `[reg]`-only body in M2.f (verified `TargetTransformInfoImpl.h:1423`); rule that query hooks are never stubs (section 2, D33); tti-hooks.ll runs `opt -O2` on GEP code in M2. |
| F9 | feasibility | major | M5 | update_llc_test_checks gate is always UNSUPPORTED on Windows | Accepted (verified `llvm/test/tools/UpdateTestChecks/lit.local.cfg` probes `llc` without `.exe`): M5.b adds run-utc-tests.py and the gate runs it on msil-basic and x86-basic. Rejected part: 'fail the gate if lit reports UNSUPPORTED', because without changing that upstream file (which the same finding forbids) it is always UNSUPPORTED here. |
| F10 | feasibility | minor | M2 | MinGW triples get `-exception-model=seh` and the TM rejects it | Accepted (verified `MinGW.cpp:645-650`, `BackendUtil.cpp:388-389`): M2.a forces ExceptionModel None; tested in M2.e empty-tu.c and M3.e emit-il.c, earlier than the suggested M10.e. |
| F11 | feasibility | minor | M7 | No milestone lowers ATOMIC_FENCE | Accepted (D36, M7.a, fence.ll, asm-barrier.c, 05-memory/barrier.c). Lowered by TableGen patterns (AArch64 precedent) instead of Custom lowering, so M7.a needs no ISelLowering ownership. |
| F12 | feasibility | minor | M1 | Syntax checks impossible before any TableGen output exists | Accepted: M1.0 build-agent pre-build (targets verified in build.ninja), plus a re-run after M1.c. |
| F13 | feasibility | minor | M2 | Errors from IR passes do not stop llc; `not` cannot see crashes on Windows | Accepted (verified `llcdriver.cpp:881-884`, `ErrorHandling.cpp:134-137`, `not.cpp:69-82`): PrepareIR neutralization, `--implicit-check-not` convention, msil-expect-error.ps1. Module asm moved to `AsmPrinter::doInitialization`, because the legacy pass manager initializes the AsmPrinter before any IR pass runs (`LegacyPassManager.cpp` MPPassManager::runOnModule). |
| R1 | farm | blocker | M3 | Same as F1 | Accepted (same fix). The REQUIRES-linux -O0 alternative was not taken: one rule for both triples is simpler. |
| R2 | farm | blocker | M4 | Same as F3 | Accepted via the alternative (SELECT in M4.b, which now owns MSILInstrControl.td in M4). |
| R3 | farm | blocker | M5 | Same as F2 | Accepted (D17 for both f32 and f64; no frame in functions that convert u64 to double). |
| R4 | farm | blocker | M2, M4, M5 | Diagnose-and-continue paths reach fatal stubs | Accepted: section 2 rule; PrepareIR `deleteBody`/erase; M4.a LowerCall builds no call; tests per milestone with `--implicit-check-not`. |
| R5 | farm | major | all | Format gate also formats `.td` files | Accepted: `--extensions c,cpp,h,hpp,hxx,cxx,inc,cppm,cl` (the code-format-helper list; verified the VS git-clang-format default includes `td`). |
| R6 | farm | major | M5 | Same as F9 | Accepted (same fix). |
| R7 | farm | major | all | Fix loop cannot touch files no WP owns | Accepted: rule 1.7a and the amendment row format of section 12, enforced by `plan-check.py scope`. |
| R8 | farm | major | M1 | Same as F12 | Accepted (M1.0 with the second clang-tablegen run after M1.c). |
| R9 | farm | major | M2 | 'Depends on' undefined; cross-WP names not pinned | Accepted: rule 1.2a; M2 split so that M2.b (TableGen) has no dependency and M2.f owns every header M2.b's old C++ needed; PrintMethods, operand defs, constructors and TargetMachine/Subtarget declarations pinned in 5.3/5.4. |
| R10 | farm | major | M3 | M3.e tests need M4/M6 features | Accepted: implicit-def.ll moves to M6.c, call cases of unused-argument.ll to M4.d, ret.ll narrow returns restricted. |
| R11 | farm | major | M4 | Same as F7 | Accepted (MSIL_CHECK). |
| R12 | farm | major | M6 | Same as F4; '-O0 needs frames' wording | Accepted (TTI off, noinline arms, 5.5 capability table). Not done: a table-forming switch test in M8.e, because lookup tables stay off in Phase 1; explicit const tables are covered by 07-globals/const-tables.c. |
| R13 | farm | major | M0 | jitall location and build step unpinned | Accepted: `<build>/tools/msil/jitall`, built by lit at configuration time and by gate step 4 before the harness runs; `msil_require_verify=1` makes a missing jitall fatal. |
| R14 | farm | major | M0 | puts-windows.test runs under the WSL leg | Accepted: REQUIRES msil-triple-windows / msil-triple-linux; `msil_runner=wsl` implies the linux triple. |
| R15 | farm | major | M3, M7 | MIR tests need exact operand lists | Accepted: 5.4 record table with `(outs)`/`(ins)` and implicit operands, plus a sample MIR. |
| R16 | farm | major | M0 | Inputs exist only in session scratch | Accepted: this planning run wrote `recon/cil-runtime.md`, `recon/build-env.md` and copied jitall.cs, csc.rsp, the experiments, logs, the tests-ci prototype and the review experiments to `recon/artifacts/` (secret-scanned, no binaries); reads re-pointed; `plan-check.py reads` before each milestone. |
| R17 | farm | minor | all | Scope check broken (no staging in M0; plan/recon edits rejected) | Accepted: the scope step stages and runs `plan-check.py scope`, with the allow-list of section 1.8. |
| R18 | farm | minor | M3-M9 | Verification optional until M10 | Accepted: `msil_require_verify=1` in every gate from M0 (D38); M10.c keeps only the new legs. |
| R19 | farm | minor | M2 | GTEST_SKIP looks like a pass | Accepted: msil-gtest.ps1 `-NoSkip -MinTests 3`. |
| R20 | farm | minor | all | Shared tree: checks see half-written files | Partly accepted. Per-WP worktrees are rejected (the protocol mandates one tree; disk and build time). Mitigated by 1.2a (checks only after the dependency hands over) and the header-first rule. |
| C1 | fidelity | blocker | M6 | `switch` on a native int fails ILVerify | Accepted with a cleaner mechanism: `getJumpTableRegTy` returns i32 (NVPTX precedent, `NVPTXISelLowering.h:132`; SelectionDAGBuilder range-checks before truncating, verified ~3100-3130); BR_TABLE_I64 removed; tests check `conv.i4` + `switch`; M0.d self-test; amendment row for isel.md. |
| C2 | fidelity | blocker | M4 | Same as F3 | Accepted (SELECT in M4.b). |
| C3 | fidelity | blocker | M5 | Same as F2 | Accepted exactly as proposed (D17). |
| C4 | fidelity | major | M7 | PEVerify rejects native int arguments to Buffer::MemoryCopy | Accepted: exact-message exception in msil-verify.py (M0.b), self-test for both sides (M0.d), frameless memmove helper test (M7.e), D21 text. |
| C5 | fidelity | major | M7 | Constant-size localloc only 8-aligned | Accepted: assumed alignment 8 (D13, M7.b), frame-overalign.ll covers align 16, frame-align16.c runs on tiered, TieredCompilation=0 and Framework; amendment row for passes-frame.md. |
| C6 | fidelity | major | M7 | Same as F11 | Accepted. The generic MEMBARRIER (not a new COMPILER_FENCE) is enough: with hasSideEffects and no memoperands, the copied RegStackify query treats it as Write+Effects (verified `WebAssemblyRegStackify.cpp:184-216`), and AsmPrinter prints it as a comment (`AsmPrinter.cpp:2243`). System fences call `[mscorlib]Interlocked::MemoryBarrier()` (D10 keeps mscorlib only). |
| C7 | fidelity | major | M3 | Exit codes above 255 pass silently on Linux | Accepted (D37): 0..255 rule, harness and update-expected rejection, M0.d self-test, masked program results in M10. |
| C8 | fidelity | major | M10 | No gate runs the optimizing JIT | Accepted (D38): `msil_dotnet_env` passthrough (M0.b/M0.c) and TieredCompilation=0 legs in M5, M7 and M10 (including WSL). |
| C9 | fidelity | minor | M10 | Design 12 criteria unproven (CI, Linux ilasm, runtimeconfig) | Accepted: Linux ilasm leg inside WSL, CI steps as msil/utils/ci scripts the local gate runs, Status.md proposals (M10.c, M10.d). |
| C10 | fidelity | minor | M9 | 10,000-frame recursion near the stack limit | Accepted: 2,500 frames; Status.md documents the 1.5 MB stack. |
| C11 | fidelity | minor | M8 | Math functions are in libm.so.6 on glibc | Accepted (D14 math library `libm.so.6`; libm-sqrt.c on WSL; pinvoke-libs.ll). |
| C12 | fidelity | minor | M0 | `-ffreestanding` removes main's implicit return | Accepted (verified `Decl.cpp` FunctionDecl::isMain): `-Werror=return-type` in cflags and update-expected.py. |
| C13 | fidelity | minor | M8 | D14 P/Invokes every undefined function | Accepted: Phase-1 import table; other undefined functions are diagnostics (D14, M8.a/M8.c/M8.d). |
| C14 | fidelity | minor | M1 | `__ELF__` silently undefined | Accepted: section 10 decision 9 with a default; init-msil.c asserts it explicitly. |
| C15 | fidelity | minor | M5 | High-range unsigned conversions and P/Invoke through function pointers untested | Accepted: conversions.ll high range (M5.d); 08-fnptr/fnptr-to-import.c (M9.d). |
