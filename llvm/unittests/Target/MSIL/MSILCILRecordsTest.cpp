//===- MSILCILRecordsTest.cpp - CIL records, BCL list and branch forms ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The MC-level CIL instruction records of M18 and what is built on them
// (ImplementationPlan.md M18, 5.4 as amended in section 12, 5.7.5, 5.11.7),
// checked from the specification:
//
// - MSILCILRecords: one record CIL_<Enum> per CILOpcodes.def entry, with the
//   entry's value in TSFlags bits 18-3 and its text as the mnemonic, one
//   operand that follows the entry's OperandKind, and the control-flow flags
//   of its FlowControl. Every MI-level record that carries a CIL value names
//   the opcode of its CIL text (recon mc-streamer E1: 175 records), and the
//   records that MSILMCInstLower expands into several CIL_ instructions
//   carry none. The shortest forms that MSIL::buildLdcI4 and
//   MSIL::buildVarInst choose are cli::ILBuilder's (Q26, 5.7.5).
// - MSILBCLMembers: the BCLMembers.def X-macro, the cli:: table built from it,
//   and printBCLMemberRef, which prints Phase 1's [mscorlib] text and the
//   object form's [msil.bcl] text (5.11.7, 5.8.4).
// - MSILBranchForms: MSIL::relaxBranches, which starts every branch short and
//   widens each branch whose displacement from the next instruction leaves
//   [-128, 127] until nothing changes (5.7.5): the boundaries forward and
//   backward, widening chains, switch entries that stay 32-bit, and a fixed
//   point that depends neither on the forms it is given nor on anything but
//   cli::ILBuilder's rule, which serves as the oracle.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSILMCInstLower.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/ILBuilder.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"

#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using cli::FlowControl;
using cli::Opcode;
using cli::OperandKind;

namespace {

//===----------------------------------------------------------------------===//
// The opcode table, through the X-macro
//===----------------------------------------------------------------------===//

// One CILOpcodes.def entry and the record M18 defines for it.
struct DefEntry {
  unsigned MCOpcode;
  Opcode Op;
  const char *Text;
  uint16_t Value;
  OperandKind Kind;
  FlowControl Flow;
  const char *RecordName;
};

const DefEntry DefEntries[] = {
#define CIL_OPCODE(Enum, Text, Value, Operand, Pop, Push, Flow)                \
  {MSIL::CIL_##Enum,     Opcode::Enum,      Text,        Value,                \
   OperandKind::Operand, FlowControl::Flow, "CIL_" #Enum},
#include "llvm/BinaryFormat/CILOpcodes.def"
};

bool isBranchKind(OperandKind K) {
  return K == OperandKind::ShortInlineBrTarget ||
         K == OperandKind::InlineBrTarget;
}

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

// The MC environment of the MSIL target, created through the target registry
// as llc creates it.
class MSILMCTestBase : public testing::Test {
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
    IP.reset(TheTarget->createMCInstPrinter(TT, MAI->getAssemblerDialect(),
                                            *MAI, *MII, *MRI));
    ASSERT_TRUE(IP);
  }

  // The CIL opcode a record carries in TSFlags, if it is one.
  std::optional<Opcode> cilOpcodeOf(unsigned MCOpcode) const {
    return cli::getOpcodeByValue(MSIL::getCILOpcodeValue(MII->get(MCOpcode)));
  }

  // The text the generated AsmWriter prints before the first operand of a
  // record, without surrounding white space ("" when the asm string starts
  // with an operand or is empty).
  std::string mnemonicOf(unsigned MCOpcode) const {
    MCInst I;
    I.setOpcode(MCOpcode);
    std::pair<const char *, uint64_t> M = IP->getMnemonic(I);
    if (!M.first)
      return "";
    return StringRef(M.first).trim().str();
  }

  std::string nameOf(unsigned MCOpcode) const {
    return MII->getName(MCOpcode).str();
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
  std::unique_ptr<MCInstPrinter> IP;
};

//===----------------------------------------------------------------------===//
// MSILCILRecords: the records
//===----------------------------------------------------------------------===//

class MSILCILRecords : public MSILMCTestBase {
protected:
  std::string describe(const MCInst &I) const {
    std::string S = nameOf(I.getOpcode());
    for (const MCOperand &Op : I)
      S += Op.isImm() ? " " + std::to_string(Op.getImm()) : " <non-imm>";
    return S;
  }

  // \p I is the instruction \p Op with the inline operand \p Imm, in the
  // operand shape of the CIL_ records: no operand for InlineNone, else one
  // immediate.
  void expectInstr(const MCInst &I, Opcode Op, int64_t Imm) const {
    SCOPED_TRACE(describe(I));
    EXPECT_EQ(nameOf(MSIL::getMCOpcodeForCIL(Op)), nameOf(I.getOpcode()));
    if (cli::getOpcodeInfo(Op).Operand == OperandKind::InlineNone) {
      EXPECT_EQ(0u, I.getNumOperands());
      return;
    }
    ASSERT_EQ(1u, I.getNumOperands());
    ASSERT_TRUE(I.getOperand(0).isImm());
    EXPECT_EQ(Imm, I.getOperand(0).getImm());
  }
};

// Every CILOpcodes.def entry has the record CIL_<Enum>, which carries the
// entry's value in TSFlags bits 18-3 and prints the entry's text, and
// getMCOpcodeForCIL finds it. No other record is named CIL_.
TEST_F(MSILCILRecords, EveryDefEntryHasItsRecord) {
  ASSERT_EQ(219u, std::size(DefEntries));
  ASSERT_EQ(cli::getAllOpcodes().size(), std::size(DefEntries));
  std::set<unsigned> Seen;
  for (const DefEntry &E : DefEntries) {
    SCOPED_TRACE(E.RecordName);
    ASSERT_LT(E.MCOpcode, MII->getNumOpcodes());
    EXPECT_TRUE(isTargetSpecificOpcode(E.MCOpcode));
    EXPECT_EQ(E.RecordName, nameOf(E.MCOpcode));
    const MCInstrDesc &Desc = MII->get(E.MCOpcode);
    EXPECT_EQ(E.Value, MSIL::getCILOpcodeValue(Desc));
    EXPECT_EQ(uint64_t(E.Value) << MSILII::CILOpcodeShift,
              Desc.TSFlags & MSILII::CILOpcodeMask);
    EXPECT_EQ(E.Text, mnemonicOf(E.MCOpcode));
    EXPECT_EQ(E.MCOpcode, MSIL::getMCOpcodeForCIL(E.Op));
    // One CIL instruction holds no transient extra stack slot.
    EXPECT_EQ(0u, Desc.TSFlags & MSILII::StackPeakExtraMask);
    EXPECT_TRUE(Seen.insert(E.MCOpcode).second) << "record used twice";
  }

  unsigned CILRecords = 0;
  for (unsigned Opc = 0, N = MII->getNumOpcodes(); Opc != N; ++Opc)
    if (StringRef(MII->getName(Opc)).starts_with("CIL_")) {
      ++CILRecords;
      EXPECT_TRUE(Seen.count(Opc)) << nameOf(Opc) << " is not a def entry";
    }
  EXPECT_EQ(219u, CILRecords);
}

// getMCOpcodeForCIL agrees with cli::getAllOpcodes, the table every other
// ECMA-335 client (msil-ld, the dumpers, ILBuilder) reads.
TEST_F(MSILCILRecords, GetMCOpcodeForCILCoversTheOpcodeTable) {
  for (const cli::OpcodeInfo &Info : cli::getAllOpcodes()) {
    SCOPED_TRACE(Info.Name.str());
    unsigned MCOpcode = MSIL::getMCOpcodeForCIL(Info.Op);
    ASSERT_LT(MCOpcode, MII->getNumOpcodes());
    EXPECT_EQ(uint16_t(Info.Op), MSIL::getCILOpcodeValue(MII->get(MCOpcode)));
    EXPECT_EQ(Info.Name.str(), mnemonicOf(MCOpcode));
  }
}

// The operand list follows the OperandKind: none; one immediate of the
// matching operand class (arguments and locals keep the classes the printer
// and later an ILAsm parser tell apart); one label; one token operand; or the
// switch's variable list of labels. No record defines a register.
TEST_F(MSILCILRecords, OperandsFollowTheOperandKind) {
  for (const DefEntry &E : DefEntries) {
    SCOPED_TRACE(E.RecordName);
    const MCInstrDesc &Desc = MII->get(E.MCOpcode);
    EXPECT_EQ(0u, Desc.getNumDefs());
    if (E.Kind == OperandKind::InlineNone) {
      EXPECT_EQ(0u, Desc.getNumOperands());
      EXPECT_FALSE(Desc.isVariadic());
      continue;
    }
    if (E.Kind == OperandKind::InlineSwitch) {
      EXPECT_EQ(0u, Desc.getNumOperands());
      EXPECT_TRUE(Desc.isVariadic());
      continue;
    }
    ASSERT_EQ(1u, Desc.getNumOperands());
    EXPECT_FALSE(Desc.isVariadic());
    const MCOperandInfo &Info = Desc.operands()[0];
    EXPECT_NE(MCOI::OPERAND_REGISTER, Info.OperandType);
    EXPECT_EQ(-1, Info.RegClass);
    StringRef Text(E.Text);
    switch (E.Kind) {
    case OperandKind::ShortInlineVar:
    case OperandKind::InlineVar:
      EXPECT_EQ(unsigned(Text.contains("arg") ? MSIL::OPERAND_ARG
                                              : MSIL::OPERAND_LOCAL),
                unsigned(Info.OperandType));
      break;
    case OperandKind::ShortInlineI:
    case OperandKind::InlineI:
      EXPECT_EQ(unsigned(MSIL::OPERAND_I32IMM), unsigned(Info.OperandType));
      break;
    case OperandKind::InlineI8:
      EXPECT_EQ(unsigned(MSIL::OPERAND_I64IMM), unsigned(Info.OperandType));
      break;
    case OperandKind::ShortInlineR:
      EXPECT_EQ(unsigned(MSIL::OPERAND_F32IMM), unsigned(Info.OperandType));
      break;
    case OperandKind::InlineR:
      EXPECT_EQ(unsigned(MSIL::OPERAND_F64IMM), unsigned(Info.OperandType));
      break;
    default:
      if (isTokenKind(E.Kind))
        EXPECT_EQ(unsigned(MSIL::OPERAND_TOKEN), unsigned(Info.OperandType));
      else
        EXPECT_TRUE(isBranchKind(E.Kind));
      break;
    }
  }
}

// The control-flow flags come from the entry's FlowControl: branches are
// branches and terminators, unconditional ones barriers too; returns are
// returns and terminators; calls are calls; nothing else claims any of it.
TEST_F(MSILCILRecords, FlagsFollowTheFlowControl) {
  for (const DefEntry &E : DefEntries) {
    SCOPED_TRACE(E.RecordName);
    const MCInstrDesc &Desc = MII->get(E.MCOpcode);
    switch (E.Flow) {
    case FlowControl::Branch:
      EXPECT_TRUE(Desc.isBranch());
      EXPECT_TRUE(Desc.isTerminator());
      EXPECT_TRUE(Desc.isBarrier());
      EXPECT_FALSE(Desc.isCall());
      EXPECT_FALSE(Desc.isReturn());
      break;
    case FlowControl::Cond_Branch:
      EXPECT_TRUE(Desc.isBranch());
      EXPECT_TRUE(Desc.isTerminator());
      EXPECT_FALSE(Desc.isBarrier());
      EXPECT_FALSE(Desc.isCall());
      EXPECT_FALSE(Desc.isReturn());
      break;
    case FlowControl::Return:
      EXPECT_TRUE(Desc.isReturn());
      EXPECT_TRUE(Desc.isTerminator());
      EXPECT_FALSE(Desc.isBranch());
      EXPECT_FALSE(Desc.isCall());
      break;
    case FlowControl::Call:
      EXPECT_TRUE(Desc.isCall());
      EXPECT_FALSE(Desc.isBranch());
      EXPECT_FALSE(Desc.isReturn());
      EXPECT_FALSE(Desc.isTerminator());
      break;
    case FlowControl::Throw:
      EXPECT_FALSE(Desc.isBranch());
      EXPECT_FALSE(Desc.isCall());
      EXPECT_FALSE(Desc.isReturn());
      break;
    case FlowControl::Next:
    case FlowControl::Meta:
    case FlowControl::Break:
      EXPECT_FALSE(Desc.isBranch());
      EXPECT_FALSE(Desc.isCall());
      EXPECT_FALSE(Desc.isReturn());
      EXPECT_FALSE(Desc.isTerminator());
      EXPECT_FALSE(Desc.isBarrier());
      break;
    }
  }
}

// The CIL text of an MI-level record as plan 5.4's catalogue gives it,
// derived from the record's name: CONST_<RC> by its class, then the register
// class suffix dropped, the catalogue's structural names mapped to their CIL
// text, and every other name lower-cased with '_' as '.' ("CONV_R_UN_I32" ->
// "conv.r.un", "BNE_UN_F64" -> "bne.un", "LDIND_I1" -> "ldind.i1").
std::string catalogueMnemonic(StringRef Name) {
  if (Name == "CONST_I32")
    return "ldc.i4";
  if (Name == "CONST_I64")
    return "ldc.i8";
  if (Name == "CONST_F32")
    return "ldc.r4";
  if (Name == "CONST_F64")
    return "ldc.r8";
  for (StringRef Suffix : {"_I32", "_I64", "_F32", "_F64"})
    if (Name.ends_with(Suffix)) {
      Name = Name.drop_back(Suffix.size());
      break;
    }
  static const std::pair<StringRef, StringRef> Structural[] = {
      {"LOCAL_GET", "ldloc"},
      {"LOCAL_SET", "stloc"},
      {"ARG_GET", "ldarg"},
      {"ARG_SET", "starg"},
      {"DROP", "pop"},
      {"RETURN", "ret"},
      {"BR_IF", "brtrue"},
      {"BR_UNLESS", "brfalse"},
      {"BR_TABLE", "switch"},
      {"CALL_INDIRECT", "calli"},
      {"MEMORY_BARRIER", "call"},
      {"FUNC_ADDR", "ldsfld"},
      {"SHR_S", "shr"},
      {"SHR_U", "shr.un"},
      {"DIV_S", "div"},
      {"DIV_U", "div.un"},
      {"REM_S", "rem"},
      {"REM_U", "rem.un"},
  };
  for (const auto &[From, To] : Structural)
    if (Name == From)
      return To.str();
  std::string Text = Name.lower();
  for (char &C : Text)
    if (C == '_')
      C = '.';
  return Text;
}

// Recon mc-streamer E1, after M18: every MI-level record that still carries a
// CIL value in TSFlags names the opcode of its CIL text, and that opcode's
// CIL_ record carries the same value. Where the record's asm string starts
// with its mnemonic, the mnemonic agrees too.
TEST_F(MSILCILRecords, MILevelRecordsNameTheirOpcode) {
  unsigned WithValue = 0;
  for (unsigned Opc = 0, N = MII->getNumOpcodes(); Opc != N; ++Opc) {
    if (!isTargetSpecificOpcode(Opc))
      continue;
    std::string Name = nameOf(Opc);
    if (StringRef(Name).starts_with("CIL_"))
      continue;
    uint16_t Value = MSIL::getCILOpcodeValue(MII->get(Opc));
    if (Value == MSIL::NoCILOpcode)
      continue;
    SCOPED_TRACE(Name);
    ++WithValue;
    std::optional<Opcode> Op = cli::getOpcodeByValue(Value);
    ASSERT_TRUE(Op.has_value()) << "no CIL opcode has the value " << Value;
    StringRef Text = cli::getOpcodeInfo(*Op).Name;
    EXPECT_EQ(catalogueMnemonic(Name), Text.str());
    std::string Mnemonic = mnemonicOf(Opc);
    if (!Mnemonic.empty())
      EXPECT_EQ(Text.str(), Mnemonic);
    EXPECT_EQ(Value,
              MSIL::getCILOpcodeValue(MII->get(MSIL::getMCOpcodeForCIL(*Op))));
  }
  EXPECT_EQ(175u, WithValue);
}

// The records that MSILMCInstLower expands into several CIL_ instructions
// (plan 5.4's StackPeakExtra records and CONST_I64) and the pseudos that never
// reach MC carry no CIL value, and the encoder rejects them.
TEST_F(MSILCILRecords, MultiOpcodeRecordsCarryNoValue) {
  EXPECT_EQ(0xFFFF, MSIL::NoCILOpcode);
  const unsigned Records[] = {
      MSIL::ARG_TEE_I32,    MSIL::ARG_TEE_I64,   MSIL::ARG_TEE_F32,
      MSIL::ARG_TEE_F64,    MSIL::LOCAL_TEE_I32, MSIL::LOCAL_TEE_I64,
      MSIL::LOCAL_TEE_F32,  MSIL::LOCAL_TEE_F64, MSIL::CONST_I64,
      MSIL::GLOBAL_ADDR,    MSIL::TRAP,          MSIL::MEMMOVE,
      MSIL::ARGUMENT_I32,   MSIL::ARGUMENT_F64,  MSIL::COPY_I32,
      MSIL::COPY_F64,       MSIL::TEE_I32,       MSIL::TEE_F64,
      MSIL::CALL_PARAMS,    MSIL::CALL_RESULTS,  MSIL::ADJCALLSTACKDOWN,
      MSIL::ADJCALLSTACKUP, MSIL::SELECT_I32,    MSIL::SELECT_F64,
  };
  for (unsigned Opc : Records) {
    SCOPED_TRACE(nameOf(Opc));
    EXPECT_EQ(MSIL::NoCILOpcode, MSIL::getCILOpcodeValue(MII->get(Opc)));
  }
}

//===----------------------------------------------------------------------===//
// MSILCILRecords: shortest forms (Q26), against cli::ILBuilder
//===----------------------------------------------------------------------===//

// What cli::ILBuilder emits for one instruction: the opcode and the inline
// operand of the last instruction of a laid-out body.
struct BuiltInstr {
  Opcode Op = Opcode::Nop;
  int64_t Imm = 0;
};

BuiltInstr lastInstr(cli::ILBuilder &B) {
  if (Error E = B.layout()) {
    ADD_FAILURE() << toString(std::move(E));
    return {};
  }
  if (B.instructions().empty()) {
    ADD_FAILURE() << "ILBuilder emitted nothing";
    return {};
  }
  const cli::ILInstr &I = B.instructions().back();
  return {I.Op, I.Imm};
}

BuiltInstr ilBuilderLdcI4(int32_t V) {
  cli::ILBuilder B(/*ReturnsValue=*/false);
  B.emitLdcI4(V);
  return lastInstr(B);
}

BuiltInstr ilBuilderVarInst(Opcode LongForm, unsigned N) {
  cli::ILBuilder B(/*ReturnsValue=*/false);
  // A store needs a value to pop.
  if (LongForm == Opcode::Starg || LongForm == Opcode::Stloc)
    B.emitLdcI4(0);
  switch (LongForm) {
  case Opcode::Ldarg:
    B.emitLdarg(N);
    break;
  case Opcode::Starg:
    B.emitStarg(N);
    break;
  case Opcode::Ldarga:
    B.emitLdarga(N);
    break;
  case Opcode::Ldloc:
    B.emitLdloc(N);
    break;
  case Opcode::Stloc:
    B.emitStloc(N);
    break;
  case Opcode::Ldloca:
    B.emitLdloca(N);
    break;
  default:
    ADD_FAILURE() << "not a variable instruction";
    break;
  }
  return lastInstr(B);
}

TEST_F(MSILCILRecords, MSILBuildLdcI4) {
  const int32_t Min = std::numeric_limits<int32_t>::min();
  const int32_t Max = std::numeric_limits<int32_t>::max();
  const struct {
    int32_t V;
    Opcode Op;
  } Cases[] = {
      {-129, Opcode::Ldc_i4},  {-128, Opcode::Ldc_i4_s}, {-2, Opcode::Ldc_i4_s},
      {-1, Opcode::Ldc_i4_m1}, {0, Opcode::Ldc_i4_0},    {1, Opcode::Ldc_i4_1},
      {7, Opcode::Ldc_i4_7},   {8, Opcode::Ldc_i4_8},    {9, Opcode::Ldc_i4_s},
      {127, Opcode::Ldc_i4_s}, {128, Opcode::Ldc_i4},    {Min, Opcode::Ldc_i4},
      {Max, Opcode::Ldc_i4},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.V);
    MCInst I = MSIL::buildLdcI4(C.V);
    expectInstr(I, C.Op, C.V);
    BuiltInstr B = ilBuilderLdcI4(C.V);
    EXPECT_EQ(cli::getOpcodeInfo(C.Op).Name, cli::getOpcodeInfo(B.Op).Name);
  }
}

// Every value from -300 to 300 and around the int8 and int32 limits gets
// exactly the instruction cli::ILBuilder::emitLdcI4 emits.
TEST_F(MSILCILRecords, MSILBuildLdcI4EqualsILBuilder) {
  std::vector<int32_t> Values;
  for (int32_t V = -300; V <= 300; ++V)
    Values.push_back(V);
  for (int64_t V : {int64_t(std::numeric_limits<int32_t>::min()),
                    int64_t(std::numeric_limits<int32_t>::min()) + 1,
                    int64_t(std::numeric_limits<int32_t>::max()) - 1,
                    int64_t(std::numeric_limits<int32_t>::max()),
                    int64_t(65535), int64_t(65536), int64_t(-65536)})
    Values.push_back(int32_t(V));
  for (int32_t V : Values) {
    SCOPED_TRACE(V);
    BuiltInstr B = ilBuilderLdcI4(V);
    expectInstr(MSIL::buildLdcI4(V), B.Op, B.Imm);
    // ILBuilder keeps the value as the operand of ldc.i4.s and ldc.i4.
    if (cli::getOpcodeInfo(B.Op).Operand != OperandKind::InlineNone)
      EXPECT_EQ(V, B.Imm);
  }
}

TEST_F(MSILCILRecords, MSILBuildVarInst) {
  const unsigned Indexes[] = {0, 3, 4, 255, 256, 65534};
  // Per long form: the .0-.3 forms (none for starg, ldarga and ldloca), the
  // .s form below 256, the long form from 256 on.
  const struct {
    Opcode Long, Short;
    std::optional<Opcode> Zero;
  } Families[] = {
      {Opcode::Ldarg, Opcode::Ldarg_s, Opcode::Ldarg_0},
      {Opcode::Starg, Opcode::Starg_s, std::nullopt},
      {Opcode::Ldloc, Opcode::Ldloc_s, Opcode::Ldloc_0},
      {Opcode::Stloc, Opcode::Stloc_s, Opcode::Stloc_0},
      {Opcode::Ldarga, Opcode::Ldarga_s, std::nullopt},
      {Opcode::Ldloca, Opcode::Ldloca_s, std::nullopt},
  };
  for (const auto &F : Families) {
    for (unsigned N : Indexes) {
      SCOPED_TRACE(cli::getOpcodeInfo(F.Long).Name.str() + " " +
                   std::to_string(N));
      Opcode Expected = N < 256 ? F.Short : F.Long;
      if (F.Zero && N <= 3)
        Expected = Opcode(uint16_t(*F.Zero) + N);
      MCInst I = MSIL::buildVarInst(F.Long, N);
      expectInstr(I, Expected, N);
      BuiltInstr B = ilBuilderVarInst(F.Long, N);
      EXPECT_EQ(cli::getOpcodeInfo(Expected).Name,
                cli::getOpcodeInfo(B.Op).Name);
    }
  }
}

// Every index from 0 to 300 and the largest one, 65534, for each of the six
// variable instructions: exactly what cli::ILBuilder emits.
TEST_F(MSILCILRecords, MSILBuildVarInstEqualsILBuilder) {
  std::vector<unsigned> Indexes;
  for (unsigned N = 0; N <= 300; ++N)
    Indexes.push_back(N);
  Indexes.push_back(65533);
  Indexes.push_back(65534);
  for (Opcode Long : {Opcode::Ldarg, Opcode::Starg, Opcode::Ldarga,
                      Opcode::Ldloc, Opcode::Stloc, Opcode::Ldloca}) {
    for (unsigned N : Indexes) {
      SCOPED_TRACE(cli::getOpcodeInfo(Long).Name.str() + " " +
                   std::to_string(N));
      BuiltInstr B = ilBuilderVarInst(Long, N);
      expectInstr(MSIL::buildVarInst(Long, N), B.Op, B.Imm);
    }
  }
}

//===----------------------------------------------------------------------===//
// MSILBCLMembers
//===----------------------------------------------------------------------===//

// One BCLMembers.def entry, through the X-macro.
struct BCLDefEntry {
  bool IsType;
  const char *IdName;
  const char *Namespace; // types only
  const char *Name;
  const char *OwnerIdName; // methods only
  bool HasThis;
  bool MscorlibSafe;
};

const BCLDefEntry BCLDefEntries[] = {
#define CLI_BCL_TYPE(Id, Namespace, Name, MscorlibSafe)                        \
  {true, #Id, Namespace, Name, "", false, bool(MscorlibSafe)},
#define CLI_BCL_METHOD(Id, OwnerType, Name, HasThis, Return, Params, Strength, \
                       Fallback, MscorlibSafe)                                 \
  {false, #Id, "", Name, #OwnerType, bool(HasThis), bool(MscorlibSafe)},
#include "llvm/CLI/BCLMembers.def"
};

std::string printRef(cli::BCLMemberId Id, StringRef Scope) {
  std::string S;
  raw_string_ostream OS(S);
  cli::printBCLMemberRef(OS, Id, Scope);
  return S;
}

class MSILBCLMembers : public testing::Test {};

// The table holds exactly the def entries, in their order, with their names,
// kinds, owners and flags; getBCLMemberInfo finds each by its id.
TEST_F(MSILBCLMembers, TableMatchesTheDefFile) {
  ArrayRef<cli::BCLMemberInfo> All = cli::getAllBCLMembers();
  ASSERT_EQ(std::size(BCLDefEntries), All.size());
  for (size_t I = 0; I != All.size(); ++I) {
    const BCLDefEntry &D = BCLDefEntries[I];
    const cli::BCLMemberInfo &Info = All[I];
    SCOPED_TRACE(D.IdName);
    EXPECT_EQ(StringRef(D.IdName), StringRef(Info.IdName));
    EXPECT_EQ(D.IsType, Info.isType());
    EXPECT_EQ(!D.IsType, Info.isMethod());
    EXPECT_EQ(StringRef(D.Name), StringRef(Info.Name));
    EXPECT_EQ(D.MscorlibSafe, bool(Info.MscorlibSafe));
    EXPECT_EQ(StringRef(Info.IdName),
              StringRef(cli::getBCLMemberInfo(Info.Id).IdName));
    if (D.IsType) {
      EXPECT_EQ(StringRef(D.Namespace), StringRef(Info.Namespace));
      continue;
    }
    EXPECT_EQ(D.HasThis, bool(Info.HasThis));
    const cli::BCLMemberInfo &Owner = cli::getBCLMemberInfo(Info.Owner);
    EXPECT_TRUE(Owner.isType());
    EXPECT_EQ(StringRef(D.OwnerIdName), StringRef(Owner.IdName));
  }
}

// M18's list (plan M18.a (6), 5.11.7): System.ValueType, the owner types of
// Phase 1's five members, and the five members; all Required, static and
// mscorlib-safe. M18's entries come first, in this order (the order fixes the
// BCLMemberId values); M20, M21 and M22 append theirs after them.
TEST_F(MSILBCLMembers, HoldsPhaseOneEntries) {
  using cli::BCLMemberId;
  const struct {
    BCLMemberId Id;
    const char *Namespace;
    const char *Name;
  } Types[] = {
      {BCLMemberId::ValueType, "System", "ValueType"},
      {BCLMemberId::Marshal, "System.Runtime.InteropServices", "Marshal"},
      {BCLMemberId::Environment, "System", "Environment"},
      {BCLMemberId::Buffer, "System", "Buffer"},
      {BCLMemberId::Interlocked, "System.Threading", "Interlocked"},
  };
  for (const auto &T : Types) {
    const cli::BCLMemberInfo &Info = cli::getBCLMemberInfo(T.Id);
    SCOPED_TRACE(T.Name);
    EXPECT_TRUE(Info.isType());
    EXPECT_EQ(StringRef(T.Namespace), StringRef(Info.Namespace));
    EXPECT_EQ(StringRef(T.Name), StringRef(Info.Name));
    EXPECT_TRUE(Info.MscorlibSafe);
  }

  const struct {
    BCLMemberId Id;
    BCLMemberId Owner;
    const char *Name;
  } Methods[] = {
      {BCLMemberId::Marshal_AllocHGlobal, BCLMemberId::Marshal, "AllocHGlobal"},
      {BCLMemberId::Marshal_StringToHGlobalAnsi, BCLMemberId::Marshal,
       "StringToHGlobalAnsi"},
      {BCLMemberId::Environment_GetCommandLineArgs, BCLMemberId::Environment,
       "GetCommandLineArgs"},
      {BCLMemberId::Buffer_MemoryCopy, BCLMemberId::Buffer, "MemoryCopy"},
      {BCLMemberId::Interlocked_MemoryBarrier, BCLMemberId::Interlocked,
       "MemoryBarrier"},
  };
  for (const auto &M : Methods) {
    const cli::BCLMemberInfo &Info = cli::getBCLMemberInfo(M.Id);
    SCOPED_TRACE(M.Name);
    EXPECT_TRUE(Info.isMethod());
    EXPECT_EQ(StringRef(M.Name), StringRef(Info.Name));
    EXPECT_TRUE(M.Owner == Info.Owner);
    EXPECT_FALSE(Info.HasThis);
    EXPECT_TRUE(Info.Strength == cli::BCLStrength::Required);
    EXPECT_TRUE(Info.MscorlibSafe);
  }
  // The table begins with M18's ten entries: the types, then the methods.
  ArrayRef<cli::BCLMemberInfo> All = cli::getAllBCLMembers();
  ASSERT_LE(std::size(Types) + std::size(Methods), All.size());
  size_t Index = 0;
  for (const auto &T : Types) {
    SCOPED_TRACE(T.Name);
    EXPECT_EQ(Index, static_cast<size_t>(T.Id));
    EXPECT_TRUE(All[Index].Id == T.Id);
    ++Index;
  }
  for (const auto &M : Methods) {
    SCOPED_TRACE(M.Name);
    EXPECT_EQ(Index, static_cast<size_t>(M.Id));
    EXPECT_TRUE(All[Index].Id == M.Id);
    ++Index;
  }
  EXPECT_EQ("mscorlib", StringRef(cli::MscorlibAssembly));
}

// M20's entries (plan 5.11.7), appended after M18's: the data-import helper's
// Marshal::StringToHGlobalUni(string) and EntryPointNotFoundException::.ctor
// (string) with its type (5.10.12), and System.Object, the base type of the
// spill classes (5.9.12); the methods Required, all mscorlib-safe.
TEST_F(MSILBCLMembers, HoldsM20Entries) {
  using cli::BCLMemberId;
  const size_t NumM18Entries = 10;
  const struct {
    BCLMemberId Id;
    const char *Namespace;
    const char *Name;
  } Types[] = {
      {BCLMemberId::EntryPointNotFoundException, "System",
       "EntryPointNotFoundException"},
      {BCLMemberId::Object, "System", "Object"},
  };
  for (const auto &T : Types) {
    const cli::BCLMemberInfo &Info = cli::getBCLMemberInfo(T.Id);
    SCOPED_TRACE(T.Name);
    EXPECT_LE(NumM18Entries, static_cast<size_t>(T.Id));
    EXPECT_TRUE(Info.isType());
    EXPECT_EQ(StringRef(T.Namespace), StringRef(Info.Namespace));
    EXPECT_EQ(StringRef(T.Name), StringRef(Info.Name));
    EXPECT_TRUE(Info.MscorlibSafe);
  }

  const struct {
    BCLMemberId Id;
    BCLMemberId Owner;
    const char *Name;
    bool HasThis;
    const char *Return;
    const char *Params;
  } Methods[] = {
      {BCLMemberId::Marshal_StringToHGlobalUni, BCLMemberId::Marshal,
       "StringToHGlobalUni", false, "native int", "string"},
      {BCLMemberId::EntryPointNotFoundException_ctor,
       BCLMemberId::EntryPointNotFoundException, ".ctor", true, "void",
       "string"},
  };
  for (const auto &M : Methods) {
    const cli::BCLMemberInfo &Info = cli::getBCLMemberInfo(M.Id);
    SCOPED_TRACE(M.Name);
    EXPECT_LE(NumM18Entries, static_cast<size_t>(M.Id));
    EXPECT_TRUE(Info.isMethod());
    EXPECT_EQ(StringRef(M.Name), StringRef(Info.Name));
    EXPECT_TRUE(M.Owner == Info.Owner);
    EXPECT_EQ(M.HasThis, bool(Info.HasThis));
    EXPECT_EQ(StringRef(M.Return), StringRef(Info.Return));
    EXPECT_EQ(StringRef(M.Params), StringRef(Info.Params));
    EXPECT_TRUE(Info.Strength == cli::BCLStrength::Required);
    EXPECT_TRUE(Info.MscorlibSafe);
  }
}

// With the scope mscorlib, the exact text Phase 1 printed (decision D10,
// D15, D27, D36; the FileCheck lines of llvm/test/CodeGen/MSIL).
TEST_F(MSILBCLMembers, PrintsPhaseOneText) {
  using cli::BCLMemberId;
  EXPECT_EQ("native int [mscorlib]System.Runtime.InteropServices.Marshal::"
            "AllocHGlobal(native int)",
            printRef(BCLMemberId::Marshal_AllocHGlobal, "mscorlib"));
  EXPECT_EQ("native int [mscorlib]System.Runtime.InteropServices.Marshal::"
            "StringToHGlobalAnsi(string)",
            printRef(BCLMemberId::Marshal_StringToHGlobalAnsi, "mscorlib"));
  EXPECT_EQ("string[] [mscorlib]System.Environment::GetCommandLineArgs()",
            printRef(BCLMemberId::Environment_GetCommandLineArgs, "mscorlib"));
  EXPECT_EQ("void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, "
            "int64)",
            printRef(BCLMemberId::Buffer_MemoryCopy, "mscorlib"));
  EXPECT_EQ("void [mscorlib]System.Threading.Interlocked::MemoryBarrier()",
            printRef(BCLMemberId::Interlocked_MemoryBarrier, "mscorlib"));
  // The base type of the data image's value type.
  EXPECT_EQ("[mscorlib]System.ValueType",
            printRef(BCLMemberId::ValueType, "mscorlib"));
}

// The object form's unbound scope (5.8.3, 5.8.4) gives the same shape.
TEST_F(MSILBCLMembers, PrintsObjectFormScope) {
  using cli::BCLMemberId;
  EXPECT_EQ("native int [msil.bcl]System.Runtime.InteropServices.Marshal::"
            "AllocHGlobal(native int)",
            printRef(BCLMemberId::Marshal_AllocHGlobal, "msil.bcl"));
  EXPECT_EQ("string[] [msil.bcl]System.Environment::GetCommandLineArgs()",
            printRef(BCLMemberId::Environment_GetCommandLineArgs, "msil.bcl"));
  EXPECT_EQ("void [msil.bcl]System.Buffer::MemoryCopy(void*, void*, int64, "
            "int64)",
            printRef(BCLMemberId::Buffer_MemoryCopy, "msil.bcl"));
  EXPECT_EQ("void [msil.bcl]System.Threading.Interlocked::MemoryBarrier()",
            printRef(BCLMemberId::Interlocked_MemoryBarrier, "msil.bcl"));
  EXPECT_EQ("[msil.bcl]System.ValueType",
            printRef(BCLMemberId::ValueType, "msil.bcl"));
  // An empty scope prints no brackets.
  EXPECT_EQ("void System.Threading.Interlocked::MemoryBarrier()",
            printRef(BCLMemberId::Interlocked_MemoryBarrier, ""));
  EXPECT_EQ("System.ValueType", printRef(BCLMemberId::ValueType, ""));
}

// Every method prints its owner type's reference followed by '::', its name
// and its parameter list, in any scope, and every entry under every scope
// names the scope once.
TEST_F(MSILBCLMembers, MethodsPrintThroughTheirOwner) {
  for (const cli::BCLMemberInfo &Info : cli::getAllBCLMembers()) {
    SCOPED_TRACE(StringRef(Info.IdName).str());
    for (StringRef Scope : {"mscorlib", "msil.bcl", "System.Runtime"}) {
      std::string Text = printRef(Info.Id, Scope);
      std::string Bracketed = ("[" + Scope + "]").str();
      size_t At = Text.find(Bracketed);
      ASSERT_NE(std::string::npos, At) << Text;
      EXPECT_EQ(std::string::npos, Text.find(Bracketed, At + 1)) << Text;
      if (Info.isType()) {
        EXPECT_EQ(0u, At) << Text;
        EXPECT_EQ(Bracketed + StringRef(Info.Namespace).str() + "." +
                      StringRef(Info.Name).str(),
                  Text);
        continue;
      }
      std::string Owner = printRef(Info.Owner, Scope);
      std::string Member = Owner + "::" + StringRef(Info.Name).str() + "(";
      EXPECT_NE(std::string::npos, Text.find(Member)) << Text;
      EXPECT_EQ(')', Text.back()) << Text;
      EXPECT_EQ(Info.HasThis, StringRef(Text).starts_with("instance ")) << Text;
    }
  }
}

//===----------------------------------------------------------------------===//
// MSILBranchForms
//===----------------------------------------------------------------------===//

class MSILBranchForms : public MSILMCTestBase {
protected:
  // A method body as computeBranchForms sees it: the MCInsts and, for each
  // label, the index of the instruction it precedes (the number of
  // instructions for a label at the end).
  struct Code {
    SmallVector<MCInst, 0> Insts;
    SmallVector<std::pair<const MCSymbol *, unsigned>, 0> Labels;
  };

  const MCSymbol *newLabel() { return Ctx->createTempSymbol(); }

  void place(Code &C, const MCSymbol *L) {
    C.Labels.push_back({L, unsigned(C.Insts.size())});
  }

  void add(Code &C, Opcode Op) {
    MCInst I;
    I.setOpcode(MSIL::getMCOpcodeForCIL(Op));
    C.Insts.push_back(I);
  }

  void nops(Code &C, unsigned N) {
    for (unsigned I = 0; I != N; ++I)
      add(C, Opcode::Nop);
  }

  // Appends the branch \p Op to \p L and returns its index.
  unsigned branch(Code &C, Opcode Op, const MCSymbol *L) {
    MCInst I;
    I.setOpcode(MSIL::getMCOpcodeForCIL(Op));
    I.addOperand(MCOperand::createExpr(MCSymbolRefExpr::create(L, *Ctx)));
    C.Insts.push_back(I);
    return C.Insts.size() - 1;
  }

  unsigned switchTo(Code &C, ArrayRef<const MCSymbol *> Targets) {
    MCInst I;
    I.setOpcode(MSIL::getMCOpcodeForCIL(Opcode::Switch));
    for (const MCSymbol *L : Targets)
      I.addOperand(MCOperand::createExpr(MCSymbolRefExpr::create(L, *Ctx)));
    C.Insts.push_back(I);
    return C.Insts.size() - 1;
  }

  void relax(Code &C) { MSIL::relaxBranches(C.Insts, C.Labels, *MII); }

  Opcode opcodeAt(const Code &C, unsigned Index) {
    std::optional<Opcode> Op = cilOpcodeOf(C.Insts[Index].getOpcode());
    EXPECT_TRUE(Op.has_value()) << nameOf(C.Insts[Index].getOpcode());
    return Op.value_or(Opcode::Nop);
  }

  bool isShortAt(const Code &C, unsigned Index) {
    return cli::getOpcodeInfo(opcodeAt(C, Index)).Operand ==
           OperandKind::ShortInlineBrTarget;
  }

  std::string textAt(const Code &C, unsigned Index) {
    return cli::getOpcodeInfo(opcodeAt(C, Index)).Name.str();
  }

  // The offset of every instruction, and the code size last, from
  // MSIL::getCILInstSize.
  std::vector<uint32_t> offsets(const Code &C) {
    std::vector<uint32_t> Offsets{0};
    for (const MCInst &I : C.Insts)
      Offsets.push_back(Offsets.back() + MSIL::getCILInstSize(I, *MII));
    return Offsets;
  }
};

// Every short branch opcode with its long form: br, brfalse, brtrue, the ten
// b<cc>, and leave.
std::vector<std::pair<Opcode, Opcode>> branchFamilies() {
  std::vector<std::pair<Opcode, Opcode>> Families;
  for (const cli::OpcodeInfo &Info : cli::getAllOpcodes())
    if (Info.Operand == OperandKind::ShortInlineBrTarget) {
      std::optional<Opcode> Long = cli::getLongBranchForm(Info.Op);
      EXPECT_TRUE(Long.has_value()) << Info.Name.str();
      if (Long)
        Families.push_back({Info.Op, *Long});
    }
  EXPECT_EQ(14u, Families.size());
  return Families;
}

// A forward branch over 127 bytes stays short and one over 128 is long,
// whichever form it is given; relaxation changes only the opcode.
TEST_F(MSILBranchForms, ForwardBoundary) {
  for (auto [Short, Long] : branchFamilies()) {
    for (Opcode Given : {Short, Long}) {
      for (unsigned Fill : {0u, 1u, 127u, 128u, 300u}) {
        SCOPED_TRACE(cli::getOpcodeInfo(Given).Name.str() + " over " +
                     std::to_string(Fill));
        Code C;
        const MCSymbol *L = newLabel();
        unsigned B = branch(C, Given, L);
        const MCExpr *Target = C.Insts[B].getOperand(0).getExpr();
        nops(C, Fill);
        place(C, L);
        add(C, Opcode::Ret);
        relax(C);
        EXPECT_EQ(cli::getOpcodeInfo(Fill <= 127 ? Short : Long).Name.str(),
                  textAt(C, B));
        ASSERT_EQ(1u, C.Insts[B].getNumOperands());
        EXPECT_EQ(Target, C.Insts[B].getOperand(0).getExpr());
        EXPECT_EQ(Fill + 2, C.Insts.size());
      }
    }
  }
}

// A backward branch whose short form would land 128 bytes back stays short;
// 129 bytes back it is long. A branch to itself is short.
TEST_F(MSILBranchForms, BackwardBoundary) {
  for (auto [Short, Long] : branchFamilies()) {
    for (Opcode Given : {Short, Long}) {
      // Displacement of the short form: -(Fill + 2).
      for (unsigned Fill : {0u, 125u, 126u, 127u, 128u}) {
        SCOPED_TRACE(cli::getOpcodeInfo(Given).Name.str() + " back over " +
                     std::to_string(Fill));
        Code C;
        const MCSymbol *L = newLabel();
        place(C, L);
        nops(C, Fill);
        unsigned B = branch(C, Given, L);
        add(C, Opcode::Ret);
        relax(C);
        EXPECT_EQ(cli::getOpcodeInfo(Fill <= 126 ? Short : Long).Name.str(),
                  textAt(C, B));
      }
    }
  }
}

// A label after the last instruction is a valid target.
TEST_F(MSILBranchForms, LabelAtTheEnd) {
  for (unsigned Fill : {127u, 128u}) {
    SCOPED_TRACE(Fill);
    Code C;
    const MCSymbol *L = newLabel();
    unsigned B = branch(C, Opcode::Br, L);
    nops(C, Fill);
    place(C, L);
    ASSERT_EQ(C.Insts.size(), C.Labels.back().second);
    relax(C);
    EXPECT_EQ(Fill <= 127, isShortAt(C, B)) << textAt(C, B);
  }
}

// Widening one branch lengthens the span of the branches around it:
//   C: br.s Lc; 123 nops; B: brtrue.s Lb; Lc: 123 nops; A: blt.s La; Lb:
//   200 nops; La: ret
// A is long (over 200 bytes), so B spans 123 + 5 = 128 bytes and widens,
// and then C spans 123 + 5 = 128 bytes and widens too. With 122 nops
// before A, B spans 127 bytes and stays short, and C spans 125.
TEST_F(MSILBranchForms, WideningChain) {
  for (unsigned Gap : {123u, 122u}) {
    SCOPED_TRACE(Gap);
    Code Prog;
    const MCSymbol *La = newLabel(), *Lb = newLabel(), *Lc = newLabel();
    unsigned C = branch(Prog, Opcode::Br_s, Lc);
    nops(Prog, 123);
    unsigned B = branch(Prog, Opcode::Brtrue_s, Lb);
    place(Prog, Lc);
    nops(Prog, Gap);
    unsigned A = branch(Prog, Opcode::Blt_s, La);
    place(Prog, Lb);
    nops(Prog, 200);
    place(Prog, La);
    add(Prog, Opcode::Ret);
    relax(Prog);
    EXPECT_EQ("blt", textAt(Prog, A));
    bool Chain = Gap == 123;
    EXPECT_EQ(Chain ? "brtrue" : "brtrue.s", textAt(Prog, B));
    EXPECT_EQ(Chain ? "br" : "br.s", textAt(Prog, C));
  }
}

// The same chain backwards:
//   La: 200 nops; Lb: A: br.s La; Lc: Gap nops; B: br.s Lb; C: br.s Lc; ret
// A jumps back over 200 bytes and is long. B's short form lands
// size(A) + Gap + 2 bytes back: 129 with A long and 122 nops, so B widens;
// C's short form then lands Gap + size(B) + 2 = 129 bytes back and widens
// too. With 121 nops B lands exactly 128 bytes back and stays short, and C
// lands 125 bytes back.
TEST_F(MSILBranchForms, BackwardWideningChain) {
  for (unsigned Gap : {122u, 121u}) {
    SCOPED_TRACE(Gap);
    Code Prog;
    const MCSymbol *La = newLabel(), *Lb = newLabel(), *Lc = newLabel();
    place(Prog, La);
    nops(Prog, 200);
    place(Prog, Lb);
    unsigned A = branch(Prog, Opcode::Br_s, La);
    place(Prog, Lc);
    nops(Prog, Gap);
    unsigned B = branch(Prog, Opcode::Brfalse, Lb);
    unsigned C = branch(Prog, Opcode::Bge_un_s, Lc);
    add(Prog, Opcode::Ret);
    relax(Prog);
    EXPECT_EQ("br", textAt(Prog, A));
    bool Chain = Gap == 122;
    EXPECT_EQ(Chain ? "brfalse" : "brfalse.s", textAt(Prog, B));
    EXPECT_EQ(Chain ? "bge.un" : "bge.un.s", textAt(Prog, C));
    // The short form's displacement from the next instruction, recomputed
    // from the final layout, is what decided each form.
    std::vector<uint32_t> Off = offsets(Prog);
    int64_t BShort = int64_t(Off[A]) - (int64_t(Off[B]) + 2);
    int64_t CShort = int64_t(Off[A + 1]) - (int64_t(Off[C]) + 2);
    EXPECT_EQ(Chain ? -129 : -128, BShort);
    EXPECT_EQ(Chain ? -129 : -125, CShort);
  }
}

// Switch entries are always 32-bit: a switch with N targets is 5 + 4N bytes
// whatever its targets, keeps its opcode and operands, and a branch over it
// sees that size. The br that follows a switch (BR_TABLE_I32) gets its own
// shortest form.
TEST_F(MSILBranchForms, SwitchEntriesStay32Bit) {
  for (unsigned N : {30u, 31u}) {
    SCOPED_TRACE(N);
    Code C;
    const MCSymbol *L = newLabel();
    unsigned B = branch(C, Opcode::Br_s, L);
    std::vector<const MCSymbol *> Targets(N, L);
    unsigned S = switchTo(C, Targets);
    place(C, L);
    add(C, Opcode::Ret);
    relax(C);
    // br.s spans 5 + 4N bytes: 125 for 30 targets, 129 for 31.
    EXPECT_EQ(N == 30, isShortAt(C, B)) << textAt(C, B);
    EXPECT_EQ("switch", textAt(C, S));
    EXPECT_EQ(N, C.Insts[S].getNumOperands());
    EXPECT_EQ(5u + 4u * N, MSIL::getCILInstSize(C.Insts[S], *MII));
  }

  // BR_TABLE_I32's 'switch (L1, L2); br Ldefault', with the targets 300
  // bytes away: the switch keeps its 32-bit entries and the br is long.
  {
    Code C;
    const MCSymbol *L1 = newLabel(), *L2 = newLabel(), *Def = newLabel();
    unsigned S = switchTo(C, {L1, L2});
    unsigned Br = branch(C, Opcode::Br_s, Def);
    place(C, L1);
    nops(C, 300);
    place(C, L2);
    add(C, Opcode::Ret);
    place(C, Def);
    add(C, Opcode::Ret);
    relax(C);
    EXPECT_EQ("switch", textAt(C, S));
    EXPECT_EQ(13u, MSIL::getCILInstSize(C.Insts[S], *MII));
    EXPECT_EQ("br", textAt(C, Br));
  }
  // The default right after the br, the targets far away: the br is short
  // and the switch is unchanged.
  {
    Code C;
    const MCSymbol *L1 = newLabel(), *Def = newLabel();
    unsigned S = switchTo(C, {L1});
    unsigned Br = branch(C, Opcode::Br, Def);
    place(C, Def);
    add(C, Opcode::Ret);
    place(C, L1);
    nops(C, 300);
    add(C, Opcode::Ret);
    relax(C);
    EXPECT_EQ("switch", textAt(C, S));
    EXPECT_EQ(9u, MSIL::getCILInstSize(C.Insts[S], *MII));
    EXPECT_EQ("br.s", textAt(C, Br));
  }
}

// A branch whose operand is a numeric displacement (an ILAsm 'br 300', the
// inline-IL path) keeps the form it was given: only label targets relax.
TEST_F(MSILBranchForms, NumericDisplacementsKeepTheirForm) {
  Code C;
  MCInst Long;
  Long.setOpcode(MSIL::getMCOpcodeForCIL(Opcode::Br));
  Long.addOperand(MCOperand::createImm(3));
  C.Insts.push_back(Long);
  MCInst Short;
  Short.setOpcode(MSIL::getMCOpcodeForCIL(Opcode::Brtrue_s));
  Short.addOperand(MCOperand::createImm(-2));
  C.Insts.push_back(Short);
  add(C, Opcode::Ret);
  relax(C);
  EXPECT_EQ("br", textAt(C, 0));
  EXPECT_EQ("brtrue.s", textAt(C, 1));
  EXPECT_EQ(3, C.Insts[0].getOperand(0).getImm());
  EXPECT_EQ(-2, C.Insts[1].getOperand(0).getImm());
}

// A pseudo-random method body: runs of nops, branches forward and backward
// (unconditional, and conditional ones after the ldc.i4.0 they pop), switches
// and labels, ending in ret; the stack is empty at every label.
struct RandomBody {
  enum Kind { PlaceLabel, Nop, LdcI4Zero, Branch, Switch, Ret };
  struct Item {
    Kind K;
    Opcode Op;
    std::vector<unsigned> Targets; // label numbers
    Item(Kind K, Opcode Op = Opcode::Nop, std::vector<unsigned> Targets = {})
        : K(K), Op(Op), Targets(std::move(Targets)) {}
  };
  std::vector<Item> Items;
  unsigned NumLabels = 0;
};

class LCG {
  uint64_t State;

public:
  explicit LCG(uint64_t Seed) : State(Seed * 2654435761u + 1) {}
  unsigned below(unsigned N) {
    State = State * 6364136223846793005ull + 1442695040888963407ull;
    return unsigned(State >> 33) % N;
  }
};

RandomBody makeRandomBody(uint64_t Seed) {
  LCG R(Seed);
  RandomBody Body;
  Body.NumLabels = 2 + R.below(9);
  unsigned Steps = 8 + R.below(40);
  // Where each label goes: before step Pos[i] (Steps: before the final ret).
  std::vector<std::vector<unsigned>> LabelsAt(Steps + 1);
  for (unsigned L = 0; L != Body.NumLabels; ++L)
    LabelsAt[R.below(Steps + 1)].push_back(L);
  const Opcode Cond1[] = {Opcode::Brtrue, Opcode::Brfalse_s};
  const Opcode Cond2[] = {Opcode::Beq_s, Opcode::Blt_un, Opcode::Bge,
                          Opcode::Bne_un_s};
  const Opcode Uncond[] = {Opcode::Br, Opcode::Br_s};
  using Item = RandomBody::Item;
  for (unsigned S = 0; S <= Steps; ++S) {
    for (unsigned L : LabelsAt[S])
      Body.Items.push_back(Item{RandomBody::PlaceLabel, Opcode::Nop, {L}});
    if (S == Steps)
      break;
    switch (R.below(6)) {
    case 0:
    case 1: {
      // Mostly short runs, now and then one that crosses the int8 range.
      unsigned N = R.below(5) == 0 ? 90 + R.below(80) : R.below(40);
      for (unsigned I = 0; I != N; ++I)
        Body.Items.push_back(Item{RandomBody::Nop});
      break;
    }
    case 2:
      Body.Items.push_back(Item{
          RandomBody::Branch, Uncond[R.below(2)], {R.below(Body.NumLabels)}});
      break;
    case 3:
      Body.Items.push_back(Item{RandomBody::LdcI4Zero});
      Body.Items.push_back(Item{
          RandomBody::Branch, Cond1[R.below(2)], {R.below(Body.NumLabels)}});
      break;
    case 4:
      Body.Items.push_back(Item{RandomBody::LdcI4Zero});
      Body.Items.push_back(Item{RandomBody::LdcI4Zero});
      Body.Items.push_back(Item{
          RandomBody::Branch, Cond2[R.below(4)], {R.below(Body.NumLabels)}});
      break;
    case 5: {
      Item Sw{RandomBody::Switch};
      for (unsigned I = 0, N = 1 + R.below(4); I != N; ++I)
        Sw.Targets.push_back(R.below(Body.NumLabels));
      Body.Items.push_back(Item{RandomBody::LdcI4Zero});
      Body.Items.push_back(Sw);
      break;
    }
    }
  }
  Body.Items.push_back(Item{RandomBody::Ret});
  return Body;
}

// Whatever forms the branches are given (all short, all long, or a random
// mix), relaxBranches reaches one fixed point, and it is cli::ILBuilder's:
// the same opcode at every position and the same offsets, so the same code
// size. Relaxing again changes nothing.
TEST_F(MSILBranchForms, FixedPointIsILBuildersForAnyInputForms) {
  unsigned Branches = 0, LongBranches = 0;
  for (uint64_t Seed = 1; Seed <= 300; ++Seed) {
    SCOPED_TRACE("seed " + std::to_string(Seed));
    RandomBody Body = makeRandomBody(Seed);

    // The oracle: cli::ILBuilder with relaxation on.
    cli::ILBuilder IB(/*ReturnsValue=*/false);
    std::vector<cli::ILLabel> ILLabels;
    for (unsigned L = 0; L != Body.NumLabels; ++L)
      ILLabels.push_back(IB.createLabel());
    for (const RandomBody::Item &It : Body.Items) {
      switch (It.K) {
      case RandomBody::PlaceLabel:
        IB.placeLabel(ILLabels[It.Targets[0]]);
        break;
      case RandomBody::Nop:
        IB.emit(Opcode::Nop);
        break;
      case RandomBody::LdcI4Zero:
        IB.emitLdcI4(0);
        break;
      case RandomBody::Branch:
        IB.emitBranch(It.Op, ILLabels[It.Targets[0]]);
        break;
      case RandomBody::Switch: {
        std::vector<cli::ILLabel> T;
        for (unsigned L : It.Targets)
          T.push_back(ILLabels[L]);
        IB.emitSwitch(T);
        break;
      }
      case RandomBody::Ret:
        IB.emitRet();
        break;
      }
    }
    if (Error E = IB.layout()) {
      ADD_FAILURE() << toString(std::move(E));
      continue;
    }
    ArrayRef<cli::ILInstr> Want = IB.instructions();

    std::vector<std::vector<Opcode>> Results;
    for (unsigned Mode = 0; Mode != 3; ++Mode) {
      SCOPED_TRACE("input forms " + std::to_string(Mode));
      LCG Mix(Seed + 1000);
      Code C;
      std::vector<const MCSymbol *> Labels;
      for (unsigned L = 0; L != Body.NumLabels; ++L)
        Labels.push_back(newLabel());
      for (const RandomBody::Item &It : Body.Items) {
        switch (It.K) {
        case RandomBody::PlaceLabel:
          place(C, Labels[It.Targets[0]]);
          break;
        case RandomBody::Nop:
          add(C, Opcode::Nop);
          break;
        case RandomBody::LdcI4Zero:
          add(C, Opcode::Ldc_i4_0);
          break;
        case RandomBody::Branch: {
          Opcode Short = cli::getShortBranchForm(It.Op).value_or(It.Op);
          Opcode Long = cli::getLongBranchForm(It.Op).value_or(It.Op);
          bool UseShort = Mode == 0 || (Mode == 2 && Mix.below(2));
          branch(C, UseShort ? Short : Long, Labels[It.Targets[0]]);
          break;
        }
        case RandomBody::Switch: {
          std::vector<const MCSymbol *> T;
          for (unsigned L : It.Targets)
            T.push_back(Labels[L]);
          switchTo(C, T);
          break;
        }
        case RandomBody::Ret:
          add(C, Opcode::Ret);
          break;
        }
      }
      relax(C);
      ASSERT_EQ(Want.size(), C.Insts.size());
      std::vector<uint32_t> Off = offsets(C);
      std::vector<Opcode> Got;
      for (unsigned I = 0; I != C.Insts.size(); ++I) {
        Opcode Op = opcodeAt(C, I);
        Got.push_back(Op);
        EXPECT_EQ(cli::getOpcodeInfo(Want[I].Op).Name.str(),
                  cli::getOpcodeInfo(Op).Name.str())
            << "instruction " << I;
        EXPECT_EQ(Want[I].Offset, Off[I]) << "instruction " << I;
      }
      EXPECT_EQ(IB.getCodeSize(), Off.back());
      // A second run finds nothing to change.
      relax(C);
      for (unsigned I = 0; I != C.Insts.size(); ++I)
        EXPECT_EQ(unsigned(Got[I]), unsigned(opcodeAt(C, I)))
            << "instruction " << I << " changed on a second run";
      Results.push_back(Got);
    }
    ASSERT_EQ(3u, Results.size());
    EXPECT_TRUE(Results[0] == Results[1]);
    EXPECT_TRUE(Results[0] == Results[2]);
    for (Opcode Op : Results[0]) {
      OperandKind K = cli::getOpcodeInfo(Op).Operand;
      Branches += isBranchKind(K);
      LongBranches += K == OperandKind::InlineBrTarget;
    }
  }
  // The bodies exercise both forms.
  EXPECT_GT(Branches, 500u);
  EXPECT_GT(LongBranches, 100u);
  EXPECT_GT(Branches - LongBranches, 100u);
}

} // namespace
