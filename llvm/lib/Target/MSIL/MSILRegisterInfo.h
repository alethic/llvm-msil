//===-- MSILRegisterInfo.h - MSIL Register Information Impl -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyRegisterInfo.h at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the MSIL implementation of the MSILRegisterInfo class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILREGISTERINFO_H
#define LLVM_LIB_TARGET_MSIL_MSILREGISTERINFO_H

#define GET_REGINFO_HEADER
#include "MSILGenRegisterInfo.inc"

namespace llvm {

class MachineFunction;
class RegScavenger;
class Triple;

class MSILRegisterInfo final : public MSILGenRegisterInfo {
  const Triple &TT;

public:
  explicit MSILRegisterInfo(const Triple &TT);

  // Code Generation virtual methods.
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  BitVector getReservedRegs(const MachineFunction &MF) const override;
  bool eliminateFrameIndex(MachineBasicBlock::iterator MI, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  // Debug information queries.
  Register getFrameRegister(const MachineFunction &MF) const override;

  // This does not apply to MSIL: there are no physical registers to preserve.
  const uint32_t *getNoPreservedMask() const override { return nullptr; }

  const Triple &getTargetTriple() const { return TT; }
};

} // end namespace llvm

#endif
