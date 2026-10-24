//===- MetadataReader.cpp - ECMA-335 metadata reader ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The metadata root (II.24.2.1), the stream headers (II.24.2.2), the heaps
// (II.24.2.3-II.24.2.5) and the #~ stream (II.24.2.6), read lazily from the
// caller's bytes. create() checks only the headers and the extent of the
// table data; every cell is checked when it is read: heap offsets against
// their heap, simple and coded indexes against the row counts. The lookups
// build their indexes on first use in time linear in the table sizes (plus a
// sort), never trusting the Sorted bits or the row order.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/MetadataReader.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/CustomAttribute.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SHA1.h"
#include <algorithm>
#include <cstring>
#include <vector>

#define DEBUG_TYPE "cli-metadatareader"

using namespace llvm;
using namespace llvm::cli;
using namespace llvm::support;

static constexpr char ErrorPrefix[] = "invalid CLI metadata: ";

static Error mdError(const Twine &Msg) {
  return createStringError(errc::illegal_byte_sequence,
                           Twine(ErrorPrefix) + Msg);
}

/// \p V as 0x and \p Digits upper-case hex digits.
static std::string hex(uint64_t V, unsigned Digits = 8) {
  return "0x" + utohexstr(V, /*LowerCase=*/false, Digits);
}

/// The tables with a schema: 0x00 (Module) to 0x2C (GenericParamConstraint).
static constexpr unsigned NumSchemaTables =
    static_cast<unsigned>(TableId::GenericParamConstraint) + 1;
static_assert(NumSchemaTables == 0x2D, "ECMA-335 has 45 tables");

static constexpr StringLiteral CodedIndexNames[] = {
#define CLI_CODED_INDEX(Name, TagBits, ...) #Name,
#include "llvm/BinaryFormat/CLITables.def"
};

static StringRef getCodedIndexName(CodedIndexKind K) {
  return CodedIndexNames[static_cast<uint8_t>(K)];
}

/// The list columns, whose value may name the row after the last of their
/// target table (an empty run at the end).
static bool isListColumn(TableId T, unsigned Column) {
  switch (T) {
  case TableId::TypeDef:
    return Column == 4 || Column == 5; // FieldList, MethodList
  case TableId::MethodDef:
    return Column == 5; // ParamList
  case TableId::EventMap:
  case TableId::PropertyMap:
    return Column == 1; // EventList, PropertyList
  default:
    return false;
  }
}

/// The name of a table for messages ("TypeDef"), or its id in hex.
static std::string tableName(uint8_t T) {
  if (T < NumSchemaTables)
    return getTableName(static_cast<TableId>(T)).str();
  return "table " + hex(T, 2);
}

/// Converts UTF-16LE code units to UTF-8; false for a lone surrogate.
static bool convertUTF16LEToUTF8(ArrayRef<uint8_t> Bytes, std::string &Out) {
  // The bytes may be unaligned and the host big-endian, so the code units are
  // copied out. The low-level converter is used because, unlike
  // convertUTF16ToUTF8String, it keeps a leading U+FEFF and never byte-swaps
  // after a leading U+FFFE.
  size_t N = Bytes.size() / 2;
  SmallVector<UTF16, 0> Units(N);
  for (size_t I = 0; I != N; ++I)
    Units[I] = endian::read16le(Bytes.data() + 2 * I);
  Out.assign(N * UNI_MAX_UTF8_BYTES_PER_CODE_POINT, '\0');
  const UTF16 *Src = Units.data();
  UTF8 *Dst = reinterpret_cast<UTF8 *>(Out.data());
  // Strict conversion rejects a lone surrogate: sourceIllegal, or
  // sourceExhausted for a high surrogate in the last code unit.
  ConversionResult R = ConvertUTF16toUTF8(&Src, Units.data() + N, &Dst,
                                          Dst + Out.size(), strictConversion);
  assert(R != targetExhausted && "a code unit needs at most 3 UTF-8 bytes");
  if (R != conversionOK) {
    Out.clear();
    return false;
  }
  Out.resize(reinterpret_cast<char *>(Dst) - Out.data());
  return true;
}

//===----------------------------------------------------------------------===//
// Lookup indexes
//===----------------------------------------------------------------------===//

namespace {
/// An index built on first use. A failed build is remembered, so every later
/// call reports the same error without rebuilding.
template <typename T> struct LazyIndex {
  bool Built = false;
  std::optional<std::string> Failure;
  T Data;

  template <typename BuildFn> Error ensure(BuildFn Build) {
    if (!Built) {
      Built = true;
      if (Error E = Build(Data)) {
        Failure = toString(std::move(E));
        Data = T();
      }
    }
    if (Failure)
      return createStringError(errc::illegal_byte_sequence, *Failure);
    return Error::success();
  }
};
} // namespace

/// (key token, row) pairs sorted by key, rows in table order within a key.
using RowsByKey = std::vector<std::pair<uint32_t, uint32_t>>;

/// The run of \p Index whose key is \p Key.
static ArrayRef<std::pair<uint32_t, uint32_t>> findRows(const RowsByKey &Index,
                                                        uint32_t Key) {
  auto Range = std::equal_range(
      Index.begin(), Index.end(), std::make_pair(Key, uint32_t(0)),
      [](const std::pair<uint32_t, uint32_t> &L,
         const std::pair<uint32_t, uint32_t> &R) { return L.first < R.first; });
  return ArrayRef(Index).slice(Range.first - Index.begin(),
                               Range.second - Range.first);
}

struct MetadataReader::Indexes {
  /// The owning TypeDef row of every Field and MethodDef row (0: none).
  LazyIndex<std::vector<uint32_t>> FieldOwner, MethodOwner;
  /// The enclosing TypeDef row of every TypeDef row (0: not nested).
  LazyIndex<std::vector<uint32_t>> Enclosing;
  /// The ClassLayout, PropertyMap and FieldRVA row of every TypeDef or Field
  /// row (0: none).
  LazyIndex<std::vector<uint32_t>> ClassLayouts, PropertyMaps, FieldRVAs;
  /// The ImplMap row of a MemberForwarded token.
  LazyIndex<DenseMap<uint32_t, uint32_t>> ImplMaps;
  /// CustomAttribute rows by Parent, MethodSemantics rows by Association.
  LazyIndex<RowsByKey> CustomAttributes, Semantics;
};

MetadataReader::MetadataReader() = default;
MetadataReader::~MetadataReader() = default;

MetadataReader::Indexes &MetadataReader::getIndexes() const {
  if (!Idx)
    Idx = std::make_unique<Indexes>();
  return *Idx;
}

//===----------------------------------------------------------------------===//
// Creation: the root, the streams and the #~ header
//===----------------------------------------------------------------------===//

Expected<std::unique_ptr<MetadataReader>>
MetadataReader::create(ArrayRef<uint8_t> File) {
  Expected<PEImage> Image = PEImage::create(File);
  if (!Image)
    return Image.takeError();
  Expected<Cor20Header> Header = Image->getCLIHeader();
  if (!Header)
    return Header.takeError();
  uint32_t RVA = Header->MetaData.RVA, Size = Header->MetaData.Size;
  if (RVA == 0 || Size == 0)
    return mdError("the CLI header names no metadata (RVA " + hex(RVA) +
                   ", size " + hex(Size) + ")");
  Expected<ArrayRef<uint8_t>> Root = Image->getRVAData(RVA, Size);
  if (!Root)
    return Root.takeError();
  std::unique_ptr<MetadataReader> R(new MetadataReader());
  R->Image.emplace(std::move(*Image));
  if (Error E = R->parseRoot(*Root))
    return std::move(E);
  return std::move(R);
}

Expected<std::unique_ptr<MetadataReader>>
MetadataReader::createFromMetadata(ArrayRef<uint8_t> Root) {
  std::unique_ptr<MetadataReader> R(new MetadataReader());
  R->OwnedRoot.assign(Root.begin(), Root.end());
  if (Error E = R->parseRoot(R->OwnedRoot))
    return std::move(E);
  return std::move(R);
}

Error MetadataReader::parseRoot(ArrayRef<uint8_t> Root) {
  const uint8_t *B = Root.data();
  const uint64_t Size = Root.size();
  if (Size < 16)
    return mdError("the metadata root has " + Twine(Size) +
                   " bytes, fewer than its 16-byte header");
  if (endian::read32le(B) != MetadataSignature)
    return mdError("bad metadata root signature " + hex(endian::read32le(B)) +
                   " at offset 0x0 (expected 0x424A5342, 'BSJB')");
  uint64_t VersionLength = endian::read32le(B + 12);
  if (VersionLength % 4 != 0)
    return mdError("the version string length " + Twine(VersionLength) +
                   " at offset 0xC is not a multiple of 4");
  if (16 + VersionLength + 4 > Size)
    return mdError("the version string (" + Twine(VersionLength) +
                   " bytes at offset 0x10) runs past the end of the "
                   "metadata root (" +
                   Twine(Size) + " bytes)");
  // II.24.2.1: at most 255 bytes with the NUL, padded to 4.
  StringRef V(reinterpret_cast<const char *>(B + 16), VersionLength);
  V = V.take_until([](char C) { return C == 0; });
  if (V.size() > 254)
    return mdError("the version string at offset 0x10 has " + Twine(V.size()) +
                   " bytes (at most 255 with its NUL)");
  Version = V;

  uint64_t P = 16 + VersionLength;
  // Flags (reserved) at P, the stream count at P + 2.
  uint32_t NumStreams = endian::read16le(B + P + 2);
  P += 4;
  ArrayRef<uint8_t> TablesStream;
  bool HaveTables = false, HaveStrings = false, HaveUS = false,
       HaveGuid = false, HaveBlob = false;
  for (uint32_t S = 0; S != NumStreams; ++S) {
    uint64_t HeaderOffset = P;
    if (P + 8 > Size)
      return mdError("stream header " + Twine(S) + " at offset " + hex(P) +
                     " runs past the end of the metadata root (" + Twine(Size) +
                     " bytes)");
    uint64_t Offset = endian::read32le(B + P);
    uint64_t StreamSize = endian::read32le(B + P + 4);
    P += 8;
    // A name of at most 32 characters, NUL-terminated and padded to 4.
    uint64_t NameEnd = std::min(Size, P + 33);
    const uint8_t *Nul = std::find(B + P, B + NameEnd, 0);
    if (Nul == B + NameEnd)
      return mdError("the name of stream header " + Twine(S) + " at offset " +
                     hex(HeaderOffset) +
                     " is not NUL-terminated within 32 characters and the "
                     "metadata root");
    StringRef Name(reinterpret_cast<const char *>(B + P), Nul - (B + P));
    P = alignTo(P + Name.size() + 1, 4);
    if (P > Size)
      return mdError("the padding of stream name '" + Name + "' at offset " +
                     hex(HeaderOffset + 8) +
                     " runs past the end of the metadata root");
    if (Offset + StreamSize > Size)
      return mdError("stream '" + Name + "' (" + Twine(StreamSize) +
                     " bytes at offset " + hex(Offset) +
                     ") runs past the end of the metadata root (" +
                     Twine(Size) + " bytes)");
    if (Name == "#-")
      return createStringError(
          errc::not_supported,
          Twine(ErrorPrefix) + "uncompressed metadata (#-) is not supported");
    bool *Have = nullptr;
    ArrayRef<uint8_t> *Field = nullptr;
    if (Name == "#~") {
      Have = &HaveTables;
      Field = &TablesStream;
    } else if (Name == "#Strings") {
      Have = &HaveStrings;
      Field = &Strings;
    } else if (Name == "#US") {
      Have = &HaveUS;
      Field = &UserStrings;
    } else if (Name == "#GUID") {
      Have = &HaveGuid;
      Field = &Guids;
    } else if (Name == "#Blob") {
      Have = &HaveBlob;
      Field = &Blobs;
    }
    // Other streams (#Pdb, #JTD, ...) are ignored.
    if (!Field)
      continue;
    if (*Have)
      return mdError("duplicate stream '" + Name + "' at offset " +
                     hex(HeaderOffset));
    *Have = true;
    *Field = Root.slice(Offset, StreamSize);
  }
  if (!HaveTables)
    return mdError("the metadata root has no #~ stream");
  return parseTables(TablesStream);
}

Error MetadataReader::parseTables(ArrayRef<uint8_t> Stream) {
  const uint8_t *B = Stream.data();
  const uint64_t Size = Stream.size();
  if (Size < 24)
    return mdError("the #~ stream has " + Twine(Size) +
                   " bytes, fewer than its 24-byte header");
  uint8_t HeapSizes = B[6];
  uint64_t Valid = endian::read64le(B + 8);
  // The Sorted vector (B + 16) is not used: lookups index the tables.
  if (uint64_t Unknown = Valid >> NumSchemaTables)
    return mdError("the #~ Valid vector names table " +
                   hex(NumSchemaTables + countr_zero(Unknown), 2) +
                   ", which has no schema");
  std::array<uint32_t, NumTableIds> Rows{};
  uint64_t P = 24;
  for (unsigned T = 0; T != NumSchemaTables; ++T) {
    if (!(Valid & (uint64_t(1) << T)))
      continue;
    if (P + 4 > Size)
      return mdError("the #~ row counts run past the end of the stream (" +
                     Twine(Size) + " bytes)");
    uint32_t N = endian::read32le(B + P);
    if (N > MaxTableRows)
      return mdError("table " + tableName(T) + " has " + Twine(N) +
                     " rows (at most " + Twine(MaxTableRows) +
                     ") at #~ offset " + hex(P));
    Rows[T] = N;
    P += 4;
  }
  if (HeapSizes & HEAP_EXTRA_DATA) {
    if (P + 4 > Size)
      return mdError("the #~ extra data field at offset " + hex(P) +
                     " runs past the end of the stream");
    P += 4;
  }
  Sizes.emplace(Rows, HeapSizes);
  if (P + Sizes->getTablesSize() > Size)
    return mdError("the table data (" + Twine(Sizes->getTablesSize()) +
                   " bytes at #~ offset " + hex(P) +
                   ") runs past the end of the #~ stream (" + Twine(Size) +
                   " bytes)");
  // Ptr tables belong to uncompressed metadata; the lists of #~ index the
  // member tables directly.
  for (TableId T : {TableId::FieldPtr, TableId::MethodPtr, TableId::ParamPtr,
                    TableId::EventPtr, TableId::PropertyPtr})
    if (uint32_t N = Rows[static_cast<uint8_t>(T)])
      return mdError("table " + getTableName(T) + " has " + Twine(N) +
                     " rows: Ptr tables belong to uncompressed metadata "
                     "(#-), which is not supported");
  for (unsigned I = 0; I != NumSchemaTables; ++I) {
    TableId T = static_cast<TableId>(I);
    TableView &V = Tables[I];
    V.Rows = Rows[I];
    V.RowSize = Sizes->getRowSize(T);
    V.Data = Stream.slice(P + Sizes->getTableOffset(T),
                          uint64_t(V.Rows) * V.RowSize);
    unsigned NumColumns = getTableColumns(T).size();
    assert(NumColumns <= MaxColumns && "a table with too many columns");
    V.NumColumns = NumColumns;
    for (unsigned C = 0; C != NumColumns; ++C) {
      V.Offsets[C] = Sizes->getColumnOffset(T, C);
      V.Widths[C] = Sizes->getColumnSize(T, C);
    }
  }
  return Error::success();
}

//===----------------------------------------------------------------------===//
// Heaps
//===----------------------------------------------------------------------===//

uint32_t MetadataReader::getRowCount(TableId T) const {
  unsigned I = static_cast<uint8_t>(T);
  return I < NumSchemaTables ? Tables[I].Rows : 0;
}

uint32_t MetadataReader::getHeapSize(StringRef StreamName) const {
  if (StreamName == "#Strings")
    return Strings.size();
  if (StreamName == "#US")
    return UserStrings.size();
  if (StreamName == "#GUID")
    return Guids.size();
  if (StreamName == "#Blob")
    return Blobs.size();
  return 0;
}

Expected<StringRef> MetadataReader::getString(uint32_t Offset) const {
  if (Offset == 0)
    return StringRef();
  if (Offset >= Strings.size())
    return mdError("#Strings offset " + hex(Offset) + " is outside the heap (" +
                   hex(Strings.size()) + " bytes)");
  ArrayRef<uint8_t> Rest = Strings.drop_front(Offset);
  const uint8_t *Nul = std::find(Rest.begin(), Rest.end(), 0);
  if (Nul == Rest.end())
    return mdError("the string at #Strings offset " + hex(Offset) +
                   " is not NUL-terminated inside the heap");
  return StringRef(reinterpret_cast<const char *>(Rest.data()),
                   Nul - Rest.begin());
}

/// The blob at \p Offset of \p Heap (#Blob or #US): a compressed length and
/// that many bytes.
static Expected<ArrayRef<uint8_t>>
readHeapBlob(ArrayRef<uint8_t> Heap, uint32_t Offset, StringRef HeapName) {
  if (Offset == 0 && Heap.empty())
    return ArrayRef<uint8_t>();
  if (Offset >= Heap.size())
    return mdError(HeapName + " offset " + hex(Offset) +
                   " is outside the heap (" + hex(Heap.size()) + " bytes)");
  ArrayRef<uint8_t> Rest = Heap.drop_front(Offset);
  std::optional<uint32_t> Length = decodeCompressedUInt(Rest);
  if (!Length)
    return mdError("bad compressed length (lead byte " + hex(Heap[Offset], 2) +
                   ") at " + HeapName + " offset " + hex(Offset));
  if (*Length > Rest.size())
    return mdError("the " + Twine(*Length) + "-byte entry at " + HeapName +
                   " offset " + hex(Offset) +
                   " runs past the end of the heap (" + hex(Heap.size()) +
                   " bytes)");
  return Rest.take_front(*Length);
}

Expected<ArrayRef<uint8_t>> MetadataReader::getBlob(uint32_t Offset) const {
  return readHeapBlob(Blobs, Offset, "#Blob");
}

Expected<ArrayRef<uint8_t>>
MetadataReader::getUserStringUTF16(uint32_t Offset) const {
  Expected<ArrayRef<uint8_t>> Entry = readHeapBlob(UserStrings, Offset, "#US");
  if (!Entry)
    return Entry.takeError();
  // 2n bytes of UTF-16 and the trailing byte (II.24.2.4).
  return Entry->take_front(Entry->size() & ~size_t(1));
}

Expected<std::string> MetadataReader::getUserString(uint32_t Offset) const {
  Expected<ArrayRef<uint8_t>> UTF16 = getUserStringUTF16(Offset);
  if (!UTF16)
    return UTF16.takeError();
  std::string UTF8;
  if (!convertUTF16LEToUTF8(*UTF16, UTF8))
    return mdError("the user string at #US offset " + hex(Offset) +
                   " has a lone surrogate (not valid UTF-16)");
  return std::move(UTF8);
}

Expected<std::array<uint8_t, 16>>
MetadataReader::getGuid(uint32_t Index) const {
  if (Index == 0)
    return mdError("#GUID index 0 is nil");
  if (uint64_t(Index) * 16 > Guids.size())
    return mdError("#GUID index " + Twine(Index) + " is outside the heap (" +
                   Twine(Guids.size() / 16) + " entries)");
  std::array<uint8_t, 16> G;
  std::memcpy(G.data(), Guids.data() + uint64_t(Index - 1) * 16, 16);
  return G;
}

//===----------------------------------------------------------------------===//
// Cells and rows
//===----------------------------------------------------------------------===//

Error MetadataReader::checkRow(TableId T, uint32_t Row) const {
  unsigned I = static_cast<uint8_t>(T);
  if (I >= NumSchemaTables)
    return mdError("table id " + hex(I, 2) + " has no schema");
  if (Row == 0 || Row > Tables[I].Rows)
    return mdError(getTableName(T) + " row " + Twine(Row) +
                   " does not exist (" + Twine(Tables[I].Rows) + " rows)");
  return Error::success();
}

Error MetadataReader::checkToken(uint32_t Token, ArrayRef<TableId> Allowed,
                                 StringRef What) const {
  uint8_t Type = getTokenType(Token);
  if (none_of(Allowed,
              [&](TableId T) { return static_cast<uint8_t>(T) == Type; }))
    return mdError("token " + hex(Token) + " is not " + What);
  uint32_t Row = getTokenRow(Token);
  if (Row == 0 || Row > Tables[Type].Rows)
    return mdError("token " + hex(Token) + " names " + tableName(Type) +
                   " row " + Twine(Row) + ", which does not exist (" +
                   Twine(Tables[Type].Rows) + " rows)");
  return Error::success();
}

Expected<uint32_t> MetadataReader::getColumnValue(TableId T, uint32_t Row,
                                                  unsigned Column) const {
  if (Error E = checkRow(T, Row))
    return std::move(E);
  const TableView &V = Tables[static_cast<uint8_t>(T)];
  if (Column >= V.NumColumns)
    return mdError(getTableName(T) + " has no column " + Twine(Column) + " (" +
                   Twine(unsigned(V.NumColumns)) + " columns)");
  const uint8_t *P =
      V.Data.data() + uint64_t(Row - 1) * V.RowSize + V.Offsets[Column];
  uint32_t Raw =
      V.Widths[Column] == 2 ? endian::read16le(P) : endian::read32le(P);
  const ColumnInfo &C = getTableColumns(T)[Column];
  auto Where = [&]() {
    return (getTableName(T) + " row " + Twine(Row) + " column " + C.Name).str();
  };
  switch (C.Kind) {
  case ColumnKind::U16:
  case ColumnKind::U32:
    return Raw;
  case ColumnKind::StringIdx:
    if (Raw != 0 && Raw >= Strings.size())
      return mdError(Where() + ": #Strings offset " + hex(Raw) +
                     " is outside the heap (" + hex(Strings.size()) +
                     " bytes)");
    return Raw;
  case ColumnKind::BlobIdx:
    if (Raw != 0 && Raw >= Blobs.size())
      return mdError(Where() + ": #Blob offset " + hex(Raw) +
                     " is outside the heap (" + hex(Blobs.size()) + " bytes)");
    return Raw;
  case ColumnKind::GuidIdx:
    if (uint64_t(Raw) * 16 > Guids.size())
      return mdError(Where() + ": #GUID index " + Twine(Raw) +
                     " is outside the heap (" + Twine(Guids.size() / 16) +
                     " entries)");
    return Raw;
  case ColumnKind::TableIdx: {
    uint8_t Target = C.Target;
    uint64_t Limit =
        uint64_t(Tables[Target].Rows) + (isListColumn(T, Column) ? 1 : 0);
    if (Raw > Limit || Raw > MaxTableRows)
      return mdError(Where() + ": " + tableName(Target) + " row " + Twine(Raw) +
                     " does not exist (" + Twine(Tables[Target].Rows) +
                     " rows)");
    return Raw == 0 ? 0 : makeToken(static_cast<TableId>(Target), Raw);
  }
  case ColumnKind::CodedIdx: {
    CodedIndexKind K = static_cast<CodedIndexKind>(C.Target);
    std::optional<uint32_t> Token = decodeCodedIndex(K, Raw);
    if (!Token)
      return mdError(Where() + ": invalid " + getCodedIndexName(K) +
                     " coded index " + hex(Raw));
    if (*Token == 0)
      return 0;
    uint8_t Type = getTokenType(*Token);
    if (getTokenRow(*Token) > Tables[Type].Rows)
      return mdError(Where() + ": " + tableName(Type) + " row " +
                     Twine(getTokenRow(*Token)) + " does not exist (" +
                     Twine(Tables[Type].Rows) + " rows)");
    return *Token;
  }
  }
  llvm_unreachable("unknown column kind");
}

Expected<StringRef> MetadataReader::getStringColumn(TableId T, uint32_t Row,
                                                    unsigned Column) const {
  Expected<uint32_t> Offset = getColumnValue(T, Row, Column);
  if (!Offset)
    return Offset.takeError();
  return getString(*Offset);
}

Expected<std::pair<uint32_t, uint32_t>>
MetadataReader::getListRange(TableId T, uint32_t Row, unsigned Column,
                             TableId Target) const {
  Expected<uint32_t> First = getColumnValue(T, Row, Column);
  if (!First)
    return First.takeError();
  uint32_t Begin = getTokenRow(*First);
  if (Begin == 0)
    return mdError(getTableName(T) + " row " + Twine(Row) + ": its " +
                   getTableColumns(T)[Column].Name +
                   " is 0 (a run starts at row 1 or later)");
  uint32_t End = getRowCount(Target) + 1;
  if (Row < getRowCount(T)) {
    Expected<uint32_t> Next = getColumnValue(T, Row + 1, Column);
    if (!Next)
      return Next.takeError();
    End = getTokenRow(*Next);
  }
  if (End < Begin)
    return mdError(getTableName(T) + " row " + Twine(Row) + ": its " +
                   getTableColumns(T)[Column].Name + " run starts at " +
                   getTableName(Target) + " row " + Twine(Begin) +
                   " after the next run's start, row " + Twine(End));
  return std::make_pair(Begin, End);
}

/// The values of every column of one row.
using Cells = std::array<uint32_t, MetadataReader::MaxColumns>;

/// Reads every column of row \p Row of table \p T into \p Out.
static Error readCells(const MetadataReader &R, TableId T, uint32_t Row,
                       Cells &Out) {
  for (unsigned C = 0, E = getTableColumns(T).size(); C != E; ++C) {
    Expected<uint32_t> V = R.getColumnValue(T, Row, C);
    if (!V)
      return V.takeError();
    Out[C] = *V;
  }
  return Error::success();
}

/// Sets \p Out to the #Strings entry at \p Offset.
static Error readString(const MetadataReader &R, uint32_t Offset,
                        StringRef &Out) {
  Expected<StringRef> S = R.getString(Offset);
  if (!S)
    return S.takeError();
  Out = *S;
  return Error::success();
}

Expected<ModuleRow> MetadataReader::getModule() const {
  if (getRowCount(TableId::Module) == 0)
    return mdError("the Module table is empty");
  Cells C;
  if (Error E = readCells(*this, TableId::Module, 1, C))
    return std::move(E);
  ModuleRow R;
  R.Generation = C[0];
  if (Error E = readString(*this, C[1], R.Name))
    return std::move(E);
  R.Mvid = C[2];
  return R;
}

Expected<AssemblyRow> MetadataReader::getAssembly() const {
  if (getRowCount(TableId::Assembly) == 0)
    return mdError("the module has no Assembly row");
  Cells C;
  if (Error E = readCells(*this, TableId::Assembly, 1, C))
    return std::move(E);
  AssemblyRow R;
  R.HashAlg = static_cast<AssemblyHashAlgorithm>(C[0]);
  R.Version = {uint16_t(C[1]), uint16_t(C[2]), uint16_t(C[3]), uint16_t(C[4])};
  R.Flags = static_cast<AssemblyFlags>(C[5]);
  R.PublicKey = C[6];
  if (Error E = readString(*this, C[7], R.Name))
    return std::move(E);
  if (Error E = readString(*this, C[8], R.Culture))
    return std::move(E);
  return R;
}

Expected<TypeRefRow> MetadataReader::getTypeRef(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::TypeRef, Row, C))
    return std::move(E);
  TypeRefRow R;
  R.ResolutionScope = C[0];
  if (Error E = readString(*this, C[1], R.Name))
    return std::move(E);
  if (Error E = readString(*this, C[2], R.Namespace))
    return std::move(E);
  return R;
}

Expected<TypeDefRow> MetadataReader::getTypeDef(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::TypeDef, Row, C))
    return std::move(E);
  TypeDefRow R;
  R.Flags = static_cast<TypeAttributes>(C[0]);
  if (Error E = readString(*this, C[1], R.Name))
    return std::move(E);
  if (Error E = readString(*this, C[2], R.Namespace))
    return std::move(E);
  R.Extends = C[3];
  Expected<std::pair<uint32_t, uint32_t>> Fields =
      getListRange(TableId::TypeDef, Row, 4, TableId::Field);
  if (!Fields)
    return Fields.takeError();
  Expected<std::pair<uint32_t, uint32_t>> Methods =
      getListRange(TableId::TypeDef, Row, 5, TableId::MethodDef);
  if (!Methods)
    return Methods.takeError();
  std::tie(R.FieldBegin, R.FieldEnd) = *Fields;
  std::tie(R.MethodBegin, R.MethodEnd) = *Methods;
  return R;
}

Expected<FieldRow> MetadataReader::getField(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::Field, Row, C))
    return std::move(E);
  FieldRow R;
  R.Flags = static_cast<FieldAttributes>(C[0]);
  if (Error E = readString(*this, C[1], R.Name))
    return std::move(E);
  R.Signature = C[2];
  return R;
}

Expected<MethodDefRow> MetadataReader::getMethodDef(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::MethodDef, Row, C))
    return std::move(E);
  MethodDefRow R;
  R.RVA = C[0];
  R.ImplFlags = static_cast<MethodImplAttributes>(C[1]);
  R.Flags = static_cast<MethodAttributes>(C[2]);
  if (Error E = readString(*this, C[3], R.Name))
    return std::move(E);
  R.Signature = C[4];
  Expected<std::pair<uint32_t, uint32_t>> Params =
      getListRange(TableId::MethodDef, Row, 5, TableId::Param);
  if (!Params)
    return Params.takeError();
  std::tie(R.ParamBegin, R.ParamEnd) = *Params;
  return R;
}

Expected<ParamRow> MetadataReader::getParam(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::Param, Row, C))
    return std::move(E);
  ParamRow R;
  R.Flags = static_cast<ParamAttributes>(C[0]);
  R.Sequence = C[1];
  if (Error E = readString(*this, C[2], R.Name))
    return std::move(E);
  return R;
}

Expected<MemberRefRow> MetadataReader::getMemberRef(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::MemberRef, Row, C))
    return std::move(E);
  MemberRefRow R;
  R.Parent = C[0];
  if (Error E = readString(*this, C[1], R.Name))
    return std::move(E);
  R.Signature = C[2];
  return R;
}

Expected<ConstantRow> MetadataReader::getConstant(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::Constant, Row, C))
    return std::move(E);
  ConstantRow R;
  R.Type = C[0] & 0xFF;
  R.Parent = C[1];
  R.Value = C[2];
  return R;
}

Expected<CustomAttributeRow>
MetadataReader::getCustomAttribute(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::CustomAttribute, Row, C))
    return std::move(E);
  CustomAttributeRow R;
  R.Parent = C[0];
  R.Constructor = C[1];
  R.Value = C[2];
  return R;
}

Expected<ClassLayoutRow> MetadataReader::getClassLayout(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::ClassLayout, Row, C))
    return std::move(E);
  ClassLayoutRow R;
  R.PackingSize = C[0];
  R.ClassSize = C[1];
  R.Parent = C[2];
  return R;
}

Expected<FieldLayoutRow> MetadataReader::getFieldLayout(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::FieldLayout, Row, C))
    return std::move(E);
  FieldLayoutRow R;
  R.Offset = C[0];
  R.Field = C[1];
  return R;
}

Expected<StandAloneSigRow>
MetadataReader::getStandAloneSig(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::StandAloneSig, Row, C))
    return std::move(E);
  StandAloneSigRow R;
  R.Signature = C[0];
  return R;
}

Expected<PropertyRow> MetadataReader::getProperty(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::Property, Row, C))
    return std::move(E);
  PropertyRow R;
  R.Flags = static_cast<PropertyAttributes>(C[0]);
  if (Error E = readString(*this, C[1], R.Name))
    return std::move(E);
  R.Signature = C[2];
  return R;
}

Expected<MethodSemanticsRow>
MetadataReader::getMethodSemanticsRow(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::MethodSemantics, Row, C))
    return std::move(E);
  MethodSemanticsRow R;
  R.Semantics = static_cast<MethodSemanticsAttributes>(C[0]);
  R.Method = C[1];
  R.Association = C[2];
  return R;
}

Expected<ModuleRefRow> MetadataReader::getModuleRef(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::ModuleRef, Row, C))
    return std::move(E);
  ModuleRefRow R;
  if (Error E = readString(*this, C[0], R.Name))
    return std::move(E);
  return R;
}

Expected<TypeSpecRow> MetadataReader::getTypeSpec(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::TypeSpec, Row, C))
    return std::move(E);
  TypeSpecRow R;
  R.Signature = C[0];
  return R;
}

Expected<ImplMapRow> MetadataReader::getImplMap(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::ImplMap, Row, C))
    return std::move(E);
  ImplMapRow R;
  R.Flags = static_cast<PInvokeAttributes>(C[0]);
  R.MemberForwarded = C[1];
  if (Error E = readString(*this, C[2], R.ImportName))
    return std::move(E);
  R.ImportScope = C[3];
  return R;
}

Expected<FieldRVARow> MetadataReader::getFieldRVA(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::FieldRVA, Row, C))
    return std::move(E);
  FieldRVARow R;
  R.RVA = C[0];
  R.Field = C[1];
  return R;
}

Expected<AssemblyRefRow> MetadataReader::getAssemblyRef(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::AssemblyRef, Row, C))
    return std::move(E);
  AssemblyRefRow R;
  R.Version = {uint16_t(C[0]), uint16_t(C[1]), uint16_t(C[2]), uint16_t(C[3])};
  R.Flags = static_cast<AssemblyFlags>(C[4]);
  R.PublicKeyOrToken = C[5];
  if (Error E = readString(*this, C[6], R.Name))
    return std::move(E);
  if (Error E = readString(*this, C[7], R.Culture))
    return std::move(E);
  R.HashValue = C[8];
  return R;
}

Expected<ExportedTypeRow> MetadataReader::getExportedType(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::ExportedType, Row, C))
    return std::move(E);
  ExportedTypeRow R;
  R.Flags = static_cast<TypeAttributes>(C[0]);
  R.TypeDefId = C[1];
  if (Error E = readString(*this, C[2], R.Name))
    return std::move(E);
  if (Error E = readString(*this, C[3], R.Namespace))
    return std::move(E);
  R.Implementation = C[4];
  return R;
}

Expected<ManifestResourceRow>
MetadataReader::getManifestResource(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::ManifestResource, Row, C))
    return std::move(E);
  ManifestResourceRow R;
  R.Offset = C[0];
  R.Flags = static_cast<ManifestResourceAttributes>(C[1]);
  if (Error E = readString(*this, C[2], R.Name))
    return std::move(E);
  R.Implementation = C[3];
  return R;
}

Expected<NestedClassRow> MetadataReader::getNestedClass(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::NestedClass, Row, C))
    return std::move(E);
  NestedClassRow R;
  R.NestedClass = C[0];
  R.EnclosingClass = C[1];
  return R;
}

Expected<GenericParamRow> MetadataReader::getGenericParam(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::GenericParam, Row, C))
    return std::move(E);
  GenericParamRow R;
  R.Number = C[0];
  R.Flags = static_cast<GenericParamAttributes>(C[1]);
  R.Owner = C[2];
  if (Error E = readString(*this, C[3], R.Name))
    return std::move(E);
  return R;
}

Expected<MethodSpecRow> MetadataReader::getMethodSpec(uint32_t Row) const {
  Cells C;
  if (Error E = readCells(*this, TableId::MethodSpec, Row, C))
    return std::move(E);
  MethodSpecRow R;
  R.Method = C[0];
  R.Instantiation = C[1];
  return R;
}

//===----------------------------------------------------------------------===//
// Lookups
//===----------------------------------------------------------------------===//

/// Builds a row-indexed map: Out[key row] = the first row of \p T whose
/// column \p Column (a simple or coded index into \p KeyTable) names it.
static Error buildRowMap(const MetadataReader &R, TableId T, unsigned Column,
                         TableId KeyTable, std::vector<uint32_t> &Out) {
  Out.assign(R.getRowCount(KeyTable) + 1, 0);
  for (uint32_t Row = 1, N = R.getRowCount(T); Row <= N; ++Row) {
    Expected<uint32_t> Key = R.getColumnValue(T, Row, Column);
    if (!Key)
      return Key.takeError();
    if (getTokenType(*Key) != static_cast<uint8_t>(KeyTable) ||
        getTokenRow(*Key) == 0)
      return mdError(getTableName(T) + " row " + Twine(Row) + ": its " +
                     getTableColumns(T)[Column].Name + " " + hex(*Key) +
                     " is not a " + getTableName(KeyTable) + " row");
    uint32_t &Slot = Out[getTokenRow(*Key)];
    if (Slot == 0)
      Slot = Row;
  }
  return Error::success();
}

/// Builds (key token, row) pairs of every row of \p T, keyed by column
/// \p Column, sorted by key with rows in table order.
static Error buildRowsByKey(const MetadataReader &R, TableId T, unsigned Column,
                            RowsByKey &Out) {
  uint32_t N = R.getRowCount(T);
  Out.clear();
  Out.reserve(N);
  for (uint32_t Row = 1; Row <= N; ++Row) {
    Expected<uint32_t> Key = R.getColumnValue(T, Row, Column);
    if (!Key)
      return Key.takeError();
    Out.push_back({*Key, Row});
  }
  // Rows are pushed in increasing order, so sorting the pairs keeps them in
  // table order within a key.
  llvm::sort(Out);
  return Error::success();
}

Expected<uint32_t>
MetadataReader::getOwnerTypeDef(uint32_t FieldOrMethodToken) const {
  if (Error E =
          checkToken(FieldOrMethodToken, {TableId::Field, TableId::MethodDef},
                     "a Field or MethodDef token"))
    return std::move(E);
  bool IsField =
      getTokenType(FieldOrMethodToken) == static_cast<uint8_t>(TableId::Field);
  Indexes &I = getIndexes();
  LazyIndex<std::vector<uint32_t>> &Owners =
      IsField ? I.FieldOwner : I.MethodOwner;
  if (Error E = Owners.ensure([&](std::vector<uint32_t> &Out) -> Error {
        // Out[member row] = the TypeDef row whose run holds it. The runs are
        // consecutive (getListRange checks that each ends where the next
        // starts or before), so this is linear in both tables.
        TableId Member = IsField ? TableId::Field : TableId::MethodDef;
        Out.assign(getRowCount(Member) + 1, 0);
        for (uint32_t T = 1, N = getRowCount(TableId::TypeDef); T <= N; ++T) {
          Expected<std::pair<uint32_t, uint32_t>> Run =
              getListRange(TableId::TypeDef, T, IsField ? 4 : 5, Member);
          if (!Run)
            return Run.takeError();
          for (uint32_t M = Run->first; M < Run->second; ++M)
            Out[M] = T;
        }
        return Error::success();
      }))
    return std::move(E);
  uint32_t Owner = Owners.Data[getTokenRow(FieldOrMethodToken)];
  if (Owner == 0)
    return mdError((IsField ? "field " : "method ") + hex(FieldOrMethodToken) +
                   " is in no TypeDef's " + (IsField ? "field" : "method") +
                   " run");
  return makeToken(TableId::TypeDef, Owner);
}

Expected<uint32_t>
MetadataReader::getEnclosingType(uint32_t TypeDefToken) const {
  if (Error E = checkToken(TypeDefToken, {TableId::TypeDef}, "a TypeDef token"))
    return std::move(E);
  Indexes &I = getIndexes();
  if (Error E = I.Enclosing.ensure([&](std::vector<uint32_t> &Out) -> Error {
        // Out[nested row] = enclosing row; the first NestedClass row wins.
        Out.assign(getRowCount(TableId::TypeDef) + 1, 0);
        for (uint32_t Row = 1, N = getRowCount(TableId::NestedClass); Row <= N;
             ++Row) {
          Expected<NestedClassRow> NC = getNestedClass(Row);
          if (!NC)
            return NC.takeError();
          if (NC->NestedClass == 0 || NC->EnclosingClass == 0)
            return mdError("NestedClass row " + Twine(Row) +
                           " names a nil TypeDef");
          uint32_t &Slot = Out[getTokenRow(NC->NestedClass)];
          if (Slot == 0)
            Slot = getTokenRow(NC->EnclosingClass);
        }
        return Error::success();
      }))
    return std::move(E);
  uint32_t Enclosing = I.Enclosing.Data[getTokenRow(TypeDefToken)];
  return Enclosing ? makeToken(TableId::TypeDef, Enclosing) : 0;
}

Expected<SmallVector<uint32_t, 4>>
MetadataReader::getCustomAttributes(uint32_t ParentToken) const {
  SmallVector<TableId, 24> Parents;
  for (std::optional<TableId> T :
       getCodedIndexTables(CodedIndexKind::HasCustomAttribute))
    if (T)
      Parents.push_back(*T);
  if (Error E = checkToken(ParentToken, Parents,
                           "a token that can own custom attributes"))
    return std::move(E);
  Indexes &I = getIndexes();
  if (Error E = I.CustomAttributes.ensure([&](RowsByKey &Out) {
        return buildRowsByKey(*this, TableId::CustomAttribute, 0, Out);
      }))
    return std::move(E);
  SmallVector<uint32_t, 4> Rows;
  for (const auto &[Key, Row] : findRows(I.CustomAttributes.Data, ParentToken))
    Rows.push_back(Row);
  return std::move(Rows);
}

Expected<std::optional<ImplMapRow>>
MetadataReader::findImplMap(uint32_t MethodDefToken) const {
  if (Error E = checkToken(MethodDefToken, {TableId::MethodDef, TableId::Field},
                           "a MethodDef or Field token"))
    return std::move(E);
  Indexes &I = getIndexes();
  if (Error E =
          I.ImplMaps.ensure([&](DenseMap<uint32_t, uint32_t> &Out) -> Error {
            for (uint32_t Row = 1, N = getRowCount(TableId::ImplMap); Row <= N;
                 ++Row) {
              Expected<uint32_t> Member =
                  getColumnValue(TableId::ImplMap, Row, 1);
              if (!Member)
                return Member.takeError();
              if (*Member == 0)
                return mdError("ImplMap row " + Twine(Row) +
                               " forwards no member");
              Out.try_emplace(*Member, Row);
            }
            return Error::success();
          }))
    return std::move(E);
  auto It = I.ImplMaps.Data.find(MethodDefToken);
  if (It == I.ImplMaps.Data.end())
    return std::nullopt;
  Expected<ImplMapRow> Row = getImplMap(It->second);
  if (!Row)
    return Row.takeError();
  return std::optional<ImplMapRow>(*Row);
}

Expected<std::optional<FieldRVARow>>
MetadataReader::findFieldRVA(uint32_t FieldToken) const {
  if (Error E = checkToken(FieldToken, {TableId::Field}, "a Field token"))
    return std::move(E);
  Indexes &I = getIndexes();
  if (Error E = I.FieldRVAs.ensure([&](std::vector<uint32_t> &Out) {
        return buildRowMap(*this, TableId::FieldRVA, 1, TableId::Field, Out);
      }))
    return std::move(E);
  uint32_t Row = I.FieldRVAs.Data[getTokenRow(FieldToken)];
  if (Row == 0)
    return std::nullopt;
  Expected<FieldRVARow> R = getFieldRVA(Row);
  if (!R)
    return R.takeError();
  return std::optional<FieldRVARow>(*R);
}

Expected<std::optional<ClassLayoutRow>>
MetadataReader::findClassLayout(uint32_t TypeDefToken) const {
  if (Error E = checkToken(TypeDefToken, {TableId::TypeDef}, "a TypeDef token"))
    return std::move(E);
  Indexes &I = getIndexes();
  if (Error E = I.ClassLayouts.ensure([&](std::vector<uint32_t> &Out) {
        return buildRowMap(*this, TableId::ClassLayout, 2, TableId::TypeDef,
                           Out);
      }))
    return std::move(E);
  uint32_t Row = I.ClassLayouts.Data[getTokenRow(TypeDefToken)];
  if (Row == 0)
    return std::nullopt;
  Expected<ClassLayoutRow> R = getClassLayout(Row);
  if (!R)
    return R.takeError();
  return std::optional<ClassLayoutRow>(*R);
}

Expected<std::pair<uint32_t, uint32_t>>
MetadataReader::getPropertyRange(uint32_t TypeDefToken) const {
  if (Error E = checkToken(TypeDefToken, {TableId::TypeDef}, "a TypeDef token"))
    return std::move(E);
  Indexes &I = getIndexes();
  if (Error E = I.PropertyMaps.ensure([&](std::vector<uint32_t> &Out) {
        return buildRowMap(*this, TableId::PropertyMap, 0, TableId::TypeDef,
                           Out);
      }))
    return std::move(E);
  uint32_t Row = I.PropertyMaps.Data[getTokenRow(TypeDefToken)];
  if (Row == 0)
    return std::make_pair(uint32_t(0), uint32_t(0));
  return getListRange(TableId::PropertyMap, Row, 1, TableId::Property);
}

Expected<SmallVector<MethodSemanticsRow, 2>>
MetadataReader::getMethodSemantics(uint32_t PropertyToken) const {
  if (Error E = checkToken(PropertyToken, {TableId::Property, TableId::Event},
                           "a Property or Event token"))
    return std::move(E);
  Indexes &I = getIndexes();
  if (Error E = I.Semantics.ensure([&](RowsByKey &Out) {
        return buildRowsByKey(*this, TableId::MethodSemantics, 2, Out);
      }))
    return std::move(E);
  SmallVector<MethodSemanticsRow, 2> Rows;
  for (const auto &[Key, Row] : findRows(I.Semantics.Data, PropertyToken)) {
    Expected<MethodSemanticsRow> R = getMethodSemanticsRow(Row);
    if (!R)
      return R.takeError();
    Rows.push_back(*R);
  }
  return std::move(Rows);
}

/// The namespace and name of a TypeDef or TypeRef token.
static Expected<std::pair<StringRef, StringRef>>
getTypeName(const MetadataReader &R, uint32_t Token) {
  TableId T = static_cast<TableId>(getTokenType(Token));
  uint32_t Row = getTokenRow(Token);
  // TypeDef: TypeName 1, TypeNamespace 2; TypeRef: TypeName 1,
  // TypeNamespace 2.
  Expected<uint32_t> Name = R.getColumnValue(T, Row, 1);
  if (!Name)
    return Name.takeError();
  Expected<uint32_t> NS = R.getColumnValue(T, Row, 2);
  if (!NS)
    return NS.takeError();
  Expected<StringRef> NameStr = R.getString(*Name);
  if (!NameStr)
    return NameStr.takeError();
  Expected<StringRef> NSStr = R.getString(*NS);
  if (!NSStr)
    return NSStr.takeError();
  return std::make_pair(*NSStr, *NameStr);
}

Expected<std::string> MetadataReader::getTypeFullName(uint32_t Token) const {
  if (Error E = checkToken(Token, {TableId::TypeDef, TableId::TypeRef},
                           "a TypeDef or TypeRef token"))
    return std::move(E);
  bool IsDef = getTokenType(Token) == static_cast<uint8_t>(TableId::TypeDef);
  TableId T = IsDef ? TableId::TypeDef : TableId::TypeRef;
  // The names from the innermost type outwards. A chain longer than the
  // table is a cycle.
  SmallVector<std::pair<StringRef, StringRef>, 2> Parts;
  uint32_t Current = Token;
  while (Current != 0) {
    if (Parts.size() > getRowCount(T))
      return mdError("the enclosing " + getTableName(T) + "s of " + hex(Token) +
                     " form a cycle");
    Expected<std::pair<StringRef, StringRef>> Name =
        getTypeName(*this, Current);
    if (!Name)
      return Name.takeError();
    Parts.push_back(*Name);
    if (IsDef) {
      Expected<uint32_t> Enclosing = getEnclosingType(Current);
      if (!Enclosing)
        return Enclosing.takeError();
      Current = *Enclosing;
      continue;
    }
    // A TypeRef whose ResolutionScope is a TypeRef is nested in it.
    Expected<uint32_t> Scope =
        getColumnValue(TableId::TypeRef, getTokenRow(Current), 0);
    if (!Scope)
      return Scope.takeError();
    Current = getTokenType(*Scope) == static_cast<uint8_t>(TableId::TypeRef)
                  ? *Scope
                  : 0;
  }
  std::string Out;
  for (const auto &[NS, Name] : reverse(Parts)) {
    if (!Out.empty())
      Out += '/';
    if (!NS.empty()) {
      Out += NS;
      Out += '.';
    }
    Out += Name;
  }
  return std::move(Out);
}

Expected<SmallVector<ArrayRef<uint8_t>, 1>>
MetadataReader::getByteArrayAttributes(uint32_t ParentToken,
                                       StringRef Namespace,
                                       StringRef Name) const {
  Expected<SmallVector<uint32_t, 4>> Rows = getCustomAttributes(ParentToken);
  if (!Rows)
    return Rows.takeError();
  SmallVector<ArrayRef<uint8_t>, 1> Payloads;
  for (uint32_t Row : *Rows) {
    Expected<CustomAttributeRow> CA = getCustomAttribute(Row);
    if (!CA)
      return CA.takeError();
    // The constructor's declaring type.
    uint32_t Type = 0;
    uint8_t CtorTable = getTokenType(CA->Constructor);
    if (CtorTable == static_cast<uint8_t>(TableId::MethodDef)) {
      Expected<uint32_t> Owner = getOwnerTypeDef(CA->Constructor);
      if (!Owner)
        return Owner.takeError();
      Type = *Owner;
    } else if (CtorTable == static_cast<uint8_t>(TableId::MemberRef)) {
      Expected<uint32_t> Parent =
          getColumnValue(TableId::MemberRef, getTokenRow(CA->Constructor), 0);
      if (!Parent)
        return Parent.takeError();
      uint8_t ParentTable = getTokenType(*Parent);
      if (*Parent != 0 &&
          (ParentTable == static_cast<uint8_t>(TableId::TypeDef) ||
           ParentTable == static_cast<uint8_t>(TableId::TypeRef)))
        Type = *Parent;
    }
    if (Type == 0)
      continue;
    Expected<std::pair<StringRef, StringRef>> TypeName =
        getTypeName(*this, Type);
    if (!TypeName)
      return TypeName.takeError();
    if (TypeName->first != Namespace || TypeName->second != Name)
      continue;
    Expected<ArrayRef<uint8_t>> Blob = getBlob(CA->Value);
    if (!Blob)
      return Blob.takeError();
    Expected<ArrayRef<uint8_t>> Payload = decodeByteArrayAttribute(*Blob);
    if (!Payload)
      return Payload.takeError();
    Payloads.push_back(*Payload);
  }
  return std::move(Payloads);
}

Expected<MethodBodyView>
MetadataReader::getMethodBody(uint32_t MethodDefToken) const {
  if (Error E =
          checkToken(MethodDefToken, {TableId::MethodDef}, "a MethodDef token"))
    return std::move(E);
  Expected<uint32_t> RVA =
      getColumnValue(TableId::MethodDef, getTokenRow(MethodDefToken), 0);
  if (!RVA)
    return RVA.takeError();
  if (*RVA == 0)
    return mdError("method " + hex(MethodDefToken) + " has no body (RVA 0)");
  if (!Image)
    return mdError("method " + hex(MethodDefToken) +
                   ": no PE image to read its body from");
  Expected<ArrayRef<uint8_t>> Bytes = Image->getRVATail(*RVA);
  if (!Bytes)
    return Bytes.takeError();
  return decodeMethodBody(*Bytes);
}

Expected<ArrayRef<uint8_t>> MetadataReader::getFieldData(uint32_t FieldToken,
                                                         uint32_t Size) const {
  Expected<std::optional<FieldRVARow>> Row = findFieldRVA(FieldToken);
  if (!Row)
    return Row.takeError();
  if (!*Row)
    return mdError("field " + hex(FieldToken) + " has no FieldRVA row");
  if (!Image)
    return mdError("field " + hex(FieldToken) +
                   ": no PE image to read its data from");
  return Image->getRVAData((*Row)->RVA, Size);
}

std::array<uint8_t, 8> cli::computePublicKeyToken(ArrayRef<uint8_t> PublicKey) {
  std::array<uint8_t, 20> Hash = SHA1::hash(PublicKey);
  std::array<uint8_t, 8> Token;
  for (unsigned I = 0; I != 8; ++I)
    Token[I] = Hash[19 - I];
  return Token;
}
