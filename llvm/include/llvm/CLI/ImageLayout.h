//===- llvm/CLI/ImageLayout.h - MSIL data image layout ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the layout of the data image of an MSIL output
// (ImplementationPlan.md 5.9.9): the one block of unmanaged memory that the
// module initializer allocates and fills (decision D12 as amended for Phase
// 2). msil-ld lays out the live segments of its inputs with it, and
// whole-program output (-S, llc and msil-ld's --lto-emit-asm text) lays out
// one segment per global variable of its module the same way, so a
// one-object link equals -S (oracle O3).
//
// It also classifies named sections (constructor and destructor tables, the
// unsupported and thread-local ones) and orders the constructor list, for
// both users.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLI_IMAGELAYOUT_H
#define LLVM_CLI_IMAGELAYOUT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
class raw_ostream;

namespace cli {

/// The section rules of a triple (5.9.9 rule 1): COFF for Windows triples,
/// ELF for every other one.
enum class SectionRules : uint8_t { ELF, COFF };

/// One live input segment (a SEGMENTS entry of the link-info record, plan
/// 5.8.8, or one global variable of a whole-program module). Flags are the
/// record's segment flags (MSILLink::SegmentFlags): ZERO segments have no
/// Bytes, every other segment has exactly Size bytes. Relocation sites hold
/// zeros.
struct ImageSegment {
  StringRef Name;
  uint32_t Flags = 0;
  uint8_t AlignLog2 = 0;
  uint64_t Size = 0;
  ArrayRef<uint8_t> Bytes;
};

/// One live common symbol, with the size and alignment merged over its
/// declarations (lld/ELF's largest-common rule).
struct ImageCommon {
  uint64_t Size = 0;
  uint8_t AlignLog2 = 0;
};

/// What layoutImage lays out.
struct ImageInput {
  /// The live segments in input order: objects in link order, segments in
  /// record order (rule 2).
  std::vector<ImageSegment> Segments;
  std::vector<ImageCommon> Commons;
  /// The order of the zero part (rule 3): (true, i) is Commons[i], (false, i)
  /// the ZERO segment Segments[i], which places the whole unit of that
  /// segment the first time one of its members is listed. An entry for a
  /// segment whose unit is in the initialized part is skipped. A ZERO segment
  /// or common that the list leaves out follows the listed ones, segments in
  /// index order, then commons; an empty list therefore means every ZERO
  /// unit in input order, then every common.
  std::vector<std::pair<bool, uint32_t>> ZeroOrder;
  /// The number of 8-byte address slots of the relocation loop (rule 4,
  /// countAddressSlots in llvm/CLI/Trailer.h).
  unsigned NumSlots = 0;
  SectionRules Rules = SectionRules::ELF;
  /// Lay out an image above rule 5's limit instead of failing: whole-program
  /// output, which reports the limit with Phase 1's text and still writes its
  /// module (plan section 2, 'Diagnostics leave well-formed output').
  bool AllowOversize = false;
  /// Apply rule 6. Whole-program output turns it off: Phase 1 never shared
  /// two equal strings of one module, and its text must not change.
  bool ShareStrings = true;
};

/// One output section (one unit of rule 1, in placement order): its output
/// name and where its members lie in the image. A segment named .rodata,
/// .data or .bss is a unit of its own, so those names can occur more than
/// once; every other name occurs once.
struct ImageSection {
  std::string Name;
  uint64_t Offset = 0, Size = 0;
};

/// The layout of the data image.
struct ImageLayout {
  /// The offset of each segment and common symbol in the image. A segment
  /// that shares another's copy (rule 6) has that copy's offset.
  std::vector<uint64_t> SegmentOffset, CommonOffset;
  /// Rule 6: the index of the segment whose copy a segment uses, or -1.
  std::vector<int32_t> SharedWith;
  /// The output sections in placement order (__start_/__stop_ symbols,
  /// map files, constructor tables).
  std::vector<ImageSection> Sections;
  /// [0, InitSize) is Image; [InitSize, TotalSize) is zero-filled at startup;
  /// the address slots of the relocation loop are [SlotsOffset, TotalSize),
  /// 8-aligned (SlotsOffset is TotalSize when there are none).
  uint64_t InitSize = 0, SlotsOffset = 0, TotalSize = 0;
  /// The alignment of the image base: at least 16, the alignment malloc
  /// guarantees on the 64-bit triples.
  uint8_t MaxAlignLog2 = 4;
  /// The initialized part [0, InitSize). Relocation sites are zero.
  std::vector<uint8_t> Image;
};

/// What a section name means to the link (5.9.9).
enum class SectionClass : uint8_t {
  Data,
  /// Every 8-byte slot is a constructor: .init_array and .init_array.N (ELF
  /// rules), .CRT$XC* (COFF rules).
  ConstructorTable,
  /// Destructors (phase 3): .fini_array and .fini_array.N (ELF rules),
  /// .CRT$XT* (COFF rules).
  DestructorTable,
  /// Rejected by name: .ctors, .dtors and their .N (and, under COFF rules,
  /// $) forms; under ELF rules .preinit_array and .preinit_array.N; under
  /// COFF rules every other .CRT$X* section (.CRT$XI*, .CRT$XL*, .CRT$XP*,
  /// ...).
  Unsupported,
  /// Thread-local data (phase 3): .tdata, .tbss and their .N forms; under
  /// COFF rules also .tls and .tls$*.
  TLS,
};

/// The class of the input section \p Name under \p Rules.
LLVM_ABI SectionClass classifySection(StringRef Name, SectionRules Rules);

/// The output section of the input section \p Name (rule 1): .init_array.N
/// and .fini_array.N give .init_array and .fini_array under ELF rules, and
/// the part before the first '$' is the output name under COFF rules
/// (lld/COFF's grouping); otherwise the name itself.
LLVM_ABI StringRef getOutputSectionName(StringRef Name, SectionRules Rules);

/// lld/ELF's priority of the input section \p Name (elf::getPriority): N for
/// a name that ends in '.N', 65535 - N for .ctors.N and .dtors.N, and 65536
/// otherwise. Members of an output section are sorted by it under ELF rules.
LLVM_ABI int getSectionPriority(StringRef Name);

/// Whether \p Name is a C identifier, the condition under which a section
/// gets __start_/__stop_ symbols (lld's isValidCIdentifier).
LLVM_ABI bool isCIdentifier(StringRef Name);

/// The offset in \p Layout that the symbol \p Symbol stands for when it is
/// __start_<X> or __stop_<X> and <X> is a C identifier that names one of
/// its output sections: the section's start or end. std::nullopt otherwise.
/// Whether the symbol is defined at all is the caller's rule (5.9.9: not on
/// Windows triples).
LLVM_ABI std::optional<uint64_t> getStartStopOffset(const ImageLayout &Layout,
                                                    StringRef Symbol);

/// The key by which the constructor list is sorted (5.9.9): under ELF rules
/// a priority (an INIT entry's, with 65535 mapped to 65536, or the priority
/// of a slot's input section), compared numerically; under COFF rules a
/// section name (the name clang's COFF lowering gives an INIT entry's
/// priority, or a slot's input section), compared byte-wise.
struct ConstructorKey {
  SectionRules Rules = SectionRules::ELF;
  int Priority = 65536;
  std::string Section;

  LLVM_ABI friend bool operator<(const ConstructorKey &L,
                                 const ConstructorKey &R);
  LLVM_ABI friend bool operator==(const ConstructorKey &L,
                                  const ConstructorKey &R);
};

/// The key of an INIT entry (llvm.global_ctors) of priority \p InitPriority:
/// ELF 65535 -> 65536, else the priority; COFF 65535 -> .CRT$XCU, below 200
/// -> .CRT$XCA<5 digits>, 200 -> .CRT$XCC, 201-399 -> .CRT$XCC<5 digits>, 400
/// -> .CRT$XCL, above 400 -> .CRT$XCT<5 digits>
/// (TargetLoweringObjectFileImpl.cpp, getCOFFStaticStructorSection).
LLVM_ABI ConstructorKey getConstructorKey(uint32_t InitPriority,
                                          SectionRules Rules);
/// The key of an 8-byte slot of the constructor-table input section
/// \p SlotSection: its priority (getSectionPriority) under ELF rules, its full
/// name under COFF rules.
LLVM_ABI ConstructorKey getConstructorKey(StringRef SlotSection,
                                          SectionRules Rules);

/// One entry of the constructor list: an INIT entry or a slot of a
/// constructor-table section.
struct ConstructorEntry {
  ConstructorKey Key;
  /// The input's position in link order (0 for a whole-program module).
  uint32_t Input = 0;
  /// A slot of a constructor-table section; otherwise an INIT entry.
  bool IsSlot = false;
  /// A slot: the index of its segment in its input (SEGMENTS order, which is
  /// IR order) and its offset in that segment. An INIT entry: Segment is 0
  /// and Offset is its index in the input's array order.
  uint32_t Segment = 0;
  uint64_t Offset = 0;
  /// The caller's handle (the function to call), carried through the sort.
  uint32_t Id = 0;
};

/// Sorts \p Entries into 5.9.9's run order: by key, and for equal keys in
/// input order: inputs in link order, and within one input its slots in
/// (segment, offset) order before its INIT entries in array order. That is
/// the order ld.lld and lld-link give a 'clang -c' object.
LLVM_ABI void sortConstructors(MutableArrayRef<ConstructorEntry> Entries);

/// A layout error (5.9.9 rules 5 and 7). TLS segments name the first one;
/// callers add the symbol and the file (5.9.14's texts).
class LLVM_ABI ImageLayoutError : public ErrorInfo<ImageLayoutError> {
public:
  enum class Reason : uint8_t {
    /// A thread-local segment (rule 7): 'thread-local storage is not
    /// supported yet: segment <i> ('<name>')'.
    ThreadLocal,
    /// Rule 5: 'the data image of <n> bytes is larger than MSIL supports
    /// (2 GiB)'.
    TooLarge,
  };

  static char ID;

  ImageLayoutError(Reason R, uint32_t Segment, StringRef SegmentName,
                   uint64_t Size)
      : R(R), Segment(Segment), SegmentName(SegmentName.str()), Size(Size) {}

  Reason getReason() const { return R; }
  /// ThreadLocal: the index of the segment in ImageInput::Segments.
  uint32_t getSegment() const { return Segment; }
  StringRef getSegmentName() const { return SegmentName; }
  /// TooLarge: the size of the image (TotalSize).
  uint64_t getSize() const { return Size; }

  void log(raw_ostream &OS) const override;
  std::error_code convertToErrorCode() const override;

private:
  Reason R;
  uint32_t Segment;
  std::string SegmentName;
  uint64_t Size;
};

/// Lays out the image by 5.9.9's rules:
/// 1. Units: a segment named .rodata, .data or .bss is a unit by itself;
///    every other name is a group of all segments of one output section
///    (getOutputSectionName) at its first segment's position, its members
///    sorted by getSectionPriority (ELF rules) or by full name byte-wise
///    (COFF rules), stably.
/// 2. The initialized part: the units with a non-ZERO segment in input
///    order, each member at its alignment, ZERO members of a mixed group as
///    zero bytes, zero padding. InitSize is the end of the last such unit.
/// 3. The zero part: the ZERO units and the commons in ZeroOrder's order.
/// 4. NumSlots 8-byte address slots, 8-aligned, last.
/// 5. MaxAlign = max(16, every alignment); TotalSize + MaxAlign - 1 must fit
///    in int32 (unless AllowOversize).
/// 6. STRINGS segments with equal name, flags, alignment, size and bytes
///    share the first copy (no tail merging), unless ShareStrings is off.
/// 7. A TLS segment (the TLS flag, or a TLS name) is an error.
/// For one object without commons or explicit sections this is Phase 1's
/// layout: initialized globals in IR order, then zero-initialized ones.
LLVM_ABI Expected<ImageLayout> layoutImage(const ImageInput &In);

} // namespace cli
} // namespace llvm

#endif // LLVM_CLI_IMAGELAYOUT_H
