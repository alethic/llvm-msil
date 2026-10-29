; The legacy codegen pipeline of MSILPassConfig (ImplementationPlan.md 5.3,
; recon passes-frame.md 4.2) was fixed in M2 (decision D33); M11 removed the
; second invoke lowering (section 12 rows of 2026-10-20, recon
; m11/eh-passes.md). This test pins the order of the MSIL passes and of the
; generic passes next to them, at -O0 and -O2 on both execution triples:
; - IR: MSILPrepareIR runs first of the target's IR passes, then
;   AddMissingPrototypes, FixFunctionBitcasts and (-O2 only here)
;   OptimizeReturned, then LowerInvoke, UnreachableBlockElim and
;   IndirectBrExpand ahead of the generic IR passes;
; - exceptions: the exception model is always None (decision D8), and
;   MSILPassConfig::addPassesToHandleExceptions adds nothing, so LowerInvoke
;   runs exactly once, in addIRPasses, and UnreachableBlockElim exactly twice:
;   MSIL's, right after LowerInvoke, and the generic one that
;   TargetPassConfig::addIRPasses adds after the GC lowering passes. Neither
;   runs again between the generic IR passes (CodeGenPrepare at -O2) and
;   AtomicExpand (addISelPrepare), where TargetPassConfig used to add them a
;   second time, and no DWARF, SjLj or Windows EH preparation pass is
;   added: TargetPassConfig adds one for each model that the TargetMachine
;   supports, and the MSIL TargetMachine supports none. The MinGW run passes
;   the model that clang's MinGW toolchain sets for x86_64
;   (-exception-model=seh, which is WinEH), even for C: llc turns it into an
;   "exception-model" module flag, which adds no pass either;
; - instruction selection: ISel, then ArgumentMove, SetMemFlags and
;   CleanCodeAfterTrap;
; - no register allocator, no virtual register rewriter, and none of the
;   post-RA passes that MSILPassConfig::addPostRegAlloc disables;
; - pre-emit, after prologue/epilogue insertion and post-RA pseudo expansion:
;   NullifyDebugValueLists, UnreachableMachineBlockElim, ReplacePhysRegs,
;   (-O2 only here) OptimizeLiveIntervals, RegStackify, (-O2 only here)
;   RegColoring, ExplicitLocals and RegNumbering;
; - MSILStackCheck is the last machine pass (addPreEmitPass2, after the stack
;   frame layout analysis), and no other MSIL pass runs between it and the
;   MSIL AsmPrinter.
;
; Every output line that names an MSIL pass, 'Lower invoke and unwind' or
; 'Remove unreachable blocks from the CFG' must be matched
; (--implicit-check-not=MSIL and the two EH strings; the 'Pass Arguments' line
; spells these passes in lower case, -msil-*, -lowerinvoke and
; -unreachableblockelim, which the case-sensitive strings do not match), so
; each MSIL pass runs exactly once, LowerInvoke once and UnreachableBlockElim
; twice, and the -O2-only passes are absent at -O0. The checks are ordered but
; not adjacent: when M3-M9 replace the stub passes, those gain required
; analyses (LiveIntervals, MachineDominatorTree, ...) that the pass manager
; schedules in between. -verify-machineinstrs is left out because it inserts
; machine verifier passes (EXPENSIVE_CHECKS builds insert them anyway, and the
; ordered checks skip them). The module has no function definitions, which
; would reach the M2 lowering stubs.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=MSIL \
; DEFINE:   --implicit-check-not="Register Allocator" \
; DEFINE:   --implicit-check-not="Virtual Register Rewriter" \
; DEFINE:   --implicit-check-not="Machine Late Instructions Cleanup Pass" \
; DEFINE:   --implicit-check-not="Machine Copy Propagation Pass" \
; DEFINE:   --implicit-check-not="PostRA Machine Sink" \
; DEFINE:   --implicit-check-not="Post RA top-down list latency scheduler" \
; DEFINE:   --implicit-check-not="Contiguously Lay Out Funclets" \
; DEFINE:   --implicit-check-not="StackMap Liveness Analysis" \
; DEFINE:   --implicit-check-not="Implement the 'patchable-function' attribute" \
; DEFINE:   --implicit-check-not="Shrink Wrapping analysis" \
; DEFINE:   --implicit-check-not="Remove Loads Into Fake Uses" \
; DEFINE:   --implicit-check-not="Branch Probability Basic Block Placement" \
; DEFINE:   --implicit-check-not="Exception handling preparation" \
; DEFINE:   --implicit-check-not="SJLJ Exception Handling preparation" \
; DEFINE:   --implicit-check-not="Windows exception handling preparation" \
; DEFINE:   --implicit-check-not="Lower invoke and unwind" \
; DEFINE:   --implicit-check-not="Remove unreachable blocks from the CFG"

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -debug-pass=Structure < %s -o /dev/null 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-w64-windows-gnu-msil -exception-model=wineh -O2 -debug-pass=Structure \
; RUN:   < %s -o /dev/null 2>&1 | %{check} --check-prefixes=CHECK,O2

; CHECK-LABEL: Pass Arguments:
; CHECK:       Expand IR instructions
; CHECK:       MSIL Prepare IR
; CHECK:       Add prototypes to prototypes-less functions
; CHECK:       MSIL Fix Function Bitcasts
; O0-NOT:      MSIL Optimize Returned
; O2:          MSIL Optimize Returned
; CHECK:       Lower invoke and unwind, for unwindless code generators
; CHECK:       Remove unreachable blocks from the CFG
; CHECK:       Expand indirectbr instructions
; CHECK:       Shadow Stack GC Lowering
; CHECK:       Remove unreachable blocks from the CFG
; O2:          CodeGen Prepare
; CHECK:       Expand Atomic instructions
; CHECK:       Insert stack protectors
; CHECK:       MSIL Instruction Selection
; CHECK:       MSIL Argument Move
; CHECK:       MSIL Set Memory Flags
; CHECK:       MSIL Clean Code After Trap
; CHECK:       Finalize ISel and expand pseudo-instructions
; CHECK:       Prologue/Epilogue Insertion & Frame Finalization
; CHECK:       Post-RA pseudo instruction expansion pass
; CHECK:       MSIL Nullify DBG_VALUE_LISTs
; CHECK:       Remove unreachable machine basic blocks
; CHECK:       MSIL Replace Physical Registers
; O0-NOT:      MSIL Optimize Live Intervals
; O2:          MSIL Optimize Live Intervals
; CHECK:       MSIL Register Stackify
; O0-NOT:      MSIL Register Coloring
; O2:          MSIL Register Coloring
; CHECK:       MSIL Explicit Locals
; CHECK:       MSIL Register Numbering
; CHECK:       Stack Frame Layout Analysis
; CHECK:       MSIL Stack Check
; CHECK:       MSIL Assembly Printer
; CHECK:       Free MachineFunction

declare i32 @f(i32)
