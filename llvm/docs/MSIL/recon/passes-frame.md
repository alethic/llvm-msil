# MSIL recon: post-ISel pass pipeline, frame lowering, locals and `.maxstack`

- Key: `passes-frame`
- Date: 2026-10-12. Checkout: branch `msil-milestone1` @ `8da03434abcc` (llvm-project, newer than most reference material; every API and line number below was checked against this tree).
- Scope: Design.md sections 2.1, 2.2, 4.2 and 12 (milestone 1). The answers to the six task questions are in sections 3–9. Section 10 lists the experiments that back them.
- Scratch experiments (IL sources and binaries): `C:\Users\JEROME~1\AppData\Local\Temp\claude\D--llvm-msil\fa776849-ce33-4cee-bcc7-05382d063d3e\scratchpad\recon\passes-frame\` (`e1_localloc.il` … `e11_verify.il`).

---

## 0. Summary

1. **Pipeline.** Copy the WebAssembly `addPreEmitPass` skeleton, minus everything that exists for structured control flow, the wasm EH model or wasm encoding. The milestone-1 order after ISel is:
   `ArgumentMove → (SetMemOpFlags, optional) → CleanCodeAfterTrap → [generic regalloc-less pipeline, PEI emits the frame] → NullifyDebugValueLists → UnreachableMachineBlockElim → ReplacePhysRegs → OptimizeLiveIntervals (opt) → RegStackify → RegColoring (opt) → ExplicitLocals → RegNumbering → [addPreEmitPass2] StackCheck (computes .maxstack and verifies CIL stack rules)`.
   Dropped: CFGSort, CFGStackify, FixIrreducibleControlFlow, LateEHPrepare, LowerBrUnless, Peephole, MemIntrinsicResults, DebugFixup, MCLowerPrePass, FixBrTableDefaults, RefTypeMem2Local, ReduceToAnyAllTrue, CoalesceFeaturesAndStripAtomics, LowerEmscriptenEHSjLj and WasmEHPrepare. LowerGlobalDtors is deferred to phase 3. Full table in section 3; exact code in section 4.
2. **RegStackify keeps the stack empty at block boundaries.** The code shows this (it only moves or clones a def into the *same* block as its single use, and an assert checks the stack is empty at each block end). Nothing else in the proposed MSIL pipeline breaks it. **CIL needs one new rule in RegStackify: never stackify the def of `LOCALLOC`.** Verified: localloc with anything under the size operand throws `InvalidProgramException` on both runtimes.
3. **LowerBrUnless is not needed.** CIL has `brfalse`, so `BR_UNLESS` → `brfalse` and `BR_IF` → `brtrue`. **Peephole must not be ported as-is:** its fallthrough-return rewrite relies on CFGStackify's `END_FUNCTION` and on wasm's implicit return. CIL needs an explicit `ret` (verified: falling off the end throws `InvalidProgramException`).
4. **Frame.** Model it as one reserved fake physreg `FP` defined in the prologue by `FP = LOCALLOC (CONST_I32 size)`. `ReplacePhysRegs` turns it into a vreg, and ExplicitLocals gives that vreg a `native int` local. `eliminateFrameIndex` rewrites a frame index as `FP + int32 const`; `FP + 0` becomes `FP` itself. There is no epilogue, because localloc memory dies with the method.
   - **Alignment:** measured 16 bytes on x64 for both .NET Framework 4.8 and .NET 8/10. Over-allocate by `MaxAlign - 16` and align up only when `MaxAlign > 16` (verified working).
   - **Zeroing:** emit `.locals` **without `init`**. C needs no zeroing; the flag zeroes the whole frame on every call (verified). Ilverify/PEVerify will report "initlocals must be set", which is a verifiability complaint, not a stack-shape error.
5. **Measured performance concern with localloc frames.** RyuJIT gives a localloc method an rbp frame, a stack probe and a GS cookie: 73 bytes against 4 bytes for the same code using an address-taken value-type local, on .NET 10. A value-type ("struct") frame local is a drop-in alternative behind the same `FP` abstraction. Design.md 4.2 chose localloc; section 6.10 has the evidence for revisiting that choice.
6. **Arguments stay arguments.** WebAssembly numbers params and locals in one index space (params first). ExplicitLocals maps an `ARGUMENT` vreg straight to its param index and makes no copy. MSIL keeps that numbering and emits `ldarg`/`starg` for index < NumParams, and `ldloc`/`stloc` with index − NumParams otherwise.
7. **`.maxstack`.** A late MachineFunction pass (`MSILStackCheck`, in `addPreEmitPass2`) computes it. After ExplicitLocals, every register operand is a stack slot, so an instruction pops its explicit register uses and pushes its explicit register defs. The pass starts each block at depth 0 and adds a per-opcode TableGen field `StackPeakExtra` for pseudos that expand to several CIL opcodes (e.g. `LOCAL_TEE` → `dup; stloc` peaks at +1). The pass also verifies the CIL invariants and stores the result in `MSILFunctionInfo`; the AsmPrinter prints it. Verified: a `.maxstack` that is too small throws `InvalidProgramException`.

---

## 1. Sources read

| File (this checkout) | What matters |
|---|---|
| `llvm/lib/Target/WebAssembly/WebAssemblyTargetMachine.cpp` | ctor 169-205 (`TrapUnreachable` 184-185, `setEnableTiedFastRegAlloc(false)` 198, no structured CFG 202-204); `WebAssemblyPassConfig` 234-266; `addIRPasses` 295-340; `addISelPrepare` 342-357; `addInstSelector` 359-380; `addOptimizedRegAlloc` 382-393; `addPostRegAlloc` 395-416; `addPreEmitPass` 418-491 |
| `llvm/lib/Target/WebAssembly/WebAssemblyCodeGenPassBuilder.cpp` | New-PM mirror of the same pipeline; disabled passes 67-71; `addPreEmitPass` 247-321 |
| `llvm/lib/Target/WebAssembly/WebAssembly.h`, `WebAssemblyPassRegistry.def` | Every pass has a `...Legacy` wrapper, a `create...LegacyPass()` factory, a new-PM `...Pass` class, and a registry name (`wasm-reg-stackify`, …) |
| `WebAssemblyRegStackify.cpp` (1062 lines) | The algorithm; same-block restriction 920-923; `ARGUMENT` skip 907-908; -O0 restriction 378-382; LIFO and empty-at-block-end assert 1003-1031 |
| `WebAssemblyExplicitLocals.cpp` | ARGUMENT → param index 248-266; local numbering 270-271; TEE → `local.tee` 331-362; `local.set`/`drop` 364-405; `local.get` insertion 407-461; COPY coalescing 463-469; local types 473-485 |
| `WebAssemblyReplacePhysRegs.cpp`, `...OptimizeLiveIntervals.cpp`, `...MemIntrinsicResults.cpp`, `...RegColoring.cpp`, `...RegNumbering.cpp`, `...Peephole.cpp`, `...LowerBrUnless.cpp`, `...CFGSort.cpp`, `...CFGStackify.cpp`, `...FixIrreducibleControlFlow.cpp`, `...FixBrTableDefaults.cpp`, `...ArgumentMove.cpp`, `...SetP2AlignOperands.cpp`, `...CleanCodeAfterTrap.cpp`, `...DebugFixup.cpp`, `...NullifyDebugValueLists.cpp`, `...MCLowerPrePass.cpp`, `...OptimizeReturned.cpp`, `...FixFunctionBitcasts.cpp`, `...AddMissingPrototypes.cpp` | Per-pass analysis in section 3 |
| `WebAssemblyFrameLowering.{h,cpp}`, `WebAssemblyRegisterInfo.{cpp,td}`, `WebAssemblyMachineFunctionInfo.h` | Prologue 274-349; `eliminateFrameIndex` 52-136; `getFrameRegister` 138-150; FP/SP fake regs; `ARGUMENTS`/`VALUE_STACK` |
| `WebAssemblyInstrInfo.cpp` | `analyzeBranch` 99-145 (bails out after CFGStackify, line 108); `insertBranch` 169-193 (`BR_IF`/`BR_UNLESS`) |
| `WebAssemblyInstrInfo.td` 312-397, `WebAssemblyInstrFormats.td` 1-75, `WebAssemblyInstrControl.td` 10-122 | `ARGUMENT`, `COPY`/`TEE`/`LOCAL_*`/`DROP`; every instruction implicitly defines `ARGUMENTS`; `BR_UNLESS` is codegen-only; `BR_TABLE` default operand |
| `WebAssemblyMCInstLower.cpp` 231-397 | `MO_MachineBasicBlock` is `llvm_unreachable` (240-241); register operands removed in `removeRegisterOperands` |
| `WebAssemblyAsmPrinter.cpp` 692-800 | Locals emitted in `emitFunctionBodyStart` |
| `WebAssemblyISelLowering.cpp` | `LowerFormalArguments` 1628-1700; byval and vararg buffers as static stack objects 1398-1410 and 1484-1506; `LowerFRAMEADDR` 2058-2071; `LowerBR_JT` 2192-2216 |
| `llvm/lib/CodeGen/PrologEpilogInserter.cpp` | `insertPrologEpilogCode` (255) runs **before** `replaceFrameIndices` (274-276); `spillCalleeSavedRegs` asserts NoVRegs (655-660) and is skipped when `usesPhysRegsForValues()` is false (239); frame rounding 1113-1160 |
| `llvm/lib/CodeGen/MachineFrameInfo.cpp` 40-56 | Object alignment is clamped to the stack alignment unless `StackRealignable` |
| `llvm/lib/CodeGen/TargetPassConfig.cpp` | `addMachinePasses` 966-1160: `addPreEmitPass` at 1064 and `addPreEmitPass2` at 1158; StackProtector at 818 |
| `clang/lib/CodeGen/Targets/WebAssembly.cpp` 122-123 | The `"no-prototype"` attribute that AddMissingPrototypes relies on |
| `llvm/lib/Transforms/IPO/ExpandVariadics.cpp` 1125-1150 | ABI table keyed on `Triple::getArch()` only |
| `git show bd24404718a6^:llvm/lib/Target/MSIL/MSILWriter.cpp` | The old 2.x backend: one localloc per alloca, `.locals` without `init`, and a guessed `.maxstack` of `2*max(8, max call operands)` (lines 1051-1061, 1250-1298). Not a model to follow for maxstack. |

---

## 2. The WebAssembly pipeline in this checkout

This checkout has both the legacy pipeline (`WebAssemblyPassConfig`) and a new-PM `CodeGenPassBuilder` (`WebAssemblyCodeGenPassBuilder`); they are kept in sync by hand. WebAssembly now also has GlobalISel (`GISel/`, `WebAssemblyGISel.td`) and FastISel. **MSIL milestone 1 should port neither.** Design.md fixes SelectionDAG. With no `createFastISel` override, SelectionDAG also runs at -O0.

Legacy order, from `WebAssemblyTargetMachine.cpp`:

| Hook | Passes, in order (line) |
|---|---|
| `addIRPasses` | AddMissingPrototypes (297), LowerGlobalDtors (300), FixFunctionBitcasts (304), OptimizeReturned [opt] (308), LowerInvoke + UnreachableBlockElim [no EH] (319-322), LowerEmscriptenEHSjLj [if enabled] (330), IndirectBrExpand (333), ReduceToAnyAllTrue (336), base (339) |
| `addISelPrepare` | WasmEHPrepare [wasm EH] (344), RefTypeMem2Local (348), CoalesceFeaturesAndStripAtomics (350), AtomicExpand (354), base (356) |
| `addInstSelector` | ISelDAG (361), ArgumentMove (366), SetP2AlignOperands (370), FixBrTableDefaults (373), CleanCodeAfterTrap (377) |
| `addOptimizedRegAlloc` | disables RegisterCoalescer at -O1 (debug-info quality) (390-391) |
| `addPostRegAlloc` | disables MachineLateInstrsCleanup, MachineCopyPropagation, PostRAMachineSinking, PostRAScheduler, FuncletLayout, StackMapLiveness, PatchableFunction, ShrinkWrap, RemoveLoadsIntoFakeUses, MachineBlockPlacement (401-413) |
| (generic) | PHIElimination, TwoAddress, [opt: LiveIntervals, RegisterCoalescer, MachineScheduler], **PEI** (prologue + frame-index elimination), [opt: BranchFolder, TailDuplicate], ExpandPostRAPseudos, … |
| `addPreEmitPass` | NullifyDebugValueLists (422), UnreachableMachineBlockElim (426), FixIrreducibleControlFlow (429), LateEHPrepare [wasm EH] (434), ReplacePhysRegs (439), OptimizeLiveIntervals [opt] (444), MemIntrinsicResults [opt] (447), **RegStackify** (455), RegColoring [opt] (461), CFGSort (466), CFGStackify (469), **ExplicitLocals** (473), LowerBrUnless (476), Peephole [opt] (480), RegNumbering (483), DebugFixup (487), MCLowerPrePass (490) |

The TargetMachine also sets `usesPhysRegsForValues() == false` (`WebAssemblyTargetMachine.h:63`). PEI relies on this to skip callee-saved spilling, which asserts NoVRegs. **MSIL must set it too.**

---

## 3. Per-pass verdicts for MSIL milestone 1 (Q1)

| WebAssembly pass | What it does | MSIL M1 | Why |
|---|---|---|---|
| **AddMissingPrototypes** (IR) | Gives `"no-prototype"` varargs declarations the type their call sites use | **Keep** (copy, rename) | CIL calls need exact signatures (Design 2.1). Clang only adds `"no-prototype"` in `clang/lib/CodeGen/Targets/WebAssembly.cpp:122-123`, so **the MSIL `TargetCodeGenInfo::setTargetAttributes` must add it too**, otherwise the pass does nothing. |
| **LowerGlobalDtors** (generic IR) | Turns `llvm.global_dtors` into ctors that call `__cxa_atexit` | **Defer** (phase 3) | It references `__cxa_atexit` and `__dso_handle`, which nothing provides in M1. Design 4.1 wants `AppDomain.ProcessExit`. M1: the AsmPrinter reports "not supported yet" for a non-empty `llvm.global_dtors`. Phase 3: either this pass plus a managed `__cxa_atexit` in `MsilC.Runtime`, or direct ProcessExit registration in the module initializer. |
| **FixFunctionBitcasts** (IR) | Wraps call sites whose `FunctionType` differs from the callee's; renames a zero-argument `main` to `__original_main` and creates a wrapper `main(i32, ptr)` | **Keep** (copy) | CIL `call` must match the callee signature exactly. Normalizing `main` is exactly what the generated `Main` → `main(argc, argv)` needs (Design 12). |
| **OptimizeReturned** (IR) [opt] | Uses of an argument marked `returned` become uses of the call result | **Keep** (copy) | Target-independent; more single-use values for RegStackify. |
| LowerInvoke + UnreachableBlockElim | No EH model | **Keep** | No EH in M1. |
| LowerEmscriptenEHSjLj / WasmEHPrepare / LateEHPrepare | Wasm EH and SjLj | **Drop** (SjLj: phase 3) | Design 4.6 starts from the Emscripten SjLj pass, but not in M1. |
| IndirectBrExpand | `indirectbr` → `switch` | **Keep** | CIL cannot take the address of a label. |
| ReduceToAnyAllTrue, RefTypeMem2Local, CoalesceFeaturesAndStripAtomics | Wasm SIMD, wasm reference types, wasm features | **Drop** | Wasm-specific. |
| AtomicExpand (generic) | | **Keep** | A no-op without atomics; atomics arrive in phase 3. |
| **ISelDAG** | | **Keep** (new MSIL ISel) | Not covered here. |
| **ArgumentMove** | Puts `ARGUMENT_*` back at the top of the entry block after the ScheduleDAG | **Keep** (copy) | ExplicitLocals (lines 249-266) and the prologue insertion point both assume contiguous `ARGUMENT`s at the top. The same TableGen trick is needed: every instruction `Defs = [ARGUMENTS]` (`WebAssemblyInstrFormats.td:39`), and `ARGUMENT` `Uses = [ARGUMENTS]` (`WebAssemblyInstrInfo.td:313-314`). |
| **SetP2AlignOperands** | Copies memoperand alignment into a `p2align` immediate right after ISel | **Modify** (optional in M1): `MSILSetMemOpFlags` | CIL's equivalent is the `unaligned. N` and `volatile.` prefixes. Capture them right after ISel, as wasm does, because later passes can drop memoperands. x64 and ARM64 JITs tolerate unaligned access, so M1 can stub it. `volatile.` matters for C `volatile`. |
| **FixBrTableDefaults** | Removes the range check before a jump table and installs the real default | **Defer** (phase 4 optimization) | Keep LLVM's range check in M1. Print `BR_TABLE` as `switch (…)` followed by `br <default>`, where default is the first-case MBB, as wasm's `LowerBR_JT` does (2211-2214). This is never taken after the range check. Note: on x64, CIL `switch` compares a 64-bit index at full width (2^32+1 goes to the default; verified), so a ported version could also drop i64 range checks. PEVerify rejects an `int64` operand but accepts `native int`, so emit `conv.i` before `switch` for i64 indices. |
| **CleanCodeAfterTrap** | Deletes instructions after a terminator trap | **Keep** (copy) | `TRAP` lowers to a throwing terminator (e.g. `ldnull; throw`). |
| RegisterCoalescer -O1 disable | Debug-info quality | **Drop the override** | M1 emits no debug info. Later, revisit together with Portable PDB. |
| `addPostRegAlloc` disables | Passes that require NoVRegs, plus MachineBlockPlacement | **Keep, same list** | The same NoVRegs constraints apply. Keeping MachineBlockPlacement off keeps layout close to IR order; RyuJIT does its own layout, so revisit in phase 4. |
| **NullifyDebugValueLists** | Sets `DBG_VALUE_LIST` undef | **Keep** (copy) | The DebugValueManager used by RegStackify and ExplicitLocals cannot handle lists. |
| UnreachableMachineBlockElim | | **Keep** | Cheap, keeps the IL clean (RyuJIT imports only reachable code anyway). |
| **FixIrreducibleControlFlow** | Gives multi-entry loops a single entry | **Drop** | CIL allows any CFG. Verified: an irreducible loop entered at two points runs on both runtimes (`e2` case f). |
| **ReplacePhysRegs** | Replaces fake `SP`/`FP` physregs with one vreg each after PEI | **Keep** (copy) | The MSIL `FP` (section 6) uses the same mechanism. |
| **OptimizeLiveIntervals** [opt] | Splits multi-component live intervals into separate vregs; `leaveSSA()` | **Keep** (copy) | Feeds RegStackify's `hasSingleUse` and RegColoring. Its IMPLICIT_DEF cleanup (lines 109-120) only matters after FixIrreducibleControlFlow and is harmless. |
| **MemIntrinsicResults** [opt] | Replaces dominated uses of `dst` with the `memcpy`/`memset` result | **Drop** | It only fires on ISel-generated *libcalls* (`Op1.isSymbol()`, line 174). MSIL lowers memcpy/memset to `cpblk`/`initblk`, which return nothing. Re-evaluate if those become libcalls. |
| **RegStackify** | Builds expression trees on the operand stack | **Keep, one CIL rule added** | See section 5. Rename wasm opcodes in `query()` (188-264), `convertImplicitDefToConstZero` (105-135) and `getTeeOpcode` (524-542). Remove the `__stack_pointer`/`__wasm_get_stack_pointer` checks (250-259). Add: never stackify a `LOCALLOC` def. |
| **RegColoring** [opt] | Merges non-overlapping vregs of the same class to reduce the number of locals | **Keep** (copy) | Fewer locals helps RyuJIT, which tracks a limited number of locals. See the narrow-parameter hazard in section 7. |
| **CFGSort** | Topological block order with contiguous loops, needed only for BLOCK/LOOP markers | **Drop** | Exists only for CFGStackify. |
| **CFGStackify** | Inserts BLOCK/LOOP/TRY markers, turns MBB operands into depth immediates, adds `END_FUNCTION`, sets `isCFGStackified` | **Drop** | Design 2.2. Section 5.3 lists the assumptions that disappear with it. |
| **ExplicitLocals** | Materializes `local.get`/`local.set`/`local.tee`/`drop`; assigns locals | **Keep, modified** | Split argument and local index spaces (`ldarg`/`starg` vs `ldloc`/`stloc`); `LOCAL_TEE` prints `dup; stloc`; `DROP` prints `pop`. See section 7. |
| **LowerBrUnless** | Rewrites `br_unless` as an inverted compare plus `br_if` | **Drop** | CIL has `brfalse`. Section 5.4. |
| **Peephole** [opt] | (a) `memcpy` result → drop; (b) final `RETURN` → `FALLTHROUGH_RETURN` | **Drop** | (a) is the same libcall-only case as MemIntrinsicResults. (b) is *wrong* for CIL, which has no implicit return at method end (verified: InvalidProgramException); it also asserts `END_FUNCTION` (line 93), which only CFGStackify adds. A future CIL peephole (`stloc x; ldloc x` → `dup; stloc x`, compare+branch fusion) is a separate pass. |
| **RegNumbering** | vreg → wasm register number, used when printing in register form | **Keep, optional** | Only needed for the `-msil-keep-registers` / `-msil-disable-explicit-locals` debugging output used in lit tests. With explicit locals, register operands are dropped at MC lowering. |
| **DebugFixup** | Points `DBG_VALUE`s of stackified vregs at `TI_OPERAND_STACK` | **Drop** (until Portable PDB) | DWARF-only. Set `MCAsmInfo::SupportsDebugInformation = false` in M1 so no `DwarfDebug` is created. |
| **MCLowerPrePass** | Gathers external symbols before the AsmPrinter runs, for `.functype` declarations | **Drop** | ILAsm resolves forward references: a call to a method defined later in the file assembles and runs (verified, `e7`). The AsmPrinter can collect P/Invoke targets as it goes and emit them in `emitEndOfAsmFile`. |
| GlobalISel hooks, FastISel | | **Don't port** | Design 2: SelectionDAG. |
| ExpandVariadics (not in the wasm pipeline) | | Phase 3 | `VariadicABIInfo::create` (`ExpandVariadics.cpp:1125-1150`) is keyed on the arch only. MSIL needs a case keyed on the object format. Note that wasm's own `LowerCall` allocates the vararg buffer as a *static* stack object in the caller (1484-1506), so in MSIL it lands in the localloc frame with no shadow stack. |

---

## 4. Proposed MSIL pass pipeline (exact order)

### 4.1 TargetMachine settings (copy from WebAssembly)

```cpp
MSILTargetMachine::MSILTargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     std::optional<Reloc::Model> RM,
                                     std::optional<CodeModel::Model> CM,
                                     CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT, CPU, FS, Options,
                               RM.value_or(Reloc::Static),
                               getEffectiveCodeModel(CM, CodeModel::Large), OL),
      TLOF(new MSILTargetObjectFile()) {
  // Every path must end in ret/br/throw: CIL rejects falling off the end of a
  // method (verified: InvalidProgramException at JIT time; ilasm accepts it
  // silently). 'unreachable' -> TRAP, also after noreturn calls.
  this->Options.TrapUnreachable = true;
  this->Options.NoTrapAfterNoreturn = false;
  initAsmInfo();
  // No register allocation; tied operands are lowered by TwoAddress.
  setEnableTiedFastRegAlloc(false);
  // Deliberately NOT setRequiresStructuredCFG(true): CIL accepts any CFG.
}
// In MSILTargetMachine.h:
bool usesPhysRegsForValues() const override { return false; } // PEI skips CSR spilling
```

(The ctor signature matches `CodeGenTargetMachineImpl.h:26-29` in this checkout. There is no DataLayout parameter.)

### 4.2 Legacy `MSILPassConfig`

```cpp
namespace {
class MSILPassConfig final : public TargetPassConfig {
public:
  MSILPassConfig(MSILTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}
  MSILTargetMachine &getMSILTargetMachine() const {
    return getTM<MSILTargetMachine>();
  }

  FunctionPass *createTargetRegisterAllocator(bool) override { return nullptr; }
  bool addRegAssignAndRewriteFast() override { return false; }      // no RA
  bool addRegAssignAndRewriteOptimized() override { return false; } // no RA
  bool addGCPasses() override { return false; }

  void addIRPasses() override;
  void addISelPrepare() override;
  bool addInstSelector() override;
  void addPostRegAlloc() override;
  void addPreEmitPass() override;
  void addPreEmitPass2() override;
};
} // namespace

void MSILPassConfig::addIRPasses() {
  addPass(createMSILAddMissingPrototypesLegacyPass());
  // llvm.global_dtors: not lowered in M1 (diagnosed by MSILAsmPrinter).
  addPass(createMSILFixFunctionBitcastsLegacyPass());
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createMSILOptimizeReturnedLegacyPass());
  // M1 has no exception model.
  addPass(createLowerInvokePass());
  addPass(createUnreachableBlockEliminationPass());
  addPass(createIndirectBrExpandPass(getOptLevel()));
  TargetPassConfig::addIRPasses();
}

void MSILPassConfig::addISelPrepare() {
  addPass(createAtomicExpandLegacyPass()); // no-op without atomics
  TargetPassConfig::addISelPrepare();      // SafeStack/StackProtector: see 6.11
}

bool MSILPassConfig::addInstSelector() {
  (void)TargetPassConfig::addInstSelector();
  addPass(createMSILISelDagLegacyPass(getMSILTargetMachine(), getOptLevel()));
  addPass(createMSILArgumentMoveLegacyPass());      // keep ARGUMENT_* on top
  addPass(createMSILSetMemOpFlagsLegacyPass());     // optional in M1
  addPass(createMSILCleanCodeAfterTrapLegacyPass());
  return false;
}

void MSILPassConfig::addPostRegAlloc() {
  // Same list as WebAssemblyTargetMachine.cpp:401-413 (NoVRegs passes).
  disablePass(&MachineLateInstrsCleanupID);
  disablePass(&MachineCopyPropagationID);
  disablePass(&PostRAMachineSinkingID);
  disablePass(&PostRASchedulerID);
  disablePass(&FuncletLayoutID);
  disablePass(&StackMapLivenessID);
  disablePass(&PatchableFunctionID);
  disablePass(&ShrinkWrapID);
  disablePass(&RemoveLoadsIntoFakeUsesID);
  disablePass(&MachineBlockPlacementID);
  TargetPassConfig::addPostRegAlloc();
}

void MSILPassConfig::addPreEmitPass() {
  TargetPassConfig::addPreEmitPass();
  addPass(createMSILNullifyDebugValueListsLegacyPass());
  addPass(&UnreachableMachineBlockElimID);
  // PEI has run: the prologue's LOCALLOC defines the FP physreg and frame
  // indices are FP+offset. Turn FP into an ordinary vreg.
  addPass(createMSILReplacePhysRegsLegacyPass());
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createMSILOptimizeLiveIntervalsLegacyPass());
  addPass(createMSILRegStackifyLegacyPass(getOptLevel()));
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createMSILRegColoringLegacyPass());
  if (!MSIL::DisableExplicitLocals)            // lit-test-only escape hatch
    addPass(createMSILExplicitLocalsLegacyPass());
  addPass(createMSILRegNumberingLegacyPass());  // only for keep-registers output
}

void MSILPassConfig::addPreEmitPass2() {
  // Last machine pass (TargetPassConfig.cpp:1158): nothing can add real
  // instructions after it. Computes .maxstack, verifies the CIL stack rules.
  if (!MSIL::DisableExplicitLocals)
    addPass(createMSILStackCheckLegacyPass());
}
```

### 4.3 New-PM `MSILCodeGenPassBuilder`

Mirror the above the way `WebAssemblyCodeGenPassBuilder.cpp` does:
- `disablePass<…>()` in the ctor, the same list as 4.2.
- `addPreEmitPass(PMW)` with `addMachineFunctionPass(MSILReplacePhysRegsPass(), PMW)` and the rest, in the same order.
- `addPreEmitPass2(PMW)` with `MSILStackCheckPass`. The hook exists at `CodeGenPassBuilder.h:246`.
- Register names in `MSILPassRegistry.def`: `msil-reg-stackify`, `msil-explicit-locals`, `msil-stack-check`, …

M1 may ship the legacy pipeline only, since clang still drives codegen through the legacy PM by default. Keep the `.def` registry so `llc -run-pass=` works for MIR tests.

---

## 5. Operand-stack discipline without CFGStackify (Q2)

### 5.1 RegStackify never leaves a value on the stack across a block boundary (code evidence)

- **Moves are same-block only.** `bool SameBlock = DefI->getParent() == &MBB; bool CanMove = SameBlock && isSafeToMove(...) && !TreeWalker.isOnStack(Reg);` (`WebAssemblyRegStackify.cpp:920-923`). Both `moveForSingleUse` and `moveAndTeeForMultiUse` require `CanMove`.
- **Rematerialization clones into the user's block.** `rematerializeCheapDef` does `DefDIs.cloneSink(&*Insert, NewReg)` (617), so the clone sits immediately before the use.
- **-O0** only accepts a def already adjacent to its use (`isSafeToMove` 370-382), which is necessarily in the same block.
- **Trees are contiguous.** Each stackified def is moved to immediately before the current `Insert`, which then becomes the new `Insert` (925, 937, 942). No unrelated instruction can end up between a tree's leaves and its root. So at the start of every tree root (an instruction whose def is not stackified, or has no def) the stack is empty.
- **Terminators are roots.** `BR_IF`, `BR_TABLE` and `RETURN` consume their stackified operands within the block.
- **Assert-builds check it.** Lines 1003-1031 simulate push/pop in LIFO order and assert `Stack.empty()` at the end of every block.
- PHIs have been eliminated before RegStackify, so values only cross blocks in vregs, which become locals.

ExplicitLocals keeps the invariant. It inserts `local.set` (→ `stloc`/`starg`) or `drop` (→ `pop`) right after every non-stackified def (364-405), and `local.get` right before the root or before the start of a stackified operand's tree (407-461, `findStartOfTree` 196-219). After ExplicitLocals, *every* register operand is a stack slot (assert at 487-501).

Passes that run after RegStackify in the MSIL pipeline:

| Pass | Effect on the stack |
|---|---|
| RegColoring | Renames only non-stackified vregs (line 244 skips stackified ones). |
| ExplicitLocals | Maintains the invariant, as above. |
| RegNumbering | Bookkeeping only. |
| StackCheck | Verifies; may append a `TRAP` to a last block that falls off the end (5.6). |
| Generic passes after `addPreEmitPass` | FuncletLayout, RemoveLoadsIntoFakeUses and StackMapLiveness are disabled. LiveDebugValues only inserts `DBG_VALUE`s. The MachineOutliner must stay off: it is not enabled by default for this target, and nothing should enable it. |

### 5.2 CIL rules confirmed empirically (both runtimes; section 10)

- A non-empty stack at a forward join, and even at a backward-branch target, is *accepted* by RyuJIT (`e2` d/e). We never rely on this: our stack is empty at every block boundary, which trivially satisfies ECMA-335 III.1.7.5.
- `ret` with anything beyond the return value → `InvalidProgramException`.
- `localloc` with anything under the size → `InvalidProgramException`.
- `.maxstack` too small → `InvalidProgramException`.
- Falling off the end of a method → `InvalidProgramException` at JIT time, while ilasm accepts it silently.

### 5.3 WebAssembly assumptions that break without CFGStackify

1. **MBB operands.** `WebAssemblyMCInstLower.cpp:239-241` treats `MO_MachineBasicBlock` as unreachable, because CFGStackify rewrote branch targets to depth immediates (`rewriteDepthImmediates`, CFGStackify 2595-2664). **MSIL MCInstLower must lower `MO_MachineBasicBlock` to `MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx)`.** The generic `AsmPrinter::emitBasicBlockStart` then prints labels for every block that is not reached only by fallthrough. Set `MCAsmInfo::PrivateLabelPrefix`/`PrivateGlobalPrefix` to something without a `.` (e.g. `"L"`), so `LBB0_3:` is a valid ILAsm label. ELF's `.L` is not.
2. **`analyzeBranch` stops working after CFGStackify** (`WebAssemblyInstrInfo.cpp:108`, `isCFGStackified()`). MSIL never sets that flag, so branches stay analyzable through emission, which the AsmPrinter's fallthrough logic needs. Drop the `CFGStackified` field and its YAML key.
3. **`END_FUNCTION` and implicit fallthrough-return.** CFGStackify's `appendEndToFunction` (2439-2444) and Peephole's `FALLTHROUGH_RETURN` (81-116) model wasm's implicit return at function end. **CIL has none:** every method needs an explicit `ret`, `br` or `throw` as its last instruction. Drop both. `TrapUnreachable` plus `NoTrapAfterNoreturn = false` (4.1) and the StackCheck safety net (5.6) guarantee it.
4. **`fixEndsAtEndOfFunction`** (CFGStackify 2381-2435) types wasm blocks that end at the function end. CIL has no equivalent. Drop.
5. **Block markers vs. expression trees.** CFGStackify places BLOCK markers outside expression trees ("Local expression tree should go after the BLOCK", 386-395). Without CFGStackify, nothing is ever inserted inside a tree. Good.
6. **Layout.** Wasm needs CFGSort's topological order. CIL is a linear instruction stream where fallthrough means "next instruction", so LLVM's MIR layout plus fallthrough maps 1:1. Layout-only fallthrough, such as a `BR_IF` followed by fallthrough to the next MBB, is valid IL as-is.
7. **`OptimizeLiveIntervals`'s IMPLICIT_DEF cleanup** assumes FixIrreducibleControlFlow ran. It is harmless without it.
8. **`switch` vs `br_table`.** Wasm `br_table` carries its default target. CIL `switch` *falls through* when out of range. Print `BR_TABLE_*` as `[conv.i;] switch (L1, …, Ln)` followed by `br Ldefault`, keeping the barrier semantics and `isBarrier = 1` from `WebAssemblyInstrControl.td:48`. Verified on x64: `switch` takes `int32` or `native int` at full width, and PEVerify flags `int64` (`e2` g/h, `e11`).

### 5.4 Is LowerBrUnless needed? No.

`BR_UNLESS` exists only because wasm lacks it (`WebAssemblyLowerBrUnless.cpp:10-13`). CIL has `brtrue` and `brfalse`, so:
- Keep both `BR_IF` (→ `brtrue`) and `BR_UNLESS` (→ `brfalse`) as real, encodable MSIL instructions. Drop `isCodeGenOnly` from `BR_UNLESS`.
- Keep `analyzeBranch`/`insertBranch`/`reverseBranchCondition` unchanged (`WebAssemblyInstrInfo.cpp:99-200`). The `Cond = {Imm(true|false), Reg}` encoding already flips between the two.
- Keep the selection patterns `brcond (setne c, 0)` → `BR_IF` and `brcond (seteq c, 0)` → `BR_UNLESS` (`WebAssemblyInstrControl.td:29-34`).

Fused compare-and-branch (`beq`, `blt`, `bge.un`, …) is a later optimization, as ISel patterns plus a 3-operand `Cond` in `analyzeBranch`. Care is needed for floats: the inverse of `blt` is `bge.un`. It is not needed for M1.

### 5.5 CIL constraints RegStackify must learn

**Milestone 1: `LOCALLOC`.** ECMA-335 III.3.47 says localloc needs an evaluation stack that is empty apart from the size; verified (`e2` a). If RegStackify nests a `LOCALLOC` inside another tree, other operands can sit beneath it. Example: `STORE addr=%y, val=LOCALLOC(...)` evaluates `%y` first. Rule: **never stackify a LOCALLOC def.** It then always stays a tree root, so the stack beneath it is empty, and only its size subtree is pushed:

```cpp
// MSILRegStackify.cpp, in the operand loop, next to the ARGUMENT skip
// (WebAssemblyRegStackify.cpp:905-908):
        // Argument instructions represent live-in registers and not real
        // instructions.
        if (MSIL::isArgument(DefI->getOpcode()))
          continue;

        // CIL: localloc requires the evaluation stack to hold nothing but its
        // size operand (ECMA-335 III.3.47; violating it throws
        // InvalidProgramException). Keep every LOCALLOC a tree root by never
        // nesting its result into a user; the result goes through a local.
        if (DefI->getOpcode() == MSIL::LOCALLOC)
          continue;
```

`LOCALLOC` must also be `hasSideEffects = 1` and must not be `isReMaterializable` or `isAsCheapAsAMove`. The skip also covers the -O0 adjacency path and the tee path, because both go through the same `DefI`.

**Later phases:**
- `tail.` calls: the stack must hold only the arguments, and `ret` must follow.
- `jmp`.
- EH: `leave` empties the stack; a catch handler starts with the exception object on the stack.

Each will need a similar "root only" rule when introduced.

### 5.6 Other RegStackify renames (mechanical)

| Location | Change |
|---|---|
| `query()` (172-265) | Replace the trapping-opcode list (DIV/REM/TRUNC) with the MSIL equivalents (`div`, `rem`, `conv.ovf.*` if ever used). Remove the `GLOBAL_SET … __stack_pointer` and `__wasm_get_stack_pointer` cases. |
| `convertImplicitDefToConstZero` (105-135) | `CONST_I32/I64/F32/F64` → `ldc.*` opcodes; add the pointer class if ISel has one. |
| `getTeeOpcode` (524-542) | MSIL register classes. |
| `hasSingleUse` frame-base debug special case (300-312) | Can stay (it compiles against `MSILFunctionInfo::isFrameBaseVirtual`), or be removed: with the LOCALLOC rule the frame base is never stackified anyway. |
| `WebAssemblyDebugValueManager` | Copy it as `MSILDebugValueManager`. RegStackify (`sink`, `cloneSink`, `updateReg`) and ExplicitLocals (`replaceWithLocal`) call it. |

---

## 6. Frame lowering (Q3)

### 6.1 Model

- **One static frame per function**, allocated by a single `localloc` at entry (Design 4.2). Memory is released automatically at return, so there is **no epilogue** and nothing to restore.
- **The frame base is a fake reserved physreg `MSIL::FP`** in the pointer register class, used the same way as wasm's `FP32`/`FP64`:
  - ISel (`LowerFRAMEADDR`) and PEI (`eliminateFrameIndex`) refer to the physreg.
  - `emitPrologue` defines it: `FP = LOCALLOC (CONST_I32 size)`.
  - After PEI, `ReplacePhysRegs` (`WebAssemblyReplacePhysRegs.cpp:76-104`) replaces all explicit `FP` operands with one vreg and records it as `FrameBaseVreg`.
  - The LOCALLOC rule keeps that vreg out of the stack, so ExplicitLocals gives it a typed local: `ldc.i4 N; localloc; stloc.k`.
  - PEI calls `emitPrologue` (`insertPrologEpilogCode`, line 255) **before** `replaceFrameIndices` (274-276), so the order is safe.
- **No SP, no shadow stack in M1.** Calls pass arguments on the CIL evaluation stack. Byval copies and vararg buffers are static stack objects in the caller (`WebAssemblyISelLowering.cpp:1398-1410` and `1484-1506`), so they live in the localloc frame. `CALLSEQ_START(0,0)` means MaxCallFrameSize is 0.

### 6.2 TableGen and registers

```tablegen
// MSILRegisterInfo.td
def FP : MSILReg<"%FP">;                  // frame base, replaced by a vreg after PEI
def VALUE_STACK : MSILReg<"STACK">;       // RegStackify ordering token (as wasm)
def ARGUMENTS   : MSILReg<"ARGUMENTS">;   // pins ARGUMENT_* at entry (as wasm)
def PTR : MSILRegClass<[iPTR-as-legal-VT], 64, (add FP, PTR_0)>; // or I64 if ISel
                                                         // keeps pointers as i64

// MSILInstrInfo.td
let hasSideEffects = 1, isCodeGenOnly = 0 in
defm LOCALLOC : I<(outs PTR:$dst), (ins I32:$size), (outs), (ins),
                  [], "localloc\t$dst, $size", "localloc", 0xFE0F>;
```

`MSILRegisterInfo::getReservedRegs` reserves `FP` (wasm reserves SP/FP, `WebAssemblyRegisterInfo.cpp:43-50`). `getFrameRegister` returns `MFI->getFrameBaseVreg()` once virtual, else `MSIL::FP`, like wasm (138-150).

### 6.3 `MSILFrameLowering`

```cpp
// MSILFrameLowering.h
class MSILFrameLowering final : public TargetFrameLowering {
public:
  MSILFrameLowering()
      : TargetFrameLowering(StackGrowsDown, /*StackAlignment=*/Align(16),
                            /*LocalAreaOffset=*/0,
                            /*TransientStackAlignment=*/Align(16),
                            /*StackRealignable=*/true) {}
  // StackRealignable must be true: otherwise MachineFrameInfo clamps object
  // alignment to 16 (MachineFrameInfo.cpp:40-56) and _Alignas(32) is lost. We
  // can always realign because we over-allocate (below).

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &, MachineBasicBlock &) const override {}
  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override {
    return MBB.erase(I); // ADJCALLSTACK*: no memory stack traffic for calls
  }
  bool hasReservedCallFrame(const MachineFunction &) const override {
    return true;
  }

  /// Alignment the backend assumes for localloc results. ECMA-335 guarantees
  /// nothing; measured 16 on x64 for .NET Framework 4.8 and .NET 8 (e1/e9).
  /// Make it a subtarget property (unmeasured: ARM64; i686 should assume 4).
  static Align getLocallocAlign(const MachineFunction &MF);

  static bool needsFrame(const MachineFunction &MF) {
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    return MFI.getStackSize() != 0 || MFI.isFrameAddressTaken();
  }

protected:
  bool hasFPImpl(const MachineFunction &MF) const override {
    return needsFrame(MF);
  }
};
```

```cpp
// MSILFrameLowering.cpp
void MSILFrameLowering::emitPrologue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  assert(MFI.getCalleeSavedInfo().empty() && "no callee-saved registers");
  assert(MFI.getObjectIndexBegin() == 0 && "MSIL has no fixed stack objects");
  if (!needsFrame(MF))
    return;

  const auto &ST = MF.getSubtarget<MSILSubtarget>();
  const MSILInstrInfo &TII = *ST.getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  // After the ARGUMENT_* pseudos, which ExplicitLocals expects first.
  auto InsertPt = MBB.begin();
  while (InsertPt != MBB.end() && MSIL::isArgument(InsertPt->getOpcode()))
    ++InsertPt;
  DebugLoc DL;

  const Align LocA = getLocallocAlign(MF);
  const Align MaxA = MFI.getMaxAlign();
  const bool Realign = MaxA > LocA;
  uint64_t Bytes = std::max<uint64_t>(MFI.getStackSize(), 1);
  if (Realign)
    Bytes += MaxA.value() - LocA.value();     // room to align up
  if (Bytes > uint64_t(INT32_MAX))
    reportFatalUsageError("MSIL: stack frame of '" + MF.getName() +
                          "' exceeds 2 GiB");

  // ldc.i4 <Bytes>; localloc   (size may be int32: "native unsigned int or
  // unsigned int32", ECMA-335 III.3.47)
  Register Size = MRI.createVirtualRegister(&MSIL::I32RegClass);
  BuildMI(MBB, InsertPt, DL, TII.get(MSIL::CONST_I32), Size)
      .addImm(int64_t(Bytes))
      .setMIFlag(MachineInstr::FrameSetup);
  Register Raw = Realign ? MRI.createVirtualRegister(&MSIL::PTRRegClass)
                         : Register(MSIL::FP);
  BuildMI(MBB, InsertPt, DL, TII.get(MSIL::LOCALLOC), Raw)
      .addReg(Size)
      .setMIFlag(MachineInstr::FrameSetup);

  if (Realign) {
    // FP = (Raw + (A-1)) & -A. int32 constants combine with native int without
    // conversions and keep the upper pointer bits (verified e9, e11).
    Register C1 = MRI.createVirtualRegister(&MSIL::I32RegClass);
    Register Sum = MRI.createVirtualRegister(&MSIL::PTRRegClass);
    Register C2 = MRI.createVirtualRegister(&MSIL::I32RegClass);
    BuildMI(MBB, InsertPt, DL, TII.get(MSIL::CONST_I32), C1)
        .addImm(int64_t(MaxA.value() - 1));
    BuildMI(MBB, InsertPt, DL, TII.get(MSIL::ADD_PTR_I32), Sum)
        .addReg(Raw).addReg(C1);
    BuildMI(MBB, InsertPt, DL, TII.get(MSIL::CONST_I32), C2)
        .addImm(-int64_t(MaxA.value()));
    BuildMI(MBB, InsertPt, DL, TII.get(MSIL::AND_PTR_I32), MSIL::FP)
        .addReg(Sum).addReg(C2);
  }
}
```

`ADD_PTR_I32` and `AND_PTR_I32` are placeholders for whatever the ISel agent defines for "native int op int32 → native int" (CIL `add`, `and`).

IL produced for a normal frame at -O0 and -O2:

```
ldc.i4   N
localloc
stloc    k        // k = FP's local, typed native int
```

For an over-aligned frame (verified shape, `e9_prologue.il`):

```
ldc.i4 N+A-16; localloc; stloc r; ldloc r; ldc.i4 A-1; add; ldc.i4 -A; and; stloc k
```

### 6.4 `eliminateFrameIndex`

With `StackGrowsDown`, PEI gives object offsets in `[-StackSize, 0)` and rounds `StackSize` to `max(StackAlign, MaxAlign)` (`PrologEpilogInserter.cpp:1113-1160`). FP is the lowest address of the frame, so an object lives at `FP + StackSize + ObjectOffset`, the same formula as `WebAssemblyRegisterInfo.cpp:63`. When FP is MaxAlign-aligned, every object is aligned.

```cpp
bool MSILRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                           int SPAdj, unsigned FIOperandNum,
                                           RegScavenger * /*RS*/) const {
  assert(SPAdj == 0);
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const auto &TII = *MF.getSubtarget<MSILSubtarget>().getInstrInfo();

  int FI = MI.getOperand(FIOperandNum).getIndex();
  assert(FI >= 0 && MFI.getObjectSize(FI) != 0 &&
         "no fixed objects; var-sized objects don't use frame indices");
  int64_t Offset = int64_t(MFI.getStackSize()) + MFI.getObjectOffset(FI);
  assert(Offset >= 0 && Offset <= INT32_MAX);
  Register FP = getFrameRegister(MF); // MSIL::FP until ReplacePhysRegs

  // (a) FI + CONST_I32 with a single-use constant: fold into the constant
  //     (same idea as WebAssemblyRegisterInfo.cpp:88-112).
  if (MI.getOpcode() == MSIL::ADD_PTR_I32 && FIOperandNum == 1) {
    MachineOperand &OtherMO = MI.getOperand(2);
    if (OtherMO.isReg() && OtherMO.getReg().isVirtual())
      if (MachineInstr *Def = MRI.getUniqueVRegDef(OtherMO.getReg()))
        if (Def->getOpcode() == MSIL::CONST_I32 &&
            MRI.hasOneNonDBGUse(Def->getOperand(0).getReg()) &&
            Def->getOperand(1).isImm() &&
            isInt<32>(Def->getOperand(1).getImm() + Offset)) {
          Def->getOperand(1).setImm(Def->getOperand(1).getImm() + Offset);
          MI.getOperand(FIOperandNum).ChangeToRegister(FP, /*isDef=*/false);
          return false;
        }
  }

  // (b) Offset 0: the frame base itself.
  if (Offset == 0) {
    MI.getOperand(FIOperandNum).ChangeToRegister(FP, /*isDef=*/false);
    return false;
  }

  // (c) General: %addr = ADD_PTR_I32 FP, (CONST_I32 Offset)
  //     -> "ldloc fp; ldc.i4 off; add" once stackified into MI's tree.
  Register Off = MRI.createVirtualRegister(&MSIL::I32RegClass);
  BuildMI(MBB, II, MI.getDebugLoc(), TII.get(MSIL::CONST_I32), Off)
      .addImm(Offset);
  Register Addr = MRI.createVirtualRegister(&MSIL::PTRRegClass);
  BuildMI(MBB, II, MI.getDebugLoc(), TII.get(MSIL::ADD_PTR_I32), Addr)
      .addReg(FP)
      .addReg(Off);
  MI.getOperand(FIOperandNum).ChangeToRegister(Addr, /*isDef=*/false);
  return false;
}
```

Notes:
- Unlike wasm, MSIL loads and stores should have **no offset immediate** (wasm folds FI offsets into the load/store `off` operand, lines 70-86). A *load* offset could be printed as `ldc.i4 off; add; ldind.*` (peak +1, see 8.2), but a *store* cannot. The address must be complete before the value is pushed, and the store's own text is printed after the value's tree. Recommend plain address operands everywhere in M1.
- Use int32 offsets (`ldc.i4`), not `ldc.i8`. PEVerify reports `native int + int64` as an *Unexpected type on the stack* error, while `native int op int32` is clean (`e11`). This is what keeps the frame code free of stack-shape errors (Design 12 exit criterion).
- `ReplacePhysRegs` must run after PEI. It does (`addPreEmitPass`).

### 6.5 Alignment

- Measured on x64: `localloc` results are 16-byte aligned for sizes 1, 7, 24, 100, 1000 on .NET Framework 4.8 and .NET 8 (`e1`). The disassembly shows the JIT rounds the size to 16 and keeps RSP 16-aligned (`e5`).
- ECMA-335 does not promise any alignment, so make the assumed value a subtarget property: `getLocallocAlign()`, 16 for x86_64 and aarch64, conservatively 4 for the later i686 triples. Add a `-msil-assume-localloc-align=` option for testing.
- `MaxAlign <= 16`: no fixup. `MaxAlign > 16` (e.g. `_Alignas(32)`, AVX vectors): over-allocate `MaxAlign - 16` bytes and align up (6.3). Verified the result is 32-aligned and the upper pointer bits survive `and` with an int32 mask (`e9`).

### 6.6 Zeroing (`localsinit`)

- Without `init` on `.locals`, `localloc` memory is **not** zeroed on either runtime (`e1`: 1000/1000 dirty bytes). With `init`, it is zeroed.
- C does not need zeroing: automatic objects without an initializer are indeterminate, and initializers lower to explicit stores or `memset`. **Never emit `.locals init`.** Zeroing the whole frame on every call would be a pure cost.
- Consequence for CI: ilverify (and PEVerify, `e2`/`e8`) report "initlocals must be set for verifiable methods with one or more local variables". This is a verifiability diagnostic, not a stack-shape error. The milestone-1 ilverify filter must allow it, together with "Expected ByRef on the stack" (any `ldind`/`stind` through `native int`) and "Instruction cannot be verified" (`localloc`).
- `IMPLICIT_DEF` values that ExplicitLocals leaves as never-written locals read garbage. That is fine, because they are undef.

### 6.7 Interaction with RegStackify and ExplicitLocals

- **LOCALLOC's def is never stackified** (5.5). FP (or `Raw`) therefore always gets a local, and LOCALLOC stays a tree root with only `ldc.i4 N` beneath it. `CONST_I32 Size` is adjacent and single-use, so RegStackify nests it at every opt level.
- **FP has many uses** (one per frame access). With the LOCALLOC skip, `moveAndTeeForMultiUse` and `rematerializeCheapDef` never touch it. Each use becomes `ldloc k`.
- **The prologue is inserted after the `ARGUMENT_*`s.** ExplicitLocals erases those, so the localloc is the first real IL in the method.
- **Optional verification hook:** `MSILStackCheck` (8.3) asserts that LOCALLOC executes at depth 1.

### 6.8 Dynamic allocas and `stacksave`/`stackrestore`

Design 4.2 calls for a per-thread shadow stack, which needs the runtime assembly (phase 3). For M1 there is a trivial and correct-but-wasteful fallback (recommended):

```cpp
// MSILISelLowering.cpp (ctor)
setOperationAction(ISD::DYNAMIC_STACKALLOC, PtrVT, Custom); // -> MSILISD::LOCALLOC
setOperationAction(ISD::STACKSAVE, MVT::Other, Custom);     // -> 0 (M1)
setOperationAction(ISD::STACKRESTORE, MVT::Other, Custom);  // -> chain only (M1)

SDValue MSILTargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op,
                                                    SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT PtrVT = Op.getValueType();
  SDValue Chain = Op.getOperand(0), Size = Op.getOperand(1);
  Align A = cast<ConstantSDNode>(Op.getOperand(2))->getMaybeAlignValue()
                .valueOrOne();
  Align LocA = Subtarget->getLocallocAlign();
  if (A > LocA) // over-allocate, align up afterwards
    Size = DAG.getNode(ISD::ADD, DL, PtrVT, Size,
                       DAG.getConstant(A.value() - LocA.value(), DL, PtrVT));
  SDValue Mem = DAG.getNode(MSILISD::LOCALLOC, DL,
                            DAG.getVTList(PtrVT, MVT::Other), Chain, Size);
  SDValue Ptr = Mem;
  if (A > LocA) {
    Ptr = DAG.getNode(ISD::ADD, DL, PtrVT, Ptr,
                      DAG.getConstant(A.value() - 1, DL, PtrVT));
    Ptr = DAG.getNode(ISD::AND, DL, PtrVT, Ptr,
                      DAG.getSignedConstant(-int64_t(A.value()), DL, PtrVT));
  }
  return DAG.getMergeValues({Ptr, Mem.getValue(1)}, DL);
}
```

- Correctness: the RegStackify rule (5.5) keeps every `LOCALLOC` a root, so this is valid IL anywhere in the method.
- **Limitation:** memory is reclaimed only at method return. A VLA or `alloca` in a loop grows the stack every iteration; native code would reclaim it at `stackrestore`.
- If that is unacceptable, use `fail(DL, DAG, "dynamic alloca is not supported yet")` (the wasm `fail` idiom) instead.
- Either way, phase 3 replaces this with the shadow stack:
  - a `[ThreadStatic] native int` SP in `MsilC.Runtime`;
  - `stacksave`/`stackrestore` read and write it;
  - functions with dynamic allocas save and restore it on every return, and on the setjmp/longjmp catch path.

### 6.9 What else lands in the static frame

All of these become ordinary frame indices in the one localloc block, so they need no shadow stack:
- byval argument copies;
- the caller-side vararg buffer;
- spill-free SelectionDAG temporaries (`CreateStackObject`);
- StackColoring-merged allocas (-O1+).

### 6.10 Measured alternative: a value-type frame local (recommend revisiting Design 4.2)

The same `FP` abstraction can be fed by an address-taken value-type local instead of localloc: `ldloca.s frame; conv.u; stloc fp`. Here `frame` is a `.locals` entry of a module-level type `.class private sealed explicit ansi '<frame_N_A>' extends [mscorlib]System.ValueType { .pack A .size N }`. Measured with the JIT disassembly on .NET 8 and .NET 10 (`e4`, `e5`):

| | localloc frame (16 B / 256 B) | value-type local frame |
|---|---|---|
| Non-inlined callee (.NET 10) | rbp frame + stack probe + GS cookie + `CORINFO_HELP_FAIL_FAST` check, **73 bytes** | struct promoted, store and load forwarded, **4 bytes** (`lea eax,[rcx+1]; ret`) |
| Inlined into a caller (.NET 8/10) | inlined, still keeps the GS cookie check (50-70 bytes) | fully optimized (4 bytes on .NET 10) |
| Empty-stack constraint | yes (RegStackify rule) | none (`ldloca` is legal anywhere) |
| Alignment | 16 on both runtimes | 0 mod 16 on .NET 8, but **8 mod 16 on .NET Framework** (`e6`). Needs an align-up when MaxAlign > 8. |
| Zeroing | not zeroed without `init` | not zeroed without `init` |

The value-type local is clearly better for RyuJIT. The cost is that the AsmPrinter must emit one explicit-layout type per distinct (size, align) and add one typed local. The design choice belongs to the Design.md owner. Recommendation: implement the localloc prologue for M1 as designed, but keep the prologue in one helper (`emitFrameBase`) so a `-msil-frame=struct` variant is a local change. Measure it in phase 4.

### 6.11 Stack protector

- `TargetPassConfig::addISelPrepare` always adds `StackProtector` (`TargetPassConfig.cpp:818`). Functions with `ssp`/`sspstrong`/`sspreq` would then reference `__stack_chk_guard` and `__stack_chk_fail`, which the CLR does not need: the JIT inserts its own GS cookies.
- The MSIL toolchain should default to `-fno-stack-protector` (driver agent).
- For robustness with foreign bitcode, the backend should strip `ssp*` attributes in an early MSIL IR pass. It is unverified whether `disablePass(&StackProtector::ID)` works for a pass added through `addPass(Pass*)`, so prefer stripping the attributes.

---

## 7. Locals and arguments (Q4)

**How WebAssembly does it.**
- `LowerFormalArguments` creates `ARGUMENT_<vt> imm:N` per parameter (`WebAssemblyISelLowering.cpp:1659-1665`) and records `MFI->addParam(VT)`. The vararg buffer pointer is one extra trailing param (1684-1696).
- ExplicitLocals maps each ARGUMENT vreg directly to local index N and erases the ARGUMENT (`WebAssemblyExplicitLocals.cpp:248-266`). **There is no copy:** a use of the argument becomes `local.get N`, and a def (possible after coalescing or coloring) becomes `local.set N`.
- Other locals are numbered from `NumParams + preassignedLocals` (270-271). wasm locals and params share one index space.
- RegStackify never stackifies an ARGUMENT def (907-908), so arguments are always read at their use point.

**MSIL recommendation:** keep exactly this. No copies of arguments into locals. Translate at ExplicitLocals time into CIL's two index spaces.

```cpp
// MSILExplicitLocals.cpp: unified numbering kept (params first), opcodes split.
const unsigned NumParams = MFI.getParams().size();
auto IsArg = [&](unsigned Local) { return Local < NumParams; };

static unsigned getGetOpcode(const TargetRegisterClass *RC, bool Arg); // LDARG_* / LDLOC_*
static unsigned getSetOpcode(const TargetRegisterClass *RC, bool Arg); // STARG_* / STLOC_*
static unsigned getTeeOpcode(const TargetRegisterClass *RC, bool Arg); // "dup; starg|stloc"

// local.get replacement (was lines 445-460):
unsigned Local = getLocalId(Reg2Local, MFI, CurLocal, OldReg);
bool Arg = IsArg(Local);
InsertPt = BuildMI(MBB, InsertPt, InsertPt->getDebugLoc(),
                   TII->get(getGetOpcode(RC, Arg)), NewReg)
               .addImm(Arg ? Local : Local - NumParams);
// Likewise for local.set (stloc/starg), local.tee, and the TEE rewrite.
// DROP_<rc> stays as is and prints as 'pop'.
```

- **`MFI.getLocals()`** (CIL locals only, index − NumParams) becomes `.locals (int32, int64, float64, native int, …)`, printed without names and **without `init`**.
- **Type mapping:** RC → CIL type. `I32`→`int32`, `I64`→`int64`, `F32`→`float32`, `F64`→`float64`, pointer class→`native int`.
  - Store a CIL-level enum per local (`std::vector<MSIL::ValType>`), not an MVT. If pointers stay `i64` in MIR, the MVT cannot tell `int64` from `native int`.
- **Printer short forms:** `ldarg.0-3`/`ldarg.s`/`ldarg`, `ldloc.0-3`/`ldloc.s`/`ldloc`, `starg.s`/`starg`, `stloc.0-3`/`stloc.s`/`stloc`. ilasm encodes exactly what is written (unverified; it does not appear to shorten). Choose the form in the InstPrinter or MC encoder.
- **Tee:** CIL has no `local.tee`. `LOCAL_TEE` is one MI that prints `dup` + `stloc k` (or `starg`). It cannot be two MIs, because `dup` would need two defs, which breaks the "at most one stack def per MI" model. Stack effect: pops 1, pushes 1, transient +1 (8.2). Verified valid on both runtimes (`e8`).
- **Hazard: narrow parameter types.** RegColoring can give a temporary the same color as an argument vreg (`WebAssemblyRegColoring.cpp:284-295`), which becomes `starg`. If the CIL signature declared that parameter as `int8`/`int16`/`bool`/`char` while the vreg is `I32`, `starg` would truncate the temporary and miscompile. **Requirement for the signature/ISel agent:** internal method signatures must be exactly the legalized `MFI->getParams()`/`getResults()` mapped 1:1 to `int32`/`int64`/`float32`/`float64`/`native int`, as wasm does with `computeSignatureVTs`.
  - Exact narrow C types belong only in P/Invoke declarations, which have no IL body, and in separate export wrapper stubs if C#-facing exports need them (Design goal 5).
  - Alternatively, ExplicitLocals could copy narrow params into a local at entry when the ARGUMENT vreg has more than one def. That is more complex; prefer widened signatures.
- **Multi-result calls must not exist.** CIL returns at most one value. The ISel agent must make `CanLowerReturn` reject more than one legal return value (sret instead). Otherwise RegStackify and ExplicitLocals produce multi-def MIs, which break the one-def stack model and the verifier.

---

## 8. `.maxstack` (Q5)

### 8.1 Why exact, and why a pass

- `.maxstack` smaller than the real peak throws `InvalidProgramException` on both runtimes (`e2` b). PEVerify reports "Stack overflow".
- ilasm defaults to 8 when the directive is omitted (`e7`), so it must always be emitted.
- Over-estimating is harmless, apart from a little JIT importer memory.
- ilasm accepts `.maxstack` and `.locals` anywhere in the method body, even after the code (`e7`), so text output could print them at the end. The phase-2 binary writer needs the value before the body (method header), though.

**Decision:** compute it in a MachineFunction pass that runs **last** (`addPreEmitPass2`, after everything that can add instructions). Store it in `MSILFunctionInfo::MaxStack`. The AsmPrinter emits it in `emitFunctionBodyStart`, ideally through `MSILTargetStreamer::emitMaxStack(N)` / `emitLocals(...)` so the phase-2 object streamer can reuse them.

### 8.2 Stack-effect model

After MSILExplicitLocals, every explicit register operand of every non-debug MI is a stack slot (assert at `WebAssemblyExplicitLocals.cpp:487-501`). Therefore, for every real CIL instruction:

```
pops   = number of explicit register *uses*
pushes = number of explicit register *defs*   (0 or 1)
```

Immediates, MBBs, symbols and implicit operands (VALUE_STACK, ARGUMENTS) do not count.

| Instruction | Stack effect |
|---|---|
| Calls (`call` / `calli`) | Args are uses (`calli`'s function pointer is the last use, as in wasm's `CALL_INDIRECT`); the result is the def. |
| `switch` | Pops the index. |
| `ret` | Pops 0 or 1. |
| `cpblk` / `initblk` | Pop 3. |

**Pseudos that print as several CIL opcodes** can go above `max(depth_before, depth_after)` while they run. Declare that extra in TableGen:

```tablegen
// MSILInstrFormats.td
class MSILInst<...> : ..., Instruction {
  // Extra evaluation-stack depth needed while this instruction executes,
  // above max(depth before, depth after). Non-zero only for pseudos that
  // print as several CIL opcodes.
  bits<2> StackPeakExtra = 0;
  let TSFlags{1-0} = StackPeakExtra;
}
// e.g.
let StackPeakExtra = 1 in defm LOCAL_TEE_I32 : ...;   // dup ; stloc
let StackPeakExtra = 1 in defm TRAP : ...;            // ldnull ; throw
let StackPeakExtra = 1 in defm LOAD_OFF_* : ...;      // ldc.i4 off ; add ; ldind  (if ever added)
// BR_TABLE_I64: "conv.i; switch(...); br L" -> 0
```

### 8.3 The pass (computes and verifies)

```cpp
// MSILStackCheck.cpp
static unsigned getStackPeakExtra(const MachineInstr &MI) {
  return MI.getDesc().TSFlags & MSILII::StackPeakExtraMask; // bits 1..0
}

static bool stackCheck(MachineFunction &MF) {
  auto &MFI = *MF.getInfo<MSILFunctionInfo>();
  const auto &TII = *MF.getSubtarget<MSILSubtarget>().getInstrInfo();
  const unsigned NumResults = MFI.getResults().size(); // 0 or 1
  bool Changed = false;
  auto Fail = [&](const MachineInstr *MI, const Twine &Why) {
    std::string S;
    raw_string_ostream OS(S);
    if (MI)
      MI->print(OS);
    reportFatalInternalError("MSIL stack check failed in '" + MF.getName() +
                             "': " + Why + (MI ? ": " + Twine(S) : Twine()));
  };

  // CIL forbids falling off the end of a method (verified: the JIT throws
  // InvalidProgramException; ilasm does not diagnose it). TrapUnreachable
  // normally guarantees a barrier here; this is a cheap safety net.
  MachineBasicBlock &Last = MF.back();
  auto LastI = Last.getLastNonDebugInstr();
  if (LastI == Last.end() || !LastI->isBarrier()) {
    BuildMI(&Last, DebugLoc(), TII.get(MSIL::TRAP));
    Changed = true;
  }

  unsigned Max = 0;
  for (MachineBasicBlock &MBB : MF) {
    unsigned Depth = 0; // invariant: empty at every block entry (section 5)
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || MI.isPosition() || MI.isKill() ||
          MI.isImplicitDef())
        continue;
      unsigned Pops = 0, Pushes = 0;
      for (const MachineOperand &MO : MI.explicit_operands()) {
        if (!MO.isReg())
          continue;
        assert(MFI.isVRegStackified(MO.getReg()) &&
               "register operand survived ExplicitLocals");
        MO.isDef() ? ++Pushes : ++Pops;
      }
      if (Pushes > 1)
        Fail(&MI, "instruction pushes more than one value");
      if (Pops > Depth)
        Fail(&MI, "evaluation stack underflow");
      if (MI.isReturn() && Depth != NumResults)
        Fail(&MI, "'ret' with extra values on the evaluation stack");
      if (MI.getOpcode() == MSIL::LOCALLOC && Depth != 1)
        Fail(&MI, "'localloc' with a non-empty evaluation stack");
      Max = std::max(Max, Depth + getStackPeakExtra(MI));
      Depth = Depth - Pops + Pushes;
      Max = std::max(Max, Depth);
    }
    if (Depth != 0)
      Fail(nullptr, "evaluation stack not empty at end of " +
                        Twine(MBB.getFullName()));
  }
  MFI.setMaxStack(Max);
  return Changed;
}
```

- Cost: one linear walk.
- The checks are always on, not just with assertions. A violation would otherwise be a runtime `InvalidProgramException` far from the compiler.
- No dataflow is needed because the depth is 0 at every block boundary.
- The pass is skipped with `-msil-disable-explicit-locals`, whose output is not valid IL.
- Add `maxStack` to the MSIL MIR YAML (`yaml::MSILFunctionInfo`) so `llc -run-pass=msil-stack-check` tests can check it.

### 8.4 AsmPrinter

```cpp
void MSILAsmPrinter::emitFunctionBodyStart() {
  auto *MFI = MF->getInfo<MSILFunctionInfo>();
  getTargetStreamer()->emitMaxStack(MFI->getMaxStack());   // ".maxstack N"
  if (!MFI->getLocals().empty())
    getTargetStreamer()->emitLocals(MFI->getLocals());     // ".locals (int32, ...)"  -- never 'init'
  AsmPrinter::emitFunctionBodyStart();
}
```

`.maxstack 0` is accepted (`e8`).

---

## 9. What guarantees the CIL stack rules (Q6)

| Rule | Guaranteed by |
|---|---|
| Stack empty at every branch target and block start | RegStackify only builds trees inside a block (5.1); ExplicitLocals stores or pops every non-stackified def; no pass after ExplicitLocals inserts unconsumed pushes; terminators are roots. Checked by `MSILStackCheck` (depth 0 at every block end). |
| `ret` sees exactly the return value | `RETURN` is a root whose explicit uses are the results (0 or 1); `CanLowerReturn` forbids more than 1 (section 7). Checked by `MSILStackCheck`. |
| `localloc` sees only its size | The RegStackify rule never stackifies a LOCALLOC def (5.5). Checked by `MSILStackCheck`. |
| No falling off the end of the method | `TrapUnreachable = true`, `NoTrapAfterNoreturn = false` (4.1); no FALLTHROUGH_RETURN (Peephole dropped); TRAP is a barrier; `MSILStackCheck` appends a TRAP otherwise. |
| `.maxstack` ≥ actual peak | `MSILStackCheck` (8.3) with `StackPeakExtra` for multi-opcode pseudos. |
| Every CIL push is a modeled def | **ISel/TableGen contract:** an MSIL MI's CIL stack effect is exactly (−explicit reg uses, +explicit reg defs), plus `StackPeakExtra`. No hidden pushes or pops; for example, `cpblk` is 3 uses and 0 defs, and a void call has no def. |
| Unused results popped | ExplicitLocals inserts `DROP` (→ `pop`) for defs with no uses (`WebAssemblyExplicitLocals.cpp:278-285` and 378-386). |
| Later: EH | Catch handlers start with the exception object on the stack, and `leave` empties it. That needs a "handler entry depth" in StackCheck when EH arrives. |

---

## 10. Experiments run (all on this host; .NET Framework 4.8 x64 and .NET 8.0.31, plus .NET 10.0.12 where noted)

All IL is assembled with `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe` and run as `./eN.exe` (Framework) and `dotnet eN.exe` with a hand-written `runtimeconfig.json`. No downloads were needed.

| # | File | Result |
|---|---|---|
| e1 | `e1_localloc.il` | `localloc` address mod 16 is 0 for sizes 1/7/24/100/1000 on both runtimes. Without `init`: memory **not** zeroed (1000 of 1000 bytes dirty). With `.locals init`: zeroed. |
| e2 | `e2_rules.il` | Both runtimes: localloc with an extra item under the size → `InvalidProgramException`; `.maxstack 1` with depth 2 → IPE; `ret` with an extra value → IPE. A value on the stack at a forward join → OK; at a backward-branch target → OK (RyuJIT is lenient). Irreducible loop plus `starg` → OK. `switch` on int64 or native int with 2^32+1 → default (no truncation). |
| e3 | `e3_falloff.il` | A method whose last block ends in a conditional branch: ilasm rc=0, but the JIT throws `InvalidProgramException`. A method ending in `ldnull; throw` → OK. |
| e4 | `e4_inline.il` + `DOTNET_JitDisasm` | Both a localloc(16) callee and a value-type-local callee are inlined on .NET 8/10. The localloc version keeps a GS-cookie check (`CORINFO_HELP_FAIL_FAST`). |
| e5 | `e5_noinline.il` + `DOTNET_JitDisasm` (.NET 10) | Non-inlined localloc(256) frame: rbp frame + stack probe + GS cookie, 73 bytes. Value-type local frame: 4 bytes. |
| e6 | `e6_structalign.il` | Address mod 16 of a value-type local with `.pack 16`: 8 on .NET Framework, 0 on .NET 8. |
| e7 | `e7_directives.il` + ildasm | `.maxstack` and `.locals` accepted after the code; forward call to a later method OK; default `.maxstack` is 8. |
| e8 | `e8_misc.il` | `.maxstack 0` OK; `dup; stloc` tee OK; PEVerify only complains about initlocals. |
| e9 | `e9_prologue.il` | The proposed over-aligned prologue (`ldc.i4 80; localloc; ldc.i4.s 31; add; ldc.i4.s -32; and; stloc`) gives a 32-aligned FP with upper bits intact on both runtimes. PEVerify: only "Instruction cannot be verified" at `localloc`. |
| e10 | `e10_i64ptr.il` | An int64 local holding the localloc result and int64 addresses for `ldind`/`stind` work on both runtimes (x64 JITs treat int64 as native int). |
| e11 | `e11_verify.il` + PEVerify | `native int + int64` → "Unexpected type on the stack" (a stack-shape error). `native int and int32` → clean. `ldind` via int64 or native int → "Expected ByRef" (unverifiable, expected). |

Tooling notes:
- PEVerify (`C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.8 Tools\x64\PEVerify.exe`) and ildasm are present and are a usable interim stack-shape checker.
- PEVerify reports only the *first* error per method, so a `localloc` hides later errors in the same method.
- ILVerify is **not** installed. It would need the NuGet global tool `dotnet-ilverify`; the version (e.g. 8.0.x or 9.0.x) could not be checked offline.

---

## 11. Requirements this design places on other components

| Component | Requirement |
|---|---|
| ISel / TableGen | One stack def per MI at most. Stack effect = (−uses, +defs) plus `StackPeakExtra`. `LOCALLOC` (`hasSideEffects`, not rematerializable). `BR_IF`→`brtrue`, `BR_UNLESS`→`brfalse` (both encodable). `BR_TABLE` prints `[conv.i;] switch(...); br default`. `TRAP` is a terminator and barrier (e.g. `ldnull; throw`). Every instruction `Defs=[ARGUMENTS]`; `ARGUMENT` `Uses=[ARGUMENTS]`. No load/store offset operands in M1. `CanLowerReturn` limits returns to 1 value. Native-int + int32 opcodes for frame addressing. Decide the pointer representation (I64 vs a `native int` class): it determines whether ilverify is free of stack-shape errors (`e11`). |
| Signatures | Internal methods use exactly the legalized `getParams()`/`getResults()` as `int32`/`int64`/`float32`/`float64`/`native int` (narrow-type hazard, section 7). |
| MC / AsmPrinter | Lower `MO_MachineBasicBlock` to a symbol; dot-free private label prefix; `.maxstack` and `.locals` (no `init`) from MFI; collect P/Invoke targets during emission (no MCLowerPrePass); diagnose non-empty `llvm.global_dtors`; `SupportsDebugInformation = false` in M1; comment string `//`. |
| clang CodeGen (`clang/lib/CodeGen/Targets/MSIL.cpp`) | Add the `"no-prototype"` attribute to prototype-less declarations without a body, as `WebAssembly.cpp:122-123` does. Otherwise AddMissingPrototypes does nothing. |
| clang Driver | Default stack-protector level off for msil triples. |
| Runtime (phase 3) | Shadow stack for dynamic allocas and stacksave/restore; `__cxa_atexit` or ProcessExit for global dtors. |

---

## 12. Tests

**Port from `llvm/test/CodeGen/WebAssembly/`**, changing the triple and the opcode names:
- `reg-stackify.ll`
- `explicit-locals.mir`
- `implicit-def.ll`
- `reg-argument.mir`
- `reg-copy.mir`
- `unused-argument.ll`
- `returned.ll`
- `function-bitcasts.ll`
- `add-prototypes.ll`
- `switch.ll`, `switch-unreachable-default.ll`
- `unreachable.ll`
- `stack-alignment.ll` and `userstack.ll` → rewrite for the localloc frame.

**New MIR and FileCheck tests:**
- `localloc-root.mir`: a LOCALLOC result used as the 2nd operand must go through a local, never nested.
- `frame-index.ll`: `ldloc fp; ldc.i4 off; add` shapes, offset folding into an `add`, `FP+0`.
- `frame-overalign.ll`: `_Alignas(32)` → the align-up sequence.
- `maxstack.ll`: tee peak +1, 3-argument call, `switch`, a void function with `.maxstack 0`.
- `stack-check-falloff.mir`: a TRAP is appended.
- `stack-check-errors.mir`: an expected fatal error for an unbalanced block (`not llc ... 2>&1 | FileCheck`).
- `args.ll`: argument writes become `starg`, arguments are never copied to locals, local indexes start at 0.

**Runtime lit tests (dotnet):**
- frame alignment check (`(uintptr_t)&x % 32 == 0`);
- deep recursion with frames;
- `alloca` in a loop (documents the M1 leak, or expects the diagnostic);
- every runtime test also goes through PEVerify (Windows) or ilverify. Filter out the allowed categories: initlocals, Expected ByRef, localloc unverifiable. Any other message fails.

---

## 13. Risks and open questions

1. **Localloc alignment is empirical** (16 on x64, both runtimes). Unmeasured on ARM64 and x86; keep it a subtarget property with a test.
2. **Localloc frames are slow under RyuJIT** (GS cookie, rbp frame, probe; `e4`/`e5`). This is a real performance issue for every C function with an address-taken local. Section 6.10 has the measured alternative; the Design.md owner should decide in or before phase 4.
3. **int64 vs native int.** If pointers remain `i64` in MIR and locals are typed `int64`, the code runs on 64-bit JITs (`e10`), but PEVerify reports stack-type errors when native int and int64 mix (`e11`). That conflicts with the Design 12 exit criterion "ilverify reports no stack-shape errors". Owned by the ISel/type agent. The frame code here is written to use `native int` with int32 constants either way.
4. **Narrow parameter types combined with RegColoring and `starg`** can silently truncate (section 7). This needs the widened-signature rule.
5. **`.maxstack` drift.** Any new multi-opcode pseudo that forgets `StackPeakExtra` under-reports, and the failure is a runtime `InvalidProgramException`. Mitigations: StackCheck plus runtime tests. A `-msil-maxstack-slack=N` option (default 0) is a cheap escape hatch.
6. **Dynamic alloca fallback** leaks stack in loops until return (6.8). Alternatively, use a diagnostic in M1.
7. **Fork maintenance.** RegStackify (1062 lines) and ExplicitLocals are copied. Keep the diff mechanical (renames plus the LOCALLOC rule plus the arg/local split) so upstream fixes can be cherry-picked (Design risk 5).
8. **Open:**
   - should M1 implement LowerGlobalDtors now, given that `__cxa_atexit` belongs to the runtime design?
   - pin an ILVerify version for CI (`dotnet-ilverify`, version unverified offline);
   - how StackCheck should model EH handler-entry depth when EH arrives.
