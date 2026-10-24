//===- llvm/CLI/PEImage.h - PE image reader ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares PEImage, a read-only view of a PE32 or PE32+ image (the
// AnyCPU reference assemblies are PE32, the toolchain's images PE32+): the
// DOS header, the PE signature, the COFF header, the optional header with its
// data directories, the section table, the CLI header (II.25.3.3) and the
// COFF symbol table that the COFF view of a CIL object adds (5.8.6).
//
// The view is zero-copy: every ArrayRef it returns points into the file that
// was passed to create(), which the caller keeps alive. Headers are parsed
// with support::endian (no dependency on llvm/Object). Every failure on input
// is an Error with the prefix "invalid CLI image: ", never a crash or an
// assertion.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_PEIMAGE_H
#define LLVM_CLI_PEIMAGE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/CLI/PEWriter.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
namespace cli {

/// One entry of the section table.
struct PESectionInfo {
  /// The name, up to its first NUL (at most 8 bytes).
  std::string Name;
  uint32_t VirtualAddress = 0, VirtualSize = 0, PointerToRawData = 0,
           SizeOfRawData = 0, Characteristics = 0;
};

/// A PE32 or PE32+ image. create() checks the headers and the section table;
/// the accessors below only read what create() checked or check what they
/// read.
class PEImage {
public:
  /// Checks and parses the headers of \p File, which the caller keeps alive:
  /// the DOS header and e_lfanew, the PE signature, the COFF header, a PE32 or
  /// PE32+ optional header (its data directories included; with fewer than 15
  /// there is no CLI header), and the section table, whose raw data must lie
  /// inside the file.
  LLVM_ABI static Expected<PEImage> create(ArrayRef<uint8_t> File);

  uint16_t getMachine() const { return Machine; }
  bool isPE32Plus() const { return PE32Plus; }
  /// IMAGE_FILE_DLL is set (Library and Module images).
  bool isDll() const { return Characteristics & COFF::IMAGE_FILE_DLL; }
  uint16_t getCharacteristics() const { return Characteristics; }
  uint32_t getTimeDateStamp() const { return TimeDateStamp; }
  uint64_t getImageBase() const { return ImageBase; }
  uint16_t getSubsystem() const { return Subsystem; }
  uint16_t getDllCharacteristics() const { return DllCharacteristics; }
  uint32_t getSizeOfImage() const { return SizeOfImage; }

  ArrayRef<PESectionInfo> sections() const { return Sections; }

  /// The RVA and size of data directory \p Index; std::nullopt if the image
  /// has fewer directories or the directory's RVA is 0.
  LLVM_ABI std::optional<std::pair<uint32_t, uint32_t>>
  getDataDirectory(unsigned Index) const;

  /// The \p Size bytes at \p RVA, which must lie inside one section's raw
  /// data (an RVA in the headers, between sections or past the raw data of
  /// its section is an Error).
  LLVM_ABI Expected<ArrayRef<uint8_t>> getRVAData(uint32_t RVA,
                                                  uint32_t Size) const;
  /// The bytes from \p RVA to the end of its section's raw data.
  LLVM_ABI Expected<ArrayRef<uint8_t>> getRVATail(uint32_t RVA) const;

  /// The CLR directory (14) is present.
  LLVM_ABI bool hasCLIHeader() const;
  /// The 72-byte CLI header that the CLR directory points at. An Error if
  /// there is none, if the directory is smaller than 72 bytes, if it does not
  /// lie inside a section's raw data, or if the header's Cb is below 72.
  LLVM_ABI Expected<Cor20Header> getCLIHeader() const;

  /// The COFF symbol table (the COFF view of a CIL object, 5.8.6): one
  /// CoffSymbol per primary 18-byte record, in table order; auxiliary records
  /// are skipped. Names longer than 8 bytes come from the string table. Empty
  /// if PointerToSymbolTable or NumberOfSymbols is 0. A table or string table
  /// outside the file, or a name offset outside the string table, is an
  /// Error.
  LLVM_ABI Expected<std::vector<CoffSymbol>> getCoffSymbols() const;

  ArrayRef<uint8_t> getFile() const { return File; }

private:
  PEImage() = default;

  /// The section whose raw data holds \p RVA, or null.
  const PESectionInfo *findSection(uint32_t RVA) const;

  ArrayRef<uint8_t> File;
  uint16_t Machine = 0, Characteristics = 0, Subsystem = 0,
           DllCharacteristics = 0;
  uint32_t TimeDateStamp = 0, PointerToSymbolTable = 0, NumberOfSymbols = 0,
           SizeOfImage = 0;
  uint64_t ImageBase = 0;
  bool PE32Plus = false;
  /// The data directories that the optional header holds (at most 16).
  SmallVector<std::pair<uint32_t, uint32_t>, 16> DataDirectories;
  SmallVector<PESectionInfo, 4> Sections;
};

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_PEIMAGE_H
