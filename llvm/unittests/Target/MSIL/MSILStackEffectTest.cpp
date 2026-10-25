//===- MSILStackEffectTest.cpp - Stack effects of the MSIL records --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The evaluation-stack contract of the MSIL instruction records
// (ImplementationPlan.md 5.4; recon isel.md 9 and passes-frame.md 8.2). After
// MSILExplicitLocals every explicit register use of an instruction pops one
// value and every explicit register def pushes one, in operand order, and
// MSILStackCheck computes .maxstack from exactly that, plus the StackPeakExtra
// field (TSFlags bits 1-0): the extra depth, above max(pops, pushes), reached
// by the CIL_ instructions that MSILMCInstLower::lowerImpl expands a record
// into. So:
//   - no record pushes more than one value (TEE_<RC>, which exists only
//     before ExplicitLocals turns it into LOCAL_TEE_<RC>, is the exception);
//   - the operand lists of the structural records are those of plan 5.4,
//     which fixes their pops and pushes and which MIR tests are written
//     against;
//   - StackPeakExtra is 1 exactly for 'dup; stloc' (LOCAL_TEE), 'dup; starg'
//     (ARG_TEE), 'ldsfld; ldc.i4; add' (GLOBAL_ADDR) and 'ldnull; throw'
//     (TRAP), and 0 for every other structural record, including CONST_I64
//     ('ldc; conv.i' never holds two values);
//   - registers are of the four classes I32, I64, F32 and F64 only (one CIL
//     type per class, decision D2);
//   - every record but ARGUMENT_<RC> implicitly defines ARGUMENTS and every
//     ARGUMENT_<RC> implicitly uses it, which keeps the arguments at the
//     function entry.
// The flags that RegStackify relies on (side effects, rematerialization) are
// checked against plan 5.4 as well.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "gtest/gtest.h"

#include <memory>
#include <string>
#include <vector>

using namespace llvm;

namespace {

// One fixed operand of a record, in MCInstrDesc order: the '(outs)' of plan
// 5.4 followed by its '(ins)'.
struct OperandSpec {
  bool IsReg;
  // Register class ID for a register operand.
  int RegClass;
  // MCOI or MSIL operand type for any other operand; AnyNonReg for bb_op,
  // whose operand type plan 5.4 leaves open.
  unsigned OperandType;
};

constexpr unsigned AnyNonReg = ~0u;

OperandSpec reg(unsigned RC) { return {true, int(RC), MCOI::OPERAND_REGISTER}; }
OperandSpec other(unsigned Type) { return {false, -1, Type}; }
const OperandSpec BBOp = {false, -1, AnyNonReg};
const OperandSpec I32Imm = other(MCOI::OPERAND_IMMEDIATE);

// What plan 5.4 says about one record.
struct RecordSpec {
  unsigned Opcode;
  std::string Name;
  // Explicit register defs (pushes) and fixed register uses (pops).
  unsigned Defs;
  unsigned Uses;
  std::vector<OperandSpec> Operands;
  bool Variadic;
  unsigned PeakExtra;
};

const unsigned RegClasses[] = {MSIL::I32RegClassID, MSIL::I64RegClassID,
                               MSIL::F32RegClassID, MSIL::F64RegClassID};
const char *const RCSuffix[] = {"I32", "I64", "F32", "F64"};

// The per-class records of plan 5.4, indexed like RegClasses.
const unsigned ArgumentOps[] = {MSIL::ARGUMENT_I32, MSIL::ARGUMENT_I64,
                                MSIL::ARGUMENT_F32, MSIL::ARGUMENT_F64};
const unsigned CopyOps[] = {MSIL::COPY_I32, MSIL::COPY_I64, MSIL::COPY_F32,
                            MSIL::COPY_F64};
const unsigned TeeOps[] = {MSIL::TEE_I32, MSIL::TEE_I64, MSIL::TEE_F32,
                           MSIL::TEE_F64};
const unsigned LocalGetOps[] = {MSIL::LOCAL_GET_I32, MSIL::LOCAL_GET_I64,
                                MSIL::LOCAL_GET_F32, MSIL::LOCAL_GET_F64};
const unsigned LocalSetOps[] = {MSIL::LOCAL_SET_I32, MSIL::LOCAL_SET_I64,
                                MSIL::LOCAL_SET_F32, MSIL::LOCAL_SET_F64};
const unsigned LocalTeeOps[] = {MSIL::LOCAL_TEE_I32, MSIL::LOCAL_TEE_I64,
                                MSIL::LOCAL_TEE_F32, MSIL::LOCAL_TEE_F64};
const unsigned ArgGetOps[] = {MSIL::ARG_GET_I32, MSIL::ARG_GET_I64,
                              MSIL::ARG_GET_F32, MSIL::ARG_GET_F64};
const unsigned ArgSetOps[] = {MSIL::ARG_SET_I32, MSIL::ARG_SET_I64,
                              MSIL::ARG_SET_F32, MSIL::ARG_SET_F64};
const unsigned ArgTeeOps[] = {MSIL::ARG_TEE_I32, MSIL::ARG_TEE_I64,
                              MSIL::ARG_TEE_F32, MSIL::ARG_TEE_F64};
const unsigned DropOps[] = {MSIL::DROP_I32, MSIL::DROP_I64, MSIL::DROP_F32,
                            MSIL::DROP_F64};

std::vector<RecordSpec> structuralRecords() {
  std::vector<RecordSpec> R;
  auto Add = [&R](unsigned Opcode, std::string Name, unsigned Defs,
                  unsigned Uses, std::vector<OperandSpec> Operands,
                  bool Variadic, unsigned PeakExtra) {
    R.push_back({Opcode, std::move(Name), Defs, Uses, std::move(Operands),
                 Variadic, PeakExtra});
  };
  const OperandSpec Local = other(MSIL::OPERAND_LOCAL);
  const OperandSpec Arg = other(MSIL::OPERAND_ARG);
  for (unsigned I = 0; I != 4; ++I) {
    const OperandSpec RC = reg(RegClasses[I]);
    const std::string S = RCSuffix[I];
    Add(ArgumentOps[I], "ARGUMENT_" + S, 1, 0, {RC, I32Imm}, false, 0);
    Add(CopyOps[I], "COPY_" + S, 1, 1, {RC, RC}, false, 0);
    Add(TeeOps[I], "TEE_" + S, 2, 1, {RC, RC, RC}, false, 0);
    Add(LocalGetOps[I], "LOCAL_GET_" + S, 1, 0, {RC, Local}, false, 0);
    Add(LocalSetOps[I], "LOCAL_SET_" + S, 0, 1, {Local, RC}, false, 0);
    Add(LocalTeeOps[I], "LOCAL_TEE_" + S, 1, 1, {RC, Local, RC}, false, 1);
    Add(ArgGetOps[I], "ARG_GET_" + S, 1, 0, {RC, Arg}, false, 0);
    Add(ArgSetOps[I], "ARG_SET_" + S, 0, 1, {Arg, RC}, false, 0);
    Add(ArgTeeOps[I], "ARG_TEE_" + S, 1, 1, {RC, Arg, RC}, false, 1);
    Add(DropOps[I], "DROP_" + S, 0, 1, {RC}, false, 0);
  }
  const OperandSpec I32 = reg(MSIL::I32RegClassID);
  const OperandSpec I64 = reg(MSIL::I64RegClassID);
  const OperandSpec F32 = reg(MSIL::F32RegClassID);
  const OperandSpec F64 = reg(MSIL::F64RegClassID);
  const OperandSpec Method = other(MSIL::OPERAND_METHOD);
  Add(MSIL::CONST_I32, "CONST_I32", 1, 0, {I32, other(MSIL::OPERAND_I32IMM)},
      false, 0);
  Add(MSIL::CONST_I64, "CONST_I64", 1, 0, {I64, other(MSIL::OPERAND_I64IMM)},
      false, 0);
  Add(MSIL::CONST_F32, "CONST_F32", 1, 0, {F32, other(MSIL::OPERAND_F32IMM)},
      false, 0);
  Add(MSIL::CONST_F64, "CONST_F64", 1, 0, {F64, other(MSIL::OPERAND_F64IMM)},
      false, 0);
  Add(MSIL::GLOBAL_ADDR, "GLOBAL_ADDR", 1, 0, {I64, other(MSIL::OPERAND_DATA)},
      false, 1);
  Add(MSIL::FUNC_ADDR, "FUNC_ADDR", 1, 0, {I64, Method}, false, 0);
  // Control.
  Add(MSIL::RETURN, "RETURN", 0, 0, {}, true, 0);
  Add(MSIL::TRAP, "TRAP", 0, 0, {}, false, 1);
  Add(MSIL::BR, "BR", 0, 0, {BBOp}, false, 0);
  Add(MSIL::BR_IF, "BR_IF", 0, 1, {BBOp, I32}, false, 0);
  Add(MSIL::BR_UNLESS, "BR_UNLESS", 0, 1, {BBOp, I32}, false, 0);
  Add(MSIL::BR_TABLE_I32, "BR_TABLE_I32", 0, 1, {I32}, true, 0);
  // Calls.
  Add(MSIL::ADJCALLSTACKDOWN, "ADJCALLSTACKDOWN", 0, 0, {I32Imm, I32Imm}, false,
      0);
  Add(MSIL::ADJCALLSTACKUP, "ADJCALLSTACKUP", 0, 0, {I32Imm, I32Imm}, false, 0);
  Add(MSIL::CALL_PARAMS, "CALL_PARAMS", 0, 0, {Method}, true, 0);
  Add(MSIL::CALL_RESULTS, "CALL_RESULTS", 0, 0, {}, true, 0);
  Add(MSIL::CALL, "CALL", 0, 0, {Method}, true, 0);
  Add(MSIL::CALL_INDIRECT, "CALL_INDIRECT", 0, 0, {}, true, 0);
  // Frame.
  Add(MSIL::LOCALLOC, "LOCALLOC", 1, 1, {I64, I32}, false, 0);
  return R;
}

unsigned peakExtra(const MCInstrDesc &Desc) {
  return Desc.TSFlags & MSILII::StackPeakExtraMask;
}

bool isArgumentOpcode(unsigned Opc) { return is_contained(ArgumentOps, Opc); }

bool isTeeOpcode(unsigned Opc) { return is_contained(TeeOps, Opc); }

class MSILStackEffectTest : public testing::Test {
protected:
  void SetUp() override { MII.reset(createMSILMCInstrInfo()); }

  // Every opcode the MSIL target defines (not the generic ones).
  std::vector<unsigned> targetOpcodes() const {
    std::vector<unsigned> Ops;
    for (unsigned Opc = 0, E = MII->getNumOpcodes(); Opc != E; ++Opc)
      if (isTargetSpecificOpcode(Opc))
        Ops.push_back(Opc);
    return Ops;
  }

  std::unique_ptr<MCInstrInfo> MII;
};

TEST_F(MSILStackEffectTest, StructuralRecordsMatchPlan54) {
  for (const RecordSpec &R : structuralRecords()) {
    SCOPED_TRACE(R.Name);
    const MCInstrDesc &Desc = MII->get(R.Opcode);
    EXPECT_EQ(R.Name, MII->getName(R.Opcode).str());
    EXPECT_EQ(R.Defs, Desc.getNumDefs());
    EXPECT_EQ(R.Variadic, Desc.isVariadic());
    ASSERT_EQ(R.Operands.size(), Desc.getNumOperands());
    unsigned Uses = 0;
    for (auto [Index, Expected] : enumerate(R.Operands)) {
      SCOPED_TRACE("operand " + std::to_string(Index));
      const MCOperandInfo &Info = Desc.operands()[Index];
      if (Expected.IsReg) {
        EXPECT_EQ(MCOI::OPERAND_REGISTER, Info.OperandType);
        EXPECT_EQ(Expected.RegClass, Info.RegClass);
        if (Index >= Desc.getNumDefs())
          ++Uses;
      } else {
        EXPECT_NE(MCOI::OPERAND_REGISTER, Info.OperandType);
        EXPECT_EQ(-1, Info.RegClass);
        if (Expected.OperandType != AnyNonReg)
          EXPECT_EQ(Expected.OperandType, Info.OperandType);
      }
    }
    EXPECT_EQ(R.Uses, Uses);
    EXPECT_EQ(R.PeakExtra, peakExtra(Desc));
    EXPECT_EQ(0u, Desc.TSFlags & MSILII::TrapsOnlyOnUB);
  }
}

// After ExplicitLocals an instruction pushes at most one value; the stack
// model and MSILStackCheck depend on it. Only TEE_<RC>, which ExplicitLocals
// always replaces, has two defs.
TEST_F(MSILStackEffectTest, MSILRecordsPushAtMostOneValue) {
  unsigned Checked = 0;
  for (unsigned Opc : targetOpcodes()) {
    SCOPED_TRACE(MII->getName(Opc).str());
    const MCInstrDesc &Desc = MII->get(Opc);
    unsigned RegDefs = 0;
    for (unsigned I = 0, E = Desc.getNumDefs(); I != E; ++I)
      if (Desc.operands()[I].OperandType == MCOI::OPERAND_REGISTER)
        ++RegDefs;
    EXPECT_EQ(Desc.getNumDefs(), RegDefs);
    EXPECT_LE(RegDefs, isTeeOpcode(Opc) ? 2u : 1u);
    ++Checked;
  }
  // The walk must at least have seen the structural records.
  EXPECT_GE(Checked, structuralRecords().size());
}

// One CIL type per register class (decision D2): every register operand of
// every record is I32, I64 (native int), F32 or F64.
TEST_F(MSILStackEffectTest, MSILRegisterOperandsUseTheFourClasses) {
  for (unsigned Opc : targetOpcodes()) {
    SCOPED_TRACE(MII->getName(Opc).str());
    const MCInstrDesc &Desc = MII->get(Opc);
    for (const MCOperandInfo &Info : Desc.operands()) {
      if (Info.OperandType != MCOI::OPERAND_REGISTER)
        continue;
      EXPECT_TRUE(is_contained(RegClasses, unsigned(Info.RegClass)))
          << "register class " << Info.RegClass;
    }
  }
}

// TSFlags holds StackPeakExtra (bits 1-0), TrapsOnlyOnUB (bit 2) and the CIL
// opcode (bits 18-3, M18) and nothing else (MSILInstrFormats.td and MSILII
// must stay in sync).
TEST_F(MSILStackEffectTest, MSILTSFlagsUseOnlyTheDefinedBits) {
  const uint64_t Known = MSILII::StackPeakExtraMask | MSILII::TrapsOnlyOnUB |
                         MSILII::CILOpcodeMask;
  for (unsigned Opc : targetOpcodes()) {
    SCOPED_TRACE(MII->getName(Opc).str());
    EXPECT_EQ(0u, MII->get(Opc).TSFlags & ~Known);
  }
}

// ARGUMENT_<RC> reads the ARGUMENTS register and every other record writes
// it, so no instruction is scheduled above an ARGUMENT (plan 5.4: MIR writes
// 'implicit $arguments' and 'implicit-def dead $arguments').
TEST_F(MSILStackEffectTest, MSILArgumentsOrdering) {
  for (unsigned Opc : targetOpcodes()) {
    SCOPED_TRACE(MII->getName(Opc).str());
    const MCInstrDesc &Desc = MII->get(Opc);
    if (isArgumentOpcode(Opc)) {
      EXPECT_TRUE(Desc.implicit_defs().empty());
      EXPECT_TRUE(
          is_contained(Desc.implicit_uses(), MCPhysReg(MSIL::ARGUMENTS)));
    } else {
      EXPECT_TRUE(
          is_contained(Desc.implicit_defs(), MCPhysReg(MSIL::ARGUMENTS)));
      EXPECT_FALSE(
          is_contained(Desc.implicit_uses(), MCPhysReg(MSIL::ARGUMENTS)));
    }
    // VALUE_STACK operands are added by RegStackify, never by a record.
    EXPECT_FALSE(
        is_contained(Desc.implicit_defs(), MCPhysReg(MSIL::VALUE_STACK)));
    EXPECT_FALSE(
        is_contained(Desc.implicit_uses(), MCPhysReg(MSIL::VALUE_STACK)));
  }
}

// The flags of plan 5.4 that RegStackify, rematerialization and branch
// analysis rely on.
TEST_F(MSILStackEffectTest, MSILRecordFlagsMatchPlan54) {
  for (unsigned I = 0; I != 4; ++I) {
    SCOPED_TRACE(RCSuffix[I]);
    const MCInstrDesc &Argument = MII->get(ArgumentOps[I]);
    EXPECT_TRUE(Argument.hasUnmodeledSideEffects());
    EXPECT_TRUE(MII->get(CopyOps[I]).isAsCheapAsAMove());
    EXPECT_TRUE(MII->get(TeeOps[I]).isAsCheapAsAMove());
    EXPECT_TRUE(MII->get(LocalGetOps[I]).mayLoad());
    EXPECT_TRUE(MII->get(LocalSetOps[I]).mayStore());
    EXPECT_TRUE(MII->get(LocalTeeOps[I]).mayStore());
    EXPECT_TRUE(MII->get(ArgGetOps[I]).mayLoad());
    EXPECT_TRUE(MII->get(ArgSetOps[I]).mayStore());
    EXPECT_TRUE(MII->get(ArgTeeOps[I]).mayStore());
  }

  for (unsigned Opc :
       {MSIL::CONST_I32, MSIL::CONST_I64, MSIL::CONST_F32, MSIL::CONST_F64}) {
    SCOPED_TRACE(MII->getName(Opc).str());
    const MCInstrDesc &Desc = MII->get(Opc);
    EXPECT_TRUE(Desc.isMoveImmediate());
    EXPECT_TRUE(Desc.isAsCheapAsAMove());
    EXPECT_TRUE(Desc.isRematerializable());
    EXPECT_FALSE(Desc.hasUnmodeledSideEffects());
    EXPECT_FALSE(Desc.mayLoad());
    EXPECT_FALSE(Desc.mayStore());
  }
  EXPECT_TRUE(MII->get(MSIL::GLOBAL_ADDR).isRematerializable());
  EXPECT_TRUE(MII->get(MSIL::FUNC_ADDR).isRematerializable());

  // A localloc is never moved, cloned or folded into a tree.
  const MCInstrDesc &Localloc = MII->get(MSIL::LOCALLOC);
  EXPECT_TRUE(Localloc.hasUnmodeledSideEffects());
  EXPECT_FALSE(Localloc.isRematerializable());
  EXPECT_FALSE(Localloc.isAsCheapAsAMove());

  const MCInstrDesc &Return = MII->get(MSIL::RETURN);
  EXPECT_TRUE(Return.isReturn());
  EXPECT_TRUE(Return.isTerminator());
  EXPECT_TRUE(Return.isBarrier());

  const MCInstrDesc &Trap = MII->get(MSIL::TRAP);
  EXPECT_TRUE(Trap.isTrap());
  EXPECT_TRUE(Trap.isTerminator());
  EXPECT_TRUE(Trap.isBarrier());

  for (unsigned Opc : {MSIL::BR, MSIL::BR_TABLE_I32}) {
    SCOPED_TRACE(MII->getName(Opc).str());
    const MCInstrDesc &Desc = MII->get(Opc);
    EXPECT_TRUE(Desc.isBranch());
    EXPECT_TRUE(Desc.isTerminator());
    EXPECT_TRUE(Desc.isBarrier());
  }
  for (unsigned Opc : {MSIL::BR_IF, MSIL::BR_UNLESS}) {
    SCOPED_TRACE(MII->getName(Opc).str());
    const MCInstrDesc &Desc = MII->get(Opc);
    EXPECT_TRUE(Desc.isBranch());
    EXPECT_TRUE(Desc.isTerminator());
    EXPECT_FALSE(Desc.isBarrier());
  }

  for (unsigned Opc : {MSIL::CALL_PARAMS, MSIL::CALL_RESULTS, MSIL::CALL,
                       MSIL::CALL_INDIRECT}) {
    SCOPED_TRACE(MII->getName(Opc).str());
    EXPECT_TRUE(MII->get(Opc).isCall());
  }
  for (unsigned Opc : {MSIL::CALL_RESULTS, MSIL::CALL, MSIL::CALL_INDIRECT}) {
    SCOPED_TRACE(MII->getName(Opc).str());
    EXPECT_TRUE(MII->get(Opc).variadicOpsAreDefs());
  }
  EXPECT_FALSE(MII->get(MSIL::CALL_PARAMS).variadicOpsAreDefs());
}

} // end anonymous namespace
