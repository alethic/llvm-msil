# M11 recon: item 3 (function splitting / basic block sections on MSIL)

Item 3 of the section 12 row 'Deferred after the upstream merge' (2026-10-19):
`-function-splitting=all` / `-fsplit-machine-functions` crash the MSIL AsmPrinter
(D8 resets basic-block sections only). Evidence below was re-established from scratch on
2026-10-20 with the gate-built tools in `D:\llvm-msil\build` (HEAD 0c5044bac349) and with a
privately relinked llc/clang carrying the proposed fix. Nothing in the repository or in
`D:\llvm-msil\build` was modified; no cmake/ninja ran in the build dir.

Saved artifacts (git-ignored, survive scratch cleanup):
`D:\llvm-msil\msil\orchestration\M11\recon\split-files\`
- `item3.diff` - the proposed fix (verified, clang-format clean)
- `proposed-tests\function-splitting.ll`, `proposed-tests\split-machine-functions.c`,
  `proposed-tests\msil-section-options.c` - validated test texts (see section 8)
- `repro\` - inputs used below (`mfs.ll`, `leaf.ll`, `noprof.ll`, `mfs.c`, `mfs2.c`, `mfs2.prof`,
  `sds.ll`, `bar-v1.txt`)
- `tools\mkrsp.py`, `tools\relink.ps1` - the one-TU private relink method (section 7)

Scratch (may be cleaned): `<scratch>\m11\split`

## 0. Verdict

- Reproduced on both MSIL triples, through llc (`-function-splitting=all`, also with `-O0`,
  `-mfs-split-ehcode`, combined with `--basic-block-sections=` and `--basic-block-address-map`)
  and through clang (`-cc1 -fsplit-machine-functions`, `-Xclang -fsplit-machine-functions`,
  `-mllvm -function-splitting=all`, also from C source with a sample profile). Always the same
  assertion: `Assertion failed: Section && "Cannot switch to a null section!", file
  llvm/lib/MC/MCStreamer.cpp, line 1587`, in pass 'MSIL Assembly Printer'.
- The clang driver is already clean: `-fsplit-machine-functions` on every MSIL triple is
  `error: unsupported option '-fsplit-machine-functions' for target '<triple>'` (upstream's
  ELF-only check; MSIL triples have object format MSIL). No driver change is needed.
- Root cause: `TargetPassConfig::addMachinePasses` adds `BasicBlockSections(SplitWithPGO=true)`
  whenever `TM.Options.FunctionSplitting == All`; D8 forces only `Options.BBSections` to None.
  The pass gives a profiled function (any function with `-mfs-split-ehcode`) BB-section type
  Preset; the AsmPrinter then asks `MSILTargetObjectFile` for a unique function section, gets
  the default nullptr, and switches to it.
- Recommended fix (verified): force `TM.Options.FunctionSplitting = FunctionSplittingMode::None`
  in the **MSILPassConfig constructor** (after `TargetPassConfig`'s constructor). The
  MSILTargetMachine constructor is NOT enough (verified: llc and `-mllvm` still crash),
  because `TargetPassConfig`'s constructor re-applies the `-function-splitting=` option to
  `TM.Options` after the TargetMachine exists. Silent, like D8's basic block sections reset.
- With the fix every reproduction exits 0 and prints IL byte-identical to the run without the
  option; the pipeline no longer contains `bbsections-prepare`; all 108 `llvm/test/CodeGen/MSIL`
  tests, all 15 `clang/test/CodeGen/MSIL` tests (plus the new one) and `clang/test/Driver/msil-toolchain.c` pass
  with the patched llc/clang.
- `-basic-block-sections` and `-fbasic-block-address-map` are already handled (D8 amended row
  2026-10-13; MSILPrepareIR warning); they crash only in combination with
  `-function-splitting=all`, which the same fix removes.
- The crash is not a merge regression: before the merge the separate MachineFunctionSplitter
  also set BB-section type Preset (`git show 2c51a5701ac6:llvm/lib/CodeGen/MachineFunctionSplitter.cpp`
  line 160 `MF.setBBSectionsType(BasicBlockSection::Preset)`); upstream 337d9d5c7f49 (came
  with the merge) moved that logic into BasicBlockSections. Code reading only (no pre-merge build).

## 1. Inputs

`repro\mfs.ll` (both callees defined in the module because Phase 1 has no linker; an InstrProf
ProfileSummary, an entry count and branch weights 7000:0 make the block that calls `@baz` cold;
modeled on upstream `llvm/test/CodeGen/Generic/machine-function-splitter.ll`):

```llvm
define internal i32 @bar() noinline { ret i32 1 }
define internal i32 @baz() noinline { ret i32 2 }
define i32 @foo1(i1 zeroext %0) !prof !14 {
  br i1 %0, label %2, label %4, !prof !17
2: %3 = call i32 @bar()
   br label %6
4: %5 = call i32 @baz()
   br label %6
6: %r = phi i32 [ %3, %2 ], [ %5, %4 ]
   ret i32 %r
}
!llvm.module.flags = !{!0}
!0 = !{i32 1, !"ProfileSummary", !1}  ; ... InstrProf summary as in the upstream test
!14 = !{!"function_entry_count", i64 7000}
!17 = !{!"branch_weights", i32 7000, i32 0}
```

`repro\leaf.ll`: `define i32 @f(i32 %a) { ret i32 %a }`. `repro\noprof.ll`: mfs.ll without any
profile metadata. A function without profile data is not split (BasicBlockSections.cpp:477
requires `hasProfileData()` unless `-mfs-split-ehcode`), which is why the merge-review symptom
needs a profiled input.

## 2. Reproduction (gate-built tools, before any fix)

llc, both triples (`L=D:/llvm-msil/build/bin/llc.exe`, run in the scratch dir):

| command | x86_64-unknown-linux-gnu-msil | x86_64-pc-windows-msvc-msil |
|---|---|---|
| `$L -mtriple=T < mfs.ll` | exit 0 | exit 0 |
| `$L -mtriple=T -function-splitting=all < mfs.ll` | **assert, exit 132** | **assert, exit 132** |
| `... -function-splitting=all -O0` | **assert** | **assert** |
| `... -function-splitting=bbsections` / `=none` | exit 0 | exit 0 |
| `... -function-splitting=all < leaf.ll` / `< noprof.ll` | exit 0 (no profile, not split) | exit 0 |
| `... -function-splitting=all -mfs-split-ehcode < leaf.ll` (and noprof.ll) | **assert** on `@f` / `@bar` | **assert** |
| `... --basic-block-sections=all -function-splitting=all < mfs.ll` | **assert** | **assert** |
| `... --basic-block-sections=bar-v1.txt -function-splitting=all` (profile names only `bar`) | **assert** | **assert** |
| `... --basic-block-address-map -function-splitting=all` | warning + **assert** | warning + **assert** |
| `$L -mtriple=T -split-machine-functions` | `Unknown command line argument` (option gone since upstream 3e6acc87c588; `-function-splitting=all` replaces it) | same |

Output of the crashing run (abridged):

```
$ llc -mtriple=x86_64-unknown-linux-gnu-msil -function-splitting=all < mfs.ll -o mfs.out.il
Assertion failed: Section && "Cannot switch to a null section!", file D:/llvm-msil/llvm/lib/MC/MCStreamer.cpp, line 1587
PLEASE submit a bug report to https://github.com/llvm/llvm-project/issues/ ...
Stack dump:
0.	Program arguments: D:\\llvm-msil\\build\\bin\\llc.exe -mtriple=x86_64-unknown-linux-gnu-msil -function-splitting=all -o mfs.out.il
1.	Running pass 'Function Pass Manager' on module '<stdin>'.
2.	Running pass 'MSIL Assembly Printer' on function '@foo1'
Exception Code: 0xC000001D
exit=132        (lit reports: command failed with exit status: 0xc000001d)
```

MIR right before the AsmPrinter (`-print-after=bbsections-prepare -filter-print-funcs=foo1`;
`-stop-after` is not usable because of item 4):

```
bb.0 (%ir-block.1):  ... BR_UNLESS %bb.2, %6:i32 ...
bb.1 (%ir-block.2):  ... CALL @bar ... RETURN ...
bb.2 (%ir-block.4, bbsections Cold):  ... CALL @baz ... RETURN ...
```

Pipeline difference (`-debug-pass=Structure`, leaf.ll): with `-function-splitting=all` the
pipeline gains `-bbsections-profile-reader` (immutable) and, after `-machine-sanmd`,
`-machine-cycles -machine-block-freq -bbsections-prepare` ("Basic Block Sections
Transformation"), i.e. after `msil-reg-stackify`, `msil-explicit-locals`, `msil-reg-numbering`
and before `msil-stack-check`. With `--basic-block-address-map` the pipeline gains
`-bbsections-profile-reader` and `-bbsections-prepare` too (inert, see section 4).

clang (`C=D:/llvm-msil/build/bin/clang.exe`):

| command (T = each MSIL triple tested) | result |
|---|---|
| `$C --target=T -O2 -S -fsplit-machine-functions mfs.c` for T = x86_64-unknown-linux-gnu-msil, x86_64-pc-windows-msvc-msil, x86_64-w64-windows-gnu-msil, aarch64-unknown-linux-gnu-msil | `clang: error: unsupported option '-fsplit-machine-functions' for target 'T'` (Windows MSVC shows `x86_64-pc-windows-msvc19.33.0-msil`), exit 1 |
| `... -fsplit-machine-functions -fno-split-machine-functions` | exit 0 |
| `$C --target=T -S -x ir mfs.ll -Xclang -fsplit-machine-functions` (linux, windows-msvc) | **assert** in 'MSIL Assembly Printer', exit 1 |
| `$C --target=T -S -x ir mfs.ll -mllvm -function-splitting=all` | **assert** |
| `$C --target=T -S -x ir mfs.ll -O0 -Xclang -fsplit-machine-functions` | **assert** |
| `$C --target=T -S -x ir mfs.ll -Xclang -fbasic-block-address-map -Xclang -fsplit-machine-functions` | warning + **assert** |
| `$C --target=T -O2 -S -gline-tables-only -fprofile-sample-use=mfs2.prof -Xclang -fsplit-machine-functions mfs2.c` (C source, sample PGO) | `warning: Using AutoFDO without FSDiscriminator for MFS may regress performance.` then **assert**; native x86_64-unknown-linux-gnu splits (`foo1.cold` in `.text.split.foo1`) |
| `$C --target=T -O2 -S mfs.c -Xclang -fsplit-machine-functions` (no profile) | exit 0 (nothing is split) |
| `clang -cc1 -triple T -S -x ir mfs.ll -fsplit-machine-functions` | **assert** (the clang test in section 8 fails at this line without the fix) |

Upstream non-ELF targets (for comparison, same build): `llc -function-splitting=all < mfs.ll`
crashes for `wasm32-unknown-unknown` ('WebAssembly Assembly Printer', 0xC0000005),
`x86_64-pc-windows-msvc` and `x86_64-apple-darwin` ('X86 Assembly Printer', 0xC0000005);
`x86_64-unknown-linux-gnu` splits (`.section .text.split.foo1`, `foo1.cold:`). Upstream llc is
not hardened; only the clang driver guards.

## 3. Root cause (file:line, HEAD 0c5044bac349)

1. Option entry points:
   - llc / clang `-mllvm`: `-function-splitting=` is a CodeGen option
     (`llvm/lib/CodeGen/CodeGenOptions.td:156-159`). `TargetPassConfig`'s constructor applies it
     to the TargetMachine **after** the TargetMachine was constructed:
     `llvm/lib/CodeGen/TargetPassConfig.cpp:472-473`
     `if (Opts.function_splitting) TM.Options.FunctionSplitting = *Opts.function_splitting;`
   - clang cc1 `-fsplit-machine-functions`: `clang/lib/CodeGen/BackendUtil.cpp:432-433` sets
     `Options.FunctionSplitting = llvm::FunctionSplittingMode::All` in the TargetOptions passed to
     the TargetMachine constructor (no triple check in cc1).
   - Every write of `BBSections`/`BBAddrMap` happens before the TargetMachine constructor
     (`llvm/lib/CodeGen/CommandFlags.cpp:532-533`, `clang/lib/CodeGen/BackendUtil.cpp:412-413`);
     `TargetPassConfig.cpp:473` is the only post-constructor writer of these fields. That is why
     D8's constructor reset works for BBSections and cannot work for FunctionSplitting.
2. D8 (`llvm/lib/Target/MSIL/MSILTargetMachine.cpp:111-116`) resets only
   `Options.BBSections = BasicBlockSection::None`. `FunctionSplitting` keeps `All`
   (default is `BBSectionsOnly`, `llvm/include/llvm/Target/TargetOptions.h:242-244`; enum at 53-66).
3. Pipeline: `llvm/lib/CodeGen/TargetPassConfig.cpp:1109-1110`
   `SplitFunctionsWithPGO = TM->Options.FunctionSplitting == FunctionSplittingMode::All`;
   1112-1128 add the MIR profile loader for `-fs-profile-file`; 1142-1157 add
   `createBasicBlockSectionsPass(SplitFunctionsWithPGO)` whenever BBSections != None, BBAddrMap
   or SplitFunctionsWithPGO. The flag is captured in the pass object at pipeline-build time
   (`BasicBlockSections.cpp:689`), so resetting `TM.Options` later (e.g. in MSILPrepareIR, as is
   done for BBAddrMap) cannot stop the split. `disablePass` cannot remove it either: the pass is
   added by instance, and `TargetPassConfig::addPass(Pass *)` (`TargetPassConfig.cpp:577-612`)
   never consults the disabled/substituted pass map (only `addPass(AnalysisID)`, 624-642, does).
4. Pass: `llvm/lib/CodeGen/BasicBlockSections.cpp:578-581` (`UsePGO` when BBSections is None)
   -> `shouldSplitWithPGO` (470-493: needs `hasProfileData()` or `-mfs-split-ehcode`, and
   `TII.isFunctionSafeToSplit`, default true at `llvm/lib/CodeGen/TargetInstrInfo.cpp:1874-1891`,
   MSIL does not override) -> `assignColdSectionWithPGO` (497-528) sets
   `MF.setBBSectionsType(BasicBlockSection::Preset)` (498) and cold blocks to `ColdSectionID`
   (528) -> `sortBasicBlocksAndUpdateBranches` -> `MF.assignBeginEndSections()` (387), which
   always marks the entry block `isBeginSection` (`llvm/lib/CodeGen/MachineFunction.cpp:461-472`).
   The pass also reorders blocks and inserts branches after MSILRegStackify/ExplicitLocals/
   RegNumbering.
5. Crash: `llvm/lib/CodeGen/AsmPrinter/AsmPrinter.cpp:1050-1054`
   `if (MF->front().isBeginSection()) MF->setSection(getObjFileLowering().getUniqueSectionForFunction(F, TM));`
   `MSILTargetObjectFile` (`llvm/lib/Target/MSIL/MSILTargetObjectFile.cpp`) does not override it,
   so the default `TargetLoweringObjectFile::getUniqueSectionForFunction` returns nullptr
   (`llvm/lib/Target/TargetLoweringObjectFile.cpp:498-501`); `switchSection(nullptr)` asserts at
   `llvm/lib/MC/MCStreamer.cpp:1587`. The cold block would hit the same assert at
   `AsmPrinter.cpp:4854-4857` (`getSectionForMachineBasicBlock`, default nullptr at
   `TargetLoweringObjectFile.cpp:492-496`). The `-mfs-split-ehcode` crash on a one-block leaf
   (no cold block at all) proves the first site fires first. Only ELF implements both hooks
   (`llvm/lib/CodeGen/TargetLoweringObjectFileImpl.cpp:982, 1137`).

## 4. -basic-block-sections, -fbasic-block-address-map and neighbours (same machinery)

| path | result today | covered by |
|---|---|---|
| llc `--basic-block-sections=all` / profile file (both triples) | exit 0, same IL (D8 amended, ctor reset) | `llvm/test/CodeGen/MSIL/basic-block-sections.ll` |
| llc `--basic-block-sections=labels` | llc treats `labels` as a file name: `Error loading basic block sections function list file: no such file or directory`, then exit 0 for MSIL (ctor resets List to None). Native x86_64-unknown-linux-gnu with the same command segfaults (exit 139): upstream llc bug, not MSIL | none needed |
| llc `--basic-block-address-map` | `warning: basic block address map is not supported for MSIL; ignored`, exit 0 (`llvm/lib/Target/MSIL/MSILPrepareIR.cpp:487-492`). `bbsections-prepare` stays in the pipeline but is inert: `handleBBSections` returns at `BasicBlockSections.cpp:581` (SplitWithPGO false) and `handleBBAddrMap` (661-666) reads the option after MSILPrepareIR cleared it | `stack-protector-warning.ll` |
| llc `--basic-block-address-map -function-splitting=all` | warning + assert | item 3 fix |
| llc `-split-static-data`, `-partition-static-data-sections` (repro\sds.ll: a constant table and a global read in the cold block) | exit 0, IL identical to the run without the option, both triples | none needed |
| llc `-enable-gc-empty-basic-blocks` | exit 0 | none needed |
| clang driver `-fbasic-block-sections=all` / `=list=<f>` | `error: unsupported option ... for target` (`clang/lib/Driver/ToolChains/Clang.cpp:6650-6679`: x86 ELF all/list/none, AArch64 ELF list/none, NVPTX silently dropped, else error unless `none`) | new driver test (section 8) |
| clang driver `-fbasic-block-sections=none` | accepted, not passed to cc1 | new driver test |
| clang driver `-fbasic-block-sections=labels` | `warning: argument '-fbasic-block-sections=labels' is deprecated, use '-fbasic-block-address-map' instead`, and `-fbasic-block-address-map` is passed **without a triple check** (Clang.cpp:6652-6656); backend: `warning: basic block address map is not supported for MSIL; ignored [-Wbackend-plugin]`, exit 0 | new driver test (driver half) |
| clang driver `-fbasic-block-address-map` (and `-fno-basic-block-address-map`) | `error: unsupported option ... for target` on all four triples (Clang.cpp:6638-6648: x86/AArch64 ELF or x86 COFF; MSIL is neither). The `-fno-` form is also an error (upstream quirk; not pinned) | new driver test |
| clang driver `-fsplit-machine-functions` | error on all four triples (`clang/lib/Driver/ToolChains/CommonArgs.cpp:2823-2838`, called from Clang.cpp:6691) | new driver test |
| clang driver `-fpartition-static-data-sections` | error (Clang.cpp:6693-6705, ELF only) | new driver test |
| cc1 / `-Xclang -fbasic-block-sections=all` | exit 0, silently ignored (D8) | `basic-block-sections.ll` (llc) |
| cc1 / `-Xclang -fbasic-block-address-map` | warning, exit 0 | - |
| cc1 / `-Xclang -fsplit-machine-functions`, `-mllvm -function-splitting=all` | **assert** | item 3 fix |

## 5. Upstream intent and precedent

- Upstream treats late function splitting, basic block sections and the address map as ELF
  features and guards them **only in the clang driver**: `CommonArgs.cpp:2830-2835` ("This
  codegen pass is only available on x86 and AArch64 ELF targets"), `Clang.cpp:6638-6679`,
  `Clang.cpp:6693-6705`; the option help says "(x86 and aarch64 ELF)"
  (`clang/include/clang/Options/Options.td:5151-5155`). `clang/test/Driver/fsplit-machine-functions.c`
  pins the `arm-unknown-linux` error. MSIL triples fall into the error branch because
  `Triple::isOSBinFormatELF()` is false (object format `Triple::MSIL`,
  `llvm/include/llvm/TargetParser/Triple.h:902`, `llvm/lib/TargetParser/Triple.cpp:732, 1356-1357`).
- Backends are not hardened: WebAssembly, NVPTX, SPIR-V, BPF, COFF and MachO targets neither
  reset `BBSections`/`FunctionSplitting` nor implement the section hooks (only
  `TargetLoweringObjectFileELF` does), and `llc -function-splitting=all` crashes on wasm32, COFF
  and MachO in this build (section 2). The closest "target opts out silently" precedent is the
  driver dropping `-fbasic-block-sections` for NVPTX device compilation (`Clang.cpp:6670-6672`).
- The per-function opt-out hook upstream designed is `TargetInstrInfo::isFunctionSafeToSplit` /
  `isMBBSafeToSplitToCold` (`llvm/include/llvm/CodeGen/TargetInstrInfo.h:2349`); AArch64 uses it
  to refuse functions with a red zone (`llvm/lib/Target/AArch64/AArch64InstrInfo.cpp:11682-11691`).
- Precedent for changing `TM.Options` while the pass configuration is constructed:
  `TargetPassConfig`'s own constructor does it for IPRA (`TargetPassConfig.cpp:448-453`), tied
  fast regalloc (458-460), GlobalISel abort (462-463) and FunctionSplitting (472-473); the MSIL
  fork already resets TargetOptions for CIL (D8: TrapUnreachable, EmitAddrsig, ExceptionModel,
  BBSections in `MSILTargetMachine.cpp:96-116`) and in MSILPrepareIR (call graph section, BB
  address map, `MSILPrepareIR.cpp:481-492`).

## 6. Fix options

| option | verdict |
|---|---|
| **A. MSILPassConfig constructor: `TM.Options.FunctionSplitting = FunctionSplittingMode::None;`** | **Recommended.** Verified (section 7). Runs after `TargetPassConfig`'s constructor applied the command-line value and before `addMachinePasses` reads it, so it covers llc, `clang -mllvm` and cc1 alike; the pipeline is then identical to the pipeline without the option (no `bbsections-prepare`, no MIR profile loader for MFS, no "Using AutoFDO without FSDiscriminator" warning). Silent, consistent with D8's BBSections reset (same reason: CIL methods have no sections; same constraint: no LLVMContext at that point). `None` ("hard off") rather than the default `BBSectionsOnly`: identical pipeline today because BBSections is always None for MSIL, and it states the intent. |
| B. MSILTargetMachine constructor only (next to BBSections) | **Insufficient, verified.** Private variant build: cc1 `-fsplit-machine-functions` fixed, but `llc -function-splitting=all` and `clang -mllvm -function-splitting=all` still assert (TargetPassConfig.cpp:472-473 runs later). Adding it there as well as A is harmless but redundant. |
| C. A plus a warning ("function splitting is not supported for MSIL; ignored") | Possible: MSILPassConfig would have to record in MSILTargetMachine (mutable flag) that it dropped `All`, and MSILPrepareIR would diagnose it next to the BB-address-map warning. Not recommended: extra cross-file state; the user-facing clang flag is already a driver error; D8 drops BBSections silently. |
| D. `MSILInstrInfo::isFunctionSafeToSplit()` returning false | Upstream's per-function hook; would stop `shouldSplitWithPGO` (BasicBlockSections.cpp:481-482) before `Preset` is set, so by code reading it avoids the crash (not built). Leaves `bbsections-prepare` + MachineBlockFrequencyInfo/ProfileSummaryInfo (and the MIR profile loader with `-fs-profile-file`) in the pipeline, and a second owned file (MSILInstrInfo.{h,cpp}). Inferior to A; could be belt-and-braces only. |
| E. Give MSILTargetObjectFile section hooks (`getUniqueSectionForFunction`, `getSectionForMachineBasicBlock` returning the text section) | Rejected: BasicBlockSections would still reorder blocks (cold blocks last) and insert branches after MSILRegStackify/ExplicitLocals/RegNumbering, and the AsmPrinter would run its section-range/end-label logic; CIL has no sections. |
| F. `disablePass(...)` in MSILPassConfig | Impossible: the pass is added by instance (`addPass(Pass *)` ignores disabled IDs, TargetPassConfig.cpp:577-612). |
| G. clang driver change | Not needed: the driver already errors for all MSIL triples (section 2). |

## 7. Recommended fix and its verification

Change (`split-files\item3.diff`; clang-format clean with the repository `.clang-format`), in
`llvm/lib/Target/MSIL/MSILTargetMachine.cpp`, class `MSILPassConfig`:

```diff
   MSILPassConfig(MSILTargetMachine &TM, PassManagerBase &PM)
-      : TargetPassConfig(TM, PM) {}
+      : TargetPassConfig(TM, PM) {
+    // CIL methods have no sections, so no function is ever split. Late
+    // function splitting is dropped silently, like basic block sections in
+    // the MSILTargetMachine constructor, but here: the base constructor has
+    // just applied llc's and -mllvm's -function-splitting=, and
+    // addMachinePasses reads the mode when it builds the pipeline (with All
+    // it adds BasicBlockSections, which gives the method a section of its
+    // own and moves cold blocks to a cold section).
+    TM.Options.FunctionSplitting = FunctionSplittingMode::None;
+  }
```

No header change; `FunctionSplittingMode` comes from `llvm/Target/TargetOptions.h`, already
included. No other file needs code changes.

Verification method (no ninja/cmake in `D:\llvm-msil\build`): compile only the patched
`MSILTargetMachine.cpp` with the compile flags from `build/compile_commands.json` (without PCH)
and relink `llc.exe` and `clang.exe` with `lld-link`, putting the new object ahead of the
unchanged `LLVMMSILCodeGen.lib` (link inputs and libraries parsed from `build/build.ninja`;
`split-files\tools\mkrsp.py`, `split-files\tools\relink.ps1`). Compile + both links took about
3 minutes. Variant B (constructor-only) was built the same way.

Results with the patched llc (both triples; IL compared with `cmp` against the gate-built llc
without the option, `-O0` runs against the `-O0` baseline):
`mfs.ll`, `leaf.ll`, `noprof.ll` x { `-function-splitting=all`, `... -O0`,
`... -mfs-split-ehcode`, `-function-splitting=bbsections`, `-function-splitting=none`,
`--basic-block-sections=all -function-splitting=all`,
`--basic-block-sections=bar-v1.txt -function-splitting=all`,
`--basic-block-address-map -function-splitting=all` } -> 48/48 exit 0, all byte-identical; the
only diagnostic is the existing BB-address-map warning. `-debug-pass=Structure` with
`-function-splitting=all --basic-block-sections=all` shows no `bbsections` pass.

Patched clang: `-cc1 -fsplit-machine-functions`, `-Xclang -fsplit-machine-functions`, `-mllvm
-function-splitting=all`, `-Xclang -fbasic-block-address-map -Xclang -fsplit-machine-functions`
on `mfs.ll`, and the C sample-PGO command of section 2 -> exit 0, IL identical to the same
command without the option, both x86_64 MSIL triples (the AutoFDO/FSDiscriminator warning is
gone too).

Regression runs in a private lit tree (copy of `build/bin` with the patched llc/clang; lit Output
in scratch):
- `llvm/test/CodeGen/MSIL`: 108/108 passed (`python llvm/utils/lit/lit.py -sv <tree>/test/CodeGen/MSIL`, 521 s with 8 workers).
- `clang/test/CodeGen/MSIL` (15 existing + new test) and `clang/test/Driver/msil-toolchain.c` +
  new driver test: 18/18 passed, `--param msil_require_verify=1`.
- Upstream neighbours: `llvm/test/CodeGen/X86/function-splitting-mode.ll` and
  `X86/basic-block-sections.ll` pass; `CodeGen/Generic/machine-function-splitter.ll` is
  UNSUPPORTED here (needs aarch64-registered-target); `clang/test/Driver/fbasic-block-sections.c`
  and `basic-block-address-map.c` pass; `clang/test/Driver/fsplit-machine-functions.c` FAILS in
  this build with or without the fix because it runs `llvm-profdata`, which is not a gate build
  target (environmental, unrelated).

## 8. Tests (validated texts in `split-files\proposed-tests\`)

All three were run in private lit suites against the gate-built tools (expected result "before")
and against the patched llc/clang ("after").

### 8.1 `llvm/test/CodeGen/MSIL/function-splitting.ll` (new; before: FAIL, after: PASS, ~34 s)

Spec for an independent author:
- split-file input `mfs.ll` as in section 1 (internal noinline `@bar`/`@baz` defined in the
  module, `@foo1` with `!prof` entry count 7000, branch weights 7000:0, InstrProf
  ProfileSummary module flag) plus a basic-block-sections v1 profile `other.txt` that names only
  `bar` (`v1` / `f bar` / `c 0`).
- For each of x86_64-unknown-linux-gnu-msil and x86_64-pc-windows-msvc-msil
  (`-verify-machineinstrs` everywhere): a baseline `llc -o %t/<t>.il` and an `-O0` baseline;
  then `-function-splitting=all`, `-function-splitting=all -O0`,
  `-function-splitting=all -mfs-split-ehcode`, `-function-splitting=all --basic-block-sections=all`,
  `-function-splitting=all --basic-block-sections=%t/other.txt` each with `2>&1 | count 0` and a
  `diff` against the baseline of the same -O level; `-function-splitting=all
  --basic-block-address-map` checked with FileCheck: exactly the line
  `warning: basic block address map is not supported for MSIL; ignored` (then `MAP-NOT: {{.}}`),
  plus `diff`.
- A FileCheck of the baseline IL: method `foo1` has `brfalse` to a label, `call int32 'bar'()`,
  `ret`, the label, `call int32 'baz'()`, `ret`, end of method (proves the cold block exists).
- Pipeline: `llc -function-splitting=all --basic-block-sections=all -debug-pass=Structure ...
  -o /dev/null 2>&1 | FileCheck --check-prefix=PIPE --implicit-check-not=bbsections
  --implicit-check-not="Basic Block Sections"` with `PIPE: Pass Arguments:` and
  `PIPE: MSIL Assembly Printer`, both triples.
- `%if ilasm %{ ... -function-splitting=all ... && %ilasm-verify ... %}` round trip.
- Native control so the input stays split-eligible after upstream changes:
  `%if x86-registered-target %{ llc -mtriple=x86_64-unknown-linux-gnu -function-splitting=all < %t/mfs.ll | FileCheck --check-prefix=NATIVE %}`
  with `NATIVE: .section .text.split.foo1` / `NATIVE-NEXT: foo1.cold:`.
- lit features: none beyond the directory's (optional `ilasm`, `x86-registered-target` inside `%if`).
Before the fix it fails at the first `-function-splitting=all` RUN (`0xc000001d`, count 0 got 25 lines).
It pins fix A specifically (option D would fail the PIPE check, option B fails the llc runs).

### 8.2 `clang/test/CodeGen/MSIL/split-machine-functions.c` (new; before: FAIL, after: PASS, ~7 s)

- `REQUIRES: msil-registered-target`; split-file with the same IR (no target triple).
- Both x86_64 MSIL triples: cc1 baseline `%clang_cc1 -triple T -S -Wno-override-module -x ir
  %t/mfs.ll -o %t/<t>.il` + FileCheck (calls to `bar` and `baz` in `foo1`); cc1
  `-fsplit-machine-functions` and cc1 `-mllvm -function-splitting=all` (`2>&1 | count 0`, `diff`
  against the cc1 baseline); driver baseline `%clang --target=T -S -Wno-override-module -x ir`
  and driver `-Xclang -fsplit-machine-functions` (`count 0`, `diff` against the driver baseline).
- `%if ilasm %{ %ilasm-verify ... %}` on one split output.
- Gotchas found while validating: clang warns `overriding the module target triple` even for IR
  without a triple (hence `-Wno-override-module`); no `-O2`, because the IR optimizer folds the
  calls to constants (the crash still happens at -O2, but the CHECKs lose their anchors); the
  driver runs need their own baseline because the driver turns the Windows triple into
  `x86_64-pc-windows-msvc19.33.0-msil` (host MSVC version), which the IL header comment prints.
Before the fix it fails at the first cc1 `-fsplit-machine-functions` RUN (`0xc000001d`).

### 8.3 `clang/test/Driver/msil-section-options.c` (new; before: PASS, after: PASS, ~5 s)

Regression guard for the driver half (upstream behaviour that MSIL relies on), no REQUIRES:
`not %clang -### --target=T -S <opt> %s 2>&1 | FileCheck -DT=<printed triple>` with
`error: unsupported option '<opt>' for target '[[T]]'` for
- `-fsplit-machine-functions`: x86_64-unknown-linux-gnu-msil, x86_64-w64-windows-gnu-msil,
  aarch64-unknown-linux-gnu-msil, x86_64-pc-windows-msvc-msil with
  `-fms-compatibility-version=19.40` (printed `x86_64-pc-windows-msvc19.40.0-msil`);
- `-fbasic-block-sections=all` and `=list=%s`: linux x86_64 and aarch64 MSIL;
- `-fbasic-block-address-map`: linux x86_64, MinGW, windows-msvc (19.40), aarch64 MSIL;
- `-fpartition-static-data-sections`: linux x86_64 MSIL;
accepted forms: `-fsplit-machine-functions -fno-split-machine-functions` (x86_64 and aarch64
linux; `--implicit-check-not=error: --implicit-check-not=split-machine`, `CHECK: "-cc1"`),
`-fbasic-block-sections=none` (`--implicit-check-not=basic-block`), and
`-fbasic-block-sections=labels` (deprecation warning, then `"-fbasic-block-address-map"` on the
cc1 line). Do not pin `-fno-basic-block-address-map` (upstream errors on it too; quirk).
Alternative: add these RUN lines to `clang/test/Driver/msil-toolchain.c` instead (an owned
Phase-1 file).

Optional (not needed): a MSILTargetMachineTest unit test that builds a pass config with
`Options.FunctionSplitting = All` and expects `None` afterwards; the lit tests already fail
without the fix.

## 9. Gate step

New gate step that fails without the fix (about 1 minute):

```
pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" llvm/test/CodeGen/MSIL/function-splitting.ll llvm/test/CodeGen/MSIL/basic-block-sections.ll llvm/test/CodeGen/MSIL/stack-protector-warning.ll clang/test/CodeGen/MSIL/split-machine-functions.c clang/test/Driver/msil-section-options.c
```

Without the fix: function-splitting.ll and split-machine-functions.c FAIL (llc / clang assert,
exit status 0xc000001d). If the milestone keeps the usual whole-directory lit step
(`llvm/test/CodeGen/MSIL clang/test/CodeGen/MSIL clang/test/Preprocessor/init-msil.c
clang/test/Driver/msil-toolchain.c msil/test`), the two directory tests are picked up
automatically and only `clang/test/Driver/msil-section-options.c` has to be added to its path
list. Build targets: `llc clang FileCheck not count split-file` (all already in the M10 list).

## 10. Plan and documentation updates for the drafter

- Decision D8 (plan section 3, row D8, line 147) / new section 12 row "D8 amended (function
  splitting)": MSILPassConfig's constructor forces `Options.FunctionSplitting` to
  `FunctionSplittingMode::None`, silently, after TargetPassConfig's constructor applied
  `-function-splitting=` (llc, `-mllvm`) and before addMachinePasses reads it; the constructor
  reset of the 2026-10-13 row cannot do it (verified). The clang driver keeps rejecting
  `-fsplit-machine-functions` for MSIL triples.
- `llvm/docs/MSIL/Status.md` section 3 table (unsupported features, around line 132): a row such
  as "Basic block sections, late function splitting, basic block address map, static data
  partitioning (`-fbasic-block-sections=`, `-fsplit-machine-functions`,
  `-fbasic-block-address-map`, `-fpartition-static-data-sections`) | driver error (MSIL is not
  ELF); passed to cc1 or llc directly they are ignored (the address map with a warning) | none,
  CIL methods have no sections".
- The comment in `llvm/test/CodeGen/MSIL/basic-block-sections.ll` (lines 1-13) can mention
  function-splitting.ll; not required.

## 11. Ownership, conflicts, cost

- Files to change: `llvm/lib/Target/MSIL/MSILTargetMachine.cpp` (implement);
  new tests `llvm/test/CodeGen/MSIL/function-splitting.ll`,
  `clang/test/CodeGen/MSIL/split-machine-functions.c`, `clang/test/Driver/msil-section-options.c`
  (test WP); docs `llvm/docs/MSIL/Status.md`, plan section 12 row (docs/drafter).
- Conflict warning: item 5 (LowerInvoke/UnreachableBlockElim twice; MSILPassConfig::addIRPasses
  or an addPassesToHandleExceptions override) and possibly item 4 (MIR verifier; e.g. a
  TargetMachine override) are likely to edit `MSILTargetMachine.cpp` too. Owned files must be
  disjoint inside a milestone, so items 3 and 5 (and 4 if it touches the TargetMachine) should
  share one implement WP. Item 5's fix will also change `llvm/test/CodeGen/MSIL/pipeline.ll`;
  function-splitting.ll's PIPE check only forbids `bbsections`/"Basic Block Sections" lines, so
  it is independent of item 5.
- Cost on this host: one TU recompile (about 30 s with the PCH) plus relinks of the binaries that
  link LLVMMSILCodeGen (llc, opt, clang, MSILTests, MCTests): a few minutes; no extra disk on D:
  beyond the in-place relinks. Tests: about 34 s + 7 s + 5 s with one worker each.

## 12. Side observations (not part of item 3)

- `llc --basic-block-sections=labels` crashes native x86_64-unknown-linux-gnu (exit 139) after
  "Error loading basic block sections function list file"; MSIL survives only because D8 resets
  the option. Upstream bug.
- `clang/test/Driver/fsplit-machine-functions.c` needs `llvm-profdata`, which no MSIL gate builds;
  relevant for item 7 (running upstream regression tests of hooked files) if that gate picks
  driver tests.
- `-stop-after=bbsections-prepare` was not used for MIR evidence because of item 4 (MIR output
  verification aborts on pre-ArgumentMove MSIL MIR); `-print-after` works.
