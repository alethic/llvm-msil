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
/// global method per function, and the module trailer: the P/Invoke
/// declarations, the data segment of the global variables with their address
/// fields, the module initializer and the entry point. It also owns the
/// module's table of CIL method signatures, so every method header, call site,
/// 'ldftn', 'calli' and P/Invoke declaration spells a signature the same way.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILASMPRINTER_H
#define LLVM_LIB_TARGET_MSIL_MSILASMPRINTER_H

#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "MSILDataSegment.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <map>
#include <memory>
#include <set>
#include <utility>

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
  /// True from the method header of that function to its closing brace.
  bool InMethod = false;

  /// Owns every signature handed out by the services below. They outlive the
  /// MCInsts and MSILMemberRefExprs that point to them.
  SpecificBumpPtrAllocator<MSIL::Signature> SigAlloc;
  /// Signatures of IR functions: definitions, call targets (defined or not)
  /// and ldftn targets.
  DenseMap<const Function *, const MSIL::Signature *> FuncSigs;
  /// The signatures that each external symbol (a library call) is called
  /// with, by name. CIL methods may be overloaded by signature, so one name
  /// can have several (ldexp with an int32 and a native int exponent).
  StringMap<SmallVector<const MSIL::Signature *, 1>> ExtSymSigs;
  /// Every name and signature of ExtSymSigs in first-use order (the names
  /// point into the map's keys, which never move): the order of their
  /// P/Invoke declarations.
  SmallVector<std::pair<StringRef, const MSIL::Signature *>, 4> ExtSymOrder;
  /// Interned call-site signatures ('calli').
  std::set<MSIL::Signature> CalliSigs;
  /// Undefined functions referenced from code or data, in first-use order,
  /// with their signatures: the P/Invoke declarations of the module trailer.
  MapVector<const Function *, const MSIL::Signature *> ExternFuncs;
  /// The method of every function this module defines, by IL name and
  /// signature (what a call or 'ldftn' names), set by emitFunctionEntryLabel.
  std::map<std::pair<StringRef, MSIL::Signature>, const Function *> Methods;
  /// The global variables that hold no program data and that the code of the
  /// function being printed refers to (reported once each).
  SmallPtrSet<const GlobalVariable *, 2> NonDataRefs;

  /// The data segment: the layout and initializer bytes of every global
  /// variable this module defines (decision D12), filled by
  /// emitGlobalVariable and finalized by emitEndOfAsmFile.
  std::unique_ptr<MSILDataSegment> Data;
  /// The symbol of every global value, for the data collector, which maps
  /// the symbols of relocations back to globals. Built on first use.
  DenseMap<const MCSymbol *, const GlobalValue *> SymToGV;
  bool SymToGVBuilt = false;
  /// Global variable declarations (and available_externally definitions)
  /// that the emitted code or the data segment refers to.
  SmallPtrSet<const GlobalVariable *, 4> UsedExternGlobals;
  /// UsedExternGlobals in module order, set by emitEndOfAsmFile: errors
  /// (Phase 1 has no linker), which still get an address field so that the
  /// output stays well formed.
  SmallVector<const GlobalVariable *, 0> ExternGlobals;
  /// llvm.global_ctors, sorted by priority: the module initializer calls them
  /// after the data segment is in place.
  SmallVector<Structor, 8> Ctors;

public:
  MSILAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "MSIL Assembly Printer"; }

  /// The streamer for ILAsm directives (an MSILTargetAsmStreamer, or an
  /// MSILTargetNullStreamer for -filetype=null).
  MSILTargetStreamer &getTargetStreamer();

  /// The CIL signature of \p F, a definition or a declaration alike, computed
  /// once per module from the IR function type with MSIL::computeSignatureVTs
  /// (the lowering ISel applies to formal arguments, returns and call
  /// operands) and mapped through MSIL::toValType. The method header of a
  /// definition, every call site and 'ldftn', and the P/Invoke declaration of
  /// an undefined function all print this one signature, so a call token
  /// always spells its callee's method header. The reference stays valid for
  /// the rest of the module.
  const MSIL::Signature &getSignature(const Function &F);

  /// Returns the unique copy of the call-site signature \p S ('calli'), which
  /// lives as long as the printer.
  const MSIL::Signature &internSignature(const MSIL::Signature &S);

  /// Records that the undefined function \p F (a declaration, or an
  /// available_externally definition, which gets no method) is referenced
  /// from code or data, so that the module trailer declares it as a P/Invoke
  /// if it is a Phase-1 import (MSIL::getPInvokeModule, decision D14), and
  /// reports it as an error otherwise: Phase 1 has no linker.
  void noteExternFunction(const Function &F);

  /// Records that the external symbol \p Name (a library call) is called with
  /// signature \p S and returns the printer's copy of it, which the P/Invoke
  /// declaration of that name and signature prints. A symbol called with
  /// several signatures gets one declaration per signature.
  const MSIL::Signature &noteExternalSymbol(StringRef Name,
                                            const MSIL::Signature &S);

  /// Records that the code refers to the global variable \p GV, which this
  /// module does not define (a declaration, or an available_externally
  /// definition, which gets no data), so that the module trailer reports it:
  /// Phase 1 has no linker.
  void noteExternGlobal(const GlobalVariable &GV);

  /// Reports that the code of the function being printed refers to \p GV, a
  /// global variable that holds no program data (MSIL::isSpecialLLVMGlobal)
  /// and therefore has no address field; once per function and global.
  void noteNonDataGlobal(const GlobalVariable &GV);

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
  /// Closes the method. MSIL has no out-of-line jump tables, and this is the
  /// last per-function hook that runs after the end-of-function label (which
  /// -g adds) and before the '-- End function' comment and the blank line, so
  /// the closing brace directly follows the body (decision D29).
  void emitJumpTableInfo() override;
  void emitGlobalVariable(const GlobalVariable *GV) override;
  void emitFunctionEntryLabel() override;
  void emitFunctionBodyStart() override;
  void emitInstruction(const MachineInstr *MI) override;

protected:
  void emitGlobalAlias(const Module &M, const GlobalAlias &GA) override;

private:
  void emitGlobalIFunc(Module &M, const GlobalIFunc &GI) override;

  /// Closes the method of the function being printed.
  void emitMethodEnd();

  /// The symbol-to-global map that MSILDataSegment::addGlobal takes.
  const DenseMap<const MCSymbol *, const GlobalValue *> &
  getSymToGV(const Module &M);

  /// Reports every global variable that the module refers to but does not
  /// define, in module order, and lists it in ExternGlobals.
  void collectExternGlobals(const Module &M);

  /// Reports the address fields that ilasm would reject: a global variable
  /// named like the field of the data segment, and two global variables with
  /// one IL name.
  void checkAddressFieldNames(const Module &M);

  /// Returns the functions of llvm.global_ctors in the order the module
  /// initializer calls them, notes the undefined ones for P/Invoke
  /// declarations and diagnoses the entries it cannot call.
  SmallVector<const Function *, 8> collectConstructors();

  /// Emits the P/Invoke declarations of the undefined functions and external
  /// symbols that are Phase-1 imports (decision D14), and diagnoses the rest.
  /// Those with the IL name and signature of a method of the module get none:
  /// they bind to that method, as a native link binds the symbol.
  void emitPInvokeDeclarations(const Module &M);

  /// Emits the data image and the address fields (decision D12).
  void emitDataSegment();

  /// Emits <Module>::.cctor: it allocates and fills the data segment, sets the
  /// address fields, applies the relocations and calls \p CtorFns.
  void emitModuleInitializer(ArrayRef<const Function *> CtorFns);

  /// Emits '<msil>Main'(string[]), the .entrypoint method that calls the C
  /// function \p Main (decision D27), or diagnoses an unsupported signature.
  void emitEntryPoint(const Function &Main);
};

} // end namespace llvm

#endif
