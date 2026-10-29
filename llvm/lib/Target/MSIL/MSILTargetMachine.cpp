//===-- MSILTargetMachine.cpp - Define TargetMachine for MSIL -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyTargetMachine.cpp at
// d1e15b695381.
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
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MIRParser/MIParser.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/PassRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Utils.h"
#include <optional>

using namespace llvm;

static cl::opt<bool> MSILObjectForm(
    "msil-object-form",
    cl::desc("MSIL: compile in object mode and write the CIL object as ILAsm "
             "text (object-form text) instead of whole-program text"),
    cl::init(false));

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

/// The exception models that a TargetMachine can support: all but None and
/// Default. MSIL supports none of them (decision D8).
static constexpr ExceptionHandling NativeExceptionModels[] = {
    ExceptionHandling::DwarfCFI, ExceptionHandling::SjLj,
    ExceptionHandling::ARM,      ExceptionHandling::WinEH,
    ExceptionHandling::Wasm,     ExceptionHandling::Emscripten,
    ExceptionHandling::AIX,      ExceptionHandling::ZOS,
};

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
  // tables (decision D8): no exception model is supported, so
  // TargetPassConfig adds none of the EH preparation passes. The defaults
  // that TargetMachine derives from the triple say the same for the MSIL
  // object format; this does not depend on them. The model that codegen uses
  // comes from Module::getExceptionModel(): clang emits no "exception-model"
  // flag for an MSIL triple, so the MSIL triple's default, None, applies, and
  // MSILAsmPrinter and MSILPrepareIR reset an incoming flag to "none".
  for (ExceptionHandling Model : NativeExceptionModels)
    setSupportsExceptionModel(Model, false);

  // CIL methods have no sections. Basic block sections are dropped silently
  // (there is no LLVMContext to warn through), and here rather than in
  // MSILPrepareIR: TargetPassConfig::addMachinePasses reads the option when
  // it builds the pipeline, and a profile file adds passes that assert the
  // List type.
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
      : TargetPassConfig(TM, PM) {
    // CIL methods have no sections, so no function is ever split. Late
    // function splitting is dropped silently, like basic block sections in
    // the MSILTargetMachine constructor, but here: the base constructor has
    // just applied llc's and -mllvm's -function-splitting=, and
    // addMachinePasses reads the mode when it builds the pipeline (with All
    // it adds BasicBlockSections, which gives the method a section of its
    // own and moves cold blocks to a cold section).
    TM.Options.FunctionSplitting = FunctionSplittingMode::None;
  }

  MSILTargetMachine &getMSILTargetMachine() const {
    return getTM<MSILTargetMachine>();
  }

  FunctionPass *createTargetRegisterAllocator(bool) override;

  void addIRPasses() override;
  void addPassesToHandleExceptions() override;
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

void MSILTargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {
  // msil-ld's LTO applies cli::classifyCall's table before optimization as
  // well as in code generation (plan 5.9.8): otherwise LTO's InstCombine
  // folds a call whose argument has the size of the parameter into the
  // definition's type (transformConstExprCastCall), and the table never sees
  // it. Only the LTO post-link pipelines invoke these extension points, and
  // only msil-ld links MSIL bitcode, so -S, llc and the pre-link pipelines are
  // unchanged. So at -O1 and above the compile step of -flto may still fold a
  // mismatched call inside one TU before msil-ld sees it, as 'clang -c' does
  // on the object path (section 12); mismatches across TUs reach the table.
  auto AddCallTable = [this](ModulePassManager &MPM, OptimizationLevel) {
    MPM.addPass(MSILFixFunctionBitcastsLTOPass(*this));
  };
  PB.registerFullLinkTimeOptimizationEarlyEPCallback(AddCallTable);
  PB.registerThinLinkTimeOptimizationEarlyEPCallback(AddCallTable);
}

bool MSILTargetMachine::isObjectMode() const {
  if (OutputFileType == CodeGenFileType::ObjectFile)
    return true;
  if (OutputFileType == CodeGenFileType::Null)
    return false;
  return MSILObjectForm;
}

Expected<std::unique_ptr<MCStreamer>>
MSILTargetMachine::createMCStreamer(raw_pwrite_stream &Out,
                                    raw_pwrite_stream *DwoOut,
                                    CodeGenFileType FileType, MCContext &Ctx) {
  // CodeGenTargetMachineImpl::addPassesToEmitFile calls this after
  // addIRPasses has added the IR passes and before any pass runs, so the IR
  // passes see the mode when they run.
  OutputFileType = FileType;
  // Object-form text prints the token operands in object form: placeholder
  // MemberRefs on [msil.extern] and BCL members in [msil.bcl].
  if (FileType == CodeGenFileType::AssemblyFile && isObjectMode() &&
      !is_contained(Options.MCOptions.InstPrinterOptions, "object-form"))
    Options.MCOptions.InstPrinterOptions.push_back("object-form");
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
  // unreachable landing pads, here, after the MSIL IR passes above and before
  // IndirectBrExpand and the generic IR passes that
  // TargetPassConfig::addIRPasses adds; addPassesToHandleExceptions adds
  // nothing.
  addPass(createLowerInvokePass());
  addPass(createUnreachableBlockEliminationPass());

  // Expand indirectbr instructions to switches: CIL cannot take the address
  // of a label.
  addPass(createIndirectBrExpandPass(getOptLevel()));

  TargetPassConfig::addIRPasses();
}

void MSILPassConfig::addPassesToHandleExceptions() {
  // The exception model is always None (decision D8), and addIRPasses has
  // already lowered every invoke into a call and removed the landing pads
  // that became unreachable, after the MSIL IR passes and before
  // IndirectBrExpand and the generic IR passes that
  // TargetPassConfig::addIRPasses adds.
  // TargetPassConfig would add LowerInvoke and UnreachableBlockElim a second
  // time, after CodeGenPrepare.
  assert(none_of(NativeExceptionModels,
                 [this](ExceptionHandling Model) {
                   return TM->supportsExceptionModel(Model);
                 }) &&
         "MSIL has no exception model (decision D8)");
}

void MSILPassConfig::addISelPrepare() {
  // This is a no-op if atomics are not used in the module.
  addPass(createAtomicExpandLegacyPass());

  TargetPassConfig::addISelPrepare();
}

bool MSILPassConfig::addInstSelector() {
  (void)TargetPassConfig::addInstSelector();
  addPass(createMSILISelDagLegacyPass(getMSILTargetMachine(), getOptLevel()));
  // Instruction selection already moves the ARGUMENT instructions that the
  // ScheduleDAG scheduler put in the wrong place back to the top of the entry
  // block. The argument-move pass does the same for MIR input that starts
  // after instruction selection.
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
