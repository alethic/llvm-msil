//===- llvm/CLI/SymbolTable.h - MSIL library symbol table -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the model and the codec of the symbol table of a library
// that msil-ld writes with -shared (P2-C). The table is the payload of one or
// more custom attributes of the Assembly row whose constructor takes uint8[]
// and belongs to a private class '<msil>SymbolTable' of the library itself;
// the attribute value is framed by encodeByteArrayAttribute
// (llvm/CLI/CustomAttribute.h) and found with
// MetadataReader::getByteArrayAttributes.
//
// The payload (section 5.9.15 of llvm/docs/MSIL/ImplementationPlan.md) is
// written with the link-info record's primitives (cli::RecordWriter): the
// magic "MSYM", u8 major 1, u8 minor 0, u8 flags, uleb toolchain ABI version,
// str triple, str C runtime identity, 4 x u8 data model, then a uleb count and
// the export classes (str), the data symbols (str name, uleb size, u8 log2
// alignment, u8 flags) and the flagged functions (str name, u8 flags).
//
// M22 adds the header flag INTERFACE (0x01) and the function flag ABI (0x04)
// with its signature descriptor, and its commit freezes the format; until
// then the decoder refuses both as unknown flags. Errors on input have the
// prefix "invalid symbol table: ".
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_SYMBOLTABLE_H
#define LLVM_CLI_SYMBOLTABLE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>
#include <vector>

namespace llvm {

class raw_ostream;

namespace cli {

/// The first four bytes of a payload: 4D 53 59 4D.
inline constexpr char SymbolTableMagic[4] = {'M', 'S', 'Y', 'M'};
/// The name of the private attribute class (empty namespace) whose
/// constructor carries the payload.
inline constexpr StringLiteral SymbolTableType = "<msil>SymbolTable";
/// The version written; the decoder refuses another major version.
inline constexpr uint8_t SymbolTableMajorVersion = 1,
                         SymbolTableMinorVersion = 0;

/// One exported variable: its name, size, log2 alignment and flags.
struct LibraryDataSymbol {
  enum : uint8_t {
    Const = 0x1,
    ThreadLocal = 0x2,
    Weak = 0x4,
    KnownFlags = 0x7
  };
  std::string Name;
  uint64_t Size = 0;
  uint8_t AlignLog2 = 0;
  uint8_t Flags = 0;
};

/// One exported function whose flags are not 0 (every other exported
/// function is a plain one): its name and flags.
struct LibraryFunctionFlags {
  enum : uint8_t { Variadic = 0x1, Weak = 0x2, KnownFlags = 0x3 };
  std::string Name;
  uint8_t Flags = 0;
};

/// The symbol table of a library (5.9.15). Triple is the library's triple
/// without its environment version; CRuntime is the C runtime identity (the
/// first -l among c, msvcrt, ucrt, ucrtbase, crtdll and msvcr*, else "none");
/// PointerSize..LongDoubleSize are the data model of 5.8.9. ExportClasses are
/// full type names ("NS.Name" or "Name").
struct LibrarySymbolTable {
  /// No header flag is defined before M22.
  enum : uint8_t { KnownFlags = 0x0 };
  uint8_t Major = SymbolTableMajorVersion, Minor = SymbolTableMinorVersion;
  uint8_t Flags = 0;
  uint32_t ABIVersion = MSILLink::ToolchainABIVersion;
  std::string Triple;
  std::string CRuntime;
  uint8_t PointerSize = 8, LongSize = 8, WCharSize = 4, LongDoubleSize = 8;
  std::vector<std::string> ExportClasses;
  std::vector<LibraryDataSymbol> DataSymbols;
  std::vector<LibraryFunctionFlags> Functions;
};

/// Appends the payload of \p T to \p Out, written with RecordWriter in the
/// field order above (Major and Minor as given). Equal tables give equal
/// bytes.
LLVM_ABI void encodeSymbolTable(const LibrarySymbolTable &T,
                                SmallVectorImpl<uint8_t> &Out);

/// Decodes a payload. It refuses a bad magic, another major version (any
/// minor version is read), unknown header, data-symbol and function flags,
/// strings that are not UTF-8 or hold a NUL byte, an ABI version above
/// 2^32 - 1, an alignment above 2^30, truncation and trailing bytes.
LLVM_ABI Expected<LibrarySymbolTable>
decodeSymbolTable(ArrayRef<uint8_t> Bytes);

/// Prints \p T in llvm-cil-dump --link-info's format, one line per item:
///   SymbolTable v1.<minor> triple=<t> abi=<n> runtime='<c>'
///     datamodel=<p>,<l>,<w>,<d>[ flags=0x<2X>]         (one line)
///   ExportClass '<name>'
///   Data[ CONST][ TLS][ WEAK] '<name>' size=<n> align=<bytes>
///   Function[ VARARG][ WEAK] '<name>'
/// Names are quoted as in the canonical listing.
LLVM_ABI void printSymbolTable(const LibrarySymbolTable &T, raw_ostream &OS);

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_SYMBOLTABLE_H
