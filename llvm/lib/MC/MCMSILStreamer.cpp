//===- lib/MC/MCMSILStreamer.cpp - MSIL Object Output ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements MCMSILStreamer, which emits MSIL (.NET CIL) objects.
// It maps generic symbol attributes to MCSymbolMSIL; the MSIL target streamer
// and the target's object writer do everything else.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCMSILStreamer.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSymbolMSIL.h"

using namespace llvm;

MCMSILStreamer::~MCMSILStreamer() = default; // anchor.

bool MCMSILStreamer::emitSymbolAttribute(MCSymbol *S, MCSymbolAttr Attribute) {
  auto *Symbol = static_cast<MCSymbolMSIL *>(S);

  // Adding a symbol attribute always introduces the symbol; note that an
  // important side effect of calling registerSymbol here is to register the
  // symbol with the assembler.
  getAssembler().registerSymbol(*Symbol);

  switch (Attribute) {
  case MCSA_Global:
  case MCSA_Extern:
    Symbol->setExternal(true);
    break;

  case MCSA_Weak:
  case MCSA_WeakReference:
    Symbol->setWeak(true);
    Symbol->setExternal(true);
    break;

  case MCSA_Hidden:
    Symbol->setVisibility(MSILLink::Visibility::Hidden);
    break;

  case MCSA_Internal:
    Symbol->setVisibility(MSILLink::Visibility::Internal);
    break;

  case MCSA_Protected:
    Symbol->setVisibility(MSILLink::Visibility::Protected);
    break;

  case MCSA_NoDeadStrip:
    Symbol->setNoStrip(true);
    break;

  case MCSA_ELF_TypeFunction:
    Symbol->setKind(MCSymbolMSIL::Kind::Method);
    break;

  case MCSA_ELF_TypeObject:
    Symbol->setKind(MCSymbolMSIL::Kind::Data);
    break;

  default:
    // MSIL supports no other attribute.
    return false;
  }

  return true;
}

void MCMSILStreamer::emitCommonSymbol(MCSymbol *S, uint64_t Size,
                                      Align ByteAlignment) {
  auto *Symbol = static_cast<MCSymbolMSIL *>(S);
  getAssembler().registerSymbol(*Symbol);

  Symbol->setExternal(true);

  Symbol->setKind(MCSymbolMSIL::Kind::Data);

  if (Symbol->declareCommon(Size, ByteAlignment)) {
    getContext().reportError(getStartTokLoc(),
                             "symbol '" + Symbol->getName() +
                                 "' is already declared as a different type");
    return;
  }

  Symbol->setSize(MCConstantExpr::create(Size, getContext()));
}

void MCMSILStreamer::emitLocalCommonSymbol(MCSymbol *S, uint64_t Size,
                                           Align ByteAlignment) {
  getContext().reportError(getStartTokLoc(),
                           "local common symbols are not supported for MSIL");
}

void MCMSILStreamer::emitELFSize(MCSymbol *Symbol, const MCExpr *Value) {
  static_cast<MCSymbolMSIL *>(Symbol)->setSize(Value);
}

MCStreamer *llvm::createMSILStreamer(MCContext &Context,
                                     std::unique_ptr<MCAsmBackend> &&MAB,
                                     std::unique_ptr<MCObjectWriter> &&OW,
                                     std::unique_ptr<MCCodeEmitter> &&CE) {
  return new MCMSILStreamer(Context, std::move(MAB), std::move(OW),
                            std::move(CE));
}
