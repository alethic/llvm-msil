//===- llvm/CLI/Heaps.h - ECMA-335 metadata heap builders -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the builders of the four metadata heaps (II.24.2.2 -
// II.24.2.5) that MetadataBuilder writes:
// - #Strings: NUL-terminated UTF-8 names, with offset 0 the empty string. It
//   is a StringTableBuilder(ELF) with tail merging, so a name that ends
//   another one shares its bytes, and the layout depends only on the set of
//   names (StringTableBuilder's finalize order), never on the order of add()
//   calls;
// - #Blob: offset 0 is the empty blob; every other entry is a compressed
//   length and the bytes;
// - #US: offset 0 is a 0x00 byte; every entry is a compressed length 2n + 1,
//   the n UTF-16LE code units of the string and the trailing byte of
//   II.24.2.4, which is 1 if and only if some code unit is 0x7F or above, in
//   0x01-0x08 or 0x0E-0x1F, or 0x27 or 0x2D (the rule Framework ilasm, the
//   CLR's emitter and Roslyn/SRM write; II.24.2.4's "non-zero high byte"
//   would give 0 for U+0080-U+00FF, and .NET Framework reads the byte as
//   fIs80Plus, so a 0 makes it treat a Latin-1 literal as ASCII-only);
// - #GUID: 16-byte entries, numbered from 1.
//
// #Blob, #US and #GUID deduplicate identical entries and keep insertion order,
// so their offsets are final when add() returns.
//
// getSize() is the size of the heap's content, without the padding to 4
// bytes that the metadata root adds to every stream; write() writes the
// content and zeroes the rest of its output.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_HEAPS_H
#define LLVM_CLI_HEAPS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstdint>
#include <map>

namespace llvm {
namespace cli {

/// The #Strings heap (II.24.2.3).
class StringHeapBuilder {
public:
  LLVM_ABI StringHeapBuilder();

  /// Adds \p S, which is copied. The empty string is never stored: its index
  /// is 0. Not allowed after finalize().
  LLVM_ABI void add(StringRef S);
  /// Lays out the heap: sorts the strings by their tails and merges every
  /// string that ends another one into it. Nothing may be added afterwards.
  LLVM_ABI void finalize();
  bool isFinalized() const { return Builder.isFinalized(); }

  /// The offset of \p S, which must have been added (or be empty); after
  /// finalize().
  LLVM_ABI uint32_t getIndex(StringRef S) const;
  /// The size of the heap in bytes, the leading NUL included; after
  /// finalize().
  LLVM_ABI uint32_t getSize() const;
  /// Writes the heap to the first getSize() bytes of \p Out and zeroes the
  /// rest; after finalize().
  LLVM_ABI void write(MutableArrayRef<uint8_t> Out) const;

private:
  /// The copies of the strings that Builder refers to.
  BumpPtrAllocator Alloc;
  StringTableBuilder Builder;
};

/// The #Blob heap (II.24.2.4).
class BlobHeapBuilder {
public:
  LLVM_ABI BlobHeapBuilder();

  /// Adds \p Bytes (copied) and returns its offset: 0 for an empty blob, the
  /// offset of an identical earlier blob, or the end of the heap.
  LLVM_ABI uint32_t add(ArrayRef<uint8_t> Bytes);
  uint32_t getSize() const { return static_cast<uint32_t>(Data.size()); }
  /// Writes the heap to the first getSize() bytes of \p Out and zeroes the
  /// rest.
  LLVM_ABI void write(MutableArrayRef<uint8_t> Out) const;

private:
  SmallVector<uint8_t, 0> Data;
  StringMap<uint32_t> Offsets;
};

/// The #US heap (II.24.2.4): the strings of ldstr instructions.
class UserStringHeapBuilder {
public:
  LLVM_ABI UserStringHeapBuilder();

  /// Adds the UTF-8 string \p UTF8 as UTF-16 and returns its offset (the
  /// low 24 bits of the ldstr token): the offset of an identical earlier
  /// string, or the end of the heap. The empty string gets an entry of its
  /// own (01 00). Gives an Error with the prefix "cannot write CLI metadata: "
  /// if \p UTF8 is not valid UTF-8, or if the heap would grow past
  /// MaxUserStringHeapSize bytes; the heap is unchanged then.
  LLVM_ABI Expected<uint32_t> add(StringRef UTF8);
  uint32_t getSize() const { return static_cast<uint32_t>(Data.size()); }
  /// Writes the heap to the first getSize() bytes of \p Out and zeroes the
  /// rest.
  LLVM_ABI void write(MutableArrayRef<uint8_t> Out) const;

private:
  SmallVector<uint8_t, 0> Data;
  StringMap<uint32_t> Offsets;
};

/// The #GUID heap (II.24.2.5).
class GuidHeapBuilder {
public:
  /// Adds \p G and returns its 1-based index: the index of an identical
  /// earlier GUID, or the next one.
  LLVM_ABI uint32_t add(const std::array<uint8_t, 16> &G);
  uint32_t getSize() const { return static_cast<uint32_t>(Data.size()); }
  /// Writes the heap to the first getSize() bytes of \p Out and zeroes the
  /// rest.
  LLVM_ABI void write(MutableArrayRef<uint8_t> Out) const;

private:
  SmallVector<uint8_t, 0> Data;
  std::map<std::array<uint8_t, 16>, uint32_t> Indexes;
};

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_HEAPS_H
