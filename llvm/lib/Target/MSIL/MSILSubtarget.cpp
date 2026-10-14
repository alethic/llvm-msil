//===-- MSILSubtarget.cpp - MSIL Subtarget Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblySubtarget.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the MSIL-specific subclass of TargetSubtarget.
///
//===----------------------------------------------------------------------===//

#include "MSILSubtarget.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "llvm/Analysis/LibcallLoweringInfo.h"
#include "llvm/IR/RuntimeLibcalls.h"
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
  // There is no runtime library to link against in Phase 1, so the helpers
  // that the legalizer can do without are unavailable and it expands them
  // inline: i128 multiplication (__multi3, otherwise also used for the wide
  // multiply behind 64-bit overflow checks) and the multiplication overflow
  // checks (__mulodi4, __muloti4). Shifts of i128 never need __ashlti3 and
  // friends: SHL_PARTS, SRA_PARTS and SRL_PARTS are Custom.
  for (RTLIB::Libcall LC : {RTLIB::MUL_I128, RTLIB::MULO_I64, RTLIB::MULO_I128})
    Info.setLibcallImpl(LC, RTLIB::Unsupported);
}
