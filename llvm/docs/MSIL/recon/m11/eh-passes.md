# Recon: item 5, LowerInvoke and UnreachableBlockElim run twice in the MSIL pipeline

Item numbering: plan section 12, row 'Deferred after the upstream merge' (2026-10-19), item (5).
Tree: `msil` at 0c5044bac349 (contains the merge 2ea3fa74b232 of upstream 64f50ffd4f1c).
Tools: the gate build `D:\llvm-msil\build\bin\llc.exe` (built 2026-10-19 22:16), plus three private llc
builds in scratch (section 6). Nothing in the repository or in `D:\llvm-msil\build` was changed.
Scratch: `<scratch>\m11\eh-passes`
(below: `<S>`).

## 1. Verdict

- **Confirmed.** On every MSIL triple and at every level (-O0, -O1, -O2, -O3; x86_64 and aarch64, Linux
  and Windows), llc schedules `Lower invoke and unwind, for unwindless code generators` twice and
  `Remove unreachable blocks from the CFG` three times. One UnreachableBlockElim is the generic one that
  `TargetPassConfig::addIRPasses` adds for every target; the duplicated pair is
  LowerInvoke + UnreachableBlockElim, once from `MSILPassConfig::addIRPasses` and once from
  `TargetPassConfig::addPassesToHandleExceptions` (ExceptionHandling::None case).
- **Harmless, as the row says.** Over 1692 llc runs (16224 function executions of each pass instance)
  the second LowerInvoke and the third UnreachableBlockElim never changed a function (section 4).
- **Correction to the row's wording.** The duplication is not caused by upstream fa24f6dba7cd. It exists
  since M2 (737f368c919f, 2026-10-13): M2 copied WebAssembly's no-EH `LowerInvoke` +
  `UnreachableBlockElim` into `MSILPassConfig::addIRPasses`, and TargetPassConfig always added a second
  pair for ExceptionHandling::None. The M2 test `llvm/test/CodeGen/MSIL/pipeline.ll` pins both pairs on
  purpose (its header: "TargetPassConfig lowers invokes a second time"). WebAssembly at the fork base
  d1e15b695381 had the same duplicate in its no-EH configuration. What fa24f6dba7cd changed: it made
  `TargetPassConfig::addPassesToHandleExceptions` virtual (`llvm/include/llvm/CodeGen/TargetPassConfig.h:273`),
  and WebAssembly, MSIL's template target, dropped its own copy. MSIL is now the only in-tree target
  that schedules LowerInvoke twice, and the override that removes the late pair is now possible.
- **Recommended fix (option A):** override `addPassesToHandleExceptions()` in `MSILPassConfig`
  (`llvm/lib/Target/MSIL/MSILTargetMachine.cpp`) with a body that adds no pass, and keep the early pair
  in `addIRPasses`. This is WebAssembly's post-fa24f6dba7cd shape. Verified in a private build:
  byte-identical IL, exit codes and stderr on the whole corpus, and llvm/test/CodeGen/MSIL is
  107/108 (only the current pipeline.ll fails, as intended). The amended pipeline.ll (section 8) passes.
- **Test:** amend `llvm/test/CodeGen/MSIL/pipeline.ll` so that every 'Lower invoke and unwind' and every
  'Remove unreachable blocks from the CFG' line must be matched (`--implicit-check-not`): exactly one
  LowerInvoke and exactly two UnreachableBlockElim, at fixed positions. Against the gate llc it fails
  with "IMPLICIT-CHECK-NOT: excluded string found in input". It also fails with option B.

## 2. Reproduction

Input: `<S>\empty.ll` = the pipeline.ll module (`declare i32 @f(i32)`).

```sh
LLC=/d/llvm-msil/build/bin/llc.exe
for T in x86_64-unknown-linux-gnu-msil x86_64-pc-windows-msvc-msil aarch64-unknown-linux-gnu-msil aarch64-pc-windows-msvc-msil; do
 for O in 0 1 2 3; do
  out=$($LLC -mtriple=$T -O$O -debug-pass=Structure < empty.ll -o /dev/null 2>&1)
  echo "$T -O$O LowerInvoke=$(echo "$out" | grep -c 'Lower invoke and unwind') UBE=$(echo "$out" | grep -c 'Remove unreachable blocks from the CFG')"
 done; done
```

Output (all 16 lines identical in substance):

```
x86_64-unknown-linux-gnu-msil -O0 LowerInvoke=2 UnreachableBlockElim=3
x86_64-unknown-linux-gnu-msil -O2 LowerInvoke=2 UnreachableBlockElim=3
x86_64-pc-windows-msvc-msil -O0 LowerInvoke=2 UnreachableBlockElim=3
x86_64-pc-windows-msvc-msil -O2 LowerInvoke=2 UnreachableBlockElim=3
aarch64-unknown-linux-gnu-msil -O{0,1,2,3} LowerInvoke=2 UnreachableBlockElim=3
aarch64-pc-windows-msvc-msil -O{0,1,2,3} LowerInvoke=2 UnreachableBlockElim=3
```

Comparison targets with the same build: `wasm32-unknown-unknown`, `x86_64-unknown-linux-gnu`,
`x86_64-pc-windows-msvc` at -O0 and -O2: LowerInvoke=0, UnreachableBlockElim=1 (only the generic one).

`-debug-pass=Arguments` view (first line of `-debug-pass=Structure`, -O0 Linux):
`... -msil-fix-function-bitcasts -lowerinvoke -unreachableblockelim -indirectbr-expand -verify -gc-lowering
-shadow-stack-gc-lowering -unreachableblockelim -post-inline-ee-instrument -scalarize-masked-mem-intrin
-expand-reductions -lowerinvoke -unreachableblockelim -atomic-expand ...`

Structure excerpts (line numbers of the `-debug-pass=Structure` output; files `<S>\struct-*.txt`):

```
x86_64-unknown-linux-gnu-msil -O0              x86_64-pc-windows-msvc-msil -O2
18:    MSIL Fix Function Bitcasts               25:    MSIL Fix Function Bitcasts
20:      Lower invoke and unwind, ...           28:      MSIL Optimize Returned
21:      Remove unreachable blocks from the CFG 29:      Lower invoke and unwind, ...
22:      Expand indirectbr instructions         30:      Remove unreachable blocks from the CFG
25:      Shadow Stack GC Lowering               35:      Expand indirectbr instructions
26:      Remove unreachable blocks from the CFG 46:      Shadow Stack GC Lowering
29:      Expand reduction intrinsics            47:      Remove unreachable blocks from the CFG
30:      Lower invoke and unwind, ...           69:      CodeGen Prepare
31:      Remove unreachable blocks from the CFG 70:      Lower invoke and unwind, ...
32:      Expand Atomic instructions             71:      Remove unreachable blocks from the CFG
                                                72:      Expand Atomic instructions
```

Instance map used below (0-based, the numbering of `-stop-before=<pass>,<n>`):
LowerInvoke #0 = MSIL addIRPasses, #1 = addPassesToHandleExceptions; UnreachableBlockElim #0 = MSIL
addIRPasses, #1 = generic (after Shadow Stack GC Lowering), #2 = addPassesToHandleExceptions.

The x86_64 Linux and Windows pipelines differ only by 'Mark functions for Windows hot patch support'
(a module pass that splits the function pass manager on the Windows triple); the EH passes are the same.

## 3. Root cause (file:line)

- `llvm/lib/Target/MSIL/MSILTargetMachine.cpp:234-237` (`MSILPassConfig::addIRPasses`, lines 218-244):
  `addPass(createLowerInvokePass()); addPass(createUnreachableBlockEliminationPass());` with the comment
  "There is no exception model: lower invokes into calls and delete the unreachable landing pads."
  The class (lines 156-180) overrides addIRPasses and addISelPrepare but not addPassesToHandleExceptions.
- `llvm/lib/Target/MSIL/MSILTargetMachine.cpp:105-109`: the constructor forces
  `Options.ExceptionModel = ExceptionHandling::None` before `initAsmInfo()` (decision D8), so
  `MCAsmInfo::getExceptionHandlingType()` is None (pinned by
  `llvm/unittests/Target/MSIL/MSILTargetMachineTest.cpp:92-96`, test
  `MSILExceptionModelIsNoneForEveryIncomingModel` at line 104).
- `llvm/lib/CodeGen/TargetPassConfig.cpp:933-954` (`addISelPasses`): `addIRPasses()` (944),
  `addCodeGenPrepare()` (949), `addPassesToHandleExceptions()` (950), `addISelPrepare()` (951).
- `llvm/lib/CodeGen/TargetPassConfig.cpp:767-811` (`addPassesToHandleExceptions`), case
  `Default/None/Emscripten` at 800-809: `addPass(createLowerInvokePass());` (805) and
  `addPass(createUnreachableBlockEliminationPass());` (808). Identical at d1e15b695381 (lines 783-792)
  and at 2c51a5701ac6.
- `llvm/lib/CodeGen/TargetPassConfig.cpp:717-727`: the generic `ShadowStackGCLowering` (718) and
  "Make sure that no unreachable blocks are instruction selected" UnreachableBlockElim (726-727). Every
  target has this one; it is not part of the item.
- `llvm/include/llvm/CodeGen/TargetPassConfig.h:273`: `virtual void addPassesToHandleExceptions();`
  (made virtual by fa24f6dba7cd; `llvm/include/llvm/Passes/CodeGenPassBuilder.h:304` likewise for the
  new PM, which MSIL does not use: `llc -enable-new-pm -mtriple=x86_64-unknown-linux-gnu-msil` prints
  "buildCodeGenPipeline is not overridden", decision D23).
- Provenance: `llvm/docs/MSIL/recon/passes-frame.md:59` and `:79` (M0 recon: "LowerInvoke +
  UnreachableBlockElim | No EH model | Keep | No EH in M1") and `:176-177` copied WebAssembly's no-EH
  branch (`git show d1e15b695381:llvm/lib/Target/WebAssembly/WebAssemblyTargetMachine.cpp`,
  addIRPasses: "Lowering invokes when there is no EH support is done in
  TargetPassConfig::addPassesToHandleExceptions, but that runs after these IR passes and Emscripten
  SjLj handling expects all invokes to be lowered before"). WebAssembly needed the early copy for
  SjLj; MSIL copied it without the reason and kept the base's late copy.
- Pinned since M2: `git show 737f368c919f:llvm/test/CodeGen/MSIL/pipeline.ll` lines 10 and 72/76;
  current `llvm/test/CodeGen/MSIL/pipeline.ll:9-14` (header) and `:72-77` (two
  LowerInvoke/UnreachableBlockElim CHECK pairs). Plan 5.3 listing:
  `llvm/docs/MSIL/ImplementationPlan.md:404-406` (addIRPasses: "... LowerInvoke,
  UnreachableBlockElim, IndirectBrExpand, base"; no entry for addPassesToHandleExceptions).
- What fa24f6dba7cd did (`git show fa24f6dba7cd`, "WebAssembly: Take the exception model from the
  module flag (#227583)"): `TargetPassConfig.h` and `CodeGenPassBuilder.h` make the hook virtual;
  WebAssembly removes its LowerInvoke + UnreachableBlockElim from addIRPasses, lowers invokes inside
  WebAssemblyLowerEmscriptenEHSjLj when neither Wasm nor Emscripten EH is used
  (`llvm/lib/Target/WebAssembly/WebAssemblyLowerEmscriptenEHSjLj.cpp:929-946`, scheduled
  unconditionally at `WebAssemblyTargetMachine.cpp:314`), and overrides addPassesToHandleExceptions
  (`WebAssemblyTargetMachine.cpp:247` declaration, `:326-331` body: WinEHPrepare + WasmEHPrepare only,
  no LowerInvoke). The merge brought this in; it did not touch MSIL files.

## 4. Evidence that the late pair is a no-op

Corpus (`<S>\tools\corpus.py`, list in `<S>\corpus\inputs.txt`, 768 entries): every .ll of
llvm/test/CodeGen/MSIL and msil/test (split-file tests split into their parts); IR emitted by the gate
clang (`clang --target=<T> -ffreestanding -I msil/test/Inputs -O0|-O2 -S -emit-llvm`) for every .c of
msil/test and clang/test/CodeGen/MSIL on both x86_64 MSIL triples (452 of 460 compiled; the 8 others
are diagnostic tests); and invoke-heavy extras: `<S>\extra\cleanup.c` (C with `-fexceptions` and
`__attribute__((cleanup))`, 3-5 invokes per triple), `<S>\extra\eh.cpp` (C++ try/catch and a
destructor; Itanium landingpads on Linux, cleanuppad/catchswitch on Windows), `<S>\invoke.ll`.
Each input ran at -O0 and -O2 on x86_64-pc-windows-msvc-msil and x86_64-unknown-linux-gnu-msil
(inputs that carry a triple/level ran once with it).

Command (`<S>\tools\ehcount.py`): `llc -mtriple=<T> -O<n> -debug-pass=Details <in.ll> -o NUL`; each
"Made Modification '<pass>' on Function 'f'" line is attributed to the instance of the pass that the
preceding "Executing Pass '<pass>' on Function 'f'" lines identify. Result (`<S>\ehcount-current.txt`):

```
llc runs: 1692  exit!=0: 466        (466 = diagnostic tests that exit 1 by design)
LowerInvoke           instance 0: executed on 16224 functions, modified 32
LowerInvoke           instance 1: executed on 16224 functions, modified  0
UnreachableBlockElim  instance 0: executed on 16224 functions, modified 44
UnreachableBlockElim  instance 1: executed on 16224 functions, modified  0
UnreachableBlockElim  instance 2: executed on 16224 functions, modified  0
```

Instance 0 of LowerInvoke changed functions in: extra/cleanup.*, extra/eh.*, extra/invoke.ll,
call-errors.ll:invoke.ll, frame-escape-errors.ll, function-bitcasts.ll:main.ll (all 4 configurations
each). Instance 0 of UnreachableBlockElim: the same plus llvm/test/CodeGen/MSIL/unreachable.ll (the
blocks MSILPrepareIR's endBlocksAtTraps cuts off, `MSILPrepareIR.cpp:830-856`). Nothing can create an
invoke between the two LowerInvoke instances (IndirectBrExpand, LSR, MergeICmps, ExpandMemCmp, GC
lowering, ConstantHoisting, PartiallyInlineLibCalls, ScalarizeMaskedMemIntrin, ExpandReductions,
CodeGenPrepare create calls and blocks, never invokes).

## 5. Fix options

| | Change | Pipeline after | Output | For / against |
|---|---|---|---|---|
| **A (recommended)** | add `void addPassesToHandleExceptions() override;` to MSILPassConfig with a body that adds no pass; keep `MSILTargetMachine.cpp:234-237` | LowerInvoke 1x and UBE 1x right after FixFunctionBitcasts/OptimizeReturned (before IndirectBrExpand); generic UBE after Shadow Stack GC Lowering; nothing between CodeGenPrepare and AtomicExpand | identical (section 6) | Same shape as WebAssembly after fa24f6dba7cd (early lowering in addIRPasses, override that adds no LowerInvoke). Keeps plan 5.3's addIRPasses listing and the Phase-1 invariant that no target or generic IR pass (LSR, CodeGenPrepare, ...) sees an invoke or a dead landing pad. Keeps the hook where a later phase adds EH preparation, and the early slot that Design.md 4.6's WebAssembly-derived setjmp/longjmp lowering needs (WebAssembly's SjLj lowering requires invokes lowered first). Needs the virtual hook, so it only became possible with the merge. |
| B | delete `MSILTargetMachine.cpp:234-237`, keep the base's late pair | LowerInvoke + UBE after CodeGenPrepare only (what AMDGPU, SPIR-V and NVPTX get) | identical on this corpus (section 6) | Smaller diff and the generic unwindless convention. But invokes and dead landing pads then reach IndirectBrExpand, LSR, MergeICmps, ExpandMemCmp and CodeGenPrepare, which MSIL has never been tested with; blocks cut off by MSILPrepareIR survive until the generic UBE; changes plan 5.3 and pipeline.ll positions; moves away from the WebAssembly template. |
| C | leave as is | unchanged | unchanged | Costs two redundant function passes per function (each is a linear walk); keeps the deferred row open. |

Upstream precedent: WebAssembly `llvm/lib/Target/WebAssembly/WebAssemblyTargetMachine.cpp:247, 314,
326-331` (and `WebAssemblyCodeGenPassBuilder.cpp:81, 132-138` for the new PM): invokes lowered early in
addIRPasses, addPassesToHandleExceptions overridden without LowerInvoke. AMDGPU and SPIR-V add no
LowerInvoke of their own and get exactly one pair from TargetPassConfig.cpp:800-809, after
CodeGenPrepare (`llvm/test/CodeGen/AMDGPU/llc-pipeline.ll:56-57`, `:255-256`;
`llvm/test/CodeGen/SPIRV/llc-pipeline.ll:43-44`, `:154-156`). NVPTX has no own LowerInvoke either
(`grep createLowerInvokePass llvm/lib` finds only TargetPassConfig.cpp:805 and MSIL). BPF uses
DwarfCFI (`llvm/lib/Target/BPF/MCTargetDesc/BPFMCAsmInfo.h:37`). Only WebAssembly overrides the hook.

## 6. Verification in private builds (nothing written to D:\llvm-msil\build)

Procedure (`<S>\tools\build.ps1 -V <variant>`, ~1-2.5 min each on this host): copy
`llvm/lib/Target/MSIL/MSILTargetMachine.cpp` to `<S>\build\<V>\`, edit it, compile it with its exact
compile_commands.json flags (clang-cl, the LLVMCodeGen PCH read from the build tree, `/Fo` into
scratch), then link `<S>\build\<V>\bin\llc.exe` with lld-link from `tools/llc/CMakeFiles/llc.dir/llc.cpp.obj`
and the build tree's LINK_LIBRARIES of `build bin/llc.exe` in build.ninja, with the new object first
(the linker then never pulls the library's MSILTargetMachine.cpp.obj). Variants: C0 = unmodified
source (control), A = option A, B = option B, A2 = option A with the assert of section 7 (syntax-checked
with `msil-syntax.ps1 -FlagsFrom llvm/lib/Target/MSIL/MSILTargetMachine.cpp`: OK, 19 s).

1. Pass counts (`-debug-pass=Structure`, both x86_64 triples, -O0 and -O2): C0 LowerInvoke=2 UBE=3;
   A LowerInvoke=1 UBE=2; B LowerInvoke=1 UBE=2. Positions at -O2 Linux:
   A: 29 Lower invoke, 30 Remove unreachable, 35 Expand indirectbr, 46 Shadow Stack GC, 47 Remove
   unreachable, 66 CodeGen Prepare, 67 Expand Atomic. B: 32 Expand indirectbr, 43 Shadow Stack GC,
   44 Remove unreachable, 63 CodeGen Prepare, 64 Lower invoke, 65 Remove unreachable, 66 Expand Atomic.
2. Control equals the gate llc: 39 corpus inputs (every 20th entry), IL on stdout byte-identical.
3. IL identity (`<S>\tools\ildiff.py`, result `<S>\ildiff.txt`): all 1692 corpus runs with C0, A and B,
   comparing exit code, stderr and the IL bytes:
   `variant A: identical IL+exit+stderr in 1692 runs, different in 0`;
   `variant B: identical IL+exit+stderr in 1692 runs, different in 0`.
   C code is covered through clang-emitted IR fed to llc (same TargetPassConfig as clang -S; clang was
   not relinked).
4. Full llvm/test/CodeGen/MSIL suite with the real llvm lit configuration, run from a scratch copy of
   the site config (`<S>\fake\test\lit.site.cfg.py`, llvm_src_root = D:/llvm-msil/llvm, tools dir and
   Output under `<S>\fake`; wrapper `<S>\fake\llvm-lit-fake.py`), `-s --param msil_require_verify=1`:
   C0: 108/108 passed (108 s, 8 workers). A: 107 passed, 1 failed: `LLVM :: CodeGen/MSIL/pipeline.ll`
   (current version, line 76: second LowerInvoke CHECK not found), as intended.
5. Amended pipeline.ll (`<S>\lit\pipeline.ll`, minimal lit config `<S>\lit\lit.cfg.py` putting the
   chosen llc first on PATH): gate llc FAIL, C0 FAIL, A PASS, B FAIL. Current pipeline.ll: C0 PASS,
   A FAIL, B FAIL. Failure text with the gate llc at -O0:
   ```
   command line:1:22: error: IMPLICIT-CHECK-NOT: excluded string found in input
   -implicit-check-not='Lower invoke and unwind'
   <stdin>:30:2: note: found here
    Lower invoke and unwind, for unwindless code generators
   command line:1:22: error: IMPLICIT-CHECK-NOT: excluded string found in input
   -implicit-check-not='Remove unreachable blocks from the CFG'
   <stdin>:31:2: note: found here
   ```
6. clang-format (`VC\Tools\Llvm\x64\bin\clang-format.exe --style=file:llvm/.clang-format`) leaves the
   A and A2 sources unchanged.

## 7. Recommended fix (exact)

File: `llvm/lib/Target/MSIL/MSILTargetMachine.cpp` only.

1. In `class MSILPassConfig` (after `void addIRPasses() override;`, line 167):
   `void addPassesToHandleExceptions() override;`
2. After `MSILPassConfig::addIRPasses()` (after line 244):

```cpp
void MSILPassConfig::addPassesToHandleExceptions() {
  // The exception model is always None (decision D8), and addIRPasses has
  // already lowered every invoke into a call and removed the landing pads
  // that became unreachable, before the target and generic IR passes.
  // TargetPassConfig would add LowerInvoke and UnreachableBlockElim a second
  // time, after CodeGenPrepare.
  assert(TM->getMCAsmInfo().getExceptionHandlingType() ==
             ExceptionHandling::None &&
         "MSIL has no exception model (decision D8)");
}
```

   plus `#include "llvm/MC/MCAsmInfo.h"` after `#include "llvm/IR/Function.h"` (line 30). The assert is
   optional (variant A has an empty body with the same comment; both build, both clang-format clean);
   it documents that a later phase that gives MSIL an exception model must revisit this override.
3. Optionally extend the comment at lines 234-235 to say why the lowering is here: "... here, before
   the target and generic IR passes; addPassesToHandleExceptions adds nothing."

Do not touch lines 234-237 (the early pair stays). No other source file changes. Clang needs no change
(it runs the same MSILPassConfig).

## 8. Test plan

Owned file: `llvm/test/CodeGen/MSIL/pipeline.ll` (amend; no new test file is needed). Spec for an
independent test author (no need to read MSILTargetMachine.cpp): on both execution triples at -O0 and
-O2, and in the MinGW `-exception-model=wineh` run, `llc -debug-pass=Structure` shows
'Lower invoke and unwind, for unwindless code generators' exactly once, after 'MSIL Fix Function
Bitcasts' (after 'MSIL Optimize Returned' at -O2) and before 'Expand indirectbr instructions', and
'Remove unreachable blocks from the CFG' exactly twice: once right after LowerInvoke (before Expand
indirectbr) and once after 'Shadow Stack GC Lowering'; neither appears after 'CodeGen Prepare' or
between 'Expand reduction intrinsics' and 'Expand Atomic instructions'.

Exact change (verified, `<S>\lit\pipeline.ll`): extend `%{check}`

```
; DEFINE:   --implicit-check-not="Windows exception handling preparation" \
; DEFINE:   --implicit-check-not="Lower invoke and unwind" \
; DEFINE:   --implicit-check-not="Remove unreachable blocks from the CFG"
```

and replace the CHECK block lines 72-78

```
; CHECK:       Lower invoke and unwind, for unwindless code generators
; CHECK:       Remove unreachable blocks from the CFG
; CHECK:       Expand indirectbr instructions
; CHECK:       Shadow Stack GC Lowering
; CHECK:       Remove unreachable blocks from the CFG
; O2:          CodeGen Prepare
; CHECK:       Expand Atomic instructions
```

(the old block had `O2: CodeGen Prepare` followed by a second LowerInvoke/UBE pair). The 'Pass
Arguments' line spells the passes `-lowerinvoke` and `-unreachableblockelim`, which the case-sensitive
implicit checks do not match. RUN lines stay as they are (5 runs; no new lit feature;
`REQUIRES` comes from lit.local.cfg). Header comment (lines 1-14) to rewrite: the pipeline was fixed
in M2 (D33) and M11 removed the second invoke lowering; the 'exceptions' bullet becomes "the exception
model is always None (decision D8) and MSILPassConfig::addPassesToHandleExceptions adds nothing, so
LowerInvoke runs exactly once, in addIRPasses, and UnreachableBlockElim exactly twice (MSIL's, right
after LowerInvoke, and the generic one TargetPassConfig::addIRPasses adds after the GC lowering
passes); nothing runs between CodeGenPrepare and AtomicExpand, and no DWARF, SjLj or Windows EH
preparation pass is added, even when -exception-model asks for one (MinGW run)"; and the
"Every output line that names an MSIL pass must be matched" paragraph also names the two
EH strings.

Existing functional coverage that guards the early lowering (all pass with option A, section 6.4):
`function-bitcasts.ll` (test_invoke, landingpad, -O0 and -O2, both triples, -verify-machineinstrs),
`call-errors.ll` (split part invoke.ll), `frame-escape-errors.ll` (cleanuppad/catchswitch).
`llvm/unittests/Target/MSIL/MSILTargetMachineTest.cpp:92-104` already pins MCAsmInfo exception type
None. Optional synergy with item 10: a clang test that runs `clang --target=<msil> -S -mllvm
-debug-pass=Structure` and counts LowerInvoke once would cover both items (not required here).

## 9. Gate steps

- Targeted (fails without the fix, passes with it):
  `pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv" llvm/test/CodeGen/MSIL/pipeline.ll`
  Runtime 10-25 s (5 llc runs; 24 s measured on a loaded host). No ':' in the value, so no leading
  space is needed.
- It is also inside the standing lit step (M10 step 5 form: `msil-lit.ps1 -LitArgs "-sv --param
  msil_require_verify=1 ..." llvm/test/CodeGen/MSIL ...`); llvm/test/CodeGen/MSIL alone took 108 s
  with 8 workers.
- Build step: `msil-build.ps1 llc opt clang ...` rebuilds one TU (MSILTargetMachine.cpp), the
  LLVMMSILCodeGen library and relinks llc, opt, clang and the unit tests that link MSILCodeGen:
  about 5-10 minutes, no measurable extra disk on D:.

## 10. Plan and documentation updates for the drafter

- Plan 5.3 listing (`ImplementationPlan.md:404-406`): add a line
  `// addPassesToHandleExceptions: nothing (invokes are lowered in addIRPasses; D8)`.
- D8 (`ImplementationPlan.md:147`) may mention that MSILPassConfig adds no exception-handling passes.
- Section 12: a row that closes item (5) and corrects its cause (pre-existing since M2;
  fa24f6dba7cd only made the override possible); pipeline.ll's "does not change again in Phase 1"
  (D33) does not bind M11.
- `llvm/docs/MSIL/recon/passes-frame.md` is historical M0 recon; leave it.
- Suggested ownership: an implement WP owns `llvm/lib/Target/MSIL/MSILTargetMachine.cpp` (MSILTargetMachine.cpp
  has had no owner since M3's fix-7); a test WP owns `llvm/test/CodeGen/MSIL/pipeline.ll`.

## 11. Scratch artifacts

`<S>\struct-*.txt` (Structure dumps), `<S>\corpus\` (inputs, 452 clang IR files), `<S>\extra\`
(invoke inputs), `<S>\ehcount-current.txt`, `<S>\ildiff.txt`, `<S>\lit-C0.txt`, `<S>\lit-A.txt`,
`<S>\build\{C0,A,A2,B}\` (sources, objects, llc.exe), `<S>\lit\` (draft pipeline.ll + minimal lit
config), `<S>\fake\` (scratch site config + tool copies), `<S>\tools\` (corpus.py, ehcount.py,
ildiff.py, build.ps1, patch.py, patchA2.py, edit_pipeline.py). About 330 MB on C:.
