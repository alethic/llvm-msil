//===- MSILMCCodeEmitterTest.cpp - The CIL encoder and asm backend --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The binary encoding of the MSIL target (ImplementationPlan.md M18, recon
// mc-streamer 5.4 and 5.5, ECMA-335 III.1.2 and III.1.9), through the code
// emitter and asm backend that the target registry creates, as llc and the
// object streamer of M19 create them:
//
// - MSILMCCodeEmitter: one CIL instruction per MCInst, the opcode in one byte
//   or FE and one byte, the inline operand little-endian by its OperandKind
//   (floats as their exact bits, signaling NaNs kept), a token as four zero
//   bytes and a fixup_cil_<class>_token fixup, a branch as a PC-relative
//   FK_Data_1 or FK_Data_4 fixup on label - size, and a switch as its count
//   and one PC-relative FK_Data_4 fixup per entry on label - 4 * (N - i), so
//   that every displacement counts from the end of the instruction as ilasm's
//   do (recon mc-streamer E3). getCILInstSize equals the encoded size for
//   every CIL_ record.
// - MSILAsmBackend: the six token fixup kinds, no relaxation, nop padding,
//   the object target writer it creates, and fixups applied through the
//   MSIL object streamer: branches and switch entries resolve inside their
//   method, a short branch out of range and a branch out of its method are
//   errors.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILFixupKinds.h"
#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "MSILMCInstLower.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCMSILObjectWriter.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionMSIL.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"

#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using cli::Opcode;
using cli::OperandKind;
using Bytes = std::vector<uint8_t>;

namespace {

bool isTokenKind(OperandKind K) {
  switch (K) {
  case OperandKind::InlineMethod:
  case OperandKind::InlineField:
  case OperandKind::InlineType:
  case OperandKind::InlineTok:
  case OperandKind::InlineSig:
  case OperandKind::InlineString:
    return true;
  default:
    return false;
  }
}

// The fixup kind of each token operand class (mc-streamer 5.5).
MCFixupKind tokenFixupKind(OperandKind K) {
  switch (K) {
  case OperandKind::InlineMethod:
    return MCFixupKind(MSIL::fixup_cil_method_token);
  case OperandKind::InlineField:
    return MCFixupKind(MSIL::fixup_cil_field_token);
  case OperandKind::InlineSig:
    return MCFixupKind(MSIL::fixup_cil_sig_token);
  case OperandKind::InlineType:
    return MCFixupKind(MSIL::fixup_cil_type_token);
  case OperandKind::InlineTok:
    return MCFixupKind(MSIL::fixup_cil_tok_token);
  case OperandKind::InlineString:
    return MCFixupKind(MSIL::fixup_cil_string_token);
  default:
    ADD_FAILURE() << "not a token operand kind";
    return FK_NONE;
  }
}

// The opcode bytes of \p Op: one byte, or FE and the low byte.
Bytes opcodeBytes(Opcode Op) {
  uint16_t V = uint16_t(Op);
  if (V < 0x100)
    return {uint8_t(V)};
  return {cli::TwoByteOpcodePrefix, uint8_t(V & 0xFF)};
}

Bytes concat(Bytes A, const Bytes &B) {
  A.insert(A.end(), B.begin(), B.end());
  return A;
}

std::string hex(ArrayRef<uint8_t> B) {
  std::string S;
  raw_string_ostream OS(S);
  for (uint8_t C : B)
    OS << format_hex_no_prefix(C, 2, /*Upper=*/true) << ' ';
  return S;
}

struct Diag {
  SourceMgr::DiagKind Kind;
  std::string Message;
};

// The MC environment of the MSIL target from the target registry, with the
// registered code emitter, and the context's diagnostics collected.
class MSILMCEncoderTestBase : public testing::Test {
protected:
  static void SetUpTestSuite() {
    LLVMInitializeMSILTargetInfo();
    LLVMInitializeMSILTargetMC();
  }

  void SetUp() override {
    std::string Error;
    TheTarget = TargetRegistry::lookupTarget(TT, Error);
    ASSERT_NE(nullptr, TheTarget) << Error;
    MRI.reset(TheTarget->createMCRegInfo(TT));
    ASSERT_TRUE(MRI);
    MAI.reset(TheTarget->createMCAsmInfo(*MRI, TT, MCOptions));
    ASSERT_TRUE(MAI);
    MII.reset(TheTarget->createMCInstrInfo());
    ASSERT_TRUE(MII);
    STI.reset(TheTarget->createMCSubtargetInfo(TT, "", ""));
    ASSERT_TRUE(STI);
    Ctx = std::make_unique<MCContext>(TT, *MAI, *MRI, *STI);
    MOFI.reset(TheTarget->createMCObjectFileInfo(*Ctx, /*PIC=*/false));
    Ctx->setObjectFileInfo(MOFI.get());
    Ctx->setDiagnosticHandler([this](const SMDiagnostic &D, bool,
                                     const SourceMgr &,
                                     std::vector<const MDNode *> &) {
      Diags.push_back({D.getKind(), D.getMessage().str()});
    });
    CE.reset(TheTarget->createMCCodeEmitter(*MII, *Ctx));
    ASSERT_TRUE(CE);
  }

  MCInst inst(Opcode Op) const {
    MCInst I;
    I.setOpcode(MSIL::getMCOpcodeForCIL(Op));
    return I;
  }

  MCInst inst(Opcode Op, MCOperand O) const {
    MCInst I = inst(Op);
    I.addOperand(O);
    return I;
  }

  MCInst imm(Opcode Op, int64_t V) const {
    return inst(Op, MCOperand::createImm(V));
  }

  MCInst expr(Opcode Op, const MCExpr *E) const {
    return inst(Op, MCOperand::createExpr(E));
  }

  const MCExpr *ref(const MCSymbol *S) const {
    return MCSymbolRefExpr::create(S, *Ctx);
  }

  MCInst switchOf(ArrayRef<const MCSymbol *> Targets) const {
    MCInst I = inst(Opcode::Switch);
    for (const MCSymbol *L : Targets)
      I.addOperand(MCOperand::createExpr(ref(L)));
    return I;
  }

  struct Encoding {
    Bytes Code;
    SmallVector<MCFixup, 4> Fixups;
  };

  Encoding encode(const MCInst &I) const {
    SmallVector<char, 32> CB;
    Encoding E;
    CE->encodeInstruction(I, CB, E.Fixups, *STI);
    for (char C : CB)
      E.Code.push_back(uint8_t(C));
    return E;
  }

  // The symbol and the constant of a fixup's value (Sym + Cst).
  std::pair<const MCSymbol *, int64_t> valueOf(const MCFixup &F) const {
    MCValue V;
    EXPECT_TRUE(F.getValue()->evaluateAsRelocatable(V, nullptr));
    EXPECT_EQ(nullptr, V.getSubSym());
    return {V.getAddSym(), V.getConstant()};
  }

  // Checks one fixup: kind, offset from the start of the instruction,
  // PC-relative flag, and the symbol and constant of its value.
  void expectFixup(const MCFixup &F, MCFixupKind Kind, uint32_t Offset,
                   bool PCRel, const MCSymbol *Sym, int64_t Addend) const {
    EXPECT_EQ(Kind, F.getKind());
    EXPECT_EQ(Offset, F.getOffset());
    EXPECT_EQ(PCRel, F.isPCRel());
    auto [S, C] = valueOf(F);
    EXPECT_EQ(Sym, S);
    EXPECT_EQ(Addend, C);
  }

  std::vector<std::string> errors() const {
    std::vector<std::string> E;
    for (const Diag &D : Diags)
      if (D.Kind == SourceMgr::DK_Error)
        E.push_back(D.Message);
    return E;
  }

  Triple TT{"x86_64-pc-windows-msvc-msil"};
  const Target *TheTarget = nullptr;
  MCTargetOptions MCOptions;
  std::unique_ptr<MCRegisterInfo> MRI;
  std::unique_ptr<MCAsmInfo> MAI;
  std::unique_ptr<MCInstrInfo> MII;
  std::unique_ptr<MCSubtargetInfo> STI;
  std::unique_ptr<MCContext> Ctx;
  std::unique_ptr<MCObjectFileInfo> MOFI;
  std::vector<Diag> Diags;
  std::unique_ptr<MCCodeEmitter> CE;
};

//===----------------------------------------------------------------------===//
// MSILMCCodeEmitter
//===----------------------------------------------------------------------===//

class MSILMCCodeEmitter : public MSILMCEncoderTestBase {};

TEST_F(MSILMCCodeEmitter, RegisteredWithTheTarget) {
  EXPECT_TRUE(TheTarget->hasMCAsmBackend());
  std::unique_ptr<MCAsmBackend> MAB(
      TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions));
  EXPECT_NE(nullptr, MAB.get());
  // Both x86_64 MSIL triples reach the same emitter.
  std::string Error;
  EXPECT_EQ(TheTarget, TargetRegistry::lookupTarget(
                           Triple("x86_64-unknown-linux-gnu-msil"), Error));
}

// The byte sequences of the plan's list (M18.c) and of ECMA-335's one- and
// two-byte opcodes with each small operand size.
TEST_F(MSILMCCodeEmitter, OpcodeAndOperandBytes) {
  const struct {
    MCInst I;
    Bytes Want;
  } Cases[] = {
      {imm(Opcode::Ldc_i4_s, -5), {0x1F, 0xFB}},
      {imm(Opcode::Ldarg, 300), {0xFE, 0x09, 0x2C, 0x01}},
      {inst(Opcode::Ceq), {0xFE, 0x01}},
      {inst(Opcode::Cpblk), {0xFE, 0x17}},
      {imm(Opcode::Unaligned, 1), {0xFE, 0x12, 0x01}},
      {inst(Opcode::Volatile), {0xFE, 0x13}},
      {imm(Opcode::Ldc_i8, int64_t(0x0123456789ABCDEFull)),
       {0x21, 0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01}},
      {imm(Opcode::Ldc_i8, -2),
       {0x21, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}},
      {imm(Opcode::Ldc_i4, 0x12345678), {0x20, 0x78, 0x56, 0x34, 0x12}},
      {imm(Opcode::Ldc_i4, -129), {0x20, 0x7F, 0xFF, 0xFF, 0xFF}},
      {imm(Opcode::Ldc_i4_s, -128), {0x1F, 0x80}},
      {imm(Opcode::Ldc_i4_s, 127), {0x1F, 0x7F}},
      {imm(Opcode::Ldarg_s, 255), {0x0E, 0xFF}},
      {imm(Opcode::Ldloc_s, 4), {0x11, 0x04}},
      {imm(Opcode::Stloc, 65534), {0xFE, 0x0E, 0xFE, 0xFF}},
      {imm(Opcode::Ldloca, 256), {0xFE, 0x0D, 0x00, 0x01}},
      {imm(Opcode::Starg_s, 0), {0x10, 0x00}},
      {imm(Opcode::Unaligned, 4), {0xFE, 0x12, 0x04}},
      {imm(Opcode::No, 1), {0xFE, 0x19, 0x01}},
      {inst(Opcode::Nop), {0x00}},
      {inst(Opcode::Ldarg_0), {0x02}},
      {inst(Opcode::Ldc_i4_m1), {0x15}},
      {inst(Opcode::Ret), {0x2A}},
      {inst(Opcode::Conv_u), {0xE0}},
      {inst(Opcode::Localloc), {0xFE, 0x0F}},
      {inst(Opcode::Readonly), {0xFE, 0x1E}},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(MII->getName(C.I.getOpcode()).str());
    Encoding E = encode(C.I);
    EXPECT_EQ(hex(C.Want), hex(E.Code));
    EXPECT_TRUE(E.Fixups.empty());
    EXPECT_EQ(C.Want.size(), MSIL::getCILInstSize(C.I, *MII));
  }
  EXPECT_TRUE(errors().empty());
}

// Float operands are written as the exact bits they carry: the float32
// signaling NaN 0x7FA00000, which both ilasms quiet to 0x7FE00000 (recon
// mc-streamer E4), stays as it is; so do the float64 one and -0.
TEST_F(MSILMCCodeEmitter, FloatOperandsKeepTheirBits) {
  const struct {
    MCInst I;
    Bytes Want;
  } Cases[] = {
      {inst(Opcode::Ldc_r4, MCOperand::createSFPImm(0x7FA00000)),
       {0x22, 0x00, 0x00, 0xA0, 0x7F}},
      {inst(Opcode::Ldc_r4, MCOperand::createSFPImm(0x80000000)),
       {0x22, 0x00, 0x00, 0x00, 0x80}},
      {inst(Opcode::Ldc_r4, MCOperand::createSFPImm(0x3F800000)),
       {0x22, 0x00, 0x00, 0x80, 0x3F}},
      {inst(Opcode::Ldc_r8, MCOperand::createDFPImm(0x7FF4000000000000ull)),
       {0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF4, 0x7F}},
      {inst(Opcode::Ldc_r8, MCOperand::createDFPImm(0x3FF0000000000001ull)),
       {0x23, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F}},
  };
  for (const auto &C : Cases) {
    Encoding E = encode(C.I);
    EXPECT_EQ(hex(C.Want), hex(E.Code));
    EXPECT_TRUE(E.Fixups.empty());
  }
}

// Method, field and signature tokens (the classes Phase 1 code uses, recon
// mc-streamer E2): four zero bytes after the opcode and a fixup of the
// operand's token class on the expression itself, not PC-relative.
TEST_F(MSILMCCodeEmitter, MethodFieldAndSigTokenFixups) {
  MSIL::Signature Sig;
  Sig.Results.push_back(MSIL::ValType::I32);
  Sig.Params.push_back(MSIL::ValType::I32);
  const MCExpr *Method = MSILMemberRefExpr::createMethod("f", &Sig, *Ctx);
  const MCExpr *Extern = MSILMemberRefExpr::createExternMethod("e", &Sig, *Ctx);
  const MCExpr *BCL = MSILMemberRefExpr::createBCL(
      cli::BCLMemberId::Interlocked_MemoryBarrier, *Ctx);
  const MCExpr *Field = MSILMemberRefExpr::createField("g", *Ctx);
  const MCExpr *ExtField = MSILMemberRefExpr::createExternField("x", *Ctx);
  const MCExpr *FnAddr = MSILMemberRefExpr::createFnAddr("f", *Ctx);
  const MCExpr *StandAlone = MSILMemberRefExpr::createSig(&Sig, *Ctx);
  const MCFixupKind MethodK = MCFixupKind(MSIL::fixup_cil_method_token);
  const MCFixupKind FieldK = MCFixupKind(MSIL::fixup_cil_field_token);
  const MCFixupKind SigK = MCFixupKind(MSIL::fixup_cil_sig_token);
  const struct {
    Opcode Op;
    const MCExpr *E;
    MCFixupKind Kind;
  } Cases[] = {
      {Opcode::Call, Method, MethodK},  {Opcode::Call, Extern, MethodK},
      {Opcode::Call, BCL, MethodK},     {Opcode::Ldftn, Method, MethodK},
      {Opcode::Ldsfld, Field, FieldK},  {Opcode::Ldsfld, ExtField, FieldK},
      {Opcode::Ldsfld, FnAddr, FieldK}, {Opcode::Stsfld, Field, FieldK},
      {Opcode::Ldsflda, Field, FieldK}, {Opcode::Calli, StandAlone, SigK},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(cli::getOpcodeInfo(C.Op).Name.str());
    Encoding E = encode(expr(C.Op, C.E));
    Bytes Op = opcodeBytes(C.Op);
    EXPECT_EQ(hex(concat(Op, {0, 0, 0, 0})), hex(E.Code));
    ASSERT_EQ(1u, E.Fixups.size());
    const MCFixup &F = E.Fixups[0];
    EXPECT_EQ(C.Kind, F.getKind());
    EXPECT_EQ(Op.size(), F.getOffset());
    EXPECT_FALSE(F.isPCRel());
    // The writer reads the operand class from the expression (M19).
    EXPECT_EQ(C.E, F.getValue());
  }
  // 'call int32 'f'(int32)' is 28 then the token at offset 1.
  Encoding Call = encode(expr(Opcode::Call, Method));
  EXPECT_EQ(hex({0x28, 0, 0, 0, 0}), hex(Call.Code));
  ASSERT_EQ(1u, Call.Fixups.size());
  EXPECT_EQ(1u, Call.Fixups[0].getOffset());
}

// The token classes no Phase 1 code uses yet still get their own fixup kind.
TEST_F(MSILMCCodeEmitter, TypeTokAndStringTokenFixups) {
  const MCExpr *T =
      MSILMemberRefExpr::createBCL(cli::BCLMemberId::ValueType, *Ctx);
  const MCExpr *S = ref(Ctx->getOrCreateSymbol("str"));
  const struct {
    Opcode Op;
    const MCExpr *E;
    MCFixupKind Kind;
  } Cases[] = {
      {Opcode::Sizeof, T, MCFixupKind(MSIL::fixup_cil_type_token)},
      {Opcode::Box, T, MCFixupKind(MSIL::fixup_cil_type_token)},
      {Opcode::Ldtoken, T, MCFixupKind(MSIL::fixup_cil_tok_token)},
      {Opcode::Ldstr, S, MCFixupKind(MSIL::fixup_cil_string_token)},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(cli::getOpcodeInfo(C.Op).Name.str());
    Encoding E = encode(expr(C.Op, C.E));
    Bytes Op = opcodeBytes(C.Op);
    EXPECT_EQ(hex(concat(Op, {0, 0, 0, 0})), hex(E.Code));
    ASSERT_EQ(1u, E.Fixups.size());
    EXPECT_EQ(C.Kind, E.Fixups[0].getKind());
    EXPECT_EQ(Op.size(), E.Fixups[0].getOffset());
    EXPECT_FALSE(E.Fixups[0].isPCRel());
  }
}

// A token operand given as an immediate is a final token (ILAsm's
// 'mdtoken(0x...)', the inline-IL path): written as it is, no fixup.
TEST_F(MSILMCCodeEmitter, ImmediateTokensAreWrittenAsIs) {
  Encoding Call = encode(imm(Opcode::Call, 0x0A000001));
  EXPECT_EQ(hex({0x28, 0x01, 0x00, 0x00, 0x0A}), hex(Call.Code));
  EXPECT_TRUE(Call.Fixups.empty());
  Encoding Field = encode(imm(Opcode::Ldsfld, 0x04000002));
  EXPECT_EQ(hex({0x7E, 0x02, 0x00, 0x00, 0x04}), hex(Field.Code));
  EXPECT_TRUE(Field.Fixups.empty());
  Encoding Ftn = encode(imm(Opcode::Ldftn, 0x06000003));
  EXPECT_EQ(hex({0xFE, 0x06, 0x03, 0x00, 0x00, 0x06}), hex(Ftn.Code));
  EXPECT_TRUE(Ftn.Fixups.empty());
}

// A short branch: one byte and a PC-relative FK_Data_1 fixup at offset 1 on
// label - 1, so the value is the displacement from the next instruction.
TEST_F(MSILMCCodeEmitter, ShortBranchFixup) {
  const MCSymbol *L = Ctx->createTempSymbol();
  for (Opcode Op : {Opcode::Br_s, Opcode::Brtrue_s, Opcode::Brfalse_s,
                    Opcode::Blt_un_s, Opcode::Beq_s, Opcode::Leave_s}) {
    SCOPED_TRACE(cli::getOpcodeInfo(Op).Name.str());
    Encoding E = encode(expr(Op, ref(L)));
    EXPECT_EQ(hex(concat(opcodeBytes(Op), {0})), hex(E.Code));
    ASSERT_EQ(1u, E.Fixups.size());
    expectFixup(E.Fixups[0], FK_Data_1, 1, /*PCRel=*/true, L, -1);
  }
}

// A long branch: four bytes and a PC-relative FK_Data_4 fixup at offset 1 on
// label - 4.
TEST_F(MSILMCCodeEmitter, LongBranchFixup) {
  const MCSymbol *L = Ctx->createTempSymbol();
  for (Opcode Op : {Opcode::Br, Opcode::Brtrue, Opcode::Brfalse, Opcode::Bge_un,
                    Opcode::Bne_un, Opcode::Leave}) {
    SCOPED_TRACE(cli::getOpcodeInfo(Op).Name.str());
    Encoding E = encode(expr(Op, ref(L)));
    EXPECT_EQ(hex(concat(opcodeBytes(Op), {0, 0, 0, 0})), hex(E.Code));
    ASSERT_EQ(1u, E.Fixups.size());
    expectFixup(E.Fixups[0], FK_Data_4, 1, /*PCRel=*/true, L, -4);
  }
}

// A branch given a numeric displacement (ILAsm's 'br 300') is written as it
// is, with no fixup.
TEST_F(MSILMCCodeEmitter, NumericBranchDisplacements) {
  Encoding S = encode(imm(Opcode::Br_s, -2));
  EXPECT_EQ(hex({0x2B, 0xFE}), hex(S.Code));
  EXPECT_TRUE(S.Fixups.empty());
  Encoding L = encode(imm(Opcode::Br, 300));
  EXPECT_EQ(hex({0x38, 0x2C, 0x01, 0x00, 0x00}), hex(L.Code));
  EXPECT_TRUE(L.Fixups.empty());
  Encoding B = encode(imm(Opcode::Blt, -5));
  EXPECT_EQ(hex({0x3F, 0xFB, 0xFF, 0xFF, 0xFF}), hex(B.Code));
  EXPECT_TRUE(B.Fixups.empty());
}

// A switch: its count, then one 32-bit entry per target with a PC-relative
// FK_Data_4 fixup on label - 4 * (N - i), which makes each entry relative to
// the end of the whole instruction: a 3-target switch is 17 bytes with
// fixups at 5, 9 and 13 and addends -12, -8 and -4.
TEST_F(MSILMCCodeEmitter, SwitchFixups) {
  const MCSymbol *L1 = Ctx->createTempSymbol(), *L2 = Ctx->createTempSymbol(),
                 *L3 = Ctx->createTempSymbol();
  MCInst Sw = switchOf({L1, L2, L3});
  Encoding E = encode(Sw);
  EXPECT_EQ(
      hex({0x45, 0x03, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
      hex(E.Code));
  EXPECT_EQ(17u, MSIL::getCILInstSize(Sw, *MII));
  ASSERT_EQ(3u, E.Fixups.size());
  expectFixup(E.Fixups[0], FK_Data_4, 5, true, L1, -12);
  expectFixup(E.Fixups[1], FK_Data_4, 9, true, L2, -8);
  expectFixup(E.Fixups[2], FK_Data_4, 13, true, L3, -4);

  // One target: 9 bytes, the entry at 5 on label - 4.
  Encoding One = encode(switchOf({L2}));
  EXPECT_EQ(9u, One.Code.size());
  ASSERT_EQ(1u, One.Fixups.size());
  expectFixup(One.Fixups[0], FK_Data_4, 5, true, L2, -4);

  // No target: the count only.
  Encoding None = encode(switchOf({}));
  EXPECT_EQ(hex({0x45, 0x00, 0x00, 0x00, 0x00}), hex(None.Code));
  EXPECT_TRUE(None.Fixups.empty());

  // Numeric entries are written as they are.
  MCInst Num = inst(Opcode::Switch);
  Num.addOperand(MCOperand::createImm(0));
  Num.addOperand(MCOperand::createImm(-13));
  Encoding N = encode(Num);
  EXPECT_EQ(hex({0x45, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF3,
                 0xFF, 0xFF, 0xFF}),
            hex(N.Code));
  EXPECT_TRUE(N.Fixups.empty());
}

// For every CIL_ record, with label and token operands as expressions and as
// numbers and switches of 0 to 3 targets: the encoding starts with the
// opcode bytes, is getOpcodeSize + getOperandSize (+ 4 per switch entry)
// bytes long, equals getCILInstSize, and has the fixups of its operand kind
// at the operand's offset.
TEST_F(MSILMCCodeEmitter, SizeEqualsEncodingForEveryRecord) {
  const MCSymbol *L = Ctx->createTempSymbol();
  const MCExpr *Token = ref(Ctx->getOrCreateSymbol("token"));
  unsigned Records = 0;
  for (const cli::OpcodeInfo &Info : cli::getAllOpcodes()) {
    for (unsigned Variant = 0; Variant != 4; ++Variant) {
      bool Numeric = Variant == 1;
      unsigned NumTargets = Variant == 3 ? 0 : Variant + 1;
      if (Info.Operand != OperandKind::InlineSwitch && Variant > 1)
        break;
      SCOPED_TRACE(Info.Name.str() + " variant " + std::to_string(Variant));
      MCInst I = inst(Info.Op);
      switch (Info.Operand) {
      case OperandKind::InlineNone:
        break;
      case OperandKind::ShortInlineVar:
      case OperandKind::ShortInlineI:
        I.addOperand(MCOperand::createImm(1));
        break;
      case OperandKind::InlineVar:
        I.addOperand(MCOperand::createImm(300));
        break;
      case OperandKind::InlineI:
      case OperandKind::InlineI8:
        I.addOperand(MCOperand::createImm(-5));
        break;
      case OperandKind::ShortInlineR:
        I.addOperand(MCOperand::createSFPImm(0x3F800000));
        break;
      case OperandKind::InlineR:
        I.addOperand(MCOperand::createDFPImm(0x3FF0000000000000ull));
        break;
      case OperandKind::InlineSwitch:
        for (unsigned T = 0; T != NumTargets; ++T)
          I.addOperand(Numeric ? MCOperand::createImm(T)
                               : MCOperand::createExpr(ref(L)));
        break;
      default:
        if (isTokenKind(Info.Operand))
          I.addOperand(Numeric ? MCOperand::createImm(0x0A000001)
                               : MCOperand::createExpr(Token));
        else
          I.addOperand(Numeric ? MCOperand::createImm(0)
                               : MCOperand::createExpr(ref(L)));
        break;
      }
      Encoding E = encode(I);
      unsigned OpSize = cli::getOpcodeSize(Info.Op);
      unsigned Want = OpSize + cli::getOperandSize(Info.Operand);
      if (Info.Operand == OperandKind::InlineSwitch)
        Want += 4 * NumTargets;
      EXPECT_EQ(Want, E.Code.size());
      EXPECT_EQ(Want, MSIL::getCILInstSize(I, *MII));
      Bytes Op = opcodeBytes(Info.Op);
      ASSERT_GE(E.Code.size(), Op.size());
      EXPECT_EQ(hex(Op), hex(ArrayRef<uint8_t>(E.Code).take_front(Op.size())));

      unsigned WantFixups = 0;
      if (!Numeric) {
        if (Info.Operand == OperandKind::InlineSwitch)
          WantFixups = NumTargets;
        else if (isTokenKind(Info.Operand) ||
                 Info.Operand == OperandKind::ShortInlineBrTarget ||
                 Info.Operand == OperandKind::InlineBrTarget)
          WantFixups = 1;
      }
      ASSERT_EQ(WantFixups, E.Fixups.size());
      for (unsigned F = 0; F != WantFixups; ++F) {
        const MCFixup &Fx = E.Fixups[F];
        if (isTokenKind(Info.Operand)) {
          EXPECT_EQ(tokenFixupKind(Info.Operand), Fx.getKind());
          EXPECT_EQ(OpSize, Fx.getOffset());
          EXPECT_FALSE(Fx.isPCRel());
          EXPECT_EQ(Token, Fx.getValue());
        } else if (Info.Operand == OperandKind::InlineSwitch) {
          expectFixup(Fx, FK_Data_4, OpSize + 4 + 4 * F, true, L,
                      -4 * int64_t(NumTargets - F));
        } else if (Info.Operand == OperandKind::ShortInlineBrTarget) {
          expectFixup(Fx, FK_Data_1, OpSize, true, L, -1);
        } else {
          expectFixup(Fx, FK_Data_4, OpSize, true, L, -4);
        }
      }
      // Bytes under a fixup are zero.
      if (WantFixups)
        for (size_t B =
                 OpSize + (Info.Operand == OperandKind::InlineSwitch ? 4 : 0);
             B != E.Code.size(); ++B)
          EXPECT_EQ(0, E.Code[B]) << "byte " << B;
    }
    ++Records;
  }
  EXPECT_EQ(219u, Records);
  EXPECT_TRUE(errors().empty());
}

// An immediate that does not fit its operand is reported through the
// MCContext, naming the mnemonic and the value, and the encoding keeps its
// size; values at the limits are accepted.
TEST_F(MSILMCCodeEmitter, OutOfRangeImmediatesAreErrors) {
  const struct {
    MCInst I;
    const char *Message;
  } Bad[] = {
      {imm(Opcode::Ldarg_s, 256), "operand of 'ldarg.s' out of range: 256"},
      {imm(Opcode::Ldc_i4_s, 128), "operand of 'ldc.i4.s' out of range: 128"},
      {imm(Opcode::Ldc_i4_s, -129), "operand of 'ldc.i4.s' out of range: -129"},
      {imm(Opcode::Ldloc, 65536), "operand of 'ldloc' out of range: 65536"},
      {imm(Opcode::Unaligned, 256),
       "operand of 'unaligned.' out of range: 256"},
  };
  for (const auto &C : Bad) {
    SCOPED_TRACE(C.Message);
    Diags.clear();
    Encoding E = encode(C.I);
    EXPECT_EQ(MSIL::getCILInstSize(C.I, *MII), E.Code.size());
    std::vector<std::string> Errors = errors();
    ASSERT_EQ(1u, Errors.size());
    EXPECT_EQ(C.Message, Errors[0]);
  }
  Diags.clear();
  for (const MCInst &I :
       {imm(Opcode::Ldarg_s, 255), imm(Opcode::Ldarg_s, 0),
        imm(Opcode::Ldc_i4_s, 127), imm(Opcode::Ldc_i4_s, -128),
        imm(Opcode::Ldloc, 65535), imm(Opcode::Unaligned, 2),
        imm(Opcode::Ldc_i4, -2147483648LL), imm(Opcode::Ldc_i4, 2147483647)})
    encode(I);
  EXPECT_TRUE(errors().empty());
}

#if GTEST_HAS_DEATH_TEST
// Only the CIL_ records are CIL instructions (plan 5.4 as amended in section
// 12). Every other record never encodes as anything: the encoder and
// getCILInstSize stop with an internal error. That holds for the records
// with TSFlags value 0xFFFF (the multi-opcode MI-level records and the
// pseudos), for a generic TargetOpcode record, whose TSFlags value 0 must not
// become the nop that value 0 would be, and for an MI-level one-to-one record,
// whose TSFlags carry its cilop (ADD_I32: add's 0x58) but which MSILMCInstLower
// lowers to a CIL_ record. An operand list of the wrong shape stops it too.
TEST_F(MSILMCCodeEmitter, NonCILRecordsAreInternalErrors) {
  MCInst Multi;
  Multi.setOpcode(MSIL::GLOBAL_ADDR);
  Multi.addOperand(MCOperand::createExpr(ref(Ctx->getOrCreateSymbol("g"))));
  EXPECT_DEATH(encode(Multi), "LLVM ERROR");
  EXPECT_DEATH(MSIL::getCILInstSize(Multi, *MII), "LLVM ERROR");
  MCInst Pseudo;
  Pseudo.setOpcode(MSIL::ADJCALLSTACKDOWN);
  Pseudo.addOperand(MCOperand::createImm(0));
  Pseudo.addOperand(MCOperand::createImm(0));
  EXPECT_DEATH(encode(Pseudo), "LLVM ERROR");
  EXPECT_DEATH(encode(inst(Opcode::Ldc_i4)), "");

  MCInst Kill;
  Kill.setOpcode(TargetOpcode::KILL);
  EXPECT_DEATH(encode(Kill), "KILL.*is not a CIL instruction");
  EXPECT_DEATH(MSIL::getCILInstSize(Kill, *MII),
               "KILL.*is not a CIL instruction");
  MCInst OneToOne;
  OneToOne.setOpcode(MSIL::ADD_I32);
  EXPECT_DEATH(encode(OneToOne), "ADD_I32.*is not a CIL instruction");
  EXPECT_DEATH(MSIL::getCILInstSize(OneToOne, *MII),
               "ADD_I32.*is not a CIL instruction");
}
#endif

//===----------------------------------------------------------------------===//
// MSILAsmBackend
//===----------------------------------------------------------------------===//

// One recordRelocation call as the object writer receives it.
struct RecordedRelocation {
  MCFixupKind Kind;
  const MCSection *Section;
  uint64_t Offset; // From the start of the section.
  bool PCRel;
  const MCExpr *Value;
};

// An object writer that records every recordRelocation call and writes
// nothing, standing in for M19's writer: it shows what applyFixup hands the
// writer. FixedValue is left as the backend passes it.
class RecordingObjectWriter final : public MCObjectWriter {
public:
  explicit RecordingObjectWriter(std::vector<RecordedRelocation> &Relocs)
      : Relocs(Relocs) {}

  void recordRelocation(const MCFragment &F, const MCFixup &Fixup, MCValue,
                        uint64_t &) override {
    Relocs.push_back({Fixup.getKind(), F.getParent(),
                      Asm->getFragmentOffset(F) + Fixup.getOffset(),
                      Fixup.isPCRel(), Fixup.getValue()});
  }

  uint64_t writeObject() override { return 0; }

private:
  std::vector<RecordedRelocation> &Relocs;
};

class MSILAsmBackend : public MSILMCEncoderTestBase {
protected:
  void SetUp() override {
    MSILMCEncoderTestBase::SetUp();
    if (HasFatalFailure())
      return;
    MAB.reset(TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions));
    ASSERT_TRUE(MAB);
  }

  // An object streamer of the registry with this target's backend and code
  // emitter, and the object writer \p OW or, by default, the backend's own
  // writing to Out; returns the backend it owns.
  MCAsmBackend &
  makeObjectStreamer(std::unique_ptr<MCObjectWriter> OW = nullptr) {
    std::unique_ptr<MCAsmBackend> B(
        TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions));
    EXPECT_TRUE(B);
    MCAsmBackend &Ref = *B;
    if (!OW)
      OW = B->createObjectWriter(Out);
    EXPECT_TRUE(OW);
    Streamer.reset(TheTarget->createMCObjectStreamer(
        TT, *Ctx, std::move(B), std::move(OW),
        std::unique_ptr<MCCodeEmitter>(
            TheTarget->createMCCodeEmitter(*MII, *Ctx)),
        *STI));
    EXPECT_TRUE(Streamer);
    return Ref;
  }

  MCSection *method(StringRef Name) {
    return Ctx->getMSILSection(Name, SectionKind::getText());
  }

  void emit(const MCInst &I) { Streamer->emitInstruction(I, *STI); }

  void nops(unsigned N) {
    for (unsigned I = 0; I != N; ++I)
      emit(inst(Opcode::Nop));
  }

  // The bytes of a section after the assembler has applied its fixups.
  static Bytes contents(MCSection &Sec) {
    Bytes B;
    for (MCFragment &F : Sec) {
      for (char C : F.getContents())
        B.push_back(uint8_t(C));
      for (char C : F.getVarContents())
        B.push_back(uint8_t(C));
    }
    return B;
  }

  // The errors other than MSILObjectWriter's for an object without the code
  // generator's events (M19): these streamers emit instructions only, never
  // emitObjectHeader or emitLinkInfo.
  std::vector<std::string> fixupErrors() const {
    std::vector<std::string> E;
    for (const std::string &M : errors())
      if (!StringRef(M).starts_with("MSIL: a CIL object is written from the "
                                    "events of the code generator"))
        E.push_back(M);
    return E;
  }

  std::unique_ptr<MCAsmBackend> MAB;
  SmallString<256> Object;
  raw_svector_ostream Out{Object};
  // What a RecordingObjectWriter of Streamer records; declared first, so it
  // outlives the streamer.
  std::vector<RecordedRelocation> Relocs;
  std::unique_ptr<MCStreamer> Streamer;
};

// One fixup kind per token operand class, each written as 32 bits at the
// start of its field with no flags.
TEST_F(MSILAsmBackend, FixupKindNamesAndSizes) {
  EXPECT_EQ(FirstTargetFixupKind, MCFixupKind(MSIL::fixup_cil_method_token));
  EXPECT_EQ(6, int(MSIL::NumTargetFixupKinds));
  EXPECT_EQ(int(FirstTargetFixupKind) + 6, int(MSIL::LastTargetFixupKind));
  const struct {
    MSIL::Fixups Kind;
    const char *Name;
  } Kinds[] = {
      {MSIL::fixup_cil_method_token, "fixup_cil_method_token"},
      {MSIL::fixup_cil_field_token, "fixup_cil_field_token"},
      {MSIL::fixup_cil_sig_token, "fixup_cil_sig_token"},
      {MSIL::fixup_cil_type_token, "fixup_cil_type_token"},
      {MSIL::fixup_cil_tok_token, "fixup_cil_tok_token"},
      {MSIL::fixup_cil_string_token, "fixup_cil_string_token"},
  };
  for (const auto &K : Kinds) {
    SCOPED_TRACE(K.Name);
    MCFixupKindInfo Info = MAB->getFixupKindInfo(MCFixupKind(K.Kind));
    ASSERT_NE(nullptr, Info.Name);
    EXPECT_EQ(StringRef(K.Name), StringRef(Info.Name));
    EXPECT_EQ(0u, Info.TargetOffset);
    EXPECT_EQ(32u, Info.TargetSize);
    EXPECT_EQ(0u, Info.Flags);
  }
  // The generic data kinds that branches and switch entries use keep their
  // sizes.
  EXPECT_EQ(8u, MAB->getFixupKindInfo(FK_Data_1).TargetSize);
  EXPECT_EQ(32u, MAB->getFixupKindInfo(FK_Data_4).TargetSize);
}

// msil-ld never relaxes and MC chooses the branch forms before emission, so
// no instruction needs relaxation; padding, never needed, is nop (0x00).
TEST_F(MSILAsmBackend, NoRelaxationAndNopPadding) {
  const MCSymbol *L = Ctx->createTempSymbol();
  for (Opcode Op : {Opcode::Br_s, Opcode::Br, Opcode::Blt_un_s, Opcode::Call,
                    Opcode::Nop, Opcode::Switch}) {
    SCOPED_TRACE(cli::getOpcodeInfo(Op).Name.str());
    MCInst I = inst(Op);
    if (Op != Opcode::Nop)
      I.addOperand(MCOperand::createExpr(ref(L)));
    EXPECT_FALSE(MAB->mayNeedRelaxation(I.getOpcode(), I.getOperands(), *STI));
  }
  SmallString<8> Pad;
  raw_svector_ostream OS(Pad);
  EXPECT_TRUE(MAB->writeNopData(OS, 3, STI.get()));
  EXPECT_EQ(StringRef("\0\0\0", 3), Pad.str());
}

// The backend remembers the target writer it created last, which M19's
// streamer reaches through MSIL::getObjectTargetWriter; from M19 it is an
// MSILObjectWriter.
TEST_F(MSILAsmBackend, RemembersItsObjectTargetWriter) {
  EXPECT_EQ(nullptr, MSIL::getObjectTargetWriter(*MAB));
  SmallString<16> Buf1, Buf2;
  raw_svector_ostream OS1(Buf1), OS2(Buf2);
  std::unique_ptr<MCObjectWriter> W1 = MAB->createObjectWriter(OS1);
  ASSERT_TRUE(W1);
  MCMSILObjectTargetWriter *T1 = MSIL::getObjectTargetWriter(*MAB);
  ASSERT_NE(nullptr, T1);
  EXPECT_EQ(Triple::MSIL, T1->getFormat());
  std::unique_ptr<MCObjectWriter> W2 = MAB->createObjectWriter(OS2);
  ASSERT_TRUE(W2);
  MCMSILObjectTargetWriter *T2 = MSIL::getObjectTargetWriter(*MAB);
  ASSERT_NE(nullptr, T2);
  EXPECT_NE(T1, T2);
}

// Through the MSIL object streamer: branches and switch entries resolve to
// displacements from the end of their instruction, token bytes stay zero for
// the writer (relocations), and every symbol a token names is registered
// with the assembler.
//   L0: br.s L1; nop; br L0; switch (L0, L1); L1: call 'ext'; ret
TEST_F(MSILAsmBackend, ObjectStreamerResolvesBranches) {
  makeObjectStreamer();
  MCSection *Text = method("'f'");
  Streamer->switchSection(Text);
  MCSymbol *L0 = Ctx->createTempSymbol(), *L1 = Ctx->createTempSymbol();
  MSIL::Signature Sig;
  const MSILMemberRefExpr *Ext =
      MSILMemberRefExpr::createExternMethod("ext", &Sig, *Ctx);
  const MSILMemberRefExpr *Barrier = MSILMemberRefExpr::createBCL(
      cli::BCLMemberId::Interlocked_MemoryBarrier, *Ctx);
  Streamer->emitLabel(L0);
  emit(expr(Opcode::Br_s, ref(L1)));
  emit(inst(Opcode::Nop));
  emit(expr(Opcode::Br, ref(L0)));
  emit(switchOf({L0, L1}));
  Streamer->emitLabel(L1);
  emit(expr(Opcode::Call, Ext));
  emit(expr(Opcode::Call, Barrier));
  emit(inst(Opcode::Ret));
  Streamer->finish();

  // br.s at 0: L1 (21) - 2 = 0x13; br at 3: L0 (0) - 8 = -8; switch at 8
  // ends at 21: L0 - 21 = -21, L1 - 21 = 0.
  EXPECT_EQ(
      hex({0x2B, 0x13, 0x00, 0x38, 0xF8, 0xFF, 0xFF, 0xFF, 0x45, 0x02, 0x00,
           0x00, 0x00, 0xEB, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x28,
           0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x2A}),
      hex(contents(*Text)));
  EXPECT_TRUE(fixupErrors().empty())
      << (fixupErrors().empty() ? "" : fixupErrors()[0]);
  ASSERT_NE(nullptr, Ext->getSymbol());
  EXPECT_TRUE(Ext->getSymbol()->isRegistered());
  ASSERT_NE(nullptr, Barrier->getSymbol());
  EXPECT_TRUE(Barrier->getSymbol()->isRegistered());
}

// applyFixup sends every token fixup to the object writer's recordRelocation
// (M18.b), exactly once, with its kind, its offset in the method's section
// and the operand's MSILMemberRefExpr as the value: that is how M19's writer
// learns which member each token names. Branches and switch entries resolve
// inside their method and never reach the writer. What applyFixup does with
// the FixedValue the writer returns is left to M19's writer.
//   L0: call 'callee'; ldsfld 'g'; calli <sig>; br.s L1; br L0;
//       switch (L0, L1); L1: ret
TEST_F(MSILAsmBackend, TokenFixupsReachTheObjectWriter) {
  makeObjectStreamer(std::make_unique<RecordingObjectWriter>(Relocs));
  MCSection *Text = method("'f'");
  Streamer->switchSection(Text);
  MSIL::Signature Sig;
  Sig.Results.push_back(MSIL::ValType::I32);
  Sig.Params.push_back(MSIL::ValType::I32);
  const MSILMemberRefExpr *Method =
      MSILMemberRefExpr::createMethod("callee", &Sig, *Ctx);
  const MSILMemberRefExpr *Field = MSILMemberRefExpr::createField("g", *Ctx);
  const MSILMemberRefExpr *CallSig = MSILMemberRefExpr::createSig(&Sig, *Ctx);
  MCSymbol *L0 = Ctx->createTempSymbol(), *L1 = Ctx->createTempSymbol();
  Streamer->emitLabel(L0);
  emit(expr(Opcode::Call, Method));   // 0, token at 1
  emit(expr(Opcode::Ldsfld, Field));  // 5, token at 6
  emit(expr(Opcode::Calli, CallSig)); // 10, token at 11
  emit(expr(Opcode::Br_s, ref(L1)));  // 15
  emit(expr(Opcode::Br, ref(L0)));    // 17
  emit(switchOf({L0, L1}));           // 22, ends at 35
  Streamer->emitLabel(L1);
  emit(inst(Opcode::Ret));
  Streamer->finish();
  EXPECT_TRUE(errors().empty()) << (errors().empty() ? "" : errors()[0]);

  const struct {
    MSIL::Fixups Kind;
    uint64_t Offset;
    const MSILMemberRefExpr *Value;
    MSILMemberRefExpr::Kind ExprKind;
    StringRef Name;
  } Want[] = {
      {MSIL::fixup_cil_method_token, 1, Method, MSILMemberRefExpr::Method,
       "callee"},
      {MSIL::fixup_cil_field_token, 6, Field, MSILMemberRefExpr::Field, "g"},
      {MSIL::fixup_cil_sig_token, 11, CallSig, MSILMemberRefExpr::StandAloneSig,
       ""},
  };
  // One call per token fixup and no other: no branch or switch entry
  // (PC-relative FK_Data_1 or FK_Data_4 on a label) reaches the writer.
  EXPECT_EQ(std::size(Want), Relocs.size());
  for (const RecordedRelocation &R : Relocs) {
    SCOPED_TRACE("relocation at " + std::to_string(R.Offset));
    EXPECT_FALSE(R.PCRel);
    EXPECT_NE(FK_Data_1, R.Kind);
    EXPECT_NE(FK_Data_4, R.Kind);
    EXPECT_GE(R.Kind, FirstTargetFixupKind);
  }
  for (const auto &W : Want) {
    SCOPED_TRACE("token at " + std::to_string(W.Offset));
    auto AtOffset = [&](const RecordedRelocation &R) {
      return R.Offset == W.Offset;
    };
    EXPECT_EQ(1, count_if(Relocs, AtOffset));
    auto R = find_if(Relocs, AtOffset);
    if (R == Relocs.end())
      continue; // Reported by the count.
    EXPECT_EQ(MCFixupKind(W.Kind), R->Kind);
    EXPECT_EQ(Text, R->Section);
    EXPECT_FALSE(R->PCRel);
    EXPECT_EQ(W.Value, R->Value);
    const auto *E = dyn_cast_or_null<MSILMemberRefExpr>(R->Value);
    ASSERT_NE(nullptr, E);
    EXPECT_EQ(int(W.ExprKind), int(E->getKind()));
    EXPECT_EQ(W.Name, E->getName());
  }

  // The branches and the switch were resolved in place: br.s at 15 to L1
  // (35) is 35 - 17; br at 17 to L0 is 0 - 22; the switch ends at 35.
  Bytes B = contents(*Text);
  ASSERT_EQ(36u, B.size());
  ArrayRef<uint8_t> Code(B);
  EXPECT_EQ(hex({0x2B, 0x12}), hex(Code.slice(15, 2)));
  EXPECT_EQ(hex({0x38, 0xEA, 0xFF, 0xFF, 0xFF}), hex(Code.slice(17, 5)));
  EXPECT_EQ(hex({0x45, 0x02, 0x00, 0x00, 0x00, 0xDD, 0xFF, 0xFF, 0xFF, 0x00,
                 0x00, 0x00, 0x00}),
            hex(Code.slice(22, 13)));
}

// The short branch boundaries in an object: +127 and -128 fit; +128 and
// -129 are 'short branch out of range'.
TEST_F(MSILAsmBackend, ShortBranchOutOfRangeIsAnError) {
  makeObjectStreamer();
  MCSection *Fits = method("'fits'");
  Streamer->switchSection(Fits);
  MCSymbol *Fwd = Ctx->createTempSymbol(), *Back = Ctx->createTempSymbol();
  emit(expr(Opcode::Br_s, ref(Fwd)));
  nops(127);
  Streamer->emitLabel(Fwd);
  Streamer->emitLabel(Back);
  nops(126);
  emit(expr(Opcode::Brtrue_s, ref(Back)));
  emit(inst(Opcode::Ret));

  MCSection *Far = method("'far'");
  Streamer->switchSection(Far);
  MCSymbol *Fwd2 = Ctx->createTempSymbol(), *Back2 = Ctx->createTempSymbol();
  emit(expr(Opcode::Br_s, ref(Fwd2)));
  nops(128);
  Streamer->emitLabel(Fwd2);
  emit(inst(Opcode::Ret));
  Streamer->emitLabel(Back2);
  nops(127);
  emit(expr(Opcode::Brfalse_s, ref(Back2)));
  emit(inst(Opcode::Ret));
  Streamer->finish();

  Bytes F = contents(*Fits);
  ASSERT_EQ(2u + 127 + 126 + 2 + 1, F.size());
  EXPECT_EQ(0x7F, F[1]);
  EXPECT_EQ(0x80, F[2 + 127 + 126 + 1]);
  std::vector<std::string> Errors = fixupErrors();
  ASSERT_EQ(2u, Errors.size());
  for (const std::string &E : Errors)
    EXPECT_NE(std::string::npos, E.find("short branch out of range")) << E;
}

// A branch to a label in another method's section cannot be resolved inside
// its method: 'branch target outside its method'.
TEST_F(MSILAsmBackend, BranchOutsideItsMethodIsAnError) {
  makeObjectStreamer();
  MCSymbol *Elsewhere = Ctx->createTempSymbol();
  Streamer->switchSection(method("'g'"));
  Streamer->emitLabel(Elsewhere);
  emit(inst(Opcode::Ret));
  Streamer->switchSection(method("'f'"));
  emit(expr(Opcode::Br, ref(Elsewhere)));
  emit(inst(Opcode::Ret));
  Streamer->finish();
  std::vector<std::string> Errors = fixupErrors();
  ASSERT_EQ(1u, Errors.size());
  EXPECT_NE(std::string::npos,
            Errors[0].find("branch target outside its method"))
      << Errors[0];
}

} // namespace
