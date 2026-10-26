//===- OutputSegment.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The data image of msil-ld's output (llvm/docs/MSIL/ImplementationPlan.md
// 5.9.9; lld/wasm's OutputSegment has the role for wasm): the one block of
// unmanaged memory that '<Module>::.cctor' allocates and fills (decision D12
// as amended for Phase 2). It is laid out by cli::layoutImage, which
// whole-program output shares (so a one-object link equals -S, oracle O3),
// from the live segments of the CIL objects and LTO objects, the common
// symbols that survived resolution and the address slots of the relocation
// loop.
//
// The image also gives the linker-defined __start_X and __stop_X on
// non-Windows triples, folds the symbol differences (DIFF) into its bytes,
// turns the other data relocations into the trailer's run-time relocations
// (cli::RuntimeReloc), and orders the constructor list: the INIT entries and
// the 8-byte slots of the constructor-table sections, by cli::sortConstructors.
// The sections that Phase 2 cannot link (thread-local data, destructor tables
// and the other unsupported ones) are reported by name and kept out of it.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MSIL_OUTPUT_SEGMENT_H
#define LLD_MSIL_OUTPUT_SEGMENT_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CLI/ImageLayout.h"
#include "llvm/CLI/Trailer.h"
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace lld::msil {
class InputSegment;
class OutputModule;
class Symbol;
class SymbolBinder;
class SyntheticSegment;
struct OutputMap;

class OutputImage {
public:
  explicit OutputImage(OutputModule &out) : out(out) {}

  // Reports the live segments that Phase 2 cannot place (5.9.9 rule 7 and
  // the destructor and unsupported sections, 5.9.14's texts) and the FINI
  // entries, lays out the other live segments and the common symbols
  // without address slots, and defines the referenced __start_X and
  // __stop_X of the output sections with C-identifier names on non-Windows
  // triples (SymbolTable::addSyntheticData).
  void layout();

  // After the symbols are bound: turns the relocations of the image's
  // segments into run-time relocations (\p relocs, in site order), folds the
  // symbol differences and the relocations against undefined weak symbols
  // (address 0, plus the addend) into the image, and lays the image out
  // again with the address slots that the relocation loop needs above
  // \p threshold relocations (rule 4: slots are last, so no other offset
  // moves).
  void addRelocations(SymbolBinder &symbols, unsigned threshold,
                      std::vector<llvm::cli::RuntimeReloc> &relocs);

  // The constructor list of 5.9.9 in run order: (method, returns a value).
  // A natively bound function is called through its canonical P/Invoke stub
  // (\p symbols). Reports the slots that hold no function address and the
  // constructors that take arguments.
  std::vector<std::pair<uint32_t, bool>> getConstructors(SymbolBinder &symbols);

  // True if the output has no data image (no segment or common symbol is
  // laid out).
  bool empty() const { return segments.empty() && commons.empty(); }
  const llvm::cli::ImageLayout &getLayout() const { return result; }

  // True if \p seg is laid out in the image.
  bool contains(const InputSegment *seg) const {
    return segmentIndex.count(seg);
  }
  std::optional<uint64_t> getSegmentOffset(const InputSegment *seg) const;
  // The offset of a DATA definition, a common symbol or a linker-defined
  // symbol in the image.
  std::optional<uint64_t> getSymbolOffset(const Symbol *sym) const;

  // The Image: section of the PE map (plan 5.9.14).
  void fillMap(OutputMap &map) const;

private:
  bool runLayout(unsigned numSlots);

  OutputModule &out;
  llvm::cli::ImageInput input;
  llvm::cli::ImageLayout result;
  // The segments and commons of input, in its order.
  SmallVector<InputSegment *, 0> segments;
  SmallVector<SyntheticSegment *, 0> commons;
  llvm::DenseMap<const InputSegment *, uint32_t> segmentIndex;
  llvm::DenseMap<const SyntheticSegment *, uint32_t> commonIndex;
  // Bytes that addRelocations() stores in the image: (offset, value, width).
  struct ImageStore {
    uint64_t offset;
    uint64_t value;
    uint8_t width;
  };
  std::vector<ImageStore> stores;
};
} // namespace lld::msil

#endif
