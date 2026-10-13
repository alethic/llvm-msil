//===-- MSILFrameLowering.cpp - MSIL Frame Lowering -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyFrameLowering.cpp at
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the MSIL implementation of the TargetFrameLowering
/// class.
///
//===----------------------------------------------------------------------===//

#include "MSILFrameLowering.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
using namespace llvm;

#define DEBUG_TYPE "msil-frame-info"

bool MSILFrameLowering::needsFrame(const MachineFunction &MF) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MFI.getStackSize() != 0 || MFI.isFrameAddressTaken();
}

bool MSILFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  return needsFrame(MF);
}

bool MSILFrameLowering::hasReservedCallFrame(
    const MachineFunction & /*MF*/) const {
  // Arguments travel on the evaluation stack, so calls never adjust a stack
  // pointer.
  return true;
}

MachineBasicBlock::iterator MSILFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction & /*MF*/, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // ADJCALLSTACKDOWN and ADJCALLSTACKUP have nothing to do.
  return MBB.erase(I);
}

void MSILFrameLowering::emitPrologue(MachineFunction & /*MF*/,
                                     MachineBasicBlock & /*MBB*/) const {
  // The localloc that allocates the static frame is emitted in M7. Until
  // then no frame index can be selected, so a function never uses a frame.
}

void MSILFrameLowering::emitEpilogue(MachineFunction & /*MF*/,
                                     MachineBasicBlock & /*MBB*/) const {
  // Nothing to do: CIL releases the localloc'ed frame when the method returns.
}
