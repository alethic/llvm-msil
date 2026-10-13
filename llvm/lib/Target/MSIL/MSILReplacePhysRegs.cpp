//===-- MSILReplacePhysRegs.cpp - Replace phys regs with virt regs --------===//
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
/// This file declares the pass that replaces the FP physical register with
/// a virtual register after prologue and epilogue insertion.
///
/// Until M3 it is a stub that changes nothing; M3 replaces it with a copy of
/// WebAssemblyReplacePhysRegs.cpp.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "msil-replace-phys-regs"

namespace {
class MSILReplacePhysRegsLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "MSIL Replace Physical Registers";
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
  MSILReplacePhysRegsLegacy() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char MSILReplacePhysRegsLegacy::ID = 0;
INITIALIZE_PASS(MSILReplacePhysRegsLegacy, DEBUG_TYPE,
                "Replace physical registers with virtual registers", false,
                false)

FunctionPass *llvm::createMSILReplacePhysRegsLegacyPass() {
  return new MSILReplacePhysRegsLegacy();
}
