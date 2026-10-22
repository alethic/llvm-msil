//===- TableSchemaTest.cpp - ECMA-335 table schema and index widths -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/TableSchema.h against ECMA-335 II.22 (columns), II.24.2.6
// (coded indexes and the index width rules) and System.Reflection.Metadata.
// The schema below was transcribed from ECMA-335 for this test. The row
// counts, row sizes and table offsets of the real assemblies were read with
// System.Reflection.Metadata (MetadataReader.GetTableRowCount, GetTableRowSize
// and GetTableMetadataOffset, .NET 10.0.13) in scratch and embedded, after
// the method of llvm/docs/MSIL/recon/p2/metadata-writer-files/py/
// verify_schema.py; the stream sizes come from the stream headers.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/TableSchema.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

constexpr unsigned NumSchemaTables = 0x2D; // TableId 0x00-0x2C

struct ExpectedColumn {
  ColumnKind Kind;
  uint8_t Target; // TableId or CodedIndexKind for index columns
};

constexpr ExpectedColumn ColU16{ColumnKind::U16, 0};
constexpr ExpectedColumn ColU32{ColumnKind::U32, 0};
constexpr ExpectedColumn ColStr{ColumnKind::StringIdx, 0};
constexpr ExpectedColumn ColGuid{ColumnKind::GuidIdx, 0};
constexpr ExpectedColumn ColBlob{ColumnKind::BlobIdx, 0};
constexpr ExpectedColumn idx(TableId T) {
  return {ColumnKind::TableIdx, static_cast<uint8_t>(T)};
}
constexpr ExpectedColumn coded(CodedIndexKind K) {
  return {ColumnKind::CodedIdx, static_cast<uint8_t>(K)};
}

struct ECMATable {
  TableId Id;
  const char *Name;
  bool Sorted;
  std::vector<ExpectedColumn> Columns;
};

// II.22's tables in id order, each with its columns in II.22 order. The
// pre-standard tables (FieldPtr, MethodPtr, ParamPtr, EventPtr, PropertyPtr,
// ENCLog, ENCMap) are named by number: II.22 does not define them, but
// II.24.2.6 readers size them.
const std::vector<ECMATable> &getECMASchema() {
  static const std::vector<ECMATable> Schema = {
      {TableId::Module,
       "Module",
       false,
       {ColU16, ColStr, ColGuid, ColGuid, ColGuid}},
      {TableId::TypeRef,
       "TypeRef",
       false,
       {coded(CodedIndexKind::ResolutionScope), ColStr, ColStr}},
      {TableId::TypeDef,
       "TypeDef",
       false,
       {ColU32, ColStr, ColStr, coded(CodedIndexKind::TypeDefOrRef),
        idx(TableId::Field), idx(TableId::MethodDef)}},
      {TableId(0x03), "FieldPtr", false, {idx(TableId::Field)}},
      {TableId::Field, "Field", false, {ColU16, ColStr, ColBlob}},
      {TableId(0x05), "MethodPtr", false, {idx(TableId::MethodDef)}},
      {TableId::MethodDef,
       "MethodDef",
       false,
       {ColU32, ColU16, ColU16, ColStr, ColBlob, idx(TableId::Param)}},
      {TableId(0x07), "ParamPtr", false, {idx(TableId::Param)}},
      {TableId::Param, "Param", false, {ColU16, ColU16, ColStr}},
      {TableId::InterfaceImpl,
       "InterfaceImpl",
       true,
       {idx(TableId::TypeDef), coded(CodedIndexKind::TypeDefOrRef)}},
      {TableId::MemberRef,
       "MemberRef",
       false,
       {coded(CodedIndexKind::MemberRefParent), ColStr, ColBlob}},
      {TableId::Constant,
       "Constant",
       true,
       {ColU16, coded(CodedIndexKind::HasConstant), ColBlob}},
      {TableId::CustomAttribute,
       "CustomAttribute",
       true,
       {coded(CodedIndexKind::HasCustomAttribute),
        coded(CodedIndexKind::CustomAttributeType), ColBlob}},
      {TableId::FieldMarshal,
       "FieldMarshal",
       true,
       {coded(CodedIndexKind::HasFieldMarshal), ColBlob}},
      {TableId::DeclSecurity,
       "DeclSecurity",
       true,
       {ColU16, coded(CodedIndexKind::HasDeclSecurity), ColBlob}},
      {TableId::ClassLayout,
       "ClassLayout",
       true,
       {ColU16, ColU32, idx(TableId::TypeDef)}},
      {TableId::FieldLayout,
       "FieldLayout",
       true,
       {ColU32, idx(TableId::Field)}},
      {TableId::StandAloneSig, "StandAloneSig", false, {ColBlob}},
      {TableId::EventMap,
       "EventMap",
       false,
       {idx(TableId::TypeDef), idx(TableId::Event)}},
      {TableId(0x13), "EventPtr", false, {idx(TableId::Event)}},
      {TableId::Event,
       "Event",
       false,
       {ColU16, ColStr, coded(CodedIndexKind::TypeDefOrRef)}},
      {TableId::PropertyMap,
       "PropertyMap",
       false,
       {idx(TableId::TypeDef), idx(TableId::Property)}},
      {TableId(0x16), "PropertyPtr", false, {idx(TableId::Property)}},
      {TableId::Property, "Property", false, {ColU16, ColStr, ColBlob}},
      {TableId::MethodSemantics,
       "MethodSemantics",
       true,
       {ColU16, idx(TableId::MethodDef), coded(CodedIndexKind::HasSemantics)}},
      {TableId::MethodImpl,
       "MethodImpl",
       true,
       {idx(TableId::TypeDef), coded(CodedIndexKind::MethodDefOrRef),
        coded(CodedIndexKind::MethodDefOrRef)}},
      {TableId::ModuleRef, "ModuleRef", false, {ColStr}},
      {TableId::TypeSpec, "TypeSpec", false, {ColBlob}},
      {TableId::ImplMap,
       "ImplMap",
       true,
       {ColU16, coded(CodedIndexKind::MemberForwarded), ColStr,
        idx(TableId::ModuleRef)}},
      {TableId::FieldRVA, "FieldRVA", true, {ColU32, idx(TableId::Field)}},
      {TableId(0x1E), "ENCLog", false, {ColU32, ColU32}},
      {TableId(0x1F), "ENCMap", false, {ColU32}},
      {TableId::Assembly,
       "Assembly",
       false,
       {ColU32, ColU16, ColU16, ColU16, ColU16, ColU32, ColBlob, ColStr,
        ColStr}},
      {TableId::AssemblyProcessor, "AssemblyProcessor", false, {ColU32}},
      {TableId::AssemblyOS, "AssemblyOS", false, {ColU32, ColU32, ColU32}},
      {TableId::AssemblyRef,
       "AssemblyRef",
       false,
       {ColU16, ColU16, ColU16, ColU16, ColU32, ColBlob, ColStr, ColStr,
        ColBlob}},
      {TableId::AssemblyRefProcessor,
       "AssemblyRefProcessor",
       false,
       {ColU32, idx(TableId::AssemblyRef)}},
      {TableId::AssemblyRefOS,
       "AssemblyRefOS",
       false,
       {ColU32, ColU32, ColU32, idx(TableId::AssemblyRef)}},
      {TableId::File, "File", false, {ColU32, ColStr, ColBlob}},
      {TableId::ExportedType,
       "ExportedType",
       false,
       {ColU32, ColU32, ColStr, ColStr, coded(CodedIndexKind::Implementation)}},
      {TableId::ManifestResource,
       "ManifestResource",
       false,
       {ColU32, ColU32, ColStr, coded(CodedIndexKind::Implementation)}},
      {TableId::NestedClass,
       "NestedClass",
       true,
       {idx(TableId::TypeDef), idx(TableId::TypeDef)}},
      {TableId::GenericParam,
       "GenericParam",
       true,
       {ColU16, ColU16, coded(CodedIndexKind::TypeOrMethodDef), ColStr}},
      {TableId::MethodSpec,
       "MethodSpec",
       false,
       {coded(CodedIndexKind::MethodDefOrRef), ColBlob}},
      {TableId::GenericParamConstraint,
       "GenericParamConstraint",
       true,
       {idx(TableId::GenericParam), coded(CodedIndexKind::TypeDefOrRef)}},
  };
  return Schema;
}

struct ECMACodedIndex {
  CodedIndexKind Kind;
  unsigned TagBits;
  std::vector<std::optional<TableId>> Tables; // in tag order; nullopt: unused
};

// II.24.2.6's coded indexes.
const std::vector<ECMACodedIndex> &getECMACodedIndexes() {
  static const std::vector<ECMACodedIndex> List = {
      {CodedIndexKind::TypeDefOrRef,
       2,
       {TableId::TypeDef, TableId::TypeRef, TableId::TypeSpec}},
      {CodedIndexKind::HasConstant,
       2,
       {TableId::Field, TableId::Param, TableId::Property}},
      {CodedIndexKind::HasCustomAttribute,
       5,
       {TableId::MethodDef,
        TableId::Field,
        TableId::TypeRef,
        TableId::TypeDef,
        TableId::Param,
        TableId::InterfaceImpl,
        TableId::MemberRef,
        TableId::Module,
        TableId::DeclSecurity,
        TableId::Property,
        TableId::Event,
        TableId::StandAloneSig,
        TableId::ModuleRef,
        TableId::TypeSpec,
        TableId::Assembly,
        TableId::AssemblyRef,
        TableId::File,
        TableId::ExportedType,
        TableId::ManifestResource,
        TableId::GenericParam,
        TableId::GenericParamConstraint,
        TableId::MethodSpec}},
      {CodedIndexKind::HasFieldMarshal, 1, {TableId::Field, TableId::Param}},
      {CodedIndexKind::HasDeclSecurity,
       2,
       {TableId::TypeDef, TableId::MethodDef, TableId::Assembly}},
      {CodedIndexKind::MemberRefParent,
       3,
       {TableId::TypeDef, TableId::TypeRef, TableId::ModuleRef,
        TableId::MethodDef, TableId::TypeSpec}},
      {CodedIndexKind::HasSemantics, 1, {TableId::Event, TableId::Property}},
      {CodedIndexKind::MethodDefOrRef,
       1,
       {TableId::MethodDef, TableId::MemberRef}},
      {CodedIndexKind::MemberForwarded,
       1,
       {TableId::Field, TableId::MethodDef}},
      {CodedIndexKind::Implementation,
       2,
       {TableId::File, TableId::AssemblyRef, TableId::ExportedType}},
      {CodedIndexKind::CustomAttributeType,
       3,
       {std::nullopt, std::nullopt, TableId::MethodDef, TableId::MemberRef,
        std::nullopt}},
      {CodedIndexKind::ResolutionScope,
       2,
       {TableId::Module, TableId::ModuleRef, TableId::AssemblyRef,
        TableId::TypeRef}},
      {CodedIndexKind::TypeOrMethodDef,
       1,
       {TableId::TypeDef, TableId::MethodDef}},
  };
  return List;
}

using RowCounts = std::array<uint32_t, NumTableIds>;

RowCounts noRows() {
  RowCounts R;
  R.fill(0);
  return R;
}

// II.24.2.6's width rules applied to the ECMA schema above.
unsigned getExpectedColumnSize(const ExpectedColumn &C, const RowCounts &Rows,
                               uint8_t HeapSizes) {
  switch (C.Kind) {
  case ColumnKind::U16:
    return 2;
  case ColumnKind::U32:
    return 4;
  case ColumnKind::StringIdx:
    return (HeapSizes & 0x01) ? 4 : 2;
  case ColumnKind::GuidIdx:
    return (HeapSizes & 0x02) ? 4 : 2;
  case ColumnKind::BlobIdx:
    return (HeapSizes & 0x04) ? 4 : 2;
  case ColumnKind::TableIdx:
    return Rows[C.Target] < 0x10000 ? 2 : 4;
  case ColumnKind::CodedIdx:
    for (const ECMACodedIndex &K : getECMACodedIndexes()) {
      if (static_cast<uint8_t>(K.Kind) != C.Target)
        continue;
      uint32_t Max = 0;
      for (const std::optional<TableId> &T : K.Tables)
        if (T)
          Max = std::max(Max, Rows[static_cast<unsigned>(*T)]);
      return Max < (1u << (16 - K.TagBits)) ? 2 : 4;
    }
    break;
  }
  ADD_FAILURE() << "column kind without a width rule";
  return 0;
}

// Every column and row size of every table follows the rules.
void expectWidths(const MetadataSizes &S, const RowCounts &Rows,
                  uint8_t HeapSizes) {
  for (const ECMATable &T : getECMASchema()) {
    unsigned RowSize = 0;
    for (unsigned C = 0; C != T.Columns.size(); ++C) {
      unsigned Size = getExpectedColumnSize(T.Columns[C], Rows, HeapSizes);
      EXPECT_EQ(S.getColumnSize(T.Id, C), Size) << T.Name << " column " << C;
      RowSize += Size;
    }
    EXPECT_EQ(S.getRowSize(T.Id), RowSize) << T.Name;
  }
}

TEST(CLITableSchemaTest, TableNames) {
  const std::vector<ECMATable> &Schema = getECMASchema();
  ASSERT_EQ(Schema.size(), NumSchemaTables);
  for (const ECMATable &T : Schema) {
    SCOPED_TRACE(T.Name);
    StringRef Name = getTableName(T.Id);
    switch (static_cast<unsigned>(T.Id)) {
    case 0x1E:
      EXPECT_TRUE(Name.equals_insensitive("ENCLog")) << Name.str();
      break;
    case 0x1F:
      EXPECT_TRUE(Name.equals_insensitive("ENCMap")) << Name.str();
      break;
    default:
      EXPECT_EQ(Name, T.Name);
      break;
    }
  }
}

// The tables II.22 requires to be sorted, which are exactly SortedTablesMask.
TEST(CLITableSchemaTest, SortedTables) {
  uint64_t Mask = 0;
  for (const ECMATable &T : getECMASchema()) {
    SCOPED_TRACE(T.Name);
    EXPECT_EQ(isSortedTable(T.Id), T.Sorted);
    if (T.Sorted)
      Mask |= 1ULL << static_cast<unsigned>(T.Id);
  }
  EXPECT_EQ(Mask, SortedTablesMask);
}

TEST(CLITableSchemaTest, ColumnsMatchECMA) {
  for (const ECMATable &T : getECMASchema()) {
    SCOPED_TRACE(T.Name);
    ArrayRef<ColumnInfo> Columns = getTableColumns(T.Id);
    ASSERT_EQ(Columns.size(), T.Columns.size());
    for (unsigned C = 0; C != Columns.size(); ++C) {
      SCOPED_TRACE("column " + std::to_string(C) + " " + Columns[C].Name.str());
      EXPECT_FALSE(Columns[C].Name.empty());
      EXPECT_EQ(Columns[C].Kind, T.Columns[C].Kind);
      if (T.Columns[C].Kind == ColumnKind::TableIdx ||
          T.Columns[C].Kind == ColumnKind::CodedIdx)
        EXPECT_EQ(unsigned(Columns[C].Target), unsigned(T.Columns[C].Target));
    }
  }
}

TEST(CLITableSchemaTest, CodedIndexTables) {
  for (const ECMACodedIndex &K : getECMACodedIndexes()) {
    SCOPED_TRACE("coded index " + std::to_string(unsigned(K.Kind)));
    EXPECT_EQ(getCodedIndexTagBits(K.Kind), K.TagBits);
    ArrayRef<std::optional<TableId>> Tables = getCodedIndexTables(K.Kind);
    EXPECT_LE(Tables.size(), 1u << K.TagBits);
    // CustomAttributeType's trailing unused tag 4 may be left out.
    if (K.Kind == CodedIndexKind::CustomAttributeType)
      ASSERT_GE(Tables.size(), 4u);
    else
      ASSERT_EQ(Tables.size(), K.Tables.size());
    for (unsigned Tag = 0; Tag != Tables.size(); ++Tag) {
      std::optional<TableId> Expected;
      if (Tag < K.Tables.size())
        Expected = K.Tables[Tag];
      EXPECT_EQ(Tables[Tag], Expected) << "tag " << Tag;
    }
  }
}

std::optional<uint32_t> tok(TableId T, uint32_t Row) {
  return makeToken(T, Row);
}

// II.24.2.6: e << log n | tag, with its example: HasConstant 0x321 is Param
// row 0xC8. Nil (token 0 or row 0) encodes to 0 and decodes from 0 or row 0;
// a table outside the index, an unused tag and a row beyond 24 bits fail.
TEST(CLITableSchemaTest, CodedIndexEncoding) {
  EXPECT_EQ(decodeCodedIndex(CodedIndexKind::HasConstant, 0x321),
            tok(TableId::Param, 0xC8));
  EXPECT_EQ(encodeCodedIndex(CodedIndexKind::HasConstant,
                             makeToken(TableId::Param, 0xC8)),
            std::optional<uint32_t>(0x321));
  EXPECT_EQ(encodeCodedIndex(CodedIndexKind::ResolutionScope,
                             makeToken(TableId::AssemblyRef, 1)),
            std::optional<uint32_t>((1 << 2) | 2));
  EXPECT_EQ(encodeCodedIndex(CodedIndexKind::CustomAttributeType,
                             makeToken(TableId::MemberRef, 5)),
            std::optional<uint32_t>((5 << 3) | 3));
  EXPECT_EQ(encodeCodedIndex(CodedIndexKind::HasCustomAttribute,
                             makeToken(TableId::MethodSpec, 1)),
            std::optional<uint32_t>((1 << 5) | 21));
  EXPECT_EQ(decodeCodedIndex(CodedIndexKind::TypeOrMethodDef, (2 << 1) | 1),
            tok(TableId::MethodDef, 2));

  // Every table of every coded index round-trips at row 1 and row 2^24-1.
  for (const ECMACodedIndex &K : getECMACodedIndexes()) {
    for (unsigned Tag = 0; Tag != K.Tables.size(); ++Tag) {
      if (!K.Tables[Tag])
        continue;
      for (uint32_t Row : {1u, 0x1234u, 0x00FFFFFFu}) {
        uint32_t Token = makeToken(*K.Tables[Tag], Row);
        uint32_t Expected = (Row << K.TagBits) | Tag;
        SCOPED_TRACE("token 0x" + utohexstr(Token));
        EXPECT_EQ(encodeCodedIndex(K.Kind, Token),
                  std::optional<uint32_t>(Expected));
        EXPECT_EQ(decodeCodedIndex(K.Kind, Expected),
                  std::optional<uint32_t>(Token));
      }
      // Row 0 of a table the index can reference is nil.
      EXPECT_EQ(encodeCodedIndex(K.Kind, makeToken(*K.Tables[Tag], 0)),
                std::optional<uint32_t>(0));
      EXPECT_EQ(decodeCodedIndex(K.Kind, Tag), std::optional<uint32_t>(0));
    }
    EXPECT_EQ(encodeCodedIndex(K.Kind, 0), std::optional<uint32_t>(0));
    EXPECT_EQ(decodeCodedIndex(K.Kind, 0), std::optional<uint32_t>(0));
    // Unused tags up to 2^TagBits - 1.
    for (unsigned Tag = 0; Tag != (1u << K.TagBits); ++Tag)
      if (Tag >= K.Tables.size() || !K.Tables[Tag])
        EXPECT_FALSE(decodeCodedIndex(K.Kind, (1u << K.TagBits) | Tag))
            << "unused tag " << Tag;
  }

  // Tables an index cannot reference.
  EXPECT_FALSE(encodeCodedIndex(CodedIndexKind::TypeDefOrRef,
                                makeToken(TableId::MethodDef, 1)));
  EXPECT_FALSE(encodeCodedIndex(CodedIndexKind::HasConstant,
                                makeToken(TableId::TypeDef, 1)));
  EXPECT_FALSE(encodeCodedIndex(CodedIndexKind::CustomAttributeType,
                                makeToken(TableId::TypeRef, 1)));
  EXPECT_FALSE(encodeCodedIndex(CodedIndexKind::HasCustomAttribute,
                                makeToken(TableId::ClassLayout, 1)));
  EXPECT_FALSE(encodeCodedIndex(CodedIndexKind::Implementation,
                                makeToken(TableId::Module, 1)));
  EXPECT_FALSE(encodeCodedIndex(CodedIndexKind::ResolutionScope,
                                makeToken(TableId::TypeSpec, 1)));
  EXPECT_FALSE(encodeCodedIndex(CodedIndexKind::MethodDefOrRef, 0x70000001))
      << "a user-string token";
  // A row that does not fit a token (24 bits).
  EXPECT_FALSE(decodeCodedIndex(CodedIndexKind::HasFieldMarshal,
                                (0x01000000u << 1) | 1));
  EXPECT_FALSE(
      decodeCodedIndex(CodedIndexKind::TypeDefOrRef, (0x01000000u << 2) | 1));
}

// II.24.2.6: a simple index is 2 bytes iff its table has fewer than 2^16 rows.
TEST(CLITableSchemaTest, SimpleIndexWidthBoundary) {
  for (const ECMATable &T : getECMASchema()) {
    for (unsigned C = 0; C != T.Columns.size(); ++C) {
      if (T.Columns[C].Kind != ColumnKind::TableIdx)
        continue;
      SCOPED_TRACE(std::string(T.Name) + " column " + std::to_string(C));
      RowCounts Rows = noRows();
      Rows[T.Columns[C].Target] = 0xFFFF;
      MetadataSizes Small(Rows, 0);
      EXPECT_EQ(Small.getColumnSize(T.Id, C), 2u);
      Rows[T.Columns[C].Target] = 0x10000;
      MetadataSizes Large(Rows, 0);
      EXPECT_EQ(Large.getColumnSize(T.Id, C), 4u);
      Rows[T.Columns[C].Target] = 0xFFFF;
      expectWidths(Small, Rows, 0);
      Rows[T.Columns[C].Target] = 0x10000;
      expectWidths(Large, Rows, 0);
    }
  }
}

// II.24.2.6: a coded index with n tag bits is 2 bytes iff every table it can
// reference has fewer than 2^(16-n) rows.
TEST(CLITableSchemaTest, CodedIndexWidthBoundary) {
  const std::vector<ECMACodedIndex> &Coded = getECMACodedIndexes();
  for (const ECMACodedIndex &K : Coded) {
    uint32_t Limit = 1u << (16 - K.TagBits);
    for (const std::optional<TableId> &Table : K.Tables) {
      if (!Table)
        continue;
      SCOPED_TRACE("coded index " + std::to_string(unsigned(K.Kind)) +
                   ", table " + std::to_string(unsigned(*Table)));
      RowCounts Rows = noRows();
      Rows[unsigned(*Table)] = Limit - 1;
      MetadataSizes Small(Rows, 0);
      EXPECT_EQ(Small.getCodedIndexSize(K.Kind), 2u);
      Rows[unsigned(*Table)] = Limit;
      MetadataSizes Large(Rows, 0);
      EXPECT_EQ(Large.getCodedIndexSize(K.Kind), 4u);
      expectWidths(Large, Rows, 0);
      // The other coded indexes grow only if they also reference the table
      // and their own limit is reached.
      for (const ECMACodedIndex &Other : Coded) {
        bool References = false;
        for (const std::optional<TableId> &T : Other.Tables)
          References |= T == Table;
        bool Wide = References && Limit >= (1u << (16 - Other.TagBits));
        EXPECT_EQ(Large.getCodedIndexSize(Other.Kind), Wide ? 4u : 2u)
            << "coded index " << unsigned(Other.Kind);
      }
      // Columns of the coded kind follow getCodedIndexSize.
      for (const ECMATable &T : getECMASchema())
        for (unsigned C = 0; C != T.Columns.size(); ++C)
          if (T.Columns[C].Kind == ColumnKind::CodedIdx &&
              T.Columns[C].Target == uint8_t(K.Kind)) {
            EXPECT_EQ(Small.getColumnSize(T.Id, C), 2u) << T.Name;
            EXPECT_EQ(Large.getColumnSize(T.Id, C), 4u) << T.Name;
          }
    }
  }
}

// II.24.2.6: HeapSizes bits 0x01 (#Strings), 0x02 (#GUID) and 0x04 (#Blob)
// make that heap's indexes 4 bytes; other bits (0x40, extra data) do not
// change any index width, and the flags are kept as given.
TEST(CLITableSchemaTest, HeapIndexWidths) {
  RowCounts Rows = noRows();
  Rows[unsigned(TableId::Module)] = 1;
  for (unsigned Flags : {0x00u, 0x01u, 0x02u, 0x04u, 0x07u, 0x40u, 0x45u}) {
    SCOPED_TRACE("HeapSizes 0x" + utohexstr(Flags));
    MetadataSizes S(Rows, Flags);
    EXPECT_EQ(unsigned(S.getHeapSizeFlags()), Flags);
    unsigned Str = (Flags & 0x01) ? 4 : 2, Guid = (Flags & 0x02) ? 4 : 2,
             Blob = (Flags & 0x04) ? 4 : 2;
    // Module: Generation, Name, Mvid, EncId, EncBaseId.
    EXPECT_EQ(S.getColumnSize(TableId::Module, 0), 2u);
    EXPECT_EQ(S.getColumnSize(TableId::Module, 1), Str);
    EXPECT_EQ(S.getColumnSize(TableId::Module, 2), Guid);
    EXPECT_EQ(S.getRowSize(TableId::Module), 2 + Str + 3 * Guid);
    // Field: Flags, Name, Signature.
    EXPECT_EQ(S.getRowSize(TableId::Field), 2 + Str + Blob);
    // Assembly: HashAlgId, four versions, Flags, PublicKey, Name, Culture.
    EXPECT_EQ(S.getRowSize(TableId::Assembly), 16 + Blob + 2 * Str);
    expectWidths(S, Rows, Flags);
  }
}

// computeHeapSizeFlags sets a bit iff the stream's size (with padding) is at
// least 0x10000.
TEST(CLITableSchemaTest, ComputeHeapSizeFlags) {
  EXPECT_EQ(MetadataSizes::computeHeapSizeFlags(0, 0, 0), 0u);
  EXPECT_EQ(MetadataSizes::computeHeapSizeFlags(0xFFFF, 0xFFFF, 0xFFFF), 0u);
  EXPECT_EQ(MetadataSizes::computeHeapSizeFlags(0x10000, 0, 0), HEAP_STRINGS_4);
  EXPECT_EQ(MetadataSizes::computeHeapSizeFlags(0, 0x10000, 0), HEAP_GUID_4);
  EXPECT_EQ(MetadataSizes::computeHeapSizeFlags(0, 0, 0x10000), HEAP_BLOB_4);
  EXPECT_EQ(MetadataSizes::computeHeapSizeFlags(0x10000, 0xFFFF, 0x10000),
            HEAP_STRINGS_4 | HEAP_BLOB_4);
  EXPECT_EQ(MetadataSizes::computeHeapSizeFlags(0x10000, 0x10000, 0x10000),
            HEAP_STRINGS_4 | HEAP_GUID_4 | HEAP_BLOB_4);
  EXPECT_EQ(MetadataSizes::computeHeapSizeFlags(1ULL << 32, 1ULL << 33, 0),
            HEAP_STRINGS_4 | HEAP_GUID_4);
}

// Fixed-width columns never change, and column offsets are the running sum
// of the column sizes: TypeDef with small and large indexes.
TEST(CLITableSchemaTest, ColumnOffsets) {
  RowCounts Rows = noRows();
  MetadataSizes Small(Rows, 0);
  const unsigned SmallOffsets[] = {0, 4, 6, 8, 10, 12};
  for (unsigned C = 0; C != 6; ++C)
    EXPECT_EQ(Small.getColumnOffset(TableId::TypeDef, C), SmallOffsets[C]);
  EXPECT_EQ(Small.getRowSize(TableId::TypeDef), 14u);
  EXPECT_EQ(Small.getColumnSize(TableId::MethodDef, 0), 4u);
  EXPECT_EQ(Small.getColumnSize(TableId::MethodDef, 1), 2u);

  Rows[unsigned(TableId::TypeRef)] = 0x4000; // TypeDefOrRef: 4 bytes
  Rows[unsigned(TableId::MethodDef)] = 0x10000;
  MetadataSizes Large(Rows, HEAP_STRINGS_4);
  const unsigned LargeOffsets[] = {0, 4, 8, 12, 16, 18};
  for (unsigned C = 0; C != 6; ++C)
    EXPECT_EQ(Large.getColumnOffset(TableId::TypeDef, C), LargeOffsets[C]);
  EXPECT_EQ(Large.getRowSize(TableId::TypeDef), 22u);
  EXPECT_EQ(Large.getColumnSize(TableId::MethodDef, 0), 4u);
  EXPECT_EQ(Large.getColumnSize(TableId::MethodDef, 1), 2u);
  EXPECT_EQ(Large.getRowCount(TableId::MethodDef), 0x10000u);
  EXPECT_EQ(Large.getRowCount(TableId::Field), 0u);

  for (const ECMATable &T : getECMASchema()) {
    SCOPED_TRACE(T.Name);
    for (const MetadataSizes *S : {&Small, &Large}) {
      unsigned Offset = 0;
      for (unsigned C = 0; C != T.Columns.size(); ++C) {
        EXPECT_EQ(S->getColumnOffset(T.Id, C), Offset);
        unsigned Size = S->getColumnSize(T.Id, C);
        if (T.Columns[C].Kind == ColumnKind::U16)
          EXPECT_EQ(Size, 2u);
        else if (T.Columns[C].Kind == ColumnKind::U32)
          EXPECT_EQ(Size, 4u);
        else
          EXPECT_TRUE(Size == 2 || Size == 4);
        Offset += Size;
      }
      EXPECT_EQ(S->getRowSize(T.Id), Offset);
    }
  }
}

// What System.Reflection.Metadata reports for a real metadata image.
struct AssemblyLayout {
  const char *Name;
  uint8_t HeapSizes;
  uint64_t StringsSize, GuidSize, BlobSize; // stream header sizes
  uint32_t Rows[NumSchemaTables];
  uint8_t RowSizes[NumSchemaTables];
  int64_t Offsets[NumSchemaTables]; // in the table data; -1: no rows
};

const AssemblyLayout Layouts[] = {
    {"ilasm: msil/test/M1/11-programs/dispatch-vm.c",
     0x00,
     0x580,
     0x10,
     0xA4,
     {
         // rows of table ids 0x00-0x2C
         1, 2, 2,  0, 56, 0, 25, 0, 23, 0, 1, 0, 0, 0, 0,
         1, 0, 15, 0, 0,  0, 0,  0, 0,  0, 0, 1, 0, 1, 1,
         0, 0, 1,  0, 0,  1, 0,  0, 0,  0, 0, 0, 0, 0, 0,
     },
     {
         // row sizes
         10, 6, 14, 2, 6,  2,  14, 2,  6, 4,  6,  6, 6, 4, 6,
         8,  6, 2,  4, 2,  6,  4,  2,  6, 6,  6,  2, 2, 8, 6,
         8,  4, 22, 4, 12, 20, 6,  14, 8, 14, 12, 4, 8, 4, 4,
     },
     {
         // offsets in the table data (-1: no rows)
         0,   10, 22,  -1, 50, -1,  386, -1, 736, -1, 874, -1,  -1, -1,  -1,
         880, -1, 888, -1, -1, -1,  -1,  -1, -1,  -1, -1,  918, -1, 920, 928,
         -1,  -1, 934, -1, -1, 956, -1,  -1, -1,  -1, -1,  -1,  -1, -1,  -1,
     }},
    {"net8 ref System.Runtime.dll",
     0x05,
     0x24488,
     0x10,
     0x10F64,
     {
         // rows of table ids 0x00-0x2C
         1,    0,    891, 0,   1300, 0,  11108, 0, 15805, 1339, 1920, 1229,
         8392, 0,    1,   6,   0,    0,  5,     0, 13,    410,  0,    1693,
         1978, 2420, 0,   626, 0,    0,  0,     0, 1,     0,    0,    0,
         0,    0,    0,   0,   0,    33, 1834,  0, 302,
     },
     {
         // row sizes
         12, 10, 18, 2, 10, 2,  18, 2,  8,  4,  12, 8, 12, 6,  8,
         8,  6,  4,  4, 2,  8,  4,  2,  10, 6,  6,  4, 4,  10, 6,
         8,  4,  28, 4, 12, 28, 6,  14, 12, 18, 14, 4, 10, 6,  4,
     },
     {
         // offsets in the table data (-1: no rows)
         0,      -1,     12,     -1,     16050, -1,     29050,  -1,     228994,
         355434, 360790, 383830, 393662, -1,    494366, 494374, -1,     -1,
         494422, -1,     494442, 494546, -1,    496186, 513116, 524984, -1,
         539504, -1,     -1,     -1,     -1,    542008, -1,     -1,     -1,
         -1,     -1,     -1,     -1,     -1,    542036, 542168, -1,     560508,
     }},
    {"Framework 4.8 ref mscorlib.dll",
     0x05,
     0x4D3F0,
     0x10,
     0x2A7D4,
     {
         // rows of table ids 0x00-0x2C
         1,     0,    2993, 0,   8785, 0,   24237, 0, 30391, 1236, 58, 5128,
         10672, 759,  443,  58,  78,   0,   15,    0, 31,    1136, 0,  4583,
         5510,  1211, 16,   199, 312,  104, 0,     0, 1,     0,    0,  0,
         0,     0,    5,    0,   8,    350, 1595,  1, 84,
     },
     {
         // row sizes
         12, 10, 18, 2, 10, 2,  18, 2,  8,  4,  12, 10, 12, 6,  10,
         8,  6,  4,  4, 2,  8,  4,  2,  10, 6,  6,  4,  4,  10, 6,
         8,  4,  28, 4, 12, 28, 6,  14, 12, 18, 14, 4,  10, 6,  4,
     },
     {
         // offsets in the table data (-1: no rows)
         0,       -1,      12,      -1,      53886,   -1,      141736,  -1,
         578002,  821130,  826074,  826770,  878050,  1006114, 1010668, 1015098,
         1015562, -1,      1016030, -1,      1016090, 1016338, -1,      1020882,
         1066712, 1099772, 1107038, 1107102, 1107898, 1111018, -1,      -1,
         1111642, -1,      -1,      -1,      -1,      -1,      1111670, -1,
         1111730, 1111842, 1113242, 1129192, 1129198,
     }},
    {"net8 System.Private.CoreLib.dll",
     0x05,
     0x7A740,
     0x10,
     0x52C68,
     {
         // rows of table ids 0x00-0x2C
         1,    0,    2595, 0,     9012, 0,    34519, 0,    52862,
         1994, 6707, 3480, 21765, 202,  0,    127,   112,  3662,
         12,   0,    33,   967,   0,    5128, 5657,  3304, 15,
         1992, 432,  121,  0,     0,    1,    0,     0,    0,
         0,    0,    0,    0,     4,    852,  3380,  3282, 1330,
     },
     {
         // row sizes
         12, 10, 18, 2, 10, 2,  18, 2,  8,  4,  12, 10, 12, 8,  10,
         8,  6,  4,  4, 2,  8,  4,  2,  10, 6,  10, 4,  4,  12, 6,
         8,  4,  28, 4, 12, 28, 6,  14, 12, 18, 14, 4,  12, 8,  4,
     },
     {
         // offsets in the table data (-1: no rows)
         0,       -1,      12,      -1,      46722,   -1,      136842, -1,
         758184,  1181080, 1189056, 1269540, 1304340, 1565520, -1,     1567136,
         1568152, 1568824, 1583472, -1,      1583520, 1583784, -1,     1587652,
         1638932, 1672874, 1705914, 1705974, 1713942, 1719126, -1,     -1,
         1719852, -1,      -1,      -1,      -1,      -1,      -1,     -1,
         1719880, 1719936, 1723344, 1763904, 1790160,
     }},
    {"SRM MetadataBuilder: 70000 Fields, 16384 TypeRefs",
     0x07,
     0xA4B98,
     0x10000,
     0x1117C,
     {
         // rows of table ids 0x00-0x2C
         1, 16384, 1, 0, 70000, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0,     0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0,     0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     },
     {
         // row sizes
         18, 12, 22, 4, 10, 2,  18, 2,  8,  6,  12, 10, 10, 8,  8,
         8,  8,  4,  4, 2,  10, 4,  2,  10, 6,  6,  4,  4,  12, 8,
         8,  4,  28, 4, 12, 28, 6,  14, 12, 18, 14, 4,  10, 6,  6,
     },
     {
         // offsets in the table data (-1: no rows)
         0,  18, 196626, -1, 196648, -1, 896648, -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1,     -1, -1,     -1, -1,     -1, -1, -1, -1, -1, -1, -1, -1,
         -1, -1, -1,     -1, -1,     -1, -1,     -1, -1, -1, -1, -1, -1, -1, -1,
     }},
};

void checkLayout(const AssemblyLayout &L) {
  RowCounts Rows = noRows();
  for (unsigned I = 0; I != NumSchemaTables; ++I)
    Rows[I] = L.Rows[I];
  MetadataSizes S(Rows, L.HeapSizes);
  EXPECT_EQ(unsigned(S.getHeapSizeFlags()), unsigned(L.HeapSizes));
  EXPECT_EQ(unsigned(MetadataSizes::computeHeapSizeFlags(
                L.StringsSize, L.GuidSize, L.BlobSize)),
            unsigned(L.HeapSizes & 0x07))
      << "the HeapSizes the producer wrote";
  uint64_t Total = 0;
  for (unsigned I = 0; I != NumSchemaTables; ++I) {
    TableId T = static_cast<TableId>(I);
    SCOPED_TRACE(getECMASchema()[I].Name);
    EXPECT_EQ(S.getRowCount(T), L.Rows[I]);
    EXPECT_EQ(S.getRowSize(T), unsigned(L.RowSizes[I]));
    if (L.Rows[I])
      EXPECT_EQ(S.getTableOffset(T), uint64_t(L.Offsets[I]));
    Total += uint64_t(L.Rows[I]) * L.RowSizes[I];
  }
  EXPECT_EQ(S.getTablesSize(), Total);
}

// An ilasm 4.8 build of msil/test/M1/11-programs/dispatch-vm.c (clang -O2
// -S --target=x86_64-pc-windows-msvc-msil): 14 tables, HeapSizes 0x00.
TEST(CLITableSchemaTest, RowSizesIlasmProgram) { checkLayout(Layouts[0]); }

// The net8 reference System.Runtime.dll (Microsoft.NETCore.App.Ref 8.0.31):
// HeapSizes 0x05 and 4-byte coded indexes (11,108 MethodDefs).
TEST(CLITableSchemaTest, RowSizesNet8SystemRuntime) { checkLayout(Layouts[1]); }

// The .NET Framework 4.8 reference mscorlib.dll: 30 tables.
TEST(CLITableSchemaTest, RowSizesFramework48Mscorlib) {
  checkLayout(Layouts[2]);
}

// The net8 System.Private.CoreLib.dll (8.0.31): 1-bit coded indexes past
// 2^15 rows (34,519 MethodDefs, 52,862 Params).
TEST(CLITableSchemaTest, RowSizesNet8CoreLib) { checkLayout(Layouts[3]); }

// Metadata written by System.Reflection.Metadata's MetadataBuilder with 70,000
// Fields (4-byte Field indexes), 16,384 TypeRefs (2^14: 4-byte TypeDefOrRef,
// ResolutionScope and MemberRefParent), one MethodDef (2-byte MethodDef
// indexes) and all three heaps at least 0x10000 bytes (#GUID exactly).
TEST(CLITableSchemaTest, RowSizesLargeIndexes) { checkLayout(Layouts[4]); }

} // namespace
