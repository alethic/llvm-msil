//===- ImageLayoutTest.cpp - data image layout and constructor tables -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks llvm/CLI/ImageLayout.h (ImplementationPlan.md 5.9.9, M20), the data
// image layout that msil-ld and whole-program output (-S, llc,
// --lto-emit-asm) share:
// - rule 1: .rodata, .data and .bss segments are units by themselves; every
//   other name is a group placed at its first segment's position, named by
//   the segment name except .init_array.N/.fini_array.N on ELF triples and
//   the part before the first '$' on COFF triples; inside a group, by
//   priority on ELF (lld/ELF/OutputSections.cpp:832-879: '.x.N' -> N, plain
//   '.x' -> 65536) and by full name, byte-wise, on COFF, both stable;
// - rule 2: the initialized part (units with a non-ZERO segment, in input
//   order; ZERO members of a mixed group become zero bytes);
// - rule 3: the zero part (ZERO units and commons in ZeroOrder);
// - rule 4: 8-byte address slots, 8-aligned, last;
// - rule 5: MaxAlign = max(16, every alignment) and the 2 GiB limit;
// - rule 6: STRINGS sharing; rule 7: TLS is rejected;
// - Phase 1's MSILDataSegment layout for one object without commons or
//   explicit sections: the layouts of llvm/test/CodeGen/MSIL/
//   module-initializer.ll and global-relocs.ll, offsets and image bytes;
// - classifySection under both rules, getConstructorKey for every priority
//   band of clang's COFF lowering (TargetLoweringObjectFileImpl.cpp:
//   2066-2101), and sortConstructors' stable order (inputs in link order;
//   within one input its slots in (segment, offset) order before its INIT
//   entries of equal key).
// Expected values come from the plan, from lld's sources and from the IL that
// HEAD's llc printed for the two FileCheck tests, never from the
// implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/CLI/ImageLayout.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/Support/Error.h"
#include "llvm/Testing/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::cli;

namespace {

using ByteVec = std::vector<uint8_t>;

constexpr uint32_t Strings = MSILLink::SegStrings;
constexpr uint32_t TLS = MSILLink::SegTLS;
constexpr uint32_t Zero = MSILLink::SegZero;
constexpr uint32_t ReadOnly = MSILLink::SegReadOnly;

// Builds an ImageInput; the byte vectors live as long as the builder.
class InputBuilder {
public:
  explicit InputBuilder(SectionRules Rules = SectionRules::ELF) {
    In.Rules = Rules;
  }

  // A non-ZERO segment with the given bytes; returns its index.
  uint32_t data(StringRef Name, uint8_t AlignLog2, ByteVec Bytes,
                uint32_t Flags = 0) {
    Store.push_back(std::move(Bytes));
    const ByteVec &B = Store.back();
    return add(Name, Flags, AlignLog2, B.size(), B);
  }
  // A non-ZERO segment of Size bytes, each equal to Fill.
  uint32_t fill(StringRef Name, uint8_t AlignLog2, uint64_t Size, uint8_t Fill,
                uint32_t Flags = 0) {
    return data(Name, AlignLog2, ByteVec(Size, Fill), Flags);
  }
  // A ZERO segment.
  uint32_t zero(StringRef Name, uint8_t AlignLog2, uint64_t Size,
                uint32_t ExtraFlags = 0) {
    return add(Name, Zero | ExtraFlags, AlignLog2, Size, {});
  }
  uint32_t common(uint64_t Size, uint8_t AlignLog2) {
    ImageCommon C;
    C.Size = Size;
    C.AlignLog2 = AlignLog2;
    In.Commons.push_back(C);
    return In.Commons.size() - 1;
  }
  void zeroOrder(bool IsCommon, uint32_t Index) {
    In.ZeroOrder.push_back({IsCommon, Index});
  }

  ImageInput In;

private:
  uint32_t add(StringRef Name, uint32_t Flags, uint8_t AlignLog2, uint64_t Size,
               ArrayRef<uint8_t> Bytes) {
    ImageSegment S;
    S.Name = Name;
    S.Flags = Flags;
    S.AlignLog2 = AlignLog2;
    S.Size = Size;
    S.Bytes = Bytes;
    In.Segments.push_back(S);
    return In.Segments.size() - 1;
  }

  std::deque<ByteVec> Store;
};

ImageLayout layoutOrFail(const ImageInput &In) {
  Expected<ImageLayout> L = layoutImage(In);
  if (!L) {
    ADD_FAILURE() << "layoutImage failed: " << toString(L.takeError());
    return ImageLayout();
  }
  return std::move(*L);
}

std::vector<uint64_t> offsets(const ImageLayout &L) {
  return std::vector<uint64_t>(L.SegmentOffset.begin(), L.SegmentOffset.end());
}

ByteVec image(const ImageLayout &L) {
  return ByteVec(L.Image.begin(), L.Image.end());
}

ByteVec le32(uint32_t V) {
  return {uint8_t(V), uint8_t(V >> 8), uint8_t(V >> 16), uint8_t(V >> 24)};
}

ByteVec cat(std::initializer_list<ByteVec> Parts) {
  ByteVec R;
  for (const ByteVec &P : Parts)
    R.insert(R.end(), P.begin(), P.end());
  return R;
}

ByteVec str(StringRef S) {
  ByteVec R(S.begin(), S.end());
  R.push_back(0);
  return R;
}

const ImageSection *findSection(const ImageLayout &L, StringRef Name) {
  const ImageSection *Found = nullptr;
  for (const ImageSection &S : L.Sections)
    if (S.Name == Name) {
      EXPECT_EQ(Found, nullptr) << "two units named " << Name.str();
      Found = &S;
    }
  return Found;
}

void expectSection(const ImageLayout &L, StringRef Name, uint64_t Offset,
                   uint64_t Size) {
  const ImageSection *S = findSection(L, Name);
  ASSERT_NE(S, nullptr) << "no unit named " << Name.str();
  EXPECT_EQ(S->Offset, Offset) << Name.str();
  EXPECT_EQ(S->Size, Size) << Name.str();
}

std::string errorMessage(const ImageInput &In) {
  Expected<ImageLayout> L = layoutImage(In);
  if (L) {
    ADD_FAILURE() << "layoutImage succeeded";
    return "";
  }
  return toString(L.takeError());
}

// module-initializer.ll's program: table (.data, 20 bytes, align 16), the
// strings "zero", "one" and "hello from msil" (.rodata STRINGS READONLY,
// align 1), names (.data, 16 bytes, align 16), op (.data, 8 bytes, align 8)
// and hits (.bss, 4 bytes, align 4), in IR order. Relocation sites hold
// zeros.
void addModuleInitializerProgram(InputBuilder &B) {
  B.data(".data", 4, cat({le32(5), le32(3), le32(9), le32(1), le32(7)}));
  B.data(".rodata", 0, str("zero"), Strings | ReadOnly);
  B.data(".rodata", 0, str("one"), Strings | ReadOnly);
  B.data(".data", 4, ByteVec(16, 0));
  B.data(".data", 3, ByteVec(8, 0));
  B.data(".rodata", 0, str("hello from msil"), Strings | ReadOnly);
  B.zero(".bss", 2, 4);
}

// The image bytes that HEAD's llc printed for module-initializer.ll.
const ByteVec ModuleInitializerImage = {
    0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x7A, 0x65, 0x72, 0x6F,
    0x00, 0x6F, 0x6E, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x65, 0x6C, 0x6C,
    0x6F, 0x20, 0x66, 0x72, 0x6F, 0x6D, 0x20, 0x6D, 0x73, 0x69, 0x6C, 0x00};

//===----------------------------------------------------------------------===//
// Phase 1's layout for one object
//===----------------------------------------------------------------------===//

TEST(CLIImageLayoutTest, Phase1ModuleInitializerLayout) {
  for (SectionRules Rules : {SectionRules::ELF, SectionRules::COFF}) {
    InputBuilder B(Rules);
    addModuleInitializerProgram(B);
    ImageLayout L = layoutOrFail(B.In);
    EXPECT_EQ(offsets(L), (std::vector<uint64_t>{0, 20, 25, 32, 48, 56, 72}));
    EXPECT_EQ(L.InitSize, 72u);
    EXPECT_EQ(L.TotalSize, 76u);
    EXPECT_EQ(L.SlotsOffset, L.TotalSize);
    EXPECT_EQ(L.MaxAlignLog2, 4u);
    EXPECT_EQ(image(L), ModuleInitializerImage);
    EXPECT_EQ(L.SharedWith, (std::vector<int32_t>(7, -1)));
    EXPECT_TRUE(L.CommonOffset.empty());
  }
}

TEST(CLIImageLayoutTest, Phase1GlobalRelocsLayout) {
  // global-relocs.ll's relocs.ll: target, p0, p8, pneg, pend, fp, ifp, tbl,
  // mixed, s4, f4, s4add, self, pz, then the zero-initialized zero.
  InputBuilder B;
  B.data(".data", 4, cat({le32(1), le32(2), le32(3), le32(4)}));
  for (int I = 0; I != 6; ++I)
    B.data(".data", 3, ByteVec(8, 0));
  B.data(".data", 4, ByteVec(24, 0));
  B.data(".data", 3,
         cat({le32(9), ByteVec(12, 0), ByteVec{5, 0}, ByteVec(6, 0)}));
  B.data(".data", 2, cat({le32(0), le32(7)}));
  B.data(".data", 2, ByteVec(4, 0));
  B.data(".data", 2, ByteVec(4, 0));
  B.data(".data", 3, ByteVec(8, 0));
  B.data(".data", 3, ByteVec(8, 0));
  B.zero(".bss", 2, 4);
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(offsets(L),
            (std::vector<uint64_t>{0, 16, 24, 32, 40, 48, 56, 64, 88, 112, 120,
                                   124, 128, 136, 144}));
  EXPECT_EQ(L.InitSize, 144u);
  EXPECT_EQ(L.TotalSize, 148u);
  EXPECT_EQ(L.MaxAlignLog2, 4u);
  ByteVec Expected(144, 0);
  for (int I = 0; I != 4; ++I)
    Expected[4 * I] = I + 1;
  Expected[88] = 9;
  Expected[104] = 5;
  Expected[116] = 7;
  EXPECT_EQ(image(L), Expected);
}

TEST(CLIImageLayoutTest, Phase1UnalignedLayout) {
  // global-relocs.ll's unaligned.ll: x (4, align 4), the packed pk (40 bytes,
  // align 16), q4 (16, align 8), h (2, align 2) and the zero tail zz.
  InputBuilder B(SectionRules::COFF);
  B.data(".data", 2, le32(11));
  ByteVec Pk(40, 0);
  Pk[0] = 0x61;
  Pk[9] = 0x62;
  Pk[18] = 7;
  Pk[28] = 9;
  B.data(".data", 4, Pk);
  ByteVec Q4(16, 0);
  Q4[0] = 1;
  Q4[6] = 2;
  Q4[11] = 3;
  B.data(".data", 3, Q4);
  B.data(".data", 1, ByteVec{5, 0});
  B.zero(".bss", 1, 2);
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(offsets(L), (std::vector<uint64_t>{0, 16, 56, 72, 74}));
  EXPECT_EQ(L.InitSize, 74u);
  EXPECT_EQ(L.TotalSize, 76u);
  ByteVec Img = image(L);
  ASSERT_EQ(Img.size(), 74u);
  EXPECT_EQ(Img[0], 11);
  EXPECT_EQ(Img[16], 0x61);
  EXPECT_EQ(Img[25], 0x62);
  EXPECT_EQ(Img[34], 7);
  EXPECT_EQ(Img[44], 9);
  EXPECT_EQ(Img[56], 1);
  EXPECT_EQ(Img[62], 2);
  EXPECT_EQ(Img[67], 3);
  EXPECT_EQ(Img[72], 5);
  // Padding between x and pk is zero.
  for (unsigned I = 4; I != 16; ++I)
    EXPECT_EQ(Img[I], 0) << I;
}

//===----------------------------------------------------------------------===//
// Rules 1-3: units, groups, the initialized and the zero part
//===----------------------------------------------------------------------===//

TEST(CLIImageLayoutTest, NamedSectionGroupsAtFirstSegment) {
  for (SectionRules Rules : {SectionRules::ELF, SectionRules::COFF}) {
    InputBuilder B(Rules);
    B.data(".data", 2, le32(1));             // 0
    B.data("mysec", 2, le32(0x11));          // 4
    B.data(".data", 2, le32(2));             // 12
    B.data("mysec", 2, le32(0x12));          // 8
    B.data(".rodata", 2, le32(3), ReadOnly); // 16
    B.data(".data", 2, le32(4));             // 20
    ImageLayout L = layoutOrFail(B.In);
    EXPECT_EQ(offsets(L), (std::vector<uint64_t>{0, 4, 12, 8, 16, 20}));
    EXPECT_EQ(L.InitSize, 24u);
    expectSection(L, "mysec", 4, 8);
    EXPECT_EQ(image(L), cat({le32(1), le32(0x11), le32(0x12), le32(2), le32(3),
                             le32(4)}));
  }
}

TEST(CLIImageLayoutTest, DefaultNamesAreUnitsByThemselves) {
  // Two .data segments separated by a group never form one unit: each keeps
  // its own position in input order.
  InputBuilder B;
  B.data(".data", 3, ByteVec(8, 1)); // 0
  B.data("grp", 0, ByteVec(3, 2));   // 8
  B.data(".data", 3, ByteVec(8, 3)); // 16
  B.data("grp", 0, ByteVec(1, 4));   // 11
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(offsets(L), (std::vector<uint64_t>{0, 8, 16, 11}));
  EXPECT_EQ(L.InitSize, 24u);
  expectSection(L, "grp", 8, 4);
}

TEST(CLIImageLayoutTest, InitializedPartBeforeZeroPart) {
  InputBuilder B;
  B.zero(".bss", 2, 4);                       // zero part, first
  B.data(".data", 2, le32(7));                // 0
  B.zero(".bss", 3, 8);                       // zero part, second
  B.data(".rodata", 0, ByteVec{9}, ReadOnly); // 4
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(offsets(L), (std::vector<uint64_t>{8, 0, 16, 4}));
  EXPECT_EQ(L.InitSize, 5u);
  EXPECT_EQ(L.TotalSize, 24u);
  EXPECT_EQ(image(L), (ByteVec{7, 0, 0, 0, 9}));
}

TEST(CLIImageLayoutTest, MixedGroupZeroMembersBecomeZeroBytes) {
  InputBuilder B;
  B.data("mysec", 2, le32(0x11)); // 0
  B.zero("mysec", 2, 4);          // 4: zero bytes inside the image
  B.zero(".bss", 2, 4);           // 12: the zero part
  B.data("mysec", 2, le32(0x12)); // 8
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(offsets(L), (std::vector<uint64_t>{0, 4, 12, 8}));
  EXPECT_EQ(L.InitSize, 12u);
  EXPECT_EQ(L.TotalSize, 16u);
  EXPECT_EQ(image(L), cat({le32(0x11), le32(0), le32(0x12)}));
  expectSection(L, "mysec", 0, 12);
}

TEST(CLIImageLayoutTest, ZeroOnlyGroupsAndZeroOrder) {
  // Without ZeroOrder: ZERO segments in index order.
  {
    InputBuilder B;
    B.data(".data", 2, le32(1)); // 0
    B.zero("zsec", 2, 4);        // 4
    B.zero(".bss", 2, 4);        // 8
    B.zero("zsec", 2, 4);        // 4 + 4: same group as segment 1
    ImageLayout L = layoutOrFail(B.In);
    EXPECT_EQ(offsets(L), (std::vector<uint64_t>{0, 4, 12, 8}));
    EXPECT_EQ(L.InitSize, 4u);
    EXPECT_EQ(L.TotalSize, 16u);
    expectSection(L, "zsec", 4, 8);
  }
  // ZeroOrder places the .bss segment before the group.
  {
    InputBuilder B;
    B.data(".data", 2, le32(1));
    uint32_t G = B.zero("zsec", 2, 4);
    uint32_t Bss = B.zero(".bss", 2, 4);
    B.zeroOrder(false, Bss);
    B.zeroOrder(false, G);
    ImageLayout L = layoutOrFail(B.In);
    EXPECT_EQ(L.SegmentOffset[Bss], 4u);
    EXPECT_EQ(L.SegmentOffset[G], 8u);
    EXPECT_EQ(L.TotalSize, 12u);
  }
}

TEST(CLIImageLayoutTest, CommonsAtTheirZeroOrderPosition) {
  // Commons carry their merged size and alignment and sit at the position of
  // their first declaration among the zero part.
  InputBuilder B;
  B.data(".data", 2, le32(1));
  uint32_t Z = B.zero(".bss", 2, 4);
  uint32_t C0 = B.common(16, 4);
  uint32_t C1 = B.common(2, 1);
  B.zeroOrder(false, Z);
  B.zeroOrder(true, C1);
  B.zeroOrder(true, C0);
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(L.SegmentOffset[Z], 4u);
  ASSERT_EQ(L.CommonOffset.size(), 2u);
  EXPECT_EQ(L.CommonOffset[C1], 8u);
  EXPECT_EQ(L.CommonOffset[C0], 16u);
  EXPECT_EQ(L.InitSize, 4u);
  EXPECT_EQ(L.TotalSize, 32u);
  EXPECT_EQ(L.Image.size(), 4u);
}

TEST(CLIImageLayoutTest, CommonsAfterZeroSegmentsWithoutZeroOrder) {
  InputBuilder B;
  B.data(".data", 2, le32(1));
  uint32_t C0 = B.common(16, 4);
  uint32_t C1 = B.common(2, 1);
  uint32_t Z = B.zero(".bss", 2, 4);
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(L.SegmentOffset[Z], 4u);
  EXPECT_EQ(L.CommonOffset[C0], 16u);
  EXPECT_EQ(L.CommonOffset[C1], 32u);
  EXPECT_EQ(L.TotalSize, 34u);
}

TEST(CLIImageLayoutTest, ElfInitFiniArrayPriorities) {
  // ELF: .init_array.N joins .init_array, sorted by priority (plain -> 65536),
  // stable for equal priorities; .fini_array likewise.
  InputBuilder B(SectionRules::ELF);
  uint32_t A = B.fill(".init_array", 3, 8, 0xA);
  uint32_t P300a = B.fill(".init_array.300", 3, 8, 0xB);
  uint32_t P101 = B.fill(".init_array.101", 3, 8, 0xC);
  uint32_t P300b = B.fill(".init_array.300", 3, 8, 0xD);
  uint32_t P65535 = B.fill(".init_array.65535", 3, 8, 0xE);
  uint32_t F = B.fill(".fini_array", 3, 8, 0xF);
  uint32_t F5 = B.fill(".fini_array.5", 3, 8, 0x10);
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(L.SegmentOffset[P101], 0u);
  EXPECT_EQ(L.SegmentOffset[P300a], 8u);
  EXPECT_EQ(L.SegmentOffset[P300b], 16u);
  EXPECT_EQ(L.SegmentOffset[P65535], 24u);
  EXPECT_EQ(L.SegmentOffset[A], 32u);
  EXPECT_EQ(L.SegmentOffset[F5], 40u);
  EXPECT_EQ(L.SegmentOffset[F], 48u);
  expectSection(L, ".init_array", 0, 40);
  expectSection(L, ".fini_array", 40, 16);
  EXPECT_EQ(findSection(L, ".init_array.101"), nullptr);
}

TEST(CLIImageLayoutTest, CoffKeepsDottedPriorityNames) {
  // COFF: no '$', so every distinct name is its own group (equal names
  // together), in input order.
  InputBuilder B(SectionRules::COFF);
  uint32_t A = B.fill(".init_array", 3, 8, 0xA);
  uint32_t P300a = B.fill(".init_array.300", 3, 8, 0xB);
  uint32_t P101 = B.fill(".init_array.101", 3, 8, 0xC);
  uint32_t P300b = B.fill(".init_array.300", 3, 8, 0xD);
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(L.SegmentOffset[A], 0u);
  EXPECT_EQ(L.SegmentOffset[P300a], 8u);
  EXPECT_EQ(L.SegmentOffset[P300b], 16u);
  EXPECT_EQ(L.SegmentOffset[P101], 24u);
  expectSection(L, ".init_array.300", 8, 16);
}

TEST(CLIImageLayoutTest, CoffDollarGroupsSortByFullName) {
  InputBuilder B(SectionRules::COFF);
  uint32_t D = B.data(".data", 2, le32(1));
  uint32_t Rb = B.data("reg$b", 2, le32(0xB));
  uint32_t M = B.data("mysec", 2, le32(0x11));
  uint32_t Ra1 = B.data("reg$a", 2, le32(0xA));
  uint32_t R0 = B.data("reg", 2, le32(0x10));
  uint32_t RB = B.data("reg$B", 2, le32(0x42));
  uint32_t Ra2 = B.data("reg$a", 2, le32(0xA2));
  ImageLayout L = layoutOrFail(B.In);
  // Group "reg" at reg$b's position: reg, reg$B ('B' < 'a' byte-wise),
  // reg$a (stable: Ra1 before Ra2), reg$b.
  EXPECT_EQ(L.SegmentOffset[D], 0u);
  EXPECT_EQ(L.SegmentOffset[R0], 4u);
  EXPECT_EQ(L.SegmentOffset[RB], 8u);
  EXPECT_EQ(L.SegmentOffset[Ra1], 12u);
  EXPECT_EQ(L.SegmentOffset[Ra2], 16u);
  EXPECT_EQ(L.SegmentOffset[Rb], 20u);
  EXPECT_EQ(L.SegmentOffset[M], 24u);
  expectSection(L, "reg", 4, 20);
  EXPECT_EQ(findSection(L, "reg$a"), nullptr);
}

TEST(CLIImageLayoutTest, ElfDollarNamesAreSeparateGroups) {
  InputBuilder B(SectionRules::ELF);
  uint32_t Rb = B.data("reg$b", 2, le32(0xB));
  uint32_t Ra = B.data("reg$a", 2, le32(0xA));
  uint32_t R0 = B.data("reg", 2, le32(0x10));
  uint32_t Rb2 = B.data("reg$b", 2, le32(0xB2));
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(L.SegmentOffset[Rb], 0u);
  EXPECT_EQ(L.SegmentOffset[Rb2], 4u);
  EXPECT_EQ(L.SegmentOffset[Ra], 8u);
  EXPECT_EQ(L.SegmentOffset[R0], 12u);
  expectSection(L, "reg$b", 0, 8);
  expectSection(L, "reg$a", 8, 4);
  expectSection(L, "reg", 12, 4);
}

TEST(CLIImageLayoutTest, CoffConstructorTableGroup) {
  InputBuilder B(SectionRules::COFF);
  uint32_t U = B.fill(".CRT$XCU", 3, 8, 1);
  uint32_t A = B.fill(".CRT$XCA00101", 3, 8, 2);
  uint32_t Lsec = B.fill(".CRT$XCL", 3, 16, 3);
  uint32_t T = B.fill(".CRT$XCT00500", 3, 8, 4);
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(L.SegmentOffset[A], 0u);
  EXPECT_EQ(L.SegmentOffset[Lsec], 8u);
  EXPECT_EQ(L.SegmentOffset[T], 24u);
  EXPECT_EQ(L.SegmentOffset[U], 32u);
  expectSection(L, ".CRT", 0, 40);
}

TEST(CLIImageLayoutTest, SectionsInPlacementOrder) {
  InputBuilder B(SectionRules::ELF);
  B.data(".data", 2, le32(1));
  B.zero("zz", 2, 4);
  B.data("bb", 2, le32(2));
  B.data("aa", 3, ByteVec(8, 3));
  ImageLayout L = layoutOrFail(B.In);
  expectSection(L, "bb", 4, 4);
  expectSection(L, "aa", 8, 8);
  expectSection(L, "zz", 16, 4);
  for (size_t I = 1; I < L.Sections.size(); ++I)
    EXPECT_LE(L.Sections[I - 1].Offset, L.Sections[I].Offset) << I;
}

//===----------------------------------------------------------------------===//
// Rules 4-7: slots, alignment, size limit, strings, TLS
//===----------------------------------------------------------------------===//

TEST(CLIImageLayoutTest, SlotsAreLastAndEightAligned) {
  InputBuilder B;
  B.data(".data", 0, ByteVec(74, 1));
  B.zero(".bss", 0, 2);
  B.In.NumSlots = 3;
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(L.InitSize, 74u);
  EXPECT_EQ(L.SlotsOffset, 80u);
  EXPECT_EQ(L.TotalSize, 104u);
  EXPECT_EQ(L.Image.size(), 74u);

  B.In.NumSlots = 0;
  ImageLayout L0 = layoutOrFail(B.In);
  EXPECT_EQ(L0.TotalSize, 76u);
  EXPECT_EQ(L0.SlotsOffset, 76u);
}

TEST(CLIImageLayoutTest, BaseAlignmentIsAtLeastSixteen) {
  {
    InputBuilder B;
    B.data(".data", 0, ByteVec{1});
    EXPECT_EQ(layoutOrFail(B.In).MaxAlignLog2, 4u);
  }
  {
    InputBuilder B;
    uint32_t S0 = B.data(".data", 2, le32(1));
    uint32_t S1 = B.data(".data", 5, ByteVec(32, 2));
    ImageLayout L = layoutOrFail(B.In);
    EXPECT_EQ(L.MaxAlignLog2, 5u);
    EXPECT_EQ(L.SegmentOffset[S0], 0u);
    EXPECT_EQ(L.SegmentOffset[S1], 32u);
  }
  {
    InputBuilder B;
    B.data(".data", 2, le32(1));
    uint32_t C = B.common(4, 6);
    ImageLayout L = layoutOrFail(B.In);
    EXPECT_EQ(L.MaxAlignLog2, 6u);
    EXPECT_EQ(L.CommonOffset[C], 64u);
    EXPECT_EQ(L.TotalSize, 68u);
  }
}

TEST(CLIImageLayoutTest, TwoGiBLimit) {
  // TotalSize + MaxAlign - 1 must fit in int32 (Phase 1's limit).
  {
    InputBuilder B;
    B.zero(".bss", 4, 2147483632u);
    ImageLayout L = layoutOrFail(B.In);
    EXPECT_EQ(L.TotalSize, 2147483632u);
  }
  {
    InputBuilder B;
    B.zero(".bss", 4, 2147483633u);
    EXPECT_THAT(errorMessage(B.In),
                testing::HasSubstr("the data image of 2147483633 bytes is "
                                   "larger than MSIL supports (2 GiB)"));
  }
  {
    InputBuilder B;
    B.zero(".bss", 5, 2147483616u);
    EXPECT_EQ(layoutOrFail(B.In).TotalSize, 2147483616u);
  }
  {
    InputBuilder B;
    B.zero(".bss", 5, 2147483617u);
    EXPECT_THAT(errorMessage(B.In),
                testing::HasSubstr("the data image of 2147483617 bytes is "
                                   "larger than MSIL supports (2 GiB)"));
  }
  {
    // Address slots count.
    InputBuilder B;
    B.zero(".bss", 4, 2147483632u - 8);
    B.In.NumSlots = 2;
    EXPECT_THAT(errorMessage(B.In),
                testing::HasSubstr("larger than MSIL supports (2 GiB)"));
  }
  {
    // Whole-program output reports the limit itself.
    InputBuilder B;
    B.zero(".bss", 4, 2147483633u);
    B.In.AllowOversize = true;
    EXPECT_EQ(layoutOrFail(B.In).TotalSize, 2147483633u);
  }
}

TEST(CLIImageLayoutTest, StringsShareTheFirstCopy) {
  InputBuilder B;
  uint32_t S1 = B.data(".rodata", 0, str("abc"), Strings | ReadOnly);
  uint32_t D = B.data(".data", 2, le32(1));
  uint32_t S2 = B.data(".rodata", 0, str("abc"), Strings | ReadOnly);
  uint32_t S3 = B.data(".rodata", 0, str("abd"), Strings | ReadOnly);
  uint32_t S4 = B.data(".rodata", 1, str("abc"), Strings | ReadOnly);
  uint32_t S5 = B.data(".rodata", 0, str("abc"), ReadOnly);
  uint32_t S6 = B.data(".rodata", 0, str("abc"), Strings);
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(L.SharedWith[S1], -1);
  EXPECT_EQ(L.SharedWith[D], -1);
  EXPECT_EQ(L.SharedWith[S2], int32_t(S1));
  EXPECT_EQ(L.SharedWith[S3], -1); // other bytes
  EXPECT_EQ(L.SharedWith[S4], -1); // other alignment
  EXPECT_EQ(L.SharedWith[S5], -1); // not STRINGS
  EXPECT_EQ(L.SharedWith[S6], -1); // other flags
  EXPECT_EQ(L.SegmentOffset[S1], 0u);
  EXPECT_EQ(L.SegmentOffset[D], 4u);
  EXPECT_EQ(L.SegmentOffset[S2], 0u);
  EXPECT_EQ(L.SegmentOffset[S3], 8u);
  EXPECT_EQ(L.SegmentOffset[S4], 12u);
  EXPECT_EQ(L.SegmentOffset[S5], 16u);
  EXPECT_EQ(L.SegmentOffset[S6], 20u);
  EXPECT_EQ(L.InitSize, 24u);
}

TEST(CLIImageLayoutTest, StringsSharingNeedsEqualNames) {
  InputBuilder B;
  uint32_t S1 = B.data(".rodata", 0, str("abc"), Strings | ReadOnly);
  uint32_t S2 = B.data("strs", 0, str("abc"), Strings | ReadOnly);
  uint32_t S3 = B.data("strs", 0, str("abc"), Strings | ReadOnly);
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(L.SharedWith[S2], -1);
  EXPECT_EQ(L.SharedWith[S3], int32_t(S2));
  EXPECT_EQ(L.SegmentOffset[S1], 0u);
  EXPECT_EQ(L.SegmentOffset[S2], 4u);
  EXPECT_EQ(L.SegmentOffset[S3], 4u);
  EXPECT_EQ(L.InitSize, 8u);
}

TEST(CLIImageLayoutTest, StringsSharingCanBeTurnedOff) {
  InputBuilder B;
  B.data(".rodata", 0, str("abc"), Strings | ReadOnly);
  B.data(".rodata", 0, str("abc"), Strings | ReadOnly);
  B.In.ShareStrings = false;
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(offsets(L), (std::vector<uint64_t>{0, 4}));
  EXPECT_EQ(L.SharedWith, (std::vector<int32_t>{-1, -1}));
  EXPECT_EQ(L.InitSize, 8u);
}

TEST(CLIImageLayoutTest, ThreadLocalSegmentsAreRejected) {
  struct Case {
    const char *Name;
    uint32_t Flags;
    bool IsZero;
  } Cases[] = {{".tdata", 0, false},
               {".tbss", 0, true},
               {".data", TLS, false},
               {"tls_named", TLS, true}};
  for (const Case &C : Cases) {
    InputBuilder B;
    B.data(".data", 2, le32(1));
    if (C.IsZero)
      B.zero(C.Name, 2, 4, C.Flags);
    else
      B.data(C.Name, 2, le32(2), C.Flags);
    std::string Msg = errorMessage(B.In);
    EXPECT_THAT(Msg,
                testing::HasSubstr("thread-local storage is not supported yet"))
        << C.Name;
    EXPECT_THAT(Msg, testing::HasSubstr(C.Name));
  }
}

//===----------------------------------------------------------------------===//
// Start and stop symbols, section names and priorities
//===----------------------------------------------------------------------===//

TEST(CLIImageLayoutTest, StartStopOffsets) {
  InputBuilder B(SectionRules::ELF);
  B.data(".data", 2, le32(1));
  B.data("mysec", 2, le32(2));
  B.data("mysec", 2, le32(3));
  B.data(".data.custom", 2, le32(4));
  ImageLayout L = layoutOrFail(B.In);
  EXPECT_EQ(getStartStopOffset(L, "__start_mysec"), std::optional<uint64_t>(4));
  EXPECT_EQ(getStartStopOffset(L, "__stop_mysec"), std::optional<uint64_t>(12));
  EXPECT_FALSE(getStartStopOffset(L, "__start_nosuch").has_value());
  // Not a C identifier: no start or stop symbol.
  EXPECT_FALSE(getStartStopOffset(L, "__start_.data.custom").has_value());
  EXPECT_FALSE(getStartStopOffset(L, "mysec").has_value());
}

TEST(CLIImageLayoutTest, SectionNameHelpers) {
  EXPECT_TRUE(isCIdentifier("mysec"));
  EXPECT_TRUE(isCIdentifier("_a1"));
  EXPECT_TRUE(isCIdentifier("Z"));
  EXPECT_FALSE(isCIdentifier(""));
  EXPECT_FALSE(isCIdentifier("1abc"));
  EXPECT_FALSE(isCIdentifier(".data"));
  EXPECT_FALSE(isCIdentifier("a-b"));
  EXPECT_FALSE(isCIdentifier("reg$a"));

  EXPECT_EQ(
      StringRef(getOutputSectionName(".init_array.101", SectionRules::ELF)),
      ".init_array");
  EXPECT_EQ(StringRef(getOutputSectionName(".fini_array.7", SectionRules::ELF)),
            ".fini_array");
  EXPECT_EQ(StringRef(getOutputSectionName("reg$a", SectionRules::ELF)),
            "reg$a");
  EXPECT_EQ(StringRef(getOutputSectionName("mysec", SectionRules::ELF)),
            "mysec");
  EXPECT_EQ(StringRef(getOutputSectionName(".CRT$XCU", SectionRules::COFF)),
            ".CRT");
  EXPECT_EQ(StringRef(getOutputSectionName("reg$a$b", SectionRules::COFF)),
            "reg");
  EXPECT_EQ(
      StringRef(getOutputSectionName(".init_array.101", SectionRules::COFF)),
      ".init_array.101");

  // lld/ELF's getPriority.
  EXPECT_EQ(getSectionPriority(".init_array.101"), 101);
  EXPECT_EQ(getSectionPriority(".init_array.00200"), 200);
  EXPECT_EQ(getSectionPriority(".init_array"), 65536);
  EXPECT_EQ(getSectionPriority(".init_array.x"), 65536);
  EXPECT_EQ(getSectionPriority("noprio"), 65536);
  EXPECT_EQ(getSectionPriority(".ctors.100"), 65435);
}

//===----------------------------------------------------------------------===//
// Section classes, constructor keys and the constructor order
//===----------------------------------------------------------------------===//

TEST(CLIImageLayoutTest, ClassifySectionElf) {
  const SectionRules R = SectionRules::ELF;
  for (StringRef N : {".init_array", ".init_array.101", ".init_array.65535"})
    EXPECT_EQ(classifySection(N, R), SectionClass::ConstructorTable) << N.str();
  for (StringRef N : {".fini_array", ".fini_array.101"})
    EXPECT_EQ(classifySection(N, R), SectionClass::DestructorTable) << N.str();
  // .preinit_array and .preinit_array.* are unsupported under ELF rules
  // (section 12 row 'Plan 5.9.9 and M20.a (1) amended (`.preinit_array`)'):
  // no silent drop of a function registered there.
  for (StringRef N : {".ctors", ".ctors.100", ".dtors", ".dtors.5",
                      ".preinit_array", ".preinit_array.0"})
    EXPECT_EQ(classifySection(N, R), SectionClass::Unsupported) << N.str();
  for (StringRef N : {".tdata", ".tbss"})
    EXPECT_EQ(classifySection(N, R), SectionClass::TLS) << N.str();
  for (StringRef N : {".data", ".rodata", ".bss", "mysec", ".data.custom",
                      ".rodata.x", "myreg", "reg$a"})
    EXPECT_EQ(classifySection(N, R), SectionClass::Data) << N.str();
}

TEST(CLIImageLayoutTest, ClassifySectionCoff) {
  const SectionRules R = SectionRules::COFF;
  for (StringRef N : {".CRT$XCU", ".CRT$XCA00101", ".CRT$XCC", ".CRT$XCL",
                      ".CRT$XCT00500", ".CRT$XCC00300"})
    EXPECT_EQ(classifySection(N, R), SectionClass::ConstructorTable) << N.str();
  for (StringRef N : {".CRT$XTU", ".CRT$XTA00101"})
    EXPECT_EQ(classifySection(N, R), SectionClass::DestructorTable) << N.str();
  for (StringRef N : {".CRT$XIA", ".CRT$XIC", ".CRT$XLB", ".CRT$XPA",
                      ".CRT$XDA", ".ctors", ".dtors"})
    EXPECT_EQ(classifySection(N, R), SectionClass::Unsupported) << N.str();
  for (StringRef N : {".tdata", ".tbss"})
    EXPECT_EQ(classifySection(N, R), SectionClass::TLS) << N.str();
  // .preinit_array is an ELF name only: COFF rules treat it as data.
  for (StringRef N : {".data", ".rodata", ".bss", "mysec", "reg$a", "myreg",
                      ".preinit_array"})
    EXPECT_EQ(classifySection(N, R), SectionClass::Data) << N.str();
}

TEST(CLIImageLayoutTest, ConstructorKeysElf) {
  const SectionRules R = SectionRules::ELF;
  // INIT priorities: clang puts the default priority in plain .init_array,
  // so 65535 becomes 65536.
  struct {
    uint32_t Priority;
    int Key;
  } Inits[] = {{0, 0}, {101, 101}, {200, 200}, {65534, 65534}, {65535, 65536}};
  for (auto &C : Inits)
    EXPECT_EQ(getConstructorKey(C.Priority, R).Priority, C.Key) << C.Priority;
  // Slots: their input section's priority.
  struct {
    const char *Section;
    int Key;
  } Slots[] = {{".init_array", 65536},
               {".init_array.101", 101},
               {".init_array.00200", 200},
               {".init_array.65535", 65535}};
  for (auto &C : Slots)
    EXPECT_EQ(getConstructorKey(StringRef(C.Section), R).Priority, C.Key)
        << C.Section;
}

TEST(CLIImageLayoutTest, ConstructorKeysCoff) {
  const SectionRules R = SectionRules::COFF;
  // clang's COFF structor sections (TargetLoweringObjectFileImpl.cpp:
  // 2066-2101) for every band.
  struct {
    uint32_t Priority;
    const char *Section;
  } Inits[] = {{0, ".CRT$XCA00000"},     {101, ".CRT$XCA00101"},
               {199, ".CRT$XCA00199"},   {200, ".CRT$XCC"},
               {201, ".CRT$XCC00201"},   {399, ".CRT$XCC00399"},
               {400, ".CRT$XCL"},        {401, ".CRT$XCT00401"},
               {65534, ".CRT$XCT65534"}, {65535, ".CRT$XCU"}};
  for (auto &C : Inits)
    EXPECT_EQ(getConstructorKey(C.Priority, R).Section, C.Section)
        << C.Priority;
  for (const char *S : {".CRT$XCU", ".CRT$XCA00101", ".CRT$XCZ", ".CRT$XCC"})
    EXPECT_EQ(getConstructorKey(StringRef(S), R).Section, S);
}

ConstructorEntry initEntry(const ConstructorKey &Key, uint32_t Input,
                           uint32_t Id) {
  ConstructorEntry E;
  E.Key = Key;
  E.Input = Input;
  E.IsSlot = false;
  E.Segment = 0;
  E.Offset = 0;
  E.Id = Id;
  return E;
}

ConstructorEntry slotEntry(const ConstructorKey &Key, uint32_t Input,
                           uint32_t Segment, uint64_t Offset, uint32_t Id) {
  ConstructorEntry E;
  E.Key = Key;
  E.Input = Input;
  E.IsSlot = true;
  E.Segment = Segment;
  E.Offset = Offset;
  E.Id = Id;
  return E;
}

std::vector<uint32_t> sortedIds(std::vector<ConstructorEntry> Entries) {
  sortConstructors(Entries);
  std::vector<uint32_t> Ids;
  for (const ConstructorEntry &E : Entries)
    Ids.push_back(E.Id);
  return Ids;
}

TEST(CLIImageLayoutTest, SortConstructorsElf) {
  const SectionRules R = SectionRules::ELF;
  auto Init = [&](uint32_t P, uint32_t Input, uint32_t Id) {
    return initEntry(getConstructorKey(P, R), Input, Id);
  };
  auto Slot = [&](StringRef S, uint32_t Input, uint32_t Seg, uint64_t Off,
                  uint32_t Id) {
    return slotEntry(getConstructorKey(S, R), Input, Seg, Off, Id);
  };
  // One object: INIT entries in array order (ids 10-13), slots listed out of
  // (segment, offset) order.
  std::vector<ConstructorEntry> E = {
      Init(65535, 0, 10),
      Init(101, 0, 11),
      Init(200, 0, 12),
      Init(101, 0, 13),
      Slot(".init_array", 0, 5, 8, 20),
      Slot(".init_array", 0, 5, 0, 21),
      Slot(".init_array.101", 0, 3, 0, 22),
      Slot(".init_array.300", 0, 4, 8, 23),
      Slot(".init_array.300", 0, 4, 0, 24),
      Slot(".init_array", 0, 2, 0, 25),
  };
  EXPECT_EQ(sortedIds(E),
            (std::vector<uint32_t>{22, 11, 13, 12, 24, 23, 25, 21, 20, 10}));
}

TEST(CLIImageLayoutTest, SortConstructorsCoff) {
  const SectionRules R = SectionRules::COFF;
  auto Init = [&](uint32_t P, uint32_t Input, uint32_t Id) {
    return initEntry(getConstructorKey(P, R), Input, Id);
  };
  auto Slot = [&](StringRef S, uint32_t Input, uint32_t Seg, uint64_t Off,
                  uint32_t Id) {
    return slotEntry(getConstructorKey(S, R), Input, Seg, Off, Id);
  };
  std::vector<ConstructorEntry> E = {
      Init(65535, 0, 10), // .CRT$XCU
      Init(101, 0, 11),   // .CRT$XCA00101
      Init(200, 0, 12),   // .CRT$XCC
      Init(300, 0, 13),   // .CRT$XCC00300
      Slot(".CRT$XCU", 0, 7, 0, 20),
      Slot(".CRT$XCA00101", 0, 6, 8, 21),
      Slot(".CRT$XCA00101", 0, 6, 0, 22),
      Slot(".CRT$XCC00300", 0, 2, 0, 23),
      Slot(".CRT$XCT00500", 0, 1, 0, 24),
  };
  // Byte-wise: .CRT$XCA00101 < .CRT$XCC < .CRT$XCC00300 < .CRT$XCT00500 <
  // .CRT$XCU.
  EXPECT_EQ(sortedIds(E),
            (std::vector<uint32_t>{22, 21, 11, 12, 23, 13, 24, 20, 10}));
}

TEST(CLIImageLayoutTest, SortConstructorsAcrossInputs) {
  // Equal keys: inputs in link order first, then an input's slots before its
  // INIT entries.
  for (SectionRules R : {SectionRules::ELF, SectionRules::COFF}) {
    StringRef Plain = R == SectionRules::ELF ? ".init_array" : ".CRT$XCU";
    ConstructorKey Default = getConstructorKey(uint32_t(65535), R);
    ConstructorKey SlotKey = getConstructorKey(Plain, R);
    std::vector<ConstructorEntry> E = {
        initEntry(Default, 1, 1),       slotEntry(SlotKey, 1, 0, 0, 2),
        initEntry(Default, 0, 3),       initEntry(Default, 0, 4),
        slotEntry(SlotKey, 0, 1, 0, 5), slotEntry(SlotKey, 2, 0, 0, 6),
        initEntry(Default, 2, 7),
    };
    EXPECT_EQ(sortedIds(E), (std::vector<uint32_t>{5, 3, 4, 2, 1, 6, 7}));
  }
}

TEST(CLIImageLayoutTest, SortConstructorsIsStableForEqualEntries) {
  // INIT entries of one input with one key keep array order, whatever their
  // ids; so do slots at distinct (segment, offset) in order.
  const SectionRules R = SectionRules::ELF;
  ConstructorKey K = getConstructorKey(uint32_t(500), R);
  std::vector<ConstructorEntry> E;
  for (uint32_t I = 0; I != 6; ++I)
    E.push_back(initEntry(K, 0, 100 + I));
  EXPECT_EQ(sortedIds(E),
            (std::vector<uint32_t>{100, 101, 102, 103, 104, 105}));
}

} // namespace
