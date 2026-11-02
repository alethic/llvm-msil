//===-- MSILLowerDynamicAlloca.cpp - Dynamic allocas for MSIL -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Prepares the dynamic allocas of every function for instruction selection
/// (ImplementationPlan.md 5.15.8, H0; slot P9 of the IR pipeline, 5.13.6).
///
/// A dynamic alloca is one that is not static (a variable count, or an alloca
/// outside the entry block). The static frame is one 'localloc' at entry
/// (decision D13); a dynamic alloca becomes a 'localloc' of its own in the
/// middle of the function (ISel's DYNAMIC_STACKALLOC, LOCALLOC_I64). CIL frees
/// localloc'ed memory only when the method returns, so that is correct only
/// for an alloca that runs at most once per call: one that lies on no CFG
/// cycle. Its memory is then bounded like the static frame's, and the CLR
/// frees it when an exception or a longjmp leaves the frame.
///
/// - An alloca on no cycle stays an alloca.
/// - In a function without a cyclic alloca, every llvm.stacksave and
///   llvm.stackrestore is deleted: a restore frees nothing that the
///   localloc of an acyclic alloca holds, so it has nothing to do. A saved
///   value that something other than a restore reads becomes null.
/// - A dynamic alloca on a cycle (a VLA or alloca in a loop) needs the shadow
///   stack, which does not exist yet: every such function gets one error, and
///   its body is dropped (it becomes a lone 'unreachable', so the function
///   stays defined and its callers and its address compile unchanged; plan
///   section 2, 'Diagnostics leave well-formed output').
///
/// The cycles are computed with CycleInfo, which also sees irreducible
/// control flow, on the CFG after every pass that can add a cycle around an
/// alloca (setjmp's re-entry runs at P8, before this pass).
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/CycleInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "msil-lower-dynamic-alloca"

namespace {
class MSILLowerDynamicAllocaLegacy final : public ModulePass {
  StringRef getPassName() const override {
    return "MSIL Lower Dynamic Allocas";
  }

  bool runOnModule(Module &M) override;

public:
  static char ID;
  MSILLowerDynamicAllocaLegacy() : ModulePass(ID) {}
};
} // end anonymous namespace

char MSILLowerDynamicAllocaLegacy::ID = 0;
INITIALIZE_PASS(MSILLowerDynamicAllocaLegacy, DEBUG_TYPE,
                "MSIL Lower Dynamic Allocas", false, false)

ModulePass *llvm::createMSILLowerDynamicAllocaLegacyPass() {
  return new MSILLowerDynamicAllocaLegacy();
}

/// The error for a dynamic alloca on a CFG cycle (5.15.8 H0, until the shadow
/// stack of M32).
static constexpr const char *CyclicAllocaUnsupported =
    "a dynamic stack allocation that can repeat within one call (a VLA or "
    "alloca in a loop) is not supported for MSIL yet";

/// Returns the first dynamic alloca of \p F that lies on a CFG cycle, or null
/// if there is none.
static const AllocaInst *findCyclicAlloca(Function &F) {
  SmallVector<const AllocaInst *, 4> Dynamic;
  for (const BasicBlock &BB : F)
    for (const Instruction &I : BB)
      if (const auto *AI = dyn_cast<AllocaInst>(&I);
          AI && !AI->isStaticAlloca())
        Dynamic.push_back(AI);
  if (Dynamic.empty())
    return nullptr;

  CycleInfo CI;
  CI.compute(F);
  for (const AllocaInst *AI : Dynamic)
    if (CI.getCycle(AI->getParent()))
      return AI;
  return nullptr;
}

/// Replaces the body of \p F by a lone 'unreachable', keeping the function
/// defined with its linkage, comdat and attributes.
static void dropBody(Function &F) {
  // No instruction refers to another one once every operand is dropped, so
  // the blocks can then go in any order.
  for (BasicBlock &BB : F)
    BB.dropAllReferences();
  while (!F.empty())
    F.back().eraseFromParent();
  LLVMContext &Ctx = F.getContext();
  new UnreachableInst(Ctx, BasicBlock::Create(Ctx, "entry", &F));
}

/// Deletes every llvm.stacksave and llvm.stackrestore of \p F. Returns true
/// if one was deleted.
static bool deleteStackSaveRestore(Function &F) {
  SmallVector<IntrinsicInst *, 8> ToErase;
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      if (auto *II = dyn_cast<IntrinsicInst>(&I);
          II && (II->getIntrinsicID() == Intrinsic::stacksave ||
                 II->getIntrinsicID() == Intrinsic::stackrestore))
        ToErase.push_back(II);
  // The restores first, which are the usual users of the saves.
  for (IntrinsicInst *II : ToErase)
    if (II->getIntrinsicID() == Intrinsic::stackrestore)
      II->eraseFromParent();
  for (IntrinsicInst *II : ToErase) {
    if (II->getIntrinsicID() != Intrinsic::stacksave)
      continue;
    if (!II->use_empty())
      II->replaceAllUsesWith(
          ConstantPointerNull::get(cast<PointerType>(II->getType())));
    II->eraseFromParent();
  }
  return !ToErase.empty();
}

bool MSILLowerDynamicAllocaLegacy::runOnModule(Module &M) {
  LLVM_DEBUG(dbgs() << "********** Lower Dynamic Allocas **********\n");
  bool Changed = false;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (const AllocaInst *AI = findCyclicAlloca(F)) {
      LLVM_DEBUG(dbgs() << "Cyclic dynamic alloca in " << F.getName() << ": "
                        << *AI << '\n');
      F.getContext().diagnose(DiagnosticInfoUnsupported(
          F, CyclicAllocaUnsupported, AI->getDebugLoc()));
      dropBody(F);
      Changed = true;
      continue;
    }
    Changed |= deleteStackSaveRestore(F);
  }
  return Changed;
}
