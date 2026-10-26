//===- LinkInfoTest.cpp - Link-info record v1 codec -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/LinkInfo.h against the MSIL implementation plan: the record
// primitives and framing (5.8.8), the encoder's field order, the decoder's C8
// checks and version rules (5.8.8, 5.8.9), the carrier (5.8.7), the C1-C7
// validation against the metadata (5.8.5), the COFF symbol view (5.8.6) and
// the --link-info printer (5.8.11, extended uniformly by M17.c item 7).
//
// The test has its own encoder of 5.8.8 (SpecEncoder), written from the
// field tables: it builds the malformed records the decoder must refuse, and
// encodeLinkInfo must agree with it byte for byte on valid ones. The golden
// record's bytes were computed by hand and checked with an independent
// Python encoder in scratch. Expected values never come from the
// implementation under test.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/LinkInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/bit.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/CustomAttribute.h"
#include "llvm/CLI/MetadataBuilder.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/CLI/MethodBody.h"
#include "llvm/CLI/PEImage.h"
#include "llvm/CLI/PEWriter.h"
#include "llvm/CLI/TableSchema.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::cli;
using namespace llvm::MSILLink;
using testing::HasSubstr;
using testing::StartsWith;

namespace {

using ByteVec = std::vector<uint8_t>;

constexpr char RecordPrefix[] = "invalid link-info record: ";
constexpr char FeatureSuffix[] = ", which this toolchain does not support";

//===----------------------------------------------------------------------===//
// The test's own encoder of 5.8.8
//===----------------------------------------------------------------------===//

struct SpecEncoder {
  ByteVec Out;
  // The end of the header and of each subsection: the lengths at which a
  // record is complete.
  std::vector<size_t> Boundaries;
  // Where named fields start in Out ("magic", "symbol 2", "symbol 2 name",
  // "relocation 1", "subsection 0x01", ...): the offsets that the decoder's
  // messages must give (5.7.9).
  std::vector<std::pair<std::string, size_t>> Marks;

  void mark(std::string Name) {
    Marks.emplace_back(std::move(Name), Out.size());
  }
  size_t at(StringRef Name) const {
    for (const auto &[N, Offset] : Marks)
      if (N == Name)
        return Offset;
    ADD_FAILURE() << "the test's encoder has no field '" << Name.str() << "'";
    return SIZE_MAX;
  }

  void u8(uint8_t V) { Out.push_back(V); }
  void u32(uint32_t V) {
    for (unsigned I = 0; I < 4; ++I)
      Out.push_back((V >> (8 * I)) & 0xFF);
  }
  void uleb(uint64_t V) {
    do {
      uint8_t B = V & 0x7F;
      V >>= 7;
      Out.push_back(V ? B | 0x80 : B);
    } while (V);
  }
  void sleb(int64_t V) {
    bool More;
    do {
      uint8_t B = V & 0x7F;
      V >>= 7; // arithmetic
      More = !((V == 0 && !(B & 0x40)) || (V == -1 && (B & 0x40)));
      Out.push_back(More ? B | 0x80 : B);
    } while (More);
  }
  void bytes(ArrayRef<uint8_t> B) { Out.insert(Out.end(), B.begin(), B.end()); }
  void str(StringRef S) {
    uleb(S.size());
    bytes(arrayRefFromStringRef(S));
  }
  // A subsection: "subsection 0x<KK>" marks its kind byte, "subsection
  // 0x<KK> payload" its payload, and the payload's own marks are moved along.
  void sub(uint8_t Kind, const SpecEncoder &Payload) {
    std::string Name = "subsection 0x" + utohexstr(Kind, false, 2);
    mark(Name);
    u8(Kind);
    uleb(Payload.Out.size());
    const size_t Base = Out.size();
    mark(Name + " payload");
    bytes(Payload.Out);
    for (const auto &[N, Offset] : Payload.Marks)
      Marks.emplace_back(N, Base + Offset);
    Boundaries.push_back(Out.size());
  }
};

// The SYMTAB payload, with the marks "symbol <i>" (the entry), "symbol <i>
// name", "symbol <i> size" (defined data) and "symbol <i> import name".
SpecEncoder specSymbolTable(const Record &R) {
  SpecEncoder P;
  P.uleb(R.Symbols.size());
  for (size_t I = 0; I < R.Symbols.size(); ++I) {
    const Symbol &S = R.Symbols[I];
    const std::string Entry = "symbol " + std::to_string(I);
    bool Undef = S.Flags & Undefined, IsAlias = S.Flags & Alias,
         Common = (S.Flags & BindingMask) == BindingCommon,
         Data = S.Kind == SymbolKind::Data;
    P.mark(Entry);
    P.u8(static_cast<uint8_t>(S.Kind));
    P.uleb(S.Flags);
    P.mark(Entry + " name");
    P.str(S.Name);
    if (!Undef && !IsAlias)
      P.u32(S.Token);
    if (Data && !Undef && !IsAlias && !Common) {
      P.uleb(S.Segment);
      P.uleb(S.Offset);
      P.mark(Entry + " size");
      P.uleb(S.Size);
    }
    if (IsAlias)
      P.uleb(S.Aliasee);
    if (IsAlias && Data) {
      P.sleb(S.AliasOffset);
      P.uleb(S.Size);
    }
    if (Common) {
      P.uleb(S.CommonSize);
      P.u8(S.CommonAlignLog2);
    }
    if (S.Flags & Import) {
      P.str(S.ImportModule);
      P.mark(Entry + " import name");
      P.str(S.ImportName);
    }
  }
  return P;
}

ByteVec specSymbols(const Record &R) { return specSymbolTable(R).Out; }

// 5.8.8, field by field: the header, then each non-empty subsection in kind
// order, then the kept optional subsections.
SpecEncoder encodeSpec(const Record &R) {
  SpecEncoder E;
  E.mark("magic");
  E.bytes({'M', 'S', 'L', 'K'});
  E.mark("major version");
  E.u8(R.Hdr.Major);
  E.u8(R.Hdr.Minor);
  E.mark("header flags");
  E.uleb(R.Hdr.Flags);
  E.str(R.Hdr.Triple);
  E.uleb(R.Hdr.ABIVersion);
  E.uleb(R.Hdr.ABIFlags);
  E.uleb(R.Hdr.FeatureFlags);
  E.u8(R.Hdr.PointerSize);
  E.u8(R.Hdr.LongSize);
  E.u8(R.Hdr.WCharSize);
  E.u8(R.Hdr.LongDoubleSize);
  E.Boundaries.push_back(E.Out.size());
  if (!R.Symbols.empty())
    E.sub(0x01, specSymbolTable(R));
  if (!R.Segments.empty()) {
    SpecEncoder P;
    P.uleb(R.Segments.size());
    for (size_t I = 0; I < R.Segments.size(); ++I) {
      const Segment &S = R.Segments[I];
      const std::string Entry = "segment " + std::to_string(I);
      P.mark(Entry);
      P.str(S.Name);
      P.uleb(S.Flags);
      P.mark(Entry + " alignment");
      P.u8(S.AlignLog2);
      P.mark(Entry + " size");
      P.uleb(S.Size);
      if (!(S.Flags & SegZero))
        P.bytes(S.Bytes);
    }
    E.sub(0x02, P);
  }
  if (!R.Relocs.empty()) {
    SpecEncoder P;
    P.uleb(R.Relocs.size());
    for (const MSILLink::Reloc &X : R.Relocs) {
      P.mark("relocation " + std::to_string(&X - R.Relocs.data()));
      P.u8(static_cast<uint8_t>(X.Type));
      P.uleb(X.Segment);
      P.uleb(X.Offset);
      P.uleb(X.Symbol);
      P.sleb(X.Addend);
      if (X.Type == RelocType::Diff) {
        P.u8(X.Width);
        P.uleb(X.Symbol2);
      }
    }
    E.sub(0x03, P);
  }
  if (!R.Comdats.empty()) {
    SpecEncoder P;
    P.uleb(R.Comdats.size());
    for (const ComdatGroup &G : R.Comdats) {
      P.mark("comdat " + std::to_string(&G - R.Comdats.data()));
      P.str(G.Name);
      P.u8(static_cast<uint8_t>(G.Selection));
      P.uleb(G.Members.size());
      for (const auto &M : G.Members) {
        P.u8(static_cast<uint8_t>(M.first));
        P.uleb(M.second);
      }
    }
    E.sub(0x04, P);
  }
  if (!R.Inits.empty()) {
    SpecEncoder P;
    P.uleb(R.Inits.size());
    for (const InitEntry &I : R.Inits) {
      const std::string Entry = "init " + std::to_string(&I - R.Inits.data());
      P.mark(Entry);
      P.u8(static_cast<uint8_t>(I.Kind));
      P.mark(Entry + " priority");
      P.uleb(I.Priority);
      P.uleb(I.Symbol);
      P.uleb(I.ComdatPlusOne);
    }
    E.sub(0x05, P);
  }
  if (!R.Signatures.empty()) {
    SpecEncoder P;
    P.uleb(R.Signatures.size());
    for (const SignatureInfo &S : R.Signatures) {
      P.mark("signature " + std::to_string(&S - R.Signatures.data()));
      P.u32(S.Token);
      P.uleb(S.Flags);
      if (S.Flags & VarArg)
        P.uleb(S.FixedParams);
      if (S.Flags & Sret) {
        P.uleb(S.SretSize);
        P.u8(S.SretAlignLog2);
      }
      P.uleb(S.Byval.size());
      for (const ByvalParam &B : S.Byval) {
        P.uleb(B.Param);
        P.uleb(B.Size);
        P.u8(B.AlignLog2);
      }
    }
    E.sub(0x06, P);
  }
  if (!R.LinkerOptions.empty()) {
    SpecEncoder P;
    P.uleb(R.LinkerOptions.size());
    for (const LinkerOption &L : R.LinkerOptions) {
      P.u8(static_cast<uint8_t>(L.Kind));
      if (L.Kind == LinkerOptionKind::DependentLib) {
        P.str(L.Args.empty() ? StringRef() : StringRef(L.Args[0]));
        continue;
      }
      P.uleb(L.Args.size());
      for (const std::string &A : L.Args)
        P.str(A);
    }
    E.sub(0x07, P);
  }
  for (const auto &[Kind, Payload] : R.SkippedOptional) {
    SpecEncoder P;
    P.bytes(Payload);
    E.sub(Kind, P);
  }
  return E;
}

ByteVec encodeLib(const Record &R) {
  SmallVector<uint8_t, 0> Out;
  encodeLinkInfo(R, Out);
  return ByteVec(Out.begin(), Out.end());
}

//===----------------------------------------------------------------------===//
// Records
//===----------------------------------------------------------------------===//

Symbol sym(SymbolKind K, uint32_t Flags, StringRef Name, uint32_t Token = 0) {
  Symbol S;
  S.Kind = K;
  S.Flags = Flags;
  S.Name = Name.str();
  S.Token = Token;
  return S;
}
Symbol dataSym(uint32_t Flags, StringRef Name, uint32_t Token, uint32_t Segment,
               uint64_t Offset, uint64_t Size) {
  Symbol S = sym(SymbolKind::Data, Flags, Name, Token);
  S.Segment = Segment;
  S.Offset = Offset;
  S.Size = Size;
  return S;
}
Symbol importSym(uint32_t Flags, StringRef Name, StringRef Module,
                 StringRef Entry) {
  Symbol S = sym(SymbolKind::Data, Flags | Undefined | Import, Name);
  S.ImportModule = Module.str();
  S.ImportName = Entry.str();
  return S;
}
Segment segment(StringRef Name, uint32_t Flags, uint8_t AlignLog2,
                uint64_t Size, ByteVec Bytes = {}) {
  Segment S;
  S.Name = Name.str();
  S.Flags = Flags;
  S.AlignLog2 = AlignLog2;
  S.Size = Size;
  if (!(Flags & SegZero) && Bytes.empty())
    Bytes.assign(Size, 0);
  S.Bytes = std::move(Bytes);
  return S;
}
MSILLink::Reloc reloc(RelocType T, uint32_t Seg, uint64_t Off, uint32_t Sym,
                      int64_t Addend = 0, uint8_t Width = 0,
                      uint32_t Sym2 = 0) {
  MSILLink::Reloc R;
  R.Type = T;
  R.Segment = Seg;
  R.Offset = Off;
  R.Symbol = Sym;
  R.Addend = Addend;
  R.Width = Width;
  R.Symbol2 = Sym2;
  return R;
}
InitEntry init(InitKind K, uint32_t Priority, uint32_t Sym,
               uint32_t ComdatPlusOne = 0) {
  InitEntry I;
  I.Kind = K;
  I.Priority = Priority;
  I.Symbol = Sym;
  I.ComdatPlusOne = ComdatPlusOne;
  return I;
}
SignatureInfo sig(uint32_t Token, uint32_t Flags,
                  std::vector<ByvalParam> Byval = {}) {
  SignatureInfo S;
  S.Token = Token;
  S.Flags = Flags;
  S.Byval = std::move(Byval);
  return S;
}
ComdatGroup comdat(StringRef Name, ComdatSelection Sel,
                   std::vector<std::pair<ComdatMemberKind, uint32_t>> M) {
  ComdatGroup G;
  G.Name = Name.str();
  G.Selection = Sel;
  G.Members = std::move(M);
  return G;
}
LinkerOption linkerOption(LinkerOptionKind K, std::vector<std::string> Args) {
  LinkerOption L;
  L.Kind = K;
  L.Args = std::move(Args);
  return L;
}

constexpr uint32_t vis(Visibility V) {
  return static_cast<uint32_t>(V) << VisibilityShift;
}
constexpr uint32_t field(uint32_t Row) { return 0x04000000 | Row; }
constexpr uint32_t method(uint32_t Row) { return 0x06000000 | Row; }
constexpr uint32_t memberRef(uint32_t Row) { return 0x0A000000 | Row; }

// A record with every entry kind, binding, visibility, flag, segment flag,
// relocation type, selection, member kind, init kind, call flag, linker
// option kind and a kept optional subsection. Its indexes match 5.8.11's
// --link-info example (table, a, b, my_counter, common_c, weakfn, ctor_a,
// add3, hookee and hook).
Record fullRecord() {
  Record R;
  R.Hdr.Triple = "x86_64-pc-windows-msvc-msil";
  R.Hdr.PointerSize = 8;
  R.Hdr.LongSize = 4;
  R.Hdr.WCharSize = 2;
  R.Hdr.LongDoubleSize = 8;
  const SymbolKind F = SymbolKind::Function;
  R.Symbols = {
      dataSym(BindingGlobal, "table", field(1), 0, 0, 24), // 0
      dataSym(BindingLocal | vis(Visibility::Hidden) | NoStrip, "a", field(2),
              1, 0, 8), // 1
      dataSym(BindingWeak | vis(Visibility::Protected), "b", field(3), 2, 0,
              16), // 2
      dataSym(BindingGlobal | vis(Visibility::Internal) | DLLExport, "zeroed",
              field(4), 4, 0, 16),                                     // 3
      importSym(BindingGlobal, "my_counter", "", "native_counter"),    // 4
      sym(SymbolKind::Data, BindingCommon, "common_c", field(5)),      // 5
      sym(F, BindingWeak, "weakfn", method(1)),                        // 6
      sym(F, BindingLocal, "ctor_a", method(2)),                       // 7
      sym(F, BindingGlobal | Undefined, "add3"),                       // 8
      sym(F, Undefined | ExplicitImport | DLLImport, "GetTickCount"),  // 9
      sym(F, BindingGlobal, "hookee", method(3)),                      // 10
      sym(F, BindingWeak | Alias, "hook"),                             // 11
      sym(SymbolKind::Data, Alias, "table_tail"),                      // 12
      importSym(BindingWeak, "imported_var", "libfoo", "foo_var"),     // 13
      dataSym(BindingLocal | ThreadLocal, "tls_v", field(6), 5, 0, 4), // 14
      sym(SymbolKind::Data, BindingWeak | Undefined, "weak_undef_data"),
      sym(F, BindingWeak | Undefined, "weak_undef_fn"),            // 16
      sym(F, DLLExport | NoStrip, "exported", method(4)),          // 17
      sym(SymbolKind::Data, Undefined | DLLImport, "dllimp_data"), // 18
      dataSym(ThreadLocal, "tls_z", field(7), 6, 0, 4),            // 19
  };
  R.Symbols[5].CommonSize = 4;
  R.Symbols[5].CommonAlignLog2 = 2;
  R.Symbols[11].Aliasee = 10;
  R.Symbols[12].Aliasee = 0;
  R.Symbols[12].AliasOffset = 16;
  R.Symbols[12].Size = 8;

  ByteVec Seg7(40, 0);
  for (unsigned I = 0; I < 4; ++I) {
    Seg7[4 + I] = 0xAA + 0x11 * I;           // AA BB CC DD
    Seg7[36 + I] = (0xEE + 0x11 * I) & 0xFF; // EE FF 10 21
  }
  R.Segments = {
      segment(".rodata", SegReadOnly, 3, 24), // 0
      segment(".data", SegRetain, 3, 8),      // 1
      segment(".data.rel", 0, 3, 16,
              {1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 13, 14, 15, 16}), // 2
      segment(".rodata.str1.1", SegStrings | SegReadOnly, 0, 6,
              {'h', 'e', 'l', 'l', 'o', 0}),            // 3
      segment(".bss", SegZero, 2, 16),                  // 4
      segment(".tdata", SegTLS, 2, 4, {0x2A, 0, 0, 0}), // 5
      segment(".tbss", SegTLS | SegZero, 2, 4),         // 6
      segment("my$section", 0, 0, 40, Seg7),            // 7
  };
  R.Relocs = {
      reloc(RelocType::FnAddr64, 0, 0, 8),
      reloc(RelocType::Addr64, 0, 8, 1),
      reloc(RelocType::FnAddr32, 0, 16, 6),
      reloc(RelocType::Addr32, 0, 20, 2, -4),
      reloc(RelocType::Addr64, 1, 0, 12, 300),
      reloc(RelocType::Diff, 2, 8, 1, 0, 4, 2),
      reloc(RelocType::TLSOffset32, 7, 0, 14),
      reloc(RelocType::TLSOffset64, 7, 8, 19, -1),
      reloc(RelocType::Diff, 7, 16, 0, 64, 16, 5),
      reloc(RelocType::Addr32, 7, 32, 4),
  };
  const ComdatMemberKind Fn = ComdatMemberKind::FunctionSymbol,
                         Sg = ComdatMemberKind::Segment;
  R.Comdats = {
      comdat("??_C@_05CJBACGMB@hello?$AA@", ComdatSelection::Any, {{Sg, 3}}),
      comdat("weakfn", ComdatSelection::NoDeduplicate, {{Fn, 6}, {Sg, 1}}),
      comdat("x", ComdatSelection::ExactMatch, {{Fn, 10}}),
      comdat("l", ComdatSelection::Largest, {{Sg, 5}}),
      comdat("s", ComdatSelection::SameSize, {{Sg, 6}}),
  };
  R.Inits = {init(InitKind::Init, 200, 7), init(InitKind::Init, 65535, 6, 2),
             init(InitKind::Fini, 0, 10), init(InitKind::Init, 101, 17)};
  SignatureInfo VarArgs = sig(memberRef(3), VarArg);
  VarArgs.FixedParams = 2;
  SignatureInfo SretSig =
      sig(method(3), Sret | NativeIncompatible, {{1, 8, 3}, {2, 300, 4}});
  SretSig.SretSize = 24;
  SretSig.SretAlignLog2 = 3;
  SignatureInfo All =
      sig(method(4), NoPrototype | VarArg | Sret | NativeIncompatible);
  All.FixedParams = 1;
  All.SretSize = 16;
  All.SretAlignLog2 = 2;
  R.Signatures = {sig(method(1), 0),
                  sig(method(2), NoPrototype),
                  sig(memberRef(2), NoPrototype, {{1, 12, 2}}),
                  VarArgs,
                  SretSig,
                  All};
  R.LinkerOptions = {linkerOption(LinkerOptionKind::DependentLib, {"m"}),
                     linkerOption(LinkerOptionKind::LinkerOption,
                                  {"/DEFAULTLIB:ws2_32.lib", "-lws2_32"}),
                     linkerOption(LinkerOptionKind::DependentLib, {"ws2_32"})};
  R.SkippedOptional = {{0x45, {1, 2, 3}}, {0x7F, {}}};
  return R;
}

// A small valid record that the C8 cases change one field at a time.
Record baseRecord() {
  Record R;
  R.Hdr.Triple = "x86_64-unknown-linux-gnu-msil";
  const SymbolKind F = SymbolKind::Function;
  R.Symbols = {dataSym(0, "d", field(1), 0, 0, 8), // 0
               sym(F, 0, "f", method(1)),          // 1
               sym(F, Undefined, "u"),             // 2
               importSym(0, "iv", "m", "e"),       // 3
               dataSym(BindingLocal | ThreadLocal, "t", field(2), 1, 0, 4)};
  R.Segments = {segment(".data", 0, 3, 16), segment(".tdata", SegTLS, 2, 4),
                segment(".bss", SegZero, 2, 8)};
  R.Relocs = {reloc(RelocType::Addr64, 0, 0, 0),
              reloc(RelocType::FnAddr64, 0, 8, 1)};
  R.Comdats = {comdat("f", ComdatSelection::Any,
                      {{ComdatMemberKind::FunctionSymbol, 1}})};
  R.Inits = {init(InitKind::Init, 100, 1, 1)};
  R.Signatures = {sig(method(1), NoPrototype)};
  R.LinkerOptions = {linkerOption(LinkerOptionKind::DependentLib, {"m"})};
  return R;
}

void expectSameRecord(const Record &A, const Record &B) {
  EXPECT_EQ(A.Hdr.Major, B.Hdr.Major);
  EXPECT_EQ(A.Hdr.Minor, B.Hdr.Minor);
  EXPECT_EQ(A.Hdr.Flags, B.Hdr.Flags);
  EXPECT_EQ(A.Hdr.Triple, B.Hdr.Triple);
  EXPECT_EQ(A.Hdr.ABIVersion, B.Hdr.ABIVersion);
  EXPECT_EQ(A.Hdr.ABIFlags, B.Hdr.ABIFlags);
  EXPECT_EQ(A.Hdr.FeatureFlags, B.Hdr.FeatureFlags);
  EXPECT_EQ(A.Hdr.PointerSize, B.Hdr.PointerSize);
  EXPECT_EQ(A.Hdr.LongSize, B.Hdr.LongSize);
  EXPECT_EQ(A.Hdr.WCharSize, B.Hdr.WCharSize);
  EXPECT_EQ(A.Hdr.LongDoubleSize, B.Hdr.LongDoubleSize);
  ASSERT_EQ(A.Symbols.size(), B.Symbols.size());
  for (size_t I = 0; I < A.Symbols.size(); ++I) {
    const Symbol &X = A.Symbols[I], &Y = B.Symbols[I];
    SCOPED_TRACE("symbol " + std::to_string(I) + " " + X.Name);
    EXPECT_EQ(X.Kind, Y.Kind);
    EXPECT_EQ(X.Flags, Y.Flags);
    EXPECT_EQ(X.Name, Y.Name);
    EXPECT_EQ(X.Token, Y.Token);
    EXPECT_EQ(X.Segment, Y.Segment);
    EXPECT_EQ(X.Offset, Y.Offset);
    EXPECT_EQ(X.Size, Y.Size);
    EXPECT_EQ(X.Aliasee, Y.Aliasee);
    EXPECT_EQ(X.AliasOffset, Y.AliasOffset);
    EXPECT_EQ(X.CommonSize, Y.CommonSize);
    EXPECT_EQ(X.CommonAlignLog2, Y.CommonAlignLog2);
    EXPECT_EQ(X.ImportModule, Y.ImportModule);
    EXPECT_EQ(X.ImportName, Y.ImportName);
  }
  ASSERT_EQ(A.Segments.size(), B.Segments.size());
  for (size_t I = 0; I < A.Segments.size(); ++I) {
    SCOPED_TRACE("segment " + std::to_string(I));
    EXPECT_EQ(A.Segments[I].Name, B.Segments[I].Name);
    EXPECT_EQ(A.Segments[I].Flags, B.Segments[I].Flags);
    EXPECT_EQ(A.Segments[I].AlignLog2, B.Segments[I].AlignLog2);
    EXPECT_EQ(A.Segments[I].Size, B.Segments[I].Size);
    EXPECT_EQ(A.Segments[I].Bytes, B.Segments[I].Bytes);
  }
  ASSERT_EQ(A.Relocs.size(), B.Relocs.size());
  for (size_t I = 0; I < A.Relocs.size(); ++I) {
    SCOPED_TRACE("reloc " + std::to_string(I));
    EXPECT_EQ(A.Relocs[I].Type, B.Relocs[I].Type);
    EXPECT_EQ(A.Relocs[I].Segment, B.Relocs[I].Segment);
    EXPECT_EQ(A.Relocs[I].Offset, B.Relocs[I].Offset);
    EXPECT_EQ(A.Relocs[I].Symbol, B.Relocs[I].Symbol);
    EXPECT_EQ(A.Relocs[I].Addend, B.Relocs[I].Addend);
    EXPECT_EQ(A.Relocs[I].Width, B.Relocs[I].Width);
    EXPECT_EQ(A.Relocs[I].Symbol2, B.Relocs[I].Symbol2);
  }
  ASSERT_EQ(A.Comdats.size(), B.Comdats.size());
  for (size_t I = 0; I < A.Comdats.size(); ++I) {
    SCOPED_TRACE("comdat " + std::to_string(I));
    EXPECT_EQ(A.Comdats[I].Name, B.Comdats[I].Name);
    EXPECT_EQ(A.Comdats[I].Selection, B.Comdats[I].Selection);
    EXPECT_EQ(A.Comdats[I].Members, B.Comdats[I].Members);
  }
  ASSERT_EQ(A.Inits.size(), B.Inits.size());
  for (size_t I = 0; I < A.Inits.size(); ++I) {
    SCOPED_TRACE("init " + std::to_string(I));
    EXPECT_EQ(A.Inits[I].Kind, B.Inits[I].Kind);
    EXPECT_EQ(A.Inits[I].Priority, B.Inits[I].Priority);
    EXPECT_EQ(A.Inits[I].Symbol, B.Inits[I].Symbol);
    EXPECT_EQ(A.Inits[I].ComdatPlusOne, B.Inits[I].ComdatPlusOne);
  }
  ASSERT_EQ(A.Signatures.size(), B.Signatures.size());
  for (size_t I = 0; I < A.Signatures.size(); ++I) {
    const SignatureInfo &X = A.Signatures[I], &Y = B.Signatures[I];
    SCOPED_TRACE("signature " + std::to_string(I));
    EXPECT_EQ(X.Token, Y.Token);
    EXPECT_EQ(X.Flags, Y.Flags);
    EXPECT_EQ(X.FixedParams, Y.FixedParams);
    EXPECT_EQ(X.SretSize, Y.SretSize);
    EXPECT_EQ(X.SretAlignLog2, Y.SretAlignLog2);
    ASSERT_EQ(X.Byval.size(), Y.Byval.size());
    for (size_t J = 0; J < X.Byval.size(); ++J) {
      EXPECT_EQ(X.Byval[J].Param, Y.Byval[J].Param);
      EXPECT_EQ(X.Byval[J].Size, Y.Byval[J].Size);
      EXPECT_EQ(X.Byval[J].AlignLog2, Y.Byval[J].AlignLog2);
    }
  }
  ASSERT_EQ(A.LinkerOptions.size(), B.LinkerOptions.size());
  for (size_t I = 0; I < A.LinkerOptions.size(); ++I) {
    EXPECT_EQ(A.LinkerOptions[I].Kind, B.LinkerOptions[I].Kind);
    EXPECT_EQ(A.LinkerOptions[I].Args, B.LinkerOptions[I].Args);
  }
  EXPECT_EQ(A.SkippedOptional, B.SkippedOptional);
}

Record decodeOK(ArrayRef<uint8_t> Bytes) {
  Expected<Record> R = decodeLinkInfo(Bytes);
  EXPECT_THAT_EXPECTED(R, Succeeded());
  return R ? std::move(*R) : Record();
}

std::string decodeError(ArrayRef<uint8_t> Bytes) {
  Expected<Record> R = decodeLinkInfo(Bytes);
  if (R) {
    ADD_FAILURE() << "decodeLinkInfo accepted the record";
    return "";
  }
  return toString(R.takeError());
}

// 5.7.9's grammar of the decoder's messages, apart from the two version
// messages of 5.8.9: "invalid link-info record: <field> at offset <n>:
// <problem>" on one line, where <n> is the decimal offset in the whole record
// at which the named field starts.
struct RecordMessage {
  std::string Field;
  uint64_t Offset = 0;
  std::string Problem;
};

std::optional<RecordMessage> parseRecordMessage(StringRef Msg) {
  // llvm::Regex is POSIX extended on every host (gtest's MatchesRegex uses
  // its simple dialect on Windows).
  Regex Grammar("^invalid link-info record: (.+) at offset ([0-9]+): (.+)$");
  SmallVector<StringRef, 4> M;
  if (Msg.contains('\n') || !Grammar.match(Msg, &M))
    return std::nullopt;
  RecordMessage R;
  R.Field = M[1].str();
  if (M[2].getAsInteger(10, R.Offset))
    return std::nullopt;
  R.Problem = M[3].str();
  return R;
}

std::optional<RecordMessage> expectGrammar(StringRef Msg) {
  std::optional<RecordMessage> M = parseRecordMessage(Msg);
  if (!M)
    ADD_FAILURE() << "not '" << RecordPrefix
                  << "<field> at offset <n>: <problem>': " << Msg.str();
  return M;
}

// A term of 5.8.8 in a message's problem: the names of flags, kinds and types
// (all capitals: IMPORT, ZERO, FNADDR64) as 5.8.8 spells them, other words in
// any case.
bool namesTerm(StringRef Problem, StringRef Term) {
  if (llvm::none_of(Term, isLower))
    return Problem.contains(Term);
  return Problem.contains_insensitive(Term);
}

// 5.8.9's message for an unknown must-understand kind, flag bit or enum
// value: "invalid link-info record: v1.<minor> uses <feature>, which this
// toolchain does not support", where <feature> names every term of \p Terms.
void expectFeatureError(ArrayRef<uint8_t> Bytes, unsigned Minor = 0,
                        ArrayRef<std::string> Terms = {}) {
  std::string Msg = decodeError(Bytes);
  const std::string Head =
      std::string(RecordPrefix) + "v1." + std::to_string(Minor) + " uses ";
  EXPECT_THAT(Msg, StartsWith(Head));
  EXPECT_TRUE(StringRef(Msg).ends_with(FeatureSuffix)) << Msg;
  StringRef Feature = StringRef(Msg).drop_front(
      StringRef(Msg).starts_with(Head) ? Head.size() : 0);
  Feature.consume_back(FeatureSuffix);
  for (const std::string &T : Terms)
    EXPECT_TRUE(namesTerm(Feature, T))
        << "the feature does not name '" << T << "': " << Msg;
}

// The decoder refused a record with a message of 5.7.9's grammar that gives
// \p Offset and whose problem names every term of \p Terms (the violated
// constraint, so that a refusal for another reason fails).
void expectRecordError(StringRef Msg, uint64_t Offset,
                       ArrayRef<std::string> Terms) {
  std::optional<RecordMessage> M = expectGrammar(Msg);
  if (M)
    EXPECT_EQ(M->Offset, Offset) << Msg.str();
  // Without the grammar, the whole message must still name the constraint.
  StringRef Problem = M ? StringRef(M->Problem) : Msg;
  for (const std::string &T : Terms)
    EXPECT_TRUE(namesTerm(Problem, T))
        << "the problem does not name '" << T << "': " << Msg.str();
}

//===----------------------------------------------------------------------===//
// Primitives (5.8.8 encoding primitives; RecordWriter, 5.8.11)
//===----------------------------------------------------------------------===//

TEST(CLILinkInfoTest, RecordWriterPrimitives) {
  SmallVector<uint8_t, 64> Out;
  RecordWriter W(Out);
  W.writeU8(0xFE);
  W.writeU32(0x11223344);
  W.writeULEB(0);
  W.writeULEB(127);
  W.writeULEB(128);
  W.writeULEB(624485);
  W.writeSLEB(0);
  W.writeSLEB(-1);
  W.writeSLEB(63);
  W.writeSLEB(64);
  W.writeSLEB(-64);
  W.writeSLEB(-65);
  W.writeString("ab");
  W.writeString("");
  const uint8_t Raw[] = {9, 8};
  W.writeBytes(Raw);
  const uint8_t Want[] = {0xFE, 0x44, 0x33, 0x22, 0x11,             // u8, u32
                          0x00, 0x7F, 0x80, 0x01, 0xE5, 0x8E, 0x26, // uleb
                          0x00, 0x7F, 0x3F, 0xC0, 0x00, 0x40, 0xBF,
                          0x7F, 0x02, 'a',  'b',  0x00, // str
                          9,    8};
  EXPECT_EQ(ArrayRef<uint8_t>(Out), ArrayRef<uint8_t>(Want));

  // The widest values: 10 bytes.
  Out.clear();
  W.writeULEB(UINT64_MAX);
  W.writeSLEB(INT64_MIN);
  const uint8_t Wide[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                          0xFF, 0xFF, 0x01, 0x80, 0x80, 0x80, 0x80,
                          0x80, 0x80, 0x80, 0x80, 0x80, 0x7F};
  EXPECT_EQ(ArrayRef<uint8_t>(Out), ArrayRef<uint8_t>(Wide));
}

// A subsection is its u8 kind, the minimal uleb size of its payload and the
// payload.
TEST(CLILinkInfoTest, RecordWriterSubsections) {
  SmallVector<uint8_t, 0> Out;
  RecordWriter W(Out);
  W.beginSubsection(0x02);
  for (unsigned I = 0; I < 130; ++I)
    W.writeU8(I);
  W.endSubsection();
  W.beginSubsection(0x45);
  W.endSubsection();
  ASSERT_EQ(Out.size(), 3u + 130 + 2);
  EXPECT_EQ(Out[0], 0x02);
  EXPECT_EQ(Out[1], 0x82);
  EXPECT_EQ(Out[2], 0x01);
  for (unsigned I = 0; I < 130; ++I)
    EXPECT_EQ(Out[3 + I], I);
  EXPECT_EQ(Out[133], 0x45);
  EXPECT_EQ(Out[134], 0x00);
}

//===----------------------------------------------------------------------===//
// The encoder (5.8.8 field order, minimal LEB, determinism)
//===----------------------------------------------------------------------===//

// Hand-computed bytes (checked with an independent Python encoder in
// scratch): multi-byte uleb sizes and flags, sleb addends -65 and 64, an
// IMPORT symbol with an empty module, priority 65535.
TEST(CLILinkInfoTest, HandComputedRecord) {
  Record R;
  R.Hdr.Triple = "x86_64-unknown-linux-gnu-msil";
  R.Hdr.PointerSize = 8;
  R.Hdr.LongSize = 8;
  R.Hdr.WCharSize = 4;
  R.Hdr.LongDoubleSize = 8;
  R.Symbols = {dataSym(0, "v", field(1), 0, 0, 130),
               importSym(0, "imp", "", "e"),
               sym(SymbolKind::Function, Undefined, "f"),
               sym(SymbolKind::Function, BindingLocal, "g", method(1))};
  ByteVec Data(130, 0);
  Data[0] = 0xAB;
  Data[1] = 0xCD;
  R.Segments = {segment(".data", 0, 3, 130, Data)};
  R.Relocs = {reloc(RelocType::Addr64, 0, 8, 0, -65),
              reloc(RelocType::FnAddr64, 0, 16, 2),
              reloc(RelocType::Diff, 0, 24, 0, 64, 4, 0)};
  R.Inits = {init(InitKind::Init, 65535, 3)};
  R.Signatures = {sig(memberRef(1), NoPrototype, {{0, 130, 3}})};
  R.LinkerOptions = {linkerOption(LinkerOptionKind::DependentLib, {"m"})};

  ByteVec Want = {
      // Header: magic, 1.0, flags, triple, ABI 1, ABIFlags, FeatureFlags,
      // data model.
      0x4D, 0x53, 0x4C, 0x4B, 0x01, 0x00, 0x00, 0x1D};
  StringRef Triple = "x86_64-unknown-linux-gnu-msil";
  Want.insert(Want.end(), Triple.begin(), Triple.end());
  ByteVec Rest = {
      0x01, 0x00, 0x00, 0x08, 0x08, 0x04, 0x08,
      // SYMTAB, 35 bytes.
      0x01, 0x23, 0x04, 0x01, 0x00, 0x01, 0x76, 0x01, 0x00, 0x00, 0x04, 0x00,
      0x00, 0x82, 0x01, 0x01, 0x90, 0x10, 0x03, 0x69, 0x6D, 0x70, 0x00, 0x01,
      0x65, 0x00, 0x10, 0x01, 0x66, 0x00, 0x02, 0x01, 0x67, 0x01, 0x00, 0x00,
      0x06,
      // SEGMENTS, 141 bytes: '.data', flags 0, align 2^3, size 130, bytes.
      0x02, 0x8D, 0x01, 0x01, 0x05, 0x2E, 0x64, 0x61, 0x74, 0x61, 0x00, 0x03,
      0x82, 0x01};
  Want.insert(Want.end(), Rest.begin(), Rest.end());
  Want.insert(Want.end(), Data.begin(), Data.end());
  ByteVec Tail = {// DATA_RELOCS, 20 bytes.
                  0x03, 0x14, 0x03, 0x00, 0x00, 0x08, 0x00, 0xBF, 0x7F, 0x02,
                  0x00, 0x10, 0x02, 0x00, 0x04, 0x00, 0x18, 0x00, 0xC0, 0x00,
                  0x04, 0x00,
                  // INIT_FINI, 7 bytes.
                  0x05, 0x07, 0x01, 0x00, 0xFF, 0xFF, 0x03, 0x03, 0x00,
                  // SIGNATURES, 11 bytes.
                  0x06, 0x0B, 0x01, 0x01, 0x00, 0x00, 0x0A, 0x01, 0x01, 0x00,
                  0x82, 0x01, 0x03,
                  // LINKER_OPTIONS, 4 bytes.
                  0x07, 0x04, 0x01, 0x00, 0x01, 0x6D};
  Want.insert(Want.end(), Tail.begin(), Tail.end());
  ASSERT_EQ(Want.size(), 275u);

  EXPECT_EQ(encodeLib(R), Want);
  EXPECT_EQ(encodeSpec(R).Out, Want);
  expectSameRecord(decodeOK(Want), R);
}

TEST(CLILinkInfoTest, EncoderMatchesSpecification) {
  Record R = fullRecord();
  EXPECT_EQ(encodeLib(R), encodeSpec(R).Out);
  Record B = baseRecord();
  EXPECT_EQ(encodeLib(B), encodeSpec(B).Out);
}

TEST(CLILinkInfoTest, EveryEntryKindAndFlagRoundTrips) {
  Record R = fullRecord();
  ByteVec Bytes = encodeLib(R);
  Record D = decodeOK(Bytes);
  expectSameRecord(D, R);
  EXPECT_EQ(encodeLib(D), Bytes);
  // Headers with HAS_DEBUG_INFO and other data models too.
  R.Hdr.Flags = HasDebugInfo;
  R.Hdr.Triple = "aarch64-unknown-linux-gnu-msil";
  R.Hdr.LongSize = 8;
  R.Hdr.WCharSize = 4;
  expectSameRecord(decodeOK(encodeLib(R)), R);
}

TEST(CLILinkInfoTest, EqualRecordsGiveEqualBytes) {
  ByteVec A = encodeLib(fullRecord());
  ByteVec B = encodeLib(fullRecord());
  EXPECT_EQ(A, B);
  Record R = fullRecord();
  EXPECT_EQ(encodeLib(R), encodeLib(R));
}

// Empty subsections are not written: a header-only record is 4 + 2 + ... bytes.
TEST(CLILinkInfoTest, EmptySubsectionsAreOmitted) {
  Record R;
  R.Hdr.Triple = "x86_64-pc-windows-msvc-msil";
  SpecEncoder E = encodeSpec(R);
  ASSERT_EQ(E.Boundaries.size(), 1u);
  EXPECT_EQ(encodeLib(R), E.Out);
  expectSameRecord(decodeOK(E.Out), R);
}

// IMPORT data symbols (5.8.8, 5.10.12): ImportModule then ImportName follow
// the other fields, with and without a module.
TEST(CLILinkInfoTest, ImportDataSymbols) {
  Record R;
  R.Hdr.Triple = "x86_64-unknown-linux-gnu-msil";
  R.Symbols = {importSym(0, "my_counter", "", "native_counter"),
               importSym(BindingWeak, "v", "libfoo", "foo_v"),
               importSym(DLLImport | vis(Visibility::Hidden), "w", "k", "w")};
  ByteVec Bytes = encodeLib(R);
  EXPECT_EQ(Bytes, encodeSpec(R).Out);
  Record D = decodeOK(Bytes);
  expectSameRecord(D, R);
  // Symbol 0's bytes: DATA, flags 0x810, name, "" and "native_counter".
  const uint8_t First[] = {0x01, 0x90, 0x10, 0x0A, 'm', 'y', '_',  'c',
                           'o',  'u',  'n',  't',  'e', 'r', 0x00, 0x0E};
  Record HeaderOnly;
  HeaderOnly.Hdr = R.Hdr;
  SpecEncoder H = encodeSpec(HeaderOnly);
  ArrayRef<uint8_t> Sym0 = ArrayRef<uint8_t>(Bytes).drop_front(
      H.Out.size() + 3); // kind, one-byte size, count
  EXPECT_EQ(Sym0.take_front(sizeof(First)), ArrayRef<uint8_t>(First));
}

//===----------------------------------------------------------------------===//
// Versions (5.8.9)
//===----------------------------------------------------------------------===//

TEST(CLILinkInfoTest, OtherMajorVersionsAreRefused) {
  ByteVec B = encodeLib(baseRecord());
  B[4] = 2;
  EXPECT_EQ(decodeError(B), "invalid link-info record: version 2 is newer "
                            "than this toolchain supports (1)");
  // Major 0 is another major too, but not a newer one: 5.7.9's grammar, at
  // the Major field.
  B[4] = 0;
  expectRecordError(decodeError(B),
                    encodeSpec(baseRecord()).at("major version"),
                    {"version 0"});
}

TEST(CLILinkInfoTest, LaterMinorVersionsAreRead) {
  Record R = baseRecord();
  R.Hdr.Minor = 3;
  ByteVec B = encodeSpec(R).Out;
  Record D = decodeOK(B);
  EXPECT_EQ(D.Hdr.Minor, 3);
  EXPECT_EQ(D.Symbols.size(), R.Symbols.size());
}

// An unknown must-understand kind (below 0x40) is refused with the version
// rule's message; kinds 0x40-0x7F are kept.
TEST(CLILinkInfoTest, UnknownSubsectionKinds) {
  Record R = baseRecord();
  ByteVec B = encodeSpec(R).Out;
  ByteVec WithA = B;
  WithA.insert(WithA.end(), {0x0A, 0x00});
  expectFeatureError(WithA, 0, {"subsection kind 0x0A"});
  R.Hdr.Minor = 3;
  ByteVec B3 = encodeSpec(R).Out;
  B3.insert(B3.end(), {0x0A, 0x01, 0x00});
  expectFeatureError(B3, 3, {"subsection kind 0x0A"});

  ByteVec WithOpt = B;
  WithOpt.insert(WithOpt.end(), {0x40, 0x00, 0x45, 0x02, 0x09, 0x09, 0x7F, 0x03,
                                 0xFF, 0xFF, 0xFF});
  Record D = decodeOK(WithOpt);
  std::vector<std::pair<uint8_t, std::vector<uint8_t>>> Kept = {
      {0x40, {}}, {0x45, {9, 9}}, {0x7F, {0xFF, 0xFF, 0xFF}}};
  EXPECT_EQ(D.SkippedOptional, Kept);
  EXPECT_EQ(D.Symbols.size(), R.Symbols.size());
  // They are kept for dumping, and written back in kind order.
  EXPECT_EQ(encodeLib(D), WithOpt);
}

TEST(CLILinkInfoTest, UnknownFlagsAndEnumValues) {
  struct Case {
    const char *What;
    std::function<void(Record &)> Change;
    // The field and its value, in 5.8.8's names.
    std::vector<std::string> Terms;
  } Cases[] = {
      {"header flag 0x2",
       [](Record &R) { R.Hdr.Flags = 0x2; },
       {"header flag", "0x2"}},
      {"symbol flag 0x1000",
       [](Record &R) { R.Symbols[1].Flags |= 0x1000; },
       {"symbol flag", "0x1000"}},
      {"symbol kind 2",
       [](Record &R) { R.Symbols[2].Kind = static_cast<SymbolKind>(2); },
       {"symbol kind 2"}},
      {"segment flag 0x20",
       [](Record &R) { R.Segments[0].Flags |= 0x20; },
       {"segment flag", "0x20"}},
      {"relocation type 7",
       [](Record &R) { R.Relocs[1].Type = static_cast<RelocType>(7); },
       {"relocation type 7"}},
      {"comdat selection 5",
       [](Record &R) {
         R.Comdats[0].Selection = static_cast<ComdatSelection>(5);
       },
       {"selection 5"}},
      {"comdat member kind 2",
       [](Record &R) {
         R.Comdats[0].Members[0].first = static_cast<ComdatMemberKind>(2);
       },
       {"member kind 2"}},
      {"init kind 2",
       [](Record &R) { R.Inits[0].Kind = static_cast<InitKind>(2); },
       {"init kind 2"}},
      {"call flag 0x10",
       [](Record &R) { R.Signatures[0].Flags |= 0x10; },
       {"call flag", "0x10"}},
      {"linker option kind 2",
       [](Record &R) {
         R.LinkerOptions[0].Kind = static_cast<LinkerOptionKind>(2);
       },
       {"linker option kind 2"}},
  };
  for (const Case &C : Cases) {
    SCOPED_TRACE(C.What);
    Record R = baseRecord();
    C.Change(R);
    expectFeatureError(encodeSpec(R).Out, 0, C.Terms);
  }
}

// In baseRecord's header (magic, major, minor, flags 0, the 29-byte triple's
// str, ABIVersion 1), ABIFlags start at offset 38 and FeatureFlags follow.
constexpr size_t BaseABIFlagsOffset = 4 + 1 + 1 + 1 + (1 + 29) + 1;

std::string headerLine(const Record &R) {
  std::string S;
  raw_string_ostream OS(S);
  printLinkInfo(R, nullptr, OS);
  OS.flush();
  return StringRef(S).split('\n').first.str();
}

// FeatureFlags are not must-understand (5.8.8, 5.8.9 as clarified by section
// 12's row of 2026-10-23): codegen switches that do not change the ABI, which
// a later minor version may add. Every value is decoded and kept, written back
// unchanged, and printed as ` features=0x<X>` on the header line.
TEST(CLILinkInfoTest, FeatureFlagsAreKept) {
  struct {
    uint64_t Value;
    std::vector<uint8_t> Uleb; // the minimal uleb, by hand
    const char *Text;
  } Cases[] = {
      {0x5, {0x05}, "features=0x5"},
      {0x80, {0x80, 0x01}, "features=0x80"},
      {0x123456789, {0x89, 0xCF, 0x95, 0x9A, 0x12}, "features=0x123456789"},
      {0x8000000000000001,
       {0x81, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01},
       "features=0x8000000000000001"},
  };
  ASSERT_EQ(baseRecord().Hdr.Triple.size(), 29u);
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.Text);
    Record R = baseRecord();
    R.Hdr.FeatureFlags = C.Value;
    ByteVec B = encodeSpec(R).Out;
    // ABIFlags 0, the value, then the data model 8,8,4,8.
    ByteVec Field = {0x00};
    Field.insert(Field.end(), C.Uleb.begin(), C.Uleb.end());
    Field.insert(Field.end(), {8, 8, 4, 8});
    ASSERT_EQ(ArrayRef<uint8_t>(B).slice(BaseABIFlagsOffset, Field.size()),
              ArrayRef<uint8_t>(Field));
    EXPECT_EQ(encodeLib(R), B);
    Expected<Record> D = decodeLinkInfo(B);
    if (!D) {
      ADD_FAILURE() << "decodeLinkInfo refused the record: "
                    << toString(D.takeError());
      continue;
    }
    EXPECT_EQ(D->Hdr.FeatureFlags, C.Value);
    expectSameRecord(*D, R);
    EXPECT_EQ(encodeLib(*D), B);
    EXPECT_EQ(headerLine(*D),
              std::string("LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil "
                          "abi=1 abiflags=0x0 ") +
                  C.Text + " datamodel=8,8,4,8");
  }
  // A bit a later minor version adds.
  Record R = baseRecord();
  R.Hdr.Minor = 3;
  R.Hdr.FeatureFlags = 0x5;
  Expected<Record> D = decodeLinkInfo(encodeSpec(R).Out);
  ASSERT_THAT_EXPECTED(D, Succeeded());
  EXPECT_EQ(D->Hdr.Minor, 3);
  EXPECT_EQ(D->Hdr.FeatureFlags, 0x5u);
  EXPECT_EQ(D->Symbols.size(), R.Symbols.size());
  EXPECT_EQ(headerLine(*D),
            "LinkInfo v1.3 triple=x86_64-unknown-linux-gnu-msil "
            "abi=1 abiflags=0x0 features=0x5 datamodel=8,8,4,8");
}

// ABIFlags stay must-understand: Phase 2 defines no bit, so every bit is a
// feature this toolchain does not support (5.8.9 as clarified by section 12's
// row of 2026-10-23), named with the field's offset.
TEST(CLILinkInfoTest, ABIFlagsAreRefused) {
  struct {
    uint8_t Minor;
    uint64_t ABIFlags, FeatureFlags;
    const char *Text;
  } Cases[] = {
      {0, 0x1, 0, "v1.0 uses ABI flags 0x1 (at offset 38)"},
      {0, 0x100, 0, "v1.0 uses ABI flags 0x100 (at offset 38)"},
      {3, 0x1, 0, "v1.3 uses ABI flags 0x1 (at offset 38)"},
      {0, 0x1, 0x5, "v1.0 uses ABI flags 0x1 (at offset 38)"},
  };
  static_assert(BaseABIFlagsOffset == 38);
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.Text);
    Record R = baseRecord();
    R.Hdr.Minor = C.Minor;
    R.Hdr.ABIFlags = C.ABIFlags;
    R.Hdr.FeatureFlags = C.FeatureFlags;
    ByteVec B = encodeSpec(R).Out;
    ASSERT_EQ(B[BaseABIFlagsOffset - 1], 0x01); // ABIVersion
    EXPECT_EQ(decodeError(B),
              std::string(RecordPrefix) + C.Text + FeatureSuffix);
  }
}

//===----------------------------------------------------------------------===//
// C8 (5.8.8 constraints)
//===----------------------------------------------------------------------===//

struct C8Case {
  const char *What;
  std::function<void(Record &)> Change;
  // The field whose offset the message gives (a mark of SpecEncoder in the
  // changed record): the offending entry, or the field of it that is wrong.
  const char *Field;
  // The terms of 5.8.8 that name the violated constraint (namesTerm).
  std::vector<std::string> Terms;
};

void expectC8Errors(ArrayRef<C8Case> Cases) {
  ASSERT_THAT_EXPECTED(decodeLinkInfo(encodeSpec(baseRecord()).Out),
                       Succeeded());
  for (const C8Case &C : Cases) {
    SCOPED_TRACE(C.What);
    Record R = baseRecord();
    C.Change(R);
    SpecEncoder E = encodeSpec(R);
    expectRecordError(decodeError(E.Out), E.at(C.Field), C.Terms);
  }
}

TEST(CLILinkInfoTest, C8SymbolFlagCombinations) {
  const SymbolKind F = SymbolKind::Function, D = SymbolKind::Data;
  C8Case Cases[] = {
      {"COMMON on a FUNCTION",
       [](Record &R) {
         R.Symbols[1].Flags = BindingCommon;
         R.Symbols[1].CommonSize = 4;
       },
       "symbol 1",
       {"COMMON", "FUNCTION"}},
      {"COMMON with UNDEFINED",
       [&](Record &R) {
         R.Symbols[2] = sym(D, BindingCommon | Undefined, "u");
       },
       "symbol 2",
       {"COMMON", "UNDEFINED"}},
      {"COMMON with ALIAS",
       [&](Record &R) {
         Symbol S = sym(D, BindingCommon | Alias, "ca");
         R.Symbols.push_back(S);
       },
       "symbol 5",
       {"COMMON", "ALIAS"}},
      {"LOCAL with UNDEFINED",
       [](Record &R) { R.Symbols[2].Flags = BindingLocal | Undefined; },
       "symbol 2",
       {"LOCAL", "UNDEFINED"}},
      {"EXPLICIT_IMPORT on DATA",
       [&](Record &R) {
         R.Symbols[3] = sym(D, Undefined | ExplicitImport, "iv");
       },
       "symbol 3",
       {"EXPLICIT_IMPORT", "FUNCTION"}},
      {"EXPLICIT_IMPORT on a defined FUNCTION",
       [](Record &R) { R.Symbols[1].Flags |= ExplicitImport; },
       "symbol 1",
       {"EXPLICIT_IMPORT", "undefined"}},
      // The four IMPORT violations.
      {"IMPORT on a FUNCTION",
       [&](Record &R) {
         Symbol S = sym(F, Undefined | Import, "u");
         S.ImportName = "e";
         R.Symbols[2] = S;
       },
       "symbol 2",
       {"IMPORT", "DATA"}},
      {"IMPORT without UNDEFINED",
       [](Record &R) {
         R.Symbols[0].Flags |= Import;
         R.Symbols[0].ImportName = "e";
       },
       "symbol 0",
       {"IMPORT", "undefined"}},
      {"IMPORT with TLS",
       [](Record &R) { R.Symbols[3].Flags |= ThreadLocal; },
       "symbol 3",
       {"IMPORT", "TLS"}},
      {"IMPORT with an empty ImportName",
       [](Record &R) { R.Symbols[3].ImportName.clear(); },
       "symbol 3 import name",
       {"IMPORT", "empty"}},
      {"DLLIMPORT on a definition",
       [](Record &R) { R.Symbols[1].Flags |= DLLImport; },
       "symbol 1",
       {"DLLIMPORT"}},
      {"NO_STRIP with UNDEFINED",
       [](Record &R) { R.Symbols[2].Flags |= NoStrip; },
       "symbol 2",
       {"NO_STRIP", "UNDEFINED"}},
      {"TLS on a FUNCTION",
       [](Record &R) { R.Symbols[1].Flags |= ThreadLocal; },
       "symbol 1",
       {"TLS", "FUNCTION"}},
  };
  expectC8Errors(Cases);
}

TEST(CLILinkInfoTest, C8SymbolNames) {
  C8Case Cases[] = {
      {"duplicate name",
       [](Record &R) { R.Symbols[2].Name = "f"; },
       "symbol 2",
       {"name", "symbol 1"}},
      {"empty name",
       [](Record &R) { R.Symbols[2].Name.clear(); },
       "symbol 2",
       {"name", "empty"}},
      {"<msil> prefix",
       [](Record &R) { R.Symbols[2].Name = "<msil>u"; },
       "symbol 2",
       {"<msil>"}},
      {"NUL in a name",
       [](Record &R) { R.Symbols[2].Name = std::string("a\0b", 3); },
       "symbol 2 name",
       {"NUL"}},
      {"invalid UTF-8",
       [](Record &R) { R.Symbols[2].Name = "\xFF\xFE"; },
       "symbol 2 name",
       {"UTF-8"}},
      {"invalid UTF-8 in an import name",
       [](Record &R) { R.Symbols[3].ImportName = "\xC3"; },
       "symbol 3 import name",
       {"UTF-8"}},
  };
  expectC8Errors(Cases);
}

TEST(CLILinkInfoTest, C8IndexesAndSegments) {
  const SymbolKind F = SymbolKind::Function;
  C8Case Cases[] = {
      {"symbol segment out of range",
       [](Record &R) { R.Symbols[0].Segment = 9; },
       "symbol 0",
       {"segment 9", "range"}},
      {"symbol past its segment's end",
       [](Record &R) { R.Symbols[0].Offset = 12; },
       "symbol 0",
       {"segment 0", "outside"}},
      {"aliasee out of range",
       [&](Record &R) {
         Symbol S = sym(F, Alias, "al");
         S.Aliasee = 99;
         R.Symbols.push_back(S);
       },
       "symbol 5",
       {"aliasee 99", "range"}},
      {"alias of an undefined symbol",
       [&](Record &R) {
         Symbol S = sym(F, Alias, "al");
         S.Aliasee = 2;
         R.Symbols.push_back(S);
       },
       "symbol 5",
       {"aliasee 2", "undefined"}},
      {"alias of a symbol of the other kind",
       [&](Record &R) {
         Symbol S = sym(F, Alias, "al");
         S.Aliasee = 0;
         R.Symbols.push_back(S);
       },
       "symbol 5",
       {"aliasee 0", "kind"}},
      {"alias of an alias",
       [&](Record &R) {
         Symbol A = sym(F, Alias, "al");
         A.Aliasee = 1;
         Symbol B = sym(F, Alias, "al2");
         B.Aliasee = 5;
         R.Symbols.push_back(A);
         R.Symbols.push_back(B);
       },
       "symbol 6",
       {"aliasee 5", "an alias"}},
      {"segment alignment 2^31",
       [](Record &R) { R.Segments[1].AlignLog2 = 31; },
       "segment 1 alignment",
       {"align", "30"}},
      {"segment of 2^31 bytes",
       [](Record &R) { R.Segments[2].Size = uint64_t(1) << 31; },
       "segment 2 size",
       {"size", "2^31"}},
  };
  expectC8Errors(Cases);
}

TEST(CLILinkInfoTest, C8Relocations) {
  C8Case Cases[] = {
      {"not sorted",
       [](Record &R) { std::swap(R.Relocs[0], R.Relocs[1]); },
       "relocation 1",
       {"sorted"}},
      {"overlapping",
       [](Record &R) { R.Relocs[1] = reloc(RelocType::Addr32, 0, 4, 0); },
       "relocation 1",
       {"overlap"}},
      {"in a ZERO segment",
       [](Record &R) { R.Relocs.push_back(reloc(RelocType::Addr32, 2, 0, 0)); },
       "relocation 2",
       {"ZERO"}},
      {"past the segment's end",
       [](Record &R) { R.Relocs[1] = reloc(RelocType::FnAddr64, 0, 12, 1); },
       "relocation 1",
       {"segment 0", "outside"}},
      {"segment out of range",
       [](Record &R) { R.Relocs.push_back(reloc(RelocType::Addr32, 9, 0, 0)); },
       "relocation 2",
       {"segment 9", "range"}},
      {"symbol out of range",
       [](Record &R) { R.Relocs[0].Symbol = 99; },
       "relocation 0",
       {"symbol 99", "range"}},
      {"ADDR names a FUNCTION",
       [](Record &R) { R.Relocs[0].Symbol = 1; },
       "relocation 0",
       {"ADDR64", "FUNCTION"}},
      {"FNADDR names DATA",
       [](Record &R) { R.Relocs[1].Symbol = 0; },
       "relocation 1",
       {"FNADDR64", "DATA"}},
      {"FNADDR with an addend",
       [](Record &R) { R.Relocs[1].Addend = 8; },
       "relocation 1",
       {"FNADDR64", "addend"}},
      {"TLS_OFFSET names a non-TLS symbol",
       [](Record &R) { R.Relocs[1] = reloc(RelocType::TLSOffset32, 0, 8, 0); },
       "relocation 1",
       {"TLS_OFFSET32", "not TLS"}},
      {"DIFF of width 0",
       [](Record &R) {
         R.Relocs[1] = reloc(RelocType::Diff, 0, 8, 0, 0, 0, 0);
       },
       "relocation 1",
       {"DIFF", "width 0"}},
      {"DIFF of width 17",
       [](Record &R) {
         R.Relocs[1] = reloc(RelocType::Diff, 0, 8, 0, 0, 17, 0);
       },
       "relocation 1",
       {"DIFF", "width 17"}},
      {"DIFF with a FUNCTION subtrahend",
       [](Record &R) {
         R.Relocs[1] = reloc(RelocType::Diff, 0, 8, 0, 0, 4, 1);
       },
       "relocation 1",
       {"DIFF", "FUNCTION"}},
      {"DIFF subtrahend out of range",
       [](Record &R) {
         R.Relocs[1] = reloc(RelocType::Diff, 0, 8, 0, 0, 4, 99);
       },
       "relocation 1",
       {"symbol2 99", "range"}},
  };
  expectC8Errors(Cases);
}

TEST(CLILinkInfoTest, C8ComdatsInitsAndSignatures) {
  C8Case Cases[] = {
      {"comdat function member out of range",
       [](Record &R) { R.Comdats[0].Members[0].second = 99; },
       "comdat 0",
       {"symbol 99", "range"}},
      {"comdat segment member out of range",
       [](Record &R) {
         R.Comdats[0].Members.push_back({ComdatMemberKind::Segment, 9});
       },
       "comdat 0",
       {"segment 9", "range"}},
      {"INIT of a DATA symbol",
       [](Record &R) { R.Inits[0].Symbol = 0; },
       "init 0",
       {"symbol 0", "defined FUNCTION"}},
      {"INIT of an undefined function",
       [](Record &R) { R.Inits[0].Symbol = 2; },
       "init 0",
       {"symbol 2", "defined FUNCTION"}},
      {"INIT symbol out of range",
       [](Record &R) { R.Inits[0].Symbol = 99; },
       "init 0",
       {"symbol 99", "range"}},
      {"INIT priority 65536",
       [](Record &R) { R.Inits[0].Priority = 65536; },
       "init 0 priority",
       {"priority", "65535"}},
      {"INIT comdat out of range",
       [](Record &R) { R.Inits[0].ComdatPlusOne = 2; },
       "init 0",
       {"comdat", "range"}},
      {"two SIGNATURES entries for one token",
       [](Record &R) { R.Signatures.push_back(sig(R.Signatures[0].Token, 0)); },
       "signature 1",
       {"0x06000001", "entry 0"}},
  };
  expectC8Errors(Cases);
}

//===----------------------------------------------------------------------===//
// Framing (5.8.8 subsections)
//===----------------------------------------------------------------------===//

// Each refusal has 5.7.9's grammar, with the offset of the subsection header
// or field that the hand-built record breaks.
TEST(CLILinkInfoTest, Framing) {
  Record R = baseRecord();
  SpecEncoder E = encodeSpec(R);
  const ByteVec &B = E.Out;
  const size_t Header = E.Boundaries[0];
  ASSERT_EQ(E.at("subsection 0x01"), Header);
  ASSERT_EQ(E.at("subsection 0x02"), E.Boundaries[1]);
  {
    SCOPED_TRACE("bad magic");
    ByteVec X = B;
    X[3] = 'X';
    expectRecordError(decodeError(X), E.at("magic"), {"MSLK"});
  }
  {
    SCOPED_TRACE("SEGMENTS before SYMTAB");
    ByteVec X(B.begin(), B.begin() + Header);
    X.insert(X.end(), B.begin() + E.Boundaries[1], B.begin() + E.Boundaries[2]);
    X.insert(X.end(), B.begin() + Header, B.begin() + E.Boundaries[1]);
    // SYMTAB's header now follows SEGMENTS.
    expectRecordError(decodeError(X),
                      Header + (E.Boundaries[2] - E.Boundaries[1]),
                      {"increas"});
  }
  {
    SCOPED_TRACE("SYMTAB twice");
    ByteVec X(B.begin(), B.begin() + E.Boundaries[1]);
    X.insert(X.end(), B.begin() + Header, B.begin() + E.Boundaries[1]);
    expectRecordError(decodeError(X), E.Boundaries[1], {"increas"});
  }
  {
    SCOPED_TRACE("an optional kind before a required one");
    ByteVec X(B.begin(), B.begin() + Header);
    X.insert(X.end(), {0x45, 0x00});
    X.insert(X.end(), B.begin() + Header, B.end());
    // SYMTAB's header follows the two bytes of the empty 0x45 subsection.
    expectRecordError(decodeError(X), Header + 2, {"increas"});
  }
  {
    SCOPED_TRACE("payload size past the end");
    ByteVec X(B.begin(), B.begin() + E.Boundaries.back());
    X.insert(X.end(), {0x45, 0x05, 0x01});
    // The 5-byte payload starts after the kind and the one-byte size; one
    // byte of it is there.
    expectRecordError(decodeError(X), E.Boundaries.back() + 2, {"end"});
  }
  {
    SCOPED_TRACE("payload not consumed exactly");
    // SYMTAB's size one larger, with one extra byte at its end.
    ByteVec Sym = specSymbols(R);
    ByteVec X(B.begin(), B.begin() + Header);
    X.push_back(0x01);
    ASSERT_LT(Sym.size() + 1, 0x80u);
    X.push_back(Sym.size() + 1);
    X.insert(X.end(), Sym.begin(), Sym.end());
    X.push_back(0x00);
    X.insert(X.end(), B.begin() + E.Boundaries[1], B.end());
    // The offset of the byte left over.
    expectRecordError(decodeError(X), Header + 2 + Sym.size(), {"left over"});
  }
  {
    SCOPED_TRACE("payload shorter than its content");
    ByteVec X = B;
    ASSERT_EQ(X[Header], 0x01);
    --X[Header + 1];
    // The last field of the SYMTAB payload, symbol 4's one-byte Size, is now
    // outside the payload.
    ASSERT_EQ(E.at("symbol 4 size"), E.Boundaries[1] - 1);
    expectRecordError(decodeError(X), E.at("symbol 4 size"), {"end"});
  }
  {
    SCOPED_TRACE("a stray byte after the last subsection");
    ByteVec X = B;
    X.push_back(0x07);
    // The stray byte is a subsection kind, and the record ends where its
    // size would start.
    expectRecordError(decodeError(X), B.size() + 1, {"end"});
  }
}

// Readers take a uleb of up to 10 bytes (writers emit the minimal form) and
// refuse a longer one.
TEST(CLILinkInfoTest, LongLEBs) {
  Record R;
  R.Hdr.Triple = "t";
  SpecEncoder E = encodeSpec(R);
  ByteVec B = E.Out;
  ASSERT_EQ(E.at("header flags"), 6u);
  ASSERT_EQ(B[6], 0x00); // HeaderFlags
  ByteVec Ten = B;
  Ten[6] = 0x80;
  Ten.insert(Ten.begin() + 7,
             {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00});
  Record D = decodeOK(Ten);
  EXPECT_EQ(D.Hdr.Flags, 0u);
  EXPECT_EQ(D.Hdr.Triple, "t");
  ByteVec Eleven = Ten;
  Eleven[15] = 0x80;
  Eleven.insert(Eleven.begin() + 16, 0x00);
  // The 11-byte uleb starts at HeaderFlags.
  expectRecordError(decodeError(Eleven), E.at("header flags"), {"10 bytes"});
}

// Every prefix of a valid record that does not end at a subsection boundary
// is refused, with 5.7.9's grammar: the field the cut breaks starts at or
// after the last boundary before the cut and at or before the cut.
TEST(CLILinkInfoTest, Truncation) {
  SpecEncoder E = encodeSpec(fullRecord());
  ASSERT_EQ(E.Out, encodeLib(fullRecord()));
  for (size_t N = 0; N < E.Out.size(); ++N) {
    if (is_contained(E.Boundaries, N))
      continue;
    Expected<Record> R = decodeLinkInfo(ArrayRef<uint8_t>(E.Out).take_front(N));
    if (R) {
      ADD_FAILURE() << "a record cut to " << N << " bytes decoded";
      continue;
    }
    std::string Msg = toString(R.takeError());
    SCOPED_TRACE("cut to " + std::to_string(N) + " bytes");
    std::optional<RecordMessage> M = expectGrammar(Msg);
    if (!M)
      continue;
    size_t Last = 0;
    for (size_t Boundary : E.Boundaries)
      if (Boundary < N)
        Last = std::max(Last, Boundary);
    EXPECT_GE(M->Offset, Last) << Msg;
    EXPECT_LE(M->Offset, N) << Msg;
    EXPECT_TRUE(namesTerm(M->Problem, "end")) << Msg;
  }
  // Cut at a boundary, the record simply has fewer subsections.
  Record Head = decodeOK(ArrayRef<uint8_t>(E.Out).take_front(E.Boundaries[0]));
  EXPECT_TRUE(Head.Symbols.empty());
  Record Two = decodeOK(ArrayRef<uint8_t>(E.Out).take_front(E.Boundaries[2]));
  EXPECT_EQ(Two.Symbols.size(), fullRecord().Symbols.size());
  EXPECT_EQ(Two.Segments.size(), fullRecord().Segments.size());
  EXPECT_TRUE(Two.Relocs.empty());
}

//===----------------------------------------------------------------------===//
// The carrier (5.8.7) through MetadataBuilder, writePEImage and the reader
//===----------------------------------------------------------------------===//

EntityHandle rowHandle(TableId T, uint32_t Row) {
  EntityHandle H;
  H.Value = makeToken(T, Row);
  return H;
}

uint32_t blob(MetadataBuilder &MB, std::initializer_list<uint8_t> B) {
  return MB.addBlob(ArrayRef<uint8_t>(B.begin(), B.end()));
}

// A CIL object: <Module>::gv (native int) and <Module>::f(), whose tokens
// are known before finalize() because every member is on <Module> (5.8.2
// O6), and the record in a Module-row attribute of [msil.link]'<msil>LinkInfo'
// with constructor signature 20 01 01 1D 05.
struct CarrierObject {
  Record Rec;
  ByteVec File;
  std::unique_ptr<MetadataReader> R;
};

enum class CarrierShape { One, None, Two, OtherScope };

std::unique_ptr<CarrierObject> buildCarrierObject(CarrierShape Shape) {
  auto O = std::make_unique<CarrierObject>();
  Record &Rec = O->Rec;
  Rec.Hdr.Triple = "x86_64-pc-windows-msvc-msil";
  Rec.Hdr.LongSize = 4;
  Rec.Hdr.WCharSize = 2;
  Rec.Symbols = {dataSym(0, "gv", field(1), 0, 0, 8),
                 sym(SymbolKind::Function, 0, "f", method(1)),
                 importSym(0, "counter", "", "native_counter"),
                 importSym(BindingWeak, "v2", "libv", "v2_real")};
  Rec.Segments = {segment(".data", 0, 3, 8)};

  MetadataBuilder MB;
  MB.setModuleName("a.c");
  AssemblyRefInfo Link;
  Link.Name = Shape == CarrierShape::OtherScope ? "msil.other" : "msil.link";
  EntityHandle LinkType =
      MB.addTypeRef(MB.addAssemblyRef(Link), "", MSILLink::LinkInfoType);
  EntityHandle Ctor = MB.addMemberRef(LinkType, ".ctor",
                                      blob(MB, {0x20, 0x01, 0x01, 0x1D, 0x05}));
  EntityHandle GV = MB.addField(
      MB.getModuleType(), FieldAttributes::Public | FieldAttributes::Static,
      "gv", blob(MB, {0x06, 0x18}));
  EntityHandle F = MB.addMethod(
      MB.getModuleType(), MethodAttributes::Public | MethodAttributes::Static,
      MethodImplAttributes::IL, "f", blob(MB, {0x00, 0x00, 0x08}));
  SmallVector<uint8_t, 0> Payload, Value;
  encodeLinkInfo(Rec, Payload);
  encodeByteArrayAttribute(Payload, Value);
  if (Shape != CarrierShape::None)
    MB.addCustomAttribute(rowHandle(TableId::Module, 1), Ctor,
                          MB.addBlob(Value));
  if (Shape == CarrierShape::Two)
    MB.addCustomAttribute(rowHandle(TableId::Module, 1), Ctor,
                          MB.addBlob(Value));
  MethodBodyStream Bodies;
  const uint8_t Code[] = {0x17, 0x2A};
  MethodBodyInfo B;
  B.Code = Code;
  MB.setMethodBodyOffset(F, Bodies.addBody(B));
  EXPECT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  EXPECT_EQ(MB.getToken(GV), field(1));
  EXPECT_EQ(MB.getToken(F), method(1));

  Expected<PEOptions> Opts = PEOptions::get(
      Triple("x86_64-pc-windows-msvc-msil"), PEImageKind::Module);
  EXPECT_THAT_EXPECTED(Opts, Succeeded());
  if (!Opts)
    return O;
  std::vector<CoffSymbol> View = buildCOFFSymbolView(Rec);
  PEContent C;
  C.Metadata = &MB;
  C.ILStream = Bodies.getBytes();
  C.ExtraSections = getCOFFViewMarkerSections();
  C.CoffSymbols = View;
  SmallVector<uint8_t, 0> Out;
  EXPECT_THAT_ERROR(writePEImage(*Opts, C, Out), Succeeded());
  O->File.assign(Out.begin(), Out.end());
  Expected<std::unique_ptr<MetadataReader>> R = MetadataReader::create(O->File);
  EXPECT_THAT_EXPECTED(R, Succeeded());
  if (R)
    O->R = std::move(*R);
  return O;
}

TEST(CLILinkInfoTest, CarrierThroughBuilderPEAndReader) {
  std::unique_ptr<CarrierObject> O = buildCarrierObject(CarrierShape::One);
  ASSERT_TRUE(O->R);
  const MetadataReader &R = *O->R;
  EXPECT_FALSE(R.isAssembly());
  Expected<ArrayRef<uint8_t>> Bytes = findLinkInfo(R);
  ASSERT_THAT_EXPECTED(Bytes, Succeeded());
  EXPECT_EQ(ByteVec(Bytes->begin(), Bytes->end()), encodeLib(O->Rec));
  Expected<Record> D = decodeLinkInfo(*Bytes);
  ASSERT_THAT_EXPECTED(D, Succeeded());
  expectSameRecord(*D, O->Rec);
  EXPECT_THAT_ERROR(validateLinkInfo(*D, R), Succeeded());

  // The attribute row: on the Module row, constructor
  // [msil.link]'<msil>LinkInfo'::.ctor(uint8[]), blob 01 00 <n> ... 00 00.
  ASSERT_EQ(R.getRowCount(TableId::CustomAttribute), 1u);
  Expected<CustomAttributeRow> CA = R.getCustomAttribute(1);
  ASSERT_THAT_EXPECTED(CA, Succeeded());
  EXPECT_EQ(CA->Parent, makeToken(TableId::Module, 1));
  ASSERT_EQ(getTokenType(CA->Constructor), uint8_t(TableId::MemberRef));
  Expected<MemberRefRow> Ctor = R.getMemberRef(getTokenRow(CA->Constructor));
  ASSERT_THAT_EXPECTED(Ctor, Succeeded());
  EXPECT_EQ(Ctor->Name, ".ctor");
  const uint8_t CtorSig[] = {0x20, 0x01, 0x01, 0x1D, 0x05};
  EXPECT_THAT_EXPECTED(R.getBlob(Ctor->Signature),
                       HasValue(ArrayRef<uint8_t>(CtorSig)));
  EXPECT_THAT_EXPECTED(R.getTypeFullName(Ctor->Parent),
                       HasValue("<msil>LinkInfo"));
  Expected<ArrayRef<uint8_t>> Value = R.getBlob(CA->Value);
  ASSERT_THAT_EXPECTED(Value, Succeeded());
  ASSERT_EQ(Value->size(), Bytes->size() + 8);
  EXPECT_EQ((*Value)[0], 0x01);
  EXPECT_EQ((*Value)[1], 0x00);
  EXPECT_EQ(support::endian::read32le(Value->data() + 2), Bytes->size());
  const uint8_t NoNamedArgs[] = {0x00, 0x00};
  EXPECT_EQ(Value->take_back(2), ArrayRef<uint8_t>(NoNamedArgs));

  // The COFF view (5.8.6) is in the file.
  Expected<PEImage> P = PEImage::create(O->File);
  ASSERT_THAT_EXPECTED(P, Succeeded());
  Expected<std::vector<CoffSymbol>> Syms = P->getCoffSymbols();
  ASSERT_THAT_EXPECTED(Syms, Succeeded());
  std::vector<CoffSymbol> View = buildCOFFSymbolView(O->Rec);
  ASSERT_EQ(Syms->size(), View.size());
  for (size_t I = 0; I < View.size(); ++I) {
    EXPECT_EQ((*Syms)[I].Name, View[I].Name);
    EXPECT_EQ((*Syms)[I].SectionNumber, View[I].SectionNumber);
  }
  ASSERT_EQ(P->sections().size(), 4u);
  EXPECT_EQ(P->sections()[1].Name, ".data");
  EXPECT_EQ(P->sections()[2].Name, ".rdata");
  EXPECT_EQ(P->sections()[3].Name, ".bss");
}

TEST(CLILinkInfoTest, NoCarrier) {
  for (CarrierShape S : {CarrierShape::None, CarrierShape::OtherScope}) {
    SCOPED_TRACE(S == CarrierShape::None ? "none" : "other scope");
    std::unique_ptr<CarrierObject> O = buildCarrierObject(S);
    ASSERT_TRUE(O->R);
    Expected<ArrayRef<uint8_t>> Bytes = findLinkInfo(*O->R);
    ASSERT_FALSE(bool(Bytes));
    EXPECT_THAT(toString(Bytes.takeError()), HasSubstr("no link-info record"));
  }
}

// Exactly one carrier per object (5.8.7).
TEST(CLILinkInfoTest, TwoCarriers) {
  std::unique_ptr<CarrierObject> O = buildCarrierObject(CarrierShape::Two);
  ASSERT_TRUE(O->R);
  EXPECT_THAT_EXPECTED(findLinkInfo(*O->R), Failed());
}

//===----------------------------------------------------------------------===//
// C1-C7 (5.8.5)
//===----------------------------------------------------------------------===//

// A CIL object for the validation rules, in the shape 5.8.3 and 5.8.10
// describe: defined data (native int fields of <Module>) and functions,
// P/Invoke stubs on a named module (EXPLICIT_IMPORT) and on msil.unresolved,
// MemberRefs on [msil.extern]'<msil>Symbols' and '<msil>FnAddr', a BCL
// MemberRef and the carrier's constructor. Each knob breaks one rule.
struct ObjSpec {
  std::string FName = "f";
  bool GvInt32 = false, GvInOtherType = false;
  std::string GvName = "gv";
  bool ExtraMethod = false, ExtraField = false;
  bool StubWithoutImplMap = false, SecondExtStubOnOtherModule = false,
       EmptyImportName = false;
  bool SymbolsRefNoSymbol = false, MethodRefToData = false,
       FnAddrToData = false;
  int BadParent =
      0; // 1: TypeRef scoped to a ModuleRef, 2: TypeDef, 3: ModuleRef
};

struct Obj {
  MetadataBuilder MB;
  MethodBodyStream Bodies;
  ByteVec File;
  std::unique_ptr<MetadataReader> R;
  Record Rec;
  uint32_t GV = 0, CV = 0, F = 0, G = 0, UndefFn = 0, UndefVar = 0,
           AllocHGlobal = 0, Ext = 0;
};

std::unique_ptr<Obj> buildObj(const ObjSpec &S) {
  auto O = std::make_unique<Obj>();
  MetadataBuilder &MB = O->MB;
  MB.setModuleName("obj.c");
  AssemblyRefInfo Extern, Link, Bcl;
  Extern.Name = MSILLink::ExternAssembly.str();
  Link.Name = MSILLink::LinkInfoAssembly.str();
  Bcl.Name = MSILLink::BCLAssembly.str();
  EntityHandle ExternRef = MB.addAssemblyRef(Extern);
  EntityHandle Symbols = MB.addTypeRef(ExternRef, "", MSILLink::SymbolsType);
  EntityHandle FnAddr = MB.addTypeRef(ExternRef, "", MSILLink::FnAddrType);
  EntityHandle LinkType =
      MB.addTypeRef(MB.addAssemblyRef(Link), "", MSILLink::LinkInfoType);
  EntityHandle Marshal = MB.addTypeRef(
      MB.addAssemblyRef(Bcl), "System.Runtime.InteropServices", "Marshal");
  EntityHandle LibFoo = MB.addModuleRef("libfoo");
  EntityHandle Unresolved = MB.addModuleRef(MSILLink::UnresolvedModule);
  EntityHandle Mod = MB.getModuleType();
  const uint32_t NativeInt = blob(MB, {0x06, 0x18});
  const uint32_t Int32 = blob(MB, {0x06, 0x08});
  const uint32_t VoidFn = blob(MB, {0x00, 0x00, 0x01});
  const uint32_t IntFn = blob(MB, {0x00, 0x00, 0x08});
  const MethodAttributes Pub =
      MethodAttributes::Public | MethodAttributes::Static;
  const MethodAttributes Stub = Pub | MethodAttributes::PinvokeImpl;

  EntityHandle Other;
  if (S.GvInOtherType)
    Other = MB.addTypeDef(TypeAttributes::Public | TypeAttributes::Abstract |
                              TypeAttributes::Sealed,
                          "", "Other", EntityHandle());
  EntityHandle GV =
      MB.addField(S.GvInOtherType ? Other : Mod,
                  FieldAttributes::Public | FieldAttributes::Static, S.GvName,
                  S.GvInt32 ? Int32 : NativeInt);
  EntityHandle CV = MB.addField(
      Mod, FieldAttributes::Public | FieldAttributes::Static, "cv", NativeInt);
  EntityHandle ExtraF;
  if (S.ExtraField)
    ExtraF = MB.addField(Mod, FieldAttributes::Public | FieldAttributes::Static,
                         "extra_v", NativeInt);
  EntityHandle F =
      MB.addMethod(Mod, Pub, MethodImplAttributes::IL, S.FName, IntFn);
  EntityHandle G =
      MB.addMethod(Mod, MethodAttributes::Assembly | MethodAttributes::Static,
                   MethodImplAttributes::IL, "g", IntFn);
  EntityHandle Extra;
  if (S.ExtraMethod)
    Extra = MB.addMethod(Mod, Pub, MethodImplAttributes::IL, "extra", IntFn);
  EntityHandle Ext =
      MB.addMethod(Mod, Stub, MethodImplAttributes::PreserveSig, "ext", IntFn);
  MB.addImplMap(Ext,
                PInvokeAttributes::NoMangle | PInvokeAttributes::CallConvCdecl,
                S.EmptyImportName ? "" : "ext_real", LibFoo);
  if (S.SecondExtStubOnOtherModule) {
    EntityHandle Ext2 = MB.addMethod(
        Mod, Stub, MethodImplAttributes::PreserveSig, "ext", VoidFn);
    MB.addImplMap(
        Ext2, PInvokeAttributes::NoMangle | PInvokeAttributes::CallConvCdecl,
        "ext_real", MB.addModuleRef("libbar"));
  }
  EntityHandle U2 =
      MB.addMethod(Mod, Stub, MethodImplAttributes::PreserveSig, "u2", IntFn);
  MB.addImplMap(U2,
                PInvokeAttributes::NoMangle | PInvokeAttributes::CallConvCdecl,
                "real_u2", Unresolved);
  if (S.StubWithoutImplMap)
    MB.addMethod(Mod, Stub, MethodImplAttributes::PreserveSig, "nomap", IntFn);

  EntityHandle UndefFn = MB.addMemberRef(Symbols, "undef_fn", VoidFn);
  EntityHandle UndefVar = MB.addMemberRef(Symbols, "undef_var", NativeInt);
  MB.addMemberRef(FnAddr, "f", NativeInt);
  EntityHandle Alloc = MB.addMemberRef(Marshal, "AllocHGlobal",
                                       blob(MB, {0x00, 0x01, 0x18, 0x08}));
  EntityHandle Ctor = MB.addMemberRef(LinkType, ".ctor",
                                      blob(MB, {0x20, 0x01, 0x01, 0x1D, 0x05}));
  if (S.SymbolsRefNoSymbol)
    MB.addMemberRef(Symbols, "nosuch", VoidFn);
  if (S.MethodRefToData)
    MB.addMemberRef(Symbols, "undef_var", VoidFn);
  if (S.FnAddrToData)
    MB.addMemberRef(FnAddr, "gv", NativeInt);
  if (S.BadParent == 1) {
    EntityHandle InModule = MB.addTypeRef(LibFoo, "", "T");
    MB.addMemberRef(InModule, "m", VoidFn);
  } else if (S.BadParent == 2) {
    MB.addMemberRef(Mod, "m", VoidFn);
  } else if (S.BadParent == 3) {
    MB.addMemberRef(LibFoo, "m", VoidFn);
  }
  // The carrier, with a header-only payload (its content is not C1-C7's).
  {
    Record Hdr;
    Hdr.Hdr.Triple = "x86_64-unknown-linux-gnu-msil";
    SmallVector<uint8_t, 0> P, V;
    encodeLinkInfo(Hdr, P);
    encodeByteArrayAttribute(P, V);
    MB.addCustomAttribute(rowHandle(TableId::Module, 1), Ctor, MB.addBlob(V));
  }

  const uint8_t Ret1[] = {0x17, 0x2A};
  MethodBodyInfo B;
  B.Code = Ret1;
  for (EntityHandle M : {F, G, Extra})
    if (!M.isNil())
      MB.setMethodBodyOffset(M, O->Bodies.addBody(B));
  EXPECT_THAT_ERROR(MB.finalize(O->Bodies), Succeeded());

  O->GV = MB.getToken(GV);
  O->CV = MB.getToken(CV);
  O->F = MB.getToken(F);
  O->G = MB.getToken(G);
  O->Ext = MB.getToken(Ext);
  O->UndefFn = MB.getToken(UndefFn);
  O->UndefVar = MB.getToken(UndefVar);
  O->AllocHGlobal = MB.getToken(Alloc);

  Record &R = O->Rec;
  R.Hdr.Triple = "x86_64-unknown-linux-gnu-msil";
  const SymbolKind Fn = SymbolKind::Function, D = SymbolKind::Data;
  R.Symbols = {dataSym(0, "gv", O->GV, 0, 0, 8),           // 0
               sym(D, BindingCommon, "cv", O->CV),         // 1
               sym(Fn, 0, "f", O->F),                      // 2
               sym(Fn, BindingLocal, "g", O->G),           // 3
               sym(Fn, Undefined | ExplicitImport, "ext"), // 4
               sym(Fn, Undefined, "u2"),                   // 5
               sym(Fn, Undefined, "undef_fn"),             // 6
               sym(D, Undefined, "undef_var")};            // 7
  R.Symbols[1].CommonSize = 4;
  R.Symbols[1].CommonAlignLog2 = 2;
  if (S.StubWithoutImplMap)
    R.Symbols.push_back(sym(Fn, Undefined, "nomap"));
  R.Segments = {segment(".data", 0, 3, 8)};
  R.Signatures = {sig(O->F, NoPrototype), sig(O->UndefFn, NoPrototype)};

  Expected<PEOptions> Opts = PEOptions::get(
      Triple("x86_64-unknown-linux-gnu-msil"), PEImageKind::Module);
  EXPECT_THAT_EXPECTED(Opts, Succeeded());
  if (!Opts)
    return O;
  PEContent C;
  C.Metadata = &MB;
  C.ILStream = O->Bodies.getBytes();
  SmallVector<uint8_t, 0> Out;
  EXPECT_THAT_ERROR(writePEImage(*Opts, C, Out), Succeeded());
  O->File.assign(Out.begin(), Out.end());
  Expected<std::unique_ptr<MetadataReader>> Rd =
      MetadataReader::create(O->File);
  EXPECT_THAT_EXPECTED(Rd, Succeeded());
  if (Rd)
    O->R = std::move(*Rd);
  return O;
}

// The record must decode (C8) before C1-C7 apply: every case here does.
void expectRule(const Obj &O, const Record &R,
                std::initializer_list<int> Rules) {
  ASSERT_TRUE(O.R);
  ASSERT_THAT_EXPECTED(decodeLinkInfo(encodeLib(R)), Succeeded());
  Error E = validateLinkInfo(R, *O.R);
  ASSERT_TRUE(bool(E)) << "validateLinkInfo accepted the object";
  std::string Msg = toString(std::move(E));
  bool Match = llvm::any_of(Rules, [&](int N) {
    return StringRef(Msg).starts_with("C" + std::to_string(N) + ": ");
  });
  EXPECT_TRUE(Match) << Msg;
}

TEST(CLILinkInfoTest, ValidObjectPassesC1ToC7) {
  std::unique_ptr<Obj> O = buildObj({});
  ASSERT_TRUE(O->R);
  EXPECT_THAT_ERROR(validateLinkInfo(O->Rec, *O->R), Succeeded());
}

TEST(CLILinkInfoTest, C1DefinedFunctions) {
  {
    SCOPED_TRACE("the MethodDef has another name");
    ObjSpec S;
    S.FName = "h";
    std::unique_ptr<Obj> O = buildObj(S);
    expectRule(*O, O->Rec, {1});
  }
  {
    SCOPED_TRACE("the token is a body-less MethodDef");
    std::unique_ptr<Obj> O = buildObj({});
    Record R = O->Rec;
    R.Symbols[2].Token = O->Ext;
    expectRule(*O, R, {1, 3});
  }
  {
    SCOPED_TRACE("the token names no row");
    std::unique_ptr<Obj> O = buildObj({});
    Record R = O->Rec;
    R.Symbols[2].Token = makeToken(TableId::MethodDef, 99);
    expectRule(*O, R, {1, 3});
  }
}

TEST(CLILinkInfoTest, C2DefinedData) {
  {
    SCOPED_TRACE("int32 instead of native int");
    ObjSpec S;
    S.GvInt32 = true;
    std::unique_ptr<Obj> O = buildObj(S);
    expectRule(*O, O->Rec, {2});
  }
  {
    SCOPED_TRACE("a field of another type");
    ObjSpec S;
    S.GvInOtherType = true;
    std::unique_ptr<Obj> O = buildObj(S);
    expectRule(*O, O->Rec, {2});
  }
  {
    SCOPED_TRACE("the FieldDef has another name");
    ObjSpec S;
    S.GvName = "gw";
    std::unique_ptr<Obj> O = buildObj(S);
    expectRule(*O, O->Rec, {2});
  }
  {
    SCOPED_TRACE("a COMMON symbol whose token is a MethodDef");
    std::unique_ptr<Obj> O = buildObj({});
    Record R = O->Rec;
    R.Symbols[1].Token = O->G;
    expectRule(*O, R, {2, 3});
  }
}

TEST(CLILinkInfoTest, C3EveryDefinitionHasASymbol) {
  {
    SCOPED_TRACE("a method with a body and no symbol");
    ObjSpec S;
    S.ExtraMethod = true;
    std::unique_ptr<Obj> O = buildObj(S);
    expectRule(*O, O->Rec, {3});
  }
  {
    SCOPED_TRACE("a field and no symbol");
    ObjSpec S;
    S.ExtraField = true;
    std::unique_ptr<Obj> O = buildObj(S);
    expectRule(*O, O->Rec, {3});
  }
}

TEST(CLILinkInfoTest, C4Stubs) {
  {
    SCOPED_TRACE("a body-less method without an ImplMap row");
    ObjSpec S;
    S.StubWithoutImplMap = true;
    std::unique_ptr<Obj> O = buildObj(S);
    expectRule(*O, O->Rec, {4});
  }
  {
    SCOPED_TRACE("a named module without EXPLICIT_IMPORT");
    std::unique_ptr<Obj> O = buildObj({});
    Record R = O->Rec;
    R.Symbols[4].Flags = Undefined;
    expectRule(*O, R, {4});
  }
  {
    SCOPED_TRACE("msil.unresolved with EXPLICIT_IMPORT");
    std::unique_ptr<Obj> O = buildObj({});
    Record R = O->Rec;
    R.Symbols[5].Flags = Undefined | ExplicitImport;
    expectRule(*O, R, {4});
  }
  {
    SCOPED_TRACE("a stub that names no symbol");
    std::unique_ptr<Obj> O = buildObj({});
    Record R = O->Rec;
    R.Symbols[5].Name = "u3";
    expectRule(*O, R, {4});
  }
  {
    SCOPED_TRACE("a stub that names a DATA symbol");
    std::unique_ptr<Obj> O = buildObj({});
    Record R = O->Rec;
    R.Symbols[5].Kind = SymbolKind::Data;
    expectRule(*O, R, {4, 5});
  }
  {
    SCOPED_TRACE("two stubs of one symbol on different modules");
    ObjSpec S;
    S.SecondExtStubOnOtherModule = true;
    std::unique_ptr<Obj> O = buildObj(S);
    expectRule(*O, O->Rec, {4});
  }
  {
    SCOPED_TRACE("an empty import name");
    ObjSpec S;
    S.EmptyImportName = true;
    std::unique_ptr<Obj> O = buildObj(S);
    expectRule(*O, O->Rec, {4});
  }
}

TEST(CLILinkInfoTest, C5SymbolReferences) {
  {
    SCOPED_TRACE("a '<msil>Symbols' MemberRef without a symbol");
    ObjSpec S;
    S.SymbolsRefNoSymbol = true;
    std::unique_ptr<Obj> O = buildObj(S);
    expectRule(*O, O->Rec, {5});
  }
  {
    SCOPED_TRACE("a method signature naming a DATA symbol");
    ObjSpec S;
    S.MethodRefToData = true;
    std::unique_ptr<Obj> O = buildObj(S);
    expectRule(*O, O->Rec, {5});
  }
  {
    SCOPED_TRACE("a field signature naming a FUNCTION symbol");
    std::unique_ptr<Obj> O = buildObj({});
    Record R = O->Rec;
    R.Symbols[7].Kind = SymbolKind::Function;
    expectRule(*O, R, {5});
  }
  {
    SCOPED_TRACE("'<msil>FnAddr' naming a DATA symbol");
    ObjSpec S;
    S.FnAddrToData = true;
    std::unique_ptr<Obj> O = buildObj(S);
    expectRule(*O, O->Rec, {5});
  }
}

TEST(CLILinkInfoTest, C6OtherMemberRefs) {
  for (int Bad : {1, 2, 3}) {
    SCOPED_TRACE("parent kind " + std::to_string(Bad));
    ObjSpec S;
    S.BadParent = Bad;
    std::unique_ptr<Obj> O = buildObj(S);
    expectRule(*O, O->Rec, {6});
  }
}

// Nested BCL type references (C6): a TypeRef whose ResolutionScope is a
// TypeRef names a nested type, so a MemberRef on it is a BCL request when the
// outermost enclosing TypeRef is scoped to an AssemblyRef. The module holds
// <Module>, TypeRefs 1 to N (TypeRef 1 scoped to an AssemblyRef, or to a
// ModuleRef, and each other one to the one before) and a MemberRef on TypeRef
// N, and nothing that C1-C5 or C7 would ask a symbol for.
struct ChainObject {
  ByteVec File;
  std::unique_ptr<MetadataReader> R;
  uint32_t First = 0, Inner = 0, Member = 0; // tokens
};

std::unique_ptr<ChainObject> buildTypeRefChain(uint32_t N,
                                               bool InModuleRef = false) {
  auto O = std::make_unique<ChainObject>();
  MetadataBuilder MB;
  MB.setModuleName("chain.c");
  AssemblyRefInfo Bcl;
  Bcl.Name = MSILLink::BCLAssembly.str();
  EntityHandle Scope =
      InModuleRef ? MB.addModuleRef("libfoo") : MB.addAssemblyRef(Bcl);
  EntityHandle First;
  for (uint32_t I = 1; I <= N; ++I) {
    Scope = I == 1 ? MB.addTypeRef(Scope, "System", "Outer")
                   : MB.addTypeRef(Scope, "", "Nested");
    if (I == 1)
      First = Scope;
  }
  EntityHandle Member =
      MB.addMemberRef(Scope, "M", blob(MB, {0x00, 0x00, 0x01}));
  MethodBodyStream Bodies;
  EXPECT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  O->First = MB.getToken(First);
  O->Inner = MB.getToken(Scope);
  O->Member = MB.getToken(Member);

  Expected<PEOptions> Opts = PEOptions::get(
      Triple("x86_64-unknown-linux-gnu-msil"), PEImageKind::Module);
  EXPECT_THAT_EXPECTED(Opts, Succeeded());
  if (!Opts)
    return O;
  PEContent C;
  C.Metadata = &MB;
  C.ILStream = Bodies.getBytes();
  SmallVector<uint8_t, 0> Out;
  EXPECT_THAT_ERROR(writePEImage(*Opts, C, Out), Succeeded());
  O->File.assign(Out.begin(), Out.end());
  Expected<std::unique_ptr<MetadataReader>> R = MetadataReader::create(O->File);
  EXPECT_THAT_EXPECTED(R, Succeeded());
  if (R)
    O->R = std::move(*R);
  return O;
}

Record headerOnlyRecord() {
  Record R;
  R.Hdr.Triple = "x86_64-unknown-linux-gnu-msil";
  return R;
}

// The offset of the table data in a metadata root: after the root header and
// its stream headers (II.24.2.1, II.24.2.2), the #~ stream's header, its row
// counts and its extra data field (II.24.2.6). 0 if there is no #~ stream.
size_t tableDataOffset(ArrayRef<uint8_t> Root) {
  using namespace support::endian;
  if (Root.size() < 16)
    return 0;
  size_t P = 16 + read32le(Root.data() + 12) + 2; // version string, flags
  if (P + 2 > Root.size())
    return 0;
  const unsigned Streams = read16le(Root.data() + P);
  P += 2;
  for (unsigned I = 0; I < Streams; ++I) {
    if (P + 8 > Root.size())
      return 0;
    const uint32_t Offset = read32le(Root.data() + P);
    StringRef Name =
        StringRef(reinterpret_cast<const char *>(Root.data()) + P + 8,
                  Root.size() - P - 8)
            .split('\0')
            .first;
    P += 8 + alignTo(Name.size() + 1, 4);
    if (Name != "#~")
      continue;
    if (uint64_t(Offset) + 24 > Root.size())
      return 0;
    const uint8_t HeapSizes = Root[Offset + 6];
    const uint64_t Valid = read64le(Root.data() + Offset + 8);
    return Offset + 24 + 4 * llvm::popcount(Valid) + (HeapSizes & 0x40 ? 4 : 0);
  }
  return 0;
}

// A reader of the chain object's metadata in which TypeRef row \p Row is
// scoped to TypeRef row \p Target: the ResolutionScope column patched in a
// copy of the metadata root, after checking that it holds the row's scope.
std::unique_ptr<MetadataReader>
withTypeRefScope(const ChainObject &O, uint32_t Row, uint32_t Target) {
  Expected<PEImage> P = PEImage::create(O.File);
  EXPECT_THAT_EXPECTED(P, Succeeded());
  if (!P)
    return nullptr;
  Expected<Cor20Header> H = P->getCLIHeader();
  EXPECT_THAT_EXPECTED(H, Succeeded());
  if (!H)
    return nullptr;
  Expected<ArrayRef<uint8_t>> RootData =
      P->getRVAData(H->MetaData.RVA, H->MetaData.Size);
  EXPECT_THAT_EXPECTED(RootData, Succeeded());
  if (!RootData)
    return nullptr;
  ByteVec Root(RootData->begin(), RootData->end());
  const size_t Tables = tableDataOffset(Root);
  EXPECT_NE(Tables, 0u) << "no #~ stream";
  const MetadataSizes &S = O.R->getSizes();
  const size_t At = Tables + S.getTableOffset(TableId::TypeRef) +
                    size_t(Row - 1) * S.getRowSize(TableId::TypeRef) +
                    S.getColumnOffset(TableId::TypeRef, 0);
  const unsigned Width = S.getColumnSize(TableId::TypeRef, 0);
  if (Tables == 0 || At + Width > Root.size()) {
    ADD_FAILURE() << "TypeRef " << Row << " is outside the metadata root";
    return nullptr;
  }
  Expected<TypeRefRow> Old = O.R->getTypeRef(Row);
  EXPECT_THAT_EXPECTED(Old, Succeeded());
  if (!Old)
    return nullptr;
  std::optional<uint32_t> OldValue =
      encodeCodedIndex(CodedIndexKind::ResolutionScope, Old->ResolutionScope);
  std::optional<uint32_t> NewValue = encodeCodedIndex(
      CodedIndexKind::ResolutionScope, makeToken(TableId::TypeRef, Target));
  if (!OldValue || !NewValue) {
    ADD_FAILURE() << "no ResolutionScope coded index";
    return nullptr;
  }
  uint32_t Stored = Width == 2 ? support::endian::read16le(&Root[At])
                               : support::endian::read32le(&Root[At]);
  EXPECT_EQ(Stored, *OldValue) << "the column is not where II.24.2.6 says";
  if (Stored != *OldValue)
    return nullptr;
  if (Width == 2)
    support::endian::write16le(&Root[At], *NewValue);
  else
    support::endian::write32le(&Root[At], *NewValue);
  Expected<std::unique_ptr<MetadataReader>> R =
      MetadataReader::createFromMetadata(Root);
  EXPECT_THAT_EXPECTED(R, Succeeded());
  if (!R)
    return nullptr;
  Expected<TypeRefRow> New = (*R)->getTypeRef(Row);
  EXPECT_THAT_EXPECTED(New, Succeeded());
  if (!New || New->ResolutionScope != makeToken(TableId::TypeRef, Target)) {
    ADD_FAILURE() << "the patch did not take";
    return nullptr;
  }
  return std::move(*R);
}

// A long chain: 100,000 nested TypeRefs (a check that walks a chain once per
// enclosing level is quadratic here). No timing assertion.
TEST(CLILinkInfoTest, C6NestedTypeRefChain) {
  constexpr uint32_t N = 100000;
  std::unique_ptr<ChainObject> O = buildTypeRefChain(N);
  ASSERT_TRUE(O->R);
  const MetadataReader &R = *O->R;
  ASSERT_EQ(R.getRowCount(TableId::TypeRef), N);
  ASSERT_EQ(R.getRowCount(TableId::MemberRef), 1u);
  // Over 2^14 TypeRefs: ResolutionScope (two tag bits) is 4 bytes wide.
  EXPECT_EQ(R.getSizes().getColumnSize(TableId::TypeRef, 0), 4u);
  Expected<TypeRefRow> Outer = R.getTypeRef(getTokenRow(O->First));
  ASSERT_THAT_EXPECTED(Outer, Succeeded());
  EXPECT_EQ(getTokenType(Outer->ResolutionScope),
            uint8_t(TableId::AssemblyRef));
  Expected<TypeRefRow> Inner = R.getTypeRef(getTokenRow(O->Inner));
  ASSERT_THAT_EXPECTED(Inner, Succeeded());
  EXPECT_EQ(getTokenType(Inner->ResolutionScope), uint8_t(TableId::TypeRef));
  Expected<MemberRefRow> M = R.getMemberRef(getTokenRow(O->Member));
  ASSERT_THAT_EXPECTED(M, Succeeded());
  EXPECT_EQ(M->Parent, O->Inner);
  EXPECT_THAT_ERROR(validateLinkInfo(headerOnlyRecord(), R), Succeeded());

  // The same nesting under a ModuleRef is not a BCL request.
  std::unique_ptr<ChainObject> InModule = buildTypeRefChain(3, true);
  ASSERT_TRUE(InModule->R);
  Error E = validateLinkInfo(headerOnlyRecord(), *InModule->R);
  ASSERT_TRUE(bool(E)) << "validateLinkInfo accepted the object";
  EXPECT_THAT(toString(std::move(E)), StartsWith("C6: "));
}

// A ResolutionScope cycle never reaches an AssemblyRef: a MemberRef on a
// TypeRef in a cycle, or on one whose enclosing chain runs into a cycle, is a
// C6 violation (and the check ends).
TEST(CLILinkInfoTest, C6TypeRefScopeCycle) {
  std::unique_ptr<ChainObject> O = buildTypeRefChain(3);
  ASSERT_TRUE(O->R);
  ASSERT_THAT_ERROR(validateLinkInfo(headerOnlyRecord(), *O->R), Succeeded());
  const uint32_t First = getTokenRow(O->First), Inner = getTokenRow(O->Inner);
  struct {
    const char *What;
    uint32_t Target;
  } Cases[] = {{"TypeRef 1 scoped to TypeRef 3", Inner},
               {"TypeRef 1 scoped to itself", First}};
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.What);
    std::unique_ptr<MetadataReader> R = withTypeRefScope(*O, First, C.Target);
    ASSERT_TRUE(R);
    Error E = validateLinkInfo(headerOnlyRecord(), *R);
    ASSERT_TRUE(bool(E)) << "validateLinkInfo accepted the object";
    EXPECT_THAT(toString(std::move(E)), StartsWith("C6: "));
  }
}

TEST(CLILinkInfoTest, C7Signatures) {
  std::unique_ptr<Obj> O = buildObj({});
  struct {
    const char *What;
    uint32_t Token;
  } Cases[] = {
      {"a FieldDef", O->GV},
      {"a MemberRef on a BCL type", O->AllocHGlobal},
      {"a field MemberRef on '<msil>Symbols'", O->UndefVar},
      {"a MethodDef that does not exist", makeToken(TableId::MethodDef, 99)},
      {"a TypeRef", makeToken(TableId::TypeRef, 1)}};
  for (const auto &C : Cases) {
    SCOPED_TRACE(C.What);
    Record R = O->Rec;
    R.Signatures.push_back(sig(C.Token, NoPrototype));
    expectRule(*O, R, {7});
  }
  // At most one entry per token (the decoder refuses this too).
  Record R = O->Rec;
  R.Signatures.push_back(sig(O->F, 0));
  Error E = validateLinkInfo(R, *O->R);
  ASSERT_TRUE(bool(E));
  EXPECT_THAT(toString(std::move(E)), StartsWith("C7: "));
}

//===----------------------------------------------------------------------===//
// The COFF view (5.8.6)
//===----------------------------------------------------------------------===//

TEST(CLILinkInfoTest, MarkerSections) {
  ArrayRef<PESection> M = getCOFFViewMarkerSections();
  ASSERT_EQ(M.size(), 3u);
  EXPECT_EQ(M[0].Name, ".data");
  EXPECT_EQ(M[0].Characteristics, 0xC0000040u);
  EXPECT_EQ(M[1].Name, ".rdata");
  EXPECT_EQ(M[1].Characteristics, 0x40000040u);
  EXPECT_EQ(M[2].Name, ".bss");
  EXPECT_EQ(M[2].Characteristics, 0xC0000080u);
  for (const PESection &S : M)
    EXPECT_TRUE(S.Data.empty()) << S.Name;
}

TEST(CLILinkInfoTest, COFFSymbolViewRows) {
  Record R = fullRecord();
  std::vector<CoffSymbol> V = buildCOFFSymbolView(R);
  const uint8_t Ext = COFF::IMAGE_SYM_CLASS_EXTERNAL,
                Sta = COFF::IMAGE_SYM_CLASS_STATIC;
  struct Row {
    int16_t Section;
    uint16_t Type;
    uint8_t Class;
    uint32_t Value;
  } Want[] = {
      {3, 0, Ext, 0},    // table: READONLY segment -> .rdata
      {2, 0, Sta, 0},    // a: LOCAL, .data
      {2, 0, Ext, 0},    // b: WEAK, .data
      {4, 0, Ext, 0},    // zeroed: ZERO segment -> .bss
      {0, 0, Ext, 0},    // my_counter: undefined
      {0, 0, Ext, 4},    // common_c: COMMON, Value = size
      {1, 0x20, Ext, 0}, // weakfn: plain record, never WEAK_EXTERNAL
      {1, 0x20, Sta, 0}, // ctor_a: LOCAL
      {0, 0x20, Ext, 0}, // add3: undefined function
      {0, 0x20, Ext, 0}, // GetTickCount
      {1, 0x20, Ext, 0}, // hookee
      {1, 0x20, Ext, 0}, // hook: function alias
      {3, 0, Ext, 0},    // table_tail: the aliasee's section
      {0, 0, Ext, 0},    // imported_var: weak undefined
      {2, 0, Sta, 0},    // tls_v: other defined data
      {0, 0, Ext, 0},    // weak_undef_data
      {0, 0x20, Ext, 0}, // weak_undef_fn
      {1, 0x20, Ext, 0}, // exported
      {0, 0, Ext, 0},    // dllimp_data
      {4, 0, Ext, 0},    // tls_z: TLS|ZERO segment -> .bss
  };
  ASSERT_EQ(V.size(), R.Symbols.size());
  ASSERT_EQ(V.size(), std::size(Want));
  for (size_t I = 0; I < V.size(); ++I) {
    SCOPED_TRACE(R.Symbols[I].Name);
    EXPECT_EQ(V[I].Name, R.Symbols[I].Name);
    EXPECT_EQ(V[I].SectionNumber, Want[I].Section);
    EXPECT_EQ(V[I].Type, Want[I].Type);
    EXPECT_EQ(V[I].StorageClass, Want[I].Class);
    EXPECT_EQ(V[I].Value, Want[I].Value);
  }
  EXPECT_TRUE(buildCOFFSymbolView(Record()).empty());
}

//===----------------------------------------------------------------------===//
// printLinkInfo (5.8.11; M17.c item 7)
//===----------------------------------------------------------------------===//

std::vector<std::string> printLines(const Record &R,
                                    const MetadataReader *MD = nullptr) {
  std::string S;
  raw_string_ostream OS(S);
  printLinkInfo(R, MD, OS);
  OS.flush();
  std::vector<std::string> Lines;
  StringRef Text(S);
  EXPECT_TRUE(Text.empty() || Text.ends_with("\n")) << "no final newline";
  while (!Text.empty()) {
    auto [Line, Rest] = Text.split('\n');
    Lines.push_back(Line.str());
    Text = Rest;
  }
  return Lines;
}

std::string joinLines(ArrayRef<std::string> Lines) {
  std::string All;
  for (const std::string &L : Lines)
    All += L + "\n";
  return All;
}

// Every Want line appears whole, in order.
void expectLinesInOrder(ArrayRef<std::string> Got, ArrayRef<std::string> Want) {
  size_t Pos = 0;
  for (const std::string &W : Want) {
    size_t I = Pos;
    while (I < Got.size() && Got[I] != W)
      ++I;
    if (I == Got.size()) {
      ADD_FAILURE() << "missing (in order): " << W << "\nprinted:\n"
                    << joinLines(Got);
      return;
    }
    Pos = I + 1;
  }
}

// The printed lines are exactly Want.
void expectExactLines(ArrayRef<std::string> Got, ArrayRef<std::string> Want) {
  for (size_t I = 0, E = std::min(Got.size(), Want.size()); I < E; ++I)
    if (Got[I] != Want[I]) {
      ADD_FAILURE() << "line " << I << " differs\n  printed: " << Got[I]
                    << "\n  wanted:  " << Want[I] << "\nprinted:\n"
                    << joinLines(Got);
      return;
    }
  if (Got.size() != Want.size())
    ADD_FAILURE() << Got.size() << " lines printed, " << Want.size()
                  << " wanted\nprinted:\n"
                  << joinLines(Got);
}

// 5.8.11's example, from the same symbols, segments and items.
TEST(CLILinkInfoTest, PrintsTheExampleLines) {
  std::vector<std::string> Got = printLines(fullRecord());
  ASSERT_FALSE(Got.empty());
  EXPECT_EQ(Got[0], "LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 "
                    "abiflags=0x0 features=0x0 datamodel=8,4,2,8");
  expectLinesInOrder(
      Got, {"Symbol 0 DATA GLOBAL DEFAULT 'table' field=0x04000001 segment=0 "
            "offset=0 size=24",
            "Symbol 4 DATA GLOBAL DEFAULT UNDEFINED IMPORT 'my_counter' "
            "import_module='' import_name='native_counter'",
            "Symbol 5 DATA COMMON DEFAULT 'common_c' field=0x04000005 size=4 "
            "align=4",
            "Symbol 6 FUNCTION WEAK DEFAULT 'weakfn' method=0x06000001",
            "Symbol 8 FUNCTION GLOBAL DEFAULT UNDEFINED 'add3'",
            "Symbol 11 FUNCTION WEAK DEFAULT ALIAS 'hook' aliasee=10",
            "Segment 0 '.rodata' align=8 size=24 flags=READONLY",
            "Reloc FNADDR64 segment=0 offset=0 symbol=8 'add3' addend=0",
            "Reloc DIFF width=4 segment=2 offset=8 symbol=1 'a' symbol2=2 'b' "
            "addend=0",
            "Comdat '??_C@_05CJBACGMB@hello?$AA@' ANY segment=3",
            "Init priority=200 symbol=7 'ctor_a'",
            // Without a reader the entity name is left out.
            "Signature 0x0A000002 NO_PROTOTYPE byval=1:12:4",
            "LinkerOption DEPENDENT_LIB 'm'"});
}

// The grammar extended uniformly to every item of the full record: one line
// per item in record order, plus the hex lines of non-ZERO segments, and
// nothing else. The selection names, the 0-based comdat index of an Init line
// (the stored value minus 1: 'weakfn' is entry 1) and the align= bytes are
// section 12's row of 2026-10-23 (printer spellings).
TEST(CLILinkInfoTest, PrintsEveryItem) {
  std::vector<std::string> Got = printLines(fullRecord());
  const std::string Zeros32 =
      "  00 00 00 00 AA BB CC DD 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
      "00 00 00 00 00 00 00 00 00";
  expectExactLines(
      Got,
      {"LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 "
       "features=0x0 datamodel=8,4,2,8",
       "Symbol 0 DATA GLOBAL DEFAULT 'table' field=0x04000001 segment=0 "
       "offset=0 size=24",
       "Symbol 1 DATA LOCAL HIDDEN NO_STRIP 'a' field=0x04000002 segment=1 "
       "offset=0 size=8",
       "Symbol 2 DATA WEAK PROTECTED 'b' field=0x04000003 segment=2 offset=0 "
       "size=16",
       "Symbol 3 DATA GLOBAL INTERNAL DLLEXPORT 'zeroed' field=0x04000004 "
       "segment=4 offset=0 size=16",
       "Symbol 4 DATA GLOBAL DEFAULT UNDEFINED IMPORT 'my_counter' "
       "import_module='' import_name='native_counter'",
       "Symbol 5 DATA COMMON DEFAULT 'common_c' field=0x04000005 size=4 "
       "align=4",
       "Symbol 6 FUNCTION WEAK DEFAULT 'weakfn' method=0x06000001",
       "Symbol 7 FUNCTION LOCAL DEFAULT 'ctor_a' method=0x06000002",
       "Symbol 8 FUNCTION GLOBAL DEFAULT UNDEFINED 'add3'",
       "Symbol 9 FUNCTION GLOBAL DEFAULT UNDEFINED EXPLICIT_IMPORT DLLIMPORT "
       "'GetTickCount'",
       "Symbol 10 FUNCTION GLOBAL DEFAULT 'hookee' method=0x06000003",
       "Symbol 11 FUNCTION WEAK DEFAULT ALIAS 'hook' aliasee=10",
       "Symbol 12 DATA GLOBAL DEFAULT ALIAS 'table_tail' aliasee=0 offset=16 "
       "size=8",
       "Symbol 13 DATA WEAK DEFAULT UNDEFINED IMPORT 'imported_var' "
       "import_module='libfoo' import_name='foo_var'",
       "Symbol 14 DATA LOCAL DEFAULT TLS 'tls_v' field=0x04000006 segment=5 "
       "offset=0 size=4",
       "Symbol 15 DATA WEAK DEFAULT UNDEFINED 'weak_undef_data'",
       "Symbol 16 FUNCTION WEAK DEFAULT UNDEFINED 'weak_undef_fn'",
       "Symbol 17 FUNCTION GLOBAL DEFAULT DLLEXPORT NO_STRIP 'exported' "
       "method=0x06000004",
       "Symbol 18 DATA GLOBAL DEFAULT UNDEFINED DLLIMPORT 'dllimp_data'",
       "Symbol 19 DATA GLOBAL DEFAULT TLS 'tls_z' field=0x04000007 segment=6 "
       "offset=0 size=4",
       "Segment 0 '.rodata' align=8 size=24 flags=READONLY",
       "  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
       "00",
       "Segment 1 '.data' align=8 size=8 flags=RETAIN",
       "  00 00 00 00 00 00 00 00",
       "Segment 2 '.data.rel' align=8 size=16 flags=0",
       "  01 02 03 04 05 06 07 08 00 00 00 00 0D 0E 0F 10",
       "Segment 3 '.rodata.str1.1' align=1 size=6 flags=STRINGS|READONLY",
       "  68 65 6C 6C 6F 00",
       "Segment 4 '.bss' align=4 size=16 flags=ZERO",
       "Segment 5 '.tdata' align=4 size=4 flags=TLS",
       "  2A 00 00 00",
       "Segment 6 '.tbss' align=4 size=4 flags=TLS|ZERO",
       "Segment 7 'my$section' align=1 size=40 flags=0",
       Zeros32,
       "  00 00 00 00 EE FF 10 21",
       "Reloc FNADDR64 segment=0 offset=0 symbol=8 'add3' addend=0",
       "Reloc ADDR64 segment=0 offset=8 symbol=1 'a' addend=0",
       "Reloc FNADDR32 segment=0 offset=16 symbol=6 'weakfn' addend=0",
       "Reloc ADDR32 segment=0 offset=20 symbol=2 'b' addend=-4",
       "Reloc ADDR64 segment=1 offset=0 symbol=12 'table_tail' addend=300",
       "Reloc DIFF width=4 segment=2 offset=8 symbol=1 'a' symbol2=2 'b' "
       "addend=0",
       "Reloc TLS_OFFSET32 segment=7 offset=0 symbol=14 'tls_v' addend=0",
       "Reloc TLS_OFFSET64 segment=7 offset=8 symbol=19 'tls_z' addend=-1",
       "Reloc DIFF width=16 segment=7 offset=16 symbol=0 'table' symbol2=5 "
       "'common_c' addend=64",
       "Reloc ADDR32 segment=7 offset=32 symbol=4 'my_counter' addend=0",
       "Comdat '??_C@_05CJBACGMB@hello?$AA@' ANY segment=3",
       "Comdat 'weakfn' NO_DEDUPLICATE function=6 segment=1",
       "Comdat 'x' EXACT_MATCH function=10",
       "Comdat 'l' LARGEST segment=5",
       "Comdat 's' SAME_SIZE segment=6",
       "Init priority=200 symbol=7 'ctor_a'",
       "Init priority=65535 symbol=6 'weakfn' comdat=1",
       "Fini priority=0 symbol=10 'hookee'",
       "Init priority=101 symbol=17 'exported'",
       "Signature 0x06000001",
       "Signature 0x06000002 NO_PROTOTYPE",
       "Signature 0x0A000002 NO_PROTOTYPE byval=1:12:4",
       "Signature 0x0A000003 VARARG fixed=2",
       "Signature 0x06000003 SRET size=24 align=8 NATIVE_INCOMPATIBLE "
       "byval=1:8:8 byval=2:300:16",
       "Signature 0x06000004 NO_PROTOTYPE VARARG fixed=1 SRET size=16 align=4 "
       "NATIVE_INCOMPATIBLE",
       "LinkerOption DEPENDENT_LIB 'm'",
       "LinkerOption LINKER_OPTION '/DEFAULTLIB:ws2_32.lib' '-lws2_32'",
       "LinkerOption DEPENDENT_LIB 'ws2_32'",
       "Optional kind=0x45 size=3",
       "Optional kind=0x7F size=0"});
}

// Each selection by its name (5.8.8's values 0-4), and the comdat index of
// Init and Fini lines counted from 0, printed only when the entry has one.
TEST(CLILinkInfoTest, PrintsSelectionsAndInitComdats) {
  Record R;
  R.Hdr.Triple = "x86_64-unknown-linux-gnu-msil";
  const SymbolKind F = SymbolKind::Function;
  R.Symbols = {sym(F, 0, "f0", method(1)), sym(F, 0, "f1", method(2)),
               sym(F, 0, "f2", method(3)), sym(F, 0, "f3", method(4)),
               sym(F, 0, "f4", method(5))};
  const ComdatMemberKind Fn = ComdatMemberKind::FunctionSymbol;
  R.Comdats = {comdat("g0", ComdatSelection::Any, {{Fn, 0}}),
               comdat("g1", ComdatSelection::ExactMatch, {{Fn, 1}}),
               comdat("g2", ComdatSelection::Largest, {{Fn, 2}}),
               comdat("g3", ComdatSelection::NoDeduplicate, {{Fn, 3}}),
               comdat("g4", ComdatSelection::SameSize, {{Fn, 4}})};
  R.Inits = {init(InitKind::Init, 101, 0, 1), init(InitKind::Fini, 102, 4, 5),
             init(InitKind::Init, 103, 2)};
  ASSERT_THAT_EXPECTED(decodeLinkInfo(encodeSpec(R).Out), Succeeded());
  std::vector<std::string> Got = printLines(R);
  ASSERT_FALSE(Got.empty());
  expectExactLines(ArrayRef<std::string>(Got).drop_front(6),
                   {"Comdat 'g0' ANY function=0",
                    "Comdat 'g1' EXACT_MATCH function=1",
                    "Comdat 'g2' LARGEST function=2",
                    "Comdat 'g3' NO_DEDUPLICATE function=3",
                    "Comdat 'g4' SAME_SIZE function=4",
                    "Init priority=101 symbol=0 'f0' comdat=0",
                    "Fini priority=102 symbol=4 'f4' comdat=4",
                    "Init priority=103 symbol=2 'f2'"});
}

TEST(CLILinkInfoTest, PrintsHeaderFlagsAndQuotes) {
  Record R;
  R.Hdr.Triple = "aarch64-unknown-linux-gnu-msil";
  R.Hdr.Flags = HasDebugInfo;
  R.Hdr.Minor = 2;
  R.Symbols = {sym(SymbolKind::Function, Undefined, "it's\\\x01\x10")};
  std::vector<std::string> Got = printLines(R);
  ASSERT_EQ(Got.size(), 2u);
  EXPECT_EQ(Got[0], "LinkInfo v1.2 triple=aarch64-unknown-linux-gnu-msil "
                    "abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8 "
                    "flags=HAS_DEBUG_INFO");
  EXPECT_EQ(
      Got[1],
      "Symbol 0 FUNCTION GLOBAL DEFAULT UNDEFINED 'it\\'s\\\\\\x01\\x10'");
}

// With a reader, a SIGNATURES token is followed by its entity's name.
TEST(CLILinkInfoTest, PrintsEntityNamesWithAReader) {
  MetadataBuilder MB;
  MB.setModuleName("a.c");
  AssemblyRefInfo Extern;
  Extern.Name = MSILLink::ExternAssembly.str();
  EntityHandle Symbols =
      MB.addTypeRef(MB.addAssemblyRef(Extern), "", MSILLink::SymbolsType);
  MB.addMemberRef(Symbols, "add3", blob(MB, {0x00, 0x00, 0x08}));
  EntityHandle F =
      MB.addMemberRef(Symbols, "f", blob(MB, {0x00, 0x01, 0x01, 0x18}));
  MethodBodyStream Bodies;
  ASSERT_THAT_ERROR(MB.finalize(Bodies), Succeeded());
  ASSERT_EQ(MB.getToken(F), memberRef(2));
  ByteVec Root(MB.getSerializedSize());
  MB.serialize(Root, 0, 0);
  Expected<std::unique_ptr<MetadataReader>> MD =
      MetadataReader::createFromMetadata(Root);
  ASSERT_THAT_EXPECTED(MD, Succeeded());
  Record R;
  R.Hdr.Triple = "x86_64-pc-windows-msvc-msil";
  R.Hdr.LongSize = 4;
  R.Hdr.WCharSize = 2;
  R.Signatures = {sig(memberRef(2), NoPrototype, {{1, 12, 2}})};
  std::vector<std::string> Got = printLines(R, MD->get());
  ASSERT_EQ(Got.size(), 2u);
  EXPECT_EQ(Got[1], "Signature 0x0A000002 'f' NO_PROTOTYPE byval=1:12:4");
  Got = printLines(R);
  ASSERT_EQ(Got.size(), 2u);
  EXPECT_EQ(Got[1], "Signature 0x0A000002 NO_PROTOTYPE byval=1:12:4");
}

} // namespace
