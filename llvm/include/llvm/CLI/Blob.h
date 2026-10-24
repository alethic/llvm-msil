//===- llvm/CLI/Blob.h - ECMA-335 blob writer and reader --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares BlobWriter and BlobReader, the byte-level codecs of the
// ECMA-335 blobs (signatures, custom attributes, marshalling descriptors and
// the other #Blob heap entries): little-endian fixed-width integers, the
// compressed integers of II.23.2 and the SerString of II.23.3.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_BLOB_H
#define LLVM_CLI_BLOB_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace llvm {
namespace cli {

/// Appends little-endian values to a caller-owned vector. Every write appends;
/// nothing already in the vector is changed. Invalid arguments (a compressed
/// integer out of range, a zero alignment) are API misuse and assert.
class LLVM_ABI BlobWriter {
public:
  explicit BlobWriter(SmallVectorImpl<uint8_t> &Out) : Out(Out) {}

  void writeU8(uint8_t V);
  void writeU16(uint16_t V);
  void writeU32(uint32_t V);
  void writeU64(uint64_t V);
  /// II.23.2: 1, 2 or 4 bytes, big-endian; V <= MaxCompressedUInt.
  void writeCompressedUInt(uint32_t V);
  /// II.23.2: the rotated-sign form; -2^28 <= V < 2^28.
  void writeCompressedInt(int32_t V);
  void writeBytes(ArrayRef<uint8_t> B);
  /// II.23.3: the single byte 0xFF for a null string, else the compressed
  /// length and the UTF-8 bytes.
  void writeSerString(std::optional<StringRef> UTF8);
  /// The compressed length and the UTF-8 bytes. This is not the record
  /// codecs' 'str', which has a ULEB128 length (cli::RecordWriter, 5.8.8).
  void writeString(StringRef UTF8);
  /// Appends zero bytes until the vector's size is a multiple of Alignment.
  void align(unsigned Alignment);
  /// The size of the vector (including what it held before this writer).
  size_t size() const { return Out.size(); }

private:
  SmallVectorImpl<uint8_t> &Out;
};

/// Reads values from a blob, with a sticky error like DataExtractor::Cursor:
/// the first read that fails records an error and does not advance, and every
/// read after it returns 0, an empty ArrayRef or StringRef, or std::nullopt
/// (readSerString), without advancing, until takeError() hands the error
/// over. The error message names the cause and the offset ("unexpected end of
/// blob at offset 0x3 (need 4 bytes, 1 left)", "bad compressed integer lead
/// byte 0xE0 at offset 0x0"); the decoders built on BlobReader prefix it.
///
/// Unlike DataExtractor::Cursor, a BlobReader that is destroyed with an
/// error that nobody took does not assert.
class LLVM_ABI BlobReader {
public:
  explicit BlobReader(ArrayRef<uint8_t> Bytes) : Bytes(Bytes) {}

  uint8_t readU8();
  uint16_t readU16();
  uint32_t readU32();
  uint64_t readU64();
  uint32_t readCompressedUInt();
  int32_t readCompressedInt();
  /// The next N bytes (a view into the blob).
  ArrayRef<uint8_t> readBytes(size_t N);
  /// II.23.3: std::nullopt for the null string (the byte 0xFF), else a view of
  /// the UTF-8 bytes. The bytes are not checked for UTF-8 validity.
  std::optional<StringRef> readSerString();
  /// A compressed length and that many bytes; 0xFF is an error here.
  StringRef readString();

  /// True if no bytes are left.
  bool empty() const { return Pos == Bytes.size(); }
  /// The offset of the next byte; after a failure, the offset of the read
  /// that failed.
  size_t tell() const { return Pos; }
  size_t remaining() const { return Bytes.size() - Pos; }
  /// True if a read failed and takeError() has not taken the error yet.
  bool hasError() const { return Failed; }
  /// The first failure, or success; it clears the error state, after which
  /// reads continue from tell().
  Error takeError();

private:
  /// Records a failure at the current offset unless one is recorded already.
  void fail(const Twine &Msg);
  /// Checks that N more bytes exist; records a failure if not.
  bool check(size_t N);
  /// The compressed integer at Pos and its size, or a recorded failure.
  bool readCompressed(uint32_t &Value, unsigned &Size);
  /// A compressed length and that many bytes, or a recorded failure.
  bool readCountedString(StringRef &S);

  ArrayRef<uint8_t> Bytes;
  size_t Pos = 0;
  bool Failed = false;
  std::string Message;
};

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_BLOB_H
