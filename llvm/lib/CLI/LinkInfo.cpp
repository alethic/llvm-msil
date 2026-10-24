//===- LinkInfo.cpp - MSIL link-info record codec -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the record primitives, the encoder, the decoder (C8),
// the carrier lookup, the C1-C7 validation, the COFF symbol view and the
// printer of the link-info record v1 (plan sections 5.8.5-5.8.11).
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/LinkInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/CLI/MetadataReader.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>

#define DEBUG_TYPE "cli-linkinfo"

using namespace llvm;
using namespace llvm::cli;

using MSILLink::Record;
using MSILLink::SymbolKind;

//===----------------------------------------------------------------------===//
// Shared helpers
//===----------------------------------------------------------------------===//

/// \p V as 0x and \p Digits upper-case hex digits.
static std::string hex(uint64_t V, unsigned Digits = 8) {
  return "0x" + utohexstr(V, /*LowerCase=*/false, Digits);
}

/// A name in the canonical listing's quoting (5.7.10): raw UTF-8 between
/// single quotes; '\' -> '\\', '\'' -> '\'' and bytes 0x00-0x1F and 0x7F ->
/// '\xHH'.
static void printQuoted(raw_ostream &OS, StringRef S) {
  OS << '\'';
  for (unsigned char C : S) {
    if (C == '\\')
      OS << "\\\\";
    else if (C == '\'')
      OS << "\\'";
    else if (C < 0x20 || C == 0x7F)
      OS << "\\x" << hexdigit(C >> 4) << hexdigit(C & 0xF);
    else
      OS << C;
  }
  OS << '\'';
}

static std::string quoted(StringRef S) {
  std::string Out;
  raw_string_ostream OS(Out);
  printQuoted(OS, S);
  return Out;
}

/// An alignment given as log2, in bytes.
static void printAlign(raw_ostream &OS, unsigned Log2) {
  if (Log2 < 64)
    OS << (uint64_t(1) << Log2);
  else
    OS << "2^" << Log2;
}

/// Bytes as upper-case pairs separated by one blank.
static void printHexBytes(raw_ostream &OS, ArrayRef<uint8_t> Bytes) {
  for (size_t I = 0; I < Bytes.size(); ++I) {
    if (I)
      OS << ' ';
    OS << hexdigit(Bytes[I] >> 4) << hexdigit(Bytes[I] & 0xF);
  }
}

static bool isValidString(StringRef S) {
  if (S.contains('\0'))
    return false;
  const UTF8 *P = reinterpret_cast<const UTF8 *>(S.data());
  return isLegalUTF8String(&P, P + S.size());
}

//===----------------------------------------------------------------------===//
// RecordWriter
//===----------------------------------------------------------------------===//

void RecordWriter::writeU8(uint8_t V) { Out.push_back(V); }

void RecordWriter::writeU32(uint32_t V) {
  uint8_t B[4];
  support::endian::write32le(B, V);
  Out.append(std::begin(B), std::end(B));
}

void RecordWriter::writeULEB(uint64_t V) {
  uint8_t B[10];
  unsigned N = encodeULEB128(V, B);
  Out.append(B, B + N);
}

void RecordWriter::writeSLEB(int64_t V) {
  uint8_t B[10];
  unsigned N = encodeSLEB128(V, B);
  Out.append(B, B + N);
}

void RecordWriter::writeString(StringRef S) {
  writeULEB(S.size());
  Out.append(S.bytes_begin(), S.bytes_end());
}

void RecordWriter::writeBytes(ArrayRef<uint8_t> B) {
  Out.append(B.begin(), B.end());
}

void RecordWriter::beginSubsection(uint8_t Kind) {
  writeU8(Kind);
  Open.push_back(Out.size());
}

void RecordWriter::endSubsection() {
  assert(!Open.empty() && "endSubsection without beginSubsection");
  size_t Start = Open.pop_back_val();
  uint8_t B[10];
  unsigned N = encodeULEB128(Out.size() - Start, B);
  Out.insert(Out.begin() + Start, B, B + N);
}

//===----------------------------------------------------------------------===//
// RecordReader
//===----------------------------------------------------------------------===//

RecordReader::RecordReader(ArrayRef<uint8_t> Data, StringRef Format,
                           uint64_t BaseOffset)
    : Data(Data), Format(Format.str()), BaseOffset(BaseOffset) {}

void RecordReader::setError(const Twine &Field, uint64_t Offset,
                            const Twine &Problem) {
  if (HasError)
    return;
  HasError = true;
  Message = (Field + " at offset " + Twine(Offset) + ": " + Problem).str();
}

void RecordReader::setUnsupported(const Twine &Msg) {
  if (HasError)
    return;
  HasError = Unsupported = true;
  Message = Msg.str();
}

Error RecordReader::takeError() {
  if (!HasError)
    return Error::success();
  HasError = false;
  errc Code = Unsupported ? errc::not_supported : errc::illegal_byte_sequence;
  Unsupported = false;
  return createStringError(Code, "invalid " + Format + ": " + Message);
}

bool RecordReader::need(uint64_t Size, const Twine &Field) {
  if (HasError)
    return false;
  if (Size <= bytesLeft())
    return true;
  setError(Field, getOffset(),
           "unexpected end of data (" + Twine(Size) + " bytes needed, " +
               Twine(bytesLeft()) + " left)");
  return false;
}

uint8_t RecordReader::readU8(const Twine &Field) {
  if (!need(1, Field))
    return 0;
  return Data[Pos++];
}

uint32_t RecordReader::readU32(const Twine &Field) {
  if (!need(4, Field))
    return 0;
  uint32_t V = support::endian::read32le(Data.data() + Pos);
  Pos += 4;
  return V;
}

/// 5.8.8: a LEB128 value takes at most 10 bytes. RecordReader hands the
/// decoders of LEB128.h at most this many, so a value still continuing at
/// that bound is longer than 10 bytes, whatever follows it.
static constexpr size_t MaxLEBBytes = 10;

/// The problem of a LEB128 value that decodeULEB128 or decodeSLEB128 refused
/// after consuming \p N of the \p Avail bytes it was given: it stops before
/// the end only at a tenth byte that does not fit in 64 bits (\p TooBig).
/// The reader then moves past all Avail bytes, which it looked at.
static const char *lebProblem(unsigned N, size_t Avail, const char *TooBig) {
  if (N != Avail)
    return TooBig;
  if (Avail == MaxLEBBytes)
    return "LEB128 value longer than 10 bytes";
  return "unexpected end of data in a LEB128 value";
}

uint64_t RecordReader::readULEB(const Twine &Field) {
  if (HasError)
    return 0;
  const size_t Avail = std::min(bytesLeft(), MaxLEBBytes);
  const uint8_t *P = Data.data() + Pos;
  unsigned N = 0;
  const char *Problem = nullptr;
  uint64_t V = decodeULEB128(P, &N, P + Avail, &Problem);
  if (Problem) {
    setError(Field, getOffset(),
             lebProblem(N, Avail, "ULEB128 value does not fit in 64 bits"));
    Pos += Avail;
    return 0;
  }
  Pos += N;
  return V;
}

uint32_t RecordReader::readULEB32(const Twine &Field) {
  uint64_t Start = getOffset();
  uint64_t V = readULEB(Field);
  if (V > UINT32_MAX) {
    setError(Field, Start, "value " + Twine(V) + " does not fit in 32 bits");
    return 0;
  }
  return static_cast<uint32_t>(V);
}

int64_t RecordReader::readSLEB(const Twine &Field) {
  if (HasError)
    return 0;
  const size_t Avail = std::min(bytesLeft(), MaxLEBBytes);
  const uint8_t *P = Data.data() + Pos;
  unsigned N = 0;
  const char *Problem = nullptr;
  int64_t V = decodeSLEB128(P, &N, P + Avail, &Problem);
  if (Problem) {
    setError(Field, getOffset(),
             lebProblem(N, Avail, "SLEB128 value does not fit in 64 bits"));
    Pos += Avail;
    return 0;
  }
  Pos += N;
  return V;
}

StringRef RecordReader::readString(const Twine &Field) {
  uint64_t Start = getOffset();
  uint64_t Len = readULEB(Field);
  if (!need(Len, Field))
    return StringRef();
  StringRef S(reinterpret_cast<const char *>(Data.data() + Pos), Len);
  if (S.contains('\0')) {
    setError(Field, Start, "the string holds a NUL byte");
    return StringRef();
  }
  if (!isValidString(S)) {
    setError(Field, Start, "the string is not valid UTF-8");
    return StringRef();
  }
  Pos += Len;
  return S;
}

ArrayRef<uint8_t> RecordReader::readBytes(uint64_t Size, const Twine &Field) {
  if (!need(Size, Field))
    return {};
  ArrayRef<uint8_t> B = Data.slice(Pos, Size);
  Pos += Size;
  return B;
}

RecordReader RecordReader::readSubsection(uint8_t &Kind, const Twine &Field) {
  Kind = readU8(Field + " kind");
  uint64_t Size = readULEB(Field + " size");
  if (!need(Size, Field + " payload"))
    return RecordReader({}, Format, getOffset());
  RecordReader Sub(Data.slice(Pos, Size), Format, getOffset());
  Pos += Size;
  return Sub;
}

void RecordReader::expectEnd(const Twine &Field) {
  if (HasError || empty())
    return;
  setError(Field, getOffset(), Twine(bytesLeft()) + " bytes left over");
}

//===----------------------------------------------------------------------===//
// Names of the record's values
//===----------------------------------------------------------------------===//

static constexpr uint64_t KnownHeaderFlags = MSILLink::HasDebugInfo;
static constexpr uint64_t KnownSymbolFlags =
    MSILLink::BindingMask | MSILLink::VisibilityMask | MSILLink::Undefined |
    MSILLink::DLLExport | MSILLink::ExplicitImport | MSILLink::NoStrip |
    MSILLink::ThreadLocal | MSILLink::Alias | MSILLink::DLLImport |
    MSILLink::Import;
static_assert(KnownSymbolFlags == 0xFFF,
              "v1.0's SymbolFlags are the twelve low bits (5.8.8)");
static constexpr uint64_t KnownSegmentFlags =
    MSILLink::SegStrings | MSILLink::SegTLS | MSILLink::SegRetain |
    MSILLink::SegZero | MSILLink::SegReadOnly;
static constexpr uint64_t KnownCallFlags =
    MSILLink::NoPrototype | MSILLink::VarArg | MSILLink::Sret;
/// The largest log2 alignment (5.8.8, SEGMENTS) and the size limit of a
/// segment.
static constexpr unsigned MaxAlignLog2 = 30;
static constexpr uint64_t SegmentSizeLimit = uint64_t(1) << 31;

static const char *subsectionName(uint8_t Kind) {
  switch (static_cast<MSILLink::SubsectionKind>(Kind)) {
  case MSILLink::SubsectionKind::SymTab:
    return "SYMTAB";
  case MSILLink::SubsectionKind::Segments:
    return "SEGMENTS";
  case MSILLink::SubsectionKind::DataRelocs:
    return "DATA_RELOCS";
  case MSILLink::SubsectionKind::Comdats:
    return "COMDATS";
  case MSILLink::SubsectionKind::InitFini:
    return "INIT_FINI";
  case MSILLink::SubsectionKind::Signatures:
    return "SIGNATURES";
  case MSILLink::SubsectionKind::LinkerOptions:
    return "LINKER_OPTIONS";
  case MSILLink::SubsectionKind::ManagedRequests:
    return "MANAGED_REQUESTS";
  case MSILLink::SubsectionKind::Types:
    return "TYPES";
  default:
    return nullptr;
  }
}

static const char *bindingName(uint32_t Flags) {
  switch (Flags & MSILLink::BindingMask) {
  case MSILLink::BindingGlobal:
    return "GLOBAL";
  case MSILLink::BindingWeak:
    return "WEAK";
  case MSILLink::BindingLocal:
    return "LOCAL";
  default:
    return "COMMON";
  }
}

static const char *visibilityName(uint32_t Flags) {
  switch ((Flags & MSILLink::VisibilityMask) >> MSILLink::VisibilityShift) {
  case 0:
    return "DEFAULT";
  case 1:
    return "INTERNAL";
  case 2:
    return "HIDDEN";
  default:
    return "PROTECTED";
  }
}

/// The SymbolFlags bits other than BINDING and VISIBILITY, in bit order.
static constexpr std::pair<uint32_t, const char *> SymbolFlagNames[] = {
    {MSILLink::Undefined, "UNDEFINED"},
    {MSILLink::DLLExport, "DLLEXPORT"},
    {MSILLink::ExplicitImport, "EXPLICIT_IMPORT"},
    {MSILLink::NoStrip, "NO_STRIP"},
    {MSILLink::ThreadLocal, "TLS"},
    {MSILLink::Alias, "ALIAS"},
    {MSILLink::DLLImport, "DLLIMPORT"},
    {MSILLink::Import, "IMPORT"}};

static constexpr std::pair<uint32_t, const char *> SegmentFlagNames[] = {
    {MSILLink::SegStrings, "STRINGS"},
    {MSILLink::SegTLS, "TLS"},
    {MSILLink::SegRetain, "RETAIN"},
    {MSILLink::SegZero, "ZERO"},
    {MSILLink::SegReadOnly, "READONLY"}};

static const char *relocName(MSILLink::RelocType T) {
  switch (T) {
  case MSILLink::RelocType::Addr64:
    return "ADDR64";
  case MSILLink::RelocType::Addr32:
    return "ADDR32";
  case MSILLink::RelocType::FnAddr64:
    return "FNADDR64";
  case MSILLink::RelocType::FnAddr32:
    return "FNADDR32";
  case MSILLink::RelocType::Diff:
    return "DIFF";
  case MSILLink::RelocType::TLSOffset32:
    return "TLS_OFFSET32";
  case MSILLink::RelocType::TLSOffset64:
    return "TLS_OFFSET64";
  }
  return nullptr;
}

/// The size of a relocation site; 0 for an unknown type.
static uint64_t relocSiteSize(const MSILLink::Reloc &X) {
  switch (X.Type) {
  case MSILLink::RelocType::Addr64:
  case MSILLink::RelocType::FnAddr64:
  case MSILLink::RelocType::TLSOffset64:
    return 8;
  case MSILLink::RelocType::Addr32:
  case MSILLink::RelocType::FnAddr32:
  case MSILLink::RelocType::TLSOffset32:
    return 4;
  case MSILLink::RelocType::Diff:
    return X.Width;
  }
  return 0;
}

static const char *selectionName(MSILLink::ComdatSelection S) {
  switch (S) {
  case MSILLink::ComdatSelection::Any:
    return "ANY";
  case MSILLink::ComdatSelection::ExactMatch:
    return "EXACT_MATCH";
  case MSILLink::ComdatSelection::Largest:
    return "LARGEST";
  case MSILLink::ComdatSelection::NoDeduplicate:
    return "NO_DEDUPLICATE";
  case MSILLink::ComdatSelection::SameSize:
    return "SAME_SIZE";
  }
  return nullptr;
}

static bool isUndefined(const MSILLink::Symbol &S) {
  return S.Flags & MSILLink::Undefined;
}
static bool isAlias(const MSILLink::Symbol &S) {
  return S.Flags & MSILLink::Alias;
}
static bool isCommon(const MSILLink::Symbol &S) {
  return (S.Flags & MSILLink::BindingMask) == MSILLink::BindingCommon;
}
static bool isData(const MSILLink::Symbol &S) {
  return S.Kind == SymbolKind::Data;
}
/// The symbol has a Token field: a defined non-alias symbol.
static bool hasToken(const MSILLink::Symbol &S) {
  return !isUndefined(S) && !isAlias(S);
}
/// The symbol has Segment, Offset and Size fields.
static bool hasPlacement(const MSILLink::Symbol &S) {
  return isData(S) && hasToken(S) && !isCommon(S);
}

//===----------------------------------------------------------------------===//
// encodeLinkInfo
//===----------------------------------------------------------------------===//

void cli::encodeLinkInfo(const Record &R, SmallVectorImpl<uint8_t> &Out) {
  RecordWriter W(Out);
  const MSILLink::Header &H = R.Hdr;
  W.writeBytes(arrayRefFromStringRef(
      StringRef(MSILLink::Magic, sizeof(MSILLink::Magic))));
  W.writeU8(H.Major);
  W.writeU8(H.Minor);
  W.writeULEB(H.Flags);
  W.writeString(H.Triple);
  W.writeULEB(H.ABIVersion);
  W.writeULEB(H.ABIFlags);
  W.writeULEB(H.FeatureFlags);
  W.writeU8(H.PointerSize);
  W.writeU8(H.LongSize);
  W.writeU8(H.WCharSize);
  W.writeU8(H.LongDoubleSize);

  if (!R.Symbols.empty()) {
    W.beginSubsection(static_cast<uint8_t>(MSILLink::SubsectionKind::SymTab));
    W.writeULEB(R.Symbols.size());
    for (const MSILLink::Symbol &S : R.Symbols) {
      W.writeU8(static_cast<uint8_t>(S.Kind));
      W.writeULEB(S.Flags);
      W.writeString(S.Name);
      if (hasToken(S))
        W.writeU32(S.Token);
      if (hasPlacement(S)) {
        W.writeULEB(S.Segment);
        W.writeULEB(S.Offset);
        W.writeULEB(S.Size);
      }
      if (isAlias(S)) {
        W.writeULEB(S.Aliasee);
        if (isData(S)) {
          W.writeSLEB(S.AliasOffset);
          W.writeULEB(S.Size);
        }
      }
      if (isCommon(S)) {
        W.writeULEB(S.CommonSize);
        W.writeU8(S.CommonAlignLog2);
      }
      if (S.Flags & MSILLink::Import) {
        W.writeString(S.ImportModule);
        W.writeString(S.ImportName);
      }
    }
    W.endSubsection();
  }

  if (!R.Segments.empty()) {
    W.beginSubsection(static_cast<uint8_t>(MSILLink::SubsectionKind::Segments));
    W.writeULEB(R.Segments.size());
    for (const MSILLink::Segment &S : R.Segments) {
      W.writeString(S.Name);
      W.writeULEB(S.Flags);
      W.writeU8(S.AlignLog2);
      W.writeULEB(S.Size);
      if (S.Flags & MSILLink::SegZero)
        continue;
      assert(S.Bytes.size() == S.Size &&
             "a segment with bytes holds exactly Size bytes");
      // Exactly Size bytes, whatever the model holds.
      ArrayRef<uint8_t> Bytes = ArrayRef<uint8_t>(S.Bytes).take_front(S.Size);
      W.writeBytes(Bytes);
      for (uint64_t I = Bytes.size(); I < S.Size; ++I)
        W.writeU8(0);
    }
    W.endSubsection();
  }

  if (!R.Relocs.empty()) {
    W.beginSubsection(
        static_cast<uint8_t>(MSILLink::SubsectionKind::DataRelocs));
    W.writeULEB(R.Relocs.size());
    for (const MSILLink::Reloc &X : R.Relocs) {
      W.writeU8(static_cast<uint8_t>(X.Type));
      W.writeULEB(X.Segment);
      W.writeULEB(X.Offset);
      W.writeULEB(X.Symbol);
      W.writeSLEB(X.Addend);
      if (X.Type == MSILLink::RelocType::Diff) {
        W.writeU8(X.Width);
        W.writeULEB(X.Symbol2);
      }
    }
    W.endSubsection();
  }

  if (!R.Comdats.empty()) {
    W.beginSubsection(static_cast<uint8_t>(MSILLink::SubsectionKind::Comdats));
    W.writeULEB(R.Comdats.size());
    for (const MSILLink::ComdatGroup &G : R.Comdats) {
      W.writeString(G.Name);
      W.writeU8(static_cast<uint8_t>(G.Selection));
      W.writeULEB(G.Members.size());
      for (const auto &[Kind, Index] : G.Members) {
        W.writeU8(static_cast<uint8_t>(Kind));
        W.writeULEB(Index);
      }
    }
    W.endSubsection();
  }

  if (!R.Inits.empty()) {
    W.beginSubsection(static_cast<uint8_t>(MSILLink::SubsectionKind::InitFini));
    W.writeULEB(R.Inits.size());
    for (const MSILLink::InitEntry &I : R.Inits) {
      W.writeU8(static_cast<uint8_t>(I.Kind));
      W.writeULEB(I.Priority);
      W.writeULEB(I.Symbol);
      W.writeULEB(I.ComdatPlusOne);
    }
    W.endSubsection();
  }

  if (!R.Signatures.empty()) {
    W.beginSubsection(
        static_cast<uint8_t>(MSILLink::SubsectionKind::Signatures));
    W.writeULEB(R.Signatures.size());
    for (const MSILLink::SignatureInfo &S : R.Signatures) {
      W.writeU32(S.Token);
      W.writeULEB(S.Flags);
      if (S.Flags & MSILLink::VarArg)
        W.writeULEB(S.FixedParams);
      if (S.Flags & MSILLink::Sret) {
        W.writeULEB(S.SretSize);
        W.writeU8(S.SretAlignLog2);
      }
      W.writeULEB(S.Byval.size());
      for (const MSILLink::ByvalParam &B : S.Byval) {
        W.writeULEB(B.Param);
        W.writeULEB(B.Size);
        W.writeU8(B.AlignLog2);
      }
    }
    W.endSubsection();
  }

  if (!R.LinkerOptions.empty()) {
    W.beginSubsection(
        static_cast<uint8_t>(MSILLink::SubsectionKind::LinkerOptions));
    W.writeULEB(R.LinkerOptions.size());
    for (const MSILLink::LinkerOption &L : R.LinkerOptions) {
      W.writeU8(static_cast<uint8_t>(L.Kind));
      if (L.Kind == MSILLink::LinkerOptionKind::DependentLib) {
        assert(L.Args.size() == 1 && "a DEPENDENT_LIB entry names one library");
        W.writeString(L.Args.empty() ? StringRef() : StringRef(L.Args[0]));
        continue;
      }
      W.writeULEB(L.Args.size());
      for (const std::string &A : L.Args)
        W.writeString(A);
    }
    W.endSubsection();
  }

  // The kept optional subsections, in kind order (a stable sort keeps equal
  // records equal).
  SmallVector<const std::pair<uint8_t, std::vector<uint8_t>> *, 4> Optional;
  for (const auto &P : R.SkippedOptional) {
    assert(P.first >= uint8_t(MSILLink::SubsectionKind::FirstOptional) &&
           P.first <= uint8_t(MSILLink::SubsectionKind::LastOptional) &&
           "only kinds 0x40-0x7F are skippable");
    Optional.push_back(&P);
  }
  llvm::stable_sort(Optional, [](const auto *A, const auto *B) {
    return A->first < B->first;
  });
  for (const auto *P : Optional) {
    W.beginSubsection(P->first);
    W.writeBytes(P->second);
    W.endSubsection();
  }
}

//===----------------------------------------------------------------------===//
// decodeLinkInfo
//===----------------------------------------------------------------------===//

namespace {

/// Decodes one record: the framing and the per-field checks while reading,
/// then the checks between subsections (index ranges, segment bounds,
/// relocation sites and targets, INIT_FINI symbols).
class LinkInfoDecoder {
public:
  explicit LinkInfoDecoder(ArrayRef<uint8_t> Bytes) : Rd(Bytes) {}

  Expected<Record> decode();

private:
  /// The feature error of 5.8.9 on \p Rd.
  void unsupported(RecordReader &Rd, const Twine &Feature) {
    Rd.setUnsupported("v1." + Twine(unsigned(R.Hdr.Minor)) + " uses " +
                      Feature + ", which this toolchain does not support");
  }
  /// Reads a count of entries of at least one byte each.
  uint32_t readCount(RecordReader &P, const Twine &Field) {
    uint64_t Off = P.getOffset();
    uint32_t N = P.readULEB32(Field);
    if (!P.hasError() && N > P.bytesLeft())
      P.setError(Field, Off,
                 Twine(N) + " entries cannot fit in the " +
                     Twine(P.bytesLeft()) + " bytes left");
    return P.hasError() ? 0 : N;
  }
  /// Reads a log2 alignment of at most MaxAlignLog2.
  uint8_t readAlign(RecordReader &P, const Twine &Field) {
    uint64_t Off = P.getOffset();
    uint8_t A = P.readU8(Field);
    if (!P.hasError() && A > MaxAlignLog2)
      P.setError(Field, Off,
                 "alignment 2^" + Twine(unsigned(A)) + " is above 2^" +
                     Twine(MaxAlignLog2));
    return A;
  }

  void header();
  void symbols(RecordReader &P);
  void segments(RecordReader &P);
  void relocs(RecordReader &P);
  void comdats(RecordReader &P);
  void inits(RecordReader &P);
  void signatures(RecordReader &P);
  void linkerOptions(RecordReader &P);
  void checkSymbols();
  void checkRelocs();
  void checkComdats();
  void checkInits();

  std::string symbolText(uint32_t I) const {
    return "symbol " + std::to_string(I) + " " + quoted(R.Symbols[I].Name);
  }

  RecordReader Rd;
  Record R;
  /// The offset of each entry, for messages.
  std::vector<uint64_t> SymbolOffsets, SegmentOffsets, RelocOffsets,
      ComdatOffsets, InitOffsets;
};

} // namespace

void LinkInfoDecoder::header() {
  ArrayRef<uint8_t> M = Rd.readBytes(4, "magic");
  if (Rd.hasError())
    return;
  if (M != arrayRefFromStringRef(
               StringRef(MSILLink::Magic, sizeof(MSILLink::Magic)))) {
    std::string Found;
    raw_string_ostream OS(Found);
    printHexBytes(OS, M);
    Rd.setError("magic", 0, "expected 4D 53 4C 4B (\"MSLK\"), found " + Found);
    return;
  }
  R.Hdr.Major = Rd.readU8("major version");
  if (Rd.hasError())
    return;
  if (R.Hdr.Major > MSILLink::MajorVersion) {
    Rd.setUnsupported("version " + Twine(unsigned(R.Hdr.Major)) +
                      " is newer than this toolchain supports (" +
                      Twine(unsigned(MSILLink::MajorVersion)) + ")");
    return;
  }
  if (R.Hdr.Major != MSILLink::MajorVersion) {
    Rd.setError("major version", 4,
                "version " + Twine(unsigned(R.Hdr.Major)) +
                    " is not supported (this toolchain reads version " +
                    Twine(unsigned(MSILLink::MajorVersion)) + ")");
    return;
  }
  R.Hdr.Minor = Rd.readU8("minor version");
  uint64_t FlagsOff = Rd.getOffset();
  uint64_t Flags = Rd.readULEB("header flags");
  if (Rd.hasError())
    return;
  if (Flags & ~KnownHeaderFlags) {
    unsupported(Rd, "header flags " + hex(Flags & ~KnownHeaderFlags, 0) +
                        " (at offset " + Twine(FlagsOff) + ")");
    return;
  }
  R.Hdr.Flags = static_cast<uint32_t>(Flags);
  R.Hdr.Triple = Rd.readString("triple").str();
  R.Hdr.ABIVersion = Rd.readULEB32("ABI version");
  uint64_t ABIFlagsOff = Rd.getOffset();
  R.Hdr.ABIFlags = Rd.readULEB("ABI flags");
  R.Hdr.FeatureFlags = Rd.readULEB("feature flags");
  if (Rd.hasError())
    return;
  // ABI flags are must-understand and Phase 2 defines none (5.8.8), so every
  // bit is unknown. Feature flags are codegen switches that do not change the
  // ABI and that msil-ld ORs (5.8.8), so they are not must-understand: every
  // value is accepted and kept, and encodeLinkInfo writes it back unchanged.
  if (R.Hdr.ABIFlags) {
    unsupported(Rd, "ABI flags " + hex(R.Hdr.ABIFlags, 0) + " (at offset " +
                        Twine(ABIFlagsOff) + ")");
    return;
  }
  R.Hdr.PointerSize = Rd.readU8("data model (pointer size)");
  R.Hdr.LongSize = Rd.readU8("data model (long size)");
  R.Hdr.WCharSize = Rd.readU8("data model (wchar_t size)");
  R.Hdr.LongDoubleSize = Rd.readU8("data model (long double size)");
}

void LinkInfoDecoder::symbols(RecordReader &P) {
  uint32_t N = readCount(P, "SYMTAB count");
  R.Symbols.reserve(N);
  StringMap<uint32_t> Names;
  for (uint32_t I = 0; I < N && !P.hasError(); ++I) {
    const uint64_t Off = P.getOffset();
    const std::string F = "symbol " + std::to_string(I);
    SymbolOffsets.push_back(Off);
    MSILLink::Symbol S;
    uint8_t Kind = P.readU8(F + " kind");
    uint64_t Flags = P.readULEB(F + " flags");
    if (P.hasError())
      return;
    if (Kind > static_cast<uint8_t>(SymbolKind::Data))
      return unsupported(P, "symbol kind " + Twine(unsigned(Kind)) + " (" + F +
                                " at offset " + Twine(Off) + ")");
    if (Flags & ~KnownSymbolFlags)
      return unsupported(P, "symbol flags " +
                                hex(Flags & ~KnownSymbolFlags, 0) + " (" + F +
                                " at offset " + Twine(Off) + ")");
    S.Kind = static_cast<SymbolKind>(Kind);
    S.Flags = static_cast<uint32_t>(Flags);
    S.Name = P.readString(F + " name").str();
    if (P.hasError())
      return;
    const std::string FN = F + " " + quoted(S.Name);

    // C8: flag combinations.
    const bool Fn = !isData(S), Undef = isUndefined(S), IsAlias = isAlias(S),
               Common = isCommon(S);
    const uint32_t Binding = S.Flags & MSILLink::BindingMask;
    const char *Bad = nullptr;
    if (Common && Fn)
      Bad = "COMMON on a FUNCTION symbol";
    else if (Common && Undef)
      Bad = "COMMON with UNDEFINED";
    else if (Common && IsAlias)
      Bad = "COMMON with ALIAS";
    else if (Undef && Binding == MSILLink::BindingLocal)
      Bad = "LOCAL with UNDEFINED";
    else if (Undef && IsAlias)
      Bad = "ALIAS with UNDEFINED (an alias is a definition)";
    else if ((S.Flags & MSILLink::ExplicitImport) && !(Fn && Undef))
      Bad = "EXPLICIT_IMPORT on a symbol that is not an undefined FUNCTION";
    else if ((S.Flags & MSILLink::Import) && !(!Fn && Undef))
      Bad = "IMPORT on a symbol that is not undefined DATA";
    else if ((S.Flags & MSILLink::Import) && (S.Flags & MSILLink::ThreadLocal))
      Bad = "IMPORT with TLS";
    else if ((S.Flags & MSILLink::DLLImport) && !Undef)
      Bad = "DLLIMPORT on a definition";
    else if ((S.Flags & MSILLink::NoStrip) && Undef)
      Bad = "NO_STRIP with UNDEFINED";
    else if ((S.Flags & MSILLink::ThreadLocal) && Fn)
      Bad = "TLS on a FUNCTION symbol";
    if (Bad)
      return P.setError(FN, Off, Bad);

    // C8: names.
    if (S.Name.empty())
      return P.setError(F, Off, "the name is empty");
    if (StringRef(S.Name).starts_with("<msil>"))
      return P.setError(FN, Off, "names starting with <msil> are reserved");
    auto [It, New] = Names.try_emplace(S.Name, I);
    if (!New)
      return P.setError(FN, Off,
                        "the name is also symbol " + Twine(It->second) + "'s");

    if (hasToken(S))
      S.Token = P.readU32(FN + " token");
    if (hasPlacement(S)) {
      S.Segment = P.readULEB32(FN + " segment");
      S.Offset = P.readULEB(FN + " offset");
      S.Size = P.readULEB(FN + " size");
    }
    if (IsAlias) {
      S.Aliasee = P.readULEB32(FN + " aliasee");
      if (!Fn) {
        S.AliasOffset = P.readSLEB(FN + " alias offset");
        S.Size = P.readULEB(FN + " size");
      }
    }
    if (Common) {
      uint64_t SizeOff = P.getOffset();
      S.CommonSize = P.readULEB(FN + " common size");
      if (!P.hasError() && S.CommonSize >= SegmentSizeLimit)
        return P.setError(FN + " common size", SizeOff,
                          "size " + Twine(S.CommonSize) + " is not below 2^31");
      S.CommonAlignLog2 = readAlign(P, FN + " common alignment");
    }
    if (S.Flags & MSILLink::Import) {
      S.ImportModule = P.readString(FN + " import module").str();
      uint64_t NameOff = P.getOffset();
      S.ImportName = P.readString(FN + " import name").str();
      if (!P.hasError() && S.ImportName.empty())
        return P.setError(FN + " import name", NameOff,
                          "an IMPORT symbol needs a non-empty import name");
    }
    R.Symbols.push_back(std::move(S));
  }
}

void LinkInfoDecoder::segments(RecordReader &P) {
  uint32_t N = readCount(P, "SEGMENTS count");
  R.Segments.reserve(N);
  for (uint32_t I = 0; I < N && !P.hasError(); ++I) {
    const uint64_t Off = P.getOffset();
    const std::string F = "segment " + std::to_string(I);
    SegmentOffsets.push_back(Off);
    MSILLink::Segment S;
    S.Name = P.readString(F + " name").str();
    uint64_t Flags = P.readULEB(F + " flags");
    if (P.hasError())
      return;
    if (Flags & ~KnownSegmentFlags)
      return unsupported(P, "segment flags " +
                                hex(Flags & ~KnownSegmentFlags, 0) + " (" + F +
                                " at offset " + Twine(Off) + ")");
    S.Flags = static_cast<uint32_t>(Flags);
    S.AlignLog2 = readAlign(P, F + " alignment");
    uint64_t SizeOff = P.getOffset();
    S.Size = P.readULEB(F + " size");
    if (P.hasError())
      return;
    if (S.Size >= SegmentSizeLimit)
      return P.setError(F + " size", SizeOff,
                        "size " + Twine(S.Size) + " is not below 2^31");
    if (!(S.Flags & MSILLink::SegZero)) {
      ArrayRef<uint8_t> B = P.readBytes(S.Size, F + " bytes");
      S.Bytes.assign(B.begin(), B.end());
    }
    R.Segments.push_back(std::move(S));
  }
}

void LinkInfoDecoder::relocs(RecordReader &P) {
  uint32_t N = readCount(P, "DATA_RELOCS count");
  R.Relocs.reserve(N);
  for (uint32_t I = 0; I < N && !P.hasError(); ++I) {
    const uint64_t Off = P.getOffset();
    const std::string F = "relocation " + std::to_string(I);
    RelocOffsets.push_back(Off);
    MSILLink::Reloc X;
    uint8_t Type = P.readU8(F + " type");
    if (P.hasError())
      return;
    if (Type > static_cast<uint8_t>(MSILLink::RelocType::TLSOffset64))
      return unsupported(P, "relocation type " + Twine(unsigned(Type)) + " (" +
                                F + " at offset " + Twine(Off) + ")");
    X.Type = static_cast<MSILLink::RelocType>(Type);
    X.Segment = P.readULEB32(F + " segment");
    X.Offset = P.readULEB(F + " offset");
    X.Symbol = P.readULEB32(F + " symbol");
    X.Addend = P.readSLEB(F + " addend");
    if (X.Type == MSILLink::RelocType::Diff) {
      X.Width = P.readU8(F + " width");
      X.Symbol2 = P.readULEB32(F + " symbol2");
    }
    R.Relocs.push_back(X);
  }
}

void LinkInfoDecoder::comdats(RecordReader &P) {
  uint32_t N = readCount(P, "COMDATS count");
  R.Comdats.reserve(N);
  for (uint32_t I = 0; I < N && !P.hasError(); ++I) {
    const uint64_t Off = P.getOffset();
    const std::string F = "comdat " + std::to_string(I);
    ComdatOffsets.push_back(Off);
    MSILLink::ComdatGroup G;
    G.Name = P.readString(F + " name").str();
    uint8_t Sel = P.readU8(F + " selection");
    if (P.hasError())
      return;
    if (Sel > static_cast<uint8_t>(MSILLink::ComdatSelection::SameSize))
      return unsupported(P, "comdat selection " + Twine(unsigned(Sel)) + " (" +
                                F + " at offset " + Twine(Off) + ")");
    G.Selection = static_cast<MSILLink::ComdatSelection>(Sel);
    uint32_t M = readCount(P, F + " member count");
    G.Members.reserve(M);
    for (uint32_t J = 0; J < M && !P.hasError(); ++J) {
      uint64_t MOff = P.getOffset();
      uint8_t Kind = P.readU8(F + " member " + Twine(J) + " kind");
      if (P.hasError())
        return;
      if (Kind > static_cast<uint8_t>(MSILLink::ComdatMemberKind::Segment))
        return unsupported(P, "comdat member kind " + Twine(unsigned(Kind)) +
                                  " (" + F + " at offset " + Twine(MOff) + ")");
      uint32_t Index = P.readULEB32(F + " member " + Twine(J) + " index");
      G.Members.emplace_back(static_cast<MSILLink::ComdatMemberKind>(Kind),
                             Index);
    }
    R.Comdats.push_back(std::move(G));
  }
}

void LinkInfoDecoder::inits(RecordReader &P) {
  uint32_t N = readCount(P, "INIT_FINI count");
  R.Inits.reserve(N);
  for (uint32_t I = 0; I < N && !P.hasError(); ++I) {
    const uint64_t Off = P.getOffset();
    const std::string F = "init entry " + std::to_string(I);
    InitOffsets.push_back(Off);
    MSILLink::InitEntry E;
    uint8_t Kind = P.readU8(F + " kind");
    if (P.hasError())
      return;
    if (Kind > static_cast<uint8_t>(MSILLink::InitKind::Fini))
      return unsupported(P, "init kind " + Twine(unsigned(Kind)) + " (" + F +
                                " at offset " + Twine(Off) + ")");
    E.Kind = static_cast<MSILLink::InitKind>(Kind);
    uint64_t PrioOff = P.getOffset();
    E.Priority = P.readULEB32(F + " priority");
    if (!P.hasError() && E.Priority > 65535)
      return P.setError(F + " priority", PrioOff,
                        "priority " + Twine(E.Priority) + " is above 65535");
    E.Symbol = P.readULEB32(F + " symbol");
    E.ComdatPlusOne = P.readULEB32(F + " comdat");
    R.Inits.push_back(E);
  }
}

void LinkInfoDecoder::signatures(RecordReader &P) {
  uint32_t N = readCount(P, "SIGNATURES count");
  R.Signatures.reserve(N);
  DenseMap<uint32_t, uint32_t> Tokens;
  for (uint32_t I = 0; I < N && !P.hasError(); ++I) {
    const uint64_t Off = P.getOffset();
    const std::string F = "signature " + std::to_string(I);
    MSILLink::SignatureInfo S;
    S.Token = P.readU32(F + " token");
    uint64_t Flags = P.readULEB(F + " flags");
    if (P.hasError())
      return;
    if (Flags & ~KnownCallFlags)
      return unsupported(P, "call flags " + hex(Flags & ~KnownCallFlags, 0) +
                                " (" + F + " at offset " + Twine(Off) + ")");
    auto [It, New] = Tokens.try_emplace(S.Token, I);
    if (!New)
      return P.setError(F, Off,
                        "token " + hex(S.Token) +
                            " already has SIGNATURES entry " +
                            Twine(It->second));
    S.Flags = static_cast<uint32_t>(Flags);
    if (S.Flags & MSILLink::VarArg)
      S.FixedParams = P.readULEB32(F + " fixed parameters");
    if (S.Flags & MSILLink::Sret) {
      S.SretSize = P.readULEB(F + " sret size");
      S.SretAlignLog2 = readAlign(P, F + " sret alignment");
    }
    uint32_t NB = readCount(P, F + " byval count");
    S.Byval.reserve(NB);
    for (uint32_t J = 0; J < NB && !P.hasError(); ++J) {
      MSILLink::ByvalParam B;
      const std::string FB = F + " byval " + std::to_string(J);
      B.Param = P.readULEB32(FB + " parameter");
      B.Size = P.readULEB(FB + " size");
      B.AlignLog2 = readAlign(P, FB + " alignment");
      S.Byval.push_back(B);
    }
    R.Signatures.push_back(std::move(S));
  }
}

void LinkInfoDecoder::linkerOptions(RecordReader &P) {
  uint32_t N = readCount(P, "LINKER_OPTIONS count");
  R.LinkerOptions.reserve(N);
  for (uint32_t I = 0; I < N && !P.hasError(); ++I) {
    const uint64_t Off = P.getOffset();
    const std::string F = "linker option " + std::to_string(I);
    MSILLink::LinkerOption L;
    uint8_t Kind = P.readU8(F + " kind");
    if (P.hasError())
      return;
    if (Kind > static_cast<uint8_t>(MSILLink::LinkerOptionKind::LinkerOption))
      return unsupported(P, "linker option kind " + Twine(unsigned(Kind)) +
                                " (" + F + " at offset " + Twine(Off) + ")");
    L.Kind = static_cast<MSILLink::LinkerOptionKind>(Kind);
    if (L.Kind == MSILLink::LinkerOptionKind::DependentLib) {
      L.Args.push_back(P.readString(F + " library").str());
    } else {
      uint32_t NA = readCount(P, F + " argument count");
      L.Args.reserve(NA);
      for (uint32_t J = 0; J < NA && !P.hasError(); ++J)
        L.Args.push_back(
            P.readString(F + " argument " + std::to_string(J)).str());
    }
    R.LinkerOptions.push_back(std::move(L));
  }
}

void LinkInfoDecoder::checkSymbols() {
  const size_t NumSegments = R.Segments.size();
  const size_t NumSymbols = R.Symbols.size();
  // Defined data first: aliases are checked against their aliasee's place.
  for (uint32_t I = 0; I < NumSymbols && !Rd.hasError(); ++I) {
    const MSILLink::Symbol &S = R.Symbols[I];
    if (!hasPlacement(S))
      continue;
    if (S.Segment >= NumSegments) {
      Rd.setError(symbolText(I), SymbolOffsets[I],
                  "segment " + Twine(S.Segment) + " is out of range (" +
                      Twine(NumSegments) + " segments)");
      return;
    }
    uint64_t SegSize = R.Segments[S.Segment].Size;
    if (S.Offset > SegSize || S.Size > SegSize - S.Offset) {
      Rd.setError(symbolText(I), SymbolOffsets[I],
                  "offset " + Twine(S.Offset) + " and size " + Twine(S.Size) +
                      " lie outside segment " + Twine(S.Segment) + " (" +
                      Twine(SegSize) + " bytes)");
      return;
    }
  }
  for (uint32_t I = 0; I < NumSymbols && !Rd.hasError(); ++I) {
    const MSILLink::Symbol &S = R.Symbols[I];
    if (!isAlias(S))
      continue;
    auto Fail = [&](const Twine &Problem) {
      Rd.setError(symbolText(I), SymbolOffsets[I], Problem);
    };
    if (S.Aliasee >= NumSymbols)
      return Fail("aliasee " + Twine(S.Aliasee) + " is out of range (" +
                  Twine(NumSymbols) + " symbols)");
    const MSILLink::Symbol &A = R.Symbols[S.Aliasee];
    if (isAlias(A))
      return Fail("aliasee " + Twine(S.Aliasee) + " is an alias");
    if (isUndefined(A))
      return Fail("aliasee " + Twine(S.Aliasee) + " is undefined");
    if (A.Kind != S.Kind)
      return Fail("aliasee " + Twine(S.Aliasee) + " is of the other kind");
    if (!isData(S))
      continue;
    if (isCommon(A))
      return Fail("aliasee " + Twine(S.Aliasee) +
                  " is a COMMON symbol, which has no place");
    // The alias's bytes lie in its aliasee's segment.
    uint64_t SegSize = R.Segments[A.Segment].Size;
    bool Inside = false;
    uint64_t Start = 0;
    if (S.AliasOffset >= 0) {
      Start = A.Offset + static_cast<uint64_t>(S.AliasOffset);
      Inside = Start >= A.Offset;
    } else {
      uint64_t Back = 0 - static_cast<uint64_t>(S.AliasOffset);
      Inside = Back <= A.Offset;
      Start = A.Offset - Back;
    }
    if (!Inside || Start > SegSize || S.Size > SegSize - Start)
      return Fail("alias offset " + Twine(S.AliasOffset) + " and size " +
                  Twine(S.Size) + " lie outside segment " + Twine(A.Segment) +
                  " of aliasee " + Twine(S.Aliasee));
  }
}

void LinkInfoDecoder::checkRelocs() {
  const size_t NumSegments = R.Segments.size();
  const size_t NumSymbols = R.Symbols.size();
  for (uint32_t I = 0; I < R.Relocs.size() && !Rd.hasError(); ++I) {
    const MSILLink::Reloc &X = R.Relocs[I];
    auto Fail = [&](const Twine &Problem) {
      Rd.setError("relocation " + Twine(I), RelocOffsets[I], Problem);
    };
    if (X.Segment >= NumSegments)
      return Fail("segment " + Twine(X.Segment) + " is out of range (" +
                  Twine(NumSegments) + " segments)");
    const MSILLink::Segment &Seg = R.Segments[X.Segment];
    if (Seg.Flags & MSILLink::SegZero)
      return Fail("segment " + Twine(X.Segment) + " is a ZERO segment");
    if (X.Type == MSILLink::RelocType::Diff && (X.Width < 1 || X.Width > 16))
      return Fail("DIFF width " + Twine(unsigned(X.Width)) + " is not 1 to 16");
    uint64_t Size = relocSiteSize(X);
    if (X.Offset > Seg.Size || Size > Seg.Size - X.Offset)
      return Fail("the " + Twine(Size) + "-byte site at offset " +
                  Twine(X.Offset) + " lies outside segment " +
                  Twine(X.Segment) + " (" + Twine(Seg.Size) + " bytes)");
    if (I > 0) {
      const MSILLink::Reloc &Prev = R.Relocs[I - 1];
      if (X.Segment < Prev.Segment ||
          (X.Segment == Prev.Segment && X.Offset <= Prev.Offset))
        return Fail("relocations are not sorted by (segment, offset)");
      if (X.Segment == Prev.Segment &&
          Prev.Offset + relocSiteSize(Prev) > X.Offset)
        return Fail("the site overlaps relocation " + Twine(I - 1) + "'s");
    }
    if (X.Symbol >= NumSymbols)
      return Fail("symbol " + Twine(X.Symbol) + " is out of range (" +
                  Twine(NumSymbols) + " symbols)");
    const MSILLink::Symbol &S = R.Symbols[X.Symbol];
    switch (X.Type) {
    case MSILLink::RelocType::Addr64:
    case MSILLink::RelocType::Addr32:
      if (!isData(S))
        return Fail(Twine(relocName(X.Type)) + " names FUNCTION " +
                    symbolText(X.Symbol));
      break;
    case MSILLink::RelocType::FnAddr64:
    case MSILLink::RelocType::FnAddr32:
      if (isData(S))
        return Fail(Twine(relocName(X.Type)) + " names DATA " +
                    symbolText(X.Symbol));
      if (X.Addend != 0)
        return Fail(Twine(relocName(X.Type)) + " has addend " +
                    Twine(X.Addend) + " (it must be 0)");
      break;
    case MSILLink::RelocType::Diff:
      if (!isData(S))
        return Fail("DIFF names FUNCTION " + symbolText(X.Symbol));
      if (X.Symbol2 >= NumSymbols)
        return Fail("symbol2 " + Twine(X.Symbol2) + " is out of range (" +
                    Twine(NumSymbols) + " symbols)");
      if (!isData(R.Symbols[X.Symbol2]))
        return Fail("DIFF subtracts FUNCTION " + symbolText(X.Symbol2));
      break;
    case MSILLink::RelocType::TLSOffset32:
    case MSILLink::RelocType::TLSOffset64:
      if (!isData(S) || !(S.Flags & MSILLink::ThreadLocal))
        return Fail(Twine(relocName(X.Type)) + " names " +
                    symbolText(X.Symbol) + ", which is not TLS data");
      break;
    }
  }
}

void LinkInfoDecoder::checkComdats() {
  for (uint32_t I = 0; I < R.Comdats.size() && !Rd.hasError(); ++I) {
    for (const auto &[Kind, Index] : R.Comdats[I].Members) {
      auto Fail = [&](const Twine &Problem) {
        Rd.setError("comdat " + Twine(I) + " " + quoted(R.Comdats[I].Name),
                    ComdatOffsets[I], Problem);
      };
      if (Kind == MSILLink::ComdatMemberKind::Segment) {
        if (Index >= R.Segments.size())
          return Fail("segment " + Twine(Index) + " is out of range (" +
                      Twine(R.Segments.size()) + " segments)");
        continue;
      }
      if (Index >= R.Symbols.size())
        return Fail("function symbol " + Twine(Index) + " is out of range (" +
                    Twine(R.Symbols.size()) + " symbols)");
      const MSILLink::Symbol &S = R.Symbols[Index];
      if (isData(S) || isUndefined(S))
        return Fail(symbolText(Index) + " is not a defined FUNCTION");
    }
  }
}

void LinkInfoDecoder::checkInits() {
  for (uint32_t I = 0; I < R.Inits.size() && !Rd.hasError(); ++I) {
    const MSILLink::InitEntry &E = R.Inits[I];
    auto Fail = [&](const Twine &Problem) {
      Rd.setError("init entry " + Twine(I), InitOffsets[I], Problem);
    };
    if (E.Symbol >= R.Symbols.size())
      return Fail("symbol " + Twine(E.Symbol) + " is out of range (" +
                  Twine(R.Symbols.size()) + " symbols)");
    const MSILLink::Symbol &S = R.Symbols[E.Symbol];
    if (isData(S) || isUndefined(S))
      return Fail(symbolText(E.Symbol) + " is not a defined FUNCTION");
    if (E.ComdatPlusOne > R.Comdats.size())
      return Fail("comdat " + Twine(E.ComdatPlusOne) +
                  " - 1 is out of range (" + Twine(R.Comdats.size()) +
                  " groups)");
  }
}

Expected<Record> LinkInfoDecoder::decode() {
  header();
  if (Rd.hasError())
    return Rd.takeError();
  int Last = -1;
  while (!Rd.empty()) {
    const uint64_t Off = Rd.getOffset();
    uint8_t Kind;
    RecordReader P = Rd.readSubsection(Kind);
    if (Rd.hasError())
      return Rd.takeError();
    const std::string F = "subsection " + hex(Kind, 2);
    if (int(Kind) <= Last) {
      Rd.setError(F, Off,
                  "subsection kinds must increase (it follows " + hex(Last, 2) +
                      ")");
      return Rd.takeError();
    }
    Last = Kind;
    if (Kind >= uint8_t(MSILLink::SubsectionKind::FirstOptional) &&
        Kind <= uint8_t(MSILLink::SubsectionKind::LastOptional)) {
      ArrayRef<uint8_t> Payload = P.readBytes(P.bytesLeft(), F);
      R.SkippedOptional.emplace_back(
          Kind, std::vector<uint8_t>(Payload.begin(), Payload.end()));
      continue;
    }
    switch (static_cast<MSILLink::SubsectionKind>(Kind)) {
    case MSILLink::SubsectionKind::SymTab:
      symbols(P);
      break;
    case MSILLink::SubsectionKind::Segments:
      segments(P);
      break;
    case MSILLink::SubsectionKind::DataRelocs:
      relocs(P);
      break;
    case MSILLink::SubsectionKind::Comdats:
      comdats(P);
      break;
    case MSILLink::SubsectionKind::InitFini:
      inits(P);
      break;
    case MSILLink::SubsectionKind::Signatures:
      signatures(P);
      break;
    case MSILLink::SubsectionKind::LinkerOptions:
      linkerOptions(P);
      break;
    default: {
      std::string Feature = "subsection kind " + hex(Kind, 2);
      if (const char *Name = subsectionName(Kind))
        Feature += " (" + std::string(Name) + ")";
      unsupported(P, Feature + " (at offset " + Twine(Off) + ")");
      break;
    }
    }
    if (!P.hasError()) {
      const char *Name = subsectionName(Kind);
      P.expectEnd(Twine(Name) + " payload");
    }
    if (P.hasError())
      return P.takeError();
  }
  checkSymbols();
  checkRelocs();
  checkComdats();
  checkInits();
  if (Rd.hasError())
    return Rd.takeError();
  return std::move(R);
}

Expected<Record> cli::decodeLinkInfo(ArrayRef<uint8_t> Bytes) {
  return LinkInfoDecoder(Bytes).decode();
}

//===----------------------------------------------------------------------===//
// findLinkInfo
//===----------------------------------------------------------------------===//

/// True if \p Ctor is the MemberRef '.ctor' of [msil.link]'<msil>LinkInfo'.
static Expected<bool> isLinkInfoConstructor(const MetadataReader &MD,
                                            uint32_t Ctor) {
  if (getTokenType(Ctor) != uint8_t(TableId::MemberRef))
    return false;
  Expected<MemberRefRow> M = MD.getMemberRef(getTokenRow(Ctor));
  if (!M)
    return M.takeError();
  if (M->Name != ".ctor" ||
      getTokenType(M->Parent) != uint8_t(TableId::TypeRef))
    return false;
  Expected<TypeRefRow> T = MD.getTypeRef(getTokenRow(M->Parent));
  if (!T)
    return T.takeError();
  if (!T->Namespace.empty() || T->Name != MSILLink::LinkInfoType ||
      getTokenType(T->ResolutionScope) != uint8_t(TableId::AssemblyRef))
    return false;
  Expected<AssemblyRefRow> A =
      MD.getAssemblyRef(getTokenRow(T->ResolutionScope));
  if (!A)
    return A.takeError();
  return A->Name == MSILLink::LinkInfoAssembly;
}

Expected<ArrayRef<uint8_t>> cli::findLinkInfo(const MetadataReader &MD) {
  if (MD.getRowCount(TableId::Module) == 0)
    return createStringError(errc::invalid_argument, "no link-info record");
  Expected<SmallVector<uint32_t, 4>> Rows =
      MD.getCustomAttributes(makeToken(TableId::Module, 1));
  if (!Rows)
    return Rows.takeError();
  std::optional<CustomAttributeRow> Found;
  unsigned Count = 0;
  for (uint32_t Row : *Rows) {
    Expected<CustomAttributeRow> CA = MD.getCustomAttribute(Row);
    if (!CA)
      return CA.takeError();
    Expected<bool> Is = isLinkInfoConstructor(MD, CA->Constructor);
    if (!Is)
      return Is.takeError();
    if (!*Is)
      continue;
    if (!Count++)
      Found = *CA;
  }
  if (!Count)
    return createStringError(errc::invalid_argument, "no link-info record");
  if (Count > 1)
    return createStringError(errc::illegal_byte_sequence,
                             "invalid link-info record: the Module row has " +
                                 Twine(Count) +
                                 " link-info attributes (an object has one)");
  Expected<MemberRefRow> Ctor =
      MD.getMemberRef(getTokenRow(Found->Constructor));
  if (!Ctor)
    return Ctor.takeError();
  Expected<ArrayRef<uint8_t>> Sig = MD.getBlob(Ctor->Signature);
  if (!Sig)
    return Sig.takeError();
  static const uint8_t CtorSig[] = {0x20, 0x01, 0x01, 0x1D, 0x05};
  if (*Sig != ArrayRef<uint8_t>(CtorSig)) {
    std::string Text;
    raw_string_ostream OS(Text);
    printHexBytes(OS, *Sig);
    return createStringError(
        errc::illegal_byte_sequence,
        "invalid link-info record: the constructor of the link-info "
        "attribute has signature " +
            (Text.empty() ? std::string("-") : Text) +
            ", not 20 01 01 1D 05 (instance void(uint8[]))");
  }
  Expected<ArrayRef<uint8_t>> Value = MD.getBlob(Found->Value);
  if (!Value)
    return Value.takeError();
  return decodeByteArrayAttribute(*Value);
}

//===----------------------------------------------------------------------===//
// validateLinkInfo
//===----------------------------------------------------------------------===//

static Error ruleError(unsigned Rule, const Twine &Msg) {
  return createStringError(errc::illegal_byte_sequence,
                           "C" + Twine(Rule) + ": " + Msg);
}

namespace {
/// What a TypeRef of an object is to C5 and C6.
struct TypeRefInfo {
  /// [msil.extern]'<msil>Symbols' or [msil.extern]'<msil>FnAddr'.
  bool Symbols = false, FnAddr = false;
  /// The TypeRef (or the outermost TypeRef enclosing it) is scoped to an
  /// AssemblyRef.
  bool ScopedToAssembly = false;
};
} // namespace

/// Classifies every TypeRef row (index = row; row 0 unused).
static Expected<std::vector<TypeRefInfo>>
classifyTypeRefs(const MetadataReader &MD) {
  const uint32_t N = MD.getRowCount(TableId::TypeRef);
  std::vector<TypeRefInfo> Info(N + 1);
  std::vector<TypeRefRow> Rows(N + 1);
  for (uint32_t Row = 1; Row <= N; ++Row) {
    Expected<TypeRefRow> T = MD.getTypeRef(Row);
    if (!T)
      return T.takeError();
    Rows[Row] = *T;
  }
  // Outermost[Row] is the scope that following Row's nested TypeRefs ends
  // at: the first scope that is not a TypeRef, a TypeRef token whose row is 0
  // or out of range, or, for a row that reaches a cycle, a TypeRef token of
  // the cycle (never an AssemblyRef). Each row is followed once: a walk stops
  // at a row already resolved and reuses its result, and every row of the
  // walk gets the walk's result.
  enum : uint8_t { Unvisited, OnWalk, Resolved };
  std::vector<uint32_t> Outermost(N + 1);
  std::vector<uint8_t> State(N + 1, Unvisited);
  SmallVector<uint32_t, 16> Walk;
  for (uint32_t Row = 1; Row <= N; ++Row) {
    if (State[Row] == Resolved)
      continue;
    uint32_t Scope;
    for (uint32_t Cur = Row;;) {
      State[Cur] = OnWalk;
      Walk.push_back(Cur);
      Scope = Rows[Cur].ResolutionScope;
      if (getTokenType(Scope) != uint8_t(TableId::TypeRef))
        break;
      uint32_t Outer = getTokenRow(Scope);
      // Row 0 or out of range ends the walk, and so does a row already on
      // this walk (a cycle).
      if (Outer == 0 || Outer > N || State[Outer] == OnWalk)
        break;
      if (State[Outer] == Resolved) {
        Scope = Outermost[Outer];
        break;
      }
      Cur = Outer;
    }
    for (uint32_t W : Walk) {
      Outermost[W] = Scope;
      State[W] = Resolved;
    }
    Walk.clear();
  }
  for (uint32_t Row = 1; Row <= N; ++Row) {
    const uint32_t Scope = Outermost[Row];
    if (getTokenType(Scope) != uint8_t(TableId::AssemblyRef) ||
        getTokenRow(Scope) == 0)
      continue;
    Info[Row].ScopedToAssembly = true;
    const TypeRefRow &T = Rows[Row];
    if (T.ResolutionScope != Scope || !T.Namespace.empty() ||
        (T.Name != MSILLink::SymbolsType && T.Name != MSILLink::FnAddrType))
      continue;
    Expected<AssemblyRefRow> A = MD.getAssemblyRef(getTokenRow(Scope));
    if (!A)
      return A.takeError();
    if (A->Name != MSILLink::ExternAssembly)
      continue;
    Info[Row].Symbols = T.Name == MSILLink::SymbolsType;
    Info[Row].FnAddr = T.Name == MSILLink::FnAddrType;
  }
  return std::move(Info);
}

Error cli::validateLinkInfo(const Record &R, const MetadataReader &MD) {
  StringMap<uint32_t> ByName;
  for (uint32_t I = 0; I < R.Symbols.size(); ++I)
    ByName.try_emplace(R.Symbols[I].Name, I);
  auto SymbolText = [&](uint32_t I) {
    return "symbol " + std::to_string(I) + " " + quoted(R.Symbols[I].Name);
  };
  auto Find = [&](StringRef Name) -> const MSILLink::Symbol * {
    auto It = ByName.find(Name);
    return It == ByName.end() ? nullptr : &R.Symbols[It->second];
  };
  const uint32_t NumMethods = MD.getRowCount(TableId::MethodDef);
  const uint32_t NumFields = MD.getRowCount(TableId::Field);
  const uint32_t NumMemberRefs = MD.getRowCount(TableId::MemberRef);
  auto IsRowOf = [](uint32_t Token, TableId T, uint32_t Count) {
    return getTokenType(Token) == uint8_t(T) && getTokenRow(Token) != 0 &&
           getTokenRow(Token) <= Count;
  };

  // C1: a defined, non-alias FUNCTION symbol's Token is a MethodDef with a
  // body whose name is the symbol's.
  for (uint32_t I = 0; I < R.Symbols.size(); ++I) {
    const MSILLink::Symbol &S = R.Symbols[I];
    if (isData(S) || !hasToken(S))
      continue;
    if (!IsRowOf(S.Token, TableId::MethodDef, NumMethods))
      return ruleError(1, "FUNCTION " + SymbolText(I) + ": token " +
                              hex(S.Token) +
                              " is not a MethodDef of the object");
    Expected<MethodDefRow> M = MD.getMethodDef(getTokenRow(S.Token));
    if (!M)
      return M.takeError();
    if (M->RVA == 0)
      return ruleError(1, "FUNCTION " + SymbolText(I) + ": MethodDef " +
                              hex(S.Token) + " " + quoted(M->Name) +
                              " has no body");
    if (M->Name != S.Name)
      return ruleError(1, "FUNCTION " + SymbolText(I) + ": MethodDef " +
                              hex(S.Token) + " is named " + quoted(M->Name));
  }

  // C2: a defined or COMMON non-alias DATA symbol's Token is a FieldDef of
  // <Module> with signature FIELD native int whose name is the symbol's.
  const uint32_t ModuleType = makeToken(TableId::TypeDef, 1);
  static const uint8_t NativeIntField[] = {SIG_FIELD, ELEMENT_TYPE_I};
  for (uint32_t I = 0; I < R.Symbols.size(); ++I) {
    const MSILLink::Symbol &S = R.Symbols[I];
    if (!isData(S) || !hasToken(S))
      continue;
    if (!IsRowOf(S.Token, TableId::Field, NumFields))
      return ruleError(2, "DATA " + SymbolText(I) + ": token " + hex(S.Token) +
                              " is not a FieldDef of the object");
    Expected<FieldRow> Fd = MD.getField(getTokenRow(S.Token));
    if (!Fd)
      return Fd.takeError();
    Expected<uint32_t> Owner = MD.getOwnerTypeDef(S.Token);
    if (!Owner)
      return Owner.takeError();
    if (*Owner != ModuleType)
      return ruleError(2, "DATA " + SymbolText(I) + ": FieldDef " +
                              hex(S.Token) + " belongs to TypeDef " +
                              hex(*Owner) + ", not <Module>");
    Expected<ArrayRef<uint8_t>> Sig = MD.getBlob(Fd->Signature);
    if (!Sig)
      return Sig.takeError();
    if (*Sig != ArrayRef<uint8_t>(NativeIntField)) {
      std::string Text;
      raw_string_ostream OS(Text);
      printHexBytes(OS, *Sig);
      return ruleError(2, "DATA " + SymbolText(I) + ": FieldDef " +
                              hex(S.Token) + " has signature " +
                              (Text.empty() ? std::string("-") : Text) +
                              ", not FIELD native int (06 18)");
    }
    if (Fd->Name != S.Name)
      return ruleError(2, "DATA " + SymbolText(I) + ": FieldDef " +
                              hex(S.Token) + " is named " + quoted(Fd->Name));
  }

  // C3: every MethodDef with a body is the Token of exactly one FUNCTION
  // symbol, and every FieldDef of exactly one DATA symbol.
  DenseMap<uint32_t, unsigned> FunctionTokens, DataTokens;
  for (const MSILLink::Symbol &S : R.Symbols)
    if (hasToken(S))
      ++(isData(S) ? DataTokens : FunctionTokens)[S.Token];
  for (uint32_t Row = 1; Row <= NumMethods; ++Row) {
    Expected<MethodDefRow> M = MD.getMethodDef(Row);
    if (!M)
      return M.takeError();
    if (M->RVA == 0)
      continue;
    uint32_t Token = makeToken(TableId::MethodDef, Row);
    unsigned Count = FunctionTokens.lookup(Token);
    if (Count != 1)
      return ruleError(3, "MethodDef " + hex(Token) + " " + quoted(M->Name) +
                              " has a body and is the token of " +
                              Twine(Count) + " FUNCTION symbols (not 1)");
  }
  for (uint32_t Row = 1; Row <= NumFields; ++Row) {
    uint32_t Token = makeToken(TableId::Field, Row);
    unsigned Count = DataTokens.lookup(Token);
    if (Count == 1)
      continue;
    Expected<FieldRow> Fd = MD.getField(Row);
    if (!Fd)
      return Fd.takeError();
    return ruleError(3, "FieldDef " + hex(Token) + " " + quoted(Fd->Name) +
                            " is the token of " + Twine(Count) +
                            " DATA symbols (not 1)");
  }

  // C4: every body-less MethodDef is an import stub: it has an ImplMap row,
  // its name is an UNDEFINED FUNCTION symbol, EXPLICIT_IMPORT exactly when
  // its module is not msil.unresolved, and all stubs of one symbol name the
  // same module and the same non-empty import name.
  struct Stub {
    uint32_t Method;
    StringRef Module, ImportName;
  };
  DenseMap<uint32_t, Stub> Stubs;
  for (uint32_t Row = 1; Row <= NumMethods; ++Row) {
    Expected<MethodDefRow> M = MD.getMethodDef(Row);
    if (!M)
      return M.takeError();
    if (M->RVA != 0)
      continue;
    uint32_t Token = makeToken(TableId::MethodDef, Row);
    std::string What = "MethodDef " + hex(Token) + " " + quoted(M->Name);
    Expected<std::optional<ImplMapRow>> IM = MD.findImplMap(Token);
    if (!IM)
      return IM.takeError();
    if (!*IM)
      return ruleError(4, What + " has no body and no ImplMap row");
    const ImplMapRow &Map = **IM;
    if (getTokenType(Map.ImportScope) != uint8_t(TableId::ModuleRef))
      return ruleError(4, What + ": the import scope " + hex(Map.ImportScope) +
                              " is not a ModuleRef");
    Expected<ModuleRefRow> Mod = MD.getModuleRef(getTokenRow(Map.ImportScope));
    if (!Mod)
      return Mod.takeError();
    if (Map.ImportName.empty())
      return ruleError(4, What + " has an empty import name");
    auto It = ByName.find(M->Name);
    if (It == ByName.end())
      return ruleError(4, What + " is a stub of no symbol");
    const uint32_t SymIndex = It->second;
    const MSILLink::Symbol &S = R.Symbols[SymIndex];
    if (isData(S) || !isUndefined(S))
      return ruleError(4, What + " is a stub of " + SymbolText(SymIndex) +
                              ", which is not an UNDEFINED FUNCTION");
    const bool Unresolved = Mod->Name == MSILLink::UnresolvedModule;
    const bool Explicit = S.Flags & MSILLink::ExplicitImport;
    if (Unresolved && Explicit)
      return ruleError(4, What + " imports from " + quoted(Mod->Name) +
                              ", but " + SymbolText(SymIndex) +
                              " has EXPLICIT_IMPORT");
    if (!Unresolved && !Explicit)
      return ruleError(4, What + " imports from " + quoted(Mod->Name) +
                              ", but " + SymbolText(SymIndex) +
                              " lacks EXPLICIT_IMPORT");
    auto [SIt, New] =
        Stubs.try_emplace(SymIndex, Stub{Token, Mod->Name, Map.ImportName});
    if (!New && (SIt->second.Module != Mod->Name ||
                 SIt->second.ImportName != Map.ImportName))
      return ruleError(4, What + " imports " + quoted(Map.ImportName) +
                              " from " + quoted(Mod->Name) + ", but stub " +
                              hex(SIt->second.Method) + " of " +
                              SymbolText(SymIndex) + " imports " +
                              quoted(SIt->second.ImportName) + " from " +
                              quoted(SIt->second.Module));
  }

  // C5 and C6: the MemberRefs.
  Expected<std::vector<TypeRefInfo>> TypeRefs = classifyTypeRefs(MD);
  if (!TypeRefs)
    return TypeRefs.takeError();
  auto ParentInfo = [&](uint32_t Parent) -> const TypeRefInfo * {
    if (getTokenType(Parent) != uint8_t(TableId::TypeRef))
      return nullptr;
    uint32_t Row = getTokenRow(Parent);
    return Row != 0 && Row < TypeRefs->size() ? &(*TypeRefs)[Row] : nullptr;
  };
  for (uint32_t Row = 1; Row <= NumMemberRefs; ++Row) {
    Expected<MemberRefRow> MR = MD.getMemberRef(Row);
    if (!MR)
      return MR.takeError();
    const TypeRefInfo *P = ParentInfo(MR->Parent);
    if (!P || (!P->Symbols && !P->FnAddr))
      continue;
    std::string What = "MemberRef " + hex(makeToken(TableId::MemberRef, Row)) +
                       " " + quoted(MR->Name);
    const MSILLink::Symbol *S = Find(MR->Name);
    if (P->FnAddr) {
      if (!S || isData(*S))
        return ruleError(5, What + " on '<msil>FnAddr' names " +
                                (S ? "a DATA symbol" : "no symbol") +
                                " (it needs a FUNCTION symbol)");
      continue;
    }
    Expected<ArrayRef<uint8_t>> Sig = MD.getBlob(MR->Signature);
    if (!Sig)
      return Sig.takeError();
    const bool FieldSig =
        !Sig->empty() && ((*Sig)[0] & SIG_KIND_MASK) == SIG_FIELD;
    if (!S)
      return ruleError(5, What + " on '<msil>Symbols' names no symbol");
    if (isData(*S) != FieldSig)
      return ruleError(5, What + " on '<msil>Symbols' has a " +
                              (FieldSig ? "field" : "method") +
                              " signature, but the symbol is " +
                              (isData(*S) ? "DATA" : "a FUNCTION"));
  }
  for (uint32_t Row = 1; Row <= NumMemberRefs; ++Row) {
    Expected<MemberRefRow> MR = MD.getMemberRef(Row);
    if (!MR)
      return MR.takeError();
    const TypeRefInfo *P = ParentInfo(MR->Parent);
    if (P && (P->Symbols || P->FnAddr || P->ScopedToAssembly))
      continue;
    return ruleError(6, "MemberRef " + hex(makeToken(TableId::MemberRef, Row)) +
                            " " + quoted(MR->Name) + ": its parent " +
                            hex(MR->Parent) +
                            " is not a TypeRef scoped to an AssemblyRef");
  }

  // C7: SIGNATURES tokens are MethodDefs or method MemberRefs on
  // '<msil>Symbols', at most one entry per token.
  DenseSet<uint32_t> Seen;
  for (const MSILLink::SignatureInfo &SI : R.Signatures) {
    if (!Seen.insert(SI.Token).second)
      return ruleError(7, "token " + hex(SI.Token) +
                              " has more than one SIGNATURES entry");
    if (IsRowOf(SI.Token, TableId::MethodDef, NumMethods))
      continue;
    if (IsRowOf(SI.Token, TableId::MemberRef, NumMemberRefs)) {
      Expected<MemberRefRow> MR = MD.getMemberRef(getTokenRow(SI.Token));
      if (!MR)
        return MR.takeError();
      const TypeRefInfo *P = ParentInfo(MR->Parent);
      Expected<ArrayRef<uint8_t>> Sig = MD.getBlob(MR->Signature);
      if (!Sig)
        return Sig.takeError();
      const bool FieldSig =
          !Sig->empty() && ((*Sig)[0] & SIG_KIND_MASK) == SIG_FIELD;
      if (P && P->Symbols && !FieldSig)
        continue;
      return ruleError(7, "SIGNATURES token " + hex(SI.Token) + " " +
                              quoted(MR->Name) +
                              " is not a method MemberRef on '<msil>Symbols'");
    }
    return ruleError(7, "SIGNATURES token " + hex(SI.Token) +
                            " is neither a MethodDef nor a MemberRef of the "
                            "object");
  }
  return Error::success();
}

//===----------------------------------------------------------------------===//
// The COFF symbol view
//===----------------------------------------------------------------------===//

namespace {
/// Section numbers of the COFF view (5.8.6): .text, then the markers of
/// getCOFFViewMarkerSections().
enum : int16_t { TextSection = 1, DataSection, RDataSection, BssSection };
} // namespace

std::vector<CoffSymbol> cli::buildCOFFSymbolView(const Record &R) {
  auto SegmentSection = [&](uint32_t Index) -> int16_t {
    if (Index >= R.Segments.size())
      return DataSection;
    uint32_t Flags = R.Segments[Index].Flags;
    if (Flags & MSILLink::SegReadOnly)
      return RDataSection;
    if (Flags & MSILLink::SegZero)
      return BssSection;
    return DataSection;
  };
  std::vector<CoffSymbol> View;
  View.reserve(R.Symbols.size());
  for (const MSILLink::Symbol &S : R.Symbols) {
    CoffSymbol C;
    C.Name = S.Name;
    C.Type = isData(S) ? 0
                       : COFF::IMAGE_SYM_DTYPE_FUNCTION
                             << COFF::SCT_COMPLEX_TYPE_SHIFT;
    C.StorageClass = COFF::IMAGE_SYM_CLASS_EXTERNAL;
    if (isUndefined(S)) {
      C.SectionNumber = 0;
    } else if (isCommon(S)) {
      C.SectionNumber = 0;
      C.Value = static_cast<uint32_t>(S.CommonSize);
    } else {
      if ((S.Flags & MSILLink::BindingMask) == MSILLink::BindingLocal)
        C.StorageClass = COFF::IMAGE_SYM_CLASS_STATIC;
      if (!isData(S)) {
        C.SectionNumber = TextSection;
      } else if (isAlias(S)) {
        C.SectionNumber = DataSection;
        if (S.Aliasee < R.Symbols.size() && hasPlacement(R.Symbols[S.Aliasee]))
          C.SectionNumber = SegmentSection(R.Symbols[S.Aliasee].Segment);
      } else {
        C.SectionNumber = SegmentSection(S.Segment);
      }
    }
    View.push_back(std::move(C));
  }
  return View;
}

ArrayRef<PESection> cli::getCOFFViewMarkerSections() {
  static const PESection Markers[] = {
      {".data",
       COFF::IMAGE_SCN_CNT_INITIALIZED_DATA | COFF::IMAGE_SCN_MEM_READ |
           COFF::IMAGE_SCN_MEM_WRITE,
       {}},
      {".rdata",
       COFF::IMAGE_SCN_CNT_INITIALIZED_DATA | COFF::IMAGE_SCN_MEM_READ,
       {}},
      {".bss",
       COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA | COFF::IMAGE_SCN_MEM_READ |
           COFF::IMAGE_SCN_MEM_WRITE,
       {}}};
  return Markers;
}

//===----------------------------------------------------------------------===//
// printLinkInfo
//===----------------------------------------------------------------------===//

/// Prints the names of the set bits of \p Flags among \p Names, joined by
/// \p Sep, and the unknown rest as hex; "0" when no bit is set.
static void printFlagNames(raw_ostream &OS, uint64_t Flags,
                           ArrayRef<std::pair<uint32_t, const char *>> Names,
                           char Sep) {
  bool First = true;
  for (const auto &[Bit, Name] : Names) {
    if (!(Flags & Bit))
      continue;
    if (!First)
      OS << Sep;
    OS << Name;
    First = false;
    Flags &= ~uint64_t(Bit);
  }
  if (Flags || First) {
    if (!First)
      OS << Sep;
    OS << (Flags ? hex(Flags, 0) : std::string("0"));
  }
}

/// The name of the MethodDef, MemberRef or Field row of \p Token.
static std::optional<StringRef> entityName(const MetadataReader &MD,
                                           uint32_t Token) {
  const uint8_t Type = getTokenType(Token);
  const uint32_t Row = getTokenRow(Token);
  if (Type == uint8_t(TableId::MethodDef)) {
    if (Expected<MethodDefRow> M = MD.getMethodDef(Row))
      return M->Name;
    else
      consumeError(M.takeError());
  } else if (Type == uint8_t(TableId::MemberRef)) {
    if (Expected<MemberRefRow> M = MD.getMemberRef(Row))
      return M->Name;
    else
      consumeError(M.takeError());
  } else if (Type == uint8_t(TableId::Field)) {
    if (Expected<FieldRow> F = MD.getField(Row))
      return F->Name;
    else
      consumeError(F.takeError());
  }
  return std::nullopt;
}

void cli::printLinkInfo(const Record &R, const MetadataReader *MD,
                        raw_ostream &OS) {
  const MSILLink::Header &H = R.Hdr;
  OS << "LinkInfo v" << unsigned(H.Major) << '.' << unsigned(H.Minor)
     << " triple=" << H.Triple << " abi=" << H.ABIVersion
     << " abiflags=" << hex(H.ABIFlags, 0)
     << " features=" << hex(H.FeatureFlags, 0)
     << " datamodel=" << unsigned(H.PointerSize) << ',' << unsigned(H.LongSize)
     << ',' << unsigned(H.WCharSize) << ',' << unsigned(H.LongDoubleSize);
  if (H.Flags) {
    static constexpr std::pair<uint32_t, const char *> HeaderFlagNames[] = {
        {MSILLink::HasDebugInfo, "HAS_DEBUG_INFO"}};
    OS << " flags=";
    printFlagNames(OS, H.Flags, HeaderFlagNames, '|');
  }
  OS << '\n';

  auto PrintSymbolRef = [&](uint32_t Index) {
    if (Index < R.Symbols.size()) {
      OS << ' ';
      printQuoted(OS, R.Symbols[Index].Name);
    }
  };

  for (uint32_t I = 0; I < R.Symbols.size(); ++I) {
    const MSILLink::Symbol &S = R.Symbols[I];
    OS << "Symbol " << I << ' ';
    if (S.Kind == SymbolKind::Function)
      OS << "FUNCTION";
    else if (S.Kind == SymbolKind::Data)
      OS << "DATA";
    else
      OS << "KIND" << unsigned(S.Kind);
    OS << ' ' << bindingName(S.Flags) << ' ' << visibilityName(S.Flags);
    uint32_t Flags =
        S.Flags & ~uint32_t(MSILLink::BindingMask | MSILLink::VisibilityMask);
    if (Flags) {
      OS << ' ';
      printFlagNames(OS, Flags, SymbolFlagNames, ' ');
    }
    OS << ' ';
    printQuoted(OS, S.Name);
    if (S.Flags & MSILLink::Import) {
      OS << " import_module=";
      printQuoted(OS, S.ImportModule);
      OS << " import_name=";
      printQuoted(OS, S.ImportName);
    }
    if (hasToken(S))
      OS << (isData(S) ? " field=" : " method=") << hex(S.Token);
    if (hasPlacement(S))
      OS << " segment=" << S.Segment << " offset=" << S.Offset
         << " size=" << S.Size;
    if (isAlias(S)) {
      OS << " aliasee=" << S.Aliasee;
      if (isData(S))
        OS << " offset=" << S.AliasOffset << " size=" << S.Size;
    }
    if (isCommon(S)) {
      OS << " size=" << S.CommonSize << " align=";
      printAlign(OS, S.CommonAlignLog2);
    }
    OS << '\n';
  }

  for (uint32_t I = 0; I < R.Segments.size(); ++I) {
    const MSILLink::Segment &S = R.Segments[I];
    OS << "Segment " << I << ' ';
    printQuoted(OS, S.Name);
    OS << " align=";
    printAlign(OS, S.AlignLog2);
    OS << " size=" << S.Size << " flags=";
    printFlagNames(OS, S.Flags, SegmentFlagNames, '|');
    OS << '\n';
    if (S.Flags & MSILLink::SegZero)
      continue;
    ArrayRef<uint8_t> Bytes = S.Bytes;
    while (!Bytes.empty()) {
      OS << "  ";
      printHexBytes(OS, Bytes.take_front(32));
      OS << '\n';
      Bytes = Bytes.drop_front(std::min<size_t>(32, Bytes.size()));
    }
  }

  for (const MSILLink::Reloc &X : R.Relocs) {
    OS << "Reloc ";
    if (const char *Name = relocName(X.Type))
      OS << Name;
    else
      OS << "TYPE" << unsigned(X.Type);
    if (X.Type == MSILLink::RelocType::Diff)
      OS << " width=" << unsigned(X.Width);
    OS << " segment=" << X.Segment << " offset=" << X.Offset
       << " symbol=" << X.Symbol;
    PrintSymbolRef(X.Symbol);
    if (X.Type == MSILLink::RelocType::Diff) {
      OS << " symbol2=" << X.Symbol2;
      PrintSymbolRef(X.Symbol2);
    }
    OS << " addend=" << X.Addend << '\n';
  }

  for (const MSILLink::ComdatGroup &G : R.Comdats) {
    OS << "Comdat ";
    printQuoted(OS, G.Name);
    if (const char *Name = selectionName(G.Selection))
      OS << ' ' << Name;
    else
      OS << " SELECTION" << unsigned(G.Selection);
    for (const auto &[Kind, Index] : G.Members) {
      if (Kind == MSILLink::ComdatMemberKind::FunctionSymbol)
        OS << " function=";
      else if (Kind == MSILLink::ComdatMemberKind::Segment)
        OS << " segment=";
      else
        OS << " member" << unsigned(Kind) << '=';
      OS << Index;
    }
    OS << '\n';
  }

  for (const MSILLink::InitEntry &E : R.Inits) {
    OS << (E.Kind == MSILLink::InitKind::Fini ? "Fini" : "Init")
       << " priority=" << E.Priority << " symbol=" << E.Symbol;
    PrintSymbolRef(E.Symbol);
    if (E.ComdatPlusOne)
      OS << " comdat=" << E.ComdatPlusOne - 1;
    OS << '\n';
  }

  for (const MSILLink::SignatureInfo &S : R.Signatures) {
    OS << "Signature " << hex(S.Token);
    if (MD)
      if (std::optional<StringRef> Name = entityName(*MD, S.Token)) {
        OS << ' ';
        printQuoted(OS, *Name);
      }
    if (S.Flags & MSILLink::NoPrototype)
      OS << " NO_PROTOTYPE";
    if (S.Flags & MSILLink::VarArg)
      OS << " VARARG fixed=" << S.FixedParams;
    if (S.Flags & MSILLink::Sret) {
      OS << " SRET size=" << S.SretSize << " align=";
      printAlign(OS, S.SretAlignLog2);
    }
    if (uint32_t Unknown = S.Flags & ~uint32_t(KnownCallFlags))
      OS << ' ' << hex(Unknown, 0);
    for (const MSILLink::ByvalParam &B : S.Byval) {
      OS << " byval=" << B.Param << ':' << B.Size << ':';
      printAlign(OS, B.AlignLog2);
    }
    OS << '\n';
  }

  for (const MSILLink::LinkerOption &L : R.LinkerOptions) {
    OS << "LinkerOption ";
    if (L.Kind == MSILLink::LinkerOptionKind::DependentLib)
      OS << "DEPENDENT_LIB";
    else if (L.Kind == MSILLink::LinkerOptionKind::LinkerOption)
      OS << "LINKER_OPTION";
    else
      OS << "KIND" << unsigned(L.Kind);
    for (const std::string &A : L.Args) {
      OS << ' ';
      printQuoted(OS, A);
    }
    OS << '\n';
  }

  for (const auto &[Kind, Payload] : R.SkippedOptional)
    OS << "Optional kind=" << hex(Kind, 2) << " size=" << Payload.size()
       << '\n';
}
