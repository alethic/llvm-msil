//===- llvm-cil-copy.cpp - Copy an ECMA-335 module ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm-cil-copy <input> -o <output>
//
// Reads an IL-only PE32+ image (an assembly or a netmodule, ilasm's envelope
// included) with MetadataReader and writes it again with MetadataBuilder,
// MethodBodyStream and writePEImage: the end-to-end oracle of the ECMA-335
// library before msil-ld, and a working model of msil-ld's row copy and
// token remapping. The copy is canonically equal to its input (canonical
// listing v1) and depends only on the input's bytes.
//
// Every row is rebuilt in table order with an old-to-new token map, where
// the rows that MetadataBuilder merges map to one row:
// - signatures are decoded, their type references remapped (mapTypeRefs) and
//   re-encoded; custom-attribute blobs are copied verbatim;
// - method bodies are copied, their tokens patched with rewriteILTokens (the
//   ldstr strings re-added to #US), and re-headed with the same maxstack,
//   local signature, InitLocals and (remapped) clauses; methods that share a
//   body share its copy;
// - FieldRVA data is copied, 8-aligned, with the size rule of the listing's
//   '.data' line: a primitive field type, or a TypeDef valuetype with a
//   ClassLayout row (any other size is an error); empty data still takes one
//   zero byte, so that its RVA lies inside .text;
// - the Assembly row, the references and the entry point are carried over;
//   an entry point makes an Executable, an Assembly row a Library, anything
//   else a Module, for the input's machine, subsystem and CorFlags (without
//   STRONGNAMESIGNED: the copy has no signature).
// TypeRefs whose scope is a later TypeRef and TypeSpecs that name later
// TypeSpecs are added after what they name. A type that extends or is nested
// in a later type is an error (MetadataBuilder adds TypeDefs in order). A row
// of a table outside the writer set is the error "unsupported table <Name>
// (<n> rows)", and an ldstr string whose #US entry holds an unpaired UTF-16
// surrogate the error "unsupported user string at #US offset 0x<8X>: it holds
// a lone UTF-16 surrogate" (the image is valid, but addUserString takes
// UTF-8).
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/CLI/Handles.h"
#include "llvm/CLI/ILDecoder.h"
#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/CLI/PEImage.h"
#include "llvm/CLI/PEWriter.h"
#include "llvm/CLI/Signature.h"
#include "llvm/CLI/TableSchema.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

static constexpr StringLiteral ToolName = "llvm-cil-copy";

static cl::OptionCategory CopyCategory("llvm-cil-copy Options");

static cl::opt<std::string> InputFilename(cl::Positional, cl::Required,
                                          cl::desc("<input>"),
                                          cl::cat(CopyCategory));
static cl::opt<std::string> OutputFilename("o", cl::Required,
                                           cl::desc("Output file"),
                                           cl::value_desc("output"),
                                           cl::cat(CopyCategory));

/// The deepest chain of TypeRef scopes or TypeSpecs that the copy follows
/// (the listing's limit).
static constexpr unsigned MaxNesting = 64;

static Error unsupported(const Twine &Msg) {
  return createStringError(errc::not_supported, Msg);
}

static Error malformed(const Twine &Msg) {
  return createStringError(errc::illegal_byte_sequence, Msg);
}

static std::string hex8(uint32_t V) {
  return "0x" + utohexstr(V, /*LowerCase=*/false, 8);
}

static bool isTable(uint32_t Token, TableId T) {
  return getTokenType(Token) == static_cast<uint8_t>(T);
}

/// The tables that MetadataBuilder writes (PropertyMap and NestedClass rows
/// are implicit).
static bool isWriterTable(TableId T) {
  switch (T) {
  case TableId::Module:
  case TableId::TypeRef:
  case TableId::TypeDef:
  case TableId::Field:
  case TableId::MethodDef:
  case TableId::Param:
  case TableId::MemberRef:
  case TableId::CustomAttribute:
  case TableId::ClassLayout:
  case TableId::FieldLayout:
  case TableId::StandAloneSig:
  case TableId::PropertyMap:
  case TableId::Property:
  case TableId::MethodSemantics:
  case TableId::ModuleRef:
  case TableId::TypeSpec:
  case TableId::ImplMap:
  case TableId::FieldRVA:
  case TableId::Assembly:
  case TableId::AssemblyRef:
  case TableId::NestedClass:
  case TableId::MethodSpec:
    return true;
  default:
    return false;
  }
}

/// The bytes [RVA, RVA + Size) as the loader maps them (the rule of the
/// listing's '.data' line): raw data, then zeros up to the section's mapped
/// size.
static Expected<std::vector<uint8_t>> readMapped(const PEImage &Image,
                                                 uint32_t RVA, uint64_t Size) {
  for (const PESectionInfo &S : Image.sections()) {
    uint64_t Extent = S.VirtualSize ? S.VirtualSize : S.SizeOfRawData;
    if (RVA < S.VirtualAddress || RVA >= uint64_t(S.VirtualAddress) + Extent)
      continue;
    uint64_t Rel = RVA - S.VirtualAddress;
    if (Rel + Size > Extent)
      return malformed("invalid CLI image: data at RVA 0x" + utohexstr(RVA) +
                       " (" + Twine(Size) + " bytes) extends past its section");
    ArrayRef<uint8_t> File = Image.getFile();
    std::vector<uint8_t> Out(Size, 0);
    for (uint64_t K = 0; K != Size; ++K) {
      uint64_t At = Rel + K;
      if (At >= S.SizeOfRawData)
        break;
      uint64_t Offset = uint64_t(S.PointerToRawData) + At;
      if (Offset >= File.size())
        return malformed("invalid CLI image: section data at RVA 0x" +
                         utohexstr(RVA) + " runs past the end of the file");
      Out[K] = File[Offset];
    }
    return Out;
  }
  return malformed("invalid CLI image: RVA 0x" + utohexstr(RVA) +
                   " is not in a section");
}

namespace {

/// The kinds of signature blob that the copy re-encodes.
enum class SigKind {
  Method,
  Field,
  Local,
  Property,
  TypeSpec,
  MethodSpec,
  /// A field or a method signature, by its first byte.
  MemberRef,
  /// A local, field or method signature, by its first byte.
  StandAlone
};

/// One copy of one image.
class Copier {
public:
  explicit Copier(const MetadataReader &R) : R(R), Image(*R.getImage()) {}

  Error copy(SmallVectorImpl<uint8_t> &Out);

private:
  Error checkTables() const;
  Error checkRow(uint32_t Token, StringRef What) const;
  /// The handle of an input row that was copied.
  Expected<EntityHandle> mapped(uint32_t Token, StringRef What) const;
  /// The final token of an input row, after finalize().
  Expected<uint32_t> finalToken(uint32_t Token, StringRef What) const;

  Error copyAssembly();
  Error copyReferences();
  Error copyTypeRef(uint32_t Row, unsigned Depth);
  Error copyTypeSpec(uint32_t Row);
  Error copyTypeDefs();
  Error copyMembers();
  Error copyMemberRefsAndSigs();
  Error copySortedTables();
  Error copyFieldData();
  Error copyCustomAttributes();
  Error copyUserStrings();
  Error copyBodies();

  /// Copies every TypeSpec that \p T names, and checks every token.
  Error prepare(const TypeSig &T);
  Error prepare(const MethodSig &S);
  /// The new token of a TypeDef, TypeRef or TypeSpec token that prepare()
  /// accepted. TypeDefs keep their rows: they are added in order.
  uint32_t mapTypeToken(uint32_t Token) const;
  /// Re-encodes the signature at #Blob offset \p Blob of the input and adds
  /// it to the copy.
  Expected<uint32_t> copySignature(uint32_t Blob, SigKind Kind);
  Expected<int64_t> fieldDataSize(uint32_t SigBlob) const;

  const MetadataReader &R;
  const PEImage &Image;
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  SmallVector<uint8_t, 0> MappedData;
  /// Input token -> the copy's handle.
  DenseMap<uint32_t, EntityHandle> Map;
  /// TypeSpec rows being copied (cycle detection).
  DenseSet<uint32_t> TypeSpecsInProgress;
  /// #US offset of the input -> ldstr token of the copy.
  DenseMap<uint32_t, uint32_t> UserStrings;
};

} // namespace

Error Copier::checkTables() const {
  for (unsigned Id = 0;
       Id <= static_cast<unsigned>(TableId::GenericParamConstraint); ++Id) {
    TableId T = static_cast<TableId>(Id);
    uint32_t Rows = R.getRowCount(T);
    if (Rows && !isWriterTable(T))
      return unsupported("unsupported table " + getTableName(T) + " (" +
                         Twine(Rows) + " rows)");
  }
  return Error::success();
}

Error Copier::checkRow(uint32_t Token, StringRef What) const {
  uint8_t T = getTokenType(Token);
  uint32_t Row = getTokenRow(Token);
  if (T > static_cast<uint8_t>(TableId::GenericParamConstraint) || Row == 0 ||
      Row > R.getRowCount(static_cast<TableId>(T)))
    return malformed("invalid CLI metadata: invalid " + What + " token " +
                     hex8(Token));
  return Error::success();
}

Expected<EntityHandle> Copier::mapped(uint32_t Token, StringRef What) const {
  auto It = Map.find(Token);
  if (It == Map.end())
    return malformed("invalid CLI metadata: " + What + " token " + hex8(Token) +
                     " names no row that the copy holds");
  return It->second;
}

Expected<uint32_t> Copier::finalToken(uint32_t Token, StringRef What) const {
  Expected<EntityHandle> H = mapped(Token, What);
  if (!H)
    return H.takeError();
  return MB.getToken(*H);
}

uint32_t Copier::mapTypeToken(uint32_t Token) const {
  if (isTable(Token, TableId::TypeDef))
    return Token;
  return Map.lookup(Token).Value;
}

Error Copier::prepare(const TypeSig &T) {
  auto Ref = [&](uint32_t Token) -> Error {
    if (Error E = checkRow(Token, "signature type"))
      return E;
    if (isTable(Token, TableId::TypeSpec))
      return copyTypeSpec(getTokenRow(Token));
    if (isTable(Token, TableId::TypeRef) && !Map.count(Token))
      return malformed("invalid CLI metadata: TypeRef " + hex8(Token) +
                       " was not copied");
    return Error::success();
  };
  for (const CustomModifier &M : T.getModifiers())
    if (Error E = Ref(M.Type))
      return E;
  switch (T.getElementType()) {
  case ELEMENT_TYPE_CLASS:
  case ELEMENT_TYPE_VALUETYPE:
    return Ref(T.getTypeRef());
  case ELEMENT_TYPE_GENERICINST:
    if (Error E = Ref(T.getTypeRef()))
      return E;
    for (const TypeSig &A : T.getOperands())
      if (Error E = prepare(A))
        return E;
    return Error::success();
  case ELEMENT_TYPE_PTR:
  case ELEMENT_TYPE_BYREF:
  case ELEMENT_TYPE_SZARRAY:
  case ELEMENT_TYPE_ARRAY:
  case ELEMENT_TYPE_PINNED:
    return prepare(T.getOperands()[0]);
  case ELEMENT_TYPE_FNPTR:
    return prepare(T.getFunctionSig());
  default:
    return Error::success();
  }
}

Error Copier::prepare(const MethodSig &S) {
  if (Error E = prepare(S.Return))
    return E;
  for (const TypeSig &P : S.Params)
    if (Error E = prepare(P))
      return E;
  return Error::success();
}

Expected<uint32_t> Copier::copySignature(uint32_t Blob, SigKind Kind) {
  Expected<ArrayRef<uint8_t>> Bytes = R.getBlob(Blob);
  if (!Bytes)
    return Bytes.takeError();
  uint8_t First = Bytes->empty() ? 0 : (*Bytes)[0];
  if (Kind == SigKind::MemberRef)
    Kind = First == SIG_FIELD ? SigKind::Field : SigKind::Method;
  else if (Kind == SigKind::StandAlone)
    Kind = First == SIG_LOCAL   ? SigKind::Local
           : First == SIG_FIELD ? SigKind::Field
                                : SigKind::Method;
  auto MapRef = [this](uint32_t Token) { return mapTypeToken(Token); };
  SmallVector<uint8_t, 32> Out;
  switch (Kind) {
  case SigKind::Method: {
    Expected<MethodSig> S = decodeMethodSig(*Bytes);
    if (!S)
      return S.takeError();
    if (Error E = prepare(*S))
      return std::move(E);
    encodeMethodSig(mapTypeRefs(*S, MapRef), Out);
    break;
  }
  case SigKind::Field: {
    Expected<TypeSig> T = decodeFieldSig(*Bytes);
    if (!T)
      return T.takeError();
    if (Error E = prepare(*T))
      return std::move(E);
    encodeFieldSig(mapTypeRefs(*T, MapRef), Out);
    break;
  }
  case SigKind::Local: {
    Expected<std::vector<TypeSig>> Locals = decodeLocalSig(*Bytes);
    if (!Locals)
      return Locals.takeError();
    std::vector<TypeSig> New;
    for (const TypeSig &L : *Locals) {
      if (Error E = prepare(L))
        return std::move(E);
      New.push_back(mapTypeRefs(L, MapRef));
    }
    encodeLocalSig(New, Out);
    break;
  }
  case SigKind::Property: {
    Expected<PropertySig> P = decodePropertySig(*Bytes);
    if (!P)
      return P.takeError();
    if (Error E = prepare(P->Type))
      return std::move(E);
    PropertySig New;
    New.HasThis = P->HasThis;
    New.Type = mapTypeRefs(P->Type, MapRef);
    for (const TypeSig &T : P->Params) {
      if (Error E = prepare(T))
        return std::move(E);
      New.Params.push_back(mapTypeRefs(T, MapRef));
    }
    encodePropertySig(New, Out);
    break;
  }
  case SigKind::TypeSpec: {
    Expected<TypeSig> T = decodeTypeSpec(*Bytes);
    if (!T)
      return T.takeError();
    if (Error E = prepare(*T))
      return std::move(E);
    encodeTypeSpec(mapTypeRefs(*T, MapRef), Out);
    break;
  }
  case SigKind::MethodSpec: {
    Expected<std::vector<TypeSig>> Args = decodeMethodSpec(*Bytes);
    if (!Args)
      return Args.takeError();
    std::vector<TypeSig> New;
    for (const TypeSig &A : *Args) {
      if (Error E = prepare(A))
        return std::move(E);
      New.push_back(mapTypeRefs(A, MapRef));
    }
    encodeMethodSpec(New, Out);
    break;
  }
  case SigKind::MemberRef:
  case SigKind::StandAlone:
    llvm_unreachable("resolved above");
  }
  return MB.addBlob(Out);
}

/// The size of a primitive field type, or the ClassLayout size of a TypeDef
/// valuetype; -1 otherwise (the rule of the listing's '.data' line).
Expected<int64_t> Copier::fieldDataSize(uint32_t SigBlob) const {
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
      return malformed("invalid signature: bad valuetype in a field signature");
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
      return malformed("invalid CLI metadata: ClassLayout size " +
                       Twine((*Layout)->ClassSize) + " is too large");
    return int64_t((*Layout)->ClassSize);
  }
  default:
    return -1;
  }
}

Error Copier::copyAssembly() {
  if (R.getRowCount(TableId::Module) != 1)
    return malformed("invalid CLI metadata: the Module table has " +
                     Twine(R.getRowCount(TableId::Module)) +
                     " rows (a module has one)");
  Expected<ModuleRow> Module = R.getModule();
  if (!Module)
    return Module.takeError();
  MB.setModuleName(Module->Name);
  Map[makeToken(TableId::Module, 1)] =
      EntityHandle{makeToken(TableId::Module, 1)};
  if (!R.isAssembly())
    return Error::success();
  if (R.getRowCount(TableId::Assembly) != 1)
    return malformed("invalid CLI metadata: the Assembly table has " +
                     Twine(R.getRowCount(TableId::Assembly)) +
                     " rows (an assembly has one)");
  Expected<AssemblyRow> A = R.getAssembly();
  if (!A)
    return A.takeError();
  Expected<ArrayRef<uint8_t>> Key = R.getBlob(A->PublicKey);
  if (!Key)
    return Key.takeError();
  AssemblyIdentity Id;
  Id.Name = A->Name.str();
  Id.Version = A->Version;
  Id.Flags = A->Flags;
  Id.HashAlg = A->HashAlg;
  Id.PublicKey.assign(Key->begin(), Key->end());
  Id.Culture = A->Culture.str();
  MB.setAssembly(Id);
  Map[makeToken(TableId::Assembly, 1)] =
      EntityHandle{makeToken(TableId::Assembly, 1)};
  return Error::success();
}

Error Copier::copyReferences() {
  for (uint32_t Row = 1, N = R.getRowCount(TableId::AssemblyRef); Row <= N;
       ++Row) {
    Expected<AssemblyRefRow> A = R.getAssemblyRef(Row);
    if (!A)
      return A.takeError();
    Expected<ArrayRef<uint8_t>> Key = R.getBlob(A->PublicKeyOrToken);
    if (!Key)
      return Key.takeError();
    Expected<ArrayRef<uint8_t>> Hash = R.getBlob(A->HashValue);
    if (!Hash)
      return Hash.takeError();
    AssemblyRefInfo Info;
    Info.Name = A->Name.str();
    Info.Version = A->Version;
    Info.Flags = A->Flags;
    Info.PublicKeyOrToken.assign(Key->begin(), Key->end());
    Info.Culture = A->Culture.str();
    Info.HashValue.assign(Hash->begin(), Hash->end());
    Map[makeToken(TableId::AssemblyRef, Row)] = MB.addAssemblyRef(Info);
  }
  for (uint32_t Row = 1, N = R.getRowCount(TableId::ModuleRef); Row <= N;
       ++Row) {
    Expected<ModuleRefRow> M = R.getModuleRef(Row);
    if (!M)
      return M.takeError();
    Map[makeToken(TableId::ModuleRef, Row)] = MB.addModuleRef(M->Name);
  }
  for (uint32_t Row = 1, N = R.getRowCount(TableId::TypeRef); Row <= N; ++Row)
    if (Error E = copyTypeRef(Row, 0))
      return E;
  return Error::success();
}

Error Copier::copyTypeRef(uint32_t Row, unsigned Depth) {
  uint32_t Token = makeToken(TableId::TypeRef, Row);
  if (Map.count(Token))
    return Error::success();
  if (Depth > MaxNesting)
    return malformed("invalid CLI metadata: the resolution scopes of TypeRef " +
                     hex8(Token) + " are nested deeper than " +
                     Twine(MaxNesting) + " levels");
  Expected<TypeRefRow> T = R.getTypeRef(Row);
  if (!T)
    return T.takeError();
  EntityHandle Scope;
  if (uint32_t S = T->ResolutionScope) {
    if (isTable(S, TableId::TypeRef))
      if (Error E = copyTypeRef(getTokenRow(S), Depth + 1))
        return E;
    Expected<EntityHandle> H = mapped(S, "resolution scope");
    if (!H)
      return H.takeError();
    Scope = *H;
  }
  EntityHandle H = MB.addTypeRef(Scope, T->Namespace, T->Name);
  if (H.isNil())
    return malformed("cannot write CLI metadata: too many TypeRef rows");
  Map[Token] = H;
  return Error::success();
}

Error Copier::copyTypeSpec(uint32_t Row) {
  uint32_t Token = makeToken(TableId::TypeSpec, Row);
  if (Map.count(Token))
    return Error::success();
  if (TypeSpecsInProgress.count(Token))
    return malformed("invalid signature: TypeSpec " + hex8(Token) +
                     " names itself");
  if (TypeSpecsInProgress.size() >= MaxNesting)
    return malformed("invalid signature: TypeSpecs nested deeper than " +
                     Twine(MaxNesting) + " levels");
  TypeSpecsInProgress.insert(Token);
  Expected<TypeSpecRow> T = R.getTypeSpec(Row);
  if (!T)
    return T.takeError();
  Expected<uint32_t> Blob = copySignature(T->Signature, SigKind::TypeSpec);
  if (!Blob)
    return Blob.takeError();
  TypeSpecsInProgress.erase(Token);
  EntityHandle H = MB.addTypeSpec(*Blob);
  if (H.isNil())
    return malformed("cannot write CLI metadata: too many TypeSpec rows");
  Map[Token] = H;
  return Error::success();
}

Error Copier::copyTypeDefs() {
  uint32_t NumTypes = R.getRowCount(TableId::TypeDef);
  if (NumTypes == 0)
    return malformed("invalid CLI metadata: the module has no TypeDef rows");
  // MetadataBuilder creates TypeDef row 1, '<Module>' (flags 0, no base).
  Expected<TypeDefRow> Module = R.getTypeDef(1);
  if (!Module)
    return Module.takeError();
  if (Module->Name != "<Module>" || !Module->Namespace.empty() ||
      Module->Flags != TypeAttributes() || Module->Extends != 0)
    return unsupported("TypeDef row 1 is not the '<Module>' type that "
                       "MetadataBuilder writes (flags 0, no base type)");
  Map[makeToken(TableId::TypeDef, 1)] = MB.getModuleType();
  for (uint32_t Row = 2; Row <= NumTypes; ++Row) {
    uint32_t Token = makeToken(TableId::TypeDef, Row);
    Expected<TypeDefRow> T = R.getTypeDef(Row);
    if (!T)
      return T.takeError();
    EntityHandle Extends;
    if (T->Extends) {
      if (isTable(T->Extends, TableId::TypeDef) &&
          getTokenRow(T->Extends) >= Row)
        return unsupported("type '" + T->Name + "' (TypeDef row " + Twine(Row) +
                           ") extends a later TypeDef, row " +
                           Twine(getTokenRow(T->Extends)));
      Expected<EntityHandle> H = mapped(T->Extends, "base type");
      if (!H)
        return H.takeError();
      Extends = *H;
    }
    EntityHandle Enclosing;
    Expected<uint32_t> Outer = R.getEnclosingType(Token);
    if (!Outer)
      return Outer.takeError();
    if (*Outer) {
      if (getTokenRow(*Outer) >= Row)
        return unsupported("type '" + T->Name + "' (TypeDef row " + Twine(Row) +
                           ") is nested in a later TypeDef, row " +
                           Twine(getTokenRow(*Outer)));
      Expected<EntityHandle> H = mapped(*Outer, "enclosing type");
      if (!H)
        return H.takeError();
      Enclosing = *H;
    }
    EntityHandle H =
        MB.addTypeDef(T->Flags, T->Namespace, T->Name, Extends, Enclosing);
    // Signatures name TypeDefs by their input rows (mapTypeToken).
    if (H.getIndex() != Row)
      return malformed("cannot write CLI metadata: TypeDef row " + Twine(Row) +
                       " became row " + Twine(H.getIndex()));
    Map[Token] = H;
  }
  return Error::success();
}

/// Fields, methods, parameters and properties, grouped by their owners as in
/// the input.
Error Copier::copyMembers() {
  uint32_t NumTypes = R.getRowCount(TableId::TypeDef);
  uint32_t Fields = 0, Methods = 0, Params = 0, Properties = 0;
  for (uint32_t Row = 1; Row <= NumTypes; ++Row) {
    uint32_t TypeToken = makeToken(TableId::TypeDef, Row);
    EntityHandle Owner = Map.lookup(TypeToken);
    Expected<TypeDefRow> T = R.getTypeDef(Row);
    if (!T)
      return T.takeError();
    for (uint32_t F = T->FieldBegin; F < T->FieldEnd; ++F, ++Fields) {
      Expected<FieldRow> FR = R.getField(F);
      if (!FR)
        return FR.takeError();
      Expected<uint32_t> Sig = copySignature(FR->Signature, SigKind::Field);
      if (!Sig)
        return Sig.takeError();
      Map[makeToken(TableId::Field, F)] =
          MB.addField(Owner, FR->Flags, FR->Name, *Sig);
    }
    for (uint32_t M = T->MethodBegin; M < T->MethodEnd; ++M, ++Methods) {
      Expected<MethodDefRow> MR = R.getMethodDef(M);
      if (!MR)
        return MR.takeError();
      Expected<uint32_t> Sig = copySignature(MR->Signature, SigKind::Method);
      if (!Sig)
        return Sig.takeError();
      EntityHandle Method =
          MB.addMethod(Owner, MR->Flags, MR->ImplFlags, MR->Name, *Sig);
      Map[makeToken(TableId::MethodDef, M)] = Method;
      for (uint32_t P = MR->ParamBegin; P < MR->ParamEnd; ++P, ++Params) {
        Expected<ParamRow> PR = R.getParam(P);
        if (!PR)
          return PR.takeError();
        Map[makeToken(TableId::Param, P)] =
            MB.addParam(Method, PR->Sequence, PR->Flags, PR->Name);
      }
    }
    Expected<std::pair<uint32_t, uint32_t>> Props =
        R.getPropertyRange(TypeToken);
    if (!Props)
      return Props.takeError();
    for (uint32_t P = Props->first; P < Props->second; ++P, ++Properties) {
      Expected<PropertyRow> PR = R.getProperty(P);
      if (!PR)
        return PR.takeError();
      Expected<uint32_t> Sig = copySignature(PR->Signature, SigKind::Property);
      if (!Sig)
        return Sig.takeError();
      Map[makeToken(TableId::Property, P)] =
          MB.addProperty(Owner, PR->Flags, PR->Name, *Sig);
    }
  }
  // Every member row must belong to an owner: the runs cover their tables.
  struct {
    TableId Table;
    uint32_t Copied;
  } Members[] = {{TableId::Field, Fields},
                 {TableId::MethodDef, Methods},
                 {TableId::Param, Params},
                 {TableId::Property, Properties}};
  for (const auto &M : Members)
    if (M.Copied != R.getRowCount(M.Table))
      return malformed("invalid CLI metadata: " + Twine(M.Copied) + " of the " +
                       Twine(R.getRowCount(M.Table)) + " " +
                       getTableName(M.Table) +
                       " rows are in the runs of their owners");
  return Error::success();
}

Error Copier::copyMemberRefsAndSigs() {
  for (uint32_t Row = 1, N = R.getRowCount(TableId::MemberRef); Row <= N;
       ++Row) {
    Expected<MemberRefRow> M = R.getMemberRef(Row);
    if (!M)
      return M.takeError();
    if (M->Parent == 0)
      return malformed("invalid CLI metadata: MemberRef row " + Twine(Row) +
                       " has no parent");
    if (isTable(M->Parent, TableId::TypeSpec))
      if (Error E = copyTypeSpec(getTokenRow(M->Parent)))
        return E;
    Expected<EntityHandle> Parent = mapped(M->Parent, "MemberRef parent");
    if (!Parent)
      return Parent.takeError();
    Expected<uint32_t> Sig = copySignature(M->Signature, SigKind::MemberRef);
    if (!Sig)
      return Sig.takeError();
    Map[makeToken(TableId::MemberRef, Row)] =
        MB.addMemberRef(*Parent, M->Name, *Sig);
  }
  for (uint32_t Row = 1, N = R.getRowCount(TableId::StandAloneSig); Row <= N;
       ++Row) {
    Expected<StandAloneSigRow> S = R.getStandAloneSig(Row);
    if (!S)
      return S.takeError();
    Expected<uint32_t> Sig = copySignature(S->Signature, SigKind::StandAlone);
    if (!Sig)
      return Sig.takeError();
    Map[makeToken(TableId::StandAloneSig, Row)] = MB.addStandAloneSig(*Sig);
  }
  for (uint32_t Row = 1, N = R.getRowCount(TableId::MethodSpec); Row <= N;
       ++Row) {
    Expected<MethodSpecRow> S = R.getMethodSpec(Row);
    if (!S)
      return S.takeError();
    if (S->Method == 0)
      return malformed("invalid CLI metadata: MethodSpec row " + Twine(Row) +
                       " names no method");
    Expected<EntityHandle> Method = mapped(S->Method, "MethodSpec method");
    if (!Method)
      return Method.takeError();
    Expected<uint32_t> Sig =
        copySignature(S->Instantiation, SigKind::MethodSpec);
    if (!Sig)
      return Sig.takeError();
    Map[makeToken(TableId::MethodSpec, Row)] = MB.addMethodSpec(*Method, *Sig);
  }
  return Error::success();
}

Error Copier::copySortedTables() {
  for (uint32_t Row = 1, N = R.getRowCount(TableId::MethodSemantics); Row <= N;
       ++Row) {
    Expected<MethodSemanticsRow> S = R.getMethodSemanticsRow(Row);
    if (!S)
      return S.takeError();
    if (S->Association == 0 || !isTable(S->Association, TableId::Property))
      return unsupported("MethodSemantics row " + Twine(Row) +
                         " is not a property's (events are not copied)");
    if (S->Method == 0)
      return malformed("invalid CLI metadata: MethodSemantics row " +
                       Twine(Row) + " names no method");
    Expected<EntityHandle> Property = mapped(S->Association, "property");
    if (!Property)
      return Property.takeError();
    Expected<EntityHandle> Method = mapped(S->Method, "semantics method");
    if (!Method)
      return Method.takeError();
    MB.addMethodSemantics(*Property, S->Semantics, *Method);
  }
  for (uint32_t Row = 1, N = R.getRowCount(TableId::ImplMap); Row <= N; ++Row) {
    Expected<ImplMapRow> I = R.getImplMap(Row);
    if (!I)
      return I.takeError();
    if (I->MemberForwarded == 0 || I->ImportScope == 0)
      return malformed("invalid CLI metadata: ImplMap row " + Twine(Row) +
                       " names no member or no module");
    Expected<EntityHandle> Member = mapped(I->MemberForwarded, "P/Invoke");
    if (!Member)
      return Member.takeError();
    Expected<EntityHandle> Module = mapped(I->ImportScope, "P/Invoke module");
    if (!Module)
      return Module.takeError();
    MB.addImplMap(*Member, I->Flags, I->ImportName, *Module);
  }
  for (uint32_t Row = 1, N = R.getRowCount(TableId::ClassLayout); Row <= N;
       ++Row) {
    Expected<ClassLayoutRow> L = R.getClassLayout(Row);
    if (!L)
      return L.takeError();
    if (L->Parent == 0)
      return malformed("invalid CLI metadata: ClassLayout row " + Twine(Row) +
                       " names no type");
    Expected<EntityHandle> Type = mapped(L->Parent, "ClassLayout parent");
    if (!Type)
      return Type.takeError();
    MB.addClassLayout(*Type, L->PackingSize, L->ClassSize);
  }
  for (uint32_t Row = 1, N = R.getRowCount(TableId::FieldLayout); Row <= N;
       ++Row) {
    Expected<FieldLayoutRow> L = R.getFieldLayout(Row);
    if (!L)
      return L.takeError();
    if (L->Field == 0)
      return malformed("invalid CLI metadata: FieldLayout row " + Twine(Row) +
                       " names no field");
    Expected<EntityHandle> Field = mapped(L->Field, "FieldLayout field");
    if (!Field)
      return Field.takeError();
    MB.addFieldLayout(*Field, L->Offset);
  }
  return Error::success();
}

Error Copier::copyFieldData() {
  for (uint32_t Row = 1, N = R.getRowCount(TableId::FieldRVA); Row <= N;
       ++Row) {
    Expected<FieldRVARow> F = R.getFieldRVA(Row);
    if (!F)
      return F.takeError();
    if (F->Field == 0)
      return malformed("invalid CLI metadata: FieldRVA row " + Twine(Row) +
                       " names no field");
    Expected<EntityHandle> Field = mapped(F->Field, "FieldRVA field");
    if (!Field)
      return Field.takeError();
    Expected<FieldRow> FR = R.getField(getTokenRow(F->Field));
    if (!FR)
      return FR.takeError();
    Expected<int64_t> Size = fieldDataSize(FR->Signature);
    if (!Size)
      return Size.takeError();
    if (*Size < 0)
      return unsupported(
          "the FieldRVA data of field '" + FR->Name +
          "' has no known size (its type is neither a primitive nor a "
          "valuetype TypeDef with a ClassLayout row)");
    Expected<std::vector<uint8_t>> Data =
        readMapped(Image, F->RVA, uint64_t(*Size));
    if (!Data)
      return Data.takeError();
    MappedData.resize(alignTo(MappedData.size(), 8), 0);
    if (MappedData.size() + Data->size() > UINT32_MAX)
      return unsupported("the FieldRVA data is larger than 4 GiB");
    MB.addFieldRVA(*Field, static_cast<uint32_t>(MappedData.size()));
    MappedData.append(Data->begin(), Data->end());
    // An RVA must lie inside a section even without data (a ClassLayout of
    // size 0), so empty data still takes a byte.
    if (Data->empty())
      MappedData.push_back(0);
  }
  return Error::success();
}

Error Copier::copyCustomAttributes() {
  for (uint32_t Row = 1, N = R.getRowCount(TableId::CustomAttribute); Row <= N;
       ++Row) {
    Expected<CustomAttributeRow> CA = R.getCustomAttribute(Row);
    if (!CA)
      return CA.takeError();
    if (CA->Parent == 0 || CA->Constructor == 0)
      return malformed("invalid CLI metadata: CustomAttribute row " +
                       Twine(Row) + " names no parent or no constructor");
    Expected<EntityHandle> Parent = mapped(CA->Parent, "attribute parent");
    if (!Parent)
      return Parent.takeError();
    Expected<EntityHandle> Ctor = mapped(CA->Constructor, "constructor");
    if (!Ctor)
      return Ctor.takeError();
    Expected<ArrayRef<uint8_t>> Value = R.getBlob(CA->Value);
    if (!Value)
      return Value.takeError();
    MB.addCustomAttribute(*Parent, *Ctor, MB.addBlob(*Value));
  }
  return Error::success();
}

/// Adds the ldstr strings of every body to the copy's #US, before
/// finalize().
Error Copier::copyUserStrings() {
  DenseSet<uint32_t> Seen;
  for (uint32_t Row = 1, N = R.getRowCount(TableId::MethodDef); Row <= N;
       ++Row) {
    uint32_t Token = makeToken(TableId::MethodDef, Row);
    Expected<MethodDefRow> M = R.getMethodDef(Row);
    if (!M)
      return M.takeError();
    if (M->RVA == 0 || !Seen.insert(M->RVA).second)
      continue;
    if ((M->ImplFlags & MethodImplAttributes::CodeTypeMask) !=
        MethodImplAttributes::IL)
      return unsupported("method '" + M->Name + "' (MethodDef row " +
                         Twine(Row) + ") has a body that is not IL");
    Expected<MethodBodyView> B = R.getMethodBody(Token);
    if (!B)
      return B.takeError();
    Error E = decodeIL(B->Code, [&](const ILInstruction &I) -> Error {
      if (I.Op != Opcode::Ldstr)
        return Error::success();
      uint32_t Offset = getTokenRow(I.Token);
      if (getTokenType(I.Token) != UserStringTokenType || Offset == 0)
        return malformed("invalid CLI metadata: invalid string token " +
                         hex8(I.Token));
      if (UserStrings.count(Offset))
        return Error::success();
      // getUserStringUTF16 checks the offset and the length, so getUserString
      // can then fail only for a lone surrogate (MetadataReader.h). The image
      // is valid, but addUserString takes UTF-8, which cannot hold an unpaired
      // surrogate (plan section 12, 5.7.10 amended).
      if (Error E = R.getUserStringUTF16(Offset).takeError())
        return E;
      Expected<std::string> S = R.getUserString(Offset);
      if (!S) {
        consumeError(S.takeError());
        return unsupported("unsupported user string at #US offset " +
                           hex8(Offset) + ": it holds a lone UTF-16 surrogate");
      }
      UserStrings[Offset] = MB.addUserString(*S);
      return Error::success();
    });
    if (E)
      return E;
  }
  return Error::success();
}

/// Copies the bodies after finalize(), which numbered the member rows.
Error Copier::copyBodies() {
  DenseMap<uint32_t, uint32_t> Offsets;
  auto MapToken = [&](const ILInstruction &I) -> Expected<uint32_t> {
    if (getTokenType(I.Token) == UserStringTokenType) {
      auto It = UserStrings.find(getTokenRow(I.Token));
      if (It == UserStrings.end())
        return malformed("invalid CLI metadata: invalid string token " +
                         hex8(I.Token));
      return It->second;
    }
    return finalToken(I.Token, "IL operand");
  };
  for (uint32_t Row = 1, N = R.getRowCount(TableId::MethodDef); Row <= N;
       ++Row) {
    uint32_t Token = makeToken(TableId::MethodDef, Row);
    Expected<MethodDefRow> M = R.getMethodDef(Row);
    if (!M)
      return M.takeError();
    if (M->RVA == 0)
      continue;
    auto It = Offsets.find(M->RVA);
    if (It == Offsets.end()) {
      Expected<MethodBodyView> B = R.getMethodBody(Token);
      if (!B)
        return B.takeError();
      SmallVector<uint8_t, 0> Code(B->Code.begin(), B->Code.end());
      if (Error E = rewriteILTokens(Code, MapToken))
        return E;
      SmallVector<ExceptionClause, 0> Clauses(B->Clauses.begin(),
                                              B->Clauses.end());
      for (ExceptionClause &C : Clauses) {
        if (C.Kind != ExceptionClauseKind::Catch)
          continue;
        Expected<uint32_t> Class =
            finalToken(C.ClassTokenOrFilterOffset, "catch type");
        if (!Class)
          return Class.takeError();
        C.ClassTokenOrFilterOffset = *Class;
      }
      MethodBodyInfo Info;
      Info.Code = Code;
      Info.MaxStack = B->MaxStack;
      Info.InitLocals = B->InitLocals;
      Info.Clauses = Clauses;
      if (B->LocalVarSigToken) {
        if (!isTable(B->LocalVarSigToken, TableId::StandAloneSig))
          return malformed("invalid CLI metadata: local signature token " +
                           hex8(B->LocalVarSigToken) +
                           " is not a StandAloneSig");
        Expected<uint32_t> Locals =
            finalToken(B->LocalVarSigToken, "local signature");
        if (!Locals)
          return Locals.takeError();
        Info.LocalVarSigToken = *Locals;
      }
      It = Offsets.insert({M->RVA, Bodies.addBody(Info)}).first;
    }
    MB.setMethodBodyOffset(Map.lookup(Token), It->second);
  }
  return Error::success();
}

Error Copier::copy(SmallVectorImpl<uint8_t> &Out) {
  if (Error E = checkTables())
    return E;
  Expected<Cor20Header> Cor = Image.getCLIHeader();
  if (!Cor)
    return Cor.takeError();
  uint32_t Flags = Cor->Flags;
  if (!(Flags & COMIMAGE_FLAGS_ILONLY))
    return unsupported("not an IL-only image (CorFlags " + hex8(Flags) + ")");
  if (Flags & COMIMAGE_FLAGS_NATIVE_ENTRYPOINT)
    return unsupported("the entry point is native code (CorFlags " +
                       hex8(Flags) + ")");
  uint32_t EntryPoint = Cor->EntryPointToken;
  if (EntryPoint != 0) {
    if (!isTable(EntryPoint, TableId::MethodDef))
      return unsupported("the entry point token " + hex8(EntryPoint) +
                         " is not a MethodDef");
    if (Error E = checkRow(EntryPoint, "entry point"))
      return E;
  }

  // Rows in table order: the stable tables first, so that every signature
  // blob names final tokens when it is added.
  if (Error E = copyAssembly())
    return E;
  if (Error E = copyReferences())
    return E;
  for (uint32_t Row = 1, N = R.getRowCount(TableId::TypeSpec); Row <= N; ++Row)
    if (Error E = copyTypeSpec(Row))
      return E;
  if (Error E = copyTypeDefs())
    return E;
  if (Error E = copyMembers())
    return E;
  if (Error E = copyMemberRefsAndSigs())
    return E;
  if (Error E = copySortedTables())
    return E;
  if (Error E = copyFieldData())
    return E;
  if (Error E = copyCustomAttributes())
    return E;
  if (Error E = copyUserStrings())
    return E;
  if (Error E = MB.finalize(Bodies))
    return E;
  if (Error E = copyBodies())
    return E;

  PEImageKind Kind = EntryPoint       ? PEImageKind::Executable
                     : R.isAssembly() ? PEImageKind::Library
                                      : PEImageKind::Module;
  // A PE32+ machine takes PEOptions::get's defaults for its kind (the image
  // base included; get reads only the triple's architecture). Any other
  // machine goes to writePEImage as it is, which refuses it until phase 5.
  PEOptions Opts;
  uint16_t Machine = Image.getMachine();
  if (Machine == COFF::IMAGE_FILE_MACHINE_AMD64 ||
      Machine == COFF::IMAGE_FILE_MACHINE_ARM64) {
    Expected<PEOptions> Defaults =
        PEOptions::get(Triple(Machine == COFF::IMAGE_FILE_MACHINE_AMD64
                                  ? "x86_64-pc-windows-msvc-msil"
                                  : "aarch64-pc-windows-msvc-msil"),
                       Kind);
    if (!Defaults)
      return Defaults.takeError();
    Opts = *Defaults;
  } else {
    Opts.Machine = static_cast<COFF::MachineTypes>(Machine);
    Opts.Kind = Kind;
  }
  // The copy carries no strong-name signature.
  Opts.CorFlags = Flags & ~uint32_t(COMIMAGE_FLAGS_STRONGNAMESIGNED);
  Opts.Subsystem = Image.getSubsystem();
  PEContent C;
  C.Metadata = &MB;
  C.ILStream = Bodies.getBytes();
  C.MappedFieldData = MappedData;
  if (EntryPoint) {
    Expected<uint32_t> Token = finalToken(EntryPoint, "entry point");
    if (!Token)
      return Token.takeError();
    C.EntryPointToken = *Token;
  }
  return writePEImage(Opts, C, Out);
}

static int fail(StringRef File, const Twine &Message) {
  WithColor::error(errs(), ToolName) << "'" << File << "': " << Message << "\n";
  return 1;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  cl::HideUnrelatedOptions(CopyCategory);
  cl::ParseCommandLineOptions(
      argc, argv,
      "Copy an IL-only image through the ECMA-335 reader and writer\n");

  SmallVector<uint8_t, 0> Image;
  {
    ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
        MemoryBuffer::getFileOrSTDIN(InputFilename, /*IsText=*/false,
                                     /*RequiresNullTerminator=*/false);
    if (!Buffer)
      return fail(InputFilename, Buffer.getError().message());
    Expected<std::unique_ptr<MetadataReader>> Reader =
        MetadataReader::create(arrayRefFromStringRef((*Buffer)->getBuffer()));
    if (!Reader)
      return fail(InputFilename, toString(Reader.takeError()));
    Copier C(**Reader);
    if (Error E = C.copy(Image))
      return fail(InputFilename, toString(std::move(E)));
  }
  // The input is released before the output is opened: Windows cannot
  // truncate a file that is mapped, which an in-place copy (-o <input>) would
  // do. ToolOutputFile removes the output unless it was written in full, also
  // when the tool is interrupted.
  std::error_code EC;
  ToolOutputFile Out(OutputFilename, EC, sys::fs::OF_None);
  if (EC)
    return fail(OutputFilename, EC.message());
  raw_fd_ostream &OS = Out.os();
  OS.write(reinterpret_cast<const char *>(Image.data()), Image.size());
  // Standard output ('-o -') is flushed, not closed.
  if (OutputFilename == "-")
    OS.flush();
  else
    OS.close();
  if (OS.has_error()) {
    EC = OS.error();
    OS.clear_error();
    return fail(OutputFilename, EC.message());
  }
  Out.keep();
  return 0;
}
