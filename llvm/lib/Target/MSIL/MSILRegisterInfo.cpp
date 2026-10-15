//===-- MSILRegisterInfo.cpp - MSIL Register Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyRegisterInfo.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the MSIL implementation of the TargetRegisterInfo
/// class.
///
//===----------------------------------------------------------------------===//

#include "MSILRegisterInfo.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSILFrameLowering.h"
#include "MSILInstrInfo.h"
#include "MSILMachineFunctionInfo.h"
#include "MSILSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/MathExtras.h"
using namespace llvm;

#define DEBUG_TYPE "msil-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "MSILGenRegisterInfo.inc"

MSILRegisterInfo::MSILRegisterInfo(const Triple &TT)
    : MSILGenRegisterInfo(0), TT(TT) {}

const MCPhysReg *
MSILRegisterInfo::getCalleeSavedRegs(const MachineFunction *) const {
  static const MCPhysReg CalleeSavedRegs[] = {0};
  return CalleeSavedRegs;
}

BitVector MSILRegisterInfo::getReservedRegs(const MachineFunction &) const {
  // Only the frame base is reserved, as in WebAssembly. ARGUMENTS and
  // VALUE_STACK only order instructions; reserving ARGUMENTS, which every
  // instruction defines implicitly, would keep dead instructions alive
  // (MachineInstr::isDead never deletes a def of a reserved register).
  BitVector Reserved(getNumRegs());
  Reserved.set(MSIL::FP);
  return Reserved;
}

bool MSILRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                           int SPAdj, unsigned FIOperandNum,
                                           RegScavenger * /*RS*/) const {
  assert(SPAdj == 0);
  MachineInstr &MI = *II;

  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  // FP is the lowest address of the static frame (MSILFrameLowering), and
  // the objects have negative offsets from its top.
  int64_t FrameOffset = MFI.getStackSize() + MFI.getObjectOffset(FrameIndex);

  assert(MFI.getObjectSize(FrameIndex) != 0 &&
         "We assume that variable-sized objects have already been lowered, "
         "and don't use FrameIndex operands.");
  assert(FrameOffset >= 0 && "Stack object outside the static frame");
  Register FrameRegister = getFrameRegister(MF);

  // ldind and stind take no offset, so there is nothing to fold into a load
  // or a store.

  // If this is an address being added to a constant, fold the frame offset
  // into the constant.
  if (MI.getOpcode() == MSIL::ADD_I64) {
    MachineOperand &OtherMO = MI.getOperand(3 - FIOperandNum);
    if (OtherMO.isReg()) {
      Register OtherMOReg = OtherMO.getReg();
      if (OtherMOReg.isVirtual()) {
        MachineInstr *Def = MF.getRegInfo().getUniqueVRegDef(OtherMOReg);
        // TODO: For now we just opportunistically do this in the case where
        // the CONST_I64 happens to have exactly one def and one use. We
        // should generalize this to optimize in more cases.
        if (Def && Def->getOpcode() == MSIL::CONST_I64 &&
            MRI.hasOneNonDBGUse(Def->getOperand(0).getReg())) {
          MachineOperand &ImmMO = Def->getOperand(1);
          int64_t Offset;
          if (ImmMO.isImm() &&
              !AddOverflow(ImmMO.getImm(), FrameOffset, Offset)) {
            ImmMO.setImm(Offset);
            MI.getOperand(FIOperandNum)
                .ChangeToRegister(FrameRegister, /*isDef=*/false);
            return false;
          }
        }
      }
    }
  }

  // Otherwise create a native int add FP, offset ('ldc.i4 off; conv.i; add')
  // and make it the operand.
  const auto *TII = MF.getSubtarget<MSILSubtarget>().getInstrInfo();

  Register FIRegOperand = FrameRegister;
  if (FrameOffset) {
    Register OffsetOp = MRI.createVirtualRegister(&MSIL::I64RegClass);
    BuildMI(MBB, *II, II->getDebugLoc(), TII->get(MSIL::CONST_I64), OffsetOp)
        .addImm(FrameOffset);
    FIRegOperand = MRI.createVirtualRegister(&MSIL::I64RegClass);
    BuildMI(MBB, *II, II->getDebugLoc(), TII->get(MSIL::ADD_I64), FIRegOperand)
        .addReg(FrameRegister)
        .addReg(OffsetOp);
  }
  MI.getOperand(FIOperandNum).ChangeToRegister(FIRegOperand, /*isDef=*/false);
  return false;
}

Register MSILRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  // If the physical register has been replaced by a virtual register, return
  // that.
  const auto *MFI = MF.getInfo<MSILFunctionInfo>();
  if (MFI->isFrameBaseVirtual())
    return MFI->getFrameBaseVreg();
  return MSIL::FP;
}
