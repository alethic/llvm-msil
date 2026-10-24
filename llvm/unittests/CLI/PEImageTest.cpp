//===- PEImageTest.cpp - PE image reader of the ECMA-335 library ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/PEImage.h against the MSIL implementation plan (5.7.8,
// 5.7.12): PE32+ images that writePEImage writes (EXE, DLL and a netmodule
// with 5.8.6's COFF symbol view), a PE32 image built here byte by byte as the
// AnyCPU reference assemblies are (PE/COFF specification; Roslyn's anycpu
// values from recon/p2/metadata-writer.md 3.3), and malformed variants of
// both, which must give Errors with the prefix "invalid CLI image: ", never a
// crash. Expected values come from the PE/COFF specification and the plan,
// never from the implementation under test.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/PEImage.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/CLI/PEWriter.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
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

constexpr char ImagePrefix[] = "invalid CLI image: ";

// File offsets in images with the classic 128-byte DOS header.
constexpr uint64_t PESignature = 0x80;
constexpr uint64_t COFFHeader = 0x84;
constexpr uint64_t OptionalHeader = 0x98;

void w16(ByteVec &F, uint64_t Off, uint16_t V) {
  ASSERT_LE(Off + 2, F.size());
  support::endian::write16le(F.data() + Off, V);
}
void w32(ByteVec &F, uint64_t Off, uint32_t V) {
  ASSERT_LE(Off + 4, F.size());
  support::endian::write32le(F.data() + Off, V);
}
uint16_t r16(ArrayRef<uint8_t> F, uint64_t Off) {
  return Off + 2 <= F.size() ? support::endian::read16le(F.data() + Off) : 0;
}
uint32_t r32(ArrayRef<uint8_t> F, uint64_t Off) {
  return Off + 4 <= F.size() ? support::endian::read32le(F.data() + Off) : 0;
}

// The message of a failed Expected, checked to carry the image prefix.
template <typename T> void expectImageError(Expected<T> E) {
  ASSERT_FALSE(bool(E)) << "expected an error";
  EXPECT_THAT(toString(E.takeError()), StartsWith(ImagePrefix));
}

// The classic DOS header and stub (Roslyn and ilasm), e_lfanew = 0x80.
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

//===----------------------------------------------------------------------===//
// Images
//===----------------------------------------------------------------------===//

// A module with <Module>::main() { ldc.i4.s 42; ret } and, unless it is a
// netmodule, an Assembly row.
struct Image {
  std::unique_ptr<MetadataBuilder> MB;
  MethodBodyStream Bodies;
  uint32_t MainToken = 0;
  ByteVec File;
  BlobContentId Id;
  std::vector<PESection> Markers;
  std::vector<CoffSymbol> Symbols;
};

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

std::unique_ptr<Image> writeImage(PEImageKind Kind, bool WithView = false) {
  auto I = std::make_unique<Image>();
  I->MB = std::make_unique<MetadataBuilder>();
  MetadataBuilder &MB = *I->MB;
  MB.setModuleName(Kind == PEImageKind::Executable ? "img.exe" : "img.dll");
  if (Kind != PEImageKind::Module) {
    AssemblyIdentity Id;
    Id.Name = "img";
    MB.setAssembly(Id);
  }
  const uint8_t Sig[] = {0x00, 0x00, 0x08};
  EntityHandle Main = MB.addMethod(
      MB.getModuleType(), MethodAttributes::Public | MethodAttributes::Static,
      MethodImplAttributes::IL, "main", MB.addBlob(Sig));
  const uint8_t Code[] = {0x1F, 0x2A, 0x2A};
  MethodBodyInfo B;
  B.Code = Code;
  MB.setMethodBodyOffset(Main, I->Bodies.addBody(B));
  EXPECT_THAT_ERROR(MB.finalize(I->Bodies), Succeeded());
  I->MainToken = MB.getToken(Main);

  Expected<PEOptions> O =
      PEOptions::get(Triple("x86_64-pc-windows-msvc-msil"), Kind);
  EXPECT_THAT_EXPECTED(O, Succeeded());
  if (!O)
    return I;
  PEContent C;
  C.Metadata = &MB;
  C.ILStream = I->Bodies.getBytes();
  C.EntryPointToken = Kind == PEImageKind::Executable ? I->MainToken : 0;
  if (WithView) {
    I->Markers.resize(3);
    I->Markers[0].Name = ".data";
    I->Markers[0].Characteristics = 0xC0000040;
    I->Markers[1].Name = ".rdata";
    I->Markers[1].Characteristics = 0x40000040;
    I->Markers[2].Name = ".bss";
    I->Markers[2].Characteristics = 0xC0000080;
    const uint8_t Ext = COFF::IMAGE_SYM_CLASS_EXTERNAL;
    I->Symbols = {coffSymbol("main", 0, 1, 0x20, Ext),
                  coffSymbol("a_long_data_name", 0, 2, 0, Ext),
                  coffSymbol("ro", 0, 3, 0, COFF::IMAGE_SYM_CLASS_STATIC),
                  coffSymbol("zeroed", 0, 4, 0, Ext),
                  coffSymbol("common_c", 12, 0, 0, Ext),
                  coffSymbol("undefined_function", 0, 0, 0x20, Ext)};
    C.ExtraSections = I->Markers;
    C.CoffSymbols = I->Symbols;
  }
  SmallVector<uint8_t, 0> Out;
  EXPECT_THAT_ERROR(writePEImage(*O, C, Out, &I->Id), Succeeded());
  I->File.assign(Out.begin(), Out.end());
  return I;
}

// An AnyCPU-shaped PE32 image (PE/COFF specification, Roslyn's anycpu values
// of metadata-writer 3.3): COFF machine I386, 32-bit optional header with
// NumDirs data directories, one .text section holding the CLI header and the
// metadata root at RVA 0x2048.
ByteVec buildPE32(ArrayRef<uint8_t> Metadata, uint32_t NumDirs = 16) {
  const uint32_t TextRVA = 0x2000, TextRaw = 0x200;
  uint32_t TextSize = 72 + Metadata.size();
  uint32_t RawSize = alignTo(TextSize, 0x200);
  ByteVec F(TextRaw + RawSize, 0);
  std::memcpy(F.data(), ClassicDOSHeader, sizeof(ClassicDOSHeader));
  w32(F, PESignature, 0x00004550);
  w16(F, COFFHeader, COFF::IMAGE_FILE_MACHINE_I386);
  w16(F, COFFHeader + 2, 1);          // NumberOfSections
  w32(F, COFFHeader + 4, 0x9ABCDEF0); // TimeDateStamp
  w16(F, COFFHeader + 16, 96 + 8 * NumDirs);
  w16(F, COFFHeader + 18, 0x2022); // EXECUTABLE | LARGE_ADDRESS | DLL
  const uint64_t O = OptionalHeader;
  w16(F, O, 0x10B);
  F[O + 2] = 48;
  w32(F, O + 4, RawSize);     // SizeOfCode
  w32(F, O + 20, TextRVA);    // BaseOfCode
  w32(F, O + 28, 0x10000000); // ImageBase (a PE32 library)
  w32(F, O + 32, 0x2000);
  w32(F, O + 36, 0x200);
  w16(F, O + 40, 4);
  w16(F, O + 48, 4);
  w32(F, O + 56, alignTo(TextRVA + TextSize, 0x2000)); // SizeOfImage
  w32(F, O + 60, TextRaw);                             // SizeOfHeaders
  w16(F, O + 68, COFF::IMAGE_SUBSYSTEM_WINDOWS_CUI);
  w16(F, O + 70, 0x8540);
  w32(F, O + 72, 0x100000);
  w32(F, O + 76, 0x1000);
  w32(F, O + 80, 0x100000);
  w32(F, O + 84, 0x1000);
  w32(F, O + 92, NumDirs);
  if (NumDirs > COFF::CLR_RUNTIME_HEADER) {
    w32(F, O + 96 + 8 * COFF::CLR_RUNTIME_HEADER, TextRVA);
    w32(F, O + 100 + 8 * COFF::CLR_RUNTIME_HEADER, 72);
  }
  const uint64_t S = O + 96 + 8 * NumDirs;
  std::memcpy(&F[S], ".text", 5);
  w32(F, S + 8, TextSize);
  w32(F, S + 12, TextRVA);
  w32(F, S + 16, RawSize);
  w32(F, S + 20, TextRaw);
  w32(F, S + 36, 0x60000020);
  const uint64_t H = TextRaw;
  w32(F, H, 72);
  w16(F, H + 4, 2);
  w16(F, H + 6, 5);
  w32(F, H + 8, TextRVA + 72);
  w32(F, H + 12, Metadata.size());
  w32(F, H + 16, COMIMAGE_FLAGS_ILONLY);
  std::copy(Metadata.begin(), Metadata.end(), F.begin() + H + 72);
  return F;
}

// The metadata of a small library without method bodies.
ByteVec smallMetadata() {
  MetadataBuilder MB;
  MB.setModuleName("anycpu.dll");
  AssemblyIdentity Id;
  Id.Name = "anycpu";
  Id.Version = {1, 0, 0, 0};
  MB.setAssembly(Id);
  AssemblyRefInfo R;
  R.Name = "mscorlib";
  R.Version = {4, 0, 0, 0};
  EntityHandle Obj = MB.addTypeRef(MB.addAssemblyRef(R), "System", "Object");
  MB.addTypeDef(TypeAttributes::Public, "N", "C", Obj);
  MethodBodyStream Bodies;
  EXPECT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  ByteVec MD(MB.getSerializedSize());
  MB.serialize(MD, 0, 0);
  return MD;
}

// The section header I (40 bytes) of an image written by writePEImage.
uint64_t sectionHeader(ArrayRef<uint8_t> F, unsigned I) {
  return COFFHeader + 20 + r16(F, COFFHeader + 16) + 40ull * I;
}

//===----------------------------------------------------------------------===//
// Our images
//===----------------------------------------------------------------------===//

TEST(CLIPEImageTest, OurExecutable) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Executable);
  ArrayRef<uint8_t> F = I->File;
  Expected<PEImage> P = PEImage::create(F);
  ASSERT_THAT_EXPECTED(P, Succeeded());
  EXPECT_EQ(P->getFile().data(), F.data());
  EXPECT_EQ(P->getFile().size(), F.size());
  EXPECT_EQ(P->getMachine(), uint16_t(COFF::IMAGE_FILE_MACHINE_AMD64));
  EXPECT_TRUE(P->isPE32Plus());
  EXPECT_FALSE(P->isDll());
  EXPECT_EQ(P->getCharacteristics(), 0x0022);
  EXPECT_EQ(P->getTimeDateStamp(), I->Id.Stamp);
  EXPECT_EQ(P->getImageBase(), 0x140000000u);
  EXPECT_EQ(P->getSubsystem(), uint16_t(COFF::IMAGE_SUBSYSTEM_WINDOWS_CUI));
  EXPECT_EQ(P->getDllCharacteristics(), 0x8540);
  EXPECT_EQ(P->getSizeOfImage(), r32(F, OptionalHeader + 56));

  ASSERT_EQ(P->sections().size(), 1u);
  const PESectionInfo &T = P->sections()[0];
  uint64_t S = sectionHeader(F, 0);
  EXPECT_EQ(T.Name, ".text");
  EXPECT_EQ(T.VirtualSize, r32(F, S + 8));
  EXPECT_EQ(T.VirtualAddress, 0x2000u);
  EXPECT_EQ(T.SizeOfRawData, r32(F, S + 16));
  EXPECT_EQ(T.PointerToRawData, 0x200u);
  EXPECT_EQ(T.Characteristics, 0x60000020u);

  std::optional<std::pair<uint32_t, uint32_t>> CLR =
      P->getDataDirectory(COFF::CLR_RUNTIME_HEADER);
  ASSERT_TRUE(CLR.has_value());
  EXPECT_EQ(CLR->first, 0x2000u);
  EXPECT_EQ(CLR->second, 72u);
  for (unsigned D : {0u, 1u, 2u, 5u, 6u, 12u, 15u})
    EXPECT_FALSE(P->getDataDirectory(D).has_value()) << "directory " << D;

  ASSERT_TRUE(P->hasCLIHeader());
  Expected<Cor20Header> H = P->getCLIHeader();
  ASSERT_THAT_EXPECTED(H, Succeeded());
  EXPECT_EQ(uint32_t(H->Cb), 72u);
  EXPECT_EQ(uint16_t(H->MajorRuntimeVersion), 2);
  EXPECT_EQ(uint16_t(H->MinorRuntimeVersion), 5);
  EXPECT_EQ(uint32_t(H->Flags), uint32_t(COMIMAGE_FLAGS_ILONLY));
  EXPECT_EQ(uint32_t(H->EntryPointToken), I->MainToken);
  EXPECT_EQ(uint32_t(H->MetaData.Size), I->MB->getSerializedSize());

  // The metadata root through getRVAData and getRVATail.
  uint32_t MD = H->MetaData.RVA;
  Expected<ArrayRef<uint8_t>> Sig = P->getRVAData(MD, 4);
  ASSERT_THAT_EXPECTED(Sig, Succeeded());
  EXPECT_EQ(*Sig, arrayRefFromStringRef("BSJB"));
  EXPECT_EQ(Sig->data(), F.data() + T.PointerToRawData + (MD - 0x2000));
  Expected<ArrayRef<uint8_t>> Tail = P->getRVATail(MD);
  ASSERT_THAT_EXPECTED(Tail, Succeeded());
  EXPECT_EQ(Tail->data(), Sig->data());
  EXPECT_GE(Tail->size(), uint64_t(H->MetaData.Size));
  EXPECT_LE(Tail->size(), T.SizeOfRawData - (MD - 0x2000));
  // The IL stream follows the 72-byte CLI header.
  Expected<ArrayRef<uint8_t>> IL =
      P->getRVAData(0x2048, I->Bodies.getBytes().size());
  ASSERT_THAT_EXPECTED(IL, Succeeded());
  EXPECT_EQ(*IL, I->Bodies.getBytes());

  Expected<std::vector<CoffSymbol>> Syms = P->getCoffSymbols();
  ASSERT_THAT_EXPECTED(Syms, Succeeded());
  EXPECT_TRUE(Syms->empty());
}

TEST(CLIPEImageTest, OurLibrary) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Library);
  Expected<PEImage> P = PEImage::create(I->File);
  ASSERT_THAT_EXPECTED(P, Succeeded());
  EXPECT_TRUE(P->isDll());
  EXPECT_TRUE(P->isPE32Plus());
  EXPECT_EQ(P->getCharacteristics(), 0x2022);
  EXPECT_EQ(P->getImageBase(), 0x180000000u);
  Expected<Cor20Header> H = P->getCLIHeader();
  ASSERT_THAT_EXPECTED(H, Succeeded());
  EXPECT_EQ(uint32_t(H->EntryPointToken), 0u);
}

// 5.8.6's view, read back through PEImage::getCoffSymbols (5.7.9).
TEST(CLIPEImageTest, OurObjectWithCOFFView) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Module, true);
  Expected<PEImage> P = PEImage::create(I->File);
  ASSERT_THAT_EXPECTED(P, Succeeded());
  EXPECT_TRUE(P->isDll());
  ASSERT_EQ(P->sections().size(), 4u);
  for (unsigned S = 0; S < 3; ++S) {
    const PESectionInfo &M = P->sections()[S + 1];
    EXPECT_EQ(M.Name, I->Markers[S].Name);
    EXPECT_EQ(M.Characteristics, I->Markers[S].Characteristics);
    EXPECT_EQ(M.VirtualSize, 0u);
    EXPECT_EQ(M.SizeOfRawData, 0u);
    EXPECT_EQ(M.PointerToRawData, 0u);
    // A marker section maps no bytes.
    expectImageError(P->getRVAData(M.VirtualAddress, 1));
  }
  Expected<std::vector<CoffSymbol>> Syms = P->getCoffSymbols();
  ASSERT_THAT_EXPECTED(Syms, Succeeded());
  ASSERT_EQ(Syms->size(), I->Symbols.size());
  for (unsigned S = 0; S < Syms->size(); ++S) {
    const CoffSymbol &A = (*Syms)[S], &E = I->Symbols[S];
    SCOPED_TRACE(E.Name);
    EXPECT_EQ(A.Name, E.Name);
    EXPECT_EQ(A.Value, E.Value);
    EXPECT_EQ(A.SectionNumber, E.SectionNumber);
    EXPECT_EQ(A.Type, E.Type);
    EXPECT_EQ(A.StorageClass, E.StorageClass);
  }
}

//===----------------------------------------------------------------------===//
// A hand-built PE32 image (the AnyCPU reference assemblies are PE32)
//===----------------------------------------------------------------------===//

TEST(CLIPEImageTest, PE32Image) {
  ByteVec MD = smallMetadata();
  ByteVec F = buildPE32(MD);
  Expected<PEImage> P = PEImage::create(F);
  ASSERT_THAT_EXPECTED(P, Succeeded());
  EXPECT_FALSE(P->isPE32Plus());
  EXPECT_TRUE(P->isDll());
  EXPECT_EQ(P->getMachine(), uint16_t(COFF::IMAGE_FILE_MACHINE_I386));
  EXPECT_EQ(P->getCharacteristics(), 0x2022);
  EXPECT_EQ(P->getTimeDateStamp(), 0x9ABCDEF0u);
  EXPECT_EQ(P->getImageBase(), 0x10000000u);
  EXPECT_EQ(P->getSubsystem(), uint16_t(COFF::IMAGE_SUBSYSTEM_WINDOWS_CUI));
  EXPECT_EQ(P->getDllCharacteristics(), 0x8540);
  EXPECT_EQ(P->getSizeOfImage(), 0x4000u);
  ASSERT_EQ(P->sections().size(), 1u);
  EXPECT_EQ(P->sections()[0].Name, ".text");
  EXPECT_EQ(P->sections()[0].VirtualSize, 72 + MD.size());
  std::optional<std::pair<uint32_t, uint32_t>> CLR =
      P->getDataDirectory(COFF::CLR_RUNTIME_HEADER);
  ASSERT_TRUE(CLR.has_value());
  EXPECT_EQ(*CLR, std::make_pair(uint32_t(0x2000), uint32_t(72)));
  ASSERT_TRUE(P->hasCLIHeader());
  Expected<Cor20Header> H = P->getCLIHeader();
  ASSERT_THAT_EXPECTED(H, Succeeded());
  EXPECT_EQ(uint32_t(H->MetaData.RVA), 0x2048u);
  Expected<ArrayRef<uint8_t>> Root = P->getRVAData(0x2048, MD.size());
  ASSERT_THAT_EXPECTED(Root, Succeeded());
  EXPECT_EQ(*Root, ArrayRef<uint8_t>(MD));
  Expected<std::vector<CoffSymbol>> Syms = P->getCoffSymbols();
  ASSERT_THAT_EXPECTED(Syms, Succeeded());
  EXPECT_TRUE(Syms->empty());

  // The metadata reader takes PE32 images too.
  Expected<std::unique_ptr<MetadataReader>> R = MetadataReader::create(F);
  ASSERT_THAT_EXPECTED(R, Succeeded());
  EXPECT_TRUE((*R)->isAssembly());
  ASSERT_NE((*R)->getImage(), nullptr);
  EXPECT_FALSE((*R)->getImage()->isPE32Plus());
  Expected<ModuleRow> Mod = (*R)->getModule();
  ASSERT_THAT_EXPECTED(Mod, Succeeded());
  EXPECT_EQ(Mod->Name, "anycpu.dll");
}

// Fewer than 15 data directories: a valid PE without a CLI header.
TEST(CLIPEImageTest, FewerThan15DataDirectories) {
  ByteVec F = buildPE32(smallMetadata(), 14);
  Expected<PEImage> P = PEImage::create(F);
  ASSERT_THAT_EXPECTED(P, Succeeded());
  EXPECT_FALSE(P->hasCLIHeader());
  EXPECT_FALSE(P->getDataDirectory(COFF::CLR_RUNTIME_HEADER).has_value());
  expectImageError(P->getCLIHeader());
  EXPECT_THAT_EXPECTED(MetadataReader::create(F), Failed());
}

// A zero CLR directory is no CLI header either.
TEST(CLIPEImageTest, ZeroCLRDirectory) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Library);
  ByteVec F = I->File;
  w32(F, OptionalHeader + 112 + 8 * COFF::CLR_RUNTIME_HEADER, 0);
  w32(F, OptionalHeader + 116 + 8 * COFF::CLR_RUNTIME_HEADER, 0);
  Expected<PEImage> P = PEImage::create(F);
  ASSERT_THAT_EXPECTED(P, Succeeded());
  EXPECT_FALSE(P->hasCLIHeader());
  expectImageError(P->getCLIHeader());
  EXPECT_THAT_EXPECTED(MetadataReader::create(F), Failed());
}

//===----------------------------------------------------------------------===//
// Malformed images
//===----------------------------------------------------------------------===//

TEST(CLIPEImageTest, TruncatedDOSHeader) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Executable);
  for (size_t N :
       {size_t(0), size_t(1), size_t(2), size_t(0x3C), size_t(0x3F)}) {
    SCOPED_TRACE("size " + std::to_string(N));
    expectImageError(PEImage::create(ArrayRef<uint8_t>(I->File).take_front(N)));
  }
}

TEST(CLIPEImageTest, BadDOSSignature) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Executable);
  ByteVec F = I->File;
  F[1] = 'X';
  expectImageError(PEImage::create(F));
}

TEST(CLIPEImageTest, BadLfanew) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Executable);
  for (uint32_t Lfanew :
       {uint32_t(I->File.size()), uint32_t(I->File.size() - 2), 0xFFFFFFF0u,
        0x7FFFFFFFu}) {
    SCOPED_TRACE("e_lfanew 0x" + utohexstr(Lfanew));
    ByteVec F = I->File;
    w32(F, 0x3C, Lfanew);
    expectImageError(PEImage::create(F));
  }
}

TEST(CLIPEImageTest, BadPESignature) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Executable);
  ByteVec F = I->File;
  F[PESignature + 3] = 1; // "PE\0\1"
  expectImageError(PEImage::create(F));
  F = I->File;
  F[PESignature] = 'N'; // "NE\0\0"
  expectImageError(PEImage::create(F));
}

TEST(CLIPEImageTest, TruncatedHeaders) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Executable);
  ArrayRef<uint8_t> F = I->File;
  // Inside the COFF header, inside the optional header, inside the section
  // table.
  for (size_t N :
       {size_t(COFFHeader + 10), size_t(OptionalHeader + 2),
        size_t(OptionalHeader + 0x70), size_t(OptionalHeader + 0xF8)}) {
    SCOPED_TRACE("size 0x" + utohexstr(N));
    expectImageError(PEImage::create(F.take_front(N)));
  }
}

TEST(CLIPEImageTest, BadOptionalHeader) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Executable);
  ByteVec F = I->File;
  w16(F, OptionalHeader, 0x10C); // neither PE32 (0x10B) nor PE32+ (0x20B)
  expectImageError(PEImage::create(F));
  // A SizeOfOptionalHeader too small for the PE32+ fields.
  F = I->File;
  w16(F, COFFHeader + 16, 0x40);
  expectImageError(PEImage::create(F));
}

TEST(CLIPEImageTest, SectionTableOutOfRange) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Executable);
  ByteVec F = I->File;
  // 0x2000 section headers (320 KiB) do not fit in the file.
  w16(F, COFFHeader + 2, 0x2000);
  expectImageError(PEImage::create(F));
}

// Raw data past the end of the file: either create() refuses the image or
// nothing in that section can be read.
TEST(CLIPEImageTest, SectionRawDataOutOfRange) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Executable);
  for (int Variant = 0; Variant < 2; ++Variant) {
    SCOPED_TRACE("variant " + std::to_string(Variant));
    ByteVec F = I->File;
    uint64_t S = sectionHeader(F, 0);
    if (Variant == 0)
      w32(F, S + 20, F.size()); // PointerToRawData at the end of the file
    else
      w32(F, S + 16, 0x7FFFFE00); // SizeOfRawData far past the end
    Expected<PEImage> P = PEImage::create(F);
    if (!P) {
      EXPECT_THAT(toString(P.takeError()), StartsWith(ImagePrefix));
      continue;
    }
    if (Variant == 0) {
      expectImageError(P->getRVAData(0x2000, 4));
      expectImageError(P->getCLIHeader());
    } else {
      expectImageError(P->getRVAData(0x2000 + uint32_t(F.size()), 4));
    }
  }
}

TEST(CLIPEImageTest, UnmappedRVAs) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Executable);
  Expected<PEImage> P = PEImage::create(I->File);
  ASSERT_THAT_EXPECTED(P, Succeeded());
  const PESectionInfo &T = P->sections()[0];
  uint32_t RawEnd = T.VirtualAddress + T.SizeOfRawData;
  // In the headers, between sections and past the image.
  expectImageError(P->getRVAData(0x10, 4));
  expectImageError(P->getRVAData(0x1FFF, 2));
  expectImageError(P->getRVAData(0x100000, 1));
  expectImageError(P->getRVAData(0xFFFFFFF0, 0x20));
  // Straddling the end of .text's raw data, and an overflowing size.
  expectImageError(P->getRVAData(RawEnd - 2, 4));
  expectImageError(P->getRVAData(0x2000, 0xFFFFFFFF));
  expectImageError(P->getRVATail(0x100000));
  expectImageError(P->getRVATail(0x10));
  // The last byte of the raw data is readable.
  Expected<ArrayRef<uint8_t>> Last = P->getRVAData(RawEnd - 1, 1);
  ASSERT_THAT_EXPECTED(Last, Succeeded());
  EXPECT_EQ(Last->size(), 1u);
}

// A CLR directory that points outside every section: either create()
// refuses the image or getCLIHeader() does.
TEST(CLIPEImageTest, CLIHeaderOutOfRange) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Executable);
  ByteVec F = I->File;
  w32(F, OptionalHeader + 112 + 8 * COFF::CLR_RUNTIME_HEADER, 0x9000);
  Expected<PEImage> P = PEImage::create(F);
  if (!P) {
    EXPECT_THAT(toString(P.takeError()), StartsWith(ImagePrefix));
    return;
  }
  expectImageError(P->getCLIHeader());
  EXPECT_THAT_EXPECTED(MetadataReader::create(F), Failed());
}

// A symbol table or string table outside the file gives an Error (from
// create() or getCoffSymbols()).
TEST(CLIPEImageTest, COFFSymbolsOutOfRange) {
  std::unique_ptr<Image> I = writeImage(PEImageKind::Module, true);
  uint32_t Ptr = r32(I->File, COFFHeader + 8);
  ASSERT_NE(Ptr, 0u);
  auto Check = [](const ByteVec &F) {
    Expected<PEImage> P = PEImage::create(F);
    if (!P) {
      EXPECT_THAT(toString(P.takeError()), StartsWith(ImagePrefix));
      return;
    }
    expectImageError(P->getCoffSymbols());
  };
  {
    SCOPED_TRACE("symbol table past the end");
    ByteVec F = I->File;
    w32(F, COFFHeader + 8, F.size() + 0x100);
    Check(F);
  }
  {
    SCOPED_TRACE("too many symbols");
    ByteVec F = I->File;
    w32(F, COFFHeader + 12, 0x10000000);
    Check(F);
  }
  {
    SCOPED_TRACE("long name offset past the string table");
    ByteVec F = I->File;
    // Symbol 1 ("a_long_data_name") has its name in the string table.
    ASSERT_EQ(r32(F, Ptr + 18), 0u);
    w32(F, Ptr + 18 + 4, 0x00FFFFFF);
    Check(F);
  }
  {
    SCOPED_TRACE("string table cut off");
    ByteVec F = I->File;
    F.resize(Ptr + 18 * I->Symbols.size() + 2);
    Check(F);
  }
}

} // namespace
