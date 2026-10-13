//===--- MSILOptimizeLiveIntervals.cpp - LiveInterval processing ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from
// llvm/lib/Target/WebAssembly/WebAssemblyOptimizeLiveIntervals.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Optimize LiveIntervals for use in a post-RA context.
//
/// LiveIntervals normally runs before register allocation when the code is
/// only recently lowered out of SSA form, so it's uncommon for registers to
/// have multiple defs, and when they do, the defs are usually closely related.
/// Later, after coalescing, tail duplication, and other optimizations, it's
/// more common to see registers with multiple unrelated defs. This pass
/// updates LiveIntervals to distribute the value numbers across separate
/// LiveIntervals.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "MSILMachineFunctionInfo.h"
#include "MSILSubtarget.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "msil-optimize-live-intervals"

namespace {
class MSILOptimizeLiveIntervalsLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "MSIL Optimize Live Intervals";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<LiveIntervalsWrapperPass>();
    AU.addPreserved<SlotIndexesWrapperPass>();
    AU.addPreserved<LiveIntervalsWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setTracksLiveness();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID; // Pass identification, replacement for typeid
  MSILOptimizeLiveIntervalsLegacy() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char MSILOptimizeLiveIntervalsLegacy::ID = 0;
INITIALIZE_PASS(MSILOptimizeLiveIntervalsLegacy, DEBUG_TYPE,
                "Optimize LiveIntervals for MSIL", false, false)

FunctionPass *llvm::createMSILOptimizeLiveIntervalsLegacyPass() {
  return new MSILOptimizeLiveIntervalsLegacy();
}

static bool optimizeLiveIntervals(MachineFunction &MF, LiveIntervals &LIS) {
  LLVM_DEBUG(dbgs() << "********** Optimize LiveIntervals **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  MachineRegisterInfo &MRI = MF.getRegInfo();

  // We don't preserve SSA form.
  MRI.leaveSSA();

  assert(MRI.tracksLiveness() && "OptimizeLiveIntervals expects liveness");

  // Split multiple-VN LiveIntervals into multiple LiveIntervals.
  SmallVector<LiveInterval *, 4> SplitLIs;
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I < E; ++I) {
    Register Reg = Register::index2VirtReg(I);
    auto &TRI = *MF.getSubtarget<MSILSubtarget>().getRegisterInfo();

    if (MRI.reg_nodbg_empty(Reg))
      continue;

    LIS.splitSeparateComponents(LIS.getInterval(Reg), SplitLIs);
    if (Reg == TRI.getFrameRegister(MF) && SplitLIs.size() > 0) {
      // The live interval for the frame register was split, resulting in a new
      // VReg. For now we only support debug info output for a single frame base
      // value for the function, so just use the last one. It will certainly be
      // wrong for some part of the function, but until we are able to track
      // values through live-range splitting and stackification, it will have to
      // do.
      MF.getInfo<MSILFunctionInfo>()->setFrameBaseVreg(SplitLIs.back()->reg());
    }
    SplitLIs.clear();
  }

  // In FixIrreducibleControlFlow, we conservatively inserted IMPLICIT_DEF
  // instructions to satisfy LiveIntervals' requirement that all uses be
  // dominated by defs. Now that LiveIntervals has computed which of these
  // defs are actually needed and which are dead, remove the dead ones.
  for (MachineInstr &MI : llvm::make_early_inc_range(MF.front())) {
    if (MI.isImplicitDef() && MI.getOperand(0).isDead()) {
      LiveInterval &LI = LIS.getInterval(MI.getOperand(0).getReg());
      LIS.removeVRegDefAt(LI, LIS.getInstructionIndex(MI).getRegSlot());
      LIS.RemoveMachineInstrFromMaps(MI);
      MI.eraseFromParent();
    }
  }

  return true;
}

bool MSILOptimizeLiveIntervalsLegacy::runOnMachineFunction(
    MachineFunction &MF) {
  return optimizeLiveIntervals(
      MF, getAnalysis<LiveIntervalsWrapperPass>().getLIS());
}
