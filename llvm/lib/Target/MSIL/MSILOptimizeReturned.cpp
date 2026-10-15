//===-- MSILOptimizeReturned.cpp - Optimize "returned" attributes ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyOptimizeReturned.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Optimize calls with "returned" attributes for MSIL.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "msil-optimize-returned"

namespace {
class MSILOptimizeReturnedImpl : public InstVisitor<MSILOptimizeReturnedImpl> {
  DominatorTree *DT = nullptr;

public:
  MSILOptimizeReturnedImpl(DominatorTree *DT) : DT(DT) {}
  bool runOnFunction(Function &F);
  void visitCallBase(CallBase &CB);
};

class MSILOptimizeReturnedLegacy final : public FunctionPass {
  StringRef getPassName() const override { return "MSIL Optimize Returned"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<DominatorTreeWrapperPass>();
    FunctionPass::getAnalysisUsage(AU);
  }

  bool runOnFunction(Function &F) override;

public:
  static char ID;
  MSILOptimizeReturnedLegacy() : FunctionPass(ID) {}
};
} // End anonymous namespace

char MSILOptimizeReturnedLegacy::ID = 0;
INITIALIZE_PASS(MSILOptimizeReturnedLegacy, DEBUG_TYPE,
                "Optimize calls with \"returned\" attributes for MSIL", false,
                false)

FunctionPass *llvm::createMSILOptimizeReturnedLegacyPass() {
  return new MSILOptimizeReturnedLegacy();
}

void MSILOptimizeReturnedImpl::visitCallBase(CallBase &CB) {
  for (unsigned I = 0, E = CB.arg_size(); I < E; ++I)
    if (CB.paramHasAttr(I, Attribute::Returned)) {
      Value *Arg = CB.getArgOperand(I);
      // Ignore constants, globals, undef, etc.
      if (isa<Constant>(Arg))
        continue;
      // The verifier accepts a "returned" argument whose type only converts
      // losslessly to the result's (vectors of the same size), but its uses
      // can only be replaced by a value of the same type.
      if (Arg->getType() != CB.getType())
        continue;
      // Like replaceDominatedUsesWith but using Instruction/Use dominance.
      Arg->replaceUsesWithIf(&CB, [&](Use &U) {
        auto *I = cast<Instruction>(U.getUser());
        return !I->isLifetimeStartOrEnd() && DT->dominates(&CB, U);
      });
    }
}

bool MSILOptimizeReturnedImpl::runOnFunction(Function &F) {
  LLVM_DEBUG(dbgs() << "********** Optimize returned Attributes **********\n"
                       "********** Function: "
                    << F.getName() << '\n');

  visit(F);
  return true;
}

bool MSILOptimizeReturnedLegacy::runOnFunction(Function &F) {
  DominatorTree *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  MSILOptimizeReturnedImpl Impl(DT);
  return Impl.runOnFunction(F);
}
