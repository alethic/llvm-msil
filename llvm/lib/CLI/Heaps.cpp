//===- Heaps.cpp - ECMA-335 metadata heap builders ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The #Strings, #Blob, #US and #GUID heaps of II.24.2.2-II.24.2.5.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/Heaps.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/StringSaver.h"
#include <algorithm>
#include <cassert>

#define DEBUG_TYPE "cli-heaps"

using namespace llvm;
using namespace llvm::cli;

/// The bytes of \p Bytes as a StringMap key.
static StringRef asKey(ArrayRef<uint8_t> Bytes) {
  return StringRef(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
}

/// Copies \p Content to the front of \p Out and zeroes the rest.
static void writeContent(ArrayRef<uint8_t> Content,
                         MutableArrayRef<uint8_t> Out) {
  assert(Out.size() >= Content.size() && "heap output too small");
  std::copy(Content.begin(), Content.end(), Out.begin());
  std::fill(Out.begin() + Content.size(), Out.end(), 0);
}

/// Appends \p V as a compressed unsigned integer (II.23.2).
static void appendCompressedUInt(SmallVectorImpl<uint8_t> &Data, uint32_t V) {
  uint8_t Buf[4];
  unsigned N = encodeCompressedUInt(V, Buf);
  Data.append(Buf, Buf + N);
}

//===----------------------------------------------------------------------===//
// #Strings
//===----------------------------------------------------------------------===//

StringHeapBuilder::StringHeapBuilder() : Builder(StringTableBuilder::ELF) {}

void StringHeapBuilder::add(StringRef S) {
  assert(!Builder.isFinalized() && "add to a finalized #Strings heap");
  if (S.empty() || Builder.contains(S))
    return;
  Builder.add(StringSaver(Alloc).save(S));
}

void StringHeapBuilder::finalize() {
  assert(!Builder.isFinalized() && "#Strings heap finalized twice");
  // Optimized ELF finalization: the strings sorted by their tails (an order
  // that depends only on their content, since they are distinct) with every
  // string that ends the previous one merged into it.
  Builder.finalize();
}

uint32_t StringHeapBuilder::getIndex(StringRef S) const {
  assert(Builder.isFinalized() && "#Strings index before finalize()");
  if (S.empty())
    return 0;
  assert(Builder.contains(S) && "string was not added to the #Strings heap");
  return static_cast<uint32_t>(Builder.getOffset(S));
}

uint32_t StringHeapBuilder::getSize() const {
  assert(Builder.isFinalized() && "#Strings size before finalize()");
  assert(Builder.getSize() <= UINT32_MAX && "#Strings heap too large");
  return static_cast<uint32_t>(Builder.getSize());
}

void StringHeapBuilder::write(MutableArrayRef<uint8_t> Out) const {
  assert(Builder.isFinalized() && "#Strings written before finalize()");
  assert(Out.size() >= getSize() && "#Strings output too small");
  // StringTableBuilder writes only the strings: the leading NUL, the
  // terminators and the padding are the zeroes written first.
  std::fill(Out.begin(), Out.end(), 0);
  Builder.write(Out.data());
}

//===----------------------------------------------------------------------===//
// #Blob
//===----------------------------------------------------------------------===//

BlobHeapBuilder::BlobHeapBuilder() { Data.push_back(0); }

uint32_t BlobHeapBuilder::add(ArrayRef<uint8_t> Bytes) {
  if (Bytes.empty())
    return 0;
  assert(Bytes.size() <= MaxCompressedUInt && "blob too large");
  auto [It, Inserted] = Offsets.try_emplace(asKey(Bytes), 0);
  if (!Inserted)
    return It->second;
  assert(Data.size() + 4 + Bytes.size() <= UINT32_MAX &&
         "#Blob heap too large");
  It->second = static_cast<uint32_t>(Data.size());
  appendCompressedUInt(Data, static_cast<uint32_t>(Bytes.size()));
  Data.append(Bytes.begin(), Bytes.end());
  return It->second;
}

void BlobHeapBuilder::write(MutableArrayRef<uint8_t> Out) const {
  writeContent(Data, Out);
}

//===----------------------------------------------------------------------===//
// #US
//===----------------------------------------------------------------------===//

UserStringHeapBuilder::UserStringHeapBuilder() { Data.push_back(0); }

/// Whether a UTF-16 code unit makes the #US trailing byte 1: a unit of 0x7F
/// or above, or one in 0x01-0x08 or 0x0E-0x1F, or 0x27 or 0x2D (plan 5.7.6 as
/// amended in section 12). This is the rule that Framework ilasm, the CLR's
/// own emitter and Roslyn/SRM write. II.24.2.4's wording ("a non-zero high
/// byte") would give 0 for U+0080-U+00FF, but .NET Framework reads the byte
/// as fIs80Plus and treats a literal whose byte is 0 as ASCII-only, so a
/// Latin-1 literal would then hash differently under OrdinalIgnoreCase from
/// the equal run-time string. A 1 is always safe; a 0 is a promise.
static bool needsSpecialHandling(UTF16 Unit) {
  if (Unit >= 0x7F)
    return true;
  return (Unit >= 0x01 && Unit <= 0x08) || (Unit >= 0x0E && Unit <= 0x1F) ||
         Unit == 0x27 || Unit == 0x2D;
}

static Error makeHeapError(const Twine &Msg) {
  return createStringError(errc::invalid_argument,
                           "cannot write CLI metadata: " + Msg);
}

Expected<uint32_t> UserStringHeapBuilder::add(StringRef Str) {
  auto It = Offsets.find(Str);
  if (It != Offsets.end())
    return It->second;

  // Strict conversion rejects truncated, overlong and surrogate sequences and
  // code points above U+10FFFF. A string never needs more UTF-16 code units
  // than it has UTF-8 bytes.
  SmallVector<UTF16, 0> Units(Str.size());
  const UTF8 *Begin = reinterpret_cast<const UTF8 *>(Str.data());
  const UTF8 *Src = Begin;
  UTF16 *Dst = Units.data();
  if (!Str.empty()) {
    ConversionResult R = ConvertUTF8toUTF16(
        &Src, Begin + Str.size(), &Dst, Dst + Units.size(), strictConversion);
    assert(R != targetExhausted && "UTF-16 never needs more units than UTF-8");
    if (R != conversionOK)
      return makeHeapError("user string is not valid UTF-8 (at byte " +
                           Twine(static_cast<uint64_t>(Src - Begin)) + ")");
  }
  Units.truncate(Dst - Units.data());

  // The entry: a compressed length 2n + 1, the code units and the trailing
  // byte. The heap may not grow past 2^24 - 1 bytes, the reach of an ldstr
  // token's offset.
  uint64_t Length = 2 * uint64_t(Units.size()) + 1;
  uint64_t End =
      Data.size() + Length +
      (Length <= MaxCompressedUInt ? getCompressedUIntSize(Length) : 4);
  if (Length > MaxCompressedUInt || End > MaxUserStringHeapSize)
    return makeHeapError("a user string of " + Twine(Units.size()) +
                         " UTF-16 code units would grow the #US heap to 0x" +
                         Twine::utohexstr(End) + " bytes (limit 0x" +
                         Twine::utohexstr(MaxUserStringHeapSize) + ")");

  uint32_t Offset = static_cast<uint32_t>(Data.size());
  appendCompressedUInt(Data, static_cast<uint32_t>(Length));
  bool Special = false;
  for (UTF16 Unit : Units) {
    Data.push_back(static_cast<uint8_t>(Unit));
    Data.push_back(static_cast<uint8_t>(Unit >> 8));
    Special |= needsSpecialHandling(Unit);
  }
  Data.push_back(Special ? 1 : 0);
  Offsets.try_emplace(Str, Offset);
  return Offset;
}

void UserStringHeapBuilder::write(MutableArrayRef<uint8_t> Out) const {
  writeContent(Data, Out);
}

//===----------------------------------------------------------------------===//
// #GUID
//===----------------------------------------------------------------------===//

uint32_t GuidHeapBuilder::add(const std::array<uint8_t, 16> &G) {
  uint32_t Next = static_cast<uint32_t>(Data.size() / 16 + 1);
  auto [It, Inserted] = Indexes.try_emplace(G, Next);
  if (Inserted)
    Data.append(G.begin(), G.end());
  return It->second;
}

void GuidHeapBuilder::write(MutableArrayRef<uint8_t> Out) const {
  writeContent(Data, Out);
}
