//===- MCSectionMSIL.h - MSIL (.NET CIL) Machine Code Sections --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSectionMSIL class.
//
// MSIL output is an ECMA-335 assembly, which has no sections in the ELF/COFF
// sense. These sections are logical buckets (method bodies, initialized data,
// zero-initialized data, read-only data) that the MSIL AsmPrinter and, later,
// the MSIL object writer use to group what they emit.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTIONMSIL_H
#define LLVM_MC_MCSECTIONMSIL_H

#include "llvm/MC/MCSection.h"
#include "llvm/MC/SectionKind.h"

namespace llvm {

class MCSymbol;

class MCSectionMSIL final : public MCSection {
  friend class MCContext;

  SectionKind Kind;

  MCSectionMSIL(StringRef Name, SectionKind K, MCSymbol *Begin)
      : MCSection(Name, K.isText(), /*IsBss=*/K.isBSS(), Begin), Kind(K) {}

public:
  SectionKind getKind() const { return Kind; }
};

} // end namespace llvm

#endif // LLVM_MC_MCSECTIONMSIL_H
