//===-- llvm/MC/MCMSILObjectWriter.h - MSIL Object Writer -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the object writer interface of MSIL (.NET CIL) triples.
// MCAsmBackend::createObjectWriter dispatches Triple::MSIL to
// createMSILObjectWriter(), whose writer forwards recordRelocation() and
// writeObject() to the target's MCMSILObjectTargetWriter. lib/MC therefore
// needs no MSIL target code: the MSIL target overrides those two calls to
// write CIL objects. The defaults record nothing and report that CIL objects
// are not supported yet.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCMSILOBJECTWRITER_H
#define LLVM_MC_MCMSILOBJECTWRITER_H

#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>
#include <memory>

namespace llvm {

class MCAssembler;
class MCFixup;
class MCFragment;
class MCValue;
class raw_pwrite_stream;

class LLVM_ABI MCMSILObjectTargetWriter : public MCObjectTargetWriter {
public:
  MCMSILObjectTargetWriter();
  ~MCMSILObjectTargetWriter() override;

  Triple::ObjectFormatType getFormat() const override { return Triple::MSIL; }
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::MSIL;
  }

  /// Record a relocation entry; the object writer forwards
  /// MCObjectWriter::recordRelocation() here. The default records nothing.
  virtual void recordRelocation(const MCFragment &F, const MCFixup &Fixup,
                                MCValue Target, uint64_t &FixedValue);

  /// Write the object file to \p OS and return the number of bytes written;
  /// the object writer forwards MCObjectWriter::writeObject() here. The
  /// default reports "CIL object files are not supported for MSIL target
  /// '<triple>' yet" through MCContext::reportError and writes nothing.
  virtual uint64_t writeObject(MCAssembler &Assembler, raw_pwrite_stream &OS);
};

/// Construct a new MSIL writer instance.
///
/// \param MOTW - The target specific MSIL writer subclass.
/// \param OS - The stream to write to.
/// \returns The constructed object writer.
LLVM_ABI std::unique_ptr<MCObjectWriter>
createMSILObjectWriter(std::unique_ptr<MCMSILObjectTargetWriter> MOTW,
                       raw_pwrite_stream &OS);

} // end namespace llvm

#endif // LLVM_MC_MCMSILOBJECTWRITER_H
