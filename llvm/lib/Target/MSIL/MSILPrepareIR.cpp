//===-- MSILPrepareIR.cpp - Diagnose IR that MSIL cannot compile ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSILPrepareIR is the first IR pass of the MSIL code generator. It reports
/// the IR constructs that MSIL cannot compile, naming the symbol, and
/// neutralizes each one so that no later pass meets it: llc and clang keep
/// running after an error and look for errors only when the pipeline is done,
/// so the rest of the pipeline must see valid IR without the construct.
///
/// - A target triple that is not an x86_64 or aarch64 MSIL triple with 64-bit
///   pointers is an error. Every function body is deleted, and every alias,
///   ifunc and global variable (llvm.used and the like excepted) is erased,
///   so only the module header is printed.
/// - The stack protector and SafeStack attributes are removed with a warning.
/// - The function entry instrumentation attributes (patchable function entry
///   and prefix, fentry calls) are removed, with a warning when one of them
///   asks for instrumentation.
/// - The function padding attributes ("tail-pad-to-size", "tail-pad-value")
///   are removed, with a warning when one of them asks for padding.
/// - The "zero-call-used-regs" attribute is removed, with a warning when a
///   compiled definition asks for registers to be zeroed.
/// - The call graph section and the basic block address map are turned off,
///   and !pcsections metadata is erased, with one warning each.
/// - Pseudo probes (llvm.pseudoprobe calls and !llvm.pseudo_probe_desc) are
///   erased with a warning, before any pass runs (doInitialization).
/// - A function definition with a garbage collection strategy is an error;
///   its body and its strategy are deleted before any pass runs
///   (doInitialization).
/// - A function definition that is variadic, calls inline assembly, calls a
///   target intrinsic (llvm.x86.*, llvm.aarch64.*, ...), accesses a named
///   register, reads the thread pointer (llvm.thread.pointer), records a
///   stack map (stackmap, patchpoint, statepoint), has deoptimization state
///   ("deopt" operand bundles), makes a preallocated call
///   (llvm.call.preallocated.*, "preallocated" operand bundles), uses
///   the frame escape intrinsics (llvm.localescape, llvm.localrecover,
///   llvm.localaddress, llvm.eh.recoverfp: clang emits them for C __finally
///   blocks and for __except filters that read locals), uses a pointer in a
///   non-zero address space, uses a floating-point type that CIL does not
///   have (fp128, x86_fp80, ppc_fp128), uses a scalable vector type or a
///   target extension type (SVE's <vscale x 4 x i32> and
///   target("aarch64.svcount")), reads vscale (llvm.vscale, the scalable form
///   of llvm.experimental.get.vector.length) or reads or writes the
///   floating-point environment (llvm.get.fpenv, llvm.set.fpenv,
///   llvm.get.fpmode, llvm.set.fpmode) is an error, and its body is deleted.
/// - A function definition or used declaration whose IL name is longer than
///   ilasm accepts is an error; a definition loses its body.
/// - A declaration whose signature has a scalable vector or target extension
///   type is an error when its address is taken.
/// - A call or invoke of a definition that lost its body after an error
///   (above), or of a declaration reported for its signature, is erased, and
///   its uses become poison; an invoke branches to its normal destination.
///   The function is not compiled, so the call would otherwise become a call
///   of an external function. For the same reason every other use of the
///   function (its address in code, in a global initializer or in
///   llvm.global_ctors) becomes poison, and it is removed from llvm.used and
///   llvm.compiler.used.
/// - The code after a trap (a call of llvm.trap, llvm.debugtrap or
///   llvm.ubsantrap without a "trap-func-name") in its block is replaced by
///   'unreachable', which also drops the block's successor edges: the trap
///   becomes the TRAP terminator, so nothing after it runs.
/// - An alias, an ifunc, a global variable in a non-zero address space or one
///   whose type has a target extension type is an error; its uses become
///   poison and it is erased.
/// - A thread-local variable is an error; it becomes an ordinary global.
/// - A non-empty llvm.global_dtors is an error, and it is erased.
/// - An "exception-model" module flag is reset to "none", the model the
///   MSILTargetMachine always uses.
///
/// Module-level inline assembly is diagnosed by MSILAsmPrinter: the legacy
/// pass manager initializes the AsmPrinter before any pass runs. It
/// initializes the passes in pipeline order, so this pass's doInitialization
/// still runs before the AsmPrinter's.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "MSILUtilities.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PseudoProbe.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <string>

using namespace llvm;

#define DEBUG_TYPE "msil-prepare-ir"

namespace {
class MSILPrepareIRLegacy final : public ModulePass {
  StringRef getPassName() const override { return "MSIL Prepare IR"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetPassConfig>();
  }

  bool doInitialization(Module &M) override;
  bool runOnModule(Module &M) override;

  /// The definitions whose bodies this pass deleted after an error, in
  /// doInitialization and in runOnModule, and the declarations that it
  /// reported for their signatures (diagnoseAddressTakenDeclarations).
  SmallVector<Function *, 4> DeletedBodies;

public:
  static char ID; // Pass identification, replacement for typeid
  MSILPrepareIRLegacy() : ModulePass(ID) {}
};
} // end anonymous namespace

/// Returns true if \p Ty is a pointer in a non-zero address space.
static bool isNonFlatPointer(Type *Ty) {
  return Ty->isPointerTy() && Ty->getPointerAddressSpace() != 0;
}

/// Returns true if \p Ty is a floating-point type that CIL has no type for.
static bool isFPTypeWithoutCILType(Type *Ty) {
  return Ty->isFP128Ty() || Ty->isX86_FP80Ty() || Ty->isPPC_FP128Ty();
}

/// Returns true if \p Ty is a scalable vector type or a target extension
/// type, which CIL has no type for.
static bool isScalableOrTargetExtType(Type *Ty) {
  return isa<ScalableVectorType>(Ty) || isa<TargetExtType>(Ty);
}

namespace {
/// Finds the types that \p IsLeaf accepts in types and values, and caches
/// the result for every type and constant it walks. With
/// \p WalkAllocatedType, the allocated type of an alloca counts as a type of
/// the instruction, and with \p WalkGEPSourceType, the source element type of
/// a getelementptr.
template <bool (*IsLeaf)(Type *), bool WalkAllocatedType,
          bool WalkGEPSourceType = false>
class LeafTypeFinder {
  DenseMap<Type *, Type *> TypeCache;
  DenseMap<const Constant *, Type *> ConstantCache;

public:
  /// Returns the first such type that \p Ty is or contains (through vector,
  /// array, structure and function types), or null.
  Type *inType(Type *Ty) {
    if (IsLeaf(Ty))
      return Ty;
    if (Ty->getNumContainedTypes() == 0)
      return nullptr;
    auto [It, Inserted] = TypeCache.try_emplace(Ty, nullptr);
    if (!Inserted)
      return It->second;
    Type *Found = nullptr;
    for (Type *Sub : Ty->subtypes())
      if ((Found = inType(Sub)))
        break;
    // The recursion may have grown the map: look the entry up again.
    TypeCache[Ty] = Found;
    return Found;
  }

  /// Returns the first such type in the type of \p V or, for a constant
  /// other than a global, in its operands (as in 'ptrtoint (ptr
  /// addrspace(256) inttoptr (i64 48 to ptr addrspace(256)) to i64)'), or
  /// null.
  Type *inValue(const Value *V) {
    if (Type *Found = inType(V->getType()))
      return Found;
    const auto *C = dyn_cast<Constant>(V);
    if (!C || isa<GlobalValue>(C) || C->getNumOperands() == 0)
      return nullptr;
    auto [It, Inserted] = ConstantCache.try_emplace(C, nullptr);
    if (!Inserted)
      return It->second;
    Type *Found = nullptr;
    for (const Use &Op : C->operands())
      if ((Found = inValue(Op.get())))
        break;
    ConstantCache[C] = Found;
    return Found;
  }

  /// The same for the result and the operands of \p I and, with
  /// WalkAllocatedType, the allocated type of an alloca, and with
  /// WalkGEPSourceType, the source element type of a getelementptr.
  Type *inInstruction(const Instruction &I) {
    if (Type *Found = inType(I.getType()))
      return Found;
    if constexpr (WalkAllocatedType)
      if (const auto *AI = dyn_cast<AllocaInst>(&I))
        if (Type *Found = inType(AI->getAllocatedType()))
          return Found;
    if constexpr (WalkGEPSourceType)
      if (const auto *GEP = dyn_cast<GetElementPtrInst>(&I))
        if (Type *Found = inType(GEP->getSourceElementType()))
          return Found;
    for (const Use &Op : I.operands())
      if (Type *Found = inValue(Op.get()))
        return Found;
    return nullptr;
  }
};

/// Finds pointers in non-zero address spaces (x86 segment spaces 256-258,
/// the MS 32-bit pointer spaces 270-271, ...) in types and values. CIL has
/// only flat native-int pointers. An alloca's allocated type is not walked:
/// a load or a store of such a pointer has it as its result or operand type.
using AddrSpaceFinder =
    LeafTypeFinder<isNonFlatPointer, /*WalkAllocatedType=*/false>;

/// Finds the floating-point types that CIL has no type for (fp128, x86_fp80,
/// ppc_fp128) in types and values, the allocated type of an alloca included.
/// Instruction selection cannot lower them: their operations become
/// compiler-rt calls whose results come back through a stack slot, and their
/// values are split or passed in memory. long double is double on every MSIL
/// triple, so they come only from __float128 and from IR.
using FPTypeFinder =
    LeafTypeFinder<isFPTypeWithoutCILType, /*WalkAllocatedType=*/true>;

/// Finds the scalable vector types and the target extension types in types
/// and values, the allocated type of an alloca and the source element type of
/// a getelementptr included. D7 accepts every native feature string, so on
/// the aarch64 MSIL triples clang compiles the SVE types: __SVInt32_t is
/// <vscale x 4 x i32> and __SVCount_t is target("aarch64.svcount"). Their
/// size is a multiple of vscale, a property of the native vector unit that
/// CIL does not have: type legalization cannot scalarize them, and an alloca
/// or a getelementptr of one needs vscale (Design.md 3.2 maps only
/// fixed-length vectors). The other target extension types belong to other
/// targets.
using ScalableOrTargetExtTypeFinder =
    LeafTypeFinder<isScalableOrTargetExtType, /*WalkAllocatedType=*/true,
                   /*WalkGEPSourceType=*/true>;
} // end anonymous namespace

/// Returns the message for \p Ty, a type that a ScalableOrTargetExtTypeFinder
/// found.
static std::string getScalableOrTargetExtTypeMessage(Type *Ty) {
  std::string Msg;
  raw_string_ostream OS(Msg);
  if (isa<ScalableVectorType>(Ty))
    OS << "scalable vector type '" << *Ty << "' is not supported for MSIL";
  else
    OS << "type '" << *Ty << "' is not supported for MSIL";
  return Msg;
}

char MSILPrepareIRLegacy::ID = 0;
INITIALIZE_PASS_BEGIN(MSILPrepareIRLegacy, DEBUG_TYPE,
                      "Diagnose and neutralize IR that MSIL cannot compile",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(MSILPrepareIRLegacy, DEBUG_TYPE,
                    "Diagnose and neutralize IR that MSIL cannot compile",
                    false, false)

ModulePass *llvm::createMSILPrepareIRLegacyPass() {
  return new MSILPrepareIRLegacy();
}

/// Phase 1 compiles C for the x86_64 and aarch64 MSIL triples with 64-bit
/// pointers only: the arch selects the C ABI, and pointers are CIL native
/// ints (decision D2), so the ILP32 environments (x86_64 gnux32, aarch64
/// gnu_ilp32), whose DataLayout has 32-bit pointers, are rejected too.
static bool isSupportedTarget(const TargetMachine &TM) {
  const Triple &TT = TM.getTargetTriple();
  return TT.isOSBinFormatMSIL() &&
         (TT.getArch() == Triple::x86_64 || TT.getArch() == Triple::aarch64) &&
         TM.getPointerSizeInBits(0) == 64;
}

/// Resets an "exception-model" module flag to "none". The MSILTargetMachine
/// always uses ExceptionHandling::None, but the codegen code that calls
/// Module::getExceptionModel() reads the flag before the TargetOptions, and
/// IR built for a native triple (MinGW: "wineh") keeps its flag when it is
/// compiled for an MSIL triple.
static bool resetExceptionModel(Module &M) {
  SmallVector<Module::ModuleFlagEntry, 8> Flags;
  M.getModuleFlagsMetadata(Flags);
  StringRef None = getExceptionModelName(ExceptionHandling::None);
  for (const Module::ModuleFlagEntry &Flag : Flags) {
    if (Flag.Key->getString() != "exception-model")
      continue;
    if (auto *Model = dyn_cast_or_null<MDString>(Flag.Val);
        Model && Model->getString() == None)
      return false;
    M.setModuleFlag(Flag.Behavior, "exception-model",
                    MDString::get(M.getContext(), None));
    return true;
  }
  return false;
}

/// Removes the stack protector and SafeStack attributes of \p F with a
/// warning: the CIL runtime checks its own stack, and the native guard
/// variables and check functions do not exist for MSIL.
static bool stripStackProtector(Function &F) {
  static constexpr Attribute::AttrKind Kinds[] = {
      Attribute::StackProtect, Attribute::StackProtectStrong,
      Attribute::StackProtectReq, Attribute::SafeStack};
  bool Found = false;
  for (Attribute::AttrKind Kind : Kinds) {
    if (!F.hasFnAttribute(Kind))
      continue;
    F.removeFnAttr(Kind);
    Found = true;
  }
  if (Found)
    F.getContext().diagnose(DiagnosticInfoUnsupported(
        F, "stack protector is not supported for MSIL; attribute ignored",
        DiagnosticLocation(F.getSubprogram()), DS_Warning));
  return Found;
}

/// Removes the function entry instrumentation attributes of \p F
/// (-fpatchable-function-entry, -pg -mfentry, -mnop-mcount, -mrecord-mcount),
/// with a warning when they ask for instrumentation: a CIL method has no
/// native entry sequence to pad with nops or to call __fentry__ from.
static bool stripEntryInstrumentation(Function &F) {
  static constexpr StringLiteral Kinds[] = {
      "patchable-function-entry", "patchable-function-prefix", "fentry-call",
      "mnop-mcount", "mrecord-mcount"};
  bool Instrumented =
      F.getFnAttributeAsParsedInteger("patchable-function-entry") != 0 ||
      F.getFnAttributeAsParsedInteger("patchable-function-prefix") != 0 ||
      F.getFnAttribute("fentry-call").getValueAsString() == "true";
  bool Found = false;
  for (StringRef Kind : Kinds) {
    if (!F.hasFnAttribute(Kind))
      continue;
    F.removeFnAttr(Kind);
    Found = true;
  }
  if (Instrumented)
    F.getContext().diagnose(DiagnosticInfoUnsupported(
        F,
        "function entry instrumentation is not supported for MSIL; attribute "
        "ignored",
        DiagnosticLocation(F.getSubprogram()), DS_Warning));
  return Found;
}

/// Removes the function padding attributes of \p F ("tail-pad-to-size" and
/// "tail-pad-value", which the DynamicDebugging pass adds), with a warning
/// when it asks for padding, as for "patchable-function-entry": the
/// AsmPrinter would print a native fill directive into the method body, even
/// for "tail-pad-to-size"="0", so the attributes are always removed.
static bool stripTailPadding(Function &F) {
  static constexpr StringLiteral Kinds[] = {"tail-pad-to-size",
                                            "tail-pad-value"};
  bool Padded = F.getFnAttributeAsParsedInteger("tail-pad-to-size") != 0;
  bool Found = false;
  for (StringRef Kind : Kinds) {
    if (!F.hasFnAttribute(Kind))
      continue;
    F.removeFnAttr(Kind);
    Found = true;
  }
  if (Padded)
    F.getContext().diagnose(DiagnosticInfoUnsupported(
        F, "function padding is not supported for MSIL; attribute ignored",
        DiagnosticLocation(F.getSubprogram()), DS_Warning));
  return Found;
}

/// Removes the "zero-call-used-regs" attribute of \p F (-fzero-call-used-regs,
/// __attribute__((zero_call_used_regs))), with a warning when a compiled
/// definition asks for registers to be zeroed: a CIL method has no registers,
/// and PEI would take the virtual registers that RETURN reads for physical
/// ones. clang also puts the -fzero-call-used-regs value on declarations,
/// which are not compiled, so they get no warning.
static bool stripZeroCallUsedRegs(Function &F) {
  Attribute Attr = F.getFnAttribute("zero-call-used-regs");
  if (!Attr.isValid())
    return false;
  if (Attr.getValueAsString() != "skip" && !F.isDeclaration() &&
      !F.hasAvailableExternallyLinkage())
    F.getContext().diagnose(DiagnosticInfoUnsupported(
        F, "zero-call-used-regs is not supported for MSIL; attribute ignored",
        DiagnosticLocation(F.getSubprogram()), DS_Warning));
  F.removeFnAttr("zero-call-used-regs");
  return true;
}

/// Erases the pseudo probes of -fpseudo-probe-for-profiling, the
/// llvm.pseudoprobe calls and the !llvm.pseudo_probe_desc descriptors, with
/// a warning: MSIL has no pseudo probe sections for a profiler to read.
static bool stripPseudoProbes(Module &M) {
  NamedMDNode *Desc = M.getNamedMetadata(PseudoProbeDescMetadataName);
  Function *Probe =
      Intrinsic::getDeclarationIfExists(&M, Intrinsic::pseudoprobe);
  if (!Desc && (!Probe || Probe->use_empty()))
    return false;

  M.getContext().diagnose(DiagnosticInfoGeneric(
      "pseudo probes are not supported for MSIL; ignored", DS_Warning));
  if (Probe) {
    // An intrinsic is only ever called; its calls return void.
    for (User *U : make_early_inc_range(Probe->users()))
      if (auto *CI = dyn_cast<CallInst>(U))
        CI->eraseFromParent();
    if (Probe->use_empty())
      Probe->eraseFromParent();
  }
  if (Desc)
    M.eraseNamedMetadata(Desc);
  return true;
}

/// Reports every function definition with a garbage collection strategy and
/// deletes its body, then removes the strategy. MSIL emits no stack maps or
/// GC frame tables, so every strategy is an error: the AsmPrinter would print
/// the native frame tables of "erlang" and "ocaml" or crash, and the other
/// strategies ("shadow-stack", "statepoint-example", "coreclr") need a
/// collector that walks native frames. An available_externally body is never
/// compiled and is not reported, but GC lowering instantiates the strategy of
/// every function that has a body, and the AsmPrinter then prints its
/// tables, so it loses its strategy too. Adds the definitions that lost their
/// bodies to \p Deleted.
static bool diagnoseGCStrategies(Module &M,
                                 SmallVectorImpl<Function *> &Deleted) {
  bool Changed = false;
  for (Function &F : M) {
    if (F.isDeclaration() || !F.hasGC())
      continue;
    if (!F.hasAvailableExternallyLinkage()) {
      F.getContext().diagnose(DiagnosticInfoUnsupported(
          F,
          "garbage collection strategy '" + Twine(F.getGC()) +
              "' is not supported for MSIL",
          DiagnosticLocation(F.getSubprogram())));
      F.deleteBody();
      // A declaration may not be in a comdat.
      F.setComdat(nullptr);
      Deleted.push_back(&F);
    }
    F.clearGC();
    Changed = true;
  }
  return Changed;
}

/// Turns off the per-function metadata sections that the AsmPrinter writes
/// to native sections, which MSIL does not have (the AsmPrinter asserts on
/// the missing section): the call graph section (llc --call-graph-section,
/// clang -fexperimental-call-graph-section), the basic block address map
/// (llc --basic-block-address-map, clang -fbasic-block-sections=labels) and
/// PC sections (!pcsections on functions and instructions). Each is ignored
/// with one warning for the module. MachineFunction, BasicBlockSections,
/// SelectionDAG and the AsmPrinter read the options and the metadata only
/// when they compile a function, after this module pass has run.
static bool disableMetadataSections(Module &M, TargetMachine &TM) {
  LLVMContext &Ctx = M.getContext();
  if (TM.Options.EmitCallGraphSection) {
    Ctx.diagnose(DiagnosticInfoGeneric(
        "call graph section is not supported for MSIL; ignored", DS_Warning));
    TM.Options.EmitCallGraphSection = false;
  }
  if (TM.Options.BBAddrMap) {
    Ctx.diagnose(DiagnosticInfoGeneric(
        "basic block address map is not supported for MSIL; ignored",
        DS_Warning));
    TM.Options.BBAddrMap = false;
  }

  bool HasPCSections = false;
  for (Function &F : M) {
    HasPCSections |= F.eraseMetadata(LLVMContext::MD_pcsections);
    for (Instruction &I : instructions(F)) {
      if (!I.hasMetadata(LLVMContext::MD_pcsections))
        continue;
      I.setMetadata(LLVMContext::MD_pcsections, nullptr);
      HasPCSections = true;
    }
  }
  if (HasPCSections)
    Ctx.diagnose(DiagnosticInfoGeneric(
        "PC sections are not supported for MSIL; ignored", DS_Warning));
  return HasPCSections;
}

/// Reports \p F if its IL name is longer than ilasm accepts
/// (MSIL::MaxILNameBytes: Framework 4.8 and NuGet ilasm 10.0.12 report 'name
/// too long' for 1024 bytes or more, counting UTF-8 bytes after they decode
/// the \' and octal escapes of MSIL::printQuotedName, decision D11). The
/// diagnostic names the function in full, so the message gives only the
/// length. Returns true if it was reported.
static bool diagnoseILNameLength(const Function &F, const TargetMachine &TM) {
  // Unnamed functions get short generated names ('__unnamed_N').
  if (!F.hasName())
    return false;
  size_t Size = MSIL::getILName(F, TM).size();
  if (Size <= MSIL::MaxILNameBytes)
    return false;
  F.getContext().diagnose(DiagnosticInfoUnsupported(
      F,
      "identifier of " + Twine(Size) + " bytes is longer than the " +
          Twine(MSIL::MaxILNameBytes) + " bytes ilasm accepts",
      DiagnosticLocation(F.getSubprogram())));
  return true;
}

/// Reports every construct in the definition \p F that MSIL cannot compile.
/// Returns true if there was one.
static bool diagnoseDefinition(Function &F, AddrSpaceFinder &ASFinder,
                               FPTypeFinder &FPFinder,
                               ScalableOrTargetExtTypeFinder &STFinder) {
  LLVMContext &Ctx = F.getContext();
  DiagnosticLocation FnLoc(F.getSubprogram());
  bool HasError = false;

  if (F.isVarArg()) {
    Ctx.diagnose(DiagnosticInfoUnsupported(
        F, "variadic function definitions are not supported for MSIL yet",
        FnLoc));
    HasError = true;
  }

  // One diagnostic per definition for address spaces: at the signature (the
  // address space of the function itself is that of its pointer type), or
  // at the first instruction that uses one.
  DiagnosticLocation ASLoc = FnLoc;
  Type *ASTy = ASFinder.inType(F.getType());
  if (!ASTy)
    ASTy = ASFinder.inType(F.getFunctionType());

  // The same for floating-point types without a CIL type.
  DiagnosticLocation FPLoc = FnLoc;
  Type *FPTy = FPFinder.inType(F.getFunctionType());

  // And for scalable vector and target extension types.
  DiagnosticLocation STLoc = FnLoc;
  Type *STTy = STFinder.inType(F.getFunctionType());

  // One diagnostic per definition for preallocated calls too: each one comes
  // with an llvm.call.preallocated.setup call and usually .arg calls.
  bool HasPreallocated = false;

  // And for the frame escape intrinsics: clang emits several for one __try,
  // and inlining a funclet copies its llvm.localrecover calls.
  bool HasFrameEscape = false;

  // And for llvm.thread.pointer.
  bool HasThreadPointer = false;

  // Each target intrinsic is reported once per definition, at its first call:
  // an unrolled loop or the inlined wrappers of immintrin.h call one many
  // times.
  SmallPtrSet<const Function *, 4> ReportedIntrinsics;

  for (Instruction &I : instructions(F)) {
    if (auto *CB = dyn_cast<CallBase>(&I)) {
      if (CB->isInlineAsm()) {
        Ctx.diagnose(DiagnosticInfoUnsupported(
            F, "inline assembly is not supported for MSIL", I.getDebugLoc()));
        HasError = true;
      } else if (Intrinsic::ID IID = CB->getIntrinsicID();
                 IID == Intrinsic::read_register ||
                 IID == Intrinsic::read_volatile_register ||
                 IID == Intrinsic::write_register) {
        Ctx.diagnose(DiagnosticInfoUnsupported(
            F,
            Twine("named register access (") + Intrinsic::getBaseName(IID) +
                ") is not supported for MSIL",
            I.getDebugLoc()));
        HasError = true;
      } else if (IID == Intrinsic::thread_pointer) {
        // __builtin_thread_pointer reads the native thread pointer (%fs:0 on
        // x86_64 Linux, TPIDR_EL0 on aarch64), which CIL has no equivalent
        // for. It is a generic intrinsic, but SelectionDAGBuilder makes an
        // INTRINSIC_WO_CHAIN node of the call, which only a target with a
        // lowering for it can select. Thread-local storage comes with
        // phase 3 (Design.md 4.4). Reported once per definition: an inlined
        // accessor such as a static libc's pthread_self calls it many times.
        if (!HasThreadPointer)
          Ctx.diagnose(DiagnosticInfoUnsupported(
              F,
              Twine("thread pointer access ('") + Intrinsic::getBaseName(IID) +
                  "') is not supported for MSIL yet (phase 3 TLS)",
              I.getDebugLoc()));
        HasThreadPointer = true;
        HasError = true;
      } else if (IID == Intrinsic::experimental_stackmap ||
                 IID == Intrinsic::experimental_patchpoint_void ||
                 IID == Intrinsic::experimental_patchpoint ||
                 IID == Intrinsic::experimental_gc_statepoint) {
        // Stack maps describe native frames to a runtime that reads or
        // patches machine code. A statepoint is valid only in a function
        // with a gc (the IR verifier checks it), so diagnoseGCStrategies
        // reports it first while every strategy is an error.
        Ctx.diagnose(DiagnosticInfoUnsupported(
            F,
            Twine("stack maps (") + Intrinsic::getBaseName(IID) +
                ") are not supported for MSIL",
            I.getDebugLoc()));
        HasError = true;
      } else if (IID == Intrinsic::localescape ||
                 IID == Intrinsic::localrecover ||
                 IID == Intrinsic::localaddress ||
                 IID == Intrinsic::eh_recoverfp) {
        // An outlined SEH funclet (a C __finally block or __except filter,
        // -fms-extensions) reaches the locals of its parent through the
        // parent's native frame pointer and the frame offsets that the
        // AsmPrinter publishes as symbols. MSIL has no exception handling in
        // Phase 1 and no lowering for these intrinsics: llvm.localescape
        // would reach the AsmPrinter, and the others instruction selection.
        // The parent (localescape, localaddress) and its funclets
        // (localrecover, eh.recoverfp) each lose their bodies.
        if (!HasFrameEscape)
          Ctx.diagnose(DiagnosticInfoUnsupported(
              F,
              Twine("frame escape ('") + Intrinsic::getBaseName(IID) +
                  "') is not supported for MSIL",
              I.getDebugLoc()));
        HasFrameEscape = true;
        HasError = true;
      } else if (CB->hasDeoptState() ||
                 IID == Intrinsic::experimental_deoptimize) {
        // SelectionDAGBuilder lowers a call or invoke with deoptimization
        // state as a statepoint, which needs the native call sequence;
        // llvm.experimental.deoptimize also calls a runtime function, and
        // llvm.experimental.guard has no lowering at all. The IR verifier
        // requires a "deopt" bundle on both intrinsics; deoptimize is also
        // matched by name for IR that was not verified.
        Ctx.diagnose(DiagnosticInfoUnsupported(
            F,
            "deoptimization state (\"deopt\" operand bundles) is not "
            "supported for MSIL",
            I.getDebugLoc()));
        HasError = true;
      } else if (IID == Intrinsic::call_preallocated_setup ||
                 IID == Intrinsic::call_preallocated_arg ||
                 IID == Intrinsic::call_preallocated_teardown ||
                 CB->countOperandBundlesOfType(LLVMContext::OB_preallocated)) {
        // LowerCall rejects the preallocated argument, but the setup and arg
        // calls would still reach instruction selection.
        if (!HasPreallocated)
          Ctx.diagnose(DiagnosticInfoUnsupported(
              F, "preallocated arguments are not supported for MSIL",
              I.getDebugLoc()));
        HasPreallocated = true;
        HasError = true;
      } else if (IID == Intrinsic::get_fpenv || IID == Intrinsic::set_fpenv ||
                 IID == Intrinsic::get_fpmode || IID == Intrinsic::set_fpmode) {
        // The environment value of these calls, whatever its width, goes
        // through a stack temporary: SelectionDAGBuilder makes one for
        // GET_FPENV_MEM or SET_FPENV_MEM (GET_FPENV and SET_FPENV are not
        // legal), LegalizeDAG one for GET_FPMODE or SET_FPMODE, and
        // LegalizeDAG passes it to fegetenv, fesetenv, fegetmode or
        // fesetmode. The temporary is an ordinary frame object, so nothing
        // crashes without this check, but LowerCall's diagnostic would name
        // that library call instead of the floating-point environment access
        // the program asked for. reset.fpenv and reset.fpmode pass no
        // temporary, and LowerCall's diagnostic covers them.
        Ctx.diagnose(DiagnosticInfoUnsupported(
            F,
            Twine("floating-point environment access ('") +
                Intrinsic::getBaseName(IID) + "') is not supported for MSIL",
            I.getDebugLoc()));
        HasError = true;
      } else if (Intrinsic::isTargetIntrinsic(IID) ||
                 IID == Intrinsic::vscale ||
                 (IID == Intrinsic::experimental_get_vector_length &&
                  cast<ConstantInt>(CB->getArgOperand(2))->isOne())) {
        // MSIL has no target intrinsics of its own, and Phase 1 maps none of
        // another target's (llvm.x86.* from immintrin.h, llvm.aarch64.* from
        // arm_acle.h and arm_neon.h) to CIL: without the codegen switches of
        // phase 3 they are rejected (Design.md 3.2). SelectionDAGBuilder
        // would make an INTRINSIC_* node of the call, which instruction
        // selection cannot select. Its calls go with the deleted body, and
        // the declaration that stays is skipped like every intrinsic: it is
        // never a P/Invoke or a Phase-1 import. The same goes for the
        // generic intrinsics that read vscale (see
        // ScalableOrTargetExtTypeFinder), llvm.vscale and
        // llvm.experimental.get.vector.length with a true third operand
        // (the scalable form): no scalable type is in their signatures, and
        // SelectionDAGBuilder makes an ISD::VSCALE node of each, which
        // instruction selection cannot select either.
        const Function *Callee = CB->getCalledFunction();
        if (ReportedIntrinsics.insert(Callee).second)
          Ctx.diagnose(DiagnosticInfoUnsupported(
              F,
              Twine("intrinsic '") + Callee->getName() +
                  "' is not supported for MSIL",
              I.getDebugLoc()));
        HasError = true;
      }
    }
    if (!ASTy && (ASTy = ASFinder.inInstruction(I)))
      ASLoc = DiagnosticLocation(I.getDebugLoc());
    if (!FPTy && (FPTy = FPFinder.inInstruction(I)))
      FPLoc = DiagnosticLocation(I.getDebugLoc());
    if (!STTy && (STTy = STFinder.inInstruction(I)))
      STLoc = DiagnosticLocation(I.getDebugLoc());
  }

  if (ASTy) {
    std::string Msg =
        ("pointers in address space " + Twine(ASTy->getPointerAddressSpace()) +
         " are not supported for MSIL")
            .str();
    Ctx.diagnose(DiagnosticInfoUnsupported(F, Msg, ASLoc));
    HasError = true;
  }

  if (FPTy) {
    std::string Msg;
    raw_string_ostream(Msg)
        << "floating-point type '" << *FPTy << "' is not supported for MSIL";
    Ctx.diagnose(DiagnosticInfoUnsupported(F, Msg, FPLoc));
    HasError = true;
  }

  if (STTy) {
    Ctx.diagnose(DiagnosticInfoUnsupported(
        F, getScalableOrTargetExtTypeMessage(STTy), STLoc));
    HasError = true;
  }
  return HasError;
}

/// Reports each function in \p Decls whose address is still taken and adds
/// it to \p Reported. \p Decls are the used declarations (and
/// available_externally definitions) whose signatures have a scalable vector
/// or target extension type. The AsmPrinter computes the CIL signature of an
/// undefined function whose address is taken (for its 'ldftn', its address
/// field or a constructor call), and these types have no CIL type. Their
/// calls need no check: a call with the declaration's type has the type in
/// its operands or its result, so its caller is reported and loses its body,
/// and MSILFixFunctionBitcasts replaces a call with another type by a call of
/// a trapping wrapper. A use in llvm.used or llvm.compiler.used takes no
/// signature.
static bool
diagnoseAddressTakenDeclarations(ArrayRef<Function *> Decls,
                                 ScalableOrTargetExtTypeFinder &STFinder,
                                 SmallVectorImpl<Function *> &Reported) {
  bool Changed = false;
  for (Function *F : Decls) {
    if (!F->hasAddressTaken(/*PutOffender=*/nullptr,
                            /*IgnoreCallbackUses=*/false,
                            /*IgnoreAssumeLikeCalls=*/false,
                            /*IgnoreLLVMUsed=*/true,
                            /*IgnoreARCAttachedCall=*/false,
                            /*IgnoreCastedDirectCall=*/true))
      continue;
    std::string Msg = getScalableOrTargetExtTypeMessage(
        STFinder.inType(F->getFunctionType()));
    F->getContext().diagnose(DiagnosticInfoUnsupported(
        *F, Msg, DiagnosticLocation(F->getSubprogram())));
    Reported.push_back(F);
    Changed = true;
  }
  return Changed;
}

/// Erases every call and invoke whose callee is one of \p Deleted, the
/// definitions whose bodies were deleted after an error (and the declarations
/// reported for their signatures): they are not compiled, so a call of one
/// would be lowered as a call of an external function. The uses of each call
/// become poison, and an invoke branches to its normal destination. The other
/// uses of the functions, their addresses, are replaced by poisonAddressesOf.
static bool eraseCallsOf(ArrayRef<Function *> Deleted) {
  // Collect the calls first: erasing one removes uses of the functions, and
  // erasing an invoke may delete PHI nodes in its unwind destination.
  SmallVector<CallBase *, 4> Calls;
  for (Function *F : Deleted)
    for (Use &U : F->uses())
      if (auto *CB = dyn_cast<CallBase>(U.getUser());
          CB && isa<CallInst, InvokeInst>(CB) && CB->isCallee(&U))
        Calls.push_back(CB);

  for (CallBase *CB : Calls) {
    if (auto *II = dyn_cast<InvokeInst>(CB)) {
      auto *BI = UncondBrInst::Create(II->getNormalDest(), II->getIterator());
      BI->setDebugLoc(II->getDebugLoc());
      II->getUnwindDest()->removePredecessor(II->getParent());
    }
    if (!CB->use_empty())
      CB->replaceAllUsesWith(PoisonValue::get(CB->getType()));
    CB->eraseFromParent();
  }
  return !Calls.empty();
}

/// Returns true if instruction selection makes \p I the TRAP terminator: a
/// call of llvm.trap, llvm.debugtrap or llvm.ubsantrap without a
/// "trap-func-name" attribute, which SelectionDAGBuilder would lower as an
/// ordinary call of that function instead.
static bool isTrapTerminator(const Instruction &I) {
  const auto *CI = dyn_cast<CallInst>(&I);
  if (!CI)
    return false;
  Intrinsic::ID IID = CI->getIntrinsicID();
  return (IID == Intrinsic::trap || IID == Intrinsic::debugtrap ||
          IID == Intrinsic::ubsantrap) &&
         CI->getAttributes()
             .getFnAttr("trap-func-name")
             .getValueAsString()
             .empty();
}

/// Ends every block of \p F at its first trap. TRAP ('ldnull; throw') is a
/// terminator and a barrier, so nothing after it runs in Phase 1, not even
/// after llvm.debugtrap, which is not noreturn but becomes TRAP too (its
/// default Expand action). Code that stayed after the trap would be erased by
/// MSILCleanCodeAfterTrap while the block keeps its successor edges, and the
/// values it defines would be read without a def in the successors.
/// changeToUnreachable on the instruction after the trap erases the rest of
/// the block and the edges, and removes the block from its successors' PHI
/// nodes; UnreachableBlockElimination then drops the blocks that only the
/// trap reached. Returns true if a block changed.
static bool endBlocksAtTraps(Function &F) {
  // Collect the first trap of each block first: changeToUnreachable erases
  // the rest of its block, later traps included.
  SmallVector<Instruction *, 4> Traps;
  for (BasicBlock &BB : F)
    for (Instruction &I : BB) {
      if (!isTrapTerminator(I))
        continue;
      // A call is never the terminator of its block.
      if (!isa<UnreachableInst>(I.getNextNode()))
        Traps.push_back(&I);
      break;
    }

  for (Instruction *Trap : Traps)
    changeToUnreachable(Trap->getNextNode());
  return !Traps.empty();
}

/// Reports a non-empty llvm.global_dtors and erases it. Destructors need an
/// exit hook, AppDomain.ProcessExit (phase 3, Design.md 4.1).
static bool diagnoseGlobalDtors(Module &M) {
  GlobalVariable *Dtors = M.getNamedGlobal("llvm.global_dtors");
  if (!Dtors || !Dtors->hasInitializer())
    return false;
  Constant *Init = Dtors->getInitializer();
  if (auto *ATy = dyn_cast<ArrayType>(Init->getType());
      ATy && ATy->getNumElements() == 0)
    return false;

  bool Named = false;
  if (auto *Entries = dyn_cast<ConstantArray>(Init))
    for (const Use &Entry : Entries->operands())
      if (auto *CS = dyn_cast<ConstantStruct>(Entry.get());
          CS && CS->getNumOperands() >= 2)
        if (auto *Fn =
                dyn_cast<GlobalValue>(CS->getOperand(1)->stripPointerCasts())) {
          M.getContext().emitError("global destructor " + MSIL::getIRName(*Fn) +
                                   " is not supported for MSIL yet");
          Named = true;
        }
  if (!Named)
    M.getContext().emitError("global destructors (llvm.global_dtors) are not "
                             "supported for MSIL yet");
  Dtors->eraseFromParent();
  return true;
}

/// llvm.threadlocal.address only takes a thread-local global value: replace
/// each call of it on \p GV by \p Replacement, before GV stops being one.
static void replaceThreadLocalAddressCalls(GlobalValue &GV,
                                           Constant *Replacement) {
  for (User *U : make_early_inc_range(GV.users()))
    if (auto *II = dyn_cast<IntrinsicInst>(U);
        II && II->getIntrinsicID() == Intrinsic::threadlocal_address) {
      II->replaceAllUsesWith(Replacement);
      II->eraseFromParent();
    }
}

/// no_cfi and dso_local_equivalent constants only take a global value as
/// their operand: replace each one on \p GV by poison and destroy it, before
/// GV itself is replaced by poison.
static void replaceGlobalValueWrappers(GlobalValue &GV) {
  // Collect them first: replacing them changes the use list of GV.
  SmallVector<Constant *, 2> Wrappers;
  for (User *U : GV.users())
    if (isa<NoCFIValue, DSOLocalEquivalent>(U))
      Wrappers.push_back(cast<Constant>(U));
  for (Constant *C : Wrappers) {
    C->replaceAllUsesWith(PoisonValue::get(C->getType()));
    C->destroyConstant();
  }
}

/// Replaces every remaining use of the functions in \p Deleted, the
/// definitions whose bodies were deleted after an error (and the declarations
/// reported for their signatures), by poison: their addresses in code, in
/// global initializers (constant expressions included) and in
/// llvm.global_ctors. The functions are not compiled, so the AsmPrinter would
/// take each address for that of an external function: an 'undefined
/// function' error after the real one, or a P/Invoke declaration for a
/// function with the name of a Phase-1 import. The functions stay in the
/// module as declarations, and metadata keeps naming them: the IR verifier
/// runs again after this pass and accepts only a function (or null) in a "CG
/// Profile" module flag entry.
static bool poisonAddressesOf(Module &M, ArrayRef<Function *> Deleted) {
  SmallPtrSet<const Constant *, 4> AddressTaken;
  for (Function *F : Deleted)
    if (!F->use_empty())
      AddressTaken.insert(F);
  if (AddressTaken.empty())
    return false;

  // llvm.used and llvm.compiler.used may only list global values.
  removeFromUsedLists(M, [&](Constant *C) { return AddressTaken.contains(C); });
  for (Function *F : Deleted) {
    if (!AddressTaken.contains(F))
      continue;
    replaceGlobalValueWrappers(*F);
    F->replaceNonMetadataUsesWith(PoisonValue::get(F->getType()));
  }
  return true;
}

/// Replaces every use of the global values in \p ToErase by poison and erases
/// them.
static void eraseGlobalValues(Module &M, ArrayRef<GlobalValue *> ToErase) {
  // llvm.used and llvm.compiler.used may only list global values.
  SmallPtrSet<const Constant *, 4> Erased(ToErase.begin(), ToErase.end());
  removeFromUsedLists(M, [&](Constant *C) { return Erased.contains(C); });
  for (GlobalValue *GV : ToErase) {
    Constant *Poison = PoisonValue::get(GV->getType());
    replaceThreadLocalAddressCalls(*GV, Poison);
    replaceGlobalValueWrappers(*GV);
    GV->replaceAllUsesWith(Poison);
  }
  for (GlobalValue *GV : ToErase)
    GV->eraseFromParent();
}

/// After an unsupported target triple nothing in \p M can be compiled:
/// deletes every function body and erases every alias, ifunc and global
/// variable, except the special llvm.* globals that hold no program data
/// (MSIL::isSpecialLLVMGlobal: llvm.used and the like), so that only the
/// module header is printed. A user global with an llvm.* name (a C asm label
/// can spell one) is program data and is erased.
static void dropModuleContents(Module &M) {
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    F.deleteBody();
    // A declaration may not be in a comdat.
    F.setComdat(nullptr);
  }

  SmallVector<GlobalValue *, 8> ToErase;
  for (GlobalVariable &GV : M.globals()) {
    // The constructor and destructor lists name functions that are no longer
    // compiled.
    StringRef Name = GV.getName();
    if (!MSIL::isSpecialLLVMGlobal(GV) || Name == "llvm.global_ctors" ||
        Name == "llvm.global_dtors")
      ToErase.push_back(&GV);
  }
  for (GlobalAlias &GA : M.aliases())
    ToErase.push_back(&GA);
  for (GlobalIFunc &GI : M.ifuncs())
    ToErase.push_back(&GI);
  eraseGlobalValues(M, ToErase);
}

// AsmPrinter::doInitialization creates a pseudo probe handler, which prints
// .pseudoprobe directives into the method bodies, when the module has
// !llvm.pseudo_probe_desc, and GC lowering's doInitialization instantiates
// the strategy of every function with a gc, for which the AsmPrinter then
// creates a GC metadata printer. runOnModule would be too late: the legacy
// pass manager initializes every pass before it runs any. It initializes them
// in pipeline order, so this module pass is initialized before the function
// pass manager that holds GC lowering and the AsmPrinter.
bool MSILPrepareIRLegacy::doInitialization(Module &M) {
  DeletedBodies.clear();
  bool Changed = stripPseudoProbes(M);
  Changed |= diagnoseGCStrategies(M, DeletedBodies);
  return Changed;
}

bool MSILPrepareIRLegacy::runOnModule(Module &M) {
  LLVMContext &Ctx = M.getContext();

  // Check the TargetMachine, not the module: it decides what is compiled and
  // supplies the DataLayout, and its triple is the one that -march=msil
  // switched to the MSIL object format.
  auto &TM = getAnalysis<TargetPassConfig>().getTM<TargetMachine>();
  if (!isSupportedTarget(TM)) {
    Ctx.emitError("unsupported target triple '" + TM.getTargetTriple().str() +
                  "' for MSIL (Phase 1 supports x86_64 and aarch64 with "
                  "64-bit pointers)");
    dropModuleContents(M);
    return true;
  }

  bool Changed = resetExceptionModel(M);
  Changed |= disableMetadataSections(M, TM);

  AddrSpaceFinder ASFinder;
  FPTypeFinder FPFinder;
  ScalableOrTargetExtTypeFinder STFinder;
  // The used declarations whose signatures have a scalable vector or target
  // extension type, checked once every diagnosed definition has lost its
  // body (and its uses of them). The definitions that lost their bodies in
  // doInitialization are declarations now, and their uses are replaced
  // anyway.
  SmallVector<Function *, 4> STDecls;
  for (Function &F : M) {
    Changed |= stripStackProtector(F);
    Changed |= stripEntryInstrumentation(F);
    Changed |= stripTailPadding(F);
    Changed |= stripZeroCallUsedRegs(F);
    // An available_externally body is never code-generated (see
    // MachineFunctionPass::runOnFunction): only its declaration matters. The
    // name of a used declaration is printed in references to it.
    if (F.isDeclaration() || F.hasAvailableExternallyLinkage()) {
      if (!F.isIntrinsic() && !F.use_empty()) {
        diagnoseILNameLength(F, TM);
        if (STFinder.inType(F.getFunctionType()) &&
            !is_contained(DeletedBodies, &F))
          STDecls.push_back(&F);
      }
      continue;
    }
    bool HasError = diagnoseILNameLength(F, TM);
    HasError |= diagnoseDefinition(F, ASFinder, FPFinder, STFinder);
    if (!HasError) {
      Changed |= endBlocksAtTraps(F);
      continue;
    }
    F.deleteBody();
    // A declaration may not be in a comdat.
    F.setComdat(nullptr);
    DeletedBodies.push_back(&F);
    Changed = true;
  }

  // Every definition that loses its body is known now, and so is every
  // declaration whose address is still taken.
  Changed |= diagnoseAddressTakenDeclarations(STDecls, STFinder, DeletedBodies);
  Changed |= eraseCallsOf(DeletedBodies);

  Changed |= diagnoseGlobalDtors(M);

  // After diagnoseGlobalDtors, which names the destructors it reports.
  Changed |= poisonAddressesOf(M, DeletedBodies);

  // The global values to replace by poison and erase.
  SmallVector<GlobalValue *, 4> ToErase;
  for (GlobalVariable &GV : M.globals()) {
    // llvm.used and the other special globals hold no program data; a user
    // global with an llvm.* name (a C asm label) does, and is checked.
    if (MSIL::isSpecialLLVMGlobal(GV))
      continue;
    if (unsigned AS = GV.getAddressSpace()) {
      Ctx.emitError("global variable " + MSIL::getIRName(GV) +
                    " in address space " + Twine(AS) +
                    " is not supported for MSIL");
      ToErase.push_back(&GV);
    } else if (Type *Ty = GV.hasInitializer()
                              ? STFinder.inValue(GV.getInitializer())
                              : STFinder.inType(GV.getValueType())) {
      // The IR verifier rejects scalable types in globals, but not every
      // target extension type (wasm.externref, spirv.*, dx.*, ...).
      Ctx.emitError("global variable " + MSIL::getIRName(GV) + ": " +
                    getScalableOrTargetExtTypeMessage(Ty));
      ToErase.push_back(&GV);
    } else if (GV.isThreadLocal()) {
      Ctx.emitError("thread-local variable " + MSIL::getIRName(GV) +
                    " is not supported for MSIL yet");
      replaceThreadLocalAddressCalls(GV, &GV);
      GV.setThreadLocal(false);
      Changed = true;
    }
  }
  for (GlobalAlias &GA : M.aliases()) {
    Ctx.emitError("alias " + MSIL::getIRName(GA) +
                  " is not supported for MSIL");
    ToErase.push_back(&GA);
  }
  for (GlobalIFunc &GI : M.ifuncs()) {
    Ctx.emitError("ifunc " + MSIL::getIRName(GI) +
                  " is not supported for MSIL");
    ToErase.push_back(&GI);
  }
  if (ToErase.empty())
    return Changed;

  eraseGlobalValues(M, ToErase);
  return true;
}
