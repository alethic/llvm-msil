//===-- MSILVariadics.cpp - Prepare variadic calls for ExpandVariadics ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSILPrepareVariadics runs immediately before ExpandVariadics, which lowers
/// every variadic definition and call over a native-layout va_list in the
/// caller's static frame (MSILPassConfig::addIRPasses, slots P6 and P7).
///
/// A call of an undefined variadic function stays expanded in an object, and
/// msil-ld binds it: to a variadic definition directly, or to a native
/// function through its va_list variant (printf as vprintf). A native
/// variadic function without a variant cannot take a va_list, so a call of a
/// declaration on the fixed-arity list of the triple's C runtime, or of a
/// variadic declaration with an explicit import (import_module; msil-ld never
/// renames such an entry), is instead made a call with the non-variadic type
/// of its arguments: one IL signature per call site, as a K&R call has. This
/// pass
/// - gives each such declaration the function attribute "msil-fixed-arity",
///   which the MSIL ABIs of ExpandVariadics ignore, and rewrites each of its
///   calls into a call of the same declaration with the non-variadic type of
///   the call's arguments and the call-site attribute "msil-fixed-arity"="<n>"
///   (n named parameters), from which the object writer sets the call's
///   FIXED_ARITY flag (MSIL::getABIDescriptor). A variadic argument of type
///   double is passed as its bits in an i64 on Windows triples, where a
///   variadic callee reads it from the integer register's home slot. On the
///   x86_64 System V triples a floating-point variadic argument is an error,
///   since a P/Invoke cannot set the vector-register count in AL that the
///   callee's prologue tests; on every triple a byval variadic argument is an
///   error. AArch64 Linux passes the arguments unchanged;
/// - gives every other variadic function that ExpandVariadics expands, the
///   definitions and the declarations, the function attribute
///   "msil-variadic", which the fixed-arity replacement keeps
///   (copyAttributesFrom), so that the object writer sets VARARG and
///   FixedParams for the expanded function and for every reference to it.
///
/// Prototype-less declarations ("no-prototype") are left alone: their calls
/// are MSILFixFunctionBitcasts'.
///
/// ExpandVariadics expands only calls of the C calling convention, and in
/// Lowering mode stops with a fatal error at any other variadic call that it
/// reaches (an indirect one, one through an alias, or one of a function that
/// it expands). This pass reports each variadic call of another convention
/// (win64cc on x86_64 System V, x86_64_sysvcc on Windows, preserve_mostcc,
/// ...) as LowerCall reports a call that stays variadic, and erases it.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "MSILUtilities.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/IPO/ExpandVariadics.h"
#include <algorithm>
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "msil-prepare-variadics"

namespace {
class MSILPrepareVariadicsLegacy final : public ModulePass {
  StringRef getPassName() const override {
    return "MSIL Prepare Variadic Calls";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    ModulePass::getAnalysisUsage(AU);
  }

  bool runOnModule(Module &M) override;

public:
  static char ID;
  MSILPrepareVariadicsLegacy() : ModulePass(ID) {}
};
} // end anonymous namespace

char MSILPrepareVariadicsLegacy::ID = 0;
INITIALIZE_PASS(MSILPrepareVariadicsLegacy, DEBUG_TYPE,
                "MSIL Prepare Variadic Calls", false, false)

ModulePass *llvm::createMSILPrepareVariadicsLegacyPass() {
  return new MSILPrepareVariadicsLegacy();
}

//===----------------------------------------------------------------------===//
// The fixed-arity lists
//===----------------------------------------------------------------------===//

// The variadic functions that a C runtime exports without a va_list variant,
// each list sorted. msil-ld holds the variants (printf -> vprintf) and the
// address adapters of the names that have one. clone and makecontext take
// callbacks and are on no list.

/// glibc: the 25 exported variadic functions without a variant.
static constexpr StringLiteral GlibcFixedArity[] = {
    "__asprintf", "argp_error", "argp_failure", "error",   "error_at_line",
    "execl",      "execle",     "execlp",       "fcntl",   "fcntl64",
    "ioctl",      "mq_open",    "mremap",       "open",    "open64",
    "openat",     "openat64",   "prctl",        "ptrace",  "sem_open",
    "semctl",     "strfmon",    "strfmon_l",    "syscall", "ulimit"};

/// musl: glibc's names that musl exports too (unverified: linux-musl is not
/// an execution triple).
static constexpr StringLiteral MuslFixedArity[] = {
    "execl",  "execle",   "execlp",    "fcntl",   "fcntl64",
    "ioctl",  "mq_open",  "mremap",    "open",    "open64",
    "openat", "openat64", "prctl",     "ptrace",  "sem_open",
    "semctl", "strfmon",  "strfmon_l", "syscall", "ulimit"};

/// The UCRT (ucrtbase.dll): its 20 exported variadic functions, none with a
/// variant, and the 10 POSIX names that its headers declare variadic and
/// oldnames.lib maps to those exports (open -> _open, execl -> _execl, ...).
/// Its printf and scanf families are inline functions of its headers, over
/// __stdio_common_v*, which take a va_list.
static constexpr StringLiteral UCRTFixedArity[] = {
    "_execl",    "_execle",    "_execlp",  "_execlpe",  "_open",    "_sopen",
    "_spawnl",   "_spawnle",   "_spawnlp", "_spawnlpe", "_wexecl",  "_wexecle",
    "_wexeclp",  "_wexeclpe",  "_wopen",   "_wsopen",   "_wspawnl", "_wspawnle",
    "_wspawnlp", "_wspawnlpe", "execl",    "execle",    "execlp",   "execlpe",
    "open",      "sopen",      "spawnl",   "spawnle",   "spawnlp",  "spawnlpe"};

/// msvcrt.dll (windows-gnu): the 62 of its 126 exported variadic functions
/// that have no variant.
static constexpr StringLiteral MsvcrtFixedArity[] = {
    "_CrtDbgReport", "_CrtDbgReportW", "_cscanf",     "_cscanf_l",
    "_cscanf_s",     "_cscanf_s_l",    "_cwscanf",    "_cwscanf_l",
    "_cwscanf_s",    "_cwscanf_s_l",   "_execl",      "_execle",
    "_execlp",       "_execlpe",       "_fscanf_l",   "_fscanf_s_l",
    "_fwscanf_l",    "_fwscanf_s_l",   "_open",       "_scanf_l",
    "_scanf_s_l",    "_snscanf",       "_snscanf_l",  "_snscanf_s",
    "_snscanf_s_l",  "_snwscanf",      "_snwscanf_l", "_snwscanf_s",
    "_snwscanf_s_l", "_sopen",         "_spawnl",     "_spawnle",
    "_spawnlp",      "_spawnlpe",      "_sscanf_l",   "_sscanf_s_l",
    "_swscanf_l",    "_swscanf_s_l",   "_wexecl",     "_wexecle",
    "_wexeclp",      "_wexeclpe",      "_wopen",      "_wscanf_l",
    "_wscanf_s_l",   "_wsopen",        "_wspawnl",    "_wspawnle",
    "_wspawnlp",     "_wspawnlpe",     "fscanf",      "fscanf_s",
    "fwscanf",       "fwscanf_s",      "scanf",       "scanf_s",
    "sscanf",        "sscanf_s",       "swscanf",     "swscanf_s",
    "wscanf",        "wscanf_s"};

/// Whether \p List is sorted without duplicates, as lookups need.
static bool isStrictlySorted(ArrayRef<StringLiteral> List) {
  return std::adjacent_find(List.begin(), List.end(),
                            [](StringRef L, StringRef R) {
                              return L.compare(R) >= 0;
                            }) == List.end();
}

/// The fixed-arity list of the C runtime of \p TT: linux-gnu (a Linux triple
/// without an environment included) glibc, linux-musl musl, windows-msvc the
/// UCRT, windows-gnu msvcrt.dll; empty for any other triple.
static ArrayRef<StringLiteral> getFixedArityList(const Triple &TT) {
  // Checked once, at first use.
  [[maybe_unused]] static const bool Sorted =
      isStrictlySorted(GlibcFixedArity) && isStrictlySorted(MuslFixedArity) &&
      isStrictlySorted(UCRTFixedArity) && isStrictlySorted(MsvcrtFixedArity);
  assert(Sorted && "the fixed-arity lists must be sorted without duplicates");
  if (TT.isOSLinux()) {
    if (TT.isMusl())
      return MuslFixedArity;
    if (TT.isAndroid())
      return {};
    return GlibcFixedArity;
  }
  if (TT.isWindowsMSVCEnvironment())
    return UCRTFixedArity;
  if (TT.isWindowsGNUEnvironment())
    return MsvcrtFixedArity;
  return {};
}

/// Whether \p Name is on \p List.
static bool isOnList(ArrayRef<StringLiteral> List, StringRef Name) {
  const StringLiteral *It = llvm::lower_bound(
      List, Name, [](StringRef L, StringRef R) { return L < R; });
  return It != List.end() && *It == Name;
}

//===----------------------------------------------------------------------===//
// The pass
//===----------------------------------------------------------------------===//

/// Whether ExpandVariadics expands the variadic function \p F
/// (ExpandVariadics::expansionApplicableToFunction in Lowering mode, with the
/// MSIL ABIs' ignoreFunction).
static bool isExpanded(const Function &F) {
  return F.isVarArg() && !F.isIntrinsic() &&
         !F.hasFnAttribute(Attribute::Naked) &&
         !F.hasFnAttribute("no-prototype") &&
         !F.hasFnAttribute("msil-fixed-arity") &&
         isExpandedVariadicCallingConv(F.getCallingConv());
}

/// Reports and erases every variadic call whose calling convention
/// ExpandVariadics does not expand (see the file comment), once per call and
/// with LowerCall's text; the call's value becomes poison. Calls of the
/// functions that the MSIL ABIs ignore (prototype-less and fixed-arity
/// declarations) and of intrinsics are left alone, as ExpandVariadics leaves
/// them.
static bool eraseUnexpandedCalls(Module &M) {
  bool Changed = false;
  for (Function &Caller : M) {
    if (Caller.isDeclaration())
      continue;
    for (Instruction &I : make_early_inc_range(instructions(Caller))) {
      auto *CI = dyn_cast<CallInst>(&I);
      if (!CI || CI->isInlineAsm() || !CI->getFunctionType()->isVarArg() ||
          isExpandedVariadicCallingConv(CI->getCallingConv()))
        continue;
      if (const Function *Callee = CI->getCalledFunction();
          Callee &&
          (Callee->isIntrinsic() || Callee->hasFnAttribute("no-prototype") ||
           Callee->hasFnAttribute("msil-fixed-arity")))
        continue;
      Caller.getContext().diagnose(DiagnosticInfoUnsupported(
          Caller, "calls to variadic functions are not supported for MSIL yet",
          CI->getDebugLoc()));
      if (!CI->getType()->isVoidTy())
        CI->replaceAllUsesWith(PoisonValue::get(CI->getType()));
      CI->eraseFromParent();
      Changed = true;
    }
  }
  return Changed;
}

/// Whether the variadic declaration \p F is called with fixed arity: it is on
/// the fixed-arity list of \p TT, it has an explicit import (msil-ld binds
/// such an entry unchecked and never gives it a variant), or it already has
/// the "msil-fixed-arity" attribute.
static bool isFixedArity(const Function &F, const Triple &TT) {
  if (!F.isVarArg() || F.isIntrinsic() || !F.isDeclarationForLinker() ||
      F.hasFnAttribute("no-prototype"))
    return false;
  if (F.hasFnAttribute("msil-fixed-arity") || MSIL::hasImportModule(&F))
    return true;
  return isOnList(getFixedArityList(TT),
                  GlobalValue::dropLLVMManglingEscape(F.getName()));
}

/// Rewrites every direct call of the fixed-arity declaration \p F into a call
/// with the non-variadic type of its arguments (see the file comment).
static void rewriteFixedArityCalls(Function &F, const Triple &TT) {
  LLVMContext &Ctx = F.getContext();
  StringRef Name = GlobalValue::dropLLVMManglingEscape(F.getName());
  unsigned NumNamed = F.getFunctionType()->getNumParams();
  bool Windows = TT.isOSWindows();
  bool SysV = !Windows && TT.getArch() == Triple::x86_64;
  for (User *U : make_early_inc_range(F.users())) {
    auto *CI = dyn_cast<CallInst>(U);
    if (!CI || CI->getCalledOperand() != &F)
      continue;
    Function &Caller = *CI->getFunction();
    IRBuilder<> Builder(CI);
    SmallVector<Value *, 8> Args(CI->args());
    AttributeList Attrs = CI->getAttributes();
    bool ReportedFP = false, ReportedByval = false;
    for (unsigned I = NumNamed, E = Args.size(); I < E; ++I) {
      Type *Ty = Args[I]->getType();
      if (CI->paramHasAttr(I, Attribute::ByVal) && !ReportedByval) {
        Ctx.diagnose(DiagnosticInfoUnsupported(
            Caller,
            Twine(
                "passing a structure by value to native variadic function '") +
                Name + "' is not supported for MSIL",
            CI->getDebugLoc()));
        ReportedByval = true;
      }
      if (SysV && Ty->getScalarType()->isFloatingPointTy() && !ReportedFP) {
        Ctx.diagnose(DiagnosticInfoUnsupported(
            Caller,
            Twine("passing a floating-point value to native variadic "
                  "function '") +
                Name +
                "' is not supported for MSIL: it has no va_list variant, and "
                ".NET cannot set the vector-register count (AL) of the x86-64 "
                "System V variadic convention",
            CI->getDebugLoc()));
        ReportedFP = true;
      }
      // A variadic double in the integer register too (Microsoft x64), and
      // in the slot that the callee's home area reads (ARM64 Windows).
      if (Windows && Ty->isDoubleTy()) {
        Args[I] = Builder.CreateBitCast(Args[I], Builder.getInt64Ty());
        // Only the attributes that an i64 parameter may carry stay.
        AttributeMask Incompatible = AttributeFuncs::typeIncompatible(
            Builder.getInt64Ty(), Attrs.getParamAttrs(I));
        Attrs = Attrs.removeParamAttributes(Ctx, I, Incompatible);
      }
    }

    SmallVector<Type *, 8> ParamTys;
    for (Value *A : Args)
      ParamTys.push_back(A->getType());
    FunctionType *FTy =
        FunctionType::get(CI->getType(), ParamTys, /*isVarArg=*/false);
    SmallVector<OperandBundleDef, 1> Bundles;
    CI->getOperandBundlesAsDefs(Bundles);
    CallInst *NewCI =
        CallInst::Create(FTy, &F, Args, Bundles, "", CI->getIterator());
    NewCI->setCallingConv(CI->getCallingConv());
    NewCI->setTailCallKind(CI->getTailCallKind());
    NewCI->setAttributes(
        Attrs.addFnAttribute(Ctx, "msil-fixed-arity", utostr(NumNamed)));
    NewCI->copyMetadata(*CI);
    NewCI->takeName(CI);
    CI->replaceAllUsesWith(NewCI);
    CI->eraseFromParent();
  }
}

bool MSILPrepareVariadicsLegacy::runOnModule(Module &M) {
  // The target machine's triple when there is one: -march=msil makes it the
  // MSIL triple of the module's. Only its operating system and environment
  // matter here.
  Triple TT = M.getTargetTriple();
  if (auto *TPC = getAnalysisIfAvailable<TargetPassConfig>())
    TT = TPC->getTM<TargetMachine>().getTargetTriple();

  bool Changed = false;
  for (Function &F : M) {
    if (isFixedArity(F, TT)) {
      if (!F.hasFnAttribute("msil-fixed-arity"))
        F.addFnAttr("msil-fixed-arity");
      rewriteFixedArityCalls(F, TT);
      Changed = true;
    } else if (isExpanded(F) && !F.hasFnAttribute("msil-variadic")) {
      F.addFnAttr("msil-variadic");
      Changed = true;
    }
  }
  Changed |= eraseUnexpandedCalls(M);
  return Changed;
}
