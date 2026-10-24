//===- PEWriterTest.cpp - ECMA-335 PE image writer ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/PEWriter.h against the MSIL implementation plan: the PE
// envelope (5.7.7), the netmodule shape of objects (5.8.2) and the COFF
// symbol view (5.8.6). Every image is parsed with the test's own PE/COFF and
// metadata-root parser (the PE/COFF specification and ECMA-335 II.24-II.25),
// never with the library's reader. Expected values come from the plan, from
// recon/p2/metadata-writer.md (3.3, 3.10, 4.9, 4.10), from mdump --contentid
// (System.Reflection.Metadata's BlobContentId.FromHash) and from a Roslyn
// netmodule built in scratch (csc 4.11, -deterministic -target:module
// -platform:x64), never from the implementation under test.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/PEWriter.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/CLI/Handles.h"
#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/CLI/TableSchema.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SHA256.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::cli;
using testing::StartsWith;

namespace {

using ByteVec = std::vector<uint8_t>;

constexpr char PEPrefix[] = "cannot write PE image: ";
constexpr char WinTriple[] = "x86_64-pc-windows-msvc-msil";

//===----------------------------------------------------------------------===//
// Little-endian reads with bounds checks
//===----------------------------------------------------------------------===//

bool inBounds(ArrayRef<uint8_t> B, uint64_t Off, uint64_t N) {
  if (Off <= B.size() && N <= B.size() - Off)
    return true;
  ADD_FAILURE() << "read of " << N << " bytes at 0x" << utohexstr(Off)
                << " is past the end (0x" << utohexstr(B.size()) << ")";
  return false;
}
uint8_t rd8(ArrayRef<uint8_t> B, uint64_t Off) {
  return inBounds(B, Off, 1) ? B[Off] : 0;
}
uint16_t rd16(ArrayRef<uint8_t> B, uint64_t Off) {
  return inBounds(B, Off, 2) ? support::endian::read16le(B.data() + Off) : 0;
}
uint32_t rd32(ArrayRef<uint8_t> B, uint64_t Off) {
  return inBounds(B, Off, 4) ? support::endian::read32le(B.data() + Off) : 0;
}
uint64_t rd64(ArrayRef<uint8_t> B, uint64_t Off) {
  return inBounds(B, Off, 8) ? support::endian::read64le(B.data() + Off) : 0;
}
ArrayRef<uint8_t> slice(ArrayRef<uint8_t> B, uint64_t Off, uint64_t N) {
  return inBounds(B, Off, N) ? B.slice(Off, N) : ArrayRef<uint8_t>();
}

//===----------------------------------------------------------------------===//
// A PE32+ parser (PE/COFF specification)
//===----------------------------------------------------------------------===//

struct SectionHeader {
  std::string Name;
  uint32_t VirtualSize = 0, VirtualAddress = 0, SizeOfRawData = 0,
           PointerToRawData = 0, PointerToRelocations = 0,
           PointerToLinenumbers = 0, Characteristics = 0;
  uint16_t NumberOfRelocations = 0, NumberOfLinenumbers = 0;
};

struct PEView {
  ArrayRef<uint8_t> File;
  uint32_t Lfanew = 0, Signature = 0;
  // COFF file header.
  uint16_t Machine = 0, NumberOfSections = 0, SizeOfOptionalHeader = 0,
           Characteristics = 0;
  uint32_t TimeDateStamp = 0, PointerToSymbolTable = 0, NumberOfSymbols = 0;
  // PE32+ optional header.
  uint16_t Magic = 0;
  uint8_t MajorLinkerVersion = 0, MinorLinkerVersion = 0;
  uint32_t AddressOfEntryPoint = 0;
  uint64_t ImageBase = 0;
  uint32_t SectionAlignment = 0, FileAlignment = 0;
  uint16_t MajorOSVersion = 0, MinorOSVersion = 0, MajorSubsystemVersion = 0,
           MinorSubsystemVersion = 0;
  uint32_t Win32VersionValue = 0, SizeOfImage = 0, SizeOfHeaders = 0,
           CheckSum = 0;
  uint16_t Subsystem = 0, DllCharacteristics = 0;
  uint64_t StackReserve = 0, StackCommit = 0, HeapReserve = 0, HeapCommit = 0;
  uint32_t LoaderFlags = 0, NumberOfRvaAndSizes = 0;
  std::vector<std::pair<uint32_t, uint32_t>> Directories;
  std::vector<SectionHeader> Sections;

  uint64_t stampOffset() const { return Lfanew + 8; }
  uint64_t sectionTableEnd() const {
    return Lfanew + 24 + SizeOfOptionalHeader + 40ull * NumberOfSections;
  }
  // The file offset of RVA inside some section's raw data, or nullopt.
  std::optional<uint64_t> offsetOf(uint32_t RVA) const {
    for (const SectionHeader &S : Sections)
      if (RVA >= S.VirtualAddress && RVA - S.VirtualAddress < S.SizeOfRawData)
        return uint64_t(S.PointerToRawData) + (RVA - S.VirtualAddress);
    return std::nullopt;
  }
  ArrayRef<uint8_t> at(uint32_t RVA, uint64_t Size) const {
    std::optional<uint64_t> Off = offsetOf(RVA);
    if (!Off) {
      ADD_FAILURE() << "RVA 0x" << utohexstr(RVA) << " is not in a section";
      return {};
    }
    return slice(File, *Off, Size);
  }
  const SectionHeader *section(StringRef Name) const {
    for (const SectionHeader &S : Sections)
      if (S.Name == Name)
        return &S;
    return nullptr;
  }
};

PEView parsePE(ArrayRef<uint8_t> F) {
  PEView V;
  V.File = F;
  V.Lfanew = rd32(F, 0x3C);
  V.Signature = rd32(F, V.Lfanew);
  uint64_t C = V.Lfanew + 4;
  V.Machine = rd16(F, C);
  V.NumberOfSections = rd16(F, C + 2);
  V.TimeDateStamp = rd32(F, C + 4);
  V.PointerToSymbolTable = rd32(F, C + 8);
  V.NumberOfSymbols = rd32(F, C + 12);
  V.SizeOfOptionalHeader = rd16(F, C + 16);
  V.Characteristics = rd16(F, C + 18);
  uint64_t O = C + 20;
  V.Magic = rd16(F, O);
  V.MajorLinkerVersion = rd8(F, O + 2);
  V.MinorLinkerVersion = rd8(F, O + 3);
  V.AddressOfEntryPoint = rd32(F, O + 16);
  V.ImageBase = rd64(F, O + 24);
  V.SectionAlignment = rd32(F, O + 32);
  V.FileAlignment = rd32(F, O + 36);
  V.MajorOSVersion = rd16(F, O + 40);
  V.MinorOSVersion = rd16(F, O + 42);
  V.MajorSubsystemVersion = rd16(F, O + 48);
  V.MinorSubsystemVersion = rd16(F, O + 50);
  V.Win32VersionValue = rd32(F, O + 52);
  V.SizeOfImage = rd32(F, O + 56);
  V.SizeOfHeaders = rd32(F, O + 60);
  V.CheckSum = rd32(F, O + 64);
  V.Subsystem = rd16(F, O + 68);
  V.DllCharacteristics = rd16(F, O + 70);
  V.StackReserve = rd64(F, O + 72);
  V.StackCommit = rd64(F, O + 80);
  V.HeapReserve = rd64(F, O + 88);
  V.HeapCommit = rd64(F, O + 96);
  V.LoaderFlags = rd32(F, O + 104);
  V.NumberOfRvaAndSizes = rd32(F, O + 108);
  for (uint32_t I = 0; I < V.NumberOfRvaAndSizes && I < 16; ++I)
    V.Directories.push_back(
        {rd32(F, O + 112 + 8 * I), rd32(F, O + 116 + 8 * I)});
  uint64_t S = C + 20 + V.SizeOfOptionalHeader;
  for (unsigned I = 0; I < V.NumberOfSections; ++I, S += 40) {
    SectionHeader H;
    ArrayRef<uint8_t> N = slice(F, S, 8);
    H.Name = std::string(N.begin(), std::find(N.begin(), N.end(), 0));
    H.VirtualSize = rd32(F, S + 8);
    H.VirtualAddress = rd32(F, S + 12);
    H.SizeOfRawData = rd32(F, S + 16);
    H.PointerToRawData = rd32(F, S + 20);
    H.PointerToRelocations = rd32(F, S + 24);
    H.PointerToLinenumbers = rd32(F, S + 28);
    H.NumberOfRelocations = rd16(F, S + 32);
    H.NumberOfLinenumbers = rd16(F, S + 34);
    H.Characteristics = rd32(F, S + 36);
    V.Sections.push_back(H);
  }
  return V;
}

// The 72-byte CLI header (II.25.3.3).
struct CLIHeaderView {
  uint32_t Cb = 0, MetadataRVA = 0, MetadataSize = 0, Flags = 0,
           EntryPointToken = 0;
  uint16_t Major = 0, Minor = 0;
  std::array<uint32_t, 12> OtherDirectories{};
};

CLIHeaderView parseCLIHeader(const PEView &V) {
  CLIHeaderView H;
  if (V.Directories.size() <= COFF::CLR_RUNTIME_HEADER) {
    ADD_FAILURE() << "no CLR data directory";
    return H;
  }
  ArrayRef<uint8_t> B = V.at(V.Directories[COFF::CLR_RUNTIME_HEADER].first, 72);
  if (B.size() != 72)
    return H;
  H.Cb = rd32(B, 0);
  H.Major = rd16(B, 4);
  H.Minor = rd16(B, 6);
  H.MetadataRVA = rd32(B, 8);
  H.MetadataSize = rd32(B, 12);
  H.Flags = rd32(B, 16);
  H.EntryPointToken = rd32(B, 20);
  for (unsigned I = 0; I < 12; ++I)
    H.OtherDirectories[I] = rd32(B, 24 + 4 * I);
  return H;
}

//===----------------------------------------------------------------------===//
// A metadata-root parser (II.24.2) that reads cells through MetadataSizes
//===----------------------------------------------------------------------===//

struct MetaView {
  ArrayRef<uint8_t> MD;
  std::string Version;
  struct Stream {
    std::string Name;
    uint32_t Offset = 0, Size = 0;
  };
  std::vector<Stream> Streams;
  std::array<uint32_t, NumTableIds> Rows{};
  std::optional<MetadataSizes> Sizes;
  uint64_t TableData = 0;

  const Stream *stream(StringRef Name) const {
    for (const Stream &S : Streams)
      if (S.Name == Name)
        return &S;
    return nullptr;
  }
  // The value of a cell, read with the column's width.
  uint32_t cell(TableId T, uint32_t Row, unsigned Col) const {
    if (!Sizes || Row == 0 || Row > Rows[static_cast<uint8_t>(T)]) {
      ADD_FAILURE() << "no row " << Row << " in table "
                    << static_cast<unsigned>(T);
      return 0;
    }
    uint64_t Off = TableData + Sizes->getTableOffset(T) +
                   uint64_t(Row - 1) * Sizes->getRowSize(T) +
                   Sizes->getColumnOffset(T, Col);
    return Sizes->getColumnSize(T, Col) == 2 ? rd16(MD, Off) : rd32(MD, Off);
  }
};

MetaView parseMetadata(ArrayRef<uint8_t> MD) {
  MetaView M;
  M.MD = MD;
  EXPECT_EQ(rd32(MD, 0), MetadataSignature);
  uint32_t Len = rd32(MD, 12);
  ArrayRef<uint8_t> V = slice(MD, 16, Len);
  M.Version = std::string(V.begin(), std::find(V.begin(), V.end(), 0));
  uint64_t P = 16 + Len;
  uint16_t NumStreams = rd16(MD, P + 2);
  P += 4;
  for (unsigned I = 0; I < NumStreams; ++I) {
    MetaView::Stream S;
    S.Offset = rd32(MD, P);
    S.Size = rd32(MD, P + 4);
    P += 8;
    while (P < MD.size() && MD[P])
      S.Name.push_back(static_cast<char>(MD[P++]));
    P = alignTo(P + 1, 4);
    M.Streams.push_back(S);
  }
  const MetaView::Stream *T = M.stream("#~");
  if (!T) {
    ADD_FAILURE() << "no #~ stream";
    return M;
  }
  uint8_t HeapSizes = rd8(MD, T->Offset + 6);
  uint64_t Valid = rd64(MD, T->Offset + 8);
  uint64_t Q = T->Offset + 24;
  for (unsigned I = 0; I < NumTableIds; ++I)
    if (Valid & (uint64_t(1) << I)) {
      M.Rows[I] = rd32(MD, Q);
      Q += 4;
    }
  if (HeapSizes & HEAP_EXTRA_DATA)
    Q += 4;
  M.TableData = Q;
  M.Sizes.emplace(M.Rows, HeapSizes);
  return M;
}

//===----------------------------------------------------------------------===//
// Test modules
//===----------------------------------------------------------------------===//

// A Phase-1-shaped module: <Module>::main() { ldc.i4.s Ret; nop; ret } (a
// 5-byte tiny body, so the IL stream's size is not a multiple of 4), an
// Assembly row unless it is a netmodule, and optionally a 5-byte FieldRVA
// field.
struct ModuleSpec {
  PEImageKind Kind = PEImageKind::Executable;
  std::string Name = "hello.exe";
  uint8_t Ret = 42;
  bool FieldData = false;
  bool WithBody = true;
  bool Finalize = true;
};

struct TestModule {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  EntityHandle Main, Data;
  ByteVec Mapped;
  uint32_t MainOffset = 0;

  uint32_t mainToken() const { return MB.getToken(Main); }
  PEContent content(uint32_t EntryPointToken) const {
    PEContent C;
    C.Metadata = &MB;
    C.ILStream = Bodies.getBytes();
    C.MappedFieldData = Mapped;
    C.EntryPointToken = EntryPointToken;
    return C;
  }
};

std::unique_ptr<TestModule> buildModule(const ModuleSpec &S) {
  auto M = std::make_unique<TestModule>();
  MetadataBuilder &MB = M->MB;
  MB.setModuleName(S.Name);
  if (S.Kind != PEImageKind::Module) {
    AssemblyIdentity Id;
    Id.Name = "hello";
    MB.setAssembly(Id);
  }
  const uint8_t MainSig[] = {0x00, 0x00, 0x08}; // static int32()
  M->Main = MB.addMethod(
      MB.getModuleType(), MethodAttributes::Public | MethodAttributes::Static,
      MethodImplAttributes::IL | MethodImplAttributes::Managed, "main",
      MB.addBlob(MainSig));
  if (S.WithBody) {
    const uint8_t Code[] = {0x1F, S.Ret, 0x00, 0x2A};
    MethodBodyInfo B;
    B.Code = Code;
    M->MainOffset = M->Bodies.addBody(B);
    MB.setMethodBodyOffset(M->Main, M->MainOffset);
  }
  if (S.FieldData) {
    const uint8_t FieldSig[] = {0x06, 0x08}; // int32
    M->Data = MB.addField(MB.getModuleType(),
                          FieldAttributes::Assembly | FieldAttributes::Static |
                              FieldAttributes::HasFieldRVA,
                          "data", MB.addBlob(FieldSig));
    MB.addFieldRVA(M->Data, 0);
    M->Mapped = {0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
  }
  if (S.Finalize)
    EXPECT_THAT_ERROR(MB.finalize(M->Bodies), Succeeded());
  return M;
}

PEOptions getOptions(PEImageKind Kind, StringRef TT = WinTriple) {
  Expected<PEOptions> O = PEOptions::get(Triple(TT), Kind);
  EXPECT_THAT_EXPECTED(O, Succeeded());
  if (!O)
    return PEOptions();
  return *O;
}

ByteVec write(const PEOptions &O, const PEContent &C,
              BlobContentId *Id = nullptr) {
  SmallVector<uint8_t, 0> Out;
  EXPECT_THAT_ERROR(writePEImage(O, C, Out, Id), Succeeded());
  return ByteVec(Out.begin(), Out.end());
}

std::string writeError(const PEOptions &O, const PEContent &C) {
  SmallVector<uint8_t, 0> Out;
  Error E = writePEImage(O, C, Out);
  if (!E) {
    ADD_FAILURE() << "writePEImage succeeded";
    return "";
  }
  return toString(std::move(E));
}

// An executable, its parsed headers and its content id.
struct Written {
  std::unique_ptr<TestModule> M;
  ByteVec File;
  BlobContentId Id;
};

Written writeModule(const ModuleSpec &S, StringRef TT = WinTriple) {
  Written W;
  W.M = buildModule(S);
  uint32_t Entry =
      S.Kind == PEImageKind::Executable ? W.M->mainToken() : uint32_t(0);
  W.File = write(getOptions(S.Kind, TT), W.M->content(Entry), &W.Id);
  return W;
}

// The content id recomputed from the file by 5.7.7's rule: MVID, COFF
// TimeDateStamp and every debug-directory stamp zeroed, then SHA-256.
BlobContentId recomputeContentId(ByteVec F, uint64_t MvidOffset,
                                 ArrayRef<uint64_t> DebugStampOffsets) {
  PEView V = parsePE(F);
  for (unsigned I = 0; I < 16; ++I)
    F[MvidOffset + I] = 0;
  support::endian::write32le(F.data() + V.stampOffset(), 0);
  for (uint64_t Off : DebugStampOffsets)
    support::endian::write32le(F.data() + Off, 0);
  std::array<uint8_t, 32> Hash = SHA256::hash(F);
  return BlobContentId::fromHash(Hash);
}

uint64_t mvidFileOffset(const PEView &V, const MetadataBuilder &MB) {
  CLIHeaderView H = parseCLIHeader(V);
  std::optional<uint64_t> MD = V.offsetOf(H.MetadataRVA);
  EXPECT_TRUE(MD.has_value());
  return MD.value_or(0) + MB.getMvidOffset();
}

std::array<uint8_t, 16> guidAt(ArrayRef<uint8_t> F, uint64_t Off) {
  std::array<uint8_t, 16> G{};
  ArrayRef<uint8_t> B = slice(F, Off, 16);
  if (B.size() == 16)
    std::copy(B.begin(), B.end(), G.begin());
  return G;
}

// The 128-byte DOS header and stub of Roslyn and ilasm images (both
// measured in scratch: csc 4.11 netmodule and Framework ilasm 4.8 DLL).
const uint8_t ClassicDOSHeader[128] = {
    0x4D, 0x5A, 0x90, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD,
    0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21, 0x54, 0x68, 0x69, 0x73, 0x20, 0x70,
    0x72, 0x6F, 0x67, 0x72, 0x61, 0x6D, 0x20, 0x63, 0x61, 0x6E, 0x6E, 0x6F,
    0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x75, 0x6E, 0x20, 0x69, 0x6E, 0x20,
    0x44, 0x4F, 0x53, 0x20, 0x6D, 0x6F, 0x64, 0x65, 0x2E, 0x0D, 0x0D, 0x0A,
    0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

std::vector<PESection> markerSections() {
  std::vector<PESection> S(3);
  S[0].Name = ".data";
  S[0].Characteristics = 0xC0000040;
  S[1].Name = ".rdata";
  S[1].Characteristics = 0x40000040;
  S[2].Name = ".bss";
  S[2].Characteristics = 0xC0000080;
  return S;
}

CoffSymbol coffSymbol(StringRef Name, uint32_t Value, int16_t Section,
                      uint16_t Type, uint8_t StorageClass) {
  CoffSymbol S;
  S.Name = Name.str();
  S.Value = Value;
  S.SectionNumber = Section;
  S.Type = Type;
  S.StorageClass = StorageClass;
  return S;
}

//===----------------------------------------------------------------------===//
// Content id (5.7.7; metadata-writer 3.10 and 4.10)
//===----------------------------------------------------------------------===//

// 5.7.7's vector, and two more from mdump --contentid in scratch.
TEST(CLIPEWriterTest, ContentIdVectors) {
  std::array<uint8_t, 32> Abc = SHA256::hash(arrayRefFromStringRef("abc"));
  BlobContentId Id = BlobContentId::fromHash(Abc);
  // bf1678ba-018f-4acf-8141-40de5dae2223, in its 16 #GUID bytes.
  const std::array<uint8_t, 16> AbcGuid = {0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01,
                                           0xCF, 0x4A, 0x81, 0x41, 0x40, 0xDE,
                                           0x5D, 0xAE, 0x22, 0x23};
  EXPECT_EQ(Id.Guid, AbcGuid);
  EXPECT_EQ(Id.Stamp, 0xA36103B0u);

  // mdump --contentid 000102...1f: 03020100-0504-4706-8809-0a0b0c0d0e0f
  // 0x93121110. Only the first 20 bytes count.
  std::array<uint8_t, 32> Seq;
  for (unsigned I = 0; I < 32; ++I)
    Seq[I] = I;
  const std::array<uint8_t, 16> SeqGuid = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                           0x06, 0x47, 0x88, 0x09, 0x0A, 0x0B,
                                           0x0C, 0x0D, 0x0E, 0x0F};
  BlobContentId S32 = BlobContentId::fromHash(Seq);
  EXPECT_EQ(S32.Guid, SeqGuid);
  EXPECT_EQ(S32.Stamp, 0x93121110u);
  BlobContentId S20 =
      BlobContentId::fromHash(ArrayRef<uint8_t>(Seq).take_front(20));
  EXPECT_EQ(S20.Guid, SeqGuid);
  EXPECT_EQ(S20.Stamp, 0x93121110u);

  // mdump --contentid ff..ff: ffffffff-ffff-4fff-bfff-ffffffffffff 0xFFFFFFFF.
  std::array<uint8_t, 32> Ones;
  Ones.fill(0xFF);
  BlobContentId O = BlobContentId::fromHash(Ones);
  std::array<uint8_t, 16> OnesGuid;
  OnesGuid.fill(0xFF);
  OnesGuid[7] = 0x4F;
  OnesGuid[8] = 0xBF;
  EXPECT_EQ(O.Guid, OnesGuid);
  EXPECT_EQ(O.Stamp, 0xFFFFFFFFu);
}

//===----------------------------------------------------------------------===//
// Options (5.7.7, 5.8.2 O1)
//===----------------------------------------------------------------------===//

TEST(CLIPEWriterTest, OptionsPerTripleAndKind) {
  struct Case {
    const char *TT;
    PEImageKind Kind;
    uint16_t Machine;
    uint64_t ImageBase;
  } Cases[] = {
      {WinTriple, PEImageKind::Executable, COFF::IMAGE_FILE_MACHINE_AMD64,
       0x140000000},
      {WinTriple, PEImageKind::Library, COFF::IMAGE_FILE_MACHINE_AMD64,
       0x180000000},
      {"x86_64-unknown-linux-gnu-msil", PEImageKind::Module,
       COFF::IMAGE_FILE_MACHINE_AMD64, 0x180000000},
      {"aarch64-unknown-linux-gnu-msil", PEImageKind::Executable,
       COFF::IMAGE_FILE_MACHINE_ARM64, 0x140000000},
      {"aarch64-pc-windows-msvc-msil", PEImageKind::Library,
       COFF::IMAGE_FILE_MACHINE_ARM64, 0x180000000},
  };
  for (const Case &C : Cases) {
    SCOPED_TRACE(std::string(C.TT) + " kind " +
                 std::to_string(static_cast<int>(C.Kind)));
    Expected<PEOptions> O = PEOptions::get(Triple(C.TT), C.Kind);
    ASSERT_THAT_EXPECTED(O, Succeeded());
    EXPECT_EQ(static_cast<uint16_t>(O->Machine), C.Machine);
    EXPECT_EQ(O->Kind, C.Kind);
    // Machine and CorFlags are independent of the rest of the triple.
    EXPECT_EQ(O->CorFlags, uint32_t(COMIMAGE_FLAGS_ILONLY));
    EXPECT_EQ(O->ImageBase, C.ImageBase);
    EXPECT_EQ(O->SectionAlignment, 0x2000u);
    EXPECT_EQ(O->FileAlignment, 0x200u);
    EXPECT_EQ(O->StackReserve, 0x400000u);
    EXPECT_EQ(O->StackCommit, 0x4000u);
    EXPECT_EQ(O->HeapReserve, 0x100000u);
    EXPECT_EQ(O->HeapCommit, 0x2000u);
    EXPECT_EQ(O->Subsystem, uint16_t(COFF::IMAGE_SUBSYSTEM_WINDOWS_CUI));
    EXPECT_EQ(O->DllCharacteristics, 0x8540);
    EXPECT_EQ(O->MajorLinkerVersion, 14);
    EXPECT_EQ(O->MinorLinkerVersion, 0);
  }
}

TEST(CLIPEWriterTest, UnsupportedArchitecture) {
  Expected<PEOptions> R =
      PEOptions::get(Triple("riscv64-unknown-linux-gnu"), PEImageKind::Library);
  EXPECT_THAT_EXPECTED(
      R, FailedWithMessage("cannot write PE image: architecture 'riscv64' "
                           "is not supported until phase 5"));
  // i686 needs PE32, the mscoree stub and .reloc (phase 5).
  for (const char *TT :
       {"i686-pc-windows-msvc", "armv7-unknown-linux-gnueabihf",
        "wasm32-unknown-unknown"}) {
    SCOPED_TRACE(TT);
    Expected<PEOptions> E = PEOptions::get(Triple(TT), PEImageKind::Executable);
    ASSERT_FALSE(bool(E));
    std::string Msg = toString(E.takeError());
    EXPECT_THAT(Msg, StartsWith("cannot write PE image: architecture '"));
    EXPECT_TRUE(StringRef(Msg).ends_with("' is not supported until phase 5"))
        << Msg;
  }
}

//===----------------------------------------------------------------------===//
// Header fields per kind (5.7.7 image shape; metadata-writer 3.3 and 4.9)
//===----------------------------------------------------------------------===//

// Fields shared by every kind.
void checkCommonHeaders(const PEView &V) {
  EXPECT_EQ(ArrayRef<uint8_t>(V.File).take_front(128),
            ArrayRef<uint8_t>(ClassicDOSHeader));
  EXPECT_EQ(V.Lfanew, 0x80u);
  EXPECT_EQ(V.Signature, 0x00004550u);     // "PE\0\0"
  EXPECT_EQ(V.SizeOfOptionalHeader, 0xF0); // PE32+ with 16 directories
  EXPECT_EQ(V.Magic, 0x20B);
  EXPECT_EQ(V.MajorLinkerVersion, 14);
  EXPECT_EQ(V.MinorLinkerVersion, 0);
  EXPECT_EQ(V.AddressOfEntryPoint, 0u);
  EXPECT_EQ(V.SectionAlignment, 0x2000u);
  EXPECT_EQ(V.FileAlignment, 0x200u);
  EXPECT_EQ(V.MajorOSVersion, 4);
  EXPECT_EQ(V.MinorOSVersion, 0);
  EXPECT_EQ(V.MajorSubsystemVersion, 4);
  EXPECT_EQ(V.MinorSubsystemVersion, 0);
  EXPECT_EQ(V.Win32VersionValue, 0u);
  EXPECT_EQ(V.CheckSum, 0u);
  EXPECT_EQ(V.Subsystem, uint16_t(COFF::IMAGE_SUBSYSTEM_WINDOWS_CUI));
  EXPECT_EQ(V.DllCharacteristics, 0x8540);
  EXPECT_EQ(V.StackReserve, 0x400000u);
  EXPECT_EQ(V.StackCommit, 0x4000u);
  EXPECT_EQ(V.HeapReserve, 0x100000u);
  EXPECT_EQ(V.HeapCommit, 0x2000u);
  EXPECT_EQ(V.LoaderFlags, 0u);
  EXPECT_EQ(V.NumberOfRvaAndSizes, 16u);
  // The stamp is the content id's (high bit set), never 0 or a time.
  EXPECT_NE(V.TimeDateStamp & 0x80000000u, 0u);

  // .text first, at RVA SectionAlignment, raw data right after the headers.
  ASSERT_FALSE(V.Sections.empty());
  const SectionHeader &T = V.Sections[0];
  EXPECT_EQ(T.Name, ".text");
  EXPECT_EQ(T.Characteristics, 0x60000020u);
  EXPECT_EQ(T.VirtualAddress, 0x2000u);
  EXPECT_EQ(T.PointerToRawData, V.SizeOfHeaders);
  EXPECT_EQ(T.SizeOfRawData % 0x200, 0u);
  EXPECT_GE(T.SizeOfRawData, T.VirtualSize);
  EXPECT_LT(T.SizeOfRawData, T.VirtualSize + 0x200);
  EXPECT_EQ(T.PointerToRelocations, 0u);
  EXPECT_EQ(T.NumberOfRelocations, 0);
  EXPECT_EQ(V.SizeOfHeaders % 0x200, 0u);
  EXPECT_LE(V.sectionTableEnd(), V.SizeOfHeaders);
  EXPECT_EQ(V.SizeOfImage % 0x2000, 0u);

  // No import table, IAT, entry stub or .reloc: only the CLR directory (and
  // Debug, which these images do not have).
  ASSERT_EQ(V.Directories.size(), 16u);
  for (unsigned I = 0; I < 16; ++I) {
    SCOPED_TRACE("data directory " + std::to_string(I));
    if (I == COFF::CLR_RUNTIME_HEADER) {
      EXPECT_EQ(V.Directories[I].first, 0x2000u);
      EXPECT_EQ(V.Directories[I].second, 72u);
    } else {
      EXPECT_EQ(V.Directories[I].first, 0u);
      EXPECT_EQ(V.Directories[I].second, 0u);
    }
  }
  for (const SectionHeader &S : V.Sections) {
    EXPECT_NE(S.Name, ".reloc");
    EXPECT_NE(S.Name, ".idata");
    EXPECT_NE(S.Name, ".rsrc");
  }

  CLIHeaderView H = parseCLIHeader(V);
  EXPECT_EQ(H.Cb, 72u);
  EXPECT_EQ(H.Major, 2);
  EXPECT_EQ(H.Minor, 5);
  EXPECT_EQ(H.Flags, uint32_t(COMIMAGE_FLAGS_ILONLY));
  for (unsigned I = 0; I < 12; ++I)
    EXPECT_EQ(H.OtherDirectories[I], 0u) << "CLI header word " << 6 + I;
}

TEST(CLIPEWriterTest, ExecutableHeaders) {
  Written W = writeModule({});
  PEView V = parsePE(W.File);
  checkCommonHeaders(V);
  EXPECT_EQ(V.Machine, uint16_t(COFF::IMAGE_FILE_MACHINE_AMD64));
  // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE, no DLL.
  EXPECT_EQ(V.Characteristics, 0x0022);
  EXPECT_EQ(V.ImageBase, 0x140000000u);
  EXPECT_EQ(V.NumberOfSections, 1);
  EXPECT_EQ(V.SizeOfHeaders, 0x200u);
  EXPECT_EQ(V.PointerToSymbolTable, 0u);
  EXPECT_EQ(V.NumberOfSymbols, 0u);
  const SectionHeader &T = V.Sections[0];
  EXPECT_EQ(V.SizeOfImage, alignTo(T.VirtualAddress + T.VirtualSize, 0x2000));
  // The file ends with .text's raw data.
  EXPECT_EQ(W.File.size(), uint64_t(T.PointerToRawData) + T.SizeOfRawData);
  CLIHeaderView H = parseCLIHeader(V);
  EXPECT_EQ(H.EntryPointToken, W.M->mainToken());
  EXPECT_EQ(H.EntryPointToken, makeToken(TableId::MethodDef, 1));
  EXPECT_EQ(H.MetadataSize, W.M->MB.getSerializedSize());
}

TEST(CLIPEWriterTest, LibraryHeaders) {
  ModuleSpec S;
  S.Kind = PEImageKind::Library;
  S.Name = "hello.dll";
  Written W = writeModule(S);
  PEView V = parsePE(W.File);
  checkCommonHeaders(V);
  // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE | DLL.
  EXPECT_EQ(V.Characteristics, 0x2022);
  EXPECT_EQ(V.ImageBase, 0x180000000u);
  EXPECT_EQ(parseCLIHeader(V).EntryPointToken, 0u);
}

// The netmodule envelope (5.8.2 O1) against the values of a Roslyn netmodule
// built in scratch (csc 4.11 -noconfig -nostdlib -deterministic
// -target:module -platform:x64): Characteristics 0x2022, PE32+, image base
// 0x180000000, alignments 0x2000/0x200, OS and subsystem 4.0, CUI,
// DllCharacteristics 0x8540, stack 0x400000/0x4000, heap 0x100000/0x2000,
// SizeOfHeaders 0x200, CheckSum 0, .text 0x60000020 at 0x2000 starting with
// the CLI header (2.5, ILONLY, entry token 0). Roslyn differs from this
// writer only where 5.7.7 says so: linker version 48.0 (lld's 14.0 here), a
// Debug directory with a REPRO entry and a .rsrc version resource (neither
// here).
TEST(CLIPEWriterTest, ModuleMatchesRoslynNetmodule) {
  ModuleSpec S;
  S.Kind = PEImageKind::Module;
  S.Name = "m.netmodule";
  Written W = writeModule(S);
  EXPECT_FALSE(W.M->MB.hasAssembly());
  PEView V = parsePE(W.File);
  checkCommonHeaders(V);
  EXPECT_EQ(V.Machine, uint16_t(COFF::IMAGE_FILE_MACHINE_AMD64));
  EXPECT_EQ(V.Characteristics, 0x2022);
  EXPECT_EQ(V.ImageBase, 0x180000000u);
  EXPECT_EQ(V.SizeOfHeaders, 0x200u);
  CLIHeaderView H = parseCLIHeader(V);
  EXPECT_EQ(H.EntryPointToken, 0u);
  EXPECT_EQ(H.Flags, uint32_t(COMIMAGE_FLAGS_ILONLY));
}

TEST(CLIPEWriterTest, ARM64Machine) {
  ModuleSpec S;
  S.Kind = PEImageKind::Library;
  Written W = writeModule(S, "aarch64-unknown-linux-gnu-msil");
  PEView V = parsePE(W.File);
  checkCommonHeaders(V);
  EXPECT_EQ(V.Machine, uint16_t(COFF::IMAGE_FILE_MACHINE_ARM64));
  EXPECT_EQ(V.Characteristics, 0x2022);
}

//===----------------------------------------------------------------------===//
// .text layout (5.7.7)
//===----------------------------------------------------------------------===//

// CLI header, IL stream (4-aligned), metadata (4-aligned), debug directory
// and data (4-aligned), mapped field data (8-aligned, last). The metadata is
// serialized once with the final RVAs: its bytes equal the builder's own
// serialization with those RVAs, but for the MVID.
TEST(CLIPEWriterTest, TextLayoutAndAlignments) {
  ModuleSpec S;
  S.FieldData = true;
  std::unique_ptr<TestModule> M = buildModule(S);
  const uint8_t PdbData[] = {1, 2, 3, 4, 5};
  DebugDirectoryEntry Debug[2];
  Debug[0].Type = 16; // REPRO, no data
  Debug[1].Type = DebugTypeEmbeddedPortablePdb;
  Debug[1].MajorVersion = 0x0100;
  Debug[1].MinorVersion = 0x0100;
  Debug[1].Data = PdbData;
  PEContent C = M->content(M->mainToken());
  C.DebugEntries = Debug;
  BlobContentId Id;
  ByteVec F = write(getOptions(PEImageKind::Executable), C, &Id);
  PEView V = parsePE(F);
  ASSERT_EQ(V.NumberOfSections, 1);
  const SectionHeader &T = V.Sections[0];
  uint32_t TextEnd = T.VirtualAddress + T.VirtualSize;

  // 1. The CLI header starts .text.
  EXPECT_EQ(V.Directories[COFF::CLR_RUNTIME_HEADER].first, T.VirtualAddress);
  // 2. The IL stream right after it, so body offsets stay 4-aligned.
  const uint32_t ILRVA = T.VirtualAddress + 72;
  ArrayRef<uint8_t> IL = M->Bodies.getBytes();
  ASSERT_EQ(IL.size() % 4, 1u) << "the test wants an unaligned IL stream";
  EXPECT_EQ(V.at(ILRVA, IL.size()), IL);
  // 3. The metadata, 4-aligned after the IL stream.
  CLIHeaderView H = parseCLIHeader(V);
  EXPECT_EQ(H.MetadataRVA, alignTo(ILRVA + IL.size(), 4));
  EXPECT_EQ(H.MetadataSize, M->MB.getSerializedSize());
  uint32_t MDEnd = H.MetadataRVA + H.MetadataSize;
  ArrayRef<uint8_t> MDBytes = V.at(H.MetadataRVA, H.MetadataSize);
  MetaView MV = parseMetadata(MDBytes);
  EXPECT_EQ(MV.Version, "v4.0.30319");
  EXPECT_EQ(MV.cell(TableId::MethodDef, 1, 0), ILRVA + M->MainOffset);
  // 5. The mapped field data, 8-aligned and last.
  uint32_t MappedRVA = MV.cell(TableId::FieldRVA, 1, 0);
  EXPECT_EQ(MappedRVA % 8, 0u);
  EXPECT_EQ(V.at(MappedRVA, M->Mapped.size()), ArrayRef<uint8_t>(M->Mapped));
  uint32_t MappedEnd = MappedRVA + M->Mapped.size();
  EXPECT_GE(TextEnd, MappedEnd);
  EXPECT_LE(TextEnd, alignTo(MappedEnd, 8));

  // The metadata equals the builder's serialization with these RVAs, except
  // for the MVID that the content id fills in.
  ByteVec Want(M->MB.getSerializedSize());
  M->MB.serialize(Want, ILRVA, MappedRVA);
  ByteVec Actual(MDBytes.begin(), MDBytes.end());
  size_t Mvid = M->MB.getMvidOffset();
  EXPECT_EQ(guidAt(Actual, Mvid), Id.Guid);
  std::fill_n(Actual.begin() + Mvid, 16, 0);
  EXPECT_EQ(Actual, Want);

  // 4. The debug directory, 4-aligned after the metadata, and its data
  // before the mapped field data.
  ASSERT_EQ(V.Directories.size(), 16u);
  auto [DebugRVA, DebugSize] = V.Directories[COFF::DEBUG_DIRECTORY];
  EXPECT_EQ(DebugRVA, alignTo(MDEnd, 4));
  ASSERT_EQ(DebugSize, 2u * 28);
  uint32_t DataEnd = DebugRVA + DebugSize;
  ArrayRef<uint8_t> Dir = V.at(DebugRVA, DebugSize);
  ASSERT_EQ(Dir.size(), 56u);
  for (unsigned I = 0; I < 2; ++I) {
    SCOPED_TRACE("debug entry " + std::to_string(I));
    ArrayRef<uint8_t> E = Dir.slice(28 * I, 28);
    EXPECT_EQ(rd32(E, 0), 0u);              // Characteristics
    EXPECT_EQ(rd32(E, 4), V.TimeDateStamp); // the content id's stamp
    EXPECT_EQ(rd16(E, 8), Debug[I].MajorVersion);
    EXPECT_EQ(rd16(E, 10), Debug[I].MinorVersion);
    EXPECT_EQ(rd32(E, 12), Debug[I].Type);
    EXPECT_EQ(rd32(E, 16), Debug[I].Data.size());
    if (Debug[I].Data.empty())
      continue;
    uint32_t RVA = rd32(E, 20);
    EXPECT_GE(RVA, DebugRVA + DebugSize);
    EXPECT_EQ(V.at(RVA, Debug[I].Data.size()), Debug[I].Data);
    EXPECT_EQ(std::optional<uint64_t>(rd32(E, 24)), V.offsetOf(RVA));
    DataEnd = std::max<uint32_t>(DataEnd, RVA + Debug[I].Data.size());
  }
  EXPECT_EQ(MappedRVA, alignTo(DataEnd, 8));
  EXPECT_GT(MappedRVA, MDEnd);

  // Every stamp and the MVID come from the content id over the whole file.
  EXPECT_EQ(V.TimeDateStamp, Id.Stamp);
  std::optional<uint64_t> DirOff = V.offsetOf(DebugRVA);
  ASSERT_TRUE(DirOff.has_value());
  const uint64_t Stamps[] = {*DirOff + 4, *DirOff + 28 + 4};
  BlobContentId R = recomputeContentId(F, mvidFileOffset(V, M->MB), Stamps);
  EXPECT_EQ(R.Guid, Id.Guid);
  EXPECT_EQ(R.Stamp, Id.Stamp);
}

// An ExtraSections entry with data goes at the next section-aligned RVA,
// after .text's raw data; two sections still fit in 0x200 bytes of headers.
TEST(CLIPEWriterTest, ExtraSectionAfterText) {
  ModuleSpec S;
  S.Kind = PEImageKind::Library;
  std::unique_ptr<TestModule> M = buildModule(S);
  const uint8_t Payload[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03};
  PESection X;
  X.Name = ".extra";
  X.Characteristics = 0x40000040;
  X.Data = Payload;
  PEContent C = M->content(0);
  C.ExtraSections = ArrayRef<PESection>(X);
  ByteVec F = write(getOptions(PEImageKind::Library), C);
  PEView V = parsePE(F);
  ASSERT_EQ(V.NumberOfSections, 2);
  EXPECT_EQ(V.SizeOfHeaders, 0x200u);
  const SectionHeader &T = V.Sections[0];
  const SectionHeader &E = V.Sections[1];
  EXPECT_EQ(T.Name, ".text");
  EXPECT_EQ(E.Name, ".extra");
  EXPECT_EQ(E.Characteristics, 0x40000040u);
  EXPECT_EQ(E.VirtualAddress,
            alignTo(T.VirtualAddress + T.VirtualSize, 0x2000));
  EXPECT_EQ(E.VirtualSize, sizeof(Payload));
  EXPECT_EQ(E.SizeOfRawData, 0x200u);
  EXPECT_EQ(E.PointerToRawData, T.PointerToRawData + T.SizeOfRawData);
  EXPECT_EQ(V.at(E.VirtualAddress, sizeof(Payload)),
            ArrayRef<uint8_t>(Payload));
  EXPECT_EQ(V.SizeOfImage, alignTo(E.VirtualAddress + E.VirtualSize, 0x2000));
  EXPECT_EQ(F.size(), uint64_t(E.PointerToRawData) + E.SizeOfRawData);
}

//===----------------------------------------------------------------------===//
// Marker sections and the COFF symbol view (5.8.6)
//===----------------------------------------------------------------------===//

TEST(CLIPEWriterTest, MarkerSectionsAndSizeOfHeaders) {
  ModuleSpec S;
  S.Kind = PEImageKind::Module;
  std::unique_ptr<TestModule> M = buildModule(S);
  std::vector<PESection> Markers = markerSections();
  PEContent C = M->content(0);
  C.ExtraSections = Markers;
  ByteVec F = write(getOptions(PEImageKind::Module), C);
  PEView V = parsePE(F);
  ASSERT_EQ(V.NumberOfSections, 4);
  // Four section headers end at 0x228: the headers grow to 0x400.
  EXPECT_EQ(V.SizeOfHeaders, 0x400u);
  const SectionHeader &T = V.Sections[0];
  EXPECT_EQ(T.Name, ".text");
  EXPECT_EQ(T.VirtualAddress, 0x2000u);
  EXPECT_EQ(T.PointerToRawData, 0x400u);
  uint32_t Prev = alignTo(T.VirtualAddress + T.VirtualSize, 0x2000);
  for (unsigned I = 0; I < 3; ++I) {
    const SectionHeader &X = V.Sections[I + 1];
    SCOPED_TRACE(X.Name);
    EXPECT_EQ(X.Name, Markers[I].Name);
    EXPECT_EQ(X.Characteristics, Markers[I].Characteristics);
    EXPECT_EQ(X.VirtualSize, 0u);
    EXPECT_EQ(X.SizeOfRawData, 0u);
    EXPECT_EQ(X.PointerToRawData, 0u);
    EXPECT_EQ(X.VirtualAddress % 0x2000, 0u);
    EXPECT_GE(X.VirtualAddress, Prev);
    Prev = X.VirtualAddress;
  }
  EXPECT_GE(V.SizeOfImage, alignTo(T.VirtualAddress + T.VirtualSize, 0x2000));
  // The file still ends with .text's raw data (no symbols here).
  EXPECT_EQ(F.size(), uint64_t(T.PointerToRawData) + T.SizeOfRawData);
  EXPECT_EQ(V.PointerToSymbolTable, 0u);
  EXPECT_EQ(V.NumberOfSymbols, 0u);
}

// 18-byte records after the last raw data, 4-aligned; names of 8 bytes stay
// in the record, longer ones go to the string table (a u32 size that counts
// itself, then NUL-terminated names); file-only, outside SizeOfImage.
TEST(CLIPEWriterTest, COFFSymbolTable) {
  ModuleSpec S;
  S.Kind = PEImageKind::Module;
  std::unique_ptr<TestModule> M = buildModule(S);
  std::vector<PESection> Markers = markerSections();
  const uint8_t Ext = COFF::IMAGE_SYM_CLASS_EXTERNAL;
  const uint8_t Sta = COFF::IMAGE_SYM_CLASS_STATIC;
  std::vector<CoffSymbol> Syms = {
      coffSymbol("main", 0, 1, 0x20, Ext),
      coffSymbol("abcdefgh", 0, 2, 0, Ext),  // 8 bytes: in the record
      coffSymbol("abcdefghi", 0, 3, 0, Sta), // 9 bytes: string table
      coffSymbol("an_undefined_function", 0, 0, 0x20, Ext),
      coffSymbol("common_c", 4, 0, 0, Ext),
      coffSymbol("bss", 0, 4, 0, Sta),
  };
  PEContent C = M->content(0);
  C.ExtraSections = Markers;
  C.CoffSymbols = Syms;
  BlobContentId Id;
  ByteVec F = write(getOptions(PEImageKind::Module), C, &Id);
  PEView V = parsePE(F);
  uint64_t RawEnd = 0;
  for (const SectionHeader &X : V.Sections)
    RawEnd = std::max<uint64_t>(RawEnd, X.PointerToRawData + X.SizeOfRawData);
  EXPECT_EQ(V.PointerToSymbolTable, alignTo(RawEnd, 4));
  ASSERT_EQ(V.NumberOfSymbols, Syms.size());
  uint64_t Table = V.PointerToSymbolTable;
  uint64_t Strings = Table + 18 * Syms.size();
  uint32_t StringsSize = rd32(F, Strings);
  EXPECT_GE(StringsSize, 4u);
  EXPECT_EQ(F.size(), Strings + StringsSize);
  for (unsigned I = 0; I < Syms.size(); ++I) {
    SCOPED_TRACE(Syms[I].Name);
    ArrayRef<uint8_t> R = slice(F, Table + 18 * I, 18);
    ASSERT_EQ(R.size(), 18u);
    std::string Name;
    if (Syms[I].Name.size() <= 8) {
      ByteVec Short(8, 0);
      std::copy(Syms[I].Name.begin(), Syms[I].Name.end(), Short.begin());
      EXPECT_EQ(R.take_front(8), ArrayRef<uint8_t>(Short));
    } else {
      EXPECT_EQ(rd32(R, 0), 0u);
      uint32_t Off = rd32(R, 4);
      EXPECT_GE(Off, 4u);
      ASSERT_LT(Off, StringsSize);
      for (uint64_t P = Strings + Off; P < F.size() && F[P]; ++P)
        Name.push_back(static_cast<char>(F[P]));
      EXPECT_EQ(Name, Syms[I].Name);
    }
    EXPECT_EQ(rd32(R, 8), Syms[I].Value);
    EXPECT_EQ(static_cast<int16_t>(rd16(R, 12)), Syms[I].SectionNumber);
    EXPECT_EQ(rd16(R, 14), Syms[I].Type);
    EXPECT_EQ(rd8(R, 16), Syms[I].StorageClass);
    EXPECT_EQ(rd8(R, 17), 0u) << "no auxiliary records";
  }
  // The view is outside the image.
  for (const SectionHeader &X : V.Sections)
    EXPECT_LE(uint64_t(X.PointerToRawData) + X.SizeOfRawData, Table);

  // The content id covers the view: the same image without it, or with one
  // name changed, has another id.
  BlobContentId R = recomputeContentId(F, mvidFileOffset(V, M->MB), {});
  EXPECT_EQ(R.Guid, Id.Guid);
  EXPECT_EQ(R.Stamp, Id.Stamp);
  std::vector<CoffSymbol> Renamed = Syms;
  Renamed[2].Name = "abcdefghj";
  C.CoffSymbols = Renamed;
  BlobContentId Id2;
  ByteVec F2 = write(getOptions(PEImageKind::Module), C, &Id2);
  EXPECT_NE(Id2.Guid, Id.Guid);
  EXPECT_NE(Id2.Stamp, Id.Stamp);
  EXPECT_EQ(F2.size(), F.size());
}

// Only short names: the string table is just its 4-byte size.
TEST(CLIPEWriterTest, COFFSymbolTableWithoutLongNames) {
  ModuleSpec S;
  S.Kind = PEImageKind::Module;
  std::unique_ptr<TestModule> M = buildModule(S);
  std::vector<PESection> Markers = markerSections();
  std::vector<CoffSymbol> Syms = {
      coffSymbol("f", 0, 1, 0x20, COFF::IMAGE_SYM_CLASS_EXTERNAL)};
  PEContent C = M->content(0);
  C.ExtraSections = Markers;
  C.CoffSymbols = Syms;
  ByteVec F = write(getOptions(PEImageKind::Module), C);
  PEView V = parsePE(F);
  EXPECT_EQ(V.NumberOfSymbols, 1u);
  EXPECT_EQ(F.size(), uint64_t(V.PointerToSymbolTable) + 18 + 4);
  EXPECT_EQ(rd32(F, V.PointerToSymbolTable + 18), 4u);
}

//===----------------------------------------------------------------------===//
// Content id and determinism (5.7.7, 5.7.12, 5.8.2 O4)
//===----------------------------------------------------------------------===//

TEST(CLIPEWriterTest, ContentIdOverWholeFile) {
  Written W = writeModule({});
  PEView V = parsePE(W.File);
  uint64_t Mvid = mvidFileOffset(V, W.M->MB);
  EXPECT_EQ(guidAt(W.File, Mvid), W.Id.Guid);
  EXPECT_EQ(V.TimeDateStamp, W.Id.Stamp);
  // RFC 4122 version 4 and variant bits.
  EXPECT_EQ(W.Id.Guid[7] & 0xF0, 0x40);
  EXPECT_EQ(W.Id.Guid[8] & 0xC0, 0x80);
  BlobContentId R = recomputeContentId(W.File, Mvid, {});
  EXPECT_EQ(R.Guid, W.Id.Guid);
  EXPECT_EQ(R.Stamp, W.Id.Stamp);
  // A null Id pointer writes the same bytes.
  ByteVec Again = write(getOptions(PEImageKind::Executable),
                        W.M->content(W.M->mainToken()));
  EXPECT_EQ(Again, W.File);
}

TEST(CLIPEWriterTest, Deterministic) {
  ModuleSpec S;
  S.FieldData = true;
  Written A = writeModule(S);
  Written B = writeModule(S);
  EXPECT_EQ(A.File, B.File);
  EXPECT_EQ(A.Id.Guid, B.Id.Guid);
  EXPECT_EQ(A.Id.Stamp, B.Id.Stamp);
  // Writing the same content again gives the same bytes.
  ByteVec C = write(getOptions(PEImageKind::Executable),
                    A.M->content(A.M->mainToken()));
  EXPECT_EQ(C, A.File);
}

// One changed byte of content changes the MVID and the stamp.
TEST(CLIPEWriterTest, ChangedByteChangesMvidAndStamp) {
  ModuleSpec S;
  Written A = writeModule(S);
  S.Ret = 43;
  Written B = writeModule(S);
  ASSERT_EQ(A.File.size(), B.File.size());
  EXPECT_NE(A.Id.Guid, B.Id.Guid);
  EXPECT_NE(A.Id.Stamp, B.Id.Stamp);
  // The module name is part of the hashed content.
  ModuleSpec N;
  N.Name = "hellp.exe";
  Written C = writeModule(N);
  EXPECT_NE(A.Id.Guid, C.Id.Guid);
  EXPECT_NE(A.Id.Stamp, C.Id.Stamp);
  // So is the mapped field data.
  ModuleSpec D1, D2;
  D1.FieldData = D2.FieldData = true;
  Written E1 = writeModule(D1);
  std::unique_ptr<TestModule> M2 = buildModule(D2);
  M2->Mapped[4] ^= 1;
  BlobContentId Id2;
  write(getOptions(PEImageKind::Executable), M2->content(M2->mainToken()),
        &Id2);
  EXPECT_NE(E1.Id.Guid, Id2.Guid);
  EXPECT_NE(E1.Id.Stamp, Id2.Stamp);
}

//===----------------------------------------------------------------------===//
// Checks (5.7.7)
//===----------------------------------------------------------------------===//

TEST(CLIPEWriterTest, ModuleMustNotHaveAnAssemblyRow) {
  std::unique_ptr<TestModule> M = buildModule({}); // has an Assembly row
  EXPECT_THAT(writeError(getOptions(PEImageKind::Module), M->content(0)),
              StartsWith(PEPrefix));
}

TEST(CLIPEWriterTest, ExecutableAndLibraryNeedAnAssemblyRow) {
  ModuleSpec S;
  S.Kind = PEImageKind::Module; // no Assembly row
  std::unique_ptr<TestModule> M = buildModule(S);
  EXPECT_THAT(writeError(getOptions(PEImageKind::Executable),
                         M->content(M->mainToken())),
              StartsWith(PEPrefix));
  EXPECT_THAT(writeError(getOptions(PEImageKind::Library), M->content(0)),
              StartsWith(PEPrefix));
}

TEST(CLIPEWriterTest, EntryPointTokenRules) {
  std::unique_ptr<TestModule> M = buildModule({});
  PEOptions Exe = getOptions(PEImageKind::Executable);
  // An executable needs a valid MethodDef token.
  for (uint32_t Bad :
       {uint32_t(0), makeToken(TableId::TypeDef, 1),
        makeToken(TableId::MethodDef, 0), makeToken(TableId::MethodDef, 99),
        makeToken(TableId::MemberRef, 1)}) {
    SCOPED_TRACE("entry token 0x" + utohexstr(Bad));
    EXPECT_THAT(writeError(Exe, M->content(Bad)), StartsWith(PEPrefix));
  }
  // A library needs 0 (PEVerify /md reports 0x80131B21 otherwise).
  EXPECT_THAT(
      writeError(getOptions(PEImageKind::Library), M->content(M->mainToken())),
      StartsWith(PEPrefix));
  // So does a netmodule.
  ModuleSpec S;
  S.Kind = PEImageKind::Module;
  std::unique_ptr<TestModule> N = buildModule(S);
  EXPECT_THAT(
      writeError(getOptions(PEImageKind::Module), N->content(N->mainToken())),
      StartsWith(PEPrefix));
}

TEST(CLIPEWriterTest, MetadataMustBeFinalized) {
  ModuleSpec S;
  S.Finalize = false;
  std::unique_ptr<TestModule> M = buildModule(S);
  EXPECT_THAT(writeError(getOptions(PEImageKind::Executable),
                         M->content(makeToken(TableId::MethodDef, 1))),
              StartsWith(PEPrefix));
}

// checkMethodBodies()'s error comes back unchanged (5.7.6).
TEST(CLIPEWriterTest, CheckMethodBodiesErrorIsReturnedUnchanged) {
  ModuleSpec S;
  S.WithBody = false;
  std::unique_ptr<TestModule> M = buildModule(S);
  EXPECT_EQ(writeError(getOptions(PEImageKind::Executable),
                       M->content(M->mainToken())),
            "cannot write CLI metadata: method '<Module>::main' has no body");
}

} // namespace
