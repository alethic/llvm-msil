//===-- MSILMCInstLower.cpp - Convert MSIL MachineInstr to an MCInst ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyMCInstLower.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains code to lower MSIL MachineInstrs to their corresponding
/// MCInst records.
///
//===----------------------------------------------------------------------===//

#include "MSILMCInstLower.h"
#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
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

MCOperand
MSILMCInstLower::lowerFunctionOperand(const MachineOperand &MO) const {
  const auto &F = cast<Function>(*MO.getGlobal());
  assert(!F.isIntrinsic() && "intrinsics are never called or referenced");
  // MSILTargetLowering::isOffsetFoldingLegal never folds an offset into a
  // function address: 'call' and 'ldftn' name a method, not an address.
  if (MO.getOffset() != 0)
    reportFatalInternalError("MSIL: reference to function '" +
                             MSIL::getILName(F, Printer.TM) +
                             "' with an offset");

  // An undefined function, or one whose body is not compiled here
  // (available_externally), becomes a P/Invoke declaration in the module
  // trailer.
  if (F.isDeclarationForLinker())
    Printer.noteExternFunction(F);

  // The method header of a definition prints the same signature, so the call
  // token always matches the method.
  return MCOperand::createExpr(MSILMemberRefExpr::createMethod(
      MSIL::getILName(F, Printer.TM), &Printer.getSignature(F), Ctx));
}

void MSILMCInstLower::lower(const MachineInstr *MI, MCInst &OutMI) const {
  // The call-site signature operand of 'calli' is built here from the
  // instruction's operands in M9.
  if (MSIL::isCallIndirect(MI->getOpcode()))
    reportFatalUsageError("MSIL: indirect calls are implemented in M9");

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
      // The callee of a direct 'call' (and from M8 the target of 'ldftn').
      if (isa<Function>(GV)) {
        MCOp = lowerFunctionOperand(MO);
        break;
      }
      reportFatalUsageError("MSIL: references to global variables ('" +
                            MSIL::getILName(*GV, Printer.TM) +
                            "') are implemented in M8");
    }
    case MachineOperand::MO_ExternalSymbol:
      reportFatalUsageError(Twine("MSIL: references to external symbols ('") +
                            MO.getSymbolName() + "') are implemented in M8");
    }

    OutMI.addOperand(MCOp);
  }
}
