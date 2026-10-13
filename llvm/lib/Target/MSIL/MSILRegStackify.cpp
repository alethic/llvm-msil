//===-- MSILRegStackify.cpp - Register Stackification ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// STUB: implemented in M3
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the pass that reorders instructions and marks virtual
/// registers as stackified, so that their values stay on the CIL evaluation
/// stack instead of in locals.
///
/// Until M3 it is a stub that changes nothing; M3 replaces it with a copy of
/// WebAssemblyRegStackify.cpp.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "msil-reg-stackify"

namespace {
class MSILRegStackifyLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override { return "MSIL Register Stackify"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    // Stub: changes nothing until M3.
    return false;
  }

public:
  static char ID; // Pass identification, replacement for typeid
  explicit MSILRegStackifyLegacy(
      CodeGenOptLevel OptLevel = CodeGenOptLevel::Default)
      : MachineFunctionPass(ID) {
    // M3: LiveIntervals and the dominator tree are used only when
    // optimizing.
    (void)OptLevel;
  }
};
} // end anonymous namespace

char MSILRegStackifyLegacy::ID = 0;
INITIALIZE_PASS(MSILRegStackifyLegacy, DEBUG_TYPE,
                "Reorder instructions to use the MSIL evaluation stack", false,
                false)

FunctionPass *llvm::createMSILRegStackifyLegacyPass(CodeGenOptLevel OptLevel) {
  return new MSILRegStackifyLegacy(OptLevel);
}
