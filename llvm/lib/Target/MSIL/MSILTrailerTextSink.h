//===-- MSILTrailerTextSink.h - The text sink of the trailer ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares MSILTrailerTextSink, the sink through which the MSIL
/// AsmPrinter prints the module trailer that cli::buildTrailer builds for
/// whole-program text (plan 5.9.10). Its members, and the AsmPrinter's
/// whole-program fills of cli::TrailerInput (MSILAsmPrinter::fillTrailer*),
/// are defined in MSILTrailerTextSink.cpp (plan 5.13.1 L9).
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILTRAILERTEXTSINK_H
#define LLVM_LIB_TARGET_MSIL_MSILTRAILERTEXTSINK_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CLI/Signature.h"
#include "llvm/CLI/Trailer.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/StringSaver.h"
#include <cstdint>
#include <string>
#include <vector>

namespace llvm {
class MSILAsmPrinter;
class MSILMemberRefExpr;

/// The text sink of cli::buildTrailer (plan 5.9.10). The trailer's entities
/// are the MSILMemberRefExprs that code uses, so its instructions are CIL_
/// MCInsts like those of a compiled function (MSILInstPrinter prints them,
/// and plan 5.1's text keeps one owner); the data-import helpers and their
/// string literals, which no MSILMemberRefExpr can spell (their signatures
/// take strings), go through MSILTargetStreamer's helper calls instead. The
/// sink records the RVA data and the methods, and the AsmPrinter emits them
/// at their places in the trailer (emitDataSegment, emitMethods).
class MSILTrailerTextSink final : public cli::TrailerSink {
  MSILAsmPrinter &AP;
  BumpPtrAllocator Alloc;
  /// Owns the names that MSILMemberRefExprs refer to.
  StringSaver Saver{Alloc};

  struct Entity {
    enum Kind : uint8_t { Member, Helper, String };
    Kind K = Member;
    /// Member: the operand of the instructions that name it.
    const MSILMemberRefExpr *Ref = nullptr;
    /// Helper: its name and signature; String: the literal.
    std::string Name;
    cli::MethodSig Sig;
  };
  /// EntityRef I is Entities[I - 1].
  std::vector<Entity> Entities;
  cli::EntityRef DataField = 0;
  struct RVAData {
    std::string FieldName;
    std::vector<uint8_t> Bytes;
  };
  std::vector<RVAData> RVA;
  /// The names of the fields that addTrailerField made, in order.
  std::vector<StringRef> TrailerFields;
  std::vector<cli::TrailerMethod> Methods;

  cli::EntityRef addEntity(Entity E);
  void printMethod(const cli::TrailerMethod &M);

public:
  explicit MSILTrailerTextSink(MSILAsmPrinter &AP) : AP(AP) {}

  /// A member that MCInsts name.
  cli::EntityRef add(const MSILMemberRefExpr *Ref);

  cli::EntityRef getDataField() override;
  cli::EntityRef addRVAData(StringRef TypeName, StringRef FieldName,
                            ArrayRef<uint8_t> Bytes) override;
  cli::EntityRef getBCLMember(cli::BCLMemberId Id) override;
  cli::EntityRef declareHelper(StringRef Name,
                               const cli::MethodSig &Sig) override;
  cli::EntityRef addUserString(StringRef UTF8) override;
  cli::EntityRef addTrailerField(StringRef Name) override;
  void emitMethod(const cli::TrailerMethod &M) override;

  /// The fields that addTrailerField made ('<msil>argc' and '<msil>argv').
  ArrayRef<StringRef> getTrailerFields() const;

  /// The bytes of the RVA field \p FieldName; empty if there is none.
  ArrayRef<uint8_t> getRVAData(StringRef FieldName) const;

  /// Emits the methods in the order cli::buildTrailer made them: the
  /// data-import helpers, <Module>::.cctor, '<msil>Main' and the trailer
  /// definitions (cli::TrailerInput::Defs).
  void emitMethods();
};
} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MSILTRAILERTEXTSINK_H
