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
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Print MCInst instructions as ILAsm text. The TableGen'erated printer prints
/// the asm strings of MSILInstrInfo.td; 'switch', 'call' and 'calli', whose
/// operands are variadic, and the offset of a global address are printed
/// here.
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
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

#include "MSILGenAsmWriter.inc"

MSILInstPrinter::MSILInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                                 const MCRegisterInfo &MRI)
    : MCInstPrinter(MAI, MII, MRI) {}

void MSILInstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) {
  // Values live on the CIL evaluation stack: register operands of MCInsts are
  // placeholders without a register, and only the fake registers have names.
  if (Reg.isValid())
    OS << getRegisterName(Reg);
}

void MSILInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                StringRef Annot, const MCSubtargetInfo &STI,
                                raw_ostream &OS) {
  // One CIL opcode per line with a two-space indent; asm strings that print
  // several opcodes separate them with a newline and the same indent.
  OS << "  ";
  switch (MI->getOpcode()) {
  case MSIL::BR_TABLE_I32:
    printBrTable(MI, STI, OS);
    break;
  case MSIL::CALL:
  case MSIL::CALL_INDIRECT:
    printCall(MI, STI, OS);
    break;
  default: {
    // Records with an empty asm string (ARGUMENT_*, COPY_*, TEE_*, the call
    // sequence markers and the CALL_PARAMS/CALL_RESULTS pseudos) are never
    // lowered to MCInsts.
    if (!getMnemonic(*MI).first)
      reportFatalInternalError(Twine("MSIL: instruction ") +
                               MII.getName(MI->getOpcode()) +
                               " has no ILAsm text");
    // The generated printer starts every instruction with a tab: replace it by
    // the indent printed above.
    SmallString<64> Text;
    raw_svector_ostream TextOS(Text);
    printInstruction(MI, Address, STI, TextOS);
    StringRef Str = Text.str();
    Str.consume_front("\t");
    OS << Str;
    if (MI->getOpcode() == MSIL::GLOBAL_ADDR)
      printGlobalAddrOffset(MI, OS);
    break;
  }
  }
  printAnnotation(OS, Annot);
}

void MSILInstPrinter::printBrTable(const MCInst *MI, const MCSubtargetInfo &STI,
                                   raw_ostream &O) {
  // switch (@BB0_1, @BB0_2)
  // br @BB0_3
  // The int32 index is on the stack (a register placeholder); the other
  // operands are the targets, and the last one is the default, reached with
  // 'br' when the index is out of range.
  unsigned First = 0, E = MI->getNumOperands();
  while (First != E && MI->getOperand(First).isReg())
    ++First;
  if (First == E)
    reportFatalInternalError("MSIL: switch without a default target");
  O << getMnemonic(*MI).first << " (";
  ListSeparator LS;
  for (unsigned I = First; I + 1 < E; ++I) {
    O << LS;
    printOperand(MI, I, STI, O);
  }
  O << ")\n  br ";
  printOperand(MI, E - 1, STI, O);
}

void MSILInstPrinter::printCall(const MCInst *MI, const MCSubtargetInfo &STI,
                                raw_ostream &O) {
  // The results and arguments are register placeholders. The one other
  // operand is the method reference of a 'call' (operand index = number of
  // results) or the call-site signature of a 'calli', both MSILMemberRefExprs.
  unsigned I = 0, E = MI->getNumOperands();
  while (I != E && MI->getOperand(I).isReg())
    ++I;
  if (I == E) {
    if (MI->getOpcode() == MSIL::CALL_INDIRECT)
      reportFatalInternalError("MSIL: calli signatures are implemented in M9");
    reportFatalInternalError("MSIL: call without a method reference");
  }
  O << getMnemonic(*MI).first << ' ';
  printOperand(MI, I, STI, O);
}

void MSILInstPrinter::printGlobalAddrOffset(const MCInst *MI, raw_ostream &O) {
  // 'ldsfld native int <field>' loads the address of the global; a folded
  // offset is added after it (GLOBAL_ADDR has StackPeakExtra 1).
  const MCOperand &Op = MI->getOperand(1);
  if (!Op.isExpr())
    return;
  const auto *Ref = dyn_cast<MSILMemberRefExpr>(Op.getExpr());
  if (!Ref || Ref->getOffset() == 0)
    return;
  int64_t Offset = Ref->getOffset();
  if (isInt<32>(Offset))
    O << "\n  ldc.i4 " << Offset;
  else
    O << "\n  ldc.i8 " << Offset << "\n  conv.i";
  O << "\n  add";
}

void MSILInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                   const MCSubtargetInfo &STI, raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    // A value on the evaluation stack: the opcode consumes or produces it.
    return;
  }
  if (Op.isImm()) {
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
  assert(Op.isExpr() && "unknown operand kind in printOperand");
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

void MSILInstPrinter::printMemFlagsOperand(const MCInst *MI, unsigned OpNo,
                                           const MCSubtargetInfo &STI,
                                           raw_ostream &O) {
  uint64_t Flags = MI->getOperand(OpNo).getImm();
  assert((Flags & ~uint64_t(MSIL::MemFlagsUnalignedMask |
                            MSIL::MemFlagsVolatile)) == 0 &&
         "unknown memflags bits");
  if (unsigned Unaligned = Flags & MSIL::MemFlagsUnalignedMask) {
    assert((Unaligned == 1 || Unaligned == 2 || Unaligned == 4) &&
           "'unaligned.' takes 1, 2 or 4");
    O << "unaligned. " << Unaligned << "\n  ";
  }
  if (Flags & MSIL::MemFlagsVolatile)
    O << "volatile.\n  ";
}
