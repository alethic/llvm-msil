//===- llvm/CLI/PEWriter.h - IL-only PE image writer ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares writePEImage, which wraps the metadata and IL of one
// module in an IL-only PE32+ image with Roslyn's envelope (not ilasm's):
// - the classic 128-byte DOS header and stub (e_lfanew 0x80), the COFF header
//   and a PE32+ optional header with 16 data directories, of which only the
//   CLR directory (14) and, with debug entries, the Debug directory (6) are
//   set: no import table, IAT, entry stub or base relocations, and
//   AddressOfEntryPoint 0;
// - one .text section (0x60000020) at RVA SectionAlignment that holds, in
//   this order, the 72-byte CLI header, the IL stream (4-aligned), the
//   metadata (4-aligned), the debug directory and its data (4-aligned, if
//   any) and the mapped field data (8-aligned, last);
// - PEContent::ExtraSections after .text at section-aligned RVAs. An extra
//   section without data is a marker: VirtualSize, SizeOfRawData and
//   PointerToRawData 0, VirtualAddress the next section-aligned RVA. The
//   headers take 0x200 bytes, or more (0x400 with the three markers of the
//   COFF view of a CIL object) when the section table needs the room;
// - PEContent::CoffSymbols (the COFF view of a CIL object) after the last
//   section's raw data, 4-aligned and outside SizeOfImage: 18-byte records
//   without auxiliary records, then the string table (a u32 size that counts
//   itself, then the NUL-terminated names longer than 8 bytes).
//
// The image is deterministic (Roslyn /deterministic): it is written with a
// zero MVID, COFF TimeDateStamp and debug-entry stamps, the SHA-256 of the
// whole file (COFF view included) gives the content id (BlobContentId), and
// its GUID and stamp are then patched into the MVID, the COFF header and
// every debug entry.
//
// Errors on the content have the prefix "cannot write PE image: ", except the
// "cannot write CLI metadata: " error of MetadataBuilder::checkMethodBodies(),
// which is returned unchanged.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_PEWRITER_H
#define LLVM_CLI_PEWRITER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstdint>
#include <string>

namespace llvm {

class Triple;

namespace cli {

class MetadataBuilder;

/// The shape of an image: an EXE program, a -shared DLL, or a netmodule (a
/// CIL object, which has no Assembly row).
enum class PEImageKind : uint8_t { Executable, Library, Module };

/// The PE header values. The defaults are Roslyn's for an x64 EXE (with lld's
/// linker version); get() sets the machine and the image base for a triple
/// and a kind.
struct PEOptions {
  COFF::MachineTypes Machine = COFF::IMAGE_FILE_MACHINE_AMD64;
  PEImageKind Kind = PEImageKind::Executable;
  uint32_t CorFlags = COMIMAGE_FLAGS_ILONLY;
  /// Library and Module: 0x180000000.
  uint64_t ImageBase = 0x140000000;
  uint32_t SectionAlignment = 0x2000, FileAlignment = 0x200;
  uint64_t StackReserve = 0x400000, StackCommit = 0x4000,
           HeapReserve = 0x100000, HeapCommit = 0x2000;
  uint16_t Subsystem = COFF::IMAGE_SUBSYSTEM_WINDOWS_CUI;
  /// DYNAMIC_BASE | NX_COMPAT | NO_SEH | TERMINAL_SERVER_AWARE.
  uint16_t DllCharacteristics = 0x8540;
  /// lld's value: a linker version of 0.0 breaks Authenticode
  /// (lld/COFF/Writer.cpp).
  uint8_t MajorLinkerVersion = 14, MinorLinkerVersion = 0;

  /// The options for \p TT and \p Kind: x86_64 is AMD64 and aarch64 is ARM64,
  /// both PE32+. Any other architecture is an Error until phase 5 (PE32, the
  /// mscoree stub and .reloc). Machine and CorFlags are independent of the
  /// rest of the triple.
  LLVM_ABI static Expected<PEOptions> get(const Triple &TT, PEImageKind Kind);
};

/// A section after .text. Name has at most 8 bytes. A section with empty
/// Data is a marker section (VirtualSize, SizeOfRawData and PointerToRawData
/// 0).
struct PESection {
  std::string Name;
  uint32_t Characteristics = 0;
  ArrayRef<uint8_t> Data;
};

/// One record of the COFF symbol table of a CIL object (the COFF view, Q2):
/// a plain 18-byte record without auxiliary records.
struct CoffSymbol {
  /// A name of more than 8 bytes goes to the string table.
  std::string Name;
  /// 0, except for common symbols, whose value is the size.
  uint32_t Value = 0;
  /// 0 undefined, 1 .text, 2 and up PEContent::ExtraSections in order.
  int16_t SectionNumber = 0;
  /// 0x20 for functions.
  uint16_t Type = 0;
  uint8_t StorageClass = COFF::IMAGE_SYM_CLASS_EXTERNAL;
};

/// One entry of the debug directory; its data is placed in .text.
struct DebugDirectoryEntry {
  uint32_t Type = 0;
  uint16_t MajorVersion = 0, MinorVersion = 0;
  ArrayRef<uint8_t> Data;
};

/// What goes into the image. The referenced data must outlive the call.
struct PEContent {
  /// The module's metadata, finalized.
  const MetadataBuilder *Metadata = nullptr;
  /// The method bodies: MethodBodyStream::getBytes().
  ArrayRef<uint8_t> ILStream;
  /// The FieldRVA data, at the offsets given to
  /// MetadataBuilder::addFieldRVA().
  ArrayRef<uint8_t> MappedFieldData;
  /// A MethodDef token for an Executable; 0 for a Library or a Module.
  uint32_t EntryPointToken = 0;
  /// None in Phase 2 outputs (the Phase 4 PDB carrier).
  ArrayRef<DebugDirectoryEntry> DebugEntries;
  /// The sections after .text, in order (section numbers 2 and up).
  ArrayRef<PESection> ExtraSections;
  /// The COFF view; objects only.
  ArrayRef<CoffSymbol> CoffSymbols;
};

/// A deterministic content id (Roslyn's BlobContentId): the MVID and the
/// PE TimeDateStamp of an image derived from a hash of its content.
struct BlobContentId {
  /// The GUID in its on-disk byte order (the bytes of the #GUID entry).
  std::array<uint8_t, 16> Guid{};
  uint32_t Stamp = 0;

  /// The id of \p Hash, which has at least 20 bytes: the GUID is bytes 0-15
  /// with byte 7 = (b7 & 0x0F) | 0x40 (version 4) and byte 8 =
  /// (b8 & 0x3F) | 0x80 (the RFC 4122 variant), and the stamp is the
  /// little-endian u32 of bytes 16-19 with its high bit set.
  LLVM_ABI static BlobContentId fromHash(ArrayRef<uint8_t> Hash);
};

/// Writes the PE image of \p C with the header values of \p Opts to \p Out,
/// replacing its contents, and stores the image's content id in \p Id if it
/// is not null. The metadata is serialized once, with the final RVAs.
///
/// Errors (\p Out is unchanged then):
/// - the metadata is missing or not finalized;
/// - a Module with an Assembly row, or an Executable or Library without one;
/// - an Executable whose entry point token is not a MethodDef row of the
///   module, or a Library or Module with an entry point token;
/// - MetadataBuilder::checkMethodBodies() fails (its error is returned
///   unchanged);
/// - a machine other than AMD64 and ARM64 (only PE32+ is written), invalid
///   alignments, a section name longer than 8 bytes, a COFF symbol with an
///   empty name, a NUL byte in its name or a section number past the
///   sections, too many sections, or an image larger than 4 GiB.
LLVM_ABI Error writePEImage(const PEOptions &Opts, const PEContent &C,
                            SmallVectorImpl<uint8_t> &Out,
                            BlobContentId *Id = nullptr);

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_PEWRITER_H
