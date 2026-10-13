//===-- MSILStackCheck.cpp - Compute and check .maxstack ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the last machine pass of the MSIL pipeline. It
/// computes the .maxstack of each method and verifies the CIL evaluation stack
/// rules.
///
/// After MSILExplicitLocals every explicit register operand of an instruction
/// is a value on the CIL evaluation stack: the instruction pops its explicit
/// register uses in operand order (the last use is on top) and pushes its
/// explicit register def, if any. Instructions that print as several CIL
/// opcodes declare the extra transient depth they need in the StackPeakExtra
/// field of TSFlags. RegStackify builds trees inside a block only, so the
/// stack is empty at every block boundary and one linear walk per block finds
/// the exact peak; no dataflow is needed.
///
/// A violation is an internal compiler error. Printing the method anyway would
/// only turn it into an InvalidProgramException at run time, far from the
/// compiler, so the checks are always on and there is no option to skip them.
///
/// CIL also forbids falling off the end of a method (the JIT throws
/// InvalidProgramException and ilasm does not diagnose it). TrapUnreachable
/// normally puts a barrier there; as a cheap safety net, this pass appends a
/// TRAP when the last block does not end in one.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSIL.h"
#include "MSILMachineFunctionInfo.h"
#include "MSILSubtarget.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <string>
using namespace llvm;

#define DEBUG_TYPE "msil-stack-check"

namespace {
class MSILStackCheckLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override { return "MSIL Stack Check"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID; // Pass identification, replacement for typeid
  MSILStackCheckLegacy() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char MSILStackCheckLegacy::ID = 0;
INITIALIZE_PASS(MSILStackCheckLegacy, DEBUG_TYPE,
                "Compute .maxstack and check the CIL stack rules for MSIL",
                false, false)

FunctionPass *llvm::createMSILStackCheckLegacyPass() {
  return new MSILStackCheckLegacy();
}

/// Returns the extra evaluation-stack depth that the CIL text of \p MI needs
/// while it executes, above max(depth before, depth after): the
/// StackPeakExtra field of TSFlags (bits 1-0).
static unsigned getStackPeakExtra(const MachineInstr &MI) {
  return MI.getDesc().TSFlags & MSILII::StackPeakExtraMask;
}

/// Returns true if \p MI prints no CIL opcode and therefore does not touch the
/// evaluation stack: debug instructions, labels, CFI, the other meta
/// instructions (KILL, IMPLICIT_DEF, LIFETIME_*, MEMBARRIER, ...) and the
/// ARGUMENT pseudos, which MSILExplicitLocals normally erases.
static bool printsNoCIL(const MachineInstr &MI) {
  return MI.isDebugInstr() || MI.isPosition() || MI.isMetaInstruction() ||
         MSIL::isArgument(MI.getOpcode());
}

/// Reports a violation of the CIL stack rules in \p MF as an internal error:
/// "MSIL stack check failed in '<fn>': <why>: <MI>". \p MI may be null.
[[noreturn]] static void fail(const MachineFunction &MF, const MachineInstr *MI,
                              const Twine &Why) {
  std::string Msg;
  raw_string_ostream OS(Msg);
  OS << "MSIL stack check failed in '" << MF.getName() << "': " << Why;
  if (MI) {
    OS << ": ";
    MI->print(OS, /*IsStandalone=*/true, /*SkipOpers=*/false,
              /*SkipDebugLoc=*/false, /*AddNewLine=*/false);
  }
  reportFatalInternalError(StringRef(Msg));
}

static bool stackCheck(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Stack Check **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  auto &MFI = *MF.getInfo<MSILFunctionInfo>();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const auto &Subtarget = MF.getSubtarget<MSILSubtarget>();
  const MSILInstrInfo &TII = *Subtarget.getInstrInfo();
  const TargetRegisterInfo &TRI = *Subtarget.getRegisterInfo();
  // CanLowerReturn allows at most one result.
  const unsigned NumResults = MFI.getResults().size();
  bool Changed = false;

  if (MF.empty())
    return Changed;

  // Never fall off the end of the method.
  MachineBasicBlock &Last = MF.back();
  MachineBasicBlock::iterator LastI = Last.getLastNonDebugInstr();
  if (LastI == Last.end() || !LastI->isBarrier()) {
    MachineInstr *Trap = BuildMI(&Last, DebugLoc(), TII.get(MSIL::TRAP));
    Trap->addRegisterDead(MSIL::ARGUMENTS, &TRI);
    LLVM_DEBUG(dbgs() << "Appended a TRAP to " << printMBBReference(Last)
                      << '\n');
    Changed = true;
  }

  unsigned MaxStack = 0;
  // The evaluation stack, bottom first: the register of each value.
  SmallVector<Register, 8> Stack;
  SmallVector<Register, 4> Uses;
  for (MachineBasicBlock &MBB : MF) {
    // The stack is empty at every block entry: the previous block ended with
    // an empty stack, or the check below failed.
    for (MachineInstr &MI : MBB) {
      if (printsNoCIL(MI))
        continue;

      Uses.clear();
      Register Def;
      unsigned Pushes = 0;
      for (const MachineOperand &MO : MI.explicit_operands()) {
        if (!MO.isReg())
          continue;
        assert(MO.getReg().isVirtual() && MFI.isVRegStackified(MO.getReg()) &&
               "register operand survived MSILExplicitLocals");
        if (MO.isDef()) {
          Def = MO.getReg();
          ++Pushes;
        } else {
          Uses.push_back(MO.getReg());
        }
      }
      const unsigned Pops = Uses.size();
      const unsigned Depth = Stack.size();

      if (Pushes > 1)
        fail(MF, &MI, "instruction pushes more than one value");
      if (Pops > Depth)
        fail(MF, &MI, "evaluation stack underflow");
      // The operands are the top Pops values, the last operand on top.
      if (!std::equal(Uses.begin(), Uses.end(), Stack.end() - Pops))
        fail(
            MF, &MI,
            "operands are not on top of the evaluation stack in operand order");
      if (MI.isReturn()) {
        if (Depth > NumResults)
          fail(MF, &MI, "'ret' with extra values on the evaluation stack");
        if (Depth < NumResults)
          fail(MF, &MI, "'ret' with too few values on the evaluation stack");
        if (Pops != NumResults)
          fail(MF, &MI, "'ret' operands do not match the method's results");
      }
      if (MI.getOpcode() == MSIL::LOCALLOC && Depth != 1)
        fail(MF, &MI, "'localloc' with a non-empty evaluation stack");

      // The CIL text of MI starts at Depth and ends at DepthAfter; a record
      // that prints as several opcodes goes StackPeakExtra above the larger
      // of the two while it runs (for example 'ldsfld; ldc.i4; add' of
      // GLOBAL_ADDR peaks one above its result).
      const unsigned DepthAfter = Depth - Pops + Pushes;
      MaxStack = std::max(MaxStack,
                          std::max(Depth, DepthAfter) + getStackPeakExtra(MI));
      Stack.truncate(Depth - Pops);
      if (Pushes)
        Stack.push_back(Def);
    }

    if (!Stack.empty()) {
      std::string Block;
      raw_string_ostream(Block) << printMBBReference(MBB);
      // Show the instruction that pushed the oldest value left behind.
      fail(MF, MRI.getUniqueVRegDef(Stack.front()),
           "evaluation stack not empty at end of " + Block);
    }
  }

  LLVM_DEBUG(dbgs() << ".maxstack " << MaxStack << '\n');
  MFI.setMaxStack(MaxStack);
  return Changed;
}

bool MSILStackCheckLegacy::runOnMachineFunction(MachineFunction &MF) {
  return stackCheck(MF);
}
