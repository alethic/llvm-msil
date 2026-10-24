//===- PEImage.cpp - PE image reader --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A read-only view of PE32 and PE32+ images: the headers and the section
// table are checked once by create(), with every offset and size checked
// against the file in 64-bit arithmetic, and the accessors return views into
// the caller's file. The field offsets are those of the PE/COFF
// specification (llvm/Object/COFF.h has the same structs).
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/PEImage.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include <algorithm>
#include <cstring>

#define DEBUG_TYPE "cli-peimage"

using namespace llvm;
using namespace llvm::cli;
using namespace llvm::support;

static constexpr char ErrorPrefix[] = "invalid CLI image: ";

static Error imageError(const Twine &Msg) {
  return createStringError(errc::illegal_byte_sequence,
                           Twine(ErrorPrefix) + Msg);
}

/// \p V as 0x and \p Digits upper-case hex digits.
static std::string hex(uint64_t V, unsigned Digits = 8) {
  return "0x" + utohexstr(V, /*LowerCase=*/false, Digits);
}

/// The DOS header is 64 bytes; e_lfanew is its last field.
static constexpr uint64_t DOSHeaderSize = 64;
static constexpr uint64_t LfanewOffset = 0x3C;
/// The fixed fields of the PE32 and PE32+ optional headers, before the data
/// directories.
static constexpr uint64_t PE32FieldsSize = 96, PE32PlusFieldsSize = 112;
/// The loaders read at most 16 data directories.
static constexpr uint32_t MaxDataDirectories = 16;
static constexpr uint64_t DataDirectorySize = 8;

/// The name of a section or a short symbol name: up to 8 bytes, NUL-padded.
static std::string shortName(const uint8_t *P) {
  size_t Len = 0;
  while (Len != COFF::NameSize && P[Len] != 0)
    ++Len;
  return std::string(reinterpret_cast<const char *>(P), Len);
}

Expected<PEImage> PEImage::create(ArrayRef<uint8_t> File) {
  PEImage I;
  I.File = File;
  const uint8_t *B = File.data();
  const uint64_t Size = File.size();

  // The DOS header and e_lfanew.
  if (Size < DOSHeaderSize)
    return imageError("truncated DOS header (the file has " + Twine(Size) +
                      " bytes, the header needs " + Twine(DOSHeaderSize) + ")");
  if (B[0] != 'M' || B[1] != 'Z')
    return imageError("bad DOS signature " + hex(endian::read16le(B), 4) +
                      " at offset 0x0 (expected 'MZ')");
  uint64_t PE = endian::read32le(B + LfanewOffset);
  uint64_t COFFHeader = PE + sizeof(COFF::PEMagic);
  if (COFFHeader + COFF::Header16Size > Size)
    return imageError("e_lfanew " + hex(PE) +
                      " leaves no room for the PE signature and the COFF "
                      "header in a file of " +
                      Twine(Size) + " bytes");
  if (std::memcmp(B + PE, COFF::PEMagic, sizeof(COFF::PEMagic)) != 0)
    return imageError("bad PE signature " + hex(endian::read32le(B + PE)) +
                      " at offset " + hex(PE) + " (expected 'PE\\0\\0')");

  // The COFF header.
  const uint8_t *H = B + COFFHeader;
  I.Machine = endian::read16le(H + 0);
  uint32_t NumberOfSections = endian::read16le(H + 2);
  I.TimeDateStamp = endian::read32le(H + 4);
  I.PointerToSymbolTable = endian::read32le(H + 8);
  I.NumberOfSymbols = endian::read32le(H + 12);
  uint64_t SizeOfOptionalHeader = endian::read16le(H + 16);
  I.Characteristics = endian::read16le(H + 18);

  // The optional header and its data directories.
  uint64_t Opt = COFFHeader + COFF::Header16Size;
  if (Opt + SizeOfOptionalHeader > Size)
    return imageError("the optional header (" + Twine(SizeOfOptionalHeader) +
                      " bytes at offset " + hex(Opt) +
                      ") runs past the end of the file (" + Twine(Size) +
                      " bytes)");
  if (SizeOfOptionalHeader < 2)
    return imageError("the optional header has " + Twine(SizeOfOptionalHeader) +
                      " bytes (no magic)");
  const uint8_t *O = B + Opt;
  uint16_t Magic = endian::read16le(O);
  uint64_t FieldsSize;
  if (Magic == COFF::PE32Header::PE32) {
    FieldsSize = PE32FieldsSize;
    I.PE32Plus = false;
  } else if (Magic == COFF::PE32Header::PE32_PLUS) {
    FieldsSize = PE32PlusFieldsSize;
    I.PE32Plus = true;
  } else {
    return imageError("unknown optional header magic " + hex(Magic, 4) +
                      " at offset " + hex(Opt) +
                      " (expected 0x010B for PE32 or 0x020B for PE32+)");
  }
  if (SizeOfOptionalHeader < FieldsSize)
    return imageError("the optional header has " + Twine(SizeOfOptionalHeader) +
                      " bytes, fewer than the " + Twine(FieldsSize) + " of a " +
                      (I.PE32Plus ? "PE32+" : "PE32") + " header");
  I.ImageBase =
      I.PE32Plus ? endian::read64le(O + 24) : endian::read32le(O + 28);
  I.SizeOfImage = endian::read32le(O + 56);
  I.Subsystem = endian::read16le(O + 68);
  I.DllCharacteristics = endian::read16le(O + 70);
  uint32_t NumberOfRvaAndSizes = endian::read32le(O + FieldsSize - 4);
  uint32_t NumDirs = std::min(NumberOfRvaAndSizes, MaxDataDirectories);
  if (FieldsSize + DataDirectorySize * NumDirs > SizeOfOptionalHeader)
    return imageError("the optional header has " + Twine(SizeOfOptionalHeader) +
                      " bytes, too few for its " + Twine(NumDirs) +
                      " data directories");
  for (uint32_t D = 0; D != NumDirs; ++D) {
    const uint8_t *P = O + FieldsSize + DataDirectorySize * D;
    I.DataDirectories.push_back({endian::read32le(P), endian::read32le(P + 4)});
  }

  // The section table.
  uint64_t Table = Opt + SizeOfOptionalHeader;
  if (Table + uint64_t(COFF::SectionSize) * NumberOfSections > Size)
    return imageError("the section table (" + Twine(NumberOfSections) +
                      " sections at offset " + hex(Table) +
                      ") runs past the end of the file (" + Twine(Size) +
                      " bytes)");
  for (uint32_t S = 0; S != NumberOfSections; ++S) {
    const uint8_t *P = B + Table + uint64_t(COFF::SectionSize) * S;
    PESectionInfo &Sec = I.Sections.emplace_back();
    Sec.Name = shortName(P);
    Sec.VirtualSize = endian::read32le(P + 8);
    Sec.VirtualAddress = endian::read32le(P + 12);
    Sec.SizeOfRawData = endian::read32le(P + 16);
    Sec.PointerToRawData = endian::read32le(P + 20);
    Sec.Characteristics = endian::read32le(P + 36);
    if (Sec.SizeOfRawData == 0)
      continue;
    if (uint64_t(Sec.PointerToRawData) + Sec.SizeOfRawData > Size)
      return imageError("section " + Twine(S + 1) + " '" + Sec.Name +
                        "': its raw data (" + hex(Sec.SizeOfRawData) +
                        " bytes at offset " + hex(Sec.PointerToRawData) +
                        ") runs past the end of the file (" + Twine(Size) +
                        " bytes)");
    if (uint64_t(Sec.VirtualAddress) + Sec.SizeOfRawData > UINT32_MAX + 1ULL)
      return imageError("section " + Twine(S + 1) + " '" + Sec.Name +
                        "': its raw data (" + hex(Sec.SizeOfRawData) +
                        " bytes at RVA " + hex(Sec.VirtualAddress) +
                        ") runs past the 32-bit address space");
  }
  return std::move(I);
}

std::optional<std::pair<uint32_t, uint32_t>>
PEImage::getDataDirectory(unsigned Index) const {
  if (Index >= DataDirectories.size() || DataDirectories[Index].first == 0)
    return std::nullopt;
  return DataDirectories[Index];
}

const PESectionInfo *PEImage::findSection(uint32_t RVA) const {
  for (const PESectionInfo &S : Sections)
    if (RVA >= S.VirtualAddress && RVA - S.VirtualAddress < S.SizeOfRawData)
      return &S;
  return nullptr;
}

Expected<ArrayRef<uint8_t>> PEImage::getRVAData(uint32_t RVA,
                                                uint32_t Size) const {
  const PESectionInfo *S = findSection(RVA);
  if (!S)
    return imageError("RVA " + hex(RVA) +
                      " is not inside the raw data of any section");
  uint64_t Offset = RVA - S->VirtualAddress;
  if (Offset + Size > S->SizeOfRawData)
    return imageError("the " + Twine(Size) + " bytes at RVA " + hex(RVA) +
                      " run past the raw data of section '" + S->Name +
                      "' (which ends at RVA " +
                      hex(uint64_t(S->VirtualAddress) + S->SizeOfRawData) +
                      ")");
  return File.slice(S->PointerToRawData + Offset, Size);
}

Expected<ArrayRef<uint8_t>> PEImage::getRVATail(uint32_t RVA) const {
  const PESectionInfo *S = findSection(RVA);
  if (!S)
    return imageError("RVA " + hex(RVA) +
                      " is not inside the raw data of any section");
  uint64_t Offset = RVA - S->VirtualAddress;
  return File.slice(S->PointerToRawData + Offset, S->SizeOfRawData - Offset);
}

bool PEImage::hasCLIHeader() const {
  return getDataDirectory(COFF::CLR_RUNTIME_HEADER).has_value();
}

Expected<Cor20Header> PEImage::getCLIHeader() const {
  std::optional<std::pair<uint32_t, uint32_t>> D =
      getDataDirectory(COFF::CLR_RUNTIME_HEADER);
  if (!D)
    return imageError("the image has no CLI header (its CLR data directory, "
                      "14, is absent or 0)");
  if (D->second < sizeof(Cor20Header))
    return imageError("the CLR data directory has " + Twine(D->second) +
                      " bytes, fewer than the " + Twine(sizeof(Cor20Header)) +
                      " of a CLI header");
  Expected<ArrayRef<uint8_t>> Bytes = getRVAData(D->first, sizeof(Cor20Header));
  if (!Bytes)
    return Bytes.takeError();
  Cor20Header Header;
  std::memcpy(&Header, Bytes->data(), sizeof(Cor20Header));
  if (Header.Cb < sizeof(Cor20Header))
    return imageError("the CLI header at RVA " + hex(D->first) +
                      " has size (Cb) " + Twine(uint32_t(Header.Cb)) +
                      ", fewer than " + Twine(sizeof(Cor20Header)) + " bytes");
  return Header;
}

Expected<std::vector<CoffSymbol>> PEImage::getCoffSymbols() const {
  std::vector<CoffSymbol> Symbols;
  if (PointerToSymbolTable == 0 || NumberOfSymbols == 0)
    return std::move(Symbols);
  const uint64_t Size = File.size();
  const uint64_t Table = PointerToSymbolTable;
  const uint64_t StringTable =
      Table + uint64_t(COFF::Symbol16Size) * NumberOfSymbols;
  if (StringTable > Size)
    return imageError("the COFF symbol table (" + Twine(NumberOfSymbols) +
                      " symbols at offset " + hex(Table) +
                      ") runs past the end of the file (" + Twine(Size) +
                      " bytes)");
  if (StringTable + 4 > Size)
    return imageError("the COFF string table at offset " + hex(StringTable) +
                      " runs past the end of the file (" + Twine(Size) +
                      " bytes)");
  uint32_t StringTableSize = endian::read32le(File.data() + StringTable);
  if (StringTableSize < 4 || StringTable + StringTableSize > Size)
    return imageError("the COFF string table at offset " + hex(StringTable) +
                      " has an invalid size " + Twine(StringTableSize) +
                      " (the file has " + Twine(Size) + " bytes)");
  ArrayRef<uint8_t> Strings = File.slice(StringTable, StringTableSize);

  for (uint64_t I = 0; I < NumberOfSymbols; ++I) {
    const uint8_t *P = File.data() + Table + COFF::Symbol16Size * I;
    CoffSymbol &S = Symbols.emplace_back();
    if (endian::read32le(P) == 0) {
      // A long name: an offset into the string table.
      uint32_t Offset = endian::read32le(P + 4);
      if (Offset < 4 || Offset >= StringTableSize)
        return imageError("COFF symbol " + Twine(I) + ": its name offset " +
                          Twine(Offset) + " is outside the string table (" +
                          Twine(StringTableSize) + " bytes)");
      ArrayRef<uint8_t> Rest = Strings.drop_front(Offset);
      const uint8_t *End = std::find(Rest.begin(), Rest.end(), 0);
      if (End == Rest.end())
        return imageError("COFF symbol " + Twine(I) +
                          ": its name at string "
                          "table offset " +
                          Twine(Offset) + " is not NUL-terminated");
      S.Name.assign(reinterpret_cast<const char *>(Rest.data()),
                    End - Rest.begin());
    } else {
      S.Name = shortName(P);
    }
    S.Value = endian::read32le(P + 8);
    S.SectionNumber = static_cast<int16_t>(endian::read16le(P + 12));
    S.Type = endian::read16le(P + 14);
    S.StorageClass = P[16];
    uint8_t NumberOfAuxSymbols = P[17];
    if (I + NumberOfAuxSymbols >= NumberOfSymbols)
      return imageError("COFF symbol " + Twine(I) + " '" + S.Name + "' has " +
                        Twine(NumberOfAuxSymbols) +
                        " auxiliary records past the end of the table");
    I += NumberOfAuxSymbols;
  }
  return std::move(Symbols);
}
