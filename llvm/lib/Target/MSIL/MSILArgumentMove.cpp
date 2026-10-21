//===-- MSILArgumentMove.cpp - Argument instruction moving ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyArgumentMove.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file moves ARGUMENT instructions after ScheduleDAG scheduling.
///
/// Arguments are really live-in registers, however, since we use virtual
/// registers and LLVM doesn't support live-in virtual registers, we're
/// currently making do with ARGUMENT instructions which are placed at the top
/// of the entry block. The trick is to get them to *stay* at the top of the
/// entry block.
///
/// The ARGUMENTS physical register keeps these instructions pinned in place
/// during liveness-aware CodeGen passes, however one thing which does not
/// respect this is the ScheduleDAG scheduler. This pass is therefore run
/// immediately after that.
///
/// Instruction selection already moves the ARGUMENTs back to the top
/// (MSIL::moveArgumentsToTop, called by MSILDAGToDAGISel after scheduling),
/// so that the MIR llc prints when it stops after instruction selection
/// passes the machine verifier, and this pass changes nothing on its output.
/// It stays in the pipeline for MIR input, which may have an ARGUMENT
/// anywhere in the entry block.
///
/// This is all hopefully a temporary solution until we find a better solution
/// for describing the live-in nature of arguments.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "MSILUtilities.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "msil-argument-move"

namespace {
class MSILArgumentMoveLegacy final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  MSILArgumentMoveLegacy() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "MSIL Argument Move"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char MSILArgumentMoveLegacy::ID = 0;
INITIALIZE_PASS(MSILArgumentMoveLegacy, DEBUG_TYPE,
                "Move ARGUMENT instructions for MSIL", false, false)

FunctionPass *llvm::createMSILArgumentMoveLegacyPass() {
  return new MSILArgumentMoveLegacy();
}

bool MSILArgumentMoveLegacy::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Argument Move **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  return MSIL::moveArgumentsToTop(MF);
}
