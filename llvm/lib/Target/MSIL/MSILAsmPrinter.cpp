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
/// global method of '<Module>' (header, .maxstack, .locals, the instructions
/// through MSILMCInstLower and MSILInstPrinter, and the closing brace), and
/// the module trailer (P/Invoke declarations, the data image, the address
/// fields of globals and of the functions whose address is used, the module
/// initializer and the entry point) comes from emitEndOfAsmFile. ILAsm
/// resolves forward references to methods and fields, so code refers to
/// globals symbolically and the trailer is laid out after the last function.
/// Everything else the base AsmPrinter would print (sections, linkage and
/// visibility directives, alignment, constant pools, jump tables) is
/// suppressed here or by MSILMCAsmInfo.
///
//===----------------------------------------------------------------------===//

#include "MSILAsmPrinter.h"
#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MCTargetDesc/MSILTargetStreamer.h"
#include "MSIL.h"
#include "MSILDataSegment.h"
#include "MSILMCInstLower.h"
#include "MSILMachineFunctionInfo.h"
#include "MSILSubtarget.h"
#include "MSILUtilities.h"
#include "TargetInfo/MSILTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>

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

/// The signature that \p MFI records (the parameters and results lowered by
/// ISel), in CIL types. The method header prints MSILAsmPrinter::getSignature
/// instead, which call sites share; the two must agree.
[[maybe_unused]] static MSIL::Signature
getFunctionInfoSignature(const MSILFunctionInfo &MFI,
                         unsigned PointerSizeInBits) {
  MSIL::Signature Sig;
  for (MVT VT : MFI.getResults())
    Sig.Results.push_back(MSIL::toValType(VT, PointerSizeInBits));
  for (MVT VT : MFI.getParams())
    Sig.Params.push_back(MSIL::toValType(VT, PointerSizeInBits));
  return Sig;
}

/// Whether \p MI, if it is a direct call, pushes and pops exactly what the
/// 'call' token, which spells the callee's signature, promises. LowerCall
/// builds a direct call only when the call site's function type is the
/// callee's and lowers its operands and results the way getSignature lowers
/// that type; a mismatch would make the JIT reject the method
/// (MissingMethodException or an unbalanced stack).
[[maybe_unused]] static bool matchesCalleeSignature(const MachineInstr &MI,
                                                    MSILAsmPrinter &AP) {
  if (!MSIL::isCallDirect(MI.getOpcode()))
    return true;
  const MachineOperand &Callee = MSIL::getCalleeOp(MI);
  const auto *F =
      Callee.isGlobal() ? dyn_cast<Function>(Callee.getGlobal()) : nullptr;
  return !F || MSILMCInstLower::getCallSiteSignature(MI) == AP.getSignature(*F);
}

/// ldarg, starg, ldloc and stloc take 16-bit indices, and ilasm silently
/// truncates larger ones. Index 0xFFFF is avoided.
static constexpr size_t MaxArgsOrLocals = 0xFFFF;

/// The name of the generated entry point (decision D27). The names that the
/// printer generates start with '<msil>', but an asm label or the IR can
/// spell any name: a function named '<msil>Main' is another method of that
/// name, because no C function takes a string[].
static constexpr StringRef EntryPointName = "<msil>Main";

/// The names of the data segment (decision D12, plan section 5.1): the field
/// that holds the segment's base address, and the RVA blob of its initialized
/// part with its value type and its static field. No dots in the type name:
/// ilasm would split it into a namespace and a name. Only DataFieldName can
/// collide with the address field of a global variable
/// (checkAddressFieldNames); the image field has a type of its own.
static constexpr StringRef DataFieldName = "<msil>data";
static constexpr StringRef ImageTypeName = "<msil>image_t";
static constexpr StringRef ImageFieldName = "<msil>image";
static constexpr StringRef ImageLabel = "<msil>image_d";

/// The prefix of the address field of a function whose address is used: the
/// field '<msil>fn_<IL name>' holds the value of 'ldftn' (plan section 12,
/// 'Phase-1 function addresses'). An asm label can give a global variable
/// such a name (checkAddressFieldNames).
static constexpr StringRef FuncFieldPrefix = "<msil>fn_";

/// The BCL method that allocates the data segment. It works on every runtime
/// of decision D10, and its memory is neither zeroed nor freed.
static constexpr StringRef AllocHGlobal =
    "call native int [mscorlib]System.Runtime.InteropServices."
    "Marshal::AllocHGlobal(native int)";

/// Whether \p Name is a constructor name, which no method or P/Invoke of a
/// function may have: the runtime runs '<Module>::.cctor', which the printer
/// generates (emitModuleInitializer), as the module initializer, and refuses
/// to load a type with a static '.ctor'.
static bool isReservedMethodName(StringRef Name) {
  return Name == ".cctor" || Name == ".ctor";
}

/// Reports that the function \p F, which gets a method or a P/Invoke
/// declaration, has the reserved IL name \p Name.
static void reportReservedMethodName(const Function &F, StringRef Name) {
  F.getContext().emitError("function " + MSIL::getIRName(F) +
                           " has the reserved IL name '" + Name + "'");
}

/// An 'ldc.i4' of \p Value: in its shortest form, as MSILInstPrinter prints
/// CONST_I32, if \p ShortForm, else always 'ldc.i4 <Value>', as it prints the
/// offset of GLOBAL_ADDR.
static std::string getLdcI4(int32_t Value, bool ShortForm = true) {
  if (ShortForm && Value == -1)
    return "ldc.i4.m1";
  if (ShortForm && Value >= 0 && Value <= 8)
    return ("ldc.i4." + Twine(Value)).str();
  if (ShortForm && isInt<8>(Value))
    return ("ldc.i4.s " + Twine(Value)).str();
  return ("ldc.i4 " + Twine(Value)).str();
}

namespace {
/// The body of a method that the printer writes itself ('<msil>Main', and the
/// module initializer): CIL lines with their stack effects, so that the
/// .maxstack of the method is computed, not guessed (the JIT rejects a method
/// whose .maxstack is too small with InvalidProgramException).
class SynthesizedBody {
  SmallVector<std::string, 0> Lines;
  unsigned Depth = 0;
  unsigned MaxStack = 0;

public:
  /// One CIL instruction that pops \p Pops values and then pushes \p Pushes.
  void addInst(const Twine &Text, unsigned Pops, unsigned Pushes) {
    assert(Depth >= Pops && "synthesized code underflows the stack");
    Depth = Depth - Pops + Pushes;
    MaxStack = std::max(MaxStack, Depth);
    Lines.push_back(("  " + Text).str());
  }

  /// A branch that pops \p Pops values. Synthesized code branches and joins
  /// with an empty evaluation stack only, so every label has depth 0.
  void addBranch(const Twine &Text, unsigned Pops) {
    addInst(Text, Pops, 0);
    assert(Depth == 0 && "synthesized code branches with a non-empty stack");
  }

  /// A branch target (see addBranch).
  void addLabel(StringRef Name) {
    assert(Depth == 0 && "synthesized code joins with a non-empty stack");
    Lines.push_back((Name + ":").str());
  }

  /// Pushes \p Value as a native int (decision D2): 'ldc.i4' (see getLdcI4
  /// for \p ShortForm) or 'ldc.i8', then 'conv.i'.
  void addNativeInt(int64_t Value, bool ShortForm) {
    if (isInt<32>(Value))
      addInst(getLdcI4(static_cast<int32_t>(Value), ShortForm), 0, 1);
    else
      addInst("ldc.i8 " + Twine(Value), 0, 1);
    addInst("conv.i", 1, 1);
  }

  /// Adds \p Offset to the native int on top of the stack, exactly as
  /// MSILInstPrinter does after the 'ldsfld' of GLOBAL_ADDR; nothing for 0.
  void addOffset(int64_t Offset) {
    if (Offset == 0)
      return;
    if (isInt<32>(Offset)) {
      addInst(getLdcI4(static_cast<int32_t>(Offset), /*ShortForm=*/false), 0,
              1);
    } else {
      addInst("ldc.i8 " + Twine(Offset), 0, 1);
      addInst("conv.i", 1, 1);
    }
    addInst("add", 2, 1);
  }

  /// Writes .maxstack, the .locals line for \p Locals (the comma-separated
  /// types; nothing if empty) and the instructions.
  void emit(MSILTargetStreamer &TS, StringRef Locals) const {
    assert(Depth == 0 && "synthesized code falls off the end of the method");
    TS.emitMaxStack(MaxStack);
    // Never 'init' (decision D13): every local is written before it is read.
    if (!Locals.empty())
      TS.emitILLine("  .locals (" + Locals + ")");
    for (const std::string &Line : Lines)
      TS.emitILLine(Line);
  }
};
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Signature services.
//===----------------------------------------------------------------------===//

MSILTargetStreamer &MSILAsmPrinter::getTargetStreamer() {
  MCTargetStreamer *TS = OutStreamer->getTargetStreamer();
  assert(TS && "the MSIL target registers an asm and a null target streamer");
  return *static_cast<MSILTargetStreamer *>(TS);
}

const MSIL::Signature &MSILAsmPrinter::getSignature(const Function &F) {
  assert(!F.isIntrinsic() && "an intrinsic has no CIL method");
  auto [It, Inserted] = FuncSigs.try_emplace(&F, nullptr);
  if (!Inserted)
    return *It->second;

  // Only the IR function type counts, never a body or the caller: ISel lowers
  // the formal arguments and the return of a definition, and the operands and
  // results of every call to it, from this type (call sites whose type
  // differs from the callee's are diagnosed by LowerCall), so a declaration
  // gets the signature its definition would have. Narrow integers widen to
  // int32, 64-bit integers and pointers are native int (decision D2), and a
  // return of more than one value becomes a leading pointer parameter. The
  // variable part of a variadic function is not part of the signature
  // (variadic definitions and calls are diagnosed in Phase 1, D19).
  SmallVector<MVT, 4> Params;
  SmallVector<MVT, 1> Results;
  MSIL::computeSignatureVTs(F.getFunctionType(), &F, F, TM, Params, Results);
  unsigned PointerSizeInBits = F.getDataLayout().getPointerSizeInBits();
  auto *Sig = new (SigAlloc.Allocate()) MSIL::Signature();
  for (MVT VT : Results)
    Sig->Results.push_back(MSIL::toValType(VT, PointerSizeInBits));
  for (MVT VT : Params)
    Sig->Params.push_back(MSIL::toValType(VT, PointerSizeInBits));
  assert(Sig->Results.size() <= 1 && "a CIL method returns at most one value");
  It->second = Sig;
  return *Sig;
}

const MSIL::Signature &
MSILAsmPrinter::internSignature(const MSIL::Signature &S) {
  // std::set never moves its elements, so the reference stays valid.
  return *CalliSigs.insert(S).first;
}

void MSILAsmPrinter::noteExternFunction(const Function &F) {
  // An available_externally definition counts as undefined: it gets no
  // MachineFunction and therefore no method.
  assert(F.isDeclarationForLinker() && !F.isIntrinsic() &&
         "only undefined functions become P/Invoke declarations");
  ExternFuncs.insert({&F, &getSignature(F)});
}

const MSIL::Signature &
MSILAsmPrinter::noteExternalSymbol(StringRef Name, const MSIL::Signature &S) {
  // One P/Invoke declaration per name and signature serves every call site
  // with that signature. One name can come with several: llvm.ldexp with an
  // i32 and an i64 exponent, or the function of -ftrap-function that is also
  // a math library call. CIL methods may be overloaded by signature, so each
  // gets its own declaration. The map owns the name: some symbol names live
  // in storage that the printer does not own.
  auto It = ExtSymSigs.try_emplace(Name).first;
  for (const MSIL::Signature *Sig : It->second)
    if (*Sig == S)
      return *Sig;
  const auto *Sig = new (SigAlloc.Allocate()) MSIL::Signature(S);
  It->second.push_back(Sig);
  ExtSymOrder.push_back({It->getKey(), Sig});
  return *Sig;
}

void MSILAsmPrinter::noteExternGlobal(const GlobalVariable &GV) {
  assert(GV.isDeclarationForLinker() &&
         "only undefined global variables are reported by reference");
  UsedExternGlobals.insert(&GV);
}

void MSILAsmPrinter::noteNonDataGlobal(const GlobalVariable &GV) {
  assert(MF && MSIL::isSpecialLLVMGlobal(GV) &&
         "a non-data global referred to outside a function");
  if (!NonDataRefs.insert(&GV).second)
    return;
  const Function &F = MF->getFunction();
  F.getContext().emitError("function '" + MSIL::getILName(F, TM) +
                           "' refers to '" + MSIL::getILName(GV, TM) +
                           "', which is not program data");
}

std::string MSILAsmPrinter::noteFunctionAddress(const Function &F) {
  assert(!F.isIntrinsic() && "an intrinsic has no address");
  UsedFuncAddrs.insert(&F);
  return getFunctionAddressField(F);
}

std::string MSILAsmPrinter::getFunctionAddressFieldName(StringRef ILName) {
  return (FuncFieldPrefix + ILName).str();
}

std::string MSILAsmPrinter::getFunctionAddressField(const Function &F) const {
  return getFunctionAddressFieldName(MSIL::getILName(F, TM));
}

//===----------------------------------------------------------------------===//
// MachineFunctionPass Implementation.
//===----------------------------------------------------------------------===//

bool MSILAsmPrinter::doInitialization(Module &M) {
  // The tables belong to one module.
  FuncSigs.clear();
  ExtSymSigs.clear();
  ExtSymOrder.clear();
  CalliSigs.clear();
  ExternFuncs.clear();
  Methods.clear();
  SigAlloc.DestroyAll();
  Data = std::make_unique<MSILDataSegment>();
  SymToGV.clear();
  SymToGVBuilt = false;
  UsedExternGlobals.clear();
  ExternGlobals.clear();
  Ctors.clear();
  UsedFuncAddrs.clear();
  FuncAddrs.clear();

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
  NonDataRefs.clear();

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
  assert(!InMethod && "emitJumpTableInfo closes every method");
  return false;
}

void MSILAsmPrinter::emitMethodEnd() {
  assert(InMethod && "no method to close");
  getTargetStreamer().emitMethodEnd(MSIL::getILName(MF->getFunction(), TM));
  InMethod = false;
}

//===----------------------------------------------------------------------===//
// AsmPrinter Implementation.
//===----------------------------------------------------------------------===//

void MSILAsmPrinter::emitStartOfAsmFile(Module &M) {
  // Without a byte order mark ilasm reads the file in the ANSI code page and
  // mangles non-ASCII identifiers and quoted strings (decision D11). The
  // header comments escape non-ASCII bytes, so only the text that is printed
  // raw matters: the assembly name, the global names, and the library and
  // entry point strings of the P/Invoke declarations, which come from the
  // "msil-import-*" attributes (the names of library calls are ASCII).
  std::string AssemblyName = getAssemblyName(M);
  auto HasNonASCIIImport = [](const Function &F) {
    for (StringRef Kind : {"msil-import-module", "msil-import-name"})
      if (!isASCII(F.getFnAttribute(Kind).getValueAsString()))
        return true;
    return false;
  };
  bool BOM =
      !isASCII(AssemblyName) ||
      any_of(M.global_values(),
             [](const GlobalValue &GV) { return !isASCII(GV.getName()); }) ||
      any_of(M.functions(), HasNonASCIIImport);

  MSILTargetStreamer &TS = getTargetStreamer();
  TS.emitFileHeader(BOM, M.getSourceFileName(), TM.getTargetTriple().str());
  TS.emitAssemblyRefs();
  TS.emitAssembly(AssemblyName);
}

void MSILAsmPrinter::emitEndOfAsmFile(Module &M) {
  // Every global variable has been added (emitGlobalVariable runs for each
  // one before this hook). The layout comes first: a relocation can name a
  // function, whose address field it reads, and an undefined one, whose
  // 'ldftn' targets its P/Invoke declaration, as can a constructor, or an
  // undefined global variable, which is an error.
  Data->finalize();
  for (const MSILReloc &R : Data->Relocs) {
    if (const auto *F = dyn_cast<Function>(R.Target)) {
      noteFunctionAddress(*F);
      if (F->isDeclarationForLinker())
        noteExternFunction(*F);
    } else if (const auto *GV = dyn_cast<GlobalVariable>(R.Target);
               GV && GV->isDeclarationForLinker()) {
      noteExternGlobal(*GV);
    }
  }
  // finalize() reports a symbol difference with an undefined end; the
  // variable is still an unresolved reference.
  for (const MSILDiff &D : Data->Diffs)
    for (const GlobalVariable *GV : {D.A, D.B})
      if (GV->isDeclarationForLinker())
        noteExternGlobal(*GV);
  collectExternGlobals(M);
  collectFunctionAddresses(M);
  checkAddressFieldNames(M);
  SmallVector<const Function *, 8> CtorFns = collectConstructors();

  // ILAsm resolves forward references, so the order only serves the reader
  // (plan section 5.1): the P/Invoke declarations, the data segment with its
  // address fields, the address fields of functions, the module initializer
  // and the entry point.
  emitPInvokeDeclarations(M);
  emitDataSegment();
  emitFunctionAddressFields();
  emitModuleInitializer(CtorFns);

  // An available_externally main has no method (no MachineFunction is
  // emitted for it), so it gets no entry point either.
  if (const Function *Main = M.getFunction("main");
      Main && !Main->isDeclarationForLinker())
    emitEntryPoint(*Main);
}

const DenseMap<const MCSymbol *, const GlobalValue *> &
MSILAsmPrinter::getSymToGV(const Module &M) {
  // AsmPrinter::lowerConstant names a global value in an initializer by
  // getSymbol, so these are the symbols the data collector sees.
  if (!SymToGVBuilt) {
    for (const GlobalValue &GV : M.global_values())
      if (const auto *F = dyn_cast<Function>(&GV); !F || !F->isIntrinsic())
        SymToGV[getSymbol(&GV)] = &GV;
    SymToGVBuilt = true;
  }
  return SymToGV;
}

void MSILAsmPrinter::collectExternGlobals(const Module &M) {
  // Phase 1 has no linker, so a variable that another module defines cannot
  // be resolved (decision D26). Only a reference is an error (Design.md 5.2
  // step 4): a declaration that nothing emitted refers to (unused, or used
  // only by an available_externally function, which is not compiled) is
  // not.
  for (const GlobalVariable &GV : M.globals()) {
    if (!UsedExternGlobals.contains(&GV))
      continue;
    StringRef ILName = MSIL::getILName(GV, TM);
    GV.getContext().emitError("external global variable '" + ILName +
                              "' is not supported (phase 2 linker)");
    // The address field is a new place where the name is printed (D11).
    if (ILName.size() > MSIL::MaxILNameBytes)
      GV.getContext().emitError(
          "global variable " + MSIL::getIRName(GV) + ": identifier of " +
          Twine(ILName.size()) + " bytes is longer than the " +
          Twine(MSIL::MaxILNameBytes) + " bytes ilasm accepts");
    ExternGlobals.push_back(&GV);
  }
}

void MSILAsmPrinter::collectFunctionAddresses(const Module &M) {
  // A function used only as a callee, the constructors that the module
  // initializer calls included, needs no address. An undefined function that
  // is diagnosed keeps its field, as a referenced extern global does, so the
  // code that reads it stays well formed.
  for (const Function &F : M.functions()) {
    if (!UsedFuncAddrs.contains(&F))
      continue;
    // The address field is a new place where the name is printed (D11). A
    // name that is too long by itself is reported with its function
    // (MSILPrepareIR).
    StringRef ILName = MSIL::getILName(F, TM);
    size_t Size = FuncFieldPrefix.size() + ILName.size();
    if (ILName.size() <= MSIL::MaxILNameBytes && Size > MSIL::MaxILNameBytes)
      F.getContext().emitError(
          "function " + MSIL::getIRName(F) + ": address field identifier of " +
          Twine(Size) + " bytes is longer than the " +
          Twine(MSIL::MaxILNameBytes) + " bytes ilasm accepts");
    FuncAddrs.push_back(&F);
  }
}

void MSILAsmPrinter::checkAddressFieldNames(const Module &M) {
  // Every address field is a 'native int', so unlike methods, two fields
  // cannot share a name: ilasm rejects the second ('Duplicate field
  // declaration'), and an IL name drops the '\1' of an IR name (@foo and
  // @"\01foo") or comes from the Mangler (@0 is '__unnamed_1'). In module
  // order, so the first of two is the one named in the error.
  SmallPtrSet<const GlobalVariable *, 16> HasField;
  for (const MSILDataSegment::Entry &E : Data->Entries)
    HasField.insert(E.GV);
  HasField.insert_range(ExternGlobals);
  StringMap<const GlobalVariable *> Fields;
  for (const GlobalVariable &GV : M.globals()) {
    if (!HasField.contains(&GV))
      continue;
    StringRef Name = MSIL::getILName(GV, TM);
    if (Name == DataFieldName) {
      GV.getContext().emitError("global variable " + MSIL::getIRName(GV) +
                                " has the reserved IL name '" + Name + "'");
      continue;
    }
    auto [It, Inserted] = Fields.try_emplace(Name, &GV);
    if (!Inserted)
      GV.getContext().emitError("global variable " + MSIL::getIRName(GV) +
                                " has the same IL name '" + Name + "' as " +
                                MSIL::getIRName(*It->second));
  }

  // The address fields of functions are '<msil>fn_<IL name>'. Two functions
  // with one IL name may both have methods (their signatures differ) but not
  // both fields, and an asm label can give a global variable the name of one.
  StringMap<const Function *> FuncFields;
  for (const Function *F : FuncAddrs) {
    std::string Name = getFunctionAddressField(*F);
    auto [It, Inserted] = FuncFields.try_emplace(Name, F);
    if (!Inserted) {
      F->getContext().emitError(
          "function " + MSIL::getIRName(*F) + " has the same IL name '" +
          MSIL::getILName(*F, TM) + "' as " + MSIL::getIRName(*It->second) +
          ", and the addresses of both are used");
      continue;
    }
    if (auto GVIt = Fields.find(Name); GVIt != Fields.end())
      GVIt->second->getContext().emitError(
          "global variable " + MSIL::getIRName(*GVIt->second) +
          " has the IL name '" + Name + "' of the address field of function " +
          MSIL::getIRName(*F));
  }
}

SmallVector<const Function *, 8> MSILAsmPrinter::collectConstructors() {
  SmallVector<const Function *, 8> Fns;
  for (const Structor &S : Ctors) {
    // MSILPrepareIR replaces a diagnosed alias or ifunc by poison.
    if (isa<UndefValue>(S.Func))
      continue;
    const auto *F = dyn_cast<Function>(S.Func->stripPointerCasts());
    if (!F || F->isIntrinsic()) {
      S.Func->getContext().emitError(
          "a global constructor that is not a function is not supported for "
          "MSIL");
      continue;
    }
    // The module initializer calls each constructor without arguments (C's
    // __attribute__((constructor)) functions take none) and drops a result.
    if (!getSignature(*F).Params.empty()) {
      F->getContext().emitError("global constructor '" +
                                MSIL::getILName(*F, TM) +
                                "' takes arguments, which MSIL does not pass");
      continue;
    }
    if (F->isDeclarationForLinker())
      noteExternFunction(*F);
    Fns.push_back(F);
  }
  return Fns;
}

/// Whether a native C function passes a parameter or returns a result of type
/// \p Ty exactly as the one CIL value that MSIL::computeSignatureVTs makes of
/// it, so that a P/Invoke of the function can pass it: an integer of at most
/// 64 bits (int32 or native int), a pointer, float or double. ISel splits or
/// demotes wider integers, vectors and aggregates, unlike the native calling
/// convention (a { i64, i64 } result comes back in two registers on x86-64
/// Linux, not through a pointer), and promotes half.
static bool isNativeScalar(const Type *Ty) {
  if (const auto *ITy = dyn_cast<IntegerType>(Ty))
    return ITy->getBitWidth() <= 64;
  return Ty->isPointerTy() || Ty->isFloatTy() || Ty->isDoubleTy();
}

/// Whether the P/Invoke of the undefined function \p F passes its arguments
/// and result as the native function expects them.
static bool hasNativeSignature(const Function &F) {
  const FunctionType *FTy = F.getFunctionType();
  if (!FTy->getReturnType()->isVoidTy() &&
      !isNativeScalar(FTy->getReturnType()))
    return false;
  for (unsigned I = 0, E = FTy->getNumParams(); I != E; ++I) {
    if (!isNativeScalar(FTy->getParamType(I)))
      return false;
    // The native callee finds a by-value aggregate in its argument area, but
    // the method would pass the address of a copy. The address of an
    // aggregate result (sret) goes in a register of its own on some targets
    // (X8 on AArch64), but the method passes it as an ordinary first
    // argument. A static chain (nest) has a register of its own too.
    if (F.hasParamAttribute(I, Attribute::ByVal) ||
        F.hasParamAttribute(I, Attribute::InAlloca) ||
        F.hasParamAttribute(I, Attribute::Preallocated) ||
        F.hasParamAttribute(I, Attribute::StructRet) ||
        F.hasParamAttribute(I, Attribute::Nest))
      return false;
  }
  return true;
}

/// Reports the undefined function or library call \p Name, which is not a
/// Phase-1 import (decision D14): Phase 1 has no linker that could resolve it.
/// An undefined function of the module gets the same text whatever its name,
/// the compiler-rt and __atomic_* helpers that clang and AtomicExpand declare
/// included. A library call (\p IsLibCall) gets the text of LowerCall's
/// library-call policy, which rejects such calls before they are built, so
/// that is only a backstop.
static void reportNotImported(LLVMContext &Ctx, StringRef Name,
                              bool IsLibCall) {
  if (!IsLibCall)
    Ctx.emitError("undefined function '" + Name +
                  "' is not defined in this module and is not a Phase-1 "
                  "import (puts, putchar, the C math library, "
                  "msil-import-module); Phase 1 has no linker");
  else if (Name.starts_with("__"))
    Ctx.emitError("runtime helper '" + Name +
                  "' is not available for MSIL yet (phase 3)");
  else
    Ctx.emitError("library call '" + Name + "' is not a Phase-1 import");
}

void MSILAsmPrinter::emitPInvokeDeclarations(const Module &M) {
  const Triple &TT = TM.getTargetTriple();
  LLVMContext &Ctx = M.getContext();
  MSILTargetStreamer &TS = getTargetStreamer();

  // One declaration per method: a library call and a call of a declared
  // function of the same name and signature (sqrt from C and from the
  // legalizer) share it. CIL methods may differ in their signature only.
  std::set<std::pair<StringRef, MSIL::Signature>> Declared;
  auto Declare = [&](const Function *F, StringRef Name,
                     const MSIL::Signature &Sig, StringRef Lib,
                     StringRef Entry) {
    if (isReservedMethodName(Name)) {
      assert(F && "the import table has no constructor names");
      reportReservedMethodName(*F, Name);
      return;
    }
    if (!Declared.insert({Name, Sig}).second)
      return;
    if (Declared.size() == 1)
      TS.emitILLine("// ---- undefined external functions: P/Invoke ----");
    // Blittable parameters only (int32, native int, float32, float64), so
    // 'preservesig' is all the marshaller needs (decision D10).
    TS.emitPInvoke({Name, &Sig, /*Public=*/true}, Lib, Entry);
  };

  for (const auto &[F, Sig] : ExternFuncs) {
    StringRef Name = MSIL::getILName(*F, TM);
    // A function that this module defines with the same IL name and
    // signature is the one a native link would bind the symbol to. The calls
    // and 'ldftn's of F name a method by its name and signature, so they
    // already reach that method, and a P/Invoke would duplicate it.
    if (Methods.count({Name, *Sig}))
      continue;
    // The variable arguments of a native variadic function need the calling
    // convention support of phase 3 (decisions D14 and D19).
    if (F->isVarArg()) {
      Ctx.emitError("undefined variadic function '" + Name +
                    "' is not supported for MSIL yet (phase 3)");
      continue;
    }
    std::optional<StringRef> Lib = MSIL::getPInvokeModule(TT, Name, F);
    if (!Lib) {
      reportNotImported(Ctx, Name, /*IsLibCall=*/false);
      continue;
    }
    // Only scalars cross the boundary as the native calling convention
    // passes them (the imports of D14 take and return nothing else).
    if (!hasNativeSignature(*F)) {
      Ctx.emitError("imported function '" + Name +
                    "' has a parameter or result that a P/Invoke cannot pass "
                    "natively (aggregates, vectors, 128-bit integers and half "
                    "are not supported for MSIL yet)");
      continue;
    }
    Declare(F, Name, *Sig, *Lib, MSIL::getPInvokeEntry(Name, F));
  }

  for (const auto &[Name, Sig] : ExtSymOrder) {
    // A library call of a method that this module defines (by IL name and
    // signature) calls that method, as a native link would resolve it.
    if (Methods.count({Name, *Sig}))
      continue;
    // A function of the same IL name takes part only if its method has this
    // signature, as in LowerCall (matchesSignature): a declaration supplies
    // its "msil-import-*" attributes. Otherwise the library and the entry
    // point come from the import table alone.
    const Function *F = MSIL::getFunctionByILName(M, Name);
    if (F && (F->isIntrinsic() || getSignature(*F) != *Sig))
      F = nullptr;
    if (F && !F->isDeclarationForLinker())
      continue;
    // LowerCall builds a library call only for a Phase-1 import (D14).
    std::optional<StringRef> Lib = MSIL::getPInvokeModule(TT, Name, F);
    if (!Lib) {
      reportNotImported(Ctx, Name, /*IsLibCall=*/true);
      continue;
    }
    Declare(F, Name, *Sig, *Lib, MSIL::getPInvokeEntry(Name, F));
  }
  if (!Declared.empty())
    TS.emitILLine("");
}

void MSILAsmPrinter::emitDataSegment() {
  MSILTargetStreamer &TS = getTargetStreamer();
  if (!Data->Entries.empty()) {
    uint64_t InitSize = Data->InitSize;
    uint64_t TotalSize = Data->TotalSize;
    TS.emitILLine("// ---- data segment: " + Twine(TotalSize) + " bytes (" +
                  Twine(InitSize) + " initialized + " +
                  Twine(TotalSize - InitSize) + " zero), align " +
                  Twine(Data->MaxAlign.value()) + " ----");
    // The image is only read (the module initializer copies it): RVA memory
    // is never written, and it never holds '&(label)' (decision D12).
    if (!Data->Image.empty())
      TS.emitDataImage(ImageTypeName, ImageFieldName, ImageLabel, Data->Image);
    TS.emitAddressField(DataFieldName, /*Public=*/false, /*Generated=*/true);
    for (const MSILDataSegment::Entry &E : Data->Entries) {
      if (isVerbose())
        OutStreamer->AddComment("+" + Twine(E.Offset));
      TS.emitAddressField(MSIL::getILName(*E.GV, TM),
                          /*Public=*/!E.GV->hasLocalLinkage());
    }
  }
  // Referenced declarations are errors (collectExternGlobals). Their fields
  // keep the code and the module initializer that refer to them well formed;
  // nothing sets them.
  for (const GlobalVariable *GV : ExternGlobals)
    TS.emitAddressField(MSIL::getILName(*GV, TM), /*Public=*/true);
  if (!Data->Entries.empty() || !ExternGlobals.empty())
    TS.emitILLine("");
}

void MSILAsmPrinter::emitFunctionAddressFields() {
  MSILTargetStreamer &TS = getTargetStreamer();
  for (const Function *F : FuncAddrs)
    TS.emitAddressField(getFunctionAddressField(*F), /*Public=*/false,
                        /*Generated=*/true);
  if (!FuncAddrs.empty())
    TS.emitILLine("");
}

void MSILAsmPrinter::emitModuleInitializer(ArrayRef<const Function *> CtorFns) {
  bool HasData = !Data->Entries.empty();
  if (!HasData && FuncAddrs.empty() && CtorFns.empty())
    return;

  // The member references print exactly as in code (MSILMemberRefExpr).
  auto FieldRef = [&](StringRef Name) {
    std::string Str;
    raw_string_ostream OS(Str);
    MAI.printExpr(OS, *MSILMemberRefExpr::createField(Name, 0, OutContext));
    return Str;
  };
  auto MethodRef = [&](const Function &F) {
    std::string Str;
    raw_string_ostream OS(Str);
    MAI.printExpr(OS, *MSILMemberRefExpr::createMethod(MSIL::getILName(F, TM),
                                                       &getSignature(F),
                                                       OutContext));
    return Str;
  };

  SynthesizedBody Body;
  // The address field of every function whose address is used, which code
  // (FUNC_ADDR) and the relocations read: the module's only 'ldftn' of each
  // function, so every pointer to it has one value. On .NET Framework, the
  // 'ldftn' of a method that the JIT compiles before the target first ran
  // yields the target's precode, and in one compiled after it its code. An
  // undefined function's is the 'ldftn' of its P/Invoke declaration.
  auto AddFunctionAddresses = [&] {
    for (const Function *F : FuncAddrs) {
      Body.addInst("ldftn " + MethodRef(*F), 0, 1);
      Body.addInst("stsfld " + FieldRef(getFunctionAddressField(*F)), 1, 0);
    }
  };

  if (HasData) {
    LLVMContext &Ctx = Data->Entries.front().GV->getContext();
    uint64_t InitSize = Data->InitSize;
    uint64_t ZeroSize = Data->TotalSize - InitSize;
    int64_t Alignment = Data->MaxAlign.value();
    // Sizes and offsets are int32 constants: cpblk and initblk take an
    // unsigned int32 size, and 'add' an int32 offset.
    if (!isInt<32>(Data->TotalSize + Alignment - 1))
      Ctx.emitError("the data segment of " + Twine(Data->TotalSize) +
                    " bytes is larger than MSIL supports (2 GiB)");
    std::string Base = "ldsfld " + FieldRef(DataFieldName);
    // 'unaligned. N' before an access to Base + Offset that may be less
    // aligned than the instruction assumes (Natural), with N the largest of
    // 1, 2 and 4 that the address guarantees, as MSILSetMemFlags decides it
    // for code (decision D24, ECMA-335 III.2.5). Base is aligned to MaxAlign.
    // The prefix has no stack effect.
    auto AddUnaligned = [&](uint64_t Offset, uint64_t Natural) {
      uint64_t A = commonAlignment(Data->MaxAlign, Offset).value();
      if (A < Natural)
        Body.addInst("unaligned. " + Twine(std::min<uint64_t>(A, 4)), 0, 0);
    };
    unsigned PointerSize = getDataLayout().getPointerSize();

    // Base = (AllocHGlobal(TotalSize + MaxAlign - 1) + MaxAlign - 1) &
    // -MaxAlign. AllocHGlobal only guarantees malloc's alignment. The
    // constants are spelled as in recon/asmprinter.md 7.3: sizes and offsets
    // as 'ldc.i4 N', the alignment mask in its short form.
    Body.addNativeInt(Data->TotalSize + Alignment - 1, /*ShortForm=*/false);
    Body.addInst(AllocHGlobal, 1, 1);
    Body.addNativeInt(Alignment - 1, /*ShortForm=*/true);
    Body.addInst("add", 2, 1);
    Body.addNativeInt(-Alignment, /*ShortForm=*/true);
    Body.addInst("and", 2, 1);
    Body.addInst("stsfld " + FieldRef(DataFieldName), 1, 0);

    // Copy the initialized part from the RVA blob.
    if (InitSize != 0) {
      SmallString<64> Image;
      raw_svector_ostream ImageOS(Image);
      ImageOS << "ldsflda valuetype ";
      MSIL::printQuotedName(ImageOS, ImageTypeName);
      ImageOS << ' ';
      MSIL::printQuotedName(ImageOS, ImageFieldName);
      Body.addInst(Base, 0, 1);
      Body.addInst(Image, 0, 1);
      Body.addInst(getLdcI4(static_cast<int32_t>(InitSize),
                            /*ShortForm=*/false),
                   0, 1);
      Body.addInst("cpblk", 3, 0);
    }
    // AllocHGlobal does not zero its memory.
    if (ZeroSize != 0) {
      Body.addInst(Base, 0, 1);
      Body.addOffset(InitSize);
      Body.addInst("ldc.i4.0", 0, 1);
      Body.addInst(getLdcI4(static_cast<int32_t>(ZeroSize),
                            /*ShortForm=*/false),
                   0, 1);
      AddUnaligned(InitSize, PointerSize);
      Body.addInst("initblk", 3, 0);
    }

    // The address field of every global, which code reads (GLOBAL_ADDR).
    SmallPtrSet<const GlobalVariable *, 16> HasField;
    for (const MSILDataSegment::Entry &E : Data->Entries) {
      Body.addInst(Base, 0, 1);
      Body.addOffset(E.Offset);
      Body.addInst("stsfld " + FieldRef(MSIL::getILName(*E.GV, TM)), 1, 0);
      HasField.insert(E.GV);
    }
    HasField.insert_range(ExternGlobals);
    AddFunctionAddresses();

    // The relocations, now that every address is known: the address of a
    // global plus its addend, or the address field of a function.
    for (const MSILReloc &R : Data->Relocs) {
      Body.addInst(Base, 0, 1);
      Body.addOffset(R.Site);
      if (const auto *F = dyn_cast<Function>(R.Target)) {
        assert(R.Addend == 0 && "the collector rejects function + offset");
        Body.addInst("ldsfld " + FieldRef(getFunctionAddressField(*F)), 0, 1);
      } else {
        const auto *GV = dyn_cast<GlobalVariable>(R.Target);
        if (!GV || !HasField.contains(GV))
          Ctx.emitError("the initializer of a global variable refers to '" +
                        MSIL::getILName(*R.Target, TM) +
                        "', which is not program data");
        Body.addInst("ldsfld " + FieldRef(MSIL::getILName(*R.Target, TM)), 0,
                     1);
        Body.addOffset(R.Addend);
      }
      if (R.Size == 4) {
        Body.addInst("conv.i4", 1, 1);
        AddUnaligned(R.Site, 4);
        Body.addInst("stind.i4", 2, 0);
      } else {
        assert(R.Size == 8 && "relocations hold 4 or 8 bytes");
        AddUnaligned(R.Site, PointerSize);
        Body.addInst("stind.i", 2, 0);
      }
    }
  } else {
    // Without a data segment, the function addresses come first.
    AddFunctionAddresses();
  }

  // The constructors last, in priority order: the data they may touch is in
  // place.
  for (const Function *F : CtorFns) {
    unsigned NumResults = getSignature(*F).Results.size();
    Body.addInst("call " + MethodRef(*F), 0, NumResults);
    if (NumResults != 0)
      Body.addInst("pop", 1, 0);
  }
  Body.addInst("ret", 0, 0);

  MSILTargetStreamer &TS = getTargetStreamer();
  TS.emitILLine(".method private hidebysig specialname rtspecialname static "
                "void .cctor() cil managed");
  TS.emitILLine("{");
  Body.emit(TS, "");
  TS.emitMethodEnd(".cctor");
}

void MSILAsmPrinter::emitEntryPoint(const Function &Main) {
  // C's two portable shapes of main, 'int main(void)' and
  // 'int main(int, char **)' (decision D27).
  const FunctionType *FTy = Main.getFunctionType();
  LLVMContext &Ctx = Main.getContext();
  bool HasArgs = FTy->getNumParams() == 2 &&
                 FTy->getParamType(0)->isIntegerTy(32) &&
                 FTy->getParamType(1) == PointerType::getUnqual(Ctx);
  if (!FTy->getReturnType()->isIntegerTy(32) || FTy->isVarArg() ||
      (FTy->getNumParams() != 0 && !HasArgs)) {
    Ctx.diagnose(
        DiagnosticInfoUnsupported(Main, "unsupported signature for main"));
    return;
  }

  // The call spells main's signature exactly as its method header does.
  const MSIL::Signature &Sig = getSignature(Main);
  assert(Sig.Results.size() == 1 && Sig.Results.front() == MSIL::ValType::I32 &&
         Sig.Params.size() == FTy->getNumParams() &&
         "main's CIL signature does not match its IR type");
  SmallString<64> Call;
  raw_svector_ostream CallOS(Call);
  CallOS << "call ";
  MAI.printExpr(CallOS, *MSILMemberRefExpr::createMethod(
                            MSIL::getILName(Main, TM), &Sig, OutContext));

  SynthesizedBody Body;
  SmallString<64> Locals;
  if (HasArgs) {
    // argc and a NULL-terminated argv as a native main gets them:
    // Environment.GetCommandLineArgs() starts with the program (the .dll),
    // and Marshal.StringToHGlobalAnsi converts to the native narrow encoding
    // (UTF-8 on Unix, the ANSI code page on Windows). The strings and the
    // array are never freed: they live as long as the program.
    // Locals: 0 = the string[], 1 = argc, 2 = argv, 3 = i.
    MSIL::ValType PtrType = Sig.Params[1];
    Locals = "string[], int32, ";
    Locals += MSIL::getValTypeName(PtrType);
    Locals += ", int32";
    std::string PtrSize =
        ("ldc.i4." + Twine(Main.getDataLayout().getPointerSize())).str();
    // Pushes the address of argv[<IndexLocal>].
    auto AddArgvSlotAddress = [&](StringRef IndexLocal) {
      Body.addInst("ldloc.2", 0, 1);
      Body.addInst(IndexLocal, 0, 1);
      Body.addInst("conv.i", 1, 1);
      Body.addInst(PtrSize, 0, 1);
      Body.addInst("mul", 2, 1);
      Body.addInst("add", 2, 1);
    };

    Body.addInst(
        "call string[] [mscorlib]System.Environment::GetCommandLineArgs()", 0,
        1);
    Body.addInst("stloc.0", 1, 0);
    // argc = args.Length
    Body.addInst("ldloc.0", 0, 1);
    Body.addInst("ldlen", 1, 1);
    Body.addInst("conv.i4", 1, 1);
    Body.addInst("stloc.1", 1, 0);
    // argv = AllocHGlobal((argc + 1) * sizeof(char *))
    Body.addInst("ldloc.1", 0, 1);
    Body.addInst("ldc.i4.1", 0, 1);
    Body.addInst("add", 2, 1);
    Body.addInst("conv.i", 1, 1);
    Body.addInst(PtrSize, 0, 1);
    Body.addInst("mul", 2, 1);
    Body.addInst("call native int [mscorlib]System.Runtime.InteropServices."
                 "Marshal::AllocHGlobal(native int)",
                 1, 1);
    Body.addInst("stloc.2", 1, 0);
    // for (i = 0; i < argc; ++i) argv[i] = StringToHGlobalAnsi(args[i])
    Body.addInst("ldc.i4.0", 0, 1);
    Body.addInst("stloc.3", 1, 0);
    Body.addBranch("br @argv_cond", 0);
    Body.addLabel("@argv_loop");
    AddArgvSlotAddress("ldloc.3");
    Body.addInst("ldloc.0", 0, 1);
    Body.addInst("ldloc.3", 0, 1);
    Body.addInst("ldelem.ref", 2, 1);
    Body.addInst("call native int [mscorlib]System.Runtime.InteropServices."
                 "Marshal::StringToHGlobalAnsi(string)",
                 1, 1);
    Body.addInst("stind.i", 2, 0);
    Body.addInst("ldloc.3", 0, 1);
    Body.addInst("ldc.i4.1", 0, 1);
    Body.addInst("add", 2, 1);
    Body.addInst("stloc.3", 1, 0);
    Body.addLabel("@argv_cond");
    Body.addInst("ldloc.3", 0, 1);
    Body.addInst("ldloc.1", 0, 1);
    Body.addBranch("blt @argv_loop", 2);
    // argv[argc] = NULL
    AddArgvSlotAddress("ldloc.1");
    Body.addInst("ldc.i4.0", 0, 1);
    Body.addInst("conv.i", 1, 1);
    Body.addInst("stind.i", 2, 0);
    // return main(argc, argv)
    Body.addInst("ldloc.1", 0, 1);
    Body.addInst("ldloc.2", 0, 1);
  }
  Body.addInst(Call, Sig.Params.size(), 1);
  Body.addInst("ret", 1, 0);

  MSILTargetStreamer &TS = getTargetStreamer();
  SmallString<64> Header;
  raw_svector_ostream HeaderOS(Header);
  HeaderOS << ".method private hidebysig static int32 ";
  MSIL::printQuotedName(HeaderOS, EntryPointName);
  HeaderOS << "(string[] args) cil managed";
  TS.emitILLine(Header);
  TS.emitILLine("{");
  TS.emitEntryPoint();
  Body.emit(TS, Locals);
  TS.emitMethodEnd(EntryPointName);
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
  // MSIL has no out-of-line jump tables (BR_TABLE_I32 prints 'switch' with its
  // targets inline), so this hook closes the method: the brace must follow
  // the body (plan 5.1, decision D29). AsmPrinter::emitFunctionBody calls it
  // for every function, after the end-of-function label, which the base
  // class prints after emitFunctionBodyEnd whenever the function has begin
  // and end labels (always with -g), and before the post-function sections,
  // the '-- End function' comment and the blank line. Closing here keeps that
  // label inside the method and puts the brace on the very next line.
  emitMethodEnd();
}

void MSILAsmPrinter::emitGlobalVariable(const GlobalVariable *GV) {
  StringRef Name = GV->getName();
  // The module initializer calls the constructors (decision D12).
  if (Name == "llvm.global_ctors") {
    if (GV->hasInitializer())
      preprocessXXStructorList(GV->getDataLayout(), GV->getInitializer(),
                               Ctors);
    return;
  }
  // MSILPrepareIR reports and erases a non-empty llvm.global_dtors; this is
  // the backstop for a pipeline without it.
  if (Name == "llvm.global_dtors") {
    if (GV->hasInitializer() && !GV->getInitializer()->isNullValue())
      GV->getContext().emitError("global destructors (llvm.global_dtors) are "
                                 "not supported for MSIL yet");
    return;
  }
  // llvm.used, llvm.compiler.used and data in the llvm.metadata section are
  // not program data. Any other llvm.* name (a C asm label can spell one) is
  // an ordinary global.
  if (MSIL::isSpecialLLVMGlobal(*GV))
    return;

  // A variable that another module defines gets no data; an
  // available_externally definition counts as such a declaration, as for
  // functions. MSILMCInstLower and emitEndOfAsmFile note the ones that the
  // code and the data refer to, and collectExternGlobals reports those.
  if (GV->isDeclarationForLinker())
    return;

  // The address field is a new place where the name is printed (D11).
  StringRef ILName = MSIL::getILName(*GV, TM);
  if (ILName.size() > MSIL::MaxILNameBytes)
    GV->getContext().emitError(
        "global variable " + MSIL::getIRName(*GV) + ": identifier of " +
        Twine(ILName.size()) + " bytes is longer than the " +
        Twine(MSIL::MaxILNameBytes) + " bytes ilasm accepts");

  // MSILPrepareIR reports a thread-local variable and makes it an ordinary
  // one; this is the backstop for a pipeline without it.
  if (GV->isThreadLocal())
    GV->getContext().emitError("thread-local variable " + MSIL::getIRName(*GV) +
                               " is not supported for MSIL yet");

  // Every definition, whatever its linkage, lives in the data segment
  // (decisions D12 and D26: comdats are ignored).
  Data->addGlobal(*GV, *this, getSymToGV(*GV->getParent()));
}

void MSILAsmPrinter::emitGlobalAlias(const Module &M, const GlobalAlias &GA) {
  // MSILPrepareIR replaces aliases by poison and erases them (decision D26),
  // so only a pipeline that skips it gets here.
  GA.getContext().emitError("alias " + MSIL::getIRName(GA) +
                            " is not supported for MSIL");
}

void MSILAsmPrinter::emitGlobalIFunc(Module &M, const GlobalIFunc &GI) {
  // As for aliases (decision D26).
  GI.getContext().emitError("ifunc " + MSIL::getIRName(GI) +
                            " is not supported for MSIL");
}

void MSILAsmPrinter::emitFunctionEntryLabel() {
  // The method header takes the place of the entry label: CIL code never
  // refers to a method by address, only by its name and signature.
  const Function &F = MF->getFunction();
  const MSIL::Signature &Sig = getSignature(F);
  assert(getFunctionInfoSignature(
             *MFI, F.getDataLayout().getPointerSizeInBits()) == Sig &&
         "the method header and the lowered arguments and results disagree");
  StringRef Name = MSIL::getILName(F, TM);
  if (isReservedMethodName(Name))
    reportReservedMethodName(F, Name);
  // CIL methods may share a name when their signatures differ, but ilasm
  // rejects a second method with the same name and signature ('Duplicate
  // method declaration'), as for @f and @"\01f". Functions are printed in
  // module order, so the first of two is the one named in the error.
  if (auto [It, Inserted] = Methods.try_emplace({Name, Sig}, &F); !Inserted)
    F.getContext().emitError(
        "function " + MSIL::getIRName(F) + " has the same IL name '" + Name +
        "' and signature as " + MSIL::getIRName(*It->second));
  getTargetStreamer().emitMethodBegin(
      {Name, &Sig, /*Public=*/!F.hasLocalLinkage()});
  InMethod = true;
}

void MSILAsmPrinter::emitFunctionBodyStart() {
  const Function &F = MF->getFunction();
  if (MFI->getParams().size() > MaxArgsOrLocals ||
      MFI->getLocals().size() > MaxArgsOrLocals)
    F.getContext().diagnose(DiagnosticInfoUnsupported(
        F, "a CIL method has at most " + Twine(MaxArgsOrLocals) +
               " arguments and " + Twine(MaxArgsOrLocals) + " locals"));

  // .maxstack comes from MSILStackCheck, the last machine pass.
  MSILTargetStreamer &TS = getTargetStreamer();
  TS.emitMaxStack(MFI->getMaxStack());
  unsigned PointerSizeInBits = F.getDataLayout().getPointerSizeInBits();
  SmallVector<MSIL::ValType, 16> Locals;
  for (MVT VT : MFI->getLocals())
    Locals.push_back(MSIL::toValType(VT, PointerSizeInBits));
  TS.emitLocals(Locals);
}

void MSILAsmPrinter::emitInstruction(const MachineInstr *MI) {
  // The ARGUMENT_* pseudos only give the incoming arguments virtual
  // registers; MSILExplicitLocals has turned their uses into ldarg.
  if (MSIL::isArgument(MI->getOpcode()))
    return;

  assert(matchesCalleeSignature(*MI, *this) &&
         "a direct call does not match its callee's signature");

  MSILMCInstLower MCInstLowering(OutContext, *this);
  MCInst TmpInst;
  MCInstLowering.lower(MI, TmpInst);
  EmitToStreamer(*OutStreamer, TmpInst);
}

char MSILAsmPrinter::ID = 0;

INITIALIZE_PASS(MSILAsmPrinter, "msil-asm-printer", "MSIL Assembly Printer",
                false, false)

// Force static initialization.
extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMSILAsmPrinter() {
  RegisterAsmPrinter<MSILAsmPrinter> X(getTheMSILTarget());
}
