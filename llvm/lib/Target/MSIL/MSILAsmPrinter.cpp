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
/// fields of globals, the module initializer and the entry point) comes from
/// emitEndOfAsmFile. ILAsm resolves forward references to methods and fields,
/// so code refers to globals symbolically and the trailer is laid out after
/// the last function. Everything else the base AsmPrinter would print
/// (sections, linkage and visibility directives, alignment, constant pools,
/// jump tables) is suppressed here or by MSILMCAsmInfo.
///
//===----------------------------------------------------------------------===//

#include "MSILAsmPrinter.h"
#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MCTargetDesc/MSILTargetStreamer.h"
#include "MSIL.h"
#include "MSILMCInstLower.h"
#include "MSILMachineFunctionInfo.h"
#include "MSILSubtarget.h"
#include "MSILUtilities.h"
#include "TargetInfo/MSILTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
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
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
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

/// The signature that the call \p MI actually uses: its explicit register
/// defs are the values it pushes and its explicit register uses the values it
/// pops, in CIL types (after MSILExplicitLocals every register operand is a
/// value on the evaluation stack).
[[maybe_unused]] static MSIL::Signature
getCallSiteSignature(const MachineInstr &MI) {
  const MachineFunction &MF = *MI.getMF();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  unsigned PointerSizeInBits = MF.getDataLayout().getPointerSizeInBits();
  MSIL::Signature Sig;
  for (const MachineOperand &MO : MI.explicit_operands()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    const TargetRegisterClass *RC = Reg.isVirtual()
                                        ? MRI.getRegClass(Reg)
                                        : TRI.getMinimalPhysRegClass(Reg);
    MSIL::ValType T =
        MSIL::toValType(*TRI.legalclasstypes_begin(*RC), PointerSizeInBits);
    if (MO.isDef())
      Sig.Results.push_back(T);
    else
      Sig.Params.push_back(T);
  }
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
  return !F || getCallSiteSignature(MI) == AP.getSignature(*F);
}

/// ldarg, starg, ldloc and stloc take 16-bit indices, and ilasm silently
/// truncates larger ones. Index 0xFFFF is avoided.
static constexpr size_t MaxArgsOrLocals = 0xFFFF;

/// The name of the generated entry point (decision D27). The '<msil>' prefix
/// cannot be spelled in C.
static constexpr StringRef EntryPointName = "<msil>Main";

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
  // Temporary, until M8 declares undefined functions as P/Invoke methods
  // (decision D14): Phase 1 has no linker, so a call to a function that this
  // module does not define cannot be resolved. The call itself was printed
  // already; the error makes llc and clang exit with 1. One error per
  // function, in the order of first use.
  for (const Function *F : ExternFuncs.keys())
    M.getContext().emitError("call to external function '" +
                             MSIL::getILName(*F, TM) +
                             "' needs P/Invoke support (M8)");

  // The P/Invoke declarations, the data image, the address fields and the
  // module initializer come with globals and P/Invoke in M8, before the entry
  // point. An available_externally main has no method (no MachineFunction is
  // emitted for it), so it gets no entry point either.
  if (const Function *Main = M.getFunction("main");
      Main && !Main->isDeclarationForLinker())
    emitEntryPoint(*Main);
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
  // The method header takes the place of the entry label: CIL code never
  // refers to a method by address, only by its name and signature.
  const Function &F = MF->getFunction();
  const MSIL::Signature &Sig = getSignature(F);
  assert(getFunctionInfoSignature(
             *MFI, F.getDataLayout().getPointerSizeInBits()) == Sig &&
         "the method header and the lowered arguments and results disagree");
  getTargetStreamer().emitMethodBegin(
      {MSIL::getILName(F, TM), &Sig, /*Public=*/!F.hasLocalLinkage()});
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
