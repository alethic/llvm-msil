//===-- MSILRegNumbering.cpp - Register Numbering -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyRegNumbering.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a pass which assigns MSIL register
/// numbers for CodeGen virtual registers.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSIL.h"
#include "MSILMachineFunctionInfo.h"
#include "MSILUtilities.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "msil-reg-numbering"

namespace {
class MSILRegNumberingLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override { return "MSIL Register Numbering"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

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

static bool regNumbering(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Register Numbering **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  MSILFunctionInfo &MFI = *MF.getInfo<MSILFunctionInfo>();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  MFI.initWARegs(MRI);

  // MSIL argument registers are in the same index space as local
  // variables. Assign the numbers for them first.
  MachineBasicBlock &EntryMBB = MF.front();
  for (MachineInstr &MI : EntryMBB) {
    if (!MSIL::isArgument(MI.getOpcode()))
      break;

    int64_t Imm = MI.getOperand(1).getImm();
    LLVM_DEBUG(dbgs() << "Arg VReg " << printReg(MI.getOperand(0).getReg())
                      << " -> WAReg " << Imm << "\n");
    MFI.setWAReg(MI.getOperand(0).getReg(), Imm);
  }

  // Then assign regular MSIL registers for all remaining used
  // virtual registers. TODO: Consider sorting the registers by frequency of
  // use, to maximize usage of small immediate fields.
  unsigned NumVRegs = MF.getRegInfo().getNumVirtRegs();
  unsigned NumStackRegs = 0;
  // Start the numbering for locals after the arg regs
  unsigned CurReg = MFI.getParams().size();
  for (unsigned VRegIdx = 0; VRegIdx < NumVRegs; ++VRegIdx) {
    Register VReg = Register::index2VirtReg(VRegIdx);
    // Skip unused registers.
    if (MRI.use_empty(VReg))
      continue;
    // Handle stackified registers.
    if (MFI.isVRegStackified(VReg)) {
      LLVM_DEBUG(dbgs() << "VReg " << printReg(VReg) << " -> WAReg "
                        << (INT32_MIN | NumStackRegs) << "\n");
      MFI.setWAReg(VReg, INT32_MIN | NumStackRegs++);
      continue;
    }
    if (MFI.getWAReg(VReg) == MSIL::UnusedReg) {
      LLVM_DEBUG(dbgs() << "VReg " << printReg(VReg) << " -> WAReg " << CurReg
                        << "\n");
      MFI.setWAReg(VReg, CurReg++);
    }
  }

  return true;
}

bool MSILRegNumberingLegacy::runOnMachineFunction(MachineFunction &MF) {
  return regNumbering(MF);
}
