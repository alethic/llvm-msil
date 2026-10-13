//===-- MSILArgumentMove.cpp - Argument instruction moving pass -----------===//
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
/// This file declares the pass that moves ARGUMENT instructions back to the
/// top of the entry block after instruction scheduling.
///
/// Until M3 it is a stub that changes nothing; M3 replaces it with a copy of
/// WebAssemblyArgumentMove.cpp.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "msil-argument-move"

namespace {
class MSILArgumentMoveLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override { return "MSIL Argument Move"; }

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
  MSILArgumentMoveLegacy() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char MSILArgumentMoveLegacy::ID = 0;
INITIALIZE_PASS(MSILArgumentMoveLegacy, DEBUG_TYPE,
                "Move ARGUMENT instructions for MSIL", false, false)

FunctionPass *llvm::createMSILArgumentMoveLegacyPass() {
  return new MSILArgumentMoveLegacy();
}
