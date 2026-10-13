//===-- MSILExplicitLocals.cpp - Make Locals Explicit ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyExplicitLocals.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file converts any remaining registers into MSIL locals.
///
/// After register stackification and register coloring, convert non-stackified
/// registers into locals, inserting explicit local.get and local.set
/// instructions.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSIL.h"
#include "MSILDebugValueManager.h"
#include "MSILMachineFunctionInfo.h"
#include "MSILSubtarget.h"
#include "MSILUtilities.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "msil-explicit-locals"

namespace {
class MSILExplicitLocalsLegacy final : public MachineFunctionPass {
  StringRef getPassName() const override { return "MSIL Explicit Locals"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID; // Pass identification, replacement for typeid
  MSILExplicitLocalsLegacy() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char MSILExplicitLocalsLegacy::ID = 0;
INITIALIZE_PASS(MSILExplicitLocalsLegacy, DEBUG_TYPE,
                "Convert registers to MSIL locals", false, false)

FunctionPass *llvm::createMSILExplicitLocalsLegacyPass() {
  return new MSILExplicitLocalsLegacy();
}

static void checkFrameBase(MSILFunctionInfo &MFI, unsigned Local,
                           unsigned Reg) {
  // Mark a local for the frame base vreg.
  if (MFI.isFrameBaseVirtual() && Reg == MFI.getFrameBaseVreg()) {
    LLVM_DEBUG({
      dbgs() << "Allocating local " << Local << "for VReg "
             << Register(Reg).virtRegIndex() << '\n';
    });
    MFI.setFrameBaseLocal(Local);
  }
}

/// Return a local id number for the given register, assigning it a new one
/// if it doesn't yet have one.
static unsigned getLocalId(DenseMap<unsigned, unsigned> &Reg2Local,
                           MSILFunctionInfo &MFI, unsigned &CurLocal,
                           unsigned Reg) {
  auto P = Reg2Local.insert(std::make_pair(Reg, CurLocal));
  if (P.second) {
    checkFrameBase(MFI, CurLocal, Reg);
    ++CurLocal;
  }
  return P.first->second;
}

/// Get the appropriate drop opcode for the given register class.
static unsigned getDropOpcode(const TargetRegisterClass *RC) {
  if (RC == &MSIL::I32RegClass)
    return MSIL::DROP_I32;
  if (RC == &MSIL::I64RegClass)
    return MSIL::DROP_I64;
  if (RC == &MSIL::F32RegClass)
    return MSIL::DROP_F32;
  if (RC == &MSIL::F64RegClass)
    return MSIL::DROP_F64;
  llvm_unreachable("Unexpected register class");
}

/// Get the appropriate local.get opcode for the given register class: ldarg
/// for an argument, ldloc for a CIL local.
static unsigned getLocalGetOpcode(const TargetRegisterClass *RC, bool IsArg) {
  if (RC == &MSIL::I32RegClass)
    return IsArg ? MSIL::ARG_GET_I32 : MSIL::LOCAL_GET_I32;
  if (RC == &MSIL::I64RegClass)
    return IsArg ? MSIL::ARG_GET_I64 : MSIL::LOCAL_GET_I64;
  if (RC == &MSIL::F32RegClass)
    return IsArg ? MSIL::ARG_GET_F32 : MSIL::LOCAL_GET_F32;
  if (RC == &MSIL::F64RegClass)
    return IsArg ? MSIL::ARG_GET_F64 : MSIL::LOCAL_GET_F64;
  llvm_unreachable("Unexpected register class");
}

/// Get the appropriate local.set opcode for the given register class: starg
/// for an argument, stloc for a CIL local.
static unsigned getLocalSetOpcode(const TargetRegisterClass *RC, bool IsArg) {
  if (RC == &MSIL::I32RegClass)
    return IsArg ? MSIL::ARG_SET_I32 : MSIL::LOCAL_SET_I32;
  if (RC == &MSIL::I64RegClass)
    return IsArg ? MSIL::ARG_SET_I64 : MSIL::LOCAL_SET_I64;
  if (RC == &MSIL::F32RegClass)
    return IsArg ? MSIL::ARG_SET_F32 : MSIL::LOCAL_SET_F32;
  if (RC == &MSIL::F64RegClass)
    return IsArg ? MSIL::ARG_SET_F64 : MSIL::LOCAL_SET_F64;
  llvm_unreachable("Unexpected register class");
}

/// Get the appropriate local.tee opcode for the given register class:
/// 'dup; starg' for an argument, 'dup; stloc' for a CIL local.
static unsigned getLocalTeeOpcode(const TargetRegisterClass *RC, bool IsArg) {
  if (RC == &MSIL::I32RegClass)
    return IsArg ? MSIL::ARG_TEE_I32 : MSIL::LOCAL_TEE_I32;
  if (RC == &MSIL::I64RegClass)
    return IsArg ? MSIL::ARG_TEE_I64 : MSIL::LOCAL_TEE_I64;
  if (RC == &MSIL::F32RegClass)
    return IsArg ? MSIL::ARG_TEE_F32 : MSIL::LOCAL_TEE_F32;
  if (RC == &MSIL::F64RegClass)
    return IsArg ? MSIL::ARG_TEE_F64 : MSIL::LOCAL_TEE_F64;
  llvm_unreachable("Unexpected register class");
}

/// Get the type associated with the given register class.
static MVT typeForRegClass(const TargetRegisterClass *RC) {
  if (RC == &MSIL::I32RegClass)
    return MVT::i32;
  if (RC == &MSIL::I64RegClass)
    return MVT::i64;
  if (RC == &MSIL::F32RegClass)
    return MVT::f32;
  if (RC == &MSIL::F64RegClass)
    return MVT::f64;
  llvm_unreachable("unrecognized register class");
}

/// Test whether the local id \p LocalId names an argument. As in WebAssembly,
/// arguments and locals share one numbering with the arguments first, but CIL
/// addresses them separately: ARG_GET, ARG_SET and ARG_TEE (ldarg, starg and
/// 'dup; starg') for an argument, LOCAL_GET, LOCAL_SET and LOCAL_TEE (ldloc,
/// stloc and 'dup; stloc') for a CIL local.
static bool isArgLocal(const MSILFunctionInfo &MFI, unsigned LocalId) {
  return LocalId < MFI.getParams().size();
}

/// Return the index operand of the CIL instruction that accesses the local id
/// \p LocalId: the argument index for an argument, else the CIL local index
/// (\p LocalId minus the parameter count; MSILFunctionInfo's locals are the
/// CIL locals only).
static unsigned getCILIndex(const MSILFunctionInfo &MFI, unsigned LocalId) {
  if (isArgLocal(MFI, LocalId))
    return LocalId;
  return LocalId - MFI.getParams().size();
}

/// Given a MachineOperand of a stackified vreg, return the instruction at the
/// start of the expression tree.
static MachineInstr *findStartOfTree(MachineOperand &MO,
                                     MachineRegisterInfo &MRI,
                                     const MSILFunctionInfo &MFI) {
  Register Reg = MO.getReg();
  assert(MFI.isVRegStackified(Reg));
  MachineInstr *Def = MRI.getVRegDef(Reg);

  // If this instruction has any non-stackified defs, it is the start
  for (auto DefReg : Def->defs()) {
    if (!MFI.isVRegStackified(DefReg.getReg())) {
      return Def;
    }
  }

  // Find the first stackified use and proceed from there.
  for (MachineOperand &DefMO : Def->explicit_uses()) {
    if (!DefMO.isReg())
      continue;
    return findStartOfTree(DefMO, MRI, MFI);
  }

  // If there were no stackified uses, we've reached the start.
  return Def;
}

// FAKE_USEs are no-ops, so remove them here so that the values used by them
// will be correctly dropped later.
static void removeFakeUses(MachineFunction &MF) {
  SmallVector<MachineInstr *> ToDelete;
  for (auto &MBB : MF)
    for (auto &MI : MBB)
      if (MI.isFakeUse())
        ToDelete.push_back(&MI);
  for (auto *MI : ToDelete)
    MI->eraseFromParent();
}

static bool explicitLocals(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Make Locals Explicit **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  bool Changed = false;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  MSILFunctionInfo &MFI = *MF.getInfo<MSILFunctionInfo>();
  const auto *TII = MF.getSubtarget<MSILSubtarget>().getInstrInfo();

  removeFakeUses(MF);

  // Map non-stackified virtual registers to their local ids.
  DenseMap<unsigned, unsigned> Reg2Local;

  // Handle ARGUMENTS first to ensure that they get the designated numbers.
  for (MachineBasicBlock::iterator I = MF.begin()->begin(),
                                   E = MF.begin()->end();
       I != E;) {
    MachineInstr &MI = *I++;
    if (!MSIL::isArgument(MI.getOpcode()))
      break;
    Register Reg = MI.getOperand(0).getReg();
    assert(!MFI.isVRegStackified(Reg));
    auto Local = static_cast<unsigned>(MI.getOperand(1).getImm());
    assert(isArgLocal(MFI, Local) && "ARGUMENT index beyond the parameters");
    Reg2Local[Reg] = Local;
    checkFrameBase(MFI, Local, Reg);

    // Update debug value to point to the local before removing.
    MSILDebugValueManager(&MI).replaceWithLocal(Local);

    MI.eraseFromParent();
    Changed = true;
  }

  // Start assigning local numbers after the last parameter and after any
  // already-assigned locals.
  unsigned CurLocal = static_cast<unsigned>(MFI.getParams().size());
  CurLocal += static_cast<unsigned>(MFI.getLocals().size());

  // Precompute the set of registers that are unused, so that we can insert
  // drops to their defs.
  // And unstackify any stackified registers that don't have any uses, so that
  // they can be dropped later. This can happen when transformations after
  // RegStackify remove instructions using stackified registers.
  BitVector UseEmpty(MRI.getNumVirtRegs());
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I < E; ++I) {
    Register Reg = Register::index2VirtReg(I);
    if (MRI.use_empty(Reg)) {
      UseEmpty[I] = true;
      MFI.unstackifyVReg(Reg);
    }
  }

  // Visit each instruction in the function.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
      assert(!MSIL::isArgument(MI.getOpcode()));

      if (MI.isDebugInstr() || MI.isLabel())
        continue;

      if (MI.getOpcode() == MSIL::IMPLICIT_DEF) {
        MI.eraseFromParent();
        Changed = true;
        continue;
      }

      // Replace tee instructions with local.tee. The difference is that tee
      // instructions have two defs, while local.tee instructions have one def
      // and an index of a local to write to.
      //
      // - Before:
      // TeeReg, Reg = TEE DefReg
      // INST ..., TeeReg, ...
      // INST ..., Reg, ...
      // INST ..., Reg, ...
      // * DefReg: may or may not be stackified
      // * Reg: not stackified
      // * TeeReg: stackified
      //
      // - After (when DefReg was already stackified):
      // TeeReg = LOCAL_TEE LocalId1, DefReg
      // INST ..., TeeReg, ...
      // INST ..., Reg, ...
      // INST ..., Reg, ...
      // * Reg: mapped to LocalId1
      // * TeeReg: stackified
      //
      // - After (when DefReg was not already stackified):
      // NewReg = LOCAL_GET LocalId1
      // TeeReg = LOCAL_TEE LocalId2, NewReg
      // INST ..., TeeReg, ...
      // INST ..., Reg, ...
      // INST ..., Reg, ...
      // * DefReg: mapped to LocalId1
      // * Reg: mapped to LocalId2
      // * TeeReg: stackified
      if (MSIL::isTee(MI.getOpcode())) {
        assert(MFI.isVRegStackified(MI.getOperand(0).getReg()));
        assert(!MFI.isVRegStackified(MI.getOperand(1).getReg()));
        Register DefReg = MI.getOperand(2).getReg();
        const TargetRegisterClass *RC = MRI.getRegClass(DefReg);

        // Stackify the input if it isn't stackified yet.
        if (!MFI.isVRegStackified(DefReg)) {
          unsigned LocalId = getLocalId(Reg2Local, MFI, CurLocal, DefReg);
          Register NewReg = MRI.createVirtualRegister(RC);
          unsigned Opc = getLocalGetOpcode(RC, isArgLocal(MFI, LocalId));
          BuildMI(MBB, &MI, MI.getDebugLoc(), TII->get(Opc), NewReg)
              .addImm(getCILIndex(MFI, LocalId));
          MI.getOperand(2).setReg(NewReg);
          MFI.stackifyVReg(MRI, NewReg);
        }

        // Replace the TEE with a LOCAL_TEE (or an ARG_TEE).
        unsigned LocalId =
            getLocalId(Reg2Local, MFI, CurLocal, MI.getOperand(1).getReg());
        unsigned Opc = getLocalTeeOpcode(RC, isArgLocal(MFI, LocalId));
        BuildMI(MBB, &MI, MI.getDebugLoc(), TII->get(Opc),
                MI.getOperand(0).getReg())
            .addImm(getCILIndex(MFI, LocalId))
            .addReg(MI.getOperand(2).getReg());

        MSILDebugValueManager(&MI).replaceWithLocal(LocalId);

        MI.eraseFromParent();
        Changed = true;
        continue;
      }

      // Insert local.sets for any defs that aren't stackified yet.
      for (auto &Def : MI.defs()) {
        Register OldReg = Def.getReg();
        if (!MFI.isVRegStackified(OldReg)) {
          const TargetRegisterClass *RC = MRI.getRegClass(OldReg);
          Register NewReg = MRI.createVirtualRegister(RC);
          auto InsertPt = std::next(MI.getIterator());
          if (UseEmpty[OldReg.virtRegIndex()]) {
            unsigned Opc = getDropOpcode(RC);
            MachineInstr *Drop =
                BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII->get(Opc))
                    .addReg(NewReg);
            // After the drop instruction, this reg operand will not be used
            Drop->getOperand(0).setIsKill();
            if (MFI.isFrameBaseVirtual() && OldReg == MFI.getFrameBaseVreg())
              MFI.clearFrameBaseVreg();
          } else {
            unsigned LocalId = getLocalId(Reg2Local, MFI, CurLocal, OldReg);
            unsigned Opc = getLocalSetOpcode(RC, isArgLocal(MFI, LocalId));

            MSILDebugValueManager(&MI).replaceWithLocal(LocalId);

            BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII->get(Opc))
                .addImm(getCILIndex(MFI, LocalId))
                .addReg(NewReg);
          }
          // This register operand of the original instruction is now being used
          // by the inserted drop or local.set instruction, so make it not dead
          // yet.
          Def.setReg(NewReg);
          Def.setIsDead(false);
          MFI.stackifyVReg(MRI, NewReg);
          Changed = true;
        }
      }

      // Insert local.gets for any uses that aren't stackified yet.
      MachineInstr *InsertPt = &MI;
      for (MachineOperand &MO : reverse(MI.explicit_uses())) {
        if (!MO.isReg())
          continue;

        Register OldReg = MO.getReg();

        // Inline asm may have a def in the middle of the operands. Our contract
        // with inline asm register operands is to provide local indices as
        // immediates.
        if (MO.isDef()) {
          assert(MI.isInlineAsm());
          unsigned LocalId = getLocalId(Reg2Local, MFI, CurLocal, OldReg);
          // If this register operand is tied to another operand, we can't
          // change it to an immediate. Untie it first.
          MI.untieRegOperand(MO.getOperandNo());
          MO.ChangeToImmediate(LocalId);
          continue;
        }

        // If we see a stackified register, prepare to insert subsequent
        // local.gets before the start of its tree.
        if (MFI.isVRegStackified(OldReg)) {
          InsertPt = findStartOfTree(MO, MRI, MFI);
          continue;
        }

        // Our contract with inline asm register operands is to provide local
        // indices as immediates.
        if (MI.isInlineAsm()) {
          unsigned LocalId = getLocalId(Reg2Local, MFI, CurLocal, OldReg);
          // Untie it first if this reg operand is tied to another operand.
          MI.untieRegOperand(MO.getOperandNo());
          MO.ChangeToImmediate(LocalId);
          continue;
        }

        // Insert a local.get.
        unsigned LocalId = getLocalId(Reg2Local, MFI, CurLocal, OldReg);
        const TargetRegisterClass *RC = MRI.getRegClass(OldReg);
        Register NewReg = MRI.createVirtualRegister(RC);
        unsigned Opc = getLocalGetOpcode(RC, isArgLocal(MFI, LocalId));
        // Use a InsertPt as our DebugLoc, since MI may be discontinuous from
        // the where this local is being inserted, causing non-linear stepping
        // in the debugger or function entry points where variables aren't live
        // yet. Alternative is previous instruction, but that is strictly worse
        // since it can point at the previous statement.
        // See crbug.com/1251909, crbug.com/1249745
        InsertPt = BuildMI(MBB, InsertPt, InsertPt->getDebugLoc(),
                           TII->get(Opc), NewReg)
                       .addImm(getCILIndex(MFI, LocalId));
        MO.setReg(NewReg);
        MFI.stackifyVReg(MRI, NewReg);
        Changed = true;
      }

      // Coalesce and eliminate COPY instructions.
      if (MSIL::isCopy(MI.getOpcode())) {
        MRI.replaceRegWith(MI.getOperand(1).getReg(),
                           MI.getOperand(0).getReg());
        MI.eraseFromParent();
        Changed = true;
      }
    }
  }

  // Define the locals.
  // TODO: Sort the locals for better compression.
  MFI.setNumLocals(CurLocal - MFI.getParams().size());
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I < E; ++I) {
    Register Reg = Register::index2VirtReg(I);
    auto RL = Reg2Local.find(Reg);
    if (RL == Reg2Local.end() || RL->second < MFI.getParams().size())
      continue;

    MFI.setLocal(RL->second - MFI.getParams().size(),
                 typeForRegClass(MRI.getRegClass(Reg)));
    Changed = true;
  }

#ifndef NDEBUG
  // Assert that all registers have been stackified at this point.
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || MI.isLabel())
        continue;
      for (const MachineOperand &MO : MI.explicit_operands()) {
        assert((!MO.isReg() || MRI.use_empty(MO.getReg()) ||
                MFI.isVRegStackified(MO.getReg())) &&
               "MSILExplicitLocals failed to stackify a register operand");
      }
    }
  }
#endif

  return Changed;
}

bool MSILExplicitLocalsLegacy::runOnMachineFunction(MachineFunction &MF) {
  return explicitLocals(MF);
}
