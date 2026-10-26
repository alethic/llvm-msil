//===-- MSILTargetMachine.h - Define TargetMachine for MSIL -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyTargetMachine.h at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the MSIL-specific subclass of TargetMachine.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILTARGETMACHINE_H
#define LLVM_LIB_TARGET_MSIL_MSILTARGETMACHINE_H

#include "MSILSubtarget.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include <memory>
#include <optional>

namespace llvm {

class MSILTargetMachine final : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  /// Subtargets keyed on the native CPU and feature strings of each function.
  mutable StringMap<std::unique_ptr<MSILSubtarget>> SubtargetMap;
  /// The file type of the last createMCStreamer call, if any.
  std::optional<CodeGenFileType> OutputFileType;

public:
  MSILTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                    StringRef FS, const TargetOptions &Options,
                    std::optional<Reloc::Model> RM,
                    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                    bool JIT);

  ~MSILTargetMachine() override;

  const MSILSubtarget *getSubtargetImpl(StringRef CPU, StringRef FS) const;
  const MSILSubtarget *getSubtargetImpl(const Function &F) const override;

  // Pass Pipeline Configuration
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  /// Adds MSILFixFunctionBitcastsLTOPass to LTO post-link pipelines (plan
  /// 5.9.8); the other pipelines are unchanged.
  void registerPassBuilderCallbacks(PassBuilder &PB) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }

  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;

  TargetTransformInfo getTargetTransformInfo(const Function &F) const override;

  bool usesPhysRegsForValues() const override { return false; }

  yaml::MachineFunctionInfo *createDefaultFuncInfoYAML() const override;
  yaml::MachineFunctionInfo *
  convertFuncInfoToYAML(const MachineFunction &MF) const override;
  bool parseMachineFunctionInfo(const yaml::MachineFunctionInfo &,
                                PerFunctionMIParsingState &PFS,
                                SMDiagnostic &Error,
                                SMRange &SourceRange) const override;

  /// Records whether the output is an object file (isObjectMode), and in
  /// object mode makes assembly output object-form text (the InstPrinter
  /// option 'object-form', plan 5.8.4); otherwise the base class's streamer.
  Expected<std::unique_ptr<MCStreamer>>
  createMCStreamer(raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
                   CodeGenFileType FileType, MCContext &Ctx) override;

  /// Whether the code generator writes a CIL object (plan 5.8.1): an object
  /// file (CodeGenFileType::ObjectFile), or its ILAsm form, object-form
  /// text, when the output is assembly and -msil-object-form is set. Object
  /// mode replaces the whole-program rules of -S, llc and msil-ld's
  /// --lto-emit-asm text by symbols (plan 5.8.10). -filetype=null and
  /// assembly output without the option are whole-program. The mode is known
  /// once createMCStreamer has run, and CodeGenTargetMachineImpl's
  /// addPassesToEmitFile adds the IR passes before it calls it, so a pass
  /// asks when it runs (runOnModule, runOnFunction), never when it is added.
  bool isObjectMode() const;
};

} // end namespace llvm

#endif
