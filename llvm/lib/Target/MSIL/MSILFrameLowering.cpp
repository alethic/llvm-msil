//===-- MSILFrameLowering.cpp - MSIL Frame Lowering -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyFrameLowering.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the MSIL implementation of the TargetFrameLowering
/// class.
///
/// The static frame is [FP, FP + StackSize). Prologue/epilogue insertion
/// gives every stack object a negative offset from the top of the frame that
/// is a multiple of its alignment, and rounds StackSize up to the largest
/// object alignment, so an object lives at FP + StackSize + ObjectOffset
/// (MSILRegisterInfo::eliminateFrameIndex) and is aligned whenever FP is
/// aligned to that largest alignment.
///
//===----------------------------------------------------------------------===//

#include "MSILFrameLowering.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSILInstrInfo.h"
#include "MSILSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include <limits>
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

Align MSILFrameLowering::getLocallocAlign(
    const MachineFunction & /*MF*/) const {
  // Measured on x86-64 (decision D13): a constant-size localloc of 32 bytes
  // or less is only 8-byte aligned on .NET Framework 4.8 and in .NET 8 tiered
  // code, and one of 8 bytes also on .NET 8 and .NET 10 with
  // TieredCompilation=0. Every Phase-1 triple assumes 8.
  return Align(8);
}

void MSILFrameLowering::emitFrameBase(MachineFunction &MF,
                                      MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator InsertPt,
                                      const DebugLoc &DL) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const MSILInstrInfo &TII = *MF.getSubtarget<MSILSubtarget>().getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  // When localloc returns less alignment than the frame needs, allocate
  // MaxAlign - LocallocAlign more bytes and align the address up: it is a
  // multiple of LocallocAlign, so aligning it up skips at most that many.
  const Align LocallocAlign = getLocallocAlign(MF);
  const Align MaxAlign = MFI.getMaxAlign();
  const bool Realign = MaxAlign > LocallocAlign;
  // needsFrame is also true for an empty frame whose address is taken:
  // allocate at least one byte.
  uint64_t Size = std::max<uint64_t>(MFI.getStackSize(), 1);
  if (Realign)
    Size += MaxAlign.value() - LocallocAlign.value();
  constexpr uint64_t MaxSize = std::numeric_limits<int32_t>::max();
  if (Size > MaxSize) {
    const Function &F = MF.getFunction();
    F.getContext().diagnose(DiagnosticInfoUnsupported(
        F, "stack frame of " + Twine(Size) +
               " bytes is larger than the 2 GiB that MSIL supports"));
    // Go on with a frame that ldc.i4 can express, so that the output stays
    // well-formed.
    Size = MaxSize;
  }

  // ldc.i4 <Size>; localloc. CIL requires the size to be the only value on
  // the evaluation stack: this is the first code of the method after the
  // arguments, and MSILRegStackify never nests a LOCALLOC into another
  // instruction, so its result always goes to a local.
  Register SizeReg = MRI.createVirtualRegister(&MSIL::I32RegClass);
  BuildMI(MBB, InsertPt, DL, TII.get(MSIL::CONST_I32), SizeReg)
      .addImm(Size)
      .setMIFlag(MachineInstr::FrameSetup);
  Register Base = Realign ? MRI.createVirtualRegister(&MSIL::I64RegClass)
                          : Register(MSIL::FP);
  BuildMI(MBB, InsertPt, DL, TII.get(MSIL::LOCALLOC), Base)
      .addReg(SizeReg)
      .setMIFlag(MachineInstr::FrameSetup);
  if (!Realign)
    return;

  // FP = (Base + (MaxAlign - 1)) & -MaxAlign, in native int (decision D2).
  Register Bias = MRI.createVirtualRegister(&MSIL::I64RegClass);
  BuildMI(MBB, InsertPt, DL, TII.get(MSIL::CONST_I64), Bias)
      .addImm(MaxAlign.value() - 1)
      .setMIFlag(MachineInstr::FrameSetup);
  Register Sum = MRI.createVirtualRegister(&MSIL::I64RegClass);
  BuildMI(MBB, InsertPt, DL, TII.get(MSIL::ADD_I64), Sum)
      .addReg(Base)
      .addReg(Bias)
      .setMIFlag(MachineInstr::FrameSetup);
  Register Mask = MRI.createVirtualRegister(&MSIL::I64RegClass);
  BuildMI(MBB, InsertPt, DL, TII.get(MSIL::CONST_I64), Mask)
      .addImm(-static_cast<int64_t>(MaxAlign.value()))
      .setMIFlag(MachineInstr::FrameSetup);
  BuildMI(MBB, InsertPt, DL, TII.get(MSIL::AND_I64), MSIL::FP)
      .addReg(Sum)
      .addReg(Mask)
      .setMIFlag(MachineInstr::FrameSetup);
}

void MSILFrameLowering::emitPrologue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  assert(MF.getFrameInfo().getCalleeSavedInfo().empty() &&
         "MSIL has no callee-saved registers");
  assert(MF.getFrameInfo().getObjectIndexBegin() == 0 &&
         "MSIL has no fixed stack objects");
  if (!needsFrame(MF))
    return;

  // Insert after the ARGUMENT_* pseudos, which MSILExplicitLocals expects at
  // the start of the entry block.
  auto InsertPt = MBB.begin();
  while (InsertPt != MBB.end() && MSIL::isArgument(InsertPt->getOpcode()))
    ++InsertPt;
  DebugLoc DL;

  emitFrameBase(MF, MBB, InsertPt, DL);
}

void MSILFrameLowering::emitEpilogue(MachineFunction & /*MF*/,
                                     MachineBasicBlock & /*MBB*/) const {
  // Nothing to do: CIL releases the localloc'ed frame when the method returns.
}
