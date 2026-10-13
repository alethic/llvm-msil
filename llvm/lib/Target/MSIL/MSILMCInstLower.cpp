//===-- MSILMCInstLower.cpp - Convert MSIL MachineInstr to an MCInst ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyMCInstLower.cpp at
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains code to lower MSIL MachineInstrs to their corresponding
/// MCInst records.
///
//===----------------------------------------------------------------------===//

#include "MSILMCInstLower.h"
#include "MSILAsmPrinter.h"
#include "MSILUtilities.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <string>

using namespace llvm;

void MSILMCInstLower::lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());

  for (const MachineOperand &MO : MI->operands()) {
    MCOperand MCOp;
    switch (MO.getType()) {
    default: {
      std::string Str;
      raw_string_ostream OS(Str);
      MI->print(OS);
      reportFatalInternalError("MSIL: unexpected operand type in " +
                               Twine(Str));
    }
    case MachineOperand::MO_Register:
      // Ignore all implicit register operands.
      if (MO.isImplicit())
        continue;
      // CIL instructions take their operands from the evaluation stack and
      // name locals and arguments with immediates, so a register operand only
      // keeps the operand positions of the instruction description (decision
      // D1): it becomes a placeholder that MSILInstPrinter prints as nothing.
      MCOp = MCOperand::createReg(MCRegister());
      break;
    case MachineOperand::MO_Immediate:
      MCOp = MCOperand::createImm(MO.getImm());
      break;
    case MachineOperand::MO_FPImmediate: {
      // Bit patterns, so NaN payloads and -0.0 print exactly.
      const ConstantFP *Imm = MO.getFPImm();
      const uint64_t BitPattern =
          Imm->getValueAPF().bitcastToAPInt().getZExtValue();
      if (Imm->getType()->isFloatTy())
        MCOp = MCOperand::createSFPImm(static_cast<uint32_t>(BitPattern));
      else if (Imm->getType()->isDoubleTy())
        MCOp = MCOperand::createDFPImm(BitPattern);
      else
        llvm_unreachable("unknown floating point immediate type");
      break;
    }
    case MachineOperand::MO_MachineBasicBlock:
      // A branch target: the block label, '@BB<f>_<b>'.
      MCOp = MCOperand::createExpr(
          MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
      break;
    case MachineOperand::MO_MCSymbol:
      MCOp =
          MCOperand::createExpr(MCSymbolRefExpr::create(MO.getMCSymbol(), Ctx));
      break;
    case MachineOperand::MO_GlobalAddress: {
      const GlobalValue *GV = MO.getGlobal();
      StringRef Name = MSIL::getILName(*GV, Printer.TM);
      if (isa<Function>(GV))
        reportFatalUsageError("MSIL: references to functions ('" + Name +
                              "') are implemented in M4");
      reportFatalUsageError("MSIL: references to global variables ('" + Name +
                            "') are implemented in M8");
    }
    case MachineOperand::MO_ExternalSymbol:
      reportFatalUsageError(Twine("MSIL: references to external symbols ('") +
                            MO.getSymbolName() + "') are implemented in M8");
    }

    OutMI.addOperand(MCOp);
  }
}
