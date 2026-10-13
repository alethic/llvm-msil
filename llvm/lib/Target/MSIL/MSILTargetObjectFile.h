//===-- MSILTargetObjectFile.h - MSIL Object Info ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the MSIL-specific subclass of TargetLoweringObjectFile.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILTARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_MSIL_MSILTARGETOBJECTFILE_H

#include "llvm/MC/SectionKind.h"
#include "llvm/Target/TargetLoweringObjectFile.h"

namespace llvm {

/// A .NET assembly has no sections in the ELF or COFF sense. Every global
/// goes into one of the logical MSIL sections that
/// MCObjectFileInfo::initMSILMCObjectFileInfo creates (text, data, bss and
/// read-only data), chosen only by its SectionKind: explicit section names
/// and section attributes are ignored.
class MSILTargetObjectFile final : public TargetLoweringObjectFile {
public:
  MSILTargetObjectFile() = default;
  ~MSILTargetObjectFile() override;

  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

protected:
  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

private:
  MCSection *getSectionForKind(SectionKind Kind) const;
};

} // end namespace llvm

#endif
