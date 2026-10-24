//===- PEWriter.cpp - IL-only PE image writer -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The PE32+ envelope of II.25 with Roslyn's layout: the headers, one .text
// section (CLI header, IL, metadata, debug directory, mapped field data), the
// caller's extra sections, the COFF symbol view of CIL objects, and the
// deterministic content id. Every field is written little-endian through
// support::endian, so the bytes do not depend on the host.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/PEWriter.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SHA256.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstring>

#define DEBUG_TYPE "cli-pewriter"

using namespace llvm;
using namespace llvm::cli;
using namespace llvm::support;

static constexpr char ErrorPrefix[] = "cannot write PE image: ";

/// The classic 128-byte DOS header and stub that Roslyn and ilasm write:
/// e_lfanew (offset 0x3C) is 0x80, where the PE signature follows.
static constexpr uint8_t DOSStub[] = {
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
static_assert(sizeof(DOSStub) == 128, "the DOS header and stub are 128 bytes");

// File offsets of the headers.
static constexpr uint32_t PESignatureOffset = sizeof(DOSStub);
static constexpr uint32_t COFFHeaderOffset =
    PESignatureOffset + sizeof(COFF::PEMagic);
static constexpr uint32_t OptionalHeaderOffset =
    COFFHeaderOffset + COFF::Header16Size;
/// The PE32+ optional header: 112 bytes of fields, then 16 data directories.
static constexpr uint32_t NumDataDirectories = 16;
static constexpr uint32_t DataDirectoriesOffset = OptionalHeaderOffset + 112;
static constexpr uint32_t OptionalHeaderSize = 112 + 8 * NumDataDirectories;
static constexpr uint32_t SectionTableOffset =
    OptionalHeaderOffset + OptionalHeaderSize;
static_assert(SectionTableOffset == 0x188, "PE32+ section table offset");

static constexpr uint32_t CLIHeaderSize = sizeof(Cor20Header);
/// IMAGE_DEBUG_DIRECTORY.
static constexpr uint32_t DebugEntrySize = 28;
static constexpr uint32_t TextCharacteristics = COFF::IMAGE_SCN_CNT_CODE |
                                                COFF::IMAGE_SCN_MEM_EXECUTE |
                                                COFF::IMAGE_SCN_MEM_READ;
static_assert(TextCharacteristics == 0x60000020, ".text characteristics");

static Error makeError(const Twine &Msg, errc EC = errc::invalid_argument) {
  return createStringError(EC, Twine(ErrorPrefix) + Msg);
}

/// \p V as 0x and \p Digits upper-case hex digits.
static std::string hex(uint64_t V, unsigned Digits = 8) {
  return "0x" + utohexstr(V, /*LowerCase=*/false, Digits);
}

//===----------------------------------------------------------------------===//
// PEOptions and BlobContentId
//===----------------------------------------------------------------------===//

Expected<PEOptions> PEOptions::get(const Triple &TT, PEImageKind Kind) {
  PEOptions Opts;
  switch (TT.getArch()) {
  case Triple::x86_64:
    Opts.Machine = COFF::IMAGE_FILE_MACHINE_AMD64;
    break;
  case Triple::aarch64:
    Opts.Machine = COFF::IMAGE_FILE_MACHINE_ARM64;
    break;
  default:
    // PE32 images (i686) need the mscoree import, the entry stub and .reloc.
    return makeError("architecture '" + TT.getArchName() +
                         "' is not supported until phase 5",
                     errc::not_supported);
  }
  Opts.Kind = Kind;
  Opts.ImageBase =
      Kind == PEImageKind::Executable ? 0x140000000ULL : 0x180000000ULL;
  return Opts;
}

BlobContentId BlobContentId::fromHash(ArrayRef<uint8_t> Hash) {
  assert(Hash.size() >= 20 && "a content id needs at least 20 hash bytes");
  BlobContentId Id;
  std::copy_n(Hash.begin(), Id.Guid.size(), Id.Guid.begin());
  // Byte 7 is the high byte of Data3 (the version), byte 8 the variant.
  Id.Guid[7] = (Id.Guid[7] & 0x0F) | 0x40;
  Id.Guid[8] = (Id.Guid[8] & 0x3F) | 0x80;
  Id.Stamp = endian::read32le(Hash.data() + 16) | 0x80000000U;
  return Id;
}

//===----------------------------------------------------------------------===//
// writePEImage
//===----------------------------------------------------------------------===//

namespace {

/// One section of the image and where it goes.
struct SectionLayout {
  StringRef Name;
  uint32_t Characteristics = 0;
  ArrayRef<uint8_t> Data; // Empty for .text, which is written in place.
  uint64_t VirtualAddress = 0, VirtualSize = 0;
  uint64_t PointerToRawData = 0, SizeOfRawData = 0;
};

/// The offsets of the parts of .text, relative to its start.
struct TextLayout {
  uint64_t IL = 0, Metadata = 0, MetadataSize = 0, DebugDirectory = 0,
           MappedData = 0, Size = 0;
  SmallVector<uint64_t, 1> DebugData; // 0 for an entry without data.
};

} // namespace

/// The checks of 5.7.7 on the content, in a fixed order.
static Error checkContent(const PEOptions &Opts, const PEContent &C) {
  const MetadataBuilder *MD = C.Metadata;
  if (!MD)
    return makeError("no metadata");
  if (!MD->isFinalized())
    return makeError("the metadata is not finalized");

  StringRef KindName = Opts.Kind == PEImageKind::Executable ? "an executable"
                       : Opts.Kind == PEImageKind::Library  ? "a library"
                                                            : "a module";
  // Q1: a netmodule (a CIL object) has no Assembly row; every other image
  // defines an assembly.
  if (Opts.Kind == PEImageKind::Module) {
    if (MD->hasAssembly())
      return makeError("a module cannot have an Assembly row");
  } else if (!MD->hasAssembly()) {
    return makeError(KindName + " needs an Assembly row");
  }

  // A DLL with an entry point runs, but PEVerify /md reports 0x80131B21.
  uint32_t Entry = C.EntryPointToken;
  if (Opts.Kind == PEImageKind::Executable) {
    if (Entry == 0)
      return makeError("an executable needs an entry point");
    uint32_t Row = getTokenRow(Entry);
    if (getTokenType(Entry) != static_cast<uint8_t>(TableId::MethodDef) ||
        Row == 0 || Row > MD->getRowCount(TableId::MethodDef))
      return makeError("entry point token " + hex(Entry) +
                       " is not a MethodDef row of the module");
  } else if (Entry != 0) {
    return makeError(KindName + " cannot have an entry point (token " +
                     hex(Entry) + ")");
  }

  // Returned unchanged: "cannot write CLI metadata: method ... has no body".
  if (Error E = MD->checkMethodBodies())
    return E;

  if (Opts.Machine != COFF::IMAGE_FILE_MACHINE_AMD64 &&
      Opts.Machine != COFF::IMAGE_FILE_MACHINE_ARM64)
    return makeError("machine " + hex(Opts.Machine, 4) +
                         " is not supported until phase 5 (only PE32+ "
                         "images are written)",
                     errc::not_supported);
  if (!isPowerOf2_32(Opts.SectionAlignment) ||
      !isPowerOf2_32(Opts.FileAlignment) ||
      Opts.FileAlignment > Opts.SectionAlignment)
    return makeError("invalid alignments: section " +
                     hex(Opts.SectionAlignment) + ", file " +
                     hex(Opts.FileAlignment));

  uint64_t NumSections = 1 + C.ExtraSections.size();
  if (NumSections > static_cast<uint64_t>(COFF::MaxNumberOfSections16))
    return makeError("too many sections (" + Twine(NumSections) + ")");
  for (const PESection &S : C.ExtraSections)
    if (S.Name.size() > COFF::NameSize)
      return makeError("section name '" + S.Name + "' is longer than " +
                       Twine(static_cast<unsigned>(COFF::NameSize)) + " bytes");

  for (auto [I, S] : enumerate(C.CoffSymbols)) {
    if (S.Name.empty())
      return makeError("COFF symbol " + Twine(I) + " has an empty name");
    if (S.Name.find('\0') != std::string::npos)
      return makeError("COFF symbol " + Twine(I) +
                       " has a NUL byte in its name");
    if (S.SectionNumber < COFF::IMAGE_SYM_DEBUG ||
        S.SectionNumber > static_cast<int64_t>(NumSections))
      return makeError("COFF symbol '" + S.Name +
                       "' has an invalid section number " +
                       Twine(S.SectionNumber) + " (sections are 1 to " +
                       Twine(NumSections) + ")");
  }
  return Error::success();
}

/// Lays out .text (5.7.7): the CLI header, the IL stream (4-aligned), the
/// metadata (4-aligned), the debug directory and its data (4-aligned), and
/// the mapped field data (8-aligned, last).
static TextLayout layOutText(const PEContent &C) {
  TextLayout L;
  L.IL = alignTo(CLIHeaderSize, 4);
  L.Metadata = alignTo(L.IL + C.ILStream.size(), 4);
  L.MetadataSize = C.Metadata->getSerializedSize();
  uint64_t End = L.Metadata + L.MetadataSize;
  if (!C.DebugEntries.empty()) {
    L.DebugDirectory = alignTo(End, 4);
    End = L.DebugDirectory + DebugEntrySize * C.DebugEntries.size();
    for (const DebugDirectoryEntry &D : C.DebugEntries) {
      if (D.Data.empty()) {
        L.DebugData.push_back(0);
        continue;
      }
      uint64_t Off = alignTo(End, 4);
      L.DebugData.push_back(Off);
      End = Off + D.Data.size();
    }
  }
  // The mapped data RVA is 8-aligned even when there is no data.
  L.MappedData = alignTo(End, 8);
  if (!C.MappedFieldData.empty())
    End = L.MappedData + C.MappedFieldData.size();
  L.Size = End;
  return L;
}

Error cli::writePEImage(const PEOptions &Opts, const PEContent &C,
                        SmallVectorImpl<uint8_t> &Out, BlobContentId *Id) {
  if (Error E = checkContent(Opts, C))
    return E;
  const MetadataBuilder &MD = *C.Metadata;

  // Headers and sections.
  uint64_t NumSections = 1 + C.ExtraSections.size();
  uint64_t SizeOfHeaders = alignTo(
      SectionTableOffset + COFF::SectionSize * NumSections, Opts.FileAlignment);
  TextLayout Text = layOutText(C);

  SmallVector<SectionLayout, 4> Sections;
  Sections.reserve(NumSections);
  {
    SectionLayout &L = Sections.emplace_back();
    L.Name = ".text";
    L.Characteristics = TextCharacteristics;
    L.VirtualAddress = alignTo(SizeOfHeaders, Opts.SectionAlignment);
    L.VirtualSize = Text.Size;
    L.PointerToRawData = SizeOfHeaders;
    L.SizeOfRawData = alignTo(Text.Size, Opts.FileAlignment);
  }
  // Read after every emplace_back, never through a reference.
  const uint64_t TextRVA = Sections[0].VirtualAddress;
  const uint64_t T = Sections[0].PointerToRawData;
  uint64_t NextRVA = alignTo(TextRVA + Text.Size, Opts.SectionAlignment);
  uint64_t NextRaw = T + Sections[0].SizeOfRawData;
  for (const PESection &S : C.ExtraSections) {
    SectionLayout &L = Sections.emplace_back();
    L.Name = S.Name;
    L.Characteristics = S.Characteristics;
    L.Data = S.Data;
    // A marker section (5.8.6) takes neither address space nor file space.
    L.VirtualAddress = NextRVA;
    if (S.Data.empty())
      continue;
    L.VirtualSize = S.Data.size();
    L.PointerToRawData = NextRaw;
    L.SizeOfRawData = alignTo(S.Data.size(), Opts.FileAlignment);
    NextRVA = alignTo(L.VirtualAddress + L.VirtualSize, Opts.SectionAlignment);
    NextRaw += L.SizeOfRawData;
  }
  uint64_t SizeOfImage = NextRVA;

  // The COFF view: after the last raw data, 4-aligned, outside SizeOfImage.
  uint64_t SymbolTable = 0, StringTableSize = 4, FileSize = NextRaw;
  if (!C.CoffSymbols.empty()) {
    SymbolTable = alignTo(NextRaw, 4);
    for (const CoffSymbol &S : C.CoffSymbols)
      if (S.Name.size() > COFF::NameSize)
        StringTableSize += S.Name.size() + 1;
    FileSize = SymbolTable + COFF::Symbol16Size * C.CoffSymbols.size() +
               StringTableSize;
  }
  if (SizeOfImage > UINT32_MAX || FileSize > UINT32_MAX)
    return makeError("the image would be larger than 4 GiB");

  uint64_t SizeOfCode = 0, SizeOfInitializedData = 0,
           SizeOfUninitializedData = 0;
  for (const SectionLayout &S : Sections) {
    if (S.Characteristics & COFF::IMAGE_SCN_CNT_CODE)
      SizeOfCode += S.SizeOfRawData;
    if (S.Characteristics & COFF::IMAGE_SCN_CNT_INITIALIZED_DATA)
      SizeOfInitializedData += S.SizeOfRawData;
    if (S.Characteristics & COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA)
      SizeOfUninitializedData += S.SizeOfRawData;
  }

  Out.assign(FileSize, 0);
  uint8_t *Buf = Out.data();
  auto W16 = [Buf](uint64_t Off, uint64_t V) {
    assert(V <= UINT16_MAX && "field does not fit in 16 bits");
    endian::write16le(Buf + Off, static_cast<uint16_t>(V));
  };
  auto W32 = [Buf](uint64_t Off, uint64_t V) {
    assert(V <= UINT32_MAX && "field does not fit in 32 bits");
    endian::write32le(Buf + Off, static_cast<uint32_t>(V));
  };
  auto W64 = [Buf](uint64_t Off, uint64_t V) {
    endian::write64le(Buf + Off, V);
  };
  auto Copy = [Buf](uint64_t Off, ArrayRef<uint8_t> Bytes) {
    if (!Bytes.empty())
      std::memcpy(Buf + Off, Bytes.data(), Bytes.size());
  };

  // DOS header and stub, PE signature.
  Copy(0, DOSStub);
  std::memcpy(Buf + PESignatureOffset, COFF::PEMagic, sizeof(COFF::PEMagic));

  // COFF header. TimeDateStamp is the content id's stamp, patched below.
  uint16_t Characteristics =
      COFF::IMAGE_FILE_EXECUTABLE_IMAGE | COFF::IMAGE_FILE_LARGE_ADDRESS_AWARE;
  if (Opts.Kind != PEImageKind::Executable)
    Characteristics |= COFF::IMAGE_FILE_DLL;
  W16(COFFHeaderOffset + 0, Opts.Machine);
  W16(COFFHeaderOffset + 2, NumSections);
  W32(COFFHeaderOffset + 4, 0);
  W32(COFFHeaderOffset + 8, SymbolTable);
  W32(COFFHeaderOffset + 12, C.CoffSymbols.size());
  W16(COFFHeaderOffset + 16, OptionalHeaderSize);
  W16(COFFHeaderOffset + 18, Characteristics);

  // PE32+ optional header.
  uint64_t P = OptionalHeaderOffset;
  W16(P + 0, COFF::PE32Header::PE32_PLUS);
  Buf[P + 2] = Opts.MajorLinkerVersion;
  Buf[P + 3] = Opts.MinorLinkerVersion;
  W32(P + 4, SizeOfCode);
  W32(P + 8, SizeOfInitializedData);
  W32(P + 12, SizeOfUninitializedData);
  W32(P + 16, 0);       // AddressOfEntryPoint: no entry stub.
  W32(P + 20, TextRVA); // BaseOfCode
  W64(P + 24, Opts.ImageBase);
  W32(P + 32, Opts.SectionAlignment);
  W32(P + 36, Opts.FileAlignment);
  W16(P + 40, 4); // MajorOperatingSystemVersion
  W16(P + 42, 0);
  W16(P + 44, 0); // MajorImageVersion
  W16(P + 46, 0);
  W16(P + 48, 4); // MajorSubsystemVersion
  W16(P + 50, 0);
  W32(P + 52, 0); // Win32VersionValue
  W32(P + 56, SizeOfImage);
  W32(P + 60, SizeOfHeaders);
  W32(P + 64, 0); // CheckSum
  W16(P + 68, Opts.Subsystem);
  W16(P + 70, Opts.DllCharacteristics);
  W64(P + 72, Opts.StackReserve);
  W64(P + 80, Opts.StackCommit);
  W64(P + 88, Opts.HeapReserve);
  W64(P + 96, Opts.HeapCommit);
  W32(P + 104, 0); // LoaderFlags
  W32(P + 108, NumDataDirectories);

  // Data directories: only CLR and, with debug entries, Debug.
  if (!C.DebugEntries.empty()) {
    W32(DataDirectoriesOffset + 8 * COFF::DEBUG_DIRECTORY,
        TextRVA + Text.DebugDirectory);
    W32(DataDirectoriesOffset + 8 * COFF::DEBUG_DIRECTORY + 4,
        DebugEntrySize * C.DebugEntries.size());
  }
  W32(DataDirectoriesOffset + 8 * COFF::CLR_RUNTIME_HEADER, TextRVA);
  W32(DataDirectoriesOffset + 8 * COFF::CLR_RUNTIME_HEADER + 4, CLIHeaderSize);

  // Section table.
  for (auto [I, S] : enumerate(Sections)) {
    uint64_t H = SectionTableOffset + COFF::SectionSize * I;
    std::memcpy(Buf + H, S.Name.data(), S.Name.size());
    W32(H + 8, S.VirtualSize);
    W32(H + 12, S.VirtualAddress);
    W32(H + 16, S.SizeOfRawData);
    W32(H + 20, S.PointerToRawData);
    // PointerToRelocations, PointerToLinenumbers and their counts are 0.
    W32(H + 36, S.Characteristics);
  }

  // .text: the CLI header (II.25.3.3).
  W32(T + 0, CLIHeaderSize);
  W16(T + 4, 2); // MajorRuntimeVersion
  W16(T + 6, 5); // MinorRuntimeVersion
  W32(T + 8, TextRVA + Text.Metadata);
  W32(T + 12, Text.MetadataSize);
  W32(T + 16, Opts.CorFlags);
  W32(T + 20, C.EntryPointToken);
  // Resources, StrongNameSignature, CodeManagerTable, VTableFixups,
  // ExportAddressTableJumps and ManagedNativeHeader are 0.

  // The IL stream and the metadata, serialized once with the final RVAs.
  Copy(T + Text.IL, C.ILStream);
  MutableArrayRef<uint8_t> MDBytes(Buf + T + Text.Metadata, Text.MetadataSize);
  MD.serialize(MDBytes, TextRVA + Text.IL, TextRVA + Text.MappedData);
  uint64_t Mvid = T + Text.Metadata + MD.getMvidOffset();
  assert(MD.getMvidOffset() + 16 <= Text.MetadataSize && "MVID out of range");
  std::memset(Buf + Mvid, 0, 16); // Hashed as zero.

  // The debug directory (IMAGE_DEBUG_DIRECTORY entries) and its data. The
  // stamps are the content id's stamp, patched below.
  for (auto [I, D] : enumerate(C.DebugEntries)) {
    uint64_t E = T + Text.DebugDirectory + DebugEntrySize * I;
    W32(E + 0, 0); // Characteristics
    W32(E + 4, 0); // TimeDateStamp
    W16(E + 8, D.MajorVersion);
    W16(E + 10, D.MinorVersion);
    W32(E + 12, D.Type);
    W32(E + 16, D.Data.size());
    if (!D.Data.empty()) {
      W32(E + 20, TextRVA + Text.DebugData[I]);
      W32(E + 24, T + Text.DebugData[I]);
      Copy(T + Text.DebugData[I], D.Data);
    }
  }

  // The mapped field data, last in .text.
  Copy(T + Text.MappedData, C.MappedFieldData);

  // The raw data of the extra sections.
  for (const SectionLayout &S : drop_begin(Sections))
    Copy(S.PointerToRawData, S.Data);

  // The COFF symbol view and its string table.
  if (!C.CoffSymbols.empty()) {
    uint64_t StringTable =
        SymbolTable + COFF::Symbol16Size * C.CoffSymbols.size();
    uint64_t NextString = 4;
    W32(StringTable, StringTableSize);
    for (auto [I, S] : enumerate(C.CoffSymbols)) {
      uint64_t R = SymbolTable + COFF::Symbol16Size * I;
      if (S.Name.size() <= COFF::NameSize) {
        std::memcpy(Buf + R, S.Name.data(), S.Name.size());
      } else {
        W32(R + 0, 0);
        W32(R + 4, NextString);
        std::memcpy(Buf + StringTable + NextString, S.Name.data(),
                    S.Name.size());
        NextString += S.Name.size() + 1;
      }
      W32(R + 8, S.Value);
      W16(R + 12, static_cast<uint16_t>(S.SectionNumber));
      W16(R + 14, S.Type);
      Buf[R + 16] = S.StorageClass;
      Buf[R + 17] = 0; // NumberOfAuxSymbols
    }
    assert(NextString == StringTableSize && "string table size mismatch");
  }

  // The content id (Roslyn /deterministic): the SHA-256 of the whole file,
  // written with a zero MVID and zero stamps, then patched in.
  std::array<uint8_t, 32> Digest = SHA256::hash(ArrayRef<uint8_t>(Out));
  BlobContentId CID = BlobContentId::fromHash(Digest);
  std::copy(CID.Guid.begin(), CID.Guid.end(), Buf + Mvid);
  W32(COFFHeaderOffset + 4, CID.Stamp);
  for (size_t I = 0, E = C.DebugEntries.size(); I != E; ++I)
    W32(T + Text.DebugDirectory + DebugEntrySize * I + 4, CID.Stamp);
  if (Id)
    *Id = CID;
  return Error::success();
}
