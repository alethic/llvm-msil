//===- Dump.cpp - ECMA-335 module dumper ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the dumps of llvm-cil-dump: canonical listing v1 with
// its options (the listing of plan 5.7.10, the normalizations of 5.12.6), the
// headers, the tables, and the link-info record and symbol tables.
//
// Canonical listing v1 has two implementations that must agree byte for byte
// on every image: this one and M16's mdcanon (msil/utils/mdtools/mdcanon.cs).
// Where the specification leaves a detail open, both choose alike, as plan
// section 12's row "5.7.10 clarified (canonical listing v1 ...)" records:
// - <propsig> is '[instance ]<type>(<p>, <p>)';
// - the header lists come kind by kind (assemblyref, moduleref, file,
//   exportedtype, resource), each sorted by its text (or in table order);
// - .implements lines, .param lines (Sequence order, ties in table order) and
//   EH clauses keep their order under both --order values, and sort keys use
//   the raw names ('<n> <type>', '<n> <sig>', '<full>');
// - '.data <n> bytes' needs a field signature that is exactly a primitive
//   (bool, char, int8 to uint64, float32, float64) or VALUETYPE of a TypeDef
//   with a ClassLayout row; anything else prints '.data (unknown size)'. Data
//   past a section's raw data reads as zeros up to its VirtualSize, and an
//   RVA outside every section is an error;
// - 'const <hex>' is the Constant row's Value blob; 'marshal <hex>' is printed
//   when the FieldMarshal row's NativeType blob index is not 0 (an empty blob
//   prints 'marshal -'); a branch target before the code prints
//   '<offset -0x<X>>'; a switch without targets prints 'switch' alone;
// - MethodSemantics 0x0002 prints .get, 0x0001 .set, any other value .other;
// - there is no entrypoint line when CorFlags has NATIVE_ENTRYPOINT, and an
//   entry point that is not a MethodDef is an error;
// - names inside [...] scopes and TypeRef names print raw, unquoted;
// - not listed: custom attributes of other parents (Param, InterfaceImpl,
//   GenericParam, ...), events, generic parameters, MethodImpl, DeclSecurity;
// - a type inside more than 64 PTR, BYREF, SZARRAY, ARRAY, GENERICINST, FNPTR
//   or PINNED constructors is an error. A TypeSpec named inside a signature
//   continues the depth of the type that names it (its own type is one level
//   deeper), counted across blobs, which bounds TypeSpec cycles; a TypeSpec
//   named outside a signature starts at depth 0.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/Dump.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/ILDecoder.h"
#include "llvm/CLI/LinkInfo.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/CLI/Opcodes.h"
#include "llvm/CLI/PEImage.h"
#include "llvm/CLI/Signature.h"
#include "llvm/CLI/SymbolTable.h"
#include "llvm/CLI/TableSchema.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::cli;
using namespace llvm::support;

#define DEBUG_TYPE "cli-dump"

/// The deepest nesting of types that the listing prints (mdcanon's limit).
static constexpr unsigned MaxTypeDepth = 64;

static Error metadataError(const Twine &Msg) {
  return createStringError(errc::illegal_byte_sequence,
                           "invalid CLI metadata: " + Msg);
}

static Error imageError(const Twine &Msg) {
  return createStringError(errc::illegal_byte_sequence,
                           "invalid CLI image: " + Msg);
}

static Error signatureError(const Twine &Msg) {
  return createStringError(errc::illegal_byte_sequence,
                           "invalid signature: " + Msg);
}

static Error noImageError() {
  return createStringError(errc::invalid_argument,
                           "invalid CLI image: the metadata was read without "
                           "its PE image");
}

/// "0x" and \p Digits uppercase hex digits.
static std::string hex(uint64_t V, unsigned Digits) {
  return "0x" + utohexstr(V, /*LowerCase=*/false, Digits);
}

/// "0x" and the uppercase hex digits of \p V without leading zeros.
static std::string hex(uint64_t V) { return "0x" + utohexstr(V); }

/// '<n>' of canonical listing v1 without its quotes: the raw bytes, with '\'
/// as '\\', ''' as '\'' and the bytes 0x00-0x1F and 0x7F as '\xHH'.
static std::string escaped(StringRef S) {
  std::string Out;
  Out.reserve(S.size());
  for (char Ch : S) {
    unsigned char C = Ch;
    if (C == '\\') {
      Out += "\\\\";
    } else if (C == '\'') {
      Out += "\\'";
    } else if (C < 0x20 || C == 0x7F) {
      Out += "\\x";
      Out += hexdigit(C >> 4);
      Out += hexdigit(C & 15);
    } else {
      Out += Ch;
    }
  }
  return Out;
}

static std::string quoted(StringRef S) { return "'" + escaped(S) + "'"; }

/// <hex>: uppercase byte pairs separated by one blank; '-' when empty.
static std::string hexBytes(ArrayRef<uint8_t> B) {
  if (B.empty())
    return "-";
  std::string Out;
  Out.reserve(B.size() * 3);
  for (size_t I = 0, E = B.size(); I != E; ++I) {
    if (I)
      Out += ' ';
    Out += hexdigit(B[I] >> 4);
    Out += hexdigit(B[I] & 15);
  }
  return Out;
}

static std::string nsName(StringRef NS, StringRef Name) {
  if (NS.empty())
    return Name.str();
  return (NS + "." + Name).str();
}

static std::string versionText(const AssemblyVersion &V) {
  return (Twine(V.Major) + "." + Twine(V.Minor) + "." + Twine(V.Build) + "." +
          Twine(V.Revision))
      .str();
}

/// A GUID in its 8-4-4-4-12 form, lowercase, from its on-disk bytes (a
/// little-endian u32 and two u16, then eight bytes).
static std::string formatGuid(const std::array<uint8_t, 16> &G) {
  std::string S = utohexstr(endian::read32le(G.data()), true, 8) + "-" +
                  utohexstr(endian::read16le(G.data() + 4), true, 4) + "-" +
                  utohexstr(endian::read16le(G.data() + 6), true, 4) + "-";
  for (unsigned I = 8; I != 16; ++I) {
    if (I == 10)
      S += '-';
    S += hexdigit(G[I] >> 4, /*LowerCase=*/true);
    S += hexdigit(G[I] & 15, /*LowerCase=*/true);
  }
  return S;
}

static TableId tableOf(uint32_t Token) {
  return static_cast<TableId>(getTokenType(Token));
}

static bool isTable(uint32_t Token, TableId T) {
  return getTokenType(Token) == static_cast<uint8_t>(T);
}

/// The name of a token's table, or its type byte if it has none.
static std::string tableNameOf(uint32_t Token) {
  uint8_t T = getTokenType(Token);
  if (T <= static_cast<uint8_t>(TableId::GenericParamConstraint))
    return getTableName(static_cast<TableId>(T)).str();
  return "table " + hex(T, 2);
}

/// The bytes [RVA, RVA + Size) as the loader maps them: the first section
/// whose mapped range (VirtualSize, or SizeOfRawData when VirtualSize is 0)
/// holds RVA must hold all of them; its raw data is read and the rest up to
/// the mapped size reads as zeros.
static Expected<std::vector<uint8_t>> readMapped(const PEImage &Image,
                                                 uint32_t RVA, uint64_t Size) {
  for (const PESectionInfo &S : Image.sections()) {
    uint64_t Extent = S.VirtualSize ? S.VirtualSize : S.SizeOfRawData;
    if (RVA < S.VirtualAddress || RVA >= uint64_t(S.VirtualAddress) + Extent)
      continue;
    uint64_t Rel = RVA - S.VirtualAddress;
    if (Rel + Size > Extent)
      return imageError("data at RVA " + hex(RVA) + " (" + Twine(Size) +
                        " bytes) extends past its section");
    ArrayRef<uint8_t> File = Image.getFile();
    std::vector<uint8_t> Out(Size, 0);
    for (uint64_t K = 0; K != Size; ++K) {
      uint64_t At = Rel + K;
      if (At >= S.SizeOfRawData)
        break;
      uint64_t Offset = uint64_t(S.PointerToRawData) + At;
      if (Offset >= File.size())
        return imageError("section data at RVA " + hex(RVA) +
                          " runs past the end of the file");
      Out[K] = File[Offset];
    }
    return Out;
  }
  return imageError("RVA " + hex(RVA) + " is not in a section");
}

namespace {

/// The text of each TypeRef value of one signature.
using RefTexts = DenseMap<uint32_t, std::string>;

/// (sort key, text) of a type, field, property or method block.
using Block = std::pair<std::string, std::string>;

/// One image's canonical listing.
class Lister {
public:
  Lister(const MetadataReader &R, const CanonicalOptions &O)
      : R(R), O(O), Image(*R.getImage()),
        FullNames(R.getRowCount(TableId::TypeDef) + 1),
        TypeSpecs(R.getRowCount(TableId::TypeSpec) + 1) {}

  Error run(raw_ostream &OS);

private:
  struct TypeSpecInfo {
    enum State : uint8_t { Unvisited, Visiting, Done } St = Unvisited;
    TypeSig Sig;
    /// The depth of its deepest type, its own type at depth 0, the types of
    /// the TypeSpecs that it names included.
    unsigned Height = 0;
    std::optional<std::string> Text;
  };

  // Names and types.
  Error checkRow(uint32_t Token, StringRef What) const;
  Error checkEntity(uint32_t Token, StringRef What) const;
  Expected<StringRef> stringColumn(TableId T, uint32_t Row,
                                   unsigned Column) const;
  Expected<std::string> fullName(uint32_t TypeDefRow, unsigned Depth = 0);
  Expected<std::string> typeRefText(uint32_t TypeRefRow, unsigned Depth);
  Expected<std::string> exportedTypeName(uint32_t Row, unsigned Depth);
  Expected<std::string> typeText(uint32_t Token, unsigned Depth);
  Expected<std::string> typeSpecText(uint32_t Row, unsigned Depth);
  Expected<unsigned> typeSpecHeight(uint32_t Row, unsigned Budget);
  Expected<unsigned> sigHeight(const TypeSig &T, unsigned Depth,
                               unsigned Budget);
  Error collectRef(uint32_t Token, unsigned Depth, RefTexts &Texts);
  Error collectRefs(const TypeSig &T, unsigned Depth, RefTexts &Texts);
  Error collectRefs(const MethodSig &S, unsigned Depth, RefTexts &Texts);
  Expected<std::string> typeSigText(const TypeSig &T);
  Expected<std::string> methodSigText(const MethodSig &S);
  Expected<std::string> methodSigText(uint32_t Blob);
  Expected<std::string> fieldSigText(uint32_t Blob);
  Expected<std::string> memberRefSigText(ArrayRef<uint8_t> Blob);
  Expected<std::string> propertySigText(uint32_t Blob);
  Expected<std::string> methodText(uint32_t Token);
  Expected<std::string> fieldText(uint32_t Token);
  Expected<std::string> entityText(uint32_t Token);
  Expected<std::string> userStringText(uint32_t Token);

  // Lookups that the reader does not have, built on first use.
  Expected<std::optional<uint32_t>> fieldOffset(uint32_t FieldRow);
  Expected<std::optional<uint32_t>> constantValue(uint32_t Token);
  Expected<std::optional<uint32_t>> marshalBlob(uint32_t Token);
  Expected<SmallVector<uint32_t, 2>> interfaces(uint32_t TypeDefRow);

  // The listing.
  Error header(raw_ostream &OS);
  Error types(raw_ostream &OS);
  Expected<std::string> typeBlock(uint32_t Row, StringRef Full);
  Expected<std::string> fieldBlock(uint32_t Row, const FieldRow &F,
                                   StringRef Type);
  Expected<std::string> propertyBlock(uint32_t Row, const PropertyRow &P,
                                      StringRef Sig);
  Expected<std::string> methodBlock(uint32_t Row, const MethodDefRow &M,
                                    StringRef Sig);
  Error customAttributes(raw_ostream &OS, uint32_t Parent, StringRef Prefix);
  Expected<int64_t> fieldDataSize(uint32_t SigBlob);
  Error body(raw_ostream &OS, uint32_t MethodToken);
  Error instructions(raw_ostream &OS, ArrayRef<uint8_t> Code,
                     ArrayRef<ExceptionClause> Clauses);
  StringRef opName(const OpcodeInfo &Info) const;
  void stats(raw_ostream &OS) const;

  /// Sorts lines by their text, unless --order=decl.
  void orderLines(std::vector<std::string> &Lines) const;
  /// Sorts blocks by key, then by text, ties in declaration order, unless
  /// --order=decl; then writes their texts.
  void emitBlocks(raw_ostream &OS, std::vector<Block> &Blocks) const;

  const MetadataReader &R;
  const CanonicalOptions &O;
  const PEImage &Image;
  /// The <full> names of TypeDef rows.
  std::vector<std::optional<std::string>> FullNames;
  /// Indexed by row; never resized.
  std::vector<TypeSpecInfo> TypeSpecs;
  /// The texts of MethodDef, MemberRef, MethodSpec and Field tokens.
  DenseMap<uint32_t, std::string> EntityTexts;
  std::optional<std::vector<uint32_t>> FieldLayoutRows;
  std::optional<DenseMap<uint32_t, uint32_t>> Constants, Marshals;
  /// (TypeDef row, interface token), stable-sorted by row.
  std::optional<std::vector<std::pair<uint32_t, uint32_t>>> InterfaceImpls;
};

} // namespace

//===----------------------------------------------------------------------===//
// Names and types
//===----------------------------------------------------------------------===//

Error Lister::checkRow(uint32_t Token, StringRef What) const {
  uint32_t Row = getTokenRow(Token);
  uint8_t T = getTokenType(Token);
  if (T > static_cast<uint8_t>(TableId::GenericParamConstraint) || Row == 0 ||
      Row > R.getRowCount(static_cast<TableId>(T)))
    return metadataError("invalid " + What + " token " + hex(Token, 8));
  return Error::success();
}

/// A token of a table that IL operands and the CLI header may name, whose
/// row exists.
Error Lister::checkEntity(uint32_t Token, StringRef What) const {
  switch (tableOf(Token)) {
  case TableId::TypeRef:
  case TableId::TypeDef:
  case TableId::Field:
  case TableId::MethodDef:
  case TableId::MemberRef:
  case TableId::StandAloneSig:
  case TableId::ModuleRef:
  case TableId::TypeSpec:
  case TableId::AssemblyRef:
  case TableId::File:
  case TableId::ExportedType:
  case TableId::MethodSpec:
    return checkRow(Token, What);
  default:
    return metadataError("invalid " + What + " token " + hex(Token, 8));
  }
}

Expected<StringRef> Lister::stringColumn(TableId T, uint32_t Row,
                                         unsigned Column) const {
  Expected<uint32_t> Offset = R.getColumnValue(T, Row, Column);
  if (!Offset)
    return Offset.takeError();
  return R.getString(*Offset);
}

/// <full>: NS.Name, nested <enclosing full>/<NS.Name>.
Expected<std::string> Lister::fullName(uint32_t Row, unsigned Depth) {
  if (FullNames[Row])
    return *FullNames[Row];
  if (Depth > MaxTypeDepth)
    return metadataError("nested types deeper than " + Twine(MaxTypeDepth) +
                         " levels");
  Expected<TypeDefRow> T = R.getTypeDef(Row);
  if (!T)
    return T.takeError();
  std::string S = nsName(T->Namespace, T->Name);
  Expected<uint32_t> Enclosing =
      R.getEnclosingType(makeToken(TableId::TypeDef, Row));
  if (!Enclosing)
    return Enclosing.takeError();
  if (*Enclosing) {
    Expected<std::string> Outer = fullName(getTokenRow(*Enclosing), Depth + 1);
    if (!Outer)
      return Outer.takeError();
    S = *Outer + "/" + S;
  }
  FullNames[Row] = S;
  return S;
}

Expected<std::string> Lister::typeRefText(uint32_t Row, unsigned Depth) {
  if (Depth > MaxTypeDepth)
    return metadataError("nested type references deeper than " +
                         Twine(MaxTypeDepth) + " levels");
  Expected<TypeRefRow> T = R.getTypeRef(Row);
  if (!T)
    return T.takeError();
  std::string Name = nsName(T->Namespace, T->Name);
  uint32_t Scope = T->ResolutionScope;
  if (Scope == 0)
    return Name;
  switch (tableOf(Scope)) {
  case TableId::AssemblyRef: {
    if (O.StripScopes)
      return "[*]" + Name;
    // AssemblyRef: Name is column 6.
    Expected<StringRef> A =
        stringColumn(TableId::AssemblyRef, getTokenRow(Scope), 6);
    if (!A)
      return A.takeError();
    return ("[" + *A + "]" + Name).str();
  }
  case TableId::ModuleRef: {
    Expected<StringRef> M =
        stringColumn(TableId::ModuleRef, getTokenRow(Scope), 0);
    if (!M)
      return M.takeError();
    return ("[.module " + *M + "]" + Name).str();
  }
  case TableId::Module:
    return "[.module self]" + Name;
  case TableId::TypeRef: {
    Expected<std::string> Outer = typeRefText(getTokenRow(Scope), Depth + 1);
    if (!Outer)
      return Outer.takeError();
    return *Outer + "/" + Name;
  }
  default:
    return metadataError("TypeRef row " + Twine(Row) +
                         " has an invalid resolution scope " + hex(Scope, 8));
  }
}

Expected<std::string> Lister::exportedTypeName(uint32_t Row, unsigned Depth) {
  if (Depth > MaxTypeDepth)
    return metadataError("nested exported types deeper than " +
                         Twine(MaxTypeDepth) + " levels");
  Expected<ExportedTypeRow> E = R.getExportedType(Row);
  if (!E)
    return E.takeError();
  std::string Name = nsName(E->Namespace, E->Name);
  if (E->Implementation && isTable(E->Implementation, TableId::ExportedType)) {
    Expected<std::string> Outer =
        exportedTypeName(getTokenRow(E->Implementation), Depth + 1);
    if (!Outer)
      return Outer.takeError();
    return *Outer + "/" + Name;
  }
  return Name;
}

/// <type> outside signatures. \p Depth is the depth at which a TypeSpec's own
/// type is decoded: 0 for a token outside a signature, one more than the
/// naming type's depth inside one. TypeDef and TypeRef names do not depend
/// on it.
Expected<std::string> Lister::typeText(uint32_t Token, unsigned Depth) {
  switch (tableOf(Token)) {
  case TableId::TypeDef: {
    if (Error E = checkRow(Token, "TypeDef"))
      return std::move(E);
    Expected<std::string> Full = fullName(getTokenRow(Token));
    if (!Full)
      return Full.takeError();
    return quoted(*Full);
  }
  case TableId::TypeRef:
    if (Error E = checkRow(Token, "TypeRef"))
      return std::move(E);
    return typeRefText(getTokenRow(Token), 0);
  case TableId::TypeSpec:
    if (Error E = checkRow(Token, "TypeSpec"))
      return std::move(E);
    return typeSpecText(getTokenRow(Token), Depth);
  default:
    return metadataError("a " + tableNameOf(Token) + " token (" +
                         hex(Token, 8) + ") is not a type");
  }
}

Expected<std::string> Lister::typeSpecText(uint32_t Row, unsigned Depth) {
  if (Depth > MaxTypeDepth)
    return signatureError("nesting deeper than " + Twine(MaxTypeDepth) +
                          " levels (TypeSpec " +
                          hex(makeToken(TableId::TypeSpec, Row), 8) + ")");
  Expected<unsigned> Height = typeSpecHeight(Row, MaxTypeDepth - Depth);
  if (!Height)
    return Height.takeError();
  if (!TypeSpecs[Row].Text) {
    // The height check covered every TypeSpec that this one names, at its
    // real depth, so the text below meets no depth error.
    Expected<std::string> Text = typeSigText(TypeSpecs[Row].Sig);
    if (!Text)
      return Text.takeError();
    TypeSpecs[Row].Text = std::move(*Text);
  }
  return *TypeSpecs[Row].Text;
}

/// The height of TypeSpec \p Row, which must be at most \p Budget (the
/// deepest depth that its own type may reach). A TypeSpec that names itself,
/// directly or not, has no height.
Expected<unsigned> Lister::typeSpecHeight(uint32_t Row, unsigned Budget) {
  uint32_t Token = makeToken(TableId::TypeSpec, Row);
  // TypeSpecs is never resized, so I stays valid across the recursion.
  TypeSpecInfo &I = TypeSpecs[Row];
  if (I.St == TypeSpecInfo::Visiting)
    return signatureError("nesting deeper than " + Twine(MaxTypeDepth) +
                          " levels (TypeSpec " + hex(Token, 8) +
                          " names itself)");
  if (I.St == TypeSpecInfo::Unvisited) {
    Expected<TypeSpecRow> Spec = R.getTypeSpec(Row);
    if (!Spec)
      return Spec.takeError();
    Expected<ArrayRef<uint8_t>> Blob = R.getBlob(Spec->Signature);
    if (!Blob)
      return Blob.takeError();
    Expected<TypeSig> Sig = decodeTypeSpec(*Blob);
    if (!Sig)
      return Sig.takeError();
    I.Sig = std::move(*Sig);
    I.St = TypeSpecInfo::Visiting;
    Expected<unsigned> Height = sigHeight(I.Sig, 0, Budget);
    if (!Height) {
      I.St = TypeSpecInfo::Unvisited;
      return Height.takeError();
    }
    I.Height = *Height;
    I.St = TypeSpecInfo::Done;
  }
  if (I.Height > Budget)
    return signatureError("nesting deeper than " + Twine(MaxTypeDepth) +
                          " levels (TypeSpec " + hex(Token, 8) + ")");
  return I.Height;
}

/// The deepest depth that \p T at \p Depth reaches, with the types of the
/// TypeSpecs that it names one level deeper than the naming type; an error
/// past \p Budget.
Expected<unsigned> Lister::sigHeight(const TypeSig &T, unsigned Depth,
                                     unsigned Budget) {
  if (Depth > Budget)
    return signatureError("nesting deeper than " + Twine(MaxTypeDepth) +
                          " levels");
  unsigned Max = Depth;
  auto Ref = [&](uint32_t Token) -> Error {
    if (Error E = checkRow(Token, "signature type"))
      return E;
    if (!isTable(Token, TableId::TypeSpec))
      return Error::success();
    if (Depth + 1 > Budget)
      return signatureError("nesting deeper than " + Twine(MaxTypeDepth) +
                            " levels (TypeSpec " + hex(Token, 8) + ")");
    Expected<unsigned> H =
        typeSpecHeight(getTokenRow(Token), Budget - (Depth + 1));
    if (!H)
      return H.takeError();
    Max = std::max(Max, Depth + 1 + *H);
    return Error::success();
  };
  auto Nested = [&](const TypeSig &N) -> Error {
    Expected<unsigned> H = sigHeight(N, Depth + 1, Budget);
    if (!H)
      return H.takeError();
    Max = std::max(Max, *H);
    return Error::success();
  };
  for (const CustomModifier &M : T.getModifiers())
    if (Error E = Ref(M.Type))
      return std::move(E);
  switch (T.getElementType()) {
  case ELEMENT_TYPE_CLASS:
  case ELEMENT_TYPE_VALUETYPE:
    if (Error E = Ref(T.getTypeRef()))
      return std::move(E);
    break;
  case ELEMENT_TYPE_GENERICINST:
    if (Error E = Ref(T.getTypeRef()))
      return std::move(E);
    for (const TypeSig &A : T.getOperands())
      if (Error E = Nested(A))
        return std::move(E);
    break;
  case ELEMENT_TYPE_PTR:
  case ELEMENT_TYPE_BYREF:
  case ELEMENT_TYPE_SZARRAY:
  case ELEMENT_TYPE_ARRAY:
  case ELEMENT_TYPE_PINNED:
    if (Error E = Nested(T.getOperands()[0]))
      return std::move(E);
    break;
  case ELEMENT_TYPE_FNPTR: {
    const MethodSig &S = T.getFunctionSig();
    if (Error E = Nested(S.Return))
      return std::move(E);
    for (const TypeSig &P : S.Params)
      if (Error E = Nested(P))
        return std::move(E);
    break;
  }
  default:
    break;
  }
  return Max;
}

/// Records the text of a TypeRef value that a type at \p Depth names.
Error Lister::collectRef(uint32_t Token, unsigned Depth, RefTexts &Texts) {
  // A TypeSpec is checked at every depth that names it.
  if (!isTable(Token, TableId::TypeSpec) && Texts.count(Token))
    return Error::success();
  if (Error E = checkRow(Token, "signature type"))
    return E;
  Expected<std::string> Text = typeText(Token, Depth + 1);
  if (!Text)
    return Text.takeError();
  Texts[Token] = std::move(*Text);
  return Error::success();
}

Error Lister::collectRefs(const TypeSig &T, unsigned Depth, RefTexts &Texts) {
  if (Depth > MaxTypeDepth)
    return signatureError("nesting deeper than " + Twine(MaxTypeDepth) +
                          " levels");
  for (const CustomModifier &M : T.getModifiers())
    if (Error E = collectRef(M.Type, Depth, Texts))
      return E;
  switch (T.getElementType()) {
  case ELEMENT_TYPE_CLASS:
  case ELEMENT_TYPE_VALUETYPE:
    return collectRef(T.getTypeRef(), Depth, Texts);
  case ELEMENT_TYPE_GENERICINST:
    if (Error E = collectRef(T.getTypeRef(), Depth, Texts))
      return E;
    for (const TypeSig &A : T.getOperands())
      if (Error E = collectRefs(A, Depth + 1, Texts))
        return E;
    return Error::success();
  case ELEMENT_TYPE_PTR:
  case ELEMENT_TYPE_BYREF:
  case ELEMENT_TYPE_SZARRAY:
  case ELEMENT_TYPE_ARRAY:
  case ELEMENT_TYPE_PINNED:
    return collectRefs(T.getOperands()[0], Depth + 1, Texts);
  case ELEMENT_TYPE_FNPTR:
    return collectRefs(T.getFunctionSig(), Depth + 1, Texts);
  default:
    return Error::success();
  }
}

/// A method signature whose return and parameter types are at \p Depth.
Error Lister::collectRefs(const MethodSig &S, unsigned Depth, RefTexts &Texts) {
  if (Error E = collectRefs(S.Return, Depth, Texts))
    return E;
  for (const TypeSig &P : S.Params)
    if (Error E = collectRefs(P, Depth, Texts))
      return E;
  return Error::success();
}

Expected<std::string> Lister::typeSigText(const TypeSig &T) {
  RefTexts Texts;
  if (Error E = collectRefs(T, 0, Texts))
    return std::move(E);
  std::string S;
  raw_string_ostream OS(S);
  printTypeSig(OS, T,
               [&](raw_ostream &OS, uint32_t Ref) { OS << Texts.lookup(Ref); });
  return S;
}

Expected<std::string> Lister::methodSigText(const MethodSig &Sig) {
  RefTexts Texts;
  if (Error E = collectRefs(Sig, 0, Texts))
    return std::move(E);
  std::string S;
  raw_string_ostream OS(S);
  printMethodSig(
      OS, Sig, [&](raw_ostream &OS, uint32_t Ref) { OS << Texts.lookup(Ref); });
  return S;
}

Expected<std::string> Lister::methodSigText(uint32_t Blob) {
  Expected<ArrayRef<uint8_t>> Bytes = R.getBlob(Blob);
  if (!Bytes)
    return Bytes.takeError();
  Expected<MethodSig> Sig = decodeMethodSig(*Bytes);
  if (!Sig)
    return Sig.takeError();
  return methodSigText(*Sig);
}

Expected<std::string> Lister::fieldSigText(uint32_t Blob) {
  Expected<ArrayRef<uint8_t>> Bytes = R.getBlob(Blob);
  if (!Bytes)
    return Bytes.takeError();
  Expected<TypeSig> Sig = decodeFieldSig(*Bytes);
  if (!Sig)
    return Sig.takeError();
  return typeSigText(*Sig);
}

/// A MemberRef's signature: a field's type, or a method signature.
Expected<std::string> Lister::memberRefSigText(ArrayRef<uint8_t> Blob) {
  if (!Blob.empty() && Blob[0] == SIG_FIELD) {
    Expected<TypeSig> Field = decodeFieldSig(Blob);
    if (!Field)
      return Field.takeError();
    return typeSigText(*Field);
  }
  Expected<MethodSig> Method = decodeMethodSig(Blob);
  if (!Method)
    return Method.takeError();
  return methodSigText(*Method);
}

/// <propsig>: '[instance ]<type>(<p>, <p>)'.
Expected<std::string> Lister::propertySigText(uint32_t Blob) {
  Expected<ArrayRef<uint8_t>> Bytes = R.getBlob(Blob);
  if (!Bytes)
    return Bytes.takeError();
  Expected<PropertySig> Sig = decodePropertySig(*Bytes);
  if (!Sig)
    return Sig.takeError();
  Expected<std::string> Type = typeSigText(Sig->Type);
  if (!Type)
    return Type.takeError();
  std::string S = Sig->HasThis ? "instance " : "";
  S += *Type;
  S += '(';
  for (size_t I = 0, E = Sig->Params.size(); I != E; ++I) {
    Expected<std::string> P = typeSigText(Sig->Params[I]);
    if (!P)
      return P.takeError();
    if (I)
      S += ", ";
    S += *P;
  }
  S += ')';
  return S;
}

/// <method>: a MethodDef, a MemberRef (field MemberRefs included) or a
/// MethodSpec.
Expected<std::string> Lister::methodText(uint32_t Token) {
  auto It = EntityTexts.find(Token);
  if (It != EntityTexts.end())
    return It->second;
  if (Error E = checkRow(Token, "method"))
    return std::move(E);
  uint32_t Row = getTokenRow(Token);
  std::string S;
  switch (tableOf(Token)) {
  case TableId::MethodDef: {
    Expected<MethodDefRow> M = R.getMethodDef(Row);
    if (!M)
      return M.takeError();
    Expected<std::string> Sig = methodSigText(M->Signature);
    if (!Sig)
      return Sig.takeError();
    Expected<uint32_t> Owner = R.getOwnerTypeDef(Token);
    if (!Owner)
      return Owner.takeError();
    Expected<std::string> Full = fullName(getTokenRow(*Owner));
    if (!Full)
      return Full.takeError();
    S = *Sig + " " + quoted(*Full) + "::" + quoted(M->Name);
    break;
  }
  case TableId::MemberRef: {
    Expected<MemberRefRow> M = R.getMemberRef(Row);
    if (!M)
      return M.takeError();
    Expected<ArrayRef<uint8_t>> Blob = R.getBlob(M->Signature);
    if (!Blob)
      return Blob.takeError();
    Expected<std::string> Sig = memberRefSigText(*Blob);
    if (!Sig)
      return Sig.takeError();
    if (M->Parent == 0)
      return metadataError("MemberRef row " + Twine(Row) + " has no parent");
    std::string Parent;
    switch (tableOf(M->Parent)) {
    case TableId::TypeDef:
    case TableId::TypeRef:
    case TableId::TypeSpec: {
      Expected<std::string> T = typeText(M->Parent, 0);
      if (!T)
        return T.takeError();
      Parent = std::move(*T);
      break;
    }
    case TableId::ModuleRef: {
      Expected<StringRef> Name =
          stringColumn(TableId::ModuleRef, getTokenRow(M->Parent), 0);
      if (!Name)
        return Name.takeError();
      Parent = ("[.module " + *Name + "]").str();
      break;
    }
    case TableId::MethodDef: {
      Expected<std::string> T = methodText(M->Parent);
      if (!T)
        return T.takeError();
      Parent = std::move(*T);
      break;
    }
    default:
      return metadataError("MemberRef row " + Twine(Row) +
                           " has an invalid parent " + hex(M->Parent, 8));
    }
    S = *Sig + " " + Parent + "::" + quoted(M->Name);
    break;
  }
  case TableId::MethodSpec: {
    Expected<MethodSpecRow> M = R.getMethodSpec(Row);
    if (!M)
      return M.takeError();
    if (M->Method == 0 || (!isTable(M->Method, TableId::MethodDef) &&
                           !isTable(M->Method, TableId::MemberRef)))
      return metadataError("MethodSpec row " + Twine(Row) +
                           " has an invalid method " + hex(M->Method, 8));
    Expected<std::string> Method = methodText(M->Method);
    if (!Method)
      return Method.takeError();
    Expected<ArrayRef<uint8_t>> Blob = R.getBlob(M->Instantiation);
    if (!Blob)
      return Blob.takeError();
    Expected<std::vector<TypeSig>> Args = decodeMethodSpec(*Blob);
    if (!Args)
      return Args.takeError();
    S = *Method + "<";
    for (size_t I = 0, E = Args->size(); I != E; ++I) {
      Expected<std::string> A = typeSigText((*Args)[I]);
      if (!A)
        return A.takeError();
      if (I)
        S += ", ";
      S += *A;
    }
    S += '>';
    break;
  }
  default:
    return metadataError("a " + tableNameOf(Token) + " token (" +
                         hex(Token, 8) + ") is not a method");
  }
  EntityTexts[Token] = S;
  return S;
}

/// A FieldDef operand: '<type> '<owner full>'::'<n>''.
Expected<std::string> Lister::fieldText(uint32_t Token) {
  auto It = EntityTexts.find(Token);
  if (It != EntityTexts.end())
    return It->second;
  Expected<FieldRow> F = R.getField(getTokenRow(Token));
  if (!F)
    return F.takeError();
  Expected<std::string> Type = fieldSigText(F->Signature);
  if (!Type)
    return Type.takeError();
  Expected<uint32_t> Owner = R.getOwnerTypeDef(Token);
  if (!Owner)
    return Owner.takeError();
  Expected<std::string> Full = fullName(getTokenRow(*Owner));
  if (!Full)
    return Full.takeError();
  std::string S = *Type + " " + quoted(*Full) + "::" + quoted(F->Name);
  EntityTexts[Token] = S;
  return S;
}

/// A token operand that checkEntity accepted: a type, a method or a field.
Expected<std::string> Lister::entityText(uint32_t Token) {
  switch (tableOf(Token)) {
  case TableId::TypeDef:
  case TableId::TypeRef:
  case TableId::TypeSpec:
    return typeText(Token, 0);
  case TableId::Field:
    return fieldText(Token);
  case TableId::MethodDef:
  case TableId::MemberRef:
  case TableId::MethodSpec:
    return methodText(Token);
  default:
    return metadataError("a " + tableNameOf(Token) + " token (" +
                         hex(Token, 8) + ") is not a type, method or field");
  }
}

/// An ldstr operand: '"', the UTF-16 code units, with '\"', '\\', and
/// '\u<4X>' for units below 0x20 or above 0x7E, then '"'.
Expected<std::string> Lister::userStringText(uint32_t Token) {
  uint32_t Offset = getTokenRow(Token);
  if (getTokenType(Token) != UserStringTokenType || Offset == 0 ||
      Offset >= R.getHeapSize("#US"))
    return metadataError("invalid string token " + hex(Token, 8));
  Expected<ArrayRef<uint8_t>> Units = R.getUserStringUTF16(Offset);
  if (!Units)
    return Units.takeError();
  std::string S = "\"";
  for (size_t I = 0; I + 1 < Units->size(); I += 2) {
    uint16_t C = endian::read16le(Units->data() + I);
    if (C == '"' || C == '\\') {
      S += '\\';
      S += char(C);
    } else if (C < 0x20 || C > 0x7E) {
      S += "\\u";
      S += utohexstr(C, /*LowerCase=*/false, 4);
    } else {
      S += char(C);
    }
  }
  S += '"';
  return S;
}

//===----------------------------------------------------------------------===//
// Lookups
//===----------------------------------------------------------------------===//

/// The Offset of the first FieldLayout row of a Field row.
Expected<std::optional<uint32_t>> Lister::fieldOffset(uint32_t FieldRow) {
  if (!FieldLayoutRows) {
    std::vector<uint32_t> Rows(R.getRowCount(TableId::Field) + 1, 0);
    for (uint32_t Row = 1, N = R.getRowCount(TableId::FieldLayout); Row <= N;
         ++Row) {
      Expected<FieldLayoutRow> L = R.getFieldLayout(Row);
      if (!L)
        return L.takeError();
      uint32_t F = getTokenRow(L->Field);
      if (F != 0 && Rows[F] == 0)
        Rows[F] = Row;
    }
    FieldLayoutRows = std::move(Rows);
  }
  uint32_t Row = (*FieldLayoutRows)[FieldRow];
  if (Row == 0)
    return std::nullopt;
  Expected<FieldLayoutRow> L = R.getFieldLayout(Row);
  if (!L)
    return L.takeError();
  return std::optional<uint32_t>(L->Offset);
}

/// The Value blob of the first Constant row of a Field, Param or Property
/// token.
Expected<std::optional<uint32_t>> Lister::constantValue(uint32_t Token) {
  if (!Constants) {
    DenseMap<uint32_t, uint32_t> Map;
    for (uint32_t Row = 1, N = R.getRowCount(TableId::Constant); Row <= N;
         ++Row) {
      Expected<ConstantRow> C = R.getConstant(Row);
      if (!C)
        return C.takeError();
      if (C->Parent)
        Map.try_emplace(C->Parent, C->Value);
    }
    Constants = std::move(Map);
  }
  auto It = Constants->find(Token);
  if (It == Constants->end())
    return std::nullopt;
  return std::optional<uint32_t>(It->second);
}

/// The NativeType blob of the first FieldMarshal row of a Field or Param
/// token.
Expected<std::optional<uint32_t>> Lister::marshalBlob(uint32_t Token) {
  if (!Marshals) {
    DenseMap<uint32_t, uint32_t> Map;
    for (uint32_t Row = 1, N = R.getRowCount(TableId::FieldMarshal); Row <= N;
         ++Row) {
      // FieldMarshal: Parent, NativeType.
      Expected<uint32_t> Parent =
          R.getColumnValue(TableId::FieldMarshal, Row, 0);
      if (!Parent)
        return Parent.takeError();
      Expected<uint32_t> Blob = R.getColumnValue(TableId::FieldMarshal, Row, 1);
      if (!Blob)
        return Blob.takeError();
      if (*Parent)
        Map.try_emplace(*Parent, *Blob);
    }
    Marshals = std::move(Map);
  }
  auto It = Marshals->find(Token);
  if (It == Marshals->end())
    return std::nullopt;
  return std::optional<uint32_t>(It->second);
}

/// The interfaces of a TypeDef row, in InterfaceImpl table order.
Expected<SmallVector<uint32_t, 2>> Lister::interfaces(uint32_t TypeDefRow) {
  using Pair = std::pair<uint32_t, uint32_t>;
  if (!InterfaceImpls) {
    std::vector<Pair> Pairs;
    for (uint32_t Row = 1, N = R.getRowCount(TableId::InterfaceImpl); Row <= N;
         ++Row) {
      // InterfaceImpl: Class, Interface.
      Expected<uint32_t> Class =
          R.getColumnValue(TableId::InterfaceImpl, Row, 0);
      if (!Class)
        return Class.takeError();
      Expected<uint32_t> Interface =
          R.getColumnValue(TableId::InterfaceImpl, Row, 1);
      if (!Interface)
        return Interface.takeError();
      if (*Class == 0 || *Interface == 0)
        return metadataError("InterfaceImpl row " + Twine(Row) +
                             " names a nil class or interface");
      Pairs.push_back({getTokenRow(*Class), *Interface});
    }
    llvm::stable_sort(
        Pairs, [](const Pair &L, const Pair &R) { return L.first < R.first; });
    InterfaceImpls = std::move(Pairs);
  }
  auto Range = std::equal_range(
      InterfaceImpls->begin(), InterfaceImpls->end(), Pair(TypeDefRow, 0),
      [](const Pair &L, const Pair &R) { return L.first < R.first; });
  SmallVector<uint32_t, 2> Out;
  for (auto It = Range.first; It != Range.second; ++It)
    Out.push_back(It->second);
  return Out;
}

//===----------------------------------------------------------------------===//
// The listing
//===----------------------------------------------------------------------===//

void Lister::orderLines(std::vector<std::string> &Lines) const {
  if (!O.DeclarationOrder)
    llvm::sort(Lines, [](const std::string &L, const std::string &R) {
      return StringRef(L) < StringRef(R);
    });
}

void Lister::emitBlocks(raw_ostream &OS, std::vector<Block> &Blocks) const {
  if (!O.DeclarationOrder)
    llvm::stable_sort(Blocks, [](const Block &L, const Block &R) {
      if (int C = StringRef(L.first).compare(R.first))
        return C < 0;
      return StringRef(L.second) < StringRef(R.second);
    });
  for (const Block &B : Blocks)
    OS << B.second;
}

Error Lister::run(raw_ostream &OS) {
  if (O.StatsMode != CanonicalOptions::Stats::Only) {
    if (Error E = header(OS))
      return E;
    if (Error E = types(OS))
      return E;
    // The trailer: the Assembly row's and the Module row's attributes.
    if (R.isAssembly()) {
      if (Error E = customAttributes(OS, makeToken(TableId::Assembly, 1),
                                     "assembly "))
        return E;
    }
    if (Error E =
            customAttributes(OS, makeToken(TableId::Module, 1), "module "))
      return E;
  }
  if (O.StatsMode != CanonicalOptions::Stats::None)
    stats(OS);
  return Error::success();
}

Error Lister::header(raw_ostream &OS) {
  OS << "pe " << (Image.isPE32Plus() ? "PE32+" : "PE32")
     << " machine=" << hex(Image.getMachine(), 4)
     << (Image.isDll() ? " dll" : " exe")
     << " subsystem=" << Image.getSubsystem() << '\n';
  Expected<Cor20Header> Cor = Image.getCLIHeader();
  if (!Cor)
    return Cor.takeError();
  uint32_t Flags = Cor->Flags;
  OS << "cor flags=" << hex(Flags, 8)
     << " runtime=" << unsigned(Cor->MajorRuntimeVersion) << '.'
     << unsigned(Cor->MinorRuntimeVersion)
     << " metadata-version=" << R.getVersionString() << '\n';
  // With NATIVE_ENTRYPOINT the field is an RVA, which the listing leaves out.
  uint32_t EntryPoint = Cor->EntryPointToken;
  if (!(Flags & COMIMAGE_FLAGS_NATIVE_ENTRYPOINT) && EntryPoint != 0) {
    if (Error E = checkEntity(EntryPoint, "entry point"))
      return E;
    if (!isTable(EntryPoint, TableId::MethodDef))
      return metadataError("the entry point token " + hex(EntryPoint, 8) +
                           " is not a MethodDef");
    Expected<std::string> Method = methodText(EntryPoint);
    if (!Method)
      return Method.takeError();
    OS << "entrypoint " << *Method << '\n';
  }
  if (R.isAssembly()) {
    Expected<AssemblyRow> A = R.getAssembly();
    if (!A)
      return A.takeError();
    Expected<ArrayRef<uint8_t>> Key = R.getBlob(A->PublicKey);
    if (!Key)
      return Key.takeError();
    OS << "assembly " << quoted(A->Name) << " ver " << versionText(A->Version)
       << " flags=" << hex(static_cast<uint32_t>(A->Flags), 8) << " hash=";
    if (O.IgnoreAssemblyHash)
      OS << '*';
    else
      OS << hex(static_cast<uint32_t>(A->HashAlg), 8);
    OS << " culture=" << quoted(A->Culture) << " key=" << hexBytes(*Key)
       << '\n';
  }
  Expected<ModuleRow> Module = R.getModule();
  if (!Module)
    return Module.takeError();
  OS << "module " << quoted(Module->Name) << '\n';

  // The header lists, one kind after the other, each sorted by its text.
  std::vector<std::string> Lines;
  auto Flush = [&] {
    orderLines(Lines);
    for (const std::string &L : Lines)
      OS << L << '\n';
    Lines.clear();
  };
  if (!O.StripScopes) {
    for (uint32_t Row = 1, N = R.getRowCount(TableId::AssemblyRef); Row <= N;
         ++Row) {
      Expected<AssemblyRefRow> A = R.getAssemblyRef(Row);
      if (!A)
        return A.takeError();
      Expected<ArrayRef<uint8_t>> Key = R.getBlob(A->PublicKeyOrToken);
      if (!Key)
        return Key.takeError();
      Lines.push_back(
          "assemblyref " + quoted(A->Name) + " ver " + versionText(A->Version) +
          " flags=" + hex(static_cast<uint32_t>(A->Flags), 8) +
          " key=" + hexBytes(*Key) + " culture=" + quoted(A->Culture));
    }
    Flush();
  }
  if (!O.StripPInvokeModule) {
    for (uint32_t Row = 1, N = R.getRowCount(TableId::ModuleRef); Row <= N;
         ++Row) {
      Expected<ModuleRefRow> M = R.getModuleRef(Row);
      if (!M)
        return M.takeError();
      Lines.push_back("moduleref " + quoted(M->Name));
    }
    Flush();
  }
  for (uint32_t Row = 1, N = R.getRowCount(TableId::File); Row <= N; ++Row) {
    // File: Flags, Name, HashValue.
    Expected<StringRef> Name = stringColumn(TableId::File, Row, 1);
    if (!Name)
      return Name.takeError();
    Lines.push_back("file " + quoted(*Name));
  }
  Flush();
  for (uint32_t Row = 1, N = R.getRowCount(TableId::ExportedType); Row <= N;
       ++Row) {
    Expected<std::string> Name = exportedTypeName(Row, 0);
    if (!Name)
      return Name.takeError();
    Lines.push_back("exportedtype " + quoted(*Name));
  }
  Flush();
  for (uint32_t Row = 1, N = R.getRowCount(TableId::ManifestResource); Row <= N;
       ++Row) {
    Expected<ManifestResourceRow> M = R.getManifestResource(Row);
    if (!M)
      return M.takeError();
    Lines.push_back("resource " + quoted(M->Name));
  }
  Flush();
  return Error::success();
}

Error Lister::types(raw_ostream &OS) {
  std::vector<Block> Types;
  for (uint32_t Row = 1, N = R.getRowCount(TableId::TypeDef); Row <= N; ++Row) {
    Expected<std::string> Full = fullName(Row);
    if (!Full)
      return Full.takeError();
    Expected<std::string> Text = typeBlock(Row, *Full);
    if (!Text)
      return Text.takeError();
    Types.push_back({std::move(*Full), std::move(*Text)});
  }
  emitBlocks(OS, Types);
  return Error::success();
}

Expected<std::string> Lister::typeBlock(uint32_t Row, StringRef Full) {
  uint32_t Token = makeToken(TableId::TypeDef, Row);
  Expected<TypeDefRow> T = R.getTypeDef(Row);
  if (!T)
    return T.takeError();
  std::string Out;
  raw_string_ostream OS(Out);
  OS << "\n.class " << quoted(Full)
     << " flags=" << hex(static_cast<uint32_t>(T->Flags), 8);
  if (T->Extends) {
    Expected<std::string> Base = typeText(T->Extends, 0);
    if (!Base)
      return Base.takeError();
    OS << " extends " << *Base;
  }
  OS << '\n';
  Expected<std::optional<ClassLayoutRow>> Layout = R.findClassLayout(Token);
  if (!Layout)
    return Layout.takeError();
  if (*Layout)
    OS << "  .layout pack " << (*Layout)->PackingSize << " size "
       << (*Layout)->ClassSize << '\n';
  Expected<SmallVector<uint32_t, 2>> Interfaces = interfaces(Row);
  if (!Interfaces)
    return Interfaces.takeError();
  for (uint32_t I : *Interfaces) {
    Expected<std::string> Text = typeText(I, 0);
    if (!Text)
      return Text.takeError();
    OS << "  .implements " << *Text << '\n';
  }
  if (Error E = customAttributes(OS, Token, "  "))
    return std::move(E);

  std::vector<Block> Fields;
  for (uint32_t F = T->FieldBegin; F < T->FieldEnd; ++F) {
    Expected<FieldRow> FR = R.getField(F);
    if (!FR)
      return FR.takeError();
    Expected<std::string> Type = fieldSigText(FR->Signature);
    if (!Type)
      return Type.takeError();
    Expected<std::string> Text = fieldBlock(F, *FR, *Type);
    if (!Text)
      return Text.takeError();
    Fields.push_back({(FR->Name + " " + *Type).str(), std::move(*Text)});
  }
  emitBlocks(OS, Fields);

  Expected<std::pair<uint32_t, uint32_t>> Props = R.getPropertyRange(Token);
  if (!Props)
    return Props.takeError();
  std::vector<Block> Properties;
  for (uint32_t P = Props->first; P < Props->second; ++P) {
    Expected<PropertyRow> PR = R.getProperty(P);
    if (!PR)
      return PR.takeError();
    Expected<std::string> Sig = propertySigText(PR->Signature);
    if (!Sig)
      return Sig.takeError();
    Expected<std::string> Text = propertyBlock(P, *PR, *Sig);
    if (!Text)
      return Text.takeError();
    Properties.push_back({(PR->Name + " " + *Sig).str(), std::move(*Text)});
  }
  emitBlocks(OS, Properties);

  std::vector<Block> Methods;
  for (uint32_t M = T->MethodBegin; M < T->MethodEnd; ++M) {
    Expected<MethodDefRow> MR = R.getMethodDef(M);
    if (!MR)
      return MR.takeError();
    Expected<std::string> Sig = methodSigText(MR->Signature);
    if (!Sig)
      return Sig.takeError();
    Expected<std::string> Text = methodBlock(M, *MR, *Sig);
    if (!Text)
      return Text.takeError();
    Methods.push_back({(MR->Name + " " + *Sig).str(), std::move(*Text)});
  }
  emitBlocks(OS, Methods);
  return Out;
}

Error Lister::customAttributes(raw_ostream &OS, uint32_t Parent,
                               StringRef Prefix) {
  Expected<SmallVector<uint32_t, 4>> Rows = R.getCustomAttributes(Parent);
  if (!Rows)
    return Rows.takeError();
  std::vector<std::string> Lines;
  for (uint32_t Row : *Rows) {
    Expected<CustomAttributeRow> CA = R.getCustomAttribute(Row);
    if (!CA)
      return CA.takeError();
    if (CA->Constructor == 0 ||
        (!isTable(CA->Constructor, TableId::MethodDef) &&
         !isTable(CA->Constructor, TableId::MemberRef)))
      return metadataError("CustomAttribute row " + Twine(Row) +
                           " has an invalid constructor " +
                           hex(CA->Constructor, 8));
    Expected<std::string> Ctor = methodText(CA->Constructor);
    if (!Ctor)
      return Ctor.takeError();
    Expected<ArrayRef<uint8_t>> Value = R.getBlob(CA->Value);
    if (!Value)
      return Value.takeError();
    Lines.push_back(".custom " + *Ctor + " = " + hexBytes(*Value));
  }
  orderLines(Lines);
  for (const std::string &L : Lines)
    OS << Prefix << L << '\n';
  return Error::success();
}

/// The size of a primitive field type, or the ClassLayout size of a TypeDef
/// valuetype; -1 otherwise. The signature has been decoded already.
Expected<int64_t> Lister::fieldDataSize(uint32_t SigBlob) {
  Expected<ArrayRef<uint8_t>> Sig = R.getBlob(SigBlob);
  if (!Sig)
    return Sig.takeError();
  if (Sig->size() < 2 || (*Sig)[0] != SIG_FIELD)
    return -1;
  switch ((*Sig)[1]) {
  case ELEMENT_TYPE_BOOLEAN:
  case ELEMENT_TYPE_I1:
  case ELEMENT_TYPE_U1:
    return 1;
  case ELEMENT_TYPE_CHAR:
  case ELEMENT_TYPE_I2:
  case ELEMENT_TYPE_U2:
    return 2;
  case ELEMENT_TYPE_I4:
  case ELEMENT_TYPE_U4:
  case ELEMENT_TYPE_R4:
    return 4;
  case ELEMENT_TYPE_I8:
  case ELEMENT_TYPE_U8:
  case ELEMENT_TYPE_R8:
    return 8;
  case ELEMENT_TYPE_VALUETYPE: {
    ArrayRef<uint8_t> Rest = Sig->drop_front(2);
    std::optional<uint32_t> Coded = decodeCompressedUInt(Rest);
    std::optional<uint32_t> Token =
        Coded ? decodeTypeDefOrRefOrSpec(*Coded) : std::nullopt;
    if (!Token)
      return signatureError("bad valuetype in field signature " +
                            hexBytes(*Sig));
    if (Error E = checkRow(*Token, "signature type"))
      return std::move(E);
    if (!isTable(*Token, TableId::TypeDef))
      return -1;
    Expected<std::optional<ClassLayoutRow>> Layout = R.findClassLayout(*Token);
    if (!Layout)
      return Layout.takeError();
    if (!*Layout)
      return -1;
    if ((*Layout)->ClassSize > uint32_t(INT32_MAX))
      return metadataError("ClassLayout size " + Twine((*Layout)->ClassSize) +
                           " is too large");
    return int64_t((*Layout)->ClassSize);
  }
  default:
    return -1;
  }
}

Expected<std::string> Lister::fieldBlock(uint32_t Row, const FieldRow &F,
                                         StringRef Type) {
  uint32_t Token = makeToken(TableId::Field, Row);
  std::string Out;
  raw_string_ostream OS(Out);
  OS << "  .field " << quoted(F.Name) << ' ' << Type
     << " flags=" << hex(static_cast<uint16_t>(F.Flags), 4);
  Expected<std::optional<uint32_t>> Offset = fieldOffset(Row);
  if (!Offset)
    return Offset.takeError();
  if (*Offset)
    OS << " offset " << **Offset;
  Expected<std::optional<uint32_t>> Constant = constantValue(Token);
  if (!Constant)
    return Constant.takeError();
  if (*Constant) {
    Expected<ArrayRef<uint8_t>> Value = R.getBlob(**Constant);
    if (!Value)
      return Value.takeError();
    OS << " const " << hexBytes(*Value);
  }
  Expected<std::optional<uint32_t>> Marshal = marshalBlob(Token);
  if (!Marshal)
    return Marshal.takeError();
  if (*Marshal && **Marshal != 0) {
    Expected<ArrayRef<uint8_t>> Value = R.getBlob(**Marshal);
    if (!Value)
      return Value.takeError();
    OS << " marshal " << hexBytes(*Value);
  }
  OS << '\n';
  if (Error E = customAttributes(OS, Token, "    "))
    return std::move(E);
  Expected<std::optional<FieldRVARow>> RVA = R.findFieldRVA(Token);
  if (!RVA)
    return RVA.takeError();
  if (*RVA) {
    Expected<int64_t> Size = fieldDataSize(F.Signature);
    if (!Size)
      return Size.takeError();
    if (*Size < 0) {
      OS << "    .data (unknown size)\n";
    } else {
      Expected<std::vector<uint8_t>> Data =
          readMapped(Image, (*RVA)->RVA, uint64_t(*Size));
      if (!Data)
        return Data.takeError();
      OS << "    .data " << *Size << " bytes\n";
      ArrayRef<uint8_t> Bytes(*Data);
      for (size_t I = 0; I < Bytes.size(); I += 32)
        OS << "      "
           << hexBytes(Bytes.slice(I, std::min<size_t>(32, Bytes.size() - I)))
           << '\n';
    }
  }
  return Out;
}

Expected<std::string> Lister::propertyBlock(uint32_t Row, const PropertyRow &P,
                                            StringRef Sig) {
  uint32_t Token = makeToken(TableId::Property, Row);
  std::string Out;
  raw_string_ostream OS(Out);
  OS << "  .property " << quoted(P.Name) << ' ' << Sig
     << " flags=" << hex(static_cast<uint16_t>(P.Flags), 4) << '\n';
  Expected<SmallVector<MethodSemanticsRow, 2>> Semantics =
      R.getMethodSemantics(Token);
  if (!Semantics)
    return Semantics.takeError();
  std::vector<std::string> Lines;
  for (const MethodSemanticsRow &S : *Semantics) {
    if (S.Method == 0)
      return metadataError("a MethodSemantics row of property " +
                           hex(Token, 8) + " names no method");
    Expected<std::string> Method = methodText(S.Method);
    if (!Method)
      return Method.takeError();
    // Exactly Getter or Setter; any other value (combinations included) is
    // .other.
    StringRef Kind = ".other ";
    if (S.Semantics == MethodSemanticsAttributes::Getter)
      Kind = ".get ";
    else if (S.Semantics == MethodSemanticsAttributes::Setter)
      Kind = ".set ";
    Lines.push_back(Kind.str() + *Method);
  }
  orderLines(Lines);
  for (const std::string &L : Lines)
    OS << "    " << L << '\n';
  if (Error E = customAttributes(OS, Token, "    "))
    return std::move(E);
  return Out;
}

Expected<std::string> Lister::methodBlock(uint32_t Row, const MethodDefRow &M,
                                          StringRef Sig) {
  uint32_t Token = makeToken(TableId::MethodDef, Row);
  std::string Out;
  raw_string_ostream OS(Out);
  uint16_t Flags = static_cast<uint16_t>(M.Flags);
  // --pinvoke-access=ignore: MemberAccess cleared on P/Invoke methods.
  if (O.IgnorePInvokeAccess &&
      (Flags & static_cast<uint16_t>(MethodAttributes::PinvokeImpl)))
    Flags &= ~static_cast<uint16_t>(MethodAttributes::MemberAccessMask);
  OS << "  .method " << quoted(M.Name) << ' ' << Sig
     << " flags=" << hex(Flags, 4)
     << " impl=" << hex(static_cast<uint16_t>(M.ImplFlags), 4) << '\n';
  Expected<std::optional<ImplMapRow>> Import = R.findImplMap(Token);
  if (!Import)
    return Import.takeError();
  if (*Import && (*Import)->ImportScope != 0) {
    OS << "    .pinvokeimpl '";
    if (O.StripPInvokeModule) {
      OS << '*';
    } else {
      Expected<StringRef> Module = stringColumn(
          TableId::ModuleRef, getTokenRow((*Import)->ImportScope), 0);
      if (!Module)
        return Module.takeError();
      OS << escaped(*Module);
    }
    OS << "' entry " << quoted((*Import)->ImportName)
       << " flags=" << hex(static_cast<uint16_t>((*Import)->Flags), 4) << '\n';
  }

  // .param lines in Sequence order, ties in table order.
  std::vector<std::pair<uint32_t, ParamRow>> Params;
  for (uint32_t P = M.ParamBegin; P < M.ParamEnd; ++P) {
    Expected<ParamRow> PR = R.getParam(P);
    if (!PR)
      return PR.takeError();
    Params.push_back({P, *PR});
  }
  llvm::stable_sort(Params, [](const std::pair<uint32_t, ParamRow> &L,
                               const std::pair<uint32_t, ParamRow> &R) {
    return L.second.Sequence < R.second.Sequence;
  });
  for (const auto &[P, PR] : Params) {
    uint32_t ParamToken = makeToken(TableId::Param, P);
    Expected<std::optional<uint32_t>> Marshal = marshalBlob(ParamToken);
    if (!Marshal)
      return Marshal.takeError();
    Expected<std::optional<uint32_t>> Constant = constantValue(ParamToken);
    if (!Constant)
      return Constant.takeError();
    bool HasMarshal = *Marshal && **Marshal != 0;
    uint16_t ParamFlags = static_cast<uint16_t>(PR.Flags);
    // --params=drop: the Param rows that ilasm adds.
    if (O.DropParams && ParamFlags == 0 && !HasMarshal && !*Constant)
      continue;
    OS << "    .param " << PR.Sequence << ' ' << quoted(PR.Name)
       << " flags=" << hex(ParamFlags, 4);
    if (HasMarshal) {
      Expected<ArrayRef<uint8_t>> Value = R.getBlob(**Marshal);
      if (!Value)
        return Value.takeError();
      OS << " marshal " << hexBytes(*Value);
    }
    if (*Constant) {
      Expected<ArrayRef<uint8_t>> Value = R.getBlob(**Constant);
      if (!Value)
        return Value.takeError();
      OS << " const " << hexBytes(*Value);
    }
    OS << '\n';
  }
  if (Error E = customAttributes(OS, Token, "    "))
    return std::move(E);
  if (M.RVA != 0)
    if (Error E = body(OS, Token))
      return std::move(E);
  return Out;
}

Error Lister::body(raw_ostream &OS, uint32_t MethodToken) {
  Expected<MethodBodyView> B = R.getMethodBody(MethodToken);
  if (!B)
    return B.takeError();
  OS << "    .maxstack " << B->MaxStack;
  // --initlocals=ignore-without-locals: ilasm's InitLocals on bodies without
  // a local signature.
  if (B->InitLocals &&
      !(O.IgnoreInitLocalsWithoutLocals && B->LocalVarSigToken == 0))
    OS << " initlocals";
  OS << '\n';
  if (uint32_t Locals = B->LocalVarSigToken) {
    if (Error E = checkEntity(Locals, "local signature"))
      return E;
    if (!isTable(Locals, TableId::StandAloneSig))
      return metadataError("local signature token " + hex(Locals, 8) +
                           " is not a StandAloneSig");
    Expected<StandAloneSigRow> S = R.getStandAloneSig(getTokenRow(Locals));
    if (!S)
      return S.takeError();
    Expected<ArrayRef<uint8_t>> Blob = R.getBlob(S->Signature);
    if (!Blob)
      return Blob.takeError();
    Expected<std::vector<TypeSig>> Types = decodeLocalSig(*Blob);
    if (!Types)
      return Types.takeError();
    std::string Text;
    for (size_t I = 0, E = Types->size(); I != E; ++I) {
      Expected<std::string> T = typeSigText((*Types)[I]);
      if (!T)
        return T.takeError();
      if (I)
        Text += ", ";
      Text += *T;
    }
    OS << "    .locals (" << Text << ")\n";
  }
  return instructions(OS, B->Code, B->Clauses);
}

/// --forms=canon: short branches as their long forms; the short variable
/// and ldc.i4.s forms without '.s'.
StringRef Lister::opName(const OpcodeInfo &Info) const {
  StringRef Name = Info.Name;
  if (!O.CanonicalForms || !Name.ends_with(".s"))
    return Name;
  if (Info.Operand == OperandKind::ShortInlineBrTarget ||
      is_contained({"ldarg.s", "ldarga.s", "starg.s", "ldloc.s", "ldloca.s",
                    "stloc.s", "ldc.i4.s"},
                   Name))
    return Name.drop_back(2);
  return Name;
}

Error Lister::instructions(raw_ostream &OS, ArrayRef<uint8_t> Code,
                           ArrayRef<ExceptionClause> Clauses) {
  std::vector<ILInstruction> Insts;
  if (Error E = decodeIL(Code, [&](const ILInstruction &I) {
        Insts.push_back(I);
        return Error::success();
      }))
    return E;
  // Index[offset] = the instruction that starts there; the code size gives
  // L<count>.
  std::vector<int64_t> Index(Code.size() + 1, -1);
  for (size_t I = 0, E = Insts.size(); I != E; ++I)
    Index[Insts[I].Offset] = I;
  Index[Code.size()] = Insts.size();
  auto Label = [&](int64_t Offset) -> std::string {
    if (Offset >= 0 && uint64_t(Offset) <= Code.size() && Index[Offset] >= 0)
      return "L" + utostr(Index[Offset]);
    if (Offset < 0)
      return "<offset -0x" + utohexstr(uint64_t(0) - uint64_t(Offset)) + ">";
    return "<offset 0x" + utohexstr(Offset) + ">";
  };

  for (size_t I = 0, E = Insts.size(); I != E; ++I) {
    const ILInstruction &X = Insts[I];
    const OpcodeInfo &Info = getOpcodeInfo(X.Op);
    OS << "    L" << I << ": " << opName(Info);
    switch (Info.Operand) {
    case OperandKind::InlineNone:
      break;
    case OperandKind::ShortInlineVar:
    case OperandKind::InlineVar:
    case OperandKind::ShortInlineI:
    case OperandKind::InlineI:
    case OperandKind::InlineI8:
      OS << ' ' << X.Imm;
      break;
    case OperandKind::ShortInlineR: {
      uint32_t Bits = static_cast<uint32_t>(X.FPBits);
      // --snan=quiet: a float32 signaling NaN prints with bit 22 set.
      if (O.QuietSignalingNaN && (Bits & 0x7F800000) == 0x7F800000 &&
          (Bits & 0x007FFFFF) != 0 && (Bits & 0x00400000) == 0)
        Bits |= 0x00400000;
      OS << " float32(" << hex(Bits, 8) << ')';
      break;
    }
    case OperandKind::InlineR:
      OS << " float64(" << hex(X.FPBits, 16) << ')';
      break;
    case OperandKind::ShortInlineBrTarget:
    case OperandKind::InlineBrTarget:
      OS << ' ' << Label(X.Target);
      break;
    case OperandKind::InlineSwitch: {
      ListSeparator LS;
      if (!X.SwitchTargets.empty())
        OS << ' ';
      for (int64_t T : X.SwitchTargets)
        OS << LS << Label(T);
      break;
    }
    case OperandKind::InlineString: {
      Expected<std::string> S = userStringText(X.Token);
      if (!S)
        return S.takeError();
      OS << ' ' << *S;
      break;
    }
    case OperandKind::InlineSig: {
      if (Error E = checkEntity(X.Token, "signature"))
        return E;
      if (!isTable(X.Token, TableId::StandAloneSig))
        return metadataError("calli token " + hex(X.Token, 8) +
                             " is not a StandAloneSig");
      Expected<StandAloneSigRow> S = R.getStandAloneSig(getTokenRow(X.Token));
      if (!S)
        return S.takeError();
      Expected<std::string> Sig = methodSigText(S->Signature);
      if (!Sig)
        return Sig.takeError();
      OS << ' ' << *Sig;
      break;
    }
    case OperandKind::InlineMethod:
    case OperandKind::InlineField:
    case OperandKind::InlineType:
    case OperandKind::InlineTok: {
      if (Error E = checkEntity(X.Token, "operand"))
        return E;
      Expected<std::string> Text = entityText(X.Token);
      if (!Text)
        return Text.takeError();
      OS << ' ' << *Text;
      break;
    }
    }
    OS << '\n';
  }

  for (const ExceptionClause &C : Clauses) {
    OS << "    .try " << Label(C.TryOffset) << " to "
       << Label(int64_t(C.TryOffset) + C.TryLength) << ' ';
    switch (C.Kind) {
    case ExceptionClauseKind::Catch: {
      if (Error E = checkEntity(C.ClassTokenOrFilterOffset, "catch type"))
        return E;
      Expected<std::string> Type = typeText(C.ClassTokenOrFilterOffset, 0);
      if (!Type)
        return Type.takeError();
      OS << "catch " << *Type;
      break;
    }
    case ExceptionClauseKind::Filter:
      OS << "filter " << Label(C.ClassTokenOrFilterOffset);
      break;
    case ExceptionClauseKind::Finally:
      OS << "finally";
      break;
    case ExceptionClauseKind::Fault:
      OS << "fault";
      break;
    }
    OS << " handler " << Label(C.HandlerOffset) << " to "
       << Label(int64_t(C.HandlerOffset) + C.HandlerLength) << '\n';
  }
  return Error::success();
}

void Lister::stats(raw_ostream &OS) const {
  for (unsigned Id = 0;
       Id <= static_cast<unsigned>(TableId::GenericParamConstraint); ++Id) {
    TableId T = static_cast<TableId>(Id);
    if (uint32_t N = R.getRowCount(T))
      OS << "# table " << getTableName(T) << " rows=" << N
         << " rowsize=" << R.getSizes().getRowSize(T) << '\n';
  }
  for (StringRef Heap : {"#Strings", "#US", "#GUID", "#Blob"})
    OS << "# heap " << Heap << " size=" << R.getHeapSize(Heap) << '\n';
  OS << "# image size=" << Image.getSizeOfImage() << " sections=";
  ListSeparator LS(",");
  for (const PESectionInfo &S : Image.sections())
    OS << LS << S.Name;
  OS << '\n';
}

Error cli::dumpCanonical(const MetadataReader &R, raw_ostream &OS,
                         const CanonicalOptions &Opts) {
  if (!R.getImage())
    return noImageError();
  std::string Text;
  raw_string_ostream TOS(Text);
  Lister L(R, Opts);
  if (Error E = L.run(TOS))
    return E;
  OS << Text;
  return Error::success();
}

//===----------------------------------------------------------------------===//
// --headers
//===----------------------------------------------------------------------===//

Error cli::dumpHeaders(const MetadataReader &R, raw_ostream &OS) {
  const PEImage *Image = R.getImage();
  if (!Image)
    return noImageError();
  ArrayRef<uint8_t> File = Image->getFile();
  // PEImage::create checked the headers; reads past the file give 0.
  auto U32 = [&](uint64_t Offset) -> uint32_t {
    return Offset + 4 <= File.size() ? endian::read32le(File.data() + Offset)
                                     : 0;
  };
  auto U64 = [&](uint64_t Offset) -> uint64_t {
    return Offset + 8 <= File.size() ? endian::read64le(File.data() + Offset)
                                     : 0;
  };
  // e_lfanew, then "PE\0\0", the 20-byte COFF header and the optional header.
  uint64_t COFFHeader = uint64_t(U32(0x3C)) + 4;
  uint64_t Optional = COFFHeader + 20;
  bool Plus = Image->isPE32Plus();
  uint64_t StackReserve, StackCommit, HeapReserve, HeapCommit;
  if (Plus) {
    StackReserve = U64(Optional + 72);
    StackCommit = U64(Optional + 80);
    HeapReserve = U64(Optional + 88);
    HeapCommit = U64(Optional + 96);
  } else {
    StackReserve = U32(Optional + 72);
    StackCommit = U32(Optional + 76);
    HeapReserve = U32(Optional + 80);
    HeapCommit = U32(Optional + 84);
  }

  std::string Text;
  raw_string_ostream TOS(Text);
  TOS << "Format: " << (Plus ? "PE32+" : "PE32")
      << " machine=" << hex(Image->getMachine(), 4)
      << (Image->isDll() ? " dll" : " exe") << '\n';
  TOS << "COFF: sections=" << Image->sections().size()
      << " timestamp=" << hex(Image->getTimeDateStamp(), 8)
      << " characteristics=" << hex(Image->getCharacteristics(), 4)
      << " symbols=" << U32(COFFHeader + 12) << '\n';
  TOS << "Optional: imagebase=" << hex(Image->getImageBase(), 16)
      << " sectionalign=" << hex(U32(Optional + 32))
      << " filealign=" << hex(U32(Optional + 36))
      << " subsystem=" << Image->getSubsystem()
      << " dllchars=" << hex(Image->getDllCharacteristics(), 4)
      << " stack=" << hex(StackReserve) << ',' << hex(StackCommit)
      << " heap=" << hex(HeapReserve) << ',' << hex(HeapCommit) << '\n';
  for (const PESectionInfo &S : Image->sections())
    TOS << "Section: " << S.Name << " rva=" << hex(S.VirtualAddress, 8)
        << " vsize=" << hex(S.VirtualSize) << " raw=" << hex(S.PointerToRawData)
        << " rawsize=" << hex(S.SizeOfRawData)
        << " flags=" << hex(S.Characteristics, 8) << '\n';

  Expected<Cor20Header> Cor = Image->getCLIHeader();
  if (!Cor)
    return Cor.takeError();
  uint32_t MetadataRVA = Cor->MetaData.RVA, MetadataSize = Cor->MetaData.Size;
  TOS << "CLI: flags=" << hex(Cor->Flags, 8)
      << " entrypoint=" << hex(Cor->EntryPointToken, 8)
      << " metadata=" << hex(MetadataRVA, 8) << ',' << hex(MetadataSize)
      << " runtime=" << unsigned(Cor->MajorRuntimeVersion) << '.'
      << unsigned(Cor->MinorRuntimeVersion) << '\n';

  // The stream headers of the metadata root, which MetadataReader::create
  // has checked.
  Expected<ArrayRef<uint8_t>> Root =
      Image->getRVAData(MetadataRVA, MetadataSize);
  if (!Root)
    return Root.takeError();
  struct Stream {
    StringRef Name;
    uint32_t Offset, Size;
  };
  SmallVector<Stream, 5> Streams;
  uint64_t Valid = 0, Sorted = 0;
  const uint8_t *B = Root->data();
  uint64_t RootSize = Root->size();
  if (RootSize >= 16) {
    uint64_t P = 16 + uint64_t(endian::read32le(B + 12));
    if (P + 4 <= RootSize) {
      unsigned Count = endian::read16le(B + P + 2);
      P += 4;
      for (unsigned I = 0; I != Count && P + 8 <= RootSize; ++I) {
        uint32_t Offset = endian::read32le(B + P);
        uint32_t Size = endian::read32le(B + P + 4);
        P += 8;
        uint64_t End = std::min<uint64_t>(RootSize, P + 32);
        const uint8_t *Nul = std::find(B + P, B + End, 0);
        StringRef Name(reinterpret_cast<const char *>(B + P), Nul - (B + P));
        Streams.push_back({Name, Offset, Size});
        P = alignTo(P + Name.size() + 1, 4);
        if (Name == "#~" && Size >= 24 && uint64_t(Offset) + 24 <= RootSize) {
          Valid = endian::read64le(B + Offset + 8);
          Sorted = endian::read64le(B + Offset + 16);
        }
      }
    }
  }
  TOS << "Metadata: version=" << quoted(R.getVersionString())
      << " heapsizes=" << hex(R.getHeapSizeFlags(), 2)
      << " valid=" << hex(Valid, 16) << " sorted=" << hex(Sorted, 16) << '\n';
  for (const Stream &S : Streams)
    TOS << "Stream: " << S.Name << " offset=" << hex(S.Offset)
        << " size=" << hex(S.Size) << '\n';
  Expected<ModuleRow> Module = R.getModule();
  if (!Module)
    return Module.takeError();
  TOS << "Mvid: ";
  if (Module->Mvid == 0) {
    TOS << "0";
  } else {
    Expected<std::array<uint8_t, 16>> Mvid = R.getGuid(Module->Mvid);
    if (!Mvid)
      return Mvid.takeError();
    TOS << formatGuid(*Mvid);
  }
  TOS << '\n';
  OS << Text;
  return Error::success();
}

//===----------------------------------------------------------------------===//
// --tables
//===----------------------------------------------------------------------===//

Error cli::dumpTables(const MetadataReader &R, raw_ostream &OS) {
  std::string Text;
  raw_string_ostream TOS(Text);
  for (unsigned Id = 0;
       Id <= static_cast<unsigned>(TableId::GenericParamConstraint); ++Id) {
    TableId T = static_cast<TableId>(Id);
    uint32_t Rows = R.getRowCount(T);
    if (Rows == 0)
      continue;
    StringRef Name = getTableName(T);
    ArrayRef<ColumnInfo> Columns = getTableColumns(T);
    TOS << "Table: " << Name << " rows=" << Rows
        << " rowsize=" << R.getSizes().getRowSize(T) << '\n';
    for (uint32_t Row = 1; Row <= Rows; ++Row) {
      TOS << "  " << Name << '[' << Row << "]:";
      for (unsigned C = 0, E = Columns.size(); C != E; ++C) {
        Expected<uint32_t> V = R.getColumnValue(T, Row, C);
        if (!V)
          return V.takeError();
        TOS << ' ' << Columns[C].Name << '=';
        switch (Columns[C].Kind) {
        case ColumnKind::U16:
          TOS << hex(*V, 4);
          break;
        case ColumnKind::U32:
          TOS << hex(*V, 8);
          break;
        case ColumnKind::StringIdx: {
          Expected<StringRef> S = R.getString(*V);
          if (!S)
            return S.takeError();
          TOS << quoted(*S);
          break;
        }
        case ColumnKind::BlobIdx: {
          Expected<ArrayRef<uint8_t>> Blob = R.getBlob(*V);
          if (!Blob)
            return Blob.takeError();
          TOS << hexBytes(*Blob);
          break;
        }
        case ColumnKind::GuidIdx: {
          if (*V == 0) {
            TOS << '0';
            break;
          }
          Expected<std::array<uint8_t, 16>> G = R.getGuid(*V);
          if (!G)
            return G.takeError();
          TOS << '{' << formatGuid(*G) << '}';
          break;
        }
        case ColumnKind::TableIdx:
        case ColumnKind::CodedIdx:
          if (*V == 0)
            TOS << '0';
          else
            TOS << hex(*V, 8);
          break;
        }
      }
      TOS << '\n';
    }
  }
  OS << Text;
  return Error::success();
}

//===----------------------------------------------------------------------===//
// --link-info
//===----------------------------------------------------------------------===//

Error cli::dumpLinkInfo(const MetadataReader &R, raw_ostream &OS) {
  std::string Text;
  raw_string_ostream TOS(Text);
  bool Found = false;
  Expected<ArrayRef<uint8_t>> Record = findLinkInfo(R);
  if (Record) {
    Expected<MSILLink::Record> Decoded = decodeLinkInfo(*Record);
    if (!Decoded)
      return Decoded.takeError();
    printLinkInfo(*Decoded, &R, TOS);
    Found = true;
  } else {
    // findLinkInfo reports a missing record with errc::invalid_argument and
    // a malformed one with another code.
    if (Error E = handleErrors(
            Record.takeError(), [](std::unique_ptr<StringError> SE) -> Error {
              if (SE->convertToErrorCode() ==
                  std::make_error_code(std::errc::invalid_argument))
                return Error::success();
              return Error(std::move(SE));
            }))
      return E;
  }
  if (R.isAssembly()) {
    Expected<SmallVector<ArrayRef<uint8_t>, 1>> Payloads =
        R.getByteArrayAttributes(makeToken(TableId::Assembly, 1), "",
                                 SymbolTableType);
    if (!Payloads)
      return Payloads.takeError();
    for (ArrayRef<uint8_t> P : *Payloads) {
      Expected<LibrarySymbolTable> Table = decodeSymbolTable(P);
      if (!Table)
        return Table.takeError();
      printSymbolTable(*Table, TOS);
      Found = true;
    }
  }
  if (!Found)
    return createStringError(errc::invalid_argument,
                             "no link-info record or symbol table");
  OS << Text;
  return Error::success();
}
