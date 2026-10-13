//===-- MSILExplicitLocals.cpp - Make locals explicit ---------------------===//
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
/// This file declares the pass that turns the virtual registers that are not
/// stackified into CIL arguments and locals, with explicit ldarg/starg and
/// ldloc/stloc instructions.
///
/// Until M3 it is a stub that changes nothing; M3 replaces it with a copy of
/// WebAssemblyExplicitLocals.cpp.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "msil-explicit-locals"

namespace {
class MSILExplicitLocalsLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override { return "MSIL Explicit Locals"; }

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
  MSILExplicitLocalsLegacy() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char MSILExplicitLocalsLegacy::ID = 0;
INITIALIZE_PASS(MSILExplicitLocalsLegacy, DEBUG_TYPE,
                "Convert registers to MSIL locals", false, false)

FunctionPass *llvm::createMSILExplicitLocalsLegacyPass() {
  return new MSILExplicitLocalsLegacy();
}
