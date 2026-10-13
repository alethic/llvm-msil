//===-- MSILSetMemFlags.cpp - Set memory flags on loads and stores --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// STUB: implemented in M7
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the pass that copies the alignment and volatility of
/// each memory operand into the memflags immediate of its load or store
/// (the unaligned. and volatile. prefixes), right after instruction
/// selection.
///
/// Until M7 it is a stub that changes nothing; M7 replaces it with
/// a new pass modeled on WebAssemblySetP2AlignOperands.cpp.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "msil-set-mem-flags"

namespace {
class MSILSetMemFlagsLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override { return "MSIL Set Memory Flags"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    // Stub: changes nothing until M7.
    return false;
  }

public:
  static char ID; // Pass identification, replacement for typeid
  MSILSetMemFlagsLegacy() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char MSILSetMemFlagsLegacy::ID = 0;
INITIALIZE_PASS(MSILSetMemFlagsLegacy, DEBUG_TYPE,
                "Set the memory-flags operands for MSIL loads and stores",
                false, false)

FunctionPass *llvm::createMSILSetMemFlagsLegacyPass() {
  return new MSILSetMemFlagsLegacy();
}
