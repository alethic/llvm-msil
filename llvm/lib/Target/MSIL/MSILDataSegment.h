//===-- MSILDataSegment.h - MSIL module data segment ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the data segment of an MSIL module (decision D12): every
/// defined global variable lives in one block of unmanaged memory that the
/// module initializer allocates. MSILDataSegment lays the globals out and holds
/// the initializer bytes; the AsmPrinter prints them as an RVA blob that the
/// initializer copies into the block, followed by the address stores that
/// relocations need.
///
/// The bytes come from AsmPrinter::emitGlobalConstant, the code that native
/// targets use for their data sections: addGlobal temporarily replaces the
/// AsmPrinter's OutStreamer with an MSILDataCollector, which appends plain
/// bytes to the image and turns every symbolic value into a relocation record.
///
/// Every global is captured as a segment of its own (decision D12 as amended
/// for Phase 2): its bytes are a slice of the image, without alignment
/// padding, until finalize() lays the segments out.
///
/// In object mode (MSILDataSegment::ObjectMode, plan 5.8.10) the AsmPrinter
/// writes each segment into the link-info record: relocations may also name
/// aliases, and symbol differences are left unfolded, as DIFF relocations,
/// because msil-ld lays the segments out.
///
/// In whole-program mode (-S, llc and msil-ld's --lto-emit-asm text) the
/// module is a one-object link (Q5, Design.md 4.1): finalize() lays the
/// segments out with cli::layoutImage, each with object mode's name and
/// flags (plan 5.8.8, 5.9.9), so named sections are grouped as a link groups
/// them. A module without explicit sections keeps Phase 1's layout and text:
/// only a zero or undef initializer is zero-initialized (an object's ZERO
/// test also accepts an aggregate of zeros and undefs), and equal strings
/// are not shared (cli::ImageInput::ShareStrings).
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILDATASEGMENT_H
#define LLVM_LIB_TARGET_MSIL_MSILDATASEGMENT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/CLI/ImageLayout.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/Alignment.h"
#include <cstdint>
#include <string>

namespace llvm {

class AsmPrinter;
class GlobalValue;
class GlobalVariable;
class MCContext;
class MCExpr;
class MCInst;
class MCSection;
class MCSubtargetInfo;
class MCSymbol;
class TargetMachine;
class Twine;

/// Maps the symbol of every global value of a module (AsmPrinter::getSymbol,
/// the symbols that AsmPrinter::lowerConstant refers to) back to the value.
using MSILSymbolMap = DenseMap<const MCSymbol *, const GlobalValue *>;

/// A slot of the data image that holds the address of a global value plus an
/// addend. The image holds zeros there; the module initializer stores the
/// address once the segment is allocated.
struct MSILReloc {
  /// The offset of the slot in the image (in the byte store of the captured
  /// initializers until a whole-program finalize() moves it).
  uint64_t Site;
  /// A global variable or a function, defined in the module or not, or in
  /// object mode an alias. The addend of a function address (a function or
  /// an alias with a function type) is always 0.
  const GlobalValue *Target;
  int64_t Addend;
  /// The width of the slot in bytes: 8 (a pointer on the 64-bit triples) or
  /// 4 (an address truncated to 32 bits).
  uint8_t Size;
  /// The global variable whose initializer contains the slot.
  const GlobalVariable *Owner;
};

/// A slot of the data image that holds the difference of the addresses of two
/// global variables plus an addend, A - B + Addend (relative tables). Both
/// variables live in the data segment, so the value is known once the layout
/// is; MSILDataSegment::finalize writes it into the image.
struct MSILDiff {
  /// The offset of the slot in the image (in the byte store of the captured
  /// initializers until a whole-program finalize() moves it).
  uint64_t Site;
  /// Global variables; in object mode also declarations and aliases of
  /// variables (data symbols).
  const GlobalValue *A;
  const GlobalValue *B;
  int64_t Addend;
  /// The width of the slot in bytes (1 to 16).
  uint8_t Size;
  /// The global variable whose initializer contains the slot (diagnostics).
  const GlobalVariable *Owner;
};

/// The layout and the initial contents of the module's global variables.
///
/// Until finalize() the image is a store of per-global bytes: each
/// initialized entry's segment is [Offset, Offset + Size) of Image, the
/// entries follow each other without padding, and relocation sites are
/// offsets in that store.
///
/// In object mode it stays so: zero-initialized entries (IsZero) are
/// appended by finalize() after the image, and finalize() leaves the symbol
/// differences to the record, which carries each segment's alignment;
/// msil-ld lays the segments out.
///
/// In whole-program mode finalize() lays the segments out (Layout), moves
/// each entry, relocation and symbol difference to its final offset, folds
/// the symbol differences into the image, and lists the entries in offset
/// order. The segment is [0, TotalSize). Its initialized part [0, InitSize)
/// is getImage(); without explicit sections it holds the initialized globals
/// in the order of addGlobal calls, each at an offset aligned to its
/// alignment, with zero padding in between, and the zero-initialized globals
/// follow it in the same way. The module initializer allocates TotalSize
/// bytes aligned to MaxAlign, copies the image to the start, zero-fills
/// [InitSize, TotalSize), stores each global's address in its address field
/// and then applies Relocs.
struct MSILDataSegment {
  struct Entry {
    Entry(const GlobalVariable *GV, uint64_t Offset, uint64_t Size,
          bool IsZero = false)
        : GV(GV), Offset(Offset), Size(Size), IsZero(IsZero) {}

    const GlobalVariable *GV;
    /// The offset of the global: in the byte store before finalize() (and in
    /// object mode after it), in the segment after a whole-program
    /// finalize().
    uint64_t Offset;
    /// The allocation size of the global's value type.
    uint64_t Size;
    /// Whether the global is zero-initialized (its bytes are not in Image).
    bool IsZero = false;
    /// Whole-program mode: the segment of the global, with object mode's
    /// name and flags (plan 5.8.8; RETAIN, which only --gc-sections reads,
    /// is left out: the module keeps every global; ZERO follows IsZero) and
    /// its alignment.
    std::string Section;
    uint32_t SegFlags = 0;
    uint8_t AlignLog2 = 0;
    /// Whole-program mode: the position of the global in addGlobal order (its
    /// SEGMENTS index in an object), which orders constructor-table slots.
    unsigned Index = 0;
    /// Whole-program mode: false for a thread-local segment, which finalize()
    /// leaves out of the image (phase 3); the AsmPrinter reports it.
    bool InImage = true;
  };
  /// A zero-initialized global that finalize() has not placed yet.
  struct ZeroEntry {
    const GlobalVariable *GV;
    uint64_t Size;
    Align Alignment;
  };

  /// The bytes of the initialized globals (see above); object mode only
  /// after finalize(), when whole-program mode's image is Layout.Image
  /// (getImage()). Relocation slots hold zeros.
  SmallVector<uint8_t, 0> Image;
  /// Every global in the segment. In object mode it lists the initialized
  /// globals until finalize() appends the zero-initialized ones, so the list
  /// is in offset order. In whole-program mode it lists every global in
  /// addGlobal order until finalize() sorts it by final offset (the
  /// thread-local ones, which have none, last).
  SmallVector<Entry, 16> Entries;
  /// Object mode: the zero-initialized globals, in the order of addGlobal
  /// calls; finalize() places them and appends them to Entries.
  SmallVector<ZeroEntry, 8> Zero;
  /// Whole-program mode: the layout from cli::layoutImage, set by finalize().
  /// Its Image is the initialized part, with the symbol differences folded
  /// in; its Sections are the output sections.
  cli::ImageLayout Layout;
  /// Address slots for the module initializer, in image order.
  SmallVector<MSILReloc, 0> Relocs;
  /// Symbol-difference slots, folded into Image by finalize().
  SmallVector<MSILDiff, 0> Diffs;
  /// The alignment of the segment base: the largest alignment of a global, at
  /// least 16 (the alignment that malloc guarantees on the 64-bit triples).
  Align MaxAlign = Align(16);
  /// The size of the initialized part (the image); valid after finalize().
  uint64_t InitSize = 0;
  /// The size of the whole segment; valid after finalize().
  uint64_t TotalSize = 0;
  /// Set by finalize(); no global can be added afterwards.
  bool Finalized = false;
  /// The target machine of the AsmPrinter that addGlobal receives, which
  /// names globals in diagnostics (MSIL::getILName); set by every addGlobal.
  const TargetMachine *TM = nullptr;
  /// Object mode (MSILTargetMachine::isObjectMode()): relocations may name
  /// aliases and undefined data, and finalize() does not fold the symbol
  /// differences.
  bool ObjectMode = false;

  /// Adds the definition \p GV (any linkage) to the segment. A zero or undef
  /// initializer (in object mode also an aggregate of zeros and undefs, as
  /// getKindForGlobal's BSS test accepts it) only reserves space; any other
  /// initializer is captured into Image through AsmPrinter::emitGlobalConstant,
  /// which \p AP runs with an MSILDataCollector in place of its OutStreamer.
  /// \p SymToGV covers every global value of the module and turns symbolic
  /// initializer values into relocations. An initializer value that the
  /// segment cannot represent is reported with LLVMContext::emitError naming
  /// \p GV by its IL name, and its bytes stay zero.
  void addGlobal(const GlobalVariable &GV, AsmPrinter &AP,
                 const MSILSymbolMap &SymToGV);

  /// Called once, after the last addGlobal. Object mode: places the
  /// zero-initialized globals after the image and sets InitSize and
  /// TotalSize. Whole-program mode: lays the segments out with
  /// cli::layoutImage under \p Rules, leaving thread-local segments out and
  /// reserving the address slots that a module initializer with more than
  /// \p RelocLoopThreshold relocations needs (cli::countAddressSlots, the
  /// relocations whose target is not in the image); moves every entry,
  /// relocation (in site order) and symbol difference to its final offset;
  /// sets InitSize, TotalSize and MaxAlign; and writes the value of every
  /// symbol difference into the image. A layout above 2 GiB is kept: the
  /// AsmPrinter reports it.
  void finalize(cli::SectionRules Rules = cli::SectionRules::ELF,
                unsigned RelocLoopThreshold = 32);

  /// The initialized part [0, InitSize) after finalize().
  ArrayRef<uint8_t> getImage() const {
    return ObjectMode ? ArrayRef<uint8_t>(Image)
                      : ArrayRef<uint8_t>(Layout.Image);
  }

  /// The segment name of the definition \p GV of section kind \p Kind (plan
  /// 5.8.8, SEGMENTS): its IR section verbatim, else .rodata, .data, .bss,
  /// .tdata or .tbss by \p Kind (TargetLoweringObjectFile::getKindForGlobal).
  static std::string getSegmentName(const GlobalVariable &GV, SectionKind Kind);
};

/// An MCStreamer that receives the output of AsmPrinter::emitGlobalConstant
/// for one global variable and appends it to the image of an MSILDataSegment:
/// bytes, fills and integers become image bytes, and every value that names a
/// global becomes an MSILReloc or MSILDiff over zero bytes (in object mode
/// also one that names an alias, or a difference of undefined data).
/// Directives that
/// AsmPrinter::emitGlobalConstant never emits (labels, alignment, '.fill',
/// LEB128, common symbols, instructions) are internal errors.
class MSILDataCollector final : public MCStreamer {
  MSILDataSegment &Seg;
  const MSILSymbolMap &SymToGV;
  /// The global whose initializer is being captured.
  const GlobalVariable &Cur;
  /// Names Cur in diagnostics (MSIL::getILName).
  const TargetMachine &TM;
  /// The messages already reported for Cur, so each is reported once.
  StringSet<> Reported;

public:
  MSILDataCollector(MCContext &Ctx, MSILDataSegment &Seg,
                    const MSILSymbolMap &SymToGV, const GlobalVariable &Cur,
                    const TargetMachine &TM);

  // Pure virtual in MCStreamer.
  bool emitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override;
  void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        Align ByteAlignment) override;

  // Data that becomes image bytes.
  using MCStreamer::emitFill;
  using MCStreamer::emitIntValue;
  void emitBytes(StringRef Data) override;
  void emitIntValue(uint64_t Value, unsigned Size) override;
  void emitValueImpl(const MCExpr *Value, unsigned Size,
                     SMLoc Loc = SMLoc()) override;
  void emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                SMLoc Loc = SMLoc()) override;
  void emitFill(const MCExpr &NumValues, int64_t Size, int64_t Expr,
                SMLoc Loc = SMLoc()) override;

  // Never emitted for a global constant (internal errors).
  void emitLabel(MCSymbol *Symbol, SMLoc Loc = SMLoc()) override;
  void emitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                             Align ByteAlignment) override;
  void emitZerofill(MCSection *Section, MCSymbol *Symbol = nullptr,
                    uint64_t Size = 0, Align ByteAlignment = Align(1),
                    SMLoc Loc = SMLoc()) override;
  void emitTBSSSymbol(MCSection *Section, MCSymbol *Symbol, uint64_t Size,
                      Align ByteAlignment = Align(1)) override;
  void emitULEB128Value(const MCExpr *Value) override;
  void emitSLEB128Value(const MCExpr *Value) override;
  void emitValueToAlignment(Align Alignment, int64_t Fill = 0,
                            uint8_t FillLen = 1,
                            unsigned MaxBytesToEmit = 0) override;
  void emitCodeAlignment(Align Alignment, const MCSubtargetInfo &STI,
                         unsigned MaxBytesToEmit = 0) override;
  void emitValueToOffset(const MCExpr *Offset, unsigned char Value,
                         SMLoc Loc) override;
  void emitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI) override;

private:
  /// Reports \p Msg about the initializer of Cur, once per message.
  void fail(const Twine &Msg);
  /// Appends \p Size zero bytes (the slot of a relocation or an error).
  void appendZeros(uint64_t Size);
  [[noreturn]] void unexpected(StringRef What);
};

} // end namespace llvm

#endif
