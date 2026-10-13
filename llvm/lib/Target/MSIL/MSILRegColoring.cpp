//===-- MSILRegColoring.cpp - Register coloring ---------------------------===//
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
/// This file declares the pass that merges virtual registers whose live
/// ranges do not interfere, to reduce the number of CIL locals.
///
/// Until M3 it is a stub that changes nothing; M3 replaces it with a copy of
/// WebAssemblyRegColoring.cpp.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "msil-reg-coloring"

namespace {
class MSILRegColoringLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override { return "MSIL Register Coloring"; }

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
  MSILRegColoringLegacy() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char MSILRegColoringLegacy::ID = 0;
INITIALIZE_PASS(MSILRegColoringLegacy, DEBUG_TYPE,
                "Minimize number of registers used", false, false)

FunctionPass *llvm::createMSILRegColoringLegacyPass() {
  return new MSILRegColoringLegacy();
}
