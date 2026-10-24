//===- MetadataBuilderTest.cpp - ECMA-335 metadata writer -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/MetadataBuilder.h against ECMA-335 (6th edition) II.22 and
// II.24 and the writer rules of the MSIL implementation plan (5.7.6, 5.7.12,
// 5.8.3 R6). Every module is serialized and read back column by column with
// the test's own parser over llvm/CLI/TableSchema.h's MetadataSizes (the
// layout that M13 checked against System.Reflection.Metadata). The
// Phase-1-shaped module has the 14 tables that ilasm writes for the
// msil/test/M1/11-programs modules (recon/p2/metadata-writer.md section 2);
// its flags, signatures and row sizes are those of
// recon/p2/metadata-writer-files/hello-python.mdump.txt. Expected values come
// from ECMA-335, the recon logs and the rules of 5.7.6, never from the
// implementation under test.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/bit.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/Handles.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/CLI/TableSchema.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::cli;
using testing::HasSubstr;
using testing::Not;
using testing::StartsWith;

namespace {

using ByteVec = std::vector<uint8_t>;

constexpr char Prefix[] = "cannot write CLI metadata: ";
constexpr uint32_t ILRVA = 0x2050;
constexpr uint32_t DataRVA = 0x2800;

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

EntityHandle rowHandle(TableId T, uint32_t Row) {
  EntityHandle H;
  H.Value = makeToken(T, Row);
  return H;
}

uint32_t addBlob(MetadataBuilder &MB, std::initializer_list<uint8_t> Bytes) {
  return MB.addBlob(ArrayRef<uint8_t>(Bytes.begin(), Bytes.end()));
}

void appendToken(ByteVec &Code, uint32_t Token) {
  for (unsigned I = 0; I < 4; ++I)
    Code.push_back(static_cast<uint8_t>(Token >> (8 * I)));
}

// The coded index K of Token (II.24.2.6), through M13's encoder.
uint32_t coded(CodedIndexKind K, uint32_t Token) {
  std::optional<uint32_t> V = encodeCodedIndex(K, Token);
  EXPECT_TRUE(V.has_value()) << "token 0x" << utohexstr(Token);
  return V.value_or(0xFFFFFFFF);
}

// The message of an Error that must be a failure.
std::string failureText(Error E) {
  if (!E) {
    ADD_FAILURE() << "expected an error";
    return "";
  }
  return toString(std::move(E));
}

// mscorlib and [mscorlib]System.Object, deduplicated across calls.
EntityHandle addObjectTypeRef(MetadataBuilder &MB) {
  AssemblyRefInfo R;
  R.Name = "mscorlib";
  R.Version = {4, 0, 0, 0};
  R.PublicKeyOrToken = {0xB7, 0x7A, 0x5C, 0x56, 0x19, 0x34, 0xE0, 0x89};
  return MB.addTypeRef(MB.addAssemblyRef(R), "System", "Object");
}

// A static class; added last, it owns the 0xFFFF pad rows (5.7.6).
EntityHandle addHolder(MetadataBuilder &MB, StringRef Name = "Holder") {
  return MB.addTypeDef(TypeAttributes::Public | TypeAttributes::Abstract |
                           TypeAttributes::Sealed,
                       "", Name, addObjectTypeRef(MB));
}

// Serializes twice, into buffers filled with 0xCC and 0x00 first: the bytes
// depend only on the calls (5.7.12), so serialize() writes every byte.
ByteVec serializeMetadata(const MetadataBuilder &MB, uint32_t IL = ILRVA,
                          uint32_t Data = DataRVA) {
  size_t Size = MB.getSerializedSize();
  ByteVec A(Size, 0xCC), B(Size, 0x00);
  MB.serialize(A, IL, Data);
  MB.serialize(B, IL, Data);
  EXPECT_EQ(A, B) << "serialize() left bytes of its output unwritten";
  return A;
}

// Distinct strings that are not suffixes of each other ("s" + 6 digits + 'x'
// padding, at most 1007 bytes), so each adds exactly Length + 1 bytes to
// #Strings: they grow a heap of Base bytes to exactly Target bytes.
std::vector<std::string> makeFillStrings(uint32_t Base, uint32_t Target) {
  std::vector<std::string> Out;
  unsigned Serial = 0;
  auto Make = [&](size_t Length) {
    char Head[16];
    std::snprintf(Head, sizeof(Head), "s%06u", Serial++);
    std::string S(Head);
    S.resize(Length, 'x');
    Out.push_back(S);
  };
  uint32_t Fill = Target - Base;
  uint32_t N = Fill / 1000, R = Fill % 1000;
  if (R != 0 && R - 1 < 8) {
    --N;
    R += 1000;
  }
  for (uint32_t I = 0; I < N; ++I)
    Make(999);
  if (R != 0)
    Make(R - 1);
  return Out;
}

//===----------------------------------------------------------------------===//
// A reader for serialized metadata (II.24.2.1-II.24.2.6)
//===----------------------------------------------------------------------===//

class MetadataView {
public:
  struct StreamHeader {
    std::string Name;
    uint32_t Offset = 0, Size = 0;
  };

  explicit MetadataView(ArrayRef<uint8_t> Data)
      : Bytes(Data.begin(), Data.end()) {
    Ok = parse();
  }
  bool ok() const { return Ok; }

  // The metadata root.
  uint32_t Signature = 0, Reserved = 0, VersionLength = 0;
  uint16_t Major = 0, Minor = 0, RootFlags = 0;
  std::string Version;
  std::vector<StreamHeader> Streams;
  uint64_t StreamHeadersEnd = 0;
  // The #~ header.
  uint32_t TablesReserved = 0;
  uint8_t TablesMajor = 0, TablesMinor = 0, HeapSizes = 0, TablesReserved2 = 0;
  uint64_t ValidMask = 0, SortedMask = 0;
  std::array<uint32_t, NumTableIds> Rows{};
  std::optional<MetadataSizes> Sizes;
  uint64_t TableData = 0; // offset of the first table in the metadata

  const StreamHeader *stream(StringRef Name) const {
    for (const StreamHeader &S : Streams)
      if (S.Name == Name)
        return &S;
    return nullptr;
  }
  ArrayRef<uint8_t> streamBytes(StringRef Name) const {
    const StreamHeader *S = stream(Name);
    if (!S) {
      ADD_FAILURE() << "no stream " << Name.str();
      return {};
    }
    return ArrayRef<uint8_t>(Bytes).slice(S->Offset, S->Size);
  }
  uint32_t rows(TableId T) const { return Rows[static_cast<uint8_t>(T)]; }

  // The offset of a cell in the metadata, and its width (0 on failure).
  uint64_t cell(TableId T, uint32_t Row, StringRef Column,
                unsigned &Width) const {
    Width = 0;
    ArrayRef<ColumnInfo> Cols = getTableColumns(T);
    unsigned Col = 0;
    while (Col < Cols.size() && StringRef(Cols[Col].Name) != Column)
      ++Col;
    if (Col == Cols.size()) {
      ADD_FAILURE() << "no column " << Column.str() << " in "
                    << getTableName(T).str();
      return 0;
    }
    if (Row == 0 || Row > rows(T)) {
      ADD_FAILURE() << getTableName(T).str() << " has no row " << Row;
      return 0;
    }
    Width = Sizes->getColumnSize(T, Col);
    return TableData + Sizes->getTableOffset(T) +
           uint64_t(Row - 1) * Sizes->getRowSize(T) +
           Sizes->getColumnOffset(T, Col);
  }
  unsigned width(TableId T, StringRef Column) const {
    ArrayRef<ColumnInfo> Cols = getTableColumns(T);
    for (unsigned Col = 0; Col < Cols.size(); ++Col)
      if (StringRef(Cols[Col].Name) == Column)
        return Sizes->getColumnSize(T, Col);
    ADD_FAILURE() << "no column " << Column.str();
    return 0;
  }
  uint32_t get(TableId T, uint32_t Row, StringRef Column) const {
    unsigned W;
    uint64_t Off = cell(T, Row, Column, W);
    if (W == 2)
      return support::endian::read16le(&Bytes[Off]);
    if (W == 4)
      return support::endian::read32le(&Bytes[Off]);
    return 0;
  }
  std::string str(uint32_t Index) const {
    ArrayRef<uint8_t> Heap = streamBytes("#Strings");
    if (Index >= Heap.size()) {
      ADD_FAILURE() << "string index " << Index << " past #Strings";
      return "";
    }
    std::string S;
    size_t I = Index;
    for (; I < Heap.size() && Heap[I]; ++I)
      S += static_cast<char>(Heap[I]);
    EXPECT_LT(I, Heap.size()) << "unterminated string at " << Index;
    return S;
  }
  ByteVec blob(uint32_t Index) const {
    ArrayRef<uint8_t> Heap = streamBytes("#Blob");
    if (Index >= Heap.size()) {
      ADD_FAILURE() << "blob index " << Index << " past #Blob";
      return {};
    }
    ArrayRef<uint8_t> Rest = Heap.drop_front(Index);
    std::optional<uint32_t> Length = decodeCompressedUInt(Rest);
    if (!Length || *Length > Rest.size()) {
      ADD_FAILURE() << "bad blob at " << Index;
      return {};
    }
    return ByteVec(Rest.begin(), Rest.begin() + *Length);
  }
  ByteVec guid(uint32_t Index) const {
    ArrayRef<uint8_t> Heap = streamBytes("#GUID");
    if (Index == 0 || uint64_t(Index) * 16 > Heap.size()) {
      ADD_FAILURE() << "GUID index " << Index << " past #GUID";
      return {};
    }
    return ByteVec(Heap.begin() + (Index - 1) * 16, Heap.begin() + Index * 16);
  }
  std::string getStr(TableId T, uint32_t Row, StringRef Column) const {
    return str(get(T, Row, Column));
  }
  ByteVec getBlob(TableId T, uint32_t Row, StringRef Column) const {
    return blob(get(T, Row, Column));
  }
  // The Name column of rows [List(Owner), List(Owner + 1)) of table Member:
  // the run that row OwnerRow of table Owner owns through ListColumn.
  std::vector<std::string> run(TableId Owner, uint32_t OwnerRow,
                               StringRef ListColumn, TableId Member) const {
    uint32_t Begin = get(Owner, OwnerRow, ListColumn);
    uint32_t End = OwnerRow < rows(Owner) ? get(Owner, OwnerRow + 1, ListColumn)
                                          : rows(Member) + 1;
    std::vector<std::string> Names;
    for (uint32_t R = Begin; R < End && R <= rows(Member); ++R)
      Names.push_back(getStr(Member, R, "Name"));
    return Names;
  }

  ByteVec Bytes;

private:
  bool fail(const char *Why) {
    ADD_FAILURE() << "malformed metadata: " << Why;
    return false;
  }

  bool parse() {
    using namespace support::endian;
    if (Bytes.size() < 32)
      return fail("shorter than a metadata root");
    const uint8_t *P = Bytes.data();
    Signature = read32le(P);
    Major = read16le(P + 4);
    Minor = read16le(P + 6);
    Reserved = read32le(P + 8);
    VersionLength = read32le(P + 12);
    uint64_t Pos = 16;
    if (VersionLength > Bytes.size() - Pos - 4)
      return fail("version string past the end");
    for (uint32_t I = 0; I < VersionLength && P[Pos + I]; ++I)
      Version += static_cast<char>(P[Pos + I]);
    Pos += VersionLength;
    RootFlags = read16le(P + Pos);
    uint16_t Count = read16le(P + Pos + 2);
    Pos += 4;
    for (unsigned S = 0; S < Count; ++S) {
      if (Pos + 8 > Bytes.size())
        return fail("stream header past the end");
      StreamHeader H;
      H.Offset = read32le(P + Pos);
      H.Size = read32le(P + Pos + 4);
      Pos += 8;
      uint64_t Start = Pos;
      while (Pos < Bytes.size() && P[Pos])
        H.Name += static_cast<char>(P[Pos++]);
      if (Pos >= Bytes.size())
        return fail("unterminated stream name");
      Pos = Start + alignTo(H.Name.size() + 1, 4);
      if (uint64_t(H.Offset) + H.Size > Bytes.size())
        return fail("stream past the end");
      Streams.push_back(H);
    }
    StreamHeadersEnd = Pos;
    const StreamHeader *T = stream("#~");
    if (!T || T->Size < 24)
      return fail("no #~ stream");
    const uint8_t *Q = P + T->Offset;
    TablesReserved = read32le(Q);
    TablesMajor = Q[4];
    TablesMinor = Q[5];
    HeapSizes = Q[6];
    TablesReserved2 = Q[7];
    ValidMask = read64le(Q + 8);
    SortedMask = read64le(Q + 16);
    uint64_t TPos = 24;
    for (unsigned I = 0; I < NumTableIds; ++I) {
      if (!((ValidMask >> I) & 1))
        continue;
      if (I >= 0x2D)
        return fail("Valid names a table without a schema");
      if (TPos + 4 > T->Size)
        return fail("row counts past #~");
      Rows[I] = read32le(Q + TPos);
      TPos += 4;
    }
    if (HeapSizes & HEAP_EXTRA_DATA)
      TPos += 4;
    TableData = T->Offset + TPos;
    Sizes.emplace(ArrayRef<uint32_t>(Rows), HeapSizes);
    if (TPos + Sizes->getTablesSize() > T->Size)
      return fail("tables past #~");
    return true;
  }

  bool Ok = false;
};

// The serialization rules of 5.7.6 that hold for every module.
void checkLayout(const MetadataView &V, const MetadataBuilder &MB) {
  const ByteVec &Bytes = V.Bytes;
  // The root (II.24.2.1).
  EXPECT_EQ(V.Signature, MetadataSignature);
  EXPECT_EQ(V.Major, 1u);
  EXPECT_EQ(V.Minor, 1u);
  EXPECT_EQ(V.Reserved, 0u);
  EXPECT_EQ(V.VersionLength, 12u);
  EXPECT_EQ(V.Version, "v4.0.30319");
  EXPECT_EQ(Bytes[26], 0);
  EXPECT_EQ(Bytes[27], 0);
  EXPECT_EQ(V.RootFlags, 0u);
  // Five streams in ilasm's and Roslyn's order, every one written, each
  // padded to 4, back to back from the end of the headers to the end.
  const char *Names[] = {"#~", "#Strings", "#US", "#GUID", "#Blob"};
  ASSERT_EQ(V.Streams.size(), 5u);
  ASSERT_EQ(V.StreamHeadersEnd, 108u);
  ByteVec Headers;
  uint64_t Pos = V.StreamHeadersEnd;
  for (unsigned I = 0; I < 5; ++I) {
    const MetadataView::StreamHeader &S = V.Streams[I];
    EXPECT_EQ(S.Name, Names[I]);
    EXPECT_EQ(S.Offset, Pos) << S.Name;
    EXPECT_EQ(S.Size % 4, 0u) << S.Name;
    EXPECT_GE(S.Size, 4u) << S.Name;
    Pos += S.Size;
    appendToken(Headers, S.Offset);
    appendToken(Headers, S.Size);
    for (char C : StringRef(Names[I]))
      Headers.push_back(static_cast<uint8_t>(C));
    do
      Headers.push_back(0);
    while (Headers.size() % 4);
  }
  EXPECT_EQ(Pos, Bytes.size());
  EXPECT_EQ(MB.getSerializedSize(), Bytes.size());
  EXPECT_EQ(ByteVec(Bytes.begin() + 32, Bytes.begin() + 108), Headers)
      << "stream headers or their name padding";
  ASSERT_TRUE(V.stream("#Strings") && V.stream("#US") && V.stream("#GUID") &&
              V.stream("#Blob"));
  // #~ (II.24.2.6).
  EXPECT_EQ(V.TablesReserved, 0u);
  EXPECT_EQ(V.TablesMajor, 2u);
  EXPECT_EQ(V.TablesMinor, 0u);
  EXPECT_EQ(V.TablesReserved2, 1u);
  EXPECT_EQ(V.SortedMask, SortedTablesMask);
  EXPECT_EQ(V.HeapSizes, MetadataSizes::computeHeapSizeFlags(
                             V.stream("#Strings")->Size,
                             V.stream("#GUID")->Size, V.stream("#Blob")->Size));
  for (unsigned T = 0; T < 0x2D; ++T) {
    EXPECT_EQ(V.Rows[T], MB.getRowCount(static_cast<TableId>(T)))
        << getTableName(static_cast<TableId>(T)).str();
    EXPECT_EQ((V.ValidMask >> T) & 1, V.Rows[T] != 0 ? 1u : 0u)
        << "Valid bit of " << getTableName(static_cast<TableId>(T)).str();
  }
  EXPECT_EQ(V.ValidMask >> 0x2D, 0u);
  const MetadataView::StreamHeader *T = V.stream("#~");
  uint64_t Used =
      24 + 4 * uint64_t(llvm::popcount(V.ValidMask)) + V.Sizes->getTablesSize();
  EXPECT_EQ(T->Size, alignTo(Used, 4));
  for (uint64_t I = T->Offset + Used; I < uint64_t(T->Offset) + T->Size; ++I)
    EXPECT_EQ(Bytes[I], 0) << "#~ padding";
  // The Module row and the MVID: #GUID entry 1, zero until the PE writer
  // stamps it.
  ASSERT_EQ(V.rows(TableId::Module), 1u);
  EXPECT_EQ(V.get(TableId::Module, 1, "Generation"), 0u);
  EXPECT_EQ(V.get(TableId::Module, 1, "Mvid"), 1u);
  EXPECT_EQ(V.get(TableId::Module, 1, "EncId"), 0u);
  EXPECT_EQ(V.get(TableId::Module, 1, "EncBaseId"), 0u);
  EXPECT_EQ(MB.getMvidOffset(), V.stream("#GUID")->Offset);
  EXPECT_EQ(V.guid(1), ByteVec(16, 0));
  // Every heap starts with its empty entry.
  EXPECT_EQ(V.streamBytes("#Strings")[0], 0);
  EXPECT_EQ(V.streamBytes("#US")[0], 0);
  EXPECT_EQ(V.streamBytes("#Blob")[0], 0);
  // TypeDef 1 is <Module>.
  ASSERT_GE(V.rows(TableId::TypeDef), 1u);
  EXPECT_EQ(V.get(TableId::TypeDef, 1, "Flags"), 0u);
  EXPECT_EQ(V.getStr(TableId::TypeDef, 1, "TypeName"), "<Module>");
  EXPECT_EQ(V.get(TableId::TypeDef, 1, "TypeNamespace"), 0u);
  EXPECT_EQ(V.get(TableId::TypeDef, 1, "Extends"), 0u);
  EXPECT_EQ(V.get(TableId::TypeDef, 1, "FieldList"), 1u);
  EXPECT_EQ(V.get(TableId::TypeDef, 1, "MethodList"), 1u);
}

//===----------------------------------------------------------------------===//
// The empty module
//===----------------------------------------------------------------------===//

TEST(CLIMetadataBuilderTest, EmptyModule) {
  MetadataBuilder MB;
  EXPECT_EQ(MB.getModuleType().Value, 0x02000001u);
  EXPECT_FALSE(MB.hasAssembly());
  EXPECT_FALSE(MB.isFinalized());
  MB.setModuleName("empty.dll");
  MethodBodyStream Bodies;
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  EXPECT_TRUE(MB.isFinalized());
  EXPECT_THAT_ERROR(MB.checkMethodBodies(), Succeeded());
  EXPECT_EQ(Bodies.size(), 0u);
  EXPECT_EQ(MB.getToken(MB.getModuleType()), 0x02000001u);
  EXPECT_EQ(MB.getRowCount(TableId::Module), 1u);
  EXPECT_EQ(MB.getRowCount(TableId::TypeDef), 1u);
  EXPECT_EQ(MB.getRowCount(TableId::Assembly), 0u);

  MetadataView V(serializeMetadata(MB));
  ASSERT_TRUE(V.ok());
  checkLayout(V, MB);
  // A netmodule: Module and TypeDef only.
  EXPECT_EQ(V.ValidMask, 0x5u);
  EXPECT_EQ(V.getStr(TableId::Module, 1, "Name"), "empty.dll");
  EXPECT_EQ(V.HeapSizes, 0u);
  // Empty heaps are still written: one 0x00 entry padded to 4, and the MVID.
  EXPECT_EQ(V.streamBytes("#US"), ArrayRef<uint8_t>({0, 0, 0, 0}));
  EXPECT_EQ(V.streamBytes("#Blob"), ArrayRef<uint8_t>({0, 0, 0, 0}));
  EXPECT_EQ(V.stream("#GUID")->Size, 16u);
  // #~: 24 header bytes, 2 row counts, Module (10) and TypeDef (14) rows.
  EXPECT_EQ(V.stream("#~")->Size, 56u);
}

//===----------------------------------------------------------------------===//
// The Phase-1-shaped module (metadata-writer 2; hello-python.mdump.txt)
//===----------------------------------------------------------------------===//

struct Phase1Module {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  EntityHandle Mscorlib, ValueType, Marshal, ImageT;
  EntityHandle FImage, FData, FMsg, FFn;
  EntityHandle Main, Square, Puts, Cctor, Entry;
  EntityHandle PX, PS, PArgs;
  EntityHandle Ucrt, Alloc, CalliSig, LocalsSig;
  uint32_t Hello = 0;
  uint32_t MainOff = 0, SquareOff = 0, CctorOff = 0, EntryOff = 0;
};

// The rows, and the bodies that use only stable tokens.
void buildPhase1Rows(Phase1Module &M) {
  MetadataBuilder &MB = M.MB;
  MB.setModuleName("fib.dll");
  AssemblyIdentity Id;
  Id.Name = "fib";
  MB.setAssembly(Id);
  AssemblyRefInfo Ms;
  Ms.Name = "mscorlib";
  Ms.Version = {4, 0, 0, 0};
  Ms.PublicKeyOrToken = {0xB7, 0x7A, 0x5C, 0x56, 0x19, 0x34, 0xE0, 0x89};
  M.Mscorlib = MB.addAssemblyRef(Ms);
  M.ValueType = MB.addTypeRef(M.Mscorlib, "System", "ValueType");
  M.Marshal =
      MB.addTypeRef(M.Mscorlib, "System.Runtime.InteropServices", "Marshal");
  M.ImageT =
      MB.addTypeDef(TypeAttributes::Sealed | TypeAttributes::ExplicitLayout, "",
                    "<msil>image_t", M.ValueType);
  MB.addClassLayout(M.ImageT, 1, 72);

  EntityHandle Mod = MB.getModuleType();
  uint32_t SigImage = addBlob(MB, {0x06, 0x11, 0x08}); // valuetype TypeDef 2
  uint32_t SigNInt = addBlob(MB, {0x06, 0x18});
  M.FImage = MB.addField(Mod,
                         FieldAttributes::Private | FieldAttributes::Static |
                             FieldAttributes::HasFieldRVA,
                         "<msil>image", SigImage);
  M.FData = MB.addField(Mod,
                        FieldAttributes::Private | FieldAttributes::Static |
                            FieldAttributes::InitOnly,
                        "<msil>data", SigNInt);
  M.FMsg = MB.addField(Mod,
                       FieldAttributes::Public | FieldAttributes::Static |
                           FieldAttributes::InitOnly,
                       "msg", SigNInt);
  M.FFn = MB.addField(Mod,
                      FieldAttributes::Assembly | FieldAttributes::Static |
                          FieldAttributes::InitOnly,
                      "<msil>fn_square", SigNInt);
  MB.addFieldRVA(M.FImage, 0);

  uint32_t SigI32 = addBlob(MB, {0x00, 0x00, 0x08});
  uint32_t SigI32I32 = addBlob(MB, {0x00, 0x01, 0x08, 0x08});
  uint32_t SigVoid = addBlob(MB, {0x00, 0x00, 0x01});
  uint32_t SigPuts = addBlob(MB, {0x00, 0x01, 0x08, 0x18});
  uint32_t SigMain = addBlob(MB, {0x00, 0x01, 0x08, 0x1D, 0x0E});
  M.Main =
      MB.addMethod(Mod, MethodAttributes::Public | MethodAttributes::Static,
                   MethodImplAttributes::IL, "main", SigI32);
  M.Square =
      MB.addMethod(Mod, MethodAttributes::Assembly | MethodAttributes::Static,
                   MethodImplAttributes::IL, "square", SigI32I32);
  M.PX = MB.addParam(M.Square, 1, ParamAttributes::None, "x");
  M.Puts = MB.addMethod(Mod,
                        MethodAttributes::Public | MethodAttributes::Static |
                            MethodAttributes::HideBySig |
                            MethodAttributes::PinvokeImpl,
                        MethodImplAttributes::PreserveSig, "puts", SigPuts);
  M.PS = MB.addParam(M.Puts, 1, ParamAttributes::None, "s");
  M.Cctor = MB.addMethod(Mod,
                         MethodAttributes::Private | MethodAttributes::Static |
                             MethodAttributes::HideBySig |
                             MethodAttributes::SpecialName |
                             MethodAttributes::RTSpecialName,
                         MethodImplAttributes::IL, ".cctor", SigVoid);
  M.Entry = MB.addMethod(Mod,
                         MethodAttributes::Private | MethodAttributes::Static |
                             MethodAttributes::HideBySig,
                         MethodImplAttributes::IL, "<msil>Main", SigMain);
  M.PArgs = MB.addParam(M.Entry, 1, ParamAttributes::None, "args");

  M.Ucrt = MB.addModuleRef("ucrtbase");
  MB.addImplMap(M.Puts, PInvokeAttributes::CallConvCdecl, "puts", M.Ucrt);
  M.Alloc = MB.addMemberRef(M.Marshal, "AllocHGlobal",
                            addBlob(MB, {0x00, 0x01, 0x18, 0x18}));
  M.CalliSig = MB.addStandAloneSig(addBlob(MB, {0x00, 0x01, 0x08, 0x08}));
  M.LocalsSig =
      MB.addStandAloneSig(addBlob(MB, {0x07, 0x04, 0x08, 0x18, 0x08, 0x08}));
  M.Hello = MB.addUserString("hello from the #US heap");

  // main: ldstr; pop; ldc.i4.0; stloc.0; ldloc.0; ret (fat: it has locals).
  ByteVec MainCode = {0x72};
  appendToken(MainCode, M.Hello);
  for (uint8_t B : {0x26, 0x16, 0x0A, 0x06, 0x2A})
    MainCode.push_back(B);
  MethodBodyInfo MainInfo;
  MainInfo.Code = MainCode;
  MainInfo.MaxStack = 1;
  MainInfo.LocalVarSigToken = MB.getToken(M.LocalsSig);
  M.MainOff = M.Bodies.addBody(MainInfo);
  MB.setMethodBodyOffset(M.Main, M.MainOff);
  // square: ldarg.0; ldarg.0; mul; ret.
  ByteVec SquareCode = {0x02, 0x02, 0x5A, 0x2A};
  MethodBodyInfo SquareInfo;
  SquareInfo.Code = SquareCode;
  SquareInfo.MaxStack = 2;
  M.SquareOff = M.Bodies.addBody(SquareInfo);
  MB.setMethodBodyOffset(M.Square, M.SquareOff);
}

void addPhase1CctorBody(Phase1Module &M) {
  // ldc.i4 72; conv.i; call AllocHGlobal; stsfld <msil>data; ret
  ByteVec Code = {0x20, 72, 0, 0, 0, 0xD3, 0x28};
  appendToken(Code, M.MB.getToken(M.Alloc));
  Code.push_back(0x80);
  appendToken(Code, M.MB.getToken(M.FData));
  Code.push_back(0x2A);
  MethodBodyInfo Info;
  Info.Code = Code;
  Info.MaxStack = 1;
  M.CctorOff = M.Bodies.addBody(Info);
  M.MB.setMethodBodyOffset(M.Cctor, M.CctorOff);
}

void addPhase1EntryBody(Phase1Module &M) {
  // call main; ret
  ByteVec Code = {0x28};
  appendToken(Code, M.MB.getToken(M.Main));
  Code.push_back(0x2A);
  MethodBodyInfo Info;
  Info.Code = Code;
  Info.MaxStack = 1;
  M.EntryOff = M.Bodies.addBody(Info);
  M.MB.setMethodBodyOffset(M.Entry, M.EntryOff);
}

std::unique_ptr<Phase1Module> buildPhase1() {
  auto M = std::make_unique<Phase1Module>();
  buildPhase1Rows(*M);
  EXPECT_THAT_ERROR(M->MB.finalize(M->Bodies), Succeeded());
  addPhase1CctorBody(*M);
  addPhase1EntryBody(*M);
  return M;
}

TEST(CLIMetadataBuilderTest, Phase1ModuleRoundTrip) {
  Phase1Module M;
  buildPhase1Rows(M);
  MetadataBuilder &MB = M.MB;
  // Stable tables: the handle is the final token at once.
  EXPECT_EQ(M.Mscorlib.Value, 0x23000001u);
  EXPECT_EQ(M.ValueType.Value, 0x01000001u);
  EXPECT_EQ(M.Marshal.Value, 0x01000002u);
  EXPECT_EQ(M.ImageT.Value, 0x02000002u);
  EXPECT_EQ(M.Ucrt.Value, 0x1A000001u);
  EXPECT_EQ(M.Alloc.Value, 0x0A000001u);
  EXPECT_EQ(M.CalliSig.Value, 0x11000001u);
  EXPECT_EQ(M.LocalsSig.Value, 0x11000002u);
  EXPECT_EQ(M.Hello, 0x70000001u);
  EXPECT_TRUE(MB.hasAssembly());

  ASSERT_THAT_ERROR(MB.finalize(M.Bodies), Succeeded());
  // Member tokens after finalize(): every member is on <Module>, in add order.
  EXPECT_EQ(MB.getToken(M.FImage), 0x04000001u);
  EXPECT_EQ(MB.getToken(M.FData), 0x04000002u);
  EXPECT_EQ(MB.getToken(M.FMsg), 0x04000003u);
  EXPECT_EQ(MB.getToken(M.FFn), 0x04000004u);
  EXPECT_EQ(MB.getToken(M.Main), 0x06000001u);
  EXPECT_EQ(MB.getToken(M.Square), 0x06000002u);
  EXPECT_EQ(MB.getToken(M.Puts), 0x06000003u);
  EXPECT_EQ(MB.getToken(M.Cctor), 0x06000004u);
  EXPECT_EQ(MB.getToken(M.Entry), 0x06000005u);
  EXPECT_EQ(MB.getToken(M.PX), 0x08000001u);
  EXPECT_EQ(MB.getToken(M.PS), 0x08000002u);
  EXPECT_EQ(MB.getToken(M.PArgs), 0x08000003u);

  // Bodies that use member tokens come after finalize(); checkMethodBodies()
  // names the first IL method without one (puts is pinvokeimpl).
  EXPECT_EQ(failureText(MB.checkMethodBodies()),
            std::string(Prefix) + "method '<Module>::.cctor' has no body");
  addPhase1CctorBody(M);
  EXPECT_EQ(failureText(MB.checkMethodBodies()),
            std::string(Prefix) + "method '<Module>::<msil>Main' has no body");
  addPhase1EntryBody(M);
  EXPECT_THAT_ERROR(MB.checkMethodBodies(), Succeeded());

  MetadataView V(serializeMetadata(MB));
  ASSERT_TRUE(V.ok());
  checkLayout(V, MB);

  // The 14 tables of an ilasm Phase-1 module.
  EXPECT_EQ(V.ValidMask, 0x0000000934028557u);
  EXPECT_EQ(V.HeapSizes, 0u);
  struct Count {
    TableId T;
    uint32_t Rows;
    unsigned RowSize; // hello-python.mdump.txt (2-byte indexes)
  };
  const Count Counts[] = {
      {TableId::Module, 1, 10},       {TableId::TypeRef, 2, 6},
      {TableId::TypeDef, 2, 14},      {TableId::Field, 4, 6},
      {TableId::MethodDef, 5, 14},    {TableId::Param, 3, 6},
      {TableId::MemberRef, 1, 6},     {TableId::ClassLayout, 1, 8},
      {TableId::StandAloneSig, 2, 2}, {TableId::ModuleRef, 1, 2},
      {TableId::ImplMap, 1, 8},       {TableId::FieldRVA, 1, 6},
      {TableId::Assembly, 1, 22},     {TableId::AssemblyRef, 1, 20},
  };
  for (const Count &C : Counts) {
    EXPECT_EQ(V.rows(C.T), C.Rows) << getTableName(C.T).str();
    EXPECT_EQ(MB.getRowCount(C.T), C.Rows) << getTableName(C.T).str();
    EXPECT_EQ(V.Sizes->getRowSize(C.T), C.RowSize) << getTableName(C.T).str();
  }
  // 24 + 14 row counts + 238 bytes of rows, padded to 4.
  EXPECT_EQ(V.stream("#~")->Size, 320u);

  using T = TableId;
  // Module.
  EXPECT_EQ(V.getStr(T::Module, 1, "Name"), "fib.dll");
  // TypeRef: ResolutionScope = AssemblyRef 1 = (1 << 2) | 2.
  EXPECT_EQ(V.get(T::TypeRef, 1, "ResolutionScope"), 6u);
  EXPECT_EQ(V.getStr(T::TypeRef, 1, "TypeName"), "ValueType");
  EXPECT_EQ(V.getStr(T::TypeRef, 1, "TypeNamespace"), "System");
  EXPECT_EQ(V.get(T::TypeRef, 2, "ResolutionScope"), 6u);
  EXPECT_EQ(V.getStr(T::TypeRef, 2, "TypeName"), "Marshal");
  EXPECT_EQ(V.getStr(T::TypeRef, 2, "TypeNamespace"),
            "System.Runtime.InteropServices");
  // TypeDef 2: '<msil>image_t' extends TypeRef 1 = (1 << 2) | 1, owns no
  // members, so its lists point past the ends of Field and MethodDef.
  EXPECT_EQ(V.get(T::TypeDef, 2, "Flags"), 0x110u);
  EXPECT_EQ(V.getStr(T::TypeDef, 2, "TypeName"), "<msil>image_t");
  EXPECT_EQ(V.get(T::TypeDef, 2, "TypeNamespace"), 0u);
  EXPECT_EQ(V.get(T::TypeDef, 2, "Extends"), 5u);
  EXPECT_EQ(V.get(T::TypeDef, 2, "FieldList"), 5u);
  EXPECT_EQ(V.get(T::TypeDef, 2, "MethodList"), 6u);
  // Field.
  const struct {
    uint32_t Flags;
    const char *Name;
    ByteVec Sig;
  } Fields[] = {{0x0111, "<msil>image", {0x06, 0x11, 0x08}},
                {0x0031, "<msil>data", {0x06, 0x18}},
                {0x0036, "msg", {0x06, 0x18}},
                {0x0033, "<msil>fn_square", {0x06, 0x18}}};
  for (uint32_t R = 1; R <= 4; ++R) {
    SCOPED_TRACE(R);
    EXPECT_EQ(V.get(T::Field, R, "Flags"), Fields[R - 1].Flags);
    EXPECT_EQ(V.getStr(T::Field, R, "Name"), Fields[R - 1].Name);
    EXPECT_EQ(V.getBlob(T::Field, R, "Signature"), Fields[R - 1].Sig);
  }
  // The three native int fields share one deduplicated blob.
  EXPECT_EQ(V.get(T::Field, 2, "Signature"), V.get(T::Field, 3, "Signature"));
  // MethodDef: RVA = IL stream RVA + body offset, 0 without a body.
  const struct {
    uint32_t RVA, ImplFlags, Flags;
    const char *Name;
    ByteVec Sig;
    uint32_t ParamList;
  } Methods[] = {
      {ILRVA + M.MainOff, 0, 0x0016, "main", {0x00, 0x00, 0x08}, 1},
      {ILRVA + M.SquareOff, 0, 0x0013, "square", {0x00, 0x01, 0x08, 0x08}, 1},
      {0, 0x0080, 0x2096, "puts", {0x00, 0x01, 0x08, 0x18}, 2},
      {ILRVA + M.CctorOff, 0, 0x1891, ".cctor", {0x00, 0x00, 0x01}, 3},
      {ILRVA + M.EntryOff,
       0,
       0x0091,
       "<msil>Main",
       {0x00, 0x01, 0x08, 0x1D, 0x0E},
       3},
  };
  for (uint32_t R = 1; R <= 5; ++R) {
    SCOPED_TRACE(R);
    EXPECT_EQ(V.get(T::MethodDef, R, "RVA"), Methods[R - 1].RVA);
    EXPECT_EQ(V.get(T::MethodDef, R, "ImplFlags"), Methods[R - 1].ImplFlags);
    EXPECT_EQ(V.get(T::MethodDef, R, "Flags"), Methods[R - 1].Flags);
    EXPECT_EQ(V.getStr(T::MethodDef, R, "Name"), Methods[R - 1].Name);
    EXPECT_EQ(V.getBlob(T::MethodDef, R, "Signature"), Methods[R - 1].Sig);
    EXPECT_EQ(V.get(T::MethodDef, R, "ParamList"), Methods[R - 1].ParamList);
  }
  // Param.
  const char *ParamNames[] = {"x", "s", "args"};
  for (uint32_t R = 1; R <= 3; ++R) {
    EXPECT_EQ(V.get(T::Param, R, "Flags"), 0u);
    EXPECT_EQ(V.get(T::Param, R, "Sequence"), 1u);
    EXPECT_EQ(V.getStr(T::Param, R, "Name"), ParamNames[R - 1]);
  }
  // MemberRef: Class = TypeRef 2 as MemberRefParent = (2 << 3) | 1.
  EXPECT_EQ(V.get(T::MemberRef, 1, "Class"), 17u);
  EXPECT_EQ(V.getStr(T::MemberRef, 1, "Name"), "AllocHGlobal");
  EXPECT_EQ(V.getBlob(T::MemberRef, 1, "Signature"),
            (ByteVec{0x00, 0x01, 0x18, 0x18}));
  // ClassLayout.
  EXPECT_EQ(V.get(T::ClassLayout, 1, "PackingSize"), 1u);
  EXPECT_EQ(V.get(T::ClassLayout, 1, "ClassSize"), 72u);
  EXPECT_EQ(V.get(T::ClassLayout, 1, "Parent"), 2u);
  // StandAloneSig; the calli signature is square's blob.
  EXPECT_EQ(V.getBlob(T::StandAloneSig, 1, "Signature"),
            (ByteVec{0x00, 0x01, 0x08, 0x08}));
  EXPECT_EQ(V.get(T::StandAloneSig, 1, "Signature"),
            V.get(T::MethodDef, 2, "Signature"));
  EXPECT_EQ(V.getBlob(T::StandAloneSig, 2, "Signature"),
            (ByteVec{0x07, 0x04, 0x08, 0x18, 0x08, 0x08}));
  // ModuleRef and ImplMap: MemberForwarded = MethodDef 3 = (3 << 1) | 1.
  EXPECT_EQ(V.getStr(T::ModuleRef, 1, "Name"), "ucrtbase");
  EXPECT_EQ(V.get(T::ImplMap, 1, "MappingFlags"), 0x0200u);
  EXPECT_EQ(V.get(T::ImplMap, 1, "MemberForwarded"), 7u);
  EXPECT_EQ(V.getStr(T::ImplMap, 1, "ImportName"), "puts");
  EXPECT_EQ(V.get(T::ImplMap, 1, "ImportScope"), 1u);
  // FieldRVA: RVA = mapped-data RVA + offset.
  EXPECT_EQ(V.get(T::FieldRVA, 1, "RVA"), DataRVA);
  EXPECT_EQ(V.get(T::FieldRVA, 1, "Field"), 1u);
  // Assembly: SHA1 by default, version 0.0.0.0, no key, no culture.
  EXPECT_EQ(V.get(T::Assembly, 1, "HashAlgId"), 0x8004u);
  for (const char *C : {"MajorVersion", "MinorVersion", "BuildNumber",
                        "RevisionNumber", "Flags"})
    EXPECT_EQ(V.get(T::Assembly, 1, C), 0u) << C;
  EXPECT_TRUE(V.getBlob(T::Assembly, 1, "PublicKey").empty());
  EXPECT_EQ(V.getStr(T::Assembly, 1, "Name"), "fib");
  EXPECT_EQ(V.getStr(T::Assembly, 1, "Culture"), "");
  // AssemblyRef.
  EXPECT_EQ(V.get(T::AssemblyRef, 1, "MajorVersion"), 4u);
  EXPECT_EQ(V.get(T::AssemblyRef, 1, "MinorVersion"), 0u);
  EXPECT_EQ(V.get(T::AssemblyRef, 1, "BuildNumber"), 0u);
  EXPECT_EQ(V.get(T::AssemblyRef, 1, "RevisionNumber"), 0u);
  EXPECT_EQ(V.get(T::AssemblyRef, 1, "Flags"), 0u);
  EXPECT_EQ(V.getBlob(T::AssemblyRef, 1, "PublicKeyOrToken"),
            (ByteVec{0xB7, 0x7A, 0x5C, 0x56, 0x19, 0x34, 0xE0, 0x89}));
  EXPECT_EQ(V.getStr(T::AssemblyRef, 1, "Name"), "mscorlib");
  EXPECT_EQ(V.getStr(T::AssemblyRef, 1, "Culture"), "");
  EXPECT_TRUE(V.getBlob(T::AssemblyRef, 1, "HashValue").empty());
  // #US: the ldstr token's offset holds the string's UTF-16.
  ArrayRef<uint8_t> US = V.streamBytes("#US");
  ASSERT_GE(US.size(), 2u + 46u + 1u);
  EXPECT_EQ(US[1], 47u); // 2 * 23 + 1
  EXPECT_EQ(US[2], 'h');
  EXPECT_EQ(US[3], 0u);
  EXPECT_EQ(US[48], 0u); // trailing byte: no special characters
}

TEST(CLIMetadataBuilderTest, TwoIdenticalBuildsGiveIdenticalBytes) {
  std::unique_ptr<Phase1Module> A = buildPhase1(), B = buildPhase1();
  EXPECT_EQ(A->MB.getSerializedSize(), B->MB.getSerializedSize());
  EXPECT_EQ(A->MB.getMvidOffset(), B->MB.getMvidOffset());
  EXPECT_EQ(serializeMetadata(A->MB), serializeMetadata(B->MB));
  EXPECT_EQ(A->Bodies.getBytes(), B->Bodies.getBytes());
}

// Fixed-width RVA columns: the size and every other byte do not depend on
// the RVAs (5.7.6).
TEST(CLIMetadataBuilderTest, SerializedSizeIsIndependentOfRVAs) {
  std::unique_ptr<Phase1Module> M = buildPhase1();
  const uint32_t IL2 = 0x12340, Data2 = 0x56780;
  ByteVec A = serializeMetadata(M->MB, ILRVA, DataRVA);
  ByteVec B = serializeMetadata(M->MB, IL2, Data2);
  ASSERT_EQ(A.size(), B.size());
  MetadataView VA(A), VB(B);
  ASSERT_TRUE(VA.ok());
  ASSERT_TRUE(VB.ok());
  for (uint32_t R = 1; R <= VA.rows(TableId::MethodDef); ++R) {
    uint32_t RA = VA.get(TableId::MethodDef, R, "RVA");
    uint32_t RB = VB.get(TableId::MethodDef, R, "RVA");
    if (RA == 0)
      EXPECT_EQ(RB, 0u);
    else
      EXPECT_EQ(RB - IL2, RA - ILRVA);
    unsigned W;
    uint64_t Off = VA.cell(TableId::MethodDef, R, "RVA", W);
    std::copy(A.begin() + Off, A.begin() + Off + W, B.begin() + Off);
  }
  EXPECT_EQ(VB.get(TableId::FieldRVA, 1, "RVA"), Data2);
  unsigned W;
  uint64_t Off = VA.cell(TableId::FieldRVA, 1, "RVA", W);
  std::copy(A.begin() + Off, A.begin() + Off + W, B.begin() + Off);
  EXPECT_EQ(A, B);
}

//===----------------------------------------------------------------------===//
// Member runs, implicit rows and sorted tables
//===----------------------------------------------------------------------===//

// Field, MethodDef and Param rows are runs of consecutive owners, in add
// order within an owner; an empty run points at the next owner's first row,
// or past the end of the table (5.7.6, 5.8.3 R6).
TEST(CLIMetadataBuilderTest, MemberRunsGroupByOwnerInAddOrder) {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  EntityHandle Obj = addObjectTypeRef(MB);
  auto Class = [&](StringRef Name) {
    return MB.addTypeDef(TypeAttributes::Public, "", Name, Obj);
  };
  EntityHandle Mod = MB.getModuleType();
  EntityHandle A = Class("A"), B = Class("B"), C = Class("C"), D = Class("D");
  EXPECT_EQ(A.Value, 0x02000002u);
  EXPECT_EQ(D.Value, 0x02000005u);
  uint32_t FSig = addBlob(MB, {0x06, 0x08});
  uint32_t MSig = addBlob(MB, {0x00, 0x00, 0x01});
  auto Field = [&](EntityHandle Owner, StringRef Name) {
    return MB.addField(Owner, FieldAttributes::Public | FieldAttributes::Static,
                       Name, FSig);
  };
  auto Method = [&](EntityHandle Owner, StringRef Name) {
    return MB.addMethod(Owner,
                        MethodAttributes::Public | MethodAttributes::Static,
                        MethodImplAttributes::IL, Name, MSig);
  };
  // Interleaved over owners; within A, 'zeta' before 'alpha'.
  EntityHandle FB1 = Field(B, "b1");
  EntityHandle MZeta = Method(A, "zeta");
  EntityHandle FM1 = Field(Mod, "mf1");
  EntityHandle MC1 = Method(C, "c1");
  EntityHandle PQ = MB.addParam(MC1, 1, ParamAttributes::None, "q");
  EntityHandle FB2 = Field(B, "b2");
  EntityHandle MAlpha = Method(A, "alpha");
  EntityHandle PP1 = MB.addParam(MAlpha, 1, ParamAttributes::None, "p1");
  EntityHandle PP2 = MB.addParam(MAlpha, 2, ParamAttributes::Out, "p2");
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());

  EXPECT_EQ(MB.getToken(FM1), 0x04000001u);
  EXPECT_EQ(MB.getToken(FB1), 0x04000002u);
  EXPECT_EQ(MB.getToken(FB2), 0x04000003u);
  EXPECT_EQ(MB.getToken(MZeta), 0x06000001u);
  EXPECT_EQ(MB.getToken(MAlpha), 0x06000002u);
  EXPECT_EQ(MB.getToken(MC1), 0x06000003u);
  EXPECT_EQ(MB.getToken(PP1), 0x08000001u);
  EXPECT_EQ(MB.getToken(PP2), 0x08000002u);
  EXPECT_EQ(MB.getToken(PQ), 0x08000003u);
  EXPECT_THAT_EXPECTED(MB.resolveToken(TokenRef::getHandle(MC1)),
                       HasValue(0x06000003u));
  EXPECT_THAT_EXPECTED(MB.resolveToken(TokenRef::getHandle(PQ)),
                       HasValue(0x08000003u));

  MetadataView V(serializeMetadata(MB));
  ASSERT_TRUE(V.ok());
  checkLayout(V, MB);
  using T = TableId;
  using Names = std::vector<std::string>;
  // FieldList: <Module> 1, A 2 (empty), B 2, C 4 (empty), D 4 (empty, at the
  // end: rows + 1).
  const uint32_t FieldLists[] = {1, 2, 2, 4, 4};
  const uint32_t MethodLists[] = {1, 1, 3, 3, 4};
  for (uint32_t R = 1; R <= 5; ++R) {
    EXPECT_EQ(V.get(T::TypeDef, R, "FieldList"), FieldLists[R - 1]) << R;
    EXPECT_EQ(V.get(T::TypeDef, R, "MethodList"), MethodLists[R - 1]) << R;
  }
  EXPECT_EQ(V.run(T::TypeDef, 1, "FieldList", T::Field), Names{"mf1"});
  EXPECT_EQ(V.run(T::TypeDef, 2, "FieldList", T::Field), Names{});
  EXPECT_EQ(V.run(T::TypeDef, 3, "FieldList", T::Field), (Names{"b1", "b2"}));
  EXPECT_EQ(V.run(T::TypeDef, 5, "FieldList", T::Field), Names{});
  EXPECT_EQ(V.run(T::TypeDef, 2, "MethodList", T::MethodDef),
            (Names{"zeta", "alpha"}));
  EXPECT_EQ(V.run(T::TypeDef, 4, "MethodList", T::MethodDef), Names{"c1"});
  EXPECT_EQ(V.run(T::TypeDef, 5, "MethodList", T::MethodDef), Names{});
  // ParamList: zeta 1 (empty), alpha 1, c1 3.
  EXPECT_EQ(V.get(T::MethodDef, 1, "ParamList"), 1u);
  EXPECT_EQ(V.get(T::MethodDef, 2, "ParamList"), 1u);
  EXPECT_EQ(V.get(T::MethodDef, 3, "ParamList"), 3u);
  EXPECT_EQ(V.run(T::MethodDef, 2, "ParamList", T::Param), (Names{"p1", "p2"}));
  EXPECT_EQ(V.run(T::MethodDef, 3, "ParamList", T::Param), Names{"q"});
  EXPECT_EQ(V.get(T::Param, 2, "Flags"), 0x0002u);
  EXPECT_EQ(V.get(T::Param, 2, "Sequence"), 2u);
}

// finalize() writes one PropertyMap row per TypeDef that owns properties (in
// TypeDef order) and one NestedClass row per enclosed TypeDef; MethodSemantics
// is stable-sorted by Association.
TEST(CLIMetadataBuilderTest, PropertyMapAndNestedClassRows) {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  EntityHandle Obj = addObjectTypeRef(MB);
  EntityHandle Outer = MB.addTypeDef(TypeAttributes::Public, "N", "Outer", Obj);
  EntityHandle Inner =
      MB.addTypeDef(TypeAttributes::NestedPublic, "", "Inner", Obj, Outer);
  EntityHandle P1 = MB.addTypeDef(TypeAttributes::Public, "N", "P1", Obj);
  EntityHandle Inner2 =
      MB.addTypeDef(TypeAttributes::NestedPrivate, "", "Inner2", Obj, Inner);
  EntityHandle P2 = MB.addTypeDef(TypeAttributes::Public, "N", "P2", Obj);
  EXPECT_EQ(Inner.Value, 0x02000003u);
  EXPECT_EQ(Inner2.Value, 0x02000005u);
  EXPECT_EQ(P2.Value, 0x02000006u);

  uint32_t Getter = addBlob(MB, {0x20, 0x00, 0x08});
  uint32_t Setter = addBlob(MB, {0x20, 0x01, 0x01, 0x08});
  uint32_t PropSig = addBlob(MB, {0x28, 0x00, 0x08});
  auto Method = [&](EntityHandle Owner, StringRef Name, uint32_t Sig) {
    return MB.addMethod(Owner,
                        MethodAttributes::Public | MethodAttributes::HideBySig |
                            MethodAttributes::SpecialName,
                        MethodImplAttributes::IL, Name, Sig);
  };
  EntityHandle GetSecond = Method(P2, "get_Second", Getter);
  EntityHandle GetA = Method(P1, "get_A", Getter);
  EntityHandle SetA = Method(P1, "set_A", Setter);
  EntityHandle GetB = Method(P1, "get_B", Getter);
  EntityHandle PSecond =
      MB.addProperty(P2, PropertyAttributes::None, "Second", PropSig);
  EntityHandle PA = MB.addProperty(P1, PropertyAttributes::None, "A", PropSig);
  EntityHandle PB = MB.addProperty(P1, PropertyAttributes::None, "B", PropSig);
  EntityHandle PThird =
      MB.addProperty(P2, PropertyAttributes::SpecialName, "Third", PropSig);
  MB.addMethodSemantics(PB, MethodSemanticsAttributes::Getter, GetB);
  MB.addMethodSemantics(PA, MethodSemanticsAttributes::Setter, SetA);
  MB.addMethodSemantics(PA, MethodSemanticsAttributes::Getter, GetA);
  MB.addMethodSemantics(PSecond, MethodSemanticsAttributes::Getter, GetSecond);
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());

  EXPECT_EQ(MB.getToken(GetA), 0x06000001u);
  EXPECT_EQ(MB.getToken(SetA), 0x06000002u);
  EXPECT_EQ(MB.getToken(GetB), 0x06000003u);
  EXPECT_EQ(MB.getToken(GetSecond), 0x06000004u);
  EXPECT_EQ(MB.getToken(PA), 0x17000001u);
  EXPECT_EQ(MB.getToken(PB), 0x17000002u);
  EXPECT_EQ(MB.getToken(PSecond), 0x17000003u);
  EXPECT_EQ(MB.getToken(PThird), 0x17000004u);
  EXPECT_EQ(MB.getRowCount(TableId::PropertyMap), 2u);
  EXPECT_EQ(MB.getRowCount(TableId::NestedClass), 2u);

  MetadataView V(serializeMetadata(MB));
  ASSERT_TRUE(V.ok());
  checkLayout(V, MB);
  using T = TableId;
  using Names = std::vector<std::string>;
  // PropertyMap: (P1, 1), (P2, 3).
  EXPECT_EQ(V.get(T::PropertyMap, 1, "Parent"), 4u);
  EXPECT_EQ(V.get(T::PropertyMap, 1, "PropertyList"), 1u);
  EXPECT_EQ(V.get(T::PropertyMap, 2, "Parent"), 6u);
  EXPECT_EQ(V.get(T::PropertyMap, 2, "PropertyList"), 3u);
  EXPECT_EQ(V.run(T::PropertyMap, 1, "PropertyList", T::Property),
            (Names{"A", "B"}));
  EXPECT_EQ(V.run(T::PropertyMap, 2, "PropertyList", T::Property),
            (Names{"Second", "Third"}));
  EXPECT_EQ(V.get(T::Property, 4, "Flags"), 0x0200u);
  EXPECT_EQ(V.getBlob(T::Property, 1, "Type"), (ByteVec{0x28, 0x00, 0x08}));
  // MethodSemantics: Association = (property row << 1) | 1, ties in add
  // order.
  const uint32_t Semantics[][3] = {
      {1, 2, 3}, // A: setter, set_A
      {2, 1, 3}, // A: getter, get_A
      {2, 3, 5}, // B: getter, get_B
      {2, 4, 7}, // Second: getter, get_Second
  };
  ASSERT_EQ(V.rows(T::MethodSemantics), 4u);
  for (uint32_t R = 1; R <= 4; ++R) {
    SCOPED_TRACE(R);
    EXPECT_EQ(V.get(T::MethodSemantics, R, "Semantics"), Semantics[R - 1][0]);
    EXPECT_EQ(V.get(T::MethodSemantics, R, "Method"), Semantics[R - 1][1]);
    EXPECT_EQ(V.get(T::MethodSemantics, R, "Association"), Semantics[R - 1][2]);
    EXPECT_EQ(V.get(T::MethodSemantics, R, "Association"),
              coded(CodedIndexKind::HasSemantics,
                    makeToken(T::Property, (Semantics[R - 1][2] >> 1))));
  }
  // NestedClass: (Inner, Outer), (Inner2, Inner).
  EXPECT_EQ(V.get(T::NestedClass, 1, "NestedClass"), 3u);
  EXPECT_EQ(V.get(T::NestedClass, 1, "EnclosingClass"), 2u);
  EXPECT_EQ(V.get(T::NestedClass, 2, "NestedClass"), 5u);
  EXPECT_EQ(V.get(T::NestedClass, 2, "EnclosingClass"), 3u);
  EXPECT_EQ(V.get(T::TypeDef, 3, "Flags"), 0x2u);
  EXPECT_EQ(V.get(T::TypeDef, 5, "Flags"), 0x3u);
  EXPECT_EQ(V.getStr(T::TypeDef, 2, "TypeNamespace"), "N");
}

// ClassLayout, FieldLayout, FieldRVA, ImplMap and CustomAttribute are
// stable-sorted on their keys as encoded, after the member tables are
// numbered (5.7.6, II.22).
TEST(CLIMetadataBuilderTest, SortedTablesUseEncodedKeys) {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  AssemblyIdentity Id;
  Id.Name = "sorted";
  MB.setAssembly(Id);
  EntityHandle Obj = addObjectTypeRef(MB);
  EntityHandle Mscorlib = rowHandle(TableId::AssemblyRef, 1);
  EntityHandle Attribute = MB.addTypeRef(Mscorlib, "System", "Attribute");
  EXPECT_EQ(Obj.Value, 0x01000001u);
  EXPECT_EQ(Attribute.Value, 0x01000002u);
  uint32_t CtorSig = addBlob(MB, {0x20, 0x01, 0x01, 0x08});
  EntityHandle AttrCtor = MB.addMemberRef(Attribute, ".ctor", CtorSig);
  EntityHandle Mod = MB.getModuleType();
  EntityHandle MyAttr =
      MB.addTypeDef(TypeAttributes::Public, "", "MyAttr", Attribute);
  EntityHandle S1 =
      MB.addTypeDef(TypeAttributes::Public | TypeAttributes::ExplicitLayout |
                        TypeAttributes::Sealed,
                    "", "S1", Obj);
  EntityHandle S2 =
      MB.addTypeDef(TypeAttributes::Public | TypeAttributes::ExplicitLayout |
                        TypeAttributes::Sealed,
                    "", "S2", Obj);
  uint32_t FSig = addBlob(MB, {0x06, 0x08});
  uint32_t MSig = addBlob(MB, {0x00, 0x00, 0x01});
  auto Field = [&](EntityHandle Owner, StringRef Name) {
    return MB.addField(Owner, FieldAttributes::Public | FieldAttributes::Static,
                       Name, FSig);
  };
  auto Method = [&](EntityHandle Owner, StringRef Name, uint32_t Sig) {
    return MB.addMethod(Owner,
                        MethodAttributes::Public | MethodAttributes::Static |
                            MethodAttributes::PinvokeImpl,
                        MethodImplAttributes::PreserveSig, Name, Sig);
  };
  EntityHandle FS2 = Field(S2, "fs2");
  EntityHandle FS1 = Field(S1, "fs1");
  EntityHandle FM = Field(Mod, "fm");
  EntityHandle MS1 = Method(S1, "ms1", MSig);
  EntityHandle MCtor = MB.addMethod(
      MyAttr,
      MethodAttributes::Public | MethodAttributes::HideBySig |
          MethodAttributes::SpecialName | MethodAttributes::RTSpecialName,
      MethodImplAttributes::IL, ".ctor", CtorSig);
  EntityHandle MMod = Method(Mod, "mmod", MSig);
  EntityHandle MMod2 = Method(Mod, "mmod2", MSig);
  EntityHandle Lib = MB.addModuleRef("lib");

  MB.addClassLayout(S2, 8, 16);
  MB.addClassLayout(S1, 4, 8);
  MB.addFieldLayout(FS2, 8);
  MB.addFieldLayout(FS1, 4);
  MB.addFieldRVA(FS2, 32);
  MB.addFieldRVA(FM, 0);
  MB.addImplMap(MS1,
                PInvokeAttributes::NoMangle | PInvokeAttributes::CallConvCdecl,
                "ms1", Lib);
  MB.addImplMap(MMod2, PInvokeAttributes::CallConvCdecl, "mmod2", Lib);
  MB.addImplMap(MMod, PInvokeAttributes::CallConvStdcall, "mmod", Lib);
  auto Value = [&](uint8_t I) {
    return addBlob(MB, {0x01, 0x00, I, 0x00, 0x00, 0x00, 0x00, 0x00});
  };
  EntityHandle Module = rowHandle(TableId::Module, 1);
  EntityHandle Assembly = rowHandle(TableId::Assembly, 1);
  MB.addCustomAttribute(Assembly, AttrCtor, Value(1));
  MB.addCustomAttribute(MS1, MCtor, Value(2));
  MB.addCustomAttribute(Module, AttrCtor, Value(3));
  MB.addCustomAttribute(FM, AttrCtor, Value(4));
  MB.addCustomAttribute(Module, MCtor, Value(5));
  MB.addCustomAttribute(S1, AttrCtor, Value(6));
  MB.addCustomAttribute(Obj, AttrCtor, Value(7));
  MB.addCustomAttribute(Mscorlib, AttrCtor, Value(8));
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());

  // Members: fields fm, fs1, fs2; methods mmod, mmod2, .ctor, ms1.
  EXPECT_EQ(MB.getToken(FM), 0x04000001u);
  EXPECT_EQ(MB.getToken(FS1), 0x04000002u);
  EXPECT_EQ(MB.getToken(FS2), 0x04000003u);
  EXPECT_EQ(MB.getToken(MMod), 0x06000001u);
  EXPECT_EQ(MB.getToken(MMod2), 0x06000002u);
  EXPECT_EQ(MB.getToken(MCtor), 0x06000003u);
  EXPECT_EQ(MB.getToken(MS1), 0x06000004u);

  MetadataView V(serializeMetadata(MB));
  ASSERT_TRUE(V.ok());
  checkLayout(V, MB);
  using T = TableId;
  // ClassLayout by Parent: S1 (TypeDef 3), S2 (4).
  ASSERT_EQ(V.rows(T::ClassLayout), 2u);
  EXPECT_EQ(V.get(T::ClassLayout, 1, "Parent"), 3u);
  EXPECT_EQ(V.get(T::ClassLayout, 1, "PackingSize"), 4u);
  EXPECT_EQ(V.get(T::ClassLayout, 1, "ClassSize"), 8u);
  EXPECT_EQ(V.get(T::ClassLayout, 2, "Parent"), 4u);
  EXPECT_EQ(V.get(T::ClassLayout, 2, "PackingSize"), 8u);
  EXPECT_EQ(V.get(T::ClassLayout, 2, "ClassSize"), 16u);
  // FieldLayout by Field: fs1 (2), fs2 (3).
  ASSERT_EQ(V.rows(T::FieldLayout), 2u);
  EXPECT_EQ(V.get(T::FieldLayout, 1, "Field"), 2u);
  EXPECT_EQ(V.get(T::FieldLayout, 1, "Offset"), 4u);
  EXPECT_EQ(V.get(T::FieldLayout, 2, "Field"), 3u);
  EXPECT_EQ(V.get(T::FieldLayout, 2, "Offset"), 8u);
  // FieldRVA by Field: fm (1), fs2 (3).
  ASSERT_EQ(V.rows(T::FieldRVA), 2u);
  EXPECT_EQ(V.get(T::FieldRVA, 1, "Field"), 1u);
  EXPECT_EQ(V.get(T::FieldRVA, 1, "RVA"), DataRVA);
  EXPECT_EQ(V.get(T::FieldRVA, 2, "Field"), 3u);
  EXPECT_EQ(V.get(T::FieldRVA, 2, "RVA"), DataRVA + 32);
  // ImplMap by MemberForwarded = (MethodDef row << 1) | 1: mmod 3, mmod2 5,
  // ms1 9.
  ASSERT_EQ(V.rows(T::ImplMap), 3u);
  const struct {
    uint32_t Forwarded, Flags;
    const char *Name;
  } Maps[] = {{3, 0x0300, "mmod"}, {5, 0x0200, "mmod2"}, {9, 0x0201, "ms1"}};
  for (uint32_t R = 1; R <= 3; ++R) {
    SCOPED_TRACE(R);
    EXPECT_EQ(V.get(T::ImplMap, R, "MemberForwarded"), Maps[R - 1].Forwarded);
    EXPECT_EQ(V.get(T::ImplMap, R, "MappingFlags"), Maps[R - 1].Flags);
    EXPECT_EQ(V.getStr(T::ImplMap, R, "ImportName"), Maps[R - 1].Name);
    EXPECT_EQ(V.get(T::ImplMap, R, "ImportScope"), 1u);
  }
  // CustomAttribute by Parent as HasCustomAttribute (5 tag bits): Field 1 =
  // 33, TypeRef 1 = 34, Module = 39 (twice, add order kept), Assembly = 46,
  // AssemblyRef 1 = 47, TypeDef 3 = 99, MethodDef 4 = 128. Type is
  // CustomAttributeType: MemberRef 1 = 11, MethodDef 3 = 26.
  EXPECT_EQ(
      coded(CodedIndexKind::HasCustomAttribute, makeToken(TableId::Module, 1)),
      39u);
  EXPECT_EQ(coded(CodedIndexKind::CustomAttributeType,
                  makeToken(TableId::MethodDef, 3)),
            26u);
  ASSERT_EQ(V.rows(T::CustomAttribute), 8u);
  const uint32_t Attrs[][3] = {{33, 11, 4}, {34, 11, 7}, {39, 11, 3},
                               {39, 26, 5}, {46, 11, 1}, {47, 11, 8},
                               {99, 11, 6}, {128, 26, 2}};
  for (uint32_t R = 1; R <= 8; ++R) {
    SCOPED_TRACE(R);
    EXPECT_EQ(V.get(T::CustomAttribute, R, "Parent"), Attrs[R - 1][0]);
    EXPECT_EQ(V.get(T::CustomAttribute, R, "Type"), Attrs[R - 1][1]);
    EXPECT_EQ(V.getBlob(T::CustomAttribute, R, "Value"),
              (ByteVec{0x01, 0x00, static_cast<uint8_t>(Attrs[R - 1][2]), 0x00,
                       0x00, 0x00, 0x00, 0x00}));
  }
}

// Sorted tables are "stable-sorted on the encoded key value, so ties keep
// insertion order" (5.7.6), and the bytes depend only on the calls and their
// order (5.7.12). The ties here are many and interleaved, in tables far
// larger than the 16 (libstdc++) or 32 (MSVC STL) elements below which
// std::sort falls back to an insertion sort that happens to keep ties in
// order. The other columns run against insertion order (Value blobs are added
// in reverse, Method rows are used in reverse), so a sort that breaks ties on
// another column fails as well.
TEST(CLIMetadataBuilderTest, SortedTablesKeepInsertionOrderOfManyTies) {
  constexpr uint32_t N = 256; // rows of CustomAttribute and of MethodSemantics
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  AssemblyIdentity Id;
  Id.Name = "ties";
  MB.setAssembly(Id);
  EntityHandle Obj = addObjectTypeRef(MB);
  EntityHandle Attribute =
      MB.addTypeRef(rowHandle(TableId::AssemblyRef, 1), "System", "Attribute");
  uint32_t CtorSig = addBlob(MB, {0x20, 0x01, 0x01, 0x08});
  EntityHandle AttrCtor = MB.addMemberRef(Attribute, ".ctor", CtorSig);
  EXPECT_EQ(AttrCtor.Value, 0x0A000001u);

  // CustomAttribute: a ctor(int32) value blob (II.23.3: prolog 01 00, the
  // int32, no named arguments) holding the insertion index I. Even indexes
  // go to the Assembly, odd ones to the Module, so the first row added sorts
  // into the second half.
  auto ValueBytes = [](uint32_t I) {
    ByteVec B{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    B[2] = static_cast<uint8_t>(I);
    B[3] = static_cast<uint8_t>(I >> 8);
    return B;
  };
  std::vector<uint32_t> ValueBlob(N);
  for (uint32_t I = N; I-- > 0;)
    ValueBlob[I] = MB.addBlob(ValueBytes(I));
  EntityHandle Module = rowHandle(TableId::Module, 1);
  EntityHandle Assembly = rowHandle(TableId::Assembly, 1);
  for (uint32_t I = 0; I < N; ++I)
    MB.addCustomAttribute(I % 2 ? Module : Assembly, AttrCtor, ValueBlob[I]);

  // MethodSemantics: N 'Other' rows; even insertion indexes go to property B
  // (Property 2), odd ones to A (Property 1). Row I names MethodDef N - I.
  EntityHandle Props = MB.addTypeDef(TypeAttributes::Public, "", "Props", Obj);
  uint32_t MSig = addBlob(MB, {0x20, 0x00, 0x01});
  uint32_t PropSig = addBlob(MB, {0x28, 0x00, 0x08});
  std::vector<EntityHandle> Methods;
  for (uint32_t M = 0; M < N; ++M) {
    char Name[16];
    std::snprintf(Name, sizeof(Name), "m%03u", static_cast<unsigned>(M));
    Methods.push_back(MB.addMethod(Props,
                                   MethodAttributes::Public |
                                       MethodAttributes::HideBySig |
                                       MethodAttributes::SpecialName,
                                   MethodImplAttributes::IL, Name, MSig));
  }
  EntityHandle PA =
      MB.addProperty(Props, PropertyAttributes::None, "A", PropSig);
  EntityHandle PB =
      MB.addProperty(Props, PropertyAttributes::None, "B", PropSig);
  for (uint32_t I = 0; I < N; ++I)
    MB.addMethodSemantics(I % 2 ? PA : PB, MethodSemanticsAttributes::Other,
                          Methods[N - 1 - I]);
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  EXPECT_EQ(MB.getToken(Methods.front()), 0x06000001u);
  EXPECT_EQ(MB.getToken(Methods.back()), 0x06000000u | N);
  EXPECT_EQ(MB.getToken(PA), 0x17000001u);
  EXPECT_EQ(MB.getToken(PB), 0x17000002u);

  MetadataView V(serializeMetadata(MB));
  ASSERT_TRUE(V.ok());
  checkLayout(V, MB);
  using T = TableId;

  // CustomAttribute by Parent as HasCustomAttribute (II.24.2.6, 5 tag bits):
  // Module 1 = (1 << 5) | 7 = 39 first, then Assembly 1 = (1 << 5) | 14 = 46,
  // each in insertion order. Type: MemberRef 1 as CustomAttributeType =
  // (1 << 3) | 3 = 11.
  EXPECT_EQ(coded(CodedIndexKind::HasCustomAttribute,
                  makeToken(TableId::Assembly, 1)),
            46u);
  std::vector<uint32_t> WantParent, WantIndex, GotParent, GotIndex;
  for (uint32_t I = 1; I < N; I += 2) {
    WantParent.push_back(39);
    WantIndex.push_back(I);
  }
  for (uint32_t I = 0; I < N; I += 2) {
    WantParent.push_back(46);
    WantIndex.push_back(I);
  }
  ASSERT_EQ(V.rows(T::CustomAttribute), N);
  for (uint32_t R = 1; R <= N; ++R) {
    GotParent.push_back(V.get(T::CustomAttribute, R, "Parent"));
    EXPECT_EQ(V.get(T::CustomAttribute, R, "Type"), 11u) << "row " << R;
    ByteVec Value = V.getBlob(T::CustomAttribute, R, "Value");
    uint32_t I = Value.size() == 8 ? Value[2] | uint32_t(Value[3]) << 8 : ~0u;
    GotIndex.push_back(I);
    EXPECT_EQ(Value, ValueBytes(I)) << "row " << R;
  }
  EXPECT_EQ(GotParent, WantParent);
  EXPECT_EQ(GotIndex, WantIndex) << "CustomAttribute ties out of add order";

  // MethodSemantics by Association as HasSemantics (1 tag bit): Property 1 =
  // (1 << 1) | 1 = 3 first, then Property 2 = 5, each in insertion order;
  // Semantics is Other (0x0004) and Method is N - I.
  EXPECT_EQ(
      coded(CodedIndexKind::HasSemantics, makeToken(TableId::Property, 2)), 5u);
  std::vector<uint32_t> WantAssoc, WantMethod, GotAssoc, GotMethod;
  for (uint32_t I = 1; I < N; I += 2) {
    WantAssoc.push_back(3);
    WantMethod.push_back(N - I);
  }
  for (uint32_t I = 0; I < N; I += 2) {
    WantAssoc.push_back(5);
    WantMethod.push_back(N - I);
  }
  ASSERT_EQ(V.rows(T::MethodSemantics), N);
  for (uint32_t R = 1; R <= N; ++R) {
    EXPECT_EQ(V.get(T::MethodSemantics, R, "Semantics"), 0x0004u)
        << "row " << R;
    GotAssoc.push_back(V.get(T::MethodSemantics, R, "Association"));
    GotMethod.push_back(V.get(T::MethodSemantics, R, "Method"));
  }
  EXPECT_EQ(GotAssoc, WantAssoc);
  EXPECT_EQ(GotMethod, WantMethod) << "MethodSemantics ties out of add order";
}

//===----------------------------------------------------------------------===//
// Stable tables (5.8.3 R6) and heaps
//===----------------------------------------------------------------------===//

TEST(CLIMetadataBuilderTest, StableTablesNumberInFirstAddOrder) {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  auto Ref = [&](StringRef Name, AssemblyVersion Ver = {}) {
    AssemblyRefInfo R;
    R.Name = Name.str();
    R.Version = Ver;
    return MB.addAssemblyRef(R);
  };
  // 5.8.3's scaffolding scopes, in declaration order.
  EntityHandle Extern = Ref("msil.extern"), Bcl = Ref("msil.bcl"),
               Link = Ref("msil.link");
  EXPECT_EQ(Extern.Value, 0x23000001u);
  EXPECT_EQ(Bcl.Value, 0x23000002u);
  EXPECT_EQ(Link.Value, 0x23000003u);
  EXPECT_EQ(Ref("msil.bcl").Value, 0x23000002u);
  EXPECT_EQ(Ref("msil.bcl", {1, 0, 0, 0}).Value, 0x23000004u);

  EntityHandle Ucrt = MB.addModuleRef("ucrtbase");
  EXPECT_EQ(Ucrt.Value, 0x1A000001u);
  EXPECT_EQ(MB.addModuleRef("libc").Value, 0x1A000002u);
  EXPECT_EQ(MB.addModuleRef("ucrtbase").Value, 0x1A000001u);
  EXPECT_EQ(MB.addModuleRef("UCRTBASE").Value, 0x1A000003u); // verbatim

  EntityHandle Symbols = MB.addTypeRef(Extern, "", "<msil>Symbols");
  EntityHandle FnAddr = MB.addTypeRef(Extern, "", "<msil>FnAddr");
  EntityHandle Obj = MB.addTypeRef(Bcl, "System", "Object");
  EXPECT_EQ(Symbols.Value, 0x01000001u);
  EXPECT_EQ(FnAddr.Value, 0x01000002u);
  EXPECT_EQ(Obj.Value, 0x01000003u);
  EXPECT_EQ(MB.addTypeRef(Extern, "", "<msil>Symbols").Value, 0x01000001u);
  EXPECT_EQ(MB.addTypeRef(Bcl, "", "<msil>Symbols").Value, 0x01000004u);
  EXPECT_EQ(MB.addTypeRef(Bcl, "A.B", "C").Value, 0x01000005u);
  EXPECT_EQ(MB.addTypeRef(Bcl, "A", "B.C").Value, 0x01000006u);
  EXPECT_EQ(MB.addTypeRef(Obj, "", "Nested").Value, 0x01000007u);
  EXPECT_EQ(MB.addTypeRef(Ucrt, "", "X").Value, 0x01000008u);

  EntityHandle TS1 = MB.addTypeSpec(addBlob(MB, {0x1D, 0x08}));
  EXPECT_EQ(TS1.Value, 0x1B000001u);
  EXPECT_EQ(MB.addTypeSpec(addBlob(MB, {0x0F, 0x01})).Value, 0x1B000002u);
  EXPECT_EQ(MB.addTypeSpec(addBlob(MB, {0x1D, 0x08})).Value, 0x1B000001u);

  // R3: one MemberRef per (parent, name, signature).
  uint32_t SigA = addBlob(MB, {0x00, 0x01, 0x08, 0x08});
  uint32_t SigB = addBlob(MB, {0x00, 0x02, 0x08, 0x08, 0x08});
  uint32_t SigF = addBlob(MB, {0x06, 0x18});
  EntityHandle MR1 = MB.addMemberRef(Symbols, "f", SigA);
  EXPECT_EQ(MR1.Value, 0x0A000001u);
  EXPECT_EQ(MB.addMemberRef(Symbols, "f", SigB).Value, 0x0A000002u);
  EXPECT_EQ(MB.addMemberRef(FnAddr, "f", SigF).Value, 0x0A000003u);
  EXPECT_EQ(MB.addMemberRef(Symbols, "f", SigA).Value, 0x0A000001u);
  EXPECT_EQ(MB.addMemberRef(Symbols, "g", SigA).Value, 0x0A000004u);
  EXPECT_EQ(MB.addMemberRef(TS1, "Get", SigA).Value, 0x0A000005u);

  EXPECT_EQ(MB.addStandAloneSig(addBlob(MB, {0x07, 0x01, 0x08})).Value,
            0x11000001u);
  EXPECT_EQ(MB.addStandAloneSig(SigA).Value, 0x11000002u);
  EXPECT_EQ(MB.addStandAloneSig(addBlob(MB, {0x07, 0x01, 0x08})).Value,
            0x11000001u);

  uint32_t Inst1 = addBlob(MB, {0x0A, 0x01, 0x08});
  uint32_t Inst2 = addBlob(MB, {0x0A, 0x01, 0x18});
  EntityHandle MS1 = MB.addMethodSpec(MR1, Inst1);
  EXPECT_EQ(MS1.Value, 0x2B000001u);
  EXPECT_EQ(MB.addMethodSpec(MR1, Inst2).Value, 0x2B000002u);
  EXPECT_EQ(MB.addMethodSpec(MR1, Inst1).Value, 0x2B000001u);

  // TypeDef rows are never merged.
  EntityHandle T1 = MB.addTypeDef(TypeAttributes::Public, "", "T", Obj);
  EntityHandle T2 = MB.addTypeDef(TypeAttributes::Public, "N", "T", Obj);
  EntityHandle T3 = MB.addTypeDef(TypeAttributes::Public, "", "T", Obj);
  EXPECT_EQ(T1.Value, 0x02000002u);
  EXPECT_EQ(T2.Value, 0x02000003u);
  EXPECT_EQ(T3.Value, 0x02000004u);

  for (EntityHandle H : {Extern, Link, Ucrt, Symbols, Obj, TS1, MR1, MS1, T1})
    EXPECT_EQ(MB.getToken(H), H.Value);
  EXPECT_THAT_EXPECTED(MB.resolveToken(TokenRef::getHandle(Obj)),
                       HasValue(0x01000003u));
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  for (EntityHandle H : {Extern, Link, Ucrt, Symbols, Obj, TS1, MR1, MS1, T3})
    EXPECT_EQ(MB.getToken(H), H.Value);

  MetadataView V(serializeMetadata(MB));
  ASSERT_TRUE(V.ok());
  checkLayout(V, MB);
  using T = TableId;
  EXPECT_EQ(V.rows(T::AssemblyRef), 4u);
  EXPECT_EQ(V.rows(T::ModuleRef), 3u);
  EXPECT_EQ(V.rows(T::TypeRef), 8u);
  EXPECT_EQ(V.rows(T::TypeSpec), 2u);
  EXPECT_EQ(V.rows(T::MemberRef), 5u);
  EXPECT_EQ(V.rows(T::StandAloneSig), 2u);
  EXPECT_EQ(V.rows(T::MethodSpec), 2u);
  EXPECT_EQ(V.rows(T::TypeDef), 4u);
  const char *RefNames[] = {"msil.extern", "msil.bcl", "msil.link", "msil.bcl"};
  for (uint32_t R = 1; R <= 4; ++R)
    EXPECT_EQ(V.getStr(T::AssemblyRef, R, "Name"), RefNames[R - 1]);
  EXPECT_EQ(V.get(T::AssemblyRef, 4, "MajorVersion"), 1u);
  const char *ModNames[] = {"ucrtbase", "libc", "UCRTBASE"};
  for (uint32_t R = 1; R <= 3; ++R)
    EXPECT_EQ(V.getStr(T::ModuleRef, R, "Name"), ModNames[R - 1]);
  // TypeRef: ResolutionScope is AssemblyRef n = (n << 2) | 2, TypeRef 3 =
  // (3 << 2) | 3, ModuleRef 1 = (1 << 2) | 1.
  const struct {
    uint32_t Scope;
    const char *Namespace, *Name;
  } TypeRefs[] = {{6, "", "<msil>Symbols"}, {6, "", "<msil>FnAddr"},
                  {10, "System", "Object"}, {10, "", "<msil>Symbols"},
                  {10, "A.B", "C"},         {10, "A", "B.C"},
                  {15, "", "Nested"},       {5, "", "X"}};
  for (uint32_t R = 1; R <= 8; ++R) {
    SCOPED_TRACE(R);
    EXPECT_EQ(V.get(T::TypeRef, R, "ResolutionScope"), TypeRefs[R - 1].Scope);
    EXPECT_EQ(V.getStr(T::TypeRef, R, "TypeNamespace"),
              TypeRefs[R - 1].Namespace);
    EXPECT_EQ(V.getStr(T::TypeRef, R, "TypeName"), TypeRefs[R - 1].Name);
  }
  EXPECT_EQ(V.getBlob(T::TypeSpec, 1, "Signature"), (ByteVec{0x1D, 0x08}));
  EXPECT_EQ(V.getBlob(T::TypeSpec, 2, "Signature"), (ByteVec{0x0F, 0x01}));
  // MemberRef: Class is TypeRef n = (n << 3) | 1, TypeSpec 1 = (1 << 3) | 4.
  const struct {
    uint32_t Class;
    const char *Name;
    ByteVec Sig;
  } MemberRefs[] = {{9, "f", {0x00, 0x01, 0x08, 0x08}},
                    {9, "f", {0x00, 0x02, 0x08, 0x08, 0x08}},
                    {17, "f", {0x06, 0x18}},
                    {9, "g", {0x00, 0x01, 0x08, 0x08}},
                    {12, "Get", {0x00, 0x01, 0x08, 0x08}}};
  for (uint32_t R = 1; R <= 5; ++R) {
    SCOPED_TRACE(R);
    EXPECT_EQ(V.get(T::MemberRef, R, "Class"), MemberRefs[R - 1].Class);
    EXPECT_EQ(V.getStr(T::MemberRef, R, "Name"), MemberRefs[R - 1].Name);
    EXPECT_EQ(V.getBlob(T::MemberRef, R, "Signature"), MemberRefs[R - 1].Sig);
  }
  EXPECT_EQ(V.getBlob(T::StandAloneSig, 1, "Signature"),
            (ByteVec{0x07, 0x01, 0x08}));
  EXPECT_EQ(V.getBlob(T::StandAloneSig, 2, "Signature"),
            (ByteVec{0x00, 0x01, 0x08, 0x08}));
  // MethodSpec: Method is MemberRef 1 as MethodDefOrRef = (1 << 1) | 1.
  EXPECT_EQ(V.get(T::MethodSpec, 1, "Method"), 3u);
  EXPECT_EQ(V.getBlob(T::MethodSpec, 1, "Instantiation"),
            (ByteVec{0x0A, 0x01, 0x08}));
  EXPECT_EQ(V.get(T::MethodSpec, 2, "Method"), 3u);
  EXPECT_EQ(V.getBlob(T::MethodSpec, 2, "Instantiation"),
            (ByteVec{0x0A, 0x01, 0x18}));
  EXPECT_EQ(V.getStr(T::TypeDef, 2, "TypeName"), "T");
  EXPECT_EQ(V.getStr(T::TypeDef, 3, "TypeNamespace"), "N");
  EXPECT_EQ(V.getStr(T::TypeDef, 4, "TypeName"), "T");
  EXPECT_EQ(V.get(T::TypeDef, 2, "Extends"),
            coded(CodedIndexKind::TypeDefOrRef, Obj.Value));
}

TEST(CLIMetadataBuilderTest, BlobsAndUserStringsDeduplicate) {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  EXPECT_EQ(MB.addBlob({}), 0u);
  uint32_t B1 = addBlob(MB, {0x06, 0x08});
  uint32_t B2 = addBlob(MB, {0x06, 0x18});
  EXPECT_EQ(B1, 1u);
  EXPECT_EQ(B2, 4u);
  EXPECT_EQ(addBlob(MB, {0x06, 0x08}), B1);
  uint32_t Hi = MB.addUserString("hi");
  uint32_t Ho = MB.addUserString("ho");
  uint32_t Its = MB.addUserString("it's");
  EXPECT_EQ(Hi, 0x70000001u);
  EXPECT_EQ(Ho, 0x70000007u);
  EXPECT_EQ(Its, 0x7000000Du);
  EXPECT_EQ(MB.addUserString("hi"), Hi);
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  EXPECT_THAT_EXPECTED(MB.resolveToken(TokenRef::getToken(Its)), HasValue(Its));

  MetadataView V(serializeMetadata(MB));
  ASSERT_TRUE(V.ok());
  checkLayout(V, MB);
  // "it's" ends with the trailing byte 1 (U+0027; 5.7.6 as amended).
  EXPECT_EQ(
      V.streamBytes("#US"),
      ArrayRef<uint8_t>({0x00, 0x05, 'h',  0x00, 'i',  0x00, 0x00, 0x05,
                         'h',  0x00, 'o',  0x00, 0x00, 0x09, 'i',  0x00,
                         't',  0x00, '\'', 0x00, 's',  0x00, 0x01, 0x00}));
  EXPECT_EQ(
      V.streamBytes("#Blob"),
      ArrayRef<uint8_t>({0x00, 0x02, 0x06, 0x08, 0x02, 0x06, 0x18, 0x00}));
}

// Names, blobs and identities are copied when they are added.
TEST(CLIMetadataBuilderTest, CopiesItsInputs) {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  {
    std::string Name = "copied.dll";
    MB.setModuleName(Name);
    Name.assign("XXXXXXXXXXXXXXXX");
  }
  EntityHandle Scope;
  {
    AssemblyRefInfo R;
    R.Name = "transient";
    R.PublicKeyOrToken = {1, 2, 3};
    Scope = MB.addAssemblyRef(R);
  }
  EntityHandle Ref;
  uint32_t Sig;
  {
    std::string Ns = "Some.Namespace", N = "Type";
    Ref = MB.addTypeRef(Scope, Ns, N);
    Ns.assign("ZZZZZZZZZZZZZZZZZZZZZZZZZZ");
    N.assign("ZZZZZZZZZZZZZZZZZZZZZZZZZZ");
    ByteVec Bytes = {0x06, 0x08};
    Sig = MB.addBlob(Bytes);
    Bytes.assign({0xFF, 0xFF});
  }
  {
    std::string F = "field";
    MB.addField(MB.getModuleType(), FieldAttributes::Public, F, Sig);
    F.assign("ZZZZZZZZZZZZZZZZZZZZZZZZZZ");
  }
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  MetadataView V(serializeMetadata(MB));
  ASSERT_TRUE(V.ok());
  checkLayout(V, MB);
  EXPECT_EQ(V.getStr(TableId::Module, 1, "Name"), "copied.dll");
  EXPECT_EQ(V.getStr(TableId::AssemblyRef, 1, "Name"), "transient");
  EXPECT_EQ(V.getBlob(TableId::AssemblyRef, 1, "PublicKeyOrToken"),
            (ByteVec{1, 2, 3}));
  EXPECT_EQ(V.getStr(TableId::TypeRef, 1, "TypeNamespace"), "Some.Namespace");
  EXPECT_EQ(V.getStr(TableId::TypeRef, 1, "TypeName"), "Type");
  EXPECT_EQ(V.getStr(TableId::Field, 1, "Name"), "field");
  EXPECT_EQ(V.getBlob(TableId::Field, 1, "Signature"), (ByteVec{0x06, 0x08}));
  EXPECT_EQ(Ref.Value, 0x01000001u);
}

TEST(CLIMetadataBuilderTest, AssemblyIdentityRows) {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  AssemblyIdentity Id;
  Id.Name = "lib";
  Id.Version = {1, 2, 3, 4};
  Id.Flags = AssemblyFlags::PublicKey;
  Id.HashAlg = AssemblyHashAlgorithm::None;
  Id.PublicKey = {0x00, 0x24, 0x00, 0x00, 0x04, 0x80};
  Id.Culture = "en-US";
  MB.setAssembly(Id);
  EXPECT_TRUE(MB.hasAssembly());
  AssemblyRefInfo R;
  R.Name = "System.Runtime";
  R.Version = {8, 0, 0, 0};
  R.Flags = AssemblyFlags::Retargetable;
  R.PublicKeyOrToken = {0xB0, 0x3F, 0x5F, 0x7F, 0x11, 0xD5, 0x0A, 0x3A};
  R.Culture = "de";
  R.HashValue = {0xAA, 0xBB};
  MB.addAssemblyRef(R);
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  MetadataView V(serializeMetadata(MB));
  ASSERT_TRUE(V.ok());
  checkLayout(V, MB);
  using T = TableId;
  EXPECT_EQ(V.rows(T::Assembly), 1u);
  EXPECT_EQ(V.get(T::Assembly, 1, "HashAlgId"), 0u);
  EXPECT_EQ(V.get(T::Assembly, 1, "MajorVersion"), 1u);
  EXPECT_EQ(V.get(T::Assembly, 1, "MinorVersion"), 2u);
  EXPECT_EQ(V.get(T::Assembly, 1, "BuildNumber"), 3u);
  EXPECT_EQ(V.get(T::Assembly, 1, "RevisionNumber"), 4u);
  EXPECT_EQ(V.get(T::Assembly, 1, "Flags"), 1u);
  EXPECT_EQ(V.getBlob(T::Assembly, 1, "PublicKey"),
            (ByteVec{0x00, 0x24, 0x00, 0x00, 0x04, 0x80}));
  EXPECT_EQ(V.getStr(T::Assembly, 1, "Name"), "lib");
  EXPECT_EQ(V.getStr(T::Assembly, 1, "Culture"), "en-US");
  EXPECT_EQ(V.get(T::AssemblyRef, 1, "MajorVersion"), 8u);
  EXPECT_EQ(V.get(T::AssemblyRef, 1, "Flags"), 0x100u);
  EXPECT_EQ(V.getBlob(T::AssemblyRef, 1, "PublicKeyOrToken"),
            (ByteVec{0xB0, 0x3F, 0x5F, 0x7F, 0x11, 0xD5, 0x0A, 0x3A}));
  EXPECT_EQ(V.getStr(T::AssemblyRef, 1, "Name"), "System.Runtime");
  EXPECT_EQ(V.getStr(T::AssemblyRef, 1, "Culture"), "de");
  EXPECT_EQ(V.getBlob(T::AssemblyRef, 1, "HashValue"), (ByteVec{0xAA, 0xBB}));
}

TEST(CLIMetadataBuilderTest, ResolveToken) {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  EntityHandle Obj = addObjectTypeRef(MB);
  EntityHandle Holder = addHolder(MB);
  EntityHandle M =
      MB.addMethod(Holder, MethodAttributes::Public | MethodAttributes::Static,
                   MethodImplAttributes::IL, "m", addBlob(MB, {0, 0, 1}));
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  EXPECT_THAT_EXPECTED(MB.resolveToken(TokenRef::getToken(0x0A000123)),
                       HasValue(0x0A000123u));
  EXPECT_THAT_EXPECTED(MB.resolveToken(TokenRef::getHandle(Obj)),
                       HasValue(Obj.Value));
  EXPECT_THAT_EXPECTED(MB.resolveToken(TokenRef::getHandle(Holder)),
                       HasValue(0x02000002u));
  EXPECT_THAT_EXPECTED(MB.resolveToken(TokenRef::getHandle(M)),
                       HasValue(0x06000001u));
  EXPECT_EQ(MB.getToken(M), 0x06000001u);
  EXPECT_THAT_EXPECTED(MB.resolveToken(TokenRef::getExternal(7)), Failed());
}

//===----------------------------------------------------------------------===//
// Method bodies
//===----------------------------------------------------------------------===//

// finalize() does not check body offsets; checkMethodBodies() names the first
// IL method that is not abstract, pinvokeimpl or runtime and has no body.
TEST(CLIMetadataBuilderTest, CheckMethodBodies) {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  EntityHandle Funcs = addHolder(MB, "Funcs");
  uint32_t Sig = addBlob(MB, {0x00, 0x00, 0x01});
  auto Static = MethodAttributes::Public | MethodAttributes::Static;
  EntityHandle A =
      MB.addMethod(Funcs, Static, MethodImplAttributes::IL, "a", Sig);
  EntityHandle Abs =
      MB.addMethod(Funcs,
                   MethodAttributes::Public | MethodAttributes::Virtual |
                       MethodAttributes::Abstract | MethodAttributes::HideBySig,
                   MethodImplAttributes::IL, "abs", Sig);
  EntityHandle PInv =
      MB.addMethod(Funcs, Static | MethodAttributes::PinvokeImpl,
                   MethodImplAttributes::PreserveSig, "pinv", Sig);
  EntityHandle Rt =
      MB.addMethod(Funcs, Static, MethodImplAttributes::Runtime, "rt", Sig);
  EntityHandle B =
      MB.addMethod(Funcs, Static, MethodImplAttributes::IL, "b", Sig);
  EntityHandle C =
      MB.addMethod(Funcs, Static, MethodImplAttributes::IL, "c", Sig);
  ByteVec Ret = {0x2A}, Ret0 = {0x16, 0x2A}, Ret1 = {0x17, 0x2A};
  MethodBodyInfo Info;
  Info.Code = Ret;
  uint32_t OffC = Bodies.addBody(Info);
  MB.setMethodBodyOffset(C, OffC);
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());

  EXPECT_EQ(failureText(MB.checkMethodBodies()),
            std::string(Prefix) + "method 'Funcs::a' has no body");
  Info.Code = Ret0;
  uint32_t OffA = Bodies.addBody(Info);
  MB.setMethodBodyOffset(A, OffA);
  EXPECT_EQ(failureText(MB.checkMethodBodies()),
            std::string(Prefix) + "method 'Funcs::b' has no body");
  Info.Code = Ret1;
  uint32_t OffB = Bodies.addBody(Info);
  MB.setMethodBodyOffset(B, OffB);
  EXPECT_THAT_ERROR(MB.checkMethodBodies(), Succeeded());

  MetadataView V(serializeMetadata(MB));
  ASSERT_TRUE(V.ok());
  checkLayout(V, MB);
  using T = TableId;
  EXPECT_EQ(V.get(T::MethodDef, 1, "RVA"), ILRVA + OffA);
  EXPECT_EQ(V.get(T::MethodDef, 2, "RVA"), 0u);
  EXPECT_EQ(V.get(T::MethodDef, 3, "RVA"), 0u);
  EXPECT_EQ(V.get(T::MethodDef, 4, "RVA"), 0u);
  EXPECT_EQ(V.get(T::MethodDef, 5, "RVA"), ILRVA + OffB);
  EXPECT_EQ(V.get(T::MethodDef, 6, "RVA"), ILRVA + OffC);
  EXPECT_EQ(MB.getToken(Abs), 0x06000002u);
  EXPECT_EQ(MB.getToken(PInv), 0x06000003u);
  EXPECT_EQ(MB.getToken(Rt), 0x06000004u);
  EXPECT_EQ(V.get(T::MethodDef, 4, "ImplFlags"), 0x0003u);
}

//===----------------------------------------------------------------------===//
// The 0xFFFF padding rule (5.7.6, metadata-writer 3.6)
//===----------------------------------------------------------------------===//

constexpr uint32_t OnModule = 65000;

TEST(CLIMetadataBuilderTest, FieldPadding) {
  for (uint32_t Count : {0xFFFEu, 0xFFFFu, 0x10000u}) {
    SCOPED_TRACE(Count);
    MetadataBuilder MB;
    MethodBodyStream Bodies;
    EntityHandle Holder = addHolder(MB);
    uint32_t Sig = addBlob(MB, {0x06, 0x18});
    auto Flags = FieldAttributes::Public | FieldAttributes::Static;
    for (uint32_t I = 0; I < OnModule; ++I)
      MB.addField(MB.getModuleType(), Flags, "f", Sig);
    for (uint32_t I = OnModule; I < Count; ++I)
      MB.addField(Holder, Flags, "h", Sig);
    ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
    const bool Padded = Count == 0xFFFF;
    const uint32_t Rows = Padded ? 0x10000 : Count;
    EXPECT_EQ(MB.getRowCount(TableId::Field), Rows);
    EXPECT_EQ(MB.getRowCount(TableId::MethodDef), 0u);
    EXPECT_EQ(MB.getRowCount(TableId::Param), 0u);
    EXPECT_EQ(Bodies.size(), 0u);

    MetadataView V(serializeMetadata(MB));
    ASSERT_TRUE(V.ok());
    checkLayout(V, MB);
    using T = TableId;
    EXPECT_EQ(V.rows(T::Field), Rows);
    EXPECT_EQ(V.width(T::TypeDef, "FieldList"), Rows >= 0x10000 ? 4u : 2u);
    EXPECT_EQ(V.get(T::TypeDef, 2, "FieldList"), OnModule + 1);
    EXPECT_EQ(V.getStr(T::Field, OnModule, "Name"), "f");
    EXPECT_EQ(V.getStr(T::Field, OnModule + 1, "Name"), "h");
    EXPECT_EQ(V.getStr(T::Field, Count, "Name"), "h");
    // The inert row goes last, in the last TypeDef's run.
    EXPECT_EQ(V.getStr(T::Field, Rows, "Name"), Padded ? "<msil>pad" : "h");
    EXPECT_EQ(V.get(T::Field, Rows, "Flags"), Padded ? 0x0011u : 0x0016u);
    EXPECT_EQ(V.getBlob(T::Field, Rows, "Signature"),
              Padded ? ByteVec({0x06, 0x08}) : ByteVec({0x06, 0x18}));
  }
}

TEST(CLIMetadataBuilderTest, MethodDefPadding) {
  for (uint32_t Count : {0xFFFEu, 0xFFFFu, 0x10000u}) {
    SCOPED_TRACE(Count);
    MetadataBuilder MB;
    MethodBodyStream Bodies;
    EntityHandle Holder = addHolder(MB);
    uint32_t Sig = addBlob(MB, {0x00, 0x00, 0x08});
    // One shared body: ldc.i4.0; ret (tiny, 3 bytes).
    ByteVec Code = {0x16, 0x2A};
    MethodBodyInfo Info;
    Info.Code = Code;
    uint32_t Shared = Bodies.addBody(Info);
    const size_t Before = Bodies.size();
    auto Flags = MethodAttributes::Public | MethodAttributes::Static |
                 MethodAttributes::HideBySig;
    for (uint32_t I = 0; I < Count; ++I) {
      EntityHandle M =
          MB.addMethod(I < OnModule ? MB.getModuleType() : Holder, Flags,
                       MethodImplAttributes::IL, I < OnModule ? "m" : "h", Sig);
      MB.setMethodBodyOffset(M, Shared);
    }
    ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
    EXPECT_THAT_ERROR(MB.checkMethodBodies(), Succeeded());
    const bool Padded = Count == 0xFFFF;
    const uint32_t Rows = Padded ? 0x10000 : Count;
    EXPECT_EQ(MB.getRowCount(TableId::MethodDef), Rows);
    EXPECT_EQ(MB.getRowCount(TableId::Param), 0u);
    // The pad method's body is a tiny ret added to the stream (unaligned).
    ASSERT_EQ(Bodies.size(), Padded ? Before + 2 : Before);
    if (Padded)
      EXPECT_EQ(Bodies.getBytes().slice(Before),
                ArrayRef<uint8_t>({0x06, 0x2A}));

    MetadataView V(serializeMetadata(MB));
    ASSERT_TRUE(V.ok());
    checkLayout(V, MB);
    using T = TableId;
    EXPECT_EQ(V.rows(T::MethodDef), Rows);
    EXPECT_EQ(V.width(T::TypeDef, "MethodList"), Rows >= 0x10000 ? 4u : 2u);
    EXPECT_EQ(V.get(T::TypeDef, 2, "MethodList"), OnModule + 1);
    EXPECT_EQ(V.getStr(T::MethodDef, OnModule, "Name"), "m");
    EXPECT_EQ(V.getStr(T::MethodDef, Count, "Name"), "h");
    EXPECT_EQ(V.get(T::MethodDef, Count, "RVA"), ILRVA + Shared);
    EXPECT_EQ(V.get(T::MethodDef, Rows, "ParamList"), 1u);
    if (Padded) {
      EXPECT_EQ(V.getStr(T::MethodDef, Rows, "Name"), "<msil>pad");
      EXPECT_EQ(V.get(T::MethodDef, Rows, "Flags"), 0x0091u);
      EXPECT_EQ(V.get(T::MethodDef, Rows, "ImplFlags"), 0u);
      EXPECT_EQ(V.getBlob(T::MethodDef, Rows, "Signature"),
                (ByteVec{0x00, 0x00, 0x01}));
      EXPECT_EQ(V.get(T::MethodDef, Rows, "RVA"), ILRVA + Before);
    }
  }
}

TEST(CLIMetadataBuilderTest, ParamPadding) {
  for (uint32_t Count : {0xFFFEu, 0xFFFFu, 0x10000u}) {
    SCOPED_TRACE(Count);
    MetadataBuilder MB;
    MethodBodyStream Bodies;
    addHolder(MB); // the last TypeDef, which gets the pad method
    uint32_t Sig = addBlob(MB, {0x00, 0x00, 0x01});
    // 257 methods on <Module>: 256 with 255 parameters, the last with the
    // rest (254, 255 or 256).
    for (uint32_t M = 0; M < 257; ++M) {
      EntityHandle H =
          MB.addMethod(MB.getModuleType(),
                       MethodAttributes::Public | MethodAttributes::Static,
                       MethodImplAttributes::IL, "m", Sig);
      uint32_t N = M < 256 ? 255 : Count - 256 * 255;
      for (uint32_t S = 1; S <= N; ++S)
        MB.addParam(H, static_cast<uint16_t>(S), ParamAttributes::None, "p");
    }
    ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
    const bool Padded = Count == 0xFFFF;
    const uint32_t Params = Padded ? 0x10000 : Count;
    const uint32_t Methods = Padded ? 258 : 257;
    EXPECT_EQ(MB.getRowCount(TableId::Param), Params);
    EXPECT_EQ(MB.getRowCount(TableId::MethodDef), Methods);
    EXPECT_EQ(Bodies.size(), Padded ? 2u : 0u);

    MetadataView V(serializeMetadata(MB));
    ASSERT_TRUE(V.ok());
    checkLayout(V, MB);
    using T = TableId;
    EXPECT_EQ(V.rows(T::Param), Params);
    EXPECT_EQ(V.width(T::MethodDef, "ParamList"), Params >= 0x10000 ? 4u : 2u);
    // Holder (the last TypeDef) owns the pad method, or nothing.
    EXPECT_EQ(V.get(T::TypeDef, 2, "MethodList"), 258u);
    EXPECT_EQ(V.get(T::MethodDef, 257, "ParamList"), 256u * 255 + 1);
    EXPECT_EQ(V.get(T::Param, Count, "Sequence"), Count - 256u * 255);
    if (Padded) {
      EXPECT_EQ(V.getStr(T::MethodDef, 258, "Name"), "<msil>pad");
      EXPECT_EQ(V.get(T::MethodDef, 258, "Flags"), 0x0091u);
      EXPECT_EQ(V.get(T::MethodDef, 258, "ParamList"), 0x10000u);
      EXPECT_EQ(V.get(T::MethodDef, 258, "RVA"), ILRVA + 0);
      EXPECT_EQ(V.get(T::Param, 0x10000, "Flags"), 0u);
      EXPECT_EQ(V.get(T::Param, 0x10000, "Sequence"), 0u);
      EXPECT_EQ(V.get(T::Param, 0x10000, "Name"), 0u);
      EXPECT_EQ(Bodies.getBytes(), ArrayRef<uint8_t>({0x06, 0x2A}));
    }
  }
}

// A pad method can itself make MethodDef 0xFFFF rows: finalize() re-checks
// until no table has exactly 0xFFFF rows.
TEST(CLIMetadataBuilderTest, PaddingCascades) {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  EntityHandle Holder = addHolder(MB);
  uint32_t Sig = addBlob(MB, {0x00, 0x00, 0x01});
  auto Flags = MethodAttributes::Public | MethodAttributes::Static;
  // 0xFFFE methods (65000 on <Module>, 534 on Holder); 0xFFFF parameters on
  // the first 257 methods.
  for (uint32_t I = 0; I < 0xFFFE; ++I) {
    EntityHandle M =
        MB.addMethod(I < OnModule ? MB.getModuleType() : Holder, Flags,
                     MethodImplAttributes::IL, I < OnModule ? "m" : "h", Sig);
    if (I < 257)
      for (uint32_t S = 1; S <= 255; ++S)
        MB.addParam(M, static_cast<uint16_t>(S), ParamAttributes::None, "");
  }
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  EXPECT_EQ(MB.getRowCount(TableId::Param), 0x10000u);
  EXPECT_EQ(MB.getRowCount(TableId::MethodDef), 0x10000u);
  EXPECT_EQ(MB.getRowCount(TableId::Field), 0u);

  MetadataView V(serializeMetadata(MB));
  ASSERT_TRUE(V.ok());
  checkLayout(V, MB);
  using T = TableId;
  EXPECT_EQ(V.get(T::TypeDef, 2, "MethodList"), OnModule + 1);
  EXPECT_EQ(V.getStr(T::MethodDef, 0xFFFE, "Name"), "h");
  EXPECT_EQ(V.getStr(T::MethodDef, 0xFFFF, "Name"), "<msil>pad");
  EXPECT_EQ(V.getStr(T::MethodDef, 0x10000, "Name"), "<msil>pad");
  EXPECT_EQ(V.get(T::Param, 0x10000, "Sequence"), 0u);
  // The pad parameter belongs to a pad method.
  uint32_t Owner = 0;
  for (uint32_t R = 1; R <= V.rows(T::MethodDef); ++R)
    if (V.get(T::MethodDef, R, "ParamList") <= 0x10000)
      Owner = R;
  ASSERT_NE(Owner, 0u);
  EXPECT_EQ(V.getStr(T::MethodDef, Owner, "Name"), "<msil>pad");
}

//===----------------------------------------------------------------------===//
// Content checks (5.7.6, 5.7.12)
//===----------------------------------------------------------------------===//

// Adds Count methods to a TypeDef 'Big'; with Before > 0, <Module> gets
// Before methods and Big is the last TypeDef (it owns the pad rows).
Error buildWithMethods(uint32_t Before, uint32_t Count) {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  EntityHandle Big = addHolder(MB, "Big");
  uint32_t Sig = addBlob(MB, {0x00, 0x00, 0x01});
  auto Flags = MethodAttributes::Public | MethodAttributes::Static;
  for (uint32_t I = 0; I < Before; ++I)
    MB.addMethod(MB.getModuleType(), Flags, MethodImplAttributes::IL, "m", Sig);
  for (uint32_t I = 0; I < Count; ++I)
    MB.addMethod(Big, Flags, MethodImplAttributes::IL, "b", Sig);
  return MB.finalize(Bodies);
}

TEST(CLIMetadataBuilderTest, MethodsPerTypeLimit) {
  EXPECT_THAT_ERROR(buildWithMethods(0, MaxMethodsPerType), Succeeded());
  std::string Msg = failureText(buildWithMethods(0, MaxMethodsPerType + 1));
  EXPECT_THAT(Msg, StartsWith(Prefix));
  EXPECT_THAT(Msg, HasSubstr("Big"));
}

// Pad rows count: 11 + 65524 methods make MethodDef 0xFFFF rows, and the pad
// method gives the last TypeDef 65525.
TEST(CLIMetadataBuilderTest, PadRowsCountTowardsTheLimit) {
  ASSERT_EQ(11 + MaxMethodsPerType, 0xFFFFu);
  std::string Msg = failureText(buildWithMethods(11, MaxMethodsPerType));
  EXPECT_THAT(Msg, StartsWith(Prefix));
  EXPECT_THAT(Msg, HasSubstr("Big"));
  EXPECT_THAT_ERROR(buildWithMethods(10, MaxMethodsPerType), Succeeded());
}

TEST(CLIMetadataBuilderTest, FieldsPerTypeLimit) {
  for (uint32_t Count : {MaxFieldsPerType, MaxFieldsPerType + 1}) {
    SCOPED_TRACE(Count);
    MetadataBuilder MB;
    MethodBodyStream Bodies;
    EntityHandle Big = addHolder(MB, "Big");
    addHolder(MB); // the last TypeDef, which gets any pad row
    uint32_t Sig = addBlob(MB, {0x06, 0x08});
    for (uint32_t I = 0; I < Count; ++I)
      MB.addField(Big, FieldAttributes::Public | FieldAttributes::Static, "f",
                  Sig);
    Error E = MB.finalize(Bodies);
    if (Count == MaxFieldsPerType) {
      EXPECT_THAT_ERROR(std::move(E), Succeeded());
      continue;
    }
    std::string Msg = failureText(std::move(E));
    EXPECT_THAT(Msg, StartsWith(Prefix));
    EXPECT_THAT(Msg, HasSubstr("Big"));
  }
}

// A token holds a row number in 24 bits, so a table past MaxTableRows is an
// Error that names the table (5.7.6). The rows are the cheapest there are:
// unnamed Param rows (sequence 0, flags None) of one method on <Module>.
// One builder per run, each under a second and about 400 MB at its peak.
TEST(CLIMetadataBuilderTest, TableRowsLimit) {
  ASSERT_EQ(MaxTableRows, 0xFFFFFFu);
  for (uint32_t Count : {MaxTableRows, MaxTableRows + 1}) {
    SCOPED_TRACE(Count);
    MetadataBuilder MB;
    MethodBodyStream Bodies;
    EntityHandle M = MB.addMethod(
        MB.getModuleType(), MethodAttributes::Public | MethodAttributes::Static,
        MethodImplAttributes::IL, "m", addBlob(MB, {0x00, 0x00, 0x01}));
    for (uint32_t I = 0; I < Count; ++I)
      MB.addParam(M, 0, ParamAttributes::None, "");
    Error E = MB.finalize(Bodies);
    if (Count == MaxTableRows) {
      ASSERT_THAT_ERROR(std::move(E), Succeeded());
      EXPECT_EQ(MB.getRowCount(TableId::Param), MaxTableRows);
      EXPECT_EQ(MB.getRowCount(TableId::MethodDef), 1u);
      continue;
    }
    std::string Msg = failureText(std::move(E));
    EXPECT_THAT(Msg, StartsWith(Prefix));
    EXPECT_THAT(Msg, HasSubstr("Param"));
  }
}

std::string longName(size_t Length) {
  std::string S = "longname";
  S.resize(Length, 'x');
  return S;
}

// Add(MB, N) adds one entity whose counted name length is N: the limit
// (MaxIdentifierBytes) is accepted and one byte more is an Error that names
// the entity.
template <typename AddFn> void expectNameLimit(AddFn Add) {
  {
    MetadataBuilder MB;
    MethodBodyStream Bodies;
    Add(MB, MaxIdentifierBytes);
    EXPECT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  }
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  Add(MB, MaxIdentifierBytes + 1);
  std::string Msg = failureText(MB.finalize(Bodies));
  EXPECT_THAT(Msg, StartsWith(Prefix));
  EXPECT_THAT(Msg, HasSubstr("longname"));
}

TEST(CLIMetadataBuilderTest, IdentifierLengthLimit) {
  {
    SCOPED_TRACE("TypeDef Namespace.Name");
    expectNameLimit([](MetadataBuilder &MB, size_t N) {
      MB.addTypeDef(TypeAttributes::Public, "Ns", longName(N - 3),
                    addObjectTypeRef(MB));
    });
  }
  {
    SCOPED_TRACE("TypeDef long namespace");
    expectNameLimit([](MetadataBuilder &MB, size_t N) {
      MB.addTypeDef(TypeAttributes::Public, longName(N - 2), "T",
                    addObjectTypeRef(MB));
    });
  }
  {
    SCOPED_TRACE("TypeDef without a namespace");
    expectNameLimit([](MetadataBuilder &MB, size_t N) {
      MB.addTypeDef(TypeAttributes::Public, "", longName(N),
                    addObjectTypeRef(MB));
    });
  }
  {
    SCOPED_TRACE("TypeRef Namespace.Name");
    expectNameLimit([](MetadataBuilder &MB, size_t N) {
      AssemblyRefInfo R;
      R.Name = "mscorlib";
      MB.addTypeRef(MB.addAssemblyRef(R), "Ns", longName(N - 3));
    });
  }
  {
    SCOPED_TRACE("Field");
    expectNameLimit([](MetadataBuilder &MB, size_t N) {
      MB.addField(MB.getModuleType(),
                  FieldAttributes::Public | FieldAttributes::Static,
                  longName(N), addBlob(MB, {0x06, 0x08}));
    });
  }
  {
    SCOPED_TRACE("MethodDef");
    expectNameLimit([](MetadataBuilder &MB, size_t N) {
      MB.addMethod(MB.getModuleType(),
                   MethodAttributes::Public | MethodAttributes::Static,
                   MethodImplAttributes::IL, longName(N),
                   addBlob(MB, {0x00, 0x00, 0x01}));
    });
  }
  {
    SCOPED_TRACE("MemberRef");
    expectNameLimit([](MetadataBuilder &MB, size_t N) {
      MB.addMemberRef(addObjectTypeRef(MB), longName(N),
                      addBlob(MB, {0x00, 0x00, 0x01}));
    });
  }
}

// #Strings entries are NUL-terminated, so no name may contain a NUL byte.
TEST(CLIMetadataBuilderTest, NamesWithNulAreErrors) {
  const StringRef Bad("Bad\0name", 8);
  // The message names the entity: its name up to the NUL is "Bad" (a
  // parameter may be named through its method 'm' instead).
  auto Expect = [&](const char *What, auto Add) {
    SCOPED_TRACE(What);
    MetadataBuilder MB;
    MethodBodyStream Bodies;
    Add(MB);
    std::string Msg = failureText(MB.finalize(Bodies));
    EXPECT_THAT(Msg, StartsWith(Prefix));
    if (StringRef(What) == "Param")
      EXPECT_TRUE(StringRef(Msg).contains("Bad") ||
                  StringRef(Msg).contains("'m'"))
          << Msg;
    else
      EXPECT_THAT(Msg, HasSubstr("Bad"));
  };
  Expect("TypeDef name", [&](MetadataBuilder &MB) {
    MB.addTypeDef(TypeAttributes::Public, "", Bad, addObjectTypeRef(MB));
  });
  Expect("TypeDef namespace", [&](MetadataBuilder &MB) {
    MB.addTypeDef(TypeAttributes::Public, Bad, "T", addObjectTypeRef(MB));
  });
  Expect("TypeRef name", [&](MetadataBuilder &MB) {
    AssemblyRefInfo R;
    R.Name = "mscorlib";
    MB.addTypeRef(MB.addAssemblyRef(R), "System", Bad);
  });
  Expect("TypeRef namespace", [&](MetadataBuilder &MB) {
    AssemblyRefInfo R;
    R.Name = "mscorlib";
    MB.addTypeRef(MB.addAssemblyRef(R), Bad, "Object");
  });
  Expect("Field", [&](MetadataBuilder &MB) {
    MB.addField(MB.getModuleType(), FieldAttributes::Static, Bad,
                addBlob(MB, {0x06, 0x08}));
  });
  Expect("MethodDef", [&](MetadataBuilder &MB) {
    MB.addMethod(MB.getModuleType(), MethodAttributes::Static,
                 MethodImplAttributes::IL, Bad,
                 addBlob(MB, {0x00, 0x00, 0x01}));
  });
  Expect("Param", [&](MetadataBuilder &MB) {
    EntityHandle M = MB.addMethod(MB.getModuleType(), MethodAttributes::Static,
                                  MethodImplAttributes::IL, "m",
                                  addBlob(MB, {0x00, 0x01, 0x01, 0x08}));
    MB.addParam(M, 1, ParamAttributes::None, Bad);
  });
  Expect("MemberRef", [&](MetadataBuilder &MB) {
    MB.addMemberRef(addObjectTypeRef(MB), Bad, addBlob(MB, {0x00, 0x00, 0x01}));
  });
  Expect("ModuleRef", [&](MetadataBuilder &MB) { MB.addModuleRef(Bad); });
  Expect("Property", [&](MetadataBuilder &MB) {
    MB.addProperty(addHolder(MB), PropertyAttributes::None, Bad,
                   addBlob(MB, {0x08, 0x00, 0x08}));
  });
}

TEST(CLIMetadataBuilderTest, UserStringErrors) {
  {
    MetadataBuilder MB;
    MethodBodyStream Bodies;
    MB.addUserString("ok");
    MB.addUserString("bad \xFF byte");
    EXPECT_THAT(failureText(MB.finalize(Bodies)), StartsWith(Prefix));
  }
  {
    // 0x800000 UTF-16 code units pass the 24-bit #US limit.
    MetadataBuilder MB;
    MethodBodyStream Bodies;
    MB.addUserString(std::string(0x800000, 'a'));
    EXPECT_THAT(failureText(MB.finalize(Bodies)), StartsWith(Prefix));
  }
}

// finalize() returns the first recorded content check.
TEST(CLIMetadataBuilderTest, FirstContentCheckWins) {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  MB.addField(MB.getModuleType(), FieldAttributes::Static,
              StringRef("first\0f", 7), addBlob(MB, {0x06, 0x08}));
  MB.addMethod(MB.getModuleType(), MethodAttributes::Static,
               MethodImplAttributes::IL, "second" + std::string(1100, 'x'),
               addBlob(MB, {0x00, 0x00, 0x01}));
  std::string Msg = failureText(MB.finalize(Bodies));
  EXPECT_THAT(Msg, StartsWith(Prefix));
  EXPECT_THAT(Msg, HasSubstr("first"));
  EXPECT_THAT(Msg, Not(HasSubstr("second")));
}

//===----------------------------------------------------------------------===//
// Heap index widths (II.24.2.6, 5.7.6)
//===----------------------------------------------------------------------===//

// #Blob indexes become 4 bytes when the stream, padded to 4, reaches 0x10000.
TEST(CLIMetadataBuilderTest, BlobIndexWidth) {
  for (uint32_t Target : {0xFFFCu, 0xFFFDu, 0x10000u}) {
    SCOPED_TRACE(Target);
    MetadataBuilder MB;
    MethodBodyStream Bodies;
    ByteVec Big(Target - 5, 0x5A); // 1 + 4-byte length + content
    uint32_t Off = MB.addBlob(Big);
    EXPECT_EQ(Off, 1u);
    MB.addField(MB.getModuleType(), FieldAttributes::Static, "f", Off);
    ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
    MetadataView V(serializeMetadata(MB));
    ASSERT_TRUE(V.ok());
    checkLayout(V, MB);
    const bool Wide = Target > 0xFFFC;
    EXPECT_EQ(V.stream("#Blob")->Size, alignTo(Target, 4));
    EXPECT_EQ(V.HeapSizes & HEAP_BLOB_4, Wide ? HEAP_BLOB_4 : 0);
    EXPECT_EQ(V.width(TableId::Field, "Signature"), Wide ? 4u : 2u);
    EXPECT_EQ(V.getBlob(TableId::Field, 1, "Signature").size(), Target - 5);
  }
}

// #Strings indexes become 4 bytes when the stream, padded to 4, reaches
// 0x10000. The fill strings go to ModuleRef rows; the base content (the
// module name and '<Module>') is measured first.
TEST(CLIMetadataBuilderTest, StringIndexWidth) {
  uint32_t Base;
  {
    MetadataBuilder MB;
    MethodBodyStream Bodies;
    MB.setModuleName("m");
    ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
    MetadataView V(serializeMetadata(MB));
    ASSERT_TRUE(V.ok());
    ArrayRef<uint8_t> S = V.streamBytes("#Strings");
    size_t Last = S.size();
    while (Last > 0 && S[Last - 1] == 0)
      --Last;
    Base = Last == 0 ? 1 : static_cast<uint32_t>(Last) + 1;
  }
  for (uint32_t Target : {0xFFFCu, 0xFFFDu, 0x10000u}) {
    SCOPED_TRACE(Target);
    MetadataBuilder MB;
    MethodBodyStream Bodies;
    MB.setModuleName("m");
    std::vector<std::string> Fill = makeFillStrings(Base, Target);
    for (const std::string &S : Fill)
      MB.addModuleRef(S);
    ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
    MetadataView V(serializeMetadata(MB));
    ASSERT_TRUE(V.ok());
    checkLayout(V, MB);
    const bool Wide = Target > 0xFFFC;
    EXPECT_EQ(V.stream("#Strings")->Size, alignTo(Target, 4));
    EXPECT_EQ(V.HeapSizes & HEAP_STRINGS_4, Wide ? HEAP_STRINGS_4 : 0);
    EXPECT_EQ(V.width(TableId::ModuleRef, "Name"), Wide ? 4u : 2u);
    EXPECT_EQ(V.Sizes->getRowSize(TableId::Module), Wide ? 12u : 10u);
    EXPECT_EQ(V.getStr(TableId::Module, 1, "Name"), "m");
    ASSERT_EQ(V.rows(TableId::ModuleRef), Fill.size());
    EXPECT_EQ(V.getStr(TableId::ModuleRef, 1, "Name"), Fill.front());
    EXPECT_EQ(V.getStr(TableId::ModuleRef, static_cast<uint32_t>(Fill.size()),
                       "Name"),
              Fill.back());
  }
}

} // namespace
