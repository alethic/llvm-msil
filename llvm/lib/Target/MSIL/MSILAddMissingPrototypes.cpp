//===-- MSILAddMissingPrototypes.cpp - Fix prototypeless functions --------===//
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
/// This file declares the pass that gives each declaration clang marks
/// "no-prototype" the function type its call sites use, because a CIL
/// call needs the exact signature of its callee.
///
/// Until M9 it is a stub that changes nothing; M9 replaces it with a copy of
/// WebAssemblyAddMissingPrototypes.cpp.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/Pass.h"
using namespace llvm;

#define DEBUG_TYPE "msil-add-missing-prototypes"

namespace {
class MSILAddMissingPrototypesLegacy final : public ModulePass {
  StringRef getPassName() const override {
    return "Add prototypes to prototypes-less functions";
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
  MSILAddMissingPrototypesLegacy() : ModulePass(ID) {}
};
} // end anonymous namespace

char MSILAddMissingPrototypesLegacy::ID = 0;
INITIALIZE_PASS(MSILAddMissingPrototypesLegacy, DEBUG_TYPE,
                "Add prototypes to prototypes-less functions", false, false)

ModulePass *llvm::createMSILAddMissingPrototypesLegacyPass() {
  return new MSILAddMissingPrototypesLegacy();
}
