//===- Blob.cpp - ECMA-335 blob writer and reader -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/Blob.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>

using namespace llvm;
using namespace llvm::cli;

#define DEBUG_TYPE "cli-blob"

//===----------------------------------------------------------------------===//
// BlobWriter
//===----------------------------------------------------------------------===//

void BlobWriter::writeU8(uint8_t V) { Out.push_back(V); }

void BlobWriter::writeU16(uint16_t V) {
  uint8_t Buf[2];
  support::endian::write16le(Buf, V);
  Out.append(std::begin(Buf), std::end(Buf));
}

void BlobWriter::writeU32(uint32_t V) {
  uint8_t Buf[4];
  support::endian::write32le(Buf, V);
  Out.append(std::begin(Buf), std::end(Buf));
}

void BlobWriter::writeU64(uint64_t V) {
  uint8_t Buf[8];
  support::endian::write64le(Buf, V);
  Out.append(std::begin(Buf), std::end(Buf));
}

void BlobWriter::writeCompressedUInt(uint32_t V) {
  uint8_t Buf[4];
  unsigned N = encodeCompressedUInt(V, Buf);
  Out.append(Buf, Buf + N);
}

void BlobWriter::writeCompressedInt(int32_t V) {
  uint8_t Buf[4];
  unsigned N = encodeCompressedInt(V, Buf);
  Out.append(Buf, Buf + N);
}

void BlobWriter::writeBytes(ArrayRef<uint8_t> B) {
  Out.append(B.begin(), B.end());
}

void BlobWriter::writeSerString(std::optional<StringRef> UTF8) {
  if (!UTF8) {
    writeU8(0xFF);
    return;
  }
  writeString(*UTF8);
}

void BlobWriter::writeString(StringRef UTF8) {
  assert(UTF8.size() <= MaxCompressedUInt && "string too long for a blob");
  writeCompressedUInt(static_cast<uint32_t>(UTF8.size()));
  writeBytes(arrayRefFromStringRef(UTF8));
}

void BlobWriter::align(unsigned Alignment) {
  assert(Alignment != 0 && "zero alignment");
  Out.resize(alignTo(Out.size(), Alignment), 0);
}

//===----------------------------------------------------------------------===//
// BlobReader
//===----------------------------------------------------------------------===//

void BlobReader::fail(const Twine &Msg) {
  if (Failed)
    return;
  Failed = true;
  Message = Msg.str();
}

bool BlobReader::check(size_t N) {
  if (Failed)
    return false;
  if (N <= remaining())
    return true;
  fail("unexpected end of blob at offset 0x" + utohexstr(Pos) + " (need " +
       Twine(N) + (N == 1 ? " byte, " : " bytes, ") + Twine(remaining()) +
       " left)");
  return false;
}

uint8_t BlobReader::readU8() {
  if (!check(1))
    return 0;
  return Bytes[Pos++];
}

uint16_t BlobReader::readU16() {
  if (!check(2))
    return 0;
  uint16_t V = support::endian::read16le(Bytes.data() + Pos);
  Pos += 2;
  return V;
}

uint32_t BlobReader::readU32() {
  if (!check(4))
    return 0;
  uint32_t V = support::endian::read32le(Bytes.data() + Pos);
  Pos += 4;
  return V;
}

uint64_t BlobReader::readU64() {
  if (!check(8))
    return 0;
  uint64_t V = support::endian::read64le(Bytes.data() + Pos);
  Pos += 8;
  return V;
}

bool BlobReader::readCompressed(uint32_t &Value, unsigned &Size) {
  if (!check(1))
    return false;
  uint8_t Lead = Bytes[Pos];
  if ((Lead & 0xE0) == 0xE0) {
    fail("bad compressed integer lead byte 0x" + utohexstr(Lead) +
         " at offset 0x" + utohexstr(Pos));
    return false;
  }
  Size = (Lead & 0x80) == 0 ? 1 : (Lead & 0xC0) == 0x80 ? 2 : 4;
  if (!check(Size))
    return false;
  ArrayRef<uint8_t> Rest = Bytes.slice(Pos, Size);
  std::optional<uint32_t> V = decodeCompressedUInt(Rest);
  assert(V && Rest.empty() && "lead byte and size checked above");
  Value = *V;
  Pos += Size;
  return true;
}

uint32_t BlobReader::readCompressedUInt() {
  uint32_t V;
  unsigned Size;
  if (!readCompressed(V, Size))
    return 0;
  return V;
}

int32_t BlobReader::readCompressedInt() {
  size_t Start = Pos;
  uint32_t V;
  unsigned Size;
  if (!readCompressed(V, Size))
    return 0;
  // Decode again with the signed rule of II.23.2 (the unsigned read above
  // checked the lead byte and the length).
  ArrayRef<uint8_t> Rest = Bytes.slice(Start, Size);
  std::optional<int32_t> S = decodeCompressedInt(Rest);
  assert(S && Rest.empty() && "checked by the unsigned read");
  return *S;
}

ArrayRef<uint8_t> BlobReader::readBytes(size_t N) {
  if (!check(N))
    return {};
  ArrayRef<uint8_t> R = Bytes.slice(Pos, N);
  Pos += N;
  return R;
}

bool BlobReader::readCountedString(StringRef &S) {
  size_t Start = Pos;
  uint32_t Len;
  unsigned Size;
  if (!readCompressed(Len, Size))
    return false;
  if (Len > remaining()) {
    // Report the string, and leave the reader at its start.
    size_t Left = remaining();
    Pos = Start;
    fail("unexpected end of blob at offset 0x" + utohexstr(Start) +
         " (string of " + Twine(Len) + " bytes, " + Twine(Left) + " left)");
    return false;
  }
  S = toStringRef(Bytes.slice(Pos, Len));
  Pos += Len;
  return true;
}

std::optional<StringRef> BlobReader::readSerString() {
  if (!check(1))
    return std::nullopt;
  if (Bytes[Pos] == 0xFF) {
    ++Pos;
    return std::nullopt;
  }
  StringRef S;
  if (!readCountedString(S))
    return std::nullopt;
  return S;
}

StringRef BlobReader::readString() {
  StringRef S;
  if (!readCountedString(S))
    return {};
  return S;
}

Error BlobReader::takeError() {
  if (!Failed)
    return Error::success();
  Failed = false;
  return createStringError(make_error_code(errc::illegal_byte_sequence),
                           Message);
}
