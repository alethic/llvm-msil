//===- Opcodes.cpp - ECMA-335 CIL opcode table ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BinaryFormat/CILOpcodes.def as one constexpr table, with constexpr indexes
// from opcode values to entries. The library has no static constructors, and
// the static_asserts below check the .def's invariants at compile time.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/Opcodes.h"
#include "llvm/Support/ErrorHandling.h"
#include <array>
#include <cassert>
#include <iterator>

using namespace llvm;
using namespace llvm::cli;

static constexpr OpcodeInfo OpcodeTable[] = {
#define CIL_OPCODE(Enum, Text, Value, Operand, Pop, Push, Flow)                \
  {Opcode::Enum,         Text,                                                 \
   OperandKind::Operand, StackBehaviour::Pop,                                  \
   StackBehaviour::Push, FlowControl::Flow},
#include "llvm/BinaryFormat/CILOpcodes.def"
};

static constexpr unsigned NumOpcodes = std::size(OpcodeTable);
static_assert(NumOpcodes == 219, "CILOpcodes.def: 218 opcodes plus 'no.'");
static_assert(NumOpcodes < 0xFF, "the value indexes use 0xFF for 'none'");

static constexpr bool opcodesAreInValueOrder() {
  for (unsigned I = 1; I != NumOpcodes; ++I)
    if (static_cast<uint16_t>(OpcodeTable[I - 1].Op) >=
        static_cast<uint16_t>(OpcodeTable[I].Op))
      return false;
  return true;
}
static_assert(opcodesAreInValueOrder(),
              "CILOpcodes.def: entries must be in strictly increasing value "
              "order");

static constexpr bool opcodeValuesAreEncodable() {
  for (const OpcodeInfo &Info : OpcodeTable) {
    uint16_t V = static_cast<uint16_t>(Info.Op);
    if (V >> 8 != 0 && V >> 8 != TwoByteOpcodePrefix)
      return false;
    // 0xF8-0xFF are reserved prefix bytes, and 0xFE starts two-byte opcodes.
    if (V >> 8 == 0 && V >= 0xF8)
      return false;
  }
  return true;
}
static_assert(opcodeValuesAreEncodable(),
              "CILOpcodes.def: a value is neither 0x00XX nor 0xFEXX");

/// Index arrays from the low byte of a value to its OpcodeTable entry, 0xFF
/// for no opcode: one for the one-byte opcodes, one for 0xFEXX.
using ValueIndex = std::array<uint8_t, 256>;

static constexpr ValueIndex buildValueIndex(bool TwoByte) {
  ValueIndex Index{};
  for (uint8_t &E : Index)
    E = 0xFF;
  for (unsigned I = 0; I != NumOpcodes; ++I) {
    uint16_t V = static_cast<uint16_t>(OpcodeTable[I].Op);
    if ((V >> 8 == TwoByteOpcodePrefix) == TwoByte)
      Index[V & 0xFF] = static_cast<uint8_t>(I);
  }
  return Index;
}

static constexpr ValueIndex OneByteIndex = buildValueIndex(false);
static constexpr ValueIndex TwoByteIndex = buildValueIndex(true);

/// The OpcodeTable index of \p Value, or 0xFF.
static unsigned lookupValue(uint16_t Value) {
  if (Value >> 8 == 0)
    return OneByteIndex[Value];
  if (Value >> 8 == TwoByteOpcodePrefix)
    return TwoByteIndex[Value & 0xFF];
  return 0xFF;
}

const OpcodeInfo &cli::getOpcodeInfo(Opcode Op) {
  unsigned I = lookupValue(static_cast<uint16_t>(Op));
  assert(I != 0xFF && "not a CILOpcodes.def opcode");
  if (I == 0xFF)
    I = 0;
  return OpcodeTable[I];
}

ArrayRef<OpcodeInfo> cli::getAllOpcodes() { return OpcodeTable; }

std::optional<Opcode> cli::getOpcodeByValue(uint16_t Value) {
  unsigned I = lookupValue(Value);
  if (I == 0xFF)
    return std::nullopt;
  return OpcodeTable[I].Op;
}

std::optional<Opcode> cli::getOpcodeByName(StringRef Name) {
  for (const OpcodeInfo &Info : OpcodeTable)
    if (Info.Name == Name)
      return Info.Op;
  return std::nullopt;
}

unsigned cli::getOpcodeSize(Opcode Op) {
  return static_cast<uint16_t>(Op) >> 8 == TwoByteOpcodePrefix ? 2 : 1;
}

unsigned cli::getOperandSize(OperandKind K) {
  switch (K) {
  case OperandKind::InlineNone:
    return 0;
  case OperandKind::ShortInlineVar:
  case OperandKind::ShortInlineI:
  case OperandKind::ShortInlineBrTarget:
    return 1;
  case OperandKind::InlineVar:
    return 2;
  case OperandKind::InlineI:
  case OperandKind::ShortInlineR:
  case OperandKind::InlineBrTarget:
  case OperandKind::InlineMethod:
  case OperandKind::InlineField:
  case OperandKind::InlineType:
  case OperandKind::InlineTok:
  case OperandKind::InlineSig:
  case OperandKind::InlineString:
  case OperandKind::InlineSwitch:
    return 4;
  case OperandKind::InlineI8:
  case OperandKind::InlineR:
    return 8;
  }
  llvm_unreachable("unknown operand kind");
}

// The short branches 0x2B-0x37 (br.s ... blt.un.s) and the long branches
// 0x38-0x44 (br ... blt.un) are two runs in the same order; leave.s and leave
// are 0xDE and 0xDD.
static constexpr uint16_t FirstShortBranch = 0x2B, LastShortBranch = 0x37;
static constexpr uint16_t FirstLongBranch = 0x38, LastLongBranch = 0x44;
static_assert(static_cast<uint16_t>(Opcode::Br_s) == FirstShortBranch &&
                  static_cast<uint16_t>(Opcode::Blt_un_s) == LastShortBranch &&
                  static_cast<uint16_t>(Opcode::Br) == FirstLongBranch &&
                  static_cast<uint16_t>(Opcode::Blt_un) == LastLongBranch,
              "CILOpcodes.def: unexpected branch opcode values");

std::optional<Opcode> cli::getShortBranchForm(Opcode Op) {
  uint16_t V = static_cast<uint16_t>(Op);
  if (V >= FirstShortBranch && V <= LastShortBranch)
    return Op;
  if (V >= FirstLongBranch && V <= LastLongBranch)
    return static_cast<Opcode>(V - FirstLongBranch + FirstShortBranch);
  if (Op == Opcode::Leave || Op == Opcode::Leave_s)
    return Opcode::Leave_s;
  return std::nullopt;
}

std::optional<Opcode> cli::getLongBranchForm(Opcode Op) {
  uint16_t V = static_cast<uint16_t>(Op);
  if (V >= FirstLongBranch && V <= LastLongBranch)
    return Op;
  if (V >= FirstShortBranch && V <= LastShortBranch)
    return static_cast<Opcode>(V - FirstShortBranch + FirstLongBranch);
  if (Op == Opcode::Leave || Op == Opcode::Leave_s)
    return Opcode::Leave;
  return std::nullopt;
}

int cli::getStackPops(StackBehaviour B) {
  switch (B) {
  case StackBehaviour::Pop0:
    return 0;
  case StackBehaviour::Pop1:
  case StackBehaviour::Popi:
  case StackBehaviour::Popref:
    return 1;
  case StackBehaviour::Pop1_pop1:
  case StackBehaviour::Popi_pop1:
  case StackBehaviour::Popi_popi:
  case StackBehaviour::Popi_popi8:
  case StackBehaviour::Popi_popr4:
  case StackBehaviour::Popi_popr8:
  case StackBehaviour::Popref_pop1:
  case StackBehaviour::Popref_popi:
    return 2;
  case StackBehaviour::Popi_popi_popi:
  case StackBehaviour::Popref_popi_popi:
  case StackBehaviour::Popref_popi_popi8:
  case StackBehaviour::Popref_popi_popr4:
  case StackBehaviour::Popref_popi_popr8:
  case StackBehaviour::Popref_popi_popref:
  case StackBehaviour::Popref_popi_pop1:
    return 3;
  case StackBehaviour::Varpop:
    return -1;
  case StackBehaviour::Push0:
  case StackBehaviour::Push1:
  case StackBehaviour::Push1_push1:
  case StackBehaviour::Pushi:
  case StackBehaviour::Pushi8:
  case StackBehaviour::Pushr4:
  case StackBehaviour::Pushr8:
  case StackBehaviour::Pushref:
  case StackBehaviour::Varpush:
    return 0;
  }
  llvm_unreachable("unknown stack behaviour");
}

int cli::getStackPushes(StackBehaviour B) {
  switch (B) {
  case StackBehaviour::Push0:
    return 0;
  case StackBehaviour::Push1:
  case StackBehaviour::Pushi:
  case StackBehaviour::Pushi8:
  case StackBehaviour::Pushr4:
  case StackBehaviour::Pushr8:
  case StackBehaviour::Pushref:
    return 1;
  case StackBehaviour::Push1_push1:
    return 2;
  case StackBehaviour::Varpush:
    return -1;
  case StackBehaviour::Pop0:
  case StackBehaviour::Pop1:
  case StackBehaviour::Pop1_pop1:
  case StackBehaviour::Popi:
  case StackBehaviour::Popi_pop1:
  case StackBehaviour::Popi_popi:
  case StackBehaviour::Popi_popi8:
  case StackBehaviour::Popi_popi_popi:
  case StackBehaviour::Popi_popr4:
  case StackBehaviour::Popi_popr8:
  case StackBehaviour::Popref:
  case StackBehaviour::Popref_pop1:
  case StackBehaviour::Popref_popi:
  case StackBehaviour::Popref_popi_popi:
  case StackBehaviour::Popref_popi_popi8:
  case StackBehaviour::Popref_popi_popr4:
  case StackBehaviour::Popref_popi_popr8:
  case StackBehaviour::Popref_popi_popref:
  case StackBehaviour::Popref_popi_pop1:
  case StackBehaviour::Varpop:
    return 0;
  }
  llvm_unreachable("unknown stack behaviour");
}
