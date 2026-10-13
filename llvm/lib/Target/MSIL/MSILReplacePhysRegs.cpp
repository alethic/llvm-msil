//===-- MSILReplacePhysRegs.cpp - Replace phys regs with virt regs --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyReplacePhysRegs.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a pass that replaces physical registers with
/// virtual registers.
///
/// LLVM expects certain physical registers, such as a stack pointer. However,
/// MSIL doesn't actually have such physical registers. This pass is run
/// once LLVM no longer needs these registers, and replaces them with virtual
/// registers, so they can participate in register stackifying and coloring in
/// the normal way.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSIL.h"
#include "MSILMachineFunctionInfo.h"
#include "MSILSubtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "msil-replace-phys-regs"

namespace {
class MSILReplacePhysRegsLegacy final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  MSILReplacePhysRegsLegacy() : MachineFunctionPass(ID) {}

private:
  StringRef getPassName() const override {
    return "MSIL Replace Physical Registers";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char MSILReplacePhysRegsLegacy::ID = 0;
INITIALIZE_PASS(MSILReplacePhysRegsLegacy, DEBUG_TYPE,
                "Replace physical registers with virtual registers", false,
                false)

FunctionPass *llvm::createMSILReplacePhysRegsLegacyPass() {
  return new MSILReplacePhysRegsLegacy();
}

static bool replacePhysRegs(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Replace Physical Registers **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  MachineRegisterInfo &MRI = MF.getRegInfo();
  auto &TRI = *MF.getSubtarget<MSILSubtarget>().getRegisterInfo();
  bool Changed = false;

  for (unsigned PReg = MSIL::NoRegister + 1; PReg < MSIL::NUM_TARGET_REGS;
       ++PReg) {
    // Skip fake registers that are never used explicitly.
    if (PReg == MSIL::VALUE_STACK || PReg == MSIL::ARGUMENTS)
      continue;

    // Replace explicit uses of the physical register with a virtual register.
    const TargetRegisterClass *RC = TRI.getMinimalPhysRegClass(PReg);
    unsigned VReg = MSIL::NoRegister;
    for (MachineOperand &MO :
         llvm::make_early_inc_range(MRI.reg_operands(PReg))) {
      if (!MO.isImplicit()) {
        if (VReg == MSIL::NoRegister) {
          VReg = MRI.createVirtualRegister(RC);
          if (PReg == TRI.getFrameRegister(MF)) {
            auto FI = MF.getInfo<MSILFunctionInfo>();
            assert(!FI->isFrameBaseVirtual());
            FI->setFrameBaseVreg(VReg);
            LLVM_DEBUG({
              dbgs() << "replacing preg " << PReg << " with " << VReg << " ("
                     << Register(VReg).virtRegIndex() << ")\n";
            });
          }
        }
        MO.setReg(VReg);
        Changed = true;
      }
    }
  }

  return Changed;
}

bool MSILReplacePhysRegsLegacy::runOnMachineFunction(MachineFunction &MF) {
  assert(!mustPreserveAnalysisID(LiveIntervalsID) &&
         "LiveIntervals shouldn't be active yet!");

  return replacePhysRegs(MF);
}
