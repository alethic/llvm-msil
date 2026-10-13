//===-- MSIL.h - Top-level interface for MSIL -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssembly.h at d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the entry points for global functions defined in the
/// LLVM MSIL back-end: the factories and PassRegistry initializers of its
/// passes. The MSIL back-end uses the legacy pass manager only.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSIL_H
#define LLVM_LIB_TARGET_MSIL_MSIL_H

#include "llvm/Support/CodeGen.h"

namespace llvm {

class FunctionPass;
class ModulePass;
class MSILTargetMachine;
class PassRegistry;

// LLVM IR passes.
ModulePass *createMSILPrepareIRLegacyPass();
ModulePass *createMSILAddMissingPrototypesLegacyPass();
ModulePass *createMSILFixFunctionBitcastsLegacyPass();
FunctionPass *createMSILOptimizeReturnedLegacyPass();

// ISel and immediate followup passes.
FunctionPass *createMSILISelDagLegacyPass(MSILTargetMachine &TM,
                                          CodeGenOptLevel OptLevel);
FunctionPass *createMSILArgumentMoveLegacyPass();
FunctionPass *createMSILSetMemFlagsLegacyPass();
FunctionPass *createMSILCleanCodeAfterTrapLegacyPass();

// Late passes.
FunctionPass *createMSILNullifyDebugValueListsLegacyPass();
FunctionPass *createMSILReplacePhysRegsLegacyPass();
FunctionPass *createMSILOptimizeLiveIntervalsLegacyPass();
FunctionPass *createMSILRegStackifyLegacyPass(CodeGenOptLevel OptLevel);
FunctionPass *createMSILRegColoringLegacyPass();
FunctionPass *createMSILExplicitLocalsLegacyPass();
FunctionPass *createMSILRegNumberingLegacyPass();
FunctionPass *createMSILStackCheckLegacyPass();

// PassRegistry initialization declarations.
void initializeMSILAddMissingPrototypesLegacyPass(PassRegistry &);
void initializeMSILArgumentMoveLegacyPass(PassRegistry &);
void initializeMSILAsmPrinterPass(PassRegistry &);
void initializeMSILCleanCodeAfterTrapLegacyPass(PassRegistry &);
void initializeMSILDAGToDAGISelLegacyPass(PassRegistry &);
void initializeMSILExplicitLocalsLegacyPass(PassRegistry &);
void initializeMSILFixFunctionBitcastsLegacyPass(PassRegistry &);
void initializeMSILNullifyDebugValueListsLegacyPass(PassRegistry &);
void initializeMSILOptimizeLiveIntervalsLegacyPass(PassRegistry &);
void initializeMSILOptimizeReturnedLegacyPass(PassRegistry &);
void initializeMSILPrepareIRLegacyPass(PassRegistry &);
void initializeMSILRegColoringLegacyPass(PassRegistry &);
void initializeMSILRegNumberingLegacyPass(PassRegistry &);
void initializeMSILRegStackifyLegacyPass(PassRegistry &);
void initializeMSILReplacePhysRegsLegacyPass(PassRegistry &);
void initializeMSILSetMemFlagsLegacyPass(PassRegistry &);
void initializeMSILStackCheckLegacyPass(PassRegistry &);

} // end namespace llvm

#endif
