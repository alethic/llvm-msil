//===- llvm/CLI/LinkInfo.h - MSIL link-info record codec --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the codec of the link-info record v1 of CIL objects, whose
// model is llvm::MSILLink::Record (llvm/BinaryFormat/MSILLink.h):
// - RecordWriter and RecordReader, the record primitives (u8, u32, uleb, sleb,
//   str) and the subsection framing, shared with the symbol-table attribute of
//   libraries (llvm/CLI/SymbolTable.h);
// - encodeLinkInfo and decodeLinkInfo, which write the canonical bytes and
//   check the framing, the version rules and every C8 constraint;
// - findLinkInfo, which finds the record in the Module-row custom attribute of
//   [msil.link]'<msil>LinkInfo' (the carrier);
// - validateLinkInfo, which checks the rules C1-C7 between the record and the
//   object's metadata;
// - buildCOFFSymbolView and getCOFFViewMarkerSections, the COFF symbol view of
//   an object that writePEImage writes for nm and ar;
// - printLinkInfo, the lines of llvm-cil-dump --link-info.
//
// The record is specified field by field in section 5.8.8 of
// llvm/docs/MSIL/ImplementationPlan.md and versioned by its section 5.8.9; the
// carrier is its 5.8.7, C1-C7 its 5.8.5 and the COFF view its 5.8.6.
//
// Errors on input have the prefix "invalid link-info record: ", in the form
// "invalid link-info record: <field> at offset <n>: <problem>", except two
// version messages: "invalid link-info record: version <n> is newer than this
// toolchain supports (1)" for another major version, and "invalid link-info
// record: v1.<minor> uses <feature>, which this toolchain does not support"
// for an unknown must-understand subsection kind, flag bit or enum value. The
// must-understand flags are HeaderFlags, ABIFlags, SymbolFlags, the segment
// flags and CallFlags; FeatureFlags are not (every value is kept).
// validateLinkInfo's messages start with "C<n>: ".
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_LINKINFO_H
#define LLVM_CLI_LINKINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/CustomAttribute.h"
#include "llvm/CLI/PEWriter.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace llvm {

class raw_ostream;

namespace cli {

class MetadataReader;

/// Writes the record primitives of 5.8.8 to a caller-owned vector:
/// little-endian u8 and u32, minimal ULEB128 and SLEB128, str (a uleb byte
/// length and the bytes) and raw bytes. Every write appends; nothing already in
/// the vector is changed.
///
/// A subsection is a u8 kind, the uleb size of its payload and the payload:
/// beginSubsection writes the kind, the payload is written with the other
/// members, and endSubsection inserts the minimal uleb size in front of it.
/// Subsections nest.
class RecordWriter {
public:
  explicit RecordWriter(SmallVectorImpl<uint8_t> &Out) : Out(Out) {}

  LLVM_ABI void writeU8(uint8_t V);
  LLVM_ABI void writeU32(uint32_t V);
  /// The minimal ULEB128 form (1 to 10 bytes).
  LLVM_ABI void writeULEB(uint64_t V);
  /// The minimal SLEB128 form (1 to 10 bytes).
  LLVM_ABI void writeSLEB(int64_t V);
  /// 'str': the uleb byte length, then the bytes. Records hold valid UTF-8
  /// without NUL bytes, which the readers check; the writer copies the bytes.
  LLVM_ABI void writeString(StringRef S);
  LLVM_ABI void writeBytes(ArrayRef<uint8_t> B);
  /// Writes \p Kind and starts the payload of a subsection.
  LLVM_ABI void beginSubsection(uint8_t Kind);
  /// Ends the innermost open subsection: inserts the minimal uleb size of its
  /// payload after its kind.
  LLVM_ABI void endSubsection();

  /// The size of the vector (including what it held before this writer).
  size_t size() const { return Out.size(); }

private:
  SmallVectorImpl<uint8_t> &Out;
  /// The payload start of each open subsection.
  SmallVector<size_t, 2> Open;
};

/// Reads the record primitives of 5.8.8 from a byte range. Each read names
/// the field it reads, which an error message quotes:
/// "invalid <format>: <field> at offset <n>: <problem>", where <format> is
/// "link-info record" or "symbol table" and <n> is the decimal offset in the
/// whole record. ULEB128 and SLEB128 values of up to 10 bytes are read, in any
/// form; a longer one, or one whose value does not fit in 64 bits, is an
/// error. A str must be valid UTF-8 without NUL bytes.
///
/// The error is sticky: after the first one, every read returns 0 (or an empty
/// range) without reading, and the first error stays until takeError(). A
/// reader that holds an error must be asked for it with takeError(); it is a
/// plain value, so destroying it with an error pending is not an assertion.
class RecordReader {
public:
  /// Reads \p Data. \p Format names the data in messages; \p BaseOffset is the
  /// offset of Data[0] in the whole record.
  LLVM_ABI explicit RecordReader(ArrayRef<uint8_t> Data,
                                 StringRef Format = "link-info record",
                                 uint64_t BaseOffset = 0);

  LLVM_ABI uint8_t readU8(const Twine &Field);
  LLVM_ABI uint32_t readU32(const Twine &Field);
  LLVM_ABI uint64_t readULEB(const Twine &Field);
  /// A uleb that must fit in 32 bits (indexes and counts of the model).
  LLVM_ABI uint32_t readULEB32(const Twine &Field);
  LLVM_ABI int64_t readSLEB(const Twine &Field);
  /// A str: a uleb byte length and that many bytes of valid UTF-8 without NUL
  /// bytes. The result points into the data.
  LLVM_ABI StringRef readString(const Twine &Field);
  /// \p Size bytes; the result points into the data.
  LLVM_ABI ArrayRef<uint8_t> readBytes(uint64_t Size, const Twine &Field);
  /// A subsection header: a u8 kind (stored in \p Kind) and a uleb payload
  /// size, which must fit in the rest of the data. Returns a reader of the
  /// payload, whose offsets count in the same record and whose format is this
  /// reader's, and moves this reader past the payload. On an error the
  /// returned reader is empty; the error is this reader's.
  LLVM_ABI RecordReader readSubsection(uint8_t &Kind,
                                       const Twine &Field = "subsection");
  /// Records an error unless every byte was read: "<field> at offset <n>:
  /// <k> bytes left over".
  LLVM_ABI void expectEnd(const Twine &Field);

  /// True when every byte was read.
  bool empty() const { return Pos == Data.size(); }
  size_t bytesLeft() const { return Data.size() - Pos; }
  /// The offset of the next byte in the whole record.
  uint64_t getOffset() const { return BaseOffset + Pos; }
  StringRef getFormat() const { return Format; }

  /// Records "<field> at offset <offset>: <problem>" unless an error is
  /// already recorded.
  LLVM_ABI void setError(const Twine &Field, uint64_t Offset,
                         const Twine &Problem);
  /// Records "<message>" (after the "invalid <format>: " prefix) with the
  /// error code errc::not_supported, unless an error is already recorded: the
  /// version messages of 5.8.9.
  LLVM_ABI void setUnsupported(const Twine &Message);
  bool hasError() const { return HasError; }
  /// The recorded error ("invalid <format>: ..."), or success; clears it.
  LLVM_ABI Error takeError();

private:
  bool need(uint64_t Size, const Twine &Field);

  ArrayRef<uint8_t> Data;
  std::string Format;
  uint64_t BaseOffset;
  size_t Pos = 0;
  bool HasError = false, Unsupported = false;
  std::string Message;
};

/// Appends the canonical bytes of \p R (5.8.8) to \p Out: the header, then
/// every non-empty subsection in kind order with its fields in 5.8.8's order
/// (a field is written when the symbol's kind and flags say it is present),
/// then R.SkippedOptional in kind order. Equal records give equal bytes. The
/// record should satisfy C8 (decodeLinkInfo's checks); what the encoder
/// relies on (a non-ZERO segment holds Size bytes, a DEPENDENT_LIB entry has
/// one argument, skipped kinds are 0x40-0x7F) is asserted.
LLVM_ABI void encodeLinkInfo(const MSILLink::Record &R,
                             SmallVectorImpl<uint8_t> &Out);

/// Decodes a record: the magic, major version 1 (another major is the version
/// error of 5.8.9), any minor version, the header, the subsections (kinds in
/// increasing order, each at most once, every payload consumed exactly, kinds
/// 0x40-0x7F kept in SkippedOptional) and every C8 constraint of 5.8.8: flag
/// combinations, names, index ranges, segment bounds and alignment,
/// relocation order, sites and target kinds, selections, INIT_FINI symbols and
/// one SIGNATURES entry per token. An unknown subsection kind below 0x40 (or
/// above 0x7F), flag bit (of HeaderFlags, ABIFlags, SymbolFlags, the segment
/// flags or CallFlags) or enum value is the feature error of 5.8.9. Every
/// FeatureFlags value is accepted and kept in the header: feature flags are
/// codegen switches that do not change the ABI, which msil-ld ORs. Alignment
/// fields (segments, commons, sret and byval) are at most 30 (log2) and a
/// common's size is below 2^31, as a segment's.
LLVM_ABI Expected<MSILLink::Record> decodeLinkInfo(ArrayRef<uint8_t> Bytes);

/// The record bytes of an object (5.8.7): the payload of the single custom
/// attribute of the Module row whose constructor is the MemberRef '.ctor' of
/// the TypeRef '<msil>LinkInfo' (empty namespace) scoped to the AssemblyRef
/// 'msil.link', with signature 20 01 01 1D 05 (instance void(uint8[])). The
/// result points into \p MD's #Blob heap. An object without one is the error
/// "no link-info record"; two or more, or another constructor signature, are
/// "invalid link-info record: " errors; decodeByteArrayAttribute's errors are
/// returned unchanged.
LLVM_ABI Expected<ArrayRef<uint8_t>> findLinkInfo(const MetadataReader &MD);

/// Checks the rules C1-C7 of 5.8.5 between a decoded record and the object's
/// metadata, in rule order, and returns the first violation as an error whose
/// message starts with "C<n>: " (msil-ld prepends "<file>: malformed CIL
/// object: "). A MethodDef has a body when its RVA is not 0. Errors of the
/// reader on malformed metadata are returned unchanged.
LLVM_ABI Error validateLinkInfo(const MSILLink::Record &R,
                                const MetadataReader &MD);

/// The COFF symbol view of an object (5.8.6): one record per SYMTAB symbol, in
/// SYMTAB order, for PEContent::CoffSymbols. Section numbers are 1 .text
/// (defined functions and their aliases), 2 .data, 3 .rdata (a READONLY
/// segment), 4 .bss (a ZERO segment) and 0 (undefined and common symbols); a
/// DATA alias takes its aliasee's section. Type is 0x20 for functions; the
/// storage class is STATIC for LOCAL definitions, else EXTERNAL; Value is 0
/// except for commons, whose Value is their size. Weak symbols are plain
/// records.
LLVM_ABI std::vector<CoffSymbol> buildCOFFSymbolView(const MSILLink::Record &R);

/// The marker sections of the COFF view (5.8.6), for PEContent::ExtraSections:
/// .data (0xC0000040), .rdata (0x40000040) and .bss (0xC0000080), without
/// data, so that they are section numbers 2-4 after .text.
LLVM_ABI ArrayRef<PESection> getCOFFViewMarkerSections();

/// Prints \p R in llvm-cil-dump --link-info's format (5.8.11): one line per
/// item in record order, names quoted as in the canonical listing. With \p MD,
/// a SIGNATURES token is followed by the name of its MethodDef, MemberRef or
/// Field row.
LLVM_ABI void printLinkInfo(const MSILLink::Record &R, const MetadataReader *MD,
                            raw_ostream &OS);

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_LINKINFO_H
