//===-- MSILAsmPrinter.h - MSIL implementation of AsmPrinter ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the MSIL AsmPrinter, which writes a module as ILAsm text
/// (llvm/docs/MSIL/ImplementationPlan.md section 5.1): the module header, one
/// global method per function, and the module trailer. It also owns the
/// module's table of CIL method signatures, so every method header, call site,
/// 'ldftn', 'calli' and P/Invoke declaration spells a signature the same way.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILASMPRINTER_H
#define LLVM_LIB_TARGET_MSIL_MSILASMPRINTER_H

#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <memory>
#include <set>

namespace llvm {
class Function;
class GlobalAlias;
class GlobalIFunc;
class GlobalValue;
class GlobalVariable;
class MCSymbol;
class MSILFunctionInfo;
class MSILSubtarget;
class MSILTargetStreamer;
class MachineFunction;
class MachineInstr;
class Module;
class TargetMachine;

class LLVM_LIBRARY_VISIBILITY MSILAsmPrinter final : public AsmPrinter {
public:
  static char ID;

private:
  /// The function being printed (set by runOnMachineFunction).
  const MSILSubtarget *Subtarget = nullptr;
  MSILFunctionInfo *MFI = nullptr;

  /// Owns every signature handed out by the services below. They outlive the
  /// MCInsts and MSILMemberRefExprs that point to them.
  SpecificBumpPtrAllocator<MSIL::Signature> SigAlloc;
  /// Signatures of IR functions: definitions, declarations, ldftn targets.
  DenseMap<const Function *, const MSIL::Signature *> FuncSigs;
  /// Signatures of external symbols (library calls), by name.
  StringMap<const MSIL::Signature *> ExtSymSigs;
  /// Interned call-site signatures ('calli').
  std::set<MSIL::Signature> CalliSigs;
  /// Undefined functions referenced from code or data, in first-use order,
  /// with their signatures: the P/Invoke declarations of the module trailer.
  MapVector<const Function *, const MSIL::Signature *> ExternFuncs;

public:
  MSILAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "MSIL Assembly Printer"; }

  /// The streamer for ILAsm directives (an MSILTargetAsmStreamer, or an
  /// MSILTargetNullStreamer for -filetype=null).
  MSILTargetStreamer &getTargetStreamer();

  /// The CIL signature of \p F, computed once per module with
  /// MSIL::computeSignatureVTs, the function ISel uses, and mapped through
  /// MSIL::toValType. The method header of a definition, every call site and
  /// 'ldftn', and the P/Invoke declaration of an undefined function all print
  /// this one signature.
  const MSIL::Signature &getSignature(const Function &F);

  /// Returns the unique copy of the call-site signature \p S ('calli'), which
  /// lives as long as the printer.
  const MSIL::Signature &internSignature(const MSIL::Signature &S);

  /// Records that the undefined function \p F is referenced from code or
  /// data, so that the module trailer declares it as a P/Invoke.
  void noteExternFunction(const Function &F);

  /// Records that the external symbol \p Name (a library call) is called with
  /// signature \p S and returns the printer's copy of it. Every call of one
  /// symbol must use the same signature.
  const MSIL::Signature &noteExternalSymbol(StringRef Name,
                                            const MSIL::Signature &S);

  //===------------------------------------------------------------------===//
  // MachineFunctionPass Implementation.
  //===------------------------------------------------------------------===//

  bool doInitialization(Module &M) override;
  bool runOnMachineFunction(MachineFunction &MF) override;

  //===------------------------------------------------------------------===//
  // AsmPrinter Implementation.
  //===------------------------------------------------------------------===//

  void emitStartOfAsmFile(Module &M) override;
  void emitEndOfAsmFile(Module &M) override;
  void emitLinkage(const GlobalValue *GV, MCSymbol *GVSym) const override;
  void emitConstantPool() override;
  void emitJumpTableInfo() override;
  void emitGlobalVariable(const GlobalVariable *GV) override;
  void emitFunctionEntryLabel() override;
  void emitFunctionBodyStart() override;
  void emitInstruction(const MachineInstr *MI) override;

protected:
  void emitGlobalAlias(const Module &M, const GlobalAlias &GA) override;

private:
  void emitGlobalIFunc(Module &M, const GlobalIFunc &GI) override;
};

} // end namespace llvm

#endif
