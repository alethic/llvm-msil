//===-- MSILInstrInfo.cpp - MSIL Instruction Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyInstrInfo.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the MSIL implementation of the TargetInstrInfo class.
///
//===----------------------------------------------------------------------===//

#include "MSILInstrInfo.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSILDebugValueManager.h"
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

/// Returns the conditional branch that is taken exactly when \p Opc is not
/// taken, or 0 if \p Opc is not a conditional branch. Every conditional
/// branch has one: brtrue and brfalse invert each other, and so do the
/// integer compare-and-branches of each signedness. A floating-point
/// compare-and-branch is inverted by the one of the other ordering, because
/// the inverse must be true when an operand is a NaN exactly when the branch
/// is false then (isel.md 11.7): blt (OLT) inverts to bge.un (UGE), not bge.
static unsigned getInvertedBranch(unsigned Opc) {
  switch (Opc) {
  default:
    return 0;
  case MSIL::BR_IF:
    return MSIL::BR_UNLESS;
  case MSIL::BR_UNLESS:
    return MSIL::BR_IF;
  // Equality inverts the same way for every class: beq is OEQ and bne.un
  // UNE on floating-point values.
  case MSIL::BEQ_I32:
    return MSIL::BNE_UN_I32;
  case MSIL::BNE_UN_I32:
    return MSIL::BEQ_I32;
  case MSIL::BEQ_I64:
    return MSIL::BNE_UN_I64;
  case MSIL::BNE_UN_I64:
    return MSIL::BEQ_I64;
  case MSIL::BEQ_F32:
    return MSIL::BNE_UN_F32;
  case MSIL::BNE_UN_F32:
    return MSIL::BEQ_F32;
  case MSIL::BEQ_F64:
    return MSIL::BNE_UN_F64;
  case MSIL::BNE_UN_F64:
    return MSIL::BEQ_F64;
  // Integers: a signed order inverts to a signed one, an unsigned order to an
  // unsigned one.
  case MSIL::BLT_I32:
    return MSIL::BGE_I32;
  case MSIL::BGE_I32:
    return MSIL::BLT_I32;
  case MSIL::BLE_I32:
    return MSIL::BGT_I32;
  case MSIL::BGT_I32:
    return MSIL::BLE_I32;
  case MSIL::BLT_UN_I32:
    return MSIL::BGE_UN_I32;
  case MSIL::BGE_UN_I32:
    return MSIL::BLT_UN_I32;
  case MSIL::BLE_UN_I32:
    return MSIL::BGT_UN_I32;
  case MSIL::BGT_UN_I32:
    return MSIL::BLE_UN_I32;
  case MSIL::BLT_I64:
    return MSIL::BGE_I64;
  case MSIL::BGE_I64:
    return MSIL::BLT_I64;
  case MSIL::BLE_I64:
    return MSIL::BGT_I64;
  case MSIL::BGT_I64:
    return MSIL::BLE_I64;
  case MSIL::BLT_UN_I64:
    return MSIL::BGE_UN_I64;
  case MSIL::BGE_UN_I64:
    return MSIL::BLT_UN_I64;
  case MSIL::BLE_UN_I64:
    return MSIL::BGT_UN_I64;
  case MSIL::BGT_UN_I64:
    return MSIL::BLE_UN_I64;
  // Floating point: an ordered order inverts to an unordered one and back.
  case MSIL::BLT_F32:
    return MSIL::BGE_UN_F32;
  case MSIL::BGE_UN_F32:
    return MSIL::BLT_F32;
  case MSIL::BLE_F32:
    return MSIL::BGT_UN_F32;
  case MSIL::BGT_UN_F32:
    return MSIL::BLE_F32;
  case MSIL::BGT_F32:
    return MSIL::BLE_UN_F32;
  case MSIL::BLE_UN_F32:
    return MSIL::BGT_F32;
  case MSIL::BGE_F32:
    return MSIL::BLT_UN_F32;
  case MSIL::BLT_UN_F32:
    return MSIL::BGE_F32;
  case MSIL::BLT_F64:
    return MSIL::BGE_UN_F64;
  case MSIL::BGE_UN_F64:
    return MSIL::BLT_F64;
  case MSIL::BLE_F64:
    return MSIL::BGT_UN_F64;
  case MSIL::BGT_UN_F64:
    return MSIL::BLE_F64;
  case MSIL::BGT_F64:
    return MSIL::BLE_UN_F64;
  case MSIL::BLE_UN_F64:
    return MSIL::BGT_F64;
  case MSIL::BGE_F64:
    return MSIL::BLT_UN_F64;
  case MSIL::BLT_UN_F64:
    return MSIL::BGE_F64;
  }
}

// Branch analysis.
bool MSILInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                  MachineBasicBlock *&TBB,
                                  MachineBasicBlock *&FBB,
                                  SmallVectorImpl<MachineOperand> &Cond,
                                  bool /*AllowModify*/) const {
  // CIL has nothing like WebAssembly's CFGStackify block, loop and try
  // markers, so branch analysis stays valid through emission, and
  // WebAssembly's bail-out for CFG-stackified functions is dropped. After
  // MSILRegStackify its only caller is the MachineVerifier, which runs it
  // after every pass under -verify-machineinstrs. Caution: no pass after
  // MSILRegStackify may rebuild branches through insertBranch or
  // updateTerminator, because the condition operands are stackified by then.
  bool HaveCond = false;
  for (MachineInstr &MI : MBB.terminators()) {
    if (MI.isDebugInstr())
      continue;
    unsigned Opc = MI.getOpcode();
    if (Opc == MSIL::BR) {
      if (!HaveCond)
        TBB = MI.getOperand(0).getMBB();
      else
        FBB = MI.getOperand(0).getMBB();
    } else if (getInvertedBranch(Opc)) {
      // brtrue, brfalse or a compare-and-branch: the target, then the
      // operands that the condition pops.
      if (HaveCond)
        return true;
      Cond.push_back(MachineOperand::CreateImm(Opc));
      for (const MachineOperand &MO : drop_begin(MI.explicit_operands()))
        Cond.push_back(MO);
      TBB = MI.getOperand(0).getMBB();
      HaveCond = true;
    } else {
      // A switch, a return, a trap or anything else: bail out.
      return true;
    }
    if (MI.isBarrier())
      break;
  }

  return false;
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

  assert(Cond.size() >= 2 && Cond[0].isImm() &&
         "Expected a branch opcode and its condition operands");
  unsigned Opc = Cond[0].getImm();
  assert(getInvertedBranch(Opc) && "Not a conditional branch");

  MachineInstrBuilder MIB = BuildMI(&MBB, DL, get(Opc)).addMBB(TBB);
  for (const MachineOperand &MO : drop_begin(Cond))
    MIB.add(MO);
  if (!FBB)
    return 1;

  BuildMI(&MBB, DL, get(MSIL::BR)).addMBB(FBB);
  return 2;
}

bool MSILInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() >= 2 && Cond[0].isImm() &&
         "Expected a branch opcode and its condition operands");
  // Every conditional branch has an exact inverse (getInvertedBranch); a
  // condition without one is reported as not reversible.
  unsigned Inverse = getInvertedBranch(Cond[0].getImm());
  if (!Inverse)
    return true;
  Cond[0].setImm(Inverse);
  return false;
}

ArrayRef<std::pair<int, const char *>>
MSILInstrInfo::getSerializableTargetIndices() const {
  static const std::pair<int, const char *> TargetIndices[] = {
      {MSIL::TI_LOCAL, "msil-local"},
      {MSIL::TI_LOCAL_INDIRECT, "msil-local-indirect"}};
  return ArrayRef(TargetIndices);
}

const MachineOperand &
MSILInstrInfo::getCalleeOperand(const MachineInstr &MI) const {
  return MSIL::getCalleeOp(MI);
}

unsigned MSILInstrInfo::getStackPeakExtra(unsigned Opc) const {
  return get(Opc).TSFlags & MSILII::StackPeakExtraMask;
}
