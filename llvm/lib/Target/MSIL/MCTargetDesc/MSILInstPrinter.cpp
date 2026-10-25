//===-- MSILInstPrinter.cpp - Print MSIL MCInsts as ILAsm -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from
// llvm/lib/Target/WebAssembly/MCTargetDesc/WebAssemblyInstPrinter.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Print MCInst instructions as ILAsm text. Every MCInst is one CIL
/// instruction, a CIL_ record of MSILInstrCIL.td, and the TableGen'erated
/// printer prints its asm string: the CIL mnemonic and, if the opcode has one,
/// its inline operand (an index, a constant, a label such as '@BB0_2' from
/// MCAsmInfo's InternalSymbolPrefix, or a token). Only 'switch', whose labels
/// are variadic, is printed here.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILInstPrinter.h"
#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

#include "MSILGenAsmWriter.inc"

MSILInstPrinter::MSILInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                                 const MCRegisterInfo &MRI)
    : MCInstPrinter(MAI, MII, MRI) {}

bool MSILInstPrinter::applyTargetSpecificCLOption(StringRef Opt) {
  // The text of a CIL object (plan 5.8.4), which M19 prints.
  if (Opt == "object-form") {
    ObjectForm = true;
    return true;
  }
  return false;
}

void MSILInstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) {
  // Values live on the CIL evaluation stack: CIL instructions have no register
  // operands, and only the fake registers have names.
  if (Reg.isValid())
    OS << getRegisterName(Reg);
}

void MSILInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                StringRef Annot, const MCSubtargetInfo &STI,
                                raw_ostream &OS) {
  // Only CIL_ records reach a streamer: MSILMCInstLower expands every
  // MachineInstr-level record into them, and the AsmPrinter's synthesized
  // methods are built from them. Every CIL_ record has an asm string.
  unsigned Opcode = MI->getOpcode();
  if (!MII.getName(Opcode).starts_with("CIL_"))
    reportFatalInternalError(Twine("MSIL: ") + MII.getName(Opcode) +
                             " is not a CIL instruction record");
  // One CIL instruction per line with a two-space indent.
  OS << "  ";
  if (Opcode == MSIL::CIL_Switch) {
    printSwitch(MI, STI, OS);
  } else {
    // The generated printer starts every instruction with a tab: the indent
    // above replaces it.
    SmallString<64> Text;
    raw_svector_ostream TextOS(Text);
    printInstruction(MI, Address, STI, TextOS);
    OS << StringRef(Text).ltrim('\t');
  }
  printAnnotation(OS, Annot);
}

void MSILInstPrinter::printSwitch(const MCInst *MI, const MCSubtargetInfo &STI,
                                  raw_ostream &O) {
  // switch (@BB0_1, @BB0_2)
  // The whole table on one line (ilasm 4.8 and 10 accept 20000 targets). An
  // empty table is valid CIL ('switch ()' falls through).
  O << getMnemonic(*MI).first << " (";
  ListSeparator LS;
  for (unsigned I = 0, E = MI->getNumOperands(); I != E; ++I) {
    O << LS;
    printOperand(MI, I, STI, O);
  }
  O << ')';
}

void MSILInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                   const MCSubtargetInfo &STI, raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm()) {
    // A constant, or a branch displacement (ILAsm's numeric branch target).
    O << Op.getImm();
    return;
  }
  if (Op.isSFPImm()) {
    printF32ImmOperand(MI, OpNo, STI, O);
    return;
  }
  if (Op.isDFPImm()) {
    printF64ImmOperand(MI, OpNo, STI, O);
    return;
  }
  if (!Op.isExpr())
    reportFatalInternalError(Twine("MSIL: unexpected operand ") + Twine(OpNo) +
                             " of " + MII.getName(MI->getOpcode()));
  MAI.printExpr(O, *Op.getExpr());
}

void MSILInstPrinter::printLocalOperand(const MCInst *MI, unsigned OpNo,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  assert(Op.isImm() && "a local index must be an immediate");
  O << Op.getImm();
}

void MSILInstPrinter::printArgOperand(const MCInst *MI, unsigned OpNo,
                                      const MCSubtargetInfo &STI,
                                      raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  assert(Op.isImm() && "an argument index must be an immediate");
  O << Op.getImm();
}

void MSILInstPrinter::printF32ImmOperand(const MCInst *MI, unsigned OpNo,
                                         const MCSubtargetInfo &STI,
                                         raw_ostream &O) {
  // The bit pattern keeps NaN payloads, -0.0 and infinities exact.
  const MCOperand &Op = MI->getOperand(OpNo);
  O << "float32(0x" << format_hex_no_prefix(Op.getSFPImm(), 8, /*Upper=*/true)
    << ')';
}

void MSILInstPrinter::printF64ImmOperand(const MCInst *MI, unsigned OpNo,
                                         const MCSubtargetInfo &STI,
                                         raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  O << "float64(0x" << format_hex_no_prefix(Op.getDFPImm(), 16, /*Upper=*/true)
    << ')';
}

void MSILInstPrinter::printTokenOperand(const MCInst *MI, unsigned OpNo,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm()) {
    // A final token, as an ILAsm parser gives it.
    O << "mdtoken(0x"
      << format_hex_no_prefix(uint32_t(Op.getImm()), 8, /*Upper=*/true) << ')';
    return;
  }
  if (!Op.isExpr())
    reportFatalInternalError(Twine("MSIL: the token of ") +
                             MII.getName(MI->getOpcode()) +
                             " is not an expression");
  if (const auto *Ref = dyn_cast<MSILMemberRefExpr>(Op.getExpr())) {
    Ref->printOperand(O, ObjectForm);
    return;
  }
  MAI.printExpr(O, *Op.getExpr());
}
