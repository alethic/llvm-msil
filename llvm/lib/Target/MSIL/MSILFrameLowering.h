//===-- MSILFrameLowering.h - TargetFrameLowering for MSIL ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyFrameLowering.h at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This class implements MSIL-specific bits of the TargetFrameLowering class.
///
/// The static frame of a function is a single 'localloc' block allocated at
/// entry, whose address is the frame base MSIL::FP. CIL releases it when the
/// method returns, so there is no epilogue, and there is no stack pointer:
/// arguments travel on the evaluation stack.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILFRAMELOWERING_H
#define LLVM_LIB_TARGET_MSIL_MSILFRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {

class MSILFrameLowering final : public TargetFrameLowering {
public:
  // StackRealignable must be true: otherwise MachineFrameInfo clamps object
  // alignments to the stack alignment. The frame can always be realigned,
  // because it is over-allocated and aligned up.
  MSILFrameLowering()
      : TargetFrameLowering(StackGrowsDown, /*StackAlignment=*/Align(16),
                            /*LocalAreaOffset=*/0,
                            /*TransientStackAlignment=*/Align(16),
                            /*StackRealignable=*/true) {}

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

  /// These methods insert prolog and epilog code into the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  bool hasReservedCallFrame(const MachineFunction &MF) const override;

  /// Returns true if \p MF needs a static frame, i.e. a frame base.
  static bool needsFrame(const MachineFunction &MF);

  /// Returns the alignment that the backend assumes for the address that
  /// 'localloc' returns. ECMA-335 promises none, and a constant-size
  /// localloc of 32 bytes or less is only 8-byte aligned on .NET Framework
  /// 4.8 and in .NET 8 tiered code (decision D13), so a frame that needs
  /// more is over-allocated and aligned up.
  Align getLocallocAlign(const MachineFunction &MF) const;

protected:
  bool hasFPImpl(const MachineFunction &MF) const override;

private:
  /// Allocates the static frame of \p MF and defines the frame base MSIL::FP
  /// before \p InsertPt. This is the only code that knows how the frame is
  /// allocated.
  void emitFrameBase(MachineFunction &MF, MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator InsertPt,
                     const DebugLoc &DL) const;
};

} // end namespace llvm

#endif
