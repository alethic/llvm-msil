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
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
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

MCOperand
MSILMCInstLower::lowerExternalSymbolOperand(const MachineInstr &MI,
                                            const MachineOperand &MO) const {
  // External symbols are the callees of library calls, and their names are
  // Phase-1 imports or diagnosed by LowerCall (decision D14). They have no IR
  // function type, so the call itself spells the signature of the P/Invoke
  // declaration that the module trailer emits for the symbol.
  StringRef Name = MO.getSymbolName();
  if (MO.getOffset() != 0)
    reportFatalInternalError("MSIL: reference to external symbol '" + Name +
                             "' with an offset");
  if (MSIL::isCallDirect(MI.getOpcode()) && &MSIL::getCalleeOp(MI) == &MO) {
    const MSIL::Signature &Sig =
        Printer.noteExternalSymbol(Name, getCallSiteSignature(MI));
    return MCOperand::createExpr(
        MSILMemberRefExpr::createMethod(Name, &Sig, Ctx));
  }

  // SelectionDAG names library functions only as callees, which LowerCall
  // turns into target symbols itself, so the address of one (FUNC_ADDR of a
  // TargetExternalSymbol) has no signature to take it with. Report it and
  // keep the output well formed.
  const Function &F = MI.getMF()->getFunction();
  F.getContext().diagnose(
      DiagnosticInfoUnsupported(F,
                                "the address of library function '" + Name +
                                    "' is not supported for MSIL",
                                MI.getDebugLoc()));
  return MCOperand::createExpr(MSILMemberRefExpr::createMethod(
      Name, &Printer.internSignature(MSIL::Signature()), Ctx));
}

MSIL::Signature MSILMCInstLower::getCallSiteSignature(const MachineInstr &MI) {
  const MachineFunction &MF = *MI.getMF();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  unsigned PointerSizeInBits = MF.getDataLayout().getPointerSizeInBits();
  MSIL::Signature Sig;
  for (const MachineOperand &MO : MI.explicit_operands()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    const TargetRegisterClass *RC = Reg.isVirtual()
                                        ? MRI.getRegClass(Reg)
                                        : TRI.getMinimalPhysRegClass(Reg);
    MSIL::ValType T =
        MSIL::toValType(*TRI.legalclasstypes_begin(*RC), PointerSizeInBits);
    if (MO.isDef())
      Sig.Results.push_back(T);
    else
      Sig.Params.push_back(T);
  }
  return Sig;
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
      // A branch target, including every target of a switch: the block label,
      // '@BB<f>_<b>'.
      MCOp = MCOperand::createExpr(
          MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
      break;
    case MachineOperand::MO_JumpTableIndex:
      // MSILTargetLowering::LowerBR_JT copies the blocks of a jump table into
      // the operands of BR_TABLE_I32, which lower as the block operands above,
      // so a jump table is never referenced, and never emitted as data
      // (MSILAsmPrinter::emitJumpTableInfo).
      reportFatalInternalError("MSIL: unexpected jump-table index operand");
    case MachineOperand::MO_MCSymbol:
      MCOp =
          MCOperand::createExpr(MCSymbolRefExpr::create(MO.getMCSymbol(), Ctx));
      break;
    case MachineOperand::MO_GlobalAddress: {
      const GlobalValue *GV = MO.getGlobal();
      // The callee of a direct 'call', or the target of 'ldftn' (FUNC_ADDR).
      if (isa<Function>(GV)) {
        MCOp = lowerFunctionOperand(MO);
        break;
      }
      // GLOBAL_ADDR: the global's address field (decision D12), which the
      // module initializer sets, plus the offset that ISel folded into the
      // address (MSILInstPrinter prints the 'ldc.i4 <offset>; add'). A
      // variable that holds no program data (llvm.used, a variable in the
      // llvm.metadata section) has no field, and one that this module does
      // not define is an error, reported at the end of the module, where it
      // still gets a field.
      if (const auto *GVar = dyn_cast<GlobalVariable>(GV)) {
        if (MSIL::isSpecialLLVMGlobal(*GVar))
          Printer.noteNonDataGlobal(*GVar);
        else if (GVar->isDeclarationForLinker())
          Printer.noteExternGlobal(*GVar);
      }
      MCOp = MCOperand::createExpr(MSILMemberRefExpr::createField(
          MSIL::getILName(*GV, Printer.TM), MO.getOffset(), Ctx));
      break;
    }
    case MachineOperand::MO_ExternalSymbol:
      MCOp = lowerExternalSymbolOperand(*MI, MO);
      break;
    }

    OutMI.addOperand(MCOp);
  }
}
