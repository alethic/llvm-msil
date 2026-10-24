//===- ILDecoderTest.cpp - CIL instruction decoder and token rewriter -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/ILDecoder.h (plan 5.7.5): decodeIL over a method body that
// .NET Framework ilasm 4.8 assembled with every System.Reflection.Emit opcode
// (the all-opcodes method of metadata-writer-files/py/check_opcodes.py,
// assembled and read with System.Reflection.Metadata in scratch), the no.
// prefix, branch and switch targets, the sign rules of each operand kind,
// token offsets, truncation, unknown opcodes and reserved prefixes, and
// rewriteILTokens. Operands follow ECMA-335 III.1.2 and metadata-writer.md
// 5.1.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/ILDecoder.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/Support/Error.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

using ByteVec = std::vector<uint8_t>;

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

std::vector<ILInstruction> decodeAll(ArrayRef<uint8_t> Code) {
  std::vector<ILInstruction> Out;
  Error E = decodeIL(Code, [&](const ILInstruction &I) {
    Out.push_back(I);
    return Error::success();
  });
  EXPECT_THAT_ERROR(std::move(E), Succeeded());
  return Out;
}

ILInstruction decodeOne(ArrayRef<uint8_t> Code) {
  std::vector<ILInstruction> All = decodeAll(Code);
  EXPECT_EQ(All.size(), 1u);
  return All.empty() ? ILInstruction() : All.front();
}

void expectInvalidIL(ArrayRef<uint8_t> Code) {
  EXPECT_THAT_ERROR(
      decodeIL(Code, [](const ILInstruction &) { return Error::success(); }),
      FailedWithMessage(testing::StartsWith("invalid IL: ")));
}

// The message of a decode error, which starts with the prefix; lowercased, so
// that callers match words of the cause in any case.
std::string invalidILMessage(ArrayRef<uint8_t> Code) {
  Error E =
      decodeIL(Code, [](const ILInstruction &) { return Error::success(); });
  if (!E) {
    ADD_FAILURE() << "the code decoded without an error";
    return "";
  }
  std::string Msg = toString(std::move(E));
  EXPECT_THAT(Msg, testing::StartsWith("invalid IL: "));
  return StringRef(Msg).lower();
}

unsigned value(Opcode Op) { return static_cast<unsigned>(Op); }

// ECMA-335 III.1.2 operand sizes (InlineSwitch: the count only).
unsigned operandBytes(OperandKind K) {
  switch (K) {
  case OperandKind::InlineNone:
    return 0;
  case OperandKind::ShortInlineVar:
  case OperandKind::ShortInlineI:
  case OperandKind::ShortInlineBrTarget:
    return 1;
  case OperandKind::InlineVar:
    return 2;
  case OperandKind::InlineI8:
  case OperandKind::InlineR:
    return 8;
  default:
    return 4;
  }
}

uint32_t readU32(ArrayRef<uint8_t> Code, size_t Off) {
  return Code[Off] | Code[Off + 1] << 8 | Code[Off + 2] << 16 |
         uint32_t(Code[Off + 3]) << 24;
}

// The body of 'allops'(int32, int32) in allops.dll: every opcode of
// System.Reflection.Emit.OpCodes in value order with the operands of
// check_opcodes.py (var operands 1, ldc.i4.s -5, unaligned. 2, ldc.i4
// 0x12345678, ldc.i8 0x123456789ABCDEF0, ldc.r4 float32(0x7F800001), which
// ilasm quiets to 0x7FC00001, ldc.r8 float64(0x7FF0000000000001), every branch
// to the next instruction, 'switch (L_s0, L_s1)' around one nop). 500 bytes.
const char *const AllOpsIL =
    "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 01 0F 01 10 01 11 01 12 01 "
    "13 01 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F FB 20 78 56 34 12 21 F0 DE BC "
    "9A 78 56 34 12 22 01 00 C0 7F 23 01 00 00 00 00 00 F0 7F 25 26 27 02 00 "
    "00 06 28 01 00 00 06 29 01 00 00 11 2A 2B 00 2C 00 2D 00 2E 00 2F 00 30 "
    "00 31 00 32 00 33 00 34 00 35 00 36 00 37 00 38 00 00 00 00 39 00 00 00 "
    "00 3A 00 00 00 00 3B 00 00 00 00 3C 00 00 00 00 3D 00 00 00 00 3E 00 00 "
    "00 00 3F 00 00 00 00 40 00 00 00 00 41 00 00 00 00 42 00 00 00 00 43 00 "
    "00 00 00 44 00 00 00 00 45 02 00 00 00 00 00 00 00 01 00 00 00 00 46 47 "
    "48 49 4A 4B 4C 4D 4E 4F 50 51 52 53 54 55 56 57 58 59 5A 5B 5C 5D 5E 5F "
    "60 61 62 63 64 65 66 67 68 69 6A 6B 6C 6D 6E 6F 01 00 00 0A 70 01 00 00 "
    "1B 71 01 00 00 1B 72 01 00 00 70 73 02 00 00 0A 74 01 00 00 1B 75 01 00 "
    "00 1B 76 79 01 00 00 1B 7A 7B 02 00 00 04 7C 02 00 00 04 7D 02 00 00 04 "
    "7E 01 00 00 04 7F 01 00 00 04 80 01 00 00 04 81 01 00 00 1B 82 83 84 85 "
    "86 87 88 89 8A 8B 8C 01 00 00 1B 8D 01 00 00 1B 8E 8F 01 00 00 1B 90 91 "
    "92 93 94 95 96 97 98 99 9A 9B 9C 9D 9E 9F A0 A1 A2 A3 01 00 00 1B A4 01 "
    "00 00 1B A5 01 00 00 1B B3 B4 B5 B6 B7 B8 B9 BA C2 01 00 00 1B C3 C6 01 "
    "00 00 1B D0 03 00 00 01 D1 D2 D3 D4 D5 D6 D7 D8 D9 DA DB DC DD 00 00 00 "
    "00 DE 00 DF E0 FE 00 FE 01 FE 02 FE 03 FE 04 FE 05 FE 06 01 00 00 06 FE "
    "07 01 00 00 0A FE 09 01 00 FE 0A 01 00 FE 0B 01 00 FE 0C 01 00 FE 0D 01 "
    "00 FE 0E 01 00 FE 0F FE 11 FE 12 02 FE 13 FE 14 FE 15 01 00 00 1B FE 16 "
    "01 00 00 1B FE 17 FE 18 FE 1A FE 1C 01 00 00 1B FE 1D FE 1E";

// The opcode of each instruction of AllOpsIL: the 218 values in order, with
// the nop between the switch and its second target.
const uint16_t AllOpsSequence[] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008,
    0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F, 0x0010, 0x0011,
    0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017, 0x0018, 0x0019, 0x001A,
    0x001B, 0x001C, 0x001D, 0x001E, 0x001F, 0x0020, 0x0021, 0x0022, 0x0023,
    0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D,
    0x002E, 0x002F, 0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036,
    0x0037, 0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0000, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F, 0x0050,
    0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059,
    0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F, 0x0060, 0x0061, 0x0062,
    0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B,
    0x006C, 0x006D, 0x006E, 0x006F, 0x0070, 0x0071, 0x0072, 0x0073, 0x0074,
    0x0075, 0x0076, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087, 0x0088,
    0x0089, 0x008A, 0x008B, 0x008C, 0x008D, 0x008E, 0x008F, 0x0090, 0x0091,
    0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097, 0x0098, 0x0099, 0x009A,
    0x009B, 0x009C, 0x009D, 0x009E, 0x009F, 0x00A0, 0x00A1, 0x00A2, 0x00A3,
    0x00A4, 0x00A5, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x00B9,
    0x00BA, 0x00C2, 0x00C3, 0x00C6, 0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4,
    0x00D5, 0x00D6, 0x00D7, 0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD,
    0x00DE, 0x00DF, 0x00E0, 0xFE00, 0xFE01, 0xFE02, 0xFE03, 0xFE04, 0xFE05,
    0xFE06, 0xFE07, 0xFE09, 0xFE0A, 0xFE0B, 0xFE0C, 0xFE0D, 0xFE0E, 0xFE0F,
    0xFE11, 0xFE12, 0xFE13, 0xFE14, 0xFE15, 0xFE16, 0xFE17, 0xFE18, 0xFE1A,
    0xFE1C, 0xFE1D, 0xFE1E};

TEST(CLIILDecoderTest, EveryOpcodeFromIlasm) {
  ByteVec Code = fromHex(AllOpsIL);
  ASSERT_EQ(Code.size(), 500u);
  std::vector<ILInstruction> Insts = decodeAll(Code);
  ASSERT_EQ(Insts.size(), std::size(AllOpsSequence));
  uint32_t Expected = 0;
  for (size_t N = 0; N != Insts.size(); ++N) {
    const ILInstruction &I = Insts[N];
    SCOPED_TRACE("instruction " + std::to_string(N) + " at offset " +
                 std::to_string(I.Offset));
    ASSERT_EQ(value(I.Op), AllOpsSequence[N]);
    EXPECT_EQ(I.Offset, Expected);
    unsigned OpSize = value(I.Op) >= 0xFE00 ? 2 : 1;
    OperandKind K = getOpcodeInfo(I.Op).Operand;
    unsigned Size = OpSize + operandBytes(K);
    if (K == OperandKind::InlineSwitch)
      Size += 4 * 2;
    EXPECT_EQ(I.Size, Size);
    switch (K) {
    case OperandKind::ShortInlineVar:
    case OperandKind::InlineVar:
      EXPECT_EQ(I.Imm, 1);
      break;
    case OperandKind::ShortInlineI:
      EXPECT_EQ(I.Imm, I.Op == Opcode::Unaligned ? 2 : -5);
      break;
    case OperandKind::InlineI:
      EXPECT_EQ(I.Imm, 0x12345678);
      break;
    case OperandKind::InlineI8:
      EXPECT_EQ(I.Imm, 0x123456789ABCDEF0);
      break;
    case OperandKind::ShortInlineR:
      EXPECT_EQ(I.FPBits & 0xFFFFFFFF, 0x7FC00001u);
      break;
    case OperandKind::InlineR:
      EXPECT_EQ(I.FPBits, 0x7FF0000000000001u);
      break;
    case OperandKind::ShortInlineBrTarget:
    case OperandKind::InlineBrTarget:
      EXPECT_EQ(I.Target, int64_t(I.Offset + I.Size));
      break;
    case OperandKind::InlineSwitch:
      EXPECT_EQ(
          I.SwitchTargets,
          (SmallVector<int64_t, 0>{I.Offset + I.Size, I.Offset + I.Size + 1}));
      break;
    case OperandKind::InlineNone:
      break;
    default: {
      EXPECT_EQ(I.TokenOffset, I.Offset + OpSize);
      EXPECT_EQ(I.Token, readU32(Code, I.Offset + OpSize));
      uint8_t T = I.Token >> 24;
      switch (K) {
      case OperandKind::InlineMethod:
        EXPECT_TRUE(T == 0x06 || T == 0x0A) << utohexstr(I.Token);
        break;
      case OperandKind::InlineField:
        EXPECT_EQ(T, 0x04);
        break;
      case OperandKind::InlineType:
        EXPECT_EQ(T, 0x1B);
        break;
      case OperandKind::InlineTok:
        EXPECT_EQ(T, 0x01);
        break;
      case OperandKind::InlineSig:
        EXPECT_EQ(T, 0x11);
        break;
      case OperandKind::InlineString:
        EXPECT_EQ(T, 0x70);
        break;
      default:
        ADD_FAILURE() << "unexpected operand kind";
      }
      break;
    }
    }
    Expected = I.Offset + I.Size;
  }
  EXPECT_EQ(Expected, 500u);
}

TEST(CLIILDecoderTest, EmptyCode) {
  unsigned Calls = 0;
  EXPECT_THAT_ERROR(decodeIL({},
                             [&](const ILInstruction &) {
                               ++Calls;
                               return Error::success();
                             }),
                    Succeeded());
  EXPECT_EQ(Calls, 0u);
}

// ldc.i4.s is signed; unaligned., no. and the var operands are unsigned;
// InlineI and InlineI8 are signed; float operands are raw bits.
TEST(CLIILDecoderTest, OperandSignRules) {
  EXPECT_EQ(decodeOne(ByteVec{0x1F, 0x80}).Imm, -128);
  EXPECT_EQ(decodeOne(ByteVec{0x1F, 0x7F}).Imm, 127);
  EXPECT_EQ(decodeOne(ByteVec{0x1F, 0xFF}).Imm, -1);
  ILInstruction Un = decodeOne(ByteVec{0xFE, 0x12, 0x81});
  EXPECT_EQ(Un.Op, Opcode::Unaligned);
  EXPECT_EQ(Un.Imm, 129);
  EXPECT_EQ(Un.Size, 3u);
  ILInstruction No = decodeOne(ByteVec{0xFE, 0x19, 0xFF});
  EXPECT_EQ(value(No.Op), 0xFE19u);
  EXPECT_EQ(No.Imm, 255);
  EXPECT_EQ(No.Size, 3u);
  EXPECT_EQ(decodeOne(ByteVec{0x0E, 0xFF}).Imm, 255);       // ldarg.s
  EXPECT_EQ(decodeOne(ByteVec{0x13, 0x80}).Imm, 128);       // stloc.s
  EXPECT_EQ(decodeOne(ByteVec{0xFE, 0x0C, 0xFF, 0xFF}).Imm, // ldloc
            65535);
  EXPECT_EQ(decodeOne(ByteVec{0xFE, 0x0A, 0x00, 0x80}).Imm, // ldarga
            32768);
  EXPECT_EQ(decodeOne(ByteVec{0x20, 0xFF, 0xFF, 0xFF, 0xFF}).Imm, -1);
  EXPECT_EQ(decodeOne(ByteVec{0x20, 0x00, 0x00, 0x00, 0x80}).Imm, INT32_MIN);
  EXPECT_EQ(decodeOne(ByteVec{0x21, 0, 0, 0, 0, 0, 0, 0, 0x80}).Imm, INT64_MIN);
  EXPECT_EQ(
      decodeOne(ByteVec{0x21, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F})
          .Imm,
      INT64_MAX);
  ILInstruction R4 = decodeOne(ByteVec{0x22, 0x01, 0x00, 0x80, 0x7F});
  EXPECT_EQ(R4.FPBits & 0xFFFFFFFF, 0x7F800001u) << "a signaling NaN is kept";
  EXPECT_EQ(R4.Size, 5u);
  ILInstruction R8 =
      decodeOne(ByteVec{0x23, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x7F});
  EXPECT_EQ(R8.FPBits, 0x7FF0000000000001u);
  EXPECT_EQ(R8.Size, 9u);
}

// Branch operands are relative to the next instruction, switch entries to the
// end of the switch; the decoder gives absolute offsets.
TEST(CLIILDecoderTest, BranchAndSwitchTargets) {
  // 0: br.s +1  2: nop  3: brtrue.s -5  5: br +0  10: leave.s -12
  // 12: leave -17  17: ret
  ByteVec Code = {0x2B, 0x01, 0x00, 0x2D, 0xFB, 0x38, 0x00, 0x00, 0x00,
                  0x00, 0xDE, 0xF4, 0xDD, 0xEF, 0xFF, 0xFF, 0xFF, 0x2A};
  std::vector<ILInstruction> I = decodeAll(Code);
  ASSERT_EQ(I.size(), 7u);
  EXPECT_EQ(I[0].Op, Opcode::Br_s);
  EXPECT_EQ(I[0].Target, 3);
  EXPECT_EQ(I[2].Op, Opcode::Brtrue_s);
  EXPECT_EQ(I[2].Target, 0);
  EXPECT_EQ(I[3].Op, Opcode::Br);
  EXPECT_EQ(I[3].Offset, 5u);
  EXPECT_EQ(I[3].Size, 5u);
  EXPECT_EQ(I[3].Target, 10);
  EXPECT_EQ(I[4].Op, Opcode::Leave_s);
  EXPECT_EQ(I[4].Target, 0);
  EXPECT_EQ(I[5].Op, Opcode::Leave);
  EXPECT_EQ(I[5].Target, 0);
  EXPECT_EQ(I[6].Offset, 17u);

  // 0: ldc.i4.0  1: switch (3 entries)  18: br.s +2  20: nop  21: nop 22: ret
  ByteVec Sw = {0x16, 0x45, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00,
                0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xEE, 0xFF,
                0xFF, 0xFF, 0x2B, 0x02, 0x00, 0x00, 0x2A};
  std::vector<ILInstruction> S = decodeAll(Sw);
  ASSERT_EQ(S.size(), 6u);
  EXPECT_EQ(S[1].Op, Opcode::Switch);
  EXPECT_EQ(S[1].Offset, 1u);
  EXPECT_EQ(S[1].Size, 17u);
  EXPECT_EQ(S[1].SwitchTargets, (SmallVector<int64_t, 0>{20, 21, 0}));
  EXPECT_EQ(S[2].Target, 22);

  // An empty switch.
  ILInstruction E = decodeOne(ByteVec{0x45, 0x00, 0x00, 0x00, 0x00});
  EXPECT_EQ(E.Size, 5u);
  EXPECT_TRUE(E.SwitchTargets.empty());

  // Targets outside the code are reported, not rejected.
  EXPECT_EQ(decodeOne(ByteVec{0x2B, 0x64}).Target, 102);
  EXPECT_EQ(decodeOne(ByteVec{0x2B, 0x80}).Target, -126);
  EXPECT_EQ(decodeOne(ByteVec{0x38, 0x00, 0x00, 0x00, 0x80}).Target,
            int64_t(5) + INT32_MIN);
}

TEST(CLIILDecoderTest, TokensAndTokenOffsets) {
  // 0: nop  1: call 0x0A000007  6: ldftn 0x06000002  12: ldstr 0x70000010
  // 17: constrained. 0x1B000003  23: callvirt 0x0A000001  28: ldtoken
  // 0x02000004  33: calli 0x11000002  38: ret
  ByteVec Code = {0x00, 0x28, 0x07, 0x00, 0x00, 0x0A, 0xFE, 0x06, 0x02, 0x00,
                  0x00, 0x06, 0x72, 0x10, 0x00, 0x00, 0x70, 0xFE, 0x16, 0x03,
                  0x00, 0x00, 0x1B, 0x6F, 0x01, 0x00, 0x00, 0x0A, 0xD0, 0x04,
                  0x00, 0x00, 0x02, 0x29, 0x02, 0x00, 0x00, 0x11, 0x2A};
  std::vector<ILInstruction> I = decodeAll(Code);
  ASSERT_EQ(I.size(), 9u);
  struct {
    unsigned Index;
    Opcode Op;
    uint32_t Token, TokenOffset;
  } Want[] = {
      {1, Opcode::Call, 0x0A000007, 2},
      {2, Opcode::Ldftn, 0x06000002, 8},
      {3, Opcode::Ldstr, 0x70000010, 13},
      {4, Opcode::Constrained, 0x1B000003, 19},
      {5, Opcode::Callvirt, 0x0A000001, 24},
      {6, Opcode::Ldtoken, 0x02000004, 29},
      {7, Opcode::Calli, 0x11000002, 34},
  };
  for (const auto &W : Want) {
    SCOPED_TRACE(W.Index);
    EXPECT_EQ(I[W.Index].Op, W.Op);
    EXPECT_EQ(I[W.Index].Token, W.Token);
    EXPECT_EQ(I[W.Index].TokenOffset, W.TokenOffset);
  }
  EXPECT_EQ(I[4].Size, 6u) << "constrained. is its own instruction";
  EXPECT_EQ(I[8].Offset, 38u);
}

TEST(CLIILDecoderTest, Truncation) {
  const char *Bad[] = {
      "FE",                         // half a two-byte opcode
      "1F",                         // ldc.i4.s without its byte
      "20 01 02",                   // ldc.i4
      "21 01 02 03 04 05 06 07",    // ldc.i8
      "22 00 00",                   // ldc.r4
      "23 00 00 00 00 00 00 00",    // ldc.r8
      "FE 0C 01",                   // ldloc
      "2B",                         // br.s
      "38 00 00 00",                // br
      "28 01 00 00",                // call
      "FE 06 01 00 00",             // ldftn
      "FE 12",                      // unaligned.
      "45 02 00 00",                // switch count
      "45 02 00 00 00 01 00 00 00", // one of two switch entries
      "45 FF FF FF 3F",             // a count far past the end
      "00 00 72 01 00 00",          // ldstr after two nops
  };
  for (const char *Hex : Bad) {
    SCOPED_TRACE(Hex);
    expectInvalidIL(fromHex(Hex));
  }
  // The error names the cause and the offset: ldc.i4 at 0x2 after two nops.
  std::string Msg = invalidILMessage(fromHex("00 00 20 01 02"));
  EXPECT_THAT(Msg, testing::HasSubstr("truncat"));
  EXPECT_THAT(Msg, testing::HasSubstr("offset 0x"));
}

TEST(CLIILDecoderTest, UnknownOpcodesAndReservedPrefixes) {
  for (unsigned B : {0x24u, 0x77u, 0x78u, 0xA6u, 0xB2u, 0xBBu, 0xC1u, 0xC4u,
                     0xC5u, 0xC7u, 0xCFu, 0xE1u, 0xF0u, 0xF7u}) {
    SCOPED_TRACE("0x" + utohexstr(B));
    expectInvalidIL(ByteVec{uint8_t(B), 0x00, 0x00, 0x00, 0x00});
  }
  // The reserved prefixes 0xF8-0xFF other than the two-byte prefix 0xFE.
  for (unsigned B : {0xF8u, 0xF9u, 0xFAu, 0xFBu, 0xFCu, 0xFDu, 0xFFu}) {
    SCOPED_TRACE("0x" + utohexstr(B));
    expectInvalidIL(ByteVec{uint8_t(B), 0x00, 0x00, 0x00, 0x00});
  }
  for (unsigned B : {0x08u, 0x10u, 0x1Bu, 0x1Fu, 0x20u, 0xFEu, 0xFFu}) {
    SCOPED_TRACE("0xFE 0x" + utohexstr(B));
    expectInvalidIL(ByteVec{0xFE, uint8_t(B), 0x00, 0x00, 0x00, 0x00});
  }
  // An unknown opcode after valid ones; the error names the cause and the
  // offset (0x3).
  std::string Msg = invalidILMessage(ByteVec{0x00, 0x17, 0x26, 0x24});
  EXPECT_THAT(Msg, testing::HasSubstr("opcode"));
  EXPECT_THAT(Msg, testing::HasSubstr("offset 0x"));
}

// The visitor's error ends the walk and is returned as is.
TEST(CLIILDecoderTest, VisitorErrorStopsTheWalk) {
  ByteVec Code = {0x00, 0x17, 0x26, 0x2A};
  unsigned Calls = 0;
  Error E = decodeIL(Code, [&](const ILInstruction &I) -> Error {
    ++Calls;
    if (I.Offset == 1)
      return createStringError(inconvertibleErrorCode(), "stop here");
    return Error::success();
  });
  EXPECT_THAT_ERROR(std::move(E), FailedWithMessage("stop here"));
  EXPECT_EQ(Calls, 2u);
}

// Every token operand kind is patched in place; nothing else changes.
TEST(CLIILDecoderTest, RewriteTokens) {
  // ldsfld, stfld, box, ldtoken, ldstr, call, ldftn, newobj, calli,
  // constrained. callvirt, jmp, and non-token instructions between them.
  ByteVec Code = {
      0x7E, 0x01, 0x00, 0x00, 0x04,       // 0  ldsfld 0x04000001
      0x16,                               // 5  ldc.i4.0
      0x7D, 0x02, 0x00, 0x00, 0x04,       // 6  stfld 0x04000002
      0x8C, 0x03, 0x00, 0x00, 0x01,       // 11 box 0x01000003
      0xD0, 0x04, 0x00, 0x00, 0x02,       // 16 ldtoken 0x02000004
      0x72, 0x05, 0x00, 0x00, 0x70,       // 21 ldstr 0x70000005
      0x28, 0x06, 0x00, 0x00, 0x0A,       // 26 call 0x0A000006
      0xFE, 0x06, 0x07, 0x00, 0x00, 0x06, // 31 ldftn 0x06000007
      0x73, 0x08, 0x00, 0x00, 0x0A,       // 37 newobj 0x0A000008
      0x29, 0x09, 0x00, 0x00, 0x11,       // 42 calli 0x11000009
      0xFE, 0x16, 0x0A, 0x00, 0x00, 0x1B, // 47 constrained. 0x1B00000A
      0x6F, 0x0B, 0x00, 0x00, 0x0A,       // 53 callvirt 0x0A00000B
      0x2B, 0x00,                         // 58 br.s +0
      0x27, 0x0C, 0x00, 0x00, 0x06,       // 60 jmp 0x0600000C
      0x2A,                               // 65 ret
  };
  ByteVec Before = Code;
  std::vector<uint32_t> Seen;
  Error E =
      rewriteILTokens(Code, [&](const ILInstruction &I) -> Expected<uint32_t> {
        Seen.push_back(I.Token);
        EXPECT_EQ(readU32(Before, I.TokenOffset), I.Token);
        return (I.Token & 0xFF000000) | ((I.Token & 0xFFFFFF) + 0x100);
      });
  ASSERT_THAT_ERROR(std::move(E), Succeeded());
  EXPECT_EQ(Seen, (std::vector<uint32_t>{0x04000001, 0x04000002, 0x01000003,
                                         0x02000004, 0x70000005, 0x0A000006,
                                         0x06000007, 0x0A000008, 0x11000009,
                                         0x1B00000A, 0x0A00000B, 0x0600000C}));
  ASSERT_EQ(Code.size(), Before.size());
  for (uint32_t Off :
       {1u, 7u, 12u, 17u, 22u, 27u, 33u, 38u, 43u, 49u, 54u, 61u}) {
    SCOPED_TRACE(Off);
    uint32_t Old = readU32(Before, Off);
    EXPECT_EQ(readU32(Code, Off),
              (Old & 0xFF000000) | ((Old & 0xFFFFFF) + 0x100));
    for (uint32_t K = 0; K != 4; ++K)
      Before[Off + K] = Code[Off + K];
  }
  EXPECT_EQ(Code, Before) << "only token bytes change";
  // The rewritten code decodes to the same instructions.
  EXPECT_EQ(decodeAll(Code).size(), 15u);
}

TEST(CLIILDecoderTest, RewriteTokensErrors) {
  ByteVec Code = {0x28, 0x01, 0x00, 0x00, 0x06, 0x28,
                  0x02, 0x00, 0x00, 0x06, 0x2A};
  unsigned Calls = 0;
  Error E =
      rewriteILTokens(Code, [&](const ILInstruction &I) -> Expected<uint32_t> {
        ++Calls;
        if (I.Token == 0x06000002)
          return createStringError(inconvertibleErrorCode(),
                                   "no such method 0x06000002");
        return I.Token;
      });
  EXPECT_THAT_ERROR(std::move(E),
                    FailedWithMessage("no such method 0x06000002"));
  EXPECT_EQ(Calls, 2u);

  // Malformed code is an error before or while tokens are mapped.
  for (const char *Hex : {"28 01 00", "24 28 01 00 00 06", "FE 08"}) {
    SCOPED_TRACE(Hex);
    ByteVec Bad = fromHex(Hex);
    EXPECT_THAT_ERROR(
        rewriteILTokens(Bad,
                        [](const ILInstruction &I) -> Expected<uint32_t> {
                          return I.Token;
                        }),
        FailedWithMessage(testing::StartsWith("invalid IL: ")));
  }
}

} // namespace
