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
/// Print MCInst instructions as ILAsm text. The TableGen'erated printer prints
/// the asm strings of MSILInstrInfo.td: plain opcodes such as 'add' or
/// 'cgt.un', and branches, which always use the long forms ('br', 'brtrue',
/// 'brfalse' and the fused compare-and-branch opcodes 'beq', 'bne.un', 'blt',
/// 'blt.un', 'ble', 'ble.un', 'bgt', 'bgt.un', 'bge', 'bge.un') with a block
/// label operand ('@BB<function>_<block>', from MCAsmInfo's
/// InternalSymbolPrefix). Printed here instead: the short forms of the
/// argument, local and constant opcodes, which depend on the value of the
/// immediate; 'switch', 'call' and 'calli', whose operands are variadic; and
/// the offset of a global address.
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
#include <optional>

using namespace llvm;

#include "MSILGenAsmWriter.inc"

namespace {
/// The instructions whose CIL text depends on the value of an immediate
/// operand (plan section 5.1). Every other instruction prints its asm string.
enum class ShortFormKind : uint8_t {
  None,
  LdArg,    ///< ldarg.0-3, ldarg.s N (N < 256), ldarg N
  StArg,    ///< starg.s N (N < 256), starg N
  TeeArg,   ///< dup, then StArg
  LdLoc,    ///< ldloc.0-3, ldloc.s N (N < 256), ldloc N
  StLoc,    ///< stloc.0-3, stloc.s N (N < 256), stloc N
  TeeLoc,   ///< dup, then StLoc
  ConstI32, ///< ldc.i4.m1, ldc.i4.0-8, ldc.i4.s N (-128..127), ldc.i4 N
  ConstI64, ///< ldc.i4 N (N fits in 32 bits) or ldc.i8 N, then conv.i (D2)
};

struct ShortForm {
  ShortFormKind Kind = ShortFormKind::None;
  /// The operand that holds the index or the constant (the operand lists of
  /// plan section 5.4).
  unsigned ImmOp = 0;
};
} // end anonymous namespace

/// The one table of the instructions printed in a short form.
static ShortForm getShortForm(unsigned Opcode) {
  switch (Opcode) {
  case MSIL::ARG_GET_I32:
  case MSIL::ARG_GET_I64:
  case MSIL::ARG_GET_F32:
  case MSIL::ARG_GET_F64:
    return {ShortFormKind::LdArg, 1};
  case MSIL::ARG_SET_I32:
  case MSIL::ARG_SET_I64:
  case MSIL::ARG_SET_F32:
  case MSIL::ARG_SET_F64:
    return {ShortFormKind::StArg, 0};
  case MSIL::ARG_TEE_I32:
  case MSIL::ARG_TEE_I64:
  case MSIL::ARG_TEE_F32:
  case MSIL::ARG_TEE_F64:
    return {ShortFormKind::TeeArg, 1};
  case MSIL::LOCAL_GET_I32:
  case MSIL::LOCAL_GET_I64:
  case MSIL::LOCAL_GET_F32:
  case MSIL::LOCAL_GET_F64:
    return {ShortFormKind::LdLoc, 1};
  case MSIL::LOCAL_SET_I32:
  case MSIL::LOCAL_SET_I64:
  case MSIL::LOCAL_SET_F32:
  case MSIL::LOCAL_SET_F64:
    return {ShortFormKind::StLoc, 0};
  case MSIL::LOCAL_TEE_I32:
  case MSIL::LOCAL_TEE_I64:
  case MSIL::LOCAL_TEE_F32:
  case MSIL::LOCAL_TEE_F64:
    return {ShortFormKind::TeeLoc, 1};
  case MSIL::CONST_I32:
    return {ShortFormKind::ConstI32, 1};
  case MSIL::CONST_I64:
    return {ShortFormKind::ConstI64, 1};
  default:
    return {};
  }
}

/// Print \p Opcode with the argument or local index \p Index in its shortest
/// form: '<Opcode>.N' when N < NumImplicit (the opcodes with the index in the
/// opcode), '<Opcode>.s N' when N fits in a byte, '<Opcode> N' otherwise.
/// ilasm does not shorten opcodes itself.
static void printIndexOpcode(raw_ostream &O, StringRef Opcode,
                             int64_t NumImplicit, int64_t Index) {
  assert(Index >= 0 && "argument and local indices are unsigned");
  if (Index < NumImplicit)
    O << Opcode << '.' << Index;
  else if (isUInt<8>(Index))
    O << Opcode << ".s " << Index;
  else
    O << Opcode << ' ' << Index;
}

/// Print 'ldc.i4' for \p Value in its shortest form.
static void printLdcI4(raw_ostream &O, int32_t Value) {
  if (Value == -1)
    O << "ldc.i4.m1";
  else if (Value >= 0 && Value <= 8)
    O << "ldc.i4." << Value;
  else if (isInt<8>(Value))
    O << "ldc.i4.s " << Value;
  else
    O << "ldc.i4 " << Value;
}

static void printShortForm(const MCInst *MI, ShortForm Form, raw_ostream &O) {
  int64_t Imm = MI->getOperand(Form.ImmOp).getImm();
  switch (Form.Kind) {
  case ShortFormKind::LdArg:
    printIndexOpcode(O, "ldarg", 4, Imm);
    return;
  case ShortFormKind::TeeArg:
    O << "dup\n  ";
    [[fallthrough]];
  case ShortFormKind::StArg:
    printIndexOpcode(O, "starg", 0, Imm);
    return;
  case ShortFormKind::LdLoc:
    printIndexOpcode(O, "ldloc", 4, Imm);
    return;
  case ShortFormKind::TeeLoc:
    O << "dup\n  ";
    [[fallthrough]];
  case ShortFormKind::StLoc:
    printIndexOpcode(O, "stloc", 4, Imm);
    return;
  case ShortFormKind::ConstI32:
    // MachineOperands hold the constant sign-extended; MIR may spell it
    // zero-extended.
    assert((isInt<32>(Imm) || isUInt<32>(Imm)) && "CONST_I32 out of range");
    printLdcI4(O, static_cast<int32_t>(Imm));
    return;
  case ShortFormKind::ConstI64:
    // native int (decision D2): conv.i sign-extends the int32 of ldc.i4.
    if (isInt<32>(Imm))
      O << "ldc.i4 " << Imm;
    else
      O << "ldc.i8 " << Imm;
    O << "\n  conv.i";
    return;
  case ShortFormKind::None:
    break;
  }
  llvm_unreachable("not an instruction with a short form");
}

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
  if (ShortForm Form = getShortForm(MI->getOpcode());
      Form.Kind != ShortFormKind::None) {
    printShortForm(MI, Form, OS);
    printAnnotation(OS, Annot);
    return;
  }
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
    // sequence markers, the CALL_PARAMS/CALL_RESULTS pseudos and the SELECT_*
    // pseudos, which custom inserters replace) are never lowered to MCInsts.
    const char *Mnemonic = getMnemonic(*MI).first;
    if (!Mnemonic)
      reportFatalInternalError(Twine("MSIL: instruction ") +
                               MII.getName(MI->getOpcode()) +
                               " has no ILAsm text");
    // A short branch ('br.s', 'beq.s', ...) has an 8-bit offset, and only the
    // assembler knows the distance to the label, so the asm strings use the
    // long forms only (plan 5.1).
    assert((!MII.get(MI->getOpcode()).isBranch() ||
            !StringRef(Mnemonic).rtrim().ends_with(".s")) &&
           "branches print their long forms");
    if (MI->getOpcode() == MSIL::GLOBAL_ADDR ||
        MI->getOpcode() == MSIL::FUNC_ADDR)
      checkAddressOperand(MI);
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
  // br @BB0_1
  // Operand 0 is the int32 index, a register placeholder for the value on the
  // stack (decision D35: there is no native int form). The other operands are
  // the block labels of the jump table, which MSILTargetLowering::LowerBR_JT
  // copied into the instruction, followed by the first entry again. 'switch'
  // falls through to the next instruction when the index is out of range
  // (taken as unsigned), so that last operand is the target of the 'br' after
  // it: a dummy fall-through target that is already a CFG successor of the
  // block, not the C switch's default. It never executes: LLVM's range check
  // before the jump table (never removed, D35) sends every out-of-range value
  // to the real default first, and when the default is unreachable an
  // out-of-range index is undefined behaviour.
  // The table is printed whole on one line (ilasm 4.8 and 10 accept 20000
  // targets) and never emitted as data.
  unsigned E = MI->getNumOperands();
  if (E < 2 || !MI->getOperand(0).isReg())
    reportFatalInternalError("MSIL: switch without an index or a fall-through "
                             "target");
  for (unsigned I = 1; I != E; ++I) {
    const MCOperand &Op = MI->getOperand(I);
    if (!Op.isExpr() || !isa<MCSymbolRefExpr>(Op.getExpr()))
      reportFatalInternalError("MSIL: switch target " + Twine(I) +
                               " is not a block label");
  }
  O << getMnemonic(*MI).first << " (";
  ListSeparator LS;
  for (unsigned I = 1; I + 1 < E; ++I) {
    O << LS;
    printOperand(MI, I, STI, O);
  }
  O << ")\n  br ";
  printOperand(MI, E - 1, STI, O);
}

void MSILInstPrinter::printCall(const MCInst *MI, const MCSubtargetInfo &STI,
                                raw_ostream &O) {
  // call int32 'f'(int32, native int)
  // calli int32(native int)
  // The results and the arguments are register placeholders (values the call
  // pushes and pops). The one other operand is an MSILMemberRefExpr: for
  // CALL, whose variadic operands are defs (variadicOpsAreDefs), the method
  // reference after the results (operand index = number of results), which
  // prints the callee's signature exactly as its .method header does; for
  // CALL_INDIRECT the call-site signature, which MSILMCInstLower adds next to
  // the placeholders and the callee address (M9).
  bool IsIndirect = MI->getOpcode() == MSIL::CALL_INDIRECT;
  std::optional<unsigned> RefOp;
  for (unsigned I = 0, E = MI->getNumOperands(); I != E; ++I) {
    if (MI->getOperand(I).isReg())
      continue;
    if (RefOp)
      reportFatalInternalError(Twine("MSIL: ") + MII.getName(MI->getOpcode()) +
                               " has more than one non-register operand");
    RefOp = I;
  }
  if (!RefOp)
    reportFatalInternalError(Twine("MSIL: ") + MII.getName(MI->getOpcode()) +
                             " has no method reference or call-site signature");

  const MCOperand &Op = MI->getOperand(*RefOp);
  const auto *Ref =
      Op.isExpr() ? dyn_cast<MSILMemberRefExpr>(Op.getExpr()) : nullptr;
  // 'call' takes a method (a BCL method is preformatted text); 'calli' takes
  // a stand-alone signature.
  bool KindOK = false;
  if (Ref) {
    switch (Ref->getKind()) {
    case MSILMemberRefExpr::Method:
    case MSILMemberRefExpr::BCLMember:
      KindOK = !IsIndirect;
      break;
    case MSILMemberRefExpr::StandAloneSig:
      KindOK = IsIndirect;
      break;
    case MSILMemberRefExpr::Field:
      break;
    }
  }
  if (!KindOK)
    reportFatalInternalError(Twine("MSIL: ") + MII.getName(MI->getOpcode()) +
                             (IsIndirect ? " without a call-site signature"
                                         : " without a method reference"));
  O << getMnemonic(*MI).first << ' ';
  printOperand(MI, *RefOp, STI, O);
}

void MSILInstPrinter::checkAddressOperand(const MCInst *MI) {
  // GLOBAL_ADDR loads a global's address field (decision D12) and FUNC_ADDR
  // takes a method's address; any other operand would print as a different
  // instruction or as no member at all.
  bool IsGlobal = MI->getOpcode() == MSIL::GLOBAL_ADDR;
  const MCOperand &Op = MI->getOperand(1);
  const auto *Ref =
      Op.isExpr() ? dyn_cast<MSILMemberRefExpr>(Op.getExpr()) : nullptr;
  if (!Ref || Ref->getKind() != (IsGlobal ? MSILMemberRefExpr::Field
                                          : MSILMemberRefExpr::Method))
    reportFatalInternalError(Twine("MSIL: ") + MII.getName(MI->getOpcode()) +
                             (IsGlobal ? " without an address field"
                                       : " without a method reference"));
}

void MSILInstPrinter::printGlobalAddrOffset(const MCInst *MI, raw_ostream &O) {
  // 'ldsfld native int <field>' loads the address of the global; a folded
  // offset is added after it (GLOBAL_ADDR has StackPeakExtra 1): 'ldc.i4'
  // when it fits in an int32, which 'add' takes with a native int, else
  // 'ldc.i8' and 'conv.i' (decision D2).
  const auto *Ref = cast<MSILMemberRefExpr>(MI->getOperand(1).getExpr());
  int64_t Offset = Ref->getOffset();
  if (Offset == 0)
    return;
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
