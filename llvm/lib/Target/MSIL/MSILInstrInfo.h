//===-- MSILInstrInfo.h - MSIL Instruction Information ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyInstrInfo.h at
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the MSIL implementation of the TargetInstrInfo class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILINSTRINFO_H
#define LLVM_LIB_TARGET_MSIL_MSILINSTRINFO_H

#include "MSILRegisterInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "MSILGenInstrInfo.inc"

namespace llvm {

class MSILSubtarget;

class MSILInstrInfo final : public MSILGenInstrInfo {
  const MSILRegisterInfo RI;

public:
  explicit MSILInstrInfo(const MSILSubtarget &STI);

  const MSILRegisterInfo &getRegisterInfo() const { return RI; }

  bool isReMaterializableImpl(const MachineInstr &MI) const override;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, Register DestReg, Register SrcReg,
                   bool KillSrc, bool RenamableDest = false,
                   bool RenamableSrc = false) const override;
  MachineInstr *commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                       unsigned OpIdx1,
                                       unsigned OpIdx2) const override;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify = false) const override;
  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;
  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  const MachineOperand &getCalleeOperand(const MachineInstr &MI) const override;

  /// Returns the extra evaluation-stack depth that the CIL text of \p Opc
  /// needs while it executes, above the larger of the depths before and after
  /// it (the StackPeakExtra field of TSFlags). It is non-zero only for records
  /// that print as several CIL opcodes, such as 'dup; stloc'.
  unsigned getStackPeakExtra(unsigned Opc) const;
};

} // end namespace llvm

#endif
