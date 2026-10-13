//===-- MSILRegNumbering.cpp - Register Numbering -------------------------===//
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
/// This file declares the pass that assigns MSIL register numbers to the
/// virtual registers that are not stackified.
///
/// Until M3 it is a stub that changes nothing; M3 replaces it with a copy of
/// WebAssemblyRegNumbering.cpp.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "msil-reg-numbering"

namespace {
class MSILRegNumberingLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override { return "MSIL Register Numbering"; }

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
  MSILRegNumberingLegacy() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char MSILRegNumberingLegacy::ID = 0;
INITIALIZE_PASS(MSILRegNumberingLegacy, DEBUG_TYPE,
                "Assigns MSIL register numbers for virtual registers", false,
                false)

FunctionPass *llvm::createMSILRegNumberingLegacyPass() {
  return new MSILRegNumberingLegacy();
}
