//===-- MSILFixFunctionBitcasts.cpp - Fix function bitcasts for MSIL ------===//
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
/// This file declares the pass that wraps the callees of calls whose
/// function type differs from the callee's own, because a CIL call needs
/// the exact signature of its callee.
///
/// Until M9 it is a stub that changes nothing; M9 replaces it with a copy of
/// WebAssemblyFixFunctionBitcasts.cpp, without the renaming of main.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/Pass.h"
using namespace llvm;

#define DEBUG_TYPE "msil-fix-function-bitcasts"

namespace {
class MSILFixFunctionBitcastsLegacy final : public ModulePass {
  StringRef getPassName() const override {
    return "MSIL Fix Function Bitcasts";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    ModulePass::getAnalysisUsage(AU);
  }

  bool runOnModule(Module &M) override {
    // Stub: changes nothing until M9.
    return false;
  }

public:
  static char ID; // Pass identification, replacement for typeid
  MSILFixFunctionBitcastsLegacy() : ModulePass(ID) {}
};
} // end anonymous namespace

char MSILFixFunctionBitcastsLegacy::ID = 0;
INITIALIZE_PASS(MSILFixFunctionBitcastsLegacy, DEBUG_TYPE,
                "Fix mismatching bitcasts for MSIL", false, false)

ModulePass *llvm::createMSILFixFunctionBitcastsLegacyPass() {
  return new MSILFixFunctionBitcastsLegacy();
}
