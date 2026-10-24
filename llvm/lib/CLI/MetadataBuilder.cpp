//===- MetadataBuilder.cpp - ECMA-335 metadata writer ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The rows of one module's metadata tables, the 0xFFFF padding rule, the
// content checks, and the serialization of the metadata root with its five
// streams (II.22, II.24.2).
//
// Every table keeps its rows in insertion order. finalize() numbers the
// member tables (Field, MethodDef, Param, Property) with a stable counting
// sort by owner, so a run is in owner order and in insertion order within an
// owner, and it stable-sorts the sorted tables on their encoded keys. Hash
// maps serve only lookups (deduplication and interning) and are never
// iterated, and #Strings is laid out by StringTableBuilder, whose order
// depends only on the strings, so the bytes depend only on the calls.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/Heaps.h"
#include "llvm/CLI/TableSchema.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <numeric>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#define DEBUG_TYPE "cli-metadatabuilder"

using namespace llvm;
using namespace llvm::cli;
using namespace llvm::support;

static constexpr char ErrorPrefix[] = "cannot write CLI metadata: ";
/// The name of the inert rows of the 0xFFFF padding rule (the '<msil>'
/// prefix is reserved, D11).
static constexpr char PadName[] = "<msil>pad";
/// The longest part of a name that an error message shows.
static constexpr size_t MaxShownNameBytes = 64;
/// The tables with a schema, 0x00-0x2C.
static constexpr unsigned NumSchemaTables = 0x2D;
/// The most columns of a table (Assembly and AssemblyRef have 9).
static constexpr unsigned MaxColumns = 9;

/// The metadata root's version field: the version string, NUL-terminated and
/// padded to 4 bytes.
static constexpr uint32_t VersionFieldSize =
    (MetadataVersionString.size() + 1 + 3) & ~uint32_t(3);

/// The five streams, in the order ilasm and Roslyn write them.
enum StreamIndex : unsigned {
  TablesStream,
  StringsStream,
  UserStringsStream,
  GuidStream,
  BlobStream,
  NumStreams
};
static constexpr StringLiteral StreamNames[NumStreams] = {
    "#~", "#Strings", "#US", "#GUID", "#Blob"};

/// The size of a stream header: offset, size and the name, NUL-terminated
/// and padded to 4 bytes.
static constexpr uint32_t getStreamHeaderSize(StringLiteral Name) {
  return 8 + ((Name.size() + 1 + 3) & ~uint32_t(3));
}

/// The size of the metadata root up to the first stream: signature,
/// versions, reserved, version length and version, flags, the stream count
/// and the stream headers.
static constexpr uint32_t computeRootSize() {
  uint32_t Size = 16 + VersionFieldSize + 4;
  for (StringLiteral Name : StreamNames)
    Size += getStreamHeaderSize(Name);
  return Size;
}
static constexpr uint32_t RootSize = computeRootSize();
static_assert(RootSize == 108, "ilasm's and Roslyn's metadata root size");

/// The size of the fixed part of the #~ stream: reserved, versions,
/// HeapSizes, reserved, Valid and Sorted.
static constexpr uint32_t TablesHeaderSize = 24;

static EntityHandle makeHandle(TableId T, uint32_t Row) {
  EntityHandle H;
  H.Value = makeToken(T, Row);
  return H;
}

static bool isMemberTable(TableId T) {
  return T == TableId::Field || T == TableId::MethodDef ||
         T == TableId::Param || T == TableId::Property;
}

/// Appends \p Name to \p Out for an error message: at most
/// MaxShownNameBytes bytes, with control bytes, DEL, the backslash and the
/// quote escaped as \XX.
static void appendShownName(std::string &Out, StringRef Name) {
  for (unsigned char C : Name.take_front(MaxShownNameBytes)) {
    if (C < 0x20 || C == 0x7F || C == '\\' || C == '\'') {
      Out += '\\';
      Out += hexdigit(C >> 4);
      Out += hexdigit(C & 0xF);
    } else {
      Out += static_cast<char>(C);
    }
  }
  if (Name.size() > MaxShownNameBytes)
    Out += "...";
}

//===----------------------------------------------------------------------===//
// Rows
//===----------------------------------------------------------------------===//

namespace {

/// An interned string; 0 is the empty string.
using StringId = uint32_t;

struct AssemblyRow {
  AssemblyHashAlgorithm HashAlg;
  AssemblyVersion Version;
  AssemblyFlags Flags;
  uint32_t PublicKey;
  StringId Name, Culture;
};

struct AssemblyRefRow {
  AssemblyVersion Version;
  AssemblyFlags Flags;
  uint32_t PublicKeyOrToken;
  StringId Name, Culture;
  uint32_t HashValue;
};

struct TypeRefRow {
  uint32_t Scope; ///< A ResolutionScope token, or 0.
  StringId Namespace, Name;
};

struct TypeDefRow {
  TypeAttributes Flags;
  StringId Namespace, Name;
  uint32_t Extends;   ///< A TypeDefOrRef token, or 0.
  uint32_t Enclosing; ///< The enclosing TypeDef row, or 0.
  uint32_t NumFields = 0, NumMethods = 0, NumProperties = 0;
};

struct FieldRow {
  uint32_t Owner; ///< A TypeDef row.
  FieldAttributes Flags;
  StringId Name;
  uint32_t Signature;
};

struct MethodRow {
  uint32_t Owner; ///< A TypeDef row.
  MethodAttributes Flags;
  MethodImplAttributes ImplFlags;
  StringId Name;
  uint32_t Signature;
  bool HasBody = false;
  uint32_t BodyOffset = 0;
};

struct ParamRow {
  uint32_t Method; ///< The method's insertion index, 1-based.
  uint16_t Sequence;
  ParamAttributes Flags;
  StringId Name;
};

struct PropertyRow {
  uint32_t Owner; ///< A TypeDef row.
  PropertyAttributes Flags;
  StringId Name;
  uint32_t Signature;
};

struct MemberRefRow {
  uint32_t Parent; ///< A MemberRefParent token (MethodDef: provisional).
  StringId Name;
  uint32_t Signature;
};

struct MethodSpecRow {
  uint32_t Method; ///< A MethodDefOrRef token (MethodDef: provisional).
  uint32_t Instantiation;
};

struct ImplMapRow {
  PInvokeAttributes Flags;
  uint32_t Member; ///< A MemberForwarded token (provisional).
  StringId ImportName;
  uint32_t ModuleRef; ///< A ModuleRef row.
};

struct ClassLayoutRow {
  uint16_t PackingSize;
  uint32_t ClassSize;
  uint32_t Parent; ///< A TypeDef row.
};

/// A FieldLayout or FieldRVA row.
struct FieldOffsetRow {
  uint32_t Offset;
  uint32_t Field; ///< The field's insertion index, 1-based.
};

struct CustomAttributeRow {
  uint32_t Parent; ///< A HasCustomAttribute token (provisional).
  uint32_t Type;   ///< A CustomAttributeType token (provisional).
  uint32_t Value;
};

struct MethodSemanticsRow {
  MethodSemanticsAttributes Semantics;
  uint32_t Method;      ///< The method's insertion index, 1-based.
  uint32_t Association; ///< A Property token (provisional).
};

struct PropertyMapRow {
  uint32_t Parent, PropertyList;
};

struct NestedClassRow {
  uint32_t NestedClass, EnclosingClass;
};

/// The final numbering of a member table, made by groupByOwner().
struct Numbering {
  /// FinalRow[I] is the final row of the row inserted I-th (0-based).
  std::vector<uint32_t> FinalRow;
  /// Order[R - 1] is the insertion index (0-based) of final row R.
  std::vector<uint32_t> Order;
  /// First[O] is the final row of owner O's first row, the value of O's
  /// list column, for O in 1..NumOwners + 1; First[NumOwners + 1] is the
  /// row count + 1. First[0] is unused.
  std::vector<uint32_t> First;
};

} // namespace

/// A stable counting sort of rows by owner: the rows of owner 1 first, in
/// insertion order, then those of owner 2, and so on. Owners[I] is the owner
/// (1..NumOwners) of the row inserted I-th.
static Numbering groupByOwner(ArrayRef<uint32_t> Owners, uint32_t NumOwners) {
  Numbering N;
  N.First.assign(NumOwners + 2, 0);
  for (uint32_t O : Owners) {
    assert(O >= 1 && O <= NumOwners && "row without a valid owner");
    ++N.First[O];
  }
  uint32_t Next = 1;
  for (uint32_t O = 1; O <= NumOwners + 1; ++O) {
    uint32_t Count = N.First[O];
    N.First[O] = Next;
    Next += Count;
  }
  std::vector<uint32_t> Cursor(N.First);
  N.FinalRow.resize(Owners.size());
  N.Order.resize(Owners.size());
  for (size_t I = 0, E = Owners.size(); I != E; ++I) {
    uint32_t Row = Cursor[Owners[I]]++;
    N.FinalRow[I] = Row;
    N.Order[Row - 1] = static_cast<uint32_t>(I);
  }
  return N;
}

/// The insertion indexes (0-based) of \p Count rows, stable-sorted by
/// Key(index).
template <typename KeyFn>
static std::vector<uint32_t> stableOrder(size_t Count, KeyFn Key) {
  std::vector<uint32_t> Keys(Count);
  for (size_t I = 0; I != Count; ++I)
    Keys[I] = Key(I);
  std::vector<uint32_t> Order(Count);
  std::iota(Order.begin(), Order.end(), 0);
  llvm::stable_sort(Order,
                    [&](uint32_t L, uint32_t R) { return Keys[L] < Keys[R]; });
  return Order;
}

/// The coded index K value of a token that the builder made, which K must
/// be able to reference.
static uint32_t encodeCoded(CodedIndexKind K, uint32_t Token) {
  std::optional<uint32_t> V = encodeCodedIndex(K, Token);
  assert(V && "token of a table that the coded index cannot reference");
  return V.value_or(0);
}

namespace {

/// Writes rows into the #~ stream with the column widths of a layout.
class RowWriter {
public:
  using WidthTable =
      std::array<std::array<uint8_t, MaxColumns>, NumSchemaTables>;

  RowWriter(uint8_t *P, const WidthTable &Widths,
            const std::array<uint8_t, NumSchemaTables> &NumColumns)
      : P(P), Widths(Widths), NumColumns(NumColumns) {}

  void row(TableId T, std::initializer_list<uint32_t> Cells) {
    unsigned Id = static_cast<uint8_t>(T);
    assert(Cells.size() == NumColumns[Id] && "wrong number of cells");
    (void)NumColumns;
    unsigned C = 0;
    for (uint32_t V : Cells) {
      if (Widths[Id][C++] == 2) {
        assert(V <= 0xFFFF && "value too wide for a 2-byte column");
        endian::write16le(P, static_cast<uint16_t>(V));
        P += 2;
      } else {
        endian::write32le(P, V);
        P += 4;
      }
    }
  }

  uint8_t *P;

private:
  const WidthTable &Widths;
  const std::array<uint8_t, NumSchemaTables> &NumColumns;
};

} // namespace

//===----------------------------------------------------------------------===//
// MetadataBuilder::Impl
//===----------------------------------------------------------------------===//

struct MetadataBuilder::Impl {
  // Interned strings: Strings[Id] is the string with id Id, a key of
  // StringIds (whose entries never move). Strings[0] is "".
  StringMap<StringId> StringIds;
  std::vector<StringRef> Strings{StringRef()};

  StringHeapBuilder StringHeap;
  BlobHeapBuilder Blobs;
  UserStringHeapBuilder UserStrings;
  GuidHeapBuilder Guids;

  StringId ModuleName = 0;
  std::optional<AssemblyRow> Assembly;
  std::vector<AssemblyRefRow> AssemblyRefs;
  std::vector<StringId> ModuleRefs;
  std::vector<TypeRefRow> TypeRefs;
  std::vector<TypeDefRow> TypeDefs;
  std::vector<uint32_t> TypeSpecs;
  std::vector<MemberRefRow> MemberRefs;
  std::vector<uint32_t> StandAloneSigs;
  std::vector<MethodSpecRow> MethodSpecs;
  std::vector<FieldRow> Fields;
  std::vector<MethodRow> Methods;
  std::vector<ParamRow> Params;
  std::vector<PropertyRow> Properties;
  std::vector<MethodSemanticsRow> MethodSemantics;
  std::vector<ImplMapRow> ImplMaps;
  std::vector<ClassLayoutRow> ClassLayouts;
  std::vector<FieldOffsetRow> FieldLayouts;
  std::vector<FieldOffsetRow> FieldRVAs;
  std::vector<CustomAttributeRow> CustomAttributes;
  uint32_t NumNestedTypes = 0;

  // Deduplication of the stable tables, by content.
  StringMap<uint32_t> AssemblyRefIds;
  DenseMap<StringId, uint32_t> ModuleRefIds;
  DenseMap<std::tuple<uint32_t, StringId, StringId>, uint32_t> TypeRefIds;
  DenseMap<uint32_t, uint32_t> TypeSpecIds;
  DenseMap<std::tuple<uint32_t, StringId, uint32_t>, uint32_t> MemberRefIds;
  DenseMap<uint32_t, uint32_t> StandAloneSigIds;
  DenseMap<std::pair<uint32_t, uint32_t>, uint32_t> MethodSpecIds;

  /// The first content check, with ErrorPrefix.
  std::optional<std::string> FirstError;

  /// finalize() was called; nothing may be added.
  bool Finalized = false;
  /// finalize() succeeded: the members are numbered and the layout is known.
  bool Ready = false;

  // Made by a successful finalize().
  Numbering FieldNum, MethodNum, ParamNum, PropertyNum;
  std::vector<PropertyMapRow> PropertyMaps;
  std::vector<NestedClassRow> NestedClasses;
  std::vector<uint32_t> MethodSemanticsOrder, ImplMapOrder, ClassLayoutOrder,
      FieldLayoutOrder, FieldRVAOrder, CustomAttributeOrder;
  /// The #Strings index of each string id (0 for ids that no row uses).
  std::vector<uint32_t> StringIndexes;
  std::array<uint32_t, NumTableIds> RowCounts{};
  std::optional<MetadataSizes> Sizes;
  uint8_t HeapSizeFlags = 0;
  uint64_t ValidMask = 0;
  RowWriter::WidthTable Widths{};
  std::array<uint8_t, NumSchemaTables> NumColumns{};
  std::array<uint32_t, NumStreams> StreamOffsets{}, StreamSizes{};
  size_t SerializedSize = 0;

  StringId intern(StringRef S) {
    if (S.empty())
      return 0;
    auto [It, Inserted] =
        StringIds.try_emplace(S, static_cast<StringId>(Strings.size()));
    if (Inserted)
      Strings.push_back(It->getKey());
    return It->second;
  }
  StringRef str(StringId Id) const { return Strings[Id]; }

  // Content checks.

  bool hasError() const { return FirstError.has_value(); }
  void recordError(const Twine &Msg) {
    if (!FirstError)
      FirstError = (ErrorPrefix + Msg).str();
  }
  /// Records an Error whose message already has the prefix.
  void recordError(Error E) {
    std::string Msg = toString(std::move(E));
    if (!FirstError)
      FirstError = std::move(Msg);
  }

  /// Records a content check for a name stored in #Strings: a NUL byte, or
  /// more than MaxIdentifierBytes bytes. TypeDef and TypeRef names count as
  /// Namespace.Name.
  void checkName(StringRef Namespace, StringRef Name,
                 function_ref<std::string()> Describe) {
    if (hasError())
      return;
    if (Namespace.contains('\0') || Name.contains('\0')) {
      recordError(Twine(Describe()) + " has a NUL byte in its name");
      return;
    }
    size_t Length =
        Namespace.empty() ? Name.size() : Namespace.size() + 1 + Name.size();
    if (Length > MaxIdentifierBytes)
      recordError(Twine(Describe()) + " has a name of " + Twine(Length) +
                  " bytes (limit " + Twine(MaxIdentifierBytes) + ")");
  }
  void checkName(StringRef Name, function_ref<std::string()> Describe) {
    checkName(StringRef(), Name, Describe);
  }

  /// Whether table \p T already has MaxTableRows rows: then a row cannot be
  /// added, and that is a content check.
  bool isTableFull(TableId T, size_t Rows) {
    if (Rows < MaxTableRows)
      return false;
    recordError("table " + Twine(getTableName(T)) + " has more than " +
                Twine(MaxTableRows) + " rows");
    return true;
  }

  /// A blob's #Blob offset, or a content check and 0 for a blob that does
  /// not fit.
  uint32_t addBlob(ArrayRef<uint8_t> Bytes) {
    if (Bytes.size() > MaxCompressedUInt) {
      recordError("a blob of " + Twine(Bytes.size()) +
                  " bytes is longer than the #Blob limit (" +
                  Twine(MaxCompressedUInt) + " bytes)");
      return 0;
    }
    if (uint64_t(Blobs.getSize()) + 4 + Bytes.size() > UINT32_MAX) {
      recordError("the #Blob heap would pass 4 GiB");
      return 0;
    }
    return Blobs.add(Bytes);
  }

  // Names of entities, for error messages.

  void appendQualified(std::string &Out, StringId Namespace,
                       StringId Name) const {
    if (Namespace) {
      appendShownName(Out, str(Namespace));
      Out += '.';
    }
    appendShownName(Out, str(Name));
  }
  /// 'Namespace.Name', with enclosing types first: 'Outer/Inner'.
  std::string getTypeName(uint32_t Row) const {
    SmallVector<uint32_t, 4> Chain;
    for (uint32_t R = Row; R != 0; R = TypeDefs[R - 1].Enclosing)
      Chain.push_back(R);
    std::string Out;
    for (uint32_t R : llvm::reverse(Chain)) {
      if (!Out.empty())
        Out += '/';
      appendQualified(Out, TypeDefs[R - 1].Namespace, TypeDefs[R - 1].Name);
    }
    return Out;
  }
  /// The same for a TypeRef: TypeRefs nested in TypeRefs give 'Outer/Inner'.
  std::string getTypeRefName(uint32_t Row) const {
    SmallVector<uint32_t, 4> Chain;
    for (uint32_t R = Row; R != 0;) {
      Chain.push_back(R);
      uint32_t Scope = TypeRefs[R - 1].Scope;
      R = getTokenType(Scope) == static_cast<uint8_t>(TableId::TypeRef)
              ? getTokenRow(Scope)
              : 0;
    }
    std::string Out;
    for (uint32_t R : llvm::reverse(Chain)) {
      if (!Out.empty())
        Out += '/';
      appendQualified(Out, TypeRefs[R - 1].Namespace, TypeRefs[R - 1].Name);
    }
    return Out;
  }
  /// 'Owner::Name' for a member of TypeDef row \p Owner.
  std::string getMemberName(uint32_t Owner, StringRef Name) const {
    std::string Out = getTypeName(Owner);
    Out += "::";
    appendShownName(Out, Name);
    return Out;
  }
  std::string getMethodName(uint32_t MethodIndex) const {
    const MethodRow &M = Methods[MethodIndex - 1];
    return getMemberName(M.Owner, str(M.Name));
  }

  // Handles.

  /// Whether \p H names an existing row of a table that the builder writes.
  bool isValidHandle(EntityHandle H) const {
    uint32_t Row = H.getIndex();
    if (Row == 0)
      return false;
    switch (H.getTable()) {
    case TableId::Module:
      return Row == 1;
    case TableId::Assembly:
      return Row == 1 && Assembly.has_value();
    case TableId::AssemblyRef:
      return Row <= AssemblyRefs.size();
    case TableId::ModuleRef:
      return Row <= ModuleRefs.size();
    case TableId::TypeRef:
      return Row <= TypeRefs.size();
    case TableId::TypeDef:
      return Row <= TypeDefs.size();
    case TableId::TypeSpec:
      return Row <= TypeSpecs.size();
    case TableId::MemberRef:
      return Row <= MemberRefs.size();
    case TableId::StandAloneSig:
      return Row <= StandAloneSigs.size();
    case TableId::MethodSpec:
      return Row <= MethodSpecs.size();
    case TableId::Field:
      return Row <= Fields.size();
    case TableId::MethodDef:
      return Row <= Methods.size();
    case TableId::Param:
      return Row <= Params.size();
    case TableId::Property:
      return Row <= Properties.size();
    default:
      return false;
    }
  }
  bool isHandleOf(TableId T, EntityHandle H) const {
    return H.getTable() == T && isValidHandle(H);
  }
  /// Whether \p H is a row that coded index \p K can reference (or nil, if
  /// \p AllowNil).
  bool canReference(CodedIndexKind K, EntityHandle H, bool AllowNil) const {
    if (H.Value == 0)
      return AllowNil;
    return encodeCodedIndex(K, H.Value).has_value() && isValidHandle(H);
  }
  [[maybe_unused]] bool isBlob(uint32_t Offset) const {
    return Offset < std::max<uint32_t>(Blobs.getSize(), 1);
  }

  /// The final token of a token that the builder made: member rows get their
  /// final numbers, every other token stays.
  uint32_t getFinalToken(uint32_t Token) const {
    uint32_t Row = getTokenRow(Token);
    if (Row == 0)
      return Token;
    TableId T = static_cast<TableId>(getTokenType(Token));
    switch (T) {
    case TableId::Field:
      return makeToken(T, FieldNum.FinalRow[Row - 1]);
    case TableId::MethodDef:
      return makeToken(T, MethodNum.FinalRow[Row - 1]);
    case TableId::Param:
      return makeToken(T, ParamNum.FinalRow[Row - 1]);
    case TableId::Property:
      return makeToken(T, PropertyNum.FinalRow[Row - 1]);
    default:
      return Token;
    }
  }

  uint32_t getRowCount(TableId T) const;
  Error addPadRows(MethodBodyStream &Bodies);
  Error checkMemberCounts() const;
  void numberRows();
  void layOutStrings();
  void computeLayout();
  void writeTables(uint8_t *Begin, uint32_t ILStreamRVA,
                   uint32_t MappedDataRVA) const;
};

uint32_t MetadataBuilder::Impl::getRowCount(TableId T) const {
  switch (T) {
  case TableId::Module:
    return 1;
  case TableId::TypeRef:
    return TypeRefs.size();
  case TableId::TypeDef:
    return TypeDefs.size();
  case TableId::Field:
    return Fields.size();
  case TableId::MethodDef:
    return Methods.size();
  case TableId::Param:
    return Params.size();
  case TableId::MemberRef:
    return MemberRefs.size();
  case TableId::CustomAttribute:
    return CustomAttributes.size();
  case TableId::ClassLayout:
    return ClassLayouts.size();
  case TableId::FieldLayout:
    return FieldLayouts.size();
  case TableId::StandAloneSig:
    return StandAloneSigs.size();
  case TableId::PropertyMap: // one row per TypeDef with properties
    return llvm::count_if(
        TypeDefs, [](const TypeDefRow &R) { return R.NumProperties != 0; });
  case TableId::Property:
    return Properties.size();
  case TableId::MethodSemantics:
    return MethodSemantics.size();
  case TableId::ModuleRef:
    return ModuleRefs.size();
  case TableId::TypeSpec:
    return TypeSpecs.size();
  case TableId::ImplMap:
    return ImplMaps.size();
  case TableId::FieldRVA:
    return FieldRVAs.size();
  case TableId::Assembly:
    return Assembly ? 1 : 0;
  case TableId::AssemblyRef:
    return AssemblyRefs.size();
  case TableId::NestedClass:
    return NumNestedTypes;
  case TableId::MethodSpec:
    return MethodSpecs.size();
  default:
    // Constant, GenericParam, InterfaceImpl, Event, File, ExportedType,
    // ManifestResource and the rest are not written (5.7.6).
    return 0;
  }
}

/// The 0xFFFF padding rule (5.7.6): while Field, MethodDef or Param has
/// exactly 0xFFFF rows, append an inert row owned by the last TypeDef. The
/// pad methods share one tiny 'ret' body; the pad Param belongs to a pad
/// method, which is the last method.
Error MetadataBuilder::Impl::addPadRows(MethodBodyStream &Bodies) {
  const uint32_t Last = TypeDefs.size();
  std::optional<uint32_t> PadBody;
  uint32_t PadMethod = 0; // the insertion index of the last pad method
  bool Checked = false;
  for (;;) {
    bool PadField = Fields.size() == 0xFFFF;
    bool PadParam = Params.size() == 0xFFFF;
    bool PadMethodRow = Methods.size() == 0xFFFF || (PadParam && !PadMethod);
    if (!PadField && !PadParam && !PadMethodRow)
      return Error::success();
    if (!Checked) {
      Checked = true;
      if ((TypeDefs[Last - 1].Flags & TypeAttributes::ClassSemanticsMask) ==
          TypeAttributes::Interface)
        return createStringError(errc::invalid_argument,
                                 Twine(ErrorPrefix) + "the last type '" +
                                     getTypeName(Last) +
                                     "' is an interface, so it cannot own the "
                                     "pad rows of the 0xFFFF rule");
    }
    if (PadField) {
      uint8_t Sig[] = {SIG_FIELD, ELEMENT_TYPE_I4};
      Fields.push_back({Last,
                        FieldAttributes::Private | FieldAttributes::Static,
                        intern(PadName), Blobs.add(Sig)});
      ++TypeDefs[Last - 1].NumFields;
      continue;
    }
    if (PadMethodRow) {
      if (!PadBody) {
        static const uint8_t Ret[] = {0x2A};
        MethodBodyInfo Body;
        Body.Code = Ret;
        PadBody = Bodies.addBody(Body);
      }
      uint8_t Sig[] = {SIG_DEFAULT, 0, ELEMENT_TYPE_VOID};
      MethodRow M{Last,
                  MethodAttributes::Private | MethodAttributes::Static |
                      MethodAttributes::HideBySig,
                  MethodImplAttributes::IL | MethodImplAttributes::Managed,
                  intern(PadName), Blobs.add(Sig)};
      M.HasBody = true;
      M.BodyOffset = *PadBody;
      Methods.push_back(M);
      ++TypeDefs[Last - 1].NumMethods;
      PadMethod = Methods.size();
      continue;
    }
    // PadParam, and the last method is a pad method.
    Params.push_back({PadMethod, 0, ParamAttributes::None, 0});
  }
}

Error MetadataBuilder::Impl::checkMemberCounts() const {
  for (uint32_t R = 1, E = TypeDefs.size(); R <= E; ++R) {
    const TypeDefRow &T = TypeDefs[R - 1];
    if (T.NumMethods > MaxMethodsPerType)
      return createStringError(errc::invalid_argument,
                               Twine(ErrorPrefix) + "type '" + getTypeName(R) +
                                   "' has " + Twine(T.NumMethods) +
                                   " methods (limit " +
                                   Twine(MaxMethodsPerType) + ")");
    if (T.NumFields > MaxFieldsPerType)
      return createStringError(errc::invalid_argument,
                               Twine(ErrorPrefix) + "type '" + getTypeName(R) +
                                   "' has " + Twine(T.NumFields) +
                                   " fields (limit " + Twine(MaxFieldsPerType) +
                                   ")");
  }
  return Error::success();
}

/// Numbers the member rows, writes the implicit PropertyMap and NestedClass
/// rows, and stable-sorts the sorted tables on their encoded keys.
void MetadataBuilder::Impl::numberRows() {
  const uint32_t NumTypes = TypeDefs.size();
  std::vector<uint32_t> Owners;

  Owners.resize(Fields.size());
  for (size_t I = 0, E = Fields.size(); I != E; ++I)
    Owners[I] = Fields[I].Owner;
  FieldNum = groupByOwner(Owners, NumTypes);

  Owners.resize(Methods.size());
  for (size_t I = 0, E = Methods.size(); I != E; ++I)
    Owners[I] = Methods[I].Owner;
  MethodNum = groupByOwner(Owners, NumTypes);

  // Params are owned by the methods in their final order.
  Owners.resize(Params.size());
  for (size_t I = 0, E = Params.size(); I != E; ++I)
    Owners[I] = MethodNum.FinalRow[Params[I].Method - 1];
  ParamNum = groupByOwner(Owners, Methods.size());

  Owners.resize(Properties.size());
  for (size_t I = 0, E = Properties.size(); I != E; ++I)
    Owners[I] = Properties[I].Owner;
  PropertyNum = groupByOwner(Owners, NumTypes);

  // One PropertyMap row per TypeDef with properties and one NestedClass row
  // per nested TypeDef, both in TypeDef order (NestedClass is sorted on its
  // first column, so that is its sorted order too).
  for (uint32_t R = 1; R <= NumTypes; ++R) {
    const TypeDefRow &T = TypeDefs[R - 1];
    if (T.NumProperties)
      PropertyMaps.push_back({R, PropertyNum.First[R]});
    if (T.Enclosing)
      NestedClasses.push_back({R, T.Enclosing});
  }
  assert(NestedClasses.size() == NumNestedTypes && "NestedClass rows");

  // The sorted tables (II.22), on their keys as encoded.
  ClassLayoutOrder = stableOrder(
      ClassLayouts.size(), [&](size_t I) { return ClassLayouts[I].Parent; });
  FieldLayoutOrder = stableOrder(FieldLayouts.size(), [&](size_t I) {
    return FieldNum.FinalRow[FieldLayouts[I].Field - 1];
  });
  FieldRVAOrder = stableOrder(FieldRVAs.size(), [&](size_t I) {
    return FieldNum.FinalRow[FieldRVAs[I].Field - 1];
  });
  ImplMapOrder = stableOrder(ImplMaps.size(), [&](size_t I) {
    return encodeCoded(CodedIndexKind::MemberForwarded,
                       getFinalToken(ImplMaps[I].Member));
  });
  CustomAttributeOrder = stableOrder(CustomAttributes.size(), [&](size_t I) {
    return encodeCoded(CodedIndexKind::HasCustomAttribute,
                       getFinalToken(CustomAttributes[I].Parent));
  });
  MethodSemanticsOrder = stableOrder(MethodSemantics.size(), [&](size_t I) {
    return encodeCoded(CodedIndexKind::HasSemantics,
                       getFinalToken(MethodSemantics[I].Association));
  });
}

/// Adds every string that a row uses to #Strings and lays the heap out.
void MetadataBuilder::Impl::layOutStrings() {
  std::vector<bool> Used(Strings.size());
  auto Use = [&](StringId Id) { Used[Id] = true; };
  Use(ModuleName);
  if (Assembly) {
    Use(Assembly->Name);
    Use(Assembly->Culture);
  }
  for (const AssemblyRefRow &R : AssemblyRefs) {
    Use(R.Name);
    Use(R.Culture);
  }
  for (StringId Name : ModuleRefs)
    Use(Name);
  for (const TypeRefRow &R : TypeRefs) {
    Use(R.Namespace);
    Use(R.Name);
  }
  for (const TypeDefRow &R : TypeDefs) {
    Use(R.Namespace);
    Use(R.Name);
  }
  for (const MemberRefRow &R : MemberRefs)
    Use(R.Name);
  for (const FieldRow &R : Fields)
    Use(R.Name);
  for (const MethodRow &R : Methods)
    Use(R.Name);
  for (const ParamRow &R : Params)
    Use(R.Name);
  for (const PropertyRow &R : Properties)
    Use(R.Name);
  for (const ImplMapRow &R : ImplMaps)
    Use(R.ImportName);

  for (StringId Id = 1, E = Strings.size(); Id != E; ++Id)
    if (Used[Id])
      StringHeap.add(Strings[Id]);
  StringHeap.finalize();
  StringIndexes.assign(Strings.size(), 0);
  for (StringId Id = 1, E = Strings.size(); Id != E; ++Id)
    if (Used[Id])
      StringIndexes[Id] = StringHeap.getIndex(Strings[Id]);
}

/// The row counts, the column widths and the place of every stream.
void MetadataBuilder::Impl::computeLayout() {
  for (unsigned T = 0; T != NumSchemaTables; ++T)
    RowCounts[T] = getRowCount(static_cast<TableId>(T));
  uint64_t StringsSize = alignTo(StringHeap.getSize(), 4);
  uint64_t UserStringsSize = alignTo(UserStrings.getSize(), 4);
  uint64_t GuidSize = alignTo(Guids.getSize(), 4);
  uint64_t BlobSize = alignTo(Blobs.getSize(), 4);
  HeapSizeFlags =
      MetadataSizes::computeHeapSizeFlags(StringsSize, GuidSize, BlobSize);
  Sizes.emplace(RowCounts, HeapSizeFlags);

  unsigned NumPresent = 0;
  ValidMask = 0;
  for (unsigned T = 0; T != NumSchemaTables; ++T) {
    TableId Id = static_cast<TableId>(T);
    unsigned N = getTableColumns(Id).size();
    assert(N <= MaxColumns && "a table with more columns than MaxColumns");
    NumColumns[T] = N;
    for (unsigned C = 0; C != N; ++C)
      Widths[T][C] = Sizes->getColumnSize(Id, C);
    if (RowCounts[T]) {
      ValidMask |= uint64_t(1) << T;
      ++NumPresent;
    }
  }
  uint64_t TablesSize =
      alignTo(TablesHeaderSize + 4 * NumPresent + Sizes->getTablesSize(), 4);

  const uint64_t Sizes64[NumStreams] = {TablesSize, StringsSize,
                                        UserStringsSize, GuidSize, BlobSize};
  uint64_t Offset = RootSize;
  for (unsigned S = 0; S != NumStreams; ++S) {
    assert(Offset + Sizes64[S] <= UINT32_MAX && "metadata larger than 4 GiB");
    StreamOffsets[S] = static_cast<uint32_t>(Offset);
    StreamSizes[S] = static_cast<uint32_t>(Sizes64[S]);
    Offset += Sizes64[S];
  }
  SerializedSize = Offset;
}

/// Writes the #~ stream (II.24.2.6) at \p Begin; the padding after it is
/// already zero.
void MetadataBuilder::Impl::writeTables(uint8_t *Begin, uint32_t ILStreamRVA,
                                        uint32_t MappedDataRVA) const {
  uint8_t *P = Begin;
  endian::write32le(P, 0);
  P[4] = TablesStreamMajorVersion;
  P[5] = TablesStreamMinorVersion;
  P[6] = HeapSizeFlags;
  P[7] = 1; // reserved, always 1
  endian::write64le(P + 8, ValidMask);
  endian::write64le(P + 16, SortedTablesMask);
  P += TablesHeaderSize;
  for (unsigned T = 0; T != NumSchemaTables; ++T) {
    if (!RowCounts[T])
      continue;
    endian::write32le(P, RowCounts[T]);
    P += 4;
  }

  const uint8_t *TableData = P;
  RowWriter W(P, Widths, NumColumns);
  auto Begins = [&]([[maybe_unused]] TableId Table) {
    assert(W.P == TableData + Sizes->getTableOffset(Table) &&
           "table misplaced");
  };
  auto S = [&](StringId Id) { return StringIndexes[Id]; };
  auto Coded = [&](CodedIndexKind K, uint32_t Token) {
    return encodeCoded(K, getFinalToken(Token));
  };
  auto Flags = [](auto F) { return static_cast<uint32_t>(F); };

  using T = TableId;
  Begins(T::Module);
  W.row(T::Module, {0, S(ModuleName), 1, 0, 0});

  Begins(T::TypeRef);
  for (const TypeRefRow &R : TypeRefs)
    W.row(T::TypeRef, {Coded(CodedIndexKind::ResolutionScope, R.Scope),
                       S(R.Name), S(R.Namespace)});

  Begins(T::TypeDef);
  for (uint32_t Row = 1, E = TypeDefs.size(); Row <= E; ++Row) {
    const TypeDefRow &R = TypeDefs[Row - 1];
    W.row(T::TypeDef, {Flags(R.Flags), S(R.Name), S(R.Namespace),
                       Coded(CodedIndexKind::TypeDefOrRef, R.Extends),
                       FieldNum.First[Row], MethodNum.First[Row]});
  }

  Begins(T::Field);
  for (uint32_t I : FieldNum.Order) {
    const FieldRow &R = Fields[I];
    W.row(T::Field, {Flags(R.Flags), S(R.Name), R.Signature});
  }

  Begins(T::MethodDef);
  for (uint32_t Row = 1, E = Methods.size(); Row <= E; ++Row) {
    const MethodRow &R = Methods[MethodNum.Order[Row - 1]];
    uint32_t RVA = R.HasBody ? ILStreamRVA + R.BodyOffset : 0;
    W.row(T::MethodDef, {RVA, Flags(R.ImplFlags), Flags(R.Flags), S(R.Name),
                         R.Signature, ParamNum.First[Row]});
  }

  Begins(T::Param);
  for (uint32_t I : ParamNum.Order) {
    const ParamRow &R = Params[I];
    W.row(T::Param, {Flags(R.Flags), R.Sequence, S(R.Name)});
  }

  Begins(T::MemberRef);
  for (const MemberRefRow &R : MemberRefs)
    W.row(T::MemberRef, {Coded(CodedIndexKind::MemberRefParent, R.Parent),
                         S(R.Name), R.Signature});

  Begins(T::CustomAttribute);
  for (uint32_t I : CustomAttributeOrder) {
    const CustomAttributeRow &R = CustomAttributes[I];
    W.row(T::CustomAttribute,
          {Coded(CodedIndexKind::HasCustomAttribute, R.Parent),
           Coded(CodedIndexKind::CustomAttributeType, R.Type), R.Value});
  }

  Begins(T::ClassLayout);
  for (uint32_t I : ClassLayoutOrder) {
    const ClassLayoutRow &R = ClassLayouts[I];
    W.row(T::ClassLayout, {R.PackingSize, R.ClassSize, R.Parent});
  }

  Begins(T::FieldLayout);
  for (uint32_t I : FieldLayoutOrder) {
    const FieldOffsetRow &R = FieldLayouts[I];
    W.row(T::FieldLayout, {R.Offset, FieldNum.FinalRow[R.Field - 1]});
  }

  Begins(T::StandAloneSig);
  for (uint32_t Sig : StandAloneSigs)
    W.row(T::StandAloneSig, {Sig});

  Begins(T::PropertyMap);
  for (const PropertyMapRow &R : PropertyMaps)
    W.row(T::PropertyMap, {R.Parent, R.PropertyList});

  Begins(T::Property);
  for (uint32_t I : PropertyNum.Order) {
    const PropertyRow &R = Properties[I];
    W.row(T::Property, {Flags(R.Flags), S(R.Name), R.Signature});
  }

  Begins(T::MethodSemantics);
  for (uint32_t I : MethodSemanticsOrder) {
    const MethodSemanticsRow &R = MethodSemantics[I];
    W.row(T::MethodSemantics,
          {Flags(R.Semantics), MethodNum.FinalRow[R.Method - 1],
           Coded(CodedIndexKind::HasSemantics, R.Association)});
  }

  Begins(T::ModuleRef);
  for (StringId Name : ModuleRefs)
    W.row(T::ModuleRef, {S(Name)});

  Begins(T::TypeSpec);
  for (uint32_t Sig : TypeSpecs)
    W.row(T::TypeSpec, {Sig});

  Begins(T::ImplMap);
  for (uint32_t I : ImplMapOrder) {
    const ImplMapRow &R = ImplMaps[I];
    W.row(T::ImplMap,
          {Flags(R.Flags), Coded(CodedIndexKind::MemberForwarded, R.Member),
           S(R.ImportName), R.ModuleRef});
  }

  Begins(T::FieldRVA);
  for (uint32_t I : FieldRVAOrder) {
    const FieldOffsetRow &R = FieldRVAs[I];
    W.row(T::FieldRVA,
          {MappedDataRVA + R.Offset, FieldNum.FinalRow[R.Field - 1]});
  }

  Begins(T::Assembly);
  if (Assembly) {
    const AssemblyRow &R = *Assembly;
    W.row(T::Assembly, {Flags(R.HashAlg), R.Version.Major, R.Version.Minor,
                        R.Version.Build, R.Version.Revision, Flags(R.Flags),
                        R.PublicKey, S(R.Name), S(R.Culture)});
  }

  Begins(T::AssemblyRef);
  for (const AssemblyRefRow &R : AssemblyRefs)
    W.row(T::AssemblyRef,
          {R.Version.Major, R.Version.Minor, R.Version.Build,
           R.Version.Revision, Flags(R.Flags), R.PublicKeyOrToken, S(R.Name),
           S(R.Culture), R.HashValue});

  Begins(T::NestedClass);
  for (const NestedClassRow &R : NestedClasses)
    W.row(T::NestedClass, {R.NestedClass, R.EnclosingClass});

  Begins(T::MethodSpec);
  for (const MethodSpecRow &R : MethodSpecs)
    W.row(T::MethodSpec,
          {Coded(CodedIndexKind::MethodDefOrRef, R.Method), R.Instantiation});

  assert(W.P == TableData + Sizes->getTablesSize() && "#~ size mismatch");
  assert(W.P <= Begin + StreamSizes[TablesStream] && "#~ overflow");
}

//===----------------------------------------------------------------------===//
// MetadataBuilder
//===----------------------------------------------------------------------===//

MetadataBuilder::MetadataBuilder() : I(std::make_unique<Impl>()) {
  // #GUID entry 1 is the MVID, zero until the PE writer stamps the content
  // id (5.7.7).
  [[maybe_unused]] uint32_t Mvid = I->Guids.add({});
  assert(Mvid == 1 && "the MVID is #GUID entry 1");
  TypeDefRow Module{TypeAttributes(), 0, I->intern("<Module>"), 0, 0};
  I->TypeDefs.push_back(Module);
}

MetadataBuilder::~MetadataBuilder() = default;
MetadataBuilder::MetadataBuilder(MetadataBuilder &&) = default;
MetadataBuilder &MetadataBuilder::operator=(MetadataBuilder &&) = default;

EntityHandle MetadataBuilder::getModuleType() const {
  return makeHandle(TableId::TypeDef, 1);
}

void MetadataBuilder::setModuleName(StringRef Name) {
  assert(!I->Finalized && "setModuleName() after finalize()");
  I->ModuleName = I->intern(Name);
  I->checkName(Name, [&] {
    std::string Out = "module '";
    appendShownName(Out, Name);
    return Out + "'";
  });
}

void MetadataBuilder::setAssembly(const AssemblyIdentity &Id) {
  assert(!I->Finalized && "setAssembly() after finalize()");
  AssemblyRow Row{Id.HashAlg,         Id.Version,
                  Id.Flags,           I->addBlob(Id.PublicKey),
                  I->intern(Id.Name), I->intern(Id.Culture)};
  I->Assembly = Row;
  auto Describe = [&] {
    std::string Out = "assembly '";
    appendShownName(Out, Id.Name);
    return Out + "'";
  };
  I->checkName(Id.Name, Describe);
  I->checkName(Id.Culture, [&] { return "the culture of " + Describe(); });
}

bool MetadataBuilder::hasAssembly() const { return I->Assembly.has_value(); }

uint32_t MetadataBuilder::addBlob(ArrayRef<uint8_t> Bytes) {
  assert(!I->Finalized && "addBlob() after finalize()");
  return I->addBlob(Bytes);
}

uint32_t MetadataBuilder::addUserString(StringRef UTF8) {
  assert(!I->Finalized && "addUserString() after finalize()");
  uint32_t Token = uint32_t(UserStringTokenType) << 24;
  Expected<uint32_t> Offset = I->UserStrings.add(UTF8);
  if (!Offset) {
    I->recordError(Offset.takeError());
    return Token;
  }
  return Token | *Offset;
}

EntityHandle MetadataBuilder::addAssemblyRef(const AssemblyRefInfo &R) {
  assert(!I->Finalized && "addAssemblyRef() after finalize()");
  AssemblyRefRow Row{R.Version,
                     R.Flags,
                     I->addBlob(R.PublicKeyOrToken),
                     I->intern(R.Name),
                     I->intern(R.Culture),
                     I->addBlob(R.HashValue)};
  // The content as a key: every part is an id or an offset, so equal ids
  // mean equal content.
  uint32_t Parts[] = {uint32_t(R.Version.Major) << 16 | R.Version.Minor,
                      uint32_t(R.Version.Build) << 16 | R.Version.Revision,
                      static_cast<uint32_t>(R.Flags),
                      Row.PublicKeyOrToken,
                      Row.Name,
                      Row.Culture,
                      Row.HashValue};
  SmallString<32> Key;
  for (uint32_t V : Parts) {
    char B[4];
    endian::write32le(B, V);
    Key.append(B, B + 4);
  }
  auto It = I->AssemblyRefIds.find(Key);
  if (It != I->AssemblyRefIds.end())
    return makeHandle(TableId::AssemblyRef, It->second);
  if (I->isTableFull(TableId::AssemblyRef, I->AssemblyRefs.size()))
    return EntityHandle();
  I->AssemblyRefs.push_back(Row);
  uint32_t Index = I->AssemblyRefs.size();
  I->AssemblyRefIds[Key] = Index;
  auto Describe = [&] {
    std::string Out = "assembly reference '";
    appendShownName(Out, R.Name);
    return Out + "'";
  };
  I->checkName(R.Name, Describe);
  I->checkName(R.Culture, [&] { return "the culture of " + Describe(); });
  return makeHandle(TableId::AssemblyRef, Index);
}

EntityHandle MetadataBuilder::addModuleRef(StringRef Name) {
  assert(!I->Finalized && "addModuleRef() after finalize()");
  StringId Id = I->intern(Name);
  auto It = I->ModuleRefIds.find(Id);
  if (It != I->ModuleRefIds.end())
    return makeHandle(TableId::ModuleRef, It->second);
  if (I->isTableFull(TableId::ModuleRef, I->ModuleRefs.size()))
    return EntityHandle();
  I->ModuleRefs.push_back(Id);
  uint32_t Index = I->ModuleRefs.size();
  I->ModuleRefIds[Id] = Index;
  I->checkName(Name, [&] {
    std::string Out = "module reference '";
    appendShownName(Out, Name);
    return Out + "'";
  });
  return makeHandle(TableId::ModuleRef, Index);
}

EntityHandle MetadataBuilder::addTypeRef(EntityHandle ResolutionScope,
                                         StringRef Namespace, StringRef Name) {
  assert(!I->Finalized && "addTypeRef() after finalize()");
  assert(I->canReference(CodedIndexKind::ResolutionScope, ResolutionScope,
                         /*AllowNil=*/true) &&
         "a TypeRef's scope is a Module, ModuleRef, AssemblyRef or TypeRef");
  std::tuple<uint32_t, StringId, StringId> Key(
      ResolutionScope.Value, I->intern(Namespace), I->intern(Name));
  auto It = I->TypeRefIds.find(Key);
  if (It != I->TypeRefIds.end())
    return makeHandle(TableId::TypeRef, It->second);
  if (I->isTableFull(TableId::TypeRef, I->TypeRefs.size()))
    return EntityHandle();
  I->TypeRefs.push_back(
      {ResolutionScope.Value, std::get<1>(Key), std::get<2>(Key)});
  uint32_t Index = I->TypeRefs.size();
  I->TypeRefIds[Key] = Index;
  I->checkName(Namespace, Name, [&] {
    return "type reference '" + I->getTypeRefName(Index) + "'";
  });
  return makeHandle(TableId::TypeRef, Index);
}

EntityHandle MetadataBuilder::addTypeDef(TypeAttributes Flags,
                                         StringRef Namespace, StringRef Name,
                                         EntityHandle Extends,
                                         EntityHandle EnclosingType) {
  assert(!I->Finalized && "addTypeDef() after finalize()");
  assert(I->canReference(CodedIndexKind::TypeDefOrRef, Extends,
                         /*AllowNil=*/true) &&
         "a TypeDef extends a TypeDef, TypeRef or TypeSpec");
  assert((EnclosingType.Value == 0 ||
          I->isHandleOf(TableId::TypeDef, EnclosingType)) &&
         "an enclosing type is an earlier TypeDef");
  if (I->isTableFull(TableId::TypeDef, I->TypeDefs.size()))
    return EntityHandle();
  TypeDefRow Row{Flags, I->intern(Namespace), I->intern(Name), Extends.Value,
                 EnclosingType.getIndex()};
  I->TypeDefs.push_back(Row);
  uint32_t Index = I->TypeDefs.size();
  if (Row.Enclosing)
    ++I->NumNestedTypes;
  I->checkName(Namespace, Name,
               [&] { return "type '" + I->getTypeName(Index) + "'"; });
  return makeHandle(TableId::TypeDef, Index);
}

EntityHandle MetadataBuilder::addTypeSpec(uint32_t SigBlob) {
  assert(!I->Finalized && "addTypeSpec() after finalize()");
  assert(I->isBlob(SigBlob) && "not a #Blob offset");
  auto It = I->TypeSpecIds.find(SigBlob);
  if (It != I->TypeSpecIds.end())
    return makeHandle(TableId::TypeSpec, It->second);
  if (I->isTableFull(TableId::TypeSpec, I->TypeSpecs.size()))
    return EntityHandle();
  I->TypeSpecs.push_back(SigBlob);
  uint32_t Index = I->TypeSpecs.size();
  I->TypeSpecIds[SigBlob] = Index;
  return makeHandle(TableId::TypeSpec, Index);
}

EntityHandle MetadataBuilder::addMemberRef(EntityHandle Parent, StringRef Name,
                                           uint32_t SigBlob) {
  assert(!I->Finalized && "addMemberRef() after finalize()");
  assert(I->canReference(CodedIndexKind::MemberRefParent, Parent,
                         /*AllowNil=*/false) &&
         "a MemberRef's parent is a TypeDef, TypeRef, ModuleRef, MethodDef or "
         "TypeSpec");
  assert(I->isBlob(SigBlob) && "not a #Blob offset");
  std::tuple<uint32_t, StringId, uint32_t> Key(Parent.Value, I->intern(Name),
                                               SigBlob);
  auto It = I->MemberRefIds.find(Key);
  if (It != I->MemberRefIds.end())
    return makeHandle(TableId::MemberRef, It->second);
  if (I->isTableFull(TableId::MemberRef, I->MemberRefs.size()))
    return EntityHandle();
  I->MemberRefs.push_back({Parent.Value, std::get<1>(Key), SigBlob});
  uint32_t Index = I->MemberRefs.size();
  I->MemberRefIds[Key] = Index;
  I->checkName(Name, [&] {
    std::string Out = "member reference '";
    if (Parent.getTable() == TableId::TypeDef)
      Out += I->getTypeName(Parent.getIndex()) + "::";
    else if (Parent.getTable() == TableId::TypeRef)
      Out += I->getTypeRefName(Parent.getIndex()) + "::";
    appendShownName(Out, Name);
    return Out + "'";
  });
  return makeHandle(TableId::MemberRef, Index);
}

EntityHandle MetadataBuilder::addStandAloneSig(uint32_t SigBlob) {
  assert(!I->Finalized && "addStandAloneSig() after finalize()");
  assert(I->isBlob(SigBlob) && "not a #Blob offset");
  auto It = I->StandAloneSigIds.find(SigBlob);
  if (It != I->StandAloneSigIds.end())
    return makeHandle(TableId::StandAloneSig, It->second);
  if (I->isTableFull(TableId::StandAloneSig, I->StandAloneSigs.size()))
    return EntityHandle();
  I->StandAloneSigs.push_back(SigBlob);
  uint32_t Index = I->StandAloneSigs.size();
  I->StandAloneSigIds[SigBlob] = Index;
  return makeHandle(TableId::StandAloneSig, Index);
}

EntityHandle MetadataBuilder::addMethodSpec(EntityHandle Method,
                                            uint32_t InstantiationBlob) {
  assert(!I->Finalized && "addMethodSpec() after finalize()");
  assert(I->canReference(CodedIndexKind::MethodDefOrRef, Method,
                         /*AllowNil=*/false) &&
         "a MethodSpec instantiates a MethodDef or MemberRef");
  assert(I->isBlob(InstantiationBlob) && "not a #Blob offset");
  std::pair<uint32_t, uint32_t> Key(Method.Value, InstantiationBlob);
  auto It = I->MethodSpecIds.find(Key);
  if (It != I->MethodSpecIds.end())
    return makeHandle(TableId::MethodSpec, It->second);
  if (I->isTableFull(TableId::MethodSpec, I->MethodSpecs.size()))
    return EntityHandle();
  I->MethodSpecs.push_back({Method.Value, InstantiationBlob});
  uint32_t Index = I->MethodSpecs.size();
  I->MethodSpecIds[Key] = Index;
  return makeHandle(TableId::MethodSpec, Index);
}

EntityHandle MetadataBuilder::addField(EntityHandle Owner,
                                       FieldAttributes Flags, StringRef Name,
                                       uint32_t SigBlob) {
  assert(!I->Finalized && "addField() after finalize()");
  assert(I->isHandleOf(TableId::TypeDef, Owner) &&
         "a field's owner is a TypeDef");
  assert(I->isBlob(SigBlob) && "not a #Blob offset");
  if (I->isTableFull(TableId::Field, I->Fields.size()))
    return EntityHandle();
  I->Fields.push_back({Owner.getIndex(), Flags, I->intern(Name), SigBlob});
  ++I->TypeDefs[Owner.getIndex() - 1].NumFields;
  I->checkName(Name, [&] {
    return "field '" + I->getMemberName(Owner.getIndex(), Name) + "'";
  });
  return makeHandle(TableId::Field, I->Fields.size());
}

EntityHandle MetadataBuilder::addMethod(EntityHandle Owner,
                                        MethodAttributes Flags,
                                        MethodImplAttributes ImplFlags,
                                        StringRef Name, uint32_t SigBlob) {
  assert(!I->Finalized && "addMethod() after finalize()");
  assert(I->isHandleOf(TableId::TypeDef, Owner) &&
         "a method's owner is a TypeDef");
  assert(I->isBlob(SigBlob) && "not a #Blob offset");
  if (I->isTableFull(TableId::MethodDef, I->Methods.size()))
    return EntityHandle();
  I->Methods.push_back(
      {Owner.getIndex(), Flags, ImplFlags, I->intern(Name), SigBlob});
  ++I->TypeDefs[Owner.getIndex() - 1].NumMethods;
  I->checkName(Name, [&] {
    return "method '" + I->getMemberName(Owner.getIndex(), Name) + "'";
  });
  return makeHandle(TableId::MethodDef, I->Methods.size());
}

EntityHandle MetadataBuilder::addParam(EntityHandle Method, uint16_t Sequence,
                                       ParamAttributes Flags, StringRef Name) {
  assert(!I->Finalized && "addParam() after finalize()");
  assert(I->isHandleOf(TableId::MethodDef, Method) &&
         "a parameter's owner is a MethodDef");
  if (I->isTableFull(TableId::Param, I->Params.size()))
    return EntityHandle();
  I->Params.push_back({Method.getIndex(), Sequence, Flags, I->intern(Name)});
  I->checkName(Name, [&] {
    std::string Out = "parameter " + std::to_string(Sequence) + " '";
    appendShownName(Out, Name);
    return Out + "' of method '" + I->getMethodName(Method.getIndex()) + "'";
  });
  return makeHandle(TableId::Param, I->Params.size());
}

EntityHandle MetadataBuilder::addProperty(EntityHandle Owner,
                                          PropertyAttributes Flags,
                                          StringRef Name, uint32_t SigBlob) {
  assert(!I->Finalized && "addProperty() after finalize()");
  assert(I->isHandleOf(TableId::TypeDef, Owner) &&
         "a property's owner is a TypeDef");
  assert(I->isBlob(SigBlob) && "not a #Blob offset");
  if (I->isTableFull(TableId::Property, I->Properties.size()))
    return EntityHandle();
  I->Properties.push_back({Owner.getIndex(), Flags, I->intern(Name), SigBlob});
  ++I->TypeDefs[Owner.getIndex() - 1].NumProperties;
  I->checkName(Name, [&] {
    return "property '" + I->getMemberName(Owner.getIndex(), Name) + "'";
  });
  return makeHandle(TableId::Property, I->Properties.size());
}

void MetadataBuilder::addMethodSemantics(EntityHandle Property,
                                         MethodSemanticsAttributes Semantics,
                                         EntityHandle Method) {
  assert(!I->Finalized && "addMethodSemantics() after finalize()");
  assert(I->isHandleOf(TableId::Property, Property) &&
         "a MethodSemantics association is a Property");
  assert(I->isHandleOf(TableId::MethodDef, Method) &&
         "a MethodSemantics method is a MethodDef");
  if (I->isTableFull(TableId::MethodSemantics, I->MethodSemantics.size()))
    return;
  I->MethodSemantics.push_back({Semantics, Method.getIndex(), Property.Value});
}

void MetadataBuilder::addImplMap(EntityHandle Method, PInvokeAttributes Flags,
                                 StringRef ImportName, EntityHandle ModuleRef) {
  assert(!I->Finalized && "addImplMap() after finalize()");
  assert(I->canReference(CodedIndexKind::MemberForwarded, Method,
                         /*AllowNil=*/false) &&
         "an ImplMap forwards a MethodDef or Field");
  assert(I->isHandleOf(TableId::ModuleRef, ModuleRef) &&
         "an ImplMap's scope is a ModuleRef");
  if (I->isTableFull(TableId::ImplMap, I->ImplMaps.size()))
    return;
  I->ImplMaps.push_back(
      {Flags, Method.Value, I->intern(ImportName), ModuleRef.getIndex()});
  I->checkName(ImportName, [&] {
    std::string Out = "import name '";
    appendShownName(Out, ImportName);
    Out += "' of ";
    uint32_t Row = Method.getIndex();
    if (Method.getTable() == TableId::MethodDef)
      return Out + "method '" + I->getMethodName(Row) + "'";
    const FieldRow &F = I->Fields[Row - 1];
    return Out + "field '" + I->getMemberName(F.Owner, I->str(F.Name)) + "'";
  });
}

void MetadataBuilder::addClassLayout(EntityHandle TypeDef, uint16_t PackingSize,
                                     uint32_t ClassSize) {
  assert(!I->Finalized && "addClassLayout() after finalize()");
  assert(I->isHandleOf(TableId::TypeDef, TypeDef) &&
         "a ClassLayout's parent is a TypeDef");
  if (I->isTableFull(TableId::ClassLayout, I->ClassLayouts.size()))
    return;
  I->ClassLayouts.push_back({PackingSize, ClassSize, TypeDef.getIndex()});
}

void MetadataBuilder::addFieldLayout(EntityHandle Field, uint32_t Offset) {
  assert(!I->Finalized && "addFieldLayout() after finalize()");
  assert(I->isHandleOf(TableId::Field, Field) && "not a Field handle");
  if (I->isTableFull(TableId::FieldLayout, I->FieldLayouts.size()))
    return;
  I->FieldLayouts.push_back({Offset, Field.getIndex()});
}

void MetadataBuilder::addFieldRVA(EntityHandle Field,
                                  uint32_t MappedDataOffset) {
  assert(!I->Finalized && "addFieldRVA() after finalize()");
  assert(I->isHandleOf(TableId::Field, Field) && "not a Field handle");
  if (I->isTableFull(TableId::FieldRVA, I->FieldRVAs.size()))
    return;
  I->FieldRVAs.push_back({MappedDataOffset, Field.getIndex()});
}

void MetadataBuilder::addCustomAttribute(EntityHandle Parent,
                                         EntityHandle Constructor,
                                         uint32_t ValueBlob) {
  assert(!I->Finalized && "addCustomAttribute() after finalize()");
  assert(I->canReference(CodedIndexKind::HasCustomAttribute, Parent,
                         /*AllowNil=*/false) &&
         "not a row that can have custom attributes");
  assert(I->canReference(CodedIndexKind::CustomAttributeType, Constructor,
                         /*AllowNil=*/false) &&
         "an attribute's constructor is a MethodDef or MemberRef");
  assert(I->isBlob(ValueBlob) && "not a #Blob offset");
  if (I->isTableFull(TableId::CustomAttribute, I->CustomAttributes.size()))
    return;
  I->CustomAttributes.push_back({Parent.Value, Constructor.Value, ValueBlob});
}

void MetadataBuilder::setMethodBodyOffset(EntityHandle Method,
                                          uint32_t ILStreamOffset) {
  assert(I->isHandleOf(TableId::MethodDef, Method) && "not a MethodDef handle");
  MethodRow &M = I->Methods[Method.getIndex() - 1];
  M.HasBody = true;
  M.BodyOffset = ILStreamOffset;
}

Error MetadataBuilder::finalize(MethodBodyStream &Bodies) {
  Impl &M = *I;
  assert(!M.Finalized && "finalize() called twice");
  M.Finalized = true;
  // The checks recorded by the adds come first, then the per-type counts
  // with the pad rows.
  if (M.FirstError)
    return createStringError(errc::invalid_argument, *M.FirstError);
  if (Error E = M.addPadRows(Bodies))
    return E;
  if (Error E = M.checkMemberCounts())
    return E;
  M.numberRows();
  M.layOutStrings();
  M.computeLayout();
  M.Ready = true;
  return Error::success();
}

Error MetadataBuilder::checkMethodBodies() const {
  assert(I->Ready && "checkMethodBodies() before a successful finalize()");
  for (uint32_t Index : I->MethodNum.Order) {
    const MethodRow &M = I->Methods[Index];
    if (M.HasBody)
      continue;
    // Abstract, pinvokeimpl and runtime methods have RVA 0 (II.22.26).
    if (any(M.Flags &
            (MethodAttributes::Abstract | MethodAttributes::PinvokeImpl)) ||
        (M.ImplFlags & MethodImplAttributes::CodeTypeMask) ==
            MethodImplAttributes::Runtime)
      continue;
    return createStringError(errc::invalid_argument,
                             Twine(ErrorPrefix) + "method '" +
                                 I->getMemberName(M.Owner, I->str(M.Name)) +
                                 "' has no body");
  }
  return Error::success();
}

bool MetadataBuilder::isFinalized() const { return I->Finalized; }

uint32_t MetadataBuilder::getToken(EntityHandle H) const {
  if (H.isNil())
    return H.Value;
  assert(I->isValidHandle(H) && "not a row of this builder");
  if (!isMemberTable(H.getTable()))
    return H.Value;
  assert(I->Ready && "a member row's token before a successful finalize()");
  return I->getFinalToken(H.Value);
}

Expected<uint32_t> MetadataBuilder::resolveToken(TokenRef T) const {
  switch (T.getKind()) {
  case TokenRef::Kind::Token:
    return T.getValue();
  case TokenRef::Kind::None:
    return createStringError(errc::invalid_argument,
                             Twine(ErrorPrefix) + "an empty token reference");
  case TokenRef::Kind::External:
    return createStringError(errc::invalid_argument,
                             Twine(ErrorPrefix) + "external token reference " +
                                 Twine(T.getValue()) +
                                 " is not a metadata handle");
  case TokenRef::Kind::Handle:
    break;
  }
  EntityHandle H;
  H.Value = T.getValue();
  if (!I->isValidHandle(H))
    return createStringError(errc::invalid_argument,
                             Twine(ErrorPrefix) + "handle 0x" +
                                 Twine::utohexstr(H.Value) +
                                 " is not a row of this module");
  if (isMemberTable(H.getTable()) && !I->Ready)
    return createStringError(errc::invalid_argument,
                             Twine(ErrorPrefix) + "the token of " +
                                 getTableName(H.getTable()) + " handle 0x" +
                                 Twine::utohexstr(H.Value) +
                                 " is known only after finalize()");
  return getToken(H);
}

uint32_t MetadataBuilder::getRowCount(TableId T) const {
  return I->getRowCount(T);
}

size_t MetadataBuilder::getSerializedSize() const {
  assert(I->Ready && "getSerializedSize() before a successful finalize()");
  return I->SerializedSize;
}

size_t MetadataBuilder::getMvidOffset() const {
  assert(I->Ready && "getMvidOffset() before a successful finalize()");
  return I->StreamOffsets[GuidStream];
}

void MetadataBuilder::serialize(MutableArrayRef<uint8_t> Out,
                                uint32_t ILStreamRVA,
                                uint32_t MappedDataRVA) const {
  const Impl &M = *I;
  assert(M.Ready && "serialize() before a successful finalize()");
  assert(Out.size() >= M.SerializedSize && "metadata output too small");
  uint8_t *Base = Out.data();
  std::fill_n(Base, M.SerializedSize, 0);

  // The metadata root (II.24.2.1).
  uint8_t *P = Base;
  endian::write32le(P, MetadataSignature);
  endian::write16le(P + 4, MetadataMajorVersion);
  endian::write16le(P + 6, MetadataMinorVersion);
  endian::write32le(P + 8, 0);
  endian::write32le(P + 12, VersionFieldSize);
  std::memcpy(P + 16, MetadataVersionString.data(),
              MetadataVersionString.size());
  P += 16 + VersionFieldSize;
  endian::write16le(P, 0); // flags
  endian::write16le(P + 2, NumStreams);
  P += 4;
  // The stream headers (II.24.2.2).
  for (unsigned S = 0; S != NumStreams; ++S) {
    endian::write32le(P, M.StreamOffsets[S]);
    endian::write32le(P + 4, M.StreamSizes[S]);
    std::memcpy(P + 8, StreamNames[S].data(), StreamNames[S].size());
    P += getStreamHeaderSize(StreamNames[S]);
  }
  assert(P == Base + RootSize && "metadata root size");

  auto Stream = [&](StreamIndex S) {
    return MutableArrayRef<uint8_t>(Base + M.StreamOffsets[S],
                                    M.StreamSizes[S]);
  };
  M.writeTables(Base + M.StreamOffsets[TablesStream], ILStreamRVA,
                MappedDataRVA);
  M.StringHeap.write(Stream(StringsStream));
  M.UserStrings.write(Stream(UserStringsStream));
  M.Guids.write(Stream(GuidStream));
  M.Blobs.write(Stream(BlobStream));
}
