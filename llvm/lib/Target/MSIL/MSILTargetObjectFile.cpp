//===-- MSILTargetObjectFile.cpp - MSIL Object Info -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the functions of the MSIL-specific subclass of
/// TargetLoweringObjectFile.
///
//===----------------------------------------------------------------------===//

#include "MSILTargetObjectFile.h"

using namespace llvm;

MSILTargetObjectFile::~MSILTargetObjectFile() = default;

MCSection *MSILTargetObjectFile::getSectionForKind(SectionKind Kind) const {
  if (Kind.isText())
    return getTextSection();
  if (Kind.isBSS() || Kind.isCommon())
    return getBSSSection();
  if (Kind.isReadOnly())
    return getReadOnlySection();
  // Writable data, including read-only-after-relocation data: CIL computes
  // global addresses at run time, in the module initializer.
  return getDataSection();
}

MCSection *MSILTargetObjectFile::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  // A section name has no meaning in a .NET assembly.
  return getSectionForKind(Kind);
}

MCSection *MSILTargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  return getSectionForKind(Kind);
}
