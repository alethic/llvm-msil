//===--- SemaMSILReference.cpp - .NET references in C (MSIL) --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The Sema rules of the .NET references of C (ImplementationPlan.md 5.21.2 to
// 5.21.4) on the MSIL object-format triples. A struct with
// __attribute__((cli_type(NAME))) stands for the .NET class NAME; 'T ^' of such
// a struct is a .NET reference (CLIReferenceType), a value that lives only in
// IL locals, arguments, results and the evaluation stack, never in C memory.
// Allowed: automatic variables, which start as NULL; parameters and results
// of static, prototyped, non-variadic functions whose address is never taken,
// and of managed calls; '=', '==', '!=', NULL, the truth tests, '?:', the
// implicit conversion to the System.Object reference and C casts between
// classes, which the program checks when it runs. Everything else that could
// put a reference into C memory or lose track of it is a loud error.
//
// Each upstream function calls one entry point of SemaMSILReference.h through
// a one-line hook; the rules that need no logic of their own are written in
// their hook with Type::isCLIReferenceType. The texts are the err_msil_ref_*
// and note_msil_ref_* diagnostics (plan 5.21.12); "yet" marks what a planned
// step may lift.
//
//===----------------------------------------------------------------------===//

#include "SemaMSILReference.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DynamicRecursiveASTVisitor.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/CapturedStmt.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CLI/ManagedSignature.h"
#include "llvm/Support/Error.h"
#include <string>

using namespace clang;
namespace cli = llvm::cli;

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

/// The key of System.Object (cli::getCLITypeKey of 'object'), the one class
/// that every reference converts to by itself (plan 5.21.4 RS4, RS5, RS7).
static constexpr llvm::StringLiteral ObjectKey = "object";

/// Whether the target is an MSIL object-format triple, the only triples on
/// which 'cli_type' exists and references can be formed (plan 5.21.1 RA4).
static bool isMSILTarget(const ASTContext &Ctx) {
  return Ctx.getTargetInfo().getTriple().isOSBinFormatMSIL();
}

/// The 'cli_type' attribute of the struct \p TD: its own, or the one that a
/// declaration of it inherits. The first declaration of the struct carries
/// it (RT4); the implicit struct of a canonical reference type carries the
/// implicit one that ASTContext::getCLIReferenceType attaches.
static const CLITypeAttr *getCLITypeAttrOf(const TagDecl *TD) {
  for (const TagDecl *R : TD->redecls())
    if (const auto *A = R->getAttr<CLITypeAttr>())
      return A;
  return nullptr;
}

/// The class \p T, a struct with 'cli_type', as the texts that quote a C
/// spelling print it (T-R6, T-R7, T-R19): the type as written without its
/// own qualifiers, or the struct itself when the qualifiers come from a
/// typedef.
static std::string getClassSpelling(const ASTContext &Ctx, QualType T) {
  QualType Unqualified = T.getLocalUnqualifiedType();
  if (Unqualified.hasQualifiers())
    Unqualified = Ctx.getCanonicalType(T).getUnqualifiedType();
  return Unqualified.getAsString(Ctx.getPrintingPolicy());
}

/// Whether the result or a parameter of the function type \p T is a .NET
/// reference (only that level: a reference inside a parameter's function
/// type is refused where that type is formed).
static bool hasCLIReferenceInSignature(QualType T) {
  const auto *FT = T->getAs<FunctionType>();
  if (!FT)
    return false;
  if (FT->getReturnType()->isCLIReferenceType())
    return true;
  if (const auto *FPT = dyn_cast<FunctionProtoType>(FT))
    for (QualType P : FPT->param_types())
      if (P->isCLIReferenceType())
        return true;
  return false;
}

/// The location of the parameter \p P of \p FD for a diagnostic, or the
/// function's when the parameter has none.
static SourceLocation getParamLoc(const FunctionDecl *FD,
                                  const ParmVarDecl *P) {
  SourceLocation L = P->getLocation();
  return L.isValid() ? L : FD->getLocation();
}

/// The 'cli_type' attribute of the type that the declaration specifiers of
/// \p DS name, before Sema has converted them: a typedef name, typeof, or a
/// struct written with its tag. Null for every other specifier.
static const CLITypeAttr *getDeclSpecCLITypeAttr(const DeclSpec &DS) {
  switch (DS.getTypeSpecType()) {
  case DeclSpec::TST_typename:
  case DeclSpec::TST_typeofType:
  case DeclSpec::TST_typeof_unqualType:
    return msil::getCLITypeAttr(Sema::GetTypeFromParser(DS.getRepAsType()));
  case DeclSpec::TST_typeofExpr:
  case DeclSpec::TST_typeof_unqualExpr:
    if (const Expr *E = DS.getRepAsExpr())
      return msil::getCLITypeAttr(E->getType());
    return nullptr;
  case DeclSpec::TST_struct:
  case DeclSpec::TST_class:
  case DeclSpec::TST_interface:
    if (const auto *TD = dyn_cast_or_null<TagDecl>(DS.getRepAsDecl()))
      return getCLITypeAttrOf(TD);
    return nullptr;
  default:
    return nullptr;
  }
}

//===----------------------------------------------------------------------===//
// Queries
//===----------------------------------------------------------------------===//

const CLITypeAttr *msil::getCLITypeAttr(QualType T) {
  if (T.isNull())
    return nullptr;
  if (const RecordDecl *RD = T->getAsRecordDecl())
    return getCLITypeAttrOf(RD);
  return nullptr;
}

bool msil::isCLIObjectReferenceType(QualType T) {
  const auto *Ref = T->getAs<CLIReferenceType>();
  return Ref && Ref->getKey()->getName() == ObjectKey;
}

bool msil::isCLIReferenceChunk(const Declarator &D, unsigned ChunkIndex) {
  if (ChunkIndex >= D.getNumTypeObjects() ||
      D.getTypeObject(ChunkIndex).Kind != DeclaratorChunk::BlockPointer)
    return false;
  // The chunks are stored from the identifier outwards: the caret applies to
  // the declaration specifiers' type only when every chunk outside it is a
  // pair of parentheses. Otherwise it applies to a function type (a block
  // pointer) or to a type that no struct is.
  for (unsigned I = ChunkIndex + 1, E = D.getNumTypeObjects(); I != E; ++I)
    if (D.getTypeObject(I).Kind != DeclaratorChunk::Paren)
      return false;
  return getDeclSpecCLITypeAttr(D.getDeclSpec()) != nullptr;
}

//===----------------------------------------------------------------------===//
// The 'cli_type' attribute (RT1-RT8)
//===----------------------------------------------------------------------===//

void msil::handleCLITypeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  StringRef Name;
  SourceLocation NameLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Name, &NameLoc))
    return;

  // RT2, RT3: NAME is MC-1's TYPE grammar without value types, folded at
  // every level into the key. T-R1 quotes the parser's reason.
  llvm::Expected<cli::CLITypeName> Parsed = cli::parseCLITypeName(Name);
  if (!Parsed) {
    S.Diag(NameLoc, diag::err_msil_ref_cli_type_invalid)
        << Name << llvm::toString(Parsed.takeError());
    return;
  }
  const std::string &Key = Parsed->Key;
  auto *TD = cast<TagDecl>(D);

  // One cli_type per declaration. (A redeclaration inherits its previous
  // declaration's attributes only after its own are processed, so an
  // attribute found here was written on this declaration.)
  if (const auto *Old = TD->getAttr<CLITypeAttr>()) {
    if (Old->getKey() == Key) {
      S.Diag(AL.getLoc(), diag::warn_duplicate_attribute_exact) << AL;
      return;
    }
    S.Diag(AL.getLoc(), diag::err_disallowed_duplicate_attribute) << AL;
    S.Diag(Old->getLocation(), diag::note_previous_attribute);
    return;
  }

  // RT4: the attribute belongs on the struct's first declaration, and a later
  // declaration that writes it repeats its key (T-R2, with the note 'first
  // declared here' at the first declaration). An attribute refused here is
  // not attached, so the struct keeps the first declaration's class, or none.
  if (const TagDecl *Prev = TD->getPreviousDecl()) {
    const CLITypeAttr *First = getCLITypeAttrOf(Prev);
    if (!First) {
      S.Diag(AL.getLoc(), diag::err_msil_ref_cli_type_not_first_decl) << TD;
      S.Diag(TD->getFirstDecl()->getLocation(),
             diag::note_msil_ref_first_declared);
      return;
    }
    if (First->getKey() != Key) {
      // The note is at the struct's first declaration, as above, not at the
      // attribute found: that may be one that a later declaration wrote with
      // the same key, or the clone that a declaration without the attribute
      // inherits, which keeps the location of the attribute it copies.
      S.Diag(AL.getLoc(), diag::err_msil_ref_cli_type_differs) << TD;
      S.Diag(TD->getFirstDecl()->getLocation(),
             diag::note_msil_ref_first_declared);
      return;
    }
  }

  D->addAttr(CLITypeAttr::Create(S.Context, Name, Key, AL));
}

void msil::checkCLITypeDefinition(Sema &S, TagDecl *Tag) {
  // RT5: a class has no layout in C memory, so a struct with cli_type, own or
  // inherited, never gets a body (T-R3, permanent). The struct is complete by
  // now and stays valid: Decl::setInvalidDecl asserts that a complete tag
  // definition never becomes invalid, and the error fails the compilation.
  const CLITypeAttr *A = getCLITypeAttrOf(Tag);
  if (!A)
    return;
  S.Diag(Tag->getLocation(), diag::err_msil_ref_class_body)
      << Tag << cli::getCLITypeDisplayName(A->getKey());
}

//===----------------------------------------------------------------------===//
// Forming and qualifying the type (RC2, RC6, RS10, RS12, RS17, RS23, RS24)
//===----------------------------------------------------------------------===//

QualType msil::buildCLIReferenceType(Sema &S, QualType Pointee,
                                     unsigned TypeQuals,
                                     SourceLocation CaretLoc) {
  ASTContext &Ctx = S.Context;
  const LangOptions &LO = S.getLangOpts();
  const CLITypeAttr *A = getCLITypeAttr(Pointee);

  // RT7, RC6: other languages accept the attribute but not the caret over a
  // class (T-R5): "yet" in C++, which R-a2 adds; permanently in Objective-C,
  // OpenCL and CUDA (each of which may also be C++).
  if (A && (LO.ObjC || LO.OpenCL || LO.CUDA || LO.CPlusPlus)) {
    enum LanguageSelect : unsigned { CXX = 0, ObjC = 1, OpenCL = 2, CUDA = 3 };
    LanguageSelect Language = LO.ObjC     ? ObjC
                              : LO.OpenCL ? OpenCL
                              : LO.CUDA   ? CUDA
                                          : CXX;
    S.Diag(CaretLoc, diag::err_msil_ref_language) << unsigned(Language);
    return QualType();
  }

  // RC2: anything but a struct with cli_type is T-R4, which replaces the
  // block-pointer errors and their '-fblocks' advice.
  if (!A) {
    S.Diag(CaretLoc, diag::err_msil_ref_caret_not_class) << Pointee;
    return QualType();
  }

  // RS24: the class itself is never qualified; the reference's own
  // qualifiers follow the caret (T-R19). This includes qualifiers that come
  // from a typedef of the struct.
  if (Pointee.hasQualifiers()) {
    S.Diag(CaretLoc, diag::err_msil_ref_class_qualifier)
        << Pointee.getQualifiers().getAsString(Ctx.getPrintingPolicy())
        << getClassSpelling(Ctx, Pointee);
    return QualType();
  }

  // RC3: one type per key, sugared with the struct as written. The chunk's
  // qualifiers go through Sema::BuildQualifiedType, whose hook reports the
  // ones a reference cannot have (RS23); 'restrict' and '_Atomic' get clang's
  // texts there.
  QualType Ref = Ctx.getCLIReferenceType(&Ctx.Idents.get(A->getKey()), Pointee);
  if (!TypeQuals)
    return Ref;
  return S.BuildQualifiedType(Ref, CaretLoc, TypeQuals);
}

bool msil::diagnoseCLIPointee(Sema &S, QualType Pointee, SourceLocation Loc) {
  if (Pointee.isNull() || !isMSILTarget(S.Context))
    return false;
  // RS12: C memory never holds a .NET reference (T-R8, permanent).
  if (Pointee->isCLIReferenceType()) {
    S.Diag(Loc, diag::err_msil_ref_pointer) << Pointee;
    return true;
  }
  // RS10: a pointer into C memory cannot point at a .NET class (T-R6,
  // permanent).
  if (getCLITypeAttr(Pointee)) {
    S.Diag(Loc, diag::err_msil_ref_class_pointer)
        << getClassSpelling(S.Context, Pointee);
    return true;
  }
  // RS17: a function pointer type with a reference.
  return diagnoseCLIReferenceFunctionType(
      S, Pointee, Loc, CLIRefFunctionTypeForm::FunctionPointer);
}

bool msil::diagnoseCLIReferenceFunctionType(Sema &S, QualType FnType,
                                            SourceLocation Loc,
                                            CLIRefFunctionTypeForm Form) {
  // A unit that has formed no reference has no such function type. Any other
  // type than a function type is not this rule's.
  if (FnType.isNull() || !S.Context.hasCLIReferenceTypes() ||
      !hasCLIReferenceInSignature(FnType))
    return false;
  S.Diag(Loc, diag::err_msil_ref_function_type) << unsigned(Form);
  return true;
}

bool msil::diagnoseCLIReferenceQualifiers(Sema &S, QualType T, Qualifiers Quals,
                                          SourceLocation Loc) {
  if (T.isNull() || !T->isCLIReferenceType())
    return false;
  // RS23: only the qualifiers that this step adds; the ones that T has were
  // checked where T was formed. 'restrict', '_Atomic' and nullability keep
  // clang's texts (T-R18 for the others).
  Qualifiers Existing = T.getQualifiers();
  Qualifiers Refused;
  if (Quals.hasVolatile() && !Existing.hasVolatile())
    Refused.addVolatile();
  if (Quals.hasUnaligned() && !Existing.hasUnaligned())
    Refused.setUnaligned(true);
  if (Quals.hasAddressSpace() &&
      Quals.getAddressSpace() != Existing.getAddressSpace())
    Refused.setAddressSpace(Quals.getAddressSpace());
  if (!Refused.hasQualifiers())
    return false;
  S.Diag(Loc, diag::err_msil_ref_qualifier)
      << Refused.getAsString(S.Context.getPrintingPolicy()) << T;
  return true;
}

//===----------------------------------------------------------------------===//
// Declarations (RS3, RS11, RS15, RS18, RS20-RS22, RS25)
//===----------------------------------------------------------------------===//

bool msil::diagnoseCLIClassByValue(Sema &S, QualType T, SourceLocation Loc) {
  if (T.isNull() || !isMSILTarget(S.Context) || !getCLITypeAttr(T))
    return false;
  // RS11: a class has no value in C (T-R7, permanent).
  S.Diag(Loc, diag::err_msil_ref_class_value) << getClassSpelling(S.Context, T);
  return true;
}

void msil::checkCLIReferenceFunctionDecl(Sema &S, FunctionDecl *FD) {
  if (FD->isInvalidDecl() || !isMSILTarget(S.Context))
    return;

  // RS11: a parameter or the result that is a class struct by value (T-R7).
  // The declaration is invalid then, which also keeps MC-1's checks and the
  // definition's completeness checks from reporting the same type again.
  bool ByValue = false;
  for (ParmVarDecl *P : FD->parameters()) {
    if (!P->isInvalidDecl() &&
        diagnoseCLIClassByValue(S, P->getType(), getParamLoc(FD, P))) {
      P->setInvalidDecl();
      ByValue = true;
    }
  }
  if (diagnoseCLIClassByValue(S, FD->getReturnType(), FD->getLocation()))
    ByValue = true;
  if (ByValue) {
    FD->setInvalidDecl();
    return;
  }

  // The first parameter that is a reference (1-based), or 0 when only the
  // result is one: the function type has the parameter types, also for a K&R
  // definition, whose FunctionProtoType clang builds from its parameter
  // declarations; else the parameter declarations have them.
  const auto *FPT = FD->getType()->getAs<FunctionProtoType>();
  unsigned RefParam = 0;
  if (FPT) {
    for (unsigned I = 0, E = FPT->getNumParams(); I != E && !RefParam; ++I)
      if (FPT->getParamType(I)->isCLIReferenceType())
        RefParam = I + 1;
  } else {
    for (unsigned I = 0, E = FD->getNumParams(); I != E && !RefParam; ++I)
      if (FD->getParamDecl(I)->getType()->isCLIReferenceType())
        RefParam = I + 1;
  }
  if (!RefParam && !FD->getReturnType()->isCLIReferenceType())
    return;

  // RM4, RM5: a managed call has MC-1's checks of its prototype (T-M12) and
  // may have external linkage, since its stub is internal.
  if (FD->hasAttr<MSILMethodAttr>())
    return;

  // RS18: no prototype (T-R13). The type cannot tell, since a K&R definition
  // has a FunctionProtoType; FunctionDecl::hasPrototype can: a prototype
  // written on this declaration, or the one that a '()' redeclaration
  // inherits from an earlier declaration (Sema::MergeFunctionDecl gives it
  // that prototype's type and parameters). A K&R definition inherits none,
  // even after a prototype: clang then refers to the function without its
  // parameter types (C99 DR 316, Sema::BuildDeclarationNameExpr), so the
  // calls after it have no prototype, and it is T-R13 as well.
  if (!FD->hasPrototype()) {
    S.Diag(FD->getLocation(), diag::err_msil_ref_unprototyped_function);
  } else if (FPT && FPT->isVariadic()) {
    // RS20: a variadic function (T-R15).
    S.Diag(FD->getLocation(), diag::err_msil_ref_variadic_function);
  }

  // RS15: external linkage (T-R10); R-b lifts it.
  if (FD->hasExternalFormalLinkage()) {
    enum PositionSelect : unsigned { Parameter = 0, Result = 1 };
    S.Diag(FD->getLocation(), diag::err_msil_ref_external)
        << FD << unsigned(RefParam ? Parameter : Result) << RefParam;
  }
}

/// T-R20 for the attribute \p Name at \p Loc on \p VD, a variable or parameter
/// that is a reference (RS25). The text names \p VD, or, for a parameter
/// without a name, its type, as T-R9 names the operand's type when the
/// operand is not a variable. Both of RS25's paths report through it:
/// diagnoseCLIReferenceDeclAttr and checkCLIReferenceVarAttrs.
static void diagnoseCLIReferenceVarAttr(Sema &S, SourceLocation Loc,
                                        StringRef Name, const VarDecl *VD) {
  if (VD->getDeclName())
    S.Diag(Loc, diag::err_msil_ref_var_attr) << Name << VD;
  else
    S.Diag(Loc, diag::err_msil_ref_var_attr) << Name << VD->getType();
}

bool msil::diagnoseCLIReferenceDeclAttr(Sema &S, const Decl *D,
                                        const ParsedAttr &AL) {
  // A unit that has formed no reference has no declaration with one.
  if (!S.Context.hasCLIReferenceTypes())
    return false;

  StringRef Name;
  bool OnFunction = true;
  switch (AL.getKind()) {
  // RS21: each makes another party call the function or records its address
  // (on a managed call, MC-1's forwarders would export it).
  case ParsedAttr::AT_Constructor:
    Name = "constructor";
    break;
  case ParsedAttr::AT_Destructor:
    Name = "destructor";
    break;
  case ParsedAttr::AT_Alias:
    Name = "alias";
    break;
  case ParsedAttr::AT_IFunc:
    Name = "ifunc";
    break;
  case ParsedAttr::AT_Weak:
    Name = "weak";
    break;
  // A weak reference makes the function another name for a symbol that may
  // be external, which T-R10 refuses for an external function.
  case ParsedAttr::AT_WeakRef:
    Name = "weakref";
    break;
  case ParsedAttr::AT_DLLExport:
    Name = "dllexport";
    break;
  // 'annotate' is RS21's on a function and RS25's on a variable or a
  // parameter.
  case ParsedAttr::AT_Annotate:
    Name = "annotate";
    OnFunction = isa<FunctionDecl>(D);
    break;
  // RS25: each would need the variable's address ('annotate' on a variable or
  // a parameter: clang's llvm.var.annotation takes its slot's address), or
  // could only be ignored silently. A variable whose type is still undeduced
  // here ('__auto_type', C23 'auto') is checked later, once its initializer
  // gives it the type, by checkCLIReferenceVarAttrs.
  case ParsedAttr::AT_Cleanup:
    Name = "cleanup";
    OnFunction = false;
    break;
  case ParsedAttr::AT_Uninitialized:
    Name = "uninitialized";
    OnFunction = false;
    break;
  case ParsedAttr::AT_Blocks:
    Name = "__block";
    OnFunction = false;
    break;
  default:
    return false;
  }

  if (OnFunction) {
    const auto *FD = dyn_cast<FunctionDecl>(D);
    if (!FD || !hasCLIReferenceInSignature(FD->getType()))
      return false;
    // T-R16.
    S.Diag(AL.getLoc(), diag::err_msil_ref_function_attr) << Name << FD;
    return true;
  }
  const auto *VD = dyn_cast<VarDecl>(D);
  if (!VD || !VD->getType()->isCLIReferenceType())
    return false;
  // T-R20, permanent.
  diagnoseCLIReferenceVarAttr(S, AL.getLoc(), Name, VD);
  return true;
}

bool msil::diagnoseCLIReferencePragmaWeakAlias(Sema &S, const NamedDecl &Target,
                                               const IdentifierInfo *Alias,
                                               SourceLocation AliasLoc) {
  // RS21 as built for '#pragma weak <alias> = <f>': the pragma declares
  // <alias> as a copy of <f>'s declaration, with <f>'s type, external linkage
  // and implicit 'weak' and 'alias' attributes, which ProcessDeclAttribute
  // never sees. Like the attribute form, another name for the function, which
  // may be external, is refused, and the caller declares no alias. A unit that
  // has formed no reference has no function with one.
  if (!S.Context.hasCLIReferenceTypes())
    return false;
  const auto *FD = dyn_cast<FunctionDecl>(&Target);
  if (!FD || !hasCLIReferenceInSignature(FD->getType()))
    return false;
  // T-R16 for 'weak', at the pragma's first identifier.
  S.Diag(AliasLoc, diag::err_msil_ref_function_attr) << "weak" << Alias;
  return true;
}

void msil::checkCLIReferenceVarAttrs(Sema &S, VarDecl *VD) {
  // RS25 for a type deduced from the initializer: ProcessDeclAttribute saw
  // the undeduced type and attached these attributes. A unit that has formed
  // no reference has no such variable; for a type written in the source,
  // ProcessDeclAttribute's hook has already dropped them.
  if (!S.Context.hasCLIReferenceTypes() || VD->isInvalidDecl() ||
      !VD->hasAttrs() || !VD->getType()->isCLIReferenceType())
    return;
  // T-R20 at each, in attribute order, with diagnoseCLIReferenceDeclAttr's
  // names; then all of them are dropped, as that hook drops each.
  for (const Attr *A : VD->attrs()) {
    StringRef Name;
    if (isa<CleanupAttr>(A))
      Name = "cleanup";
    else if (isa<AnnotateAttr>(A))
      Name = "annotate";
    else if (isa<UninitializedAttr>(A))
      Name = "uninitialized";
    else if (isa<BlocksAttr>(A))
      Name = "__block";
    else
      continue;
    diagnoseCLIReferenceVarAttr(S, A->getLocation(), Name, VD);
  }
  VD->dropAttrs<CleanupAttr, AnnotateAttr, UninitializedAttr, BlocksAttr>();
}

namespace {
/// RS22: the first call of a 'returns_twice' function in a function body
/// and the first use of a .NET reference there. Block literals are other
/// functions and are not walked.
class SetjmpReferenceFinder final : public ConstDynamicRecursiveASTVisitor {
public:
  const CallExpr *Call = nullptr;
  const FunctionDecl *Callee = nullptr;
  SourceLocation FirstUse;

  bool TraverseBlockExpr(const BlockExpr *) override { return true; }

  bool VisitCallExpr(const CallExpr *CE) override {
    if (!Call)
      if (const FunctionDecl *FD = CE->getDirectCallee())
        if (FD->hasAttr<ReturnsTwiceAttr>()) {
          Call = CE;
          Callee = FD;
        }
    return true;
  }

  // The walk meets every expression and declaration of every function body
  // in a unit that has formed a reference, and some have no type: an
  // unevaluated string literal, such as a '_Static_assert' message, has a
  // null one (Sema::ActOnUnevaluatedStringLiteral).
  static bool isCLIReference(QualType T) {
    return !T.isNull() && T->isCLIReferenceType();
  }

  bool VisitExpr(const Expr *E) override {
    if (FirstUse.isInvalid() && isCLIReference(E->getType()))
      FirstUse = E->getExprLoc();
    return true;
  }

  bool VisitVarDecl(const VarDecl *VD) override {
    if (FirstUse.isInvalid() && isCLIReference(VD->getType()))
      FirstUse = VD->getLocation();
    return true;
  }
};
} // namespace

void msil::checkCLIReferenceSetjmpCaller(Sema &S, const FunctionDecl *FD,
                                         const Stmt *Body) {
  // One body walk, only in a unit that has formed a reference.
  if (!FD || !Body || FD->isInvalidDecl() || !S.Context.hasCLIReferenceTypes())
    return;
  SetjmpReferenceFinder Finder;
  Finder.TraverseStmt(Body);
  if (!Finder.Call)
    return;

  // The first use: the signature's reference (the result, then the first
  // parameter that is one), else the body's first.
  SourceLocation Use;
  if (FD->getReturnType()->isCLIReferenceType()) {
    Use = FD->getReturnTypeSourceRange().getBegin();
    if (Use.isInvalid())
      Use = FD->getLocation();
  } else {
    for (const ParmVarDecl *P : FD->parameters())
      if (P->getType()->isCLIReferenceType()) {
        Use = getParamLoc(FD, P);
        break;
      }
  }
  if (Use.isInvalid())
    Use = Finder.FirstUse;
  if (Use.isInvalid())
    return;

  // T-R17 at the call; EH-2 decides when it lifts.
  S.Diag(Finder.Call->getBeginLoc(), diag::err_msil_ref_setjmp)
      << Finder.Callee;
  S.Diag(Use, diag::note_msil_ref_used_here);
}

//===----------------------------------------------------------------------===//
// Expressions (RS4-RS8, RS13, RS16, RS19, RS26, RS27)
//===----------------------------------------------------------------------===//

bool msil::convertNullToCLIReference(Sema &S, QualType LHSType,
                                     ExprResult &RHS) {
  // RS4: NULL, 0, (void *)0, '\0' and C23 nullptr become a null reference.
  if (!LHSType->isCLIReferenceType() ||
      !RHS.get()->isNullPointerConstant(S.Context,
                                        Expr::NPC_ValueDependentIsNull))
    return false;
  RHS = S.ImpCastExprToType(RHS.get(), LHSType.getUnqualifiedType(),
                            CK_NullToPointer);
  return true;
}

std::optional<AssignConvertType>
msil::checkCLIReferenceAssignment(Sema &S, QualType LHSType, QualType RHSType,
                                  CastKind &Kind) {
  bool LHSRef = LHSType->isCLIReferenceType();
  if (!LHSRef && !RHSType->isCLIReferenceType())
    return std::nullopt;
  // RS4: equal types were tested by the caller; a class converts by itself
  // only to System.Object (the user's pick "(a) Only to Object"). Every other
  // pair is err_typecheck_convert_incompatible, a hard error.
  if (LHSRef && RHSType->isCLIReferenceType() &&
      isCLIObjectReferenceType(LHSType)) {
    Kind = CK_BitCast;
    return AssignConvertType::Compatible;
  }
  return AssignConvertType::Incompatible;
}

std::optional<QualType>
msil::checkCLIReferenceComparison(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                  SourceLocation Loc, BinaryOperatorKind Opc) {
  QualType LT = LHS.get()->getType();
  QualType RT = RHS.get()->getType();
  bool LRef = LT->isCLIReferenceType();
  bool RRef = RT->isCLIReferenceType();
  if (!LRef && !RRef)
    return std::nullopt;
  // RS5: identity only; '<', '>', '<=' and '>=' are clang's invalid
  // operands.
  if (!BinaryOperator::isEqualityOp(Opc))
    return S.InvalidOperands(Loc, LHS, RHS);

  ASTContext &Ctx = S.Context;
  if (LRef && RRef) {
    // The same class, or System.Object against a class, which converts.
    if (Ctx.hasSameUnqualifiedType(LT, RT))
      return Ctx.getLogicalOperationType();
    if (isCLIObjectReferenceType(LT)) {
      RHS = S.ImpCastExprToType(RHS.get(), LT.getUnqualifiedType(), CK_BitCast);
      return Ctx.getLogicalOperationType();
    }
    if (isCLIObjectReferenceType(RT)) {
      LHS = S.ImpCastExprToType(LHS.get(), RT.getUnqualifiedType(), CK_BitCast);
      return Ctx.getLogicalOperationType();
    }
    return S.InvalidOperands(Loc, LHS, RHS);
  }

  // A null pointer constant against a reference converts to it.
  ExprResult &Null = LRef ? RHS : LHS;
  QualType RefTy = (LRef ? LT : RT).getUnqualifiedType();
  if (!Null.get()->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNull))
    return S.InvalidOperands(Loc, LHS, RHS);
  Null = S.ImpCastExprToType(Null.get(), RefTy, CK_NullToPointer);
  return Ctx.getLogicalOperationType();
}

ExprResult msil::buildCLIReferenceTruthTest(Sema &S, Expr *E) {
  // RS6: 'E != NULL', as AMDGPU's feature predicate is rewritten; the null
  // side is an implicit 0 converted to the reference, as RS5 converts it.
  ExprResult Converted = S.DefaultFunctionArrayLvalueConversion(E);
  if (Converted.isInvalid())
    return ExprError();
  E = Converted.get();
  ASTContext &Ctx = S.Context;
  SourceLocation Loc = E->getExprLoc();
  Expr *Zero = IntegerLiteral::Create(
      Ctx, llvm::APInt::getZero(Ctx.getIntWidth(Ctx.IntTy)), Ctx.IntTy, Loc);
  Expr *Null = S.ImpCastExprToType(Zero, E->getType().getUnqualifiedType(),
                                   CK_NullToPointer)
                   .get();
  return BinaryOperator::Create(Ctx, E, Null, BO_NE,
                                Ctx.getLogicalOperationType(), VK_PRValue,
                                OK_Ordinary, Loc, S.CurFPFeatureOverrides());
}

std::optional<QualType> msil::checkCLIReferenceConditionalOperands(
    Sema &S, ExprResult &LHS, ExprResult &RHS, SourceLocation QuestionLoc) {
  QualType LT = LHS.get()->getType();
  QualType RT = RHS.get()->getType();
  bool LRef = LT->isCLIReferenceType();
  bool RRef = RT->isCLIReferenceType();
  if (!LRef && !RRef)
    return std::nullopt;

  ASTContext &Ctx = S.Context;
  if (LRef && RRef) {
    // RS7: the same class; a class and System.Object give System.Object
    // (the user's pick "(a) Only to Object"); two classes are incompatible.
    if (Ctx.hasSameUnqualifiedType(LT, RT))
      return Ctx.getCommonSugaredType(LT.getUnqualifiedType(),
                                      RT.getUnqualifiedType());
    if (isCLIObjectReferenceType(LT)) {
      RHS = S.ImpCastExprToType(RHS.get(), LT.getUnqualifiedType(), CK_BitCast);
      return LT.getUnqualifiedType();
    }
    if (isCLIObjectReferenceType(RT)) {
      LHS = S.ImpCastExprToType(LHS.get(), RT.getUnqualifiedType(), CK_BitCast);
      return RT.getUnqualifiedType();
    }
    S.Diag(QuestionLoc, diag::err_typecheck_cond_incompatible_operands)
        << LT << RT << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    return QualType();
  }

  // A reference and a null pointer constant give the reference. A reference
  // and anything else are left to clang's incompatible operand types.
  ExprResult &Null = LRef ? RHS : LHS;
  QualType RefTy = (LRef ? LT : RT).getUnqualifiedType();
  if (!Null.get()->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNull))
    return std::nullopt;
  Null = S.ImpCastExprToType(Null.get(), RefTy, CK_NullToPointer);
  return RefTy;
}

bool msil::checkCLIReferenceCast(Sema &S, ExprResult &SrcExpr,
                                 QualType DestType, CastKind &Kind,
                                 SourceRange OpRange) {
  QualType SrcType = SrcExpr.get()->getType();
  bool DestRef = DestType->isCLIReferenceType();
  bool SrcRef = SrcType->isCLIReferenceType();
  if (!DestRef && !SrcRef)
    return false;

  if (!DestRef) {
    // RS8: from a reference to a scalar is clang's
    // err_typecheck_expect_scalar_operand. To any other type, clang's own
    // checks report the destination.
    if (!DestType->isScalarType())
      return false;
    S.Diag(SrcExpr.get()->getExprLoc(),
           diag::err_typecheck_expect_scalar_operand)
        << SrcType << SrcExpr.get()->getSourceRange();
    SrcExpr = ExprError();
    return true;
  }

  // RS8: between references: free for the same class, else a checked cast
  // (CodeGen calls a cast stub unless the target is System.Object, RG5; the
  // user's pick "Keep checked casts").
  if (SrcRef) {
    Kind = S.Context.hasSameUnqualifiedType(SrcType, DestType) ? CK_NoOp
                                                               : CK_BitCast;
    return true;
  }
  // A null pointer constant becomes a null reference.
  if (SrcExpr.get()->isNullPointerConstant(S.Context,
                                           Expr::NPC_ValueDependentIsNull)) {
    Kind = CK_NullToPointer;
    return true;
  }
  // RS27: nothing else converts to a reference (T-R22).
  S.Diag(OpRange.getBegin(), diag::err_msil_ref_conversion)
      << SrcType << DestType << SrcExpr.get()->getSourceRange();
  SrcExpr = ExprError();
  return true;
}

bool msil::diagnoseCLIReferenceAddressOf(Sema &S, const Expr *Op,
                                         SourceLocation OpLoc) {
  if (!Op->getType()->isCLIReferenceType())
    return false;
  // RS13: T-R9 names the variable; in C a reference lvalue is one.
  if (const auto *DRE = dyn_cast<DeclRefExpr>(Op->IgnoreParens()))
    S.Diag(OpLoc, diag::err_msil_ref_address)
        << DRE->getDecl() << Op->getSourceRange();
  else
    S.Diag(OpLoc, diag::err_msil_ref_address)
        << Op->getType() << Op->getSourceRange();
  return true;
}

bool msil::diagnoseCLIReferenceFunctionAddress(Sema &S, const FunctionDecl *FD,
                                               SourceLocation Loc,
                                               bool Complain) {
  // RS16: every use of such a function but a direct call (T-R11). Host
  // triples keep it (Design.md 4.5).
  if (!S.Context.hasCLIReferenceTypes() ||
      !hasCLIReferenceInSignature(FD->getType()))
    return false;
  if (Complain)
    S.Diag(Loc, diag::err_msil_ref_function_address) << FD;
  return true;
}

bool msil::diagnoseCLIReferenceUnprototypedArg(Sema &S, const Expr *Arg) {
  QualType T = Arg->getType();
  if (!T->isCLIReferenceType())
    return false;
  // RS19: an argument without a parameter type behaves as a variadic one
  // (T-R14's first form).
  S.Diag(Arg->getExprLoc(), diag::err_msil_ref_unprototyped_arg)
      << T << Arg->getSourceRange();
  return true;
}

bool msil::diagnoseCLIReferenceVAArg(Sema &S, QualType T, SourceLocation Loc) {
  if (!T->isCLIReferenceType())
    return false;
  // RS19: T-R14's second form.
  S.Diag(Loc, diag::err_msil_ref_va_arg) << T;
  return true;
}

bool msil::diagnoseCLIReferenceCapture(Sema &S,
                                       const sema::CapturingScopeInfo *CSI,
                                       const ValueDecl *Var, SourceLocation Loc,
                                       bool Diagnose) {
  if (!Var->getType()->isCLIReferenceType())
    return false;
  // RS26: a capture puts the reference into C memory (T-R21, permanent).
  // Lambdas are C++, where no reference can be formed (RC6).
  enum CaptureSelect : unsigned {
    Block = 0,
    CapturedStatement = 1,
    OpenMPRegion = 2
  };
  CaptureSelect Kind;
  if (isa<sema::BlockScopeInfo>(CSI))
    Kind = Block;
  else if (const auto *RSI = dyn_cast<sema::CapturedRegionScopeInfo>(CSI))
    Kind = RSI->CapRegionKind == CR_OpenMP ? OpenMPRegion : CapturedStatement;
  else
    return false;
  if (Diagnose)
    S.Diag(Loc, diag::err_msil_ref_capture) << Var << unsigned(Kind);
  return true;
}
