//===-- MSILManagedCalls.cpp - Managed-call stubs -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the managed-call stubs (ImplementationPlan.md 5.20.5
/// ML2): their creation at slot P0 and the IL body that the AsmPrinter writes
/// for each of them. The IR rules that slot P0 shares with msil-ld's
/// --lto-emit-asm checks are in llvm/CLI/ManagedCallIR.h.
///
/// A stub body converts the C arguments in order, accesses the member once,
/// converts the result and returns it:
///
///   [enum checks]               (whole-program text only)
///   [.try {]
///     length loops of the UTF-8 and UTF-32 arguments (empty stack)
///     'this' and the arguments, each converted (MS5)
///     call | [constrained. V] callvirt | newobj | newarr | ldsfld | ldfld
///     the result, converted
///     ret                       (with msil_catch: stloc R; leave DONE)
///   [} filter { isinst T_k ... endfilter }
///    { pop; *err = C; R = 0; leave DONE }
///   DONE: ldloc R; ret]
///
/// Object handles are Normal GCHandles as native int (5.20.9): an argument is
/// dereferenced through FromIntPtr, box, unbox and get_Target, a result
/// becomes a new handle through Alloc and ToIntPtr, and 0 is null.
///
/// Whole-program text, which ilasm assembles without msil-ld's checks of an
/// object (5.20.7), checks those at run time (5.20.6 MW2 as amended): the
/// castclass or unbox of a 'this' handle and a constructor's castclass or box
/// name TYPE as a TypeSpec of its declared kind, which the runtime's type
/// loader checks when it compiles the stub (T-M16), and the enum checks test
/// that the value type of each 'enum' parameter and result is an enum of the
/// C integer's width (typeof(E).IsEnum and sizeof E), else throw (T-M7).
///
//===----------------------------------------------------------------------===//

#include "MSILManagedCalls.h"
#include "MCTargetDesc/MSILMCExpr.h"
#include "MCTargetDesc/MSILTargetStreamer.h"
#include "MSILAsmPrinter.h"
#include "MSILMCInstLower.h"
#include "MSILUtilities.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/CLI.h"
#include "llvm/CLI/BCLMembers.h"
#include "llvm/CLI/ManagedCallDiagnostic.h"
#include "llvm/CLI/ManagedCallIR.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <algorithm>
#include <cassert>
#include <initializer_list>

using namespace llvm;
using cli::ManagedConv;
using TK = cli::ManagedType::TypeKind;

//===----------------------------------------------------------------------===//
// Parsing and checking a declaration's attributes (plan 5.20.4 MR1)
//===----------------------------------------------------------------------===//

/// An error of parseManagedCall whose message is \p Reason.
static Error callError(const Twine &Reason) {
  return createStringError(inconvertibleErrorCode(), Reason);
}

/// The text of the conversion \p C and of the types it is checked against,
/// for the errors below.
static std::string describePair(ManagedConv C, const cli::ManagedType &CIL,
                                Type *IRTy) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "conversion '" << cli::getManagedConvName(C) << "' does not fit the "
     << ".NET type '";
  CIL.print(OS);
  OS << "' and the IR type '" << *IRTy << "'";
  return S;
}

Expected<MSIL::ManagedCall> MSIL::parseManagedCall(const Function &F) {
  MSIL::ManagedCall Call;
  Call.Name = cli::getManagedCallSymbol(F.getName()).str();
  for (StringRef Name : {cli::ManagedTypeAttr, cli::ManagedMemberAttr,
                         cli::ManagedSigAttr, cli::ManagedConvAttr})
    if (!F.hasFnAttribute(Name))
      return callError("it has no \"" + Name + "\" attribute");
  auto Attr = [&](StringRef Name) {
    return F.getFnAttribute(Name).getValueAsString();
  };

  Expected<cli::ManagedType> DeclType =
      cli::parseManagedTypeName(Attr(cli::ManagedTypeAttr));
  if (!DeclType)
    return callError("type: " + toString(DeclType.takeError()));
  Call.Type = std::move(*DeclType);
  Call.Member = Attr(cli::ManagedMemberAttr).str();
  if (Error E = cli::checkManagedMemberName(Call.Member))
    return callError("member: " + toString(std::move(E)));
  Expected<cli::ManagedSignature> Sig =
      cli::parseManagedSignature(Attr(cli::ManagedSigAttr));
  if (!Sig)
    return callError("signature: " + toString(Sig.takeError()));
  Call.Sig = std::move(*Sig);
  if (std::optional<cli::ManagedShapeProblem> P =
          cli::checkManagedCallShape(Call.Type, Call.Member, Call.Sig))
    return callError(cli::getManagedCallArgName(P->Arg) + ": " + P->Reason);
  // A member of an array type is its '.ctor' (newarr); every other member
  // belongs to a class or a value type, which its token names.
  assert((Call.isArrayConstructor() || Call.Type.isNamed()) &&
         "parseManagedTypeName gives a class, a value type or an array");
  Call.Scope = F.hasFnAttribute(cli::ManagedAsmAttr)
                   ? Attr(cli::ManagedAsmAttr).str()
                   : cli::ManagedDefaultAssembly.str();
  if (Call.Scope.empty())
    return callError("the assembly is empty");
  Expected<cli::ManagedConvList> Conv =
      cli::parseManagedConvList(Attr(cli::ManagedConvAttr));
  if (!Conv)
    return callError("conversions: " + toString(Conv.takeError()));
  Call.Conv = std::move(*Conv);
  if (F.hasFnAttribute(cli::ManagedCatchAttr)) {
    Expected<SmallVector<cli::ManagedCatch, 4>> Catches =
        cli::parseManagedCatchList(Attr(cli::ManagedCatchAttr));
    if (!Catches)
      return callError("catch list: " + toString(Catches.takeError()));
    Call.Catches = std::move(*Catches);
    for (const cli::ManagedCatch &C : Call.Catches) {
      Expected<cli::ManagedType> T = cli::parseManagedCatchType(C.Type);
      if (!T)
        return callError("catch list: " + toString(T.takeError()));
      Call.CatchTypes.push_back(std::move(*T));
    }
  }

  // The C parameters: the 'this' handle, the member's parameters, the error
  // code pointer; one conversion code each.
  const FunctionType *FTy = F.getFunctionType();
  const DataLayout &DL = F.getDataLayout();
  if (FTy->isVarArg())
    return callError("it is variadic");
  size_t NumParams =
      Call.hasThisHandle() + Call.Sig.Params.size() + Call.hasErrorParam();
  if (FTy->getNumParams() != NumParams || Call.Conv.Params.size() != NumParams)
    return callError("it has " + Twine(FTy->getNumParams()) +
                     " parameters and " + Twine(Call.Conv.Params.size()) +
                     " parameter conversions where the member needs " +
                     Twine(NumParams));
  unsigned I = 0;
  if (Call.hasThisHandle()) {
    ManagedConv C = Call.Conv.Params[0];
    bool Fits = (C == ManagedConv::ThisObj && Call.Type.Kind == TK::Class) ||
                (C == ManagedConv::ThisVal && Call.Type.Kind == TK::ValueType &&
                 !Call.Type.isGCHandle()) ||
                (C == ManagedConv::ThisGCH && Call.Type.isGCHandle());
    if (!Fits || !cli::isManagedHandleType(FTy->getParamType(0), DL))
      return callError("parameter 1: " +
                       describePair(C, Call.Type, FTy->getParamType(0)));
    ++I;
  }
  // ML1, MS5 as amended: the declaring type of a static method or of a
  // 'this-val' or 'this-gch' method, which msil-ld's binder sets in its LTO
  // code generation; read here, when the AsmPrinter writes the body, not
  // when the stub is made.
  if (F.hasFnAttribute(cli::ManagedDeclAttr)) {
    StringRef Decl = Attr(cli::ManagedDeclAttr);
    bool Static = Call.isStaticMethod();
    if (!Static && !Call.hasValueThisMethod())
      return callError("\"" + cli::ManagedDeclAttr +
                       "\" belongs only to a static method or to an instance "
                       "method whose 'this' conversion is 'this-val' or "
                       "'this-gch'");
    if (Decl == Call.Type.TypeName) {
      // A static method keeps 'call V::M'.
      if (!Static)
        Call.ThisAccess = ManagedCall::ValueThisAccess::Declared;
    } else if (Decl == "System.ValueType" || Decl == "System.Object" ||
               Decl == "System.Enum") {
      if (Static)
        Call.StaticInherited = true;
      else
        Call.ThisAccess = ManagedCall::ValueThisAccess::Inherited;
      Call.InheritedFrom = cli::ManagedType::getClass(Decl);
    } else {
      return callError("\"" + cli::ManagedDeclAttr + "\" is '" + Decl +
                       "', which is neither '" + Call.Type.TypeName +
                       "' nor System.ValueType, System.Object or "
                       "System.Enum");
    }
  }
  for (const cli::ManagedType &P : Call.Sig.Params) {
    ManagedConv C = Call.Conv.Params[I];
    if (!cli::fitsManagedConversion(C, P, FTy->getParamType(I), DL))
      return callError("parameter " + Twine(I + 1) + ": " +
                       describePair(C, P, FTy->getParamType(I)));
    ++I;
  }
  if (Call.hasErrorParam() && (Call.Conv.Params[I] != ManagedConv::Err ||
                               !FTy->getParamType(I)->isPointerTy()))
    return callError("parameter " + Twine(I + 1) +
                     ": the error code of msil_catch is an 'int *' with "
                     "conversion 'err'");
  // parseManagedConvList allows 'err' only last; without msil_catch the
  // last parameter is a member's, which 'err' does not fit.

  // The result. A constructor's is the object it constructs (MS4): a value
  // type's is 'newobj' then 'box', the 'boxed' row (MS5), and never 'enum',
  // which Sema never gives a constructor; fitsManagedConversion accepts
  // 'enum' for any value type but GCHandle, so that pair is refused here
  // (hand-written IR). A class or array TYPE and GCHandle never fit 'enum'.
  Type *RetTy = FTy->getReturnType();
  const cli::ManagedType &Result = Call.getAccessResult();
  if (Call.Conv.Result == ManagedConv::Void) {
    // A value that C does not take is dropped.
    if (!RetTy->isVoidTy())
      return callError("the result: " +
                       describePair(ManagedConv::Void, Result, RetTy));
  } else if (Result.isVoid() ||
             (Call.isConstructor() && Call.Conv.Result == ManagedConv::Enum) ||
             !cli::fitsManagedConversion(Call.Conv.Result, Result, RetTy, DL)) {
    return callError("the result: " +
                     describePair(Call.Conv.Result, Result, RetTy));
  }

  AttributeList Attrs = F.getAttributes();
  for (unsigned I = 0, E = FTy->getNumParams(); I != E; ++I) {
    Type *T = FTy->getParamType(I);
    Call.ParamBits.push_back(T->isIntegerTy() ? T->getIntegerBitWidth() : 0);
    // The definition's attributes are the callers' contract: a call extends
    // an argument whose callee has signext or zeroext even if the call site
    // lacks it (CallBase::paramHasAttr). The IR verifier refuses signext
    // together with zeroext.
    Call.ParamExt.push_back(Attrs.hasParamAttr(I, Attribute::SExt)
                                ? ManagedCall::ParamExtension::Sign
                            : Attrs.hasParamAttr(I, Attribute::ZExt)
                                ? ManagedCall::ParamExtension::Zero
                                : ManagedCall::ParamExtension::None);
  }
  if (RetTy->isIntegerTy()) {
    Call.ResultBits = RetTy->getIntegerBitWidth();
    bool SExt = Attrs.hasRetAttr(Attribute::SExt);
    bool ZExt = Attrs.hasRetAttr(Attribute::ZExt);
    if (Call.ResultBits == 8 && (SExt || ZExt))
      Call.ResultExtend = SExt ? cli::Opcode::Conv_i1 : cli::Opcode::Conv_u1;
    else if (Call.ResultBits == 16 && (SExt || ZExt))
      Call.ResultExtend = SExt ? cli::Opcode::Conv_i2 : cli::Opcode::Conv_u2;
    // MS5 as amended: a C _Bool or C++ bool result ('zeroext i1') is 0 or
    // 1, whatever the member's 8-bit or enum value is.
    Call.NormalizeBoolResult =
        Call.ResultBits == 1 && (Call.Conv.Result == ManagedConv::Val ||
                                 Call.Conv.Result == ManagedConv::Enum);
  }
  return Call;
}

void MSIL::resolveStandaloneTextAccess(ManagedCall &Call) {
  if (!Call.hasValueThisMethod() ||
      Call.ThisAccess != ManagedCall::ValueThisAccess::Placeholder)
    return;
  // System.Object's public instance methods, which a value type V reaches
  // through 'constrained. V' whether it overrides them or not (ECMA-335
  // III.2.1).
  const cli::ManagedSignature &Sig = Call.Sig;
  StringRef M = Call.Member;
  bool ObjectMethod =
      Sig.HasThis && !Sig.IsField &&
      ((M == "Equals" && Sig.Return.Kind == TK::Bool &&
        Sig.Params.size() == 1 && Sig.Params[0].Kind == TK::Object) ||
       (M == "GetHashCode" && Sig.Return.Kind == TK::Int32 &&
        Sig.Params.empty()) ||
       (M == "ToString" && Sig.Return.Kind == TK::String &&
        Sig.Params.empty()) ||
       (M == "GetType" && Sig.Return.Kind == TK::Class &&
        Sig.Return.TypeName == "System.Type" && Sig.Params.empty()));
  if (ObjectMethod) {
    Call.ThisAccess = ManagedCall::ValueThisAccess::Inherited;
    Call.InheritedFrom = cli::ManagedType::getClass("System.Object");
  } else {
    // Case (a): every GCHandle row of the built-in table and every member
    // that V declares. A member that V inherits only from System.ValueType
    // or System.Enum is bound only by an object link or LTO.
    Call.ThisAccess = ManagedCall::ValueThisAccess::Declared;
  }
}

//===----------------------------------------------------------------------===//
// Slot P0: the stubs (plan 5.20.5 ML2 (1))
//===----------------------------------------------------------------------===//

/// Reports T-M17 for the managed call whose IR name is \p IRName. Its second
/// form, \p OnlyAttributes, is for declarations that differ only in their
/// 'dllexport', 'constructor' and 'destructor' attributes (plan 5.20.4 MR4
/// as amended).
static void reportDisagreement(LLVMContext &Ctx, StringRef IRName,
                               bool OnlyAttributes = false) {
  Ctx.diagnose(cli::DiagnosticInfoManagedCall(
      cli::getManagedCallSymbol(IRName),
      OnlyAttributes ? cli::ManagedCallErrorKind::DifferentAttributes
                     : cli::ManagedCallErrorKind::DifferentArguments));
}

/// MR4 (T-M17): reports every IR name whose !msil.managed.calls entries are
/// not all equal, in the order of the names' first entries, and returns
/// those names. Entries that differ only in their attributes operand (the
/// operands after cli::NumManagedCallOperands, one of them present or not)
/// get the second form of the text; any other difference gets the first.
static StringSet<> checkManagedCallsMetadata(Module &M) {
  StringSet<> Disagreeing;
  NamedMDNode *NMD = M.getNamedMetadata(cli::ManagedCallsMetadata);
  if (!NMD)
    return Disagreeing;
  // The entries of each IR name that differ from one another.
  MapVector<StringRef, SmallVector<const MDNode *, 2>> Entries;
  for (const MDNode *N : NMD->operands()) {
    const auto *Name = N->getNumOperands()
                           ? dyn_cast_or_null<MDString>(N->getOperand(0))
                           : nullptr;
    if (!Name)
      continue;
    SmallVector<const MDNode *, 2> &Distinct = Entries[Name->getString()];
    if (none_of(Distinct, [&](const MDNode *E) {
          return cli::haveEqualManagedCallOperands(E, N);
        }))
      Distinct.push_back(N);
  }
  for (const auto &[Name, Distinct] : Entries) {
    if (Distinct.size() < 2)
      continue;
    const MDNode *Front = Distinct.front();
    bool OnlyAttributes = all_of(drop_begin(Distinct), [&](const MDNode *N) {
      return cli::haveEqualManagedCallOperands(Front, N,
                                               cli::NumManagedCallOperands);
    });
    Disagreeing.insert(Name);
    reportDisagreement(M.getContext(), Name, OnlyAttributes);
  }
  return Disagreeing;
}

/// Replaces every use of \p F but a direct call with its function type:
/// poison in an instruction, null in a constant.
static void replaceNonCallUses(Function &F) {
  F.replaceUsesWithIf(PoisonValue::get(F.getType()), [&](Use &U) {
    return isa<Instruction>(U.getUser()) && !cli::isDirectManagedCall(U, F);
  });
  F.replaceUsesWithIf(ConstantPointerNull::get(F.getType()),
                      [](Use &U) { return !isa<Instruction>(U.getUser()); });
}

/// T-M10: reports every use of the managed-call declaration \p F that is not
/// a direct call with its function type, once per holding function or
/// global value (cli::collectManagedCallHolders, the walk that msil-ld's
/// check before LTO shares), unless \p Report is false (\p F already got
/// T-M17). Such a use becomes poison in an instruction and null in a
/// constant, so the IR stays valid and no address of \p F remains.
static bool refuseNonCallUses(Function &F, bool Report) {
  // llvm.used and llvm.compiler.used may only list global values; a listing
  // is not an address that the program takes.
  Module &M = *F.getParent();
  removeFromUsedLists(M, [&](Constant *C) { return C == &F; });

  SmallSetVector<const Instruction *, 4> Insts;
  SmallSetVector<const GlobalValue *, 4> Globals;
  cli::collectManagedCallHolders(F, Insts, Globals);
  if (Insts.empty() && Globals.empty())
    return false;
  if (!Report) {
    replaceNonCallUses(F);
    return true;
  }

  std::string Text =
      cli::getManagedCallAddressText(cli::getManagedCallSymbol(F.getName()));
  LLVMContext &Ctx = F.getContext();
  SmallPtrSet<const Function *, 4> Reported;
  for (const Instruction *I : Insts) {
    const Function *Holder = I->getFunction();
    if (Reported.insert(Holder).second)
      Ctx.diagnose(DiagnosticInfoUnsupported(*Holder, Text, I->getDebugLoc()));
  }
  for (const GlobalValue *GV : Globals)
    Ctx.diagnose(DiagnosticInfoGeneric(
        cli::getManagedCallGlobalHolderName(*GV) + ": " + Text));
  replaceNonCallUses(F);
  return true;
}

/// Erases the call \p CB of a declaration that cannot become a stub: its
/// value becomes poison, and an invoke or callbr branches to its normal
/// destination.
static void eraseCall(CallBase &CB) {
  if (!CB.getType()->isVoidTy())
    CB.replaceAllUsesWith(PoisonValue::get(CB.getType()));
  BasicBlock *BB = CB.getParent();
  if (auto *II = dyn_cast<InvokeInst>(&CB)) {
    II->getUnwindDest()->removePredecessor(BB);
    UncondBrInst::Create(II->getNormalDest(), II->getIterator());
  } else if (auto *CBr = dyn_cast<CallBrInst>(&CB)) {
    for (BasicBlock *Dest : CBr->getIndirectDests())
      Dest->removePredecessor(BB);
    UncondBrInst::Create(CBr->getDefaultDest(), CBr->getIterator());
  }
  CB.eraseFromParent();
}

bool MSIL::createManagedCallStubs(Module &M) {
  StringSet<> Disagreeing = checkManagedCallsMetadata(M);
  bool Changed = false;
  for (Function &F : M) {
    if (!F.isDeclaration() || !F.hasFnAttribute(cli::ManagedTypeAttr))
      continue;
    // MR4, T-M17 as amended: a direct call of F whose function type is not
    // F's is what the IR linker leaves when two units declare F with
    // different C prototypes; the units disagree, which is T-M17, never
    // T-M10 (the IR linker merges equal !msil.managed.calls entries, so no
    // count tells such a call from a call through a cast pointer in one
    // unit, which is T-M17 too). So is a direct call with F's own function
    // type whose own signext or zeroext would change F's stub (the rule of
    // cli::hasManagedCallOfOtherExtension): the IR linker keeps the first
    // unit's declaration with its attributes and leaves each call its own,
    // so two units that declare F with, for example, a 'short' and an
    // 'unsigned short' result leave such a call (msil-ld refuses the same
    // units as separate inputs, whose stubs differ), and one unit's call
    // through a cast pointer that changes only a narrow result's signedness
    // is the same IR. A function that got T-M17 gets no T-M10; a use that is
    // not a callee (an address) is T-M10.
    bool Disagrees = Disagreeing.contains(F.getName());
    if (!Disagrees && (cli::hasManagedCallOfOtherType(F) ||
                       cli::hasManagedCallOfOtherExtension(F))) {
      reportDisagreement(F.getContext(), F.getName());
      Disagrees = true;
    }
    Changed |= refuseNonCallUses(F, /*Report=*/!Disagrees);

    Expected<ManagedCall> Call = parseManagedCall(F);
    if (!Call) {
      // clang's Sema makes valid attributes; this is hand-written IR.
      F.getContext().diagnose(cli::DiagnosticInfoManagedCall(
          cli::getManagedCallSymbol(F.getName()),
          cli::ManagedCallErrorKind::InvalidAttributes,
          toString(Call.takeError())));
      for (User *U : make_early_inc_range(F.users()))
        if (auto *CB = dyn_cast<CallBase>(U))
          eraseCall(*CB);
      for (StringRef Name :
           {cli::ManagedTypeAttr, cli::ManagedMemberAttr, cli::ManagedSigAttr,
            cli::ManagedAsmAttr, cli::ManagedConvAttr, cli::ManagedCatchAttr})
        F.removeFnAttr(Name);
      Changed = true;
      continue;
    }
    // The stub, in place, also for a declaration that nothing calls: every
    // call keeps its callee (rule R1: a MethodDef of the module).
    F.setLinkage(GlobalValue::InternalLinkage);
    F.setVisibility(GlobalValue::DefaultVisibility);
    F.setDLLStorageClass(GlobalValue::DefaultStorageClass);
    F.setDSOLocal(true);
    F.removeFnAttr(Attribute::AlwaysInline);
    F.addFnAttr(Attribute::NoInline);
    F.addFnAttr(cli::ManagedStubAttr);
    // A declaration's DISubprogram (-g) is not distinct, which a definition
    // needs; the AsmPrinter writes the body, which has no source lines.
    F.setSubprogram(nullptr);
    LLVMContext &Ctx = F.getContext();
    new UnreachableInst(Ctx, BasicBlock::Create(Ctx, "entry", &F));
    Changed = true;
  }
  return Changed;
}

bool MSIL::isManagedCallStub(const Function &F) {
  return !F.isDeclaration() && F.hasFnAttribute(cli::ManagedStubAttr);
}

/// The C name of the managed call that \p GV forwards to, when \p GV is the
/// forwarder of an exported or startup managed call.
static std::optional<StringRef> getForwardedManagedCall(const GlobalValue &GV) {
  const auto *F = dyn_cast<Function>(&GV);
  if (!F || !F->hasFnAttribute(cli::ManagedForwarderAttr))
    return std::nullopt;
  return F->getFnAttribute(cli::ManagedForwarderAttr).getValueAsString();
}

StringRef MSIL::getDiagnosticILName(const GlobalValue &GV,
                                    const TargetMachine &TM) {
  if (std::optional<StringRef> Symbol = getForwardedManagedCall(GV))
    return *Symbol;
  return getILName(GV, TM);
}

std::string MSIL::getDiagnosticIRName(const GlobalValue &GV) {
  std::optional<StringRef> Symbol = getForwardedManagedCall(GV);
  if (!Symbol)
    return getIRName(GV);
  // As GlobalValue::printAsOperand spells a global named <s>.
  std::string Name = "@";
  raw_string_ostream OS(Name);
  printLLVMNameWithoutPrefix(OS, *Symbol);
  return Name;
}

//===----------------------------------------------------------------------===//
// The stub body (plan 5.20.5 ML2 (2))
//===----------------------------------------------------------------------===//

namespace {

/// The locals that a stub adds after the method's own (ML2 (2)).
struct StubLocals {
  /// The int32 length of each UTF-8 or UTF-32 parameter, by C parameter.
  SmallVector<std::optional<unsigned>, 8> Length;
  /// With msil_catch: the int32 code that the filter chose, and the result.
  std::optional<unsigned> Code, Result;
};

} // end anonymous namespace

/// The numbers of the locals that the stub of \p Call adds, from \p First
/// on: the length of each UTF-8 or UTF-32 parameter in order, then, with
/// msil_catch, the code and, unless the C result is void (\p ILSig), the
/// result.
static StubLocals layoutLocals(const MSIL::ManagedCall &Call,
                               const MSIL::Signature &ILSig, unsigned First) {
  StubLocals L;
  unsigned Next = First;
  for (ManagedConv C : Call.Conv.Params)
    L.Length.push_back(C == ManagedConv::UTF8 || C == ManagedConv::UTF32
                           ? std::optional<unsigned>(Next++)
                           : std::nullopt);
  if (Call.hasErrorParam()) {
    L.Code = Next++;
    if (!ILSig.Results.empty())
      L.Result = Next++;
  }
  return L;
}

/// Whether whole-program text checks the kind of \p T at run time (5.20.6
/// MW2 as amended), the TYPE of a 'this-obj' member or of a class's
/// constructor (a class), or of a 'this-val' member or of a value type's
/// constructor (a value type): every kind but a fixed and right one,
/// System.String or System.Object as a class and a one-byte value type
/// (MSIL::isOneByteValueType) as a value type, whose TypeSpec would be the
/// long form that PEVerify /md refuses.
static bool checksKindAtRunTime(const cli::ManagedType &T) {
  if (T.Kind == TK::Class)
    return T.TypeName != "System.String" && T.TypeName != "System.Object";
  return !MSIL::isOneByteValueType(T.TypeName);
}

/// Whether the value type \p E of an 'enum' conversion is never an enum: a
/// one-byte value type, or System.String or System.Object, which are
/// classes. Its enum check is the throw alone.
static bool isNeverEnum(const cli::ManagedType &E) {
  return MSIL::isOneByteValueType(E.TypeName) ||
         E.TypeName == "System.String" || E.TypeName == "System.Object";
}

/// The class System.Type and the signatures of the two members that an enum
/// check calls on it in the core library, as C# compiles typeof(E).IsEnum:
/// 'class System.Type GetTypeFromHandle(valuetype
/// System.RuntimeTypeHandle)' and 'instance bool get_IsEnum()'. Tokens point
/// to them.
static const cli::ManagedType &getTypeClass() {
  static const cli::ManagedType Type =
      cli::ManagedType::getClass("System.Type");
  return Type;
}
static const cli::ManagedSignature &getTypeFromHandleSig() {
  static const cli::ManagedSignature Sig = [] {
    cli::ManagedSignature S;
    S.Return = getTypeClass();
    S.Params.push_back(
        cli::ManagedType::getValueType("System.RuntimeTypeHandle"));
    return S;
  }();
  return Sig;
}
static const cli::ManagedSignature &getIsEnumSig() {
  static const cli::ManagedSignature Sig = [] {
    cli::ManagedSignature S;
    S.HasThis = true;
    S.Return = cli::ManagedType::get(TK::Bool);
    return S;
  }();
  return Sig;
}

/// System.InvalidProgramException, whose constructor(string) a failed check
/// of whole-program text calls in the core library (5.20.6 MW2 as amended),
/// and that constructor's signature. Tokens point to them.
static const cli::ManagedType &getCheckException() {
  static const cli::ManagedType Exception =
      cli::ManagedType::getClass("System.InvalidProgramException");
  return Exception;
}
static const cli::ManagedSignature &getCheckExceptionCtor() {
  static const cli::ManagedSignature Ctor = [] {
    cli::ManagedSignature Sig;
    Sig.HasThis = true;
    Sig.Params.push_back(cli::ManagedType::get(TK::String));
    return Sig;
  }();
  return Ctor;
}

namespace {

/// Builds the body of one stub, and measures its stack depth on the way.
/// Without an MCContext it only measures (MSILStackCheck's dry run): no
/// instruction, label or token is made.
class StubWriter {
  const MSIL::ManagedCall &Call;
  const MSIL::Signature &ILSig;
  StubLocals Locals;
  MCContext *Ctx;
  unsigned FunctionNumber;
  /// Whole-program text: the run-time checks of emitManagedCallStubBody.
  bool TextChecks;

  /// An enum check of whole-program text (T-M7 at run time): the value type
  /// of an 'enum' parameter or result, the size in bytes that the C integer
  /// gives it, and msil-ld's text for a mismatch.
  struct EnumCheck {
    const cli::ManagedType *Type;
    unsigned Bytes;
    std::string Message;
  };
  SmallVector<EnumCheck, 2> EnumChecks;

  SmallVector<MCInst, 64> Insts;
  /// The symbol of each label (null in a dry run), and the label positions:
  /// a label precedes the instruction of its index.
  SmallVector<MCSymbol *, 16> Syms;
  SmallVector<std::pair<unsigned, unsigned>, 16> LabelPos;
  int Depth = 0;
  unsigned MaxStack = 0;

  /// The labels of the clause of msil_catch.
  unsigned TryBegin = 0, FilterBegin = 0, HandlerBegin = 0, Done = 0;

  unsigned newLabel() {
    Syms.push_back(Ctx ? Ctx->createBlockSymbol("MC" + Twine(FunctionNumber) +
                                                    "_" + Twine(Syms.size()),
                                                /*AlwaysEmit=*/false)
                       : nullptr);
    return Syms.size() - 1;
  }
  /// Places label \p L before the next instruction, where the stack depth
  /// is \p AtDepth.
  void label(unsigned L, int AtDepth) {
    LabelPos.push_back({L, unsigned(Insts.size())});
    Depth = AtDepth;
  }
  void push(int Delta) {
    Depth += Delta;
    assert(Depth >= 0 && "a stub body pops an empty stack");
    MaxStack = std::max(MaxStack, unsigned(Depth));
  }
  static MCInst makeInst(cli::Opcode Op) {
    MCInst MI;
    MI.setOpcode(MSIL::getMCOpcodeForCIL(Op));
    return MI;
  }
  void op(cli::Opcode Op, int Delta) {
    if (Ctx)
      Insts.push_back(makeInst(Op));
    push(Delta);
  }
  void ldc(int32_t V) {
    if (Ctx)
      Insts.push_back(MSIL::buildLdcI4(V));
    push(1);
  }
  void ldarg(unsigned I) {
    if (Ctx)
      Insts.push_back(MSIL::buildVarInst(cli::Opcode::Ldarg, I));
    push(1);
  }
  void ldloc(unsigned I) {
    if (Ctx)
      Insts.push_back(MSIL::buildVarInst(cli::Opcode::Ldloc, I));
    push(1);
  }
  void stloc(unsigned I) {
    if (Ctx)
      Insts.push_back(MSIL::buildVarInst(cli::Opcode::Stloc, I));
    push(-1);
  }
  void token(cli::Opcode Op, const MCExpr *E, int Delta) {
    if (Ctx) {
      MCInst MI = makeInst(Op);
      MI.addOperand(MCOperand::createExpr(E));
      Insts.push_back(std::move(MI));
    }
    push(Delta);
  }
  /// An instruction whose token is the BCL member or type \p Id.
  void bcl(cli::Opcode Op, cli::BCLMemberId Id, int Delta) {
    token(Op, Ctx ? MSILMemberRefExpr::createBCL(Id, *Ctx) : nullptr, Delta);
  }
  /// An instruction whose token is the type \p T in the declaration's scope.
  void type(cli::Opcode Op, const cli::ManagedType &T, int Delta) {
    token(Op,
          Ctx ? MSILMemberRefExpr::createManagedType(&T, Call.Scope, *Ctx)
              : nullptr,
          Delta);
  }
  /// An instruction that keeps the stack depth and whose token is TYPE as
  /// the declaration states its kind: the castclass of a 'this-obj' handle,
  /// the unbox of a 'this-val' handle, and a constructor's castclass or box
  /// (checkConstructedKind). In whole-program text it is a TypeSpec of that
  /// kind ('class [<s>]T', 'valuetype [<s>]V'), which the runtime checks
  /// against the type's definition when it compiles the stub (T-M16 at run
  /// time), unless that kind is fixed and right (checksKindAtRunTime);
  /// otherwise TYPE's TypeRef, as objects name it, whose kind msil-ld checks
  /// (MK3).
  void declaredType(cli::Opcode Op) {
    if (!TextChecks || !checksKindAtRunTime(Call.Type)) {
      type(Op, Call.Type, 0);
      return;
    }
    token(Op,
          Ctx ? MSILMemberRefExpr::createManagedTypeSpec(&Call.Type, Call.Scope,
                                                         *Ctx)
              : nullptr,
          0);
  }
  /// T-M16 at run time for a constructor (whole-program text, MW2 as
  /// amended), whatever its result conversion: the object or value that the
  /// newobj left passes through declaredType, unless TYPE's kind is fixed
  /// and right. A class gets 'castclass class [<s>]T', which cannot fail
  /// after a newobj of T, so a JIT may drop it. A value type's 'boxed'
  /// result names it in its box (emitResult); any other result boxes a copy
  /// and drops it ('dup', 'box valuetype [<s>]V', 'pop'). Objects need
  /// neither: msil-ld checks TYPE's kind (MK3).
  void checkConstructedKind() {
    if (!TextChecks || !checksKindAtRunTime(Call.Type))
      return;
    if (Call.Type.Kind == TK::Class) {
      declaredType(cli::Opcode::Castclass);
      return;
    }
    if (Call.Conv.Result == ManagedConv::Boxed)
      return;
    op(cli::Opcode::Dup, 1);
    declaredType(cli::Opcode::Box);
    op(cli::Opcode::Pop, -1);
  }
  /// An instruction whose token is the member \p Name with the signature
  /// \p Sig of the class \p Owner, which the core library defines: in text
  /// in mscorlib, as BCL members are (plan 5.11.8); in an object in
  /// '[msil.bcl]', like TYPE.
  void coreMember(cli::Opcode Op, const cli::ManagedType &Owner, StringRef Name,
                  const cli::ManagedSignature &Sig, int Delta) {
    token(Op,
          Ctx ? MSILMemberRefExpr::createManagedMember(
                    &Owner, Name, &Sig, cli::ManagedDefaultAssembly, *Ctx)
              : nullptr,
          Delta);
  }
  /// The access: an instruction whose token is the declaration's member.
  void member(cli::Opcode Op, int Delta) {
    token(Op,
          Ctx ? MSILMemberRefExpr::createManagedMember(
                    &Call.Type, Call.Member, &Call.Sig, Call.Scope, *Ctx)
              : nullptr,
          Delta);
  }
  /// The access of a member that the declaration's TYPE only inherits: an
  /// instruction whose token is the member with the same name and signature
  /// on Call.InheritedFrom, a core type, as C# emits it.
  void inheritedMember(cli::Opcode Op, int Delta) {
    coreMember(Op, Call.InheritedFrom, Call.Member, Call.Sig, Delta);
  }
  /// A branch to label \p L (relaxBranches chooses its form).
  void branch(cli::Opcode Op, unsigned L, int Delta) {
    token(Op, Ctx ? MCSymbolRefExpr::create(Syms[L], *Ctx) : nullptr, Delta);
  }

  /// ML2 (3): handle (native int) -> object.
  void derefHandle() {
    bcl(cli::Opcode::Call, cli::BCLMemberId::GCHandle_FromIntPtr, 0);
    bcl(cli::Opcode::Box, cli::BCLMemberId::GCHandle, 0);
    bcl(cli::Opcode::Unbox, cli::BCLMemberId::GCHandle, 0);
    bcl(cli::Opcode::Call, cli::BCLMemberId::GCHandle_get_Target, 0);
  }
  /// MH1: handle argument \p Arg -> object, cast to \p CastTo if it is
  /// not null, or to TYPE through declaredType for the 'this' handle of a
  /// 'this-obj' member (\p ThisObj); the handle 0 is null and calls nothing
  /// (5.20.9 MH1 as built). The stack holds one more value.
  void loadHandleTarget(unsigned Arg, const cli::ManagedType *CastTo = nullptr,
                        bool ThisObj = false) {
    int Base = Depth;
    unsigned NotNull = newLabel(), Join = newLabel();
    ldarg(Arg);
    branch(cli::Opcode::Brtrue_s, NotNull, -1);
    op(cli::Opcode::Ldnull, 1);
    branch(cli::Opcode::Br_s, Join, 0);
    label(NotNull, Base);
    ldarg(Arg);
    derefHandle();
    if (ThisObj)
      declaredType(cli::Opcode::Castclass);
    else if (CastTo)
      type(cli::Opcode::Castclass, *CastTo, 0);
    label(Join, Base + 1);
  }
  /// MH1: object -> a new handle (native int).
  void allocHandle() {
    bcl(cli::Opcode::Call, cli::BCLMemberId::GCHandle_Alloc, 0);
    bcl(cli::Opcode::Call, cli::BCLMemberId::GCHandle_ToIntPtr, 0);
  }
  /// A handle result whose IL type is int32 (a 32-bit triple's pointer).
  void narrowHandle() {
    if (ILSig.Results.front() == MSIL::ValType::I32)
      op(cli::Opcode::Conv_i4, 0);
  }
  /// MS5 as amended: the int32 of an i1, i8 or i16 argument of \p Bits bits
  /// whose caller did not extend it, cut to those bits: 'ldc.i4.1; and',
  /// 'conv.u1' or 'conv.u2'. Any other width has no undefined bits.
  void narrowUnextended(unsigned Bits) {
    switch (Bits) {
    case 1:
      ldc(1);
      op(cli::Opcode::And, -1);
      return;
    case 8:
      op(cli::Opcode::Conv_u1, 0);
      return;
    case 16:
      op(cli::Opcode::Conv_u2, 0);
      return;
    default:
      return;
    }
  }

  void planEnumChecks();
  void emitEnumChecks();
  void emitCheckFailure(StringRef Message);
  void emitLengthLoop(unsigned Arg, bool UTF32);
  void emitThis();
  void emitArgument(unsigned Arg, const cli::ManagedType &CIL, ManagedConv C);
  void emitAccess();
  void emitResult();
  void emitZero(MSIL::ValType T);

public:
  StubWriter(const MSIL::ManagedCall &Call, const MSIL::Signature &ILSig,
             unsigned FirstLocal, MCContext *Ctx, unsigned FunctionNumber,
             bool TextChecks)
      : Call(Call), ILSig(ILSig), Locals(layoutLocals(Call, ILSig, FirstLocal)),
        Ctx(Ctx), FunctionNumber(FunctionNumber), TextChecks(TextChecks) {
    if (TextChecks)
      planEnumChecks();
  }

  void build();
  unsigned getMaxStack() const { return MaxStack; }
  void emit(MSILTargetStreamer &TS, MCStreamer &OS, const MCInstrInfo &MCII,
            function_ref<void(const MCInst &)> EmitInst);
};

} // end anonymous namespace

/// The enum checks of whole-program text (5.20.6 MW2 as amended), as msil-ld
/// checks an object's stub (MK2, T-M7): each 'enum' parameter in order, then
/// an 'enum' result, a method's or a field's (a constructor's result is its
/// object, never 'enum': parseManagedCall refuses that pair). The C integer
/// passes max(8, its IR width) bits, as msil-ld counts them: an i1 passes a
/// byte.
void StubWriter::planEnumChecks() {
  auto Add = [&](const cli::ManagedType &E, unsigned Bits,
                 const Twine &Message) {
    EnumChecks.push_back({&E, Bits / 8, Message.str()});
  };
  unsigned First = Call.hasThisHandle();
  for (unsigned K = 0, N = Call.Sig.Params.size(); K != N; ++K) {
    if (Call.Conv.Params[First + K] != ManagedConv::Enum)
      continue;
    const cli::ManagedType &P = Call.Sig.Params[K];
    unsigned Bits = std::max(8u, Call.ParamBits[First + K]);
    Add(P, Bits,
        "managed call '" + Twine(Call.Name) + "': parameter " + Twine(K + 1) +
            " passes " + Twine(Bits) + " bits for '" + P.TypeName +
            "', which is not an enum of that width");
  }
  if (Call.Conv.Result == ManagedConv::Enum && !Call.isConstructor()) {
    unsigned Bits = std::max(8u, Call.ResultBits);
    Add(Call.Sig.Return, Bits,
        "managed call '" + Twine(Call.Name) + "': the result has " +
            Twine(Bits) + " bits for '" + Call.Sig.Return.TypeName +
            "', which is not an enum of that width");
  }
}

/// The enum checks, before anything else, and so outside the clause of
/// msil_catch, which never maps their exception: E must be an enum, as
/// C#'s typeof(E).IsEnum tests it, and E's size must be the C integer's.
/// No value of E is needed, so the stub declares no local of E: its
/// locals stay primitive (MSILTargetStreamer::emitLocals). The .NET JITs
/// fold both tests to constants; the .NET Framework calls get_IsEnum once
/// per call. An E that is never an enum gets the throw alone.
void StubWriter::emitEnumChecks() {
  for (const EnumCheck &Check : EnumChecks) {
    if (isNeverEnum(*Check.Type)) {
      emitCheckFailure(Check.Message);
      continue;
    }
    unsigned Fail = newLabel(), Pass = newLabel();
    type(cli::Opcode::Ldtoken, *Check.Type, 1);
    coreMember(cli::Opcode::Call, getTypeClass(), "GetTypeFromHandle",
               getTypeFromHandleSig(), 0);
    coreMember(cli::Opcode::Callvirt, getTypeClass(), "get_IsEnum",
               getIsEnumSig(), 0);
    branch(cli::Opcode::Brfalse_s, Fail, -1);
    type(cli::Opcode::Sizeof, *Check.Type, 1);
    ldc(Check.Bytes);
    branch(cli::Opcode::Beq_s, Pass, -2);
    label(Fail, 0);
    emitCheckFailure(Check.Message);
    label(Pass, 0);
  }
}

/// A failed check of whole-program text: throws
/// System.InvalidProgramException(\p Message), msil-ld's text for the
/// declaration (5.20.6 MW2 as amended).
void StubWriter::emitCheckFailure(StringRef Message) {
  token(cli::Opcode::Ldstr,
        Ctx ? MSILMemberRefExpr::createUserString(Message, *Ctx) : nullptr, 1);
  coreMember(cli::Opcode::Newobj, getCheckException(), ".ctor",
             getCheckExceptionCtor(), 0);
  op(cli::Opcode::Throw, -1);
}

/// S1, S3: counts the bytes (UTF-8) or 4-byte units (UTF-32) of argument
/// \p Arg up to its terminating 0 into its length local, with the empty
/// stack that a backward branch needs. A null pointer gives 0.
void StubWriter::emitLengthLoop(unsigned Arg, bool UTF32) {
  unsigned Len = *Locals.Length[Arg];
  unsigned Loop = newLabel(), End = newLabel();
  ldc(0);
  stloc(Len);
  ldarg(Arg);
  branch(cli::Opcode::Brfalse_s, End, -1);
  label(Loop, 0);
  ldarg(Arg);
  ldloc(Len);
  if (UTF32) {
    ldc(2);
    op(cli::Opcode::Shl, -1);
  }
  op(cli::Opcode::Add, -1);
  op(UTF32 ? cli::Opcode::Ldind_i4 : cli::Opcode::Ldind_u1, 0);
  branch(cli::Opcode::Brfalse_s, End, -1);
  ldloc(Len);
  ldc(1);
  op(cli::Opcode::Add, -1);
  stloc(Len);
  branch(cli::Opcode::Br_s, Loop, 0);
  label(End, 0);
}

/// The 'this' of an instance member (MS5): an object reference, a managed
/// pointer to a boxed value type, or one to the GCHandle itself. A 'this'
/// handle of 0 is null, so the access or 'unbox' throws
/// NullReferenceException (5.20.8 MX4); a GCHandle value of 0 throws
/// InvalidOperationException in FromIntPtr, as in C#.
void StubWriter::emitThis() {
  switch (Call.Conv.Params[0]) {
  case ManagedConv::ThisObj:
    loadHandleTarget(0, &Call.Type, /*ThisObj=*/true);
    return;
  case ManagedConv::ThisVal:
    loadHandleTarget(0);
    declaredType(cli::Opcode::Unbox);
    return;
  case ManagedConv::ThisGCH:
    ldarg(0);
    bcl(cli::Opcode::Call, cli::BCLMemberId::GCHandle_FromIntPtr, 0);
    bcl(cli::Opcode::Box, cli::BCLMemberId::GCHandle, 0);
    bcl(cli::Opcode::Unbox, cli::BCLMemberId::GCHandle, 0);
    return;
  default:
    llvm_unreachable("parseManagedCall checked the 'this' conversion");
  }
}

/// Argument \p Arg, converted by \p C to the member's parameter type \p CIL.
void StubWriter::emitArgument(unsigned Arg, const cli::ManagedType &CIL,
                              ManagedConv C) {
  int Base = Depth;
  // A string or object argument: a null pointer or handle is null.
  auto NullCheck = [&](unsigned NotNull, unsigned Join) {
    ldarg(Arg);
    branch(cli::Opcode::Brtrue_s, NotNull, -1);
    op(cli::Opcode::Ldnull, 1);
    branch(cli::Opcode::Br_s, Join, 0);
    label(NotNull, Base);
  };
  using Extension = MSIL::ManagedCall::ParamExtension;
  unsigned Bits = Call.ParamBits[Arg];
  Extension Ext = Call.ParamExt[Arg];
  switch (C) {
  case ManagedConv::Bool:
    ldarg(Arg);
    // MS5 as amended: only the bits of the C value decide whether it is
    // true, not the undefined ones above an i1, i8 or i16 that the caller
    // did not extend (clang's Windows and non-Darwin AAPCS64 ABIs give such
    // parameters neither signext nor zeroext).
    if (Ext == Extension::None)
      narrowUnextended(Bits);
    ldc(0);
    op(cli::Opcode::Cgt_un, -1);
    return;
  case ManagedConv::Val:
  case ManagedConv::Enum: {
    ldarg(Arg);
    // MS5 as amended: an i1 (a C _Bool) that the caller did not zero-extend
    // is only its bit 0, while a call to an 8-bit parameter keeps bits 1-7
    // as well, and so does an 'enum' stub's exact 'unsigned int8' parameter.
    // An i8 or i16 needs nothing: the call truncates the int32 to the
    // member's parameter of the same width (ECMA-335 III.1.6).
    if (Bits == 1 && Ext != Extension::Zero)
      narrowUnextended(Bits);
    // D2: a 64-bit integer is a native int on a 64-bit triple.
    bool Wide = CIL.Kind == TK::Int64 || CIL.Kind == TK::UInt64 ||
                (C == ManagedConv::Enum && Bits == 64);
    if (Wide && ILSig.Params[Arg] != MSIL::ValType::I64)
      op(CIL.Kind == TK::UInt64 ? cli::Opcode::Conv_u8 : cli::Opcode::Conv_i8,
         0);
    return;
  }
  case ManagedConv::UTF8:
  case ManagedConv::UTF32: {
    bool UTF32 = C == ManagedConv::UTF32;
    unsigned NotNull = newLabel(), Join = newLabel();
    NullCheck(NotNull, Join);
    // new UTF8Encoding(false, true) or UTF32Encoding(false, false, true):
    // no BOM, little-endian, throwing (S1, S3).
    ldc(0);
    if (UTF32)
      ldc(0);
    ldc(1);
    if (UTF32)
      bcl(cli::Opcode::Newobj, cli::BCLMemberId::UTF32Encoding_ctor, -2);
    else
      bcl(cli::Opcode::Newobj, cli::BCLMemberId::UTF8Encoding_ctor, -1);
    ldarg(Arg);
    ldloc(*Locals.Length[Arg]);
    if (UTF32) {
      ldc(2);
      op(cli::Opcode::Shl, -1);
    }
    bcl(cli::Opcode::Callvirt, cli::BCLMemberId::Encoding_GetString, -2);
    label(Join, Base + 1);
    return;
  }
  case ManagedConv::UTF16: {
    unsigned NotNull = newLabel(), Join = newLabel();
    NullCheck(NotNull, Join);
    ldarg(Arg);
    bcl(cli::Opcode::Newobj, cli::BCLMemberId::String_ctor_PChar, 0);
    label(Join, Base + 1);
    return;
  }
  case ManagedConv::Obj:
    loadHandleTarget(Arg, CIL.Kind != TK::Object ? &CIL : nullptr);
    return;
  case ManagedConv::Boxed:
    // A handle of 0 is null: unbox.any throws NullReferenceException.
    loadHandleTarget(Arg);
    type(cli::Opcode::Unbox_any, CIL, 0);
    return;
  case ManagedConv::GCH:
    ldarg(Arg);
    bcl(cli::Opcode::Call, cli::BCLMemberId::GCHandle_FromIntPtr, 0);
    return;
  case ManagedConv::ThisObj:
  case ManagedConv::ThisVal:
  case ManagedConv::ThisGCH:
  case ManagedConv::Void:
  case ManagedConv::Err:
    break;
  }
  llvm_unreachable("parseManagedCall checked the parameter conversions");
}

/// The access (ML2 (2)): newarr, newobj, ldsfld, ldfld, call, callvirt, or
/// 'constrained. V' then callvirt for a value type's 'this'.
void StubWriter::emitAccess() {
  const cli::ManagedSignature &Sig = Call.Sig;
  int NumParams = Sig.Params.size();
  if (Call.isArrayConstructor()) {
    type(cli::Opcode::Newarr, Call.Type.getElement(), 0);
  } else if (Call.isConstructor()) {
    member(cli::Opcode::Newobj, 1 - NumParams);
    checkConstructedKind();
  } else if (Sig.IsField) {
    if (Sig.HasThis)
      member(cli::Opcode::Ldfld, 0);
    else
      member(cli::Opcode::Ldsfld, 1);
  } else {
    int Delta = -(NumParams + int(Sig.HasThis)) + int(!Sig.Return.isVoid());
    if (!Call.hasValueThisMethod()) {
      // A static method that a value type or an enum only inherits (MS5 as
      // amended for static methods): 'call D::M', which "msil-managed-decl"
      // names, since 'call V::M' throws MissingMethodException.
      if (Call.StaticInherited) {
        inheritedMember(cli::Opcode::Call, Delta);
        return;
      }
      // callvirt for an instance method of a reference type ('this-obj':
      // the runtime resolves an inherited member through V); call for a
      // static method.
      bool Virtual =
          Sig.HasThis && Call.Conv.Params.front() == ManagedConv::ThisObj;
      member(Virtual ? cli::Opcode::Callvirt : cli::Opcode::Call, Delta);
      return;
    }
    // A value type's 'this' (MS5 as amended): the member's declaring type
    // decides the form, which "msil-managed-decl" names.
    using Access = MSIL::ManagedCall::ValueThisAccess;
    switch (Call.ThisAccess) {
    case Access::Declared:
      member(cli::Opcode::Call, Delta);
      return;
    case Access::Placeholder:
      // Object mode only (whole-program text was resolved by
      // resolveStandaloneTextAccess): FE 16 <V> then 6F <V::M>, which
      // msil-ld turns into 6 nops and 'call V::M', or whose MemberRef's
      // parent it replaces, in place.
      type(cli::Opcode::Constrained, Call.Type, 0);
      member(cli::Opcode::Callvirt, Delta);
      return;
    case Access::Inherited:
      type(cli::Opcode::Constrained, Call.Type, 0);
      inheritedMember(cli::Opcode::Callvirt, Delta);
      return;
    }
    llvm_unreachable("unknown value-type 'this' access");
  }
}

/// The result conversion (MS5, MH2): the value that the access left becomes
/// the C result.
void StubWriter::emitResult() {
  const cli::ManagedType &R = Call.getAccessResult();
  switch (Call.Conv.Result) {
  case ManagedConv::Void:
    if (!R.isVoid())
      op(cli::Opcode::Pop, -1);
    return;
  case ManagedConv::Bool:
    ldc(0);
    op(cli::Opcode::Cgt_un, -1);
    return;
  case ManagedConv::Val:
  case ManagedConv::Enum: {
    // A method's or a field's result: parseManagedCall accepts neither code
    // for a constructor, whose result is its object (MS4).
    if (Call.NormalizeBoolResult) {
      // C11 6.3.1.2: the value compared with 0 at its own width, before
      // anything narrows it.
      ldc(0);
      if (R.Kind == TK::Int64 || R.Kind == TK::UInt64)
        op(cli::Opcode::Conv_i8, 0);
      else if (R.Kind == TK::NativeInt || R.Kind == TK::NativeUInt ||
               R.Kind == TK::Pointer)
        op(cli::Opcode::Conv_i, 0);
      op(cli::Opcode::Cgt_un, -1);
      return;
    }
    MSIL::ValType IL = ILSig.Results.front();
    bool Wide =
        R.Kind == TK::Int64 || R.Kind == TK::UInt64 ||
        (Call.Conv.Result == ManagedConv::Enum && Call.ResultBits == 64);
    if (Wide && IL == MSIL::ValType::NativeInt)
      op(R.Kind == TK::UInt64 ? cli::Opcode::Conv_u : cli::Opcode::Conv_i, 0);
    else if (IL == MSIL::ValType::I32 &&
             (R.Kind == TK::NativeInt || R.Kind == TK::NativeUInt ||
              R.Kind == TK::Pointer))
      op(cli::Opcode::Conv_i4, 0);
    if (Call.ResultExtend)
      op(*Call.ResultExtend, 0);
    return;
  }
  case ManagedConv::Obj: {
    // MH2: a null reference is the handle 0 and allocates nothing.
    int Base = Depth;
    unsigned NotNull = newLabel(), Join = newLabel();
    op(cli::Opcode::Dup, 1);
    branch(cli::Opcode::Brtrue_s, NotNull, -1);
    op(cli::Opcode::Pop, -1);
    ldc(0);
    op(cli::Opcode::Conv_i, 0);
    branch(cli::Opcode::Br_s, Join, 0);
    label(NotNull, Base);
    allocHandle();
    label(Join, Base);
    narrowHandle();
    return;
  }
  case ManagedConv::Boxed:
    // A value type's constructor: the box names TYPE with its declared kind
    // (checkConstructedKind).
    if (Call.isConstructor())
      declaredType(cli::Opcode::Box);
    else
      type(cli::Opcode::Box, R, 0);
    allocHandle();
    narrowHandle();
    return;
  case ManagedConv::GCH:
    bcl(cli::Opcode::Call, cli::BCLMemberId::GCHandle_ToIntPtr, 0);
    narrowHandle();
    return;
  case ManagedConv::UTF8:
  case ManagedConv::UTF16:
  case ManagedConv::UTF32:
  case ManagedConv::ThisObj:
  case ManagedConv::ThisVal:
  case ManagedConv::ThisGCH:
  case ManagedConv::Err:
    break;
  }
  llvm_unreachable("parseManagedCall checked the result conversion");
}

/// MX2: the zero value of the C result's IL type.
void StubWriter::emitZero(MSIL::ValType T) {
  ldc(0);
  switch (T) {
  case MSIL::ValType::I32:
    return;
  case MSIL::ValType::NativeInt:
    op(cli::Opcode::Conv_i, 0);
    return;
  case MSIL::ValType::I64:
    op(cli::Opcode::Conv_i8, 0);
    return;
  case MSIL::ValType::F32:
    op(cli::Opcode::Conv_r4, 0);
    return;
  case MSIL::ValType::F64:
    op(cli::Opcode::Conv_r8, 0);
    return;
  }
}

void StubWriter::build() {
  // Whole-program text's enum checks come first: no clause covers them.
  emitEnumChecks();

  bool HasCatch = Call.hasErrorParam();
  if (HasCatch) {
    TryBegin = newLabel();
    FilterBegin = newLabel();
    HandlerBegin = newLabel();
    Done = newLabel();
    // The stack is empty at method entry and after the enum checks, as a
    // '.try' needs (ECMA-335 II.19); the whole body is protected (MX2), the
    // conversions included (MX4).
    label(TryBegin, 0);
  }

  // The lengths first, while the stack is empty.
  unsigned NumParams = Call.Conv.Params.size();
  for (unsigned I = 0; I != NumParams; ++I)
    if (ManagedConv C = Call.Conv.Params[I];
        C == ManagedConv::UTF8 || C == ManagedConv::UTF32)
      emitLengthLoop(I, C == ManagedConv::UTF32);

  unsigned Arg = 0;
  if (Call.hasThisHandle()) {
    emitThis();
    Arg = 1;
  }
  for (const cli::ManagedType &P : Call.Sig.Params) {
    emitArgument(Arg, P, Call.Conv.Params[Arg]);
    ++Arg;
  }
  emitAccess();
  emitResult();

  if (!HasCatch) {
    op(cli::Opcode::Ret, -int(ILSig.Results.size()));
    return;
  }

  // A CIL method never returns from inside a protected block.
  if (Locals.Result)
    stloc(*Locals.Result);
  branch(cli::Opcode::Leave, Done, 0);

  // The filter (MX2): the exception is on the stack; the first listed type
  // it is an instance of chooses the code, and 'endfilter' takes 1; with no
  // match it takes 0 and the exception keeps propagating.
  label(FilterBegin, 1);
  unsigned FilterEnd = newLabel();
  SmallVector<unsigned, 4> Match;
  for (const cli::ManagedType &T : Call.CatchTypes) {
    Match.push_back(newLabel());
    op(cli::Opcode::Dup, 1);
    type(cli::Opcode::Isinst, T, 0);
    branch(cli::Opcode::Brtrue_s, Match.back(), -1);
  }
  op(cli::Opcode::Pop, -1);
  ldc(0);
  branch(cli::Opcode::Br_s, FilterEnd, 0);
  for (unsigned K = 0, E = Match.size(); K != E; ++K) {
    label(Match[K], 1);
    op(cli::Opcode::Pop, -1);
    ldc(Call.Catches[K].Code);
    stloc(*Locals.Code);
    ldc(1);
    if (K + 1 != E)
      branch(cli::Opcode::Br_s, FilterEnd, 0);
  }
  label(FilterEnd, 1);
  op(cli::Opcode::Endfilter, -1);

  // The handler: *err = code, and the zero value of the C result.
  label(HandlerBegin, 1);
  op(cli::Opcode::Pop, -1);
  ldarg(NumParams - 1);
  ldloc(*Locals.Code);
  op(cli::Opcode::Stind_i4, -2);
  if (Locals.Result) {
    emitZero(ILSig.Results.front());
    stloc(*Locals.Result);
  }
  branch(cli::Opcode::Leave_s, Done, 0);

  // The tail, after the handler, which every 'leave' targets.
  label(Done, 0);
  if (Locals.Result)
    ldloc(*Locals.Result);
  op(cli::Opcode::Ret, -int(ILSig.Results.size()));
}

void StubWriter::emit(MSILTargetStreamer &TS, MCStreamer &OS,
                      const MCInstrInfo &MCII,
                      function_ref<void(const MCInst &)> EmitInst) {
  assert(Ctx && "a dry run emits nothing");
  // msil-ld never relaxes a branch (plan 5.8): the forms are chosen here,
  // as MSILMCInstLower::computeBranchForms chooses them for lowered code.
  SmallVector<std::pair<const MCSymbol *, unsigned>, 16> Positions;
  for (auto [L, Index] : LabelPos)
    Positions.push_back({Syms[L], Index});
  MSIL::relaxBranches(Insts, Positions, MCII);

  // The protected block ends where the filter starts; the handler ends at
  // the tail.
  if (Call.hasErrorParam())
    TS.emitExceptionClause(cli::ExceptionClauseKind::Filter, Syms[TryBegin],
                           Syms[FilterBegin], Syms[HandlerBegin], Syms[Done],
                           Syms[FilterBegin]);
  size_t NextLabel = 0;
  for (size_t I = 0, E = Insts.size(); I <= E; ++I) {
    for (; NextLabel != LabelPos.size() && LabelPos[NextLabel].second == I;
         ++NextLabel)
      OS.emitLabel(Syms[LabelPos[NextLabel].first]);
    if (I != E)
      EmitInst(Insts[I]);
  }
}

unsigned
MSIL::appendManagedCallStubLocals(const ManagedCall &Call,
                                  const Signature &ILSig,
                                  SmallVectorImpl<cli::TypeSig> &Locals) {
  unsigned First = Locals.size();
  StubLocals L = layoutLocals(Call, ILSig, First);
  for (const std::optional<unsigned> &Len : L.Length)
    if (Len)
      Locals.push_back(MSIL::toTypeSig(ValType::I32));
  if (L.Code)
    Locals.push_back(MSIL::toTypeSig(ValType::I32));
  if (L.Result)
    Locals.push_back(MSIL::toTypeSig(ILSig.Results.front()));
  return First;
}

void MSIL::emitManagedCallStubBody(
    const ManagedCall &Call, const Signature &ILSig, unsigned FirstLocal,
    unsigned FunctionNumber, bool TextChecks, MSILTargetStreamer &TS,
    MCStreamer &OS, MCContext &Ctx, const MCInstrInfo &MCII,
    function_ref<void(const MCInst &)> EmitInst) {
  // One IL value per C parameter and for a non-void result: every type that
  // parseManagedCall accepts is a scalar.
  if (ILSig.Params.size() != Call.Conv.Params.size() ||
      ILSig.Results.size() != size_t(Call.Conv.Result != ManagedConv::Void))
    reportFatalInternalError(Twine("MSIL: the IL signature of the "
                                   "managed-call stub of '") +
                             Call.Name +
                             "' does not take one value per parameter");
  StubWriter W(Call, ILSig, FirstLocal, &Ctx, FunctionNumber, TextChecks);
  W.build();
  W.emit(TS, OS, MCII, EmitInst);
}

unsigned MSIL::getManagedCallStubMaxStack(const Function &F, bool TextChecks) {
  if (!isManagedCallStub(F))
    return 0;
  Expected<ManagedCall> Call = parseManagedCall(F);
  if (!Call) {
    consumeError(Call.takeError());
    return 0;
  }
  // The depth does not depend on the IL types, only on the shape, so any
  // signature with one value per parameter and result measures it.
  Signature Shape;
  auto ValTypeOf = [](Type *T) {
    return T->isFloatTy()    ? ValType::F32
           : T->isDoubleTy() ? ValType::F64
                             : ValType::NativeInt;
  };
  for (Type *T : F.getFunctionType()->params())
    Shape.Params.push_back(ValTypeOf(T));
  if (!F.getReturnType()->isVoidTy())
    Shape.Results.push_back(ValTypeOf(F.getReturnType()));
  StubWriter W(*Call, Shape, 0, /*Ctx=*/nullptr, 0, TextChecks);
  W.build();
  return W.getMaxStack();
}
