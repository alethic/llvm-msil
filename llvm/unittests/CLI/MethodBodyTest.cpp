//===- MethodBodyTest.cpp - ECMA-335 method bodies ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/MethodBody.h (plan 5.7.5, ECMA-335 II.25.4): the tiny and
// fat headers at their boundaries (code 63/64 bytes, maxstack 0/7/8/9 as
// amended in section 12, locals, InitLocals, clauses), 4-byte alignment of fat
// bodies among tiny ones, small and fat exception sections (20/21 clauses,
// offsets 0xFFFF/0x10000, lengths 0xFF/0x100), decodeMethodBody on bodies
// that .NET Framework ilasm 4.8 wrote (read back with
// System.Reflection.Metadata in scratch) and on hand-built MoreSects chains,
// the decode errors, and the 699,050-clause limit (section 12).
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/MethodBody.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

using ByteVec = std::vector<uint8_t>;

ByteVec toVec(ArrayRef<uint8_t> A) { return ByteVec(A.begin(), A.end()); }

ByteVec fromHex(StringRef Hex) {
  ByteVec R;
  SmallVector<StringRef, 128> Parts;
  Hex.split(Parts, ' ', -1, /*KeepEmpty=*/false);
  for (StringRef P : Parts) {
    unsigned V = 0;
    EXPECT_FALSE(P.getAsInteger(16, V)) << P;
    R.push_back(static_cast<uint8_t>(V));
  }
  return R;
}

ByteVec cat(std::initializer_list<ByteVec> Parts) {
  ByteVec R;
  for (const ByteVec &P : Parts)
    R.insert(R.end(), P.begin(), P.end());
  return R;
}

// N bytes of code: nops and a final ret.
ByteVec nops(size_t N) {
  ByteVec C(N, 0x00);
  if (N)
    C.back() = 0x2A;
  return C;
}

ByteVec le16(uint16_t V) { return {uint8_t(V), uint8_t(V >> 8)}; }
ByteVec le32(uint32_t V) {
  return {uint8_t(V), uint8_t(V >> 8), uint8_t(V >> 16), uint8_t(V >> 24)};
}

// A fat header: flags and size (3 dwords) word, maxstack, code size, locals.
ByteVec fatHeader(uint16_t Flags, uint16_t MaxStack, uint32_t CodeSize,
                  uint32_t LocalSig) {
  return cat({le16(uint16_t(Flags | 0x3000)), le16(MaxStack), le32(CodeSize),
              le32(LocalSig)});
}

ExceptionClause clause(ExceptionClauseKind K, uint32_t TryOff, uint32_t TryLen,
                       uint32_t HOff, uint32_t HLen, uint32_t Extra = 0) {
  ExceptionClause C;
  C.Kind = K;
  C.TryOffset = TryOff;
  C.TryLength = TryLen;
  C.HandlerOffset = HOff;
  C.HandlerLength = HLen;
  C.ClassTokenOrFilterOffset = Extra;
  return C;
}

ByteVec smallClause(const ExceptionClause &C) {
  return cat({le16(uint16_t(C.Kind)),
              le16(uint16_t(C.TryOffset)),
              {uint8_t(C.TryLength)},
              le16(uint16_t(C.HandlerOffset)),
              {uint8_t(C.HandlerLength)},
              le32(C.ClassTokenOrFilterOffset)});
}

ByteVec fatClause(const ExceptionClause &C) {
  return cat({le32(uint32_t(C.Kind)), le32(C.TryOffset), le32(C.TryLength),
              le32(C.HandlerOffset), le32(C.HandlerLength),
              le32(C.ClassTokenOrFilterOffset)});
}

void expectClause(const ExceptionClause &Got, const ExceptionClause &Want) {
  EXPECT_EQ(static_cast<uint32_t>(Got.Kind), static_cast<uint32_t>(Want.Kind));
  EXPECT_EQ(Got.TryOffset, Want.TryOffset);
  EXPECT_EQ(Got.TryLength, Want.TryLength);
  EXPECT_EQ(Got.HandlerOffset, Want.HandlerOffset);
  EXPECT_EQ(Got.HandlerLength, Want.HandlerLength);
  EXPECT_EQ(Got.ClassTokenOrFilterOffset, Want.ClassTokenOrFilterOffset);
}

MethodBodyInfo info(ArrayRef<uint8_t> Code, uint16_t MaxStack = 8,
                    uint32_t LocalSig = 0, bool InitLocals = false,
                    ArrayRef<ExceptionClause> Clauses = {}) {
  MethodBodyInfo B;
  B.Code = Code;
  B.MaxStack = MaxStack;
  B.LocalVarSigToken = LocalSig;
  B.InitLocals = InitLocals;
  B.Clauses = Clauses;
  return B;
}

// The bytes of B as the first body of a new stream.
ByteVec encode(const MethodBodyInfo &B) {
  MethodBodyStream S;
  EXPECT_EQ(S.addBody(B), 0u);
  EXPECT_EQ(S.size(), S.getBytes().size());
  return toVec(S.getBytes());
}

// Decodes Bytes and checks it against B.
void expectDecodes(ArrayRef<uint8_t> Bytes, const MethodBodyInfo &B,
                   bool IsTiny, uint32_t Size) {
  Expected<MethodBodyView> V = decodeMethodBody(Bytes);
  ASSERT_THAT_EXPECTED(V, Succeeded());
  EXPECT_EQ(V->IsTiny, IsTiny);
  EXPECT_EQ(V->MaxStack, IsTiny ? 8 : B.MaxStack);
  EXPECT_EQ(V->LocalVarSigToken, B.LocalVarSigToken);
  EXPECT_EQ(V->InitLocals, B.InitLocals);
  EXPECT_EQ(toVec(V->Code), toVec(B.Code));
  EXPECT_EQ(V->Size, Size);
  ASSERT_EQ(V->Clauses.size(), B.Clauses.size());
  for (size_t I = 0; I != B.Clauses.size(); ++I) {
    SCOPED_TRACE("clause " + std::to_string(I));
    expectClause(V->Clauses[I], B.Clauses[I]);
  }
}

void expectInvalidBody(ArrayRef<uint8_t> Bytes) {
  EXPECT_THAT_EXPECTED(
      decodeMethodBody(Bytes),
      FailedWithMessage(testing::StartsWith("invalid method body: ")));
}

// The message of a decode error, which starts with the prefix; lowercased, so
// that callers match words of the cause in any case.
std::string invalidBodyMessage(ArrayRef<uint8_t> Bytes) {
  Expected<MethodBodyView> V = decodeMethodBody(Bytes);
  if (V) {
    ADD_FAILURE() << "the body decoded without an error";
    return "";
  }
  std::string Msg = toString(V.takeError());
  EXPECT_THAT(Msg, testing::StartsWith("invalid method body: "));
  return StringRef(Msg).lower();
}

//===----------------------------------------------------------------------===//
// Headers
//===----------------------------------------------------------------------===//

// Tiny exactly when the code is at most 63 bytes, maxstack is exactly 8 and
// there are no locals, no clauses and no InitLocals (section 12 '5.7.5
// amended (tiny headers)'); header byte (size << 2) | 2. A tiny header has no
// maxstack field (ECMA-335 II.25.4.2 reads it as 8), so a maxstack below 8 is
// kept in a fat header with InitLocals clear, as both ilasms keep an explicit
// .maxstack below 8 in a fat header.
TEST(CLIMethodBodyTest, TinyAndFatBoundaries) {
  ByteVec C63 = nops(63), C64 = nops(64), Ret = {0x2A};

  ByteVec B = encode(info(C63));
  EXPECT_EQ(B, cat({{0xFE}, C63}));
  expectDecodes(B, info(C63), /*IsTiny=*/true, 64);

  B = encode(info(C64));
  EXPECT_EQ(B, cat({fatHeader(0x3, 8, 64, 0), C64}));
  expectDecodes(B, info(C64), /*IsTiny=*/false, 76);

  // Maxstack 0, 7 and 9 at 63 bytes of code: fat, InitLocals clear, the
  // maxstack kept (8 is the tiny case above).
  for (unsigned MaxStack : {0u, 7u, 9u}) {
    SCOPED_TRACE("maxstack " + std::to_string(MaxStack));
    B = encode(info(C63, uint16_t(MaxStack)));
    EXPECT_EQ(B, cat({fatHeader(0x3, uint16_t(MaxStack), 63, 0), C63}));
    expectDecodes(B, info(C63, uint16_t(MaxStack)), /*IsTiny=*/false, 75);
  }

  B = encode(info(Ret, 8));
  EXPECT_EQ(B, (ByteVec{0x06, 0x2A}));
  expectDecodes(B, info(Ret), /*IsTiny=*/true, 2);

  // Maxstack 0: a fat header with maxstack 0 and InitLocals clear.
  B = encode(info(Ret, 0));
  EXPECT_EQ(B, fromHex("03 30 00 00 01 00 00 00 00 00 00 00 2A"));
  EXPECT_EQ(B, cat({fatHeader(0x3, 0, 1, 0), Ret}));
  expectDecodes(B, info(Ret, 0), /*IsTiny=*/false, 13);

  B = encode(info(Ret, 9));
  EXPECT_EQ(B, cat({fatHeader(0x3, 9, 1, 0), Ret}));
  expectDecodes(B, info(Ret, 9), /*IsTiny=*/false, 13);

  B = encode(info(Ret, 65535));
  EXPECT_EQ(B, cat({fatHeader(0x3, 65535, 1, 0), Ret}));
  expectDecodes(B, info(Ret, 65535), /*IsTiny=*/false, 13);
}

// Exception clauses alone make a body fat: two bytes of code, maxstack 8, no
// locals and no InitLocals, but one finally clause. The header has MoreSects
// (flags 0x0B); the small section follows the code at 16, after two zero
// bytes of padding.
TEST(CLIMethodBodyTest, ClausesAloneForceAFatHeader) {
  ByteVec Code = {0x00, 0x2A};
  ExceptionClause C = clause(ExceptionClauseKind::Finally, 0, 1, 1, 1);
  MethodBodyInfo I = info(Code, 8, 0, false, ArrayRef<ExceptionClause>(C));
  ByteVec B = encode(I);
  EXPECT_EQ(B, cat({fatHeader(0x0B, 8, 2, 0),
                    Code,
                    {0x00, 0x00},
                    {0x01, 0x10, 0x00, 0x00},
                    smallClause(C)}));
  expectDecodes(B, I, /*IsTiny=*/false, 32);
}

TEST(CLIMethodBodyTest, LocalsAndInitLocals) {
  ByteVec Code = {0x16, 0x0A, 0x2A}; // ldc.i4.0 stloc.0 ret
  ByteVec B = encode(info(Code, 1, 0x11000001));
  EXPECT_EQ(B, cat({fatHeader(0x3, 1, 3, 0x11000001), Code}));
  expectDecodes(B, info(Code, 1, 0x11000001), /*IsTiny=*/false, 15);

  B = encode(info(Code, 1, 0x11000001, /*InitLocals=*/true));
  EXPECT_EQ(B, cat({fatHeader(0x13, 1, 3, 0x11000001), Code}));
  expectDecodes(B, info(Code, 1, 0x11000001, true), /*IsTiny=*/false, 15);

  // hello-ilasm.mdump.txt, 'square': ilasm sets InitLocals without locals.
  ByteVec Square = {0x02, 0x02, 0x5A, 0x2A};
  B = encode(info(Square, 2, 0, /*InitLocals=*/true));
  EXPECT_EQ(B, fromHex("13 30 02 00 04 00 00 00 00 00 00 00 02 02 5A 2A"));
  expectDecodes(B, info(Square, 2, 0, true), /*IsTiny=*/false, 16);
}

// Fat bodies start 4-aligned with zero padding; tiny ones are not aligned.
TEST(CLIMethodBodyTest, MixedBodiesAlignment) {
  MethodBodyStream S;
  ByteVec Ret = {0x2A}, Two = {0x17, 0x2A};
  EXPECT_EQ(S.addBody(info(Two)), 0u);                 // 3 bytes
  EXPECT_EQ(S.addBody(info(Ret, 9)), 4u);              // pad 1, 13 bytes
  EXPECT_EQ(S.addBody(info(Two)), 17u);                // 3 bytes, unaligned
  EXPECT_EQ(S.addBody(info(Ret)), 20u);                // 2 bytes
  EXPECT_EQ(S.addBody(info(Ret, 1, 0x11000002)), 24u); // pad 2, 13 bytes
  EXPECT_EQ(S.addBody(info(Ret)), 37u);
  EXPECT_EQ(S.size(), 39u);
  ByteVec Want = cat({{0x0A, 0x17, 0x2A, 0x00},
                      fatHeader(0x3, 9, 1, 0),
                      Ret,
                      {0x0A, 0x17, 0x2A},
                      {0x06, 0x2A, 0x00, 0x00},
                      fatHeader(0x3, 1, 1, 0x11000002),
                      Ret,
                      {0x06, 0x2A}});
  EXPECT_EQ(toVec(S.getBytes()), Want);

  // Each body decodes at its offset.
  ArrayRef<uint8_t> All = S.getBytes();
  expectDecodes(All.drop_front(4), info(Ret, 9), false, 13);
  expectDecodes(All.drop_front(17), info(Two), true, 3);
  expectDecodes(All.drop_front(24), info(Ret, 1, 0x11000002), false, 13);
}

//===----------------------------------------------------------------------===//
// Exception sections
//===----------------------------------------------------------------------===//

std::vector<ExceptionClause> manyClauses(unsigned N) {
  std::vector<ExceptionClause> R;
  for (unsigned I = 0; I != N; ++I)
    R.push_back(
        clause(ExceptionClauseKind::Catch, I, 1, 30 + I, 1, 0x01000001 + I));
  return R;
}

// Small when n <= 20, offsets <= 0xFFFF and lengths <= 0xFF: 01, DataSize
// 12n+4, 00 00, 12-byte clauses. Fat otherwise: 41, 24-bit DataSize 24n+4,
// 24-byte clauses. Either follows the code, 4-aligned, and the header has
// MoreSects.
TEST(CLIMethodBodyTest, SmallAndFatSectionsByClauseCount) {
  ByteVec Code = nops(64);
  std::vector<ExceptionClause> C20 = manyClauses(20), C21 = manyClauses(21);

  ByteVec Small =
      cat({fatHeader(0x0B, 2, 64, 0), Code, {0x01, 0xF4, 0x00, 0x00}});
  for (const ExceptionClause &C : C20)
    Small = cat({Small, smallClause(C)});
  ByteVec B = encode(info(Code, 2, 0, false, C20));
  EXPECT_EQ(B.size(), 76u + 244u);
  EXPECT_EQ(B, Small);
  expectDecodes(B, info(Code, 2, 0, false, C20), false, 320);

  ByteVec Fat =
      cat({fatHeader(0x0B, 2, 64, 0), Code, {0x41, 0xFC, 0x01, 0x00}});
  for (const ExceptionClause &C : C21)
    Fat = cat({Fat, fatClause(C)});
  B = encode(info(Code, 2, 0, false, C21));
  EXPECT_EQ(B.size(), 76u + 508u);
  EXPECT_EQ(B, Fat);
  expectDecodes(B, info(Code, 2, 0, false, C21), false, 584);
}

TEST(CLIMethodBodyTest, SmallAndFatSectionsByOffsetAndLength) {
  ByteVec Code = nops(0x10100);
  // The section starts right after the header and code (0x1010C is aligned).
  const size_t SectionAt = 12 + 0x10100;
  struct {
    ExceptionClause C;
    bool Small;
  } Cases[] = {
      {clause(ExceptionClauseKind::Finally, 0xFF00, 0xFF, 0xFFFF, 0xFF), true},
      {clause(ExceptionClauseKind::Finally, 0xFFFF, 1, 0xFF00, 0xFF), true},
      {clause(ExceptionClauseKind::Finally, 0x10000, 1, 0xFF00, 1), false},
      {clause(ExceptionClauseKind::Finally, 0xFF00, 1, 0x10000, 1), false},
      {clause(ExceptionClauseKind::Fault, 0x100, 0x100, 0x200, 1), false},
      {clause(ExceptionClauseKind::Fault, 0x100, 1, 0x200, 0x100), false},
      {clause(ExceptionClauseKind::Filter, 0x100, 0xFF, 0x300, 0xFF, 0x280),
       true},
  };
  for (const auto &T : Cases) {
    SCOPED_TRACE("try " + std::to_string(T.C.TryOffset) + "+" +
                 std::to_string(T.C.TryLength) + " handler " +
                 std::to_string(T.C.HandlerOffset) + "+" +
                 std::to_string(T.C.HandlerLength));
    ArrayRef<ExceptionClause> One(T.C);
    ByteVec B = encode(info(Code, 1, 0, false, One));
    ByteVec Header = fatHeader(0x0B, 1, 0x10100, 0);
    ASSERT_GE(B.size(), SectionAt + 4);
    EXPECT_EQ(ByteVec(B.begin(), B.begin() + 12), Header);
    ByteVec Section(B.begin() + SectionAt, B.end());
    if (T.Small)
      EXPECT_EQ(Section, cat({{0x01, 0x10, 0x00, 0x00}, smallClause(T.C)}));
    else
      EXPECT_EQ(Section, cat({{0x41, 0x1C, 0x00, 0x00}, fatClause(T.C)}));
    expectDecodes(B, info(Code, 1, 0, false, One), false,
                  SectionAt + (T.Small ? 16 : 28));
  }
}

// eh.il assembled by Framework ilasm: a catch nested in a try/finally, a
// filter and a fault; .locals init (int32); four small clauses, innermost
// first. SRM reads: catch try 0+4 handler 4+3 class 0x01000001, finally try
// 0+9 handler 9+1, filter try 10+2 handler 16+3 filter 12, fault try 19+2
// handler 21+1.
TEST(CLIMethodBodyTest, IlasmSmallSection) {
  ByteVec Body = fromHex(
      "1B 30 02 00 18 00 00 00 01 00 00 11 02 0A DE 03 26 DE 00 DE 01 DC DE "
      "07 26 17 FE 11 26 DE 00 DE 01 DC 06 2A 01 34 00 00 00 00 00 00 04 04 "
      "00 03 01 00 00 01 02 00 00 00 09 09 00 01 00 00 00 00 01 00 0A 00 02 "
      "10 00 03 0C 00 00 00 04 00 13 00 02 15 00 01 00 00 00 00");
  ASSERT_EQ(Body.size(), 88u);
  ByteVec Code(Body.begin() + 12, Body.begin() + 36);
  std::vector<ExceptionClause> Clauses = {
      clause(ExceptionClauseKind::Catch, 0, 4, 4, 3, 0x01000001),
      clause(ExceptionClauseKind::Finally, 0, 9, 9, 1),
      clause(ExceptionClauseKind::Filter, 10, 2, 16, 3, 12),
      clause(ExceptionClauseKind::Fault, 19, 2, 21, 1),
  };
  MethodBodyInfo B = info(Code, 2, 0x11000001, true, Clauses);
  expectDecodes(Body, B, false, 88);
  EXPECT_EQ(encode(B), Body);
  // Bytes after the body (the next body, padding) are not part of it.
  ByteVec More = cat({Body, {0x06, 0x2A, 0xFF, 0xFF}});
  expectDecodes(More, B, false, 88);
}

// ehfat.il: a try block of 300 nops and a long leave (TryLength 305 > 0xFF),
// so ilasm writes a fat section.
TEST(CLIMethodBodyTest, IlasmFatSection) {
  ByteVec Code =
      cat({ByteVec(300, 0x00), fromHex("DD 06 00 00 00 26 DD 00 00 00 00 2A")});
  ByteVec Body = cat({fromHex("0B 30 01 00 38 01 00 00 00 00 00 00"), Code,
                      fromHex("41 1C 00 00 00 00 00 00 00 00 00 00 31 01 00 "
                              "00 31 01 00 00 06 00 00 00 01 00 00 01")});
  ASSERT_EQ(Body.size(), 352u);
  ExceptionClause C =
      clause(ExceptionClauseKind::Catch, 0, 305, 305, 6, 0x01000001);
  MethodBodyInfo B = info(Code, 1, 0, false, ArrayRef<ExceptionClause>(C));
  expectDecodes(Body, B, false, 352);
  EXPECT_EQ(encode(B), Body);
}

// hello-ilasm.mdump.txt, 'main': fat header 0B 30 03 00 73 01 00 00 02 00 00
// 11 (MoreSects, maxstack 3, 371 bytes, locals 0x11000002), one filter
// clause (try 48+7, handler 59+7, filter 55); SRM reported a 400-byte body.
TEST(CLIMethodBodyTest, HelloMainHeader) {
  ByteVec Code = nops(371);
  ExceptionClause C = clause(ExceptionClauseKind::Filter, 48, 7, 59, 7, 55);
  MethodBodyInfo B =
      info(Code, 3, 0x11000002, false, ArrayRef<ExceptionClause>(C));
  ByteVec Bytes = encode(B);
  EXPECT_EQ(Bytes.size(), 400u);
  EXPECT_EQ(ByteVec(Bytes.begin(), Bytes.begin() + 12),
            fromHex("0B 30 03 00 73 01 00 00 02 00 00 11"));
  EXPECT_EQ(Bytes[383], 0x00) << "padding before the section";
  EXPECT_EQ(ByteVec(Bytes.begin() + 384, Bytes.end()),
            cat({{0x01, 0x10, 0x00, 0x00}, smallClause(C)}));
  expectDecodes(Bytes, B, false, 400);
}

// A hand-built body with two EH sections: a small one with MoreSects (0x81)
// and a fat one (0x41).
TEST(CLIMethodBodyTest, DecodeMoreSectsChain) {
  ByteVec Code = nops(8);
  ExceptionClause A = clause(ExceptionClauseKind::Finally, 0, 2, 2, 2);
  ExceptionClause B =
      clause(ExceptionClauseKind::Catch, 0, 4, 4, 3, 0x01000005);
  ExceptionClause C = clause(ExceptionClauseKind::Filter, 0, 1, 2, 1, 1);
  ByteVec Body = cat({fatHeader(0x0B, 1, 8, 0),
                      Code,
                      {0x81, 0x1C, 0x00, 0x00},
                      smallClause(A),
                      smallClause(B),
                      {0x41, 0x1C, 0x00, 0x00},
                      fatClause(C)});
  std::vector<ExceptionClause> All = {A, B, C};
  expectDecodes(Body, info(Code, 1, 0, false, All), false, Body.size());

  // An optional IL table section anywhere in the chain is an error.
  expectInvalidBody(cat({fatHeader(0x0B, 1, 8, 0),
                         Code,
                         {0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}));
  expectInvalidBody(cat({fatHeader(0x0B, 1, 8, 0),
                         Code,
                         {0x81, 0x10, 0x00, 0x00},
                         smallClause(A),
                         {0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}));
  expectInvalidBody(cat({fatHeader(0x0B, 1, 8, 0),
                         Code,
                         {0x42, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}));
  // The error names the cause and the offset (the section at 0x14).
  std::string Msg = invalidBodyMessage(
      cat({fatHeader(0x0B, 1, 8, 0),
           Code,
           {0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}));
  EXPECT_THAT(Msg, testing::HasSubstr("table"));
  EXPECT_THAT(Msg, testing::HasSubstr("offset 0x"));
}

TEST(CLIMethodBodyTest, DecodeErrors) {
  expectInvalidBody({});
  // Format bits 00 and 01 are neither tiny nor fat.
  expectInvalidBody(ByteVec{0x04, 0x2A});
  expectInvalidBody(ByteVec{0x05, 0x2A});
  // A tiny header promising 10 bytes of code.
  expectInvalidBody(ByteVec{0x2A, 0x00, 0x00});
  // A truncated fat header.
  expectInvalidBody(ByteVec{0x03, 0x30, 0x08, 0x00, 0x01, 0x00});
  // Fat code past the end; the error names the cause.
  std::string Msg = invalidBodyMessage(cat({fatHeader(0x3, 8, 16, 0), {0x2A}}));
  EXPECT_THAT(Msg, testing::HasSubstr("code"));
  // MoreSects without a section, and truncated sections.
  ByteVec Code = nops(4);
  expectInvalidBody(cat({fatHeader(0x0B, 1, 4, 0), Code}));
  expectInvalidBody(cat({fatHeader(0x0B, 1, 4, 0), Code, {0x01, 0x10}}));
  expectInvalidBody(
      cat({fatHeader(0x0B, 1, 4, 0), Code, {0x01, 0x10, 0x00, 0x00, 0x00}}));
  expectInvalidBody(cat({fatHeader(0x0B, 1, 4, 0),
                         Code,
                         {0x41, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}));
  // A section claiming far more data than there is.
  expectInvalidBody(
      cat({fatHeader(0x0B, 1, 4, 0), Code, {0x41, 0xFC, 0xFF, 0xFF}}));
}

// One fat section's 24-bit DataSize (24n + 4) holds at most 699,050 clauses,
// (0xFFFFFF - 4) / 24, and addBody writes every clause into one section, so
// decodeMethodBody rejects a body whose sections, MoreSects chain included,
// hold more: whatever it accepts, addBody re-encodes (section 12 '5.7.5
// clarified (EH clause count)'). The code is nop endfinally nop ret, and
// every clause is a finally with try 0+1 and handler 1+1. The bodies are
// about 16 MB.
TEST(CLIMethodBodyTest, EHClauseCountLimit) {
  const uint32_t Max = 699050;
  ASSERT_EQ(Max, (0xFFFFFFu - 4) / 24);
  ByteVec Code = {0x00, 0xDC, 0x00, 0x2A};
  ExceptionClause C = clause(ExceptionClauseKind::Finally, 0, 1, 1, 1);
  const ByteVec Fat = fatClause(C), Small = smallClause(C);
  auto append = [](ByteVec &To, const ByteVec &Bytes) {
    To.insert(To.end(), Bytes.begin(), Bytes.end());
  };

  // The boundary: one fat section of 699,050 clauses, DataSize 0xFFFFF4.
  ByteVec B = cat({fatHeader(0x0B, 1, 4, 0), Code, {0x41, 0xF4, 0xFF, 0xFF}});
  B.reserve(B.size() + size_t(24) * Max + 16);
  for (uint32_t I = 0; I != Max; ++I)
    append(B, Fat);
  ASSERT_EQ(B.size(), 20u + 24u * Max);
  {
    Expected<MethodBodyView> V = decodeMethodBody(B);
    ASSERT_THAT_EXPECTED(V, Succeeded());
    EXPECT_FALSE(V->IsTiny);
    EXPECT_EQ(V->MaxStack, 1u);
    EXPECT_EQ(V->LocalVarSigToken, 0u);
    EXPECT_FALSE(V->InitLocals);
    EXPECT_EQ(toVec(V->Code), Code);
    EXPECT_EQ(V->Size, B.size());
    ASSERT_EQ(V->Clauses.size(), size_t(Max));
    expectClause(V->Clauses.front(), C);
    expectClause(V->Clauses.back(), C);
    size_t Different = 0;
    for (const ExceptionClause &Got : V->Clauses)
      if (Got.Kind != C.Kind || Got.TryOffset != C.TryOffset ||
          Got.TryLength != C.TryLength ||
          Got.HandlerOffset != C.HandlerOffset ||
          Got.HandlerLength != C.HandlerLength ||
          Got.ClassTokenOrFilterOffset != C.ClassTokenOrFilterOffset)
        ++Different;
    EXPECT_EQ(Different, 0u);

    // addBody of what decoded writes the same bytes.
    MethodBodyInfo Again;
    Again.Code = V->Code;
    Again.MaxStack = V->MaxStack;
    Again.LocalVarSigToken = V->LocalVarSigToken;
    Again.InitLocals = V->InitLocals;
    Again.Clauses = V->Clauses;
    MethodBodyStream S;
    EXPECT_EQ(S.addBody(Again), 0u);
    EXPECT_EQ(S.size(), B.size());
    EXPECT_TRUE(S.getBytes() == ArrayRef<uint8_t>(B))
        << "addBody did not re-encode the decoded body byte for byte";
  }

  // One more: the same section with MoreSects (0xC1), followed by a small
  // section holding one clause, 699,051 clauses in all.
  B[16] = 0xC1;
  append(B, {0x01, 0x10, 0x00, 0x00});
  append(B, Small);
  EXPECT_THAT(invalidBodyMessage(B), testing::HasSubstr("clause"));

  // A chain of small sections: 34,953 sections of 20 clauses, 699,060 in all.
  B = cat({fatHeader(0x0B, 1, 4, 0), Code});
  B.reserve(B.size() + size_t(34953) * 244);
  for (unsigned S = 0; S != 34953; ++S) {
    append(B, {uint8_t(S != 34952 ? 0x81 : 0x01), 0xF4, 0x00, 0x00});
    for (unsigned I = 0; I != 20; ++I)
      append(B, Small);
  }
  EXPECT_THAT(invalidBodyMessage(B), testing::HasSubstr("clause"));
}

} // namespace
