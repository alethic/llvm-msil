//===-- MSILMCTargetDesc.cpp - MSIL target descriptions -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from
// llvm/lib/Target/WebAssembly/MCTargetDesc/WebAssemblyMCTargetDesc.cpp at
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides MSIL-specific target descriptions and registers the MC
/// layer (as WebAssemblyMCTargetDesc.cpp does). MSIL emits ILAsm text only:
/// there is no code emitter, asm backend or object target streamer.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MCTargetDesc/MSILInstPrinter.h"
#include "MCTargetDesc/MSILMCAsmInfo.h"
#include "MCTargetDesc/MSILTargetStreamer.h"
#include "TargetInfo/MSILTargetInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
using namespace llvm;

#define DEBUG_TYPE "msil-mc-target-desc"

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "MSILGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "MSILGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "MSILGenRegisterInfo.inc"

std::string llvm::MSIL::getMSILCPU(StringRef CPU) {
  // clang passes the native CPU ("x86-64", "generic", "cortex-a57", ...) or
  // none; Phase 1 has the one processor "generic" (decision D7). "help" is
  // passed through, but MCSubtargetInfo prints no CPU list for it: MSIL
  // defines no subtarget features, and getFeatures returns before its "help"
  // check when the feature table is empty (as for other featureless targets).
  if (CPU == "help")
    return CPU.str();
  return "generic";
}

std::string llvm::MSIL::filterMSILFeatures(StringRef FS) {
  // MSIL defines no subtarget features yet, so every native feature clang
  // attaches is dropped without a 'not a recognized feature' warning.
  return std::string();
}

MCAsmInfo *llvm::createMSILMCAsmInfo(const MCRegisterInfo & /*MRI*/,
                                     const Triple &TT,
                                     const MCTargetOptions &Options) {
  return new MSILMCAsmInfo(TT, Options);
}

MCInstrInfo *llvm::createMSILMCInstrInfo() {
  auto *X = new MCInstrInfo();
  InitMSILMCInstrInfo(X);
  return X;
}

MCRegisterInfo *llvm::createMSILMCRegisterInfo(const Triple & /*TT*/) {
  auto *X = new MCRegisterInfo();
  InitMSILMCRegisterInfo(X, 0);
  return X;
}

MCSubtargetInfo *llvm::createMSILMCSubtargetInfo(const Triple &TT,
                                                 StringRef CPU, StringRef FS) {
  std::string MSILCPU = MSIL::getMSILCPU(CPU);
  return createMSILMCSubtargetInfoImpl(TT, MSILCPU, /*TuneCPU=*/MSILCPU,
                                       MSIL::filterMSILFeatures(FS));
}

MCInstPrinter *llvm::createMSILMCInstPrinter(const Triple & /*TT*/,
                                             unsigned /*SyntaxVariant*/,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  // ILAsm has one syntax: every variant gets it.
  return new MSILInstPrinter(MAI, MII, MRI);
}

MCTargetStreamer *
llvm::createMSILAsmTargetStreamer(MCStreamer &S, formatted_raw_ostream & /*OS*/,
                                  MCInstPrinter * /*InstPrint*/) {
  return new MSILTargetAsmStreamer(S);
}

MCTargetStreamer *llvm::createMSILNullTargetStreamer(MCStreamer &S) {
  return new MSILTargetNullStreamer(S);
}

// Force static initialization.
extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMSILTargetMC() {
  Target &T = getTheMSILTarget();

  // Register the MC asm info.
  RegisterMCAsmInfoFn X(T, createMSILMCAsmInfo);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(T, createMSILMCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(T, createMSILMCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(T, createMSILMCSubtargetInfo);

  // Register the MCInstPrinter.
  TargetRegistry::RegisterMCInstPrinter(T, createMSILMCInstPrinter);

  // Register the asm target streamer.
  TargetRegistry::RegisterAsmTargetStreamer(T, createMSILAsmTargetStreamer);

  // Register the null target streamer.
  TargetRegistry::RegisterNullTargetStreamer(T, createMSILNullTargetStreamer);
}
