//===- llvm/CLI/TableSchema.h - ECMA-335 table schema -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the column schema of the ECMA-335 metadata tables
// (BinaryFormat/CLITables.def), the coded indexes, and MetadataSizes: the
// physical layout of the #~ stream's tables for given row counts and heap
// sizes (II.24.2.6). The metadata writer and the metadata reader share this
// one layout computation.
//
// Width rules (II.24.2.6):
// - a simple index is 2 bytes iff the target table has fewer than 2^16 rows;
// - a coded index with n tag bits is 2 bytes iff every one of its tables has
//   fewer than 2^(16-n) rows;
// - a heap index is 4 bytes iff its HeapSizeFlags bit is set.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_TABLESCHEMA_H
#define LLVM_CLI_TABLESCHEMA_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/Support/Compiler.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <optional>

namespace llvm {
namespace cli {

/// One column of a metadata table.
struct ColumnInfo {
  /// The column's name in II.22.
  StringLiteral Name;
  ColumnKind Kind;
  /// For TableIdx, the target TableId's value; for CodedIdx, the
  /// CodedIndexKind's value; 0 for the other kinds.
  uint8_t Target;
};

/// The columns of table T in II.22 order.
LLVM_ABI ArrayRef<ColumnInfo> getTableColumns(TableId T);

/// The name of table T in II.22 ("TypeDef").
LLVM_ABI StringLiteral getTableName(TableId T);

/// Whether II.22 requires table T to be sorted (its bit in SortedTablesMask).
LLVM_ABI bool isSortedTable(TableId T);

/// The number of tag bits of coded index K.
LLVM_ABI unsigned getCodedIndexTagBits(CodedIndexKind K);

/// The tables of coded index K in tag order; std::nullopt for an unused tag.
LLVM_ABI ArrayRef<std::optional<TableId>> getCodedIndexTables(CodedIndexKind K);

/// The coded index K value of a token: (row << tag bits) | tag. A nil token (0,
/// or row 0 of one of K's tables) gives 0; a token of a table that K cannot
/// reference gives std::nullopt.
LLVM_ABI std::optional<uint32_t> encodeCodedIndex(CodedIndexKind K,
                                                  uint32_t Token);

/// The token of a coded index K value; 0 for nil (row 0). An unused tag, or a
/// row that does not fit in a token, gives std::nullopt.
LLVM_ABI std::optional<uint32_t> decodeCodedIndex(CodedIndexKind K,
                                                  uint32_t Value);

/// The physical layout of the #~ tables for one set of row counts and heap
/// size flags: column sizes and offsets, row sizes, and where each table
/// starts in the table data that follows the #~ row counts.
class MetadataSizes {
public:
  /// RowCounts has NumTableIds entries, indexed by table id; the entries of
  /// ids without a schema (0x2D-0x3F) must be 0 (a reader rejects a Valid
  /// vector with such bits before it gets here). HeapSizeFlags is the #~
  /// HeapSizes byte: HEAP_STRINGS_4, HEAP_GUID_4 and HEAP_BLOB_4 set the
  /// width of heap indexes; other bits are kept but have no effect.
  LLVM_ABI MetadataSizes(ArrayRef<uint32_t> RowCounts, uint8_t HeapSizeFlags);

  /// The HeapSizes byte for heaps of these sizes, as written in their stream
  /// headers (with padding): each bit is set iff the size is at least 0x10000.
  LLVM_ABI static uint8_t computeHeapSizeFlags(uint64_t StringsSize,
                                               uint64_t GuidSize,
                                               uint64_t BlobSize);

  uint32_t getRowCount(TableId T) const {
    assert(static_cast<uint8_t>(T) < NumTableIds && "invalid table id");
    return Rows[static_cast<uint8_t>(T)];
  }
  uint8_t getHeapSizeFlags() const { return HeapFlags; }

  /// The size in bytes of column Column of table T: 2 for U16, 4 for U32, and
  /// 2 or 4 for heap, simple and coded indexes.
  LLVM_ABI unsigned getColumnSize(TableId T, unsigned Column) const;
  /// The offset of column Column within a row of table T.
  LLVM_ABI unsigned getColumnOffset(TableId T, unsigned Column) const;
  /// The size of one row of table T.
  unsigned getRowSize(TableId T) const {
    assert(static_cast<uint8_t>(T) < NumTableIds && "invalid table id");
    return RowSizes[static_cast<uint8_t>(T)];
  }
  /// The size of an index of coded index kind K, 2 or 4.
  LLVM_ABI unsigned getCodedIndexSize(CodedIndexKind K) const;

  /// The offset of table T within the table data that follows the #~ row
  /// counts (and the extra data field, if any).
  uint64_t getTableOffset(TableId T) const {
    assert(static_cast<uint8_t>(T) < NumTableIds && "invalid table id");
    return TableOffsets[static_cast<uint8_t>(T)];
  }
  /// The size of the table data: every table's rows, unpadded.
  uint64_t getTablesSize() const { return TablesSize; }

private:
  std::array<uint32_t, NumTableIds> Rows;
  std::array<uint8_t, NumTableIds> RowSizes;
  std::array<uint64_t, NumTableIds> TableOffsets;
  uint64_t TablesSize = 0;
  uint8_t HeapFlags;
};

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_TABLESCHEMA_H
