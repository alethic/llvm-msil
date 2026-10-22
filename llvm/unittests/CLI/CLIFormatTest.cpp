//===- CLIFormatTest.cpp - ECMA-335 format constants and encodings --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/BinaryFormat/CLI.h, CLITables.def, CILOpcodes.def and
// llvm/CLI/Handles.h against ECMA-335 (6th edition) and System.Reflection:
// compressed integers (II.23.2), tokens, the .def invariants, the flag enums
// (II.23.1), and the image, metadata-root and method-body constants (II.24,
// II.25). Expected values come from ECMA-335, from System.Reflection (dumped
// with PowerShell on .NET 10.0.13 and embedded here) and from
// llvm/docs/MSIL/recon/p2/metadata-writer-files/dotnet-opcodes.tsv, never from
// the headers under test.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/Handles.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

//===----------------------------------------------------------------------===//
// Compressed integers (II.23.2)
//===----------------------------------------------------------------------===//

using ByteVec = std::vector<uint8_t>;

ByteVec encodeUInt(uint32_t V) {
  uint8_t Out[4] = {0, 0, 0, 0};
  unsigned N = encodeCompressedUInt(V, Out);
  return ByteVec(Out, Out + std::min(N, 4u));
}

ByteVec encodeInt(int32_t V) {
  uint8_t Out[4] = {0, 0, 0, 0};
  unsigned N = encodeCompressedInt(V, Out);
  return ByteVec(Out, Out + std::min(N, 4u));
}

// The unsigned examples of II.23.2, plus 0 and 1 (System.Reflection.Metadata's
// BlobBuilder.WriteCompressedInteger gives the same bytes for all of them).
TEST(CLIFormatTest, CompressedUIntECMAExamples) {
  struct {
    uint32_t Value;
    ByteVec Encoded;
  } Cases[] = {
      {0x0, {0x00}},
      {0x1, {0x01}},
      {0x03, {0x03}},
      {0x7F, {0x7F}},
      {0x80, {0x80, 0x80}},
      {0x2E57, {0xAE, 0x57}},
      {0x3FFF, {0xBF, 0xFF}},
      {0x4000, {0xC0, 0x00, 0x40, 0x00}},
      {0x1FFFFFFF, {0xDF, 0xFF, 0xFF, 0xFF}},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE("value 0x" + utohexstr(C.Value));
    EXPECT_EQ(encodeUInt(C.Value), C.Encoded);
    EXPECT_EQ(getCompressedUIntSize(C.Value), C.Encoded.size());
    ArrayRef<uint8_t> In(C.Encoded);
    std::optional<uint32_t> D = decodeCompressedUInt(In);
    ASSERT_TRUE(D.has_value());
    EXPECT_EQ(*D, C.Value);
    EXPECT_TRUE(In.empty()) << "the decoder consumes exactly the encoding";
  }
  EXPECT_EQ(MaxCompressedUInt, 0x1FFFFFFFu);
}

// The width boundaries 0x7F/0x80 and 0x3FFF/0x4000 and the maximum.
TEST(CLIFormatTest, CompressedUIntSizes) {
  EXPECT_EQ(getCompressedUIntSize(0), 1u);
  EXPECT_EQ(getCompressedUIntSize(0x7F), 1u);
  EXPECT_EQ(getCompressedUIntSize(0x80), 2u);
  EXPECT_EQ(getCompressedUIntSize(0x3FFF), 2u);
  EXPECT_EQ(getCompressedUIntSize(0x4000), 4u);
  EXPECT_EQ(getCompressedUIntSize(MaxCompressedUInt), 4u);
  for (uint32_t V : {0x0u, 0x7Fu, 0x80u, 0x100u, 0x3FFFu, 0x4000u, 0x12345u,
                     0x1000000u, MaxCompressedUInt}) {
    ByteVec E = encodeUInt(V);
    ASSERT_EQ(E.size(), getCompressedUIntSize(V)) << V;
    ArrayRef<uint8_t> In(E);
    EXPECT_EQ(decodeCompressedUInt(In), std::optional<uint32_t>(V));
  }
}

// The decoder consumes one value at a time from a longer buffer.
TEST(CLIFormatTest, CompressedUIntDecodeSequence) {
  const uint8_t Buf[] = {0x03, 0xAE, 0x57, 0xC0, 0x00, 0x40, 0x00, 0x7F, 0x2A};
  ArrayRef<uint8_t> In(Buf);
  EXPECT_EQ(decodeCompressedUInt(In), std::optional<uint32_t>(0x03));
  EXPECT_EQ(In.size(), 8u);
  EXPECT_EQ(decodeCompressedUInt(In), std::optional<uint32_t>(0x2E57));
  EXPECT_EQ(In.size(), 6u);
  EXPECT_EQ(decodeCompressedUInt(In), std::optional<uint32_t>(0x4000));
  EXPECT_EQ(In.size(), 2u);
  EXPECT_EQ(decodeCompressedUInt(In), std::optional<uint32_t>(0x7F));
  ASSERT_EQ(In.size(), 1u);
  EXPECT_EQ(In[0], 0x2A);
}

// Lead bytes 111xxxxx encode nothing in II.23.2 (0xFF is the null string of
// SerString, not an integer), and a value cut off before its last byte is
// truncated. Both are input errors: nullopt, never an assertion.
TEST(CLIFormatTest, CompressedUIntRejectsBadInput) {
  for (unsigned Lead = 0xE0; Lead <= 0xFF; ++Lead) {
    const uint8_t Buf[] = {static_cast<uint8_t>(Lead), 0, 0, 0, 0};
    ArrayRef<uint8_t> In(Buf);
    EXPECT_FALSE(decodeCompressedUInt(In).has_value())
        << "lead 0x" << utohexstr(Lead);
    ArrayRef<uint8_t> InS(Buf);
    EXPECT_FALSE(decodeCompressedInt(InS).has_value())
        << "lead 0x" << utohexstr(Lead);
  }
  const ByteVec Truncated[] = {{},
                               {0x80},
                               {0xBF},
                               {0xC0},
                               {0xC0, 0x00},
                               {0xC0, 0x00, 0x40},
                               {0xDF, 0xFF, 0xFF}};
  for (const ByteVec &T : Truncated) {
    SCOPED_TRACE("truncated input of " + std::to_string(T.size()) + " bytes");
    ArrayRef<uint8_t> In(T);
    EXPECT_FALSE(decodeCompressedUInt(In).has_value());
    ArrayRef<uint8_t> InS(T);
    EXPECT_FALSE(decodeCompressedInt(InS).has_value());
  }
}

// The signed examples of II.23.2 and the range boundaries of each width
// (-2^6..2^6-1, -2^13..2^13-1, -2^28..2^28-1), as System.Reflection.Metadata's
// BlobBuilder.WriteCompressedSignedInteger writes them.
TEST(CLIFormatTest, CompressedIntECMAExamples) {
  struct {
    int32_t Value;
    ByteVec Encoded;
  } Cases[] = {
      {0, {0x00}},
      {3, {0x06}},
      {-3, {0x7B}},
      {63, {0x7E}},
      {-64, {0x01}},
      {64, {0x80, 0x80}},
      {-65, {0xBF, 0x7F}},
      {8191, {0xBF, 0xFE}},
      {-8192, {0x80, 0x01}},
      {8192, {0xC0, 0x00, 0x40, 0x00}},
      {-8193, {0xDF, 0xFF, 0xBF, 0xFF}},
      {268435455, {0xDF, 0xFF, 0xFF, 0xFE}},
      {-268435456, {0xC0, 0x00, 0x00, 0x01}},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE("value " + std::to_string(C.Value));
    EXPECT_EQ(encodeInt(C.Value), C.Encoded);
    ArrayRef<uint8_t> In(C.Encoded);
    std::optional<int32_t> D = decodeCompressedInt(In);
    ASSERT_TRUE(D.has_value());
    EXPECT_EQ(*D, C.Value);
    EXPECT_TRUE(In.empty());
  }
}

// II.23.2.8: TypeRef token 0x01000012 is encoded as 0x49; TypeDef is tag 0,
// TypeRef 1, TypeSpec 2, and tag 3 is unused.
TEST(CLIFormatTest, TypeDefOrRefOrSpecEncoded) {
  EXPECT_EQ(encodeTypeDefOrRefOrSpec(0x01000012), 0x49u);
  EXPECT_EQ(encodeTypeDefOrRefOrSpec(0x02000001), 0x04u);
  EXPECT_EQ(encodeTypeDefOrRefOrSpec(0x1B000003), 0x0Eu);
  EXPECT_EQ(encodeTypeDefOrRefOrSpec(0x01FFFFFF), 0x03FFFFFDu);
  EXPECT_LE(encodeTypeDefOrRefOrSpec(0x1BFFFFFF), MaxCompressedUInt);
  EXPECT_EQ(encodeUInt(encodeTypeDefOrRefOrSpec(0x01000012)), ByteVec{0x49});

  EXPECT_EQ(decodeTypeDefOrRefOrSpec(0x49),
            std::optional<uint32_t>(0x01000012));
  EXPECT_EQ(decodeTypeDefOrRefOrSpec(0x04),
            std::optional<uint32_t>(0x02000001));
  EXPECT_EQ(decodeTypeDefOrRefOrSpec(0x0E),
            std::optional<uint32_t>(0x1B000003));
  EXPECT_EQ(decodeTypeDefOrRefOrSpec(0x03FFFFFD),
            std::optional<uint32_t>(0x01FFFFFF));
  EXPECT_FALSE(decodeTypeDefOrRefOrSpec((0x12 << 2) | 3).has_value());
}

//===----------------------------------------------------------------------===//
// Tokens and table ids (II.22, II.24.2.6, III.1.9)
//===----------------------------------------------------------------------===//

TEST(CLIFormatTest, TokenHelpers) {
  static_assert(makeToken(TableId::MethodDef, 1) == 0x06000001,
                "makeToken is constexpr");
  static_assert(getTokenType(0x0A000002) == 0x0A, "constexpr");
  static_assert(getTokenRow(0x0A000002) == 2, "constexpr");
  EXPECT_EQ(makeToken(TableId::Module, 1), 0x00000001u);
  EXPECT_EQ(makeToken(TableId::TypeRef, 0x00FFFFFF), 0x01FFFFFFu);
  EXPECT_EQ(makeToken(TableId::Field, 0x123456), 0x04123456u);
  EXPECT_EQ(makeToken(TableId::GenericParamConstraint, 7), 0x2C000007u);
  EXPECT_EQ(getTokenType(0x2B000001), 0x2Bu);
  EXPECT_EQ(getTokenRow(0x0A123456), 0x123456u);
  EXPECT_EQ(getTokenRow(0x0A000000), 0u);
  EXPECT_EQ(UserStringTokenType, 0x70u);
  EXPECT_EQ(getTokenType(0x70000001), UserStringTokenType);
  EXPECT_EQ(getTokenRow(0x70ABCDEF), 0xABCDEFu);
  EXPECT_EQ(NumTableIds, 64u);
}

// ECMA-335 II.22's 38 tables and their numbers. The other seven ids below
// 0x2D are the pre-standard FieldPtr, MethodPtr, ParamPtr, EventPtr,
// PropertyPtr, ENCLog and ENCMap tables, which II.22 does not define but
// II.24.2.6 readers must still size.
struct ECMATableId {
  TableId Id;
  unsigned Value;
  const char *Name;
};

const ECMATableId ECMATableIds[] = {
    {TableId::Module, 0x00, "Module"},
    {TableId::TypeRef, 0x01, "TypeRef"},
    {TableId::TypeDef, 0x02, "TypeDef"},
    {TableId::Field, 0x04, "Field"},
    {TableId::MethodDef, 0x06, "MethodDef"},
    {TableId::Param, 0x08, "Param"},
    {TableId::InterfaceImpl, 0x09, "InterfaceImpl"},
    {TableId::MemberRef, 0x0A, "MemberRef"},
    {TableId::Constant, 0x0B, "Constant"},
    {TableId::CustomAttribute, 0x0C, "CustomAttribute"},
    {TableId::FieldMarshal, 0x0D, "FieldMarshal"},
    {TableId::DeclSecurity, 0x0E, "DeclSecurity"},
    {TableId::ClassLayout, 0x0F, "ClassLayout"},
    {TableId::FieldLayout, 0x10, "FieldLayout"},
    {TableId::StandAloneSig, 0x11, "StandAloneSig"},
    {TableId::EventMap, 0x12, "EventMap"},
    {TableId::Event, 0x14, "Event"},
    {TableId::PropertyMap, 0x15, "PropertyMap"},
    {TableId::Property, 0x17, "Property"},
    {TableId::MethodSemantics, 0x18, "MethodSemantics"},
    {TableId::MethodImpl, 0x19, "MethodImpl"},
    {TableId::ModuleRef, 0x1A, "ModuleRef"},
    {TableId::TypeSpec, 0x1B, "TypeSpec"},
    {TableId::ImplMap, 0x1C, "ImplMap"},
    {TableId::FieldRVA, 0x1D, "FieldRVA"},
    {TableId::Assembly, 0x20, "Assembly"},
    {TableId::AssemblyProcessor, 0x21, "AssemblyProcessor"},
    {TableId::AssemblyOS, 0x22, "AssemblyOS"},
    {TableId::AssemblyRef, 0x23, "AssemblyRef"},
    {TableId::AssemblyRefProcessor, 0x24, "AssemblyRefProcessor"},
    {TableId::AssemblyRefOS, 0x25, "AssemblyRefOS"},
    {TableId::File, 0x26, "File"},
    {TableId::ExportedType, 0x27, "ExportedType"},
    {TableId::ManifestResource, 0x28, "ManifestResource"},
    {TableId::NestedClass, 0x29, "NestedClass"},
    {TableId::GenericParam, 0x2A, "GenericParam"},
    {TableId::MethodSpec, 0x2B, "MethodSpec"},
    {TableId::GenericParamConstraint, 0x2C, "GenericParamConstraint"},
};

const char *getECMATableName(unsigned Id) {
  for (const ECMATableId &T : ECMATableIds)
    if (T.Value == Id)
      return T.Name;
  return nullptr;
}

TEST(CLIFormatTest, TableIdValues) {
  static_assert(std::is_same_v<std::underlying_type_t<TableId>, uint8_t>);
  for (const ECMATableId &T : ECMATableIds)
    EXPECT_EQ(static_cast<unsigned>(T.Id), T.Value) << T.Name;
}

//===----------------------------------------------------------------------===//
// CLITables.def invariants
//===----------------------------------------------------------------------===//

struct DefTable {
  unsigned Id;
  const char *Name;
  unsigned Sorted;
  unsigned EnumValue;
};

const DefTable DefTables[] = {
#define CLI_TABLE(Id, Name, Sorted)                                            \
  {Id, #Name, Sorted, static_cast<unsigned>(TableId::Name)},
#include "llvm/BinaryFormat/CLITables.def"
};

// The sorted tables of II.22 (each table's "sorted by" rule).
constexpr uint64_t ECMASortedTables =
    (1ULL << 0x09) | (1ULL << 0x0B) | (1ULL << 0x0C) | (1ULL << 0x0D) |
    (1ULL << 0x0E) | (1ULL << 0x0F) | (1ULL << 0x10) | (1ULL << 0x18) |
    (1ULL << 0x19) | (1ULL << 0x1C) | (1ULL << 0x1D) | (1ULL << 0x29) |
    (1ULL << 0x2A) | (1ULL << 0x2C);

TEST(CLIFormatTest, TablesDefInvariants) {
  ASSERT_EQ(std::size(DefTables), 45u) << "table ids 0x00-0x2C";
  StringSet<> Names;
  uint64_t Sorted = 0;
  for (unsigned I = 0; I != std::size(DefTables); ++I) {
    const DefTable &T = DefTables[I];
    SCOPED_TRACE(T.Name);
    EXPECT_EQ(T.Id, I) << "ids are dense and in order";
    EXPECT_EQ(T.EnumValue, T.Id) << "TableId::" << T.Name;
    EXPECT_TRUE(Names.insert(T.Name).second) << "duplicate name";
    EXPECT_LE(T.Sorted, 1u);
    if (T.Sorted)
      Sorted |= 1ULL << T.Id;
    if (const char *ECMA = getECMATableName(T.Id))
      EXPECT_STREQ(T.Name, ECMA);
  }
  EXPECT_EQ(SortedTablesMask, 0x000016003301FA00ULL);
  EXPECT_EQ(Sorted, SortedTablesMask);
  EXPECT_EQ(ECMASortedTables, SortedTablesMask);
}

struct DefColumn {
  const char *Table, *Column, *Kind, *Target;
};

const DefColumn DefColumns[] = {
#define CLI_COLUMN(Table, Column, Kind, Target)                                \
  {#Table, #Column, #Kind, #Target},
#include "llvm/BinaryFormat/CLITables.def"
};

struct DefCodedIndex {
  const char *Name;
  unsigned TagBits;
  const char *Tables;
  unsigned EnumValue;
};

const DefCodedIndex DefCodedIndexes[] = {
#define CLI_CODED_INDEX(Name, TagBits, ...)                                    \
  {#Name, TagBits, #__VA_ARGS__, static_cast<unsigned>(CodedIndexKind::Name)},
#include "llvm/BinaryFormat/CLITables.def"
};

// The coded indexes of II.24.2.6 in their order there; "None" is an unused
// tag. CustomAttributeType's last unused tag (4) may be omitted.
struct ECMACodedIndex {
  const char *Name;
  unsigned TagBits;
  std::vector<StringRef> Tables;
};

const std::vector<ECMACodedIndex> &getECMACodedIndexes() {
  static const std::vector<ECMACodedIndex> List = {
      {"TypeDefOrRef", 2, {"TypeDef", "TypeRef", "TypeSpec"}},
      {"HasConstant", 2, {"Field", "Param", "Property"}},
      {"HasCustomAttribute",
       5,
       {"MethodDef",        "Field",        "TypeRef",
        "TypeDef",          "Param",        "InterfaceImpl",
        "MemberRef",        "Module",       "DeclSecurity",
        "Property",         "Event",        "StandAloneSig",
        "ModuleRef",        "TypeSpec",     "Assembly",
        "AssemblyRef",      "File",         "ExportedType",
        "ManifestResource", "GenericParam", "GenericParamConstraint",
        "MethodSpec"}},
      {"HasFieldMarshal", 1, {"Field", "Param"}},
      {"HasDeclSecurity", 2, {"TypeDef", "MethodDef", "Assembly"}},
      {"MemberRefParent",
       3,
       {"TypeDef", "TypeRef", "ModuleRef", "MethodDef", "TypeSpec"}},
      {"HasSemantics", 1, {"Event", "Property"}},
      {"MethodDefOrRef", 1, {"MethodDef", "MemberRef"}},
      {"MemberForwarded", 1, {"Field", "MethodDef"}},
      {"Implementation", 2, {"File", "AssemblyRef", "ExportedType"}},
      {"CustomAttributeType", 3, {"None", "None", "MethodDef", "MemberRef"}},
      {"ResolutionScope", 2, {"Module", "ModuleRef", "AssemblyRef", "TypeRef"}},
      {"TypeOrMethodDef", 1, {"TypeDef", "MethodDef"}},
  };
  return List;
}

// The last component of a possibly qualified name ("ColumnKind::U16" -> "U16").
StringRef baseName(StringRef S) {
  S = S.trim();
  size_t Colon = S.rfind("::");
  return Colon == StringRef::npos ? S : S.substr(Colon + 2);
}

TEST(CLIFormatTest, ColumnsDefInvariants) {
  StringSet<> TableNames, CodedNames;
  for (const DefTable &T : DefTables)
    TableNames.insert(T.Name);
  for (const DefCodedIndex &C : DefCodedIndexes)
    CodedNames.insert(C.Name);

  // Columns are grouped by table, and every table has at least one.
  StringSet<> Done;
  StringRef Current;
  for (const DefColumn &C : DefColumns) {
    SCOPED_TRACE(std::string(C.Table) + "." + C.Column);
    StringRef Table = baseName(C.Table);
    EXPECT_TRUE(TableNames.contains(Table));
    if (Table != Current) {
      EXPECT_TRUE(Done.insert(Table).second) << "columns of a table split";
      Current = Table;
    }
    EXPECT_FALSE(StringRef(C.Column).trim().empty());
    StringRef Kind = baseName(C.Kind), Target = baseName(C.Target);
    if (Kind == "TableIdx") {
      EXPECT_TRUE(TableNames.contains(Target)) << Target.str();
    } else if (Kind == "CodedIdx") {
      EXPECT_TRUE(CodedNames.contains(Target)) << Target.str();
    } else {
      EXPECT_TRUE(Kind == "U16" || Kind == "U32" || Kind == "StringIdx" ||
                  Kind == "GuidIdx" || Kind == "BlobIdx")
          << Kind.str();
      EXPECT_EQ(Target, "None");
    }
  }
  EXPECT_EQ(Done.size(), std::size(DefTables));
}

TEST(CLIFormatTest, CodedIndexDefInvariants) {
  const std::vector<ECMACodedIndex> &ECMA = getECMACodedIndexes();
  ASSERT_EQ(std::size(DefCodedIndexes), ECMA.size());
  StringSet<> TableNames;
  for (const DefTable &T : DefTables)
    TableNames.insert(T.Name);
  for (unsigned I = 0; I != ECMA.size(); ++I) {
    const DefCodedIndex &D = DefCodedIndexes[I];
    SCOPED_TRACE(D.Name);
    EXPECT_STREQ(D.Name, ECMA[I].Name) << "II.24.2.6 order";
    EXPECT_EQ(D.EnumValue, I) << "CodedIndexKind follows the .def order";
    EXPECT_EQ(D.TagBits, ECMA[I].TagBits);
    SmallVector<StringRef, 24> Tables;
    StringRef(D.Tables).split(Tables, ',');
    for (StringRef &T : Tables)
      T = baseName(T);
    // The tag bits cover the tables.
    EXPECT_LE(Tables.size(), 1u << D.TagBits);
    for (StringRef T : Tables)
      EXPECT_TRUE(T == "None" || TableNames.contains(T)) << T.str();
    ASSERT_GE(Tables.size(), ECMA[I].Tables.size());
    for (unsigned J = 0; J != Tables.size(); ++J) {
      if (J < ECMA[I].Tables.size())
        EXPECT_EQ(Tables[J], ECMA[I].Tables[J]) << "tag " << J;
      else
        EXPECT_EQ(Tables[J], "None") << "tag " << J << " is unused";
    }
  }
}

//===----------------------------------------------------------------------===//
// CILOpcodes.def invariants (III.1.2.1) and entries from System.Reflection
//===----------------------------------------------------------------------===//

struct DefOpcode {
  const char *Enum;
  const char *Text;
  unsigned Value;
  OperandKind Operand;
  StackBehaviour Pop, Push;
  FlowControl Flow;
  unsigned EnumValue;
};

const DefOpcode DefOpcodes[] = {
#define CIL_OPCODE(Enum, Text, Value, Operand, Pop, Push, Flow)                \
  {#Enum,                                                                      \
   Text,                                                                       \
   Value,                                                                      \
   OperandKind::Operand,                                                       \
   StackBehaviour::Pop,                                                        \
   StackBehaviour::Push,                                                       \
   FlowControl::Flow,                                                          \
   static_cast<unsigned>(Opcode::Enum)},
#include "llvm/BinaryFormat/CILOpcodes.def"
};

const DefOpcode *findOpcode(unsigned Value) {
  for (const DefOpcode &O : DefOpcodes)
    if (O.Value == Value)
      return &O;
  return nullptr;
}

// 219 entries in value order: System.Reflection.Emit.OpCodes' 218 opcodes
// (dotnet-opcodes.tsv without the eight internal prefix bytes 0xF8-0xFF: 191
// one-byte and 27 two-byte values) plus ECMA's no. (0xFE19).
TEST(CLIFormatTest, OpcodesDefInvariants) {
  static_assert(std::is_same_v<std::underlying_type_t<Opcode>, uint16_t>);
  ASSERT_EQ(std::size(DefOpcodes), 219u);
  EXPECT_EQ(TwoByteOpcodePrefix, 0xFEu);
  StringSet<> Enums, Texts;
  unsigned OneByte = 0, TwoByte = 0;
  bool FlowSeen[8] = {};
  for (unsigned I = 0; I != std::size(DefOpcodes); ++I) {
    const DefOpcode &O = DefOpcodes[I];
    SCOPED_TRACE(O.Text);
    EXPECT_EQ(O.EnumValue, O.Value) << "Opcode::" << O.Enum;
    EXPECT_TRUE(Enums.insert(O.Enum).second) << "duplicate enumerator";
    EXPECT_TRUE(Texts.insert(O.Text).second) << "duplicate mnemonic";
    EXPECT_FALSE(StringRef(O.Text).empty());
    EXPECT_EQ(StringRef(O.Text).lower(), O.Text) << "mnemonics are lowercase";
    if (I)
      EXPECT_LT(DefOpcodes[I - 1].Value, O.Value) << "value order";
    if (O.Value < 0x100) {
      ++OneByte;
      // The prefix byte and the reserved prefixes 0xF8-0xFF are not opcodes.
      EXPECT_LT(O.Value, 0xF8u);
    } else {
      ++TwoByte;
      EXPECT_EQ(O.Value >> 8, unsigned(TwoByteOpcodePrefix));
      EXPECT_LE(O.Value & 0xFF, 0x1Eu);
    }
    unsigned F = static_cast<unsigned>(O.Flow);
    ASSERT_LT(F, 8u);
    FlowSeen[F] = true;
  }
  EXPECT_EQ(OneByte, 191u);
  EXPECT_EQ(TwoByte, 28u);
  for (bool Seen : FlowSeen)
    EXPECT_TRUE(Seen) << "every FlowControl value is used";

  // no. (III.2.2): FE 19 followed by an unsigned int8 of check flags; a prefix.
  const DefOpcode *No = findOpcode(0xFE19);
  ASSERT_NE(No, nullptr);
  EXPECT_STREQ(No->Text, "no.");
  EXPECT_EQ(No->Operand, OperandKind::ShortInlineI);
  EXPECT_EQ(No->Pop, StackBehaviour::Pop0);
  EXPECT_EQ(No->Push, StackBehaviour::Push0);
  EXPECT_EQ(No->Flow, FlowControl::Meta);

  EXPECT_EQ(static_cast<unsigned>(Opcode::Nop), 0x0000u);
  EXPECT_EQ(static_cast<unsigned>(Opcode::Ldc_i4_s), 0x001Fu);
  EXPECT_EQ(static_cast<unsigned>(Opcode::Readonly), 0xFE1Eu);
}

// Rows of dotnet-opcodes.tsv (System.Reflection.Emit.OpCodes with the
// System.Reflection.Metadata.ILOpCode names) that together use every operand
// kind, flow control and stack behaviour, plus every prefix and the opcodes
// codegen relies on.
TEST(CLIFormatTest, OpcodesMatchSystemReflection) {
  struct {
    const char *Text;
    unsigned Value;
    OperandKind Operand;
    StackBehaviour Pop, Push;
    FlowControl Flow;
    uint16_t Enum;
  } Cases[] = {
      {"nop", 0x0000, OperandKind::InlineNone, StackBehaviour::Pop0,
       StackBehaviour::Push0, FlowControl::Next, uint16_t(Opcode::Nop)},
      {"break", 0x0001, OperandKind::InlineNone, StackBehaviour::Pop0,
       StackBehaviour::Push0, FlowControl::Break, uint16_t(Opcode::Break)},
      {"ldarg.0", 0x0002, OperandKind::InlineNone, StackBehaviour::Pop0,
       StackBehaviour::Push1, FlowControl::Next, uint16_t(Opcode::Ldarg_0)},
      {"ldarg.s", 0x000E, OperandKind::ShortInlineVar, StackBehaviour::Pop0,
       StackBehaviour::Push1, FlowControl::Next, uint16_t(Opcode::Ldarg_s)},
      {"ldc.i4.s", 0x001F, OperandKind::ShortInlineI, StackBehaviour::Pop0,
       StackBehaviour::Pushi, FlowControl::Next, uint16_t(Opcode::Ldc_i4_s)},
      {"ldc.i4", 0x0020, OperandKind::InlineI, StackBehaviour::Pop0,
       StackBehaviour::Pushi, FlowControl::Next, uint16_t(Opcode::Ldc_i4)},
      {"ldc.i8", 0x0021, OperandKind::InlineI8, StackBehaviour::Pop0,
       StackBehaviour::Pushi8, FlowControl::Next, uint16_t(Opcode::Ldc_i8)},
      {"ldc.r4", 0x0022, OperandKind::ShortInlineR, StackBehaviour::Pop0,
       StackBehaviour::Pushr4, FlowControl::Next, uint16_t(Opcode::Ldc_r4)},
      {"ldc.r8", 0x0023, OperandKind::InlineR, StackBehaviour::Pop0,
       StackBehaviour::Pushr8, FlowControl::Next, uint16_t(Opcode::Ldc_r8)},
      {"dup", 0x0025, OperandKind::InlineNone, StackBehaviour::Pop1,
       StackBehaviour::Push1_push1, FlowControl::Next, uint16_t(Opcode::Dup)},
      {"call", 0x0028, OperandKind::InlineMethod, StackBehaviour::Varpop,
       StackBehaviour::Varpush, FlowControl::Call, uint16_t(Opcode::Call)},
      {"calli", 0x0029, OperandKind::InlineSig, StackBehaviour::Varpop,
       StackBehaviour::Varpush, FlowControl::Call, uint16_t(Opcode::Calli)},
      {"ret", 0x002A, OperandKind::InlineNone, StackBehaviour::Varpop,
       StackBehaviour::Push0, FlowControl::Return, uint16_t(Opcode::Ret)},
      {"br.s", 0x002B, OperandKind::ShortInlineBrTarget, StackBehaviour::Pop0,
       StackBehaviour::Push0, FlowControl::Branch, uint16_t(Opcode::Br_s)},
      {"br", 0x0038, OperandKind::InlineBrTarget, StackBehaviour::Pop0,
       StackBehaviour::Push0, FlowControl::Branch, uint16_t(Opcode::Br)},
      {"switch", 0x0045, OperandKind::InlineSwitch, StackBehaviour::Popi,
       StackBehaviour::Push0, FlowControl::Cond_Branch,
       uint16_t(Opcode::Switch)},
      {"stind.i8", 0x0055, OperandKind::InlineNone, StackBehaviour::Popi_popi8,
       StackBehaviour::Push0, FlowControl::Next, uint16_t(Opcode::Stind_i8)},
      {"stind.r4", 0x0056, OperandKind::InlineNone, StackBehaviour::Popi_popr4,
       StackBehaviour::Push0, FlowControl::Next, uint16_t(Opcode::Stind_r4)},
      {"stind.r8", 0x0057, OperandKind::InlineNone, StackBehaviour::Popi_popr8,
       StackBehaviour::Push0, FlowControl::Next, uint16_t(Opcode::Stind_r8)},
      {"cpobj", 0x0070, OperandKind::InlineType, StackBehaviour::Popi_popi,
       StackBehaviour::Push0, FlowControl::Next, uint16_t(Opcode::Cpobj)},
      {"ldstr", 0x0072, OperandKind::InlineString, StackBehaviour::Pop0,
       StackBehaviour::Pushref, FlowControl::Next, uint16_t(Opcode::Ldstr)},
      {"throw", 0x007A, OperandKind::InlineNone, StackBehaviour::Popref,
       StackBehaviour::Push0, FlowControl::Throw, uint16_t(Opcode::Throw)},
      {"stfld", 0x007D, OperandKind::InlineField, StackBehaviour::Popref_pop1,
       StackBehaviour::Push0, FlowControl::Next, uint16_t(Opcode::Stfld)},
      {"ldsfld", 0x007E, OperandKind::InlineField, StackBehaviour::Pop0,
       StackBehaviour::Push1, FlowControl::Next, uint16_t(Opcode::Ldsfld)},
      {"stobj", 0x0081, OperandKind::InlineType, StackBehaviour::Popi_pop1,
       StackBehaviour::Push0, FlowControl::Next, uint16_t(Opcode::Stobj)},
      {"ldelema", 0x008F, OperandKind::InlineType, StackBehaviour::Popref_popi,
       StackBehaviour::Pushi, FlowControl::Next, uint16_t(Opcode::Ldelema)},
      {"stelem.i", 0x009B, OperandKind::InlineNone,
       StackBehaviour::Popref_popi_popi, StackBehaviour::Push0,
       FlowControl::Next, uint16_t(Opcode::Stelem_i)},
      {"stelem.i8", 0x009F, OperandKind::InlineNone,
       StackBehaviour::Popref_popi_popi8, StackBehaviour::Push0,
       FlowControl::Next, uint16_t(Opcode::Stelem_i8)},
      {"stelem.r4", 0x00A0, OperandKind::InlineNone,
       StackBehaviour::Popref_popi_popr4, StackBehaviour::Push0,
       FlowControl::Next, uint16_t(Opcode::Stelem_r4)},
      {"stelem.r8", 0x00A1, OperandKind::InlineNone,
       StackBehaviour::Popref_popi_popr8, StackBehaviour::Push0,
       FlowControl::Next, uint16_t(Opcode::Stelem_r8)},
      {"stelem.ref", 0x00A2, OperandKind::InlineNone,
       StackBehaviour::Popref_popi_popref, StackBehaviour::Push0,
       FlowControl::Next, uint16_t(Opcode::Stelem_ref)},
      {"stelem", 0x00A4, OperandKind::InlineType,
       StackBehaviour::Popref_popi_pop1, StackBehaviour::Push0,
       FlowControl::Next, uint16_t(Opcode::Stelem)},
      {"ldtoken", 0x00D0, OperandKind::InlineTok, StackBehaviour::Pop0,
       StackBehaviour::Pushi, FlowControl::Next, uint16_t(Opcode::Ldtoken)},
      {"conv.u", 0x00E0, OperandKind::InlineNone, StackBehaviour::Pop1,
       StackBehaviour::Pushi, FlowControl::Next, uint16_t(Opcode::Conv_u)},
      {"ceq", 0xFE01, OperandKind::InlineNone, StackBehaviour::Pop1_pop1,
       StackBehaviour::Pushi, FlowControl::Next, uint16_t(Opcode::Ceq)},
      {"ldarg", 0xFE09, OperandKind::InlineVar, StackBehaviour::Pop0,
       StackBehaviour::Push1, FlowControl::Next, uint16_t(Opcode::Ldarg)},
      {"localloc", 0xFE0F, OperandKind::InlineNone, StackBehaviour::Popi,
       StackBehaviour::Pushi, FlowControl::Next, uint16_t(Opcode::Localloc)},
      {"unaligned.", 0xFE12, OperandKind::ShortInlineI, StackBehaviour::Pop0,
       StackBehaviour::Push0, FlowControl::Meta, uint16_t(Opcode::Unaligned)},
      {"volatile.", 0xFE13, OperandKind::InlineNone, StackBehaviour::Pop0,
       StackBehaviour::Push0, FlowControl::Meta, uint16_t(Opcode::Volatile)},
      {"tail.", 0xFE14, OperandKind::InlineNone, StackBehaviour::Pop0,
       StackBehaviour::Push0, FlowControl::Meta, uint16_t(Opcode::Tail)},
      {"cpblk", 0xFE17, OperandKind::InlineNone, StackBehaviour::Popi_popi_popi,
       StackBehaviour::Push0, FlowControl::Next, uint16_t(Opcode::Cpblk)},
      {"initblk", 0xFE18, OperandKind::InlineNone,
       StackBehaviour::Popi_popi_popi, StackBehaviour::Push0, FlowControl::Next,
       uint16_t(Opcode::Initblk)},
      {"readonly.", 0xFE1E, OperandKind::InlineNone, StackBehaviour::Pop0,
       StackBehaviour::Push0, FlowControl::Meta, uint16_t(Opcode::Readonly)},
  };
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.Text);
    EXPECT_EQ(C.Enum, C.Value) << "the ILOpCode name";
    const DefOpcode *O = findOpcode(C.Value);
    ASSERT_NE(O, nullptr);
    EXPECT_STREQ(O->Text, C.Text);
    EXPECT_EQ(O->Operand, C.Operand);
    EXPECT_EQ(O->Pop, C.Pop);
    EXPECT_EQ(O->Push, C.Push);
    EXPECT_EQ(O->Flow, C.Flow);
  }
}

// The enumerators of 5.7.2's OperandKind, in the contract's order.
TEST(CLIFormatTest, OperandKindOrder) {
  const OperandKind Order[] = {OperandKind::InlineNone,
                               OperandKind::ShortInlineVar,
                               OperandKind::InlineVar,
                               OperandKind::ShortInlineI,
                               OperandKind::InlineI,
                               OperandKind::InlineI8,
                               OperandKind::ShortInlineR,
                               OperandKind::InlineR,
                               OperandKind::ShortInlineBrTarget,
                               OperandKind::InlineBrTarget,
                               OperandKind::InlineMethod,
                               OperandKind::InlineField,
                               OperandKind::InlineType,
                               OperandKind::InlineTok,
                               OperandKind::InlineSig,
                               OperandKind::InlineString,
                               OperandKind::InlineSwitch};
  for (unsigned I = 1; I != std::size(Order); ++I)
    EXPECT_LT(static_cast<unsigned>(Order[I - 1]),
              static_cast<unsigned>(Order[I]));
}

//===----------------------------------------------------------------------===//
// Flag enums (II.23.1) against System.Reflection
//===----------------------------------------------------------------------===//

#define CHECK_FLAG(Enum, Name, Value)                                          \
  EXPECT_EQ(static_cast<uint64_t>(Enum::Name), uint64_t(Value))                \
      << #Enum "::" #Name
#define CHECK_ALIAS(Enum, Alias, Primary)                                      \
  EXPECT_EQ(static_cast<uint64_t>(Enum::Alias),                                \
            static_cast<uint64_t>(Enum::Primary))                              \
      << #Enum "::" #Alias

// II.23.1.15; System.Reflection's TypeAttributes.
TEST(CLIFormatTest, TypeAttributesValues) {
  static_assert(
      std::is_same_v<std::underlying_type_t<TypeAttributes>, uint32_t>);
  CHECK_FLAG(TypeAttributes, VisibilityMask, 0x00000007);
  CHECK_FLAG(TypeAttributes, NotPublic, 0x00000000);
  CHECK_FLAG(TypeAttributes, Public, 0x00000001);
  CHECK_FLAG(TypeAttributes, NestedPublic, 0x00000002);
  CHECK_FLAG(TypeAttributes, NestedPrivate, 0x00000003);
  CHECK_FLAG(TypeAttributes, NestedFamily, 0x00000004);
  CHECK_FLAG(TypeAttributes, NestedAssembly, 0x00000005);
  CHECK_FLAG(TypeAttributes, NestedFamANDAssem, 0x00000006);
  CHECK_FLAG(TypeAttributes, NestedFamORAssem, 0x00000007);
  CHECK_FLAG(TypeAttributes, LayoutMask, 0x00000018);
  CHECK_FLAG(TypeAttributes, AutoLayout, 0x00000000);
  CHECK_FLAG(TypeAttributes, SequentialLayout, 0x00000008);
  CHECK_FLAG(TypeAttributes, ExplicitLayout, 0x00000010);
  CHECK_FLAG(TypeAttributes, ClassSemanticsMask, 0x00000020);
  CHECK_FLAG(TypeAttributes, Class, 0x00000000);
  CHECK_FLAG(TypeAttributes, Interface, 0x00000020);
  CHECK_FLAG(TypeAttributes, Abstract, 0x00000080);
  CHECK_FLAG(TypeAttributes, Sealed, 0x00000100);
  CHECK_FLAG(TypeAttributes, SpecialName, 0x00000400);
  CHECK_FLAG(TypeAttributes, Import, 0x00001000);
  CHECK_FLAG(TypeAttributes, Serializable, 0x00002000);
  CHECK_FLAG(TypeAttributes, StringFormatMask, 0x00030000);
  CHECK_FLAG(TypeAttributes, AnsiClass, 0x00000000);
  CHECK_FLAG(TypeAttributes, UnicodeClass, 0x00010000);
  CHECK_FLAG(TypeAttributes, AutoClass, 0x00020000);
  CHECK_FLAG(TypeAttributes, CustomFormatClass, 0x00030000);
  CHECK_FLAG(TypeAttributes, CustomFormatMask, 0x00C00000);
  CHECK_FLAG(TypeAttributes, BeforeFieldInit, 0x00100000);
  CHECK_FLAG(TypeAttributes, RTSpecialName, 0x00000800);
  CHECK_FLAG(TypeAttributes, HasSecurity, 0x00040000);
  CHECK_FLAG(TypeAttributes, Forwarder, 0x00200000); // ECMA only
  CHECK_ALIAS(TypeAttributes, CustomStringFormatMask,
              CustomFormatMask);                           // ECMA spelling
  CHECK_ALIAS(TypeAttributes, IsTypeForwarder, Forwarder); // ECMA spelling
}

// II.23.1.5; System.Reflection's FieldAttributes.
TEST(CLIFormatTest, FieldAttributesValues) {
  static_assert(
      std::is_same_v<std::underlying_type_t<FieldAttributes>, uint16_t>);
  CHECK_FLAG(FieldAttributes, FieldAccessMask, 0x0007);
  CHECK_FLAG(FieldAttributes, PrivateScope, 0x0000);
  CHECK_FLAG(FieldAttributes, Private, 0x0001);
  CHECK_FLAG(FieldAttributes, FamANDAssem, 0x0002);
  CHECK_FLAG(FieldAttributes, Assembly, 0x0003);
  CHECK_FLAG(FieldAttributes, Family, 0x0004);
  CHECK_FLAG(FieldAttributes, FamORAssem, 0x0005);
  CHECK_FLAG(FieldAttributes, Public, 0x0006);
  CHECK_FLAG(FieldAttributes, Static, 0x0010);
  CHECK_FLAG(FieldAttributes, InitOnly, 0x0020);
  CHECK_FLAG(FieldAttributes, Literal, 0x0040);
  CHECK_FLAG(FieldAttributes, NotSerialized, 0x0080);
  CHECK_FLAG(FieldAttributes, SpecialName, 0x0200);
  CHECK_FLAG(FieldAttributes, PinvokeImpl, 0x2000);
  CHECK_FLAG(FieldAttributes, RTSpecialName, 0x0400);
  CHECK_FLAG(FieldAttributes, HasFieldMarshal, 0x1000);
  CHECK_FLAG(FieldAttributes, HasDefault, 0x8000);
  CHECK_FLAG(FieldAttributes, HasFieldRVA, 0x0100);
  CHECK_ALIAS(FieldAttributes, CompilerControlled,
              PrivateScope);                              // ECMA spelling
  CHECK_ALIAS(FieldAttributes, PInvokeImpl, PinvokeImpl); // ECMA spelling
}

// II.23.1.10; System.Reflection's MethodAttributes.
TEST(CLIFormatTest, MethodAttributesValues) {
  static_assert(
      std::is_same_v<std::underlying_type_t<MethodAttributes>, uint16_t>);
  CHECK_FLAG(MethodAttributes, MemberAccessMask, 0x0007);
  CHECK_FLAG(MethodAttributes, PrivateScope, 0x0000);
  CHECK_FLAG(MethodAttributes, Private, 0x0001);
  CHECK_FLAG(MethodAttributes, FamANDAssem, 0x0002);
  CHECK_FLAG(MethodAttributes, Assembly, 0x0003);
  CHECK_FLAG(MethodAttributes, Family, 0x0004);
  CHECK_FLAG(MethodAttributes, FamORAssem, 0x0005);
  CHECK_FLAG(MethodAttributes, Public, 0x0006);
  CHECK_FLAG(MethodAttributes, Static, 0x0010);
  CHECK_FLAG(MethodAttributes, Final, 0x0020);
  CHECK_FLAG(MethodAttributes, Virtual, 0x0040);
  CHECK_FLAG(MethodAttributes, HideBySig, 0x0080);
  CHECK_FLAG(MethodAttributes, VtableLayoutMask, 0x0100);
  CHECK_FLAG(MethodAttributes, ReuseSlot, 0x0000);
  CHECK_FLAG(MethodAttributes, NewSlot, 0x0100);
  CHECK_FLAG(MethodAttributes, CheckAccessOnOverride, 0x0200);
  CHECK_FLAG(MethodAttributes, Abstract, 0x0400);
  CHECK_FLAG(MethodAttributes, SpecialName, 0x0800);
  CHECK_FLAG(MethodAttributes, PinvokeImpl, 0x2000);
  CHECK_FLAG(MethodAttributes, UnmanagedExport, 0x0008);
  CHECK_FLAG(MethodAttributes, RTSpecialName, 0x1000);
  CHECK_FLAG(MethodAttributes, HasSecurity, 0x4000);
  CHECK_FLAG(MethodAttributes, RequireSecObject, 0x8000);
  CHECK_ALIAS(MethodAttributes, CompilerControlled,
              PrivateScope);                                    // ECMA spelling
  CHECK_ALIAS(MethodAttributes, Assem, Assembly);               // ECMA spelling
  CHECK_ALIAS(MethodAttributes, Strict, CheckAccessOnOverride); // ECMA spelling
  CHECK_ALIAS(MethodAttributes, PInvokeImpl, PinvokeImpl);      // ECMA spelling
}

// II.23.1.11; System.Reflection's MethodImplAttributes.
TEST(CLIFormatTest, MethodImplAttributesValues) {
  static_assert(
      std::is_same_v<std::underlying_type_t<MethodImplAttributes>, uint16_t>);
  CHECK_FLAG(MethodImplAttributes, CodeTypeMask, 0x0003);
  CHECK_FLAG(MethodImplAttributes, IL, 0x0000);
  CHECK_FLAG(MethodImplAttributes, Native, 0x0001);
  CHECK_FLAG(MethodImplAttributes, OPTIL, 0x0002);
  CHECK_FLAG(MethodImplAttributes, Runtime, 0x0003);
  CHECK_FLAG(MethodImplAttributes, ManagedMask, 0x0004);
  CHECK_FLAG(MethodImplAttributes, Unmanaged, 0x0004);
  CHECK_FLAG(MethodImplAttributes, Managed, 0x0000);
  CHECK_FLAG(MethodImplAttributes, ForwardRef, 0x0010);
  CHECK_FLAG(MethodImplAttributes, PreserveSig, 0x0080);
  CHECK_FLAG(MethodImplAttributes, InternalCall, 0x1000);
  CHECK_FLAG(MethodImplAttributes, Synchronized, 0x0020);
  CHECK_FLAG(MethodImplAttributes, NoInlining, 0x0008);
  CHECK_FLAG(MethodImplAttributes, MaxMethodImplVal, 0xFFFF);
  CHECK_FLAG(MethodImplAttributes, NoOptimization, 0x0040);
}

// II.23.1.13; System.Reflection's ParameterAttributes.
TEST(CLIFormatTest, ParamAttributesValues) {
  static_assert(
      std::is_same_v<std::underlying_type_t<ParamAttributes>, uint16_t>);
  CHECK_FLAG(ParamAttributes, None, 0x0000);
  CHECK_FLAG(ParamAttributes, In, 0x0001);
  CHECK_FLAG(ParamAttributes, Out, 0x0002);
  CHECK_FLAG(ParamAttributes, Optional, 0x0010);
  CHECK_FLAG(ParamAttributes, HasDefault, 0x1000);
  CHECK_FLAG(ParamAttributes, HasFieldMarshal, 0x2000);
  CHECK_FLAG(ParamAttributes, Unused, 0xCFE0); // ECMA only
}

// II.23.1.14; System.Reflection's PropertyAttributes.
TEST(CLIFormatTest, PropertyAttributesValues) {
  static_assert(
      std::is_same_v<std::underlying_type_t<PropertyAttributes>, uint16_t>);
  CHECK_FLAG(PropertyAttributes, None, 0x0000);
  CHECK_FLAG(PropertyAttributes, SpecialName, 0x0200);
  CHECK_FLAG(PropertyAttributes, RTSpecialName, 0x0400);
  CHECK_FLAG(PropertyAttributes, HasDefault, 0x1000);
  CHECK_FLAG(PropertyAttributes, Unused, 0xE9FF); // ECMA only
}

// II.23.1.12; System.Reflection's MethodSemanticsAttributes.
TEST(CLIFormatTest, MethodSemanticsAttributesValues) {
  static_assert(
      std::is_same_v<std::underlying_type_t<MethodSemanticsAttributes>,
                     uint16_t>);
  CHECK_FLAG(MethodSemanticsAttributes, Setter, 0x0001);
  CHECK_FLAG(MethodSemanticsAttributes, Getter, 0x0002);
  CHECK_FLAG(MethodSemanticsAttributes, Other, 0x0004);
  CHECK_FLAG(MethodSemanticsAttributes, Adder, 0x0008);
  CHECK_FLAG(MethodSemanticsAttributes, Remover, 0x0010);
  CHECK_FLAG(MethodSemanticsAttributes, Raiser, 0x0020);
  CHECK_ALIAS(MethodSemanticsAttributes, AddOn, Adder);      // ECMA spelling
  CHECK_ALIAS(MethodSemanticsAttributes, RemoveOn, Remover); // ECMA spelling
  CHECK_ALIAS(MethodSemanticsAttributes, Fire, Raiser);      // ECMA spelling
}

// II.23.1.8; System.Reflection's MethodImportAttributes.
TEST(CLIFormatTest, PInvokeAttributesValues) {
  static_assert(
      std::is_same_v<std::underlying_type_t<PInvokeAttributes>, uint16_t>);
  CHECK_FLAG(PInvokeAttributes, NoMangle,
             0x0001); // MethodImportAttributes.ExactSpelling
  CHECK_FLAG(PInvokeAttributes, CharSetMask, 0x0006);
  CHECK_FLAG(PInvokeAttributes, CharSetNotSpec,
             0x0000); // MethodImportAttributes.None
  CHECK_FLAG(PInvokeAttributes, CharSetAnsi, 0x0002);
  CHECK_FLAG(PInvokeAttributes, CharSetUnicode, 0x0004);
  CHECK_FLAG(PInvokeAttributes, CharSetAuto, 0x0006);
  CHECK_FLAG(PInvokeAttributes, SupportsLastError,
             0x0040); // MethodImportAttributes.SetLastError
  CHECK_FLAG(PInvokeAttributes, CallConvMask,
             0x0700); // MethodImportAttributes.CallingConventionMask
  CHECK_FLAG(PInvokeAttributes, CallConvPlatformapi,
             0x0100); // MethodImportAttributes.CallingConventionWinApi
  CHECK_FLAG(PInvokeAttributes, CallConvWinapi,
             0x0100); // MethodImportAttributes.CallingConventionWinApi
  CHECK_ALIAS(PInvokeAttributes, CallConvWinapi,
              CallConvPlatformapi); // ilasm's 'winapi'
  CHECK_FLAG(PInvokeAttributes, CallConvCdecl,
             0x0200); // MethodImportAttributes.CallingConventionCDecl
  CHECK_FLAG(PInvokeAttributes, CallConvStdcall,
             0x0300); // MethodImportAttributes.CallingConventionStdCall
  CHECK_FLAG(PInvokeAttributes, CallConvThiscall,
             0x0400); // MethodImportAttributes.CallingConventionThisCall
  CHECK_FLAG(PInvokeAttributes, CallConvFastcall,
             0x0500); // MethodImportAttributes.CallingConventionFastCall
}

// II.23.1.7; System.Reflection's GenericParameterAttributes.
TEST(CLIFormatTest, GenericParamAttributesValues) {
  static_assert(
      std::is_same_v<std::underlying_type_t<GenericParamAttributes>, uint16_t>);
  CHECK_FLAG(GenericParamAttributes, VarianceMask, 0x0003);
  CHECK_FLAG(GenericParamAttributes, None, 0x0000);
  CHECK_FLAG(GenericParamAttributes, Covariant, 0x0001);
  CHECK_FLAG(GenericParamAttributes, Contravariant, 0x0002);
  CHECK_FLAG(GenericParamAttributes, SpecialConstraintMask, 0x001C);
  CHECK_FLAG(GenericParamAttributes, ReferenceTypeConstraint, 0x0004);
  CHECK_FLAG(GenericParamAttributes, NotNullableValueTypeConstraint, 0x0008);
  CHECK_FLAG(GenericParamAttributes, DefaultConstructorConstraint, 0x0010);
}

// II.23.1.2; System.Reflection's AssemblyFlags.
TEST(CLIFormatTest, AssemblyFlagsValues) {
  static_assert(
      std::is_same_v<std::underlying_type_t<AssemblyFlags>, uint32_t>);
  CHECK_FLAG(AssemblyFlags, PublicKey, 0x00000001);
  CHECK_FLAG(AssemblyFlags, Retargetable, 0x00000100);
  CHECK_FLAG(AssemblyFlags, DisableJitCompileOptimizer, 0x00004000);
  CHECK_FLAG(AssemblyFlags, EnableJitCompileTracking, 0x00008000);
  CHECK_ALIAS(AssemblyFlags, DisableJITcompileOptimizer,
              DisableJitCompileOptimizer); // ECMA spelling
  CHECK_ALIAS(AssemblyFlags, EnableJITcompileTracking,
              EnableJitCompileTracking); // ECMA spelling
}

// II.23.1.9; System.Reflection's ManifestResourceAttributes.
TEST(CLIFormatTest, ManifestResourceAttributesValues) {
  static_assert(
      std::is_same_v<std::underlying_type_t<ManifestResourceAttributes>,
                     uint32_t>);
  CHECK_FLAG(ManifestResourceAttributes, VisibilityMask, 0x00000007);
  CHECK_FLAG(ManifestResourceAttributes, Public, 0x00000001);
  CHECK_FLAG(ManifestResourceAttributes, Private, 0x00000002);
}

// II.23.1.6; ECMA-335 only: no System.Reflection enum.
TEST(CLIFormatTest, FileAttributesValues) {
  static_assert(
      std::is_same_v<std::underlying_type_t<FileAttributes>, uint32_t>);
  CHECK_FLAG(FileAttributes, ContainsMetaData, 0x00000000);   // ECMA only
  CHECK_FLAG(FileAttributes, ContainsNoMetaData, 0x00000001); // ECMA only
}

// II.23.1.1; System.Reflection's AssemblyHashAlgorithm.
TEST(CLIFormatTest, AssemblyHashAlgorithmValues) {
  static_assert(
      std::is_same_v<std::underlying_type_t<AssemblyHashAlgorithm>, uint32_t>);
  CHECK_FLAG(AssemblyHashAlgorithm, None, 0x00000000);
  CHECK_FLAG(AssemblyHashAlgorithm, MD5, 0x00008003);
  CHECK_FLAG(AssemblyHashAlgorithm, SHA1, 0x00008004);
}

// The flag enums are LLVM bitmask enums, and any value a file can hold in the
// column (2 or 4 bytes) is usable with them: reading metadata never asserts
// on input data (plan 5.7.12).
TEST(CLIFormatTest, FlagEnumsAreBitmasks) {
  TypeAttributes T = TypeAttributes::Public | TypeAttributes::Sealed |
                     TypeAttributes::BeforeFieldInit;
  EXPECT_EQ(static_cast<uint32_t>(T), 0x00100101u);
  EXPECT_EQ(T & TypeAttributes::VisibilityMask, TypeAttributes::Public);
  MethodAttributes M = MethodAttributes::Public | MethodAttributes::Static |
                       MethodAttributes::HideBySig;
  EXPECT_EQ(static_cast<uint16_t>(M), 0x0096u);
  M &= ~MethodAttributes::HideBySig;
  EXPECT_EQ(static_cast<uint16_t>(M), 0x0016u);
  // D14 / 5.10.11: every P/Invoke is 'nomangle cdecl' (ImplMap flags 0x0201).
  PInvokeAttributes P =
      PInvokeAttributes::NoMangle | PInvokeAttributes::CallConvCdecl;
  EXPECT_EQ(static_cast<uint16_t>(P), 0x0201u);
  EXPECT_EQ(P & PInvokeAttributes::CallConvMask,
            PInvokeAttributes::CallConvCdecl);

  // All-ones column values from a file.
  EXPECT_EQ(static_cast<uint32_t>(static_cast<TypeAttributes>(0xFFFFFFFFu) &
                                  TypeAttributes::LayoutMask),
            0x18u);
  EXPECT_EQ(static_cast<uint32_t>(static_cast<AssemblyFlags>(0xFFFFFFFFu) &
                                  AssemblyFlags::PublicKey),
            0x1u);
  EXPECT_EQ(static_cast<uint32_t>(
                static_cast<ManifestResourceAttributes>(0xFFFFFFFFu) &
                ManifestResourceAttributes::VisibilityMask),
            0x7u);
  EXPECT_EQ(static_cast<uint32_t>(static_cast<FileAttributes>(0xFFFFFFFFu) &
                                  FileAttributes::ContainsNoMetaData),
            0x1u);
  EXPECT_EQ(static_cast<uint16_t>(static_cast<FieldAttributes>(0xFFFF) &
                                  FieldAttributes::FieldAccessMask),
            0x7u);
  EXPECT_EQ(static_cast<uint16_t>(static_cast<MethodAttributes>(0xFFFF) &
                                  MethodAttributes::MemberAccessMask),
            0x7u);
  EXPECT_EQ(static_cast<uint16_t>(static_cast<MethodImplAttributes>(0xFFFF) &
                                  MethodImplAttributes::CodeTypeMask),
            0x3u);
  EXPECT_EQ(static_cast<uint16_t>(static_cast<ParamAttributes>(0xFFFF) &
                                  ParamAttributes::Out),
            0x2u);
  EXPECT_EQ(static_cast<uint16_t>(static_cast<PropertyAttributes>(0xFFFF) &
                                  PropertyAttributes::HasDefault),
            0x1000u);
  EXPECT_EQ(
      static_cast<uint16_t>(static_cast<MethodSemanticsAttributes>(0xFFFF) &
                            MethodSemanticsAttributes::Getter),
      0x2u);
  EXPECT_EQ(static_cast<uint16_t>(static_cast<PInvokeAttributes>(0xFFFF) &
                                  PInvokeAttributes::CharSetMask),
            0x6u);
  EXPECT_EQ(static_cast<uint16_t>(static_cast<GenericParamAttributes>(0xFFFF) &
                                  GenericParamAttributes::VarianceMask),
            0x3u);
}

//===----------------------------------------------------------------------===//
// Signatures (II.23.1.16, II.23.2)
//===----------------------------------------------------------------------===//

// II.23.1.16; System.Reflection.Metadata's SignatureTypeCode has the same
// values where it names them (Void 0x01 ... Pinned 0x45).
TEST(CLIFormatTest, ElementTypeValues) {
  struct {
    unsigned Value, Expected;
  } Cases[] = {
      {ELEMENT_TYPE_END, 0x00},        {ELEMENT_TYPE_VOID, 0x01},
      {ELEMENT_TYPE_BOOLEAN, 0x02},    {ELEMENT_TYPE_CHAR, 0x03},
      {ELEMENT_TYPE_I1, 0x04},         {ELEMENT_TYPE_U1, 0x05},
      {ELEMENT_TYPE_I2, 0x06},         {ELEMENT_TYPE_U2, 0x07},
      {ELEMENT_TYPE_I4, 0x08},         {ELEMENT_TYPE_U4, 0x09},
      {ELEMENT_TYPE_I8, 0x0A},         {ELEMENT_TYPE_U8, 0x0B},
      {ELEMENT_TYPE_R4, 0x0C},         {ELEMENT_TYPE_R8, 0x0D},
      {ELEMENT_TYPE_STRING, 0x0E},     {ELEMENT_TYPE_PTR, 0x0F},
      {ELEMENT_TYPE_BYREF, 0x10},      {ELEMENT_TYPE_VALUETYPE, 0x11},
      {ELEMENT_TYPE_CLASS, 0x12},      {ELEMENT_TYPE_VAR, 0x13},
      {ELEMENT_TYPE_ARRAY, 0x14},      {ELEMENT_TYPE_GENERICINST, 0x15},
      {ELEMENT_TYPE_TYPEDBYREF, 0x16}, {ELEMENT_TYPE_I, 0x18},
      {ELEMENT_TYPE_U, 0x19},          {ELEMENT_TYPE_FNPTR, 0x1B},
      {ELEMENT_TYPE_OBJECT, 0x1C},     {ELEMENT_TYPE_SZARRAY, 0x1D},
      {ELEMENT_TYPE_MVAR, 0x1E},       {ELEMENT_TYPE_CMOD_REQD, 0x1F},
      {ELEMENT_TYPE_CMOD_OPT, 0x20},   {ELEMENT_TYPE_INTERNAL, 0x21},
      {ELEMENT_TYPE_MODIFIER, 0x40},   {ELEMENT_TYPE_SENTINEL, 0x41},
      {ELEMENT_TYPE_PINNED, 0x45},
  };
  for (const auto &C : Cases)
    EXPECT_EQ(C.Value, C.Expected);
  static_assert(std::is_same_v<std::underlying_type_t<ElementType>, uint8_t>);
}

// II.23.1.16's custom-attribute codes (System.Reflection.Metadata:
// SerializationTypeCode.Type 0x50, TaggedObject 0x51, Enum 0x55;
// CustomAttributeNamedArgumentKind.Field 0x53, Property 0x54) and the
// signature header bytes of II.23.2.1-II.23.2.15 (SignatureCallingConvention,
// SignatureKind and SignatureAttributes).
TEST(CLIFormatTest, SignatureHeaderValues) {
  EXPECT_EQ(unsigned(SERIALIZATION_TYPE_TYPE), 0x50u);
  EXPECT_EQ(unsigned(SERIALIZATION_TYPE_TAGGED_OBJECT), 0x51u);
  EXPECT_EQ(unsigned(SERIALIZATION_TYPE_FIELD), 0x53u);
  EXPECT_EQ(unsigned(SERIALIZATION_TYPE_PROPERTY), 0x54u);
  EXPECT_EQ(unsigned(SERIALIZATION_TYPE_ENUM), 0x55u);

  EXPECT_EQ(unsigned(SIG_DEFAULT), 0x00u);
  EXPECT_EQ(unsigned(SIG_C), 0x01u);
  EXPECT_EQ(unsigned(SIG_STDCALL), 0x02u);
  EXPECT_EQ(unsigned(SIG_THISCALL), 0x03u);
  EXPECT_EQ(unsigned(SIG_FASTCALL), 0x04u);
  EXPECT_EQ(unsigned(SIG_VARARG), 0x05u);
  EXPECT_EQ(unsigned(SIG_FIELD), 0x06u);
  EXPECT_EQ(unsigned(SIG_LOCAL), 0x07u);
  EXPECT_EQ(unsigned(SIG_PROPERTY), 0x08u);
  EXPECT_EQ(unsigned(SIG_UNMANAGED), 0x09u);
  EXPECT_EQ(unsigned(SIG_GENERICINST), 0x0Au);
  EXPECT_EQ(unsigned(SIG_KIND_MASK), 0x0Fu);
  EXPECT_EQ(unsigned(SIG_GENERIC), 0x10u);
  EXPECT_EQ(unsigned(SIG_HASTHIS), 0x20u);
  EXPECT_EQ(unsigned(SIG_EXPLICITTHIS), 0x40u);
  // Every kind fits the kind mask, and the attribute bits lie outside it.
  for (unsigned K :
       {unsigned(SIG_DEFAULT), unsigned(SIG_VARARG), unsigned(SIG_FIELD),
        unsigned(SIG_LOCAL), unsigned(SIG_PROPERTY), unsigned(SIG_UNMANAGED),
        unsigned(SIG_GENERICINST)})
    EXPECT_EQ(K & ~unsigned(SIG_KIND_MASK), 0u);
  EXPECT_EQ((unsigned(SIG_GENERIC) | unsigned(SIG_HASTHIS) |
             unsigned(SIG_EXPLICITTHIS)) &
                unsigned(SIG_KIND_MASK),
            0u);
}

//===----------------------------------------------------------------------===//
// Image, metadata root and method bodies (II.24, II.25)
//===----------------------------------------------------------------------===//

// II.25.3.3.1 (System.Reflection.PortableExecutable.CorFlags: ILOnly 0x1,
// Requires32Bit 0x2, ILLibrary 0x4, StrongNameSigned 0x8, NativeEntryPoint
// 0x10, TrackDebugData 0x10000, Prefers32Bit 0x20000) and the debug directory
// types of DebugDirectoryEntryType (EmbeddedPortablePdb 17, PdbChecksum 19).
TEST(CLIFormatTest, ImageConstants) {
  EXPECT_EQ(uint32_t(COMIMAGE_FLAGS_ILONLY), 0x1u);
  EXPECT_EQ(uint32_t(COMIMAGE_FLAGS_32BITREQUIRED), 0x2u);
  EXPECT_EQ(uint32_t(COMIMAGE_FLAGS_IL_LIBRARY), 0x4u);
  EXPECT_EQ(uint32_t(COMIMAGE_FLAGS_STRONGNAMESIGNED), 0x8u);
  EXPECT_EQ(uint32_t(COMIMAGE_FLAGS_NATIVE_ENTRYPOINT), 0x10u);
  EXPECT_EQ(uint32_t(COMIMAGE_FLAGS_TRACKDEBUGDATA), 0x10000u);
  EXPECT_EQ(uint32_t(COMIMAGE_FLAGS_32BITPREFERRED), 0x20000u);
  EXPECT_EQ(DebugTypeEmbeddedPortablePdb, 17u);
  EXPECT_EQ(DebugTypePdbChecksum, 19u);
}

// II.25.3.3: the 72-byte CLI header, little-endian fields at these offsets.
TEST(CLIFormatTest, Cor20HeaderLayout) {
  EXPECT_EQ(sizeof(CLIDataDirectory), 8u);
  EXPECT_EQ(offsetof(CLIDataDirectory, RVA), 0u);
  EXPECT_EQ(offsetof(CLIDataDirectory, Size), 4u);
  EXPECT_EQ(sizeof(Cor20Header), 72u);
  EXPECT_EQ(offsetof(Cor20Header, Cb), 0u);
  EXPECT_EQ(sizeof(Cor20Header::Cb), 4u);
  EXPECT_EQ(offsetof(Cor20Header, MajorRuntimeVersion), 4u);
  EXPECT_EQ(sizeof(Cor20Header::MajorRuntimeVersion), 2u);
  EXPECT_EQ(offsetof(Cor20Header, MinorRuntimeVersion), 6u);
  EXPECT_EQ(sizeof(Cor20Header::MinorRuntimeVersion), 2u);
  EXPECT_EQ(offsetof(Cor20Header, MetaData), 8u);
  EXPECT_EQ(offsetof(Cor20Header, Flags), 16u);
  EXPECT_EQ(sizeof(Cor20Header::Flags), 4u);
  EXPECT_EQ(offsetof(Cor20Header, EntryPointToken), 20u);
  EXPECT_EQ(sizeof(Cor20Header::EntryPointToken), 4u);
  EXPECT_EQ(offsetof(Cor20Header, Resources), 24u);
  EXPECT_EQ(offsetof(Cor20Header, StrongNameSignature), 32u);
  EXPECT_EQ(offsetof(Cor20Header, CodeManagerTable), 40u);
  EXPECT_EQ(offsetof(Cor20Header, VTableFixups), 48u);
  EXPECT_EQ(offsetof(Cor20Header, ExportAddressTableJumps), 56u);
  EXPECT_EQ(offsetof(Cor20Header, ManagedNativeHeader), 64u);

  Cor20Header H;
  std::memset(&H, 0, sizeof(H));
  H.Cb = 72;
  H.MajorRuntimeVersion = 2;
  H.MinorRuntimeVersion = 5;
  H.MetaData.RVA = 0x2050;
  H.MetaData.Size = 0x1234;
  H.Flags = COMIMAGE_FLAGS_ILONLY;
  H.EntryPointToken = 0x06000001;
  uint8_t Raw[72];
  std::memcpy(Raw, &H, sizeof(Raw));
  const uint8_t Expected[24] = {0x48, 0x00, 0x00, 0x00, 0x02, 0x00, 0x05, 0x00,
                                0x50, 0x20, 0x00, 0x00, 0x34, 0x12, 0x00, 0x00,
                                0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x06};
  EXPECT_EQ(ArrayRef<uint8_t>(Raw, 24), ArrayRef<uint8_t>(Expected));
}

// II.24.2.1 and II.24.2.6, with the version string every CLR writes.
TEST(CLIFormatTest, MetadataRootConstants) {
  EXPECT_EQ(MetadataSignature, 0x424A5342u);
  uint8_t Sig[4];
  for (unsigned I = 0; I != 4; ++I)
    Sig[I] = static_cast<uint8_t>(MetadataSignature >> (8 * I));
  EXPECT_EQ(StringRef(reinterpret_cast<const char *>(Sig), 4), "BSJB");
  EXPECT_EQ(MetadataMajorVersion, 1u);
  EXPECT_EQ(MetadataMinorVersion, 1u);
  EXPECT_EQ(MetadataVersionString, "v4.0.30319");
  EXPECT_EQ(TablesStreamMajorVersion, 2u);
  EXPECT_EQ(TablesStreamMinorVersion, 0u);
  EXPECT_EQ(unsigned(HEAP_STRINGS_4), 0x01u);
  EXPECT_EQ(unsigned(HEAP_GUID_4), 0x02u);
  EXPECT_EQ(unsigned(HEAP_BLOB_4), 0x04u);
  EXPECT_EQ(unsigned(HEAP_EXTRA_DATA), 0x40u);
}

// II.25.4.1-II.25.4.6 (System.Reflection.Metadata.ExceptionRegionKind: Catch
// 0, Filter 1, Finally 2, Fault 4).
TEST(CLIFormatTest, MethodBodyConstants) {
  EXPECT_EQ(unsigned(MethodHeaderTinyFormat), 0x2u);
  EXPECT_EQ(unsigned(MethodHeaderFatFormat), 0x3u);
  EXPECT_EQ(unsigned(MethodHeaderMoreSects), 0x8u);
  EXPECT_EQ(unsigned(MethodHeaderInitLocals), 0x10u);
  // A fat header is three 4-byte words; a tiny one holds 6 bits of code size
  // and implies maxstack 8.
  EXPECT_EQ(FatHeaderSize, 12u);
  EXPECT_EQ(TinyMaxCodeSize, 63u);
  EXPECT_EQ(TinyMaxStack, 8u);
  EXPECT_EQ(unsigned(EHSectionEHTable), 0x1u);
  EXPECT_EQ(unsigned(EHSectionOptILTable), 0x2u);
  EXPECT_EQ(unsigned(EHSectionFatFormat), 0x40u);
  EXPECT_EQ(unsigned(EHSectionMoreSects), 0x80u);
  static_assert(
      std::is_same_v<std::underlying_type_t<ExceptionClauseKind>, uint32_t>);
  EXPECT_EQ(uint32_t(ExceptionClauseKind::Catch), 0u);
  EXPECT_EQ(uint32_t(ExceptionClauseKind::Filter), 1u);
  EXPECT_EQ(uint32_t(ExceptionClauseKind::Finally), 2u);
  EXPECT_EQ(uint32_t(ExceptionClauseKind::Fault), 4u);
}

// 5.7.2's limits (5.7.12; metadata-writer 3.6).
TEST(CLIFormatTest, Limits) {
  EXPECT_EQ(MaxIdentifierBytes, 1023u);
  EXPECT_EQ(MaxMethodsPerType, 65524u);
  EXPECT_EQ(MaxFieldsPerType, 65534u);
  EXPECT_EQ(MaxTableRows, 0x00FFFFFFu);
  EXPECT_EQ(MaxUserStringHeapSize, 0x00FFFFFFu);
  // A token row and an ldstr offset have 24 bits.
  EXPECT_EQ(getTokenRow(makeToken(TableId::MethodDef, MaxTableRows)),
            MaxTableRows);
}

TEST(CLIFormatTest, AssemblyVersion) {
  AssemblyVersion Zero;
  EXPECT_EQ(Zero.Major, 0u);
  EXPECT_EQ(Zero.Minor, 0u);
  EXPECT_EQ(Zero.Build, 0u);
  EXPECT_EQ(Zero.Revision, 0u);
  static_assert(std::is_same_v<decltype(AssemblyVersion::Major), uint16_t>);
  static_assert(std::is_same_v<decltype(AssemblyVersion::Revision), uint16_t>);
  AssemblyVersion A{4, 0, 0, 0}, B{4, 0, 0, 0}, C{4, 0, 0, 1};
  EXPECT_TRUE(A == B);
  EXPECT_FALSE(A == C);
  EXPECT_FALSE(A == Zero);
  AssemblyVersion D{4, 1, 0, 0}, E{4, 0, 1, 0};
  EXPECT_FALSE(A == D);
  EXPECT_FALSE(A == E);
}

//===----------------------------------------------------------------------===//
// Handles.h
//===----------------------------------------------------------------------===//

TEST(CLIFormatTest, EntityHandle) {
  EntityHandle Nil;
  EXPECT_EQ(Nil.Value, 0u);
  EXPECT_TRUE(Nil.isNil());

  EntityHandle M;
  M.Value = makeToken(TableId::MethodDef, 3);
  EXPECT_EQ(M.getTable(), TableId::MethodDef);
  EXPECT_EQ(M.getIndex(), 3u);
  EXPECT_FALSE(M.isNil());

  // Index 0 is nil whatever the table.
  EntityHandle FieldNil;
  FieldNil.Value = makeToken(TableId::Field, 0);
  EXPECT_EQ(FieldNil.getTable(), TableId::Field);
  EXPECT_EQ(FieldNil.getIndex(), 0u);
  EXPECT_TRUE(FieldNil.isNil());

  EntityHandle Big;
  Big.Value = makeToken(TableId::TypeRef, 0x00FFFFFF);
  EXPECT_EQ(Big.getTable(), TableId::TypeRef);
  EXPECT_EQ(Big.getIndex(), 0x00FFFFFFu);

  EntityHandle M2;
  M2.Value = 0x06000003;
  EXPECT_TRUE(M == M2);
  EXPECT_FALSE(M != M2);
  EXPECT_TRUE(M != Big);
  EXPECT_FALSE(M == Big);
}

TEST(CLIFormatTest, TokenRef) {
  TokenRef T = TokenRef::getToken(0x0A000001);
  EXPECT_EQ(T.getKind(), TokenRef::Kind::Token);
  EXPECT_EQ(T.getValue(), 0x0A000001u);

  EntityHandle H;
  H.Value = makeToken(TableId::Field, 9);
  TokenRef R = TokenRef::getHandle(H);
  EXPECT_EQ(R.getKind(), TokenRef::Kind::Handle);
  EXPECT_EQ(R.getValue(), H.Value);

  TokenRef X = TokenRef::getExternal(42);
  EXPECT_EQ(X.getKind(), TokenRef::Kind::External);
  EXPECT_EQ(X.getValue(), 42u);

  EXPECT_NE(static_cast<unsigned>(TokenRef::Kind::None),
            static_cast<unsigned>(TokenRef::Kind::Token));
}

} // namespace
