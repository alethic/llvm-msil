//===-- MSILTargetMachine.cpp - Define TargetMachine for MSIL -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyTargetMachine.cpp at
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the MSIL-specific subclass of TargetMachine and the MSIL
/// code generation pass pipeline.
///
//===----------------------------------------------------------------------===//

#include "MSILTargetMachine.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSIL.h"
#include "MSILMachineFunctionInfo.h"
#include "MSILTargetObjectFile.h"
#include "MSILTargetTransformInfo.h"
#include "TargetInfo/MSILTargetInfo.h"
#include "llvm/CodeGen/MIRParser/MIParser.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Utils.h"
#include <optional>

using namespace llvm;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMSILTarget() {
  // Register the target.
  RegisterTargetMachine<MSILTargetMachine> X(getTheMSILTarget());

  // Register backend passes.
  auto &PR = *PassRegistry::getPassRegistry();
  initializeMSILPrepareIRLegacyPass(PR);
  initializeMSILAddMissingPrototypesLegacyPass(PR);
  initializeMSILFixFunctionBitcastsLegacyPass(PR);
  initializeMSILOptimizeReturnedLegacyPass(PR);
  initializeMSILDAGToDAGISelLegacyPass(PR);
  initializeMSILArgumentMoveLegacyPass(PR);
  initializeMSILSetMemFlagsLegacyPass(PR);
  initializeMSILCleanCodeAfterTrapLegacyPass(PR);
  initializeMSILNullifyDebugValueListsLegacyPass(PR);
  initializeMSILReplacePhysRegsLegacyPass(PR);
  initializeMSILOptimizeLiveIntervalsLegacyPass(PR);
  initializeMSILRegStackifyLegacyPass(PR);
  initializeMSILRegColoringLegacyPass(PR);
  initializeMSILExplicitLocalsLegacyPass(PR);
  initializeMSILRegNumberingLegacyPass(PR);
  initializeMSILStackCheckLegacyPass(PR);
  initializeMSILAsmPrinterPass(PR);
}

//===----------------------------------------------------------------------===//
// MSIL Lowering public interface.
//===----------------------------------------------------------------------===//

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model>) {
  // Always static, whatever the driver asks for (clang passes PIC on Linux).
  // CIL has no load-time relocations: global addresses are computed at run
  // time from the data segment that the module initializer allocates, and
  // calls name their callee directly. A position-independent model would
  // only make the optimizer build PC-relative lookup tables.
  return Reloc::Static;
}

/// Create an MSIL architecture model.
///
/// Every triple is accepted here, so that tools that only build a
/// TargetMachine (opt, llc -mcpu=help) work: MSILPrepareIR diagnoses a triple
/// that Phase 1 cannot compile.
///
MSILTargetMachine::MSILTargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     std::optional<Reloc::Model> RM,
                                     std::optional<CodeModel::Model> CM,
                                     CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT, CPU, FS, Options,
                               getEffectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<MSILTargetObjectFile>()) {
  // A CIL method body must not fall off its end (the JIT rejects it with
  // InvalidProgramException), so 'unreachable' becomes a trap, also after
  // calls to noreturn functions.
  this->Options.TrapUnreachable = true;
  this->Options.NoTrapAfterNoreturn = false;

  // There is no object file to put an address-significance table in.
  this->Options.EmitAddrsig = false;

  // C has no unwinding in Phase 1, and CIL exceptions are not native unwind
  // tables. Every incoming model becomes None, before initAsmInfo() copies it
  // into the MCAsmInfo: clang's MinGW toolchain passes -exception-model=seh
  // even for C, and that would otherwise turn into WinEH.
  this->Options.ExceptionModel = ExceptionHandling::None;

  // CIL methods have no sections. Basic block sections are dropped silently,
  // like the exception model (there is no LLVMContext to warn through), and
  // here rather than in MSILPrepareIR: TargetPassConfig::addMachinePasses
  // reads the option when it builds the pipeline, and a profile file adds
  // passes that assert the List type.
  this->Options.BBSections = BasicBlockSection::None;

  initAsmInfo();

  // MSIL does not run RegAllocFast; tied operands must be lowered by
  // TwoAddressInstructionPass.
  setEnableTiedFastRegAlloc(false);

  // Note that we don't use setRequiresStructuredCFG(true): CIL branches may
  // go anywhere in a method, so every CFG optimization stays available.
}

MSILTargetMachine::~MSILTargetMachine() = default; // anchor.

const MSILSubtarget *MSILTargetMachine::getSubtargetImpl(StringRef CPU,
                                                         StringRef FS) const {
  // Keyed on the native strings, so each subtarget keeps exactly what the
  // front end wrote; a CPU name never contains a comma. Every subtarget is
  // built as the "generic" MSIL processor without features.
  auto &I = SubtargetMap[(CPU + "," + FS).str()];
  if (!I)
    I = std::make_unique<MSILSubtarget>(TargetTriple, CPU, /*TuneCPU=*/CPU, FS,
                                        *this);
  return I.get();
}

const MSILSubtarget *
MSILTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  StringRef CPU = CPUAttr.isValid() ? CPUAttr.getValueAsString() : TargetCPU;
  StringRef FS = FSAttr.isValid() ? FSAttr.getValueAsString() : TargetFS;

  return getSubtargetImpl(CPU, FS);
}

namespace {

/// MSIL Code Generator Pass Configuration Options.
class MSILPassConfig final : public TargetPassConfig {
public:
  MSILPassConfig(MSILTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  MSILTargetMachine &getMSILTargetMachine() const {
    return getTM<MSILTargetMachine>();
  }

  FunctionPass *createTargetRegisterAllocator(bool) override;

  void addIRPasses() override;
  void addISelPrepare() override;
  bool addInstSelector() override;
  void addPostRegAlloc() override;
  bool addGCPasses() override { return false; }
  void addPreEmitPass() override;
  void addPreEmitPass2() override;

  // No reg alloc
  bool addRegAssignAndRewriteFast() override { return false; }

  // No reg alloc
  bool addRegAssignAndRewriteOptimized() override { return false; }
};
} // end anonymous namespace

MachineFunctionInfo *MSILTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return MSILFunctionInfo::create<MSILFunctionInfo>(Allocator, F, STI);
}

TargetTransformInfo
MSILTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<MSILTTIImpl>(this, F));
}

TargetPassConfig *MSILTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new MSILPassConfig(*this, PM);
}

Expected<std::unique_ptr<MCStreamer>>
MSILTargetMachine::createMCStreamer(raw_pwrite_stream &Out,
                                    raw_pwrite_stream *DwoOut,
                                    CodeGenFileType FileType, MCContext &Ctx) {
  if (FileType == CodeGenFileType::ObjectFile)
    return make_error<StringError>(
        "MSIL object file emission is not implemented; use -S (ILAsm text)",
        inconvertibleErrorCode());
  return CodeGenTargetMachineImpl::createMCStreamer(Out, DwoOut, FileType, Ctx);
}

FunctionPass *MSILPassConfig::createTargetRegisterAllocator(bool) {
  return nullptr; // No reg alloc
}

//===----------------------------------------------------------------------===//
// The following functions are called from lib/CodeGen/Passes.cpp to modify
// the CodeGen pass sequence.
//===----------------------------------------------------------------------===//

void MSILPassConfig::addIRPasses() {
  // Diagnose the IR that MSIL cannot compile, and neutralize it so that no
  // later pass meets it. This runs first.
  addPass(createMSILPrepareIRLegacyPass());

  // Add signatures to prototype-less function declarations.
  addPass(createMSILAddMissingPrototypesLegacyPass());

  // Fix function bitcasts, as CIL requires caller and callee signatures to
  // match.
  addPass(createMSILFixFunctionBitcastsLegacyPass());

  // Optimize "returned" function attributes.
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createMSILOptimizeReturnedLegacyPass());

  // There is no exception model: lower invokes into calls and delete the
  // unreachable landing pads.
  addPass(createLowerInvokePass());
  addPass(createUnreachableBlockEliminationPass());

  // Expand indirectbr instructions to switches: CIL cannot take the address
  // of a label.
  addPass(createIndirectBrExpandPass(getOptLevel()));

  TargetPassConfig::addIRPasses();
}

void MSILPassConfig::addISelPrepare() {
  // This is a no-op if atomics are not used in the module.
  addPass(createAtomicExpandLegacyPass());

  TargetPassConfig::addISelPrepare();
}

bool MSILPassConfig::addInstSelector() {
  (void)TargetPassConfig::addInstSelector();
  addPass(createMSILISelDagLegacyPass(getMSILTargetMachine(), getOptLevel()));
  // Run the argument-move pass immediately after the ScheduleDAG scheduler
  // so that we can fix up the ARGUMENT instructions before anything else
  // sees them in the wrong place.
  addPass(createMSILArgumentMoveLegacyPass());
  // Set the memory-flags operands (the unaligned. and volatile. prefixes).
  // This information is present during ISel, but later passes may drop the
  // memory operands it comes from. Collect it now.
  addPass(createMSILSetMemFlagsLegacyPass());
  // A trap is a terminator; no instruction may follow it.
  addPass(createMSILCleanCodeAfterTrapLegacyPass());

  return false;
}

void MSILPassConfig::addPostRegAlloc() {
  // TODO: The following CodeGen passes don't currently support code containing
  // virtual registers. Consider removing their restrictions and re-enabling
  // them.

  // These functions all require the NoVRegs property.
  disablePass(&MachineLateInstrsCleanupID);
  disablePass(&MachineCopyPropagationID);
  disablePass(&PostRAMachineSinkingID);
  disablePass(&PostRASchedulerID);
  disablePass(&FuncletLayoutID);
  disablePass(&StackMapLivenessID);
  disablePass(&PatchableFunctionID);
  disablePass(&ShrinkWrapID);
  disablePass(&RemoveLoadsIntoFakeUsesID);

  // Keep the block layout close to the IR order; the JIT does its own layout.
  disablePass(&MachineBlockPlacementID);

  TargetPassConfig::addPostRegAlloc();
}

void MSILPassConfig::addPreEmitPass() {
  TargetPassConfig::addPreEmitPass();

  // Nullify DBG_VALUE_LISTs that we cannot handle.
  addPass(createMSILNullifyDebugValueListsLegacyPass());

  // Remove any unreachable blocks that may be left floating around.
  addPass(&UnreachableMachineBlockElimID);

  // Now that we have a prologue and epilogue and all frame indices are
  // rewritten, eliminate FP. This allows it to be stackified, colored, and
  // numbered with the rest of the registers.
  addPass(createMSILReplacePhysRegsLegacyPass());

  // Preparations and optimizations related to register stackification.
  if (getOptLevel() != CodeGenOptLevel::None) {
    // Depend on LiveIntervals and perform some optimizations on it.
    addPass(createMSILOptimizeLiveIntervalsLegacyPass());
  }

  // Mark registers as representing the CIL evaluation stack. We run this pass
  // very late, so that it sees as much code as possible, including code
  // emitted by PEI and expanded by late tail duplication.
  addPass(createMSILRegStackifyLegacyPass(getOptLevel()));

  if (getOptLevel() != CodeGenOptLevel::None) {
    // Run the register coloring pass to reduce the total number of locals.
    // This runs after stackification so that it doesn't consider registers
    // that become stackified.
    addPass(createMSILRegColoringLegacyPass());
  }

  // Insert explicit ldloc/stloc and ldarg/starg instructions.
  addPass(createMSILExplicitLocalsLegacyPass());

  // Create a mapping from LLVM CodeGen virtual registers to MSIL registers.
  addPass(createMSILRegNumberingLegacyPass());
}

void MSILPassConfig::addPreEmitPass2() {
  // The last machine pass, so nothing can add instructions after it: compute
  // .maxstack and verify the CIL stack rules. Always on.
  addPass(createMSILStackCheckLegacyPass());
}

yaml::MachineFunctionInfo *
MSILTargetMachine::createDefaultFuncInfoYAML() const {
  return new yaml::MSILFunctionInfo();
}

yaml::MachineFunctionInfo *
MSILTargetMachine::convertFuncInfoToYAML(const MachineFunction &MF) const {
  const auto *MFI = MF.getInfo<MSILFunctionInfo>();
  return new yaml::MSILFunctionInfo(MF, *MFI);
}

bool MSILTargetMachine::parseMachineFunctionInfo(
    const yaml::MachineFunctionInfo &MFI, PerFunctionMIParsingState &PFS,
    SMDiagnostic &Error, SMRange &SourceRange) const {
  const auto &YamlMFI = static_cast<const yaml::MSILFunctionInfo &>(MFI);
  MachineFunction &MF = PFS.MF;
  MF.getInfo<MSILFunctionInfo>()->initializeBaseYamlFields(MF, YamlMFI);
  return false;
}
