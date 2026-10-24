//===- llvm/CLI/MethodBody.h - ECMA-335 method bodies -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the writer and the reader of CIL method bodies
// (II.25.4): the tiny and fat method headers, the code, and the exception
// handling sections that follow it.
//
// Writing (MethodBodyStream::addBody):
// - a body is tiny exactly when its code is at most 63 bytes, MaxStack is
//   exactly 8, it has no locals, no clauses and no InitLocals; a tiny header
//   is one byte, (CodeSize << 2) | 2, and is not aligned. It has no maxstack
//   field: a tiny header implies maxstack 8;
// - otherwise the body gets a 12-byte fat header, 4-byte aligned with zero
//   padding before it: the flags word 0x3 | (3 << 12), plus MoreSects when
//   there are clauses and InitLocals only when the caller sets it (never
//   implicitly); MaxStack, kept exactly (a MaxStack below 8 makes a small
//   body fat); CodeSize; the LocalVarSig token;
// - the clauses follow the code, 4-byte aligned, as one EH section: small
//   when there are at most 20 clauses, every offset is at most 0xFFFF and
//   every length at most 0xFF, fat otherwise.
//
// The stream's offsets are relative to its first byte, which the image
// writer places at a 4-byte aligned RVA. Several MethodDefs may share one
// body offset.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_METHODBODY_H
#define LLVM_CLI_METHODBODY_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <cstdint>

namespace llvm {
namespace cli {

/// One exception handling clause (II.25.4.6), with offsets and lengths in
/// bytes of code.
struct ExceptionClause {
  ExceptionClauseKind Kind = ExceptionClauseKind::Catch;
  uint32_t TryOffset = 0, TryLength = 0, HandlerOffset = 0, HandlerLength = 0;
  /// Catch: the class token; Filter: the offset of the filter block; Finally
  /// and Fault: 0.
  uint32_t ClassTokenOrFilterOffset = 0;
};

/// The parts of one method body.
struct MethodBodyInfo {
  ArrayRef<uint8_t> Code;
  uint16_t MaxStack = 8;
  /// A StandAloneSig token, or 0 for no locals.
  uint32_t LocalVarSigToken = 0;
  /// Codegen never sets it (locals are written before they are read); it
  /// exists so that copied bodies keep it.
  bool InitLocals = false;
  /// Innermost first (II.19).
  ArrayRef<ExceptionClause> Clauses;
};

/// The IL stream of one image: method bodies one after the other, with fat
/// bodies 4-byte aligned.
class MethodBodyStream {
public:
  /// Appends \p B with the smallest header and EH section that can describe
  /// it, and returns the offset of the body's header in the stream.
  /// Precondition: \p B has at most 699,050 clauses, (0xFFFFFF - 4) / 24, the
  /// most one fat EH section's 24-bit DataSize holds (asserted; more is API
  /// misuse). decodeMethodBody rejects a body with more, so a decoded body
  /// can always be re-encoded.
  LLVM_ABI uint32_t addBody(const MethodBodyInfo &B);

  ArrayRef<uint8_t> getBytes() const { return Bytes; }
  size_t size() const { return Bytes.size(); }

private:
  SmallVector<uint8_t, 0> Bytes;
};

/// A decoded method body. Code points into the bytes that were decoded.
struct MethodBodyView {
  bool IsTiny = false;
  /// 8 for a tiny body.
  uint16_t MaxStack = 0;
  uint32_t LocalVarSigToken = 0;
  bool InitLocals = false;
  ArrayRef<uint8_t> Code;
  /// The clauses of every EH section, in file order.
  SmallVector<ExceptionClause, 0> Clauses;
  /// The size of the body in bytes: the header, the code, and the EH
  /// sections with the padding between them (not the padding after them).
  uint32_t Size = 0;
};

/// Decodes the method body whose header starts at \p Bytes[0] (the bytes may
/// run past the body). Follows MoreSects chains of EH sections. Gives an
/// Error with the prefix "invalid method body: " for an unknown header
/// format, a fat header whose size is not 3 words, code or a section that
/// runs past the end of \p Bytes, a section that is not an EH table (an
/// OptILTable section included), a section whose data size is not 4 plus a
/// whole number of clauses, a clause of an unknown kind, and EH sections
/// (the MoreSects chain included) that hold more than 699,050 clauses in all
/// (the Error names the count; decoding stops at the section that exceeds
/// the limit).
LLVM_ABI Expected<MethodBodyView> decodeMethodBody(ArrayRef<uint8_t> Bytes);

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_METHODBODY_H
