//===- MCMSILStreamer.h - MCStreamer MSIL Object File Interface -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares MCMSILStreamer, the object streamer of MSIL (.NET CIL)
// triples, and its factory createMSILStreamer(), which
// Target::createMCObjectStreamer calls for Triple::MSIL. It has
// MCWasmStreamer's shape: generic symbol attributes become MCSymbolMSIL fields,
// and everything specific to CIL objects is left to the MSIL target streamer
// and object writer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCMSILSTREAMER_H
#define LLVM_MC_MCMSILSTREAMER_H

#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <memory>

namespace llvm {

class MCContext;
class MCExpr;
class MCStreamer;
class MCSymbol;

class LLVM_ABI MCMSILStreamer : public MCObjectStreamer {
public:
  MCMSILStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                 std::unique_ptr<MCObjectWriter> OW,
                 std::unique_ptr<MCCodeEmitter> Emitter)
      : MCObjectStreamer(Context, std::move(TAB), std::move(OW),
                         std::move(Emitter)) {}

  ~MCMSILStreamer() override;

  /// \name MCStreamer Interface
  /// @{

  bool emitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override;
  void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        Align ByteAlignment) override;
  void emitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                             Align ByteAlignment) override;
  void emitELFSize(MCSymbol *Symbol, const MCExpr *Value) override;

  /// @}
};

/// Create the object streamer of MSIL triples. It lives here rather than in
/// TargetRegistry.h, which many files include.
LLVM_ABI MCStreamer *createMSILStreamer(MCContext &Ctx,
                                        std::unique_ptr<MCAsmBackend> &&TAB,
                                        std::unique_ptr<MCObjectWriter> &&OW,
                                        std::unique_ptr<MCCodeEmitter> &&CE);

} // end namespace llvm

#endif // LLVM_MC_MCMSILSTREAMER_H
