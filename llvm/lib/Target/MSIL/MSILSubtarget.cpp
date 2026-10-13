//===-- MSILSubtarget.cpp - MSIL Subtarget Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblySubtarget.cpp at
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the MSIL-specific subclass of TargetSubtarget.
///
//===----------------------------------------------------------------------===//

#include "MSILSubtarget.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "msil-subtarget"

#define GET_SUBTARGETINFO_CTOR
#define GET_SUBTARGETINFO_TARGET_DESC
#include "MSILGenSubtargetInfo.inc"

MSILSubtarget &MSILSubtarget::initializeSubtargetDependencies() {
  LLVM_DEBUG(dbgs() << "initializeSubtargetDependencies\n");
  ParseSubtargetFeatures(getCPU(), getTuneCPU(), getFeatureString());
  return *this;
}

// The MC subtarget is always built from the MSIL processor and features, so
// the native CPU and feature names that clang attaches to every function
// (x86-64, +sse2, ...) are accepted silently instead of being reported as
// unknown.
MSILSubtarget::MSILSubtarget(const Triple &TT, StringRef CPU, StringRef TuneCPU,
                             StringRef FS, const TargetMachine &TM)
    : MSILGenSubtargetInfo(TT, MSIL::getMSILCPU(CPU), MSIL::getMSILCPU(TuneCPU),
                           MSIL::filterMSILFeatures(FS)),
      NativeCPU(CPU), NativeFS(FS),
      InstrInfo(initializeSubtargetDependencies()), TLInfo(TM, *this) {}

MSILSubtarget::~MSILSubtarget() = default;

void MSILSubtarget::initLibcallLoweringInfo(LibcallLoweringInfo &Info) const {
  // M4 marks the helpers that MSIL expands inline instead (i128
  // multiplication, overflow checks) as unavailable; until then the default
  // libcalls apply.
}
