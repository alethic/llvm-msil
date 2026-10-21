# M11 recon: item 4 - `llc -stop-after=msil-isel` / `-stop-before=msil-argument-move` abort

Item numbering: plan section 12 row 'Deferred after the upstream merge' (2026-10-19), item (4).
Tree: `msil` at 0c5044bac349 (contains merge 2ea3fa74b232 of upstream 64f50ffd4f1c; fork base d1e15b695381).
Tools: the shared gate build `D:\llvm-msil\build` (not rebuilt, not reconfigured). Scratch:
`<scratch>\m11\stopafter`
(below: `$S`).

## 1. Verdict

* Reproduced on every MSIL triple (x86_64 linux-gnu, windows-msvc, w64-windows-gnu; aarch64 linux-gnu,
  windows-msvc) at -O0..-O3. The only failing stop points are `-stop-after=msil-isel` and the equivalent
  `-stop-before=msil-argument-move`. Every later machine stop point (42 pass names, swept over the
  whole llvm/test/CodeGen/MSIL corpus, section 5) is verifier-clean.
* The only machine-verifier error is `*** Bad machine code: Using an undefined physical register ***` on
  the `implicit $arguments` operand of an `ARGUMENT_<RC>` that the ScheduleDAG scheduler placed after a
  non-ARGUMENT instruction (every such instruction carries `implicit-def dead $arguments`).
* Cause: upstream 663669080864 "llc: Verify MIR outputs by default (#228248)" (Matt Arsenault,
  2026-10-12) adds a machine verifier "At end of pipeline" whenever llc stops before code emission.
  Pre-ArgumentMove MSIL MIR was never verifier-clean: it already failed with `-verify-machineinstrs`,
  and the MIR parser (which verifies on read since before the fork base) could not read it back.
* Upstream WebAssembly has exactly the same defect (`llc -mtriple=wasm32 -stop-after=wasm-isel` aborts
  the same way on 144 of 343 WebAssembly CodeGen tests); no upstream test stops there.
* Recommended fix (prototyped, compiled into a private llc, validated): make the ISel output
  verifier-clean by moving the ARGUMENT instructions to the top of the entry block at the end of
  `MSILDAGToDAGISel::runOnMachineFunction`, through a helper shared with MSILArgumentMove (moved into
  MSILUtilities). MSILArgumentMove stays in the pipeline (no-op on ISel output, still needed for
  hand-written MIR). No `-disable-mir-output-verify`, no `FailsVerification`, no reserved register.

## 2. Reproduction

Input `$S\small\late.ll` (also used by the proposed test):

```llvm
define i64 @rotr_i64(i64 %a, i64 %b) {
  %r = call i64 @llvm.fshr.i64(i64 %a, i64 %a, i64 %b)
  ret i64 %r
}
define i32 @sel(i1 %c, i32 %a, i32 %b) {
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}
define i64 @mix(i32 %a, i64 %b) {
  %x = zext i32 %a to i64
  %y = mul i64 %x, 12345678901
  %z = add i64 %y, %b
  ret i64 %z
}
declare i64 @llvm.fshr.i64(i64, i64, i64)
```

A trivial `add i32 %a, %b` does NOT reproduce (both ARGUMENTs are scheduled first); a reproducer needs an
argument whose first user is scheduled after another node (a constant, a conversion of another argument).

Commands (Git Bash, `LLC=/d/llvm-msil/build/bin/llc.exe`), result per triple:

```
$LLC -mtriple=<T> -stop-after=msil-isel late.ll -o out.mir           -> exit 132 (0xC000001D, abort)
$LLC -mtriple=<T> -stop-before=msil-argument-move late.ll -o out.mir -> exit 132
$LLC -mtriple=<T> -stop-after=msil-argument-move late.ll -o out.mir  -> exit 0
$LLC -mtriple=<T> -stop-after=msil-isel -disable-mir-output-verify late.ll -o pre.mir -> exit 0
$LLC -mtriple=<T> -stop-after=msil-isel -verify-machineinstrs late.ll  -> exit 132 ("# After Instruction Selection")
$LLC -mtriple=<T> -run-pass=none pre.mir                           -> exit 132 (verify on MIR read)
$LLC -mtriple=<T> -run-pass=msil-argument-move pre.mir             -> exit 132 (verify on MIR read)
```

for `<T>` = x86_64-unknown-linux-gnu-msil, x86_64-pc-windows-msvc-msil (also aarch64-unknown-linux-gnu-msil,
aarch64-pc-windows-msvc-msil, x86_64-w64-windows-gnu-msil: exit 132, same instruction).

Abridged stderr (x86_64-unknown-linux-gnu-msil, same on windows-msvc):

```
# At end of pipeline
# Machine code for function rotr_i64: IsSSA, TracksLiveness
Function Live Ins: $arguments

bb.0 (%ir-block.0):
  liveins: $arguments
  %0:i64 = ARGUMENT_I64 1, implicit $arguments
  %1:i32 = CONV_I4_I64 %0:i64, implicit-def dead $arguments
  %2:i64 = ARGUMENT_I64 0, implicit $arguments
  %3:i32 = CONST_I32 63, implicit-def dead $arguments
  ...
  RETURN %9:i64, implicit-def dead $arguments

*** Bad machine code: Using an undefined physical register ***
- function:    rotr_i64
- basic block: %bb.0  (0x18752e984d0)
- instruction: %2:i64 = ARGUMENT_I64 0, implicit $arguments
- operand 2:   implicit $arguments
LLVM ERROR: Found 1 machine code errors.
PLEASE submit a bug report to https://github.com/llvm/llvm-project/issues/ ...
2.	Running pass 'Verify generated machine code' on function '@rotr_i64'
Exception Code: 0xC000001D
```

`sel` before any move (`-disable-mir-output-verify`, -O0 and -O2 identical):

```
    %0:i32 = ARGUMENT_I32 0, implicit $arguments
    %1:i32 = CONST_I32 1, implicit-def dead $arguments
    %2:i32 = AND_I32 %0, %1, implicit-def dead $arguments
    %3:i32 = ARGUMENT_I32 2, implicit $arguments        <- error
    %4:i32 = ARGUMENT_I32 1, implicit $arguments        <- error
    %5:i32 = SELECT_I32 %2, %4, %3, implicit-def dead $arguments
    RETURN %5, implicit-def dead $arguments
```

The verifier reports per function and aborts after the first function with errors, so a module shows only
its first bad function.

## 3. Which upstream change, and what it does

`git log --oneline -S "disable-mir-output-verify" d1e15b695381..64f50ffd4f1c` and the same with
`-S "MachineVerifierMode"` / `-S "At end of pipeline"` all give exactly one commit:

    663669080864 llc: Verify MIR outputs by default (#228248)

It adds `enum class MachineVerifierMode { None, End, Each }` (llvm/include/llvm/Support/CodeGen.h) and:

* llvm/lib/CodeGen/TargetPassConfig.cpp:321-327 `computeMachineVerifierMode`: `Each` with
  `-verify-machineinstrs`, else `None` with `-disable-mir-output-verify`, else `End`; :465-470 sets
  `VerifyMode` in the TargetPassConfig constructor (EXPENSIVE_CHECKS: default from
  `TM.isMachineVerifierClean()`).
* llvm/lib/CodeGen/CodeGenTargetMachineImpl.cpp:244-246: when the pipeline does not complete
  (`-stop-after/-stop-before`), `PM.add(createMachineVerifierPass("At end of pipeline"))` before
  `createPrintMIRPass`.
* llvm/tools/llc/lib/llcdriver.cpp:830-831: the same for `-run-pass`.
* llvm/lib/CodeGen/CodeGenOptions.td:124-125: `-disable-mir-output-verify` ("Do not verify machine code at
  the end of a pipeline that stops before code emission").
* llvm/lib/Passes/CodeGenPassBuilder.cpp:277-279: new-PM equivalent (MSIL has no new-PM codegen pipeline:
  `llc -enable-new-pm` on an MSIL triple prints "buildCodeGenPipeline is not overridden").
* The commit's only test adjustments add `-disable-mir-output-verify` to three tests that already failed
  the verifier, each with a FIXME naming an upstream bug (llvm/test/CodeGen/X86/callbr-asm-outputs-indirect-isel.ll,
  -isel-m32.ll: issue 60827; llvm/test/DebugInfo/Generic/linear-dbg-value.ll: PR39452). That is upstream's
  stance: MIR printed at a stop point is expected to be verifier-clean, and opting out is a tracked FIXME,
  not a target hook.

Pre-existing parts (unchanged since d1e15b695381, so the defect is older than the merge):

* TargetPassConfig.cpp:923-928 `addCoreISelPasses`: "Don't run the verifier before FinalizeISel", then
  `printAndVerify("After Instruction Selection")`. `addPass` adds the passes of `addInstSelector`
  without per-pass verifiers (AddingMachinePasses is false there, :590-600), but `printAndVerify` is
  called even when the pipeline already stopped, so `-stop-after=msil-isel -verify-machineinstrs` runs
  the verifier on pre-ArgumentMove MIR and aborted before the merge as well (base: line 913).
* llvm/lib/CodeGen/MIRParser/MIRParser.cpp:697 `MF.verify(nullptr, nullptr, &errs())` (AbortOnError):
  every parsed MIR function is verified on read, and this ignores `failsVerification: true` (checked:
  the same abort with the property set, `$S\small\pre-fv.mir`). Present at the base too (line 697).
  So `-stop-after=msil-isel` output with a late ARGUMENT could never be fed back to `-run-pass`.
* Only the pass wrappers skip `FailsVerification` (llvm/lib/CodeGen/MachineVerifier.cpp:394-399 and
  :411-415, "FIXME: Remove this mechanism when all problematic passes have been fixed"; used only by
  R600MachineCFGStructurizer.cpp:124 and HexagonVLIWPacketizer.cpp:196).

## 4. Why the MIR is not verifier-clean (MSIL and WebAssembly alike)

* llvm/lib/Target/MSIL/MSILRegisterInfo.td:49-52 `def ARGUMENTS` (copied from
  WebAssemblyRegisterInfo.td:54-57): "an opaque entity which serves to order the ARGUMENT instructions
  ... must not be scheduled below other instructions".
* llvm/lib/Target/MSIL/MSILInstrFormats.td:56-57: every normal instruction `let Defs = [ARGUMENTS]`
  (WebAssemblyInstrFormats.td:39).
* llvm/lib/Target/MSIL/MSILInstrInfo.td:123-132: `ARGUMENT_<RC>` has `Defs = []`, `Uses = [ARGUMENTS]`
  (WebAssemblyInstrInfo.td:312-318).
* llvm/lib/CodeGen/SelectionDAG/InstrEmitter.cpp:1167-1169: unused implicit physreg defs are marked
  dead (`setPhysRegsDeadExcept`), hence `implicit-def dead $arguments` everywhere.
* The SelectionDAG scheduler does not order chainless ARGUMENT nodes against chainless arithmetic or
  constants (implicit physreg defs of MachineInstrs are not DAG edges), so an ARGUMENT can follow a
  CONST/AND/CONV. With TracksLiveness, a use of `$arguments` after a dead def is "Using an undefined
  physical register".
* MSILArgumentMove (llvm/lib/Target/MSIL/MSILArgumentMove.cpp:70-98, copy of
  WebAssemblyArgumentMove.cpp:68-96) is the repair and runs immediately after ISel
  (MSILTargetMachine.cpp:253-268; WebAssemblyTargetMachine.cpp:347-...). After it, every ARGUMENT reads
  the live-in `$arguments` before any def: clean. Stopping at msil-isel prints the MIR before the repair.
* Register properties are not the problem: the printed properties are `IsSSA, TracksLiveness` (correct for
  ISel output; NoVRegs false, NoPHIs false). Register classes and other operands produce no errors.

## 5. Corpus sweeps (shared llc)

Script `$S\sweep-proto.py` (and the earlier `$S\sweep.py`, `$S\sweep2.py`): runs
`llc -mtriple=<T> <O> <stop> <file> -o NUL` over all 101 `llvm/test/CodeGen/MSIL/*.ll`; a job counts as a
verifier failure only when it fails and the identical command with `-disable-mir-output-verify` succeeds
(error tests are ignored).

| sweep | triples | opt levels | stop points | jobs | verifier failures |
|---|---|---|---|---|---|
| sweep1 (`$S\sweep1.txt`) | linux-gnu, windows-msvc | O0,O1,O2,O3 | stop-after msil-isel, stop-before msil-argument-move, stop-after msil-argument-move, stop-after msil-set-mem-flags, stop-after msil-clean-code-after-trap, stop-after finalize-isel | 4848 | 112, all at stop-after=msil-isel / stop-before=msil-argument-move (14 per stop point and level = 7 files x 2 triples), all 'Using an undefined physical register' |
| sweep2 (`$S\sweep2.txt`) | windows-msvc | O0,O2 | stop-after each of 38 later machine passes (early-tailduplication ... msil-stack-check) | 7676 | 0 |

Failing files (first bad function, -O2, windows-msvc; identical on linux-gnu and at O0/O1/O3):
indirectbr.ll @ibr_loop (ARGUMENT_I32 0), int-arith.ll @rotr_i64 (ARGUMENT_I64 0), loop-phi.ll @fib
(ARGUMENT_I32 0), narrow-ints.ll @slt_i8 (ARGUMENT_I32 0), returned.ll @test_not_dominated
(ARGUMENT_I64 0), select.ll @select_i1_noext (ARGUMENT_I32 2 and 1), tti-hooks.ll @msil_sum_1024
(ARGUMENT_I64 0).

Other pre-ArgumentMove pass names: there are none. The pipeline (`llc -debug-pass=Arguments`, -O0 and
-O2) has only `-msil-isel` between the IR passes and `-msil-argument-move`
(`... -stack-protector -verify -debug-ata -msil-isel -msil-argument-move -msil-set-mem-flags
-msil-clean-code-after-trap -finalize-isel ...`); `TargetPassConfig::addInstSelector()` adds nothing.
IR-level stop points (msil-prepare-ir, msil-fix-function-bitcasts, ...) are unaffected (the existing
tests that use them pass). The trap case WebAssembly also hits (section 6) does not exist for MSIL:
MSILPrepareIR turns everything after a trap into `unreachable` (plan section 12, M6 fix-2), and
`call @llvm.trap(); call @g()` is clean at msil-isel, msil-argument-move and both sides of
msil-clean-code-after-trap.

## 6. WebAssembly at the same stop points

`$S\small\late.ll` on wasm32/wasm64: `-stop-after=wasm-isel` and `-stop-before=wasm-argument-move` exit 132
with the same error (function @sel: `%1:i32 = ARGUMENT_i32 0, implicit $arguments` after
`%0:i32 = CONST_I32 1, implicit-def dead $arguments`, 3 errors); `-stop-after=wasm-argument-move` exits 0.

Whole WebAssembly corpus (`$S\sweep-wasm.py`, all 343 `llvm/test/CodeGen/WebAssembly/*.ll` forced to
`-mtriple=wasm32-unknown-unknown`, default features, `$S\sweep-wasm.txt`):

| stop point | ok | verifier-only failure | fails for other reasons too |
|---|---|---|---|
| `-stop-after=wasm-isel` | 158 | 145 (144 'Using an undefined physical register', 1 'Non-terminator instruction after the first terminator' in null-streamer.ll) | 40 |
| `-stop-after=wasm-argument-move` | 302 | 1 (null-streamer.ll, trap followed by a call; WebAssemblyCleanCodeAfterTrap runs later) | 40 |

Upstream has no test that stops at wasm-isel or wasm-argument-move (grep of llvm/test finds none), and no
upstream fix or hook exists; WebAssembly is in the same state as MSIL. Other targets do expect clean ISel
output at stop points: 48 AMDGPU tests use `-stop-after=amdgpu-isel` and 25 CodeGen tests use
`-stop-after=x86-isel`/`aarch64-isel`/`-stop-before=finalize-isel`, all passing the new end verifier.

## 7. Fix options

A. **(Recommended) ISel leaves the ARGUMENTs at the top.** In `MSILDAGToDAGISel::runOnMachineFunction`
   (llvm/lib/Target/MSIL/MSILISelDAGToDAG.cpp:54-73), after `SelectionDAGISel::runOnMachineFunction(MF)`
   returns, move every ARGUMENT_<RC> of the entry block above the first other instruction (the existing
   MSILArgumentMove algorithm). Nothing else runs between the scheduler and that point
   (SelectionDAGISelLegacy::runOnMachineFunction returns `Selector->runOnMachineFunction(MF)` directly,
   SelectionDAGISel.cpp:396; SelectionDAGISel itself never verifies). `TargetLowering::finalizeLowering`
   is NOT a usable hook: it runs in FinalizeISel (FinalizeISel.cpp:52), after the stop point.
   Result: `-stop-after=msil-isel` prints exactly what `-stop-after=msil-argument-move` prints today
   (verified to be clean for the whole corpus), the MIR reads back, and the contract operand lists of
   plan 5.4 (`implicit-def dead $arguments`) are unchanged.
B. `FailsVerification` (set in ISel, cleared in ArgumentMove): rejected. The MIR parser still aborts on
   read (section 3), so the output stays unusable as `-run-pass` input; upstream calls the mechanism a
   FIXME to be removed.
C. Reserve `ARGUMENTS` (the verifier does not track liveness of reserved registers): rejected; already
   decided against in llvm/lib/Target/MSIL/MSILRegisterInfo.cpp:48-51 (a def of a reserved register
   keeps dead instructions alive, MachineInstr::isDead), and it would weaken the ordering the register
   exists for.
D. Drop the `dead` flags on `implicit-def $arguments` after ISel: rejected; changes every instruction's
   operand list against plan 5.4 and every MIR test, and the defs really are dead.
E. Order ARGUMENT nodes in the DAG (chain or glue): rejected; the scheduler still places chainless
   constants and arithmetic before them, so it cannot guarantee the order.
F. Only document `-disable-mir-output-verify` for these stop points (upstream's opt-out for known
   failures): not a fix; the MIR still cannot be read back. Use only if A is not taken, with a FIXME.

Keep MSILArgumentMove in the pipeline (plan 5.3 order and pipeline.ll unchanged): it becomes a no-op on
ISel output but still repairs hand-written MIR (reg-argument.mir uses `-run-pass=msil-argument-move` and
`-start-before=msil-argument-move` on inputs with late ARGUMENTs), mirroring WebAssembly's structure.
Removing it would change pipeline.ll, plan 5.3 and reg-argument.mir for no gain.

## 8. Prototype and validation

Built without touching the repository or `D:\llvm-msil\build`:

1. `$S\proto\MSILISelDAGToDAG.cpp` = repo file plus the move loop (diff: `$S\proto\prototype.diff`):

   ```cpp
       bool Changed = SelectionDAGISel::runOnMachineFunction(MF);
       MachineBasicBlock &EntryMBB = MF.front();
       MachineBasicBlock::iterator InsertPt = EntryMBB.end();
       for (MachineInstr &MI : EntryMBB)
         if (!MSIL::isArgument(MI.getOpcode())) {
           InsertPt = MI;
           break;
         }
       for (MachineInstr &MI : llvm::make_early_inc_range(
                llvm::make_range(InsertPt, EntryMBB.end())))
         if (MSIL::isArgument(MI.getOpcode()))
           EntryMBB.insert(InsertPt, MI.removeFromParent());
       return Changed;
   ```
2. Compiled with the build's exact command line from `D:\llvm-msil\build\compile_commands.json`
   (`$S\proto\compile.ps1`, dot-sources msil-env.ps1; /W4 etc., no warnings), relinked llc with the
   build's link line (`$S\proto\llc.rsp`, the prototype object first) into `$S\proto\llc.exe`.
3. Results with the prototype llc:
   * late.ll on both x86_64 triples: `-stop-after=msil-isel` and `-stop-before=msil-argument-move` exit 0;
     `-run-pass=none` on the output exits 0; `-start-after=msil-isel` on the output gives IL byte-identical
     to the full pipeline. `sel` now prints ARGUMENT_I32 0, 2, 1, then CONST_I32, AND_I32, SELECT_I32,
     RETURN.
   * clang -g output (`$S\proto\run\dbg.c`, -O0/-O2, both triples): shared llc aborts at -O2, prototype
     exits 0; argument DBG_VALUEs (inserted after their ARGUMENT by SelectionDAGISel.cpp:708-727) stay
     after the moved ARGUMENTs; read-back and `-start-after=msil-isel` IL identical to the full pipeline.
   * Full `llvm/test/CodeGen/MSIL` (108 tests) through a private lit root (`$S\proto\litroot`, tools dir
     `$S\proto\bin` = prototype llc + copies of FileCheck/not/count/split-file/opt/llvm-config),
     `--param msil_require_verify=1`: **108/108 passed** (`$S\proto\lit-proto.txt`), including
     reg-argument.mir and pipeline.ll.
   * Corpus sweep with the prototype (`$S\sweep-proto.txt`): both x86_64 triples, -O0..-O3,
     `-stop-after=msil-isel`, `-stop-before=msil-argument-move`, `-stop-after=msil-argument-move` over all
     101 .ll files: **2424 jobs, 0 verifier failures** (shared llc: 112 failures on the same jobs).
   * The proposed test (section 10, `$S\proto\suite\stop-after-isel.ll`) FAILS with the shared llc (first
     RUN line, 'Using an undefined physical register', exit 0xc000001d) and PASSES with the prototype
     (ilasm round trip included).

## 9. Implementation spec (for the WP)

Owned files: `llvm/lib/Target/MSIL/MSILUtilities.h`, `llvm/lib/Target/MSIL/MSILUtilities.cpp`,
`llvm/lib/Target/MSIL/MSILISelDAGToDAG.cpp`, `llvm/lib/Target/MSIL/MSILArgumentMove.cpp`.

1. MSILUtilities.h (namespace `llvm::MSIL`, next to getCopyOpcodeForRegClass, :78): declare
   `bool moveArgumentsToTop(MachineFunction &MF);` with a doc comment: moves every ARGUMENT_<RC> of the
   entry block above its first other instruction, keeping the relative order of the ARGUMENTs and of the
   other instructions; returns true if an instruction moved. (Needs a `class MachineFunction;` forward
   declaration if the header does not include MachineFunction.h.)
2. MSILUtilities.cpp: define it with the body of `argumentMove` from MSILArgumentMove.cpp:70-98 (the
   LLVM_DEBUG banner stays in the pass). Use `llvm::make_early_inc_range` for the second loop (the
   current loop re-walks the block after each move; correct but quadratic).
3. MSILArgumentMove.cpp: `runOnMachineFunction` keeps the debug banner and returns
   `MSIL::moveArgumentsToTop(MF)`; delete the static `argumentMove`. Update the file comment: instruction
   selection already moves the ARGUMENTs (so the MIR printed at `-stop-after=msil-isel` passes the machine
   verifier that llc runs at the end of a pipeline that stops early); the pass stays for MIR input.
4. MSILISelDAGToDAG.cpp `runOnMachineFunction`: replace `return SelectionDAGISel::runOnMachineFunction(MF);`
   by calling it, then `MSIL::moveArgumentsToTop(MF);`, then `return Changed;`, with a comment that the
   ScheduleDAG scheduler can place an ARGUMENT after an instruction with `implicit-def dead $arguments`,
   which the machine verifier rejects (upstream 663669080864 verifies MIR at every stop point; the MIR
   parser verifies on read). Add `#include "MSILUtilities.h"`. Keep the existing `setUseDebugInstrRef`
   call (plan section 12, M4.a row).
5. Do not change `MSILTargetMachine.cpp`, the .td files, MSILRegisterInfo, pipeline.ll or
   reg-argument.mir. Do not add `-disable-mir-output-verify` anywhere.

clang-format the changed C++ (gate step like M10 step 14).

## 10. Test plan

New file `llvm/test/CodeGen/MSIL/stop-after-isel.ll` (no WebAssembly original exists; MSIL-only test,
named after the stop point). Lit features: none beyond the directory's `MSIL` target requirement
(lit.local.cfg); optional `ilasm` feature and `%ilasm-verify` substitution from lit.local.cfg. Spec:

* Functions whose ISel schedule puts an ARGUMENT after another instruction at both -O0 and -O2 on both
  x86_64 triples: `i32 @sel(i1 %c, i32 %a, i32 %b)` = `select i1 %c, i32 %a, i32 %b` (before the fix:
  ARGUMENT_I32 0, CONST_I32 1, AND_I32, ARGUMENT_I32 2, ARGUMENT_I32 1) and
  `i64 @rotr_i64(i64 %a, i64 %b)` = `llvm.fshr.i64(%a, %a, %b)` (before: ARGUMENT_I64 1, CONV_I4_I64,
  ARGUMENT_I64 0).
* RUN: `-stop-after=msil-isel` at -O0 and -O2 on x86_64-unknown-linux-gnu-msil and
  x86_64-pc-windows-msvc-msil into %t files, FileCheck each; `-stop-before=msil-argument-move` at -O2 on
  both triples piped to FileCheck; `-run-pass=none` on the -O2 outputs (verify on read);
  `-start-after=msil-isel` on the -O2 outputs compared with `diff` against the full pipeline's IL for the
  same triple; `%if ilasm %{ %ilasm-verify ... %}` on one of the IL files. No RUN line passes
  `-disable-mir-output-verify` or `-verify-machineinstrs=0` (say so in the header).
* CHECKs: per function, `liveins: $arguments`, the blank line (`{{  $}}`, as reg-argument.mir does), then
  exactly N `ARGUMENT_<RC> {{[0-N-1]}}, implicit $arguments` lines with CHECK-NEXT, then `CHECK-NOT:
  ARGUMENT_` up to `RETURN`. Do not pin the ARGUMENT order among themselves or the vreg numbers (they
  follow the scheduler).
* Fails without the fix: llc aborts in the 'At end of pipeline' verifier at the first RUN line.

Validated text (`$S\proto\suite\stop-after-isel.ll`, run with both llcs through `$S\proto\suite\lit.cfg.py`):

```llvm
; MIR printed at the msil-isel stop point must pass the machine verifier that
; llc runs at the end of a pipeline that stops before code emission (upstream
; 663669080864, -disable-mir-output-verify to opt out; no RUN line here passes
; it). The ScheduleDAG scheduler may put an ARGUMENT_<RC> after an instruction
; that has 'implicit-def dead $arguments'; the verifier then reports 'Using an
; undefined physical register' for the ARGUMENT's 'implicit $arguments'.
; MSILDAGToDAGISel moves every ARGUMENT to the top of the entry block before it
; returns, so -stop-after=msil-isel and -stop-before=msil-argument-move print
; the same verifier-clean MIR. MSILArgumentMove still runs and still repairs
; hand-written MIR (reg-argument.mir).
;
; Each MIR output is read back (the MIR parser verifies on read) and compiled
; on to IL from -start-after=msil-isel; the IL must equal the full pipeline's.

; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O0 -stop-after=msil-isel -o %t.lnx.O0.mir
; RUN: FileCheck %s --input-file=%t.lnx.O0.mir
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O2 -stop-after=msil-isel -o %t.lnx.O2.mir
; RUN: FileCheck %s --input-file=%t.lnx.O2.mir
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O0 -stop-after=msil-isel -o %t.win.O0.mir
; RUN: FileCheck %s --input-file=%t.win.O0.mir
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O2 -stop-after=msil-isel -o %t.win.O2.mir
; RUN: FileCheck %s --input-file=%t.win.O2.mir
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O2 -stop-before=msil-argument-move -o - \
; RUN:   | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O2 -stop-before=msil-argument-move -o - \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -run-pass=none %t.lnx.O2.mir -o /dev/null
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -run-pass=none %t.win.O2.mir -o /dev/null
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -start-after=msil-isel %t.lnx.O2.mir -o %t.lnx.il
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O2 -o %t.lnx.ref.il
; RUN: diff %t.lnx.ref.il %t.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -start-after=msil-isel %t.win.O2.mir -o %t.win.il
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O2 -o %t.win.ref.il
; RUN: diff %t.win.ref.il %t.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.dll %t.win.il %}

; Before the fix ISel printed ARGUMENT_I32 0, CONST_I32 1, AND_I32,
; ARGUMENT_I32 2, ARGUMENT_I32 1 at -O0 and -O2.
; CHECK-LABEL: name: sel{{$}}
; CHECK:       bb.0
; CHECK-NEXT:    liveins: $arguments
; CHECK-NEXT:    {{  $}}
; CHECK-NEXT:    %{{[0-9]+}}:i32 = ARGUMENT_I32 {{[0-2]}}, implicit $arguments
; CHECK-NEXT:    %{{[0-9]+}}:i32 = ARGUMENT_I32 {{[0-2]}}, implicit $arguments
; CHECK-NEXT:    %{{[0-9]+}}:i32 = ARGUMENT_I32 {{[0-2]}}, implicit $arguments
; CHECK-NOT:     ARGUMENT_
; CHECK:         RETURN
define i32 @sel(i1 %c, i32 %a, i32 %b) {
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}

; Before the fix: ARGUMENT_I64 1, CONV_I4_I64, ARGUMENT_I64 0.
; CHECK-LABEL: name: rotr_i64{{$}}
; CHECK:       bb.0
; CHECK-NEXT:    liveins: $arguments
; CHECK-NEXT:    {{  $}}
; CHECK-NEXT:    %{{[0-9]+}}:i64 = ARGUMENT_I64 {{[01]}}, implicit $arguments
; CHECK-NEXT:    %{{[0-9]+}}:i64 = ARGUMENT_I64 {{[01]}}, implicit $arguments
; CHECK-NOT:     ARGUMENT_
; CHECK:         RETURN
define i64 @rotr_i64(i64 %a, i64 %b) {
  %r = call i64 @llvm.fshr.i64(i64 %a, i64 %a, i64 %b)
  ret i64 %r
}

declare i64 @llvm.fshr.i64(i64, i64, i64)
```

Optional: a debug-info case (clang -g -O2 output of `int sel(_Bool c, int a, int b) { return c ? a : b; }`)
showing DBG_VALUEs after the moved ARGUMENTs; it also aborts today at -O2 and passes with the prototype.
Existing tests unchanged: reg-argument.mir (MIR-level ArgumentMove), pipeline.ll (pass order).

## 11. Gate step and cost

* Gate step (fails without the fix, ~15 s):
  `pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" llvm/test/CodeGen/MSIL/stop-after-isel.ll llvm/test/CodeGen/MSIL/reg-argument.mir llvm/test/CodeGen/MSIL/pipeline.ll`
  The new test is also covered by the full llvm/test/CodeGen/MSIL lit step (M10 step 5 style); the
  whole directory took 300 s at -j4 with the prototype.
* Optional one-off check for the WP (not a gate): `$S\sweep-proto.py` with the gate-built llc:
  `python sweep-proto.py x -stop-after=msil-isel,-stop-before=msil-argument-move -O0,-O2` must report
  `failures 0` (404 jobs, ~2 min at 6 threads).
* Cost: 3 TUs (MSILISelDAGToDAG.cpp, MSILArgumentMove.cpp, MSILUtilities.cpp) plus relinking llc, clang
  and the MSIL unit tests: ~5-10 min with msil-build.ps1; no new disk use beyond the incremental
  objects (<100 MB). The prototype compile + relink took about 1 min.

## 12. Plan text impacts

* Plan 5.3 pipeline comment (ImplementationPlan.md:407) stays `addInstSelector: base, ISelDag,
  ArgumentMove, SetMemFlags, CleanCodeAfterTrap`; add a contract sentence near it or in 5.4: "Instruction
  selection returns with every ARGUMENT_<RC> first in the entry block (MSIL::moveArgumentsToTop, shared
  with MSILArgumentMove), so MIR printed at every llc stop point from msil-isel on passes the machine
  verifier; no MSIL test passes -disable-mir-output-verify."
* Ownership table (ImplementationPlan.md:318, :330, :372-373): the M11 WP owns MSILArgumentMove.cpp,
  MSILISelDAGToDAG.cpp, MSILUtilities.h/.cpp; the test WP owns llvm/test/CodeGen/MSIL/stop-after-isel.ll.
* Section 12 row: item (4) fixed by option A, with upstream 663669080864 as the trigger and the note that
  upstream WebAssembly has the same defect (candidate upstream report/patch for
  WebAssemblyISelDAGToDAG.cpp; out of the fork's scope).

## 13. Notes

* No interaction with the other deferred items, except (3) (function splitting) and (5) (LowerInvoke
  twice), which touch MSILTargetMachine.cpp; this fix does not.
* Under EXPENSIVE_CHECKS the default `-verify-machineinstrs` comes from `isMachineVerifierClean()` (MSIL
  inherits `true`); with the fix the "After Instruction Selection" verifier (also run when stopping at
  msil-isel) is clean.
* The `$S\wasm.txt` file from the first, sequential WebAssembly sweep may have a few entries disturbed by a
  concurrent run in the same directory; section 6 gives the clean rerun (`$S\sweep-wasm.txt`), which
  agrees (145 files).
* Nothing in the repository or in `D:\llvm-msil\build` was modified; the prototype lives only in `$S\proto`.
