//===-- MSILRegisterInfo.cpp - MSIL Register Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyRegisterInfo.cpp at
// 8da03434abcc.
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
#include "MSILMachineFunctionInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Support/ErrorHandling.h"
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
  reportFatalUsageError("MSIL: frame indices are implemented in M7");
}

Register MSILRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  // If the physical register has been replaced by a virtual register, return
  // that.
  const auto *MFI = MF.getInfo<MSILFunctionInfo>();
  if (MFI->isFrameBaseVirtual())
    return MFI->getFrameBaseVreg();
  return MSIL::FP;
}
