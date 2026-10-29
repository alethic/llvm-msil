//===-- MSILUtilities.cpp - MSIL utility functions ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyUtilities.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements several utility functions for MSIL.
///
//===----------------------------------------------------------------------===//

#include "MSILUtilities.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSILMachineFunctionInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
using namespace llvm;

std::string MSIL::getIRName(const GlobalValue &GV) {
  std::string Name;
  raw_string_ostream OS(Name);
  GV.printAsOperand(OS, /*PrintType=*/false);
  return Name;
}

bool MSIL::isSpecialLLVMGlobal(const GlobalVariable &GV) {
  // Only these globals and the llvm.metadata section are reserved; any other
  // llvm.* name (a C asm label can spell one) is ordinary program data.
  if (GV.hasName()) {
    StringRef Name = GV.getName();
    if (Name == "llvm.used" || Name == "llvm.compiler.used" ||
        Name == "llvm.global_ctors" || Name == "llvm.global_dtors")
      return true;
  }
  return GV.getSection() == "llvm.metadata";
}

StringRef MSIL::getILName(const GlobalValue &GV, const TargetMachine &TM) {
  if (GV.hasName())
    return GlobalValue::dropLLVMManglingEscape(GV.getName());
  // The symbol of an unnamed value lives as long as the MCContext.
  return TM.getSymbol(&GV)->getName();
}

const Function *MSIL::getFunctionByILName(const Module &M, StringRef Name) {
  if (const Function *F = M.getFunction(Name))
    return F;
  // getILName drops the '\1' escape, so "\1Name" has the IL name Name too.
  return M.getFunction((Twine("\1") + Name).str());
}

bool MSIL::resetExceptionModel(Module &M) {
  SmallVector<Module::ModuleFlagEntry, 8> Flags;
  M.getModuleFlagsMetadata(Flags);
  StringRef None = getExceptionModelName(ExceptionHandling::None);
  for (const Module::ModuleFlagEntry &Flag : Flags) {
    if (Flag.Key->getString() != "exception-model")
      continue;
    if (auto *Model = dyn_cast_or_null<MDString>(Flag.Val);
        Model && Model->getString() == None)
      return false;
    M.setModuleFlag(Flag.Behavior, "exception-model",
                    MDString::get(M.getContext(), None));
    return true;
  }
  return false;
}

/// Test whether MI is a child of some other node in an expression tree.
bool MSIL::isChild(const MachineInstr &MI, const MSILFunctionInfo &MFI) {
  if (MI.getNumOperands() == 0)
    return false;
  const MachineOperand &MO = MI.getOperand(0);
  if (!MO.isReg() || MO.isImplicit() || !MO.isDef())
    return false;
  Register Reg = MO.getReg();
  return Reg.isVirtual() && MFI.isVRegStackified(Reg);
}

bool MSIL::mayThrow(const MachineInstr &MI) {
  // Phase 1 has no exception handling: C code throws only through calls (a
  // P/Invoke target or a managed callee may raise a .NET exception), and the
  // 'throw' of TRAP ends the program.
  return MI.isCall();
}

const MachineOperand &MSIL::getCalleeOp(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case MSIL::CALL:
    return MI.getOperand(MI.getNumExplicitDefs());
  case MSIL::CALL_INDIRECT:
    return MI.getOperand(MI.getNumExplicitOperands() - 1);
  default:
    llvm_unreachable("Not a call instruction");
  }
}

unsigned MSIL::getCopyOpcodeForRegClass(const TargetRegisterClass *RC) {
  assert(RC != nullptr);
  switch (RC->getID()) {
  case MSIL::I32RegClassID:
    return MSIL::COPY_I32;
  case MSIL::I64RegClassID:
    return MSIL::COPY_I64;
  case MSIL::F32RegClassID:
    return MSIL::COPY_F32;
  case MSIL::F64RegClassID:
    return MSIL::COPY_F64;
  default:
    llvm_unreachable("Unexpected register class");
  }
}

bool MSIL::moveArgumentsToTop(MachineFunction &MF) {
  bool Changed = false;
  MachineBasicBlock &EntryMBB = MF.front();
  MachineBasicBlock::iterator InsertPt = EntryMBB.end();

  // Look for the first NonArg instruction.
  for (MachineInstr &MI : EntryMBB) {
    if (!MSIL::isArgument(MI.getOpcode())) {
      InsertPt = MI;
      break;
    }
  }

  // Now move any argument instructions later in the block
  // to before our first NonArg instruction.
  for (MachineInstr &MI :
       llvm::make_early_inc_range(llvm::make_range(InsertPt, EntryMBB.end()))) {
    if (MSIL::isArgument(MI.getOpcode())) {
      EntryMBB.insert(InsertPt, MI.removeFromParent());
      Changed = true;
    }
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// The standalone import table (decision D14, frozen)
//===----------------------------------------------------------------------===//

namespace {

/// The C runtime of a Phase-1 triple, whose libraries the imports call.
enum class CRuntime : uint8_t { Glibc, Musl, UCRT, MSVCRT };

/// The Windows C runtime DLLs that do not export a math function.
enum : uint8_t { NotInUCRT = 1 << 0, NotInMSVCRT = 1 << 1 };

/// A math function of the import table, in its double and float32 variants.
struct MathImport {
  /// The double function; the float32 one adds an 'f'.
  StringLiteral Name;
  /// The DLLs that do not export the double function.
  uint8_t Missing;
  /// The DLLs that do not export the float32 function.
  uint8_t MissingF;
};

} // end anonymous namespace

/// The C math functions of decision D14: those that clang calls for the math
/// builtins and that the legalizer calls for the floating-point operations
/// without a CIL opcode. The table is frozen until the end of Phase 2 (plan
/// 5.10.13): no entry is added or removed.
///
/// ucrtbase.dll and msvcrt.dll do not export all of them: the UCRT headers
/// implement frexpf, ldexpf and hypotf inline (on frexp, ldexp and _hypotf),
/// and MinGW links the C99 functions and hypot statically from libmingwex
/// instead of msvcrt.dll. A P/Invoke of a missing entry point fails only when
/// it is first called (EntryPointNotFoundException), so on those triples such
/// a function is not an import, and calling it is an error, as the unresolved
/// symbol is an error when a native program is linked. Checked against the
/// exports of the DLLs of Windows 11 build 26200 (llvm-readobj
/// --coff-exports).
static constexpr MathImport MathImports[] = {
    {"sqrt", 0, 0},
    {"cbrt", NotInMSVCRT, NotInMSVCRT},
    {"floor", 0, 0},
    {"ceil", 0, 0},
    {"trunc", NotInMSVCRT, NotInMSVCRT},
    {"round", NotInMSVCRT, NotInMSVCRT},
    {"rint", NotInMSVCRT, NotInMSVCRT},
    {"nearbyint", NotInMSVCRT, NotInMSVCRT},
    {"fmod", 0, 0},
    {"remainder", NotInMSVCRT, NotInMSVCRT},
    {"fma", NotInMSVCRT, NotInMSVCRT},
    {"fmin", NotInMSVCRT, NotInMSVCRT},
    {"fmax", NotInMSVCRT, NotInMSVCRT},
    {"fdim", NotInMSVCRT, NotInMSVCRT},
    {"sin", 0, 0},
    {"cos", 0, 0},
    {"tan", 0, 0},
    {"asin", 0, 0},
    {"acos", 0, 0},
    {"atan", 0, 0},
    {"atan2", 0, 0},
    {"sinh", 0, 0},
    {"cosh", 0, 0},
    {"tanh", 0, 0},
    {"exp", 0, 0},
    {"exp2", NotInMSVCRT, NotInMSVCRT},
    {"expm1", NotInMSVCRT, NotInMSVCRT},
    {"log", 0, 0},
    {"log2", NotInMSVCRT, NotInMSVCRT},
    {"log10", 0, 0},
    {"log1p", NotInMSVCRT, NotInMSVCRT},
    {"pow", 0, 0},
    {"ldexp", 0, NotInUCRT | NotInMSVCRT},
    {"frexp", 0, NotInUCRT | NotInMSVCRT},
    {"modf", 0, 0},
    {"hypot", NotInMSVCRT, NotInUCRT | NotInMSVCRT},
};

/// Returns the C runtime of \p TT, or std::nullopt for a triple whose C
/// library Phase 1 does not know (decision D14 names linux-gnu, linux-musl,
/// windows-msvc and windows-gnu; a Linux triple without an environment uses
/// glibc, a Windows triple without one the MSVC runtime).
static std::optional<CRuntime> getCRuntime(const Triple &TT) {
  if (TT.isOSLinux()) {
    if (TT.isMusl())
      return CRuntime::Musl;
    if (TT.isAndroid())
      return std::nullopt;
    return CRuntime::Glibc;
  }
  if (TT.isWindowsMSVCEnvironment())
    return CRuntime::UCRT;
  if (TT.isWindowsGNUEnvironment())
    return CRuntime::MSVCRT;
  return std::nullopt;
}

/// Returns the bits of the DLLs that do not export \p Name if it is a math
/// function of the import table, or std::nullopt if it is none.
static std::optional<uint8_t> findMathImport(StringRef Name) {
  for (const MathImport &M : MathImports) {
    if (Name == M.Name)
      return M.Missing;
    if (Name.size() == M.Name.size() + 1 && Name.starts_with(M.Name) &&
        Name.back() == 'f')
      return M.MissingF;
  }
  return std::nullopt;
}

std::optional<StringRef>
MSIL::getPInvokeModule(const Triple &TT, StringRef Name, const Function *F) {
  // An explicit import names its library itself.
  if (F) {
    StringRef Module =
        F->getFnAttribute("msil-import-module").getValueAsString();
    if (!Module.empty())
      return Module;
  }

  std::optional<CRuntime> RT = getCRuntime(TT);
  if (!RT)
    return std::nullopt;

  if (Name == "puts" || Name == "putchar") {
    switch (*RT) {
    case CRuntime::Glibc:
    case CRuntime::Musl:
      return StringRef("libc");
    case CRuntime::UCRT:
      return StringRef("ucrtbase");
    case CRuntime::MSVCRT:
      return StringRef("msvcrt");
    }
    llvm_unreachable("Unknown C runtime");
  }

  std::optional<uint8_t> Missing = findMathImport(Name);
  if (!Missing)
    return std::nullopt;
  switch (*RT) {
  case CRuntime::Glibc:
    // CoreCLR resolves "libc" to the C library itself, but no other short
    // name, and glibc's libm is a library of its own.
    return StringRef("libm.so.6");
  case CRuntime::Musl:
    // musl has a single library; its libm is an empty stub.
    return StringRef("libc");
  case CRuntime::UCRT:
    if (*Missing & NotInUCRT)
      return std::nullopt;
    return StringRef("ucrtbase");
  case CRuntime::MSVCRT:
    if (*Missing & NotInMSVCRT)
      return std::nullopt;
    return StringRef("msvcrt");
  }
  llvm_unreachable("Unknown C runtime");
}

StringRef MSIL::getPInvokeEntry(StringRef Name, const Function *F) {
  if (F) {
    StringRef Entry = F->getFnAttribute("msil-import-name").getValueAsString();
    if (!Entry.empty())
      return Entry;
  }
  return Name;
}

bool MSIL::hasImportModule(const Function *F) {
  return F &&
         !F->getFnAttribute("msil-import-module").getValueAsString().empty();
}

//===----------------------------------------------------------------------===//
// The imports of msil-ld's linked text (plan 5.10.13)
//===----------------------------------------------------------------------===//

bool MSIL::isLinkedModule(const Module &M) {
  const auto *Flag =
      mdconst::dyn_extract_or_null<ConstantInt>(M.getModuleFlag("msil-linked"));
  return Flag && Flag->isOne();
}

std::optional<MSIL::NativeImport>
MSIL::getLinkedImport(const Module &M, StringRef Name, const Function *F) {
  // An import that the declaration names: an explicit import_module, used
  // unchecked (plan 5.10.7), or the module and entry with which msil-ld
  // annotated a natively bound declaration.
  if (hasImportModule(F))
    return NativeImport{
        F->getFnAttribute("msil-import-module").getValueAsString(),
        getPInvokeEntry(Name, F)};

  // A library function that the merged module does not declare, which msil-ld
  // bound natively: !{!"<symbol>", !"<module>", !"<entry>", i32 <kind>}. Kind
  // 1 (data) is reserved until M20, and a malformed operand binds nothing.
  const NamedMDNode *Imports = M.getNamedMetadata("msil.imports");
  if (!Imports)
    return std::nullopt;
  for (const MDNode *Op : Imports->operands()) {
    if (Op->getNumOperands() != 4)
      continue;
    const auto *Symbol = dyn_cast_or_null<MDString>(Op->getOperand(0));
    if (!Symbol || Symbol->getString() != Name)
      continue;
    const auto *Kind =
        mdconst::dyn_extract_or_null<ConstantInt>(Op->getOperand(3));
    const auto *Module = dyn_cast_or_null<MDString>(Op->getOperand(1));
    const auto *Entry = dyn_cast_or_null<MDString>(Op->getOperand(2));
    if (!Kind || !Kind->isZero() || !Module || Module->getString().empty() ||
        !Entry)
      continue;
    return NativeImport{Module->getString(),
                        Entry->getString().empty() ? Name : Entry->getString()};
  }
  return std::nullopt;
}
