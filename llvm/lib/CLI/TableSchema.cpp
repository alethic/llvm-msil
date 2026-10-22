//===- TableSchema.cpp - ECMA-335 table schema ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The table schema of BinaryFormat/CLITables.def as constant tables, the
// coded index encoding, and the #~ layout of II.24.2.6. Every table here is
// constexpr, so the library has no static constructors, and the
// static_asserts below check the .def's invariants at compile time.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/TableSchema.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <iterator>

using namespace llvm;
using namespace llvm::cli;

//===----------------------------------------------------------------------===//
// Tables
//===----------------------------------------------------------------------===//

namespace {
struct TableInfo {
  uint8_t Id;
  StringLiteral Name;
  bool Sorted;
};
} // namespace

static constexpr TableInfo Tables[] = {
#define CLI_TABLE(Id, Name, Sorted) {Id, #Name, Sorted != 0},
#include "llvm/BinaryFormat/CLITables.def"
};

static constexpr unsigned NumTables = std::size(Tables);

static constexpr bool tableIdsAreDense() {
  for (unsigned I = 0; I != NumTables; ++I)
    if (Tables[I].Id != I)
      return false;
  return true;
}
static_assert(tableIdsAreDense(), "CLITables.def: table ids must be 0, 1, ...");
static_assert(NumTables == 0x2D, "CLITables.def: ECMA-335 has 45 tables");
static_assert(NumTables <= NumTableIds, "a table id past the #~ bit vectors");

static constexpr uint64_t computeSortedMask() {
  uint64_t Mask = 0;
  for (const TableInfo &T : Tables)
    if (T.Sorted)
      Mask |= uint64_t(1) << T.Id;
  return Mask;
}
static_assert(computeSortedMask() == SortedTablesMask,
              "CLITables.def: the Sorted flags must equal SortedTablesMask");

[[maybe_unused]] static bool isSchemaTable(TableId T) {
  return static_cast<uint8_t>(T) < NumTables;
}

//===----------------------------------------------------------------------===//
// Columns
//===----------------------------------------------------------------------===//

// The Target of a column, by its Kind: a TableId or a CodedIndexKind value.
#define CLI_COLUMN_TARGET_U16(Target) 0
#define CLI_COLUMN_TARGET_U32(Target) 0
#define CLI_COLUMN_TARGET_StringIdx(Target) 0
#define CLI_COLUMN_TARGET_GuidIdx(Target) 0
#define CLI_COLUMN_TARGET_BlobIdx(Target) 0
#define CLI_COLUMN_TARGET_TableIdx(Target) static_cast<uint8_t>(TableId::Target)
#define CLI_COLUMN_TARGET_CodedIdx(Target)                                     \
  static_cast<uint8_t>(CodedIndexKind::Target)

// Every column of every table, grouped by table in id order.
static constexpr ColumnInfo Columns[] = {
#define CLI_COLUMN(Table, Column, Kind, Target)                                \
  {#Column, ColumnKind::Kind, CLI_COLUMN_TARGET_##Kind(Target)},
#include "llvm/BinaryFormat/CLITables.def"
};

// The table of each entry of Columns.
static constexpr uint8_t ColumnTables[] = {
#define CLI_COLUMN(Table, Column, Kind, Target)                                \
  static_cast<uint8_t>(TableId::Table),
#include "llvm/BinaryFormat/CLITables.def"
};

#undef CLI_COLUMN_TARGET_U16
#undef CLI_COLUMN_TARGET_U32
#undef CLI_COLUMN_TARGET_StringIdx
#undef CLI_COLUMN_TARGET_GuidIdx
#undef CLI_COLUMN_TARGET_BlobIdx
#undef CLI_COLUMN_TARGET_TableIdx
#undef CLI_COLUMN_TARGET_CodedIdx

static constexpr unsigned NumColumns = std::size(Columns);
static_assert(std::size(ColumnTables) == NumColumns);

// ColumnStarts[T] is the index in Columns of table T's first column, and
// ColumnStarts[NumTables] is NumColumns.
static constexpr std::array<uint16_t, NumTables + 1> computeColumnStarts() {
  std::array<uint16_t, NumTables + 1> Starts{};
  unsigned C = 0;
  for (unsigned T = 0; T != NumTables; ++T) {
    Starts[T] = C;
    while (C != NumColumns && ColumnTables[C] == T)
      ++C;
  }
  Starts[NumTables] = C;
  return Starts;
}
static constexpr std::array<uint16_t, NumTables + 1> ColumnStarts =
    computeColumnStarts();

static constexpr bool everyTableHasColumnsInOrder() {
  // Every column was assigned to its table (the .def lists the columns
  // grouped by table in id order) and every table has at least one column.
  if (ColumnStarts[NumTables] != NumColumns)
    return false;
  for (unsigned T = 0; T != NumTables; ++T)
    if (ColumnStarts[T + 1] == ColumnStarts[T])
      return false;
  return true;
}
static_assert(everyTableHasColumnsInOrder(),
              "CLITables.def: columns must be grouped by table in id order");

//===----------------------------------------------------------------------===//
// Coded indexes
//===----------------------------------------------------------------------===//

namespace {
// The tables of each coded index, named as CLITables.def names them: every
// table name, and None, is a constant of this namespace.
namespace coded {
constexpr std::optional<TableId> None = std::nullopt;
#define CLI_TABLE(Id, Name, Sorted)                                            \
  constexpr std::optional<TableId> Name = TableId::Name;
#include "llvm/BinaryFormat/CLITables.def"
#define CLI_CODED_INDEX(Name, TagBits, ...)                                    \
  constexpr std::optional<TableId> Name##Tables[] = {__VA_ARGS__};
#include "llvm/BinaryFormat/CLITables.def"
} // namespace coded

struct CodedIndexInfo {
  unsigned TagBits;
  /// The tables in tag order, NumTags of them.
  const std::optional<TableId> *Tables;
  unsigned NumTags;
};
} // namespace

static constexpr CodedIndexInfo CodedIndexes[] = {
#define CLI_CODED_INDEX(Name, TagBits, ...)                                    \
  {TagBits, coded::Name##Tables, std::size(coded::Name##Tables)},
#include "llvm/BinaryFormat/CLITables.def"
};

static constexpr unsigned NumCodedIndexes = std::size(CodedIndexes);
static_assert(NumCodedIndexes == 13,
              "CLITables.def: ECMA-335 has 13 coded indexes");

static constexpr bool tagBitsCoverTables() {
  for (const CodedIndexInfo &C : CodedIndexes) {
    if (C.TagBits == 0 || C.TagBits > 5 || C.NumTags > (1u << C.TagBits))
      return false;
    // A coded index references at least one table.
    bool Any = false;
    for (unsigned I = 0; I != C.NumTags; ++I)
      Any |= C.Tables[I].has_value();
    if (!Any)
      return false;
  }
  return true;
}
static_assert(tagBitsCoverTables(),
              "CLITables.def: a coded index's tag bits must cover its tables");

static const CodedIndexInfo &getCodedIndexInfo(CodedIndexKind K) {
  assert(static_cast<uint8_t>(K) < NumCodedIndexes && "invalid coded index");
  return CodedIndexes[static_cast<uint8_t>(K)];
}

//===----------------------------------------------------------------------===//
// Schema queries
//===----------------------------------------------------------------------===//

ArrayRef<ColumnInfo> cli::getTableColumns(TableId T) {
  assert(isSchemaTable(T) && "table id without a schema");
  unsigned I = static_cast<uint8_t>(T);
  return ArrayRef(Columns).slice(ColumnStarts[I],
                                 ColumnStarts[I + 1] - ColumnStarts[I]);
}

StringLiteral cli::getTableName(TableId T) {
  assert(isSchemaTable(T) && "table id without a schema");
  return Tables[static_cast<uint8_t>(T)].Name;
}

bool cli::isSortedTable(TableId T) {
  assert(isSchemaTable(T) && "table id without a schema");
  return Tables[static_cast<uint8_t>(T)].Sorted;
}

unsigned cli::getCodedIndexTagBits(CodedIndexKind K) {
  return getCodedIndexInfo(K).TagBits;
}

ArrayRef<std::optional<TableId>> cli::getCodedIndexTables(CodedIndexKind K) {
  const CodedIndexInfo &C = getCodedIndexInfo(K);
  return ArrayRef(C.Tables, C.NumTags);
}

std::optional<uint32_t> cli::encodeCodedIndex(CodedIndexKind K,
                                              uint32_t Token) {
  if (Token == 0)
    return 0;
  const CodedIndexInfo &C = getCodedIndexInfo(K);
  uint8_t Type = getTokenType(Token);
  for (unsigned Tag = 0; Tag != C.NumTags; ++Tag) {
    if (!C.Tables[Tag] || static_cast<uint8_t>(*C.Tables[Tag]) != Type)
      continue;
    uint32_t Row = getTokenRow(Token);
    if (Row == 0)
      return 0;
    // A 24-bit row and at most 5 tag bits always fit in 32 bits.
    return (Row << C.TagBits) | Tag;
  }
  return std::nullopt;
}

std::optional<uint32_t> cli::decodeCodedIndex(CodedIndexKind K,
                                              uint32_t Value) {
  if (Value == 0)
    return 0;
  const CodedIndexInfo &C = getCodedIndexInfo(K);
  uint32_t Tag = Value & ((1u << C.TagBits) - 1);
  if (Tag >= C.NumTags || !C.Tables[Tag])
    return std::nullopt;
  uint32_t Row = Value >> C.TagBits;
  if (Row == 0)
    return 0;
  if (Row > MaxTableRows)
    return std::nullopt;
  return makeToken(*C.Tables[Tag], Row);
}

//===----------------------------------------------------------------------===//
// MetadataSizes
//===----------------------------------------------------------------------===//

MetadataSizes::MetadataSizes(ArrayRef<uint32_t> RowCounts,
                             uint8_t HeapSizeFlags)
    : HeapFlags(HeapSizeFlags) {
  assert(RowCounts.size() == NumTableIds && "one row count per table id");
  for (unsigned I = 0; I != NumTableIds; ++I) {
    assert((I < NumTables || RowCounts[I] == 0) &&
           "rows in a table id without a schema");
    Rows[I] = RowCounts[I];
  }
  uint64_t Offset = 0;
  for (unsigned I = 0; I != NumTableIds; ++I) {
    TableOffsets[I] = Offset;
    if (I >= NumTables) {
      RowSizes[I] = 0;
      continue;
    }
    TableId T = static_cast<TableId>(I);
    unsigned Size = 0;
    for (unsigned C = 0, E = getTableColumns(T).size(); C != E; ++C)
      Size += getColumnSize(T, C);
    RowSizes[I] = Size;
    Offset += uint64_t(Size) * Rows[I];
  }
  TablesSize = Offset;
}

uint8_t MetadataSizes::computeHeapSizeFlags(uint64_t StringsSize,
                                            uint64_t GuidSize,
                                            uint64_t BlobSize) {
  uint8_t Flags = 0;
  if (StringsSize >= 0x10000)
    Flags |= HEAP_STRINGS_4;
  if (GuidSize >= 0x10000)
    Flags |= HEAP_GUID_4;
  if (BlobSize >= 0x10000)
    Flags |= HEAP_BLOB_4;
  return Flags;
}

unsigned MetadataSizes::getCodedIndexSize(CodedIndexKind K) const {
  const CodedIndexInfo &C = getCodedIndexInfo(K);
  uint32_t Limit = uint32_t(1) << (16 - C.TagBits);
  for (unsigned I = 0; I != C.NumTags; ++I)
    if (C.Tables[I] && getRowCount(*C.Tables[I]) >= Limit)
      return 4;
  return 2;
}

unsigned MetadataSizes::getColumnSize(TableId T, unsigned Column) const {
  ArrayRef<ColumnInfo> Cols = getTableColumns(T);
  assert(Column < Cols.size() && "column index out of range");
  const ColumnInfo &Col = Cols[Column];
  switch (Col.Kind) {
  case ColumnKind::U16:
    return 2;
  case ColumnKind::U32:
    return 4;
  case ColumnKind::StringIdx:
    return HeapFlags & HEAP_STRINGS_4 ? 4 : 2;
  case ColumnKind::GuidIdx:
    return HeapFlags & HEAP_GUID_4 ? 4 : 2;
  case ColumnKind::BlobIdx:
    return HeapFlags & HEAP_BLOB_4 ? 4 : 2;
  case ColumnKind::TableIdx:
    return Rows[Col.Target] < 0x10000 ? 2 : 4;
  case ColumnKind::CodedIdx:
    return getCodedIndexSize(static_cast<CodedIndexKind>(Col.Target));
  }
  llvm_unreachable("unknown column kind");
}

unsigned MetadataSizes::getColumnOffset(TableId T, unsigned Column) const {
  assert(Column < getTableColumns(T).size() && "column index out of range");
  unsigned Offset = 0;
  for (unsigned C = 0; C != Column; ++C)
    Offset += getColumnSize(T, C);
  return Offset;
}
