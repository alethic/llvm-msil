//===-- MSILMCCodeEmitter.cpp - Convert MSIL code to machine code ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from
// llvm/lib/Target/WebAssembly/MCTargetDesc/WebAssemblyMCCodeEmitter.cpp at
// 64f50ffd4f1c.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements MSILMCCodeEmitter, the binary encoder of CIL
/// (ECMA-335 III.1.2). Every MCInst that reaches it is one CIL instruction (a
/// CIL_ record, MSIL::isCILRecord), and the encoding is table-driven: the
/// opcode value comes from the record's TSFlags (MSILII::CILOpcodeMask) and
/// the encoding of the inline operand from the opcode's cli::OperandKind
/// (CILOpcodes.def), so the printer and the encoder read one record and
/// nothing here names an instruction.
///
/// The MCInst operands of a CIL instruction, by OperandKind:
///   InlineNone                     none;
///   ShortInlineVar, InlineVar,
///   ShortInlineI, InlineI, InlineI8 one Imm;
///   ShortInlineR, InlineR          one SFPImm or DFPImm bit pattern;
///   ShortInlineBrTarget,
///   InlineBrTarget                 one label Expr, or an Imm displacement
///                                  from the next instruction;
///   InlineMethod, InlineField, InlineType, InlineTok, InlineSig,
///   InlineString                   one token Expr (MSILMemberRefExpr), or an
///                                  Imm holding a final token;
///   InlineSwitch                   one label Expr (or Imm displacement from
///                                  the end of the switch) per target.
/// An Imm branch target or token is what an ILAsm parser gives for ILAsm's
/// numeric branch offsets and raw tokens; codegen always uses Exprs.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILFixupKinds.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "msil-mc-code-emitter"

STATISTIC(MCNumEmitted, "Number of MC instructions emitted.");
STATISTIC(MCNumFixups, "Number of MC fixups created.");

/// The CIL opcode of \p MI's record, which must be a CIL instruction record
/// (MSIL::isCILRecord). Anything else is an internal error, never an
/// encoding: a pseudo or a record that stands for several CIL instructions
/// (value MSIL::NoCILOpcode), which MSILMCInstLower expands into CIL_ records
/// first; a MachineInstr-level one-to-one record such as ADD_I32, whose value
/// is its CIL_ twin's; and a target-independent opcode such as KILL or
/// IMPLICIT_DEF, whose TSFlags value 0 would otherwise encode as nop.
/// encodeInstruction, MSIL::getCILInstSize and MSIL::relaxBranches (through
/// getCILInstSize) all stop here.
static cli::Opcode getCILOpcode(const MCInst &MI, const MCInstrInfo &MCII) {
  unsigned Opcode = MI.getOpcode();
  uint16_t Value = MSIL::getCILOpcodeValue(MCII.get(Opcode));
  std::optional<cli::Opcode> Op;
  if (MSIL::isCILRecord(MCII, Opcode))
    Op = cli::getOpcodeByValue(Value);
  if (!Op)
    reportFatalInternalError(Twine("MSIL: '") + MCII.getName(Opcode) +
                             "' (CIL opcode value 0x" +
                             utohexstr(Value, /*LowerCase=*/false, 4) +
                             ") is not a CIL instruction");
  return *Op;
}

unsigned llvm::MSIL::getCILInstSize(const MCInst &MI, const MCInstrInfo &MCII) {
  const cli::OpcodeInfo &Info = cli::getOpcodeInfo(getCILOpcode(MI, MCII));
  unsigned Size =
      cli::getOpcodeSize(Info.Op) + cli::getOperandSize(Info.Operand);
  // A switch's count is followed by one 32-bit entry per target.
  if (Info.Operand == cli::OperandKind::InlineSwitch)
    Size += 4 * MI.getNumOperands();
  return Size;
}

/// The fixup kind of a token operand of kind \p K.
static MSIL::Fixups getTokenFixupKind(cli::OperandKind K) {
  switch (K) {
  case cli::OperandKind::InlineMethod:
    return MSIL::fixup_cil_method_token;
  case cli::OperandKind::InlineField:
    return MSIL::fixup_cil_field_token;
  case cli::OperandKind::InlineSig:
    return MSIL::fixup_cil_sig_token;
  case cli::OperandKind::InlineType:
    return MSIL::fixup_cil_type_token;
  case cli::OperandKind::InlineTok:
    return MSIL::fixup_cil_tok_token;
  case cli::OperandKind::InlineString:
    return MSIL::fixup_cil_string_token;
  default:
    llvm_unreachable("not a token operand kind");
  }
}

namespace {

class MSILMCCodeEmitter final : public MCCodeEmitter {
  const MCInstrInfo &MCII;
  MCContext &Ctx;

  /// Report an operand list that does not match \p Info's OperandKind: the
  /// MCInst was built wrongly (MCInstLower or a parser), an internal error.
  [[noreturn]] void reportBadOperands(const MCInst &MI,
                                      const cli::OpcodeInfo &Info,
                                      const Twine &Problem) const;

  /// Encode the integer operand of \p Info, checking that it fits the
  /// operand's ECMA type.
  void encodeInteger(const MCInst &MI, const cli::OpcodeInfo &Info,
                     SmallVectorImpl<char> &CB) const;

  /// Encode a branch or switch target of \p Size bytes (1 or 4): an Imm is the
  /// displacement itself; an Expr gets a PC-relative FK_Data_<Size> fixup on
  /// Expr + \p Addend, so that the assembler's value (target minus the fixup's
  /// position) is the displacement from the end of the instruction.
  void encodeTarget(const MCInst &MI, const cli::OpcodeInfo &Info,
                    const MCOperand &MO, unsigned Size, int64_t Addend,
                    uint64_t Start, SmallVectorImpl<char> &CB,
                    SmallVectorImpl<MCFixup> &Fixups) const;

  /// Encode a metadata token: an Imm is the final token; an Expr gets four
  /// zero bytes and a fixup_cil_<class>_token fixup, which the object writer
  /// resolves.
  void encodeToken(const MCInst &MI, const cli::OpcodeInfo &Info,
                   uint64_t Start, SmallVectorImpl<char> &CB,
                   SmallVectorImpl<MCFixup> &Fixups) const;

public:
  MSILMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx)
      : MCII(MCII), Ctx(Ctx) {}

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;
};

} // end anonymous namespace

MCCodeEmitter *llvm::createMSILMCCodeEmitter(const MCInstrInfo &MCII,
                                             MCContext &Ctx) {
  return new MSILMCCodeEmitter(MCII, Ctx);
}

void MSILMCCodeEmitter::reportBadOperands(const MCInst &MI,
                                          const cli::OpcodeInfo &Info,
                                          const Twine &Problem) const {
  reportFatalInternalError(Twine("MSIL: cannot encode '") + Info.Name + "' (" +
                           MCII.getName(MI.getOpcode()) + "): " + Problem);
}

void MSILMCCodeEmitter::encodeInteger(const MCInst &MI,
                                      const cli::OpcodeInfo &Info,
                                      SmallVectorImpl<char> &CB) const {
  const MCOperand &MO = MI.getOperand(0);
  if (!MO.isImm())
    reportBadOperands(MI, Info, "the operand is not an immediate");
  int64_t Imm = MO.getImm();
  bool Fits;
  switch (Info.Operand) {
  case cli::OperandKind::ShortInlineVar:
    Fits = isUInt<8>(Imm);
    support::endian::write<uint8_t>(CB, uint8_t(Imm), llvm::endianness::little);
    break;
  case cli::OperandKind::InlineVar:
    Fits = isUInt<16>(Imm);
    support::endian::write<uint16_t>(CB, uint16_t(Imm),
                                     llvm::endianness::little);
    break;
  case cli::OperandKind::ShortInlineI:
    // ldc.i4.s takes an int8; unaligned. (the alignment) and no. (the
    // checks) take a uint8.
    Fits = Info.Op == cli::Opcode::Ldc_i4_s ? isInt<8>(Imm) : isUInt<8>(Imm);
    support::endian::write<uint8_t>(CB, uint8_t(Imm), llvm::endianness::little);
    break;
  case cli::OperandKind::InlineI:
    Fits = isInt<32>(Imm);
    support::endian::write<uint32_t>(CB, uint32_t(Imm),
                                     llvm::endianness::little);
    break;
  case cli::OperandKind::InlineI8:
    Fits = true;
    support::endian::write<uint64_t>(CB, uint64_t(Imm),
                                     llvm::endianness::little);
    break;
  default:
    llvm_unreachable("not an integer operand kind");
  }
  if (!Fits)
    Ctx.reportError(MI.getLoc(), Twine("operand of '") + Info.Name +
                                     "' out of range: " + Twine(Imm));
}

void MSILMCCodeEmitter::encodeTarget(const MCInst &MI,
                                     const cli::OpcodeInfo &Info,
                                     const MCOperand &MO, unsigned Size,
                                     int64_t Addend, uint64_t Start,
                                     SmallVectorImpl<char> &CB,
                                     SmallVectorImpl<MCFixup> &Fixups) const {
  assert((Size == 1 || Size == 4) && "a CIL branch target is 1 or 4 bytes");
  // An Expr's bytes stay zero until its fixup is applied.
  int64_t Disp = 0;
  if (MO.isImm()) {
    Disp = MO.getImm();
    if (Size == 1 ? !isInt<8>(Disp) : !isInt<32>(Disp))
      Ctx.reportError(MI.getLoc(), Twine("branch displacement of '") +
                                       Info.Name +
                                       "' out of range: " + Twine(Disp));
  } else {
    if (!MO.isExpr())
      reportBadOperands(MI, Info, "a target is not a label or a displacement");
    const MCExpr *Value = MCBinaryExpr::createAdd(
        MO.getExpr(), MCConstantExpr::create(Addend, Ctx), Ctx);
    Fixups.push_back(MCFixup::create(uint32_t(CB.size() - Start), Value,
                                     MCFixup::getDataKindForSize(Size),
                                     /*PCRel=*/true));
    ++MCNumFixups;
  }
  if (Size == 1)
    support::endian::write<uint8_t>(CB, uint8_t(Disp),
                                    llvm::endianness::little);
  else
    support::endian::write<uint32_t>(CB, uint32_t(Disp),
                                     llvm::endianness::little);
}

void MSILMCCodeEmitter::encodeToken(const MCInst &MI,
                                    const cli::OpcodeInfo &Info, uint64_t Start,
                                    SmallVectorImpl<char> &CB,
                                    SmallVectorImpl<MCFixup> &Fixups) const {
  const MCOperand &MO = MI.getOperand(0);
  if (MO.isImm()) {
    if (!isUInt<32>(MO.getImm()))
      reportBadOperands(MI, Info, "the token does not fit in 32 bits");
    support::endian::write<uint32_t>(CB, uint32_t(MO.getImm()),
                                     llvm::endianness::little);
    return;
  }
  if (!MO.isExpr())
    reportBadOperands(MI, Info, "the operand is not a token");
  Fixups.push_back(MCFixup::create(uint32_t(CB.size() - Start), MO.getExpr(),
                                   getTokenFixupKind(Info.Operand)));
  ++MCNumFixups;
  support::endian::write<uint32_t>(CB, 0, llvm::endianness::little);
}

void MSILMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                          SmallVectorImpl<char> &CB,
                                          SmallVectorImpl<MCFixup> &Fixups,
                                          const MCSubtargetInfo &STI) const {
  const cli::OpcodeInfo &Info = cli::getOpcodeInfo(getCILOpcode(MI, MCII));
  uint64_t Start = CB.size();

  // The opcode: one byte, or the 0xFE prefix and one byte (III.1.2.1).
  uint16_t Value = uint16_t(Info.Op);
  if (cli::getOpcodeSize(Info.Op) == 2)
    CB.push_back(char(Value >> 8));
  CB.push_back(char(Value & 0xFF));

  // The inline operand, little-endian.
  unsigned NumOps = MI.getNumOperands();
  unsigned OperandSize = cli::getOperandSize(Info.Operand);
  switch (Info.Operand) {
  case cli::OperandKind::InlineNone:
    if (NumOps != 0)
      reportBadOperands(MI, Info, "expected no operands");
    break;

  case cli::OperandKind::InlineSwitch: {
    // The count N, then entry I as a displacement from the end of the switch:
    // Expr - 4 * (N - I) relative to the entry's own position.
    support::endian::write<uint32_t>(CB, NumOps, llvm::endianness::little);
    for (unsigned I = 0; I != NumOps; ++I)
      encodeTarget(MI, Info, MI.getOperand(I), 4, -4 * int64_t(NumOps - I),
                   Start, CB, Fixups);
    break;
  }

  default: {
    if (NumOps != 1)
      reportBadOperands(MI, Info, "expected one operand");
    const MCOperand &MO = MI.getOperand(0);
    switch (Info.Operand) {
    case cli::OperandKind::ShortInlineVar:
    case cli::OperandKind::InlineVar:
    case cli::OperandKind::ShortInlineI:
    case cli::OperandKind::InlineI:
    case cli::OperandKind::InlineI8:
      encodeInteger(MI, Info, CB);
      break;

    case cli::OperandKind::ShortInlineR:
      // The exact bits: a signaling NaN stays signaling.
      if (!MO.isSFPImm())
        reportBadOperands(MI, Info, "the operand is not a float32 pattern");
      support::endian::write<uint32_t>(CB, MO.getSFPImm(),
                                       llvm::endianness::little);
      break;
    case cli::OperandKind::InlineR:
      if (!MO.isDFPImm())
        reportBadOperands(MI, Info, "the operand is not a float64 pattern");
      support::endian::write<uint64_t>(CB, MO.getDFPImm(),
                                       llvm::endianness::little);
      break;

    case cli::OperandKind::ShortInlineBrTarget:
    case cli::OperandKind::InlineBrTarget:
      // The displacement counts from the end of the branch, which is where
      // its last OperandSize bytes end.
      encodeTarget(MI, Info, MO, OperandSize, -int64_t(OperandSize), Start, CB,
                   Fixups);
      break;

    case cli::OperandKind::InlineMethod:
    case cli::OperandKind::InlineField:
    case cli::OperandKind::InlineType:
    case cli::OperandKind::InlineTok:
    case cli::OperandKind::InlineSig:
    case cli::OperandKind::InlineString:
      encodeToken(MI, Info, Start, CB, Fixups);
      break;

    case cli::OperandKind::InlineNone:
    case cli::OperandKind::InlineSwitch:
      llvm_unreachable("handled above");
    }
    break;
  }
  }

  assert(CB.size() - Start == MSIL::getCILInstSize(MI, MCII) &&
         "getCILInstSize disagrees with the encoding");
  ++MCNumEmitted; // Keep track of the # of mi's emitted.
}
