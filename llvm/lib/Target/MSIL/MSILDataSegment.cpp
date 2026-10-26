//===-- MSILDataSegment.cpp - MSIL module data segment --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file lays out the data segment of an MSIL module and captures the
/// initializer bytes of its global variables (decision D12): see
/// MSILDataSegment.h.
///
//===----------------------------------------------------------------------===//

#include "MSILDataSegment.h"
#include "MSILUtilities.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/Trailer.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

/// Reports \p Msg about the initializer of \p GV, named by its IL name as the
/// AsmPrinter's diagnostics about globals name it.
static void reportInitializerError(const GlobalVariable &GV,
                                   const TargetMachine &TM, const Twine &Msg) {
  GV.getContext().emitError("MSIL: " + Msg + " in initializer of '" +
                            MSIL::getILName(GV, TM) + "'");
}

/// Stores \p Value at \p Dst as a \p Size-byte little-endian integer: the low
/// bytes of a smaller slot (an assembler truncates a value to its slot the
/// same way), sign-extended into a slot wider than 8 bytes.
static void storeLE(uint8_t *Dst, unsigned Size, int64_t Value) {
  uint64_t Bits = static_cast<uint64_t>(Value);
  for (unsigned I = 0; I != Size; ++I)
    Dst[I] = I < 8 ? static_cast<uint8_t>(Bits >> (8 * I))
                   : (Value < 0 ? 0xFF : 0x00);
}

/// Returns why \p Init cannot become segment data, or an empty string, for the
/// constants that AsmPrinter::lowerConstant cannot lower without a native
/// object file: a block address names a label inside a method (and a block of
/// a function printed already), a dso_local_equivalent lowers to a null
/// expression on targets without PLT relocations, and a ptrauth constant is a
/// fatal error. MSILPrepareIR has erased aliases and ifuncs already.
static StringRef findUnsupportedConstant(const Constant *Init) {
  SmallVector<const Constant *, 16> Worklist = {Init};
  SmallPtrSet<const Constant *, 16> Visited;
  while (!Worklist.empty()) {
    const Constant *C = Worklist.pop_back_val();
    // A global value contributes its address, never its initializer.
    if (isa<GlobalValue>(C) || !Visited.insert(C).second)
      continue;
    if (isa<BlockAddress>(C))
      return "address of a non-global (block address)";
    if (isa<DSOLocalEquivalent>(C))
      return "unsupported constant expression (dso_local_equivalent)";
    if (isa<ConstantPtrAuth>(C))
      return "unsupported constant expression (ptrauth)";
    for (const Use &Op : C->operands())
      if (const auto *OpC = dyn_cast<Constant>(Op.get()))
        Worklist.push_back(OpC);
  }
  return {};
}

/// Whether \p C holds only zeros and undefs: upstream's isNullOrUndef
/// (file-static in TargetLoweringObjectFile.cpp), the test by which
/// getKindForGlobal puts a global in BSS.
static bool isNullOrUndef(const Constant *C) {
  if (C->isNullValue() || isa<UndefValue>(C))
    return true;
  if (!isa<ConstantAggregate>(C))
    return false;
  return all_of(C->operand_values(), [](const Value *Op) {
    return isNullOrUndef(cast<Constant>(Op));
  });
}

//===----------------------------------------------------------------------===//
// MSILDataSegment
//===----------------------------------------------------------------------===//

void MSILDataSegment::addGlobal(const GlobalVariable &GV, AsmPrinter &AP,
                                const MSILSymbolMap &SymToGV) {
  assert(!Finalized && "global added to a finalized data segment");
  assert(GV.hasInitializer() && "only definitions live in the data segment");
  TM = &AP.TM;
  const DataLayout &DL = GV.getDataLayout();
  assert(DL.isLittleEndian() && "the image is written little-endian");
  // The size and alignment a native target gives the global, so the C
  // layout of every object, and the padding between them, stay native.
  uint64_t Size = GV.getGlobalSize(DL);
  Align Alignment = AsmPrinter::getGVAlignment(&GV, DL);
  MaxAlign = std::max(MaxAlign, Alignment);

  const Constant *Init = GV.getInitializer();
  // An object's segment is ZERO (SHT_NOBITS, plan 5.8.8) by the test that
  // names it '.bss', which also accepts an aggregate of zeros and undefs;
  // whole-program output keeps Phase 1's top-level test, so its text does
  // not change.
  bool IsZero = ObjectMode ? isNullOrUndef(Init)
                           : Init->isNullValue() || isa<UndefValue>(Init);
  if (ObjectMode && IsZero) {
    Zero.push_back({&GV, Size, Alignment});
    return;
  }

  // The bytes are packed without padding, which would cost as much memory as
  // the alignment: msil-ld lays an object's segments out from the alignment
  // in the record, and finalize() a whole-program module's.
  uint64_t Offset = Image.size();
  if (ObjectMode) {
    Entries.push_back({&GV, Offset, Size});
  } else {
    // Whole-program mode: one segment per global with object mode's name
    // and flags (plan 5.8.8, 5.9.9), in addGlobal (IR) order.
    SectionKind Kind = TargetLoweringObjectFile::getKindForGlobal(&GV, AP.TM);
    Entry E{&GV, IsZero ? 0 : Offset, Size, IsZero};
    E.Section = getSegmentName(GV, Kind);
    if (Kind.isMergeable1ByteCString())
      E.SegFlags |= MSILLink::SegStrings;
    if (Kind.isThreadLocal())
      E.SegFlags |= MSILLink::SegTLS;
    if (IsZero)
      E.SegFlags |= MSILLink::SegZero;
    if (Kind.isReadOnly() || Kind.isReadOnlyWithRel())
      E.SegFlags |= MSILLink::SegReadOnly;
    E.AlignLog2 = Log2(Alignment);
    E.Index = Entries.size();
    Entries.push_back(std::move(E));
    if (IsZero)
      return;
  }

  if (StringRef Why = findUnsupportedConstant(Init); !Why.empty()) {
    reportInitializerError(GV, AP.TM, Why);
    Image.resize(Offset + Size, 0);
    return;
  }

  // Run the native targets' constant emitter into the collector: it handles
  // every constant kind (struct padding, vectors, odd-sized integers, FP
  // formats, string data) and folds constant expressions into symbol +
  // offset or symbol difference + offset values.
  std::unique_ptr<MCStreamer> Collector = std::make_unique<MSILDataCollector>(
      AP.OutContext, *this, SymToGV, GV, AP.TM);
  std::swap(AP.OutStreamer, Collector);
  AP.emitGlobalConstant(DL, Init);
  std::swap(AP.OutStreamer, Collector);

  // Every value the collector receives occupies its full size, so a mismatch
  // means AsmPrinter emitted something the collector does not understand.
  if (Image.size() != Offset + Size)
    reportFatalInternalError("MSIL: the initializer of '" +
                             MSIL::getILName(GV, AP.TM) + "' was captured as " +
                             Twine(Image.size() - Offset) +
                             " bytes instead of " + Twine(Size));
}

std::string MSILDataSegment::getSegmentName(const GlobalVariable &GV,
                                            SectionKind Kind) {
  if (GV.hasSection())
    return GV.getSection().str();
  if (Kind.isThreadBSS())
    return ".tbss";
  if (Kind.isThreadData())
    return ".tdata";
  if (Kind.isBSS())
    return ".bss";
  if (Kind.isReadOnly() || Kind.isReadOnlyWithRel())
    return ".rodata";
  return ".data";
}

void MSILDataSegment::finalize(cli::SectionRules Rules,
                               unsigned RelocLoopThreshold) {
  assert(!Finalized && "the data segment is finalized once");
  Finalized = true;

  if (ObjectMode) {
    // The zero-initialized globals follow the image, packed as in
    // addGlobal: an object leaves the layout and the symbol differences to
    // msil-ld (DIFF relocations, plan 5.8.8).
    InitSize = Image.size();
    uint64_t End = InitSize;
    for (const ZeroEntry &Z : Zero) {
      Entries.push_back({Z.GV, End, Z.Size, /*IsZero=*/true});
      End += Z.Size;
    }
    TotalSize = End;
    return;
  }

  // Whole-program mode: a one-object link (plan 5.9.9). Thread-local
  // segments (phase 3) stay out of the image, with the relocations and
  // symbol differences inside them; the AsmPrinter reports them.
  cli::ImageInput In;
  In.Rules = Rules;
  // Phase 1's limit and text are the AsmPrinter's, and Phase 1 shared no
  // strings.
  In.AllowOversize = true;
  In.ShareStrings = false;
  SmallVector<int32_t, 16> SegOf(Entries.size(), -1);
  DenseMap<const GlobalValue *, unsigned> EntryOf;
  for (unsigned I = 0, E = Entries.size(); I != E; ++I) {
    Entry &En = Entries[I];
    EntryOf.try_emplace(En.GV, I);
    if ((En.SegFlags & MSILLink::SegTLS) ||
        cli::classifySection(En.Section, Rules) == cli::SectionClass::TLS) {
      En.InImage = false;
      continue;
    }
    SegOf[I] = In.Segments.size();
    if (En.IsZero)
      In.ZeroOrder.push_back({false, SegOf[I]});
    In.Segments.push_back(
        {En.Section, En.SegFlags, En.AlignLog2, En.Size,
         En.IsZero ? ArrayRef<uint8_t>()
                   : ArrayRef<uint8_t>(Image).slice(En.Offset, En.Size)});
  }
  auto InImage = [&](const GlobalValue *GV) {
    auto It = EntryOf.find(GV);
    return It != EntryOf.end() && Entries[It->second].InImage;
  };
  erase_if(Relocs, [&](const MSILReloc &R) { return !InImage(R.Owner); });
  erase_if(Diffs, [&](const MSILDiff &D) { return !InImage(D.Owner); });

  // The address slots of a relocation loop (plan 5.9.9 rule 4): one per
  // target outside the image and addend. The AsmPrinter's relocations name
  // one field per target, so cli::buildTrailer counts the same.
  std::vector<cli::RuntimeReloc> Slots;
  DenseMap<const GlobalValue *, cli::EntityRef> TargetIds;
  for (const MSILReloc &R : Relocs) {
    cli::EntityRef Id =
        TargetIds.try_emplace(R.Target, TargetIds.size() + 1).first->second;
    std::optional<uint64_t> ImageOffset;
    if (InImage(R.Target))
      ImageOffset = 0;
    Slots.push_back({R.Site, R.Size, Id, ImageOffset, R.Addend});
  }
  In.NumSlots = cli::countAddressSlots(Slots, RelocLoopThreshold);

  // AllowOversize leaves only the TLS error, and the TLS segments are out.
  Layout = cantFail(cli::layoutImage(In));

  // Every global, relocation and symbol difference at its final offset.
  SmallVector<uint64_t, 16> StoreOffset(Entries.size());
  for (unsigned I = 0, E = Entries.size(); I != E; ++I) {
    StoreOffset[I] = Entries[I].Offset;
    if (SegOf[I] >= 0)
      Entries[I].Offset = Layout.SegmentOffset[SegOf[I]];
  }
  auto Move = [&](uint64_t &Site, const GlobalVariable *Owner) {
    unsigned I = EntryOf.lookup(Owner);
    Site = Entries[I].Offset + (Site - StoreOffset[I]);
  };
  for (MSILReloc &R : Relocs)
    Move(R.Site, R.Owner);
  for (MSILDiff &D : Diffs)
    Move(D.Site, D.Owner);
  llvm::stable_sort(Relocs, [](const MSILReloc &L, const MSILReloc &R) {
    return L.Site < R.Site;
  });
  llvm::stable_sort(Entries, [](const Entry &L, const Entry &R) {
    return std::make_pair(!L.InImage, L.Offset) <
           std::make_pair(!R.InImage, R.Offset);
  });
  Image.clear();
  InitSize = Layout.InitSize;
  TotalSize = Layout.TotalSize;
  MaxAlign = Align(uint64_t(1) << Layout.MaxAlignLog2);

  if (Diffs.empty())
    return;
  assert(TM && "symbol differences come from addGlobal, which sets TM");
  DenseMap<const GlobalValue *, uint64_t> Offsets;
  for (const Entry &E : Entries)
    if (E.InImage)
      Offsets.try_emplace(E.GV, E.Offset);
  for (const MSILDiff &D : Diffs) {
    auto A = Offsets.find(D.A);
    auto B = Offsets.find(D.B);
    // A declaration, or a definition that was never added (an error was
    // reported for it), has no offset.
    if (A == Offsets.end() || B == Offsets.end()) {
      reportInitializerError(*D.Owner, *TM,
                             "symbol difference across non-data symbols");
      continue;
    }
    // Modular arithmetic, as an assembler resolves the difference.
    int64_t Value = static_cast<int64_t>(A->second - B->second +
                                         static_cast<uint64_t>(D.Addend));
    unsigned Bits = 8 * D.Size;
    if (Bits < 64 && !isIntN(Bits, Value) && !isUIntN(Bits, Value)) {
      reportInitializerError(*D.Owner, *TM,
                             "symbol difference does not fit in a " +
                                 Twine(unsigned(D.Size)) + "-byte value");
      continue;
    }
    storeLE(&Layout.Image[D.Site], D.Size, Value);
  }
}

//===----------------------------------------------------------------------===//
// MSILDataCollector
//===----------------------------------------------------------------------===//

MSILDataCollector::MSILDataCollector(MCContext &Ctx, MSILDataSegment &Seg,
                                     const MSILSymbolMap &SymToGV,
                                     const GlobalVariable &Cur,
                                     const TargetMachine &TM)
    : MCStreamer(Ctx), Seg(Seg), SymToGV(SymToGV), Cur(Cur), TM(TM) {}

void MSILDataCollector::fail(const Twine &Msg) {
  // Once per message and global: a table of a thousand function addresses
  // with offsets needs one error.
  if (Reported.insert(Msg.str()).second)
    reportInitializerError(Cur, TM, Msg);
}

void MSILDataCollector::appendZeros(uint64_t Size) {
  Seg.Image.append(Size, 0);
}

void MSILDataCollector::unexpected(StringRef What) {
  reportFatalInternalError("MSIL: unexpected " + What +
                           " in the initializer of '" +
                           MSIL::getILName(Cur, TM) + "'");
}

bool MSILDataCollector::emitSymbolAttribute(MCSymbol *Symbol,
                                            MCSymbolAttr Attribute) {
  // Linkage and visibility are the access of the address fields, which the
  // AsmPrinter prints; they mean nothing inside the image.
  return true;
}

void MSILDataCollector::emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                         Align ByteAlignment) {
  unexpected("common symbol");
}

void MSILDataCollector::emitBytes(StringRef Data) {
  append_range(Seg.Image, arrayRefFromStringRef(Data));
}

void MSILDataCollector::emitIntValue(uint64_t Value, unsigned Size) {
  assert(Size >= 1 && Size <= 8 && "invalid integer size");
  size_t Site = Seg.Image.size();
  Seg.Image.resize(Site + Size);
  storeLE(&Seg.Image[Site], Size, static_cast<int64_t>(Value));
}

void MSILDataCollector::emitValueImpl(const MCExpr *Value, unsigned Size,
                                      SMLoc Loc) {
  // Every path appends exactly Size bytes, so the layout of the rest of the
  // initializer holds after an error.
  uint64_t Site = Seg.Image.size();
  MCValue V;
  // lowerConstant folds every expression it accepts into symbol + offset or
  // symbol - symbol + offset; anything else (the sum of two addresses, a
  // negated address) has no relocation.
  if (!Value || !Value->evaluateAsRelocatable(V, /*Asm=*/nullptr) ||
      V.getSpecifier() || (!V.getAddSym() && V.getSubSym())) {
    fail("unsupported constant expression");
    return appendZeros(Size);
  }
  if (V.isAbsolute()) {
    Seg.Image.resize(Site + Size);
    storeLE(&Seg.Image[Site], Size, V.getConstant());
    return;
  }

  const GlobalValue *Target = SymToGV.lookup(V.getAddSym());
  if (!Target) {
    fail("address of a non-global");
    return appendZeros(Size);
  }

  // In object mode an alias is a symbol of the object (plan 5.8.10), data
  // or function by its value type.
  const auto *Alias = Seg.ObjectMode ? dyn_cast<GlobalAlias>(Target) : nullptr;
  auto IsData = [&](const GlobalValue *GV) {
    if (const auto *GA = Seg.ObjectMode ? dyn_cast<GlobalAlias>(GV) : nullptr)
      return !GA->getValueType()->isFunctionTy();
    return isa<GlobalVariable>(GV);
  };

  if (const MCSymbol *SubSym = V.getSubSym()) {
    // A relative table entry: both ends must be data of this segment, whose
    // offsets finalize() knows; in an object, data symbols of any kind, whose
    // addresses msil-ld knows (DIFF, plan 5.8.8).
    const GlobalValue *B = SymToGV.lookup(SubSym);
    if (!IsData(Target) || !B || !IsData(B) || Size > 16) {
      fail("symbol difference across non-data symbols");
      return appendZeros(Size);
    }
    Seg.Diffs.push_back(
        {Site, Target, B, V.getConstant(), static_cast<uint8_t>(Size), &Cur});
    return appendZeros(Size);
  }

  if (!isa<GlobalVariable, Function>(Target) && !Alias) {
    // MSILPrepareIR erases aliases (except in object mode) and ifuncs
    // (decision D26).
    fail("address of an alias or ifunc");
    return appendZeros(Size);
  }
  // The relocation stores the value of the function's address field, which
  // the module initializer sets once with ldftn, so the value is still the
  // method itself: an offset into it means nothing.
  if (!IsData(Target) && V.getConstant()) {
    fail("function address + offset");
    return appendZeros(Size);
  }
  if (Size != 4 && Size != 8) {
    fail("address in a " + Twine(Size) + "-byte value");
    return appendZeros(Size);
  }
  Seg.Relocs.push_back(
      {Site, Target, V.getConstant(), static_cast<uint8_t>(Size), &Cur});
  appendZeros(Size);
}

void MSILDataCollector::emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                                 SMLoc Loc) {
  int64_t Num;
  if (!NumBytes.evaluateAsAbsolute(Num) || Num < 0)
    unexpected("fill of a non-constant size");
  Seg.Image.append(static_cast<uint64_t>(Num), static_cast<uint8_t>(FillValue));
}

void MSILDataCollector::emitFill(const MCExpr &NumValues, int64_t Size,
                                 int64_t Expr, SMLoc Loc) {
  unexpected("'.fill' directive");
}

void MSILDataCollector::emitLabel(MCSymbol *Symbol, SMLoc Loc) {
  unexpected("label");
}

void MSILDataCollector::emitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                              Align ByteAlignment) {
  unexpected("local common symbol");
}

void MSILDataCollector::emitZerofill(MCSection *Section, MCSymbol *Symbol,
                                     uint64_t Size, Align ByteAlignment,
                                     SMLoc Loc) {
  unexpected("zerofill");
}

void MSILDataCollector::emitTBSSSymbol(MCSection *Section, MCSymbol *Symbol,
                                       uint64_t Size, Align ByteAlignment) {
  unexpected("thread-local zerofill");
}

void MSILDataCollector::emitULEB128Value(const MCExpr *Value) {
  unexpected("ULEB128 value");
}

void MSILDataCollector::emitSLEB128Value(const MCExpr *Value) {
  unexpected("SLEB128 value");
}

void MSILDataCollector::emitValueToAlignment(Align Alignment, int64_t Fill,
                                             uint8_t FillLen,
                                             unsigned MaxBytesToEmit) {
  unexpected("alignment directive");
}

void MSILDataCollector::emitCodeAlignment(Align Alignment,
                                          const MCSubtargetInfo &STI,
                                          unsigned MaxBytesToEmit) {
  unexpected("code alignment directive");
}

void MSILDataCollector::emitValueToOffset(const MCExpr *Offset,
                                          unsigned char Value, SMLoc Loc) {
  unexpected("'.org' directive");
}

void MSILDataCollector::emitInstruction(const MCInst &Inst,
                                        const MCSubtargetInfo &STI) {
  unexpected("instruction");
}
