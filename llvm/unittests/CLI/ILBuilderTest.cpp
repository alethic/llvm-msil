//===- ILBuilderTest.cpp - CIL method body builder ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/ILBuilder.h (plan 5.7.5): the shortest index and constant
// forms of MSILInstPrinter::printShortForm (Q26), branch relaxation (metadata-
// writer.md 5.2: every branch starts short and widens to a fixed point;
// switch entries are always 32-bit), the ECMA-335 III.1.7.5 stack depth with
// handler and dead-code rules, the maxstack limit of 65535 (plan 5.7.12),
// every layout error, token resolution, and E1's 371-byte 'main'
// (metadata-writer.md 3.2) rebuilt instruction by instruction equal to the
// bytes ilasm wrote (hello-ilasm.mdump.txt). Expected bytes come
// from ECMA-335, from the recon's Python prototype (cliwriter.IL, which
// relaxes the same way; run in scratch for the relaxation cases) and from
// ilasm output read with System.Reflection.Metadata, never from the
// implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/ILBuilder.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/Handles.h"
#include "llvm/Support/Error.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <algorithm>
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
  SmallVector<StringRef, 512> Parts;
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

TokenRef tok(uint32_t T) { return TokenRef::getToken(T); }

// Resolves final tokens to themselves (and an absent catch type to 0); a
// handle or external ref is a test bug.
Expected<uint32_t> resolveTokens(TokenRef T) {
  if (T.getKind() == TokenRef::Kind::Token ||
      T.getKind() == TokenRef::Kind::None)
    return T.getValue();
  return createStringError(inconvertibleErrorCode(), "unexpected TokenRef");
}

// Encodes a laid-out builder.
ByteVec encodeCode(const ILBuilder &B,
                   std::vector<ExceptionClause> *Clauses = nullptr) {
  SmallVector<uint8_t, 64> Code;
  SmallVector<ExceptionClause, 4> C;
  EXPECT_THAT_ERROR(B.encode(Code, C, resolveTokens), Succeeded());
  EXPECT_EQ(Code.size(), B.getCodeSize());
  if (Clauses)
    Clauses->assign(C.begin(), C.end());
  return toVec(Code);
}

// Lays out and encodes B; an empty vector if layout fails.
ByteVec build(ILBuilder &B, std::vector<ExceptionClause> *Clauses = nullptr) {
  if (Error E = B.layout()) {
    ADD_FAILURE() << "layout failed: " << toString(std::move(E));
    return {};
  }
  return encodeCode(B, Clauses);
}

void expectLayoutError(ILBuilder &B) {
  EXPECT_THAT_ERROR(
      B.layout(), FailedWithMessage(testing::StartsWith("invalid IL body: ")));
}

// The message of a layout error, which starts with the prefix; lowercased, so
// that callers match words of the cause in any case.
std::string layoutErrorMessage(ILBuilder &B) {
  Error E = B.layout();
  if (!E) {
    ADD_FAILURE() << "layout succeeded";
    return "";
  }
  std::string Msg = toString(std::move(E));
  EXPECT_THAT(Msg, testing::StartsWith("invalid IL body: "));
  return StringRef(Msg).lower();
}

unsigned value(Opcode Op) { return static_cast<unsigned>(Op); }

void nops(ILBuilder &B, unsigned N) {
  for (unsigned I = 0; I != N; ++I)
    B.emit(Opcode::Nop);
}

ILExceptionClause ehClause(ExceptionClauseKind K, ILLabel TryBegin,
                           ILLabel TryEnd, ILLabel HandlerBegin,
                           ILLabel HandlerEnd, ILLabel FilterBegin = ILLabel(),
                           TokenRef CatchType = TokenRef()) {
  ILExceptionClause C;
  C.Kind = K;
  C.TryBegin = TryBegin;
  C.TryEnd = TryEnd;
  C.HandlerBegin = HandlerBegin;
  C.HandlerEnd = HandlerEnd;
  C.FilterBegin = FilterBegin;
  C.CatchType = CatchType;
  return C;
}

void expectClause(const ExceptionClause &Got, ExceptionClauseKind K,
                  uint32_t TryOff, uint32_t TryLen, uint32_t HOff,
                  uint32_t HLen, uint32_t Extra) {
  EXPECT_EQ(static_cast<uint32_t>(Got.Kind), static_cast<uint32_t>(K));
  EXPECT_EQ(Got.TryOffset, TryOff);
  EXPECT_EQ(Got.TryLength, TryLen);
  EXPECT_EQ(Got.HandlerOffset, HOff);
  EXPECT_EQ(Got.HandlerLength, HLen);
  EXPECT_EQ(Got.ClassTokenOrFilterOffset, Extra);
}

ByteVec le16(unsigned V) { return {uint8_t(V), uint8_t(V >> 8)}; }

//===----------------------------------------------------------------------===//
// Shortest forms (Q26)
//===----------------------------------------------------------------------===//

enum class IndexOp { Ldarg, Ldarga, Starg, Ldloc, Ldloca, Stloc };

// The bytes of the shortest form: '.N' opcodes for 0-3 (ldarg, ldloc,
// stloc only), '.s' below 256, else the two-byte long form.
ByteVec shortestIndexForm(IndexOp K, unsigned N) {
  struct Forms {
    int Dot0;      // opcode of '.0', or -1
    uint8_t Short; // the '.s' opcode
    uint8_t Long;  // second byte after FE
  };
  static const Forms Table[] = {
      {0x02, 0x0E, 0x09}, // ldarg
      {-1, 0x0F, 0x0A},   // ldarga
      {-1, 0x10, 0x0B},   // starg
      {0x06, 0x11, 0x0C}, // ldloc
      {-1, 0x12, 0x0D},   // ldloca
      {0x0A, 0x13, 0x0E}, // stloc
  };
  const Forms &F = Table[static_cast<int>(K)];
  if (F.Dot0 >= 0 && N < 4)
    return {uint8_t(F.Dot0 + N)};
  if (N < 256)
    return {F.Short, uint8_t(N)};
  return cat({{0xFE, F.Long}, le16(N)});
}

// Emits the index instruction so that the stack stays balanced.
void emitIndexed(ILBuilder &B, IndexOp K, unsigned N) {
  switch (K) {
  case IndexOp::Ldarg:
    B.emitLdarg(N);
    B.emit(Opcode::Pop);
    break;
  case IndexOp::Ldarga:
    B.emitLdarga(N);
    B.emit(Opcode::Pop);
    break;
  case IndexOp::Starg:
    B.emitLdcI4(0);
    B.emitStarg(N);
    break;
  case IndexOp::Ldloc:
    B.emitLdloc(N);
    B.emit(Opcode::Pop);
    break;
  case IndexOp::Ldloca:
    B.emitLdloca(N);
    B.emit(Opcode::Pop);
    break;
  case IndexOp::Stloc:
    B.emitLdcI4(0);
    B.emitStloc(N);
    break;
  }
}

TEST(CLIILBuilderTest, ShortestIndexForms) {
  const char *Names[] = {"ldarg", "ldarga", "starg",
                         "ldloc", "ldloca", "stloc"};
  for (int K = 0; K != 6; ++K) {
    for (unsigned N : {0u, 1u, 2u, 3u, 4u, 255u, 256u, 65534u}) {
      SCOPED_TRACE(std::string(Names[K]) + " " + std::to_string(N));
      IndexOp Op = static_cast<IndexOp>(K);
      ILBuilder B(/*ReturnsValue=*/false);
      emitIndexed(B, Op, N);
      B.emitRet();
      ByteVec Form = shortestIndexForm(Op, N);
      bool Store = Op == IndexOp::Starg || Op == IndexOp::Stloc;
      ByteVec Want =
          Store ? cat({{0x16}, Form, {0x2A}}) : cat({Form, {0x26, 0x2A}});
      EXPECT_EQ(build(B), Want);
      ASSERT_EQ(B.instructions().size(), 3u);
      const ILInstr &I = B.instructions()[Store ? 1 : 0];
      unsigned Encoded = Form.size() == 1   ? Form[0]
                         : Form.size() == 2 ? Form[0]
                                            : 0xFE00 | Form[1];
      EXPECT_EQ(value(I.Op), Encoded);
      EXPECT_EQ(I.Offset, Store ? 1u : 0u);
      if (Form.size() > 1)
        EXPECT_EQ(I.Imm, int64_t(N));
    }
  }
}

TEST(CLIILBuilderTest, IndexAbove65534IsALayoutError) {
  for (int K = 0; K != 6; ++K) {
    SCOPED_TRACE(K);
    ILBuilder B(/*ReturnsValue=*/false);
    emitIndexed(B, static_cast<IndexOp>(K), 65535);
    B.emitRet();
    expectLayoutError(B);
  }
  ILBuilder B(/*ReturnsValue=*/false);
  B.emitLdloca(70000);
  B.emit(Opcode::Pop);
  B.emitRet();
  expectLayoutError(B);
}

TEST(CLIILBuilderTest, ShortestLdcI4) {
  struct {
    int32_t V;
    ByteVec Bytes;
  } Cases[] = {
      {-1, {0x15}},
      {0, {0x16}},
      {1, {0x17}},
      {7, {0x1D}},
      {8, {0x1E}},
      {9, {0x1F, 0x09}},
      {-2, {0x1F, 0xFE}},
      {-128, {0x1F, 0x80}},
      {127, {0x1F, 0x7F}},
      {128, {0x20, 0x80, 0x00, 0x00, 0x00}},
      {-129, {0x20, 0x7F, 0xFF, 0xFF, 0xFF}},
      {INT32_MAX, {0x20, 0xFF, 0xFF, 0xFF, 0x7F}},
      {INT32_MIN, {0x20, 0x00, 0x00, 0x00, 0x80}},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.V);
    ILBuilder B(/*ReturnsValue=*/true);
    B.emitLdcI4(C.V);
    B.emitRet();
    EXPECT_EQ(build(B), cat({C.Bytes, {0x2A}}));
    EXPECT_EQ(B.getMaxStack(), 1u);
  }
}

// emitImm and the float emitters keep the opcode they are given.
TEST(CLIILBuilderTest, ImmediatesAsGiven) {
  ILBuilder B(/*ReturnsValue=*/false);
  B.emitImm(Opcode::Ldc_i4, 72);
  B.emitImm(Opcode::Ldc_i4_s, -5);
  B.emitImm(Opcode::Ldc_i8, 0x123456789ABCDEF0);
  B.emitImm(Opcode::Ldarg_s, 1);
  B.emitImm(Opcode::Ldloc, 2);
  B.emitR4(0x7F800001); // a signaling NaN, kept exactly
  B.emitR8(0x7FF0000000000001);
  for (int I = 0; I != 7; ++I)
    B.emit(Opcode::Pop);
  B.emitLdcI4(0);
  B.emitImm(Opcode::Unaligned, 1);
  B.emit(Opcode::Volatile);
  B.emit(Opcode::Ldind_i4);
  B.emit(Opcode::Pop);
  B.emitRet();
  EXPECT_EQ(build(B),
            fromHex("20 48 00 00 00 1F FB 21 F0 DE BC 9A 78 56 34 12 0E 01 "
                    "FE 0C 02 00 22 01 00 80 7F 23 01 00 00 00 00 00 F0 7F "
                    "26 26 26 26 26 26 26 16 FE 12 01 FE 13 4A 26 2A"));
  ArrayRef<ILInstr> I = B.instructions();
  ASSERT_EQ(I.size(), 20u);
  EXPECT_EQ(I[0].Op, Opcode::Ldc_i4);
  EXPECT_EQ(I[0].Imm, 72);
  EXPECT_EQ(I[1].Imm, -5);
  EXPECT_EQ(I[2].Imm, 0x123456789ABCDEF0);
  EXPECT_EQ(I[3].Op, Opcode::Ldarg_s);
  EXPECT_EQ(I[4].Op, Opcode::Ldloc);
  EXPECT_EQ(I[5].FPBits & 0xFFFFFFFF, 0x7F800001u);
  EXPECT_EQ(I[6].FPBits, 0x7FF0000000000001u);
  EXPECT_EQ(I[15].Op, Opcode::Unaligned) << "prefixes are instructions";
  EXPECT_EQ(I[15].Imm, 1);
  EXPECT_EQ(I[15].Offset, 44u);
  EXPECT_EQ(I[16].Op, Opcode::Volatile);
  EXPECT_EQ(I[16].Offset, 47u);
  EXPECT_EQ(I[17].Offset, 49u);
  EXPECT_EQ(B.getMaxStack(), 7u);
}

TEST(CLIILBuilderTest, TokensAndCalls) {
  ILBuilder B(/*ReturnsValue=*/true);
  B.emitToken(Opcode::Ldsfld, tok(0x04000001));        // 1
  B.emitToken(Opcode::Box, tok(0x01000002));           // 1
  B.emitToken(Opcode::Ldstr, tok(0x70000003));         // 2
  B.emitToken(Opcode::Ldtoken, tok(0x02000004));       // 3
  B.emitCall(Opcode::Newobj, tok(0x0A000005), 3, 1);   // 1
  B.emitToken(Opcode::Constrained, tok(0x1B000006));   // 1
  B.emitCall(Opcode::Callvirt, tok(0x0A000007), 1, 1); // 1
  B.emit(Opcode::Tail);
  B.emitCall(Opcode::Call, tok(0x06000008), 1, 1); // 1
  B.emitRet();
  EXPECT_EQ(build(B),
            fromHex("7E 01 00 00 04 8C 02 00 00 01 72 03 00 00 70 D0 04 00 "
                    "00 02 73 05 00 00 0A FE 16 06 00 00 1B 6F 07 00 00 0A "
                    "FE 14 28 08 00 00 06 2A"));
  EXPECT_EQ(B.getMaxStack(), 3u);
  ArrayRef<ILInstr> I = B.instructions();
  ASSERT_EQ(I.size(), 10u);
  EXPECT_TRUE(I[0].Token == tok(0x04000001));
  EXPECT_TRUE(I[4].Token == tok(0x0A000005));
  EXPECT_EQ(I[5].Op, Opcode::Constrained);
  EXPECT_EQ(I[7].Op, Opcode::Tail);
  EXPECT_EQ(I[8].Offset, 38u);
}

//===----------------------------------------------------------------------===//
// Relaxation (metadata-writer.md 5.2)
//===----------------------------------------------------------------------===//

// br over N nops to a ret: br.s while the displacement is at most 127.
TEST(CLIILBuilderTest, RelaxForward) {
  for (unsigned N : {127u, 128u}) {
    SCOPED_TRACE(N);
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L = B.createLabel();
    B.emitBranch(Opcode::Br, L);
    nops(B, N);
    B.placeLabel(L);
    B.emitRet();
    ByteVec Code = build(B);
    if (N == 127) {
      EXPECT_EQ(ByteVec(Code.begin(), Code.begin() + 2), (ByteVec{0x2B, 0x7F}));
      EXPECT_EQ(Code.size(), 130u);
      EXPECT_EQ(B.instructions()[0].Op, Opcode::Br_s);
      EXPECT_EQ(B.getLabelOffset(L), 129u);
    } else {
      EXPECT_EQ(ByteVec(Code.begin(), Code.begin() + 5),
                (ByteVec{0x38, 0x80, 0x00, 0x00, 0x00}));
      EXPECT_EQ(Code.size(), 134u);
      EXPECT_EQ(B.instructions()[0].Op, Opcode::Br);
      EXPECT_EQ(B.getLabelOffset(L), 133u);
    }
    EXPECT_EQ(B.instructions()[0].Target.Id, L.Id);
    EXPECT_EQ(B.instructions()[1].Offset, N == 127 ? 2u : 5u);
  }
  // A conditional branch, given in its short spelling.
  for (unsigned N : {127u, 128u}) {
    SCOPED_TRACE(N);
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L = B.createLabel();
    B.emitLdcI4(0);
    B.emitBranch(Opcode::Brfalse_s, L);
    nops(B, N);
    B.placeLabel(L);
    B.emitRet();
    ByteVec Code = build(B);
    ByteVec Head(Code.begin(), Code.begin() + std::min<size_t>(6, Code.size()));
    if (N == 127)
      EXPECT_EQ(Head, (ByteVec{0x16, 0x2C, 0x7F, 0x00, 0x00, 0x00}));
    else
      EXPECT_EQ(Head, (ByteVec{0x16, 0x39, 0x80, 0x00, 0x00, 0x00}));
  }
}

// A backward br after N nops: the short displacement is -(N + 2).
TEST(CLIILBuilderTest, RelaxBackward) {
  for (unsigned N : {126u, 127u}) {
    SCOPED_TRACE(N);
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L = B.createLabel();
    B.placeLabel(L);
    nops(B, N);
    B.emitBranch(Opcode::Br, L);
    B.emitRet();
    ByteVec Code = build(B);
    ByteVec Tail(Code.begin() + N, Code.end());
    if (N == 126)
      EXPECT_EQ(Tail, (ByteVec{0x2B, 0x80, 0x2A})); // -128
    else
      EXPECT_EQ(Tail, (ByteVec{0x38, 0x7C, 0xFF, 0xFF, 0xFF, 0x2A})); // -132
    EXPECT_EQ(B.getLabelOffset(L), 0u);
  }
}

TEST(CLIILBuilderTest, RelaxLeave) {
  for (unsigned N : {127u, 128u}) {
    SCOPED_TRACE(N);
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L = B.createLabel();
    B.emitBranch(Opcode::Leave, L);
    nops(B, N);
    B.placeLabel(L);
    B.emitRet();
    ByteVec Code = build(B);
    if (N == 127)
      EXPECT_EQ(ByteVec(Code.begin(), Code.begin() + 2), (ByteVec{0xDE, 0x7F}));
    else
      EXPECT_EQ(ByteVec(Code.begin(), Code.begin() + 5),
                (ByteVec{0xDD, 0x80, 0x00, 0x00, 0x00}));
  }
}

// Widening the second branch pushes the first one's target out of range:
// 0: br L1, 2: br L2, 123 nops, L1, 200 nops, L2: ret. The second branch
// widens first (displacement 323), which moves L1 from 127 to 130 and widens
// the first (displacement 128). cliwriter.IL gives the same 334 bytes.
TEST(CLIILBuilderTest, RelaxCascade) {
  ILBuilder B(/*ReturnsValue=*/false);
  ILLabel L1 = B.createLabel(), L2 = B.createLabel();
  B.emitBranch(Opcode::Br, L1);
  B.emitBranch(Opcode::Br, L2);
  nops(B, 123);
  B.placeLabel(L1);
  nops(B, 200);
  B.placeLabel(L2);
  B.emitRet();
  ByteVec Code = build(B);
  ASSERT_EQ(Code.size(), 334u);
  EXPECT_EQ(
      ByteVec(Code.begin(), Code.begin() + 10),
      (ByteVec{0x38, 0x80, 0x00, 0x00, 0x00, 0x38, 0x43, 0x01, 0x00, 0x00}));
  EXPECT_EQ(B.getLabelOffset(L1), 133u);
  EXPECT_EQ(B.getLabelOffset(L2), 333u);
  EXPECT_EQ(B.getCodeSize(), 334u);
  EXPECT_EQ(B.instructions()[0].Op, Opcode::Br);
  EXPECT_EQ(B.instructions()[1].Op, Opcode::Br);
  EXPECT_EQ(B.instructions()[1].Offset, 5u);
}

// switch has 32-bit entries relative to its end, whatever the distance.
TEST(CLIILBuilderTest, Switch) {
  {
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L0 = B.createLabel(), L1 = B.createLabel(), L2 = B.createLabel();
    B.emitLdcI4(0);
    B.emitSwitch({L0, L1, L2});
    B.placeLabel(L0);
    B.emit(Opcode::Nop);
    B.placeLabel(L1);
    B.emit(Opcode::Nop);
    B.placeLabel(L2);
    B.emitRet();
    EXPECT_EQ(build(B), fromHex("16 45 03 00 00 00 00 00 00 00 01 00 00 00 02 "
                                "00 00 00 00 00 2A"));
    const ILInstr &S = B.instructions()[1];
    EXPECT_EQ(S.Op, Opcode::Switch);
    EXPECT_EQ(S.Offset, 1u);
    EXPECT_EQ(S.SwitchCount, 3u);
    ArrayRef<ILLabel> T = B.getSwitchTargets(S);
    ASSERT_EQ(T.size(), 3u);
    EXPECT_EQ(T[0].Id, L0.Id);
    EXPECT_EQ(T[1].Id, L1.Id);
    EXPECT_EQ(T[2].Id, L2.Id);
  }
  {
    // ldc.i4.0; switch (L0, End); br End; L0: 300 nops; End: ret.
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L0 = B.createLabel(), End = B.createLabel();
    B.emitLdcI4(0);
    B.emitSwitch({L0, End});
    B.emitBranch(Opcode::Br, End);
    B.placeLabel(L0);
    nops(B, 300);
    B.placeLabel(End);
    B.emitRet();
    ByteVec Code = build(B);
    ASSERT_EQ(Code.size(), 320u);
    EXPECT_EQ(ByteVec(Code.begin(), Code.begin() + 19),
              fromHex("16 45 02 00 00 00 05 00 00 00 31 01 00 00 38 2C 01 00 "
                      "00"));
  }
  {
    // A backward switch entry.
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel Top = B.createLabel(), Out = B.createLabel();
    B.placeLabel(Top);
    B.emitLdcI4(1);
    B.emitSwitch({Out, Top});
    B.placeLabel(Out);
    B.emitRet();
    EXPECT_EQ(build(B),
              fromHex("17 45 02 00 00 00 00 00 00 00 F2 FF FF FF 2A"));
  }
}

TEST(CLIILBuilderTest, RelaxationOff) {
  {
    // A long branch stays long, a short one short.
    ILBuilder B(/*ReturnsValue=*/false);
    B.setRelaxBranches(false);
    ILLabel L = B.createLabel(), M = B.createLabel();
    B.emitBranch(Opcode::Br, L);
    B.placeLabel(L);
    B.emitBranch(Opcode::Br_s, M);
    B.placeLabel(M);
    B.emitRet();
    EXPECT_EQ(build(B), fromHex("38 00 00 00 00 2B 00 2A"));
  }
  {
    // A short branch that does not fit is an error.
    ILBuilder B(/*ReturnsValue=*/false);
    B.setRelaxBranches(false);
    ILLabel L = B.createLabel();
    B.emitBranch(Opcode::Br_s, L);
    nops(B, 200);
    B.placeLabel(L);
    B.emitRet();
    expectLayoutError(B);
  }
  {
    // With relaxation (the default) the same spellings become the right forms.
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L = B.createLabel(), M = B.createLabel();
    B.emitBranch(Opcode::Br, L);
    B.placeLabel(L);
    B.emitBranch(Opcode::Br_s, M);
    nops(B, 200);
    B.placeLabel(M);
    B.emitRet();
    ByteVec Code = build(B);
    EXPECT_EQ(ByteVec(Code.begin(), Code.begin() + 7),
              fromHex("2B 00 38 C8 00 00 00"));
  }
}

//===----------------------------------------------------------------------===//
// Stack depth (ECMA-335 III.1.7.5)
//===----------------------------------------------------------------------===//

TEST(CLIILBuilderTest, MaxStackWithCallsAndFloors) {
  {
    ILBuilder B(/*ReturnsValue=*/true);
    B.emitLdcI4(1);
    B.emitLdcI4(2);
    B.emitLdcI4(3);
    B.emitCall(Opcode::Call, tok(0x06000001), 3, 1);
    B.emitLdcI4(4);
    B.emitToken(Opcode::Ldsfld, tok(0x04000001));
    B.emitCall(Opcode::Calli, tok(0x11000001), 2, 1); // argument and pointer
    B.emitCall(Opcode::Call, tok(0x06000002), 2, 0);
    B.emitCall(Opcode::Call, tok(0x06000003), 0, 1);
    B.emitRet();
    ASSERT_THAT_ERROR(B.layout(), Succeeded());
    EXPECT_EQ(B.getMaxStack(), 3u);
  }
  {
    ILBuilder B(/*ReturnsValue=*/false);
    B.setMaxStack(5);
    B.emitLdcI4(1);
    B.emit(Opcode::Pop);
    B.emitRet();
    ASSERT_THAT_ERROR(B.layout(), Succeeded());
    EXPECT_EQ(B.getMaxStack(), 5u) << "the floor wins";
  }
  {
    ILBuilder B(/*ReturnsValue=*/false);
    B.setMaxStack(1);
    B.emitLdcI4(1);
    B.emit(Opcode::Dup);
    B.emit(Opcode::Dup);
    B.emit(Opcode::Pop);
    B.emit(Opcode::Pop);
    B.emit(Opcode::Pop);
    B.emitRet();
    ASSERT_THAT_ERROR(B.layout(), Succeeded());
    EXPECT_EQ(B.getMaxStack(), 3u) << "the computed depth wins";
  }
  {
    ILBuilder B(/*ReturnsValue=*/false);
    B.emitRet();
    ASSERT_THAT_ERROR(B.layout(), Succeeded());
    EXPECT_EQ(B.getMaxStack(), 0u);
    EXPECT_EQ(encodeCode(B), ByteVec{0x2A});
  }
}

// eh.il, which Framework ilasm assembled (see MethodBodyTest.cpp), rebuilt:
// a catch nested in a try/finally, a filter and a fault. Catch and filter
// handlers and filter blocks start at depth 1, try blocks, finally and fault
// handlers at 0. The code equals ilasm's 24 bytes, the clauses SRM's.
TEST(CLIILBuilderTest, HandlerDepthsAndClauses) {
  ILBuilder B(/*ReturnsValue=*/true);
  ILLabel T0 = B.createLabel(), C0 = B.createLabel(), L1 = B.createLabel(),
          F0 = B.createLabel(), L2 = B.createLabel(), FB = B.createLabel(),
          H2 = B.createLabel(), L3 = B.createLabel(), FL = B.createLabel(),
          L4 = B.createLabel();
  B.placeLabel(T0);
  B.emitLdarg(0);
  B.emitStloc(0);
  B.emitBranch(Opcode::Leave, L1);
  B.placeLabel(C0); // catch handler: the exception is on the stack
  B.emit(Opcode::Pop);
  B.emitBranch(Opcode::Leave, L1);
  B.placeLabel(L1);
  B.emitBranch(Opcode::Leave, L2);
  B.placeLabel(F0); // finally handler
  B.emit(Opcode::Endfinally);
  B.placeLabel(L2);
  B.emitBranch(Opcode::Leave, L3);
  B.placeLabel(FB); // filter block: the exception is on the stack
  B.emit(Opcode::Pop);
  B.emitLdcI4(1);
  B.emit(Opcode::Endfilter);
  B.placeLabel(H2); // filter handler
  B.emit(Opcode::Pop);
  B.emitBranch(Opcode::Leave, L3);
  B.placeLabel(L3);
  B.emitBranch(Opcode::Leave, L4);
  B.placeLabel(FL); // fault handler
  B.emit(Opcode::Endfinally);
  B.placeLabel(L4);
  B.emitLdloc(0);
  B.emitRet();
  B.addExceptionClause(ehClause(ExceptionClauseKind::Catch, T0, C0, C0, L1,
                                ILLabel(), tok(0x01000001)));
  B.addExceptionClause(ehClause(ExceptionClauseKind::Finally, T0, F0, F0, L2));
  B.addExceptionClause(
      ehClause(ExceptionClauseKind::Filter, L2, FB, H2, L3, FB));
  B.addExceptionClause(ehClause(ExceptionClauseKind::Fault, L3, FL, FL, L4));
  std::vector<ExceptionClause> Clauses;
  EXPECT_EQ(build(B, &Clauses),
            fromHex("02 0A DE 03 26 DE 00 DE 01 DC DE 07 26 17 FE 11 26 DE 00 "
                    "DE 01 DC 06 2A"));
  EXPECT_EQ(B.getMaxStack(), 1u);
  ASSERT_EQ(Clauses.size(), 4u);
  expectClause(Clauses[0], ExceptionClauseKind::Catch, 0, 4, 4, 3, 0x01000001);
  expectClause(Clauses[1], ExceptionClauseKind::Finally, 0, 9, 9, 1, 0);
  expectClause(Clauses[2], ExceptionClauseKind::Filter, 10, 2, 16, 3, 12);
  expectClause(Clauses[3], ExceptionClauseKind::Fault, 19, 2, 21, 1, 0);
}

TEST(CLIILBuilderTest, FinallyAndFaultStartEmpty) {
  for (ExceptionClauseKind K :
       {ExceptionClauseKind::Finally, ExceptionClauseKind::Fault}) {
    SCOPED_TRACE(static_cast<int>(K));
    {
      // try { leave Out } finally { ldc.i4.1 ldc.i4.1 pop pop endfinally }
      ILBuilder B(/*ReturnsValue=*/false);
      ILLabel T = B.createLabel(), H = B.createLabel(), E = B.createLabel(),
              Out = B.createLabel();
      B.placeLabel(T);
      B.emitBranch(Opcode::Leave, Out);
      B.placeLabel(H);
      B.emitLdcI4(1);
      B.emitLdcI4(1);
      B.emit(Opcode::Pop);
      B.emit(Opcode::Pop);
      B.emit(Opcode::Endfinally);
      B.placeLabel(E);
      B.placeLabel(Out);
      B.emitRet();
      B.addExceptionClause(ehClause(K, T, H, H, E));
      ASSERT_THAT_ERROR(B.layout(), Succeeded());
      EXPECT_EQ(B.getMaxStack(), 2u);
    }
    {
      // A handler that pops first underflows.
      ILBuilder B(/*ReturnsValue=*/false);
      ILLabel T = B.createLabel(), H = B.createLabel(), E = B.createLabel(),
              Out = B.createLabel();
      B.placeLabel(T);
      B.emitBranch(Opcode::Leave, Out);
      B.placeLabel(H);
      B.emit(Opcode::Pop);
      B.emit(Opcode::Endfinally);
      B.placeLabel(E);
      B.placeLabel(Out);
      B.emitRet();
      B.addExceptionClause(ehClause(K, T, H, H, E));
      expectLayoutError(B);
    }
  }
  {
    // The same handler as a catch is fine.
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel T = B.createLabel(), H = B.createLabel(), E = B.createLabel();
    B.placeLabel(T);
    B.emitBranch(Opcode::Leave, E);
    B.placeLabel(H);
    B.emit(Opcode::Pop);
    B.emitBranch(Opcode::Leave, E);
    B.placeLabel(E);
    B.emitRet();
    B.addExceptionClause(ehClause(ExceptionClauseKind::Catch, T, H, H, E,
                                  ILLabel(), tok(0x01000001)));
    ASSERT_THAT_ERROR(B.layout(), Succeeded());
    EXPECT_EQ(B.getMaxStack(), 1u);
  }
}

// After br, leave, ret, throw, endfinally, endfilter, rethrow or jmp the
// depth is what a forward branch recorded for a label placed there, else 0.
TEST(CLIILBuilderTest, DeadCodeAndRecordedDepths) {
  {
    ILBuilder B(/*ReturnsValue=*/true);
    B.emitLdcI4(1);
    B.emitRet();
    B.emitLdcI4(2); // unreachable, starts at depth 0
    B.emitRet();
    EXPECT_EQ(build(B), fromHex("17 2A 18 2A"));
    EXPECT_EQ(B.getMaxStack(), 1u);
  }
  {
    // Dead code that pops underflows: it starts at depth 0.
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L = B.createLabel();
    B.emitLdcI4(1);
    B.emitBranch(Opcode::Br, L);
    B.emit(Opcode::Pop);
    B.placeLabel(L);
    B.emit(Opcode::Pop);
    B.emitRet();
    expectLayoutError(B);
  }
  {
    // A label right after br takes the depth a forward branch recorded.
    // 0: ldc.i4.7 1: ldc.i4.1 2: brtrue.s L 4: pop 5: ldc.i4.3 6: br.s L
    // 8: L: ret
    ILBuilder B(/*ReturnsValue=*/true);
    ILLabel L = B.createLabel();
    B.emitLdcI4(7);
    B.emitLdcI4(1);
    B.emitBranch(Opcode::Brtrue, L);
    B.emit(Opcode::Pop);
    B.emitLdcI4(3);
    B.emitBranch(Opcode::Br, L);
    B.placeLabel(L);
    B.emitRet();
    EXPECT_EQ(build(B), fromHex("1D 17 2D 04 26 19 2B 00 2A"));
    EXPECT_EQ(B.getMaxStack(), 2u);
  }
  {
    // A label after br that only a backward branch targets starts empty.
    // 0: br.s L2 2: L1: ldc.i4.0 3: pop 4: ret 5: L2: br.s L1
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L1 = B.createLabel(), L2 = B.createLabel();
    B.emitBranch(Opcode::Br, L2);
    B.placeLabel(L1);
    B.emitLdcI4(0);
    B.emit(Opcode::Pop);
    B.emitRet();
    B.placeLabel(L2);
    B.emitBranch(Opcode::Br, L1);
    EXPECT_EQ(build(B), fromHex("2B 03 16 26 2A 2B FB"));
  }
  {
    // throw, then code at depth 0.
    ILBuilder B(/*ReturnsValue=*/false);
    B.emit(Opcode::Ldnull);
    B.emit(Opcode::Throw);
    B.emitLdcI4(5);
    B.emit(Opcode::Pop);
    B.emitRet();
    EXPECT_EQ(build(B), fromHex("14 7A 1B 26 2A"));
    EXPECT_EQ(B.getMaxStack(), 1u);
  }
}

// maxstack <= 65535 (plan 5.7.12; MethodBodyInfo::MaxStack is 16 bits wide):
// a deeper body is a layout error, never a truncated maxstack.
TEST(CLIILBuilderTest, MaxStackLimit) {
  {
    ILBuilder B(/*ReturnsValue=*/false);
    for (unsigned I = 0; I != 65535; ++I)
      B.emitLdcI4(0);
    for (unsigned I = 0; I != 65535; ++I)
      B.emit(Opcode::Pop);
    B.emitRet();
    ASSERT_THAT_ERROR(B.layout(), Succeeded());
    EXPECT_EQ(B.getMaxStack(), 65535u);
    EXPECT_EQ(B.getCodeSize(), 2u * 65535 + 1);
  }
  {
    // The 65536th ldc.i4.0 (at offset 0xFFFF) exceeds the limit.
    ILBuilder B(/*ReturnsValue=*/false);
    for (unsigned I = 0; I != 65536; ++I)
      B.emitLdcI4(0);
    for (unsigned I = 0; I != 65536; ++I)
      B.emit(Opcode::Pop);
    B.emitRet();
    std::string Msg = layoutErrorMessage(B);
    EXPECT_THAT(Msg, testing::HasSubstr("depth"));
    EXPECT_THAT(Msg, testing::HasSubstr("offset 0x"));
  }
}

TEST(CLIILBuilderTest, StackErrors) {
  {
    ILBuilder B(/*ReturnsValue=*/false); // underflow at offset 0
    B.emit(Opcode::Pop);
    B.emitRet();
    std::string Msg = layoutErrorMessage(B);
    EXPECT_THAT(Msg, testing::HasSubstr("underflow"));
    EXPECT_THAT(Msg, testing::HasSubstr("offset 0x"));
  }
  {
    ILBuilder B(/*ReturnsValue=*/false); // add needs two values
    B.emitLdcI4(1);
    B.emit(Opcode::Add);
    B.emit(Opcode::Pop);
    B.emitRet();
    expectLayoutError(B);
  }
  {
    ILBuilder B(/*ReturnsValue=*/false); // a call popping more than there is
    B.emitLdcI4(1);
    B.emitCall(Opcode::Call, tok(0x06000001), 2, 0);
    B.emitRet();
    expectLayoutError(B);
  }
  {
    ILBuilder B(/*ReturnsValue=*/true); // ret with nothing to return
    B.emitRet();
    expectLayoutError(B);
  }
  {
    // Falling into a label with depth 1 that a branch recorded as 0.
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L = B.createLabel();
    B.emitLdcI4(1);
    B.emitBranch(Opcode::Brtrue, L);
    B.emitLdcI4(1);
    B.placeLabel(L);
    B.emitRet();
    EXPECT_THAT(layoutErrorMessage(B), testing::HasSubstr("depth"));
  }
  {
    // Two branches recording different depths.
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L = B.createLabel();
    B.emitLdcI4(0);
    B.emitBranch(Opcode::Brfalse, L);
    B.emitLdcI4(1);
    B.emitLdcI4(1);
    B.emitBranch(Opcode::Brfalse, L);
    B.emit(Opcode::Pop);
    B.placeLabel(L);
    B.emitRet();
    expectLayoutError(B);
  }
  {
    // A backward branch with a different depth than its label.
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L = B.createLabel();
    B.placeLabel(L);
    B.emitLdcI4(1);
    B.emitBranch(Opcode::Br, L);
    expectLayoutError(B);
  }
}

TEST(CLIILBuilderTest, LabelErrors) {
  {
    ILBuilder B(/*ReturnsValue=*/false); // never placed
    ILLabel L = B.createLabel();
    B.emitBranch(Opcode::Br, L);
    B.emitRet();
    expectLayoutError(B);
  }
  {
    ILBuilder B(/*ReturnsValue=*/false); // a switch target never placed
    ILLabel L = B.createLabel(), M = B.createLabel();
    B.emitLdcI4(0);
    B.emitSwitch({L, M});
    B.placeLabel(L);
    B.emitRet();
    expectLayoutError(B);
  }
  // A label between a prefix and its instruction.
  {
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L = B.createLabel();
    B.emitLdcI4(0);
    B.emit(Opcode::Volatile);
    B.placeLabel(L);
    B.emit(Opcode::Ldind_i4);
    B.emit(Opcode::Pop);
    B.emitRet();
    expectLayoutError(B);
  }
  {
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L = B.createLabel();
    B.emitLdcI4(0);
    B.emitImm(Opcode::Unaligned, 1);
    B.placeLabel(L);
    B.emit(Opcode::Ldind_i4);
    B.emit(Opcode::Pop);
    B.emitRet();
    expectLayoutError(B);
  }
  {
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L = B.createLabel();
    B.emit(Opcode::Tail);
    B.placeLabel(L);
    B.emitCall(Opcode::Call, tok(0x06000001), 0, 0);
    B.emitRet();
    expectLayoutError(B);
  }
  {
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L = B.createLabel();
    B.emitLdcI4(0);
    B.emitToken(Opcode::Constrained, tok(0x1B000001));
    B.placeLabel(L);
    B.emitCall(Opcode::Callvirt, tok(0x0A000001), 1, 0);
    B.emitRet();
    expectLayoutError(B);
  }
  {
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel L = B.createLabel();
    B.emit(Opcode::Ldnull);
    B.emitLdcI4(0);
    B.emit(Opcode::Readonly);
    B.placeLabel(L);
    B.emitToken(Opcode::Ldelema, tok(0x01000001));
    B.emit(Opcode::Pop);
    B.emitRet();
    expectLayoutError(B);
  }
  // Without the label the same prefixed instructions lay out.
  {
    ILBuilder B(/*ReturnsValue=*/false);
    B.emitLdcI4(0);
    B.emit(Opcode::Volatile);
    B.emit(Opcode::Ldind_i4);
    B.emit(Opcode::Pop);
    B.emitRet();
    EXPECT_EQ(build(B), fromHex("16 FE 13 4A 26 2A"));
  }
  ILLabel None;
  EXPECT_FALSE(None.isValid());
  ILBuilder B(/*ReturnsValue=*/false);
  EXPECT_TRUE(B.createLabel().isValid());
}

// Nested clauses list the inner one first (II.19): a try/catch nested in a
// try block, in a catch handler and in a filter block. Listing the enclosing
// clause first is a layout error (plan 5.7.5: 'a clause listed after a clause
// that encloses it').
TEST(CLIILBuilderTest, ClauseOrder) {
  for (bool InnerFirst : {true, false}) {
    SCOPED_TRACE(InnerFirst);
    // try { try { leave L1 } catch { pop; leave L1 } L1: leave L2 }
    // finally { endfinally } L2: ret
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel T = B.createLabel(), C = B.createLabel(), L1 = B.createLabel(),
            F = B.createLabel(), L2 = B.createLabel();
    B.placeLabel(T);
    B.emitBranch(Opcode::Leave, L1);
    B.placeLabel(C);
    B.emit(Opcode::Pop);
    B.emitBranch(Opcode::Leave, L1);
    B.placeLabel(L1);
    B.emitBranch(Opcode::Leave, L2);
    B.placeLabel(F);
    B.emit(Opcode::Endfinally);
    B.placeLabel(L2);
    B.emitRet();
    ILExceptionClause Inner = ehClause(ExceptionClauseKind::Catch, T, C, C, L1,
                                       ILLabel(), tok(0x01000001));
    ILExceptionClause Outer =
        ehClause(ExceptionClauseKind::Finally, T, F, F, L2);
    if (InnerFirst) {
      B.addExceptionClause(Inner);
      B.addExceptionClause(Outer);
      std::vector<ExceptionClause> Clauses;
      EXPECT_EQ(build(B, &Clauses), fromHex("DE 03 26 DE 00 DE 01 DC 2A"));
      ASSERT_EQ(Clauses.size(), 2u);
      expectClause(Clauses[0], ExceptionClauseKind::Catch, 0, 2, 2, 3,
                   0x01000001);
      expectClause(Clauses[1], ExceptionClauseKind::Finally, 0, 7, 7, 1, 0);
    } else {
      B.addExceptionClause(Outer);
      B.addExceptionClause(Inner);
      expectLayoutError(B);
    }
  }

  for (bool InnerFirst : {true, false}) {
    SCOPED_TRACE(std::string("in a catch handler, ") +
                 (InnerFirst ? "inner first" : "outer first"));
    // try { leave Out } catch { pop; try { leave In } catch { pop; leave In }
    // In: leave Out } Out: ret
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel T0 = B.createLabel(), H0 = B.createLabel(), T1 = B.createLabel(),
            H1 = B.createLabel(), In = B.createLabel(), Out = B.createLabel();
    B.placeLabel(T0);
    B.emitBranch(Opcode::Leave, Out);
    B.placeLabel(H0);
    B.emit(Opcode::Pop);
    B.placeLabel(T1);
    B.emitBranch(Opcode::Leave, In);
    B.placeLabel(H1);
    B.emit(Opcode::Pop);
    B.emitBranch(Opcode::Leave, In);
    B.placeLabel(In);
    B.emitBranch(Opcode::Leave, Out);
    B.placeLabel(Out);
    B.emitRet();
    ILExceptionClause Outer = ehClause(ExceptionClauseKind::Catch, T0, H0, H0,
                                       Out, ILLabel(), tok(0x01000001));
    ILExceptionClause Inner = ehClause(ExceptionClauseKind::Catch, T1, H1, H1,
                                       In, ILLabel(), tok(0x01000001));
    if (InnerFirst) {
      B.addExceptionClause(Inner);
      B.addExceptionClause(Outer);
      std::vector<ExceptionClause> Clauses;
      EXPECT_EQ(build(B, &Clauses),
                fromHex("DE 08 26 DE 03 26 DE 00 DE 00 2A"));
      EXPECT_EQ(B.getMaxStack(), 1u);
      ASSERT_EQ(Clauses.size(), 2u);
      expectClause(Clauses[0], ExceptionClauseKind::Catch, 3, 2, 5, 3,
                   0x01000001);
      expectClause(Clauses[1], ExceptionClauseKind::Catch, 0, 2, 2, 8,
                   0x01000001);
    } else {
      B.addExceptionClause(Outer);
      B.addExceptionClause(Inner);
      expectLayoutError(B);
    }
  }

  for (bool InnerFirst : {true, false}) {
    SCOPED_TRACE(std::string("in a filter block, ") +
                 (InnerFirst ? "inner first" : "outer first"));
    // try { leave Out } filter { pop; try { leave In } catch { pop; leave In }
    // In: ldc.i4.1; endfilter } { pop; leave Out } Out: ret
    ILBuilder B(/*ReturnsValue=*/false);
    ILLabel T0 = B.createLabel(), FB = B.createLabel(), T1 = B.createLabel(),
            H1 = B.createLabel(), In = B.createLabel(), H0 = B.createLabel(),
            Out = B.createLabel();
    B.placeLabel(T0);
    B.emitBranch(Opcode::Leave, Out);
    B.placeLabel(FB);
    B.emit(Opcode::Pop);
    B.placeLabel(T1);
    B.emitBranch(Opcode::Leave, In);
    B.placeLabel(H1);
    B.emit(Opcode::Pop);
    B.emitBranch(Opcode::Leave, In);
    B.placeLabel(In);
    B.emitLdcI4(1);
    B.emit(Opcode::Endfilter);
    B.placeLabel(H0);
    B.emit(Opcode::Pop);
    B.emitBranch(Opcode::Leave, Out);
    B.placeLabel(Out);
    B.emitRet();
    ILExceptionClause Outer =
        ehClause(ExceptionClauseKind::Filter, T0, FB, H0, Out, FB);
    ILExceptionClause Inner = ehClause(ExceptionClauseKind::Catch, T1, H1, H1,
                                       In, ILLabel(), tok(0x01000001));
    if (InnerFirst) {
      B.addExceptionClause(Inner);
      B.addExceptionClause(Outer);
      std::vector<ExceptionClause> Clauses;
      EXPECT_EQ(build(B, &Clauses),
                fromHex("DE 0C 26 DE 03 26 DE 00 17 FE 11 26 DE 00 2A"));
      EXPECT_EQ(B.getMaxStack(), 1u);
      ASSERT_EQ(Clauses.size(), 2u);
      expectClause(Clauses[0], ExceptionClauseKind::Catch, 3, 2, 5, 3,
                   0x01000001);
      expectClause(Clauses[1], ExceptionClauseKind::Filter, 0, 2, 11, 3, 2);
    } else {
      B.addExceptionClause(Outer);
      B.addExceptionClause(Inner);
      expectLayoutError(B);
    }
  }
}

//===----------------------------------------------------------------------===//
// Token resolution
//===----------------------------------------------------------------------===//

TEST(CLIILBuilderTest, EncodeResolvesHandlesAndExternals) {
  ILBuilder B(/*ReturnsValue=*/false);
  ILLabel T = B.createLabel(), H = B.createLabel(), E = B.createLabel();
  B.placeLabel(T);
  B.emitToken(Opcode::Ldsfld, TokenRef::getHandle(EntityHandle{0x04000003}));
  B.emitCall(Opcode::Call, TokenRef::getExternal(7), 1, 1);
  B.emitToken(Opcode::Ldstr, tok(0x70000001));
  B.emit(Opcode::Pop);
  B.emit(Opcode::Pop);
  B.emitBranch(Opcode::Leave, E);
  B.placeLabel(H);
  B.emit(Opcode::Pop);
  B.emitBranch(Opcode::Leave, E);
  B.placeLabel(E);
  B.emitRet();
  B.addExceptionClause(ehClause(ExceptionClauseKind::Catch, T, H, H, E,
                                ILLabel(), TokenRef::getExternal(9)));
  ASSERT_THAT_ERROR(B.layout(), Succeeded());
  EXPECT_TRUE(B.instructions()[0].Token ==
              TokenRef::getHandle(EntityHandle{0x04000003}));
  EXPECT_TRUE(B.instructions()[1].Token == TokenRef::getExternal(7));

  std::vector<TokenRef> Asked;
  auto Resolve = [&](TokenRef R) -> Expected<uint32_t> {
    Asked.push_back(R);
    switch (R.getKind()) {
    case TokenRef::Kind::Token:
      return R.getValue();
    case TokenRef::Kind::Handle:
      return R.getValue() + 0x10; // a MetadataBuilder's final row
    case TokenRef::Kind::External:
      return R.getValue() == 7 ? 0x0A000002u : 0x01000004u;
    default:
      return 0u;
    }
  };
  SmallVector<uint8_t, 32> Code;
  SmallVector<ExceptionClause, 1> Clauses;
  ASSERT_THAT_ERROR(B.encode(Code, Clauses, Resolve), Succeeded());
  EXPECT_EQ(toVec(Code), fromHex("7E 13 00 00 04 28 02 00 00 0A 72 01 00 00 "
                                 "70 26 26 DE 03 26 DE 00 2A"));
  ASSERT_EQ(Clauses.size(), 1u);
  expectClause(Clauses[0], ExceptionClauseKind::Catch, 0, 19, 19, 3,
               0x01000004);
  auto Has = [&](TokenRef R) {
    return std::find(Asked.begin(), Asked.end(), R) != Asked.end();
  };
  EXPECT_TRUE(Has(TokenRef::getHandle(EntityHandle{0x04000003})));
  EXPECT_TRUE(Has(TokenRef::getExternal(7)));
  EXPECT_TRUE(Has(tok(0x70000001)));
  EXPECT_TRUE(Has(TokenRef::getExternal(9)));

  // A resolver error fails the encode.
  SmallVector<uint8_t, 32> Code2;
  SmallVector<ExceptionClause, 1> Clauses2;
  EXPECT_THAT_ERROR(B.encode(Code2, Clauses2,
                             [](TokenRef R) -> Expected<uint32_t> {
                               if (R.getKind() == TokenRef::Kind::External)
                                 return createStringError(
                                     inconvertibleErrorCode(),
                                     "undefined symbol");
                               return R.getValue();
                             }),
                    FailedWithMessage("undefined symbol"));
}

//===----------------------------------------------------------------------===//
// E1 (metadata-writer.md 3.1-3.2)
//===----------------------------------------------------------------------===//

// hello-ilasm.mdump.txt: main's 371 bytes as ilasm wrote them, with its
// tokens; the 200 nops between the two parts are left out here.
const char *const E1MainHead =
    "7E 03 00 00 04 28 05 00 00 06 26 72 01 00 00 70 28 02 00 00 0A 28 05 00 "
    "00 06 26 16 0A 1D 7E 04 00 00 04 29 01 00 00 11 1F 31 33 04 06 17 60 0A "
    "28 03 00 00 06 DE 0B 26 17 FE 11 26 06 18 60 0A DE 00 16 0D 17 0C 09 08 "
    "58 0D 08 17 58 0C 08 1F 0A 31 F3 09 1F 37 33 04 06 1A 60 0A 18 45 03 00 "
    "00 00 02 00 00 00 04 00 00 00 06 00 00 00 2B 08 2B 06 2B 04 06 1E 60 0A "
    "38 CA 00 00 00";
const char *const E1MainTail =
    "14 7A 06 1F 10 60 0A 28 04 00 00 06 2C 05 06 1F 20 60 0A 1E FE 0F 0B 07 "
    "22 01 00 C0 7F 56 07 4A 20 01 00 80 7F 33 05 06 1F 40 60 0A 06 2A";

// E1's main (py/hello.py), instruction by instruction, with ilasm's tokens:
// msg 0x04000003, '<msil>fn_square' 0x04000004, puts 0x06000005, thrower
// 0x06000003, the long name 0x06000004, StringToHGlobalAnsi 0x0A000002, the
// calli signature 0x11000001 and the #US string 0x70000001. Branches are given
// in their long spellings, as hello.py writes them. ilasm quiets the float32
// signaling NaN 0x7F800001 to 0x7FC00001 (metadata-writer.md 3.4), so the
// rebuilt body uses ilasm's bits.
TEST(CLIILBuilderTest, E1MainEqualsIlasm) {
  ILBuilder B(/*ReturnsValue=*/true);
  ILLabel NoCalli = B.createLabel(), Try0 = B.createLabel(),
          Try1 = B.createLabel(), Filter = B.createLabel(),
          H0 = B.createLabel(), H1 = B.createLabel(), After = B.createLabel(),
          Loop = B.createLabel(), NoLoop = B.createLabel(),
          C0 = B.createLabel(), C1 = B.createLabel(), C2 = B.createLabel(),
          SwEnd = B.createLabel(), Far = B.createLabel(),
          NoLong = B.createLabel(), NoSnan = B.createLabel();
  // puts(msg); puts(StringToHGlobalAnsi("hello from the #US heap ..."))
  B.emitToken(Opcode::Ldsfld, tok(0x04000003));
  B.emitCall(Opcode::Call, tok(0x06000005), 1, 1);
  B.emit(Opcode::Pop);
  B.emitToken(Opcode::Ldstr, tok(0x70000001));
  B.emitCall(Opcode::Call, tok(0x0A000002), 1, 1);
  B.emitCall(Opcode::Call, tok(0x06000005), 1, 1);
  B.emit(Opcode::Pop);
  B.emitLdcI4(0);
  B.emitStloc(0);
  // 1: calli square(7) == 49
  B.emitLdcI4(7);
  B.emitToken(Opcode::Ldsfld, tok(0x04000004));
  B.emitCall(Opcode::Calli, tok(0x11000001), 2, 1);
  B.emitLdcI4(49);
  B.emitBranch(Opcode::Bne_un, NoCalli);
  B.emitLdloc(0);
  B.emitLdcI4(1);
  B.emit(Opcode::Or);
  B.emitStloc(0);
  B.placeLabel(NoCalli);
  // 2: an exception filter
  B.placeLabel(Try0);
  B.emitCall(Opcode::Call, tok(0x06000003), 0, 0);
  B.emitBranch(Opcode::Leave, After);
  B.placeLabel(Try1);
  B.placeLabel(Filter);
  B.emit(Opcode::Pop);
  B.emitLdcI4(1);
  B.emit(Opcode::Endfilter);
  B.placeLabel(H0);
  B.emit(Opcode::Pop);
  B.emitLdloc(0);
  B.emitLdcI4(2);
  B.emit(Opcode::Or);
  B.emitStloc(0);
  B.emitBranch(Opcode::Leave, After);
  B.placeLabel(H1);
  B.placeLabel(After);
  // 4: a loop with short branches
  B.emitLdcI4(0);
  B.emitStloc(3);
  B.emitLdcI4(1);
  B.emitStloc(2);
  B.placeLabel(Loop);
  B.emitLdloc(3);
  B.emitLdloc(2);
  B.emit(Opcode::Add);
  B.emitStloc(3);
  B.emitLdloc(2);
  B.emitLdcI4(1);
  B.emit(Opcode::Add);
  B.emitStloc(2);
  B.emitLdloc(2);
  B.emitLdcI4(10);
  B.emitBranch(Opcode::Ble, Loop);
  B.emitLdloc(3);
  B.emitLdcI4(55);
  B.emitBranch(Opcode::Bne_un, NoLoop);
  B.emitLdloc(0);
  B.emitLdcI4(4);
  B.emit(Opcode::Or);
  B.emitStloc(0);
  B.placeLabel(NoLoop);
  // 8: a switch
  B.emitLdcI4(2);
  B.emitSwitch({C0, C1, C2});
  B.emitBranch(Opcode::Br, SwEnd);
  B.placeLabel(C0);
  B.emitBranch(Opcode::Br, SwEnd);
  B.placeLabel(C1);
  B.emitBranch(Opcode::Br, SwEnd);
  B.placeLabel(C2);
  B.emitLdloc(0);
  B.emitLdcI4(8);
  B.emit(Opcode::Or);
  B.emitStloc(0);
  B.placeLabel(SwEnd);
  // 16: a long branch over 200 nops
  B.emitBranch(Opcode::Br, Far);
  nops(B, 200);
  B.emit(Opcode::Ldnull);
  B.emit(Opcode::Throw);
  B.placeLabel(Far);
  B.emitLdloc(0);
  B.emitLdcI4(16);
  B.emit(Opcode::Or);
  B.emitStloc(0);
  // 32: the method with the long name
  B.emitCall(Opcode::Call, tok(0x06000004), 0, 1);
  B.emitBranch(Opcode::Brfalse, NoLong);
  B.emitLdloc(0);
  B.emitLdcI4(32);
  B.emit(Opcode::Or);
  B.emitStloc(0);
  B.placeLabel(NoLong);
  // 64: a float32 NaN stored exactly
  B.emitLdcI4(8);
  B.emit(Opcode::Localloc);
  B.emitStloc(1);
  B.emitLdloc(1);
  B.emitR4(0x7FC00001);
  B.emit(Opcode::Stind_r4);
  B.emitLdloc(1);
  B.emit(Opcode::Ldind_i4);
  B.emitLdcI4(0x7F800001);
  B.emitBranch(Opcode::Bne_un, NoSnan);
  B.emitLdloc(0);
  B.emitLdcI4(64);
  B.emit(Opcode::Or);
  B.emitStloc(0);
  B.placeLabel(NoSnan);
  B.emitLdloc(0);
  B.emitRet();
  B.addExceptionClause(
      ehClause(ExceptionClauseKind::Filter, Try0, Try1, H0, H1, Filter));

  std::vector<ExceptionClause> Clauses;
  ByteVec Code = build(B, &Clauses);
  ByteVec Want =
      cat({fromHex(E1MainHead), ByteVec(200, 0x00), fromHex(E1MainTail)});
  ASSERT_EQ(Want.size(), 371u);
  EXPECT_EQ(Code, Want);
  EXPECT_EQ(B.getCodeSize(), 371u);
  // SRM: Filter try 48+7 handler 59+7 filter 55.
  ASSERT_EQ(Clauses.size(), 1u);
  expectClause(Clauses[0], ExceptionClauseKind::Filter, 48, 7, 59, 7, 55);
  // The deepest point is two values (ilasm's .maxstack 3 was hello.py's
  // declaration, not a computed depth).
  EXPECT_EQ(B.getMaxStack(), 2u);

  ArrayRef<ILInstr> I = B.instructions();
  ASSERT_EQ(I.size(), 289u);
  EXPECT_EQ(I[13].Op, Opcode::Bne_un_s);
  EXPECT_EQ(I[13].Offset, 42u);
  EXPECT_EQ(I[13].Target.Id, NoCalli.Id);
  EXPECT_EQ(I[19].Op, Opcode::Leave_s);
  EXPECT_EQ(I[19].Offset, 53u);
  EXPECT_EQ(I[43].Op, Opcode::Ble_s);
  EXPECT_EQ(I[43].Offset, 81u);
  EXPECT_EQ(I[52].Op, Opcode::Switch);
  EXPECT_EQ(I[52].Offset, 93u);
  EXPECT_EQ(I[52].SwitchCount, 3u);
  EXPECT_EQ(I[53].Op, Opcode::Br_s);
  EXPECT_EQ(I[53].Offset, 110u);
  EXPECT_EQ(I[60].Op, Opcode::Br);
  EXPECT_EQ(I[60].Offset, 120u);
  EXPECT_EQ(I[268].Op, Opcode::Brfalse_s);
  EXPECT_EQ(I[268].Offset, 337u);
  EXPECT_EQ(B.getLabelOffset(Try0), 48u);
  EXPECT_EQ(B.getLabelOffset(Filter), 55u);
  EXPECT_EQ(B.getLabelOffset(H0), 59u);
  EXPECT_EQ(B.getLabelOffset(After), 66u);
  EXPECT_EQ(B.getLabelOffset(Loop), 70u);
  EXPECT_EQ(B.getLabelOffset(SwEnd), 120u);
  EXPECT_EQ(B.getLabelOffset(Far), 327u);
  EXPECT_EQ(B.getLabelOffset(NoSnan), 369u);
}

// E1's other bodies (hello-ilasm.mdump.txt): square, thrower and the entry
// point '<msil>Main', which calls main (0x06000001).
TEST(CLIILBuilderTest, E1SmallBodies) {
  {
    ILBuilder B(/*ReturnsValue=*/true);
    B.emitLdarg(0);
    B.emitLdarg(0);
    B.emit(Opcode::Mul);
    B.emitRet();
    EXPECT_EQ(build(B), fromHex("02 02 5A 2A"));
    EXPECT_EQ(B.getMaxStack(), 2u);
  }
  {
    ILBuilder B(/*ReturnsValue=*/false);
    B.emit(Opcode::Ldnull);
    B.emit(Opcode::Throw);
    EXPECT_EQ(build(B), fromHex("14 7A"));
    EXPECT_EQ(B.getMaxStack(), 1u);
  }
  {
    ILBuilder B(/*ReturnsValue=*/true);
    B.emitCall(Opcode::Call, tok(0x06000001), 0, 1);
    B.emitRet();
    EXPECT_EQ(build(B), fromHex("28 01 00 00 06 2A"));
    EXPECT_EQ(B.getMaxStack(), 1u);
  }
}

} // namespace
