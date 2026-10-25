//===-- MSILSetMemFlags.cpp - Set memory flags on loads and stores --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblySetP2AlignOperands.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file sets the memflags operands of loads, stores, cpblk and initblk
/// (decision D24): instruction selection leaves them 0, and this pass, right
/// after it, copies the alignment and volatility of each instruction's memory
/// operands into the immediate, which MSILMCInstLower turns into CIL_Unaligned
/// and CIL_Volatile prefix instructions ahead of the access.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSIL.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "msil-set-mem-flags"

namespace {
class MSILSetMemFlagsLegacy final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  MSILSetMemFlagsLegacy() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "MSIL Set Memory Flags"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char MSILSetMemFlagsLegacy::ID = 0;
INITIALIZE_PASS(MSILSetMemFlagsLegacy, DEBUG_TYPE,
                "Set the memory-flags operands for MSIL loads and stores",
                false, false)

FunctionPass *llvm::createMSILSetMemFlagsLegacyPass() {
  return new MSILSetMemFlagsLegacy();
}

/// The index of the memflags_op operand of \p MI, or -1 if it has none.
static int getMemFlagsOperandIdx(const MachineInstr &MI) {
  for (const auto &[Idx, Info] : enumerate(MI.getDesc().operands()))
    if (Info.OperandType == MSIL::OPERAND_MEMFLAGS)
      return static_cast<int>(Idx);
  return -1;
}

/// The alignment that the CIL instruction of \p MI assumes for its address
/// unless an 'unaligned.' prefix says otherwise (ECMA-335 III.2.5): the size
/// of the value for ldind and stind, and the natural size of the machine (the
/// pointer size) for cpblk and initblk (III.3.30, III.3.36).
static uint64_t getNaturalAlignment(const MachineInstr &MI,
                                    const DataLayout &DL) {
  switch (MI.getOpcode()) {
  case MSIL::LDIND_I1:
  case MSIL::LDIND_U1:
  case MSIL::STIND_I1:
    return 1;
  case MSIL::LDIND_I2:
  case MSIL::LDIND_U2:
  case MSIL::STIND_I2:
    return 2;
  case MSIL::LDIND_I4:
  case MSIL::LDIND_R4:
  case MSIL::STIND_I4:
  case MSIL::STIND_R4:
    return 4;
  case MSIL::LDIND_R8:
  case MSIL::STIND_R8:
    return 8;
  case MSIL::LDIND_I:
  case MSIL::STIND_I:
  case MSIL::CPBLK:
  case MSIL::INITBLK:
    return DL.getPointerSize();
  default:
    llvm_unreachable("memflags operand on an unexpected instruction");
  }
}

/// The memflags immediate (MSIL::MemFlags) for \p MI from its memory operands:
/// 'unaligned. N' when the access may be less aligned than the instruction
/// assumes, with N the largest of 1, 2 and 4 that the alignment guarantees,
/// and 'volatile.' when any memory operand is volatile. cpblk has a store and
/// a load memory operand; the less aligned one decides.
static unsigned computeMemFlags(const MachineInstr &MI, const DataLayout &DL) {
  Align A = (*MI.memoperands_begin())->getAlign();
  bool IsVolatile = false;
  for (const MachineMemOperand *MMO : MI.memoperands()) {
    A = std::min(A, MMO->getAlign());
    IsVolatile |= MMO->isVolatile();
  }

  unsigned Flags = 0;
  if (A.value() < getNaturalAlignment(MI, DL))
    Flags |= std::min<uint64_t>(A.value(), 4);
  if (IsVolatile)
    Flags |= MSIL::MemFlagsVolatile;
  return Flags;
}

static bool setMemFlags(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Set Memory Flags **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  const DataLayout &DL = MF.getDataLayout();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      int FlagsOpIdx = getMemFlagsOperandIdx(MI);
      // An instruction without memory operands keeps its flags: instruction
      // selection gives every load, store, cpblk and initblk memory operands.
      if (FlagsOpIdx < 0 || MI.memoperands_empty())
        continue;
      MachineOperand &FlagsOp = MI.getOperand(FlagsOpIdx);
      unsigned Flags = computeMemFlags(MI, DL);
      if (FlagsOp.getImm() == Flags)
        continue;
      FlagsOp.setImm(Flags);
      LLVM_DEBUG(dbgs() << "memflags " << Flags << ": " << MI);
      Changed = true;
    }
  }

  return Changed;
}

bool MSILSetMemFlagsLegacy::runOnMachineFunction(MachineFunction &MF) {
  return setMemFlags(MF);
}
