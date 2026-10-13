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
/// - Pseudo probes (llvm.pseudoprobe calls and !llvm.pseudo_probe_desc) are
///   erased with a warning, before any pass runs (doInitialization).
/// - A function definition that is variadic, calls inline assembly, accesses
///   a named register or uses a pointer in a non-zero address space is an
///   error, and its body is deleted.
/// - An alias, an ifunc or a global variable in a non-zero address space is an
///   error; its uses become poison and it is erased.
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

public:
  static char ID; // Pass identification, replacement for typeid
  MSILPrepareIRLegacy() : ModulePass(ID) {}
};

/// Finds pointers in non-zero address spaces (x86 segment spaces 256-258,
/// the MS 32-bit pointer spaces 270-271, ...) in types and values. CIL has
/// only flat native-int pointers.
class AddrSpaceFinder {
  DenseMap<Type *, unsigned> TypeCache;
  DenseMap<const Constant *, unsigned> ConstantCache;

public:
  /// Returns the address space of the first pointer in a non-zero address
  /// space that \p Ty is or contains (through vector, array, structure and
  /// function types), or 0.
  unsigned inType(Type *Ty);

  /// Returns the address space of the first pointer in a non-zero address
  /// space in the type of \p V or, for a constant other than a global, in
  /// its operands (as in 'ptrtoint (ptr addrspace(256) inttoptr (i64 48 to
  /// ptr addrspace(256)) to i64)'), or 0.
  unsigned inValue(const Value *V);

  /// The same for the result and the operands of \p I.
  unsigned inInstruction(const Instruction &I);
};
} // end anonymous namespace

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

unsigned AddrSpaceFinder::inType(Type *Ty) {
  if (auto *PTy = dyn_cast<PointerType>(Ty))
    return PTy->getAddressSpace();
  if (Ty->getNumContainedTypes() == 0)
    return 0;
  auto [It, Inserted] = TypeCache.try_emplace(Ty, 0);
  if (!Inserted)
    return It->second;
  unsigned AS = 0;
  for (Type *Sub : Ty->subtypes())
    if ((AS = inType(Sub)))
      break;
  // The recursion may have grown the map: look the entry up again.
  TypeCache[Ty] = AS;
  return AS;
}

unsigned AddrSpaceFinder::inValue(const Value *V) {
  if (unsigned AS = inType(V->getType()))
    return AS;
  const auto *C = dyn_cast<Constant>(V);
  if (!C || isa<GlobalValue>(C) || C->getNumOperands() == 0)
    return 0;
  auto [It, Inserted] = ConstantCache.try_emplace(C, 0);
  if (!Inserted)
    return It->second;
  unsigned AS = 0;
  for (const Use &Op : C->operands())
    if ((AS = inValue(Op.get())))
      break;
  ConstantCache[C] = AS;
  return AS;
}

unsigned AddrSpaceFinder::inInstruction(const Instruction &I) {
  if (unsigned AS = inType(I.getType()))
    return AS;
  for (const Use &Op : I.operands())
    if (unsigned AS = inValue(Op.get()))
      return AS;
  return 0;
}

/// Returns the IR name of \p GV as it appears in the IR ("@name").
static std::string getIRName(const GlobalValue &GV) {
  std::string Name;
  raw_string_ostream OS(Name);
  GV.printAsOperand(OS, /*PrintType=*/false);
  return Name;
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

/// Reports every construct in the definition \p F that MSIL cannot compile.
/// Returns true if there was one.
static bool diagnoseDefinition(Function &F, AddrSpaceFinder &Finder) {
  LLVMContext &Ctx = F.getContext();
  DiagnosticLocation FnLoc(F.getSubprogram());
  bool HasError = false;

  if (F.isVarArg()) {
    Ctx.diagnose(DiagnosticInfoUnsupported(
        F, "variadic function definitions are not supported for MSIL yet",
        FnLoc));
    HasError = true;
  }

  // One diagnostic per definition for address spaces: at the signature, or
  // at the first instruction that uses one.
  DiagnosticLocation ASLoc = FnLoc;
  unsigned AS = F.getAddressSpace();
  if (!AS)
    AS = Finder.inType(F.getFunctionType());

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
      }
    }
    if (!AS && (AS = Finder.inInstruction(I)))
      ASLoc = DiagnosticLocation(I.getDebugLoc());
  }

  if (AS) {
    std::string Msg = ("pointers in address space " + Twine(AS) +
                       " are not supported for MSIL")
                          .str();
    Ctx.diagnose(DiagnosticInfoUnsupported(F, Msg, ASLoc));
    HasError = true;
  }
  return HasError;
}

/// Reports a non-empty llvm.global_dtors and erases it. Destructors need an
/// exit hook in the runtime support assembly (phase 3).
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
          M.getContext().emitError("global destructor " + getIRName(*Fn) +
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
/// variable, except the llvm.* globals that name no code or data to compile
/// (llvm.used and the like), so that only the module header is printed.
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
    if (!Name.starts_with("llvm.") || Name == "llvm.global_ctors" ||
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
// !llvm.pseudo_probe_desc. runOnModule would be too late: the legacy pass
// manager initializes every pass before it runs any. It initializes them in
// pipeline order, so this module pass is initialized before the function pass
// manager that holds the AsmPrinter.
bool MSILPrepareIRLegacy::doInitialization(Module &M) {
  return stripPseudoProbes(M);
}

bool MSILPrepareIRLegacy::runOnModule(Module &M) {
  LLVMContext &Ctx = M.getContext();

  // Check the TargetMachine, not the module: it decides what is compiled and
  // supplies the DataLayout, and its triple is the one that -march=msil
  // switched to the MSIL object format.
  const auto &TM = getAnalysis<TargetPassConfig>().getTM<TargetMachine>();
  if (!isSupportedTarget(TM)) {
    Ctx.emitError("unsupported target triple '" + TM.getTargetTriple().str() +
                  "' for MSIL (Phase 1 supports x86_64 and aarch64 with "
                  "64-bit pointers)");
    dropModuleContents(M);
    return true;
  }

  bool Changed = resetExceptionModel(M);

  AddrSpaceFinder Finder;
  for (Function &F : M) {
    Changed |= stripStackProtector(F);
    Changed |= stripEntryInstrumentation(F);
    // An available_externally body is never code-generated (see
    // MachineFunctionPass::runOnFunction): only its declaration matters.
    if (F.isDeclaration() || F.hasAvailableExternallyLinkage())
      continue;
    if (!diagnoseDefinition(F, Finder))
      continue;
    F.deleteBody();
    // A declaration may not be in a comdat.
    F.setComdat(nullptr);
    Changed = true;
  }

  Changed |= diagnoseGlobalDtors(M);

  // The global values to replace by poison and erase.
  SmallVector<GlobalValue *, 4> ToErase;
  for (GlobalVariable &GV : M.globals()) {
    // llvm.used and the other intrinsic globals hold no program data.
    if (GV.getName().starts_with("llvm."))
      continue;
    if (unsigned AS = GV.getAddressSpace()) {
      Ctx.emitError("global variable " + getIRName(GV) + " in address space " +
                    Twine(AS) + " is not supported for MSIL");
      ToErase.push_back(&GV);
    } else if (GV.isThreadLocal()) {
      Ctx.emitError("thread-local variable " + getIRName(GV) +
                    " is not supported for MSIL yet");
      replaceThreadLocalAddressCalls(GV, &GV);
      GV.setThreadLocal(false);
      Changed = true;
    }
  }
  for (GlobalAlias &GA : M.aliases()) {
    Ctx.emitError("alias " + getIRName(GA) + " is not supported for MSIL");
    ToErase.push_back(&GA);
  }
  for (GlobalIFunc &GI : M.ifuncs()) {
    Ctx.emitError("ifunc " + getIRName(GI) + " is not supported for MSIL");
    ToErase.push_back(&GI);
  }
  if (ToErase.empty())
    return Changed;

  eraseGlobalValues(M, ToErase);
  return true;
}
