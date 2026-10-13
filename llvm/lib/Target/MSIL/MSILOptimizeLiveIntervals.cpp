//===-- MSILOptimizeLiveIntervals.cpp - LiveInterval processing -----------===//
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
/// This file declares the pass that splits virtual registers with several
/// independent live ranges into separate registers before stackification.
///
/// Until M3 it is a stub that changes nothing; M3 replaces it with a copy of
/// WebAssemblyOptimizeLiveIntervals.cpp.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "msil-optimize-live-intervals"

namespace {
class MSILOptimizeLiveIntervalsLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "MSIL Optimize Live Intervals";
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
  MSILOptimizeLiveIntervalsLegacy() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char MSILOptimizeLiveIntervalsLegacy::ID = 0;
INITIALIZE_PASS(MSILOptimizeLiveIntervalsLegacy, DEBUG_TYPE,
                "Optimize LiveIntervals for MSIL", false, false)

FunctionPass *llvm::createMSILOptimizeLiveIntervalsLegacyPass() {
  return new MSILOptimizeLiveIntervalsLegacy();
}
