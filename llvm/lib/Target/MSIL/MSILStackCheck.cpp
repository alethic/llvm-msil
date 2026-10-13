//===-- MSILStackCheck.cpp - Compute and check .maxstack ------------------===//
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
/// This file declares the last machine pass, which computes the .maxstack of
/// each method and verifies the CIL evaluation stack rules.
///
/// Until M3 it is a stub that changes nothing; M3 implements it.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "msil-stack-check"

namespace {
class MSILStackCheckLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override { return "MSIL Stack Check"; }

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
  MSILStackCheckLegacy() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char MSILStackCheckLegacy::ID = 0;
INITIALIZE_PASS(MSILStackCheckLegacy, DEBUG_TYPE,
                "Compute .maxstack and check the CIL stack rules for MSIL",
                false, false)

FunctionPass *llvm::createMSILStackCheckLegacyPass() {
  return new MSILStackCheckLegacy();
}
