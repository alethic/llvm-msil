//===-- MSILAddMissingPrototypes.cpp - Fix prototypeless functions --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyAddMissingPrototypes.cpp
// at d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Add prototypes to prototypes-less functions.
///
/// CIL has strict function prototype checking so we need functions
/// declarations to match the call sites.  Clang treats prototype-less functions
/// as varargs (foo(...)) which happens to work on existing platforms but
/// doesn't under CIL.  This pass will find all the call sites of each
/// prototype-less function, ensure they agree, and then set the signature
/// on the function declaration accordingly.
///
/// Unlike WebAssembly:
/// - The prototype is never variadic. MSIL clang gives a call through an
///   unprototyped function type a fixed-arity type on every MSIL triple, but
///   IR from producers that use the native x86-64 SysV convention (native
///   clang, whose datalayout is the same, and hand-written IR) gives it a
///   variadic type whose fixed parameters are all of the call's arguments
///   (X86_64TargetCodeGenInfo::isNoProtoCallVariadic, which sets %al for the
///   callee). In C such a call works like a non-variadic call, and
///   MSILFixFunctionBitcasts makes those calls non-variadic.
/// - A declaration whose address is used but that is never called directly is
///   diagnosed, because its signature cannot be derived. CIL calli does not
///   check signatures, so a call through the address of a P/Invoke with the
///   fallback signature 'T ()' would pass garbage arguments to the native
///   callee; WebAssembly traps on such a call instead. In msil-ld's linked
///   text (MSIL::isLinkedModule) the error names the native module that
///   binds the declaration, its "msil-import-module" attribute.
/// - Malformed "no-prototype" declarations are diagnosed rather than fatal,
///   and are left unchanged. A call that does not fit the sret parameter of a
///   declaration (its first argument is not a pointer, or its result is not
///   void) is diagnosed too, and the declaration gets the type it gets without
///   calls, which keeps its sret attribute valid.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "MSILUtilities.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "msil-add-missing-prototypes"

namespace {
class MSILAddMissingPrototypesLegacy final : public ModulePass {
  StringRef getPassName() const override {
    return "Add prototypes to prototypes-less functions";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    ModulePass::getAnalysisUsage(AU);
  }

  bool runOnModule(Module &M) override;

public:
  static char ID;
  MSILAddMissingPrototypesLegacy() : ModulePass(ID) {}
};
} // End anonymous namespace

char MSILAddMissingPrototypesLegacy::ID = 0;
INITIALIZE_PASS(MSILAddMissingPrototypesLegacy, DEBUG_TYPE,
                "Add prototypes to prototypes-less functions", false, false)

ModulePass *llvm::createMSILAddMissingPrototypesLegacyPass() {
  return new MSILAddMissingPrototypesLegacy();
}

/// Returns the type of a call of a prototype-less function: the call's own
/// type, without the variadic flag that IR using the native x86-64 SysV
/// convention gives it (MSIL clang emits fixed-arity calls), so that every
/// argument is a fixed parameter. MSILFixFunctionBitcasts computes the same
/// type when it makes such a call non-variadic.
static FunctionType *getNonVariadicCallType(const CallBase &CB) {
  FunctionType *Ty = CB.getFunctionType();
  if (!Ty->isVarArg())
    return Ty;
  SmallVector<Type *, 8> ArgTys;
  for (const Use &Arg : CB.args())
    ArgTys.push_back(Arg->getType());
  return FunctionType::get(Ty->getReturnType(), ArgTys, /*isVarArg=*/false);
}

/// Returns true if F is used other than as the callee of a call, looking
/// through bitcasts: its address is used, for example stored to memory, in a
/// global initializer, passed as an argument or compared.
static bool hasAddressUse(const Function &F) {
  SmallVector<const Value *> Worklist;
  Worklist.push_back(&F);
  while (!Worklist.empty()) {
    const Value *V = Worklist.pop_back_val();
    for (const Use &U : V->uses()) {
      if (auto *BC = dyn_cast<BitCastOperator>(U.getUser())) {
        Worklist.push_back(BC);
        continue;
      }
      auto *CB = dyn_cast<CallBase>(U.getUser());
      if (!CB || !CB->isCallee(&U))
        return true;
    }
  }
  return false;
}

static bool addMissingPrototypes(Module &M) {
  LLVM_DEBUG(dbgs() << "********** Add Missing Prototypes **********\n");

  std::vector<std::pair<Function *, Function *>> Replacements;

  // Find all the prototype-less function declarations
  for (Function &F : M) {
    if (!F.isDeclaration() || !F.hasFnAttribute("no-prototype"))
      continue;

    LLVM_DEBUG(dbgs() << "Found no-prototype function: " << F.getName()
                      << "\n");

    // When clang emits prototype-less C functions it uses (...), i.e. varargs
    // function that take no arguments (have no sentinel).  When we see a
    // no-prototype attribute we expect the function have these properties.
    // The input may be any IR, so a declaration without them is a diagnostic
    // and is left as it is.
    if (!F.isVarArg()) {
      M.getContext().emitError(
          "functions with 'no-prototype' attribute must take varargs: " +
          F.getName());
      continue;
    }
    unsigned NumParams = F.getFunctionType()->getNumParams();
    if (NumParams != 0) {
      if (!(NumParams == 1 && F.arg_begin()->hasStructRetAttr())) {
        M.getContext().emitError("functions with 'no-prototype' attribute "
                                 "should not have params: " +
                                 F.getName());
        continue;
      }
    }

    // Find calls of this function, looking through bitcasts.
    SmallVector<CallBase *> Calls;
    SmallVector<Value *> Worklist;
    Worklist.push_back(&F);
    while (!Worklist.empty()) {
      Value *V = Worklist.pop_back_val();
      for (User *U : V->users()) {
        if (auto *BC = dyn_cast<BitCastOperator>(U))
          Worklist.push_back(BC);
        else if (auto *CB = dyn_cast<CallBase>(U))
          if (CB->getCalledOperand() == V)
            Calls.push_back(CB);
      }
    }

    // Create a function prototype based on the first call site that we find.
    FunctionType *NewType = nullptr;
    for (CallBase *CB : Calls) {
      LLVM_DEBUG(dbgs() << "prototype-less call of " << F.getName() << ":\n");
      LLVM_DEBUG(dbgs() << *CB << "\n");
      FunctionType *DestType = getNonVariadicCallType(*CB);
      if (!NewType) {
        // Create a new function with the correct type
        NewType = DestType;
        LLVM_DEBUG(dbgs() << "found function type: " << *NewType << "\n");
      } else if (NewType != DestType) {
        M.getContext().diagnose(DiagnosticInfoGeneric(
            CB,
            "prototype-less function used with conflicting signatures: " +
                F.getName(),
            DS_Warning));
        LLVM_DEBUG(dbgs() << "  " << *DestType << "\n");
        LLVM_DEBUG(dbgs() << "  " << *NewType << "\n");
      }
    }

    // The new function keeps the sret attribute of F's parameter, which does
    // not apply to the type of a call whose first argument is not a pointer
    // or whose result is not void. Diagnose such a call and give the new
    // function the type it gets without calls, below, so that
    // MSILFixFunctionBitcasts handles the call like any other mismatched call.
    // Leaving F variadic would add LowerCall's error for calls of variadic
    // functions.
    if (NewType && NumParams == 1 &&
        (NewType->getNumParams() == 0 ||
         !NewType->getParamType(0)->isPointerTy() ||
         NewType->getReturnType() != F.getFunctionType()->getReturnType())) {
      M.getContext().emitError("call of prototype-less function '" +
                               F.getName() +
                               "' does not match its sret parameter");
      NewType = FunctionType::get(F.getFunctionType()->getReturnType(),
                                  F.getFunctionType()->params(), false);
    }

    if (!NewType) {
      LLVM_DEBUG(
          dbgs() << "could not derive a function prototype from usage: " +
                        F.getName() + "\n");
      // A call through the address of F would not be checked against the
      // type below (see the file comment), so a used address is an error.
      // Dead constant users, such as those left when MSILPrepareIR deletes
      // the body of a diagnosed definition, do not use it.
      F.removeDeadConstantUsers();
      if (hasAddressUse(F)) {
        // In msil-ld's linked text the check runs after native resolution
        // and names the native module that binds F (plan 5.10.13,
        // native-imports 6). The standalone text is frozen (D14).
        std::string Module;
        if (MSIL::isLinkedModule(M) && MSIL::hasImportModule(&F))
          Module =
              (" (" +
               F.getFnAttribute("msil-import-module").getValueAsString() + ")")
                  .str();
        M.getContext().emitError(
            "the signature of prototype-less function '" + F.getName() + "'" +
            Module +
            " cannot be derived: its address is used, but it is never called "
            "directly; declare it with a prototype");
      }
      // We could not derive a type for this function.  In this case strip
      // the isVarArg and make it a simple function of its fixed parameters
      // (none, or the sret pointer of a function returning a struct, whose
      // attributes the new function keeps).  This has more chance of being
      // correct.  The current signature of (...) is illegal in C since it
      // doesn't have any arguments before the "...", we this at least makes it
      // possible for this symbol to be resolved by the linker.
      NewType = FunctionType::get(F.getFunctionType()->getReturnType(),
                                  F.getFunctionType()->params(), false);
    }

    Function *NewF =
        Function::Create(NewType, F.getLinkage(), F.getName() + ".fixed_sig");
    NewF->setAttributes(F.getAttributes());
    NewF->removeFnAttr("no-prototype");
    Replacements.emplace_back(&F, NewF);
  }

  for (auto &Pair : Replacements) {
    Function *OldF = Pair.first;
    Function *NewF = Pair.second;
    std::string Name = std::string(OldF->getName());
    M.getFunctionList().push_back(NewF);
    OldF->replaceAllUsesWith(
        ConstantExpr::getPointerBitCastOrAddrSpaceCast(NewF, OldF->getType()));
    OldF->eraseFromParent();
    NewF->setName(Name);
  }

  return !Replacements.empty();
}

bool MSILAddMissingPrototypesLegacy::runOnModule(Module &M) {
  return addMissingPrototypes(M);
}
