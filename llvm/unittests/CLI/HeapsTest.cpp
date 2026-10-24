//===- HeapsTest.cpp - ECMA-335 metadata heap builders --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks the heap builders of llvm/CLI/Heaps.h against ECMA-335 (6th edition)
// II.24.2.2-II.24.2.5 and the heap rules of the MSIL implementation plan
// (5.7.6, 5.7.12): #Strings is StringTableBuilder(ELF) with tail merging and a
// content-only order; #Blob, #US and #GUID deduplicate in insertion order;
// #US entries are a compressed length 2n+1, UTF-16LE and the trailing byte
// of 5.7.6 as amended in section 12 (not II.24.2.4's wording; see
// UserStringTrailingByte). Expected bytes come from ECMA-335, from the recon
// prototype llvm/docs/MSIL/recon/p2/metadata-writer-files/py/cliwriter.py
// (Heaps.user_string and compress_u, run in scratch) and, for the trailing
// byte, from Framework ilasm 4.8 run in scratch, never from the
// implementation under test.
//
// 5.7.6 does not say whether a builder's getSize() includes the 4-byte
// stream padding of II.24.2.2, so the size checks accept the unpadded size or
// the size rounded up to 4; the padding itself must be zero.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/Heaps.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/TableSchema.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

using ByteVec = std::vector<uint8_t>;

// Checks a builder's getSize() against the unpadded heap size: either that
// size or that size rounded up to 4 (see the file comment).
void expectHeapSize(uint32_t Actual, uint64_t Unpadded) {
  EXPECT_GE(Actual, Unpadded);
  EXPECT_EQ(alignTo(Actual, 4), alignTo(Unpadded, 4))
      << "getSize() " << Actual << ", heap content " << Unpadded;
}

// Writes a heap twice, into buffers of getSize() bytes filled with 0xCC and
// with 0x00 first: write() must set every byte (the output depends only on the
// calls, 5.7.12), so both results are equal. Bytes from Unpadded on must be
// zero (padding).
template <typename HeapT> ByteVec writeHeap(const HeapT &H, uint64_t Unpadded) {
  ByteVec A(H.getSize(), 0xCC), B(H.getSize(), 0x00);
  H.write(A);
  H.write(B);
  EXPECT_EQ(A, B) << "write() left bytes of its output unwritten";
  for (size_t I = Unpadded; I < A.size(); ++I)
    EXPECT_EQ(A[I], 0) << "padding byte " << I << " is not zero";
  return A;
}

// The NUL-terminated string at Index of a #Strings heap.
std::string stringAt(const ByteVec &Heap, uint32_t Index) {
  std::string S;
  if (Index >= Heap.size()) {
    ADD_FAILURE() << "string index " << Index << " is past the heap ("
                  << Heap.size() << " bytes)";
    return S;
  }
  size_t I = Index;
  for (; I < Heap.size() && Heap[I] != 0; ++I)
    S += static_cast<char>(Heap[I]);
  EXPECT_LT(I, Heap.size()) << "string at " << Index << " is not terminated";
  return S;
}

// UTF-8 of one code point.
std::string utf8(uint32_t CP) {
  std::string S;
  if (CP < 0x80) {
    S += static_cast<char>(CP);
  } else if (CP < 0x800) {
    S += static_cast<char>(0xC0 | (CP >> 6));
    S += static_cast<char>(0x80 | (CP & 0x3F));
  } else if (CP < 0x10000) {
    S += static_cast<char>(0xE0 | (CP >> 12));
    S += static_cast<char>(0x80 | ((CP >> 6) & 0x3F));
    S += static_cast<char>(0x80 | (CP & 0x3F));
  } else {
    S += static_cast<char>(0xF0 | (CP >> 18));
    S += static_cast<char>(0x80 | ((CP >> 12) & 0x3F));
    S += static_cast<char>(0x80 | ((CP >> 6) & 0x3F));
    S += static_cast<char>(0x80 | (CP & 0x3F));
  }
  return S;
}

// Distinct strings of one shape ("s" + 6 digits + 'x' padding), so none is a
// suffix of another and each adds exactly Length + 1 bytes to a #Strings
// heap: they fill a heap that starts at Base bytes to exactly Target bytes.
std::vector<std::string> makeFillStrings(uint32_t Base, uint32_t Target) {
  std::vector<std::string> Out;
  unsigned Serial = 0;
  auto Make = [&](size_t Length) {
    char Prefix[16];
    std::snprintf(Prefix, sizeof(Prefix), "s%06u", Serial++);
    std::string S(Prefix);
    S.resize(Length, 'x');
    Out.push_back(S);
  };
  uint32_t Fill = Target - Base;
  uint32_t N = Fill / 1000, R = Fill % 1000;
  if (R != 0 && R - 1 < 8) {
    // Too short for a distinct string: make the last one longer instead.
    --N;
    R += 1000;
  }
  for (uint32_t I = 0; I < N; ++I)
    Make(999);
  if (R != 0)
    Make(R - 1);
  return Out;
}

std::array<uint8_t, 16> makeGuid(uint32_t N) {
  std::array<uint8_t, 16> G;
  for (unsigned I = 0; I < 16; ++I)
    G[I] = static_cast<uint8_t>((N >> ((I % 4) * 8)) + I * 0x11);
  return G;
}

//===----------------------------------------------------------------------===//
// #Strings (II.24.2.3)
//===----------------------------------------------------------------------===//

TEST(CLIHeapsTest, StringsEmptyStringIsIndexZero) {
  StringHeapBuilder H;
  H.add("");
  H.finalize();
  EXPECT_EQ(H.getIndex(""), 0u);
  expectHeapSize(H.getSize(), 1);
  ByteVec Bytes = writeHeap(H, 1);
  ASSERT_GE(Bytes.size(), 1u);
  EXPECT_EQ(Bytes[0], 0);
}

TEST(CLIHeapsTest, StringsDeduplicate) {
  StringHeapBuilder H;
  H.add("main");
  H.add("main");
  std::string Built = std::string("ma") + "in";
  H.add(Built);
  H.finalize();
  // One copy: "\0main\0".
  EXPECT_EQ(H.getIndex("main"), 1u);
  expectHeapSize(H.getSize(), 6);
  ByteVec Bytes = writeHeap(H, 6);
  ASSERT_GE(Bytes.size(), 6u);
  EXPECT_EQ(ByteVec(Bytes.begin(), Bytes.begin() + 6),
            (ByteVec{0x00, 'm', 'a', 'i', 'n', 0x00}));
}

TEST(CLIHeapsTest, StringsTailMerging) {
  StringHeapBuilder H;
  for (StringRef S : {"oo", "Foo", "r", "Bar", "ar", "Baz"})
    H.add(S);
  H.finalize();
  // Only Foo, Bar and Baz are stored; the suffixes point into them.
  expectHeapSize(H.getSize(), 1 + 4 + 4 + 4);
  ByteVec Bytes = writeHeap(H, 13);
  EXPECT_EQ(H.getIndex("oo"), H.getIndex("Foo") + 1);
  EXPECT_EQ(H.getIndex("ar"), H.getIndex("Bar") + 1);
  EXPECT_EQ(H.getIndex("r"), H.getIndex("Bar") + 2);
  for (StringRef S : {"oo", "Foo", "r", "Bar", "ar", "Baz"}) {
    EXPECT_NE(H.getIndex(S), 0u) << S.str();
    EXPECT_EQ(stringAt(Bytes, H.getIndex(S)), S.str());
  }
  EXPECT_EQ(Bytes[0], 0);
}

TEST(CLIHeapsTest, StringsOrderDependsOnContentOnly) {
  const char *Names[] = {"<Module>", "System",  "Object", "ValueType",
                         "main",     "ain",     "puts",   "<msil>Main",
                         "Main",     "ucrtbase"};
  StringHeapBuilder A, B;
  for (const char *N : Names)
    A.add(N);
  for (const char *N : llvm::reverse(Names)) {
    B.add(N);
    B.add(N);
  }
  A.finalize();
  B.finalize();
  ASSERT_EQ(A.getSize(), B.getSize());
  EXPECT_EQ(writeHeap(A, A.getSize()), writeHeap(B, B.getSize()));
  for (const char *N : Names)
    EXPECT_EQ(A.getIndex(N), B.getIndex(N)) << N;
}

TEST(CLIHeapsTest, StringsCopyTheirInput) {
  StringHeapBuilder H;
  std::string Buffer = "transient";
  H.add(Buffer);
  Buffer.assign("overwritten-and-longer");
  H.add("other");
  H.finalize();
  ByteVec Bytes = writeHeap(H, 1 + 10 + 6);
  EXPECT_EQ(stringAt(Bytes, H.getIndex("transient")), "transient");
  EXPECT_EQ(stringAt(Bytes, H.getIndex("other")), "other");
  expectHeapSize(H.getSize(), 1 + 10 + 6);
}

TEST(CLIHeapsTest, StringsKeepUTF8Bytes) {
  StringHeapBuilder H;
  std::string Cafe = "caf" + utf8(0xE9);          // 63 61 66 C3 A9
  std::string Smiley = "f" + utf8(0x1F600) + "g"; // a 4-byte sequence
  H.add(Cafe);
  H.add(Smiley);
  H.finalize();
  ByteVec Bytes = writeHeap(H, 1 + 6 + 7);
  EXPECT_EQ(stringAt(Bytes, H.getIndex(Cafe)), Cafe);
  EXPECT_EQ(stringAt(Bytes, H.getIndex(Smiley)), Smiley);
  expectHeapSize(H.getSize(), 1 + 6 + 7);
}

// The HeapSizes bit of #Strings follows the stream size with its padding
// (5.7.6): content of 0xFFFC bytes keeps 2-byte indexes, 0xFFFD (written as
// 0x10000) and 0x10000 need 4-byte indexes.
TEST(CLIHeapsTest, StringsSizeFlipsHeapSizeFlag) {
  for (uint32_t Target : {0xFFFCu, 0xFFFDu, 0xFFFFu, 0x10000u}) {
    SCOPED_TRACE(Target);
    StringHeapBuilder H;
    std::vector<std::string> Fill = makeFillStrings(1, Target);
    for (const std::string &S : Fill)
      H.add(S);
    H.finalize();
    expectHeapSize(H.getSize(), Target);
    uint8_t Flags =
        MetadataSizes::computeHeapSizeFlags(alignTo(H.getSize(), 4), 16, 4);
    EXPECT_EQ(Flags, Target <= 0xFFFC ? 0 : HEAP_STRINGS_4);
    ByteVec Bytes = writeHeap(H, Target);
    EXPECT_EQ(stringAt(Bytes, H.getIndex(Fill.back())), Fill.back());
  }
}

//===----------------------------------------------------------------------===//
// #Blob (II.24.2.4)
//===----------------------------------------------------------------------===//

TEST(CLIHeapsTest, BlobEmptyIsOffsetZero) {
  BlobHeapBuilder H;
  EXPECT_EQ(H.add({}), 0u);
  expectHeapSize(H.getSize(), 1);
  ByteVec Bytes = writeHeap(H, 1);
  ASSERT_GE(Bytes.size(), 1u);
  EXPECT_EQ(Bytes[0], 0);
}

// Compressed length prefixes (II.23.2) at the 1-, 2- and 4-byte boundaries.
TEST(CLIHeapsTest, BlobLengthPrefixes) {
  BlobHeapBuilder H;
  ByteVec B7F(0x7F, 0xA1), B80(0x80, 0xA2), B3FFF(0x3FFF, 0xA3),
      B4000(0x4000, 0xA4);
  EXPECT_EQ(H.add(B7F), 0x1u);
  EXPECT_EQ(H.add(B80), 0x81u);             // 1 + 1 + 0x7F
  EXPECT_EQ(H.add(B3FFF), 0x103u);          // 0x81 + 2 + 0x80
  EXPECT_EQ(H.add(B4000), 0x4104u);         // 0x103 + 2 + 0x3FFF
  const uint32_t End = 0x4104 + 4 + 0x4000; // 0x8108, a multiple of 4
  EXPECT_EQ(H.getSize(), End);
  ByteVec Bytes = writeHeap(H, End);
  ASSERT_EQ(Bytes.size(), End);
  EXPECT_EQ(Bytes[0], 0x00);
  EXPECT_EQ(Bytes[0x1], 0x7F);
  EXPECT_EQ(Bytes[0x2], 0xA1);
  EXPECT_EQ(Bytes[0x80], 0xA1);
  EXPECT_EQ(Bytes[0x81], 0x80);
  EXPECT_EQ(Bytes[0x82], 0x80);
  EXPECT_EQ(Bytes[0x83], 0xA2);
  EXPECT_EQ(Bytes[0x102], 0xA2);
  EXPECT_EQ(Bytes[0x103], 0xBF);
  EXPECT_EQ(Bytes[0x104], 0xFF);
  EXPECT_EQ(Bytes[0x105], 0xA3);
  EXPECT_EQ(Bytes[0x4103], 0xA3);
  EXPECT_EQ(ByteVec(Bytes.begin() + 0x4104, Bytes.begin() + 0x4108),
            (ByteVec{0xC0, 0x00, 0x40, 0x00}));
  EXPECT_EQ(Bytes[0x4108], 0xA4);
  EXPECT_EQ(Bytes[End - 1], 0xA4);
}

TEST(CLIHeapsTest, BlobDeduplicatesInInsertionOrder) {
  BlobHeapBuilder H;
  ByteVec FieldI32 = {0x06, 0x08}, FieldNInt = {0x06, 0x18};
  EXPECT_EQ(H.add(FieldI32), 1u);
  EXPECT_EQ(H.add(FieldNInt), 4u);
  EXPECT_EQ(H.add(FieldI32), 1u);
  ByteVec Copy = {0x06, 0x18};
  EXPECT_EQ(H.add(Copy), 4u);
  // A prefix of an earlier blob is a new blob.
  EXPECT_EQ(H.add(ByteVec{0x06}), 7u);
  expectHeapSize(H.getSize(), 9);
  ByteVec Bytes = writeHeap(H, 9);
  ASSERT_GE(Bytes.size(), 9u);
  EXPECT_EQ(ByteVec(Bytes.begin(), Bytes.begin() + 9),
            (ByteVec{0x00, 0x02, 0x06, 0x08, 0x02, 0x06, 0x18, 0x01, 0x06}));
}

TEST(CLIHeapsTest, BlobSizeFlipsHeapSizeFlag) {
  for (uint32_t Target : {0xFFFCu, 0xFFFDu, 0x10000u}) {
    SCOPED_TRACE(Target);
    BlobHeapBuilder H;
    // 1 (the empty blob) + a 4-byte length + the content.
    ByteVec Big(Target - 5, 0x5A);
    EXPECT_EQ(H.add(Big), 1u);
    expectHeapSize(H.getSize(), Target);
    EXPECT_EQ(
        MetadataSizes::computeHeapSizeFlags(4, 16, alignTo(H.getSize(), 4)),
        Target <= 0xFFFC ? 0 : HEAP_BLOB_4);
  }
}

//===----------------------------------------------------------------------===//
// #US (II.24.2.4)
//===----------------------------------------------------------------------===//

struct USEntry {
  std::vector<uint16_t> Units;
  int Trailing = -1; // -1: no trailing byte (a zero-length entry)
  uint32_t End = 0;
};

// Decodes the #US entry at Offset: compressed length, UTF-16LE, trailing byte.
std::optional<USEntry> readUserString(const ByteVec &Heap, uint32_t Offset) {
  if (Offset >= Heap.size())
    return std::nullopt;
  ArrayRef<uint8_t> Rest = ArrayRef<uint8_t>(Heap).drop_front(Offset);
  size_t Before = Rest.size();
  std::optional<uint32_t> Length = decodeCompressedUInt(Rest);
  if (!Length || *Length > Rest.size())
    return std::nullopt;
  USEntry E;
  E.End = static_cast<uint32_t>(Offset + (Before - Rest.size()) + *Length);
  if (*Length == 0)
    return E;
  if (*Length % 2 != 1)
    return std::nullopt;
  for (uint32_t I = 0; I + 1 < *Length; I += 2)
    E.Units.push_back(static_cast<uint16_t>(Rest[I] | (Rest[I + 1] << 8)));
  E.Trailing = Rest[*Length - 1];
  return E;
}

ByteVec writeUserStrings(const UserStringHeapBuilder &H) {
  return writeHeap(H, H.getSize());
}

TEST(CLIHeapsTest, UserStringFirstEntry) {
  UserStringHeapBuilder H;
  Expected<uint32_t> Off = H.add("A");
  ASSERT_THAT_EXPECTED(Off, Succeeded());
  EXPECT_EQ(*Off, 1u);
  expectHeapSize(H.getSize(), 5);
  ByteVec Bytes = writeHeap(H, 5);
  ASSERT_GE(Bytes.size(), 5u);
  // Offset 0 is 0x00; "A" is length 3, 41 00, trailing 00 (prototype).
  EXPECT_EQ(ByteVec(Bytes.begin(), Bytes.begin() + 5),
            (ByteVec{0x00, 0x03, 0x41, 0x00, 0x00}));
}

// 5.7.6 as amended (section 12): the trailing byte is 1 iff some UTF-16 code
// unit is 0x7F or above, lies in 0x01-0x08 or 0x0E-0x1F, or is 0x27 or 0x2D.
// This is what Framework ilasm 4.8, the CLR's emitter and Roslyn write (ilasm
// run in scratch gives 1 for U+007F, U+0080, U+00A0, U+00E9, U+00FF and
// U+0100, and 0 for U+0009, U+000A, U+000D, U+0020, U+0041 and U+007E).
// II.24.2.4's 'non-zero high byte' wording would give 0 for U+0080-U+00FF,
// but .NET Framework 4.8 reads a 0 as a promise that the literal is ASCII
// only (ordinal case-insensitive hashing of Latin-1 ldstr literals then
// disagrees with equal run-time strings), so U+007E/U+007F is the boundary.
TEST(CLIHeapsTest, UserStringTrailingByte) {
  struct Case {
    uint32_t CodePoint;
    uint8_t Trailing;
  };
  const Case Cases[] = {
      {0x0001, 1}, {0x0005, 1}, {0x0008, 1}, {0x000E, 1}, {0x0015, 1},
      {0x001F, 1}, {0x0027, 1}, {0x002D, 1}, {0x007F, 1}, {0x0100, 1},
      {0x01FF, 1}, {0x4E2D, 1}, {0xFFFD, 1}, {0x0000, 0}, {0x0009, 0},
      {0x000A, 0}, {0x000B, 0}, {0x000C, 0}, {0x000D, 0}, {0x0020, 0},
      {0x0026, 0}, {0x0028, 0}, {0x002C, 0}, {0x002E, 0}, {0x0041, 0},
      {0x007E, 0}, {0x0080, 1}, {0x00A0, 1}, {0x00E9, 1}, {0x00FF, 1},
  };
  for (const Case &C : Cases) {
    SCOPED_TRACE(C.CodePoint);
    UserStringHeapBuilder H;
    Expected<uint32_t> Mid = H.add("a" + utf8(C.CodePoint) + "b");
    ASSERT_THAT_EXPECTED(Mid, Succeeded());
    Expected<uint32_t> Alone = H.add(utf8(C.CodePoint));
    ASSERT_THAT_EXPECTED(Alone, Succeeded());
    EXPECT_EQ(*Mid, 1u);
    EXPECT_EQ(*Alone, 9u); // 1 + 1 + 7
    ByteVec Bytes = writeUserStrings(H);
    std::optional<USEntry> E = readUserString(Bytes, *Mid);
    ASSERT_TRUE(E.has_value());
    EXPECT_EQ(E->Units, (std::vector<uint16_t>{
                            0x61, static_cast<uint16_t>(C.CodePoint), 0x62}));
    EXPECT_EQ(E->Trailing, C.Trailing);
    EXPECT_EQ(Bytes[*Mid], 7u); // 2 * 3 + 1
    std::optional<USEntry> A = readUserString(Bytes, *Alone);
    ASSERT_TRUE(A.has_value());
    EXPECT_EQ(A->Units,
              (std::vector<uint16_t>{static_cast<uint16_t>(C.CodePoint)}));
    EXPECT_EQ(A->Trailing, C.Trailing);
    expectHeapSize(H.getSize(), 1 + 8 + 4);
  }
  // One special unit at the end of a long plain string still sets it.
  UserStringHeapBuilder H;
  Expected<uint32_t> Off = H.add(std::string(100, 'z') + "'");
  ASSERT_THAT_EXPECTED(Off, Succeeded());
  std::optional<USEntry> E = readUserString(writeUserStrings(H), *Off);
  ASSERT_TRUE(E.has_value());
  EXPECT_EQ(E->Units.size(), 101u);
  EXPECT_EQ(E->Trailing, 1);
}

TEST(CLIHeapsTest, UserStringSurrogatePairs) {
  UserStringHeapBuilder H;
  Expected<uint32_t> Smiley = H.add(utf8(0x1F600));
  ASSERT_THAT_EXPECTED(Smiley, Succeeded());
  Expected<uint32_t> Mixed = H.add("a" + utf8(0x1F600));
  ASSERT_THAT_EXPECTED(Mixed, Succeeded());
  Expected<uint32_t> Last = H.add(utf8(0x10FFFF));
  ASSERT_THAT_EXPECTED(Last, Succeeded());
  EXPECT_EQ(*Smiley, 1u);
  EXPECT_EQ(*Mixed, 7u);
  EXPECT_EQ(*Last, 15u);
  expectHeapSize(H.getSize(), 21);
  ByteVec Bytes = writeHeap(H, 21);
  ASSERT_GE(Bytes.size(), 21u);
  // U+1F600 is D83D DE00 in UTF-16; U+10FFFF is DBFF DFFF (prototype bytes).
  EXPECT_EQ(
      ByteVec(Bytes.begin(), Bytes.begin() + 21),
      (ByteVec{0x00, 0x05, 0x3D, 0xD8, 0x00, 0xDE, 0x01, 0x07, 0x61, 0x00, 0x3D,
               0xD8, 0x00, 0xDE, 0x01, 0x05, 0xFF, 0xDB, 0xFF, 0xDF, 0x01}));
}

TEST(CLIHeapsTest, UserStringLengthPrefixes) {
  UserStringHeapBuilder H;
  Expected<uint32_t> A = H.add(std::string(0x40, 'x')); // 0x81 bytes
  ASSERT_THAT_EXPECTED(A, Succeeded());
  Expected<uint32_t> B = H.add(std::string(0x2000, 'y')); // 0x4001 bytes
  ASSERT_THAT_EXPECTED(B, Succeeded());
  EXPECT_EQ(*A, 1u);
  EXPECT_EQ(*B, 132u); // 1 + 2 + 0x81
  const uint32_t End = 132 + 4 + 0x4001;
  expectHeapSize(H.getSize(), End);
  ByteVec Bytes = writeHeap(H, End);
  EXPECT_EQ(ByteVec(Bytes.begin() + 1, Bytes.begin() + 4),
            (ByteVec{0x80, 0x81, 'x'}));
  EXPECT_EQ(ByteVec(Bytes.begin() + 132, Bytes.begin() + 137),
            (ByteVec{0xC0, 0x00, 0x40, 0x01, 'y'}));
  std::optional<USEntry> E = readUserString(Bytes, *B);
  ASSERT_TRUE(E.has_value());
  EXPECT_EQ(E->Units.size(), 0x2000u);
  EXPECT_EQ(E->Trailing, 0);
  EXPECT_EQ(E->End, End);
}

TEST(CLIHeapsTest, UserStringDeduplicates) {
  UserStringHeapBuilder H;
  Expected<uint32_t> Hello = H.add("hello");
  ASSERT_THAT_EXPECTED(Hello, Succeeded());
  Expected<uint32_t> Bang = H.add("hello!");
  ASSERT_THAT_EXPECTED(Bang, Succeeded());
  Expected<uint32_t> Again = H.add(std::string("hel") + "lo");
  ASSERT_THAT_EXPECTED(Again, Succeeded());
  EXPECT_EQ(*Hello, 1u);
  EXPECT_EQ(*Bang, 13u); // 1 + 1 + 11
  EXPECT_EQ(*Again, *Hello);
  expectHeapSize(H.getSize(), 13 + 1 + 13);
}

// "" needs no characters: whatever offset add("") gives must read back as
// the empty string (offset 0's single 0x00, or an entry 01 00 as the
// prototype writes it).
TEST(CLIHeapsTest, UserStringEmpty) {
  UserStringHeapBuilder H;
  ASSERT_THAT_EXPECTED(H.add("x"), Succeeded());
  Expected<uint32_t> Empty = H.add("");
  ASSERT_THAT_EXPECTED(Empty, Succeeded());
  ByteVec Bytes = writeUserStrings(H);
  std::optional<USEntry> E = readUserString(Bytes, *Empty);
  ASSERT_TRUE(E.has_value());
  EXPECT_TRUE(E->Units.empty());
  if (*Empty != 0) {
    EXPECT_EQ(E->Trailing, 0);
    EXPECT_EQ(Bytes[*Empty], 1u);
  }
}

TEST(CLIHeapsTest, UserStringRejectsInvalidUTF8) {
  const char *Invalid[] = {
      "\xFF",             // never a UTF-8 byte
      "a\xC3\x28",        // a lead byte without its continuation
      "\xE2\x82",         // truncated 3-byte sequence
      "\xC0\xAF",         // overlong '/'
      "\xED\xA0\x80",     // an encoded surrogate (U+D800)
      "\xF4\x90\x80\x80", // above U+10FFFF
      "ok\x80",           // a stray continuation byte
  };
  for (const char *S : Invalid) {
    SCOPED_TRACE(S);
    UserStringHeapBuilder H;
    EXPECT_THAT_EXPECTED(H.add(S), Failed());
  }
}

// The heap may not pass MaxUserStringHeapSize: ldstr tokens hold a 24-bit
// offset (5.7.12).
TEST(CLIHeapsTest, UserStringHeapOverflow) {
  {
    // 0x800000 characters: 1 + 4 + 0x1000001 bytes.
    UserStringHeapBuilder H;
    EXPECT_THAT_EXPECTED(H.add(std::string(0x800000, 'a')), Failed());
  }
  UserStringHeapBuilder H;
  // 0x7FFF00 characters: 1 + 4 + 0xFFFE01 = 0xFFFE06 bytes, which fits.
  Expected<uint32_t> Big = H.add(std::string(0x7FFF00, 'b'));
  ASSERT_THAT_EXPECTED(Big, Succeeded());
  EXPECT_EQ(*Big, 1u);
  expectHeapSize(H.getSize(), 0xFFFE06);
  // Another 0x203 bytes would end at 0x1000009.
  EXPECT_THAT_EXPECTED(H.add(std::string(0x100, 'c')), Failed());
}

//===----------------------------------------------------------------------===//
// #GUID (II.24.2.5)
//===----------------------------------------------------------------------===//

TEST(CLIHeapsTest, GuidIndexesAreOneBased) {
  GuidHeapBuilder Empty;
  EXPECT_EQ(Empty.getSize(), 0u);

  GuidHeapBuilder H;
  std::array<uint8_t, 16> Zero{}, One = makeGuid(1), Two = makeGuid(2);
  EXPECT_EQ(H.add(Zero), 1u);
  EXPECT_EQ(H.add(One), 2u);
  EXPECT_EQ(H.add(Zero), 1u);
  EXPECT_EQ(H.add(Two), 3u);
  EXPECT_EQ(H.add(One), 2u);
  EXPECT_EQ(H.getSize(), 48u);
  ByteVec Bytes = writeHeap(H, 48);
  ASSERT_EQ(Bytes.size(), 48u);
  EXPECT_EQ(ByteVec(Bytes.begin(), Bytes.begin() + 16), ByteVec(16, 0));
  EXPECT_EQ(ByteVec(Bytes.begin() + 16, Bytes.begin() + 32),
            ByteVec(One.begin(), One.end()));
  EXPECT_EQ(ByteVec(Bytes.begin() + 32, Bytes.end()),
            ByteVec(Two.begin(), Two.end()));
}

TEST(CLIHeapsTest, GuidSizeFlipsHeapSizeFlag) {
  for (uint32_t Count : {4095u, 4096u}) {
    SCOPED_TRACE(Count);
    GuidHeapBuilder H;
    for (uint32_t I = 0; I < Count; ++I)
      EXPECT_EQ(H.add(makeGuid(I)), I + 1);
    EXPECT_EQ(H.getSize(), Count * 16);
    EXPECT_EQ(MetadataSizes::computeHeapSizeFlags(4, H.getSize(), 4),
              Count == 4095 ? 0 : HEAP_GUID_4);
  }
}

} // namespace
