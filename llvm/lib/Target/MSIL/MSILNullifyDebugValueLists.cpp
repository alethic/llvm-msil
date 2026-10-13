//=== MSILNullifyDebugValueLists.cpp - Nullify DBG_VALUE_LISTs   ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from
// llvm/lib/Target/WebAssembly/WebAssemblyNullifyDebugValueLists.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Nullify DBG_VALUE_LISTs instructions as a temporary measure before we
/// implement DBG_VALUE_LIST handling in MSILDebugValueManager.
/// See https://github.com/llvm/llvm-project/issues/49705.
/// TODO Correctly handle DBG_VALUE_LISTs
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

  bool runOnMachineFunction(MachineFunction &MF) override;

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

static bool nullifyDebugValueLists(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Nullify DBG_VALUE_LISTs **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');
  bool Changed = false;
  // Our backend, including MSILDebugValueManager, currently cannot
  // handle DBG_VALUE_LISTs correctly. So this makes them undefined, which will
  // appear as "optimized out".
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      if (MI.getOpcode() == TargetOpcode::DBG_VALUE_LIST) {
        MI.setDebugValueUndef();
        Changed = true;
      }
    }
  }
  return Changed;
}

bool MSILNullifyDebugValueListsLegacy::runOnMachineFunction(
    MachineFunction &MF) {
  return nullifyDebugValueLists(MF);
}
