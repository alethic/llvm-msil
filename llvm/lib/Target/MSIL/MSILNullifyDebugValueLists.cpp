//===-- MSILNullifyDebugValueLists.cpp - Nullify DBG_VALUE_LISTs ----------===//
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
/// This file declares the pass that sets every DBG_VALUE_LIST undefined,
/// because MSILDebugValueManager cannot handle them.
///
/// Until M3 it is a stub that changes nothing; M3 replaces it with a copy of
/// WebAssemblyNullifyDebugValueLists.cpp.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "msil-nullify-dbg-value-lists"

namespace {
class MSILNullifyDebugValueListsLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "MSIL Nullify DBG_VALUE_LISTs";
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
  MSILNullifyDebugValueListsLegacy() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char MSILNullifyDebugValueListsLegacy::ID = 0;
INITIALIZE_PASS(MSILNullifyDebugValueListsLegacy, DEBUG_TYPE,
                "MSIL Nullify DBG_VALUE_LISTs", false, false)

FunctionPass *llvm::createMSILNullifyDebugValueListsLegacyPass() {
  return new MSILNullifyDebugValueListsLegacy();
}
