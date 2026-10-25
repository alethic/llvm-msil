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
/// In object mode (MSILDataSegment::ObjectMode, plan 5.8.10) the AsmPrinter
/// writes each defined global as a segment of its own in the link-info record
/// (decision D12 as amended for Phase 2): the entries keep their bytes (a
/// slice of the image, without alignment padding), relocations may also name
/// aliases, and symbol differences are left unfolded, as DIFF relocations,
/// because msil-ld lays the segments out.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILDATASEGMENT_H
#define LLVM_LIB_TARGET_MSIL_MSILDATASEGMENT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Alignment.h"
#include <cstdint>

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
  /// The offset of the slot in the data segment (always inside the image).
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
  /// The offset of the slot in the data segment (always inside the image).
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
/// In object mode the same layout serves as a store of per-global bytes: each
/// entry's segment is [Offset, Offset + Size) of the image, or zeros
/// (IsZero), and finalize() leaves the symbol differences to the record. The
/// entries follow each other without padding: the record carries each
/// segment's alignment, and msil-ld lays the segments out.
///
/// The segment is [0, TotalSize). Its initialized part [0, InitSize) is Image:
/// the initialized globals in the order of addGlobal calls, each at an offset
/// aligned to its alignment, with zero padding in between. The zero-initialized
/// globals follow it, laid out by finalize() in the same way. The module
/// initializer allocates TotalSize bytes aligned to MaxAlign, copies Image to
/// the start, zero-fills [InitSize, TotalSize), stores each global's address
/// in its address field and then applies Relocs.
struct MSILDataSegment {
  struct Entry {
    const GlobalVariable *GV;
    /// The offset of the global in the segment.
    uint64_t Offset;
    /// The allocation size of the global's value type.
    uint64_t Size;
    /// Whether the global is zero-initialized (its bytes are not in Image).
    bool IsZero = false;
  };
  /// A zero-initialized global that finalize() has not placed yet.
  struct ZeroEntry {
    const GlobalVariable *GV;
    uint64_t Size;
    Align Alignment;
  };

  /// The initial contents of [0, InitSize). Relocation slots hold zeros, and
  /// symbol-difference slots hold their values once finalize() has run.
  SmallVector<uint8_t, 0> Image;
  /// Every global in the segment with its final offset. Before finalize() it
  /// lists the initialized globals; finalize() appends the zero-initialized
  /// ones, so the list is in offset order.
  SmallVector<Entry, 16> Entries;
  /// The zero-initialized globals, in the order of addGlobal calls; finalize()
  /// places them and appends them to Entries.
  SmallVector<ZeroEntry, 8> Zero;
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
  /// initializer is captured into Image through
  /// AsmPrinter::emitGlobalConstant, which \p AP runs with an
  /// MSILDataCollector in place of its OutStreamer. \p SymToGV covers every
  /// global value of the module and turns symbolic initializer values into
  /// relocations. An initializer value that the segment cannot represent is
  /// reported with LLVMContext::emitError naming \p GV by its IL name, and its
  /// bytes stay zero.
  void addGlobal(const GlobalVariable &GV, AsmPrinter &AP,
                 const MSILSymbolMap &SymToGV);

  /// Places the zero-initialized globals after the image, sets InitSize and
  /// TotalSize, and, except in object mode, writes the value of every symbol
  /// difference into Image. Called once, after the last addGlobal.
  void finalize();
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
