//===- MethodBody.cpp - ECMA-335 method bodies ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The method header and EH section formats of II.25.4: the writer
// (MethodBodyStream) and the reader (decodeMethodBody).
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/MethodBody.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <iterator>
#include <optional>

using namespace llvm;
using namespace llvm::cli;
using namespace llvm::support;

/// The size of a fat method header in 4-byte words, as its flags word
/// stores it (bits 12-15).
static constexpr unsigned FatHeaderWords = FatHeaderSize / 4;
/// The most clauses of a small EH section (its DataSize, 12n + 4, is a byte).
static constexpr size_t MaxSmallClauses = 20;
static constexpr uint32_t SmallClauseSize = 12, FatClauseSize = 24;
/// The size of an EH section header (the Kind byte and DataSize).
static constexpr uint32_t SectionHeaderSize = 4;
/// The largest DataSize of a fat EH section (a 24-bit field).
static constexpr uint32_t MaxFatSectionDataSize = 0xFFFFFF;
/// The most clauses of one body: addBody writes every clause into one EH
/// section, and a fat section holds at most this many, so decodeMethodBody
/// rejects more (whatever it accepts, addBody can re-encode).
static constexpr size_t MaxClauses =
    (MaxFatSectionDataSize - SectionHeaderSize) / FatClauseSize;
static_assert(MaxClauses == 699050, "MethodBody.h documents 699,050 clauses");

static void appendU8(SmallVectorImpl<uint8_t> &Out, uint8_t V) {
  Out.push_back(V);
}

static void appendU16(SmallVectorImpl<uint8_t> &Out, uint16_t V) {
  uint8_t B[2];
  endian::write16le(B, V);
  Out.append(std::begin(B), std::end(B));
}

static void appendU32(SmallVectorImpl<uint8_t> &Out, uint32_t V) {
  uint8_t B[4];
  endian::write32le(B, V);
  Out.append(std::begin(B), std::end(B));
}

static void padTo4(SmallVectorImpl<uint8_t> &Out) {
  Out.resize(alignTo(Out.size(), 4), 0);
}

static bool isSmallSectionPossible(ArrayRef<ExceptionClause> Clauses) {
  if (Clauses.size() > MaxSmallClauses)
    return false;
  for (const ExceptionClause &C : Clauses)
    if (C.TryOffset > 0xFFFF || C.HandlerOffset > 0xFFFF ||
        C.TryLength > 0xFF || C.HandlerLength > 0xFF)
      return false;
  return true;
}

uint32_t MethodBodyStream::addBody(const MethodBodyInfo &B) {
  assert(B.Code.size() <= UINT32_MAX && "code too large for a method body");
  // A tiny header has no maxstack field and implies 8, so any other MaxStack
  // (a smaller one included) needs a fat header to be kept exactly.
  bool Tiny = B.Code.size() <= TinyMaxCodeSize && B.MaxStack == TinyMaxStack &&
              B.LocalVarSigToken == 0 && B.Clauses.empty() && !B.InitLocals;
  if (Tiny) {
    assert(Bytes.size() <= UINT32_MAX && "IL stream too large");
    uint32_t Offset = Bytes.size();
    appendU8(Bytes,
             static_cast<uint8_t>(B.Code.size() << 2) | MethodHeaderTinyFormat);
    Bytes.append(B.Code.begin(), B.Code.end());
    return Offset;
  }

  padTo4(Bytes);
  assert(Bytes.size() <= UINT32_MAX && "IL stream too large");
  uint32_t Offset = Bytes.size();
  uint16_t Flags = MethodHeaderFatFormat | FatHeaderWords << 12;
  if (!B.Clauses.empty())
    Flags |= MethodHeaderMoreSects;
  if (B.InitLocals)
    Flags |= MethodHeaderInitLocals;
  appendU16(Bytes, Flags);
  appendU16(Bytes, B.MaxStack);
  appendU32(Bytes, B.Code.size());
  appendU32(Bytes, B.LocalVarSigToken);
  Bytes.append(B.Code.begin(), B.Code.end());
  if (B.Clauses.empty())
    return Offset;

  padTo4(Bytes);
  size_t N = B.Clauses.size();
  if (isSmallSectionPossible(B.Clauses)) {
    appendU8(Bytes, EHSectionEHTable);
    appendU8(Bytes,
             static_cast<uint8_t>(SmallClauseSize * N + SectionHeaderSize));
    appendU16(Bytes, 0);
    for (const ExceptionClause &C : B.Clauses) {
      appendU16(Bytes, static_cast<uint16_t>(C.Kind));
      appendU16(Bytes, C.TryOffset);
      appendU8(Bytes, C.TryLength);
      appendU16(Bytes, C.HandlerOffset);
      appendU8(Bytes, C.HandlerLength);
      appendU32(Bytes, C.ClassTokenOrFilterOffset);
    }
    return Offset;
  }

  assert(N <= MaxClauses && "too many clauses for a fat EH section");
  uint64_t DataSize = uint64_t(FatClauseSize) * N + SectionHeaderSize;
  appendU32(Bytes, (EHSectionEHTable | EHSectionFatFormat) |
                       static_cast<uint32_t>(DataSize) << 8);
  for (const ExceptionClause &C : B.Clauses) {
    appendU32(Bytes, static_cast<uint32_t>(C.Kind));
    appendU32(Bytes, C.TryOffset);
    appendU32(Bytes, C.TryLength);
    appendU32(Bytes, C.HandlerOffset);
    appendU32(Bytes, C.HandlerLength);
    appendU32(Bytes, C.ClassTokenOrFilterOffset);
  }
  return Offset;
}

static Error makeBodyError(const Twine &Msg) {
  return createStringError(make_error_code(errc::illegal_byte_sequence),
                           "invalid method body: " + Msg);
}

static std::string hex(uint64_t V) { return "0x" + utohexstr(V); }

static std::optional<ExceptionClauseKind> getClauseKind(uint32_t Flags) {
  switch (Flags) {
  case static_cast<uint32_t>(ExceptionClauseKind::Catch):
  case static_cast<uint32_t>(ExceptionClauseKind::Filter):
  case static_cast<uint32_t>(ExceptionClauseKind::Finally):
  case static_cast<uint32_t>(ExceptionClauseKind::Fault):
    return static_cast<ExceptionClauseKind>(Flags);
  default:
    return std::nullopt;
  }
}

/// Decodes the EH sections that start at the first 4-byte boundary at or
/// after \p Pos, appending their clauses to \p View, and returns the offset
/// after the last one.
static Expected<uint64_t> decodeSections(ArrayRef<uint8_t> Bytes, uint64_t Pos,
                                         MethodBodyView &View) {
  bool More = true;
  while (More) {
    Pos = alignTo(Pos, 4);
    if (Pos + SectionHeaderSize > Bytes.size())
      return makeBodyError("truncated method data section at offset " +
                           hex(Pos));
    const uint8_t *Header = Bytes.data() + Pos;
    uint8_t Kind = Header[0];
    if (Kind & EHSectionOptILTable)
      return makeBodyError("unsupported OptILTable method data section at "
                           "offset " +
                           hex(Pos));
    if (!(Kind & EHSectionEHTable))
      return makeBodyError("unknown method data section kind " + hex(Kind) +
                           " at offset " + hex(Pos));
    bool Fat = Kind & EHSectionFatFormat;
    uint32_t DataSize =
        Fat ? Header[1] | uint32_t(Header[2]) << 8 | uint32_t(Header[3]) << 16
            : Header[1];
    uint32_t ClauseSize = Fat ? FatClauseSize : SmallClauseSize;
    if (DataSize < SectionHeaderSize ||
        (DataSize - SectionHeaderSize) % ClauseSize != 0)
      return makeBodyError(Twine(Fat ? "fat" : "small") +
                           " EH section at offset " + hex(Pos) +
                           " has data size " + Twine(DataSize) +
                           ", which is not 4 + " + Twine(ClauseSize) + "n");
    if (Pos + DataSize > Bytes.size())
      return makeBodyError("EH section at offset " + hex(Pos) + " of " +
                           Twine(DataSize) + " bytes runs past the end (" +
                           Twine(Bytes.size()) + " bytes)");
    uint32_t N = (DataSize - SectionHeaderSize) / ClauseSize;
    uint64_t Total = uint64_t(View.Clauses.size()) + N;
    if (Total > MaxClauses)
      return makeBodyError("EH section at offset " + hex(Pos) +
                           " brings the clause count to " + Twine(Total) +
                           ", more than the " + Twine(MaxClauses) +
                           " that one fat EH section holds");
    const uint8_t *P = Header + SectionHeaderSize;
    for (uint32_t I = 0; I != N; ++I, P += ClauseSize) {
      ExceptionClause C;
      uint32_t Flags;
      if (Fat) {
        Flags = endian::read32le(P);
        C.TryOffset = endian::read32le(P + 4);
        C.TryLength = endian::read32le(P + 8);
        C.HandlerOffset = endian::read32le(P + 12);
        C.HandlerLength = endian::read32le(P + 16);
        C.ClassTokenOrFilterOffset = endian::read32le(P + 20);
      } else {
        Flags = endian::read16le(P);
        C.TryOffset = endian::read16le(P + 2);
        C.TryLength = P[4];
        C.HandlerOffset = endian::read16le(P + 5);
        C.HandlerLength = P[7];
        C.ClassTokenOrFilterOffset = endian::read32le(P + 8);
      }
      std::optional<ExceptionClauseKind> K = getClauseKind(Flags);
      if (!K)
        return makeBodyError("clause " + Twine(View.Clauses.size()) +
                             " has an unknown kind " + hex(Flags));
      C.Kind = *K;
      View.Clauses.push_back(C);
    }
    Pos += DataSize;
    More = Kind & EHSectionMoreSects;
  }
  return Pos;
}

Expected<MethodBodyView> cli::decodeMethodBody(ArrayRef<uint8_t> Bytes) {
  if (Bytes.empty())
    return makeBodyError("no header");
  MethodBodyView View;
  uint8_t First = Bytes[0];
  switch (First & 0x3) {
  case MethodHeaderTinyFormat: {
    uint32_t CodeSize = First >> 2;
    if (1 + uint64_t(CodeSize) > Bytes.size())
      return makeBodyError("tiny body with " + Twine(CodeSize) +
                           " bytes of code runs past the end (" +
                           Twine(Bytes.size()) + " bytes)");
    View.IsTiny = true;
    View.MaxStack = TinyMaxStack;
    View.Code = Bytes.slice(1, CodeSize);
    View.Size = 1 + CodeSize;
    return std::move(View);
  }
  case MethodHeaderFatFormat: {
    if (Bytes.size() < FatHeaderSize)
      return makeBodyError("truncated fat header (" + Twine(Bytes.size()) +
                           " bytes)");
    uint16_t FlagsAndSize = endian::read16le(Bytes.data());
    unsigned HeaderWords = FlagsAndSize >> 12;
    if (HeaderWords != FatHeaderWords)
      return makeBodyError("fat header size of " + Twine(HeaderWords) +
                           " words, expected " + Twine(FatHeaderWords));
    View.MaxStack = endian::read16le(Bytes.data() + 2);
    uint32_t CodeSize = endian::read32le(Bytes.data() + 4);
    View.LocalVarSigToken = endian::read32le(Bytes.data() + 8);
    View.InitLocals = FlagsAndSize & MethodHeaderInitLocals;
    uint64_t End = uint64_t(FatHeaderSize) + CodeSize;
    if (End > Bytes.size())
      return makeBodyError("fat body with " + Twine(CodeSize) +
                           " bytes of code runs past the end (" +
                           Twine(Bytes.size()) + " bytes)");
    View.Code = Bytes.slice(FatHeaderSize, CodeSize);
    if (FlagsAndSize & MethodHeaderMoreSects) {
      Expected<uint64_t> SectionsEnd = decodeSections(Bytes, End, View);
      if (!SectionsEnd)
        return SectionsEnd.takeError();
      End = *SectionsEnd;
    }
    if (End > UINT32_MAX)
      return makeBodyError("body larger than 4 GiB");
    View.Size = End;
    return std::move(View);
  }
  default:
    return makeBodyError("unknown header format " + hex(First & 0x3) +
                         " (first byte " + hex(First) + ")");
  }
}
