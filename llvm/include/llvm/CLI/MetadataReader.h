//===- llvm/CLI/MetadataReader.h - ECMA-335 metadata reader -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares MetadataReader, a lazy and zero-copy reader of the
// metadata of an ECMA-335 module (II.24): the metadata root and its streams,
// the #Strings, #US, #GUID and #Blob heaps, every column of the 45 tables of
// the #~ stream (with the layout of TableSchema.h's MetadataSizes), typed row
// getters, and lookups by owner, parent and nesting.
//
// Rows are decoded on access from the mapped file; nothing is decoded up
// front, so a reader over Framework mscorlib.dll costs only its headers until
// rows are read. Lookups build an index on first use (mutable state): a
// reader is not thread-safe, so use one reader per thread. The indexes never
// rely on the #~ Sorted bits or on the order of the rows.
//
// Every failure on input is an Error, never a crash or an assertion: the
// prefix "invalid CLI metadata: " for the root, the streams, the heaps and the
// rows, "invalid CLI image: " for the PE layer (PEImage), and the prefixes of
// the decoders that the reader calls (decodeMethodBody,
// decodeByteArrayAttribute). Uncompressed metadata (#-, with Ptr tables) is
// not supported.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_METADATAREADER_H
#define LLVM_CLI_METADATAREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/CLI/PEImage.h"
#include "llvm/CLI/TableSchema.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
namespace cli {

// Row structs: every string column as a StringRef into #Strings; blob columns
// as #Blob offsets; #GUID columns as 1-based #GUID indexes; every TableIdx and
// CodedIdx column as a token (nil = 0); list columns as row ranges
// [Begin, End) of 1-based row numbers.
struct ModuleRow {
  uint16_t Generation = 0;
  StringRef Name;
  /// A #GUID index.
  uint32_t Mvid = 0;
};
struct TypeRefRow {
  uint32_t ResolutionScope = 0;
  StringRef Name, Namespace;
};
struct TypeDefRow {
  TypeAttributes Flags{};
  StringRef Name, Namespace;
  uint32_t Extends = 0;
  uint32_t FieldBegin = 0, FieldEnd = 0, MethodBegin = 0, MethodEnd = 0;
};
struct FieldRow {
  FieldAttributes Flags{};
  StringRef Name;
  uint32_t Signature = 0;
};
struct MethodDefRow {
  uint32_t RVA = 0;
  MethodImplAttributes ImplFlags{};
  MethodAttributes Flags{};
  StringRef Name;
  uint32_t Signature = 0;
  uint32_t ParamBegin = 0, ParamEnd = 0;
};
struct ParamRow {
  ParamAttributes Flags{};
  uint16_t Sequence = 0;
  StringRef Name;
};
struct MemberRefRow {
  uint32_t Parent = 0;
  StringRef Name;
  uint32_t Signature = 0;
};
struct ConstantRow {
  /// An ElementType (the low byte of the column; the padding byte is dropped).
  uint8_t Type = 0;
  uint32_t Parent = 0;
  uint32_t Value = 0;
};
struct CustomAttributeRow {
  uint32_t Parent = 0;
  uint32_t Constructor = 0;
  uint32_t Value = 0;
};
struct ClassLayoutRow {
  uint16_t PackingSize = 0;
  uint32_t ClassSize = 0;
  uint32_t Parent = 0;
};
struct FieldLayoutRow {
  uint32_t Offset = 0;
  uint32_t Field = 0;
};
struct StandAloneSigRow {
  uint32_t Signature = 0;
};
struct PropertyRow {
  PropertyAttributes Flags{};
  StringRef Name;
  uint32_t Signature = 0;
};
struct MethodSemanticsRow {
  MethodSemanticsAttributes Semantics{};
  uint32_t Method = 0;
  uint32_t Association = 0;
};
struct ModuleRefRow {
  StringRef Name;
};
struct TypeSpecRow {
  uint32_t Signature = 0;
};
struct ImplMapRow {
  PInvokeAttributes Flags{};
  uint32_t MemberForwarded = 0;
  StringRef ImportName;
  uint32_t ImportScope = 0;
};
struct FieldRVARow {
  uint32_t RVA = 0;
  uint32_t Field = 0;
};
struct AssemblyRow {
  AssemblyHashAlgorithm HashAlg{};
  AssemblyVersion Version;
  AssemblyFlags Flags{};
  uint32_t PublicKey = 0;
  StringRef Name, Culture;
};
struct AssemblyRefRow {
  AssemblyVersion Version;
  AssemblyFlags Flags{};
  uint32_t PublicKeyOrToken = 0;
  StringRef Name, Culture;
  uint32_t HashValue = 0;
};
struct ExportedTypeRow {
  TypeAttributes Flags{};
  /// A TypeDef row id in the module that defines the type (a hint), not a
  /// token of this module.
  uint32_t TypeDefId = 0;
  StringRef Name, Namespace;
  uint32_t Implementation = 0;
};
struct ManifestResourceRow {
  uint32_t Offset = 0;
  ManifestResourceAttributes Flags{};
  StringRef Name;
  uint32_t Implementation = 0;
};
struct NestedClassRow {
  uint32_t NestedClass = 0, EnclosingClass = 0;
};
struct GenericParamRow {
  uint16_t Number = 0;
  GenericParamAttributes Flags{};
  uint32_t Owner = 0;
  StringRef Name;
};
struct MethodSpecRow {
  uint32_t Method = 0;
  uint32_t Instantiation = 0;
};

/// The metadata of one module. The row getters take 1-based row numbers; the
/// lookups take tokens. Checks at create: the root signature, a version
/// string of at most 255 bytes whose length field is a multiple of 4, stream
/// headers inside the root with NUL-terminated names padded to 4, a #~
/// stream (#- is an Error; unknown streams such as #Pdb and #JTD are
/// ignored), no Valid bit for a table without a schema, no rows in the Ptr
/// tables, row counts of at most MaxTableRows, the HEAP_EXTRA_DATA field, and
/// the table data inside #~.
class MetadataReader {
public:
  /// The most columns that a table has (Assembly and AssemblyRef).
  static constexpr unsigned MaxColumns = 9;

  /// The metadata of a PE image with a CLI header. The caller keeps \p File
  /// alive.
  LLVM_ABI static Expected<std::unique_ptr<MetadataReader>>
  create(ArrayRef<uint8_t> File);
  /// The metadata root \p Root without an image (tests); getImage() is null.
  /// The reader reads a copy of \p Root, which the caller need not keep
  /// alive.
  LLVM_ABI static Expected<std::unique_ptr<MetadataReader>>
  createFromMetadata(ArrayRef<uint8_t> Root);

  LLVM_ABI ~MetadataReader();
  MetadataReader(const MetadataReader &) = delete;
  MetadataReader &operator=(const MetadataReader &) = delete;

  /// Null for createFromMetadata.
  const PEImage *getImage() const { return Image ? &*Image : nullptr; }
  /// The root's version string ("v4.0.30319"), without its NUL padding.
  StringRef getVersionString() const { return Version; }
  /// The #~ HeapSizes byte as written.
  uint8_t getHeapSizeFlags() const { return Sizes->getHeapSizeFlags(); }
  const MetadataSizes &getSizes() const { return *Sizes; }
  /// An Assembly row exists.
  bool isAssembly() const { return getRowCount(TableId::Assembly) != 0; }
  /// 0 for a table id without a schema.
  LLVM_ABI uint32_t getRowCount(TableId T) const;
  /// The size of "#Strings", "#US", "#GUID" or "#Blob" as its stream header
  /// gives it; 0 for a missing stream or another name.
  LLVM_ABI uint32_t getHeapSize(StringRef StreamName) const;

  /// The raw value of column \p Column of row \p Row (1-based) of table \p T:
  /// constants as they are, heap columns as offsets (#GUID: indexes), checked
  /// against their heap, and index columns as tokens (nil = 0), checked
  /// against the row counts. A list column may name the row after the last.
  LLVM_ABI Expected<uint32_t> getColumnValue(TableId T, uint32_t Row,
                                             unsigned Column) const;

  /// The NUL-terminated string at \p Offset of #Strings; offset 0 is "".
  LLVM_ABI Expected<StringRef> getString(uint32_t Offset) const;
  /// The blob at \p Offset of #Blob (a view, without its length prefix);
  /// offset 0 is the empty blob.
  LLVM_ABI Expected<ArrayRef<uint8_t>> getBlob(uint32_t Offset) const;
  /// The #US string at \p Offset (an ldstr token's low 24 bits) as UTF-8. A
  /// lone surrogate is an Error.
  LLVM_ABI Expected<std::string> getUserString(uint32_t Offset) const;
  /// The #US string at \p Offset as raw UTF-16LE code units (the trailing
  /// byte dropped).
  LLVM_ABI Expected<ArrayRef<uint8_t>>
  getUserStringUTF16(uint32_t Offset) const;
  /// The #GUID entry \p Index (1-based; 0 is an Error), in on-disk byte
  /// order.
  LLVM_ABI Expected<std::array<uint8_t, 16>> getGuid(uint32_t Index) const;

  /// Row 1 of Module and Assembly; an Error if the table is empty.
  LLVM_ABI Expected<ModuleRow> getModule() const;
  LLVM_ABI Expected<AssemblyRow> getAssembly() const;
  /// One getter per row struct; \p Row is 1-based.
  LLVM_ABI Expected<TypeRefRow> getTypeRef(uint32_t Row) const;
  LLVM_ABI Expected<TypeDefRow> getTypeDef(uint32_t Row) const;
  LLVM_ABI Expected<FieldRow> getField(uint32_t Row) const;
  LLVM_ABI Expected<MethodDefRow> getMethodDef(uint32_t Row) const;
  LLVM_ABI Expected<ParamRow> getParam(uint32_t Row) const;
  LLVM_ABI Expected<MemberRefRow> getMemberRef(uint32_t Row) const;
  LLVM_ABI Expected<ConstantRow> getConstant(uint32_t Row) const;
  LLVM_ABI Expected<CustomAttributeRow> getCustomAttribute(uint32_t Row) const;
  LLVM_ABI Expected<ClassLayoutRow> getClassLayout(uint32_t Row) const;
  LLVM_ABI Expected<FieldLayoutRow> getFieldLayout(uint32_t Row) const;
  LLVM_ABI Expected<StandAloneSigRow> getStandAloneSig(uint32_t Row) const;
  LLVM_ABI Expected<PropertyRow> getProperty(uint32_t Row) const;
  /// Named apart from the getMethodSemantics(PropertyToken) lookup below,
  /// which has the same parameter type.
  LLVM_ABI Expected<MethodSemanticsRow>
  getMethodSemanticsRow(uint32_t Row) const;
  LLVM_ABI Expected<ModuleRefRow> getModuleRef(uint32_t Row) const;
  LLVM_ABI Expected<TypeSpecRow> getTypeSpec(uint32_t Row) const;
  LLVM_ABI Expected<ImplMapRow> getImplMap(uint32_t Row) const;
  LLVM_ABI Expected<FieldRVARow> getFieldRVA(uint32_t Row) const;
  LLVM_ABI Expected<AssemblyRefRow> getAssemblyRef(uint32_t Row) const;
  LLVM_ABI Expected<ExportedTypeRow> getExportedType(uint32_t Row) const;
  LLVM_ABI Expected<ManifestResourceRow>
  getManifestResource(uint32_t Row) const;
  LLVM_ABI Expected<NestedClassRow> getNestedClass(uint32_t Row) const;
  LLVM_ABI Expected<GenericParamRow> getGenericParam(uint32_t Row) const;
  LLVM_ABI Expected<MethodSpecRow> getMethodSpec(uint32_t Row) const;

  /// The TypeDef token whose field or method run holds a Field or MethodDef
  /// token.
  LLVM_ABI Expected<uint32_t>
  getOwnerTypeDef(uint32_t FieldOrMethodToken) const;
  /// The enclosing TypeDef token of a nested TypeDef (NestedClass), or 0.
  LLVM_ABI Expected<uint32_t> getEnclosingType(uint32_t TypeDefToken) const;
  /// The 1-based CustomAttribute rows whose Parent is \p ParentToken, in
  /// table order.
  LLVM_ABI Expected<SmallVector<uint32_t, 4>>
  getCustomAttributes(uint32_t ParentToken) const;
  /// The ImplMap row of a MethodDef (or Field) token, if any.
  LLVM_ABI Expected<std::optional<ImplMapRow>>
  findImplMap(uint32_t MethodDefToken) const;
  /// The FieldRVA row of a Field token, if any.
  LLVM_ABI Expected<std::optional<FieldRVARow>>
  findFieldRVA(uint32_t FieldToken) const;
  /// The ClassLayout row of a TypeDef token, if any.
  LLVM_ABI Expected<std::optional<ClassLayoutRow>>
  findClassLayout(uint32_t TypeDefToken) const;
  /// The 1-based Property rows [first, second) of a TypeDef token (its
  /// PropertyMap run); an empty range (0, 0) without a PropertyMap row.
  LLVM_ABI Expected<std::pair<uint32_t, uint32_t>>
  getPropertyRange(uint32_t TypeDefToken) const;
  /// The MethodSemantics rows whose Association is \p PropertyToken (a
  /// Property or Event token), in table order.
  LLVM_ABI Expected<SmallVector<MethodSemanticsRow, 2>>
  getMethodSemantics(uint32_t PropertyToken) const;
  /// The name of a TypeDef or TypeRef token: "NS.Name", or "Name" without a
  /// namespace; a nested type is "<enclosing name>/<its own name>".
  LLVM_ABI Expected<std::string> getTypeFullName(uint32_t Token) const;
  /// The payloads of the uint8[] attributes on \p ParentToken whose
  /// constructor's declaring type (a TypeDef that owns the MethodDef, or the
  /// TypeDef or TypeRef parent of the MemberRef) is \p Namespace.\p Name, in
  /// table order, decoded with decodeByteArrayAttribute (its Errors are
  /// returned unchanged). The payloads are views into #Blob.
  LLVM_ABI Expected<SmallVector<ArrayRef<uint8_t>, 1>>
  getByteArrayAttributes(uint32_t ParentToken, StringRef Namespace,
                         StringRef Name) const;
  /// The decoded body of a MethodDef token; an Error if its RVA is 0 or the
  /// reader has no image.
  LLVM_ABI Expected<MethodBodyView>
  getMethodBody(uint32_t MethodDefToken) const;
  /// The \p Size bytes of a Field token's FieldRVA data; an Error without a
  /// FieldRVA row or an image.
  LLVM_ABI Expected<ArrayRef<uint8_t>> getFieldData(uint32_t FieldToken,
                                                    uint32_t Size) const;

private:
  /// Where table T's rows are and the layout of a row.
  struct TableView {
    ArrayRef<uint8_t> Data;
    uint32_t Rows = 0;
    uint32_t RowSize = 0;
    uint8_t NumColumns = 0;
    std::array<uint8_t, MaxColumns> Offsets{}, Widths{};
  };
  struct Indexes;

  MetadataReader();
  Error parseRoot(ArrayRef<uint8_t> Root);
  Error parseTables(ArrayRef<uint8_t> Stream);
  /// Checks that \p Row (1-based) exists in table \p T.
  Error checkRow(TableId T, uint32_t Row) const;
  /// Checks that \p Token is a row of one of \p Tables.
  Error checkToken(uint32_t Token, ArrayRef<TableId> Tables,
                   StringRef What) const;
  /// The run [Begin, End) of list column \p Column of row \p Row of table
  /// \p T, whose list is \p Target.
  Expected<std::pair<uint32_t, uint32_t>>
  getListRange(TableId T, uint32_t Row, unsigned Column, TableId Target) const;
  Expected<StringRef> getStringColumn(TableId T, uint32_t Row,
                                      unsigned Column) const;
  Indexes &getIndexes() const;

  std::optional<PEImage> Image;
  /// createFromMetadata's copy of the root, which the views below point into.
  std::vector<uint8_t> OwnedRoot;
  StringRef Version;
  ArrayRef<uint8_t> Strings, UserStrings, Guids, Blobs;
  std::optional<MetadataSizes> Sizes;
  std::array<TableView, NumTableIds> Tables;
  mutable std::unique_ptr<Indexes> Idx;
};

/// The public-key token of \p PublicKey: the last 8 bytes of its SHA-1 hash,
/// in reverse order (II.6.3).
LLVM_ABI std::array<uint8_t, 8>
computePublicKeyToken(ArrayRef<uint8_t> PublicKey);

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_METADATAREADER_H
