//===-- MSILInstrInfo.cpp - MSIL Instruction Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyInstrInfo.cpp at
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the MSIL implementation of the TargetInstrInfo class.
///
//===----------------------------------------------------------------------===//

#include "MSILInstrInfo.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSILMachineFunctionInfo.h"
#include "MSILSubtarget.h"
#include "MSILUtilities.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
using namespace llvm;

#define DEBUG_TYPE "msil-instr-info"

#define GET_INSTRINFO_CTOR_DTOR
#include "MSILGenInstrInfo.inc"

MSILInstrInfo::MSILInstrInfo(const MSILSubtarget &STI)
    : MSILGenInstrInfo(STI, RI, MSIL::ADJCALLSTACKDOWN, MSIL::ADJCALLSTACKUP),
      RI(STI.getTargetTriple()) {}

bool MSILInstrInfo::isReMaterializableImpl(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case MSIL::CONST_I32:
  case MSIL::CONST_I64:
  case MSIL::CONST_F32:
  case MSIL::CONST_F64:
  case MSIL::GLOBAL_ADDR:
  case MSIL::FUNC_ADDR:
    // TargetInstrInfo::isReMaterializableImpl misses these
    // because of the ARGUMENTS implicit def, so we manually override it here.
    return true;
  default:
    return TargetInstrInfo::isReMaterializableImpl(MI);
  }
}

void MSILInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I,
                                const DebugLoc &DL, Register DestReg,
                                Register SrcReg, bool KillSrc,
                                bool RenamableDest, bool RenamableSrc) const {
  // This method is called by post-RA expansion, which expects only pregs to
  // exist. However we need to handle both here.
  auto &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *RC =
      DestReg.isVirtual()
          ? MRI.getRegClass(DestReg)
          : MRI.getTargetRegisterInfo()->getMinimalPhysRegClass(DestReg);

  unsigned CopyOpcode = MSIL::getCopyOpcodeForRegClass(RC);

  BuildMI(MBB, I, DL, get(CopyOpcode), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

MachineInstr *MSILInstrInfo::commuteInstructionImpl(MachineInstr &MI,
                                                    bool NewMI, unsigned OpIdx1,
                                                    unsigned OpIdx2) const {
  // If the operands are stackified, we can't reorder them.
  MSILFunctionInfo &MFI =
      *MI.getParent()->getParent()->getInfo<MSILFunctionInfo>();
  if (MFI.isVRegStackified(MI.getOperand(OpIdx1).getReg()) ||
      MFI.isVRegStackified(MI.getOperand(OpIdx2).getReg()))
    return nullptr;

  // Otherwise use the default implementation.
  return TargetInstrInfo::commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
}

// Branch analysis.
bool MSILInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                  MachineBasicBlock *&TBB,
                                  MachineBasicBlock *&FBB,
                                  SmallVectorImpl<MachineOperand> &Cond,
                                  bool /*AllowModify*/) const {
  // No branch is analyzable until the compare-and-branch records exist (M6),
  // so the generic CFG optimizations leave the branches alone.
  return true;
}

unsigned MSILInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                     int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::instr_iterator I = MBB.instr_end();
  unsigned Count = 0;

  while (I != MBB.instr_begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (!I->isTerminator())
      break;
    // Remove the branch.
    I->eraseFromParent();
    I = MBB.instr_end();
    ++Count;
  }

  return Count;
}

unsigned MSILInstrInfo::insertBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    ArrayRef<MachineOperand> Cond, const DebugLoc &DL, int *BytesAdded) const {
  assert(!BytesAdded && "code size not handled");

  if (Cond.empty()) {
    if (!TBB)
      return 0;

    BuildMI(&MBB, DL, get(MSIL::BR)).addMBB(TBB);
    return 1;
  }

  assert(Cond.size() == 2 && "Expected a flag and a successor block");

  if (Cond[0].getImm())
    BuildMI(&MBB, DL, get(MSIL::BR_IF)).addMBB(TBB).add(Cond[1]);
  else
    BuildMI(&MBB, DL, get(MSIL::BR_UNLESS)).addMBB(TBB).add(Cond[1]);
  if (!FBB)
    return 1;

  BuildMI(&MBB, DL, get(MSIL::BR)).addMBB(FBB);
  return 2;
}

bool MSILInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 2 && "Expected a flag and a condition expression");
  Cond.front() = MachineOperand::CreateImm(!Cond.front().getImm());
  return false;
}

const MachineOperand &
MSILInstrInfo::getCalleeOperand(const MachineInstr &MI) const {
  return MSIL::getCalleeOp(MI);
}

unsigned MSILInstrInfo::getStackPeakExtra(unsigned Opc) const {
  return get(Opc).TSFlags & MSILII::StackPeakExtraMask;
}
