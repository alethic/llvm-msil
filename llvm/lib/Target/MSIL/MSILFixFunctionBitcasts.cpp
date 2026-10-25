//===-- MSILFixFunctionBitcasts.cpp - Fix function bitcasts ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyFixFunctionBitcasts.cpp
// at d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Fix bitcasted functions.
///
/// CIL requires caller and callee signatures to match, however in LLVM,
/// some amount of slop is vaguely permitted. Detect mismatch by looking for
/// bitcasts of functions and rewrite them to use wrapper functions instead.
///
/// This doesn't catch all cases, such as when a function's address is taken in
/// one place and casted in another, but it works for many common cases.
///
/// Note that LLVM already optimizes away function bitcasts in common cases by
/// dropping arguments as needed, so this pass only ends up getting used in less
/// common cases.
///
/// Unlike WebAssembly:
/// - main is not renamed and gets no (argc, argv) wrapper: the generated entry
///   point '<msil>Main' calls main with its own signature (decision D27).
/// - A call with a variadic type of a function that is not variadic is a K&R
///   call. MSIL clang emits fixed-arity calls on every MSIL triple, but IR from
///   producers that use the native x86-64 SysV convention (native clang, whose
///   datalayout is the same, and hand-written IR) gives a call through an
///   unprototyped function type a variadic type whose fixed parameters are all
///   of the call's arguments (X86_64TargetCodeGenInfo::isNoProtoCallVariadic).
///   In C it works like a non-variadic call, so the call becomes one, and it
///   gets a wrapper only if its type still differs from the callee's.
/// - Calls of variadic functions keep their callee, so that
///   MSILTargetLowering::LowerCall diagnoses them in the caller (decision D19)
///   instead of in a wrapper.
/// - Every call whose type differs from its callee's gets a wrapper, also when
///   the arguments are only trivially castable (i64 vs ptr): LowerCall accepts
///   a direct call only if its type is the callee's.
/// - A wrapper keeps only the parameter and return attributes of the callee
///   whose types it shares; the others may not apply to its types.
/// LowerCall stays the backstop for the calls that keep a mismatched type
/// (struct types, see createWrapper).
///
/// In object mode (MSILTargetMachine::isObjectMode(), plan 5.8.10, R3) the
/// callee of a call may be defined in another object, which only msil-ld
/// sees, so:
/// - A call of a declaration for the linker (a declaration, extern_weak or
///   available_externally: Function::isDeclarationForLinker) gets no wrapper.
///   It stays a direct call with its own type, LowerCall builds it with the
///   call-site signature, and the object names the symbol with that
///   signature (one MemberRef per name and signature); msil-ld adapts it to
///   the definition.
/// - A call with a variadic type of a prototype-less declaration (it keeps its
///   variadic type and its "no-prototype" attribute in object mode,
///   MSILAddMissingPrototypes) becomes the non-variadic call of all of its
///   arguments, as a variadic call of a function that is not variadic does.
/// - A call through an alias keeps the alias as its callee: the alias is a
///   symbol of the object, and a weak one must stay overridable (plan 5.8.3
///   R1).
/// Calls of definitions keep their wrappers, and calls of truly variadic
/// functions are left to LowerCall's error (decision D19). The pass asks for
/// the mode when it runs, through the TargetPassConfig
/// (CodeGenTargetMachineImpl adds the IR passes before it creates the
/// streamer that decides the mode); without one (opt) it keeps the
/// whole-program behaviour.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "MSILTargetMachine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "msil-fix-function-bitcasts"

namespace {
class MSILFixFunctionBitcastsLegacy final : public ModulePass {
  StringRef getPassName() const override {
    return "MSIL Fix Function Bitcasts";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    ModulePass::getAnalysisUsage(AU);
  }

  bool runOnModule(Module &M) override;

public:
  static char ID;
  MSILFixFunctionBitcastsLegacy() : ModulePass(ID) {}
};
} // End anonymous namespace

char MSILFixFunctionBitcastsLegacy::ID = 0;
INITIALIZE_PASS(MSILFixFunctionBitcastsLegacy, DEBUG_TYPE,
                "Fix mismatching bitcasts for MSIL", false, false)

ModulePass *llvm::createMSILFixFunctionBitcastsLegacyPass() {
  return new MSILFixFunctionBitcastsLegacy();
}

// Recursively descend the def-use lists from V to find non-bitcast users of
// bitcasts of V. In object mode a call through an alias keeps the alias (see
// the file comment), so aliases are not looked through.
static void findUses(Value *V, Function &F,
                     SmallVectorImpl<std::pair<CallBase *, Function *>> &Uses,
                     bool ObjectMode) {
  for (User *U : V->users()) {
    if (auto *BC = dyn_cast<BitCastOperator>(U))
      findUses(BC, F, Uses, ObjectMode);
    else if (auto *A = dyn_cast<GlobalAlias>(U); A && !ObjectMode)
      findUses(A, F, Uses, ObjectMode);
    else if (auto *CB = dyn_cast<CallBase>(U)) {
      Value *Callee = CB->getCalledOperand();
      if (Callee != V)
        // Skip calls where the function isn't the callee
        continue;
      if (CB->getFunctionType() == F.getFunctionType())
        // Skip uses that are immediately called
        continue;
      Uses.push_back(std::make_pair(CB, &F));
    }
  }
}

// Give Wrapper the attributes of F that apply to it. The parameter and return
// attributes of F describe F's types, so only those of the parameters and of
// the result whose types the wrapper shares with F are kept: the verifier
// rejects, for example, an attribute on a parameter the wrapper does not have,
// or signext on a double. If the results differ, the kept parameters also lose
// the attributes that describe F's result: 'returned', and 'sret', which the
// verifier allows only in a function that returns void ('Invalid struct return
// type!'); F returns void if it has an sret parameter, so a wrapper with
// another result does not (a 'void (ptr sret(T))' callee called as
// 'i32 (ptr)'). A wrapper that returns void like F keeps sret. The wrapper has
// a real body, a call of F or a trap, so it is never naked: the verifier
// rejects a naked function that uses its arguments, and ISel would diagnose a
// naked F a second time in the wrapper.
static void setWrapperAttributes(Function *Wrapper, Function *F) {
  LLVMContext &C = F->getContext();
  FunctionType *FTy = F->getFunctionType();
  FunctionType *Ty = Wrapper->getFunctionType();
  bool SameRtnType = FTy->getReturnType() == Ty->getReturnType();
  AttributeList Attrs = F->getAttributes();
  Attrs = Attrs.removeFnAttribute(C, Attribute::Naked);
  if (!SameRtnType)
    Attrs = Attrs.removeAttributesAtIndex(C, AttributeList::ReturnIndex);
  for (unsigned I = 0, E = FTy->getNumParams(); I != E; ++I) {
    if (I >= Ty->getNumParams() || Ty->getParamType(I) != FTy->getParamType(I))
      Attrs = Attrs.removeParamAttributes(C, I);
    else if (!SameRtnType) {
      Attrs = Attrs.removeParamAttribute(C, I, Attribute::Returned);
      Attrs = Attrs.removeParamAttribute(C, I, Attribute::StructRet);
    }
  }
  Wrapper->setAttributes(Attrs);
}

// Create a wrapper function with type Ty that calls F (which may have a
// different type). Attempt to support common bitcasted function idioms:
//  - Call with more arguments than needed: arguments are dropped
//  - Call with fewer arguments than needed: arguments are filled in with poison
//  - Return value is not needed: drop it
//  - Return value needed but not present: supply a poison value
//
// Unlike WebAssembly, a wrapper is created even if all the argument types are
// trivially castable to one another (i.e. I64 vs pointer type): the call keeps
// the callee only if its type is the callee's own, which is what
// MSILTargetLowering::LowerCall checks.
//
// If there is a type mismatch that we know would result in an invalid CIL
// method then generate wrapper that contains unreachable (i.e. abort at
// runtime).  Such programs are deep into undefined behaviour territory,
// but we choose to fail at runtime rather than generate and invalid module
// or fail at compiler time.  The reason we delay the error is that we want
// to support the CMake which expects to be able to compile and link programs
// that refer to functions with entirely incorrect signatures (this is how
// CMake detects the existence of a function in a toolchain).
//
// For bitcasts that involve struct types we don't know at this stage if they
// would be equivalent at the CIL level and so we can't know if we need to
// generate a wrapper.
static Function *createWrapper(Function *F, FunctionType *Ty) {
  Module *M = F->getParent();

  Function *Wrapper = Function::Create(Ty, Function::PrivateLinkage,
                                       F->getName() + "_bitcast", M);
  setWrapperAttributes(Wrapper, F);
  BasicBlock *BB = BasicBlock::Create(M->getContext(), "body", Wrapper);
  const DataLayout &DL = BB->getDataLayout();
  IRBuilder<> Builder(BB);

  // Determine what arguments to pass.
  SmallVector<Value *, 4> Args;
  Function::arg_iterator AI = Wrapper->arg_begin();
  Function::arg_iterator AE = Wrapper->arg_end();
  FunctionType::param_iterator PI = F->getFunctionType()->param_begin();
  FunctionType::param_iterator PE = F->getFunctionType()->param_end();
  bool TypeMismatch = false;
  bool WrapperNeeded = false;

  Type *ExpectedRtnType = F->getFunctionType()->getReturnType();
  Type *RtnType = Ty->getReturnType();

  if (F->getFunctionType() != Ty)
    WrapperNeeded = true;

  for (; AI != AE && PI != PE; ++AI, ++PI) {
    Type *ArgType = AI->getType();
    Type *ParamType = *PI;

    if (ArgType == ParamType) {
      Args.push_back(&*AI);
    } else {
      if (CastInst::isBitOrNoopPointerCastable(ArgType, ParamType, DL)) {
        Args.push_back(Builder.CreateBitOrPointerCast(AI, ParamType, "cast"));
      } else if (ArgType->isStructTy() || ParamType->isStructTy()) {
        LLVM_DEBUG(dbgs() << "createWrapper: struct param type in bitcast: "
                          << F->getName() << "\n");
        WrapperNeeded = false;
      } else {
        LLVM_DEBUG(dbgs() << "createWrapper: arg type mismatch calling: "
                          << F->getName() << "\n");
        LLVM_DEBUG(dbgs() << "Arg[" << Args.size() << "] Expected: "
                          << *ParamType << " Got: " << *ArgType << "\n");
        TypeMismatch = true;
        break;
      }
    }
  }

  if (WrapperNeeded && !TypeMismatch) {
    for (; PI != PE; ++PI)
      Args.push_back(PoisonValue::get(*PI));
    if (F->isVarArg())
      for (; AI != AE; ++AI)
        Args.push_back(&*AI);

    CallInst *Call = Builder.CreateCall(F, Args);

    // Determine what value to return.
    if (RtnType->isVoidTy()) {
      Builder.CreateRetVoid();
    } else if (ExpectedRtnType->isVoidTy()) {
      LLVM_DEBUG(dbgs() << "Creating dummy return: " << *RtnType << "\n");
      Builder.CreateRet(PoisonValue::get(RtnType));
    } else if (RtnType == ExpectedRtnType) {
      Builder.CreateRet(Call);
    } else if (CastInst::isBitOrNoopPointerCastable(ExpectedRtnType, RtnType,
                                                    DL)) {
      Builder.CreateRet(Builder.CreateBitOrPointerCast(Call, RtnType, "cast"));
    } else if (RtnType->isStructTy() || ExpectedRtnType->isStructTy()) {
      LLVM_DEBUG(dbgs() << "createWrapper: struct return type in bitcast: "
                        << F->getName() << "\n");
      WrapperNeeded = false;
    } else {
      LLVM_DEBUG(dbgs() << "createWrapper: return type mismatch calling: "
                        << F->getName() << "\n");
      LLVM_DEBUG(dbgs() << "Expected: " << *ExpectedRtnType
                        << " Got: " << *RtnType << "\n");
      TypeMismatch = true;
    }
  }

  if (TypeMismatch) {
    // Create a new wrapper that simply contains `unreachable`.
    Wrapper->eraseFromParent();
    Wrapper = Function::Create(Ty, Function::PrivateLinkage,
                               F->getName() + "_bitcast_invalid", M);
    setWrapperAttributes(Wrapper, F);
    IRBuilder<> Builder(BasicBlock::Create(M->getContext(), "body", Wrapper));
    Builder.CreateUnreachable();
  } else if (!WrapperNeeded) {
    LLVM_DEBUG(dbgs() << "createWrapper: no wrapper needed: " << F->getName()
                      << "\n");
    Wrapper->eraseFromParent();
    return nullptr;
  }
  LLVM_DEBUG(dbgs() << "createWrapper: " << F->getName() << "\n");
  return Wrapper;
}

/// Returns the type of a call with the variadic type \p Ty and the arguments
/// of \p CB as a non-variadic call: \p Ty without the variadic flag, so that
/// every argument is a fixed parameter (MSILAddMissingPrototypes'
/// getNonVariadicCallType computes the same type).
static FunctionType *getNonVariadicType(FunctionType *Ty, const CallBase &CB) {
  SmallVector<Type *, 8> ArgTys;
  for (const Use &Arg : CB.args())
    ArgTys.push_back(Arg->getType());
  return FunctionType::get(Ty->getReturnType(), ArgTys, /*isVarArg=*/false);
}

/// Object mode: makes every call with a variadic type of the prototype-less
/// declaration \p F (variadic, "no-prototype") the non-variadic call of all
/// of its arguments (a K&R call from IR that uses the native x86-64 SysV
/// convention, see the file comment). The call's type may be F's own type
/// ('i32 (...)' for a call without arguments), so every call counts, not
/// only those that findUses collects.
static void makeKnRCallsNonVariadic(Function &F) {
  SmallVector<Value *, 4> Worklist = {&F};
  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();
    for (User *U : V->users()) {
      if (auto *BC = dyn_cast<BitCastOperator>(U)) {
        Worklist.push_back(BC);
        continue;
      }
      auto *CB = dyn_cast<CallBase>(U);
      if (!CB || CB->getCalledOperand() != V ||
          !CB->getFunctionType()->isVarArg())
        continue;
      CB->mutateFunctionType(getNonVariadicType(CB->getFunctionType(), *CB));
    }
  }
}

static bool fixFunctionBitcasts(Module &M, bool ObjectMode) {
  LLVM_DEBUG(dbgs() << "********** Fix Function Bitcasts **********\n");

  SmallVector<std::pair<CallBase *, Function *>, 0> Uses;

  // Collect all the places that need wrappers.
  for (Function &F : M) {
    // Skip to fix when the function is swiftcc or swifttailcc because these
    // calling conventions allow bitcast type difference for swiftself,
    // swifterror, and swiftasync.
    if (F.getCallingConv() == CallingConv::Swift ||
        F.getCallingConv() == CallingConv::SwiftTail)
      continue;
    // Calls of variadic functions are not supported yet. Leave them to
    // MSILTargetLowering::LowerCall, which diagnoses them in the caller.
    // Object mode keeps prototype-less declarations variadic, and their K&R
    // calls become fixed-arity calls.
    if (F.isVarArg()) {
      if (ObjectMode && F.isDeclarationForLinker() &&
          F.hasFnAttribute("no-prototype"))
        makeKnRCallsNonVariadic(F);
      continue;
    }
    findUses(&F, F, Uses, ObjectMode);
  }

  DenseMap<std::pair<Function *, FunctionType *>, Function *> Wrappers;

  for (auto &UseFunc : Uses) {
    CallBase *CB = UseFunc.first;
    Function *F = UseFunc.second;
    FunctionType *Ty = CB->getFunctionType();

    // F is not variadic, so a variadic call of F is a K&R call from IR that
    // uses the native x86-64 SysV convention (see the file comment): make it
    // a non-variadic call of all of its arguments.
    if (Ty->isVarArg()) {
      Ty = getNonVariadicType(Ty, *CB);
      CB->mutateFunctionType(Ty);
      if (Ty == F->getFunctionType())
        continue;
    }

    // In object mode the definition of an undefined callee is in another
    // object: the call keeps its own type, and the object names the symbol
    // with the call-site signature (see the file comment).
    if (ObjectMode && F->isDeclarationForLinker())
      continue;

    auto Pair = Wrappers.try_emplace(std::make_pair(F, Ty));
    if (Pair.second)
      Pair.first->second = createWrapper(F, Ty);

    Function *Wrapper = Pair.first->second;
    if (!Wrapper)
      continue;

    CB->setCalledOperand(Wrapper);
  }

  return true;
}

bool MSILFixFunctionBitcastsLegacy::runOnModule(Module &M) {
  // Asked here, when the pass runs, not when it is added (see the file
  // comment).
  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  return fixFunctionBitcasts(
      M, TPC && TPC->getTM<MSILTargetMachine>().isObjectMode());
}
