//===--- CGMSILManagedCalls.cpp - MSIL managed calls from C/C++ -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The IR of the MSIL managed calls (ImplementationPlan.md 5.20.4): the IR name
// of a function with msil_method (MR2), its IR attributes (MR1), the
// forwarders of a managed call with dllexport, constructor or destructor, the
// check of the managed calls that share one IR name (T-M24), the declarations
// of the cast stubs that a C cast between two .NET classes calls (plan 5.21.9
// RM8) and the !msil.managed.calls operands (MR4). Sema has checked the
// attributes and recorded the conversions of each declaration
// (SemaMSILManagedCall.cpp); nothing here classifies a C type again.
//
//===----------------------------------------------------------------------===//

#include "CGMSILManagedCalls.h"
#include "CGMSILReferences.h"
#include "CodeGenModule.h"
#include "CodeGenTypes.h"
#include "TargetInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/Expr.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/Type.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/SourceManager.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/ManagedSignature.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <tuple>

using namespace clang;
using namespace CodeGen;

namespace {

/// The attributes that ask for a forwarder, numbered as T-M25's %select.
enum class ForwarderKind : unsigned { DLLExport, Constructor, Destructor };

/// One T-M24 report: the managed-call functions whose first written
/// declarations are \c First and \c Later share the IR name whose <s> is
/// \c Symbol, and differ in what T-M24's %select numbers \c Category.
struct SymbolClash {
  const FunctionDecl *First;
  const FunctionDecl *Later;
  StringRef Symbol;
  unsigned Category;
};

} // namespace

/// The first declaration of \p FD that the source wrote, as Sema's managed-call
/// checks count it (plan 5.20.3 MS2): the first declaration, or the one after
/// it when the first is a library builtin's implicit declaration. (msil_method
/// on the builtin itself is T-M12, but a redeclaration with another type does
/// not redeclare the builtin and may carry msil_method.)
static const FunctionDecl *getMSILFirstWrittenDecl(const FunctionDecl &FD) {
  const FunctionDecl *First = FD.getFirstDecl();
  if (!First->isImplicit() || !First->getBuiltinID())
    return First;
  for (const FunctionDecl *D = FD.getMostRecentDecl(); D;
       D = D->getPreviousDecl())
    if (D->getPreviousDecl() == First)
      return D;
  return First;
}

/// Whether \p FD is an MSIL managed-call function: a function whose first
/// written declaration carries msil_method (Sema refuses it on a later
/// declaration first, so the IR name it gives is stable, plan 5.20.3 MS2).
static bool isMSILManagedCall(const FunctionDecl &FD) {
  return getMSILFirstWrittenDecl(FD)->hasAttr<MSILMethodAttr>();
}

/// The msil_method attribute of the managed-call function \p FD: its most
/// recent declaration's, which every redeclaration inherits or repeats (MS2),
/// else its first written declaration's.
static const MSILMethodAttr *getMSILMethodAttr(const FunctionDecl &FD) {
  if (const auto *MA = FD.getMostRecentDecl()->getAttr<MSILMethodAttr>())
    return MA;
  return getMSILFirstWrittenDecl(FD)->getAttr<MSILMethodAttr>();
}

/// The latest declaration of the managed-call function \p FD, whose
/// dllexport, constructor and destructor attributes ask for the forwarders:
/// its most recent declaration that is not templated. A block-scope
/// redeclaration in a template pattern joins FD's redeclaration chain but
/// declares nothing until the template is instantiated, and its constructor
/// or destructor priority may depend on a template parameter; each
/// instantiation of it is a later redeclaration that is not templated. The
/// first written declaration, which has msil_method, is never templated
/// (T-M13).
static const FunctionDecl *getMSILLatestDecl(const FunctionDecl &FD) {
  for (const FunctionDecl *D = FD.getMostRecentDecl(); D;
       D = D->getPreviousDecl())
    if (!D->isTemplated())
      return D;
  return FD.getMostRecentDecl();
}

/// Whether the declaration \p D has an attribute that asks for a forwarder
/// of its managed call: dllexport, constructor or destructor.
static bool hasMSILForwarderAttr(const FunctionDecl &D) {
  return D.hasAttr<DLLExportAttr>() || D.hasAttr<ConstructorAttr>() ||
         D.hasAttr<DestructorAttr>();
}

/// The text of the msil_catch list of the managed-call function whose most
/// recent declaration is \p Latest (llvm::cli::printManagedCatchList), or ""
/// without msil_catch.
static std::string getMSILManagedCatchList(const FunctionDecl &Latest) {
  SmallVector<llvm::cli::ManagedCatch, 4> Catches;
  for (const auto *CA : Latest.specific_attrs<MSILCatchAttr>())
    Catches.push_back({CA->getExceptionType().trim().str(), CA->getCode()});
  if (Catches.empty())
    return std::string();
  return llvm::cli::printManagedCatchList(Catches);
}

/// The function type of the managed-call function \p FD as C types:
/// '<result>(<parameter>, ...)', each one the canonical, unqualified C type,
/// an enumeration replaced by its underlying integer type (an enumeration's
/// IR type and attributes follow it, and two units may choose it differently
/// for one tag). T-M24 compares this text (getMSILManagedCallDifference). The
/// IR name of an internal declaration hashes it together with the C ABI of
/// the function (getMSILManagedCallABIText), which the text does not always
/// determine: a plain 'char' is 'char' here whether or not -funsigned-char
/// makes it unsigned.
static std::string getMSILManagedCallTypeText(const ASTContext &Ctx,
                                              const FunctionDecl &FD) {
  PrintingPolicy Policy = Ctx.getPrintingPolicy();
  Policy.AnonymousTagNameStyle =
      llvm::to_underlying(PrintingPolicy::AnonymousTagMode::Plain);
  // 'bool' in every C dialect, so that C17 and C23 units agree.
  Policy.Bool = true;
  auto Text = [&](QualType T) {
    T = Ctx.getCanonicalType(T).getUnqualifiedType();
    if (const auto *ET = T->getAs<EnumType>())
      if (QualType U = ET->getDecl()->getIntegerType(); !U.isNull())
        T = Ctx.getCanonicalType(U).getUnqualifiedType();
    return T.getAsString(Policy);
  };
  const auto *FPT = FD.getType()->getAs<FunctionProtoType>();
  if (!FPT)
    return Text(FD.getReturnType()) + "()";
  std::string S = Text(FPT->getReturnType()) + "(";
  for (unsigned I = 0, E = FPT->getNumParams(); I != E; ++I) {
    if (I)
      S += ", ";
    S += Text(FPT->getParamType(I));
  }
  S += ")";
  return S;
}

/// The C ABI of the managed-call function \p FD as the IR name of an internal
/// declaration hashes it (getMSILManagedCallUniqPostfix):
/// '<result>(<parameter>, ...)', each one the IR type that the target's C ABI
/// gives it (CodeGenTypes::arrangeGlobalDeclaration, from which the IR
/// declaration takes its types and attributes), after 'signext ' or
/// 'zeroext ' when the ABI extends it, for example 'signext i8(i32)'.
/// MSIL::createManagedCallStubs makes the stub from the IR declaration, whose
/// types and extensions the C types (getMSILManagedCallTypeText) do not
/// always determine: a plain 'char' result is 'signext i8', or 'zeroext i8'
/// with -funsigned-char, on msil64-unknown-dotnet, and a C++ 'wchar_t'
/// parameter is 'i16', or 'i32' with -fno-short-wchar, on
/// x86_64-pc-windows-msvc-msil. Arranging the function type computes no IR
/// name, so getMangledName does not recurse. Returns "" for a function type
/// that CodeGen cannot arrange, which only a declaration that Sema refused
/// has: a C++ member function or a dependent type (T-M13), or a type that is
/// incomplete at this point (T-M3).
static std::string getMSILManagedCallABIText(CodeGenModule &CGM,
                                             const FunctionDecl &FD) {
  CodeGenTypes &Types = CGM.getTypes();
  if (isa<CXXMethodDecl>(FD) || FD.getType()->isDependentType() ||
      !Types.isFuncTypeConvertible(FD.getType()->castAs<FunctionType>()))
    return std::string();
  const CGFunctionInfo &FI = Types.arrangeGlobalDeclaration(GlobalDecl(&FD));
  std::string S;
  llvm::raw_string_ostream OS(S);
  // A scalar or a pointer is Direct or Extend, with its IR type as the
  // coerced type, and a void result is Ignore. Only a record (T-M3) has
  // another kind, written as its coerced type or, without one, its number.
  auto Print = [&](const ABIArgInfo &AI) {
    if (AI.isExtend() && AI.isSignExt())
      OS << "signext ";
    else if (AI.isExtend() && AI.isZeroExt())
      OS << "zeroext ";
    if (AI.canHaveCoerceToType() && AI.getCoerceToType())
      AI.getCoerceToType()->print(OS, /*IsForDebug=*/false,
                                  /*NoDetails=*/true);
    else if (AI.isIgnore())
      OS << "void";
    else
      OS << "kind" << static_cast<unsigned>(AI.getKind());
  };
  Print(FI.getReturnInfo());
  OS << '(';
  llvm::ListSeparator Sep;
  for (const CGFunctionInfoArgInfo &Arg : FI.arguments()) {
    OS << Sep;
    Print(Arg.info);
  }
  OS << ')';
  return S;
}

/// The postfix of the IR name of the managed-call function \p FD with
/// internal linkage (plan 5.20.4 MR2 as amended):
/// llvm::cli::getManagedCallUniqPostfix of the strings of its
/// !msil.managed.calls operands 1-6 (TYPE, MEMBER, SIGNATURE, ASSEMBLY or "",
/// the conversions, the msil_catch list or "") and the function type of its
/// first written declaration, whichever redeclaration is used first: its C
/// types (getMSILManagedCallTypeText), "; " and its C ABI, the IR type and
/// extension of the result and of each parameter
/// (getMSILManagedCallABIText), for example 'char(int); signext i8(i32)'.
/// The stub is made from those operands and the IR declaration's types and
/// extensions, so two internal declarations share an IR name only when their
/// stubs are the same. All of them are known when the name is first computed:
/// msil_method and msil_catch are on the first declaration (T-M14), and Sema
/// records the conversions there.
static std::string getMSILManagedCallUniqPostfix(CodeGenModule &CGM,
                                                 const FunctionDecl &FD) {
  const MSILMethodAttr *MA = getMSILMethodAttr(FD);
  std::string Catches = getMSILManagedCatchList(*FD.getMostRecentDecl());
  const FunctionDecl &First = *getMSILFirstWrittenDecl(FD);
  std::string Type = getMSILManagedCallTypeText(CGM.getContext(), First) +
                     "; " + getMSILManagedCallABIText(CGM, First);
  return llvm::cli::getManagedCallUniqPostfix(
      {MA->getType(), MA->getMember(), MA->getSignature(), MA->getAssembly(),
       MA->getConversions(), Catches, Type});
}

void CodeGen::mangleMSILManagedCallName(CodeGenModule &CGM, GlobalDecl GD,
                                        std::string &MangledName) {
  const auto *FD = dyn_cast<FunctionDecl>(GD.getDecl());
  if (!FD || !isMSILManagedCall(*FD))
    return;
  StringRef Mangled = MangledName;
  Mangled.consume_front("\01");
  std::string Symbol = Mangled.str();
  bool Internal =
      CGM.getFunctionLinkage(GD) == llvm::GlobalValue::InternalLinkage;
  if (const std::string &Hash = CGM.getModuleNameHash();
      Internal && !Hash.empty())
    if (size_t Pos = Symbol.find(Hash); Pos != std::string::npos)
      Symbol.erase(Pos, Hash.size());
  std::string Name = (llvm::cli::ManagedCallPrefix + Symbol).str();
  if (Internal)
    Name += getMSILManagedCallUniqPostfix(CGM, *FD);
  MangledName = std::move(Name);
}

/// Gives the IR declaration \p F of a managed call the attributes of plan
/// 5.20.4 MR1: TYPE, MEMBER, SIGNATURE, ASSEMBLY when it is not empty, the
/// conversions, and the msil_catch list when it is not empty, which
/// emitMSILManagedCallsMetadata copies into the declaration's
/// !msil.managed.calls operand (MR4).
static void setMSILManagedCallAttributes(llvm::Function &F, StringRef Type,
                                         StringRef Member, StringRef Signature,
                                         StringRef Assembly,
                                         StringRef Conversions,
                                         StringRef Catches) {
  F.addFnAttr(llvm::cli::ManagedTypeAttr, Type);
  F.addFnAttr(llvm::cli::ManagedMemberAttr, Member);
  F.addFnAttr(llvm::cli::ManagedSigAttr, Signature);
  if (!Assembly.empty())
    F.addFnAttr(llvm::cli::ManagedAsmAttr, Assembly);
  F.addFnAttr(llvm::cli::ManagedConvAttr, Conversions);
  if (!Catches.empty())
    F.addFnAttr(llvm::cli::ManagedCatchAttr, Catches);
}

void CodeGen::addMSILManagedCallAttributes(const FunctionDecl &FD,
                                           llvm::Function &F) {
  if (FD.doesThisDeclarationHaveABody() || !isMSILManagedCall(FD))
    return;
  const FunctionDecl *Latest = FD.getMostRecentDecl();
  const auto *MA = Latest->getAttr<MSILMethodAttr>();
  if (!MA || MA->getConversions().empty())
    return;
  setMSILManagedCallAttributes(
      F, MA->getType(), MA->getMember(), MA->getSignature(), MA->getAssembly(),
      MA->getConversions(), getMSILManagedCatchList(*Latest));
}

llvm::Function *
CodeGen::getOrCreateMSILCastDeclaration(CodeGenModule &CGM,
                                        const CLIReferenceType &Target) {
  // TYPE is the class's display name (plan 5.21.3 RC4): a class, an
  // interface, System.String or an array, never System.Object, to which a
  // cast emits nothing (emitMSILReferenceCast).
  std::string Type =
      llvm::cli::getCLITypeDisplayName(Target.getKey()->getName());
  assert(llvm::cli::isManagedCastShape(Type, llvm::cli::ManagedCastMember,
                                       llvm::cli::ManagedCastSignature,
                                       llvm::cli::ManagedCastConversions) &&
         "a cast stub needs a class other than System.Object");
  llvm::Module &M = CGM.getModule();
  llvm::Type *Ref = getMSILReferenceType(M.getContext());
  auto *FTy = llvm::FunctionType::get(Ref, {Ref}, /*isVarArg=*/false);
  // The name hashes what an internal managed call's does (MR2 as amended):
  // the !msil.managed.calls operands 1-6 and the function type, here the IR
  // type, which every cast stub shares. So each class has its own stub, and
  // the equal casts of two units have one name, which LTO merges.
  std::string FunctionType;
  llvm::raw_string_ostream OS(FunctionType);
  FTy->print(OS);
  std::string Name =
      (llvm::cli::ManagedCallPrefix + llvm::cli::ManagedCastSymbol).str() +
      llvm::cli::getManagedCallUniqPostfix(
          {Type, llvm::cli::ManagedCastMember, llvm::cli::ManagedCastSignature,
           /*Assembly=*/"", llvm::cli::ManagedCastConversions, /*Catches=*/"",
           FunctionType});
  if (llvm::Function *F = M.getFunction(Name);
      F && F->isDeclaration() && F->getFunctionType() == FTy &&
      F->hasFnAttribute(llvm::cli::ManagedTypeAttr))
    return F;
  // Sema refuses an asm label that would give a declaration of the source this
  // name (T-M22). Should another global have it anyway, this declaration is
  // renamed, which changes nothing: the backend and msil-ld know a cast stub
  // by its attributes (llvm::cli::isManagedCastShape). Without memory
  // attributes, a cast whose value is unused still runs and throws.
  auto *F =
      llvm::Function::Create(FTy, llvm::GlobalValue::ExternalLinkage, Name, M);
  setMSILManagedCallAttributes(
      *F, Type, llvm::cli::ManagedCastMember, llvm::cli::ManagedCastSignature,
      /*Assembly=*/"", llvm::cli::ManagedCastConversions,
      /*Catches=*/"");
  return F;
}

void CodeGen::declareMSILManagedCallWithForwarders(CodeGenModule &CGM,
                                                   GlobalDecl GD) {
  const auto *FD = dyn_cast<FunctionDecl>(GD.getDecl());
  if (!FD || FD->doesThisDeclarationHaveABody() || !isMSILManagedCall(*FD))
    return;
  if (hasMSILForwarderAttr(*FD))
    CGM.GetAddrOfFunction(GD);
}

/// <s> of the managed call whose IR name is \p IRName: an external
/// declaration's IR name is the prefix and <s> exactly, an internal one's
/// (\p Internal) ends with a '.__uniq.<n>' postfix as well.
static StringRef getMSILManagedCallSymbol(StringRef IRName, bool Internal) {
  if (Internal)
    return llvm::cli::getManagedCallSymbol(IRName);
  IRName.consume_front(llvm::cli::ManagedCallPrefix);
  return IRName;
}

/// The priority of the constructor or destructor attribute \p A: its
/// argument, or 65535 when none is given.
template <typename StartupAttr>
static int getMSILStartupPriority(const ASTContext &Ctx, const StartupAttr &A) {
  if (const Expr *E = A.getPriority())
    return E->EvaluateKnownConstInt(Ctx).getExtValue();
  return A.DefaultPriority;
}

/// The attributes operand of the managed-call function \p FD (plan 5.20.4 MR4
/// as amended): llvm::cli::printManagedCallAttributes of the dllexport,
/// constructor and destructor attributes of its latest declaration
/// (getMSILLatestDecl), "dllexport", "constructor(<priority>)" and
/// "destructor(<priority>)" in that order and separated by ',', or "" without
/// any of them. msil-ld spells a CIL object's forwarders with the same
/// function, to compare them with this operand.
static std::string getMSILManagedCallAttributesText(const ASTContext &Ctx,
                                                    const FunctionDecl &FD) {
  const FunctionDecl *Latest = getMSILLatestDecl(FD);
  // The effective priority, an unsigned 32-bit value as Sema checks it.
  auto Priority = [&](const auto *A) -> std::optional<uint32_t> {
    if (!A)
      return std::nullopt;
    return static_cast<uint32_t>(getMSILStartupPriority(Ctx, *A));
  };
  return llvm::cli::printManagedCallAttributes(
      Latest->hasAttr<DLLExportAttr>(),
      Priority(Latest->getAttr<ConstructorAttr>()),
      Priority(Latest->getAttr<DestructorAttr>()));
}

/// T-M24's %select for the first difference between the managed-call
/// functions \p A and \p B: 0 for their msil_method or msil_catch arguments,
/// 1 for their function types (getMSILManagedCallTypeText), 0 again for their
/// conversions, 2 for their attributes operand
/// (getMSILManagedCallAttributesText); std::nullopt when they agree. The
/// conversions follow from the arguments and the C types, so they are
/// compared after both: a function type that differs is reported as such even
/// when it changes a conversion, and with equal arguments and types only a
/// pointer to a struct that is a handle at one declaration alone (completed
/// between them) tells the conversions apart.
static std::optional<unsigned>
getMSILManagedCallDifference(const ASTContext &Ctx, const FunctionDecl &A,
                             const FunctionDecl &B) {
  const MSILMethodAttr &MA = *getMSILMethodAttr(A);
  const MSILMethodAttr &MB = *getMSILMethodAttr(B);
  auto Written = [](const MSILMethodAttr &M, const FunctionDecl &FD) {
    return std::make_tuple(M.getType(), M.getMember(), M.getSignature(),
                           M.getAssembly(),
                           getMSILManagedCatchList(*FD.getMostRecentDecl()));
  };
  if (Written(MA, A) != Written(MB, B))
    return 0;
  if (getMSILManagedCallTypeText(Ctx, *getMSILFirstWrittenDecl(A)) !=
      getMSILManagedCallTypeText(Ctx, *getMSILFirstWrittenDecl(B)))
    return 1;
  if (MA.getConversions() != MB.getConversions())
    return 0;
  if (getMSILManagedCallAttributesText(Ctx, A) !=
      getMSILManagedCallAttributesText(Ctx, B))
    return 2;
  return std::nullopt;
}

/// T-M24 (plan 5.20.4 MR2 as amended): the managed-call functions that share
/// one IR name, as asm labels, '#pragma redefine_extname' or a C++ asm label
/// equal to a mangled name let them, are one IR declaration with one set of
/// forwarders, so they must agree in their !msil.managed.calls operands 1-6,
/// in their function types and in their attributes operand
/// (getMSILManagedCallDifference). \p MangledDeclNames lists every function
/// that the translation unit used and every managed call whose latest
/// declaration has dllexport, constructor or destructor, one declared only at
/// block scope included (MSILForwardedCallFinder), so the check runs once, at
/// the end of the unit, on the latest declaration of each function, which
/// includes a later or block-scope redeclaration that adds one of the three
/// after a use. The functions of one IR name are ordered by their
/// first written declarations, and each one that differs from the first is
/// reported once, at its first written declaration, with a note at the
/// first's. The reports are in source order, so that the text does not depend
/// on which function a use named first. A function without recorded
/// conversions (Sema reported why) takes no part. Returns, by IR name, the
/// first written of the functions, whose declarations give the forwarders.
static llvm::StringMap<GlobalDecl> checkMSILManagedCallSymbolClashes(
    CodeGenModule &CGM,
    const llvm::MapVector<GlobalDecl, StringRef> &MangledDeclNames) {
  llvm::StringMap<SmallVector<GlobalDecl, 1>> Groups;
  for (const auto &[GD, Name] : MangledDeclNames) {
    if (!llvm::cli::isManagedCallName(Name))
      continue;
    const auto *FD = dyn_cast<FunctionDecl>(GD.getDecl());
    if (!FD || !isMSILManagedCall(*FD))
      continue;
    const auto *MA = FD->getMostRecentDecl()->getAttr<MSILMethodAttr>();
    if (MA && !MA->getConversions().empty())
      Groups[Name].push_back(GD);
  }

  ASTContext &Ctx = CGM.getContext();
  const SourceManager &SM = Ctx.getSourceManager();
  auto FirstWritten = [](GlobalDecl GD) {
    return getMSILFirstWrittenDecl(*cast<FunctionDecl>(GD.getDecl()));
  };
  auto Before = [&](const FunctionDecl *A, const FunctionDecl *B) {
    return SM.isBeforeInTranslationUnit(A->getLocation(), B->getLocation());
  };
  llvm::StringMap<GlobalDecl> Firsts;
  SmallVector<SymbolClash, 0> Clashes;
  for (auto &Group : Groups) {
    SmallVector<GlobalDecl, 1> &Fns = Group.getValue();
    llvm::stable_sort(Fns, [&](GlobalDecl A, GlobalDecl B) {
      return Before(FirstWritten(A), FirstWritten(B));
    });
    GlobalDecl First = Fns.front();
    Firsts[Group.getKey()] = First;
    for (GlobalDecl GD : llvm::drop_begin(Fns)) {
      std::optional<unsigned> Category = getMSILManagedCallDifference(
          Ctx, *cast<FunctionDecl>(First.getDecl()),
          *cast<FunctionDecl>(GD.getDecl()));
      if (!Category)
        continue;
      bool Internal =
          CGM.getFunctionLinkage(GD) == llvm::GlobalValue::InternalLinkage;
      Clashes.push_back({FirstWritten(First), FirstWritten(GD),
                         getMSILManagedCallSymbol(Group.getKey(), Internal),
                         *Category});
    }
  }
  llvm::stable_sort(Clashes, [&](const SymbolClash &A, const SymbolClash &B) {
    return Before(A.Later, B.Later);
  });
  for (const SymbolClash &C : Clashes) {
    CGM.getDiags().Report(C.Later->getLocation(),
                          diag::err_msil_method_symbol_clash)
        << C.First << C.Later << C.Symbol << C.Category;
    CGM.getDiags().Report(C.First->getLocation(), diag::note_previous_decl)
        << C.First;
  }
  return Firsts;
}

/// Defines the forwarder \p Name of the managed-call declaration \p F, which
/// the attribute \p Kind, written at \p Loc, of the managed-call function
/// whose latest declaration (getMSILLatestDecl) is \p Latest asks for, with
/// linkage \p Linkage, returning F's result or, when \p ReturnsVoid, nothing:
/// a function with F's parameters and attributes, the managed-call ones (MR1)
/// aside, whose body calls F with all its arguments. A function of that name
/// that the translation unit only declares is replaced by the forwarder; a
/// variable or a definition of that name is T-M25, reported at \p Loc, and
/// gives no forwarder (null).
static llvm::Function *
createMSILForwarder(CodeGenModule &CGM, llvm::Function &F, StringRef Name,
                    llvm::GlobalValue::LinkageTypes Linkage, bool ReturnsVoid,
                    const FunctionDecl &Latest, ForwarderKind Kind,
                    SourceLocation Loc) {
  llvm::Module &M = CGM.getModule();
  llvm::LLVMContext &Ctx = M.getContext();
  llvm::GlobalValue *Existing = M.getNamedValue(Name);
  if (Existing &&
      (!isa<llvm::Function>(Existing) || !Existing->isDeclaration())) {
    CGM.getDiags().Report(Loc, diag::err_msil_method_forwarder_clash)
        << llvm::to_underlying(Kind) << &Latest << Name;
    GlobalDecl OtherGD;
    if (CGM.lookupRepresentativeDecl(Name, OtherGD)) {
      const auto *Other = cast<NamedDecl>(OtherGD.getDecl());
      CGM.getDiags().Report(Other->getLocation(), diag::note_previous_decl)
          << Other;
    }
    return nullptr;
  }
  llvm::FunctionType *CalleeTy = F.getFunctionType();
  llvm::FunctionType *Ty =
      ReturnsVoid ? llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx),
                                            CalleeTy->params(), false)
                  : CalleeTy;
  auto *Fwd = llvm::Function::Create(Ty, Linkage, F.getAddressSpace(), "", &M);
  if (Existing) {
    Existing->replaceAllUsesWith(Fwd);
    Fwd->takeName(Existing);
    cast<llvm::Function>(Existing)->eraseFromParent();
  } else {
    Fwd->setName(Name);
  }
  Fwd->setCallingConv(F.getCallingConv());

  // F's attributes are those of a C function with its prototype
  // (SetLLVMFunctionAttributes) plus MR1's.
  llvm::AttributeList Attrs = F.getAttributes();
  for (StringRef Key :
       {llvm::cli::ManagedTypeAttr, llvm::cli::ManagedMemberAttr,
        llvm::cli::ManagedSigAttr, llvm::cli::ManagedAsmAttr,
        llvm::cli::ManagedConvAttr, llvm::cli::ManagedCatchAttr})
    Attrs = Attrs.removeFnAttribute(Ctx, Key);
  if (ReturnsVoid)
    Attrs =
        Attrs.removeAttributesAtIndex(Ctx, llvm::AttributeList::ReturnIndex);
  Fwd->setAttributes(Attrs);

  auto *BB = llvm::BasicBlock::Create(Ctx, "entry", Fwd);
  SmallVector<llvm::Value *, 4> Args;
  for (llvm::Argument &A : Fwd->args())
    Args.push_back(&A);
  llvm::CallInst *Call = llvm::CallInst::Create(CalleeTy, &F, Args, "", BB);
  Call->setCallingConv(F.getCallingConv());
  // The call carries F's result and parameter attributes, as a C call does.
  SmallVector<llvm::AttributeSet, 4> ParamAttrs;
  for (unsigned I = 0, E = F.arg_size(); I != E; ++I)
    ParamAttrs.push_back(F.getAttributes().getParamAttrs(I));
  Call->setAttributes(llvm::AttributeList::get(
      Ctx, llvm::AttributeSet(), F.getAttributes().getRetAttrs(), ParamAttrs));
  if (ReturnsVoid || CalleeTy->getReturnType()->isVoidTy())
    llvm::ReturnInst::Create(Ctx, BB);
  else
    llvm::ReturnInst::Create(Ctx, Call, BB);

  // A definition's attributes once it has its body, as
  // EmitGlobalFunctionDefinition adds them: a target's setTargetAttributes
  // skips a declaration.
  CGM.SetLLVMFunctionAttributesForDefinition(&Latest, Fwd);
  CGM.getTargetCodeGenInfo().setTargetAttributes(&Latest, Fwd, CGM);
  return Fwd;
}

namespace {

/// The walk that emitMSILManagedCallForwarders begins with. It visits, in
/// source order, the declarations that the translation unit wrote: at
/// namespace scope and in 'extern "C"' and 'export' blocks, and at block
/// scope in every function definition (a 'static' function whose body is
/// never emitted, a C++ member function defined in its class and a friend
/// function defined in a class included), block, captured statement, lambda
/// and local class. Each managed call whose latest declaration
/// (getMSILLatestDecl) has dllexport, constructor or destructor gets its IR
/// declaration, as a call would give it, so that T-M24 and the forwarders see
/// it also when nothing calls it and no declaration with the attribute
/// reached EmitGlobal: CodeGen emits nothing for a block-scope function
/// declaration (CodeGenFunction::EmitDecl), and EmitGlobal sees a file-scope
/// declaration before a later one adds the attribute. Any one declaration of
/// a managed call is enough, since the latest one is read. Template patterns
/// are skipped. Each block-scope declaration in an instantiation is in no
/// declaration list, but it redeclares a managed call whose first written
/// declaration (never templated, T-M13) the walk reaches. The walk reads
/// noload_decls(), which deserializes nothing from a PCH or a module: this
/// runs after EmitDeferred, too late for a definition that deserializing
/// would hand to the AST consumer. A managed call with one of the three that a
/// PCH or a header module declares needs no walk: the AST file lists its
/// declaration among the eagerly deserialized ones, and the AST reader passes
/// it to the consumer, so it reached EmitGlobal
/// (declareMSILManagedCallWithForwarders) as the unit loaded that file
/// (clang/lib/Serialization/MSILManagedCallDecls.h). A C++20 named module lists
/// it only for the module unit's own object, which therefore defines the
/// forwarders, and an importing unit that names the managed call deserializes
/// its declaration, which the AST reader passes to the consumer too
/// (emitMSILManagedCallForwarders).
class MSILForwardedCallFinder {
public:
  explicit MSILForwardedCallFinder(CodeGenModule &CGM) : CGM(CGM) {}

  void visitContext(const DeclContext &DC) {
    for (const Decl *D : DC.noload_decls())
      visit(*D);
  }

private:
  void visit(const Decl &D);
  void visitFunction(const FunctionDecl &FD);

  CodeGenModule &CGM;
  /// The canonical declarations of the functions already looked at.
  llvm::SmallPtrSet<const FunctionDecl *, 16> Seen;
};

} // namespace

void MSILForwardedCallFinder::visitFunction(const FunctionDecl &FD) {
  if (FD.isTemplated())
    return;
  if (Seen.insert(FD.getCanonicalDecl()).second && isMSILManagedCall(FD)) {
    const FunctionDecl *Latest = getMSILLatestDecl(FD);
    if (hasMSILForwarderAttr(*Latest))
      CGM.GetAddrOfFunction(GlobalDecl(Latest));
  }
  if (FD.doesThisDeclarationHaveABody())
    visitContext(FD);
}

void MSILForwardedCallFinder::visit(const Decl &D) {
  if (const auto *FD = dyn_cast<FunctionDecl>(&D)) {
    visitFunction(*FD);
  } else if (const auto *Friend = dyn_cast<FriendDecl>(&D)) {
    // A friend function is in its class's declarations only through its
    // FriendDecl.
    if (const auto *FD =
            dyn_cast_or_null<FunctionDecl>(Friend->getFriendDecl()))
      visitFunction(*FD);
  } else if (const auto *RD = dyn_cast<CXXRecordDecl>(&D)) {
    // Member functions, a lambda's call operator, local classes. A class
    // template's pattern and a partial specialization are dependent.
    if (!RD->isDependentContext())
      visitContext(*RD);
  } else if (isa<NamespaceDecl, LinkageSpecDecl, ExportDecl, BlockDecl,
                 CapturedDecl, TopLevelStmtDecl>(D)) {
    visitContext(*cast<DeclContext>(&D));
  }
}

void CodeGen::emitMSILManagedCallForwarders(
    CodeGenModule &CGM,
    const llvm::MapVector<GlobalDecl, StringRef> &MangledDeclNames,
    llvm::StringMap<std::string> &Operands) {
  MSILForwardedCallFinder(CGM).visitContext(
      *CGM.getContext().getTranslationUnitDecl());
  llvm::StringMap<GlobalDecl> Firsts =
      checkMSILManagedCallSymbolClashes(CGM, MangledDeclNames);
  SmallVector<llvm::Function *, 8> Calls;
  for (llvm::Function &F : CGM.getModule())
    if (F.isDeclaration() && F.hasFnAttribute(llvm::cli::ManagedTypeAttr))
      Calls.push_back(&F);
  ASTContext &Ctx = CGM.getContext();
  for (llvm::Function *F : Calls) {
    GlobalDecl GD = Firsts.lookup(F->getName());
    if (!GD.getDecl() && !CGM.lookupRepresentativeDecl(F->getName(), GD))
      continue;
    const auto *FD = dyn_cast<FunctionDecl>(GD.getDecl());
    if (!FD)
      continue;
    const FunctionDecl *Latest = getMSILLatestDecl(*FD);
    const auto *Export = Latest->getAttr<DLLExportAttr>();
    const auto *Ctor = Latest->getAttr<ConstructorAttr>();
    const auto *Dtor = Latest->getAttr<DestructorAttr>();
    if (!Export && !Ctor && !Dtor)
      continue;
    bool Internal =
        CGM.getFunctionLinkage(GD) == llvm::GlobalValue::InternalLinkage;
    StringRef Symbol = getMSILManagedCallSymbol(F->getName(), Internal);

    // Sema refuses dllexport with internal linkage.
    if (Export && !Internal) {
      if (llvm::Function *Fwd = createMSILForwarder(
              CGM, *F, Symbol, llvm::GlobalValue::WeakODRLinkage,
              /*ReturnsVoid=*/false, *Latest, ForwarderKind::DLLExport,
              Export->getLocation())) {
        Fwd->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
        Fwd->setComdat(CGM.getModule().getOrInsertComdat(Fwd->getName()));
        Fwd->addFnAttr(llvm::cli::ManagedForwarderAttr, Symbol);
      }
    }
    auto Register = [&](const Attr *A, ForwarderKind Kind, StringRef Prefix,
                        int Priority) {
      llvm::Function *Fwd = createMSILForwarder(
          CGM, *F, (Prefix + Symbol).str(),
          Internal ? llvm::GlobalValue::InternalLinkage
                   : llvm::GlobalValue::LinkOnceODRLinkage,
          /*ReturnsVoid=*/true, *Latest, Kind, A->getLocation());
      if (!Fwd)
        return;
      llvm::Constant *Key = nullptr;
      if (!Internal) {
        Fwd->setVisibility(llvm::GlobalValue::HiddenVisibility);
        Fwd->setComdat(CGM.getModule().getOrInsertComdat(Fwd->getName()));
        Key = Fwd;
      }
      Fwd->addFnAttr(llvm::cli::ManagedForwarderAttr, Symbol);
      if (Kind == ForwarderKind::Constructor) {
        CGM.AddGlobalCtor(Fwd, Priority, ~0U, Key);
        return;
      }
      // Not AddGlobalDtor, which takes no key and, under
      // -fregister-global-dtors-with-atexit, registers through atexit.
      CGM.getGlobalDtors().push_back(
          CodeGenModule::Structor(Priority, ~0U, Fwd, Key));
    };
    if (Ctor)
      Register(Ctor, ForwarderKind::Constructor,
               llvm::cli::ManagedCtorForwarderPrefix,
               getMSILStartupPriority(Ctx, *Ctor));
    if (Dtor)
      Register(Dtor, ForwarderKind::Destructor,
               llvm::cli::ManagedDtorForwarderPrefix,
               getMSILStartupPriority(Ctx, *Dtor));
    if (!Internal)
      Operands[F->getName()] = getMSILManagedCallAttributesText(Ctx, *Latest);
  }
}

void CodeGen::emitMSILManagedCallsMetadata(
    llvm::Module &M, const llvm::StringMap<std::string> &Forwarded) {
  llvm::LLVMContext &Ctx = M.getContext();
  llvm::NamedMDNode *Calls = nullptr;
  for (llvm::Function &F : M) {
    if (!F.isDeclaration() || !F.hasFnAttribute(llvm::cli::ManagedTypeAttr))
      continue;
    auto Text = [&](StringRef Key) -> llvm::Metadata * {
      StringRef Value;
      if (F.hasFnAttribute(Key))
        Value = F.getFnAttribute(Key).getValueAsString();
      return llvm::MDString::get(Ctx, Value);
    };
    SmallVector<llvm::Metadata *, 8> Ops = {
        llvm::MDString::get(Ctx, F.getName()), Text(llvm::cli::ManagedTypeAttr),
        Text(llvm::cli::ManagedMemberAttr),    Text(llvm::cli::ManagedSigAttr),
        Text(llvm::cli::ManagedAsmAttr),       Text(llvm::cli::ManagedConvAttr),
        Text(llvm::cli::ManagedCatchAttr),
    };
    auto It = Forwarded.find(F.getName());
    if (It != Forwarded.end())
      Ops.push_back(llvm::MDString::get(Ctx, It->second));
    if (!Calls)
      Calls = M.getOrInsertNamedMetadata(llvm::cli::ManagedCallsMetadata);
    Calls->addOperand(llvm::MDNode::get(Ctx, Ops));
  }
}
