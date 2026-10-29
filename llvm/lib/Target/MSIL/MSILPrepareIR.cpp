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
///
/// Then, before any function is checked, the module is lowered as the whole
/// program, which it is in -S output, in llc's and in msil-ld's
/// --lto-emit-asm text (lowerWholeProgramSymbols, decision D26 as amended for
/// Phase 2):
/// - An alias of a defined function (offset 0) or of a global variable (any
///   constant offset) is replaced by its aliasee in every use and erased. A
///   function alias with non-local linkage leaves a forwarding definition
///   under its name, which calls the aliasee.
/// - An extern_weak function declaration that has no import (in a standalone
///   module a Phase-1 import, MSIL::getPInvokeModule; in msil-ld's linked
///   text an "msil-import-module" attribute or an !msil.imports entry,
///   MSIL::getLinkedImport) is null: its calls, invokes and llvm.global_ctors
///   entries call an internal trap stub '<msil>undef_<name>', and its other
///   uses become null. An extern_weak variable is null in every use and is
///   erased, unless it is thread-local (an error below), or, off Windows, a
///   __start_<X> or __stop_<X> variable while a global variable definition
///   is in section <X>: the AsmPrinter defines it at that section's start or
///   end.
/// A section on a global variable definition is laid out as a one-object
/// link lays it out (plan 5.8.10, 5.9.9: MSILDataSegment and the AsmPrinter
/// group named sections, define __start_/__stop_ symbols and run
/// constructor-table slots), so it is no longer diagnosed here (M20).
///
/// In object mode (MSILTargetMachine::isObjectMode(): a CIL object or its
/// object-form text, plan 5.8.10) the module is one object of a link instead,
/// so neither lowering applies: aliases, extern_weak declarations,
/// external globals and sections stay symbols of the object
/// (checkObjectSymbols). It reports instead:
/// - an alias that is not a definition plus a constant offset (a function
///   definition at offset 0, or a global variable definition that is not
///   common, with the alias's bytes inside it), which is erased;
/// - a comdat whose selection kind is not any or nodeduplicate, whose members
///   leave it;
/// - a symbol whose IL name starts with '<msil>', which the object's
///   scaffolding reserves (rule R5): a definition loses its body or is erased,
///   and the calls and the other uses of a declaration go as for a definition
///   that lost its body;
/// - an alias, a global variable definition or a used global variable
///   declaration whose IL name is longer than ilasm accepts, which a CIL
///   object cannot hold in its rows either: it is erased;
/// - a used function declaration whose IL name (as below), whose
///   "msil-import-module" or whose "msil-import-name" string is longer than
///   that (a CIL object's ModuleRef and ImplMap import names): its calls and
///   its other uses go as for a definition that lost its body, so it gets no
///   import stub.
/// The errors for llvm.global_dtors, thread_local, ifuncs, module asm and
/// variadic definitions stay, and an alias goes with the definition it
/// aliases when that one is erased or loses its body. The prefix data,
/// prologue data and function sanitizer metadata of a compiled definition are
/// reported here and removed, so that its method stays for the calls that
/// name it (whole-program mode reports them in the AsmPrinter, which skips
/// the method).
///
/// Then the discardable function definitions that the program cannot reach
/// lose their bodies, decided by reachability as GlobalDCE does
/// (dropUnreferencedDiscardableFunctions): the candidates are the linkonce
/// and linkonce_odr definitions and the internal and private definitions
/// their bodies refer to, directly or through other candidates; a candidate
/// is live when something other than a candidate's body uses it (a function
/// that is not a candidate, a global initializer, llvm.used,
/// llvm.global_ctors, any other global) or a live candidate's body refers to
/// it, and every other candidate is dropped, dead cycles included. Global
/// variables and the local functions that no linkonce function reaches stay.
/// A definition from a header that the program does not use is not compiled,
/// and therefore not diagnosed. Then, in each function definition, an empty
/// inline asm statement with operands (a template of nothing but whitespace
/// and /* */ comments, as Rust's optimization barriers write it, is empty)
/// becomes a compiler fence, a fake use of
/// each input and llvm.sideeffect; an indirect output tied to a by-value
/// input gets the input stored through its pointer (truncated when it is a
/// wider integer), and each direct output becomes its tied input, converted
/// by zext, inttoptr or ptrtoint where clang ties different types
/// (lowerEmptyInlineAsm), before its checks. The checks and what follows
/// them:
/// - A function definition that is variadic, calls other inline assembly, has
///   a va_arg instruction of a struct or array type (which the IR verifier
///   rejects since upstream 74a120bff99f, so only IR that skipped
///   verification has one), calls a target intrinsic (llvm.x86.*,
///   llvm.aarch64.*, ...), accesses a named register, reads the thread
///   pointer (llvm.thread.pointer), records a stack map (stackmap,
///   patchpoint, statepoint), has deoptimization state
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
/// - An alias that is not lowered (above), an ifunc, a global variable in a
///   non-zero address space or one whose type has a target extension type is
///   an error; its uses become poison and it is erased.
/// - A thread-local variable is an error; it becomes an ordinary global.
/// - A non-empty llvm.global_dtors is an error, and it is erased.
/// - An "exception-model" module flag is reset to "none": MSIL has no
///   exception model (decision D8). MSILAsmPrinter has already reset it when
///   this pass runs in the full pipeline (MSIL::resetExceptionModel).
///
/// Module-level inline assembly is diagnosed by MSILAsmPrinter: the legacy
/// pass manager initializes the AsmPrinter before any pass runs. It
/// initializes the passes in pipeline order, so this pass's doInitialization
/// still runs before the AsmPrinter's.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "MSILTargetMachine.h"
#include "MSILUtilities.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
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

/// Object mode: reports the prefix data, the prologue data and the
/// -fsanitize=function signature (!func_sanitize) of the compiled definition
/// \p F, which AsmPrinter would print as raw data in front of the method, and
/// removes them. The method stays, so that the object's calls of it still
/// name a method of the object (plan section 2). Whole-program mode reports
/// the same error in MSILAsmPrinter::runOnMachineFunction, which skips the
/// method; that output is never assembled.
static bool stripFunctionData(Function &F) {
  if (!F.hasPrefixData() && !F.hasPrologueData() &&
      !F.getMetadata(LLVMContext::MD_func_sanitize))
    return false;
  F.getContext().diagnose(DiagnosticInfoUnsupported(
      F, "prefix data, prologue data and function sanitizer metadata are "
         "not supported for MSIL"));
  F.setPrefixData(nullptr);
  F.setPrologueData(nullptr);
  F.setMetadata(LLVMContext::MD_func_sanitize, nullptr);
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

/// Object mode: reports \p GV, an alias or global variable that the object
/// names in its rows, if its IL name is longer than ilasm accepts: the name
/// of a MemberRef or FieldDef row, which a CIL object holds no longer than
/// that either (cli::MetadataBuilder). Returns true if it was reported.
static bool diagnoseObjectILNameLength(const GlobalValue &GV,
                                       const TargetMachine &TM) {
  size_t Size = MSIL::getILName(GV, TM).size();
  if (Size <= MSIL::MaxILNameBytes)
    return false;
  GV.getContext().emitError(
      (isa<GlobalVariable>(GV) ? "global variable " : "") +
      MSIL::getIRName(GV) + ": identifier of " + Twine(Size) +
      " bytes is longer than the " + Twine(MSIL::MaxILNameBytes) +
      " bytes ilasm accepts");
  return true;
}

/// Object mode: reports \p F, a used function declaration, for each string
/// of its import stubs (plan 5.8.3) that is longer than a CIL object's
/// metadata strings can be (MSIL::MaxILNameBytes, as cli::MetadataBuilder
/// checks them): "msil-import-module" names the stubs' ModuleRef, and
/// "msil-import-name" is their ImplMap import name. Once per declaration,
/// however many stubs its calls give it. Returns true if one was reported.
static bool diagnoseImportStrings(const Function &F) {
  bool Reported = false;
  for (auto [Attr, What] :
       {std::pair<StringRef, StringRef>("msil-import-module", "module"),
        std::pair<StringRef, StringRef>("msil-import-name", "name")}) {
    size_t Size = F.getFnAttribute(Attr).getValueAsString().size();
    if (Size <= MSIL::MaxILNameBytes)
      continue;
    F.getContext().emitError(
        "function " + MSIL::getIRName(F) + ": import " + What + " of " +
        Twine(Size) + " bytes is longer than the " +
        Twine(MSIL::MaxILNameBytes) + " bytes an MSIL object allows");
    Reported = true;
  }
  return Reported;
}

/// Returns the type of the direct output \p OutNo of the inline asm call
/// \p CI: one direct output is the result, several are the fields of a
/// struct.
static Type *getDirectOutputType(const CallInst &CI, unsigned OutNo,
                                 unsigned NumDirect) {
  Type *RetTy = CI.getType();
  return NumDirect == 1 ? RetTy
                        : cast<StructType>(RetTy)->getElementType(OutNo);
}

/// Returns true if an empty asm statement leaves in a direct output of type
/// \p OutTy the value of its tied input of type \p InTy, as the native code
/// does, and convertTiedInput can give it: the types are equal; the input is
/// an integer narrower than the integer output, which then holds the input
/// zero-extended (clang ties an i1 input to an i8 output for `"+r"` on a
/// _Bool); or one is a pointer and the other an integer of the pointer's
/// width, which hold the same bits (`"=r"(Ptr) : "0"(Int)`; clang widens the
/// narrower side of other integer and pointer ties itself). An input wider
/// than its output, and every other pair, has no such value.
static bool isConvertibleTie(Type *InTy, Type *OutTy, const DataLayout &DL) {
  if (InTy == OutTy)
    return true;
  if (InTy->isIntegerTy() && OutTy->isIntegerTy())
    return InTy->getIntegerBitWidth() < OutTy->getIntegerBitWidth();
  if (InTy->isPointerTy() && OutTy->isIntegerTy())
    return OutTy->getIntegerBitWidth() == DL.getPointerTypeSizeInBits(InTy);
  if (InTy->isIntegerTy() && OutTy->isPointerTy())
    return InTy->getIntegerBitWidth() == DL.getPointerTypeSizeInBits(OutTy);
  return false;
}

/// Returns the value of type \p OutTy that an empty asm statement leaves in a
/// direct output tied to the input \p In (isConvertibleTie), inserting a cast
/// with the debug location \p DL before \p IP when the types differ.
static Value *convertTiedInput(Value *In, Type *OutTy, BasicBlock::iterator IP,
                               const DebugLoc &DL) {
  Type *InTy = In->getType();
  if (InTy == OutTy)
    return In;
  Instruction::CastOps Op = InTy->isPointerTy()    ? Instruction::PtrToInt
                            : OutTy->isPointerTy() ? Instruction::IntToPtr
                                                   : Instruction::ZExt;
  auto *Cast = CastInst::Create(Op, In, OutTy, "", IP);
  Cast->setDebugLoc(DL);
  return Cast;
}

/// Returns the index in \p Constraints of the input that the direct output
/// \p Out, a local register variable, is tied to, or -1: the last input not
/// in \p Used whose constraint is exactly \p Out's own register. For `"+r"`
/// on `register int x asm("eax")` clang emits `"={eax},{eax}"` instead of a
/// numeric tie, and appends that input after the statement's own inputs (a
/// `"r"(x)` among them has the same constraint); clang rejects two outputs in
/// one register, and ties an early-clobber one by number. An empty asm
/// statement leaves the input's value in the register.
static int findRegisterTie(const InlineAsm::ConstraintInfoVector &Constraints,
                           const InlineAsm::ConstraintInfo &Out,
                           ArrayRef<bool> Used) {
  if (Out.isEarlyClobber || Out.isMultipleAlternative ||
      Out.Codes.size() != 1 || !StringRef(Out.Codes.front()).starts_with("{"))
    return -1;
  for (int I = static_cast<int>(Constraints.size()) - 1; I >= 0; --I) {
    const InlineAsm::ConstraintInfo &In = Constraints[I];
    if (In.Type == InlineAsm::isInput && !In.isIndirect &&
        !In.isMultipleAlternative && !Used[I] && In.Codes == Out.Codes)
      return I;
  }
  return -1;
}

/// Returns the index in the constraints of the input that the output \p Out
/// is tied to by number, or -1: that of its matching constraint ('=r,0',
/// clang's '+r') or, when the tie is written with alternatives, that of its
/// first alternative that has one ('=r|r,0|0' from `"=r,r"` with `"0,0"`,
/// '=r|r,r|0' from `"r,0"`). InlineAsm::ConstraintInfo::Parse records the
/// match of an input with alternatives only in the output's alternatives.
static int getTiedInput(const InlineAsm::ConstraintInfo &Out) {
  if (Out.hasMatchingInput())
    return Out.MatchingInput;
  for (const InlineAsm::SubConstraintInfo &Alt : Out.multipleAlternatives)
    if (Alt.MatchingInput >= 0)
      return Alt.MatchingInput;
  return -1;
}

/// Returns true if an alternative of the output \p Out is a condition flag
/// ('{@ccz}' from x86's `"=@ccz"`, '{@cceq}' from AArch64's `"=@cceq"`):
/// native code sets it from the flags after the statement (sete), so neither
/// a tied input nor the old value is its value.
static bool isFlagOutput(const InlineAsm::ConstraintInfo &Out) {
  auto IsFlag = [](const std::string &Code) {
    return StringRef(Code).starts_with("{@cc");
  };
  return any_of(Out.Codes, IsFlag) ||
         any_of(Out.multipleAlternatives,
                [&](const InlineAsm::SubConstraintInfo &Alt) {
                  return any_of(Alt.Codes, IsFlag);
                });
}

/// Returns true if an alternative of the output \p Out, which has several,
/// is a memory operand: it has one of the codes 'm', 'o', 'V', '<', '>' or
/// 'X' (clang turns 'g' into 'imr'), or AArch64's 'Q', the codes for which
/// clang's TargetInfo::ConstraintInfo::allowsMemory holds.
static bool hasMemoryAlternative(const InlineAsm::ConstraintInfo &Out,
                                 const Triple &TT) {
  auto IsMemory = [&](const std::string &Code) {
    return Code == "m" || Code == "o" || Code == "V" || Code == "<" ||
           Code == ">" || Code == "X" || (Code == "Q" && TT.isAArch64());
  };
  return any_of(Out.multipleAlternatives,
                [&](const InlineAsm::SubConstraintInfo &Alt) {
                  return any_of(Alt.Codes, IsMemory);
                });
}

/// Returns true if an empty asm statement leaves in the memory of an
/// indirect output whose operand has the element type \p ElemTy the value of
/// its tied input of type \p InTy, as native x86 code does when it takes the
/// register alternative, and lowerEmptyInlineAsm can store it: as it is when
/// the store sizes are equal (an i64 input for a _Complex int output tied to
/// a long long), truncated when it is an integer wider than the integer
/// element type (a char output tied to an int input gets the low byte).
/// Clang zero-extends an integer input narrower than its scalar output
/// itself. Every other pair has no such value (a float output tied to a
/// double input, which native x86 instruction selection cannot handle).
static bool isStorableTie(Type *InTy, Type *ElemTy, const DataLayout &DL) {
  if (!InTy->isSized() || !ElemTy->isSized())
    return false;
  if (DL.getTypeStoreSize(InTy) == DL.getTypeStoreSize(ElemTy))
    return true;
  return InTy->isIntegerTy() && ElemTy->isIntegerTy() &&
         InTy->getIntegerBitWidth() > ElemTy->getIntegerBitWidth();
}

namespace {
/// An indirect output of an empty asm statement and the by-value input tied
/// to it: lowerEmptyInlineAsm stores the input through the output's pointer,
/// call argument ArgNo.
struct TiedStore {
  unsigned ArgNo;
  Value *In;
};
} // end anonymous namespace

/// Returns true if the inline asm template \p Asm holds no instruction:
/// nothing but whitespace and /* */ comments, whose text (operand references
/// such as the `/*{0}*/` of Rust's zmij, `/*${0:k}*/` in IR, included)
/// native code never assembles. An unterminated comment is not blank.
static bool isBlankAsmString(StringRef Asm) {
  for (Asm = Asm.ltrim(); !Asm.empty(); Asm = Asm.ltrim()) {
    if (!Asm.consume_front("/*"))
      return false;
    size_t End = Asm.find("*/");
    if (End == StringRef::npos)
      return false;
    Asm = Asm.drop_front(End + 2);
  }
  return true;
}

/// Returns true if \p CI calls an empty inline asm statement that
/// lowerEmptyInlineAsm lowers, and appends to \p Results the input tied to
/// each of its direct outputs, in order, and to \p Stores each indirect
/// output that is to hold its input: an asm string that is blank
/// (isBlankAsmString: empty after trimming, or only comments; plan section
/// 12, 'M22.b extended', defect 2), inputs of any kind, outputs that are not
/// condition flags (isFlagOutput), indirect ('=*m') or direct and tied to an
/// input whose type isConvertibleTie accepts, any clobbers and no label. A
/// direct output is tied to the input of its matching constraint (getTiedInput)
/// or, for a local register variable, to the input in its own register
/// (findRegisterTie); each input is tied to one output at most. Such an asm
/// reads its inputs and writes nothing but its outputs, which hold after it
/// what native x86 code leaves in them: a direct output the value of its
/// input. An indirect output that no input is tied to by number keeps its
/// memory's contents ('=*m'), as does one tied to an indirect input
/// ('=*m,*0') or one with several alternatives, one of them memory, which
/// native code takes ('=*r|m,0|0' from `"=r,m"` with `"0,0"`, '=*r|m,0'
/// from `"+r,m"`). An indirect output with one alternative tied to a by-value
/// input ('=*rm,0' from `"=rm"` with `"0"`, '=*imr,0' from `"=g"`, '=*X,0',
/// '=*r,0' from `"=r"` on a _Complex int, clang's `"+rm"`) holds the input
/// (isStorableTie), as native code takes the register alternative. Any
/// other tie of an indirect output, and an untied direct output, has no
/// value to keep. These are the IR shapes that clang's native path emits for
/// the empty statements it lets through (hasOnlyKeptOutputs in CGStmt.cpp).
static bool isEmptyAsmWithOperands(const CallInst &CI,
                                   SmallVectorImpl<Value *> &Results,
                                   SmallVectorImpl<TiedStore> &Stores) {
  const auto *IA = dyn_cast<InlineAsm>(CI.getCalledOperand());
  if (!IA || !isBlankAsmString(IA->getAsmString()))
    return false;
  InlineAsm::ConstraintInfoVector Constraints = IA->ParseConstraints();
  if (Constraints.empty() && !IA->getConstraintString().empty())
    return false;

  // The call argument of each constraint that has one (the inputs and the
  // indirect outputs, in order), and the number of direct outputs.
  SmallVector<int, 8> ArgNos;
  unsigned NumArgs = 0;
  unsigned NumDirect = 0;
  for (const InlineAsm::ConstraintInfo &C : Constraints) {
    if (C.Type == InlineAsm::isLabel)
      return false;
    ArgNos.push_back(C.hasArg() ? NumArgs++ : -1);
    if (C.Type == InlineAsm::isOutput && !C.isIndirect)
      ++NumDirect;
  }
  if (NumArgs != CI.arg_size())
    return false;
  // One direct output is the result; several are the fields of a struct.
  Type *RetTy = CI.getType();
  auto *STy = dyn_cast<StructType>(RetTy);
  if (NumDirect == 0
          ? !RetTy->isVoidTy()
          : NumDirect > 1 && (!STy || STy->getNumElements() != NumDirect))
    return false;

  const DataLayout &DL = CI.getDataLayout();
  const Triple &TT = CI.getModule()->getTargetTriple();
  // The inputs tied to an output so far.
  SmallVector<bool, 8> Used(Constraints.size());
  unsigned OutNo = 0;
  for (unsigned CNo = 0, E = Constraints.size(); CNo != E; ++CNo) {
    const InlineAsm::ConstraintInfo &C = Constraints[CNo];
    if (C.Type != InlineAsm::isOutput)
      continue;
    if (isFlagOutput(C))
      return false;
    int InNo = getTiedInput(C);
    if (C.isIndirect) {
      // Untied: the memory keeps its contents.
      if (InNo < 0)
        continue;
      // Native code takes the memory alternative.
      if (C.isMultipleAlternative) {
        if (!hasMemoryAlternative(C, TT))
          return false;
        continue;
      }
      const InlineAsm::ConstraintInfo &In = Constraints[InNo];
      if (In.Type != InlineAsm::isInput)
        return false;
      // '=*m,*0': the memory keeps its contents.
      if (In.isIndirect)
        continue;
      if (Used[InNo])
        return false;
      Used[InNo] = true;
      Value *InV = CI.getArgOperand(ArgNos[InNo]);
      Type *ElemTy = CI.getParamElementType(ArgNos[CNo]);
      if (!ElemTy || !isStorableTie(InV->getType(), ElemTy, DL))
        return false;
      Stores.push_back({static_cast<unsigned>(ArgNos[CNo]), InV});
      continue;
    }
    if (InNo < 0)
      InNo = findRegisterTie(Constraints, C, Used);
    if (InNo < 0 || Constraints[InNo].Type != InlineAsm::isInput ||
        Constraints[InNo].isIndirect || Used[InNo])
      return false;
    Used[InNo] = true;
    Value *In = CI.getArgOperand(ArgNos[InNo]);
    if (!isConvertibleTie(In->getType(),
                          getDirectOutputType(CI, OutNo, NumDirect), DL))
      return false;
    Results.push_back(In);
    ++OutNo;
  }
  return true;
}

/// Returns true if lowerEmptyInlineAsm keeps a value of type \p Ty alive
/// with llvm.fake.use: a type that is one CIL value (decision D2).
static bool isFakeUseType(const Type *Ty) {
  if (const auto *ITy = dyn_cast<IntegerType>(Ty))
    return ITy->getBitWidth() <= 64;
  return Ty->isPointerTy() || Ty->isFloatTy() || Ty->isDoubleTy();
}

/// Lowers each call in \p F of an empty inline asm statement with operands
/// (isEmptyAsmWithOperands), the optimization barrier of Rust's black_box
/// and of C's DoNotOptimize idioms (backlog 2.3), into the compiler fence of
/// an empty asm statement without operands (decision D36), a call of
/// llvm.fake.use for each input that is not a constant and is one CIL value,
/// and a call of llvm.sideeffect, which keeps a loop around the statement;
/// then each indirect output with one alternative that is tied to a by-value
/// input gets the input stored through its pointer, as it is when the store
/// sizes are equal and truncated when it is a wider integer (isStorableTie),
/// and the memory of every other indirect output keeps its contents; each
/// direct output becomes its tied input: the input itself when the types are
/// equal, zero-extended when it is a narrower integer (the i1 of a _Bool
/// tied to an i8 output), and converted by inttoptr or ptrtoint when one is
/// a pointer and the other an integer of the pointer's width. The optimizer
/// has already run with the asm opaque, which is the point of the barrier.
/// Clang emits these statements as the native target does
/// (CodeGenFunction::EmitAsmStmt), and every IR shape it emits for them is
/// lowered here: numeric ties, ties written with alternatives, the register
/// ties of local register variables and the type pairs above. Every other
/// inline asm stays, and diagnoseDefinition reports it. Returns true if \p F
/// changed.
static bool lowerEmptyInlineAsm(Function &F) {
  SmallVector<CallInst *, 4> ToLower;
  SmallVector<Value *, 2> Results;
  SmallVector<TiedStore, 2> Stores;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || !CI->isInlineAsm())
      continue;
    Results.clear();
    Stores.clear();
    if (isEmptyAsmWithOperands(*CI, Results, Stores))
      ToLower.push_back(CI);
  }
  if (ToLower.empty())
    return false;

  Module &M = *F.getParent();
  LLVMContext &Ctx = F.getContext();
  const DataLayout &Layout = M.getDataLayout();
  Function *FakeUse =
      Intrinsic::getOrInsertDeclaration(&M, Intrinsic::fake_use);
  Function *SideEffect =
      Intrinsic::getOrInsertDeclaration(&M, Intrinsic::sideeffect);
  for (CallInst *CI : ToLower) {
    // Ask again: a tied input may be the result of an asm lowered before.
    Results.clear();
    Stores.clear();
    [[maybe_unused]] bool Lowerable =
        isEmptyAsmWithOperands(*CI, Results, Stores);
    assert(Lowerable && "replacing an operand changed the constraints");
    BasicBlock::iterator IP = CI->getIterator();
    const DebugLoc &DL = CI->getDebugLoc();
    auto *Fence = new FenceInst(Ctx, AtomicOrdering::SequentiallyConsistent,
                                SyncScope::SingleThread, IP);
    Fence->setDebugLoc(DL);
    unsigned ArgNo = 0;
    for (const InlineAsm::ConstraintInfo &C :
         cast<InlineAsm>(CI->getCalledOperand())->ParseConstraints()) {
      if (!C.hasArg())
        continue;
      Value *V = CI->getArgOperand(ArgNo++);
      if (C.Type == InlineAsm::isInput && !isa<Constant>(V) &&
          isFakeUseType(V->getType()))
        CallInst::Create(FakeUse, {V}, "", IP)->setDebugLoc(DL);
    }
    CallInst::Create(SideEffect, {}, "", IP)->setDebugLoc(DL);

    // The memory of an indirect output tied to a by-value input holds the
    // input, as native code leaves it.
    for (const TiedStore &S : Stores) {
      Value *Ptr = CI->getArgOperand(S.ArgNo);
      Type *ElemTy = CI->getParamElementType(S.ArgNo);
      Value *V = S.In;
      if (Layout.getTypeStoreSize(V->getType()) !=
          Layout.getTypeStoreSize(ElemTy)) {
        auto *Trunc = new TruncInst(V, ElemTy, "", IP);
        Trunc->setDebugLoc(DL);
        V = Trunc;
      }
      auto *Store = new StoreInst(V, Ptr, /*isVolatile=*/false,
                                  getKnownAlignment(Ptr, Layout, CI), IP);
      Store->setDebugLoc(DL);
    }

    if (!CI->getType()->isVoidTy()) {
      unsigned NumDirect = Results.size();
      for (unsigned OutNo = 0; OutNo != NumDirect; ++OutNo)
        Results[OutNo] = convertTiedInput(
            Results[OutNo], getDirectOutputType(*CI, OutNo, NumDirect), IP, DL);
      Value *Result = Results.front();
      if (NumDirect > 1) {
        Result = PoisonValue::get(CI->getType());
        for (auto [Idx, V] : enumerate(Results)) {
          auto *IV = InsertValueInst::Create(
              Result, V, static_cast<unsigned>(Idx), "", IP);
          IV->setDebugLoc(DL);
          Result = IV;
        }
      }
      CI->replaceAllUsesWith(Result);
    }
    CI->eraseFromParent();
  }
  return true;
}

/// The error for a va_arg instruction, the text of MSILISelLowering.cpp's
/// VAArgUnsupported, which reports the scalar and vector ones.
static constexpr const char *VAArgUnsupported =
    "the va_arg instruction is not supported for MSIL yet (phase 3 variadics)";

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
      // lowerEmptyInlineAsm has lowered every empty asm statement that MSIL
      // can keep; any other asm needs machine code.
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
    } else if (auto *VA = dyn_cast<VAArgInst>(&I);
               VA &&
               (VA->getType()->isStructTy() || VA->getType()->isArrayTy())) {
      // The IR verifier rejects a va_arg of an aggregate type since upstream
      // 74a120bff99f, so this branch only sees IR that skipped verification
      // (llc -disable-verify). SelectionDAGBuilder::visitVAArg asks for the
      // value type of a struct or array and crashes ('Unknown type!'), for
      // native targets too. A scalar or vector va_arg reaches instruction
      // selection, which reports it with the same text (decision D19;
      // variadics are phase 3).
      Ctx.diagnose(
          DiagnosticInfoUnsupported(F, VAArgUnsupported, I.getDebugLoc()));
      HasError = true;
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
/// their operand: replace each one on \p GV by poison (by null if \p ByNull)
/// and destroy it, before GV itself is replaced.
static void replaceGlobalValueWrappers(GlobalValue &GV, bool ByNull = false) {
  // Collect them first: replacing them changes the use list of GV.
  SmallVector<Constant *, 2> Wrappers;
  for (User *U : GV.users())
    if (isa<NoCFIValue, DSOLocalEquivalent>(U))
      Wrappers.push_back(cast<Constant>(U));
  for (Constant *C : Wrappers) {
    C->replaceAllUsesWith(ByNull ? Constant::getNullValue(C->getType())
                                 : PoisonValue::get(C->getType()));
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

//===----------------------------------------------------------------------===//
// Whole-program lowerings: the module is the whole program (-S, llc and
// msil-ld's --lto-emit-asm text), so no other module can define a symbol that
// it leaves undefined or preempt one that it defines (decision D26 as amended
// for Phase 2). CIL objects keep aliases, weak references and sections as
// symbols instead (plan 5.8.10).
//===----------------------------------------------------------------------===//

/// Returns the global object that \p GA aliases, through chains of aliases,
/// and sets \p Offset to the constant byte offset into it. Returns null if
/// the aliasee is not a global object plus a constant offset.
static GlobalObject *getAliasedObject(GlobalAlias &GA, APInt &Offset) {
  const DataLayout &DL = GA.getDataLayout();
  Offset = APInt(DL.getIndexTypeSizeInBits(GA.getType()), 0);
  // stripAndAccumulateConstantOffsets looks only through the aliases that
  // cannot be interposed, but in the whole program a weak alias is final
  // too.
  SmallPtrSet<const GlobalAlias *, 4> Visited;
  Visited.insert(&GA);
  Value *V = GA.getAliasee();
  while (true) {
    V = V->stripAndAccumulateConstantOffsets(DL, Offset,
                                             /*AllowNonInbounds=*/true);
    auto *Next = dyn_cast<GlobalAlias>(V);
    if (!Next)
      return dyn_cast<GlobalObject>(V);
    if (!Visited.insert(Next).second)
      return nullptr;
    V = Next->getAliasee();
  }
}

/// Defines \p Fwd, which a function alias of \p Aliasee left behind under its
/// name, as a forwarder: one plain call of \p Aliasee with the arguments of
/// \p Fwd, returning its result. A 'jmp' would forward with less code, but
/// ILVerify and PEVerify reject it. When the types are equal, the forwarder
/// and its call get the parameter and result attributes of \p Aliasee
/// (byval, sret, zeroext, ...), so that the forwarder has its ABI; otherwise
/// MSILFixFunctionBitcasts adapts the call, as it adapts any other.
static void defineForwarder(Function &Fwd, Function &Aliasee) {
  LLVMContext &Ctx = Fwd.getContext();
  FunctionType *FTy = Fwd.getFunctionType();
  Fwd.setCallingConv(Aliasee.getCallingConv());
  BasicBlock *BB = BasicBlock::Create(Ctx, "entry", &Fwd);
  SmallVector<Value *, 8> Args(make_pointer_range(Fwd.args()));
  CallInst *Call = CallInst::Create(FTy, &Aliasee, Args, "", BB);
  Call->setCallingConv(Aliasee.getCallingConv());
  if (FTy == Aliasee.getFunctionType()) {
    // A preallocated or inalloca argument needs the call sequence of its
    // own, which a forwarder cannot repeat (and MSIL diagnoses).
    AttributeList AL = Aliasee.getAttributes();
    SmallVector<AttributeSet, 8> ParamAttrs;
    for (unsigned I = 0, E = FTy->getNumParams(); I != E; ++I)
      ParamAttrs.push_back(AL.getParamAttrs(I)
                               .removeAttribute(Ctx, Attribute::Preallocated)
                               .removeAttribute(Ctx, Attribute::InAlloca));
    AttributeList Attrs =
        AttributeList::get(Ctx, AttributeSet(), AL.getRetAttrs(), ParamAttrs);
    Fwd.setAttributes(Attrs);
    Call->setAttributes(Attrs);
  }
  ReturnInst::Create(Ctx, FTy->getReturnType()->isVoidTy() ? nullptr : Call,
                     BB);
}

/// llvm.threadlocal.address only takes a thread-local global value, not the
/// aliasee of a thread-local alias at an offset: replaces each call of it on
/// \p GA, which aliases the thread-local variable \p GV at \p Offset
/// (through any chain of aliases), by a call of it on GV plus the offset.
static void replaceAliasThreadLocalAddressCalls(GlobalAlias &GA,
                                                GlobalVariable &GV,
                                                const APInt &Offset) {
  for (User *U : make_early_inc_range(GA.users())) {
    auto *II = dyn_cast<IntrinsicInst>(U);
    if (!II || II->getIntrinsicID() != Intrinsic::threadlocal_address)
      continue;
    IRBuilder<> Builder(II);
    Value *Addr = Builder.CreateThreadLocalAddress(&GV);
    if (!Offset.isZero())
      Addr = Builder.CreatePtrAdd(Addr, Builder.getInt(Offset));
    Addr = Builder.CreatePointerBitCastOrAddrSpaceCast(Addr, II->getType());
    II->replaceAllUsesWith(Addr);
    II->eraseFromParent();
  }
}

/// Lowers the aliases that the whole program can do without (backlog 2.1,
/// judge-c-fidelity F5): an alias of a defined function, whose value type is
/// a function type, at offset 0, or of a global variable, whose value type is
/// not, at any constant offset. Every use of such an alias (in code, in
/// initializers, in llvm.global_ctors, in other aliases) becomes its aliasee,
/// and the alias is erased, so callers call the aliasee and the address of
/// the alias is that of the aliasee (one '<msil>fn_' field per function). A
/// call whose type differs from the aliasee's is left to
/// MSILFixFunctionBitcasts. A function alias with non-local linkage first
/// leaves a definition under its name, with its type, linkage, visibility,
/// DLL storage class and unnamed_addr, that forwards to the aliasee: nothing
/// in the module calls it, but the name stays a public method (an entry point
/// named main, an export). A linkonce or linkonce_odr forwarder is then
/// dropped as unreferenced. A llvm.threadlocal.address call on a
/// thread-local alias becomes one on the aliased variable plus the offset
/// (replaceAliasThreadLocalAddressCalls). Every other alias (with an offset
/// into a function, a value type of the other kind, an ifunc as its aliasee,
/// or thread-local while the aliased variable is not) stays, and runOnModule
/// reports it.
static bool lowerAliases(Module &M) {
  struct Forwarder {
    std::string Name;
    FunctionType *Ty;
    GlobalValue::LinkageTypes Linkage;
    GlobalValue::VisibilityTypes Visibility;
    GlobalValue::DLLStorageClassTypes DLLStorage;
    GlobalValue::UnnamedAddr UnnamedAddr;
    unsigned AddrSpace;
    Function *Aliasee;
  };
  SmallVector<GlobalAlias *, 4> Lowered;
  SmallVector<Forwarder, 4> Forwarders;
  for (GlobalAlias &GA : M.aliases()) {
    APInt Offset;
    GlobalObject *GO = getAliasedObject(GA, Offset);
    bool IsFunctionType = GA.getValueType()->isFunctionTy();
    if (auto *F = dyn_cast_or_null<Function>(GO)) {
      if (F->isDeclaration() || !IsFunctionType || !Offset.isZero())
        continue;
      if (!GA.hasLocalLinkage())
        Forwarders.push_back(
            {GA.getName().str(), cast<FunctionType>(GA.getValueType()),
             GA.getLinkage(), GA.getVisibility(), GA.getDLLStorageClass(),
             GA.getUnnamedAddr(), GA.getAddressSpace(), F});
    } else if (auto *GV = dyn_cast_or_null<GlobalVariable>(GO);
               !GV || IsFunctionType ||
               (GA.isThreadLocal() && !GV->isThreadLocal())) {
      continue;
    } else if (GA.isThreadLocal()) {
      // Before any alias is replaced, while the calls name GA.
      replaceAliasThreadLocalAddressCalls(GA, *GV, Offset);
    }
    Lowered.push_back(&GA);
  }
  if (Lowered.empty())
    return false;

  // llvm.used and llvm.compiler.used may only list global values.
  SmallPtrSet<const Constant *, 4> LoweredSet(Lowered.begin(), Lowered.end());
  removeFromUsedLists(M, [&](Constant *C) { return LoweredSet.contains(C); });
  // Every alias of a chain is replaced, in any order: an alias whose aliasee
  // names another alias of the list gets that one's aliasee in turn.
  for (GlobalAlias *GA : Lowered)
    GA->replaceAllUsesWith(GA->getAliasee());
  for (GlobalAlias *GA : Lowered)
    GA->eraseFromParent();

  // The names are free now.
  for (const Forwarder &Fw : Forwarders) {
    Function *Fwd =
        Function::Create(Fw.Ty, Fw.Linkage, Fw.AddrSpace, Fw.Name, &M);
    assert(Fwd->getName() == Fw.Name && "the alias kept its name");
    Fwd->setVisibility(Fw.Visibility);
    Fwd->setDLLStorageClass(Fw.DLLStorage);
    Fwd->setUnnamedAddr(Fw.UnnamedAddr);
    defineForwarder(*Fwd, *Fw.Aliasee);
  }
  return true;
}

/// Creates the trap stub that replaces the unresolved extern_weak function
/// \p F as a callee: an internal definition named '<msil>undef_<IL name>'
/// whose body is llvm.trap, printed 'ldnull' and 'throw' (msil-ld's name and
/// body for the calls of an unresolved weak function, plan 5.9.6). It has the
/// type of \p F, without the variable arguments of a variadic one: a
/// variadic definition is an error (decision D19), and MSILFixFunctionBitcasts
/// turns a variadic call of a function that is not variadic into a call of
/// all of its arguments.
static Function *createUndefStub(Function &F, const TargetMachine &TM) {
  Module &M = *F.getParent();
  LLVMContext &Ctx = F.getContext();
  FunctionType *FTy = F.getFunctionType();
  if (FTy->isVarArg())
    FTy = FunctionType::get(FTy->getReturnType(), FTy->params(),
                            /*isVarArg=*/false);
  Function *Stub =
      Function::Create(FTy, GlobalValue::InternalLinkage, F.getAddressSpace(),
                       "<msil>undef_" + MSIL::getILName(F, TM), &M);
  Stub->setCallingConv(F.getCallingConv());
  BasicBlock *BB = BasicBlock::Create(Ctx, "entry", Stub);
  CallInst::Create(Intrinsic::getOrInsertDeclaration(&M, Intrinsic::trap), {},
                   "", BB);
  new UnreachableInst(Ctx, BB);
  return Stub;
}

/// Makes every llvm.global_ctors entry whose function is \p F call the
/// function that \p GetReplacement returns instead.
static void replaceGlobalCtor(Module &M, const Function &F,
                              function_ref<Function *()> GetReplacement) {
  GlobalVariable *Ctors = M.getNamedGlobal("llvm.global_ctors");
  if (!Ctors || !Ctors->hasInitializer())
    return;
  auto *Init = dyn_cast<ConstantArray>(Ctors->getInitializer());
  if (!Init)
    return;
  SmallVector<Constant *, 8> Entries;
  bool Changed = false;
  for (const Use &Op : Init->operands()) {
    auto *Entry = cast<Constant>(Op.get());
    auto *CS = dyn_cast<ConstantStruct>(Entry);
    if (CS && CS->getNumOperands() >= 2 &&
        CS->getOperand(1)->stripPointerCasts() == &F) {
      SmallVector<Constant *, 3> Fields;
      for (const Use &Field : CS->operands())
        Fields.push_back(cast<Constant>(Field.get()));
      Fields[1] = ConstantExpr::getPointerBitCastOrAddrSpaceCast(
          GetReplacement(), Fields[1]->getType());
      Entry = ConstantStruct::get(CS->getType(), Fields);
      Changed = true;
    }
    Entries.push_back(Entry);
  }
  if (Changed)
    Ctors->setInitializer(ConstantArray::get(Init->getType(), Entries));
}

/// Lowers the extern_weak declarations: nothing in the whole program defines
/// them, so their address is null, as a native link makes it (judge-c-fidelity
/// F5). A function that the import policy resolves stays a P/Invoke: in a
/// standalone module a Phase-1 import or the "msil-import-module" attribute
/// (MSIL::getPInvokeModule), in msil-ld's linked text only the attribute or
/// an !msil.imports entry (MSIL::getLinkedImport): msil-ld turns every
/// natively bound extern_weak declaration into an external one, so one that
/// is still extern_weak there is unresolved, whatever the standalone import
/// table says, and keeps the null-plus-trap lowering (plan 5.9.6 step 3,
/// 5.10.13).
/// For any other function, its calls and invokes (undefined behavior, which a
/// native program usually guards with 'if (f)') and the llvm.global_ctors
/// entries naming it call the trap stub (createUndefStub), created once and
/// only if needed; every other use (compares, initializers, stored addresses,
/// the no_cfi and dso_local_equivalent wrappers) becomes null. The function
/// leaves llvm.used and llvm.compiler.used and stays an unused declaration,
/// which metadata may still name. A variable is null in every use and is
/// erased, except a thread-local one, which stays: like any other
/// thread-local variable it is an error (runOnModule) until phase 3 (P3-A).
/// Off Windows a __start_<X> or __stop_<X> variable stays too while a global
/// variable definition is in section <X>: the one-object link defines it
/// (plan 5.9.9; the AsmPrinter checks that <X> names an output section).
static bool lowerExternWeak(Module &M, const TargetMachine &TM) {
  const Triple &TT = TM.getTargetTriple();
  bool Linked = MSIL::isLinkedModule(M);
  auto HasImport = [&](const Function &F) {
    StringRef Name = MSIL::getILName(F, TM);
    return Linked ? MSIL::getLinkedImport(M, Name, &F).has_value()
                  : MSIL::getPInvokeModule(TT, Name, &F).has_value();
  };
  StringSet<> Sections;
  if (!TT.isOSWindows())
    for (const GlobalVariable &GV : M.globals())
      if (GV.hasSection() && !GV.isDeclarationForLinker())
        Sections.insert(GV.getSection());
  auto IsStartStop = [&](const GlobalVariable &GV) {
    StringRef Name = MSIL::getILName(GV, TM);
    return (Name.consume_front("__start_") || Name.consume_front("__stop_")) &&
           Sections.contains(Name);
  };
  SmallVector<Function *, 4> Fns;
  for (Function &F : M)
    if (F.hasExternalWeakLinkage() && !F.isIntrinsic() && !HasImport(F))
      Fns.push_back(&F);
  SmallVector<GlobalVariable *, 4> Vars;
  for (GlobalVariable &GV : M.globals())
    if (GV.hasExternalWeakLinkage() && !GV.isThreadLocal() && !IsStartStop(GV))
      Vars.push_back(&GV);
  if (Fns.empty() && Vars.empty())
    return false;

  // llvm.used and llvm.compiler.used may only list global values.
  SmallPtrSet<const Constant *, 8> Weak(Fns.begin(), Fns.end());
  Weak.insert_range(Vars);
  removeFromUsedLists(M, [&](Constant *C) { return Weak.contains(C); });

  for (Function *F : Fns) {
    Function *Stub = nullptr;
    auto GetStub = [&] {
      if (!Stub)
        Stub = createUndefStub(*F, TM);
      return Stub;
    };
    for (Use &U : make_early_inc_range(F->uses()))
      if (auto *CB = dyn_cast<CallBase>(U.getUser()); CB && CB->isCallee(&U))
        U.set(GetStub());
    replaceGlobalCtor(M, *F, GetStub);
    // The old constructor list is dead now.
    F->removeDeadConstantUsers();
    replaceGlobalValueWrappers(*F, /*ByNull=*/true);
    F->replaceNonMetadataUsesWith(Constant::getNullValue(F->getType()));
  }
  for (GlobalVariable *GV : Vars) {
    Constant *Null = Constant::getNullValue(GV->getType());
    replaceGlobalValueWrappers(*GV, /*ByNull=*/true);
    GV->replaceAllUsesWith(Null);
    GV->eraseFromParent();
  }
  return true;
}

/// The lowerings that hold only when the module is the whole program:
/// aliases and extern_weak declarations, in that order. Named sections are
/// laid out as a one-object link lays them out (MSILDataSegment, plan
/// 5.9.9). Returns true if the module changed.
static bool lowerWholeProgramSymbols(Module &M, const TargetMachine &TM) {
  bool Changed = lowerAliases(M);
  Changed |= lowerExternWeak(M, TM);
  return Changed;
}

//===----------------------------------------------------------------------===//
// Object mode: the module is one object of a link (a CIL object, plan 5.8),
// so aliases, extern_weak declarations, external globals and sections stay
// symbols of the object (plan 5.8.10); what an object cannot carry is
// reported here.
//===----------------------------------------------------------------------===//

/// The IR spelling of the comdat selection kind \p K.
static StringRef getSelectionKindName(Comdat::SelectionKind K) {
  switch (K) {
  case Comdat::Any:
    return "any";
  case Comdat::ExactMatch:
    return "exactmatch";
  case Comdat::Largest:
    return "largest";
  case Comdat::NoDeduplicate:
    return "nodeduplicate";
  case Comdat::SameSize:
    return "samesize";
  }
  llvm_unreachable("unknown comdat selection kind");
}

/// Reports every comdat whose selection kind is neither any nor
/// nodeduplicate, once, and takes its members out of it: the link-info
/// record carries those two kinds only (plan 5.8.8, COMDATS), as C produces
/// only any. Returns true if a member changed.
static bool diagnoseComdatSelections(Module &M) {
  SmallPtrSet<const Comdat *, 4> Reported;
  bool Changed = false;
  for (GlobalObject &GO : M.global_objects()) {
    const Comdat *C = GO.getComdat();
    if (!C)
      continue;
    Comdat::SelectionKind K = C->getSelectionKind();
    if (K == Comdat::Any || K == Comdat::NoDeduplicate)
      continue;
    if (Reported.insert(C).second)
      M.getContext().emitError(
          "comdat '" + C->getName() + "' uses selection kind '" +
          getSelectionKindName(K) + "', which MSIL does not support");
    GO.setComdat(nullptr);
    Changed = true;
  }
  return Changed;
}

/// Reports every symbol of the object whose IL name starts with '<msil>'
/// (rule R5: the object's scaffolding, '<msil>Symbols', '<msil>FnAddr' and
/// '<msil>LinkInfo', and msil-ld's generated members own the prefix): every
/// function and global variable definition, every used declaration and every
/// alias. A function definition loses its body, and it and a used function
/// declaration are added to \p DeletedBodies, so that their calls are erased
/// and their other uses become poison, as for a definition with an error; a
/// global variable or alias is added to \p ToErase. Returns true if a body
/// was deleted.
static bool diagnoseReservedNames(Module &M, const TargetMachine &TM,
                                  SmallVectorImpl<Function *> &DeletedBodies,
                                  SmallVectorImpl<GlobalValue *> &ToErase) {
  auto IsReserved = [&](const GlobalValue &GV) {
    if (!MSIL::getILName(GV, TM).starts_with("<msil>"))
      return false;
    M.getContext().emitError("symbol '" + MSIL::getILName(GV, TM) +
                             "' starts with '<msil>', which MSIL reserves for "
                             "the names it generates");
    return true;
  };
  bool Changed = false;
  for (Function &F : M) {
    if (F.isIntrinsic() || (F.isDeclarationForLinker() && F.use_empty()))
      continue;
    if (!IsReserved(F))
      continue;
    if (!F.isDeclaration()) {
      F.deleteBody();
      // A declaration may not be in a comdat.
      F.setComdat(nullptr);
      Changed = true;
    }
    DeletedBodies.push_back(&F);
  }
  for (GlobalVariable &GV : M.globals())
    if (!MSIL::isSpecialLLVMGlobal(GV) &&
        (!GV.isDeclarationForLinker() || !GV.use_empty()) && IsReserved(GV))
      ToErase.push_back(&GV);
  for (GlobalAlias &GA : M.aliases())
    if (IsReserved(GA))
      ToErase.push_back(&GA);
  return Changed;
}

/// Returns true if \p GA is a symbol that a CIL object can carry (plan
/// 5.8.10): an alias of a definition plus a constant offset, through any chain
/// of aliases (getAliasedObject). That is a function definition at offset 0,
/// with a function value type, or a global variable definition that is not
/// common (a common symbol has no place in the object) and holds the
/// alias's bytes: an offset of at least 0 and at most the variable's size
/// minus the alias's. A thread-local alias is not one (thread_local stays an
/// error until phase 3, decision D26).
static bool isObjectAlias(GlobalAlias &GA) {
  APInt Offset;
  GlobalObject *GO = getAliasedObject(GA, Offset);
  if (!GO || GO->isDeclarationForLinker() || GA.isThreadLocal())
    return false;
  bool IsFunctionType = GA.getValueType()->isFunctionTy();
  if (isa<Function>(GO))
    return IsFunctionType && Offset.isZero();
  auto *GV = dyn_cast<GlobalVariable>(GO);
  if (!GV || IsFunctionType || GV->hasCommonLinkage() ||
      MSIL::isSpecialLLVMGlobal(*GV) || Offset.isNegative())
    return false;
  const DataLayout &DL = GA.getDataLayout();
  uint64_t Size = GV->getGlobalSize(DL);
  uint64_t AliasSize =
      GA.getValueType()->isSized() ? DL.getTypeAllocSize(GA.getValueType()) : 0;
  return Offset.ule(Size) && AliasSize <= Size - Offset.getZExtValue();
}

/// Returns true if the chain of aliases that starts at \p GA, constant
/// offsets stripped, reaches one of \p Values.
static bool aliasChainReaches(const GlobalAlias &GA,
                              const SmallPtrSetImpl<const Value *> &Values) {
  const DataLayout &DL = GA.getDataLayout();
  SmallPtrSet<const GlobalAlias *, 4> Visited;
  const GlobalAlias *Cur = &GA;
  while (Cur && Visited.insert(Cur).second) {
    APInt Offset(DL.getIndexTypeSizeInBits(Cur->getType()), 0);
    const Value *V = Cur->getAliasee()->stripAndAccumulateConstantOffsets(
        DL, Offset, /*AllowNonInbounds=*/true);
    if (Values.contains(V))
      return true;
    Cur = dyn_cast<GlobalAlias>(V);
  }
  return false;
}

/// Object mode: reports the comdats and the names that an object cannot carry
/// and every alias that is not an object alias (isObjectAlias), and adds the
/// global values to erase to \p ToErase (an erased alias's uses become
/// poison, as in whole-program mode). Aliases, extern_weak declarations,
/// external globals and sections stay. Returns true if the module changed.
static bool checkObjectSymbols(Module &M, const TargetMachine &TM,
                               SmallVectorImpl<Function *> &DeletedBodies,
                               SmallVectorImpl<GlobalValue *> &ToErase) {
  bool Changed = diagnoseComdatSelections(M);
  Changed |= diagnoseReservedNames(M, TM, DeletedBodies, ToErase);
  for (GlobalAlias &GA : M.aliases()) {
    if (is_contained(ToErase, &GA))
      continue;
    if (!isObjectAlias(GA)) {
      M.getContext().emitError("alias '" + MSIL::getILName(GA, TM) +
                               "' must point to a definition plus a constant "
                               "offset for MSIL");
      ToErase.push_back(&GA);
      continue;
    }
    // The calls and the addresses of an alias name it in MemberRef rows.
    if (diagnoseObjectILNameLength(GA, TM))
      ToErase.push_back(&GA);
  }
  return Changed;
}

/// Object mode: adds to \p ToErase every alias whose chain reaches one of
/// \p Gone (definitions that lost their body, global values about to be
/// erased) and that is not there yet: an alias may only alias a definition,
/// and its aliasee would otherwise become poison. The definition's own error
/// covers the alias.
static void collectAliasesOf(Module &M, ArrayRef<const Value *> Gone,
                             SmallVectorImpl<GlobalValue *> &ToErase) {
  SmallPtrSet<const Value *, 8> GoneSet(Gone.begin(), Gone.end());
  if (GoneSet.empty())
    return;
  for (GlobalAlias &GA : M.aliases())
    if (!is_contained(ToErase, &GA) && aliasChainReaches(GA, GoneSet))
      ToErase.push_back(&GA);
}

/// Returns true if \p V, a function or a constant that refers to one, has a
/// use outside the bodies of the functions in \p Bodies: in another function,
/// in a global variable initializer (llvm.used, llvm.compiler.used and
/// llvm.global_ctors included), in an alias or any other global value (the
/// prefix or prologue data of a function, the personality of a function not
/// in \p Bodies). The personality of a function in \p Bodies is part of its
/// body, as for collectReferencedFunctions. A constant that nothing uses does
/// not count. \p Visited holds the constants already walked.
static bool isUsedOutside(const Value &V,
                          const SmallPtrSetImpl<const Function *> &Bodies,
                          SmallPtrSetImpl<const Constant *> &Visited) {
  for (const User *U : V.users()) {
    if (const auto *I = dyn_cast<Instruction>(U)) {
      if (!Bodies.contains(I->getFunction()))
        return true;
      continue;
    }
    if (const auto *G = dyn_cast<Function>(U)) {
      if (!Bodies.contains(G) || !G->hasPersonalityFn() ||
          G->getPersonalityFn() != &V)
        return true;
      continue;
    }
    const auto *C = dyn_cast<Constant>(U);
    if (!C || isa<GlobalValue>(C))
      return true;
    if (Visited.insert(C).second && isUsedOutside(*C, Bodies, Visited))
      return true;
  }
  return false;
}

/// Adds to \p Refs the functions that the body of \p F refers to, directly or
/// through constants (not through the initializers of global variables), and
/// its personality function.
static void collectReferencedFunctions(Function &F,
                                       SmallVectorImpl<Function *> &Refs) {
  SmallPtrSet<const Constant *, 16> Visited;
  SmallVector<Constant *, 16> Worklist;
  auto Add = [&](Value *V) {
    if (auto *C = dyn_cast<Constant>(V); C && Visited.insert(C).second)
      Worklist.push_back(C);
  };
  for (Instruction &I : instructions(F))
    for (Use &Op : I.operands())
      Add(Op.get());
  if (F.hasPersonalityFn())
    Add(F.getPersonalityFn());
  while (!Worklist.empty()) {
    Constant *C = Worklist.pop_back_val();
    if (auto *G = dyn_cast<Function>(C))
      Refs.push_back(G);
    else if (!isa<GlobalValue>(C))
      for (Use &Op : C->operands())
        Add(Op.get());
  }
}

/// Deletes the body of every discardable function definition that the
/// program cannot reach, deciding by reachability as GlobalDCE does
/// (driver-toolchain 5.4). The candidates are every linkonce and linkonce_odr
/// function definition and every internal or private definition that the
/// body of a candidate refers to, directly or through other candidates
/// (collectReferencedFunctions). A candidate is live when something other
/// than the body of a candidate uses it (isUsedOutside: a function that is
/// not a candidate, a global variable initializer, llvm.used,
/// llvm.compiler.used, llvm.global_ctors, any other global value), and when
/// the body of a live candidate refers to it; every other candidate loses
/// its body. So a dead cycle (two linkonce_odr functions that call each
/// other, a linkonce_odr function that calls a cycle of internal functions)
/// is dropped. Every module may drop such a definition (another module that
/// uses it has its own copy, or the definition was local), and headers
/// define many: the UCRT stdio.h of a -O0 compile defines the variadic
/// sprintf and its helpers in every module, and a definition that is not
/// compiled is not diagnosed. As for a diagnosed definition, the function
/// stays as a declaration without a comdat. Global variables are never
/// dropped, and a local function that no linkonce function reaches (one
/// that nothing used from the start, a cycle of local functions) is not a
/// candidate and stays, as before. Returns true if a body was deleted.
static bool dropUnreferencedDiscardableFunctions(Module &M) {
  // The candidates, the linkonce definitions first.
  SmallPtrSet<const Function *, 16> Candidates;
  SmallVector<Function *, 16> Order;
  for (Function &F : M)
    if (!F.isDeclaration() && F.hasLinkOnceLinkage()) {
      Candidates.insert(&F);
      Order.push_back(&F);
    }
  for (unsigned I = 0; I != Order.size(); ++I) {
    SmallVector<Function *, 8> Refs;
    collectReferencedFunctions(*Order[I], Refs);
    for (Function *G : Refs)
      if (!G->isDeclaration() && G->hasLocalLinkage() &&
          Candidates.insert(G).second)
        Order.push_back(G);
  }

  // The live candidates: the ones used outside the candidates' bodies, then
  // every candidate that the body of a live one refers to.
  SmallPtrSet<const Function *, 16> Live;
  SmallVector<Function *, 16> Worklist;
  for (Function *F : Order) {
    SmallPtrSet<const Constant *, 8> Visited;
    if (isUsedOutside(*F, Candidates, Visited)) {
      Live.insert(F);
      Worklist.push_back(F);
    }
  }
  while (!Worklist.empty()) {
    SmallVector<Function *, 8> Refs;
    collectReferencedFunctions(*Worklist.pop_back_val(), Refs);
    for (Function *G : Refs)
      if (Candidates.contains(G) && Live.insert(G).second)
        Worklist.push_back(G);
  }

  SmallVector<Function *, 16> Dropped;
  for (Function *F : Order) {
    if (Live.contains(F))
      continue;
    F->deleteBody();
    // A declaration may not be in a comdat.
    F->setComdat(nullptr);
    Dropped.push_back(F);
  }
  // The constants that only the deleted bodies used refer to them still.
  for (Function *F : Dropped)
    F->removeDeadConstantUsers();
  return !Dropped.empty();
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

  bool Changed = MSIL::resetExceptionModel(M);
  Changed |= disableMetadataSections(M, TM);

  // Whole-program mode (-S, llc and msil-ld's --lto-emit-asm text) lowers
  // the symbols that need another module; object mode keeps them and checks
  // what an object can carry (plan 5.8.10). The mode is known now that the
  // pipeline runs (MSILTargetMachine::isObjectMode).
  bool ObjectMode = static_cast<const MSILTargetMachine &>(TM).isObjectMode();
  // Object mode: the global values to erase, reported by checkObjectSymbols.
  SmallVector<GlobalValue *, 4> ObjectErase;
  if (ObjectMode)
    Changed |= checkObjectSymbols(M, TM, DeletedBodies, ObjectErase);
  else
    Changed |= lowerWholeProgramSymbols(M, TM);

  // Before any definition is diagnosed: one that is dropped is not compiled.
  Changed |= dropUnreferencedDiscardableFunctions(M);

  AddrSpaceFinder ASFinder;
  FPTypeFinder FPFinder;
  ScalableOrTargetExtTypeFinder STFinder;
  // The used declarations whose signatures have a scalable vector or target
  // extension type, checked once every diagnosed definition has lost its
  // body (and its uses of them). The definitions that lost their bodies in
  // doInitialization are declarations now, and their uses are replaced
  // anyway.
  SmallVector<Function *, 4> STDecls;
  // Object mode: the used declarations reported for their IL names or import
  // strings. An object names them in MemberRef rows and import stubs, so
  // they go as a definition that lost its body goes: the writer is never
  // handed a string that a CIL object cannot hold.
  SmallVector<Function *, 4> ObjectDecls;
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
        bool HasError = diagnoseILNameLength(F, TM);
        if (ObjectMode)
          HasError |= diagnoseImportStrings(F);
        if (STFinder.inType(F.getFunctionType()) &&
            !is_contained(DeletedBodies, &F))
          STDecls.push_back(&F);
        if (ObjectMode && HasError)
          ObjectDecls.push_back(&F);
      }
      continue;
    }
    Changed |= lowerEmptyInlineAsm(F);
    bool HasError = diagnoseILNameLength(F, TM);
    HasError |= diagnoseDefinition(F, ASFinder, FPFinder, STFinder);
    if (!HasError) {
      Changed |= endBlocksAtTraps(F);
      if (ObjectMode)
        Changed |= stripFunctionData(F);
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
  for (Function *F : ObjectDecls) {
    if (is_contained(DeletedBodies, F))
      continue;
    if (!F->isDeclaration()) {
      F->deleteBody();
      // A declaration may not be in a comdat.
      F->setComdat(nullptr);
    }
    DeletedBodies.push_back(F);
    Changed = true;
  }
  Changed |= eraseCallsOf(DeletedBodies);

  // Object mode: an alias of a definition that lost its body goes too, before
  // poisonAddressesOf would make its aliasee poison.
  if (ObjectMode) {
    SmallVector<GlobalValue *, 4> DeadAliases;
    SmallVector<const Value *, 4> Gone(DeletedBodies.begin(),
                                       DeletedBodies.end());
    collectAliasesOf(M, Gone, DeadAliases);
    // Except the ones reported already, which ObjectErase erases below.
    erase_if(DeadAliases,
             [&](GlobalValue *GV) { return is_contained(ObjectErase, GV); });
    if (!DeadAliases.empty()) {
      eraseGlobalValues(M, DeadAliases);
      Changed = true;
    }
  }

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
    // Object mode: a definition's address field (a FieldDef row) and a used
    // declaration's '<msil>Symbols' MemberRef carry the IL name, so a name
    // that is too long goes with the variable (the AsmPrinter's error in
    // whole-program mode). A variable erased above, or reported by
    // checkObjectSymbols, is not reported again.
    if (ObjectMode && (ToErase.empty() || ToErase.back() != &GV) &&
        !is_contained(ObjectErase, &GV) &&
        (!GV.isDeclarationForLinker() || !GV.use_empty()) &&
        diagnoseObjectILNameLength(GV, TM))
      ToErase.push_back(&GV);
  }
  if (ObjectMode) {
    // checkObjectSymbols has reported the aliases that an object cannot
    // carry; an alias also goes with the global value it aliases.
    for (GlobalValue *GV : ObjectErase)
      if (!is_contained(ToErase, GV))
        ToErase.push_back(GV);
    SmallVector<const Value *, 8> Gone(ToErase.begin(), ToErase.end());
    collectAliasesOf(M, Gone, ToErase);
  } else {
    // lowerAliases has lowered every alias of a definition plus a constant
    // offset that fits its kind.
    for (GlobalAlias &GA : M.aliases()) {
      Ctx.emitError("alias " + MSIL::getIRName(GA) +
                    " is not supported for MSIL");
      ToErase.push_back(&GA);
    }
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
