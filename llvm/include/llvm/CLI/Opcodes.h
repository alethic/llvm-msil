//===- llvm/CLI/Opcodes.h - ECMA-335 CIL opcode table -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the CIL opcode table of the ECMA-335 library: one entry
// per CILOpcodes.def entry (219, in value order: the 218 opcodes of
// System.Reflection.Emit.OpCodes plus ECMA's 'no.'), with its mnemonic,
// operand kind, stack behaviour and flow control, and the queries that the
// IL builder, the IL decoder and the MSIL target's encoder share.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_OPCODES_H
#define LLVM_CLI_OPCODES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <optional>

namespace llvm {
namespace cli {

/// One CIL opcode (one CILOpcodes.def entry).
struct OpcodeInfo {
  Opcode Op;
  /// The ILAsm mnemonic, e.g. "ldc.i4.s".
  StringLiteral Name;
  /// The encoding of the inline operand.
  OperandKind Operand;
  /// What the instruction pops and pushes (Varpop and Varpush: the count
  /// depends on the call site or the method).
  StackBehaviour Pop, Push;
  FlowControl Flow;
};

/// The entry of \p Op, which must be a CILOpcodes.def value.
LLVM_ABI const OpcodeInfo &getOpcodeInfo(Opcode Op);

/// Every entry, 219 in value order (0x0000-0x00E0, then 0xFE00-0xFE1E). The
/// one-byte holes and FE 08, FE 10 and FE 1B are not opcodes.
LLVM_ABI ArrayRef<OpcodeInfo> getAllOpcodes();

/// The opcode encoded as \p Value (0x00XX for a one-byte opcode, 0xFEXX for a
/// two-byte opcode), or std::nullopt if no opcode has that value.
LLVM_ABI std::optional<Opcode> getOpcodeByValue(uint16_t Value);

/// The opcode whose ILAsm mnemonic is \p Name ("ldc.i4.s"; case-sensitive),
/// or std::nullopt.
LLVM_ABI std::optional<Opcode> getOpcodeByName(StringRef Name);

/// The number of bytes of \p Op's encoding: 1, or 2 for the 0xFEXX opcodes.
LLVM_ABI unsigned getOpcodeSize(Opcode Op);

/// The number of bytes of an inline operand of kind \p K: 0, 1, 2, 4 or 8.
/// InlineSwitch gives 4, the size of the count; the N 32-bit targets that
/// follow it are not included.
LLVM_ABI unsigned getOperandSize(OperandKind K);

/// The short (8-bit displacement) spelling of the branch \p Op: br -> br.s,
/// brfalse -> brfalse.s, ..., blt.un -> blt.un.s, leave -> leave.s. A short
/// branch maps to itself. std::nullopt for every other opcode (switch
/// included).
LLVM_ABI std::optional<Opcode> getShortBranchForm(Opcode Op);

/// The long (32-bit displacement) spelling of the branch \p Op: br.s -> br,
/// ..., leave.s -> leave. A long branch maps to itself. std::nullopt for
/// every other opcode (switch included).
LLVM_ABI std::optional<Opcode> getLongBranchForm(Opcode Op);

/// The number of values that \p B pops: 0-3, or -1 for Varpop. The Push
/// behaviours pop nothing (0).
LLVM_ABI int getStackPops(StackBehaviour B);

/// The number of values that \p B pushes: 0-2, or -1 for Varpush. The Pop
/// behaviours push nothing (0).
LLVM_ABI int getStackPushes(StackBehaviour B);

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_OPCODES_H
