//===-- MSILUtilities.cpp - MSIL utility functions ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyUtilities.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements several utility functions for MSIL.
///
//===----------------------------------------------------------------------===//

#include "MSILUtilities.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSILMachineFunctionInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

StringRef MSIL::getILName(const GlobalValue &GV, const TargetMachine &TM) {
  if (GV.hasName())
    return GlobalValue::dropLLVMManglingEscape(GV.getName());
  // The symbol of an unnamed value lives as long as the MCContext.
  return TM.getSymbol(&GV)->getName();
}

/// Test whether MI is a child of some other node in an expression tree.
bool MSIL::isChild(const MachineInstr &MI, const MSILFunctionInfo &MFI) {
  if (MI.getNumOperands() == 0)
    return false;
  const MachineOperand &MO = MI.getOperand(0);
  if (!MO.isReg() || MO.isImplicit() || !MO.isDef())
    return false;
  Register Reg = MO.getReg();
  return Reg.isVirtual() && MFI.isVRegStackified(Reg);
}

bool MSIL::mayThrow(const MachineInstr &MI) {
  // Phase 1 has no exception handling: C code throws only through calls (a
  // P/Invoke target or a managed callee may raise a .NET exception), and the
  // 'throw' of TRAP ends the program.
  return MI.isCall();
}

const MachineOperand &MSIL::getCalleeOp(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case MSIL::CALL:
    return MI.getOperand(MI.getNumExplicitDefs());
  case MSIL::CALL_INDIRECT:
    return MI.getOperand(MI.getNumExplicitOperands() - 1);
  default:
    llvm_unreachable("Not a call instruction");
  }
}

unsigned MSIL::getCopyOpcodeForRegClass(const TargetRegisterClass *RC) {
  assert(RC != nullptr);
  switch (RC->getID()) {
  case MSIL::I32RegClassID:
    return MSIL::COPY_I32;
  case MSIL::I64RegClassID:
    return MSIL::COPY_I64;
  case MSIL::F32RegClassID:
    return MSIL::COPY_F32;
  case MSIL::F64RegClassID:
    return MSIL::COPY_F64;
  default:
    llvm_unreachable("Unexpected register class");
  }
}
