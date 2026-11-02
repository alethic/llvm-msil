# P-0 recon `linkrules`: the dotnet link-rule family for msil64-unknown-dotnet

Read-only recon at HEAD `c3460446ac09` (M25), 2026-11-01. Scratch, probes and transcripts are in
`<scratch>/p0/linkrules/`. The transcript of measurements M1-M8 is in `measurements.txt` there. Nothing in
the repository was changed except this file.

Binding inputs: the user rejected ELF-style rules. The portable triples define no `__ELF__`, they get a
dotnet-specific rule family, and compiler-rt's `int_lib.h` and llvm-libc need a dotnet branch. This report designs
that family. It does not reopen the decision.

## 0. Summary

1. **Today every link rule is a two-way switch on `Triple::isOSWindows()`.** It selects COFF rules or "everything
   else" (ELF rules) in 25 places across lld/MSIL, lib/CLI and the MSIL backend. Without changes, a dotnet triple would
   silently get the ELF rules, plus the native-binding rules of a Linux triple (dlopen/dlsym data imports, glibc
   constructor arguments, `libc.so.6` environ, `.so` library candidates, ELF native-library readers). The family needs
   one selector, `cli::getLinkRules(const Triple &)`, returning ELF, COFF or **Dotnet**, plus a "binds natively"
   predicate. Each site then makes an explicit choice for Dotnet (sections 2.1 and 7).
2. **Not defining `__ELF__` is right for a stronger reason than taste.** In llvm-libc, `__ELF__` without `__linux__`
   means the baremetal OS layer: `src/__support/OSUtil/io.h`, `src/stdlib/abort_utils.h` and
   `include/llvm-libc-macros/time-macros.h` all dispatch that way. Defining `__ELF__` on the portable triple would
   silently pick the baremetal I/O and abort hooks (`__llvm_libc_stdio_write` and friends). That is an interim OS
   layer, which the user ruled out, and it fails at link time with misleading names. Without `__ELF__` those headers
   select nothing, and an OS-dependent use fails to compile or link, which is the loud behaviour the user asked for
   (section 2.3).
3. **compiler-rt has exactly one blocker.** `int_lib.h`'s `COMPILER_RT_ALIAS` chain reaches `#error Unsupported
   target`. The fix is one token in the first branch (`|| defined(__dotnet__)`), on the precedent of the existing
   `__wasm__` term. Every other builtins chain has a generic fallback that is correct for a little-endian LP64 target
   without x87 (section 2.2).
4. **llvm-libc needs two property macros.** `LIBC_TARGET_OS_IS_DOTNET` goes in `properties/os.h` and
   `LIBC_TARGET_ARCH_IS_MSIL` in `properties/architectures.h`, keyed on the new `__msil64__`/`__msil32__`. They must
   **not** be keyed on `__MSIL__`, which host MSIL triples also define next to `__x86_64__`. The other chains are
   libc-port work owned by the libc recon (fenv_t, `__jmp_buf` and math-macros `#error`s; the OSUtil dispatch in P-2).
5. **Constructors.** Keep the record's INIT entries (the wasm `WASM_INIT_FUNCS` analogue), sorted stably by priority
   (wasm and ELF agree on this). `.init_array[.N]` slots run too (wasm's object writer also folds them). Constructors
   take **no arguments** (the wasm rule; there is no glibc argc/argv/envp on .NET). They run in the assembly's module
   initializer after relocations, as today. llvm-libc itself has **no** static constructors: its sources have no
   `constructor` attribute and are built with `-Wglobal-constructors`, and the startup walkers in `libc/startup/` are
   not used. The task's premise that llvm-libc relies on them is wrong. C++ dynamic initializers in user code use this
   path (measured M3).
6. **Destructors: lower `llvm.global_dtors` to `__cxa_atexit` with upstream's `LowerGlobalDtorsPass`, as WebAssembly
   does.** msil-ld defines a hidden per-assembly `__dso_handle`, as wasm-ld does. `.fini_array`, `.preinit_array`,
   `.ctors`/`.dtors` and `.CRT$X*` stay named errors on dotnet. Measured M4: the lowered IR compiles to a CIL object,
   links and runs on .NET 10 in ELF's destructor order with managed function pointers (no M28 thunk). Measured M5:
   C++ static destructors already need `__dso_handle`, and today's link fails with `undefined hidden symbol:
   __dso_handle`. This route needs no trailer exit table, so M29 (host C library exit) stays off on dotnet. The only
   start-up change is `'<msil>Main'` = `exit(main())` whenever the link resolves `exit`, which P-2 needs for stdout
   flushing.
7. **`__start_X`/`__stop_X`: supported, with the same rule as today's non-Windows triples** (C-identifier output
   section, defined only when referenced, never exported). lld/wasm defines them the same way, and the rule belongs to
   msil-ld's image layout, not to any native format.
8. **Exports of a `-shared` assembly (c.dll).** The selection is default-visibility GLOBAL/WEAK definitions (the
   ELF/wasm `-shared` rule), **plus wasm-ld's `--export=<sym>` / `--export-if-defined=<sym>`**, which force an export
   even for a hidden symbol. Measured M1/M2/M6: llvm-libc's public names are HIDDEN by default. `LIBC_NAMESPACE_DECL`
   is `[[gnu::visibility("hidden")]]` and the `asm("strlen")` declaration inherits it, so a plain `-shared` c.dll
   exports **nothing**. `-DLLVM_LIBC_FUNCTION_ATTR=[[gnu::visibility("default")]]` fixes the entrypoints but not
   `__cxa_atexit`, `__cxa_finalize` or `__llvm_libc_errno`, which stay HIDDEN (M6). An explicit export list generated
   from the dotnet entrypoint list doubles as the reviewable, append-only C ABI of c.dll that the user's decision
   requires.
9. **Undefined symbols.** Late binding step 2 (native libraries) does not exist on dotnet. Native library inputs are a
   loud error, not skipped, and there are no data-import helpers. Weak undefined becomes null plus a trap stub (wasm's
   `createUndefinedStub`). A strong undefined symbol gets today's `undefined symbol:` text plus a new dotnet
   name-class line for C library names that c.dll does not provide: before P-2 that is every OS-dependent function,
   which matches the user's "link errors until P-2".
10. **The record needs no change** (no v1 minor bump). Dotnet-ness is in the triple key, which must already be equal
    across inputs, so a host-triple object can never mix with a portable one. The data model rule (`setDataModel`)
    already gives 8,8,4,8 for msil64 and 4,4,4,8 for msil32.

## 1. Method and evidence

- Searched with grep for `isOSWindows|isOSLinux|isOSBinFormatELF|isOSBinFormatCOFF|getNativeFormatTriple|isMusl|
  isWindows*Environment|getArch` in `lld/MSIL`, `llvm/lib/Target/MSIL`, `llvm/lib/CLI`, `llvm/include/llvm/CLI`,
  `llvm/include/llvm/BinaryFormat/MSILLink.h`, `clang/lib/Driver/ToolChains/MSIL.cpp` and
  `clang/lib/Basic/Targets/MSIL.h` (81 hits, `triple-greps.txt`), and read each site.
- Read plan 5.8.8-5.8.10 (record, object-mode constructs), 5.9.5-5.9.10 (resolution, late binding, layout,
  trailer), 5.9.15 (`-shared`), 5.14.2 (name classes) and M29 (5.15.5 exit), the reorder recommendation and critic,
  and the user's decisions recorded in the plan's section 12 rows (2026-10-31, 2026-11-01).
- Searched compiler-rt/lib/builtins and libc/{src,hdr,include} for platform and arch chains
  (`libc-hdr-chains.txt`).
- Read lld/wasm: `Writer.cpp` (`calculateInitFunctions`, `createCallCtorsFunction`, `addStartStopSymbols`,
  `createCommandExportWrapper`), `Symbols.cpp` (`Symbol::isExported`), `Driver.cpp` (exportDynamic default,
  `getUnresolvedSymbolPolicy`, `__dso_handle`, `__wasm_call_dtors`), `Relocations.cpp` (`reportUndefined`,
  `allowUndefined`), `SymbolTable.cpp` (`createUndefinedStub`), plus `llvm/lib/MC/WasmObjectWriter.cpp`
  (`.init_array` folding, `.fini_array` fatal), `TargetLoweringObjectFileWasm::getStaticDtorSection` (fatal: dtors
  must be lowered) and `llvm/lib/Transforms/Utils/LowerGlobalDtors.cpp`.
- Measurements M1-M8 used `<build>/bin` read-only (clang, opt, llc, msil-ld, llvm-cil-dump) and `dotnet`
  (net10.0 runtimeconfig). The x86_64-unknown-linux-gnu-msil triple stands in for the portable triple, which does not
  exist yet. The behaviours measured (record contents, visibility, resolution, ordering) do not depend on the OS part
  of the triple.

## 2. Findings

### 2.1 What the toolchain derives from the object format or OS today

Classes: **LR** = a link rule the dotnet family must define; **NB** = native binding or platform-C-library behaviour,
which is off on dotnet; **ABI** = codegen ABI (target recon's scope, listed for completeness); **HOST** = depends on
the host OS, not the target.

| Site (file : function) | What it keys on | Today | Class | Dotnet choice (section 4) |
|---|---|---|---|---|
| `lld/MSIL/Exports.cpp : selectExports` | `isOSWindows()` | Windows: COMDAT members never auto-exported; dllexport-if-any, else everything | LR | D7: visibility + `--export` list; no dllexport mode |
| `lld/MSIL/Exports.cpp : mayBeExported` | none | not synthetic, GLOBAL/WEAK, DEFAULT/PROTECTED, not `<msil>`, not `??_C@`, not `--exclude-libs` | LR | unchanged |
| `lld/MSIL/Exports.cpp : getCRuntimeIdentity` | `-l` names | `c`, `msvcrt`, `ucrt`, ... or `none` | LR | D11: `c` (c.dll's own link: set explicitly) |
| `lld/MSIL/MarkLive.cpp : MarkLive::run` | `isOSWindows()` | non-Windows: start/stop sections are gc roots; COFF: explicit sections outside COMDATs are roots | LR | D5: start/stop roots as today |
| `lld/MSIL/MarkLive.cpp : isTableSection` | SectionRules | constructor/destructor tables retained | LR | D2/D3 |
| `lld/MSIL/OutputSegment.cpp : OutputImage::layout` | `isOSWindows()` | non-Windows: defines `__start_`/`__stop_` | LR | D5 |
| `lld/MSIL/OutputSegment.cpp : getConstructorArgs` | SectionRules | ELF: prefix of (int, char**, char**); COFF: none | LR+NB | D2: none |
| `lld/MSIL/Writer.cpp : Writer::Writer` | `isOSWindows()` | picks `SectionRules::COFF`/`ELF` | LR | `getLinkRules` |
| `lld/MSIL/SymbolTable.cpp : bindLateSymbols` (`isObjectSectionBoundary`, `isSectionBoundary`) | `isOSWindows()` | non-Windows: start/stop never bind natively | LR | D5 |
| `lld/MSIL/SymbolTable.cpp : bindLateSymbols` (Q21 warning) | none | warns for every object definition a library exports | LR | D7: skip when both are WEAK (question Q4) |
| `lld/MSIL/LTO.cpp : isSectionBoundary`, `isHiddenSectionBoundaryReference`, usedStartStop in `BitcodeCompiler` | `isOSWindows()` | non-Windows start/stop handling under LTO | LR | D5 |
| `lld/MSIL/LTO.cpp : findEnvpConstructor` | `isOSWindows()` | non-Windows: imports environ for envp constructors | NB | off (D2) |
| `lld/MSIL/InputFiles.cpp : ObjFile::getLinkerOptions`, `BitcodeFile` options | `isOSWindows()` of the record triple | Windows tokenization of `/DEFAULTLIB:"a b"` | LR | GNU tokenization (D10) |
| `lld/MSIL/Driver.cpp : isSupportedTarget` | arch x86_64/aarch64 + Linux/MSVC/MinGW | rejects other triples | LR | accept msil64-unknown-dotnet (msil32 refused until P-4) |
| `lld/MSIL/Driver.cpp : getLibraryCandidates` | MSVC / Windows / else | non-Windows: `libX.a`, `X.dll`, `libX.dll`, `libX.so` | LR+NB | D9: `X.dll`, `libX.dll`, then `libX.a`; no `.so`/`.lib` |
| `lld/MSIL/Driver.cpp : hasTripleKey` | arch/OS/env | input triple key equality | LR | unchanged (dotnet keys never match host keys) |
| `lld/MSIL/Driver.cpp` (`--ucrtbase-imports`) | `isOSWindows()` | Windows only | NB | error on dotnet (already, since not Windows) |
| `lld/MSIL/NativeLibraries.cpp : mayBindNatively` | `isOSWindows()` for `__atomic_*` | Linux binds libatomic | NB | false for every name on dotnet |
| `lld/MSIL/NativeLibraries.cpp : getNameClassLine` | `isOSWindows()` | N2 text per OS | LR | D8 dotnet texts |
| `lld/MSIL/NativeLibraries.cpp : findDataImportFunctions` | `isOSWindows()` | kernel32 or dlopen/dlsym | NB | never reached on dotnet (D8) |
| `lld/MSIL/NativeLibraries.cpp : hasWideNativeLongDouble` | `!isOSWindows()` -> true | would wrongly say true for dotnet | NB | false on dotnet |
| `lld/MSIL/NativeLibraries.cpp : getOtherFormatMessage`, `isOtherFormat`, machine checks | `isOSWindows()`, arch | ELF vs PE/COFF native readers | NB | every native library is an error (D8) |
| `lld/MSIL/Relocations.cpp` (data-import helper) | `isOSWindows()` | Kernel32 vs Dlfcn helper | NB | never on dotnet |
| `lld/MSIL/AssemblyFiles.cpp : hasTripleKey` + C runtime check | triple key, identity | library compatibility | LR | unchanged; identity `c` |
| `llvm/lib/CLI/ImageLayout.cpp : classifySection`, `getOutputSectionName`, `getSectionPriority`, `operator<`/`==` on `ConstructorKey`, `layoutImage` | SectionRules ELF/COFF | ELF: `.init_array*`/`.fini_array*` tables, `.ctors`/`.dtors`/`.preinit_array` unsupported, `.N` priority grouping; COFF: `.CRT$XC*`/`.CRT$XT*`, `$` grouping | LR | D2/D3/D6: a `Dotnet` value |
| `llvm/lib/CLI/Trailer.cpp : skipsNullConstructorSlots` | SectionRules | COFF skips null slots | LR | Dotnet: null slot is the ELF error |
| `llvm/lib/CLI/TrailerDefs.cpp : isOfferedIn` | `isOSWindows()` (WindowsExecutable) | K2 scope | LR | unchanged (dotnet is not Windows) |
| `llvm/include/llvm/BinaryFormat/MSILLink.h : setDataModel` | `isOSWindows()` for LongSize | LP64 off Windows | ABI | unchanged: 8,8,4,8 / 4,4,4,8 |
| `llvm/lib/CLI/PEWriter.cpp : PEOptions::get` | arch | AMD64/ARM64 machine | ABI | loader recon (PE32/I386 ILONLY question) |
| `MSILAsmPrinter.cpp : getSectionRules`, `checkSections` (StartStop), `getConstructorArgs` | `isOSWindows()` | whole-program twin of the lld rules | LR | the same `getLinkRules` |
| `MSILAsmPrinter.cpp : getDataImportFunctions`, `getCLibrarySoname`, the DataImportHelper kind in `emitTrailer` | `isOSWindows()`, `isMusl()` | dlopen/dlsym from libc.so[.6] | NB | never on dotnet |
| `MSILPrepareIR.cpp` (start/stop section set, `isSupportedTarget`) | `isOSWindows()`, arch | | LR / ABI | `getLinkRules`; accept msil64 |
| `MSILUtilities.cpp : getCRuntime` | Linux/MSVC/MinGW | D14 import table | NB | `nullopt` -> standalone undefined is an error |
| `MSILISelLowering.cpp : getNativeVAListStructSize`, ldexp/frexp rule; `MSILISelLoweringCall.cpp` int128 by pointer | OS/arch | | ABI | target recon |
| `clang/lib/Basic/Targets/MSIL.h : MSILTargetInfo::getTargetDefines` | `getNativeFormatTriple().isOSBinFormatELF()` | `__MSIL__`, `__ELF__` on ELF natives | LR (preproc) | section 3 |
| `clang/lib/Driver/ToolChains/MSIL.cpp : addDefaultLibs`, `createMSILToolChain`, `-static` error, libgcc claims | Linux/MSVC/MinGW | `-lc -lm`, ucrt set, ... | NB | dotnet toolchain: `-lc` + builtins; `-static` portable-specific error |
| `lld/MSIL/Driver.cpp : getQuotingStyle` | host process triple | response-file quoting | HOST | unchanged |

The resolution core is format-neutral and stays as it is on dotnet: `Symbol::resolve` (the lld/ELF port, 5.9.5),
COMDAT Any/NoDeduplicate, aliases, commons, kind and TLS checks, weak undefined trap stubs (5.9.6 step 3, which is
also wasm-ld's `createUndefinedStub`), and the hidden-undefined rule.

### 2.2 compiler-rt/lib/builtins chains

Ranked by file, counting `#if` chains that test OS, format or arch macros: clear_cache.c 19, assembly.h 11,
int_math.h 8, int_endianness.h 8, crtbegin.c 8, int_types.h 7, int_lib.h 7, enable_execute_stack.c 6, int_util.c 5,
emutls.c 4, atomic.c 4. Most of the rest are one `CRT_HAS_128BIT` or `_MSC_VER` test.

| File : construct | Chain | On a dotnet triple without `__ELF__` | Action |
|---|---|---|---|
| `int_lib.h : COMPILER_RT_ALIAS` | `__ELF__ \|\| __MINGW32__ \|\| __wasm__ \|\| _AIX \|\| __CYGWIN__` -> alias; `__APPLE__` -> asm; `_WIN32 \|\| __UEFI__ \|\| __SPIRV__` -> no alias; else `#error Unsupported target` | **#error** (the critic's MUST-FIX) | add `\|\| defined(__dotnet__)` to the first branch. MSIL objects support `__attribute__((alias))` (ALIAS symbols, record 5.8.8), as measured for llvm-libc's aliases in M1 |
| `int_types.h : CRT_HAS_128BIT` | `__LP64__ \|\| __wasm__ \|\| __mips64 \|\| __SIZEOF_INT128__ \|\| _WIN64` | msil64: defined (`__LP64__`, `__SIZEOF_INT128__`); msil32: not unless `__int128` is enabled | none (the `*ti*` sources are excluded from the archive, category int128) |
| `int_types.h : HAS_80_BIT_LONG_DOUBLE`, `CRT_LDBL_128BIT`, `__powerpc64__` | `__i386__\|\|__x86_64__`, `__LDBL_MANT_DIG__ == 113` | 0 / not defined (long double = double, `__LDBL_MANT_DIG__` 53) | none |
| `int_endianness.h` | `__BYTE_ORDER__` first; then Solaris/BSD/Apple/`_WIN32` | `__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__` from the DataLayout | none |
| `int_util.c : __compilerrt_abort_impl` | `KERNEL_USE`, `__APPLE__`, `_WIN32`, `!__STDC_HOSTED__` | weak hidden, `__builtin_trap()` under `-ffreestanding` | none; add the dotnet triple to its `sources.txt` glob (today `*-linux-*`) and to the `-ftrapv` helpers' globs |
| `comparesf2.c`/`comparedf2.c` | `__ELF__` -> `__cmpsf2` libgcc alias; `_WIN32 && !__MINGW32__` -> wrapper functions | no `__cmp?f2` alias (nothing in LLVM's RuntimeLibcalls for MSIL calls it) | none |
| `fp_lib.h` | `__aarch64__ \|\| __arm64ec__` (TF mode), `CRT_HAS_F128` | generic | none |
| `udivmoddi4.c`, `int_math.h` | `_MSC_VER && !__clang__` | not taken | none |
| clear_cache.c, enable_execute_stack.c, emutls.c, atomic.c, crtbegin.c, gcc_personality_v0.c, os_version_check.c, apple_versioning.c | OS-specific | already excluded from `msil/lib/builtins/sources.txt` (hosted/atomic) | keep excluded. atomic.c stays out: the portable lock path is T11's interned-name Monitors. crtbegin.c's `__dso_handle` is replaced by msil-ld's synthetic (D4) |
| `msil/lib/builtins/msil_longdouble.c` | glob `*-linux-*,x86_64-*-windows-gnu-*` | must not apply: llvm-libc implements the `l` functions with long double = double | keep its glob as is |

Other `#error`s in builtins (`fp_extend.h`, `fp_trunc.h`, `fp_lib.h`, `int_endianness.h`) are precision or
endianness checks that a dotnet triple passes.

### 2.3 llvm-libc chains that key on OS, format or arch

- **Property macros** (`src/__support/macros/properties/os.h`, `architectures.h`): OS macros come from
  `__linux__`, `_WIN32`, `__FreeBSD__`, `__ANDROID__` and `__Fuchsia__`. `LIBC_TARGET_OS_IS_GPU`/`_BAREMETAL` come
  from CMake config definitions. Arch macros come from compiler macros. `LIBC_TARGET_ARCH_IS_VM` is keyed on
  `__CLR_VER` (MSVC C++/CLI) or GPU and only suppresses x86 detection. Usage counts over src/hdr/include: AARCH64 44,
  X86 36, ANY_RISCV 19, OS_IS_GPU 15, X86_64 14, ARCH_IS_GPU 14, ... WASM 4. With no x86/aarch64/riscv macro defined,
  every `#if LIBC_TARGET_ARCH_IS_*` dispatch falls to its generic `#else`. For example `inline_memcpy.h` falls to
  byte-per-byte unless MSIL joins the `GPU || WASM` builtin branch (which lowers to `cpblk`): libc recon.
- **`__ELF__` = baremetal** (the main finding for this report):
  - `src/__support/OSUtil/io.h`: `... #elif defined(__ELF__)` -> `baremetal/io.h` ("TODO: Ideally we would have
    LIBC_TARGET_OS_IS_BAREMETAL").
  - `src/stdlib/abort_utils.h`: `#elif defined(__ELF__)` -> `baremetal/abort_utils.h`.
  - `include/llvm-libc-macros/time-macros.h`: `#elif defined(__ELF__)` -> `baremetal/time-macros.h`.

  With `__ELF__` the portable libc would silently compile against baremetal hooks (`__llvm_libc_stdio_write` and
  friends), which is exactly the interim OS layer the user ruled out. Without it, io.h includes nothing, and
  `write_to_stderr` users fail to compile until P-2 adds a `__dotnet__` branch. That is loud.
- **Headers keyed on `__linux__`/`__APPLE__`** (about 70 sites in `include/llvm-libc-macros`,
  `include/llvm-libc-types` and `hdr/`, `libc-hdr-chains.txt`). On dotnet most fall back to generic definitions
  (`hdr/errno_macros.h` -> `generic-error-number-macros.h`) or to nothing (OS-specific macro headers: fcntl, signal,
  sys/*). Hard `#error`s on unknown arch: `include/llvm-libc-types/fenv_t.h` ("fenv_t not defined for your
  platform") and `__jmp_buf.h`. Both need a dotnet branch (libc recon; the reorder libc probe stubbed fenv_t with a
  `pre.h`).
- **Public symbol visibility** (`src/__support/macros/config.h : LIBC_NAMESPACE_DECL`,
  `src/__support/common.h : LLVM_LIBC_FUNCTION_IMPL_4`): the namespace is `[[gnu::visibility("hidden")]]`. The public
  C name is `__<name>_impl__ asm("<name>")`, declared inside it, with the hook `LLVM_LIBC_FUNCTION_ATTR` (also
  per-name `LLVM_LIBC_FUNCTION_ATTR_<name>`); `LLVM_LIBC_VARIABLE_ATTR` is the same for variables. Plain `extern "C"`
  definitions inside the namespace (`__llvm_libc_errno` in `src/errno/libc_errno.cpp`, `__cxa_atexit` and
  `__cxa_finalize` in `src/stdlib/atexit.cpp`, `__stack_chk_fail`) get no hook and stay hidden (M1, M6). CMake adds
  `-fvisibility=hidden` only for GPU (`LLVMLibCCompileOptionRules.cmake`). llvm-libc's full build ships only static
  archives, so upstream never needed exported names.
- **Constructors**: none in `libc/src` (grep for `constructor`/`destructor` attributes: 0 hits). The code is built
  with `-Wglobal-constructors`. `libc/startup/{linux,baremetal}` walk `__init_array_start`/`__fini_array_start`, which
  are linker-script symbols, not `__start_X`; the portable target uses none of that startup code. M8 scanned the 829
  objects of the libc recon's dotnet-flag build for INIT/FINI entries: see section 5.

### 2.4 lld/wasm's rules, the closest analogue

| Rule | lld/wasm (wasm32-wasi) | msil-ld today, non-Windows | Proposed dotnet |
|---|---|---|---|
| Constructors | `WASM_INIT_FUNCS` from `llvm.global_ctors` and folded `.init_array[.N]` (`WasmObjectWriter`); `Writer::calculateInitFunctions` stable-sorts by priority; error "constructor functions cannot take arguments"; synthetic `__wasm_call_ctors`, called by crt1's `_start` or command export wrappers | INIT entries + `.init_array*` slots, stable sort by ELF key, slots before entries per object; (argc, argv, envp) allowed; run from `.cctor` | INIT + slots, same order; **no arguments**; run from `.cctor` |
| Destructors | `LowerGlobalDtorsPass` -> `__cxa_atexit` in a ctor (`getStaticDtorSection` is fatal); `.fini_array` fatal in the object writer; `__wasm_call_dtors` comes from libc | D26 error until M29 (`.fini_array`, FINI entries) | `LowerGlobalDtorsPass`; `.fini_array` error |
| `__dso_handle` | `addOptionalDataSymbol("__dso_handle")`, set to the data start | M29.d plans a hidden synthetic (ELF) | hidden synthetic per assembly from P-1a |
| `__start_`/`__stop_` | `addStartStopSymbols`: C-identifier output segments, optional data symbols | C-identifier output sections, when referenced | same |
| Exports | executable: `--export`, `export_name`, `--export-dynamic` (default off); `-shared`: exportDynamic default on (non-hidden), all live non-hidden weak; `forceExport` from `--export` overrides hidden | Windows: dllexport-if-any; else default-visibility GLOBAL/WEAK | default-visibility GLOBAL/WEAK + `--export`/`--export-if-defined` (force, may be hidden) |
| Undefined | `--unresolved-symbols` (default report-all = error), `--allow-undefined`, `--import-undefined`; explicit `import_module` symbols are always allowed (`allowUndefined`) | late binding: managed libs, native libs, weak -> trap stub, error | managed libs only, weak -> trap stub, error + dotnet class line |
| Weak undefined function | trap stub with the call's signature | trap stub `'<msil>undef_<name>'` | same |

## 3. Proposal: predefines for msil64-unknown-dotnet (and msil32)

The standalone portable TargetInfo is the target recon's job. These are the macros the link rules and the
compiler-rt and llvm-libc chains depend on.

| Macro | msil64-unknown-dotnet | msil32-unknown-dotnet | Why |
|---|---|---|---|
| `__MSIL__` | 1 (kept) | 1 | "compiled to CIL": all MSIL triples define it today (`MSILTargetInfo::getTargetDefines`) |
| `__msil64__` / `__msil32__` | 1 / - | - / 1 | the portable architecture, like `__wasm32__`/`__wasm64__`. The key for libc's `LIBC_TARGET_ARCH_IS_MSIL`. **Not** `__MSIL__`, which host triples define next to `__x86_64__` (both arch macros would then be set; the reorder recommendation's "keyed on `__MSIL__`" is corrected here) |
| `__dotnet__` | 1 | 1 | the portable OS, like `__wasi__`. The key for compiler-rt's alias branch and libc's `LIBC_TARGET_OS_IS_DOTNET` |
| `__ELF__`, `__unix__`, `__unix`, `__linux__`, `_WIN32`, `_WIN64`, `__APPLE__`, `__MACH__` | absent | absent | user decision; `__ELF__` would select llvm-libc's baremetal layer (2.3) |
| `__x86_64__`, `__amd64__`, `__aarch64__`, `__i386__`, `__SSE*__`, `__MMX__`, `__code_model_small__`, `__SEG_FS`/`__SEG_GS`, `__FLOAT128__`, `__SIZEOF_FLOAT128__`, `__GCC_ASM_FLAG_OUTPUTS__` | absent | absent | no host architecture. A standalone TargetInfo never defines them (the libc recon had to `-U` them over the host triple) |
| `_LP64`, `__LP64__` / `_ILP32`, `__ILP32__` | defined / - | - / defined | generic `InitializePredefinedMacros` from int/long/pointer widths (`InitPreprocessor.cpp`) |
| `__SIZEOF_POINTER__`, `__SIZEOF_LONG__`, `__SIZEOF_SIZE_T__`, `__POINTER_WIDTH__` | 8, 8, 8, 64 | 4, 4, 4, 32 | DataModel in the record (`setDataModel`) must match |
| `__SIZEOF_WCHAR_T__`, `__WCHAR_TYPE__` | 4, `int` | 4, `int` | user decision (32-bit signed); record WCharSize 4 |
| `__WINT_TYPE__`, `__SIZEOF_WINT_T__` | `unsigned int`, 4 (recommended) | same | glibc/musl and llvm-libc's `WEOF 0xffffffffu`; TargetInfo's default is `SignedInt` (wasm keeps `int`): question Q7 |
| `__SIZEOF_LONG_DOUBLE__`, `__LDBL_MANT_DIG__` | 8, 53 | 8, 53 | record LongDoubleSize 8 (D3) |
| `__SIZEOF_INT128__` | 16 | absent unless the target enables `__int128` on msil32 | `TargetInfo::hasInt128Type` (pointer width >= 64) |
| `__BYTE_ORDER__`, `__ORDER_LITTLE_ENDIAN__`, `__LITTLE_ENDIAN__` | LE | LE | DataLayout `e`; the trailer must also refuse a big-endian runtime (risk R3) |
| `__USER_LABEL_PREFIX__`, `__REGISTER_PREFIX__` | empty | empty | `SYMBOL_NAME` in int_lib.h; llvm-libc's `LIBC_TARGET_USES_LEADING_UNDERSCORE` must stay off |
| `__CHAR_UNSIGNED__` | absent (signed char) | absent | like x86, wasm and Windows; question Q7 |
| `__BIGGEST_ALIGNMENT__` | 16 | 16 | ImageLayout rule 5 aligns the image base to max(16, ...) |
| `__GCC_HAVE_SYNC_COMPARE_AND_SWAP_{1,2,4,8}` (no `_16`), `__GCC_ATOMIC_*_LOCK_FREE` 2 up to 8 bytes | yes | yes (CLR `Interlocked` on 8 bytes in 32-bit processes) | MaxAtomicInlineWidth 64, as `setMaxAtomicWidth` caps today |
| `__STDC_HOSTED__` | 1 (0 with `-ffreestanding`) | 1 | hosted C with a managed libc |
| `__pic__`/`__PIC__`/`__pie__` | absent by default | absent | an assembly has no PIC concept; the driver already claims `-pie`/`-fPIC` |

## 4. Proposal: the dotnet link-rule family

### D1 Selection

- Add `enum class LinkRules : uint8_t { ELF, COFF, Dotnet }` and `LinkRules getLinkRules(const Triple &)` to lib/CLI
  (`llvm/include/llvm/CLI/ImageLayout.h`, next to `SectionRules`, or a small new `LinkRules.h`). It returns COFF for
  `isOSWindows()`, Dotnet for the dotnet OS (or the msil32/msil64 arch), else ELF.
- Either extend `SectionRules` with `Dotnet` (`ImageLayout.cpp` and `Trailer.cpp` switch on it in 8 places) or
  derive SectionRules from LinkRules. Recommended: extend `SectionRules`, because every switch site must then say
  what Dotnet does and none can fall through silently.
- Add `bool bindsNatively(const Triple &)`, false for Dotnet, which every NB site in 2.1 asks before it reaches a
  native reader, the data-import helper, the envp import or the D14 table.
- Replace each `isOSWindows()` LR/NB site in 2.1 with these two. There are about 25 sites in 12 files, mostly hot
  files (5.13.2), so the hook points belong in P-1a's ownership map.

### D2 Constructors (C `__attribute__((constructor))`, C++ dynamic initialization, `.init_array` slots)

- **Collection**: as today. `llvm.global_ctors` becomes record INIT entries (wasm `WASM_INIT_FUNCS`), and 8-byte
  slots of `.init_array[.N]` input sections count as constructors with key N (65535 -> 65536, plain -> 65536). Clang
  puts C++ dynamic initializers (`_GLOBAL__sub_I_<file>`) and `constructor(N)` functions in `llvm.global_ctors`, so
  both arrive as INIT entries. `.init_array` support matches wasm, whose object writer folds those sections into
  init functions.
- **Order**: one stable sort by priority; ties keep input order (objects in link order, within an object its slots
  first, then its INIT entries). This is today's ELF order (5.9.9), which ld.lld and wasm-ld also give. Measured M3:
  `c1(200)`, then slot, then `c2(65535)`.
- **Signature**: `void()`, or a value the trailer pops. **No arguments.** The ELF prefix (int, char**, char**) is
  glibc's protocol, and its envp would need a native `environ`. A dotnet constructor with parameters gets
  `constructor '<f>' takes arguments, which msil64-unknown-dotnet does not pass` (wasm-ld: "constructor functions
  cannot take arguments"). `getConstructorArgs` in OutputSegment.cpp and MSILAsmPrinter.cpp returns `nullopt` for
  Dotnet, and `findEnvpConstructor` returns null.
- **Null slots**: the ELF error (`skipsNullConstructorSlots` is false for Dotnet).
- **Where they run**: `<Module>::.cctor` of an EXE, after relocations, library `ldtoken`s and cross relocations
  (5.9.10). In a `-shared` assembly they run in phase 2, after its `'<msil>Data'::.cctor`. The CLR's module
  initializer order makes c.dll's phase 1 complete before a program's cross relocations read c.dll's data, as M22
  already relies on.
- **llvm-libc**: contributes no constructors (2.3; M8). If a future llvm-libc version adds one, it runs in c.dll's
  phase 2.

### D3 Destructors, atexit and exit (and how M29 relates)

- **`llvm.global_dtors`**: on dotnet the MSIL pass pipeline (5.13.6) runs upstream's `LowerGlobalDtorsPass`
  (`llvm/lib/Transforms/Utils/LowerGlobalDtors.cpp`, as `WebAssemblyCodeGenPassBuilder` adds it) before MSILPrepareIR.
  It runs in object and whole-program mode alike. Each priority level gets a `call_dtors.N` function and a
  `register_call_dtors.N` constructor that calls `__cxa_atexit(call_dtors.N, null, &__dso_handle)`. The destructors
  then run at `exit` in reverse registration order, which reproduces ELF's `.fini_array` order: higher priority
  numbers first, and within a level, reverse order. Measured M4: the pass output compiles with today's llc to a CIL
  object, links, and runs on .NET 10 with exit code 123 (constructor, d1(65535), d2(300)). The function pointer handed
  to `__cxa_atexit` is the method's managed pointer (`ldftn`), and the libc calls it with `calli`, so no M28
  native-callable thunk is involved (consistent with "function pointers are managed on portable").
- **C++ static objects with destructors**: clang already emits `__cxa_atexit(dtor, obj, &__dso_handle)` from the
  dynamic initializer. `-fuse-cxa-atexit` must stay the dotnet ToolChain's default.
- **The record**: dotnet objects never carry FINI entries (the backend's D26 destructor error is unreachable there),
  so INIT_FINI is unchanged.
- **Destructor and pre-init sections**: `.fini_array[.N]`, `.preinit_array`, `.ctors*`, `.dtors*`, `.CRT$X*` are
  `section '<name>' in <file> is not supported for msil64-unknown-dotnet` (5.9.9's text with the triple; no silent
  drop, J6). For `.fini_array` add `; use __attribute__((destructor)) or atexit`. wasm makes `.fini_array` fatal too.
  `classifySection(…, Dotnet)` returns ConstructorTable for `.init_array*`, Unsupported for the rest, and Data
  otherwise.
- **Where the handlers run**: in c.dll's `exit`, which calls `__cxa_finalize(nullptr)`, then (with
  `LIBC_COPT_EXIT_FLUSH_STREAMS`) flushes the streams, then calls `internal::exit` (`libc/src/stdlib/exit.cpp`). So on
  dotnet `'<msil>Main'` is `exit(main(argc, argv))` whenever the link resolves `exit` (from c.dll or an object), and
  `ret main(...)` otherwise. That covers P-1b, where c.dll has no `exit`, and `-nostdlib` links. This is C17
  5.1.2.2.3 ("a return from the initial call to main is equivalent to calling exit") and the same shape as M29's
  exit-table `'<msil>Main'`, but it needs none of M29's machinery (no Register stub, runner, ProcessExit fallback or
  native callback). It must land with **P-2**: buffered stdout has to be flushed at exit for the first portable
  printf.
- **Before P-2**: `__cxa_atexit` and `exit` are OS-dependent in llvm-libc (they need a `Mutex`), so P-1b's c.dll may
  lack them. A program with destructors then fails with `undefined symbol: __cxa_atexit` plus the D8 class line,
  which is loud, with no interim mechanism.
- **M29 on dotnet**: not applicable. Its exit table, `'<msil>run_dtors'`, ExitRegistration forwarders, Framework
  `ProcessExit` fallback, `wmain` and `.preinit_array` are host-C-library features. P-1r adds a gating row ("M29's
  exit-table decision and destructor tables are off on dotnet; dotnet lowers destructors with LowerGlobalDtors").
  M29's msil-ld code must test `getLinkRules` (or `bindsNatively`), not `isOSWindows()`, so that Dotnet does not
  take its ELF (CxaAtexit/glibc) path.
- **M31 (thread_local destructors)**: on dotnet `__cxa_thread_atexit_impl` is c.dll's (P-3), not M31's
  `msil_thread_exit.c`. P-1r adds a gating row.
- **Hosts that never call `exit`** (a C# program using a C library assembly): handlers never run unless c.dll's OS
  layer subscribes `AppDomain.ProcessExit` to call `__cxa_finalize(nullptr)` and flush the streams. That is a P-2
  design item (question Q5).

### D4 `__dso_handle`

- msil-ld defines `__dso_handle` as a hidden 8-byte zero synthetic data symbol in each output's image when a live
  reference remains after archive extraction. That is wasm-ld's `addOptionalDataSymbol("__dso_handle")` and the same
  shape M29.d (4) plans for ELF. It is never exported (`mayBeExported` excludes `SyntheticData`).
- Under `--lto-emit-asm`, msil-ld adds `@__dso_handle = hidden global i64 0` to the merged module in the same case,
  as M29.d plans.
- Each assembly therefore registers with a distinct handle, so a future `__cxa_finalize(dso)` can tell assemblies
  apart. .NET does not unload assemblies outside collectible contexts, so in practice all handlers run at exit.
- Measured M5: today a C++ static destructor already fails with `undefined hidden symbol: __dso_handle`. M4's
  LowerGlobalDtors reference is `extern_weak hidden` and links as address 0, which loses the per-assembly identity.
- Must land in P-1a (C++ codegen tests with static objects) and cannot wait for M29.

### D5 Section start/stop symbols

- Supported on dotnet with today's non-Windows rule. When an undefined `__start_X`/`__stop_X` is referenced and
  output section X exists with a C-identifier name, msil-ld defines DATA symbols at its start and end. The sections
  they name are gc roots (lld/ELF `-z nostart-stop-gc`). LTO keeps definitions in such sections visible (usedStartStop
  in `BitcodeCompiler`). They never bind late and are never exported. Whole-program output (`-S`, llc,
  `--lto-emit-asm`) gives the same address fields.
- Precedent: lld/ELF, and lld/wasm's `addStartStopSymbols`. The rule belongs to msil-ld's own image layout
  (`cli::layoutImage`), so it is not "ELF-style" in any sense the triple has to claim.
- Scope: per assembly. `__start_X` in a `-shared` library sees only that library's sections, like a per-DSO ELF link.
- Hidden references: today's M21.a rule applies.

### D6 Output sections and grouping

- Grouping rule: output name = segment name, except `.init_array.N` -> `.init_array`. Groups are placed at their
  first segment. Inside a group, the `.N` priority order. This is today's ELF rule; lld/ELF's `getPriority` matters
  only for `.init_array.N` here. No `$` splitting (COFF only).
- wasm-ld additionally merges `.data.*`/`.rodata.*`/`.bss.*` into their bases. The record never names those (default
  segments are `.data`/`.rodata`/`.bss`, 5.8.8), so the difference cannot arise.
- TLS sections (`.tdata`/`.tbss`): today's error until P-3 lays out the per-thread template.

### D7 Exports of a `-shared` assembly (c.dll and user libraries)

- **Selection** = `--export` symbols (forced, may be hidden, must be defined: `symbol exported via --export not
  found: <s>`, wasm-ld's text) ∪ `--export-if-defined` symbols ∪ every `mayBeExported` definition (default or
  protected visibility, GLOBAL or WEAK, not `--exclude-libs`).
- There is no dllexport-if-any mode. Clang ignores `dllexport` off Windows (measured on
  x86_64-unknown-linux-gnu-msil: `warning: unknown attribute 'dllexport' ignored`, symbol GLOBAL DEFAULT without
  DLLEXPORT), so dotnet objects never carry DLLEXPORT,
  and `--export-all-symbols` keeps its Windows-only meaning.
- Export class, accessors and the symbol table attribute are as 5.9.15.
- **c.dll**: its build passes `--exclude-libs=ALL` (the helper archive is already `-fvisibility=hidden`, A7) and an
  export list file generated from the dotnet entrypoint list (`libc/config/dotnet/entrypoints.txt` in the libc
  recon's layout). The file also lists the non-entrypoint public names: `__llvm_libc_errno` (if errno mode is not
  EXTERNAL), `__cxa_atexit`, `__cxa_finalize`, `__cxa_thread_atexit_impl` (P-3) and `__stack_chk_fail`.
  - Reason: M1/M2 show that a plain `-shared` link of llvm-libc objects exports nothing. M6 shows that
    `LLVM_LIBC_FUNCTION_ATTR` default visibility still leaves `__cxa_atexit`/`__cxa_finalize` hidden.
  - The list needs no upstream libc patch.
  - The list is the C ABI of c.dll. Keeping it in the repo lets a test enforce the user's "append-only ABI contract
    between libc releases" (no removal, no signature change through the SIGNATURES descriptors in the symbol table).
  - Recommended syntax: `--export=<sym>` (repeatable) plus `--export-list=<file>` (one name per line; `#` comments),
    or plain response files of `--export=` lines.
- **COMDAT copies**: keep the ELF/wasm behaviour. WEAK default-visibility COMDAT members (C++ inline functions,
  template instantiations) are exported, so a program's `extern template` declaration can bind to a library's
  explicit instantiation (`weak_odr` in a COMDAT). Windows needs dllexport for that, and dotnet has none.
  - Cost: Q21's "defined in X and exported by Y" warning would fire for every inline function a C++ program shares
    with a C++ library.
  - Proposed refinement: on dotnet, skip that warning when the object's definition is WEAK and the library's export
    is flagged weak in its symbol table. Those are interchangeable ODR copies, and -Bsymbolic already makes each
    assembly bind its own (question Q4, since Q21 was a user decision).

### D8 Undefined symbols with no native libraries

- **Late binding on dotnet** (5.9.6): step 1 binds to managed libraries (c.dll and other toolchain assemblies) in
  command-line order. **Step 2 is skipped**: there is no `NativeIndex`, and every `mayBindNatively` and
  `findDataImportFunctions` path is unreachable. Step 3: weak undefined becomes address 0 plus a `'<msil>undef_<name>'`
  trap stub (unchanged; wasm-ld's `createUndefinedStub`). Step 4: a strong undefined symbol is the error.
- **Native inputs are refused loudly**:
  - A native ELF/PE shared library or import library on the command line or found by `-l`: `<path>: native library
    cannot be linked for msil64-unknown-dotnet, which binds no native libraries`.
  - An archive with no CIL or bitcode members: the same text, not today's `--verbose` skip note.
  - `--ucrtbase-imports`: already an error off Windows.
- **Data imports**: an undefined variable that no object or assembly defines is `undefined symbol: <v>`. There is no
  dlopen/dlsym helper and no environ import.
- **Explicit import attributes** (`import_module`/`import_name`, Q35): recommended as P/Invokes bound unchecked, as on
  wasm. Explicit means the user asked for a platform binding, and P/Invoke is a platform mechanism. Function-pointer
  parameters in such a signature are an error on dotnet (no native thunks). This is question Q3.
- **Report lines** (after `undefined symbol: <s>`, before `>>> referenced by`, as 5.14.2):
  - N1/N3: today's texts (the helper archive exists for msil64).
  - N2 `__atomic_*`: a dotnet text naming the lock path that the lockpath decision fixes (T11). That is the lockpath
    recon's text.
  - **New, N6 C library names**: when `TargetLibraryInfoImpl::getLibFunc(name)` knows the name (or it is on a short
    POSIX list):
    - with the C library linked: `>>> a C library function that the C library c.dll (msil64-unknown-dotnet) does
      not provide`;
    - with no C library: `>>> a C library function: link the C library (-lc)`.

    Before P-2 this is exactly the user's "OS-dependent functions are absent (link errors)". It lives in
    `getNameClassLine` (`NativeLibraries.cpp`, M24's never-copy rule) and is used by `getUnresolvedMessage` and LTO's
    `reportUnbound`.
- **N4 setjmp**: unchanged (the compiler lowers it, M27).
- **Standalone whole-program mode** (`-S`/llc without msil-ld): `getCRuntime` returns `nullopt` on dotnet, so an
  undefined function is Phase 1's undefined-symbol error, never a D14 P/Invoke.

### D9 Library search (`-l`)

- Dotnet candidates, `-Bdynamic` (default): `<name>.dll`, `lib<name>.dll`, then `lib<name>.a` (shared first,
  ld.lld's `.so`-before-`.a` order, so `-lc` finds c.dll even when the mode-3 `libc.a` sits next to it).
- `-Bstatic`: `lib<name>.a` only.
- Never `.so`/`.lib`/`.dll.a`.
- `-static` stays a portable-specific driver error until P-3 (user decision). The order matters from P-3 on.

### D10 Linker options in objects

`!llvm.linker.options`/LINKER_OPTIONS strings are one option each, with GNU tokenization (the non-Windows branch of
`ObjFile::getLinkerOptions` and `BitcodeFile`). `#pragma comment(lib, "m")` becomes `-lm`, and the driver provides an
empty or absent libm, because llvm-libc includes math in c.dll (libc recon decides whether `-lm` maps to `c`).

### D11 Record, symbol table, identity

- Link-info record v1: **no new field and no minor bump**. The family is derived from the header Triple, whose key
  (arch msil64, OS dotnet, environment) must already be equal across inputs (5.8.9), so a host-triple object or
  library can never join a portable link. This enforces "nothing shared with host-triple assemblies".
- DataModel stays as is: msil64 8,8,4,8; msil32 4,4,4,8 (`setDataModel`'s non-Windows LP64/ILP32 rule).
- Symbol table (MSYM): triple and data model unchanged. **C runtime identity**: programs that link with `-lc` record
  `c`. c.dll's own link has no `-lc`, so its identity is `none` today. Recommended: c.dll's build sets it explicitly
  to `c` (a small msil-ld option, `--c-runtime=c`). P-3's `-static` mode then needs an identity rule of its own
  (state recon): static copies and c.dll might share one C state or might not.

### D12 The trailer start-up sequence on dotnet

1. Phase 1 (`.cctor`, or `'<msil>Data'::.cctor` for `-shared`): allocate and align the image, copy the RVA image,
   zero the zero part, address fields, `'<msil>fn_'` fields, relocations or the loop. There are no
   `'<msil>init_dataimports'` and no native data.
2. The width and endianness guard. Loader recon: refuse a process whose `IntPtr.Size` differs from the triple's,
   and, per R3, `!BitConverter.IsLittleEndian`. It must run before the first image access.
3. Library `ldtoken`s (c.dll first, by command-line order) and cross-assembly relocations.
4. Constructors in D2's order. LowerGlobalDtors' registration constructors and C++ initializers that call
   `__cxa_atexit` are among them.
5. `'<msil>Main'`: argc/argv from `Environment.GetCommandLineArgs()` (BCL, Framework 4.8 and modern .NET), then
   `exit(main(...))` if `exit` resolved, else `ret`.

No ExitTable, ProcessExit subscription, K2 Windows command-line definitions or envp on dotnet.

### D13 Resolution rules

Unchanged on dotnet: the lld/ELF `Symbol::resolve` port, visibility merge, COMDAT, aliases (llvm-libc's
`[[gnu::alias]]` public names and compiler-rt's `COMPILER_RT_ALIAS` rely on them), commons, kind and TLS checks.
They follow the record's C semantics, not a native format. lld/wasm has the same rules for commons and weak symbols
(5.9.5's note).

## 5. Measurements

All commands run from `<scratch>/p0/linkrules/` with `B=<build>/bin` (HEAD's build,
read-only). Full output is in `measurements.txt`.

- **M1 entrypoint visibility.** Command: `vis/cc.sh string/strlen.cpp strlen-def.o` (the reorder probe's flags:
  x86_64-unknown-linux-gnu-msil, `-DLIBC_COPT_PUBLIC_PACKAGING`, host macros undefined), and the same with
  `'-DLLVM_LIBC_FUNCTION_ATTR=[[gnu::visibility("default")]]'`; then `llvm-cil-dump --link-info`. Result: without the
  attribute, `Symbol 0 FUNCTION GLOBAL HIDDEN 'strlen'` plus the HIDDEN ALIAS `_ZN16__llvm_libc_msil6strlenEPKc`;
  with it, `FUNCTION GLOBAL DEFAULT 'strlen'`. `errno/libc_errno.cpp` with the attribute: `__llvm_libc_errno` stays
  `GLOBAL HIDDEN`.
- **M2 `-shared` exports.** Command: `msil-ld -shared -o def.dll strlen-def.o` and `-o vis.dll strlen-vis.o`. Result:
  both rc 0. def.dll has no `strlen` method (gc'd, not exported). vis.dll has `public static 'strlen'`,
  `'<msil>addr_strlen'` and `'<msil>fn_strlen'`.
- **M3 constructor order today (ELF rules).** Command: `clang --target=x86_64-unknown-linux-gnu-msil -O1 -c
  m1/ctor3.c`, `msil-ld -o ctor3.exe`, `dotnet ctor3.exe` (net10.0 runtimeconfig). Inputs: `constructor(200)` c1, a
  `.init_array` slot, a default constructor c2, two 4-byte `myreg` globals, and `main` returning `(__stop_myreg -
  __start_myreg)*1000 + trace`. Record: `Init priority=200 'c1'`, `Init priority=65535 'c2'`, segment `.init_array`
  with a FNADDR64 reloc. Result: exit 196 = 8132 mod 256, so the start/stop difference is 8 and the order is c1, slot,
  c2.
- **M4 LowerGlobalDtors on the MSIL backend.** Command: `clang -S -emit-llvm dtors/prog2.c` (a constructor,
  `destructor` d1, `destructor(300)` d2), `opt -passes=lower-global-dtors`, `llc -filetype=obj`, link with `rt.c` (a
  stand-in `__cxa_atexit` list run by `finish()`), `dotnet d.exe`. Result:
  - The IR has `@__dso_handle = extern_weak hidden constant i8` and `__cxa_atexit(ptr @call_dtors.300, ptr null, ptr
    @__dso_handle)`.
  - The object has `Init priority=300 'register_call_dtors.300'`, `Init priority=65535 'register_call_dtors'` and
    `DATA WEAK HIDDEN UNDEFINED '__dso_handle'`.
  - The link succeeds and the run exits 123: constructor, then d1, then d2, which is ELF's destructor order.
- **M5 C++ static destructor.** Command: `clang -O1 -fno-exceptions -c dtors/s.cpp` (a static object with a
  destructor), `msil-ld -o s.exe s.o rt.o`. Result: `DATA GLOBAL HIDDEN UNDEFINED '__dso_handle'`, `__cxa_atexit`
  called from `_GLOBAL__sub_I_s.cpp`; link fails with `msil-ld: error: undefined hidden symbol: __dso_handle`.
- **M6 the libc recon's dotnet-flag objects** (`<scratch>/p0/libc/obj/all`, built with `-fvisibility=hidden
  -DLLVM_LIBC_FUNCTION_ATTR=__attribute__((visibility("default")))`, `-D__dotnet__ -D__msil64__`).
  `stdlib_atexit.cpp.o`: `atexit` GLOBAL DEFAULT, but `__cxa_atexit` and `__cxa_finalize` GLOBAL HIDDEN.
- **M7 today's undefined report.** Command: `msil-ld -o u.exe u.o` with `puts` and `sqrt` and no libraries. Result:
  `undefined symbol: puts` / `>>> referenced by u.o:(main)`, the same for `sqrt`. No class line exists for C library
  names.
- **M8 constructor scan of the libc objects.** Command: `llvm-cil-dump --link-info` over the 829 objects in
  `p0/libc/obj/all`, grepping for `^Init|^Fini`. Result: see the note at the end of this section.
- **Predefines.** `clang -dM -E` for wasm32-wasi, wasm64, x86_64-unknown-linux-gnu-msil and x86_64-pc-windows-msvc-msil
  (`*.macros` in scratch).
  - wasm: no `__ELF__`; `__USER_LABEL_PREFIX__` empty; `__WCHAR_TYPE__ int`; `__WINT_TYPE__ int`.
  - linux-gnu-msil: `__ELF__`, `__MSIL__`, `__linux__`, `__unix__`, `__x86_64__`, `__SSE2__`, `__WINT_TYPE__
    unsigned int`.
  - windows-msvc-msil: `_WIN32`, `_WIN64`, `__WCHAR_TYPE__ unsigned short`.

M8 result: the scan completed over all 829 objects (a sample of 25 dumped cleanly) and found **0 INIT and 0 FINI
entries**. So the freestanding and hosted llvm-libc sources that the libc recon compiled contribute no constructors
or destructors, and c.dll's phase 2 has no constructor list.

## 6. Minimal upstream patches

| Patch | Where | Content | Conflict surface |
|---|---|---|---|
| U1 | `compiler-rt/lib/builtins/int_lib.h` | `\|\| defined(__dotnet__)` in the `COMPILER_RT_ALIAS` alias branch | one line in a chain that changes rarely (last: SPIR-V, UEFI terms) |
| U2 | `libc/src/__support/macros/properties/os.h` | `#if defined(__dotnet__)` / `#define LIBC_TARGET_OS_IS_DOTNET` | new block |
| U3 | `libc/src/__support/macros/properties/architectures.h` | `#if defined(__msil32__) \|\| defined(__msil64__)` / `#define LIBC_TARGET_ARCH_IS_MSIL`; optionally add it to the `LIBC_TARGET_ARCH_IS_VM` condition | new block |
| U4 (libc recon) | `fenv_t.h`, `__jmp_buf.h`, `math-macros.h` (math_errhandling), `inline_mem*.h` (builtin branch), later `OSUtil/io.h`, `abort_utils.h`, `time-macros.h`, `exit` (P-2) | `__dotnet__`/`LIBC_TARGET_*_MSIL` branches | per header |
| L1 (LLVM, ours) | the MSIL pass pipeline (5.13.6) | `LowerGlobalDtorsPass` when `getLinkRules(TT) == Dotnet` | MSIL-owned file |
| none | llvm-libc visibility | none: c.dll uses msil-ld's export list (D7) | - |

`msil/lib/builtins/sources.txt`: add the dotnet glob to `int_util.c` and the `-ftrapv` helpers. `build-builtins.py`
gains msil64-unknown-dotnet in its triple list.

## 7. Where each piece lands (option A order)

- **P-1a** (toolchain, no libc):
  - D1 selector and every LR/NB site.
  - D2 (no-argument constructors).
  - D3's LowerGlobalDtors and section errors.
  - D4 `__dso_handle`.
  - D5/D6 as today.
  - D7's `--export`/`--export-list`.
  - D8 (native inputs refused, N6 line, step 2 off).
  - D9 order; D10.
  - U1 and the predefines.
  - Tests: constructor order, `.init_array`, start/stop, `-shared` exports with and without `--export`, a C++ static
    destructor linking against a stand-in `__cxa_atexit` object, a native library refused, the N6 line.
- **P-1b** (c.dll):
  - U2/U3 (+U4 freestanding parts).
  - c.dll built with `--exclude-libs=ALL`, the generated export list and `--c-runtime=c`.
  - A test that the export list is append-only against the previous release list.
- **P-1r**: gating rows. M28 thunks off on dotnet. M29's exit table and destructor tables off on dotnet (D3). M31's
  thread-exit library off on dotnet (P-3 owns it). M30's `__atomic_*` binding follows T11 on dotnet. Every M27-M35
  hook that tests `isOSWindows()` to mean ELF must use `getLinkRules`.
- **P-2**: `'<msil>Main'` = `exit(main())` when `exit` resolves (D12 step 5); c.dll's `exit`/`atexit`/`__cxa_atexit`
  and the stdio flush; the ProcessExit question (Q5).
- **P-3**: TLS template (D6), `__cxa_thread_atexit_impl`, `-static` and its identity (D11), `-Bstatic` order (D9).
- **P-4**: msil32 (same family; ILP32 predefines).

## 8. Risks

- **R1 Silent ELF fallthrough.** Any `isOSWindows()` site left unconverted silently gives dotnet ELF or Linux
  behaviour. Examples: `hasWideNativeLongDouble` returns true, envp constructors import `libc.so.6` environ, `.so`
  candidates. Mitigation: D1's enum with no default in switches, and a P-1a grep gate that fails on `isOSWindows()`
  in lld/MSIL, lib/CLI and MSIL target files outside an allowlist.
- **R2 Export ABI drift.** Without an explicit list, c.dll's exports depend on upstream visibility details (M1, M6):
  an upstream change to `LIBC_NAMESPACE_DECL` or `LLVM_LIBC_FUNCTION` silently adds or removes exports. The export
  list plus the append-only test turns that into a test failure.
- **R3 Big-endian .NET.** .NET runs on big-endian s390x (IBM's port). The trailer's image copy, relocations and every
  `ldind`/`cpblk` assume little-endian layout, so a portable assembly would compute garbage there. The width guard
  must also check `BitConverter.IsLittleEndian` (loader recon).
- **R4 LowerGlobalDtors vs. a later FINI design.** If the user later wants `.fini_array` support on dotnet, it would
  need a different mechanism: FINI entries plus an M29-like runner registered with c.dll's `__cxa_atexit`. Choosing
  LowerGlobalDtors now does not block that.
- **R5 `__dso_handle` duplication.** P-1a defines it for dotnet while M29.d defines it for ELF later. Both must share
  one implementation (`Synthetic.cpp`), so P-1a should write the general one and M29.d reuse it (an ownership row).
- **R6 Q21 noise for C++ libraries** if D7's refinement is not accepted (Q4).
- **R7 Interim OS layer.** The reorder recommendation's P-1b text ("OSUtil/dotnet stubs: write_to_stderr and exit call
  `__builtin_trap`") contradicts the user's "OS-dependent functions are absent (link errors)". With no `__ELF__` and
  no stubs, the OS-dependent entrypoints simply stay off the entrypoint list (libc recon to confirm).
- **R8 Constructor arguments.** Rust std's Linux argv capture (the plan's Rust std notes) relies on glibc-style
  constructor arguments. A future portable Rust target would need its own std path; refusing them on dotnet is the
  wasm precedent.

## 9. Questions for the user

- **Q1 Export mechanism for c.dll.** The options:
  - (a) msil-ld `--export`/`--export-list` with a generated, checked-in export list;
  - (b) upstream-style visibility: `LLVM_LIBC_FUNCTION_ATTR` default plus libc patches marking `__cxa_atexit`,
    `__cxa_finalize`, `__llvm_libc_errno` and `__stack_chk_fail` default;
  - (c) both.

  Recommendation: (a). There is no upstream libc patch, and the list doubles as the append-only ABI contract.
- **Q2 Destructors on dotnet.** The options:
  - (a) LowerGlobalDtors to `__cxa_atexit` (wasm), with `.fini_array` an error;
  - (b) FINI entries plus an M29-style runner registered with c.dll's `__cxa_atexit`, which also supports
    `.fini_array`.

  Recommendation: (a), available from P-1a with an upstream pass and no trailer exit table.
- **Q3 Explicit import attributes on dotnet.** The options:
  - (a) allowed as unchecked P/Invokes (wasm `import_module` analogue), with function-pointer parameters refused;
  - (b) refused (strictly no native code on portable).

  Recommendation: (a). The user asked for that binding explicitly, and P/Invoke is a platform mechanism.
- **Q4 Q21's shadow warning for ODR copies on dotnet.** The options:
  - (a) skip it when the object's definition and the library's export are both WEAK;
  - (b) keep Q21 as decided (warn for every shared inline function).

  Recommendation: (a).
- **Q5 Exit handlers when the host is not a C program.** The options:
  - (a) c.dll's P-2 OS layer subscribes `AppDomain.ProcessExit` to run `__cxa_finalize(nullptr)` and flush the
    streams;
  - (b) handlers run only via `exit`.

  Recommendation: (a), decided in P-2 with the state recon (run-once flag).
- **Q6 Constructor arguments on dotnet.** The options:
  - (a) none (wasm);
  - (b) (argc, argv) from the trailer's existing argv, no envp.

  Recommendation: (a).
- **Q7 `wint_t` and `char` signedness.** The options:
  - (a) `wint_t` = `unsigned int` and signed `char`;
  - (b) wasm's `int` `wint_t`.

  Recommendation: (a). It matches glibc/musl, llvm-libc's WEOF and the host x86_64 triples.

## 10. Corrections to earlier notes

- The reorder recommendation keys `LIBC_TARGET_ARCH_IS_MSIL` on `__MSIL__`. Use `__msil64__`/`__msil32__`, because
  host MSIL triples define `__MSIL__` next to `__x86_64__`/`__aarch64__`.
- The task text says llvm-libc uses C++ static constructors. Its sources have none and are built with
  `-Wglobal-constructors`. Its startup code (not used on dotnet) walks linker-script `__init_array_start`, not
  `__start_X`.
- The critic's int_lib.h MUST-FIX offered "define `__ELF__` or patch int_lib.h". The user chose the patch, and
  llvm-libc's `__ELF__`-means-baremetal dispatch (2.3) independently shows that `__ELF__` would have been wrong.

## Verifier corrections

Adversarial check of this report, 2026-11-01, at the same HEAD. Probes and outputs are in
`<scratch>/p0/verify-linkrules/`. Nothing else in the repository was changed. The report's text above is
left as written. Where this section disagrees with it, this section wins.

### Errors

- **V1 (D1, R1): extending `SectionRules` does not make sites fail loudly. Every use is an `==` comparison.** No
  code switches on `SectionRules`. All 13 uses compare with `==`:
  - `ImageLayout.cpp` (`classifySection`, `getOutputSectionName`, `getSectionPriority`, the `ConstructorKey`
    operators, `layoutImage`) tests `== SectionRules::ELF`.
  - `Trailer.cpp : skipsNullConstructorSlots`, `OutputSegment.cpp : getConstructorArgs` and `MarkLive::run` test
    `== SectionRules::COFF`.

  A new `Dotnet` enumerator would therefore silently take the COFF branch at every `== ELF` test (for example, no
  `.init_array` constructor tables and `$` grouping) and the ELF branch at every `== COFF` test, which is a
  COFF/ELF mix. The report also missed sites that hard-code ELF:
  - `lld/MSIL/LTO.cpp : findEnvpConstructor` declares `constexpr cli::SectionRules rules = cli::SectionRules::ELF`.
  - `llvm/lib/Target/MSIL/MSILDataSegment.h : finalize` defaults its argument to `SectionRules::ELF`, and
    `MSILAsmPrinter::emitObjectEnd` calls `Data->finalize()` with no argument.
  - `MarkLive::run` chooses its own rules from `isOSWindows()`.

  Fix: turn each comparison into an exhaustive `switch`, or into named predicates on the rule set (for example
  `hasInitArrayTables`, `skipsNullSlots`, `groupsByDollar`). The P-1a grep gate must also flag
  `== SectionRules::`/`!= SectionRules::` and the default argument, not only `isOSWindows()`.
- **V2 (D9): reordering `getLibraryCandidates` does not make `-lc` pick c.dll over `libc.a`.**
  `lld/MSIL/Driver.cpp : searchLibraryBaseName` returns a `LibraryKind::Static` candidate as soon as it finds it in
  a directory. Candidate order only breaks ties between managed and native candidates. This is Design.md 5.2's
  static-first rule. D9 must change the kind precedence in `searchLibraryBaseName` (under `-Bdynamic`, managed
  before static), not just the list order. The direction is already approved: Design.md 5.2's `-lfoo` paragraph and
  the plan's section 12 row of 2026-11-01 ("C library linking modes") both say that `-l` turns shared-first once the
  toolchain ships shared libraries of its own. D9 should cite them. Neither text is limited to particular triples,
  so the plan must say whether the switch is dotnet-only or applies to every triple.
- **V3 (2.3, D7, M6, Q1): `__stack_chk_fail` is not hidden, and it does not belong in c.dll's export list.**
  `libc/src/compiler/generic/__stack_chk_fail.cpp` defines it in a file-scope `extern "C"` block, outside
  `LIBC_NAMESPACE_DECL`. Re-measured with M1's flags (`vis/cc.sh`, no `-fvisibility=hidden`): `__stack_chk_fail` and
  `__stack_chk_guard` are GLOBAL DEFAULT. M6 cannot show which names the namespace hides, because the libc recon
  built its objects with `-fvisibility=hidden`. Re-run without that flag, `__cxa_atexit` and `__cxa_finalize` are
  still GLOBAL HIDDEN (namespace attribute) and `atexit` is DEFAULT, so the D7/M6 conclusion holds for those names.
  `__stack_chk_fail` should still be dropped from the list:
  - MSIL has no stack protector. The `Clang.cpp` driver does not enable one for MSIL, and MSILPrepareIR strips an
    explicit one.
  - It calls `write_to_stderr` and `abort_utils::abort`, which are OS-dependent, so it cannot exist before P-2.
- **V4 (D3 "Before P-2"): the stated reason for `__cxa_atexit` being OS-dependent is wrong.** Under
  `LIBC_THREAD_MODE_SINGLE` (the reorder's P-1b config and the libc recon's flags), `Mutex` is a passthrough
  (`src/__support/threads/mutex.h`), so the mutex is not the dependency. The real one is the heap:
  `ExitCallbackList` is a `ReverseOrderBlockStore<AtExitUnit, 32>` (`src/stdlib/exit_handler.h`), and its
  `new_obj()` allocates further blocks with `new (ac)` (`src/__support/blockstore.h`), which needs `malloc`/`free`.
  `exit` needs `internal::exit`, which `src/__support/OSUtil/exit.h` only declares. The conclusion stands (both are
  absent before P-2, and a program with destructors fails to link loudly). The reason should be "heap and
  `internal::exit`", not "a Mutex".
- **V5 (D12): the start-up sequence contradicts itself.** Step 2 (the width and endianness guard) "must run before
  the first image access", but it comes after step 1, which allocates, copies and relocates the image using
  pointer-width assumptions. The guard must be step 0: the first code in `<Module>::.cctor`, or in
  `'<msil>Data'::.cctor` for `-shared`.

### Gaps

- **G1 Leaving out `__ELF__` depends on the Triple's object format, not only on TargetInfo macros.**
  `clang/lib/Frontend/InitPreprocessor.cpp` defines `__ELF__` whenever `getTriple().isOSBinFormatELF()`. Host MSIL
  triples avoid that only because their ObjectFormat is `Triple::MSIL` (parsed from the `-msil` suffix). For
  `msil64-unknown-dotnet` there is no suffix, so the format comes from `Triple.cpp : getDefaultFormat`. P-1a must add
  msil32/msil64 there, returning `Triple::MSIL`. The switch has no default, so `-Wswitch` warns but does not fail
  the build. `getNativeFormatTriple()` of a portable triple then returns the triple itself, still MSIL. Its callers
  (`MSILTargetInfo::getTargetDefines`, `SemaDeclAttr.cpp`'s alignment limit, lld's key checks) need a dotnet case.
  This belongs to the target recon, but section 3 should name it as the precondition for "no `__ELF__`". Another OS
  site that was missed and is harmless: `clang/lib/Parse/ParsePragma.cpp`'s `#pragma comment` kind rule tests
  `isOSWindows()`, so dotnet gets 'lib' only, which matches D10.
- **G2 `'<msil>Main'` = `exit(main())` duplicates M29.** M29.d's "library case" and `TrailerExit.cpp`'s
  `EntryPoint::Exit` plan the same rule for host triples, and M29 lands after P-2. This is the same ownership risk
  as R5 (`__dso_handle`). P-1r needs a row saying that M29 reuses P-2's entry code.
- **G3 Q3 needs a guard.** `clang/lib/CodeGen/CodeGenModule.cpp : addMSILImportAttributes` already turns
  `import_module`/`import_name` into P/Invokes on every MSIL triple. If Q3 (a) is chosen, the plan must also say
  that c.dll and every toolchain library carry 0 `pinvokeimpl` (the reorder's P-1b test). Otherwise a P/Invoke
  becomes a route to the native OS layer that the user ruled out ("OS layer is C/C++ calling System.* through managed
  calls"). The user should also hear that a P/Invoke makes that assembly platform-dependent.
- **G4 msil32 (P-4) constructor slots.** D2 and the LTO comment in `findEnvpConstructor` assume 8-byte
  function-address leaves in `.init_array`. On msil32 they are 4-byte slots with 32-bit relocations. P-4's entry
  should list this, together with `setDataModel`'s ILP32 path.
- **G5 D4's trigger.** LowerGlobalDtors references `__dso_handle` as `extern_weak` (M4). D4 must say that a
  weak undefined reference also triggers the synthetic definition. Otherwise it binds to 0, which is exactly the
  identity loss D4 describes.
- **G6 An alternative was not discussed.** Clang's `-fregister-global-dtors-with-atexit` (the Darwin default)
  registers destructors from a constructor in the frontend. LowerGlobalDtors is still preferable, because it also
  covers IR from other frontends and works under LTO, but Q2 should mention the alternative.

### Confirmed by re-measurement or re-reading

- M2: `msil-ld -shared` of the hidden `strlen` exports nothing, and with the attribute it exports `strlen`,
  `'<msil>addr_strlen'` and `'<msil>fn_strlen'`.
- M3: exit code 196 (c1, slot, c2; the start/stop difference is 8).
- M4: `opt -passes=lower-global-dtors` gives `extern_weak hidden` `__dso_handle` and `register_call_dtors.N`
  constructors, and the program exits 123.
- M5: `undefined hidden symbol: __dso_handle`.
- New, cross-assembly variant of M4: the stand-in `__cxa_atexit` linked as a separate `-shared` `rt.dll` also exits
  123 (`xasm/`). Managed function pointers passed to another assembly's `__cxa_atexit` and called from there work.
- M8: there are now 834 objects in `p0/libc/obj/all`. All of them dump, with 0 Init and 0 Fini entries.
- compiler-rt: with the host macros undefined and `-D__dotnet__ -D__msil64__`, the 88 listed builtins sources
  compiled as follows (`crt-check.sh`; `msil_longdouble.c` is not in compiler-rt and is not for dotnet):
  - unpatched: only `fp_mode.c` compiles; the rest stop at `int_lib.h:#error Unsupported target`;
  - with U1 applied to a copy (`crt/`): all 88 compile, `int_util.c` and the `-ftrapv` helpers included.

  U1 is the only compile blocker.
- llvm-libc has `__ELF__` in exactly the three cited headers. With `__ELF__`, `__stack_chk_fail.cpp` compiles against
  the baremetal `write_to_stderr`. Without it, the compile fails (loud, though the message, "undeclared identifier
  `__llvm_libc_msil`", is unhelpful).
- `LIBC_NAMESPACE_DECL` is hidden (`config.h`), and so is the `LLVM_LIBC_FUNCTION_ATTR` hook.
- lld/wasm:
  - `Symbol::isExported`: `forceExport` exports even hidden symbols; `-shared` sets `exportDynamic` by default;
    `__dso_handle` uses `addOptionalDataSymbol`.
  - `calculateInitFunctions` reports "constructor functions cannot take arguments" and uses `stable_sort`.
  - `WasmObjectWriter` treats `.fini_array` as fatal.
  - `WebAssemblyCodeGenPassBuilder` adds `LowerGlobalDtorsPass`.
  - `TargetLoweringObjectFileWasm::getStaticDtorSection` is fatal.
- Other re-read facts:
  - `TargetInfo` defaults `WIntType` to SignedInt, and llvm-libc's `WEOF` is `0xffffffffu`.
  - `hasWideNativeLongDouble` returns true off Windows.
  - Today's non-Windows candidate list is `libX.a, X.dll, libX.dll, libX.so`.
  - `mayBeExported` excludes `SyntheticData`.
  - M29.d (4) plans `__dso_handle` for ELF only.
  - The reorder recommendation keys `LIBC_TARGET_ARCH_IS_MSIL` on `__MSIL__` and describes `__builtin_trap` OSUtil
    stubs (R7 is valid).
  - All functions cited in the 2.1 table exist in the cited files.
