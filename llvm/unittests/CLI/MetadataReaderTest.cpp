//===- MetadataReaderTest.cpp - ECMA-335 metadata reader ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/MetadataReader.h against the MSIL implementation plan
// (5.7.8, 5.7.12) and ECMA-335 II.24: the create-time checks of the metadata
// root, streams and #~ header, heap and row bounds, the lookups (which must
// not trust the Sorted bits), 4-byte heap and coded indexes, and
// computePublicKeyToken. Malformed roots are rebuilt here from the parts of a
// MetadataBuilder module; every failure must be an Error with the prefix
// "invalid CLI metadata: ", never a crash or an assertion. The public-key
// vectors come from bcl-binding.md 5.6 and System.Reflection.Metadata (the
// System.Runtime 8.0.32 reference assembly's key, read in scratch with
// MetadataReader.GetAssemblyDefinition().PublicKey, whose token
// AssemblyName.GetPublicKeyToken gives as b03f5f7f11d50a3a).
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/MetadataReader.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/CustomAttribute.h"
#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/CLI/PEWriter.h"
#include "llvm/CLI/TableSchema.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::cli;
using testing::HasSubstr;
using testing::StartsWith;

namespace {

using ByteVec = std::vector<uint8_t>;

constexpr char MDPrefix[] = "invalid CLI metadata: ";

template <typename T> void expectMDError(Expected<T> E) {
  ASSERT_FALSE(bool(E)) << "expected an error";
  EXPECT_THAT(toString(E.takeError()), StartsWith(MDPrefix));
}

uint32_t rowOf(uint32_t TokenOrRow) { return TokenOrRow & MaxTableRows; }

EntityHandle rowHandle(TableId T, uint32_t Row) {
  EntityHandle H;
  H.Value = makeToken(T, Row);
  return H;
}

//===----------------------------------------------------------------------===//
// Taking a metadata root apart and putting it back together (II.24.2.1)
//===----------------------------------------------------------------------===//

struct RootParts {
  std::string Version = "v4.0.30319";
  // Overrides the version-length field when set.
  std::optional<uint32_t> VersionLength;
  std::vector<std::pair<std::string, ByteVec>> Streams;

  ByteVec *stream(StringRef Name) {
    for (auto &S : Streams)
      if (S.first == Name)
        return &S.second;
    return nullptr;
  }
};

uint32_t le32(ArrayRef<uint8_t> B, uint64_t Off) {
  return Off + 4 <= B.size() ? support::endian::read32le(B.data() + Off) : 0;
}

RootParts splitRoot(ArrayRef<uint8_t> MD) {
  RootParts P;
  EXPECT_EQ(le32(MD, 0), MetadataSignature);
  uint32_t Len = le32(MD, 12);
  StringRef V(reinterpret_cast<const char *>(MD.data()) + 16, Len);
  P.Version = V.take_until([](char C) { return C == 0; }).str();
  uint64_t Q = 16 + Len;
  uint16_t N = support::endian::read16le(MD.data() + Q + 2);
  Q += 4;
  for (unsigned I = 0; I < N; ++I) {
    uint32_t Off = le32(MD, Q), Size = le32(MD, Q + 4);
    Q += 8;
    std::string Name;
    while (MD[Q])
      Name.push_back(static_cast<char>(MD[Q++]));
    Q = alignTo(Q + 1, 4);
    ArrayRef<uint8_t> Data = MD.slice(Off, Size);
    P.Streams.push_back({Name, ByteVec(Data.begin(), Data.end())});
  }
  return P;
}

ByteVec joinRoot(const RootParts &P) {
  ByteVec B;
  auto U16 = [&](uint16_t V) {
    B.push_back(V & 0xFF);
    B.push_back(V >> 8);
  };
  auto U32 = [&](uint32_t V) {
    for (unsigned I = 0; I < 4; ++I)
      B.push_back((V >> (8 * I)) & 0xFF);
  };
  U32(MetadataSignature);
  U16(1);
  U16(1);
  U32(0);
  uint32_t Len = alignTo(P.Version.size() + 1, 4);
  U32(P.VersionLength.value_or(Len));
  B.insert(B.end(), P.Version.begin(), P.Version.end());
  B.resize(B.size() + Len - P.Version.size(), 0);
  U16(0);
  U16(P.Streams.size());
  uint64_t HeaderSize = 0;
  for (const auto &S : P.Streams)
    HeaderSize += 8 + alignTo(S.first.size() + 1, 4);
  uint64_t Off = B.size() + HeaderSize;
  for (const auto &S : P.Streams) {
    U32(Off);
    U32(S.second.size());
    B.insert(B.end(), S.first.begin(), S.first.end());
    B.resize(alignTo(B.size() + 1, 4), 0);
    Off += alignTo(S.second.size(), 4);
  }
  for (const auto &S : P.Streams) {
    B.insert(B.end(), S.second.begin(), S.second.end());
    B.resize(alignTo(B.size(), 4), 0);
  }
  return B;
}

// The #~ stream's parts: the header fields and row counts, and where the
// table data starts.
struct TablesHeader {
  uint8_t HeapSizes = 0;
  uint64_t Valid = 0, Sorted = 0;
  std::array<uint32_t, NumTableIds> Rows{};
  uint64_t DataOffset = 0;
};

TablesHeader parseTables(ArrayRef<uint8_t> T) {
  TablesHeader H;
  H.HeapSizes = T[6];
  H.Valid = support::endian::read64le(T.data() + 8);
  H.Sorted = support::endian::read64le(T.data() + 16);
  uint64_t Q = 24;
  for (unsigned I = 0; I < NumTableIds; ++I)
    if (H.Valid & (uint64_t(1) << I)) {
      H.Rows[I] = le32(T, Q);
      Q += 4;
    }
  if (H.HeapSizes & HEAP_EXTRA_DATA)
    Q += 4;
  H.DataOffset = Q;
  return H;
}

// The offset of a cell in the #~ stream, and its width.
uint64_t cellOffset(ArrayRef<uint8_t> Tables, TableId T, uint32_t Row,
                    unsigned Col, unsigned &Width) {
  TablesHeader H = parseTables(Tables);
  MetadataSizes S(H.Rows, H.HeapSizes);
  Width = S.getColumnSize(T, Col);
  return H.DataOffset + S.getTableOffset(T) +
         uint64_t(Row - 1) * S.getRowSize(T) + S.getColumnOffset(T, Col);
}

void patchCell(ByteVec &Tables, TableId T, uint32_t Row, unsigned Col,
               uint32_t Value) {
  unsigned Width;
  uint64_t Off = cellOffset(Tables, T, Row, Col, Width);
  ASSERT_LE(Off + Width, Tables.size());
  if (Width == 2)
    support::endian::write16le(Tables.data() + Off, Value);
  else
    support::endian::write32le(Tables.data() + Off, Value);
}

//===----------------------------------------------------------------------===//
// A sample library
//===----------------------------------------------------------------------===//

// The 16-byte ECMA "neutral" key of mscorlib (bcl-binding 5.6).
const uint8_t ECMAKey[16] = {0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0};

// System.Runtime 8.0's public key, read with System.Reflection.Metadata from
// the Microsoft.NETCore.App.Ref 8.0.32 System.Runtime.dll in scratch.
const uint8_t SystemRuntimeKey[160] = {
    0x00, 0x24, 0x00, 0x00, 0x04, 0x80, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00,
    0x06, 0x02, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x52, 0x53, 0x41, 0x31,
    0x00, 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x07, 0xD1, 0xFA, 0x57,
    0xC4, 0xAE, 0xD9, 0xF0, 0xA3, 0x2E, 0x84, 0xAA, 0x0F, 0xAE, 0xFD, 0x0D,
    0xE9, 0xE8, 0xFD, 0x6A, 0xEC, 0x8F, 0x87, 0xFB, 0x03, 0x76, 0x6C, 0x83,
    0x4C, 0x99, 0x92, 0x1E, 0xB2, 0x3B, 0xE7, 0x9A, 0xD9, 0xD5, 0xDC, 0xC1,
    0xDD, 0x9A, 0xD2, 0x36, 0x13, 0x21, 0x02, 0x90, 0x0B, 0x72, 0x3C, 0xF9,
    0x80, 0x95, 0x7F, 0xC4, 0xE1, 0x77, 0x10, 0x8F, 0xC6, 0x07, 0x77, 0x4F,
    0x29, 0xE8, 0x32, 0x0E, 0x92, 0xEA, 0x05, 0xEC, 0xE4, 0xE8, 0x21, 0xC0,
    0xA5, 0xEF, 0xE8, 0xF1, 0x64, 0x5C, 0x4C, 0x0C, 0x93, 0xC1, 0xAB, 0x99,
    0x28, 0x5D, 0x62, 0x2C, 0xAA, 0x65, 0x2C, 0x1D, 0xFA, 0xD6, 0x3D, 0x74,
    0x5D, 0x6F, 0x2D, 0xE5, 0xF1, 0x7E, 0x5E, 0xAF, 0x0F, 0xC4, 0x96, 0x3D,
    0x26, 0x1C, 0x8A, 0x12, 0x43, 0x65, 0x18, 0x20, 0x6D, 0xC0, 0x93, 0x34,
    0x4D, 0x5A, 0xD2, 0x93};

// A library with every kind of row the lookups find: nested types, member
// runs (one empty), a property, a P/Invoke stub, explicit layout, FieldRVA
// data, custom attributes on the assembly, the module and a type (byte-array
// ones through a TypeDef and through a TypeRef constructor) and an ldstr
// string.
struct Sample {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  EntityHandle Obj, ValueType, Outer, Inner, Layout, Empty, MyAttr;
  EntityHandle GData, F1, F2, LayoutX;
  EntityHandle Main, Puts, OuterCtor, GetP, InnerM, MyAttrCtor;
  EntityHandle P, PutsParam, Kernel, LinkCtor;
  uint32_t UserString = 0;
  ByteVec Mapped = {0x10, 0x20, 0x30, 0x40};
  const ByteVec MainCode = {0x72, 0, 0, 0, 0, 0x26, 0x16, 0x2A};
  ByteVec AsmPayload = {0xA0, 0xA1}, LinkPayload = {0x4D, 0x53},
          TypePayload1 = {1}, TypePayload2 = {2, 2};

  uint32_t tok(EntityHandle H) const { return MB.getToken(H); }
};

uint32_t blob(MetadataBuilder &MB, std::initializer_list<uint8_t> B) {
  return MB.addBlob(ArrayRef<uint8_t>(B.begin(), B.end()));
}

uint32_t byteArrayBlob(MetadataBuilder &MB, ArrayRef<uint8_t> Payload) {
  SmallVector<uint8_t, 16> V;
  encodeByteArrayAttribute(Payload, V);
  return MB.addBlob(V);
}

std::unique_ptr<Sample> buildSample() {
  auto S = std::make_unique<Sample>();
  MetadataBuilder &MB = S->MB;
  MB.setModuleName("sample.dll");
  AssemblyIdentity Id;
  Id.Name = "sample";
  Id.Version = {1, 2, 3, 4};
  Id.Flags = AssemblyFlags::PublicKey;
  Id.PublicKey.assign(std::begin(ECMAKey), std::end(ECMAKey));
  MB.setAssembly(Id);
  AssemblyRefInfo Core;
  Core.Name = "mscorlib";
  Core.Version = {4, 0, 0, 0};
  Core.PublicKeyOrToken = {0xB7, 0x7A, 0x5C, 0x56, 0x19, 0x34, 0xE0, 0x89};
  EntityHandle CoreRef = MB.addAssemblyRef(Core);
  S->Obj = MB.addTypeRef(CoreRef, "System", "Object");
  S->ValueType = MB.addTypeRef(CoreRef, "System", "ValueType");
  EntityHandle Attr = MB.addTypeRef(CoreRef, "System", "Attribute");
  AssemblyRefInfo Link;
  Link.Name = "msil.link";
  EntityHandle LinkType =
      MB.addTypeRef(MB.addAssemblyRef(Link), "", "<msil>LinkInfo");
  const uint32_t ByteArrayCtorSig = blob(MB, {0x20, 0x01, 0x01, 0x1D, 0x05});
  S->LinkCtor = MB.addMemberRef(LinkType, ".ctor", ByteArrayCtorSig);
  EntityHandle ObjCtor =
      MB.addMemberRef(S->Obj, ".ctor", blob(MB, {0x20, 0x00, 0x01}));
  S->Kernel = MB.addModuleRef("ucrtbase");

  const uint32_t I4 = blob(MB, {0x06, 0x08});
  const uint32_t StaticVoid = blob(MB, {0x00, 0x00, 0x01});
  const uint32_t InstanceVoid = blob(MB, {0x20, 0x00, 0x01});
  const uint32_t InstanceI4 = blob(MB, {0x20, 0x00, 0x08});
  EntityHandle M = MB.getModuleType();

  S->Outer = MB.addTypeDef(TypeAttributes::Public, "NS", "Outer", S->Obj);
  S->Inner = MB.addTypeDef(TypeAttributes::NestedPublic, "", "Inner", S->Obj,
                           S->Outer);
  S->Layout =
      MB.addTypeDef(TypeAttributes::Public | TypeAttributes::ExplicitLayout |
                        TypeAttributes::Sealed,
                    "NS", "Layout", S->ValueType);
  S->Empty = MB.addTypeDef(TypeAttributes::Public, "NS", "Empty", S->Obj);
  S->MyAttr = MB.addTypeDef(TypeAttributes::Public, "NS", "MyAttr", Attr);

  S->GData = MB.addField(M,
                         FieldAttributes::Assembly | FieldAttributes::Static |
                             FieldAttributes::HasFieldRVA,
                         "gdata", I4);
  S->F1 = MB.addField(S->Outer, FieldAttributes::Public, "f1", I4);
  S->F2 = MB.addField(S->Outer, FieldAttributes::Private, "f2", I4);
  S->LayoutX = MB.addField(S->Layout, FieldAttributes::Public, "x", I4);

  MethodAttributes PubStatic =
      MethodAttributes::Public | MethodAttributes::Static;
  MethodAttributes PubInst = MethodAttributes::Public |
                             MethodAttributes::HideBySig |
                             MethodAttributes::SpecialName;
  S->Main =
      MB.addMethod(M, PubStatic, MethodImplAttributes::IL, "main", StaticVoid);
  S->Puts = MB.addMethod(M, PubStatic | MethodAttributes::PinvokeImpl,
                         MethodImplAttributes::PreserveSig, "puts",
                         blob(MB, {0x00, 0x01, 0x08, 0x18}));
  S->PutsParam = MB.addParam(S->Puts, 1, ParamAttributes::None, "str");
  S->OuterCtor =
      MB.addMethod(S->Outer, PubInst | MethodAttributes::RTSpecialName,
                   MethodImplAttributes::IL, ".ctor", InstanceVoid);
  S->GetP = MB.addMethod(S->Outer, PubInst, MethodImplAttributes::IL, "get_P",
                         InstanceI4);
  S->InnerM = MB.addMethod(S->Inner, PubStatic, MethodImplAttributes::IL, "m",
                           StaticVoid);
  S->MyAttrCtor =
      MB.addMethod(S->MyAttr, PubInst | MethodAttributes::RTSpecialName,
                   MethodImplAttributes::IL, ".ctor", ByteArrayCtorSig);

  S->P = MB.addProperty(S->Outer, PropertyAttributes::None, "P",
                        blob(MB, {0x28, 0x00, 0x08}));
  MB.addMethodSemantics(S->P, MethodSemanticsAttributes::Getter, S->GetP);
  MB.addImplMap(S->Puts, PInvokeAttributes::CallConvCdecl, "puts", S->Kernel);
  MB.addClassLayout(S->Layout, 8, 24);
  MB.addFieldLayout(S->LayoutX, 16);
  MB.addFieldRVA(S->GData, 0);

  // Custom attributes, added in an order that sorting changes.
  MB.addCustomAttribute(S->Outer, S->MyAttrCtor,
                        byteArrayBlob(MB, S->TypePayload1));
  MB.addCustomAttribute(rowHandle(TableId::Assembly, 1), S->MyAttrCtor,
                        byteArrayBlob(MB, S->AsmPayload));
  MB.addCustomAttribute(rowHandle(TableId::Module, 1), S->LinkCtor,
                        byteArrayBlob(MB, S->LinkPayload));
  MB.addCustomAttribute(S->Outer, ObjCtor, blob(MB, {0x01, 0x00, 0x00, 0x00}));
  MB.addCustomAttribute(S->Outer, S->MyAttrCtor,
                        byteArrayBlob(MB, S->TypePayload2));

  S->UserString = MB.addUserString("h\xC3\xA9llo"); // "héllo"
  EXPECT_THAT_ERROR(MB.finalize(S->Bodies), Succeeded());

  // Bodies: main { ldstr "héllo"; pop; ldc.i4.0; ret } (the ret pops
  // nothing here, it is only bytes), and empty bodies for the others.
  ByteVec Code = S->MainCode;
  support::endian::write32le(Code.data() + 1, S->UserString);
  MethodBodyInfo B;
  B.Code = Code;
  MB.setMethodBodyOffset(S->Main, S->Bodies.addBody(B));
  const uint8_t Ret[] = {0x2A};
  MethodBodyInfo R;
  R.Code = Ret;
  for (EntityHandle H : {S->OuterCtor, S->GetP, S->InnerM, S->MyAttrCtor})
    MB.setMethodBodyOffset(H, S->Bodies.addBody(R));
  return S;
}

ByteVec serializeRoot(const MetadataBuilder &MB) {
  ByteVec MD(MB.getSerializedSize());
  MB.serialize(MD, 0x2048, 0x3000);
  return MD;
}

ByteVec writeSample(const Sample &S) {
  Expected<PEOptions> O = PEOptions::get(Triple("x86_64-pc-windows-msvc-msil"),
                                         PEImageKind::Library);
  EXPECT_THAT_EXPECTED(O, Succeeded());
  if (!O)
    return {};
  PEContent C;
  C.Metadata = &S.MB;
  C.ILStream = S.Bodies.getBytes();
  C.MappedFieldData = S.Mapped;
  SmallVector<uint8_t, 0> Out;
  EXPECT_THAT_ERROR(writePEImage(*O, C, Out), Succeeded());
  return ByteVec(Out.begin(), Out.end());
}

std::unique_ptr<MetadataReader> readRoot(ArrayRef<uint8_t> Root) {
  Expected<std::unique_ptr<MetadataReader>> R =
      MetadataReader::createFromMetadata(Root);
  EXPECT_THAT_EXPECTED(R, Succeeded());
  return R ? std::move(*R) : nullptr;
}

//===----------------------------------------------------------------------===//
// Valid metadata
//===----------------------------------------------------------------------===//

TEST(CLIMetadataReaderTest, RootStreamsAndHeaps) {
  std::unique_ptr<Sample> S = buildSample();
  ByteVec MD = serializeRoot(S->MB);
  std::unique_ptr<MetadataReader> R = readRoot(MD);
  ASSERT_TRUE(R);
  EXPECT_EQ(R->getImage(), nullptr);
  EXPECT_EQ(R->getVersionString(), "v4.0.30319");
  EXPECT_TRUE(R->isAssembly());
  RootParts P = splitRoot(MD);
  for (StringRef Heap : {"#Strings", "#US", "#GUID", "#Blob"}) {
    ByteVec *Bytes = P.stream(Heap);
    ASSERT_TRUE(Bytes) << Heap.str();
    EXPECT_EQ(R->getHeapSize(Heap), Bytes->size()) << Heap.str();
  }
  TablesHeader H = parseTables(*P.stream("#~"));
  EXPECT_EQ(R->getHeapSizeFlags(), H.HeapSizes);
  for (unsigned T = 0;
       T <= static_cast<unsigned>(TableId::GenericParamConstraint); ++T) {
    TableId Id = static_cast<TableId>(T);
    EXPECT_EQ(R->getRowCount(Id), H.Rows[T]) << "table " << T;
    EXPECT_EQ(R->getRowCount(Id), S->MB.getRowCount(Id)) << "table " << T;
    EXPECT_EQ(R->getSizes().getRowCount(Id), H.Rows[T]) << "table " << T;
  }
  Expected<ModuleRow> Mod = R->getModule();
  ASSERT_THAT_EXPECTED(Mod, Succeeded());
  EXPECT_EQ(Mod->Name, "sample.dll");
  EXPECT_EQ(Mod->Generation, 0);
  EXPECT_EQ(Mod->Mvid, 1u);
  Expected<std::array<uint8_t, 16>> Mvid = R->getGuid(1);
  ASSERT_THAT_EXPECTED(Mvid, Succeeded());
  EXPECT_EQ(*Mvid,
            (std::array<uint8_t, 16>{})); // stamped only by the PE writer
  // Heap offset 0 is the empty string and the empty blob.
  EXPECT_THAT_EXPECTED(R->getString(0), HasValue(""));
  Expected<ArrayRef<uint8_t>> Empty = R->getBlob(0);
  ASSERT_THAT_EXPECTED(Empty, Succeeded());
  EXPECT_TRUE(Empty->empty());
}

TEST(CLIMetadataReaderTest, UserStrings) {
  std::unique_ptr<Sample> S = buildSample();
  ByteVec MD = serializeRoot(S->MB);
  std::unique_ptr<MetadataReader> R = readRoot(MD);
  ASSERT_TRUE(R);
  uint32_t Off = getTokenRow(S->UserString);
  EXPECT_THAT_EXPECTED(R->getUserString(Off), HasValue("h\xC3\xA9llo"));
  Expected<ArrayRef<uint8_t>> U16 = R->getUserStringUTF16(Off);
  ASSERT_THAT_EXPECTED(U16, Succeeded());
  const uint8_t UTF16[] = {'h', 0, 0xE9, 0, 'l', 0, 'l', 0, 'o', 0};
  EXPECT_EQ(*U16, ArrayRef<uint8_t>(UTF16));
  expectMDError(R->getUserString(R->getHeapSize("#US")));
  expectMDError(R->getUserString(0x00FFFFFF));
  expectMDError(R->getUserStringUTF16(R->getHeapSize("#US") + 3));
}

// The lookups of 5.7.8 on the PE image.
TEST(CLIMetadataReaderTest, Lookups) {
  std::unique_ptr<Sample> S = buildSample();
  ByteVec F = writeSample(*S);
  Expected<std::unique_ptr<MetadataReader>> RE = MetadataReader::create(F);
  ASSERT_THAT_EXPECTED(RE, Succeeded());
  const MetadataReader &R = **RE;
  ASSERT_NE(R.getImage(), nullptr);
  auto Tok = [&](EntityHandle H) { return S->tok(H); };
  const uint32_t ModuleType = makeToken(TableId::TypeDef, 1);

  // Owners, the empty runs of NS.Empty included.
  struct {
    EntityHandle Member, Owner;
  } Owners[] = {{S->GData, S->MB.getModuleType()},
                {S->F1, S->Outer},
                {S->F2, S->Outer},
                {S->LayoutX, S->Layout},
                {S->Main, S->MB.getModuleType()},
                {S->Puts, S->MB.getModuleType()},
                {S->OuterCtor, S->Outer},
                {S->GetP, S->Outer},
                {S->InnerM, S->Inner},
                {S->MyAttrCtor, S->MyAttr}};
  for (const auto &O : Owners)
    EXPECT_THAT_EXPECTED(R.getOwnerTypeDef(Tok(O.Member)),
                         HasValue(Tok(O.Owner)))
        << "member 0x" << utohexstr(Tok(O.Member));
  Expected<TypeDefRow> EmptyRow = R.getTypeDef(rowOf(Tok(S->Empty)));
  ASSERT_THAT_EXPECTED(EmptyRow, Succeeded());
  EXPECT_EQ(EmptyRow->FieldBegin, EmptyRow->FieldEnd);
  EXPECT_EQ(EmptyRow->MethodBegin, EmptyRow->MethodEnd);

  EXPECT_THAT_EXPECTED(R.getEnclosingType(Tok(S->Inner)),
                       HasValue(Tok(S->Outer)));
  EXPECT_THAT_EXPECTED(R.getEnclosingType(Tok(S->Outer)), HasValue(0u));
  EXPECT_THAT_EXPECTED(R.getEnclosingType(ModuleType), HasValue(0u));

  EXPECT_THAT_EXPECTED(R.getTypeFullName(Tok(S->Outer)), HasValue("NS.Outer"));
  EXPECT_THAT_EXPECTED(R.getTypeFullName(Tok(S->Inner)),
                       HasValue("NS.Outer/Inner"));
  EXPECT_THAT_EXPECTED(R.getTypeFullName(ModuleType), HasValue("<Module>"));
  EXPECT_THAT_EXPECTED(R.getTypeFullName(Tok(S->Obj)),
                       HasValue("System.Object"));

  // Custom attributes per parent, by content.
  Expected<SmallVector<uint32_t, 4>> OnOuter =
      R.getCustomAttributes(Tok(S->Outer));
  ASSERT_THAT_EXPECTED(OnOuter, Succeeded());
  EXPECT_EQ(OnOuter->size(), 3u);
  for (uint32_t Row : *OnOuter) {
    Expected<CustomAttributeRow> CA = R.getCustomAttribute(rowOf(Row));
    ASSERT_THAT_EXPECTED(CA, Succeeded());
    EXPECT_EQ(CA->Parent, Tok(S->Outer));
  }
  Expected<SmallVector<uint32_t, 4>> OnInner =
      R.getCustomAttributes(Tok(S->Inner));
  ASSERT_THAT_EXPECTED(OnInner, Succeeded());
  EXPECT_TRUE(OnInner->empty());

  // Byte-array attributes: the constructor's declaring type is a TypeDef
  // (two on NS.Outer, in table order, and one on the Assembly row) or a
  // TypeRef (the Module row's '<msil>LinkInfo').
  Expected<SmallVector<ArrayRef<uint8_t>, 1>> Payloads =
      R.getByteArrayAttributes(Tok(S->Outer), "NS", "MyAttr");
  ASSERT_THAT_EXPECTED(Payloads, Succeeded());
  ASSERT_EQ(Payloads->size(), 2u);
  EXPECT_EQ((*Payloads)[0], ArrayRef<uint8_t>(S->TypePayload1));
  EXPECT_EQ((*Payloads)[1], ArrayRef<uint8_t>(S->TypePayload2));
  Payloads =
      R.getByteArrayAttributes(makeToken(TableId::Assembly, 1), "NS", "MyAttr");
  ASSERT_THAT_EXPECTED(Payloads, Succeeded());
  ASSERT_EQ(Payloads->size(), 1u);
  EXPECT_EQ((*Payloads)[0], ArrayRef<uint8_t>(S->AsmPayload));
  Payloads = R.getByteArrayAttributes(makeToken(TableId::Module, 1), "",
                                      "<msil>LinkInfo");
  ASSERT_THAT_EXPECTED(Payloads, Succeeded());
  ASSERT_EQ(Payloads->size(), 1u);
  EXPECT_EQ((*Payloads)[0], ArrayRef<uint8_t>(S->LinkPayload));
  const std::pair<StringRef, StringRef> NoMatch[] = {
      {"", "MyAttr"}, {"NS", "Outer"}, {"msil.link", "<msil>LinkInfo"}};
  for (auto [NS, Name] : NoMatch) {
    Payloads = R.getByteArrayAttributes(Tok(S->Outer), NS, Name);
    ASSERT_THAT_EXPECTED(Payloads, Succeeded());
    EXPECT_TRUE(Payloads->empty()) << NS.str() << "." << Name.str();
  }

  // ImplMap, FieldRVA, ClassLayout.
  Expected<std::optional<ImplMapRow>> IM = R.findImplMap(Tok(S->Puts));
  ASSERT_THAT_EXPECTED(IM, Succeeded());
  ASSERT_TRUE(IM->has_value());
  EXPECT_EQ((*IM)->MemberForwarded, Tok(S->Puts));
  EXPECT_EQ((*IM)->ImportName, "puts");
  EXPECT_EQ((*IM)->ImportScope, Tok(S->Kernel));
  EXPECT_EQ((*IM)->Flags, PInvokeAttributes::CallConvCdecl);
  IM = R.findImplMap(Tok(S->Main));
  ASSERT_THAT_EXPECTED(IM, Succeeded());
  EXPECT_FALSE(IM->has_value());

  Expected<std::optional<FieldRVARow>> FR = R.findFieldRVA(Tok(S->GData));
  ASSERT_THAT_EXPECTED(FR, Succeeded());
  ASSERT_TRUE(FR->has_value());
  EXPECT_EQ((*FR)->Field, Tok(S->GData));
  EXPECT_NE((*FR)->RVA, 0u);
  FR = R.findFieldRVA(Tok(S->F1));
  ASSERT_THAT_EXPECTED(FR, Succeeded());
  EXPECT_FALSE(FR->has_value());
  Expected<ArrayRef<uint8_t>> Data = R.getFieldData(Tok(S->GData), 4);
  ASSERT_THAT_EXPECTED(Data, Succeeded());
  EXPECT_EQ(*Data, ArrayRef<uint8_t>(S->Mapped));

  Expected<std::optional<ClassLayoutRow>> CL =
      R.findClassLayout(Tok(S->Layout));
  ASSERT_THAT_EXPECTED(CL, Succeeded());
  ASSERT_TRUE(CL->has_value());
  EXPECT_EQ((*CL)->PackingSize, 8);
  EXPECT_EQ((*CL)->ClassSize, 24u);
  EXPECT_EQ((*CL)->Parent, Tok(S->Layout));
  CL = R.findClassLayout(Tok(S->Outer));
  ASSERT_THAT_EXPECTED(CL, Succeeded());
  EXPECT_FALSE(CL->has_value());

  // Properties.
  Expected<std::pair<uint32_t, uint32_t>> PR =
      R.getPropertyRange(Tok(S->Outer));
  ASSERT_THAT_EXPECTED(PR, Succeeded());
  EXPECT_EQ(PR->first, rowOf(Tok(S->P)));
  EXPECT_EQ(PR->second, rowOf(Tok(S->P)) + 1);
  PR = R.getPropertyRange(Tok(S->Inner));
  ASSERT_THAT_EXPECTED(PR, Succeeded());
  EXPECT_EQ(PR->first, PR->second);

  // Bodies: an IL method's, and an Error for a P/Invoke stub (RVA 0).
  Expected<MethodBodyView> Body = R.getMethodBody(Tok(S->Main));
  ASSERT_THAT_EXPECTED(Body, Succeeded());
  ASSERT_EQ(Body->Code.size(), S->MainCode.size());
  EXPECT_EQ(support::endian::read32le(Body->Code.data() + 1), S->UserString);
  EXPECT_EQ(Body->Code[0], 0x72);
  EXPECT_THAT_EXPECTED(R.getMethodBody(Tok(S->Puts)), Failed());
}

// Lookups index the tables themselves: the Sorted bits and the order of a
// sorted table are not trusted.
TEST(CLIMetadataReaderTest, LookupsIgnoreSortedBits) {
  std::unique_ptr<Sample> S = buildSample();
  RootParts P = splitRoot(serializeRoot(S->MB));
  ByteVec &T = *P.stream("#~");
  // Clear Sorted.
  std::fill_n(T.begin() + 16, 8, 0);
  // Reverse the CustomAttribute table's rows.
  TablesHeader H = parseTables(T);
  MetadataSizes Sizes(H.Rows, H.HeapSizes);
  uint32_t N = H.Rows[static_cast<uint8_t>(TableId::CustomAttribute)];
  ASSERT_EQ(N, 5u);
  unsigned RowSize = Sizes.getRowSize(TableId::CustomAttribute);
  uint64_t Base = H.DataOffset + Sizes.getTableOffset(TableId::CustomAttribute);
  std::vector<ByteVec> Rows;
  for (uint32_t I = 0; I < N; ++I)
    Rows.emplace_back(T.begin() + Base + I * RowSize,
                      T.begin() + Base + (I + 1) * RowSize);
  for (uint32_t I = 0; I < N; ++I)
    std::copy(Rows[N - 1 - I].begin(), Rows[N - 1 - I].end(),
              T.begin() + Base + I * RowSize);
  ByteVec MD = joinRoot(P);
  std::unique_ptr<MetadataReader> R = readRoot(MD);
  ASSERT_TRUE(R);
  const std::pair<uint32_t, size_t> Cases[] = {
      {S->tok(S->Outer), 3},
      {makeToken(TableId::Assembly, 1), 1},
      {makeToken(TableId::Module, 1), 1},
      {S->tok(S->Inner), 0}};
  for (auto [Parent, Count] : Cases) {
    SCOPED_TRACE("parent 0x" + utohexstr(Parent));
    Expected<SmallVector<uint32_t, 4>> CAs = R->getCustomAttributes(Parent);
    ASSERT_THAT_EXPECTED(CAs, Succeeded());
    EXPECT_EQ(CAs->size(), Count);
    for (uint32_t Row : *CAs) {
      Expected<CustomAttributeRow> CA = R->getCustomAttribute(rowOf(Row));
      ASSERT_THAT_EXPECTED(CA, Succeeded());
      EXPECT_EQ(CA->Parent, Parent);
    }
  }
  Expected<SmallVector<ArrayRef<uint8_t>, 1>> Link = R->getByteArrayAttributes(
      makeToken(TableId::Module, 1), "", "<msil>LinkInfo");
  ASSERT_THAT_EXPECTED(Link, Succeeded());
  ASSERT_EQ(Link->size(), 1u);
  EXPECT_EQ((*Link)[0], ArrayRef<uint8_t>(S->LinkPayload));
}

// A root rebuilt from its own parts reads the same; unknown streams
// (#Pdb, #JTD) are ignored.
TEST(CLIMetadataReaderTest, UnknownStreamsAreIgnored) {
  std::unique_ptr<Sample> S = buildSample();
  RootParts P = splitRoot(serializeRoot(S->MB));
  P.Streams.insert(P.Streams.begin(), {"#Pdb", ByteVec(32, 0xEE)});
  P.Streams.push_back({"#JTD", ByteVec()});
  std::unique_ptr<MetadataReader> R = readRoot(joinRoot(P));
  ASSERT_TRUE(R);
  Expected<TypeDefRow> Outer = R->getTypeDef(rowOf(S->tok(S->Outer)));
  ASSERT_THAT_EXPECTED(Outer, Succeeded());
  EXPECT_EQ(Outer->Name, "Outer");
  EXPECT_EQ(Outer->Namespace, "NS");
  EXPECT_EQ(R->getRowCount(TableId::TypeDef), 6u);
}

// HEAP_EXTRA_DATA: one u32 follows the row counts.
TEST(CLIMetadataReaderTest, ExtraDataField) {
  std::unique_ptr<Sample> S = buildSample();
  RootParts P = splitRoot(serializeRoot(S->MB));
  ByteVec &T = *P.stream("#~");
  TablesHeader H = parseTables(T);
  T[6] |= HEAP_EXTRA_DATA;
  const uint8_t Extra[] = {0xDE, 0xC0, 0xAD, 0x0B};
  T.insert(T.begin() + H.DataOffset, std::begin(Extra), std::end(Extra));
  std::unique_ptr<MetadataReader> R = readRoot(joinRoot(P));
  ASSERT_TRUE(R);
  Expected<TypeDefRow> Inner = R->getTypeDef(rowOf(S->tok(S->Inner)));
  ASSERT_THAT_EXPECTED(Inner, Succeeded());
  EXPECT_EQ(Inner->Name, "Inner");
  Expected<MemberRefRow> MR = R->getMemberRef(rowOf(S->tok(S->LinkCtor)));
  ASSERT_THAT_EXPECTED(MR, Succeeded());
  EXPECT_EQ(MR->Name, ".ctor");
}

//===----------------------------------------------------------------------===//
// Malformed metadata (5.7.8 checks at create, then heap and row bounds)
//===----------------------------------------------------------------------===//

void expectCreateError(ArrayRef<uint8_t> Root) {
  expectMDError(MetadataReader::createFromMetadata(Root));
}

TEST(CLIMetadataReaderTest, BadRootSignature) {
  std::unique_ptr<Sample> S = buildSample();
  ByteVec MD = serializeRoot(S->MB);
  MD[0] = 'X';
  expectCreateError(MD);
}

TEST(CLIMetadataReaderTest, BadVersionLength) {
  std::unique_ptr<Sample> S = buildSample();
  RootParts P = splitRoot(serializeRoot(S->MB));
  {
    SCOPED_TRACE("longer than 255");
    RootParts Q = P;
    Q.Version = std::string(259, 'v');
    expectCreateError(joinRoot(Q));
  }
  {
    SCOPED_TRACE("not padded to 4");
    RootParts Q = P;
    Q.VersionLength = 10;
    expectCreateError(joinRoot(Q));
  }
  {
    SCOPED_TRACE("past the end");
    RootParts Q = P;
    Q.VersionLength = 0x7FFFFFFC;
    expectCreateError(joinRoot(Q));
  }
}

TEST(CLIMetadataReaderTest, TruncatedRoot) {
  std::unique_ptr<Sample> S = buildSample();
  ByteVec MD = serializeRoot(S->MB);
  // Inside the fixed header, the version, the stream headers, and the first
  // stream.
  for (size_t N : {size_t(0), size_t(3), size_t(15), size_t(20), size_t(30),
                   size_t(40), size_t(60)}) {
    SCOPED_TRACE("size " + std::to_string(N));
    expectCreateError(ArrayRef<uint8_t>(MD).take_front(N));
  }
}

TEST(CLIMetadataReaderTest, StreamHeadersOutOfBounds) {
  std::unique_ptr<Sample> S = buildSample();
  ByteVec MD = serializeRoot(S->MB);
  const uint64_t Headers = 16 + 12 + 4; // "v4.0.30319" pads to 12 bytes
  {
    SCOPED_TRACE("stream count");
    ByteVec B = MD;
    support::endian::write16le(B.data() + Headers - 2, 0x7FFF);
    expectCreateError(B);
  }
  {
    SCOPED_TRACE("stream past the end of the root");
    ByteVec B = MD;
    support::endian::write32le(B.data() + Headers + 4, B.size()); // #~ size
    expectCreateError(B);
  }
  {
    SCOPED_TRACE("stream offset past the end");
    ByteVec B = MD;
    support::endian::write32le(B.data() + Headers, 0xFFFFFFF0);
    expectCreateError(B);
  }
  {
    SCOPED_TRACE("name without a NUL");
    RootParts P = splitRoot(MD);
    ByteVec B = joinRoot(P);
    // Cut the root inside the last stream header's name.
    uint64_t End = Headers;
    for (size_t I = 0; I < P.Streams.size(); ++I)
      End += 8 + alignTo(P.Streams[I].first.size() + 1, 4);
    B.resize(End - 3);
    expectCreateError(B);
  }
}

TEST(CLIMetadataReaderTest, MissingTablesStream) {
  std::unique_ptr<Sample> S = buildSample();
  RootParts P = splitRoot(serializeRoot(S->MB));
  P.Streams.front().first = "#X";
  ASSERT_TRUE(P.stream("#X"));
  expectCreateError(joinRoot(P));
}

TEST(CLIMetadataReaderTest, UncompressedTablesStream) {
  std::unique_ptr<Sample> S = buildSample();
  RootParts P = splitRoot(serializeRoot(S->MB));
  ASSERT_TRUE(P.stream("#~"));
  for (auto &Str : P.Streams)
    if (Str.first == "#~")
      Str.first = "#-";
  Expected<std::unique_ptr<MetadataReader>> R =
      MetadataReader::createFromMetadata(joinRoot(P));
  ASSERT_FALSE(bool(R));
  std::string Msg = toString(R.takeError());
  EXPECT_THAT(Msg, StartsWith(MDPrefix));
  EXPECT_THAT(Msg, HasSubstr("uncompressed metadata (#-) is not supported"));
}

TEST(CLIMetadataReaderTest, BadRowCounts) {
  std::unique_ptr<Sample> S = buildSample();
  RootParts P = splitRoot(serializeRoot(S->MB));
  ByteVec T = *P.stream("#~");
  // The TypeDef count is the third one (Module, TypeRef, TypeDef).
  ASSERT_EQ(support::endian::read32le(T.data() + 24 + 8), 6u);
  {
    SCOPED_TRACE("more than MaxTableRows");
    *P.stream("#~") = T;
    support::endian::write32le(P.stream("#~")->data() + 24 + 8, 0x01000000);
    expectCreateError(joinRoot(P));
  }
  {
    SCOPED_TRACE("rows past the end of #~");
    *P.stream("#~") = T;
    support::endian::write32le(P.stream("#~")->data() + 24 + 8, 6 + 4000);
    expectCreateError(joinRoot(P));
  }
  {
    SCOPED_TRACE("#~ cut short");
    *P.stream("#~") = T;
    P.stream("#~")->resize(T.size() - 8);
    expectCreateError(joinRoot(P));
  }
  {
    SCOPED_TRACE("#~ header cut short");
    *P.stream("#~") = T;
    P.stream("#~")->resize(20);
    expectCreateError(joinRoot(P));
  }
}

// A Valid bit for an id without a schema (0x2D-0x3F).
TEST(CLIMetadataReaderTest, ValidBitWithoutSchema) {
  std::unique_ptr<Sample> S = buildSample();
  for (unsigned Bit : {0x2Du, 0x3Fu}) {
    SCOPED_TRACE("bit " + std::to_string(Bit));
    RootParts P = splitRoot(serializeRoot(S->MB));
    ByteVec &T = *P.stream("#~");
    TablesHeader H = parseTables(T);
    support::endian::write64le(T.data() + 8, H.Valid | (uint64_t(1) << Bit));
    const uint8_t One[] = {1, 0, 0, 0};
    T.insert(T.begin() + H.DataOffset, std::begin(One), std::end(One));
    expectCreateError(joinRoot(P));
  }
}

TEST(CLIMetadataReaderTest, HeapAndRowIndexesOutOfRange) {
  std::unique_ptr<Sample> S = buildSample();
  std::unique_ptr<MetadataReader> R = readRoot(serializeRoot(S->MB));
  ASSERT_TRUE(R);
  uint32_t Strings = R->getHeapSize("#Strings");
  uint32_t Blobs = R->getHeapSize("#Blob");
  uint32_t Guids = R->getHeapSize("#GUID");
  expectMDError(R->getString(Strings));
  expectMDError(R->getString(0xFFFFFFFF));
  expectMDError(R->getBlob(Blobs));
  expectMDError(R->getBlob(0x7FFFFFFF));
  expectMDError(R->getGuid(0));
  expectMDError(R->getGuid(Guids / 16 + 1));
  uint32_t TypeDefs = R->getRowCount(TableId::TypeDef);
  expectMDError(R->getTypeDef(0));
  expectMDError(R->getTypeDef(TypeDefs + 1));
  expectMDError(R->getTypeRef(R->getRowCount(TableId::TypeRef) + 1));
  expectMDError(R->getField(R->getRowCount(TableId::Field) + 1));
  expectMDError(R->getMethodDef(R->getRowCount(TableId::MethodDef) + 1));
  expectMDError(R->getCustomAttribute(0));
  expectMDError(R->getImplMap(2));
  expectMDError(R->getNestedClass(2));
  // Tables this module does not have.
  expectMDError(R->getConstant(1));
  expectMDError(R->getGenericParam(1));
  expectMDError(R->getExportedType(1));
  expectMDError(R->getColumnValue(TableId::TypeDef, TypeDefs + 1, 0));
  expectMDError(R->getColumnValue(TableId::Event, 1, 0));
  // Lookups on tokens of rows that do not exist.
  expectMDError(R->getOwnerTypeDef(makeToken(TableId::MethodDef, 99)));
  expectMDError(R->getTypeFullName(makeToken(TableId::TypeDef, 99)));
}

// Heap entries that run past the end of their heap.
TEST(CLIMetadataReaderTest, HeapEntriesPastTheEnd) {
  std::unique_ptr<Sample> S = buildSample();
  RootParts P = splitRoot(serializeRoot(S->MB));
  uint32_t StringsEnd = P.stream("#Strings")->size();
  uint32_t BlobEnd = P.stream("#Blob")->size();
  // A string without a NUL, a blob whose length runs past the heap, and a
  // blob with an invalid compressed-length lead byte.
  const uint8_t Str[] = {'a', 'b', 'c', 'd'};
  P.stream("#Strings")
      ->insert(P.stream("#Strings")->end(), std::begin(Str), std::end(Str));
  const uint8_t Blob[] = {0x0A, 0x01, 0x02, 0x03, 0xFF, 0x00, 0x00, 0x00};
  P.stream("#Blob")->insert(P.stream("#Blob")->end(), std::begin(Blob),
                            std::end(Blob));
  std::unique_ptr<MetadataReader> R = readRoot(joinRoot(P));
  ASSERT_TRUE(R);
  expectMDError(R->getString(StringsEnd));
  expectMDError(R->getString(StringsEnd + 2));
  expectMDError(R->getBlob(BlobEnd));
  expectMDError(R->getBlob(BlobEnd + 4));
}

// Cells whose values are out of range give Errors from the row getters.
TEST(CLIMetadataReaderTest, BadCells) {
  std::unique_ptr<Sample> S = buildSample();
  const uint32_t OuterRow = rowOf(S->tok(S->Outer));
  {
    SCOPED_TRACE("TypeDef name past #Strings");
    RootParts P = splitRoot(serializeRoot(S->MB));
    patchCell(*P.stream("#~"), TableId::TypeDef, OuterRow, 1,
              P.stream("#Strings")->size() + 4);
    std::unique_ptr<MetadataReader> R = readRoot(joinRoot(P));
    ASSERT_TRUE(R);
    expectMDError(R->getTypeDef(OuterRow));
  }
  {
    SCOPED_TRACE("TypeDef.Extends with the unused tag 3");
    RootParts P = splitRoot(serializeRoot(S->MB));
    patchCell(*P.stream("#~"), TableId::TypeDef, OuterRow, 3, (1 << 2) | 3);
    std::unique_ptr<MetadataReader> R = readRoot(joinRoot(P));
    ASSERT_TRUE(R);
    expectMDError(R->getTypeDef(OuterRow));
    expectMDError(R->getColumnValue(TableId::TypeDef, OuterRow, 3));
  }
  {
    SCOPED_TRACE("Field signature past #Blob");
    RootParts P = splitRoot(serializeRoot(S->MB));
    const uint32_t F1Row = rowOf(S->tok(S->F1));
    patchCell(*P.stream("#~"), TableId::Field, F1Row, 2,
              P.stream("#Blob")->size() + 1);
    std::unique_ptr<MetadataReader> R = readRoot(joinRoot(P));
    ASSERT_TRUE(R);
    Expected<FieldRow> F = R->getField(F1Row);
    // The getter may check the offset or leave it to getBlob.
    if (F)
      expectMDError(R->getBlob(F->Signature));
    else
      EXPECT_THAT(toString(F.takeError()), StartsWith(MDPrefix));
  }
}

// A PE whose CLI header names no metadata root.
TEST(CLIMetadataReaderTest, ImageWithoutMetadataRoot) {
  std::unique_ptr<Sample> S = buildSample();
  ByteVec F = writeSample(*S);
  // The CLI header's MetaData RVA (at .text + 8) now points at the IL stream.
  Expected<std::unique_ptr<MetadataReader>> Good = MetadataReader::create(F);
  ASSERT_THAT_EXPECTED(Good, Succeeded());
  const PEImage *Img = (*Good)->getImage();
  ASSERT_NE(Img, nullptr);
  ASSERT_FALSE(Img->sections().empty());
  uint64_t TextRaw = Img->sections()[0].PointerToRawData;
  ByteVec B = F;
  support::endian::write32le(B.data() + TextRaw + 8, 0x2048);
  expectMDError(MetadataReader::create(B));
  // Not a PE at all.
  const uint8_t Junk[] = {'B', 'S', 'J', 'B', 0, 0, 0, 0};
  Expected<std::unique_ptr<MetadataReader>> J = MetadataReader::create(Junk);
  ASSERT_FALSE(bool(J));
  EXPECT_THAT(toString(J.takeError()), StartsWith("invalid CLI "));
}

//===----------------------------------------------------------------------===//
// 4-byte heap and coded indexes (II.24.2.6)
//===----------------------------------------------------------------------===//

// 2^14 TypeRefs make TypeDefOrRef, ResolutionScope, MemberRefParent and
// HasCustomAttribute indexes 4 bytes; their names make #Strings larger than
// 64 KiB and a filler blob does the same for #Blob.
TEST(CLIMetadataReaderTest, WideIndexes) {
  constexpr uint32_t NumTypeRefs = 1 << 14;
  MetadataBuilder MB;
  MB.setModuleName("wide.dll");
  AssemblyIdentity Id;
  Id.Name = "wide";
  MB.setAssembly(Id);
  AssemblyRefInfo Core;
  Core.Name = "mscorlib";
  EntityHandle Scope = MB.addAssemblyRef(Core);
  std::vector<EntityHandle> Refs;
  char Name[16];
  for (uint32_t I = 0; I < NumTypeRefs; ++I) {
    std::snprintf(Name, sizeof(Name), "T%05u", I);
    Refs.push_back(MB.addTypeRef(Scope, "N", Name));
  }
  EntityHandle Last = Refs.back();
  EntityHandle Big = MB.addTypeDef(TypeAttributes::Public, "N", "Big", Last);
  EntityHandle Ctor =
      MB.addMemberRef(Last, ".ctor", blob(MB, {0x20, 0x00, 0x01}));
  ByteVec Filler(70000, 0x5A);
  MB.addBlob(Filler);
  const uint8_t Value[] = {0x01, 0x00, 0x00, 0x00, 0x33};
  uint32_t ValueBlob = MB.addBlob(Value);
  ASSERT_GT(ValueBlob, 0xFFFFu);
  MB.addCustomAttribute(Last, Ctor, ValueBlob);
  MethodBodyStream Bodies;
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  ASSERT_EQ(MB.getToken(Last), makeToken(TableId::TypeRef, NumTypeRefs));

  std::unique_ptr<MetadataReader> R = readRoot(serializeRoot(MB));
  ASSERT_TRUE(R);
  EXPECT_NE(R->getHeapSizeFlags() & HEAP_STRINGS_4, 0);
  EXPECT_NE(R->getHeapSizeFlags() & HEAP_BLOB_4, 0);
  EXPECT_EQ(R->getHeapSizeFlags() & HEAP_GUID_4, 0);
  EXPECT_GE(R->getHeapSize("#Strings"), 0x10000u);
  const MetadataSizes &Sz = R->getSizes();
  EXPECT_EQ(Sz.getCodedIndexSize(CodedIndexKind::TypeDefOrRef), 4u);
  EXPECT_EQ(Sz.getCodedIndexSize(CodedIndexKind::ResolutionScope), 4u);
  EXPECT_EQ(Sz.getCodedIndexSize(CodedIndexKind::MemberRefParent), 4u);
  EXPECT_EQ(Sz.getCodedIndexSize(CodedIndexKind::HasCustomAttribute), 4u);
  EXPECT_EQ(Sz.getCodedIndexSize(CodedIndexKind::CustomAttributeType), 2u);
  EXPECT_EQ(Sz.getRowSize(TableId::TypeRef), 12u);
  EXPECT_EQ(R->getRowCount(TableId::TypeRef), NumTypeRefs);

  const uint32_t ScopeTok = MB.getToken(Scope);
  for (uint32_t I = 0; I < NumTypeRefs; ++I) {
    Expected<TypeRefRow> T = R->getTypeRef(I + 1);
    ASSERT_THAT_EXPECTED(T, Succeeded());
    std::snprintf(Name, sizeof(Name), "T%05u", I);
    ASSERT_EQ(T->Name, Name);
    ASSERT_EQ(T->Namespace, "N");
    ASSERT_EQ(T->ResolutionScope, ScopeTok);
  }
  const uint32_t LastTok = makeToken(TableId::TypeRef, NumTypeRefs);
  Expected<TypeDefRow> D = R->getTypeDef(rowOf(MB.getToken(Big)));
  ASSERT_THAT_EXPECTED(D, Succeeded());
  EXPECT_EQ(D->Extends, LastTok);
  EXPECT_EQ(D->Name, "Big");
  EXPECT_THAT_EXPECTED(
      R->getColumnValue(TableId::TypeDef, rowOf(MB.getToken(Big)), 3),
      HasValue(LastTok));
  Expected<MemberRefRow> M = R->getMemberRef(1);
  ASSERT_THAT_EXPECTED(M, Succeeded());
  EXPECT_EQ(M->Parent, LastTok);
  Expected<CustomAttributeRow> CA = R->getCustomAttribute(1);
  ASSERT_THAT_EXPECTED(CA, Succeeded());
  EXPECT_EQ(CA->Parent, LastTok);
  EXPECT_EQ(CA->Constructor, MB.getToken(Ctor));
  EXPECT_EQ(CA->Value, ValueBlob);
  EXPECT_THAT_EXPECTED(R->getBlob(CA->Value),
                       HasValue(ArrayRef<uint8_t>(Value)));
  Expected<SmallVector<uint32_t, 4>> CAs = R->getCustomAttributes(LastTok);
  ASSERT_THAT_EXPECTED(CAs, Succeeded());
  EXPECT_EQ(CAs->size(), 1u);
  EXPECT_THAT_EXPECTED(R->getTypeFullName(LastTok), HasValue("N.T16383"));
}

//===----------------------------------------------------------------------===//
// Public-key tokens (5.7.8; bcl-binding 5.6)
//===----------------------------------------------------------------------===//

TEST(CLIMetadataReaderTest, PublicKeyTokens) {
  const std::array<uint8_t, 8> ECMAToken = {0xB7, 0x7A, 0x5C, 0x56,
                                            0x19, 0x34, 0xE0, 0x89};
  const std::array<uint8_t, 8> MSFTToken = {0xB0, 0x3F, 0x5F, 0x7F,
                                            0x11, 0xD5, 0x0A, 0x3A};
  EXPECT_EQ(computePublicKeyToken(ECMAKey), ECMAToken);
  EXPECT_EQ(computePublicKeyToken(SystemRuntimeKey), MSFTToken);

  // The Assembly row's key, read back from an image.
  std::unique_ptr<Sample> S = buildSample();
  ByteVec F = writeSample(*S);
  Expected<std::unique_ptr<MetadataReader>> R = MetadataReader::create(F);
  ASSERT_THAT_EXPECTED(R, Succeeded());
  Expected<AssemblyRow> A = (*R)->getAssembly();
  ASSERT_THAT_EXPECTED(A, Succeeded());
  EXPECT_EQ(A->Name, "sample");
  EXPECT_EQ(A->Version, (AssemblyVersion{1, 2, 3, 4}));
  EXPECT_EQ(A->HashAlg, AssemblyHashAlgorithm::SHA1);
  EXPECT_EQ(A->Flags, AssemblyFlags::PublicKey);
  Expected<ArrayRef<uint8_t>> Key = (*R)->getBlob(A->PublicKey);
  ASSERT_THAT_EXPECTED(Key, Succeeded());
  EXPECT_EQ(*Key, ArrayRef<uint8_t>(ECMAKey));
  EXPECT_EQ(computePublicKeyToken(*Key), ECMAToken);
  Expected<AssemblyRefRow> Core = (*R)->getAssemblyRef(1);
  ASSERT_THAT_EXPECTED(Core, Succeeded());
  EXPECT_EQ(Core->Name, "mscorlib");
  Expected<ArrayRef<uint8_t>> CoreKey = (*R)->getBlob(Core->PublicKeyOrToken);
  ASSERT_THAT_EXPECTED(CoreKey, Succeeded());
  EXPECT_EQ(*CoreKey, ArrayRef<uint8_t>(ECMAToken));
}

} // namespace
