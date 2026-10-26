//===- ImageLayout.cpp - MSIL data image layout ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the data image layout of ImplementationPlan.md 5.9.9
// (see llvm/CLI/ImageLayout.h).
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/ImageLayout.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <tuple>

using namespace llvm;
using namespace llvm::cli;

#define DEBUG_TYPE "cli-imagelayout"

//===----------------------------------------------------------------------===//
// Section names
//===----------------------------------------------------------------------===//

/// Whether \p Name is \p Base or \p Base followed by '.' and anything (the
/// '.N' forms that lld/ELF groups with the plain name).
static bool isOrDotted(StringRef Name, StringRef Base) {
  return Name == Base || (Name.size() > Base.size() && Name.starts_with(Base) &&
                          Name[Base.size()] == '.');
}

SectionClass cli::classifySection(StringRef Name, SectionRules Rules) {
  if (isOrDotted(Name, ".tdata") || isOrDotted(Name, ".tbss"))
    return SectionClass::TLS;
  if (Rules == SectionRules::ELF) {
    if (isOrDotted(Name, ".init_array"))
      return SectionClass::ConstructorTable;
    if (isOrDotted(Name, ".fini_array"))
      return SectionClass::DestructorTable;
    // ld.so runs .preinit_array (DT_PREINIT_ARRAY) before every
    // .init_array entry; MSIL has no such phase yet.
    if (isOrDotted(Name, ".ctors") || isOrDotted(Name, ".dtors") ||
        isOrDotted(Name, ".preinit_array"))
      return SectionClass::Unsupported;
    return SectionClass::Data;
  }
  // COFF: the CRT's tables are sorted by the part after '$' (.CRT$XCA-XCZ
  // constructors, .CRT$XTA-XTZ terminators); the C initializers .CRT$XI*,
  // the TLS callbacks .CRT$XL*, the pre-terminators .CRT$XP* and the rest
  // have no MSIL equivalent.
  if (Name.starts_with(".CRT$XC"))
    return SectionClass::ConstructorTable;
  if (Name.starts_with(".CRT$XT"))
    return SectionClass::DestructorTable;
  if (Name.starts_with(".CRT$X"))
    return SectionClass::Unsupported;
  StringRef Base = Name.split('$').first;
  if (Base == ".tls")
    return SectionClass::TLS;
  if (isOrDotted(Base, ".ctors") || isOrDotted(Base, ".dtors"))
    return SectionClass::Unsupported;
  return SectionClass::Data;
}

StringRef cli::getOutputSectionName(StringRef Name, SectionRules Rules) {
  if (Rules == SectionRules::COFF)
    return Name.split('$').first;
  for (StringRef Base : {".init_array", ".fini_array"})
    if (isOrDotted(Name, Base))
      return Base;
  return Name;
}

int cli::getSectionPriority(StringRef Name) {
  // lld/ELF/OutputSections.cpp, elf::getPriority, verbatim.
  size_t Pos = Name.rfind('.');
  if (Pos == StringRef::npos)
    return 65536;
  int V = 65536;
  if (to_integer(Name.substr(Pos + 1), V, 10) &&
      (Pos == 6 && (Name.starts_with(".ctors") || Name.starts_with(".dtors"))))
    V = 65535 - V;
  return V;
}

bool cli::isCIdentifier(StringRef Name) {
  return !Name.empty() && !isDigit(Name[0]) &&
         all_of(Name, [](char C) { return isAlnum(C) || C == '_'; });
}

std::optional<uint64_t> cli::getStartStopOffset(const ImageLayout &Layout,
                                                StringRef Symbol) {
  bool IsStart = Symbol.consume_front("__start_");
  if (!IsStart && !Symbol.consume_front("__stop_"))
    return std::nullopt;
  if (!isCIdentifier(Symbol))
    return std::nullopt;
  // A C identifier never starts with '.', so it names a group, which occurs
  // once in Sections.
  for (const ImageSection &S : Layout.Sections)
    if (S.Name == Symbol)
      return IsStart ? S.Offset : S.Offset + S.Size;
  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Constructors
//===----------------------------------------------------------------------===//

bool cli::operator<(const ConstructorKey &L, const ConstructorKey &R) {
  assert(L.Rules == R.Rules && "constructor keys of different rules");
  if (L.Rules == SectionRules::ELF)
    return L.Priority < R.Priority;
  return L.Section < R.Section;
}

bool cli::operator==(const ConstructorKey &L, const ConstructorKey &R) {
  assert(L.Rules == R.Rules && "constructor keys of different rules");
  if (L.Rules == SectionRules::ELF)
    return L.Priority == R.Priority;
  return L.Section == R.Section;
}

ConstructorKey cli::getConstructorKey(uint32_t InitPriority,
                                      SectionRules Rules) {
  ConstructorKey K;
  K.Rules = Rules;
  if (Rules == SectionRules::ELF) {
    // clang puts the default priority in plain .init_array, whose slots have
    // priority 65536.
    K.Priority = InitPriority == 65535 ? 65536 : static_cast<int>(InitPriority);
    return K;
  }
  // The section names of getCOFFStaticStructorSection for MSVC.
  if (InitPriority == 65535) {
    K.Section = ".CRT$XCU";
    return K;
  }
  char LastLetter = 'T';
  if (InitPriority < 200)
    LastLetter = 'A';
  else if (InitPriority < 400)
    LastLetter = 'C';
  else if (InitPriority == 400)
    LastLetter = 'L';
  SmallString<24> Name;
  raw_svector_ostream OS(Name);
  OS << ".CRT$XC" << LastLetter;
  if (InitPriority != 200 && InitPriority != 400)
    OS << format("%05u", InitPriority);
  K.Section = std::string(Name);
  return K;
}

ConstructorKey cli::getConstructorKey(StringRef SlotSection,
                                      SectionRules Rules) {
  ConstructorKey K;
  K.Rules = Rules;
  if (Rules == SectionRules::ELF)
    K.Priority = getSectionPriority(SlotSection);
  else
    K.Section = SlotSection.str();
  return K;
}

void cli::sortConstructors(MutableArrayRef<ConstructorEntry> Entries) {
  // A total order, so the result depends only on the entries.
  llvm::stable_sort(
      Entries, [](const ConstructorEntry &L, const ConstructorEntry &R) {
        if (L.Key < R.Key)
          return true;
        if (R.Key < L.Key)
          return false;
        return std::make_tuple(L.Input, !L.IsSlot, L.Segment, L.Offset) <
               std::make_tuple(R.Input, !R.IsSlot, R.Segment, R.Offset);
      });
}

//===----------------------------------------------------------------------===//
// ImageLayoutError
//===----------------------------------------------------------------------===//

char ImageLayoutError::ID = 0;

void ImageLayoutError::log(raw_ostream &OS) const {
  switch (R) {
  case Reason::ThreadLocal:
    OS << "thread-local storage is not supported yet: segment " << Segment
       << " ('" << SegmentName << "')";
    return;
  case Reason::TooLarge:
    OS << "the data image of " << Size
       << " bytes is larger than MSIL supports (2 GiB)";
    return;
  }
  llvm_unreachable("unknown image layout error");
}

std::error_code ImageLayoutError::convertToErrorCode() const {
  return R == Reason::ThreadLocal
             ? std::make_error_code(std::errc::not_supported)
             : std::make_error_code(std::errc::file_too_large);
}

//===----------------------------------------------------------------------===//
// layoutImage
//===----------------------------------------------------------------------===//

namespace {
/// A unit of rule 1: one default segment, or a group of one output section.
struct Unit {
  StringRef Name;
  SmallVector<uint32_t, 1> Members;
  bool HasInit = false;
  bool Placed = false;
};

/// The rule 6 identity of a STRINGS segment: name, flags, alignment, size
/// and bytes, serialized (lookups only; nothing iterates the map).
std::string getSharingKey(const ImageSegment &S) {
  std::string Key;
  raw_string_ostream OS(Key);
  OS << S.Name.size() << ':' << S.Name << ':' << S.Flags << ':'
     << unsigned(S.AlignLog2) << ':' << S.Size << ':';
  OS.write(reinterpret_cast<const char *>(S.Bytes.data()), S.Bytes.size());
  return Key;
}
} // end anonymous namespace

Expected<ImageLayout> cli::layoutImage(const ImageInput &In) {
  const size_t NumSegments = In.Segments.size();
  auto IsZero = [&](uint32_t I) {
    return (In.Segments[I].Flags & MSILLink::SegZero) != 0;
  };
  for (size_t I = 0; I != NumSegments; ++I) {
    const ImageSegment &S = In.Segments[I];
    assert(S.AlignLog2 < 64 && "alignment out of range");
    assert((IsZero(I) ? S.Bytes.empty() : S.Bytes.size() == S.Size) &&
           "a segment has exactly Size bytes unless it is ZERO");
    (void)S;
  }

  // Rule 7, first: phase 3 lays TLS segments out as the TLS template.
  for (size_t I = 0; I != NumSegments; ++I) {
    const ImageSegment &S = In.Segments[I];
    if ((S.Flags & MSILLink::SegTLS) ||
        classifySection(S.Name, In.Rules) == SectionClass::TLS)
      return make_error<ImageLayoutError>(ImageLayoutError::Reason::ThreadLocal,
                                          I, S.Name, 0);
  }

  ImageLayout L;
  L.SegmentOffset.assign(NumSegments, 0);
  L.CommonOffset.assign(In.Commons.size(), 0);
  L.SharedWith.assign(NumSegments, -1);

  // Rule 6: equal STRINGS segments share the first copy.
  StringMap<uint32_t> FirstCopy;
  for (size_t I = 0; I != NumSegments && In.ShareStrings; ++I) {
    if (!(In.Segments[I].Flags & MSILLink::SegStrings))
      continue;
    auto [It, Inserted] =
        FirstCopy.try_emplace(getSharingKey(In.Segments[I]), I);
    if (!Inserted)
      L.SharedWith[I] = It->second;
  }

  // Rule 1: the units, each at its first segment's position.
  std::vector<Unit> Units;
  StringMap<uint32_t> GroupOf;
  SmallVector<int32_t, 0> UnitOf(NumSegments, -1);
  for (size_t I = 0; I != NumSegments; ++I) {
    if (L.SharedWith[I] >= 0)
      continue;
    StringRef Name = In.Segments[I].Name;
    uint32_t U;
    if (Name == ".rodata" || Name == ".data" || Name == ".bss") {
      U = Units.size();
      Units.push_back({Name, {}, false, false});
    } else {
      StringRef Out = getOutputSectionName(Name, In.Rules);
      auto [It, Inserted] = GroupOf.try_emplace(Out, Units.size());
      if (Inserted)
        Units.push_back({Out, {}, false, false});
      U = It->second;
    }
    Units[U].Members.push_back(I);
    Units[U].HasInit |= !IsZero(I);
    UnitOf[I] = U;
  }
  for (Unit &U : Units) {
    if (U.Members.size() < 2)
      continue;
    if (In.Rules == SectionRules::ELF)
      llvm::stable_sort(U.Members, [&](uint32_t A, uint32_t B) {
        return getSectionPriority(In.Segments[A].Name) <
               getSectionPriority(In.Segments[B].Name);
      });
    else
      llvm::stable_sort(U.Members, [&](uint32_t A, uint32_t B) {
        return In.Segments[A].Name < In.Segments[B].Name;
      });
  }

  uint8_t MaxAlignLog2 = 4;
  uint64_t End = 0;
  auto PlaceUnit = [&](Unit &U) {
    U.Placed = true;
    uint64_t Start = 0;
    for (size_t K = 0, E = U.Members.size(); K != E; ++K) {
      const ImageSegment &S = In.Segments[U.Members[K]];
      MaxAlignLog2 = std::max(MaxAlignLog2, S.AlignLog2);
      End = alignTo(End, uint64_t(1) << S.AlignLog2);
      if (K == 0)
        Start = End;
      L.SegmentOffset[U.Members[K]] = End;
      End += S.Size;
    }
    L.Sections.push_back({U.Name.str(), Start, End - Start});
  };

  // Rule 2: the initialized part, in input order.
  for (Unit &U : Units)
    if (U.HasInit)
      PlaceUnit(U);
  L.InitSize = End;
  L.Image.assign(L.InitSize, 0);
  for (const Unit &U : Units) {
    if (!U.HasInit)
      continue;
    for (uint32_t I : U.Members)
      if (!IsZero(I) && In.Segments[I].Size != 0)
        std::memcpy(&L.Image[L.SegmentOffset[I]], In.Segments[I].Bytes.data(),
                    In.Segments[I].Size);
  }

  // Rule 3: the zero part, in ZeroOrder, then whatever it left out.
  SmallVector<bool, 0> CommonPlaced(In.Commons.size(), false);
  auto PlaceZero = [&](bool IsCommon, uint32_t I) {
    if (IsCommon) {
      assert(I < In.Commons.size() && "unknown common symbol");
      if (CommonPlaced[I])
        return;
      CommonPlaced[I] = true;
      const ImageCommon &C = In.Commons[I];
      MaxAlignLog2 = std::max(MaxAlignLog2, C.AlignLog2);
      End = alignTo(End, uint64_t(1) << C.AlignLog2);
      L.CommonOffset[I] = End;
      End += C.Size;
      return;
    }
    assert(I < NumSegments && "unknown segment");
    if (UnitOf[I] < 0)
      return;
    Unit &U = Units[UnitOf[I]];
    if (!U.Placed)
      PlaceUnit(U);
  };
  for (const auto &[IsCommon, I] : In.ZeroOrder)
    PlaceZero(IsCommon, I);
  for (size_t I = 0; I != NumSegments; ++I)
    if (IsZero(I))
      PlaceZero(false, I);
  for (size_t I = 0, E = In.Commons.size(); I != E; ++I)
    PlaceZero(true, I);

  // Rule 6: a shared segment lies where its first copy does.
  for (size_t I = 0; I != NumSegments; ++I)
    if (L.SharedWith[I] >= 0) {
      L.SegmentOffset[I] = L.SegmentOffset[L.SharedWith[I]];
      MaxAlignLog2 = std::max(MaxAlignLog2, In.Segments[I].AlignLog2);
    }

  // Rule 4: the address slots, last.
  L.SlotsOffset = End;
  if (In.NumSlots != 0) {
    L.SlotsOffset = alignTo(End, 8);
    End = L.SlotsOffset + 8 * uint64_t(In.NumSlots);
  }
  L.TotalSize = End;
  L.MaxAlignLog2 = MaxAlignLog2;

  // Rule 5: the module initializer allocates TotalSize + MaxAlign - 1 bytes
  // with int32 sizes (cpblk, initblk, and Phase 1's int32 offsets).
  if (!In.AllowOversize &&
      (MaxAlignLog2 >= 31 ||
       L.TotalSize > uint64_t(INT32_MAX) - ((uint64_t(1) << MaxAlignLog2) - 1)))
    return make_error<ImageLayoutError>(ImageLayoutError::Reason::TooLarge, 0,
                                        StringRef(), L.TotalSize);
  return std::move(L);
}
