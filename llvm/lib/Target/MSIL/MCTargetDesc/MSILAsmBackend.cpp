//===-- MSILAsmBackend.cpp - MSIL assembler backend -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from
// llvm/lib/Target/WebAssembly/MCTargetDesc/WebAssemblyAsmBackend.cpp at
// 64f50ffd4f1c.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements MSILAsmBackend. It knows two kinds of fixups:
/// - metadata tokens (MSILFixupKinds.h), which the assembler never resolves:
///   each goes to the object writer, which assigns the token;
/// - branch displacements and switch entries (PC-relative FK_Data_1 and
///   FK_Data_4), which must resolve inside the method: every method has its
///   own section, and msil-ld never relaxes a branch, so MSILMCInstLower
///   picks each branch's form before emission and nothing is relaxed here.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILFixupKinds.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCMSILObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

namespace {

class MSILAsmBackend final : public MCAsmBackend {
  /// The writer that createObjectTargetWriter made last; the MCObjectWriter
  /// built around it owns it (MCAsmBackend::createObjectWriter).
  mutable MCMSILObjectTargetWriter *LastWriter = nullptr;

public:
  MSILAsmBackend() : MCAsmBackend(llvm::endianness::little) {}

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override;

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override;

  /// Branch forms are chosen once, before emission (MSILMCInstLower), and
  /// msil-ld never relaxes, so no instruction is ever relaxed.
  bool mayNeedRelaxation(unsigned Opcode, ArrayRef<MCOperand> Operands,
                         const MCSubtargetInfo &STI) const override {
    return false;
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override;

  MCMSILObjectTargetWriter *getLastWriter() const { return LastWriter; }
};

} // end anonymous namespace

/// Whether \p Kind is one of the token kinds of MSILFixupKinds.h.
static bool isTokenFixup(MCFixupKind Kind) {
  return Kind >= FirstTargetFixupKind && Kind < MSIL::LastTargetFixupKind;
}

MCFixupKindInfo MSILAsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  const static MCFixupKindInfo Infos[MSIL::NumTargetFixupKinds] = {
      // This table *must* be in the order that the fixup_* kinds are defined in
      // MSILFixupKinds.h.
      //
      // Name                     Offset (bits) Size (bits)     Flags
      {"fixup_cil_method_token", 0, 32, 0},
      {"fixup_cil_field_token", 0, 32, 0},
      {"fixup_cil_sig_token", 0, 32, 0},
      {"fixup_cil_type_token", 0, 32, 0},
      {"fixup_cil_tok_token", 0, 32, 0},
      {"fixup_cil_string_token", 0, 32, 0},
  };

  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);

  assert(unsigned(Kind - FirstTargetFixupKind) < MSIL::NumTargetFixupKinds &&
         "Invalid kind!");
  return Infos[Kind - FirstTargetFixupKind];
}

void MSILAsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                                const MCValue &Target, uint8_t *Data,
                                uint64_t Value, bool IsResolved) {
  MCFixupKind Kind = Fixup.getKind();
  MCFixupKindInfo Info = getFixupKindInfo(Kind);
  unsigned NumBytes = Info.TargetSize / 8;
  assert(Fixup.getOffset() + NumBytes <= F.getSize() &&
         "Invalid fixup offset!");

  if (isTokenFixup(Kind)) {
    // A token is never resolved by the assembler, whatever the expression
    // evaluates to: the object writer assigns it. The writer is called with
    // FixedValue 0 and may set it to the token, which is written here;
    // otherwise the encoder's four zero bytes stay.
    uint64_t Token = 0;
    Asm->getWriter().recordRelocation(F, Fixup, Target, Token);
    support::endian::write32le(Data, uint32_t(Token));
    return;
  }

  if (Fixup.isPCRel()) {
    // A branch displacement or a switch entry. The encoder's addend makes
    // Value the displacement from the end of the instruction.
    if (!IsResolved) {
      getContext().reportError(Fixup.getLoc(),
                               "branch target outside its method");
      return;
    }
    if (NumBytes == 1 && !isInt<8>(int64_t(Value))) {
      getContext().reportError(Fixup.getLoc(), "short branch out of range");
      return;
    }
  } else if (!IsResolved) {
    // Other data fixups are relocations of the object writer, as on other
    // targets.
    Asm->getWriter().recordRelocation(F, Fixup, Target, Value);
  }

  // For each byte of the fragment that the fixup touches, mask in the
  // bits from the fixup value.
  for (unsigned I = 0; I != NumBytes; ++I)
    Data[I] |= uint8_t((Value >> (I * 8)) & 0xff);
}

bool MSILAsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                  const MCSubtargetInfo *STI) const {
  // 'nop' is 0x00. Methods are never aligned, so this is not expected to run.
  OS.write_zeros(Count);
  return true;
}

std::unique_ptr<MCObjectTargetWriter>
MSILAsmBackend::createObjectTargetWriter() const {
  std::unique_ptr<MCObjectTargetWriter> W = createMSILObjectTargetWriter();
  LastWriter = cast<MCMSILObjectTargetWriter>(W.get());
  return W;
}

MCAsmBackend *llvm::createMSILAsmBackend(const Target & /*T*/,
                                         const MCSubtargetInfo & /*STI*/,
                                         const MCRegisterInfo & /*MRI*/,
                                         const MCTargetOptions & /*Options*/) {
  return new MSILAsmBackend();
}

MCMSILObjectTargetWriter *
llvm::MSIL::getObjectTargetWriter(const MCAsmBackend &MAB) {
  return static_cast<const MSILAsmBackend &>(MAB).getLastWriter();
}
