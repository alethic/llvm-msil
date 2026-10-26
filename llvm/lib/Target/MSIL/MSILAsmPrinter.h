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
/// fields, the address fields of the functions whose address is used, the
/// module initializer and the entry point. It emits only CIL_ MCInsts (one
/// per CIL instruction), labels and structured MSILTargetStreamer calls, so
/// the text and an object are two encodings of one event stream. It also owns
/// the module's table of CIL method signatures, so every method header, call
/// site, 'ldftn', 'calli' and P/Invoke declaration spells a signature the same
/// way.
///
/// The trailer is a one-object link (Q5, Design.md 4.1): the data image is
/// laid out by cli::layoutImage (named sections, __start_/__stop_ symbols,
/// constructor-table slots), and the module initializer, the entry point and
/// the data-import helpers are built by cli::buildTrailer, msil-ld's trailer
/// builder (plan 5.9.9, 5.9.10), which this printer feeds and whose methods
/// it prints through its text sink (MSILTrailerTextSink).
///
/// In object mode (MSILTargetMachine::isObjectMode(), plan 5.8) it writes a
/// CIL object instead, through the same streamer calls (MSILTargetStreamer's
/// object event order): the object header, the methods, the address fields
/// of the defined globals, the import stubs and the link-info record, which
/// it builds (plan 5.8.8, 5.8.10) with every token numbered by row order R6,
/// as ilasm numbers the object-form text and as MSILObjectWriter checks.
/// There is no module initializer, entry point, data image or trailer.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILASMPRINTER_H
#define LLVM_LIB_TARGET_MSIL_MSILASMPRINTER_H

#include "MCTargetDesc/MSILMCTypeUtilities.h"
#include "MSILDataSegment.h"
#include "MSILMCInstLower.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/MSILLink.h"
#include "llvm/CLI/ImageLayout.h"
#include "llvm/CLI/Signature.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace llvm {
class AttributeList;
class Function;
class FunctionType;
class GlobalAlias;
class GlobalIFunc;
class GlobalValue;
class GlobalVariable;
class MCInst;
class MCSymbol;
class MSILFunctionInfo;
class MSILSubtarget;
class MSILTargetStreamer;
class MSILTrailerTextSink;
class MachineFunction;
class MachineInstr;
class Module;
class TargetMachine;

namespace MSIL {
/// The CIL signature of a call (\p TargetFunc null) or of the definition
/// \p TargetFunc of the IR function type \p FTy in \p Context: the lowering
/// of MSIL::computeSignatureVTs, mapped through MSIL::toValType, which ISel
/// applies to formal arguments, returns and call operands.
Signature getFunctionTypeSignature(const FunctionType *FTy,
                                   const Function *TargetFunc,
                                   const Function &Context,
                                   const TargetMachine &TM);

/// The ABI descriptor (plan 5.8.8, SIGNATURES; its Token is left 0) of a call
/// or definition of the IR function type \p FTy with the attributes
/// \p Attrs: NO_PROTOTYPE if \p NoPrototype; SRET with the size and alignment
/// of the returned aggregate for an sret first parameter (IL parameter 0);
/// and the IL parameter index, size and alignment of each byval parameter.
/// The object writer records it, and MSILFixFunctionBitcasts compares it
/// under "msil-lto" (cli::classifyCall).
MSILLink::SignatureInfo getABIDescriptor(const FunctionType *FTy,
                                         const AttributeList &Attrs,
                                         bool NoPrototype,
                                         const Function &Context,
                                         const TargetMachine &TM);

/// The CIL type of a value of type \p T (canonical listing v1's spelling is
/// ILAsm's for these).
cli::TypeSig toTypeSig(ValType T);

/// The cli::MethodSig of \p S (a static, default-convention method).
cli::MethodSig toMethodSig(const Signature &S);
} // end namespace MSIL

class LLVM_LIBRARY_VISIBILITY MSILAsmPrinter final : public AsmPrinter {
public:
  static char ID;

private:
  /// The function being printed (set by runOnMachineFunction).
  const MSILSubtarget *Subtarget = nullptr;
  MSILFunctionInfo *MFI = nullptr;
  /// True from the method header of that function to its closing brace.
  bool InMethod = false;
  /// Lowers MachineInstrs to CIL_ MCInsts, with the branch forms of the
  /// function being printed.
  std::unique_ptr<MSILMCInstLower> MCInstLowering;

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
  /// The functions that emitFunctionEntryLabel reported because an earlier
  /// method has their IL name and signature; an object leaves their symbols
  /// out without a second error (emitObjectEnd).
  SmallPtrSet<const Function *, 2> ClashingMethods;
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
  /// UsedExternGlobals in module order, set by emitEndOfAsmFile, each with an
  /// address field: the __start_/__stop_ symbols of output sections
  /// (StartStopFields), native data in msil-ld's linked text
  /// (DataImportVars), and errors, whose fields keep the output well formed.
  SmallVector<const GlobalVariable *, 0> ExternGlobals;
  /// llvm.global_ctors, sorted by priority: the module initializer calls them
  /// after the data segment is in place.
  SmallVector<Structor, 8> Ctors;
  /// The functions whose address is used, by FUNC_ADDR in code or as the
  /// target of a relocation of the data segment (noteFunctionAddress).
  SmallPtrSet<const Function *, 8> UsedFuncAddrs;
  /// UsedFuncAddrs in module order, set by emitEndOfAsmFile: the order of
  /// their address fields and of the module initializer code that sets them.
  SmallVector<const Function *, 0> FuncAddrs;
  /// In msil-ld's linked text, the symbol name of every P/Invoke that
  /// emitPInvokeDeclarations declared, once each, in declaration order (the
  /// names point into the set's keys, which never move): the operands of
  /// !msil.imports.used, which doFinalization adds (plan 5.10.13).
  StringSet<> LinkedPInvokeNames;
  SmallVector<StringRef, 0> LinkedPInvokes;
  /// The referenced external variables __start_<X> and __stop_<X> where <X>
  /// is an output section of the data image, with their offsets: address
  /// fields at the section's start and end (plan 5.9.9; not on Windows
  /// triples, which keep decision D26's error), set by collectExternGlobals.
  SmallVector<std::pair<const GlobalVariable *, uint64_t>, 2> StartStopFields;
  /// In msil-ld's linked text, the referenced external variables that native
  /// data defines, with the module and the entry of each (plan 5.10.12), set
  /// by collectExternGlobals: '<msil>init_dataimports' sets their fields.
  struct DataImportVar {
    const GlobalVariable *GV;
    std::string Module, Entry;
  };
  SmallVector<DataImportVar, 2> DataImportVars;

  //===------------------------------------------------------------------===//
  // Object mode (plan 5.8).
  //===------------------------------------------------------------------===//

  /// Whether the printer writes a CIL object (MSILTargetMachine::isObjectMode),
  /// set by doInitialization.
  bool ObjectMode = false;
  /// The MethodDef row of the method of each function this object defines,
  /// numbered in emission order (rule R6), set by emitFunctionEntryLabel.
  DenseMap<const Function *, unsigned> MethodRows;
  /// The functions of MethodRows in row order.
  SmallVector<const Function *, 0> MethodOrder;
  /// The identity of a MemberRef row (rule R3: parent, name, signature): the
  /// placeholder class (ObjectRefClass), the IL name, the signature of a
  /// method reference, and the BCL member of a BCL reference.
  using MemberRefKey =
      std::tuple<uint8_t, std::string, MSIL::Signature, unsigned>;
  /// The MemberRef row of each MemberRef the emitted token operands name,
  /// numbered by first appearance (rule R6).
  std::map<MemberRefKey, unsigned> MemberRefRows;
  /// The method MemberRefs on '<msil>Symbols' (calls of undefined functions,
  /// of aliases and of library calls) in row order, with their IL names and
  /// signatures: the references that SIGNATURES describes.
  SmallVector<std::tuple<unsigned, std::string, MSIL::Signature>, 0>
      SymbolMethodRefs;
  /// The import stubs that calls name (plan 5.8.3 R7: an undefined function
  /// with "msil-import-module" or "msil-import-name", per call signature), in
  /// first-use order.
  SmallVector<std::pair<const Function *, const MSIL::Signature *>, 4>
      CallStubs;
  std::set<std::pair<const Function *, MSIL::Signature>> CallStubSet;
  /// The global values and the external symbols that direct calls name.
  SmallPtrSet<const GlobalValue *, 16> CalledValues;
  StringSet<> CalledNames;
  /// The ABI descriptor (CallFlags, sret and byval) of the IR calls of
  /// undefined functions and of aliases, by IL name and call-site IL
  /// signature: the first call wins (rule R3). The flag says that a later
  /// call conflicted, and was reported.
  std::map<std::pair<std::string, MSIL::Signature>,
           std::pair<MSILLink::SignatureInfo, bool>>
      CallABIs;

public:
  MSILAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "MSIL Assembly Printer"; }

  /// The streamer for ILAsm directives (an MSILTargetAsmStreamer, the object
  /// target streamer of -filetype=obj, or an MSILTargetNullStreamer for
  /// -filetype=null).
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
  /// if it has an import, and reports it as an error otherwise: in a
  /// standalone module a Phase-1 import of the standalone import table
  /// (MSIL::getPInvokeModule, decision D14), in msil-ld's linked text a
  /// natively bound function (MSIL::getLinkedImport, plan 5.10.13). In object
  /// mode it is an undefined symbol of the link-info record instead.
  void noteExternFunction(const Function &F);

  /// Records that the external symbol \p Name (a library call) is called with
  /// signature \p S and returns the printer's copy of it, which the P/Invoke
  /// declaration of that name and signature prints. A symbol called with
  /// several signatures gets one declaration per signature. In object mode
  /// it is an undefined symbol of the record, in first-reference order.
  const MSIL::Signature &noteExternalSymbol(StringRef Name,
                                            const MSIL::Signature &S);

  /// Records that the code refers to the global variable \p GV, which this
  /// module does not define (a declaration, or an available_externally
  /// definition, which gets no data), so that the module trailer reports it:
  /// Phase 1 has no linker. In object mode it is an undefined symbol of the
  /// record instead.
  void noteExternGlobal(const GlobalVariable &GV);

  /// Reports that the code of the function being printed refers to \p GV, a
  /// global variable that holds no program data (MSIL::isSpecialLLVMGlobal)
  /// and therefore has no address field; once per function and global.
  void noteNonDataGlobal(const GlobalVariable &GV);

  /// Records that the address of \p F is used (FUNC_ADDR, or a relocation of
  /// the data segment) and returns the name of its address field,
  /// '<msil>fn_<IL name>' (MSIL::FnAddrFieldPrefix), which every use reads.
  /// <Module>::.cctor sets the field once, with 'ldftn' of the method or of
  /// the P/Invoke declaration:
  /// on .NET Framework, 'ldftn' in a method that the JIT compiles before the
  /// target first ran yields another value than in one compiled after, and
  /// two pointers to one C function must compare equal. In object mode the
  /// address is the function's FnAddr placeholder, and an undefined function
  /// whose address is used but that is never called gets an address-only
  /// import stub (plan 5.8.3).
  std::string noteFunctionAddress(const Function &F);

  /// The name of the address field of a function whose IL name is \p ILName.
  static std::string getFunctionAddressFieldName(StringRef ILName);

  //===------------------------------------------------------------------===//
  // MachineFunctionPass Implementation.
  //===------------------------------------------------------------------===//

  bool doInitialization(Module &M) override;
  bool runOnMachineFunction(MachineFunction &MF) override;
  /// Prints the module trailer (AsmPrinter::doFinalization), then, in
  /// msil-ld's linked text (MSIL::isLinkedModule), records the P/Invokes it
  /// declared in the named metadata !msil.imports.used, one operand
  /// !{!"<symbol>"} per symbol name in declaration order, for msil-ld's
  /// --print-imports (MSILLinkHooksPass::doFinalization, which the legacy
  /// pass manager runs after this one; plan 5.10.13, 5.10.14). The IL does
  /// not change, and a standalone module never gets the node.
  bool doFinalization(Module &M) override;

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
  /// Emits .maxstack and .locals, and chooses the branch forms of the
  /// function (MSILMCInstLower::computeBranchForms).
  void emitFunctionBodyStart() override;
  /// Emits the CIL_ MCInsts of \p MI (MSILMCInstLower::lower), which exist
  /// for exactly the instructions that computeBranchForms laid out.
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

  /// The section rules of the target triple (plan 5.9.9): COFF for Windows
  /// triples, ELF for the others.
  cli::SectionRules getSectionRules() const;

  /// Reports the global variables in named sections that the one-object
  /// link of whole-program output cannot give their meaning (plan 5.9.9):
  /// destructor tables, unsupported and thread-local sections, in IR order,
  /// naming the module's source file.
  void checkSections(const Module &M);

  /// Lists every global variable that the module refers to but does not
  /// define, in module order, in ExternGlobals, and reports it unless it is
  /// a __start_/__stop_ symbol of an output section (StartStopFields) or, in
  /// msil-ld's linked text, native data (DataImportVars).
  void collectExternGlobals(const Module &M);

  /// Lists the functions whose address is used in FuncAddrs, in module order.
  void collectFunctionAddresses(const Module &M);

  /// The name of the address field of \p F (getFunctionAddressFieldName).
  std::string getFunctionAddressField(const Function &F) const;

  /// Reports the address fields that ilasm would reject: a global variable
  /// named like the field of the data segment or like the address field of a
  /// function, two global variables with one IL name, and two functions with
  /// one IL name whose addresses are used.
  void checkAddressFieldNames(const Module &M);

  /// Returns the constructors in the order the module initializer calls
  /// them: the functions of llvm.global_ctors and of the 8-byte slots of the
  /// constructor-table sections (.init_array*, .CRT$XC*), sorted by
  /// cli::sortConstructors (plan 5.9.9); notes the undefined ones for
  /// P/Invoke declarations and diagnoses the entries it cannot call.
  SmallVector<const Function *, 8> collectConstructors(const Module &M);

  /// Emits the P/Invoke declarations of the undefined functions and external
  /// symbols that have an import, and diagnoses the rest: in a standalone
  /// module the Phase-1 imports of the standalone import table (decision
  /// D14, frozen), 'public'; in msil-ld's linked text the natively bound
  /// functions, with msil-ld's module and entry strings, 'assembly' (plan
  /// 5.10.11, 5.10.13), where the native-callee checks name the module (plan
  /// 5.10.14), and whose names it records in LinkedPInvokes, and the native
  /// functions of the data-import helper (plan 5.10.12). Those with the IL
  /// name and signature of a method of the module get none: they bind to
  /// that method, as a native link binds the symbol.
  void emitPInvokeDeclarations(const Module &M);

  /// Emits the data image, the relocation table that \p Sink recorded (loop
  /// mode) and the address fields (decision D12).
  void emitDataSegment(const MSILTrailerTextSink &Sink);

  /// Emits the address fields of the functions whose address is used.
  void emitFunctionAddressFields();

  /// Builds the module trailer with cli::buildTrailer (plan 5.9.10) and
  /// emits it: the data segment and the address fields, then the
  /// data-import helpers, <Module>::.cctor (which allocates and fills the
  /// data segment, sets the address fields of the globals and of the
  /// functions, initializes the data imports, applies the relocations and
  /// calls \p CtorFns) and '<msil>Main'(string[]), the .entrypoint method
  /// that calls main (decision D27), whose unsupported signatures it
  /// diagnoses.
  void emitTrailer(Module &M, ArrayRef<const Function *> CtorFns);

  /// Object mode: records the ABI descriptors of the IR calls in \p Caller
  /// (CallABIs) and warns about a call that conflicts with an earlier one of
  /// the same name and IL signature (rule R3).
  void collectCallABIs(const Function &Caller);

  /// Object mode: numbers the MemberRefs that the CIL instructions \p Insts
  /// of \p MI name (rule R6), and records the import stubs and the callees
  /// that \p MI calls.
  void noteObjectTokens(const MachineInstr &MI, ArrayRef<MCInst> Insts);

  /// Object mode: builds the link-info record (plan 5.8.8) and emits the
  /// address fields, the import stubs and the record (emitLinkInfo).
  void emitObjectEnd(Module &M);
};

} // end namespace llvm

#endif
