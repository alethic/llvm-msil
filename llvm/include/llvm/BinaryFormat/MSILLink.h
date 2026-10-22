//===- llvm/BinaryFormat/MSILLink.h - MSIL link-info record -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines the constants and the in-memory form of the link-info
// record v1 of MSIL (.NET CIL) object files. A CIL object is an ECMA-335
// netmodule; the C link semantics that its metadata cannot express (binding
// and visibility, data images and data relocations, COMDAT groups,
// constructors, call-site signature flags and linker options) travel in this
// record, which the object stores in a module-level custom attribute. The MSIL
// backend writes the record and msil-ld reads it; the encoder and the decoder
// are in llvm/CLI/LinkInfo.h.
//
// The record is specified field by field in section 5.8.8 of
// llvm/docs/MSIL/ImplementationPlan.md, and versioned by its section 5.8.9.
// The comments below name the 5.8.8 table that each declaration implements
// and the rule it follows. The layout follows WebAssembly's "linking" section
// (llvm/BinaryFormat/Wasm.h) and the ELF gABI.
//
// Header only: BinaryFormat gains no source file for it. It includes Triple.h
// for setDataModel(), as BinaryFormat/ELF.h does.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_MSILLINK_H
#define LLVM_BINARYFORMAT_MSILLINK_H

#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace llvm::MSILLink {

/// Header table, field Magic: the bytes 4D 53 4C 4B that start every record
/// (the counterpart of the name of wasm's "linking" section).
inline constexpr char Magic[4] = {'M', 'S', 'L', 'K'};

/// Header table, fields Major and Minor. A reader refuses another major
/// version; a minor version only adds subsection kinds, flag bits and enum
/// values within major 1 (5.8.9).
inline constexpr uint8_t MajorVersion = 1, MinorVersion = 0;

/// Header table, field ABIVersion: the version of the internal calling
/// convention (aggregates passed indirectly, sret first, phase 3's variadic
/// buffer). It must be equal across all inputs of a link, and it changes only
/// with a section 12 row of the plan (5.8.9).
inline constexpr unsigned ToolchainABIVersion = 1;

/// Names of the metadata rows that carry the record and the placeholder
/// references of a CIL object (5.8.3):
/// - ExternAssembly: the AssemblyRef that scopes SymbolsType and FnAddrType;
/// - SymbolsType: the parent of the MemberRefs that name undefined C symbols;
/// - FnAddrType: the parent of the MemberRefs that take a function's address;
/// - BCLAssembly: the unbound scope of every BCL TypeRef, bound by msil-ld;
/// - LinkInfoAssembly, LinkInfoType: the scope and the type of the custom
///   attribute that carries the record (5.8.7);
/// - UnresolvedModule: the ModuleRef of the P/Invoke stubs of a function that
///   has an import name but no import module.
inline constexpr StringLiteral ExternAssembly = "msil.extern",
                               SymbolsType = "<msil>Symbols",
                               FnAddrType = "<msil>FnAddr",
                               BCLAssembly = "msil.bcl",
                               LinkInfoAssembly = "msil.link",
                               LinkInfoType = "<msil>LinkInfo",
                               UnresolvedModule = "msil.unresolved";

/// Subsections table. Each subsection is a u8 kind, a uleb payload size and the
/// payload. Kinds appear in increasing order, each at most once. A reader must
/// understand every kind below FirstOptional (an unknown one is an error) and
/// may skip kinds FirstOptional..LastOptional; FirstDebug..LastDebug are
/// reserved for debug information (5.8.7). ManagedRequests and Types are
/// reserved for phase 3.
enum class SubsectionKind : uint8_t {
  SymTab = 0x01,
  Segments = 0x02,
  DataRelocs = 0x03,
  Comdats = 0x04,
  InitFini = 0x05,
  Signatures = 0x06,
  LinkerOptions = 0x07,
  ManagedRequests = 0x08,
  Types = 0x09,
  FirstOptional = 0x40,
  FirstDebug = 0x40,
  LastDebug = 0x4F,
  LastOptional = 0x7F
};

/// SYMTAB table, field Kind (ELF STT_FUNC and STT_OBJECT; wasm's
/// WASM_SYMBOL_TYPE_FUNCTION and WASM_SYMBOL_TYPE_DATA).
enum class SymbolKind : uint8_t { Function = 0, Data = 1 };

/// SymbolFlags table: the uleb Flags of a SYMTAB symbol. The bit values follow
/// wasm's WASM_SYMBOL_* where the meaning matches.
/// - BindingMask: GLOBAL, WEAK, LOCAL or COMMON (ELF STB_GLOBAL, STB_WEAK,
///   STB_LOCAL and SHN_COMMON).
/// - VisibilityMask: a Visibility value << VisibilityShift (ELF STV_*).
/// - Undefined: a reference only (SHN_UNDEF).
/// - DLLExport: IR dllexport (Windows triples).
/// - ExplicitImport: an undefined function whose ImplMap stubs carry the
///   import module and name.
/// - NoStrip: the global is in llvm.used.
/// - ThreadLocal: thread-local data (TLS).
/// - Alias: defined through Symbol::Aliasee.
/// - DLLImport: IR dllimport on an undefined symbol (a hint).
/// - Import: an undefined variable with the import attributes; ImportModule
///   and ImportName follow (v1.0 sets it on DATA only, because functions carry
///   their attributes in their stubs).
///
/// Constraints (C8): COMMON only for DATA, never with Undefined or Alias;
/// LOCAL never with Undefined; Undefined only with GLOBAL or WEAK;
/// ExplicitImport only with FUNCTION and Undefined; Import only with DATA and
/// Undefined, never with ThreadLocal, and with a non-empty ImportName;
/// DLLImport only with Undefined; NoStrip never with Undefined; ThreadLocal
/// only for DATA; names unique within SYMTAB, non-empty and not starting with
/// "<msil>".
enum SymbolFlags : uint32_t {
  BindingMask = 0x3,
  BindingGlobal = 0x0,
  BindingWeak = 0x1,
  BindingLocal = 0x2,
  BindingCommon = 0x3,
  VisibilityMask = 0xC,
  VisibilityShift = 2,
  Undefined = 0x10,
  DLLExport = 0x20,
  ExplicitImport = 0x40,
  NoStrip = 0x80,
  ThreadLocal = 0x100,
  Alias = 0x200,
  DLLImport = 0x400,
  Import = 0x800
};

/// SymbolFlags table, bits VISIBILITY. The values equal ELF's STV_*; msil-ld
/// merges them with lld/ELF's minimum rule.
enum class Visibility : uint8_t {
  Default = 0,
  Internal = 1,
  Hidden = 2,
  Protected = 3
};

/// SEGMENTS table, field Flags. SegStrings, SegTLS and SegRetain have wasm's
/// WASM_SEG_FLAG_* values (ELF SHF_MERGE|SHF_STRINGS, SHF_TLS and
/// SHF_GNU_RETAIN); SegZero is SHT_NOBITS (the segment has no bytes) and
/// SegReadOnly is the absence of SHF_WRITE.
enum SegmentFlags : uint32_t {
  SegStrings = 0x1,
  SegTLS = 0x2,
  SegRetain = 0x4,
  SegZero = 0x8,
  SegReadOnly = 0x10
};

/// DATA_RELOCS type table. Targets (C8): Addr* and Diff name DATA symbols,
/// FnAddr* names FUNCTION symbols with addend 0, TLSOffset* names TLS symbols.
/// - Addr64, Addr32: the address of a DATA symbol + addend, in 8 bytes or its
///   low 32 bits (R_X86_64_64, R_X86_64_32 without the overflow check).
/// - FnAddr64, FnAddr32: the function's pointer value, in 8 bytes or its low
///   32 bits.
/// - Diff: addr(Symbol) - addr(Symbol2) + addend in Width bytes.
/// - TLSOffset32, TLSOffset64: the offset of a TLS symbol in the output's TLS
///   template + addend (phase 3).
enum class RelocType : uint8_t {
  Addr64 = 0,
  Addr32 = 1,
  FnAddr64 = 2,
  FnAddr32 = 3,
  Diff = 4,
  TLSOffset32 = 5,
  TLSOffset64 = 6
};

/// COMDATS, field Selection. The values equal llvm::Comdat::SelectionKind's
/// (BinaryFormat cannot include IR headers). C produces only Any; the backend
/// and msil-ld reject ExactMatch, Largest and SameSize.
enum class ComdatSelection : uint8_t {
  Any = 0,
  ExactMatch = 1,
  Largest = 2,
  NoDeduplicate = 3,
  SameSize = 4
};

/// COMDATS, the kind of a group member: a FUNCTION symbol or a segment.
enum class ComdatMemberKind : uint8_t { FunctionSymbol = 0, Segment = 1 };

/// INIT_FINI, field Kind: an llvm.global_ctors or an llvm.global_dtors entry.
enum class InitKind : uint8_t { Init = 0, Fini = 1 };

/// SIGNATURES, field CallFlags (C7). Phase 2 writers never set VarArg.
enum CallFlags : uint32_t { NoPrototype = 0x1, VarArg = 0x2, Sret = 0x4 };

/// LINKER_OPTIONS, field Kind: an llvm.dependent-libraries entry (one string)
/// or one llvm.linker.options node (n strings).
enum class LinkerOptionKind : uint8_t { DependentLib = 0, LinkerOption = 1 };

/// Header table, field HeaderFlags: bit 0 HAS_DEBUG_INFO (5.8.7); the other
/// bits are 0.
enum HeaderFlags : uint32_t { HasDebugInfo = 0x1 };

/// Header table. Triple is the target machine's normalized triple. ABIFlags
/// are codegen switches that change the ABI and FeatureFlags those that do
/// not; Phase 2 defines none of either. PointerSize, LongSize, WCharSize and
/// LongDoubleSize are the DataModel, set by setDataModel(). ABIVersion, the
/// ABIFlags and the DataModel must be equal across all inputs of a link.
struct Header {
  uint8_t Major = MajorVersion, Minor = MinorVersion;
  uint32_t Flags = 0;
  std::string Triple;
  uint32_t ABIVersion = ToolchainABIVersion;
  uint64_t ABIFlags = 0, FeatureFlags = 0;
  uint8_t PointerSize = 8, LongSize = 8, WCharSize = 4, LongDoubleSize = 8;
};

/// SYMTAB table: one symbol. Token is present for a defined non-alias
/// FUNCTION (its MethodDef) and for a defined or COMMON non-alias DATA symbol
/// (its FieldDef). The other fields are present only for the symbols named in
/// their comments.
struct Symbol {
  SymbolKind Kind;
  uint32_t Flags = 0;
  std::string Name;
  uint32_t Token = 0;
  // Defined non-alias, non-common DATA (Size also for DATA aliases).
  uint32_t Segment = 0;
  uint64_t Offset = 0, Size = 0;
  // ALIAS: a defined non-alias symbol of the same kind in this object; a DATA
  // alias sits at the aliasee's offset + AliasOffset.
  uint32_t Aliasee = 0;
  int64_t AliasOffset = 0;
  // COMMON: size and log2 alignment.
  uint64_t CommonSize = 0;
  uint8_t CommonAlignLog2 = 0;
  // IMPORT: the import module (empty: msil-ld finds the module natively under
  // ImportName) and the import entry (non-empty).
  std::string ImportModule, ImportName;
};

/// SEGMENTS table: one segment, in the SYMTAB order of the symbol that
/// defines it. Name is the output section name; Bytes is empty for a SegZero
/// segment, and relocation sites hold zero bytes.
struct Segment {
  std::string Name;
  uint32_t Flags = 0;
  uint8_t AlignLog2 = 0;
  uint64_t Size = 0;
  std::vector<uint8_t> Bytes;
};

/// DATA_RELOCS table: one data relocation. Entries are sorted by (Segment,
/// Offset), never overlap and never lie in a SegZero segment.
struct Reloc {
  RelocType Type;
  uint32_t Segment;
  uint64_t Offset;
  uint32_t Symbol;
  int64_t Addend = 0;
  // DIFF only: the width of the site (1 to 16 bytes) and the subtrahend.
  uint8_t Width = 0;
  uint32_t Symbol2 = 0;
};

/// COMDATS: one group (ELF SHT_GROUP with GRP_COMDAT; wasm WASM_COMDAT_INFO,
/// plus the selection kind). Members are SYMTAB or SEGMENTS indexes.
struct ComdatGroup {
  std::string Name;
  ComdatSelection Selection;
  std::vector<std::pair<ComdatMemberKind, uint32_t>> Members;
};

/// INIT_FINI: one entry, in llvm.global_ctors/llvm.global_dtors order.
/// Priority is 0-65535; Symbol is a defined FUNCTION of the object;
/// ComdatPlusOne is 1 + the index of the COMDAT group of the entry's
/// associated global, or 0 for none. The record has no position field: msil-ld
/// runs an object's section slots before its entries of equal key.
struct InitEntry {
  InitKind Kind;
  uint32_t Priority;
  uint32_t Symbol;
  uint32_t ComdatPlusOne = 0;
};

/// SIGNATURES: one byval parameter (its IL parameter index, size and
/// alignment).
struct ByvalParam {
  uint32_t Param;
  uint64_t Size;
  uint8_t AlignLog2;
};

/// SIGNATURES: the call-site flags of one MethodDef or method MemberRef
/// (C7). FixedParams is present for VarArg only, SretSize and SretAlignLog2 for
/// Sret only. A token without an entry is prototyped, not variadic, and has no
/// sret or byval parameter.
struct SignatureInfo {
  uint32_t Token;
  uint32_t Flags = 0;
  uint32_t FixedParams = 0;
  uint64_t SretSize = 0;
  uint8_t SretAlignLog2 = 0;
  std::vector<ByvalParam> Byval;
};

/// LINKER_OPTIONS: one entry. A DependentLib has exactly one argument (the
/// library, "m" or "ws2_32"); a LinkerOption has the strings of one node
/// ("/DEFAULTLIB:ws2_32.lib", "-lws2_32").
struct LinkerOption {
  LinkerOptionKind Kind;
  std::vector<std::string> Args;
};

/// The whole record v1: the header, then one vector per subsection.
/// SkippedOptional keeps the kinds 0x40-0x7F that a reader skipped, for
/// dumping.
struct Record {
  Header Hdr;
  std::vector<Symbol> Symbols;
  std::vector<Segment> Segments;
  std::vector<Reloc> Relocs;
  std::vector<ComdatGroup> Comdats;
  std::vector<InitEntry> Inits;
  std::vector<SignatureInfo> Signatures;
  std::vector<LinkerOption> LinkerOptions;
  std::vector<std::pair<uint8_t, std::vector<uint8_t>>> SkippedOptional;
};

/// Set the DataModel of \p H by 5.8.9's DataModel rule, which the object
/// writer and msil-ld's bitcode inputs share:
/// - PointerSize: the size of address space 0's pointers, as given (the
///   module's DataLayout; for a bitcode input, 8 for a 64-bit triple
///   architecture, else 4).
/// - LongSize: 4 when PointerSize is 4 or the triple is Windows other than
///   Cygwin (isOSWindows() && !isWindowsCygwinEnvironment()), else 8.
/// - WCharSize: as given: the module flag "wchar_size" if present, else
///   TT.getDefaultWCharSize() (TargetLibraryInfoImpl::getWCharSize's rule).
/// - LongDoubleSize: always 8 (long double is IEEE double on MSIL triples);
///   stock bitcode's x86_fp80 is a documented limit.
inline void setDataModel(Header &H, const Triple &TT, unsigned PointerSize,
                         unsigned WCharSize) {
  H.PointerSize = PointerSize;
  H.LongSize = (PointerSize == 4 ||
                (TT.isOSWindows() && !TT.isWindowsCygwinEnvironment()))
                   ? 4
                   : 8;
  H.WCharSize = WCharSize;
  H.LongDoubleSize = 8;
}

} // namespace llvm::MSILLink

#endif // LLVM_BINARYFORMAT_MSILLINK_H
