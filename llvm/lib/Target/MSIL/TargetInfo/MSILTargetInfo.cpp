//===-- MSILTargetInfo.cpp - MSIL Target Implementation -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file registers the MSIL target.
///
//===----------------------------------------------------------------------===//

#include "TargetInfo/MSILTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
using namespace llvm;

Target &llvm::getTheMSILTarget() {
  static Target TheMSILTarget;
  return TheMSILTarget;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMSILTargetInfo() {
  // The MSIL target never claims an architecture: x86_64, aarch64 and the
  // others belong to their native targets, and an MSIL triple names one only
  // to select the C ABI. TargetRegistry::lookupTarget selects this target for
  // every triple whose object format is MSIL, or by name with -march=msil.
  TargetRegistry::RegisterTarget(
      getTheMSILTarget(), "msil", "MSIL (.NET CIL as ILAsm text)", "MSIL",
      [](Triple::ArchType) { return false; }, /*HasJIT=*/false);
}
