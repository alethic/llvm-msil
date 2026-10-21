# M11 recon: items 6 and 7 (fork hooks in upstream files)

Recon agent for plan section 12 row 'Deferred after the upstream merge' (2026-10-19), items
(6) "fork hooks with no test or no gate build" and (7) "no gate runs the upstream regression
tests for the upstream files the fork hooks". The merge review's evidence is lost; everything
below was re-established on 2026-10-20 at HEAD 0c5044bac349 (branch `msil`, upstream base
64f50ffd4f1c). No repository file was edited; the shared build D:\llvm-msil\build was only used
to run its tools; every lit run went into a private exec root.

Artifacts (git-ignored, persistent): `D:\llvm-msil\msil\orchestration\M11\recon\hooks-artifacts\`
(scripts, the draft manifest `upstream-hooks.txt`, its prototype checker `check_manifest.py`, the
prototype `proto-hooks-compile.ps1`, the proposed test `cc1as-unsupported.s`, logs). Private
build tree: `C:\llvm-msil-hooks-build` (kept for reuse by the gate, 464 MB now).

## 0. Verdicts

| Item | Reproduced | Finding |
|---|---|---|
| 6 (AsmParser.cpp IsMSIL) | yes | The hook is in **llvm/lib/MC/MCParser/AsmParser.cpp:808-813** (MC assembly parser), not in llvm/lib/AsmParser (the IR parser, which the fork does not touch; `git diff --name-only 64f50ffd4f1c HEAD -- llvm/lib/AsmParser` is empty). No test reaches it (`assembly parsing is not supported` occurs only in the source and the plan). Reachable only through `clang -cc1as` or `llvm-mc` with an MSIL triple; llc, llvm-as and opt never reach it (section 3). A clang `-cc1as` lit test pins it (written, passes). |
| 6 (ARMAsmParser.cpp, GDBRemoteCommunicationClient.cpp) | yes | Neither TU is in D:\llvm-msil\build\compile_commands.json (targets X86;WebAssembly;MSIL, projects clang;lld). Plan M1.b item (8) said "edit by inspection only"; M1 gate step 2 amendment excluded ARMAsmParser.cpp from msil-syntax. Both now compile in a private tree, and a mutation test (case removed) fails with `-Werror=switch` for both (section 4). |
| 7 | yes | lit-legs.ps1:58 (`$checkMsil`) and every M10 lit gate step pass only MSIL paths; unit-tests.ps1:28 filters `*MSIL*:TargetRegistry.*`, which runs 2 of 718 TargetParserTests and 6 of 45 MCTests. 36 upstream files are hooked; none of their upstream lit tests runs in any gate. 378 candidate upstream tests run here: 0 real failures, 12 environment failures (tools not built), 27 UNSUPPORTED (targets not built). A curated list of 298 tests passes in 56 s (section 2). |

## 1. Inventory: upstream files the fork changes

Command: `git diff --name-only 64f50ffd4f1c 0c5044bac349` = 919 paths. Fork-only prefixes dropped:
`llvm/lib/Target/MSIL/` (76), `llvm/test/CodeGen/MSIL/` (109), `llvm/docs/MSIL/` (148),
`llvm/unittests/Target/MSIL/`, `clang/test/CodeGen/MSIL/`, `msil/`, `.github/msil/`.
Fork-only files inside upstream directories (not hooks): `.github/workflows/msil-ci.yml`,
`clang/lib/Basic/Targets/MSIL.h`, `llvm/include/llvm/MC/MCSectionMSIL.h`,
`clang/test/Driver/msil-toolchain.c`, `clang/test/Preprocessor/init-msil.c`,
`llvm/test/tools/UpdateTestChecks/update_llc_test_checks/{msil-basic.test,Inputs/msil-basic.ll,Inputs/msil-basic.ll.expected}`.

That leaves **36 hooked upstream paths**. The upstream base can be derived mechanically:
`git log --first-parent --merges -1 --grep='^\[MSIL\] Merge upstream llvm-project main at ' --format=%P HEAD`
gives `2c51a5701ac6 64f50ffd4f1c`; the second parent is the base (0.3 s). `origin/main` is NOT
usable (it is an older upstream snapshot, 6a269f2b9ae8).

### 1.1 Per hooked file: hook, fork tests, upstream tests (candidates run here)

Line numbers are HEAD line numbers of the fork's hunks (`git diff -U0`).

| # | Hooked file | What the hook does | Fork tests that pin the MSIL side | Upstream tests run here (result) |
|---|---|---|---|---|
| 1 | clang/include/clang/Basic/DiagnosticCommonKinds.td:299-301 | new error `err_msil_asm_unsupported` | CodeGen/MSIL/inline-asm.c, ms-inline-asm.c | none runnable: Misc/warning-flags.c needs diagtool (not built) and only lists warnings |
| 2 | clang/include/clang/Basic/DiagnosticDriverKinds.td:162-164 | new error `err_drv_msil_no_object_output` | Driver/msil-toolchain.c | none runnable (as 1) |
| 3 | clang/lib/Basic/Targets.cpp:28,112-169,172-173 | `AllocateMSILTarget`; `AllocateTarget` returns it first for MSIL triples | CodeGen/MSIL/triples.c, datalayout.c, Preprocessor/init-msil.c, Driver/msil-toolchain.c | Preprocessor/init-x86.c (19 s), init-aarch64.c (12 s), predefined-win-macros.c (17 s) PASS; init.c (112 s) and predefined-arch-macros.c (174 s) PASS but too slow for a gate |
| 4 | clang/lib/CodeGen/BackendUtil.cpp:304, 1296-1299 | `asanUseGlobalsGC` case MSIL; `consumeError` before `err_fe_unable_to_interface_with_target` (an upstream-visible fix: the unconsumed Error aborted in assert builds) | CodeGen/MSIL/emit-obj-unsupported.c | CodeGen/asan-globals-gc.cpp PASS. No upstream test reaches err_fe_unable_to_interface_with_target |
| 5 | clang/lib/CodeGen/CGBuiltin.cpp:30,1686-1689,1952-1961 | `_bittest*` generic path and `__fastfail` diagnosed + llvm.trap on MSIL | CodeGen/MSIL/inline-asm.c | CodeGen/bittest-intrin.c, ms-intrinsics.c, Sema/bittest-intrinsics.c PASS |
| 6 | clang/lib/CodeGen/CGCall.cpp:712-717 | unprototyped calls never variadic on MSIL | CodeGen/MSIL/no-proto-call.c | CodeGen/X86/x86_64-arguments.c, 2007-02-25-C-DotDotDot.c, variadic-null-win64.c, no-prototype.c PASS |
| 7 | clang/lib/CodeGen/CGObjCMac.cpp:5963 | `GetSectionName` case MSIL (switch completeness) | none (ObjC on MSIL unexercised) | CodeGenObjC/section-name.m, sections.m PASS |
| 8 | clang/lib/CodeGen/CGStmt.cpp:2739-2763,2765-2767 | `emitMSILAsmStmt`: empty GNU asm -> fence+llvm.sideeffect, else error | CodeGen/MSIL/asm-barrier.c, inline-asm.c | CodeGen/asm.c, asm_64.c, asm-inout.c, asm-goto.c, asm-errors.c, X86/x86-64-inline-asm.c, X86/inline-asm-constraints.c PASS |
| 9 | clang/lib/CodeGen/CodeGenAction.cpp:84-100,254-255,567-572,612-613 | `removeMSILModuleAsm`; no exception-model flag for MSIL IR input | CodeGen/MSIL/triples.c, inline-asm.c | CodeGen/exception-model-flag.c, Driver/ir-exception-model.c, CodeGen/WebAssembly/wasm-exception-model-flag-{ir-input-mismatch,parse-ir-input}.ll PASS; thinlto-inline-asm.c ENV-FAIL (llvm-lto2); invalid_global_asm.c UNSUPPORTED (arm) |
| 10 | clang/lib/CodeGen/CodeGenModule.cpp:1500-1507,1732,3773-3775,3831-3841,7518,8214-8222 | no exception-model flag; ptrauth-sign-personality only for ELF; KCFI type constants skipped; "no-prototype" attr; CFString case; file-scope asm rejected | CodeGen/MSIL/triples.c, no-prototype.c, no-proto-address.c, inline-asm.c | exception-model-flag.c, ptrauth-module-flags.c, kcfi.c, kcfi-arity.c, kcfi-generalize.c, kcfi-normalize.c, cfstring.c, cfstring-windows.c, cfstring-elf-sections-x86_64.c, 2006-01-23-FileScopeAsm.c, no-prototype.c PASS |
| 11 | clang/lib/CodeGen/TargetBuiltins/X86.cpp:14,2924-2932,3045-3053 | `__cpuid`/`__cpuidex`/`__int2c` diagnosed + llvm.trap on MSIL | CodeGen/MSIL/inline-asm.c | CodeGen/ms-intrinsics-cpuid.c, ms-intrinsics.c, Headers/cpuid.c, Headers/__cpuidex_conflict.c PASS |
| 12 | clang/lib/Driver/ToolChain.cpp:736-745,758-767,798-804,1960-1963 | getAssemble/getLink reject MSIL; getLinkerWrapper null-safe; KCFI removed for MSIL | Driver/msil-toolchain.c | Driver/integrated-as.c, fsanitize-cfi.c (42 s) PASS; fsanitize.c PASS but 250 s |
| 13 | clang/lib/Driver/ToolChains/Clang.cpp:5541-5543,5715-5723,6418-6424,8974-8979,9340-9343 | -c / -fembed-bitcode / -fms-volatile / /GS / ClangAs for MSIL | Driver/msil-toolchain.c | Driver/embed-bitcode.c, fembed-bitcode.c, as-options.s, integrated-as.s PASS; cl-options.c and clang_f_opts.c ENV-FAIL (llvm-profdata, the only tool they need); embed-bitcode-wasm.c ENV-FAIL (llvm-readobj); embed-bitcode.s UNSUPPORTED (arm) |
| 14 | clang/lib/Parse/ParseStmtAsm.cpp:18,352-412,575-579 | MSIL builds an opaque MSAsmStmt and marks names referenced | CodeGen/MSIL/ms-inline-asm.c | Parser/ms-inline-asm.c, ms-inline-asm-nested-braces.c, Sema/ms-inline-asm.c, ms-inline-asm-invalid-arch.c, CodeGen/ms-inline-asm*.c/.cpp (11) PASS |
| 15 | clang/lib/Sema/SemaChecking.cpp:3221,3224 | CFStringMakeConstantString unsupported on MSIL | CodeGen/MSIL/triples.c | Sema/builtins.c, CodeGen/cfstring.c PASS |
| 16 | clang/lib/Sema/SemaDeclAttr.cpp:4884-4888 | 8192 alignment cap uses the native triple's format | CodeGen/MSIL/triples.c, datalayout.c | Sema/MicrosoftCompatibility.c, Sema/attr-aligned.c PASS |
| 17 | lldb/source/Plugins/Process/gdb-remote/GDBRemoteCommunicationClient.cpp:2479 | `case llvm::Triple::MSIL` in the "not supported" group | none possible (compile only) | none run (lldb tests need a full lldb build; check-lldb is excluded on Windows even upstream) -> compile coverage, section 4 |
| 18 | llvm/CMakeLists.txt:592 | MSIL in LLVM_ALL_EXPERIMENTAL_TARGETS | every configure | none (configure only) |
| 19 | llvm/include/llvm/Config/Targets.h.cmake:53-55 | `LLVM_HAS_MSIL_TARGET` (no consumer in the tree) | every configure | none (configure only) |
| 20 | llvm/include/llvm/MC/MCContext.h:59,98-99,148,325,660-664 | `IsMSIL`, `MSILAllocator`, `MSILUniquingMap`, `getMSILSection` | every llc MSIL test, MSILTests | MCTests (full) PASS |
| 21 | llvm/include/llvm/MC/MCObjectFileInfo.h:462 | `initMSILMCObjectFileInfo` decl | every llc MSIL test | MCTests (full) |
| 22 | llvm/include/llvm/TargetParser/Triple.h:429,542-551,900-903 | `Triple::MSIL`, `getDefaultObjectFormat`, `getNativeFormatTriple`, `isOSBinFormatMSIL` | TargetParserTests *MSIL* (2 tests) | TargetParserTests full: 718 tests, 714 PASS, 4 SKIPPED (HostTest.getMacOSHostVersion, AIXHostVersionDetect, AIXTargetVersionDetect, AIXHostCPUDetect: host-OS gated) |
| 23 | llvm/lib/Bitcode/Writer/BitcodeWriter.cpp:6084-6087,6115-6118 | reportFatalUsageError for MSIL embed sections | Driver/msil-toolchain.c (EMBED-CC1, line 252) | Transforms/EmbedBitcode/*.ll (4), Frontend/x86-embed-bitcode.ll PASS; Frontend/embed-bitcode-noopt.{c,ll} ENV-FAIL (llvm-objcopy); embed-bitcode.ll UNSUPPORTED (arm/aarch64) |
| 24 | llvm/lib/MC/MCContext.cpp:30,116-118,158,188,315-318,961-973 | ctor env, reset, createSymbolImpl, getMSILSection | every llc MSIL test | X86 coff-comdat*/elf-comdat*/section-*.ll, WebAssembly/custom-sections.ll PASS; MCTests full PASS |
| 25 | llvm/lib/MC/MCObjectFileInfo.cpp:24,1180-1190,1240-1242,1261 | `initMSILMCObjectFileInfo`; getDwarfComdatSection fatal group (unreachable for MSIL: MSILMCAsmInfo has SupportsDebugInformation=false and the driver rejects -fdebug-types-section) | every llc MSIL test | as 24; DebugInfo/X86/generate-odr-hash.ll, c-type-units.ll ENV-FAIL (llvm-dwarfdump) |
| 26 | llvm/lib/MC/MCParser/AsmParser.cpp:808-813 | `case IsMSIL: reportFatalUsageError(...)` | **none** (item 6) -> new test, section 3 | Misc/cc1as-asm.s, cc1as-output-asm-variant.c, CodeGen/X86/inline-asm*.ll (51) + 2008-09-17-inline-asm-1.ll, WebAssembly/inline-asm{,-m,-tied,-failure}.ll PASS (X86 has UseIntegratedAssembler, so llc parses inline asm through this constructor for ELF/COFF/MachO); cc1as-defsym.s ENV-FAIL (llvm-nm), cc1as-relax-all.s ENV-FAIL (llvm-objdump), WebAssembly/inline-asm-roundtrip.ll ENV-FAIL (llvm-mc) |
| 27 | llvm/lib/MC/TargetRegistry.cpp:93-96,128-137,146-147,152-165,177-182,197-209 | MSIL lookup by object format, -march=msil, createMCObjectStreamer case | CodeGen/MSIL/target-selection.ll, march-msil.ll, MCTests TargetRegistry.* | tools/llc/*.ll *.mir: 9 PASS (invalid-target.ll pins both rewritten error paths), 5 UNSUPPORTED (aarch64/powerpc); MCTests full 44 PASS + 1 expected skip (TargetRegistry.MSILLookupWithoutTarget) |
| 28 | llvm/lib/Target/ARM/AsmParser/ARMAsmParser.cpp:6548 | `case MCContext::IsMSIL` in the unreachable group | none possible (compile only) | none (ARM not built) -> compile coverage, section 4 |
| 29 | llvm/lib/Target/TargetMachine.cpp:260-264 | shouldAssumeDSOLocal for MSIL | MSIL llc tests (globals, pinvoke) | CodeGen/X86/*pic*.ll, dso_local_equivalent*.ll, extern_weak.ll, WebAssembly/function-addr-offset.ll: 42 PASS |
| 30 | llvm/lib/TargetParser/TargetDataLayout.cpp:470-475 | MSIL data layout = native triple's | CodeGen/MSIL/datalayout.c, TripleTest MSIL | clang CodeGen/target-data.c PASS (61 s); TargetParserTests full |
| 31 | llvm/lib/TargetParser/Triple.cpp:430-431,732,1051-1067,1351-1358,2502-2505,2587-2591 | msil format name/parse, getNativeFormatTriple, normalize() fix, EH None, long double = double | TargetParserTests *MSIL*, CodeGen/MSIL/triples.c | TargetParserTests full PASS |
| 32 | llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp:2093 | getGlobalMetadataSection fatal group (unreachable for MSIL: instrumentGlobals takes the generic metadata-array path; verified, `clang --target=x86_64-unknown-linux-gnu-msil -fsanitize=address -S` gives clean backend errors) | none | Instrumentation/AddressSanitizer/*.ll + X86/*.ll: 110 PASS, 5 UNSUPPORTED; X86/asm_mov_no_instrumentation.s ENV-FAIL (llvm-mc) |
| 33 | llvm/unittests/MC/TargetRegistry.cpp | fork added MSIL tests to an upstream unittest file | MCTests TargetRegistry.* | MCTests full |
| 34 | llvm/unittests/TargetParser/TripleTest.cpp | fork added MSIL tests | TargetParserTests *MSIL* | TargetParserTests full |
| 35 | llvm/utils/UpdateTestChecks/asm.py:279-299,580-590,595-599,684-688 | MSIL function regex, scrubber, run handler | update_llc_test_checks/msil-basic.test | run-utc-tests.py msil-basic x86-basic x86-mir-only (item 8; lit marks the directory UNSUPPORTED on Windows) |
| 36 | llvm/utils/update_llc_test_checks.py:18,124-125,149-151,173-174,200-229,262,280 | MSIL check-label format etc. | as 35 | as 35 |

Switch completeness across the monorepo (not just the built part):
`git grep -l -E "case (llvm::)?Triple::XCOFF *:|case (llvm::)?MCContext::IsXCOFF *:" -- '*.cpp' '*.h' '*.cc' '*.inc' '*.def'`
finds 13 files (26 s cold). 11 name MSIL (rows 4, 7, 10, 17, 23, 24, 25, 26, 27, 28, 32); the
other two end in `default:` (llvm/lib/MC/MCAsmBackend.cpp:60, llvm/lib/ExecutionEngine/JITLink/JITLink.cpp:575).
So the fork's set of switch hooks is complete today; only ARMAsmParser.cpp and
GDBRemoteCommunicationClient.cpp are outside the build. A new exhaustive switch added upstream
in an unbuilt project (lld, bolt, mlir, another target) would be missed silently: the scan is
cheap enough to run in the gate (section 5).

## 2. Upstream lit and unit test runs (item 7)

Method. A private lit root (`hooks-artifacts\gen_litroot.py`): copies of
`build\test\lit.site.cfg.py`, `build\test\Unit\lit.site.cfg.py` and
`build\tools\clang\test\lit.site.cfg.py` whose `llvm_obj_root`/`clang_obj_root` point into the
scratch directory (so every Output\ directory is private) while every tool path still points at
D:\llvm-msil\build\bin, plus a private `llvm-lit.py` whose config_map points at the copies.
Tools available: clang, llc, opt, FileCheck, not, count, split-file, llvm-config (and
llvm-tblgen, clang-tblgen). Not built: llvm-mc, llvm-as, llvm-dis, llvm-objdump, llvm-readobj,
llvm-nm, llvm-objcopy, llvm-dwarfdump, llvm-lto2, llvm-profdata, diagtool, yaml2obj.

### 2.1 Candidate run (21 groups, one lit invocation each, -j8)

`python run_groups.py 8` (log `run-groups-all.log`, per-group xunit in the scratch dir):

```
g00 clang/lib/Basic/Targets.cpp            rc=0 314.0s inputs=6   pass=6   fail=0 unsup=0
g01 clang/lib/CodeGen/BackendUtil.cpp      rc=0  62.1s inputs=1   pass=1
g02 clang/lib/CodeGen/CGBuiltin.cpp        rc=0  33.0s inputs=3   pass=3
g03 clang/lib/CodeGen/CGCall.cpp           rc=0  18.1s inputs=4   pass=4
g04 clang/lib/CodeGen/CGObjCMac.cpp        rc=0  14.0s inputs=2   pass=2
g05 clang/lib/CodeGen/CGStmt.cpp           rc=0  19.3s inputs=7   pass=7
g06 clang/lib/CodeGen/CodeGenAction.cpp    rc=1  16.5s inputs=6   pass=4 fail=1 unsup=1   FAIL thinlto-inline-asm.c
g07 clang/lib/CodeGen/CodeGenModule.cpp    rc=0  19.5s inputs=11  pass=11
g08 clang/lib/CodeGen/TargetBuiltins/X86   rc=0  19.1s inputs=4   pass=4
g09 clang/lib/Driver/ToolChain.cpp         rc=0 307.2s inputs=3   pass=3
g10 clang/lib/Driver/ToolChains/Clang.cpp  rc=1  65.8s inputs=8   pass=4 fail=3 unsup=1   FAIL cl-options.c clang_f_opts.c embed-bitcode-wasm.c
g11 clang/lib/Parse/ParseStmtAsm.cpp       rc=0  21.4s inputs=15  pass=15
g12 clang/lib/Sema/SemaChecking.cpp        rc=0  14.0s inputs=2   pass=2
g13 clang/lib/Sema/SemaDeclAttr.cpp        rc=0  17.7s inputs=2   pass=2
g14 cc1as (AsmParser.cpp)                  rc=1  16.3s inputs=4   pass=2 fail=2           FAIL cc1as-defsym.s cc1as-relax-all.s
g15 llvm/lib/MC/MCParser/AsmParser.cpp     rc=1  21.9s inputs=54  pass=53 fail=1          FAIL WebAssembly/inline-asm-roundtrip.ll
g16 llvm/lib/MC/TargetRegistry.cpp         rc=0  24.2s inputs=14  pass=9 unsup=5
g17 MCContext.cpp + MCObjectFileInfo.cpp   rc=1  25.0s inputs=9   pass=7 fail=2           FAIL DebugInfo/X86/c-type-units.ll generate-odr-hash.ll
g18 llvm/lib/Target/TargetMachine.cpp      rc=0  14.0s inputs=42  pass=42
g19 BitcodeWriter.cpp                      rc=1  11.5s inputs=8   pass=5 fail=2 unsup=1   FAIL Frontend/embed-bitcode-noopt.c .ll
g20 AddressSanitizer.cpp                   rc=1  18.0s inputs=dir pass=110 fail=1 unsup=19 FAIL AddressSanitizer/X86/asm_mov_no_instrumentation.s
total wall 1077.8 s (21 lit start-ups of 10-15 s each; fsanitize.c 250 s, predefined-arch-macros.c 174 s, init.c 112 s)
```

Every FAIL is an environment failure, exit 127 `command not found` (`failinfo.py`):

| Test | Missing tool |
|---|---|
| clang/test/CodeGen/thinlto-inline-asm.c | llvm-lto2 |
| clang/test/Driver/cl-options.c, clang_f_opts.c | llvm-profdata (their only non-clang tool) |
| clang/test/Driver/embed-bitcode-wasm.c | llvm-readobj |
| clang/test/Misc/cc1as-defsym.s | llvm-nm |
| clang/test/Misc/cc1as-relax-all.s | llvm-objdump |
| llvm/test/CodeGen/WebAssembly/inline-asm-roundtrip.ll | llvm-mc |
| llvm/test/DebugInfo/X86/c-type-units.ll, generate-odr-hash.ll | llvm-dwarfdump |
| clang/test/Frontend/embed-bitcode-noopt.c, .ll | llvm-objcopy |
| llvm/test/Instrumentation/AddressSanitizer/X86/asm_mov_no_instrumentation.s | llvm-mc |

**Real failures: 0.** (An earlier run showed Sema/bittest-intrinsics.c with exit 143: my own
`timeout 590` killed it; the rerun passed.) UNSUPPORTED (27) are all `Missing required feature(s):
<arm|aarch64|powerpc|riscv|systemz|mips|loongarch>-registered-target` or AMDGPU/RISCV
`Unsupported configuration`.

### 2.2 Curated list (option A: no new tools)

`python run_curated.py 8` -> one lit invocation over the 298 inputs of
`hooks-artifacts\curated-paths.txt` (the `lit` lines of the draft manifest, globs expanded):

```
inputs=298 rc=0 wall=56.4s pass=287 fail=0 unsupported=11
  UNSUPPORTED Clang.Frontend/embed-bitcode.ll (arm, aarch64)
  UNSUPPORTED LLVM.Instrumentation/AddressSanitizer/{aarch64be,module-flags-aarch64,ptrauth-module-flags-aarch64}.ll (aarch64)
  UNSUPPORTED LLVM.Instrumentation/AddressSanitizer/{experiment,mem-intrinsics}.ll (systemz, mips, loongarch)
  UNSUPPORTED LLVM.tools/llc/{aix-pic-setting (powerpc),mattr-mcpu-help,module-triple-default,module-triple,mtune (aarch64)}.ll
```

56.4 s wall at -j8 on an otherwise idle host, lit start-up included (expanded command line
~21 KB, under the 32 KB Windows limit; a directory plus `--filter` keeps it shorter if needed).
Dropped from the candidates: the 12 ENV-FAIL tests and the four slow ones (fsanitize.c,
predefined-arch-macros.c, init.c are covered by cheaper siblings; target-data.c is kept for
TargetDataLayout.cpp, 61 s, it runs in parallel with the rest).

Option B: add `llvm-profdata` to the gate's build targets (1 TU, llvm-profdata.cpp, linking
libraries the build already has: Core, Object, ProfileData, Debuginfod, HTTP; estimate 1-2 min
first build, ~30-60 MB on D:). That enables clang/test/Driver/cl-options.c (26 s; /GS default and
/volatile:ms) and clang_f_opts.c (39 s; -fms-volatile), the only upstream tests for the Clang.cpp
hunks at 6418-6424 and 8974-8979. Estimated wall: ~60-70 s. Not measured (the shared build must
not be rebuilt by recon).

Not recommended: llvm-mc, llvm-dwarfdump, llvm-objdump, llvm-readobj, llvm-nm, llvm-objcopy,
llvm-lto2 (8 tools, ~35 TUs + 8 links, est. 5-8 min first build and ~250 MB) for 10 tests whose
hooks already have passing siblings in the curated list.

### 2.3 Unit tests

```
build\unittests\TargetParser\TargetParserTests.exe --gtest_brief=1
  718 tests from 28 suites; 714 PASSED, 4 SKIPPED (HostTest.getMacOSHostVersion,
  HostTest.AIXHostVersionDetect, HostTest.AIXTargetVersionDetect, HostTest.AIXHostCPUDetect); 23 s cold, 4 s warm
build\unittests\MC\MCTests.exe --gtest_brief=1
  45 tests from 10 suites; 44 PASSED, 1 SKIPPED (TargetRegistry.MSILLookupWithoutTarget, expected when MSIL is built); 3 s
with negative filters (so msil-gtest.ps1 -NoSkip passes):
  TargetParserTests --gtest_filter=-HostTest.getMacOSHostVersion:HostTest.AIXHostVersionDetect:HostTest.AIXTargetVersionDetect:HostTest.AIXHostCPUDetect -> 714 PASSED
  MCTests --gtest_filter=-TargetRegistry.MSILLookupWithoutTarget -> 44 PASSED
the current gate filter '*MSIL*:TargetRegistry.*:-TargetRegistry.MSILLookupWithoutTarget' selects 2 and 6 of them
```

The Linux CI job may skip a different HostTest set; the Linux twin (if any) must list its own
skips or use `--gtest_filter=-HostTest.*:TargetRegistry.MSILLookupWithoutTarget`.

## 3. Item 6b: the AsmParser.cpp IsMSIL hook

**Where.** llvm/lib/MC/MCParser/AsmParser.cpp, `AsmParser::AsmParser`, switch over
`Ctx.getObjectFileType()` at :781; the fork's case at :808-813:
`case MCContext::IsMSIL: reportFatalUsageError("assembly parsing is not supported for MSIL");`.
Without it the switch leaves `PlatformParser` null and :816 `PlatformParser->Initialize(*this)`
dereferences it (and -Wswitch only warns; LLVM_ENABLE_WERROR is OFF). Upstream precedent: the
SPIR-V and DXContainer cases (:796-798, :805-806) use report_fatal_error and have no test
(no match for either message under llvm/test).

**Reachability (verified).**
- `clang -cc1as -triple <msil> -filetype asm|null` reaches it: `clang -cc1as: fatal error: error
  in backend: assembly parsing is not supported for MSIL`, exit 1, no stack dump, for
  x86_64-unknown-linux-gnu-msil, x86_64-pc-windows-msvc-msil, aarch64-unknown-linux-gnu-msil.
- `llvm-mc -triple <msil>` would reach it too (llvm-mc is not built here).
- llc never does: MSILMCAsmInfo.cpp:63 `UseIntegratedAssembler = false`, so AsmPrinter emits
  inline asm as raw text instead of parsing it, and MSILPrepareIR (MSILPrepareIR.cpp:581) /
  MSILAsmPrinter::doInitialization (MSILAsmPrinter.cpp:403-407) diagnose inline and module asm first.
- llvm-as / opt never do: the IR parser is not involved; BitcodeWriter::writeSymtab skips targets
  without an MC asm parser (BitcodeWriter.cpp:5770), and ModuleSymbolTable asserts
  `T->hasMCAsmParser()` first (ModuleSymbolTable.cpp:81; clang removes module asm for MSIL in
  CodeGenAction.cpp:84-100 for exactly that reason). So "llvm-as/opt/llc on IR" cannot pin it.
- The clang driver never does: ClangAs::ConstructJob reports err_drv_msil_no_object_output
  (Clang.cpp:9340-9343).

**Side finding (not a fork bug).** `clang -cc1as -triple <msil> -filetype obj` hits
`Assertion failed: MAB && "Unable to create asm backend!"` (clang/tools/driver/cc1as_main.cpp:600)
before the parser exists. Upstream-generic for every target without an MCAsmBackend; unreachable
from the driver. Do not pin it.

**Test (written and run).** Proposed new file `clang/test/CodeGen/MSIL/cc1as-unsupported.s`
(fork directory, already in M10 gate step 5 / the net8 leg through `clang/test/CodeGen/MSIL`;
clang's suffixes include .s). Text in `hooks-artifacts\cc1as-unsupported.s`:

```
# REQUIRES: msil-registered-target
# RUN: not %clang -cc1as -triple x86_64-unknown-linux-gnu-msil -filetype asm %s -o - 2>&1 \
# RUN:   | FileCheck %s --implicit-check-not="PLEASE submit" --implicit-check-not="Stack dump"
# RUN: not %clang -cc1as -triple x86_64-pc-windows-msvc-msil -filetype asm %s -o - 2>&1 | FileCheck ... (same)
# RUN: not %clang -cc1as -triple aarch64-unknown-linux-gnu-msil -filetype null %s 2>&1 | FileCheck ... (same)
# RUN: not %clang -cc1as -triple aarch64-w64-windows-gnu-msil -filetype null %s 2>&1 | FileCheck ... (same)
# CHECK: error in backend: assembly parsing is not supported for MSIL
# RUN: %if x86-registered-target %{ %clang -cc1as -triple x86_64-unknown-linux-gnu -filetype null %s %}
  nop
```

Run through a scratch copy of the clang suite (its lit.cfg.py and the CodeGen/MSIL lit.local.cfg,
which loads llvm/test/CodeGen/MSIL/lit.local.cfg): `PASS: Clang :: CodeGen/MSIL/cc1as-unsupported.s`
(12.4 s incl. lit start-up). Without the hook the constructor dereferences a null PlatformParser
(crash: "PLEASE submit"/"Stack dump" -> the implicit-check-nots and the CHECK fail). Optional
extra (needs llvm-mc in the build list): an llvm-mc RUN line under `%if`; not recommended.

## 4. Item 6c: compile coverage for ARMAsmParser.cpp and GDBRemoteCommunicationClient.cpp

### 4.1 Private tree (configured and built)

`hooks-artifacts\configure-hooks.ps1` (dot-sources msil-env.ps1), essentials:

```
cmake -G Ninja -S D:/llvm-msil/llvm -B C:\llvm-msil-hooks-build -DCMAKE_BUILD_TYPE=Release
  -DLLVM_ENABLE_ASSERTIONS=ON -DLLVM_ENABLE_PROJECTS=clang;lldb -DLLVM_TARGETS_TO_BUILD=ARM
  -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD= -DLLVM_EXTERNAL_PROJECTS= -DLLVM_INCLUDE_TESTS=OFF
  -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_DOCS=OFF
  -DCLANG_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_DOCS=OFF -DLLDB_INCLUDE_TESTS=OFF
  -DLLVM_APPEND_VC_REV=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF
  -DLLVM_ENABLE_DIA_SDK=OFF -DLLVM_ENABLE_TELEMETRY=OFF -DLLVM_ENABLE_PDB=OFF
  -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_ENABLE_OBJC_REWRITER=OFF
  -DLLDB_ENABLE_PYTHON=OFF -DLLDB_ENABLE_LUA=OFF -DLLDB_ENABLE_SWIG=OFF -DLLDB_ENABLE_LIBXML2=OFF
  -DLLDB_ENABLE_LIBEDIT=OFF -DLLDB_ENABLE_CURSES=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  -DPython3_EXECUTABLE=<msil python> clang-cl / lld-link / llvm-lib from VS (as msil-configure.ps1)
  -DLLVM_ENABLE_LLD=ON -DLLVM_PARALLEL_COMPILE_JOBS=8 -DLLVM_PARALLEL_LINK_JOBS=2
```

- lldb on Windows needs **no extra dependency**: SWIG is not installed on this host, and with the
  LLDB_ENABLE_* switches OFF configure reports "Enable SWIG/editline/curses/Lua/Python/Libxml 2
  support in LLDB: FALSE" and succeeds. lldb requires clang in the project list (llvm/CMakeLists.txt:215-217
  enables it automatically). LLDB_ENABLE_LZMA is deprecated in-tree (no effect).
- Configure: **242 s** (196 s configure + 45 s generate), exit 0, 36 MB.
- `ninja lib\Target\ARM\AsmParser\CMakeFiles\LLVMARMAsmParser.dir\ARMAsmParser.cpp.obj`:
  **347 steps / 278 C++ + 9 C compiles, 473 s** (2026-10-20 07:55:42-08:03:35; two 1-core /Zs
  runs overlapped), tree 133 MB. Pulls in: llvm-min-tblgen and llvm-tblgen (LLVMSupport,
  LLVMDemangle, LLVMTableGen, TableGen Basic/Common, LLVMCodeGenTypes), the common generated
  headers (VCSRevision.h, GenVT.inc, Attributes.inc, IntrinsicEnums/Impl.inc, RuntimeLibcalls.inc,
  TargetLibraryInfo.inc, AArch64/AMDGPU/PPC/R600/RISCV TargetParserDef.inc, MCCLOptions.inc) and
  the 16 ARMGen*.inc. No-op rerun 3.5-5.9 s; single-TU recompile 45.7 s.
- `ninja tools\lldb\source\Plugins\Process\gdb-remote\CMakeFiles\lldbPluginProcessGDBRemote.dir\GDBRemoteCommunicationClient.cpp.obj`
  (after the ARM build): **1187 steps / 981 C++ compiles, 1141 s** (08:09:29-08:28:30, lit runs
  overlapped), tree 464 MB. CMake's Ninja generator makes the object order-depend on
  `cmake_object_order_depends_target_*` of 38 LLVM libraries plus clang-tblgen and lldb-tblgen
  (AddLLDB.cmake:359 `add_dependencies(${name} clang-tablegen-targets)`), so ninja builds most of
  LLVMSupport/IR/Analysis/TransformUtils/ScalarOpts/MC/Object/DebugInfo*/clangBasic first, although
  the TU itself includes **no generated header** beyond configure-time ones (lldb/Host/Config.h,
  llvm/Config/*.h).
- msil-syntax.ps1 against the private compile database (`-BuildDir C:\llvm-msil-hooks-build`):
  GDBRemoteCommunicationClient.cpp /Zs passed **before any ninja run** (only configure); idle host:
  /Zs 18.8-22.6 s, real compile (`-Obj`) 19.3 s; ARMAsmParser.cpp /Zs 16.5-22.7 s, `-Obj` 40.7 s.
- **Mutation tests** (scratch copies with the MSIL case line removed, `-FlagsFrom` the original TU,
  `-ExtraInclude` its directory, `-ExtraFlags /clang:-Werror=switch`):
  `mut\GDBRemoteCommunicationClient.cpp(2464,17): error: enumeration value 'MSIL' not handled in switch [-Werror,-Wswitch]`
  `mut\ARMAsmParser.cpp(6531,11): error: enumeration value 'IsMSIL' not handled in switch [-Werror,-Wswitch]`
  Both unmutated files pass with -Werror=switch.

### 4.2 Recommended design (prototype run)

`hooks-artifacts\proto-hooks-compile.ps1`: (1) configure `$env:MSIL_HOOKS_BUILD_DIR` (default
`C:\llvm-msil-hooks-build`) only if it has no build.ninja (later CMake changes re-run cmake
through ninja's regeneration rule); (2) `ninja -C <dir> <ARM object>` (real compile with the tree's
flags, brings the ARM .inc up to date); (3) `msil-syntax.ps1 -BuildDir <dir> -ExtraFlags
/clang:-Werror=switch <ARMAsmParser.cpp> <GDBRemoteCommunicationClient.cpp>` on every run (does not
depend on build history, the failure mode of the old M1 gate step 2 log grep). The lldb object
is never built by ninja. If a future upstream change makes the lldb TU include a generated header,
/Zs fails with msil-syntax's ".inc ... file not found" hint; then build that object with ninja
(19 min first time) instead.

Prototype steady state: `[hooks-compile] PASS total 47s` (ninja no-op 1 s, /Zs both 46 s).

Costs:

| | Time | Disk |
|---|---|---|
| first run (configure + ARM object + /Zs) | ~12.7 min (242 + 473 + 47 s) | ~140 MB on C: |
| steady state (nothing changed) | 47 s | - |
| after an upstream merge | est. 5-13 min (cmake regeneration 1-4 min, rebuild of changed Support/TableGen TUs and ARM .inc up to the full 473 s, /Zs 47 s) | +0-50 MB |
| if the lldb object were built by ninja too | +19 min first run, most of it again after each merge | 464 MB |

Rejected alternatives: ARM in the main build's LLVM_TARGETS_TO_BUILD (rebuilds/relinks llc,
clang, opt with a full ARM backend, ~250 more TUs and larger binaries on the 27-31 GB D:, and it
changes which upstream/MSIL tests are UNSUPPORTED); lldb in the main build (thousands of TUs);
/Zs of ARMAsmParser.cpp against the main compile database with hand-generated ARM .inc (needs
-FlagsFrom another target's TU and hand-run llvm-tblgen; lldb would need a hand-written
lldb/Host/Config.h: brittle).

Host caveat: agent-side `Remove-Item` under `C:\llvm-msil-hooks-build` is refused by the agent
harness ("system path is protected"), so agents cannot clean that tree; a gate script must never
need to delete it (use `cmake --fresh` for a broken cache, or point MSIL_HOOKS_BUILD_DIR at a new
directory). The script itself never deletes anything.

Upstream precedent: upstream compiles these files because premerge CI builds every target
(.ci/monolithic-linux.sh:49-54 and monolithic-windows.sh:42-45 set no LLVM_TARGETS_TO_BUILD, so
"all") and builds lldb when llvm or clang change (.ci/compute_projects.py:33 lldb depends on
llvm+clang, :83 DEPENDENTS_TO_TEST, Windows excludes lldb dependents at :166). The same switch
groups were extended by each new object format: ARMAsmParser.cpp:6544-6547 (IsGOFF a8cfa4b9bda3
2021, IsSPIRV 6c69427e880b 2022, IsXCOFF 632ebc4ab437, IsDXContainer 9130e471fe82 2022) and
GDBRemoteCommunicationClient.cpp:2474-2478 (GOFF 33d0031edba2, SPIRV c129220eaa98, Wasm
4f19fce2d6cd, XCOFF a03ae73c2932, DXContainer dfde354958ca).

## 5. Proposed 'upstream-hooks' check (item 7, with item 6c folded in)

**Files (new):**
- `msil/utils/upstream-hooks.txt` - the manifest; draft with all 36 hooks in
  `hooks-artifacts\upstream-hooks.txt` (format in its header: `hook <path>` blocks with
  `lit`/`gtest`/`compile`/`utc`/`none <reason>` lines and `fork <test>` lines; `fork-only`,
  `switch-default`, `allow <test> <reason>` top-level lines).
- `msil/utils/ci/upstream-hooks.ps1` - legs (rows via ci-common.ps1 `Add-MsilCiResult`, step
  `upstream-hooks`):
  1. `manifest`: base = second parent of the newest first-parent '[MSIL] Merge upstream
     llvm-project main at' merge (or `$env:MSIL_UPSTREAM_BASE`); every path of `git diff
     --name-only <base> HEAD` outside the fork-only prefixes and not `fork-only` needs a block;
     no stale blocks; every lit glob matches; every `fork` path exists; switch-coverage scan
     (every file with `case [llvm::]Triple::XCOFF:` / `case [llvm::]MCContext::IsXCOFF:` names
     MSIL or is `switch-default`). Prototype `check_manifest.py`: on HEAD it reports exactly one
     problem, the not-yet-written cc1as-unsupported.s (`FAIL hook llvm/lib/MC/MCParser/AsmParser.cpp:
     fork test 'clang/test/CodeGen/MSIL/cc1as-unsupported.s' does not exist`); with a fake extra
     changed path `lld/ELF/Writer.cpp` and the TargetMachine.cpp block removed it reports
     `unlisted hook: lld/ELF/Writer.cpp` and `unlisted hook: llvm/lib/Target/TargetMachine.cpp`.
     10.7 s (git grep dominates).
  2. `lit`: one `msil-lit.ps1 -LitArgs "-sv --xunit-xml-output <build>/lit-results/upstream-hooks.xml"`
     over the expanded `lit` lines (shared build lock; upstream Output\ dirs in the shared build
     are not used by any other leg). Pass = lit exit 0, at least 280 PASS, and every FAIL on the
     allow-list; an allow entry whose test passes is reported stale (FAIL).
  3. `gtest`: msil-gtest.ps1 -NoSkip -MinTests 700 on TargetParserTests with
     `-HostTest.getMacOSHostVersion:HostTest.AIXHostVersionDetect:HostTest.AIXTargetVersionDetect:HostTest.AIXHostCPUDetect`
     and -MinTests 40 on MCTests with `-TargetRegistry.MSILLookupWithoutTarget`.
  4. `compile`: the hooks-compile steps of section 4.2 (`-NoCompile` skips it, for CI).
  The `utc` lines are documentation only; item 8's 'utc' row runs run-utc-tests.py.
- `msil/utils/run-m1-gate.ps1`: call `upstream-hooks.ps1` after lit-legs (expected rows
  upstream-hooks/manifest, /lit, /gtest, /compile through Test-ChildRows).
- `.github/workflows/msil-ci.yml` (Windows job): a step `pwsh -NoProfile -File
  msil/utils/ci/upstream-hooks.ps1 -NoCompile -NoManifest` after the lit legs. Both checkouts use
  `fetch-depth: 1` (msil-ci.yml:93 and :205), so the manifest leg cannot diff against the upstream
  base in CI (fetch-depth 0 on llvm-project costs several GB); the manifest leg matters at merge
  time, which happens on the development host, so CI skips it explicitly (`-NoManifest` must print
  a SKIP line, and without `-NoManifest` a shallow clone is a FAIL row naming the reason, never a
  silent pass). Linux job: not needed (same tests); keep the script Windows-only and say so in
  its header.

**Allow-list (narrow).** Empty at introduction: the curated list contains only tests that pass
here. Rule for adding one: only an environment failure (a tool or runtime the build cannot
provide), named per test file with the missing tool, never a directory or glob, never a real
FileCheck mismatch; a real failure is fixed in the fork or the test is dropped with a manifest
comment. Tests needing unbuilt tools are left out of the list instead (the 12 of section 2.1).

**Runtime on this host:** manifest ~11 s; lit ~56 s (option A) / est. ~65-70 s (option B);
gtest ~10 s warm (~25 s cold); compile 47 s steady state (12.7 min first run, 5-13 min after a
merge). Total steady state ~2-2.5 min.

**Gate commands (M11 plan):**
```
N.   pwsh -NoProfile -File D:/llvm-msil/msil/utils/ci/upstream-hooks.ps1          (all legs; first run ~15 min, then ~2.5 min)
N+1. pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv" clang/test/CodeGen/MSIL/cc1as-unsupported.s
```
(N+1 is also covered by M10 step 5, which runs clang/test/CodeGen/MSIL.) Build targets: the M10
list unchanged for option A; add `llvm-profdata` (ci/build.ps1:28 and the M11 build targets
line) for option B.

**Owned-file sketch for the drafter:** infra WP: `msil/utils/upstream-hooks.txt`,
`msil/utils/ci/upstream-hooks.ps1`, `msil/utils/run-m1-gate.ps1`, `.github/workflows/msil-ci.yml`
(shared with item 2's upload-artifact bump: one WP must own it), `msil/utils/ci/build.ps1`
(option B), `msil/utils/build/README.md` or msil/test/README.md (doc row). Test WP (independent
author): `clang/test/CodeGen/MSIL/cc1as-unsupported.s`. The test author must not read
AsmParser.cpp's hunk beyond the spec: "MSIL triples in cc1as -filetype asm/null report 'assembly
parsing is not supported for MSIL' with exit 1 and no crash".
