//===- lib/MC/MCMSILObjectWriter.cpp - MSIL Object Writer -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the object writer of MSIL (.NET CIL) triples: a writer
// that owns the target's MCMSILObjectTargetWriter and forwards to it, and the
// target writer's defaults, which report that CIL objects are not supported.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCMSILObjectWriter.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

MCMSILObjectTargetWriter::MCMSILObjectTargetWriter() = default;

// Pin the vtable to this file.
MCMSILObjectTargetWriter::~MCMSILObjectTargetWriter() = default;

void MCMSILObjectTargetWriter::recordRelocation(const MCFragment &F,
                                                const MCFixup &Fixup,
                                                MCValue Target,
                                                uint64_t &FixedValue) {}

uint64_t MCMSILObjectTargetWriter::writeObject(MCAssembler &Assembler,
                                               raw_pwrite_stream &OS) {
  MCContext &Ctx = Assembler.getContext();
  Ctx.reportError(SMLoc(),
                  "CIL object files are not supported for MSIL target '" +
                      Ctx.getTargetTriple().str() + "' yet");
  return 0;
}

namespace {

/// The MCObjectWriter of MSIL triples. It owns the target writer and forwards
/// the calls that a CIL object writer implements, so that the MSIL target, not
/// lib/MC, decides what an object contains.
class MSILForwardingObjectWriter final : public MCObjectWriter {
  std::unique_ptr<MCMSILObjectTargetWriter> TargetObjectWriter;
  raw_pwrite_stream &OS;

public:
  MSILForwardingObjectWriter(std::unique_ptr<MCMSILObjectTargetWriter> MOTW,
                             raw_pwrite_stream &OS)
      : TargetObjectWriter(std::move(MOTW)), OS(OS) {}

  void setAssembler(MCAssembler *A) override {
    MCObjectWriter::setAssembler(A);
    TargetObjectWriter->setAssembler(A);
  }

  void recordRelocation(const MCFragment &F, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) override {
    TargetObjectWriter->recordRelocation(F, Fixup, Target, FixedValue);
  }

  uint64_t writeObject() override {
    assert(Asm && "writeObject() before setAssembler()");
    return TargetObjectWriter->writeObject(*Asm, OS);
  }
};

} // end anonymous namespace

std::unique_ptr<MCObjectWriter>
llvm::createMSILObjectWriter(std::unique_ptr<MCMSILObjectTargetWriter> MOTW,
                             raw_pwrite_stream &OS) {
  return std::make_unique<MSILForwardingObjectWriter>(std::move(MOTW), OS);
}
