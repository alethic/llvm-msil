//===-- MSILOptimizeReturned.cpp - Optimize "returned" attributes ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// STUB: implemented in M9
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the pass that replaces uses of an argument marked
/// "returned" by uses of the call's result.
///
/// Until M9 it is a stub that changes nothing; M9 replaces it with a copy of
/// WebAssemblyOptimizeReturned.cpp.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/Pass.h"
using namespace llvm;

#define DEBUG_TYPE "msil-optimize-returned"

namespace {
class MSILOptimizeReturnedLegacy final : public FunctionPass {
  StringRef getPassName() const override { return "MSIL Optimize Returned"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    FunctionPass::getAnalysisUsage(AU);
  }

  bool runOnFunction(Function &F) override {
    // Stub: changes nothing until M9.
    return false;
  }

public:
  static char ID; // Pass identification, replacement for typeid
  MSILOptimizeReturnedLegacy() : FunctionPass(ID) {}
};
} // end anonymous namespace

char MSILOptimizeReturnedLegacy::ID = 0;
INITIALIZE_PASS(MSILOptimizeReturnedLegacy, DEBUG_TYPE,
                "Optimize calls with \"returned\" attributes for MSIL", false,
                false)

FunctionPass *llvm::createMSILOptimizeReturnedLegacyPass() {
  return new MSILOptimizeReturnedLegacy();
}
