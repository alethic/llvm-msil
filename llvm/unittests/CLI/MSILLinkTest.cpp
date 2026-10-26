//===- MSILLinkTest.cpp - Link-info record v1 constants and data model ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/BinaryFormat/MSILLink.h against the link-info record v1
// specification (ImplementationPlan.md 5.8.8, 5.8.9 and 5.8.11): every
// constant, enumerator and default, the wasm and ELF values the record's bit
// assignments follow, and MSILLink::setDataModel per MSIL triple (5.9.17).
// Expected values come from the specification, never from the header.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/IR/Comdat.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::MSILLink;

namespace {

template <typename E> constexpr uint64_t val(E V) {
  return static_cast<uint64_t>(V);
}

// 5.8.8 Header: Magic "MSLK" (4D 53 4C 4B), Major 1, Minor 0, and the
// toolchain ABI version 1 (5.8.9: it changes only with a section 12 row).
TEST(CLIMSILLinkTest, MagicAndVersions) {
  static_assert(sizeof(Magic) == 4, "the magic is four bytes, no NUL");
  const uint8_t Expected[4] = {0x4D, 0x53, 0x4C, 0x4B};
  for (unsigned I = 0; I != 4; ++I)
    EXPECT_EQ(static_cast<uint8_t>(Magic[I]), Expected[I]) << "byte " << I;
  EXPECT_EQ(StringRef(Magic, 4), "MSLK");

  static_assert(
      std::is_same_v<std::remove_cv_t<decltype(MajorVersion)>, uint8_t>,
      "Major is a u8");
  static_assert(
      std::is_same_v<std::remove_cv_t<decltype(MinorVersion)>, uint8_t>,
      "Minor is a u8");
  EXPECT_EQ(MajorVersion, 1u);
  EXPECT_EQ(MinorVersion, 0u);
  EXPECT_EQ(ToolchainABIVersion, 1u);
}

// 5.8.11: the names of the reserved assemblies, types and modules.
TEST(CLIMSILLinkTest, ReservedNames) {
  EXPECT_EQ(ExternAssembly, "msil.extern");
  EXPECT_EQ(SymbolsType, "<msil>Symbols");
  EXPECT_EQ(FnAddrType, "<msil>FnAddr");
  EXPECT_EQ(BCLAssembly, "msil.bcl");
  EXPECT_EQ(LinkInfoAssembly, "msil.link");
  EXPECT_EQ(LinkInfoType, "<msil>LinkInfo");
  EXPECT_EQ(UnresolvedModule, "msil.unresolved");
}

// 5.8.8 Subsections: the kind table, must-understand kinds below 0x40, the
// skippable range 0x40-0x7F with the debug kinds 0x40-0x4F at its start.
TEST(CLIMSILLinkTest, SubsectionKinds) {
  static_assert(std::is_same_v<std::underlying_type_t<SubsectionKind>, uint8_t>,
                "the kind is a u8");
  EXPECT_EQ(val(SubsectionKind::SymTab), 0x01u);
  EXPECT_EQ(val(SubsectionKind::Segments), 0x02u);
  EXPECT_EQ(val(SubsectionKind::DataRelocs), 0x03u);
  EXPECT_EQ(val(SubsectionKind::Comdats), 0x04u);
  EXPECT_EQ(val(SubsectionKind::InitFini), 0x05u);
  EXPECT_EQ(val(SubsectionKind::Signatures), 0x06u);
  EXPECT_EQ(val(SubsectionKind::LinkerOptions), 0x07u);
  EXPECT_EQ(val(SubsectionKind::ManagedRequests), 0x08u);
  EXPECT_EQ(val(SubsectionKind::Types), 0x09u);
  EXPECT_EQ(val(SubsectionKind::FirstOptional), 0x40u);
  EXPECT_EQ(val(SubsectionKind::FirstDebug), 0x40u);
  EXPECT_EQ(val(SubsectionKind::LastDebug), 0x4Fu);
  EXPECT_EQ(val(SubsectionKind::LastOptional), 0x7Fu);

  // Kinds appear in increasing order and every kind Phase 2 defines must be
  // understood (below FirstOptional).
  const SubsectionKind Required[] = {
      SubsectionKind::SymTab,        SubsectionKind::Segments,
      SubsectionKind::DataRelocs,    SubsectionKind::Comdats,
      SubsectionKind::InitFini,      SubsectionKind::Signatures,
      SubsectionKind::LinkerOptions, SubsectionKind::ManagedRequests,
      SubsectionKind::Types};
  for (unsigned I = 0; I != std::size(Required); ++I) {
    EXPECT_LT(val(Required[I]), val(SubsectionKind::FirstOptional));
    if (I)
      EXPECT_LT(val(Required[I - 1]), val(Required[I]));
  }
}

// 5.8.8 SYMTAB Kind (FUNCTION 0, DATA 1, the wasm symbol type values) and
// SymbolFlags, whose bits follow wasm where the meaning matches.
TEST(CLIMSILLinkTest, SymbolKindAndFlags) {
  static_assert(std::is_same_v<std::underlying_type_t<SymbolKind>, uint8_t>,
                "Kind is a u8");
  EXPECT_EQ(val(SymbolKind::Function), 0u);
  EXPECT_EQ(val(SymbolKind::Data), 1u);
  EXPECT_EQ(val(SymbolKind::Function), val(wasm::WASM_SYMBOL_TYPE_FUNCTION));
  EXPECT_EQ(val(SymbolKind::Data), val(wasm::WASM_SYMBOL_TYPE_DATA));

  static_assert(std::is_same_v<std::underlying_type_t<SymbolFlags>, uint32_t>,
                "SymbolFlags is uint32_t");
  EXPECT_EQ(val(BindingMask), 0x3u);
  EXPECT_EQ(val(BindingGlobal), 0x0u);
  EXPECT_EQ(val(BindingWeak), 0x1u);
  EXPECT_EQ(val(BindingLocal), 0x2u);
  EXPECT_EQ(val(BindingCommon), 0x3u);
  EXPECT_EQ(val(VisibilityMask), 0xCu);
  EXPECT_EQ(val(VisibilityShift), 2u);
  EXPECT_EQ(val(Undefined), 0x10u);
  EXPECT_EQ(val(DLLExport), 0x20u);
  EXPECT_EQ(val(ExplicitImport), 0x40u);
  EXPECT_EQ(val(NoStrip), 0x80u);
  EXPECT_EQ(val(ThreadLocal), 0x100u);
  EXPECT_EQ(val(Alias), 0x200u);
  EXPECT_EQ(val(DLLImport), 0x400u);
  EXPECT_EQ(val(Import), 0x800u);

  // The wasm values the table names as the rule each bit implements.
  EXPECT_EQ(val(BindingMask), wasm::WASM_SYMBOL_BINDING_MASK);
  EXPECT_EQ(val(BindingGlobal), wasm::WASM_SYMBOL_BINDING_GLOBAL);
  EXPECT_EQ(val(BindingWeak), wasm::WASM_SYMBOL_BINDING_WEAK);
  EXPECT_EQ(val(BindingLocal), wasm::WASM_SYMBOL_BINDING_LOCAL);
  EXPECT_EQ(val(BindingCommon), wasm::WASM_SYMBOL_BINDING_COMMON);
  EXPECT_EQ(val(VisibilityMask), wasm::WASM_SYMBOL_VISIBILITY_MASK);
  EXPECT_EQ(val(Undefined), wasm::WASM_SYMBOL_UNDEFINED);
  EXPECT_EQ(val(DLLExport), wasm::WASM_SYMBOL_EXPORTED);
  EXPECT_EQ(val(ExplicitImport), wasm::WASM_SYMBOL_EXPLICIT_NAME);
  EXPECT_EQ(val(NoStrip), wasm::WASM_SYMBOL_NO_STRIP);
  EXPECT_EQ(val(ThreadLocal), wasm::WASM_SYMBOL_TLS);

  // The single-bit flags are distinct powers of two outside the binding and
  // visibility fields; Import (Q35 data imports) is one of them.
  const SymbolFlags Single[] = {Undefined,   DLLExport, ExplicitImport, NoStrip,
                                ThreadLocal, Alias,     DLLImport,      Import};
  uint64_t Seen = val(BindingMask) | val(VisibilityMask);
  EXPECT_EQ(Seen, 0xFu);
  for (SymbolFlags F : Single) {
    EXPECT_TRUE(isPowerOf2_64(val(F))) << "flag 0x" << utohexstr(val(F));
    EXPECT_EQ(Seen & val(F), 0u) << "flag 0x" << utohexstr(val(F));
    Seen |= val(F);
  }
  EXPECT_EQ(Seen, 0xFFFu);
}

// 5.8.8: VISIBILITY is ELF's STV_* (merged with lld/ELF's minimum rule),
// stored as value << VisibilityShift inside VisibilityMask.
TEST(CLIMSILLinkTest, Visibility) {
  static_assert(std::is_same_v<std::underlying_type_t<Visibility>, uint8_t>,
                "Visibility is a u8");
  EXPECT_EQ(val(Visibility::Default), val(ELF::STV_DEFAULT));
  EXPECT_EQ(val(Visibility::Internal), val(ELF::STV_INTERNAL));
  EXPECT_EQ(val(Visibility::Hidden), val(ELF::STV_HIDDEN));
  EXPECT_EQ(val(Visibility::Protected), val(ELF::STV_PROTECTED));
  EXPECT_EQ(val(Visibility::Default), 0u);
  EXPECT_EQ(val(Visibility::Internal), 1u);
  EXPECT_EQ(val(Visibility::Hidden), 2u);
  EXPECT_EQ(val(Visibility::Protected), 3u);
  for (Visibility V : {Visibility::Default, Visibility::Internal,
                       Visibility::Hidden, Visibility::Protected}) {
    uint64_t Bits = val(V) << val(VisibilityShift);
    EXPECT_EQ(Bits & ~val(VisibilityMask), 0u);
    EXPECT_EQ((Bits & val(VisibilityMask)) >> val(VisibilityShift), val(V));
  }
  EXPECT_EQ(val(Visibility::Protected) << val(VisibilityShift),
            val(VisibilityMask));
}

// 5.8.8 SEGMENTS flags (STRINGS, TLS and RETAIN are the wasm values),
// DATA_RELOCS types, COMDATS selection kinds and member kinds, INIT_FINI
// kinds, SIGNATURES call flags, LINKER_OPTIONS kinds and HeaderFlags.
TEST(CLIMSILLinkTest, RecordEnums) {
  static_assert(std::is_same_v<std::underlying_type_t<SegmentFlags>, uint32_t>,
                "SegmentFlags is uint32_t");
  static_assert(std::is_same_v<std::underlying_type_t<CallFlags>, uint32_t>,
                "CallFlags is uint32_t");
  static_assert(std::is_same_v<std::underlying_type_t<HeaderFlags>, uint32_t>,
                "HeaderFlags is uint32_t");
  EXPECT_EQ(val(SegStrings), 0x1u);
  EXPECT_EQ(val(SegTLS), 0x2u);
  EXPECT_EQ(val(SegRetain), 0x4u);
  EXPECT_EQ(val(SegZero), 0x8u);
  EXPECT_EQ(val(SegReadOnly), 0x10u);
  EXPECT_EQ(val(SegStrings), val(wasm::WASM_SEG_FLAG_STRINGS));
  EXPECT_EQ(val(SegTLS), val(wasm::WASM_SEG_FLAG_TLS));
  EXPECT_EQ(val(SegRetain), val(wasm::WASM_SEG_FLAG_RETAIN));

  static_assert(std::is_same_v<std::underlying_type_t<RelocType>, uint8_t>,
                "RelocType is a u8");
  EXPECT_EQ(val(RelocType::Addr64), 0u);
  EXPECT_EQ(val(RelocType::Addr32), 1u);
  EXPECT_EQ(val(RelocType::FnAddr64), 2u);
  EXPECT_EQ(val(RelocType::FnAddr32), 3u);
  EXPECT_EQ(val(RelocType::Diff), 4u);
  EXPECT_EQ(val(RelocType::TLSOffset32), 5u);
  EXPECT_EQ(val(RelocType::TLSOffset64), 6u);

  static_assert(
      std::is_same_v<std::underlying_type_t<ComdatMemberKind>, uint8_t>,
      "the member kind is a u8");
  EXPECT_EQ(val(ComdatMemberKind::FunctionSymbol), 0u);
  EXPECT_EQ(val(ComdatMemberKind::Segment), 1u);

  static_assert(std::is_same_v<std::underlying_type_t<InitKind>, uint8_t>,
                "the init kind is a u8");
  EXPECT_EQ(val(InitKind::Init), 0u);
  EXPECT_EQ(val(InitKind::Fini), 1u);

  EXPECT_EQ(val(NoPrototype), 0x1u);
  EXPECT_EQ(val(VarArg), 0x2u);
  EXPECT_EQ(val(Sret), 0x4u);
  EXPECT_EQ(val(NativeIncompatible), 0x8u);

  static_assert(
      std::is_same_v<std::underlying_type_t<LinkerOptionKind>, uint8_t>,
      "the linker option kind is a u8");
  EXPECT_EQ(val(LinkerOptionKind::DependentLib), 0u);
  EXPECT_EQ(val(LinkerOptionKind::LinkerOption), 1u);

  EXPECT_EQ(val(HasDebugInfo), 0x1u);
}

// 5.8.8 COMDATS: Selection equals llvm::Comdat::SelectionKind (header-only use
// of llvm/IR/Comdat.h; BinaryFormat cannot include IR headers itself).
TEST(CLIMSILLinkTest, ComdatSelectionMatchesIR) {
  static_assert(
      std::is_same_v<std::underlying_type_t<ComdatSelection>, uint8_t>,
      "Selection is a u8");
  EXPECT_EQ(val(ComdatSelection::Any), val(Comdat::Any));
  EXPECT_EQ(val(ComdatSelection::ExactMatch), val(Comdat::ExactMatch));
  EXPECT_EQ(val(ComdatSelection::Largest), val(Comdat::Largest));
  EXPECT_EQ(val(ComdatSelection::NoDeduplicate), val(Comdat::NoDeduplicate));
  EXPECT_EQ(val(ComdatSelection::SameSize), val(Comdat::SameSize));
  EXPECT_EQ(val(ComdatSelection::Any), 0u);
  EXPECT_EQ(val(ComdatSelection::ExactMatch), 1u);
  EXPECT_EQ(val(ComdatSelection::Largest), 2u);
  EXPECT_EQ(val(ComdatSelection::NoDeduplicate), 3u);
  EXPECT_EQ(val(ComdatSelection::SameSize), 4u);
}

// 5.8.11: the field types of the in-memory record.
#define EXPECT_FIELD_TYPE(Struct, Field, Type)                                 \
  static_assert(std::is_same_v<decltype(Struct::Field), Type>,                 \
                #Struct "::" #Field " is " #Type)

TEST(CLIMSILLinkTest, FieldTypes) {
  EXPECT_FIELD_TYPE(Header, Major, uint8_t);
  EXPECT_FIELD_TYPE(Header, Minor, uint8_t);
  EXPECT_FIELD_TYPE(Header, Flags, uint32_t);
  EXPECT_FIELD_TYPE(Header, Triple, std::string);
  EXPECT_FIELD_TYPE(Header, ABIVersion, uint32_t);
  EXPECT_FIELD_TYPE(Header, ABIFlags, uint64_t);
  EXPECT_FIELD_TYPE(Header, FeatureFlags, uint64_t);
  EXPECT_FIELD_TYPE(Header, PointerSize, uint8_t);
  EXPECT_FIELD_TYPE(Header, LongSize, uint8_t);
  EXPECT_FIELD_TYPE(Header, WCharSize, uint8_t);
  EXPECT_FIELD_TYPE(Header, LongDoubleSize, uint8_t);

  EXPECT_FIELD_TYPE(Symbol, Kind, SymbolKind);
  EXPECT_FIELD_TYPE(Symbol, Flags, uint32_t);
  EXPECT_FIELD_TYPE(Symbol, Name, std::string);
  EXPECT_FIELD_TYPE(Symbol, Token, uint32_t);
  EXPECT_FIELD_TYPE(Symbol, Segment, uint32_t);
  EXPECT_FIELD_TYPE(Symbol, Offset, uint64_t);
  EXPECT_FIELD_TYPE(Symbol, Size, uint64_t);
  EXPECT_FIELD_TYPE(Symbol, Aliasee, uint32_t);
  EXPECT_FIELD_TYPE(Symbol, AliasOffset, int64_t);
  EXPECT_FIELD_TYPE(Symbol, CommonSize, uint64_t);
  EXPECT_FIELD_TYPE(Symbol, CommonAlignLog2, uint8_t);
  EXPECT_FIELD_TYPE(Symbol, ImportModule, std::string);
  EXPECT_FIELD_TYPE(Symbol, ImportName, std::string);

  EXPECT_FIELD_TYPE(Segment, Name, std::string);
  EXPECT_FIELD_TYPE(Segment, Flags, uint32_t);
  EXPECT_FIELD_TYPE(Segment, AlignLog2, uint8_t);
  EXPECT_FIELD_TYPE(Segment, Size, uint64_t);
  EXPECT_FIELD_TYPE(Segment, Bytes, std::vector<uint8_t>);

  EXPECT_FIELD_TYPE(MSILLink::Reloc, Type, RelocType);
  EXPECT_FIELD_TYPE(MSILLink::Reloc, Segment, uint32_t);
  EXPECT_FIELD_TYPE(MSILLink::Reloc, Offset, uint64_t);
  EXPECT_FIELD_TYPE(MSILLink::Reloc, Symbol, uint32_t);
  EXPECT_FIELD_TYPE(MSILLink::Reloc, Addend, int64_t);
  EXPECT_FIELD_TYPE(MSILLink::Reloc, Width, uint8_t);
  EXPECT_FIELD_TYPE(MSILLink::Reloc, Symbol2, uint32_t);

  using MemberList = std::vector<std::pair<ComdatMemberKind, uint32_t>>;
  EXPECT_FIELD_TYPE(ComdatGroup, Name, std::string);
  EXPECT_FIELD_TYPE(ComdatGroup, Selection, ComdatSelection);
  EXPECT_FIELD_TYPE(ComdatGroup, Members, MemberList);

  EXPECT_FIELD_TYPE(InitEntry, Kind, InitKind);
  EXPECT_FIELD_TYPE(InitEntry, Priority, uint32_t);
  EXPECT_FIELD_TYPE(InitEntry, Symbol, uint32_t);
  EXPECT_FIELD_TYPE(InitEntry, ComdatPlusOne, uint32_t);

  EXPECT_FIELD_TYPE(ByvalParam, Param, uint32_t);
  EXPECT_FIELD_TYPE(ByvalParam, Size, uint64_t);
  EXPECT_FIELD_TYPE(ByvalParam, AlignLog2, uint8_t);

  EXPECT_FIELD_TYPE(SignatureInfo, Token, uint32_t);
  EXPECT_FIELD_TYPE(SignatureInfo, Flags, uint32_t);
  EXPECT_FIELD_TYPE(SignatureInfo, FixedParams, uint32_t);
  EXPECT_FIELD_TYPE(SignatureInfo, SretSize, uint64_t);
  EXPECT_FIELD_TYPE(SignatureInfo, SretAlignLog2, uint8_t);
  EXPECT_FIELD_TYPE(SignatureInfo, Byval, std::vector<ByvalParam>);

  EXPECT_FIELD_TYPE(LinkerOption, Kind, LinkerOptionKind);
  EXPECT_FIELD_TYPE(LinkerOption, Args, std::vector<std::string>);

  using Skipped = std::vector<std::pair<uint8_t, std::vector<uint8_t>>>;
  EXPECT_FIELD_TYPE(Record, Hdr, Header);
  EXPECT_FIELD_TYPE(Record, Symbols, std::vector<Symbol>);
  EXPECT_FIELD_TYPE(Record, Segments, std::vector<Segment>);
  EXPECT_FIELD_TYPE(Record, Relocs, std::vector<MSILLink::Reloc>);
  EXPECT_FIELD_TYPE(Record, Comdats, std::vector<ComdatGroup>);
  EXPECT_FIELD_TYPE(Record, Inits, std::vector<InitEntry>);
  EXPECT_FIELD_TYPE(Record, Signatures, std::vector<SignatureInfo>);
  EXPECT_FIELD_TYPE(Record, LinkerOptions, std::vector<LinkerOption>);
  EXPECT_FIELD_TYPE(Record, SkippedOptional, Skipped);
}

// 5.8.11: the default member values. The kinds without a default
// (Symbol::Kind, Reloc::Type, ComdatGroup::Selection, InitEntry::Kind,
// LinkerOption::Kind) are left alone.
TEST(CLIMSILLinkTest, Defaults) {
  Header H;
  EXPECT_EQ(H.Major, 1u);
  EXPECT_EQ(H.Minor, 0u);
  EXPECT_EQ(H.Flags, 0u);
  EXPECT_TRUE(H.Triple.empty());
  EXPECT_EQ(H.ABIVersion, 1u);
  EXPECT_EQ(H.ABIFlags, 0u);
  EXPECT_EQ(H.FeatureFlags, 0u);
  EXPECT_EQ(H.PointerSize, 8u);
  EXPECT_EQ(H.LongSize, 8u);
  EXPECT_EQ(H.WCharSize, 4u);
  EXPECT_EQ(H.LongDoubleSize, 8u);

  Symbol S;
  EXPECT_EQ(S.Flags, 0u);
  EXPECT_TRUE(S.Name.empty());
  EXPECT_EQ(S.Token, 0u);
  EXPECT_EQ(S.Segment, 0u);
  EXPECT_EQ(S.Offset, 0u);
  EXPECT_EQ(S.Size, 0u);
  EXPECT_EQ(S.Aliasee, 0u);
  EXPECT_EQ(S.AliasOffset, 0);
  EXPECT_EQ(S.CommonSize, 0u);
  EXPECT_EQ(S.CommonAlignLog2, 0u);
  EXPECT_TRUE(S.ImportModule.empty());
  EXPECT_TRUE(S.ImportName.empty());

  Segment Seg;
  EXPECT_TRUE(Seg.Name.empty());
  EXPECT_EQ(Seg.Flags, 0u);
  EXPECT_EQ(Seg.AlignLog2, 0u);
  EXPECT_EQ(Seg.Size, 0u);
  EXPECT_TRUE(Seg.Bytes.empty());

  MSILLink::Reloc R;
  EXPECT_EQ(R.Addend, 0);
  EXPECT_EQ(R.Width, 0u);
  EXPECT_EQ(R.Symbol2, 0u);

  ComdatGroup G;
  EXPECT_TRUE(G.Name.empty());
  EXPECT_TRUE(G.Members.empty());

  InitEntry IE;
  EXPECT_EQ(IE.ComdatPlusOne, 0u);

  SignatureInfo SI;
  EXPECT_EQ(SI.Flags, 0u);
  EXPECT_EQ(SI.FixedParams, 0u);
  EXPECT_EQ(SI.SretSize, 0u);
  EXPECT_EQ(SI.SretAlignLog2, 0u);
  EXPECT_TRUE(SI.Byval.empty());

  LinkerOption LO;
  EXPECT_TRUE(LO.Args.empty());

  Record Rec;
  EXPECT_EQ(Rec.Hdr.Major, 1u);
  EXPECT_EQ(Rec.Hdr.ABIVersion, 1u);
  EXPECT_TRUE(Rec.Symbols.empty());
  EXPECT_TRUE(Rec.Segments.empty());
  EXPECT_TRUE(Rec.Relocs.empty());
  EXPECT_TRUE(Rec.Comdats.empty());
  EXPECT_TRUE(Rec.Inits.empty());
  EXPECT_TRUE(Rec.Signatures.empty());
  EXPECT_TRUE(Rec.LinkerOptions.empty());
  EXPECT_TRUE(Rec.SkippedOptional.empty());
}

// An IMPORT data symbol (5.8.8, C8: DATA and UNDEFINED, a non-empty
// ImportName, an empty ImportModule meaning "find it natively") is plain data
// in the in-memory record.
TEST(CLIMSILLinkTest, ImportSymbol) {
  Symbol S{};
  S.Kind = SymbolKind::Data;
  S.Flags = BindingGlobal | Undefined | Import;
  S.Name = "my_counter";
  S.ImportName = "native_counter";
  EXPECT_EQ(S.Flags & BindingMask, val(BindingGlobal));
  EXPECT_NE(S.Flags & Import, 0u);
  EXPECT_NE(S.Flags & Undefined, 0u);
  EXPECT_EQ(S.Flags & ThreadLocal, 0u);
  EXPECT_TRUE(S.ImportModule.empty());
  EXPECT_EQ(S.ImportName, "native_counter");

  Record Rec;
  Rec.Symbols.push_back(S);
  Rec.Symbols.back().ImportModule = "libfoo";
  EXPECT_EQ(Rec.Symbols[0].ImportModule, "libfoo");
  EXPECT_TRUE(S.ImportModule.empty());
}

struct DataModel {
  unsigned Pointer, Long, WChar, LongDouble;
};

DataModel getDataModel(const Triple &TT, unsigned PointerSize,
                       unsigned WCharSize) {
  Header H;
  setDataModel(H, TT, PointerSize, WCharSize);
  return {H.PointerSize, H.LongSize, H.WCharSize, H.LongDoubleSize};
}

#define EXPECT_DATA_MODEL(DM, P, L, W, D)                                      \
  do {                                                                         \
    DataModel M = (DM);                                                        \
    EXPECT_EQ(M.Pointer, P##u);                                                \
    EXPECT_EQ(M.Long, L##u);                                                   \
    EXPECT_EQ(M.WChar, W##u);                                                  \
    EXPECT_EQ(M.LongDouble, D##u);                                             \
  } while (false)

// 5.8.9's DataModel rule and 5.9.17's M13 cases: pointer size 8 and
// WCharSize = Triple::getDefaultWCharSize() give 8,4,2,8 on the Windows MSIL
// triples and 8,8,4,8 on the Linux ones.
TEST(CLIMSILLinkTest, SetDataModelPerTriple) {
  struct Case {
    const char *TripleName;
    unsigned DefaultWChar;
    DataModel Expected;
  } Cases[] = {
      {"x86_64-pc-windows-msvc-msil", 2, {8, 4, 2, 8}},
      {"x86_64-w64-windows-gnu-msil", 2, {8, 4, 2, 8}},
      {"x86_64-unknown-linux-gnu-msil", 4, {8, 8, 4, 8}},
      {"aarch64-unknown-linux-gnu-msil", 4, {8, 8, 4, 8}},
  };
  for (const Case &C : Cases) {
    SCOPED_TRACE(C.TripleName);
    Triple TT(C.TripleName);
    ASSERT_TRUE(TT.isOSBinFormatMSIL());
    ASSERT_EQ(TT.getDefaultWCharSize(), C.DefaultWChar);
    DataModel M = getDataModel(TT, 8, TT.getDefaultWCharSize());
    EXPECT_EQ(M.Pointer, C.Expected.Pointer);
    EXPECT_EQ(M.Long, C.Expected.Long);
    EXPECT_EQ(M.WChar, C.Expected.WChar);
    EXPECT_EQ(M.LongDouble, C.Expected.LongDouble);
  }
}

// The overrides: the module flag wchar_size replaces the triple's default
// (2 on linux-gnu gives 8,8,2,8), a 4-byte pointer makes long 4 bytes on every
// OS, Cygwin keeps an 8-byte long, and long double is always 8 (D3).
TEST(CLIMSILLinkTest, SetDataModelOverrides) {
  Triple Linux("x86_64-unknown-linux-gnu-msil");
  Triple Windows("x86_64-pc-windows-msvc-msil");
  EXPECT_DATA_MODEL(getDataModel(Linux, 8, 2), 8, 8, 2, 8);
  EXPECT_DATA_MODEL(getDataModel(Windows, 8, 4), 8, 4, 4, 8);
  EXPECT_DATA_MODEL(getDataModel(Linux, 4, 4), 4, 4, 4, 8);
  EXPECT_DATA_MODEL(getDataModel(Windows, 4, 2), 4, 4, 2, 8);

  Triple Cygwin("x86_64-pc-windows-cygnus");
  ASSERT_TRUE(Cygwin.isOSWindows());
  ASSERT_TRUE(Cygwin.isWindowsCygwinEnvironment());
  EXPECT_DATA_MODEL(getDataModel(Cygwin, 8, 2), 8, 8, 2, 8);
  EXPECT_DATA_MODEL(getDataModel(Cygwin, 4, 2), 4, 4, 2, 8);
}

// setDataModel writes the four DataModel bytes and nothing else.
TEST(CLIMSILLinkTest, SetDataModelTouchesOnlyTheDataModel) {
  Header H;
  H.Flags = HasDebugInfo;
  H.Triple = "x86_64-pc-windows-msvc-msil";
  H.ABIFlags = 5;
  H.FeatureFlags = 6;
  H.PointerSize = 1;
  H.LongSize = 1;
  H.WCharSize = 1;
  H.LongDoubleSize = 16;
  setDataModel(H, Triple(H.Triple), 8, 2);
  EXPECT_EQ(H.Major, 1u);
  EXPECT_EQ(H.Minor, 0u);
  EXPECT_EQ(H.Flags, val(HasDebugInfo));
  EXPECT_EQ(H.Triple, "x86_64-pc-windows-msvc-msil");
  EXPECT_EQ(H.ABIVersion, 1u);
  EXPECT_EQ(H.ABIFlags, 5u);
  EXPECT_EQ(H.FeatureFlags, 6u);
  EXPECT_EQ(H.PointerSize, 8u);
  EXPECT_EQ(H.LongSize, 4u);
  EXPECT_EQ(H.WCharSize, 2u);
  EXPECT_EQ(H.LongDoubleSize, 8u);
}

} // namespace
