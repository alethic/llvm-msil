//===-- MSILAsmPrinter.cpp - MSIL LLVM assembly writer --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains a printer that converts from our internal representation
/// of machine-dependent LLVM code to ILAsm text.
///
/// The module header comes from emitStartOfAsmFile, each function becomes a
/// global method of '<Module>', and the module trailer (P/Invoke declarations,
/// the data image, the address fields of globals, the module initializer and
/// the entry point) comes from emitEndOfAsmFile. ILAsm resolves forward
/// references to methods and fields, so code refers to globals symbolically
/// and the trailer is laid out after the last function. Everything else the
/// base AsmPrinter would print (sections, linkage and visibility directives,
/// alignment, constant pools, jump tables) is suppressed here or by
/// MSILMCAsmInfo.
///
//===----------------------------------------------------------------------===//

#include "MSILAsmPrinter.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MCTargetDesc/MSILTargetStreamer.h"
#include "MSIL.h"
#include "MSILMachineFunctionInfo.h"
#include "MSILSubtarget.h"
#include "MSILUtilities.h"
#include "TargetInfo/MSILTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <string>

using namespace llvm;

#define DEBUG_TYPE "msil-asm-printer"

static cl::opt<std::string> MSILAssemblyName(
    "msil-assembly-name",
    cl::desc("MSIL: the name of the emitted assembly (default: the stem of "
             "the module's source file name, or 'module')"),
    cl::value_desc("name"));

/// The name of the assembly: -msil-assembly-name, else the stem of the
/// source file name (decision D28). Windows separators are recognized on
/// every host, so a module compiled on Windows gets the same name everywhere.
static std::string getAssemblyName(const Module &M) {
  if (!MSILAssemblyName.empty())
    return MSILAssemblyName;
  StringRef Stem =
      sys::path::stem(M.getSourceFileName(), sys::path::Style::windows);
  if (Stem.empty() || Stem == "." || Stem == "..")
    return "module";
  return Stem.str();
}

/// Returns the IR name of \p GV as it appears in the IR ("@name"), for
/// diagnostics.
static std::string getIRName(const GlobalValue &GV) {
  std::string Name;
  raw_string_ostream OS(Name);
  GV.printAsOperand(OS, /*PrintType=*/false);
  return Name;
}

//===----------------------------------------------------------------------===//
// Signature services.
//===----------------------------------------------------------------------===//

MSILTargetStreamer &MSILAsmPrinter::getTargetStreamer() {
  MCTargetStreamer *TS = OutStreamer->getTargetStreamer();
  assert(TS && "the MSIL target registers an asm and a null target streamer");
  return *static_cast<MSILTargetStreamer *>(TS);
}

const MSIL::Signature &MSILAsmPrinter::getSignature(const Function &F) {
  auto It = FuncSigs.find(&F);
  if (It != FuncSigs.end())
    return *It->second;

  SmallVector<MVT, 4> Params;
  SmallVector<MVT, 1> Results;
  MSIL::computeSignatureVTs(F.getFunctionType(), &F, F, TM, Params, Results);
  unsigned PointerSizeInBits = F.getDataLayout().getPointerSizeInBits();
  auto *Sig = new (SigAlloc.Allocate()) MSIL::Signature();
  for (MVT VT : Results)
    Sig->Results.push_back(MSIL::toValType(VT, PointerSizeInBits));
  for (MVT VT : Params)
    Sig->Params.push_back(MSIL::toValType(VT, PointerSizeInBits));
  FuncSigs[&F] = Sig;
  return *Sig;
}

const MSIL::Signature &
MSILAsmPrinter::internSignature(const MSIL::Signature &S) {
  // std::set never moves its elements, so the reference stays valid.
  return *CalliSigs.insert(S).first;
}

void MSILAsmPrinter::noteExternFunction(const Function &F) {
  assert(F.isDeclarationForLinker() &&
         "only undefined functions become P/Invoke declarations");
  ExternFuncs.insert({&F, &getSignature(F)});
}

const MSIL::Signature &
MSILAsmPrinter::noteExternalSymbol(StringRef Name, const MSIL::Signature &S) {
  // One P/Invoke declaration serves every call site, so the call sites must
  // agree; library calls of one name always do.
  auto [It, Inserted] = ExtSymSigs.try_emplace(Name, nullptr);
  if (Inserted)
    It->second = new (SigAlloc.Allocate()) MSIL::Signature(S);
  else if (*It->second != S)
    reportFatalInternalError("MSIL: external symbol '" + Name +
                             "' is called with two different signatures");
  return *It->second;
}

//===----------------------------------------------------------------------===//
// MachineFunctionPass Implementation.
//===----------------------------------------------------------------------===//

bool MSILAsmPrinter::doInitialization(Module &M) {
  // The tables belong to one module.
  FuncSigs.clear();
  ExtSymSigs.clear();
  CalliSigs.clear();
  ExternFuncs.clear();
  SigAlloc.DestroyAll();

  // The base class would copy module-level inline assembly into the output
  // verbatim. The legacy pass manager initializes the AsmPrinter before any
  // IR pass runs, so MSILPrepareIR cannot remove it first: diagnose it here
  // (decision D26) and drop it, so the output stays well formed.
  bool Changed = false;
  if (M.hasModuleInlineAsm()) {
    M.getContext().emitError(
        "module-level inline assembly is not supported for MSIL");
    M.removeModuleInlineAsm();
    Changed = true;
  }

  // For a "cfguard" module flag the base class would add a WinCFGuard
  // handler, which switches to the .gfids section at the end of the module;
  // the MSIL object format has none. As for module asm, MSILPrepareIR runs
  // too late: warn and clear the flag here, as for the stack protector
  // (decision D25). The flag keeps its merge behavior.
  if (M.getControlFlowGuardMode() != ControlFlowGuardMode::Disabled) {
    M.getContext().diagnose(DiagnosticInfoGeneric(
        "control flow guard is not supported for MSIL; ignored", DS_Warning));
    SmallVector<Module::ModuleFlagEntry, 8> Flags;
    M.getModuleFlagsMetadata(Flags);
    Module::ModFlagBehavior Behavior = Module::Warning;
    for (const Module::ModuleFlagEntry &Flag : Flags)
      if (Flag.Key->getString() == "cfguard")
        Behavior = Flag.Behavior;
    M.setModuleFlag(Behavior, "cfguard",
                    static_cast<uint32_t>(ControlFlowGuardMode::Disabled));
    Changed = true;
  }
  return AsmPrinter::doInitialization(M) || Changed;
}

bool MSILAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &MF.getSubtarget<MSILSubtarget>();
  MFI = MF.getInfo<MSILFunctionInfo>();

  // The base class would print prefix data, prologue data and the
  // -fsanitize=function signature as raw data in front of the method.
  const Function &F = MF.getFunction();
  if (F.hasPrefixData() || F.hasPrologueData() ||
      F.getMetadata(LLVMContext::MD_func_sanitize)) {
    F.getContext().diagnose(DiagnosticInfoUnsupported(
        F, "prefix data, prologue data and function sanitizer metadata are "
           "not supported for MSIL"));
    return false;
  }

  AsmPrinter::runOnMachineFunction(MF);
  // Close the method after everything the base class prints, including the
  // end-of-function label that -g adds after emitFunctionBodyEnd (NVPTX does
  // the same).
  getTargetStreamer().emitMethodEnd(MSIL::getILName(F, TM));
  return false;
}

//===----------------------------------------------------------------------===//
// AsmPrinter Implementation.
//===----------------------------------------------------------------------===//

void MSILAsmPrinter::emitStartOfAsmFile(Module &M) {
  // Without a byte order mark ilasm reads the file in the ANSI code page and
  // mangles non-ASCII identifiers (decision D11). The header comments escape
  // non-ASCII bytes, so only the identifiers matter: the global names and the
  // assembly name.
  std::string AssemblyName = getAssemblyName(M);
  bool BOM = !isASCII(AssemblyName) ||
             any_of(M.global_values(), [](const GlobalValue &GV) {
               return !isASCII(GV.getName());
             });

  MSILTargetStreamer &TS = getTargetStreamer();
  TS.emitFileHeader(BOM, M.getSourceFileName(), TM.getTargetTriple().str());
  TS.emitAssemblyRefs();
  TS.emitAssembly(AssemblyName);
}

void MSILAsmPrinter::emitEndOfAsmFile(Module &M) {
  // The module trailer comes with the features that need it: the entry point
  // in M3, the P/Invoke declarations, the data image, the address fields and
  // the module initializer in M8.
}

void MSILAsmPrinter::emitLinkage(const GlobalValue *GV, MCSymbol *GVSym) const {
  // ILAsm has no linkage directives: the access of a method or field
  // ('public' or 'assembly') is part of its declaration.
}

void MSILAsmPrinter::emitConstantPool() {
  // FP immediates are legal (ldc.r4, ldc.r8), so nothing is ever placed in a
  // constant pool, and ILAsm could not express one.
  if (!MF->getConstantPool()->isEmpty())
    reportFatalInternalError("MSIL: constant pools are not supported");
}

void MSILAsmPrinter::emitJumpTableInfo() {
  // Nothing to do: BR_TABLE_I32 prints 'switch' with its targets inline.
}

void MSILAsmPrinter::emitGlobalVariable(const GlobalVariable *GV) {
  // llvm.used, llvm.compiler.used, llvm.global_ctors and the other llvm.*
  // globals, and data in the llvm.metadata section, are not program data.
  if (GV->getName().starts_with("llvm.") ||
      GV->getSection() == "llvm.metadata") {
    if (GV->getName() == "llvm.global_ctors" && GV->hasInitializer() &&
        !GV->getInitializer()->isNullValue())
      GV->getContext().emitError("global constructors (llvm.global_ctors) "
                                 "are not supported for MSIL yet: they are "
                                 "implemented in M8");
    return;
  }
  // A declaration nothing refers to needs no address field.
  if (GV->isDeclarationForLinker() && GV->use_empty())
    return;
  // Temporary: M8 replaces this with the data segment (decision D12).
  GV->getContext().emitError("global variable " + getIRName(*GV) +
                             " is not supported for MSIL yet: global "
                             "variables are implemented in M8");
}

void MSILAsmPrinter::emitGlobalAlias(const Module &M, const GlobalAlias &GA) {
  // MSILPrepareIR replaces aliases by poison and erases them (decision D26),
  // so only a pipeline that skips it gets here.
  GA.getContext().emitError("alias " + getIRName(GA) +
                            " is not supported for MSIL");
}

void MSILAsmPrinter::emitGlobalIFunc(Module &M, const GlobalIFunc &GI) {
  // As for aliases (decision D26).
  GI.getContext().emitError("ifunc " + getIRName(GI) +
                            " is not supported for MSIL");
}

void MSILAsmPrinter::emitFunctionEntryLabel() {
  // STUB: the method header (TS.emitMethodBegin) is implemented in M3.
  reportFatalUsageError("MSIL: function bodies are implemented in M3");
}

void MSILAsmPrinter::emitFunctionBodyStart() {
  // STUB: .maxstack and .locals are implemented in M3.
  reportFatalUsageError("MSIL: function bodies are implemented in M3");
}

void MSILAsmPrinter::emitInstruction(const MachineInstr *MI) {
  // STUB: instruction emission through MSILMCInstLower is implemented in M3.
  reportFatalUsageError("MSIL: function bodies are implemented in M3");
}

char MSILAsmPrinter::ID = 0;

INITIALIZE_PASS(MSILAsmPrinter, "msil-asm-printer", "MSIL Assembly Printer",
                false, false)

// Force static initialization.
extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMSILAsmPrinter() {
  RegisterAsmPrinter<MSILAsmPrinter> X(getTheMSILTarget());
}
