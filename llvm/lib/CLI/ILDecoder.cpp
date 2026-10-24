//===- ILDecoder.cpp - ECMA-335 CIL decoder -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Decodes CIL instruction streams with the opcode table of Opcodes.cpp, and
// rewrites their token operands in place.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/ILDecoder.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include <utility>

using namespace llvm;
using namespace llvm::cli;
using namespace llvm::support;

static Error makeILError(const Twine &Msg, uint64_t Offset) {
  return createStringError(make_error_code(errc::illegal_byte_sequence),
                           "invalid IL: " + Msg + " at offset 0x" +
                               utohexstr(Offset));
}

static bool isTokenOperand(OperandKind K) {
  switch (K) {
  case OperandKind::InlineMethod:
  case OperandKind::InlineField:
  case OperandKind::InlineType:
  case OperandKind::InlineTok:
  case OperandKind::InlineSig:
  case OperandKind::InlineString:
    return true;
  default:
    return false;
  }
}

Error cli::decodeIL(ArrayRef<uint8_t> Code,
                    function_ref<Error(const ILInstruction &)> Visit) {
  ILInstruction I;
  const uint64_t End = Code.size();
  uint64_t Pos = 0;
  while (Pos < End) {
    // Reset every field, but keep SwitchTargets' allocation.
    I.Offset = Pos;
    I.Size = 0;
    I.Imm = 0;
    I.FPBits = 0;
    I.Token = 0;
    I.TokenOffset = 0;
    I.Target = 0;
    I.SwitchTargets.clear();

    uint8_t Lead = Code[Pos];
    uint16_t Value;
    if (Lead == TwoByteOpcodePrefix) {
      if (Pos + 2 > End)
        return makeILError("truncated two-byte opcode", Pos);
      Value = uint16_t(TwoByteOpcodePrefix) << 8 | Code[Pos + 1];
    } else if (Lead >= 0xF8) {
      return makeILError("reserved prefix byte 0x" + utohexstr(Lead), Pos);
    } else {
      Value = Lead;
    }
    std::optional<Opcode> Op = getOpcodeByValue(Value);
    if (!Op)
      return makeILError("unknown opcode 0x" + utohexstr(Value), Pos);
    const OpcodeInfo &Info = getOpcodeInfo(*Op);
    I.Op = *Op;
    uint64_t OperandPos = Pos + getOpcodeSize(*Op);
    uint64_t Next = OperandPos + getOperandSize(Info.Operand);
    if (Next > End)
      return makeILError("truncated operand of '" + Info.Name + "'", Pos);
    const uint8_t *P = Code.data() + OperandPos;

    switch (Info.Operand) {
    case OperandKind::InlineNone:
      break;
    case OperandKind::ShortInlineVar:
      I.Imm = P[0];
      break;
    case OperandKind::InlineVar:
      I.Imm = endian::read16le(P);
      break;
    case OperandKind::ShortInlineI:
      // ldc.i4.s takes a signed byte; unaligned. and no. take flags.
      I.Imm = *Op == Opcode::Ldc_i4_s ? int64_t(int8_t(P[0])) : int64_t(P[0]);
      break;
    case OperandKind::InlineI:
      I.Imm = int32_t(endian::read32le(P));
      break;
    case OperandKind::InlineI8:
      I.Imm = int64_t(endian::read64le(P));
      break;
    case OperandKind::ShortInlineR:
      I.FPBits = endian::read32le(P);
      break;
    case OperandKind::InlineR:
      I.FPBits = endian::read64le(P);
      break;
    case OperandKind::ShortInlineBrTarget:
      I.Target = int64_t(Next) + int8_t(P[0]);
      break;
    case OperandKind::InlineBrTarget:
      I.Target = int64_t(Next) + int32_t(endian::read32le(P));
      break;
    case OperandKind::InlineMethod:
    case OperandKind::InlineField:
    case OperandKind::InlineType:
    case OperandKind::InlineTok:
    case OperandKind::InlineSig:
    case OperandKind::InlineString:
      I.Token = endian::read32le(P);
      I.TokenOffset = OperandPos;
      break;
    case OperandKind::InlineSwitch: {
      uint32_t Count = endian::read32le(P);
      Next += uint64_t(Count) * 4;
      if (Next > End)
        return makeILError(
            "truncated switch table of " + Twine(Count) + " targets", Pos);
      I.SwitchTargets.reserve(Count);
      for (uint32_t J = 0; J != Count; ++J)
        I.SwitchTargets.push_back(int64_t(Next) +
                                  int32_t(endian::read32le(P + 4 + 4 * J)));
      break;
    }
    }

    I.Size = Next - Pos;
    if (Error E = Visit(I))
      return E;
    Pos = Next;
  }
  return Error::success();
}

Error cli::rewriteILTokens(
    MutableArrayRef<uint8_t> Code,
    function_ref<Expected<uint32_t>(const ILInstruction &)> Map) {
  // Map every token first, so that the code changes only if everything
  // succeeds.
  SmallVector<std::pair<uint32_t, uint32_t>, 0> NewTokens;
  if (Error E = decodeIL(Code, [&](const ILInstruction &I) -> Error {
        if (!isTokenOperand(getOpcodeInfo(I.Op).Operand))
          return Error::success();
        Expected<uint32_t> Token = Map(I);
        if (!Token)
          return Token.takeError();
        NewTokens.emplace_back(I.TokenOffset, *Token);
        return Error::success();
      }))
    return E;
  for (auto [Offset, Token] : NewTokens)
    endian::write32le(Code.data() + Offset, Token);
  return Error::success();
}
