//===- BlobTest.cpp - ECMA-335 blob writer and reader ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/Blob.h (plan 5.7.3): the little-endian fixed-width writers,
// compressed integers (ECMA-335 II.23.2, with the standard's examples and the
// 1/2/4-byte boundaries), SerStrings (II.23.3: null, empty, UTF-8), the
// compressed-length strings, alignment, and BlobReader's sticky error, which
// behaves like DataExtractor::Cursor. Expected bytes come from ECMA-335 and
// from blobs that csc wrote and System.Reflection.Metadata read (the 'été'
// SerString), never from the implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/Blob.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

using ByteVec = std::vector<uint8_t>;

ByteVec toVec(ArrayRef<uint8_t> A) { return ByteVec(A.begin(), A.end()); }

// Runs Fn on a writer over an empty vector and returns the bytes written.
template <typename Fn> ByteVec writeBlob(Fn F) {
  SmallVector<uint8_t, 32> Out;
  BlobWriter W(Out);
  F(W);
  EXPECT_EQ(W.size(), Out.size());
  return toVec(Out);
}

ByteVec concat(ByteVec A, StringRef B) {
  A.insert(A.end(), B.bytes_begin(), B.bytes_end());
  return A;
}

//===----------------------------------------------------------------------===//
// BlobWriter
//===----------------------------------------------------------------------===//

TEST(CLIBlobTest, FixedWidthsAreLittleEndian) {
  EXPECT_EQ(writeBlob([](BlobWriter &W) { W.writeU8(0xAB); }), ByteVec{0xAB});
  EXPECT_EQ(writeBlob([](BlobWriter &W) { W.writeU16(0x1234); }),
            (ByteVec{0x34, 0x12}));
  EXPECT_EQ(writeBlob([](BlobWriter &W) { W.writeU32(0x12345678); }),
            (ByteVec{0x78, 0x56, 0x34, 0x12}));
  EXPECT_EQ(writeBlob([](BlobWriter &W) { W.writeU64(0x0102030405060708); }),
            (ByteVec{0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01}));
  EXPECT_EQ(writeBlob([](BlobWriter &W) {
              W.writeU8(0xFF);
              W.writeU16(0xFFFE);
              W.writeU32(0xDEADBEEF);
              W.writeU64(UINT64_MAX);
            }),
            (ByteVec{0xFF, 0xFE, 0xFF, 0xEF, 0xBE, 0xAD, 0xDE, 0xFF, 0xFF, 0xFF,
                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
}

// The unsigned examples of II.23.2 and the 1/2/4-byte boundaries.
TEST(CLIBlobTest, CompressedUInt) {
  struct {
    uint32_t Value;
    ByteVec Bytes;
  } Cases[] = {
      {0x00, {0x00}},
      {0x03, {0x03}},
      {0x7F, {0x7F}},
      {0x80, {0x80, 0x80}},
      {0x2E57, {0xAE, 0x57}},
      {0x3FFF, {0xBF, 0xFF}},
      {0x4000, {0xC0, 0x00, 0x40, 0x00}},
      {0x123456, {0xC0, 0x12, 0x34, 0x56}},
      {0x1FFFFFFF, {0xDF, 0xFF, 0xFF, 0xFF}},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE("value 0x" + utohexstr(C.Value));
    EXPECT_EQ(writeBlob([&](BlobWriter &W) { W.writeCompressedUInt(C.Value); }),
              C.Bytes);
    BlobReader R(C.Bytes);
    EXPECT_EQ(R.readCompressedUInt(), C.Value);
    EXPECT_TRUE(R.empty());
    EXPECT_THAT_ERROR(R.takeError(), Succeeded());
  }
}

// The signed examples of II.23.2 and the 1/2/4-byte boundaries: a value takes
// one byte in [-2^6, 2^6), two in [-2^13, 2^13) and four in [-2^28, 2^28).
TEST(CLIBlobTest, CompressedInt) {
  struct {
    int32_t Value;
    ByteVec Bytes;
  } Cases[] = {
      {3, {0x06}},
      {-3, {0x7B}},
      {64, {0x80, 0x80}},
      {-64, {0x01}},
      {8192, {0xC0, 0x00, 0x40, 0x00}},
      {-8192, {0x80, 0x01}},
      {268435455, {0xDF, 0xFF, 0xFF, 0xFE}},
      {-268435456, {0xC0, 0x00, 0x00, 0x01}},
      {0, {0x00}},
      {-1, {0x7F}},
      {63, {0x7E}},
      {-65, {0xBF, 0x7F}},
      {8191, {0xBF, 0xFE}},
      {-8193, {0xDF, 0xFF, 0xBF, 0xFF}},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE("value " + std::to_string(C.Value));
    EXPECT_EQ(writeBlob([&](BlobWriter &W) { W.writeCompressedInt(C.Value); }),
              C.Bytes);
    BlobReader R(C.Bytes);
    EXPECT_EQ(R.readCompressedInt(), C.Value);
    EXPECT_TRUE(R.empty());
    EXPECT_THAT_ERROR(R.takeError(), Succeeded());
  }
}

// II.23.3: a SerString is 0xFF for null, else a compressed byte count and the
// UTF-8 bytes; the empty string is a single 0x00.
TEST(CLIBlobTest, SerString) {
  EXPECT_EQ(writeBlob([](BlobWriter &W) { W.writeSerString(std::nullopt); }),
            ByteVec{0xFF});
  EXPECT_EQ(writeBlob([](BlobWriter &W) { W.writeSerString(StringRef("")); }),
            ByteVec{0x00});
  EXPECT_EQ(
      writeBlob([](BlobWriter &W) { W.writeSerString(StringRef("abc")); }),
      (ByteVec{0x03, 'a', 'b', 'c'}));
  // csc: [T("été")] writes 05 C3 A9 74 C3 A9.
  EXPECT_EQ(writeBlob([](BlobWriter &W) {
              W.writeSerString(StringRef("\xC3\xA9t\xC3\xA9"));
            }),
            (ByteVec{0x05, 0xC3, 0xA9, 0x74, 0xC3, 0xA9}));
  std::string S127(127, 'x'), S128(128, 'y');
  EXPECT_EQ(
      writeBlob([&](BlobWriter &W) { W.writeSerString(StringRef(S127)); }),
      concat({0x7F}, S127));
  EXPECT_EQ(
      writeBlob([&](BlobWriter &W) { W.writeSerString(StringRef(S128)); }),
      concat({0x80, 0x80}, S128));
}

TEST(CLIBlobTest, CompressedLengthString) {
  EXPECT_EQ(writeBlob([](BlobWriter &W) { W.writeString(""); }), ByteVec{0x00});
  EXPECT_EQ(writeBlob([](BlobWriter &W) { W.writeString("msil"); }),
            (ByteVec{0x04, 'm', 's', 'i', 'l'}));
  std::string S200(200, 'z');
  EXPECT_EQ(writeBlob([&](BlobWriter &W) { W.writeString(S200); }),
            concat({0x80, 0xC8}, S200));
}

TEST(CLIBlobTest, BytesAndAlignment) {
  EXPECT_EQ(writeBlob([](BlobWriter &W) {
              W.writeBytes({});
              W.writeBytes({1, 2, 3});
              W.align(4); // one zero byte
              W.align(4); // already aligned: nothing
              W.writeU8(9);
              W.align(8); // three zero bytes
              W.align(1);
              W.writeBytes({7});
              W.align(2);
            }),
            (ByteVec{1, 2, 3, 0, 9, 0, 0, 0, 7, 0}));
}

TEST(CLIBlobTest, WriterAppendsToTheCallersVector) {
  SmallVector<uint8_t, 8> Out = {0xEE, 0xDD};
  BlobWriter W(Out);
  W.writeU16(0x0102);
  W.writeCompressedUInt(0x80);
  EXPECT_EQ(toVec(Out), (ByteVec{0xEE, 0xDD, 0x02, 0x01, 0x80, 0x80}));
}

//===----------------------------------------------------------------------===//
// BlobReader
//===----------------------------------------------------------------------===//

TEST(CLIBlobTest, ReaderFixedWidths) {
  const ByteVec Bytes = {0xAB, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x08,
                         0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
  BlobReader R(Bytes);
  EXPECT_FALSE(R.empty());
  EXPECT_EQ(R.tell(), 0u);
  EXPECT_EQ(R.remaining(), Bytes.size());
  EXPECT_EQ(R.readU8(), 0xABu);
  EXPECT_EQ(R.readU16(), 0x1234u);
  EXPECT_EQ(R.tell(), 3u);
  EXPECT_EQ(R.readU32(), 0x12345678u);
  EXPECT_EQ(R.remaining(), 8u);
  EXPECT_EQ(R.readU64(), 0x0102030405060708u);
  EXPECT_TRUE(R.empty());
  EXPECT_EQ(R.tell(), Bytes.size());
  EXPECT_EQ(R.remaining(), 0u);
  EXPECT_THAT_ERROR(R.takeError(), Succeeded());
}

TEST(CLIBlobTest, ReaderStringsAndBytes) {
  const ByteVec Bytes = {0xFF, 0x00, 0x03, 'a',  'b', 'c', 0x05, 0xC3, 0xA9,
                         0x74, 0xC3, 0xA9, 0x02, 'h', 'i', 0x10, 0x20, 0x30};
  BlobReader R(Bytes);
  EXPECT_EQ(R.readSerString(), std::nullopt);
  std::optional<StringRef> Empty = R.readSerString();
  ASSERT_TRUE(Empty.has_value()) << "00 is the empty string, not null";
  EXPECT_EQ(*Empty, "");
  EXPECT_EQ(R.readSerString(), std::optional<StringRef>("abc"));
  EXPECT_EQ(R.readSerString(), std::optional<StringRef>("\xC3\xA9t\xC3\xA9"));
  EXPECT_EQ(R.readString(), "hi");
  EXPECT_EQ(toVec(R.readBytes(2)), (ByteVec{0x10, 0x20}));
  EXPECT_EQ(toVec(R.readBytes(0)), ByteVec{});
  EXPECT_EQ(toVec(R.readBytes(1)), ByteVec{0x30});
  EXPECT_TRUE(R.empty());
  EXPECT_THAT_ERROR(R.takeError(), Succeeded());
}

TEST(CLIBlobTest, WriterReaderRoundTrip) {
  SmallVector<uint8_t, 64> Out;
  BlobWriter W(Out);
  W.writeU8(1);
  W.writeCompressedUInt(0x3FFF);
  W.writeCompressedInt(-8193);
  W.writeSerString(std::nullopt);
  W.writeSerString(StringRef("x"));
  W.writeString("long string");
  W.align(4);
  W.writeU32(0xCAFEF00D);
  W.writeU64(42);
  W.writeU16(7);

  BlobReader R(Out);
  EXPECT_EQ(R.readU8(), 1u);
  EXPECT_EQ(R.readCompressedUInt(), 0x3FFFu);
  EXPECT_EQ(R.readCompressedInt(), -8193);
  EXPECT_EQ(R.readSerString(), std::nullopt);
  EXPECT_EQ(R.readSerString(), std::optional<StringRef>("x"));
  EXPECT_EQ(R.readString(), "long string");
  size_t Pad = (4 - R.tell() % 4) % 4;
  EXPECT_EQ(toVec(R.readBytes(Pad)), ByteVec(Pad, 0));
  EXPECT_EQ(R.readU32(), 0xCAFEF00Du);
  EXPECT_EQ(R.readU64(), 42u);
  EXPECT_EQ(R.readU16(), 7u);
  EXPECT_TRUE(R.empty());
  EXPECT_THAT_ERROR(R.takeError(), Succeeded());
}

// A read past the end fails; afterwards every read returns 0 or empty and the
// position stays at the read that failed, even for reads that would fit,
// until takeError() returns the failure and clears it, as
// DataExtractor::Cursor::takeError does; reads then continue from tell()
// (section 12 '5.7.3 and 5.7.6 amended (API as built)').
TEST(CLIBlobTest, ReaderErrorIsSticky) {
  const ByteVec Bytes = {0x01, 0x02, 0x03, 0x04, 0x05};
  BlobReader R(Bytes);
  EXPECT_FALSE(R.hasError());
  EXPECT_EQ(R.readU16(), 0x0201u);
  EXPECT_FALSE(R.hasError());
  EXPECT_EQ(R.readU32(), 0u); // only three bytes left
  EXPECT_TRUE(R.hasError());
  size_t Pos = R.tell();
  EXPECT_EQ(Pos, 2u);
  EXPECT_EQ(R.readU8(), 0u);
  EXPECT_EQ(R.readU16(), 0u);
  EXPECT_EQ(R.readCompressedUInt(), 0u);
  EXPECT_EQ(R.readCompressedInt(), 0);
  EXPECT_TRUE(R.readBytes(1).empty());
  EXPECT_EQ(R.readString(), "");
  EXPECT_EQ(R.tell(), Pos);
  EXPECT_EQ(R.remaining(), Bytes.size() - Pos);
  EXPECT_TRUE(R.hasError());
  EXPECT_THAT_ERROR(R.takeError(), Failed());

  // The failure is taken: the error is clear and reading resumes at 2.
  EXPECT_FALSE(R.hasError());
  EXPECT_THAT_ERROR(R.takeError(), Succeeded());
  EXPECT_EQ(R.tell(), 2u);
  EXPECT_EQ(R.readU8(), 0x03u);
  EXPECT_EQ(R.tell(), 3u);
  EXPECT_EQ(R.readU16(), 0x0504u);
  EXPECT_TRUE(R.empty());
  EXPECT_FALSE(R.hasError());
  EXPECT_THAT_ERROR(R.takeError(), Succeeded());
}

TEST(CLIBlobTest, ReaderTruncation) {
  {
    BlobReader R(ArrayRef<uint8_t>{});
    EXPECT_TRUE(R.empty());
    EXPECT_EQ(R.readU8(), 0u);
    EXPECT_THAT_ERROR(R.takeError(), Failed());
  }
  {
    const ByteVec Bytes = {1, 2, 3, 4, 5, 6, 7};
    BlobReader R(Bytes);
    EXPECT_EQ(R.readU64(), 0u);
    EXPECT_THAT_ERROR(R.takeError(), Failed());
  }
  {
    const ByteVec Bytes = {0x05, 'a', 'b'}; // SerString longer than the blob
    BlobReader R(Bytes);
    (void)R.readSerString();
    EXPECT_THAT_ERROR(R.takeError(), Failed());
  }
  {
    const ByteVec Bytes = {0x04, 'a'}; // string longer than the blob
    BlobReader R(Bytes);
    EXPECT_EQ(R.readString(), "");
    EXPECT_THAT_ERROR(R.takeError(), Failed());
  }
  {
    const ByteVec Bytes = {1, 2, 3};
    BlobReader R(Bytes);
    EXPECT_TRUE(R.readBytes(4).empty());
    EXPECT_THAT_ERROR(R.takeError(), Failed());
  }
  {
    const ByteVec Bytes = {0xC0, 0x00, 0x40}; // a 4-byte integer cut short
    BlobReader R(Bytes);
    EXPECT_EQ(R.readCompressedUInt(), 0u);
    EXPECT_THAT_ERROR(R.takeError(), Failed());
  }
  {
    const ByteVec Bytes = {0x80}; // a 2-byte integer cut short
    BlobReader R(Bytes);
    EXPECT_EQ(R.readCompressedInt(), 0);
    EXPECT_THAT_ERROR(R.takeError(), Failed());
  }
}

// Lead bytes 111xxxxx start no compressed integer (II.23.2).
TEST(CLIBlobTest, ReaderBadCompressedInteger) {
  for (uint8_t Lead : {0xE0, 0xF0, 0xFF}) {
    SCOPED_TRACE("lead 0x" + utohexstr(Lead));
    const ByteVec Bytes = {Lead, 0x00, 0x00, 0x00, 0x00};
    {
      BlobReader R(Bytes);
      EXPECT_EQ(R.readCompressedUInt(), 0u);
      EXPECT_THAT_ERROR(R.takeError(), Failed());
    }
    {
      BlobReader R(Bytes);
      EXPECT_EQ(R.readCompressedInt(), 0);
      EXPECT_THAT_ERROR(R.takeError(), Failed());
    }
    {
      // 0xFF alone is the null SerString, but no string length starts with
      // another 111xxxxx byte.
      BlobReader R(Bytes);
      EXPECT_EQ(R.readString(), "");
      EXPECT_THAT_ERROR(R.takeError(), Failed());
    }
  }
  {
    const ByteVec Bytes = {0xE5, 'a'};
    BlobReader R(Bytes);
    (void)R.readSerString();
    EXPECT_THAT_ERROR(R.takeError(), Failed());
  }
}

} // namespace
