//===-- MSILPortable.cpp - The portable triples' checks and texts ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the checks of the portable triples
/// (msil64/msil32-unknown-dotnet, plan 5.18) and the texts of the Dotnet
/// link-rule family (plan 5.18.5) that whole-program text and objects
/// report.
///
//===----------------------------------------------------------------------===//

#include "MSILPortable.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

/// The one portable triple that the backend compiles (plan 5.18.11), as the
/// portable texts name it.
static constexpr StringLiteral PortableTriple = "msil64-unknown-dotnet";

/// The import attributes of C's import_module and import_name (plan 5.10.7).
static constexpr StringLiteral ImportModuleAttr = "msil-import-module";
static constexpr StringLiteral ImportNameAttr = "msil-import-name";

/// T-P2 for the declaration \p GV, imported from \p Module, named by its C
/// name (a declaration with an import attribute always has one).
static void reportImport(const GlobalValue &GV, StringRef Module) {
  GV.getContext().emitError(
      "'" + GlobalValue::dropLLVMManglingEscape(GV.getName()) +
      "' is declared with import module '" + Module + "', which " +
      PortableTriple +
      " does not support: the portable target binds no native code");
}

/// Erases every call and invoke of \p F, as MSILPrepareIR erases the calls
/// of a function whose body it deleted: the uses of a call become poison,
/// and an invoke branches to its normal destination.
static void eraseCallsOf(Function &F) {
  SmallVector<CallBase *, 4> Calls;
  for (Use &U : F.uses())
    if (auto *CB = dyn_cast<CallBase>(U.getUser());
        CB && isa<CallInst, InvokeInst>(CB) && CB->isCallee(&U))
      Calls.push_back(CB);
  for (CallBase *CB : Calls) {
    if (auto *II = dyn_cast<InvokeInst>(CB)) {
      auto *BI = UncondBrInst::Create(II->getNormalDest(), II->getIterator());
      BI->setDebugLoc(II->getDebugLoc());
      II->getUnwindDest()->removePredecessor(II->getParent());
    }
    if (!CB->use_empty())
      CB->replaceAllUsesWith(PoisonValue::get(CB->getType()));
    CB->eraseFromParent();
  }
}

bool MSIL::checkPortableModule(Module &M) {
  SmallVector<Function *, 4> Functions;
  bool Changed = false;
  for (Function &F : M) {
    StringRef Module = F.getFnAttribute(ImportModuleAttr).getValueAsString();
    if (!F.isDeclaration() || Module.empty())
      continue;
    reportImport(F, Module);
    F.removeFnAttr(ImportModuleAttr);
    F.removeFnAttr(ImportNameAttr);
    Functions.push_back(&F);
    Changed = true;
  }
  for (GlobalVariable &GV : M.globals()) {
    StringRef Module = GV.getAttribute(ImportModuleAttr).getValueAsString();
    if (!GV.isDeclaration() || Module.empty())
      continue;
    reportImport(GV, Module);
    AttributeSet Attrs = GV.getAttributes()
                             .removeAttribute(GV.getContext(), ImportModuleAttr)
                             .removeAttribute(GV.getContext(), ImportNameAttr);
    GV.setAttributes(Attrs);
    Changed = true;
  }
  if (Functions.empty())
    return Changed;

  // The reported functions are never called and have no address: without
  // their imports, the AsmPrinter would report each one again as an
  // undefined function (T-P3), and an object would list it as an undefined
  // symbol. They stay as unused declarations, which metadata may still name.
  SmallPtrSet<const Constant *, 4> Reported(Functions.begin(), Functions.end());
  removeFromUsedLists(M, [&](Constant *C) { return Reported.contains(C); });
  for (Function *F : Functions) {
    eraseCallsOf(*F);
    if (!F->use_empty())
      F->replaceNonMetadataUsesWith(PoisonValue::get(F->getType()));
  }
  return true;
}

std::string MSIL::getTripleText(const Triple &TT) {
  if (!TT.isOSDotNet() || TT.getVendor() == Triple::UnknownVendor)
    return Triple::normalize(TT.str());
  Triple Reduced = TT;
  Reduced.setVendor(Triple::UnknownVendor);
  return Triple::normalize(Reduced.str());
}

std::string MSIL::getUnsupportedPortableTargetText(const Triple &TT) {
  return ("target '" + getTripleText(TT) + "' is not supported yet: only " +
          PortableTriple + " is implemented")
      .str();
}

std::string MSIL::getStandaloneUndefinedText(StringRef Name) {
  return ("function '" + Name + "' is not defined in this module: standalone " +
          "text for " + PortableTriple + " imports nothing (link with msil-ld)")
      .str();
}

std::string MSIL::getConstructorArgsText(StringRef Name,
                                         cli::SectionRules Rules) {
  switch (Rules) {
  case cli::SectionRules::ELF:
  case cli::SectionRules::COFF:
    return ("global constructor '" + Name +
            "' takes arguments, which MSIL does not pass")
        .str();
  case cli::SectionRules::Dotnet:
    return ("constructor '" + Name + "' takes arguments, which " +
            PortableTriple + " does not pass")
        .str();
  }
  llvm_unreachable("unknown section rules");
}

std::string MSIL::getNullConstructorSlotText(StringRef Section,
                                             StringRef File) {
  return ("null constructor slot in '" + Section + "' of '" + File +
          "': " + PortableTriple + " calls every slot")
      .str();
}

std::optional<std::string>
MSIL::getUnsupportedSectionText(StringRef Section, cli::SectionRules Rules) {
  switch (Rules) {
  case cli::SectionRules::ELF:
  case cli::SectionRules::COFF:
    return std::nullopt;
  case cli::SectionRules::Dotnet:
    if (std::optional<StringRef> Reason =
            cli::getUnsupportedSectionReason(Section, Rules))
      return ("section '" + Section + "' is not supported for " +
              PortableTriple + ": " + *Reason)
          .str();
    return std::nullopt;
  }
  llvm_unreachable("unknown section rules");
}
