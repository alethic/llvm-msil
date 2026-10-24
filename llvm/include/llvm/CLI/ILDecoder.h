//===- llvm/CLI/ILDecoder.h - ECMA-335 CIL decoder --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the CIL instruction decoder of the ECMA-335 library and
// the token rewriter built on it: msil-ld's token walker (it renumbers the
// token operands of copied method bodies in place) and the dumpers use them.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_ILDECODER_H
#define LLVM_CLI_ILDECODER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {
namespace cli {

/// One decoded CIL instruction. Only the fields of the instruction's operand
/// kind are set; the others are 0 (or empty).
struct ILInstruction {
  /// The offset of the opcode in the code, and the number of bytes of the
  /// opcode and its operand.
  uint32_t Offset = 0, Size = 0;
  Opcode Op = Opcode::Nop;
  /// ShortInlineI (ldc.i4.s sign-extended; unaligned. and no. zero-extended),
  /// InlineI (sign-extended), InlineI8, ShortInlineVar and InlineVar
  /// (zero-extended).
  int64_t Imm = 0;
  /// ShortInlineR (the low 32 bits) and InlineR: the exact IEEE bits.
  uint64_t FPBits = 0;
  /// InlineMethod, InlineField, InlineType, InlineTok, InlineSig and
  /// InlineString: the token.
  uint32_t Token = 0;
  /// The offset of the token's 4 bytes in the code.
  uint32_t TokenOffset = 0;
  /// Branches: the absolute target offset (encoded relative to the next
  /// instruction). Targets outside the code are not errors.
  int64_t Target = 0;
  /// Switch: the absolute target offsets (encoded relative to the end of the
  /// switch instruction).
  SmallVector<int64_t, 0> SwitchTargets;
};

/// Decodes \p Code instruction by instruction and calls \p Visit on each; an
/// Error from \p Visit stops the walk and is returned. Gives an Error with
/// the prefix "invalid IL: " for a truncated opcode or operand, an unknown
/// opcode, or a reserved prefix byte 0xF8-0xFF other than 0xFE.
LLVM_ABI Error decodeIL(ArrayRef<uint8_t> Code,
                        function_ref<Error(const ILInstruction &)> Visit);

/// Replaces the token operand of every instruction of \p Code that has one
/// (InlineMethod, InlineField, InlineType, InlineTok, InlineSig and
/// InlineString) with the token that \p Map gives for it. Tokens are always 4
/// bytes, so sizes and branch offsets never change. \p Code is changed only
/// if decoding and every \p Map call succeed; otherwise the first Error is
/// returned and \p Code is unchanged.
LLVM_ABI Error
rewriteILTokens(MutableArrayRef<uint8_t> Code,
                function_ref<Expected<uint32_t>(const ILInstruction &)> Map);

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_ILDECODER_H
