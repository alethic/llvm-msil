//===-- MSILCleanCodeAfterTrap.cpp - Clean Code After Trap ----------------===//
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
/// This file declares the pass that removes the instructions that follow a
/// trap, which is a terminator.
///
/// Until M3 it is a stub that changes nothing; M3 replaces it with a copy of
/// WebAssemblyCleanCodeAfterTrap.cpp.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "msil-clean-code-after-trap"

namespace {
class MSILCleanCodeAfterTrapLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "MSIL Clean Code After Trap";
  }

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
  MSILCleanCodeAfterTrapLegacy() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char MSILCleanCodeAfterTrapLegacy::ID = 0;
INITIALIZE_PASS(MSILCleanCodeAfterTrapLegacy, DEBUG_TYPE,
                "MSIL Clean Code After Trap", false, false)

FunctionPass *llvm::createMSILCleanCodeAfterTrapLegacyPass() {
  return new MSILCleanCodeAfterTrapLegacy();
}
