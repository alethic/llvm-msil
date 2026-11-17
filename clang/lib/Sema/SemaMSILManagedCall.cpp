//===--- SemaMSILManagedCall.cpp - MSIL managed calls from C and C++ ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The Sema checks of the msil_method and msil_catch attributes
// (ImplementationPlan.md 5.20.3, MS1-MS7). A function declaration with
// __attribute__((msil_method(TYPE, MEMBER, SIGNATURE[, ASSEMBLY]))) names one
// public .NET member; each call of it calls a stub that the MSIL backend
// writes, which converts the C arguments as this file decides, accesses the
// member and converts the result. msil_catch(EXCEPTION_TYPE, CODE) entries,
// in order, map the exceptions the member throws to codes stored through a
// trailing 'int *'.
//
// The texts are 5.20.3's T-M table: they name the function as written, never
// with the '__msil_mc.' prefix of its IR name. An asm label with that prefix,
// or with '__msil_mc_ctor.' or '__msil_mc_dtor.', which start the names of
// the forwarders that clang's CodeGen defines for a constructor or destructor
// managed call, is reserved for the toolchain (T-M22, checkMSILAsmLabel).
//
//===----------------------------------------------------------------------===//

#include "SemaMSILManagedCall.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CLI/ManagedSignature.h"
#include "llvm/CLI/SectionRules.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <optional>
#include <string>

using namespace clang;
namespace cli = llvm::cli;

//===----------------------------------------------------------------------===//
// MS5's conversion table (checkMSILManagedCallDecl)
//===----------------------------------------------------------------------===//

namespace {
/// The parsed arguments of one msil_method attribute.
struct ManagedCallSpec {
  cli::ManagedType Type;
  std::string Member;
  cli::ManagedSignature Sig;

  bool isCtor() const { return Member == ".ctor"; }
  /// Whether the C parameters start with a 'this' handle (MS4): an instance
  /// member other than a constructor.
  bool hasThis() const { return Sig.HasThis && !isCtor(); }
  /// The CIL type that the C result carries (MS4): the constructed object or
  /// array for '.ctor', else the method's result or the field's type.
  const cli::ManagedType &getResultType() const {
    return isCtor() ? Type : Sig.Return;
  }
};
} // namespace

/// Parses TYPE, MEMBER and SIGNATURE as handleMSILMethodAttr checks them;
/// std::nullopt when any of them is invalid (handleMSILMethodAttr has reported
/// it).
static std::optional<ManagedCallSpec>
parseManagedCallSpec(llvm::StringRef Type, llvm::StringRef Member,
                     llvm::StringRef Signature) {
  llvm::Expected<cli::ManagedType> T = cli::parseManagedTypeName(Type);
  llvm::Expected<cli::ManagedSignature> S =
      cli::parseManagedSignature(Signature);
  llvm::Error M = cli::checkManagedMemberName(Member);
  if (!T || !S || M) {
    if (!T)
      llvm::consumeError(T.takeError());
    if (!S)
      llvm::consumeError(S.takeError());
    llvm::consumeError(std::move(M));
    return std::nullopt;
  }
  if (cli::checkManagedCallShape(*T, Member, *S))
    return std::nullopt;
  return ManagedCallSpec{std::move(*T), Member.str(), std::move(*S)};
}

/// The canonical, unqualified form of \p T.
static QualType canonicalUnqualified(const ASTContext &Ctx, QualType T) {
  return Ctx.getCanonicalType(T).getUnqualifiedType();
}

/// An integer type of MS5's table: any integer type of at most 64 bits except
/// _BitInt, whose storage width is not its value width. Plain, signed and
/// unsigned char, bool, the character types and complete unscoped
/// enumerations count; __int128 and unsigned __int128 do not, since no CIL
/// integer or enum is wider than 64 bits.
static bool isManagedInteger(const ASTContext &Ctx, QualType T) {
  return T->isIntegerType() && !T->isBitIntType() && Ctx.getTypeSize(T) <= 64;
}

/// intptr_t or uintptr_t (as types, so on the host triples 'long' or 'long
/// long' of the same canonical type count).
static bool isIntPtrType(const ASTContext &Ctx, QualType T) {
  QualType C = canonicalUnqualified(Ctx, T);
  return isManagedInteger(Ctx, C) &&
         (C == canonicalUnqualified(Ctx, Ctx.getIntPtrType()) ||
          C == canonicalUnqualified(Ctx, Ctx.getUIntPtrType()));
}

/// An integer type that carries 'native int' (MS5): intptr_t, uintptr_t,
/// size_t or ptrdiff_t.
static bool isNativeIntType(const ASTContext &Ctx, QualType T) {
  QualType C = canonicalUnqualified(Ctx, T);
  return isIntPtrType(Ctx, C) ||
         (isManagedInteger(Ctx, C) &&
          (C == canonicalUnqualified(Ctx, Ctx.getSizeType()) ||
           C == canonicalUnqualified(Ctx, Ctx.getSignedSizeType()) ||
           C == canonicalUnqualified(Ctx, Ctx.getPointerDiffType())));
}

/// MS5's handle type: void *, intptr_t, uintptr_t, or a pointer to a struct
/// (or union or class) type that is incomplete at the declaration. (A struct
/// completed later in the translation unit does not change the decision:
/// CodeGen copies the conversions Sema recorded.)
static bool isHandleType(const ASTContext &Ctx, QualType T) {
  QualType C = canonicalUnqualified(Ctx, T);
  if (const auto *PT = C->getAs<PointerType>()) {
    QualType P = PT->getPointeeType();
    if (P->isVoidType())
      return true;
    return P->isRecordType() && P->isIncompleteType();
  }
  return isIntPtrType(Ctx, C);
}

/// The C character type of a C string, by the CIL conversion it gets
/// (MS5's string rows): char and char8_t (and, in C, where char8_t is
/// unsigned char, unsigned char) are UTF-8; char16_t is UTF-16; char32_t is
/// UTF-32; wchar_t is UTF-16 where it is 16 bits wide (the Windows triples)
/// and UTF-32 where it is 32 bits wide.
static std::optional<cli::ManagedConv> getStringConv(const ASTContext &Ctx,
                                                     QualType Pointee) {
  using cli::ManagedConv;
  QualType P = canonicalUnqualified(Ctx, Pointee);
  const TargetInfo &TI = Ctx.getTargetInfo();
  bool CPlusPlus = Ctx.getLangOpts().CPlusPlus;
  auto ByWCharWidth = [&]() -> std::optional<ManagedConv> {
    switch (TI.getWCharWidth()) {
    case 16:
      return ManagedConv::UTF16;
    case 32:
      return ManagedConv::UTF32;
    default:
      return std::nullopt;
    }
  };
  if (P->isSpecificBuiltinType(BuiltinType::Char_S) ||
      P->isSpecificBuiltinType(BuiltinType::Char_U) || P->isChar8Type() ||
      (!CPlusPlus && P->isSpecificBuiltinType(BuiltinType::UChar)))
    return ManagedConv::UTF8;
  // In C, wchar_t, char16_t and char32_t are integer types, which these
  // ASTContext types are; in C++ they are the distinct builtin types.
  if (P == canonicalUnqualified(Ctx, Ctx.getWideCharType()))
    return ByWCharWidth();
  if (P == canonicalUnqualified(Ctx, Ctx.Char16Ty))
    return ManagedConv::UTF16;
  if (P == canonicalUnqualified(Ctx, Ctx.Char32Ty))
    return ManagedConv::UTF32;
  return std::nullopt;
}

/// A C type that carries CIL 'char' (MS5): char16_t, or wchar_t where it is
/// 16 bits wide.
static bool isManagedCharType(const ASTContext &Ctx, QualType T) {
  QualType C = canonicalUnqualified(Ctx, T);
  if (C == canonicalUnqualified(Ctx, Ctx.Char16Ty))
    return true;
  return C == canonicalUnqualified(Ctx, Ctx.getWideCharType()) &&
         Ctx.getTargetInfo().getWCharWidth() == 16;
}

/// The bit width of a CIL integer kind, or 0.
static unsigned getIntegerWidth(cli::ManagedType::TypeKind K) {
  using TK = cli::ManagedType::TypeKind;
  switch (K) {
  case TK::Int8:
  case TK::UInt8:
    return 8;
  case TK::Int16:
  case TK::UInt16:
    return 16;
  case TK::Int32:
  case TK::UInt32:
    return 32;
  case TK::Int64:
  case TK::UInt64:
    return 64;
  default:
    return 0;
  }
}

/// The rows of MS5 that do not depend on the direction: 'bool' and 'val'.
static std::optional<cli::ManagedConv>
classifyScalar(const ASTContext &Ctx, QualType T, const cli::ManagedType &CIL) {
  using cli::ManagedConv;
  using TK = cli::ManagedType::TypeKind;
  QualType C = canonicalUnqualified(Ctx, T);
  switch (CIL.Kind) {
  case TK::Bool:
    if (C->isBooleanType() ||
        (isManagedInteger(Ctx, C) && Ctx.getTypeSize(C) <= 32))
      return ManagedConv::Bool;
    return std::nullopt;
  case TK::Char:
    if (isManagedCharType(Ctx, C))
      return ManagedConv::Val;
    return std::nullopt;
  case TK::Int8:
  case TK::UInt8:
  case TK::Int16:
  case TK::UInt16:
  case TK::Int32:
  case TK::UInt32:
  case TK::Int64:
  case TK::UInt64:
    if (isManagedInteger(Ctx, C) &&
        Ctx.getTypeSize(C) == getIntegerWidth(CIL.Kind))
      return ManagedConv::Val;
    return std::nullopt;
  case TK::Float32:
    if (C->isSpecificBuiltinType(BuiltinType::Float))
      return ManagedConv::Val;
    return std::nullopt;
  case TK::Float64:
    if (C->isSpecificBuiltinType(BuiltinType::Double))
      return ManagedConv::Val;
    return std::nullopt;
  case TK::NativeInt:
  case TK::NativeUInt:
    if (C->isObjectPointerType() || isNativeIntType(Ctx, C) ||
        isHandleType(Ctx, C))
      return ManagedConv::Val;
    return std::nullopt;
  case TK::Pointer:
    if (C->isObjectPointerType())
      return ManagedConv::Val;
    return std::nullopt;
  default:
    return std::nullopt;
  }
}

/// The conversion of the C parameter type \p T for the CIL parameter type
/// \p CIL (MS5), or std::nullopt when no row accepts the pair. An integer of
/// at most 64 bits for a non-GCHandle value type is 'enum' (msil-ld checks
/// that the type is an enum of that width), even when it is intptr_t; a
/// pointer handle for one is 'boxed'.
static std::optional<cli::ManagedConv>
classifyManagedParam(const ASTContext &Ctx, QualType T,
                     const cli::ManagedType &CIL) {
  using cli::ManagedConv;
  using TK = cli::ManagedType::TypeKind;
  QualType C = canonicalUnqualified(Ctx, T);
  bool Handle = isHandleType(Ctx, C);
  switch (CIL.Kind) {
  case TK::String:
    if (Handle)
      return ManagedConv::Obj;
    if (const auto *PT = C->getAs<PointerType>())
      return getStringConv(Ctx, PT->getPointeeType());
    return std::nullopt;
  case TK::Object:
  case TK::Class:
  case TK::SZArray:
    if (Handle)
      return ManagedConv::Obj;
    return std::nullopt;
  case TK::ValueType:
    if (CIL.isGCHandle())
      return Handle ? std::optional(ManagedConv::GCH) : std::nullopt;
    if (isManagedInteger(Ctx, C))
      return ManagedConv::Enum;
    if (Handle)
      return ManagedConv::Boxed;
    return std::nullopt;
  default:
    return classifyScalar(Ctx, C, CIL);
  }
}

/// The conversion of the C result type \p T for the CIL type \p CIL that the
/// result carries (ManagedCallSpec::getResultType) (MS5), or std::nullopt.
/// \p IsCtor: \p CIL is the constructed type of a '.ctor', whose value type
/// is boxed (never an enum).
static std::optional<cli::ManagedConv>
classifyManagedResult(const ASTContext &Ctx, QualType T,
                      const cli::ManagedType &CIL, bool IsCtor) {
  using cli::ManagedConv;
  using TK = cli::ManagedType::TypeKind;
  QualType C = canonicalUnqualified(Ctx, T);
  if (CIL.Kind == TK::Void)
    return C->isVoidType() ? std::optional(ManagedConv::Void) : std::nullopt;
  if (C->isVoidType())
    return std::nullopt;
  bool Handle = isHandleType(Ctx, C);
  switch (CIL.Kind) {
  case TK::String:
  case TK::Object:
  case TK::Class:
  case TK::SZArray:
    if (Handle)
      return ManagedConv::Obj;
    return std::nullopt;
  case TK::ValueType:
    if (CIL.isGCHandle())
      return Handle ? std::optional(ManagedConv::GCH) : std::nullopt;
    if (!IsCtor && isManagedInteger(Ctx, C))
      return ManagedConv::Enum;
    if (Handle)
      return ManagedConv::Boxed;
    return std::nullopt;
  default:
    return classifyScalar(Ctx, C, CIL);
  }
}

/// The conversion of the C 'this' handle of type \p T for the declaring
/// type \p Type (MS5): 'this-obj' for a reference type, 'this-gch' for
/// GCHandle and 'this-val' for another value type; the C type must be a
/// handle type.
static std::optional<cli::ManagedConv>
classifyManagedThis(const ASTContext &Ctx, QualType T,
                    const cli::ManagedType &Type) {
  using cli::ManagedConv;
  using TK = cli::ManagedType::TypeKind;
  if (!isHandleType(Ctx, T))
    return std::nullopt;
  switch (Type.Kind) {
  case TK::Class:
    return ManagedConv::ThisObj;
  case TK::ValueType:
    return Type.isGCHandle() ? ManagedConv::ThisGCH : ManagedConv::ThisVal;
  default:
    return std::nullopt;
  }
}

/// MS5's trailing parameter of msil_catch: exactly 'int *' (no qualifier on
/// the int).
static bool isManagedErrorParam(const ASTContext &Ctx, QualType T) {
  QualType C = canonicalUnqualified(Ctx, T);
  const auto *PT = C->getAs<PointerType>();
  return PT && Ctx.getCanonicalType(PT->getPointeeType()) ==
                   Ctx.getCanonicalType(Ctx.IntTy);
}

namespace {
/// What classifyManagedCall found for a declaration.
struct ManagedCallClass {
  enum ProblemKind {
    None,       ///< Convs holds the conversions.
    Arity,      ///< MS4: the C parameter count is not Expected (T-M4).
    ErrorParam, ///< MS4: the last parameter is not 'int *' (T-M5).
    Conversion, ///< MS5: no row accepts one pair (T-M3).
  };
  ProblemKind Problem = None;
  /// The conversions, when Problem is None.
  cli::ManagedConvList Convs;
  /// For Arity: the expected C parameter count and its parts.
  unsigned Expected = 0;
  bool HasThis = false;
  unsigned NumCIL = 0;
  bool HasErr = false;
  /// For Conversion: 0 for a parameter, 1 for the 'this' handle, 2 for the
  /// result (T-M3's %select); the 1-based C parameter position for a
  /// parameter (and for 'this', 1); the CIL type of the pair.
  unsigned Position = 0;
  unsigned CParam = 0;
  cli::ManagedType CILType;
};
} // namespace

/// Applies MS4 and MS5 to \p FD, whose msil_method arguments are \p Spec and
/// which has \p HasCatch msil_catch entries: the C parameter count, the
/// trailing 'int *', then each pair in order ('this', the parameters, the
/// result). The first problem is returned; with none, the conversion of every
/// C parameter and of the result.
static ManagedCallClass classifyManagedCall(const ASTContext &Ctx,
                                            const FunctionDecl &FD,
                                            const ManagedCallSpec &Spec,
                                            bool HasCatch) {
  using cli::ManagedConv;
  ManagedCallClass R;
  R.HasThis = Spec.hasThis();
  R.NumCIL = Spec.Sig.Params.size();
  R.HasErr = HasCatch;
  R.Expected = unsigned(R.HasThis) + R.NumCIL + unsigned(R.HasErr);
  if (FD.getNumParams() != R.Expected) {
    R.Problem = ManagedCallClass::Arity;
    return R;
  }
  if (R.HasErr &&
      !isManagedErrorParam(Ctx, FD.getParamDecl(R.Expected - 1)->getType())) {
    R.Problem = ManagedCallClass::ErrorParam;
    return R;
  }
  unsigned CPos = 0;
  if (R.HasThis) {
    std::optional<ManagedConv> C =
        classifyManagedThis(Ctx, FD.getParamDecl(0)->getType(), Spec.Type);
    if (!C) {
      R.Problem = ManagedCallClass::Conversion;
      R.Position = 1;
      R.CParam = 1;
      R.CILType = Spec.Type;
      return R;
    }
    R.Convs.Params.push_back(*C);
    ++CPos;
  }
  for (const cli::ManagedType &P : Spec.Sig.Params) {
    std::optional<ManagedConv> C =
        classifyManagedParam(Ctx, FD.getParamDecl(CPos)->getType(), P);
    if (!C) {
      R.Problem = ManagedCallClass::Conversion;
      R.Position = 0;
      R.CParam = CPos + 1;
      R.CILType = P;
      return R;
    }
    R.Convs.Params.push_back(*C);
    ++CPos;
  }
  if (R.HasErr)
    R.Convs.Params.push_back(ManagedConv::Err);
  std::optional<ManagedConv> Res = classifyManagedResult(
      Ctx, FD.getReturnType(), Spec.getResultType(), Spec.isCtor());
  if (!Res) {
    R.Problem = ManagedCallClass::Conversion;
    R.Position = 2;
    R.CParam = 0;
    R.CILType = Spec.getResultType();
    return R;
  }
  R.Convs.Result = *Res;
  return R;
}

//===----------------------------------------------------------------------===//
// The diagnostics' helpers
//===----------------------------------------------------------------------===//

namespace {
/// T-M2's %select{...}0 index of each argument.
enum InvalidArg : unsigned {
  ArgMethodType = 0,
  ArgMethodMember = 1,
  ArgMethodSignature = 2,
  ArgCatchType = 3,
  ArgMethodAssembly = 4,
};

/// T-M14's %select{msil_method|msil_catch}0.
enum AttrKindSelect : unsigned { SelectMethod = 0, SelectCatch = 1 };

/// T-M13's %select{...}0.
enum CXXKindSelect : unsigned {
  KindMemberFunction = 0,
  KindFunctionTemplate = 1,
  KindConstexpr = 2,
  KindConsteval = 3,
  KindAllocation = 4,
  KindDeallocation = 5,
};

/// T-M19's %select{constructor|destructor}0, also T-M20's attribute name.
enum StartupKindSelect : unsigned {
  SelectConstructor = 0,
  SelectDestructor = 1
};

/// T-M22's %select{stub|forwarder}1: what a reserved asm label names.
enum ReservedNameSelect : unsigned { ReservedStub = 0, ReservedForwarder = 1 };
} // namespace

/// The msil_catch attributes of \p D, in the order written.
static SmallVector<const MSILCatchAttr *, 4> getCatches(const Decl *D) {
  SmallVector<const MSILCatchAttr *, 4> List;
  for (const auto *A : D->specific_attrs<MSILCatchAttr>())
    List.push_back(A);
  return List;
}

/// The msil_catch attributes written on \p D itself.
static SmallVector<const MSILCatchAttr *, 4> getOwnCatches(const Decl *D) {
  SmallVector<const MSILCatchAttr *, 4> List;
  for (const auto *A : D->specific_attrs<MSILCatchAttr>())
    if (!A->isInherited())
      List.push_back(A);
  return List;
}

/// An exception type as msil_catch compares and records it.
static StringRef catchTypeName(const MSILCatchAttr &A) {
  return A.getExceptionType().trim();
}

static bool sameMethodArgs(const MSILMethodAttr &A, const MSILMethodAttr &B) {
  return A.getType() == B.getType() && A.getMember() == B.getMember() &&
         A.getSignature() == B.getSignature() &&
         A.getAssembly() == B.getAssembly();
}

static bool sameCatchLists(ArrayRef<const MSILCatchAttr *> A,
                           ArrayRef<const MSILCatchAttr *> B) {
  if (A.size() != B.size())
    return false;
  for (size_t I = 0, E = A.size(); I != E; ++I)
    if (catchTypeName(*A[I]) != catchTypeName(*B[I]) ||
        A[I]->getCode() != B[I]->getCode())
      return false;
  return true;
}

/// The first declaration of \p FD's function that the source wrote, which
/// MS2 compares a redeclaration with: the first declaration, or the one after
/// it when the first is a library builtin's implicit declaration (see
/// checkMSILManagedCallDecl).
static const FunctionDecl *getFirstWrittenDecl(const FunctionDecl &FD) {
  const FunctionDecl *First = FD.getFirstDecl();
  if (!First->isImplicit() || !First->getBuiltinID())
    return First;
  for (const FunctionDecl *D = &FD; D; D = D->getPreviousDecl())
    if (D->getPreviousDecl() == First)
      return D;
  return First;
}

/// The location of a parameter for T-M3 and T-M5, or the function's.
static SourceLocation paramLoc(const FunctionDecl *FD, unsigned I) {
  SourceLocation L = FD->getParamDecl(I)->getLocation();
  return L.isValid() ? L : FD->getLocation();
}

/// T-M4's breakdown: "a 'this' handle, 2 CIL parameters and the 'msil_catch'
/// error code".
static std::string arityBreakdown(const ManagedCallClass &R) {
  SmallVector<std::string, 3> Parts;
  if (R.HasThis)
    Parts.push_back("a 'this' handle");
  if (R.NumCIL == 0)
    Parts.push_back("no CIL parameters");
  else if (R.NumCIL == 1)
    Parts.push_back("1 CIL parameter");
  else
    Parts.push_back(std::to_string(R.NumCIL) + " CIL parameters");
  if (R.HasErr)
    Parts.push_back("the 'msil_catch' error code");
  std::string S;
  for (size_t I = 0, E = Parts.size(); I != E; ++I) {
    if (I)
      S += I + 1 == E ? " and " : ", ";
    S += Parts[I];
  }
  return S;
}

/// MS1's attributes that msil_method cannot be combined with (T-M6). Reported
/// when either attribute is written on \p FD itself, so a redeclaration that
/// inherits both reports nothing again.
static bool checkIncompatible(Sema &S, const FunctionDecl *FD,
                              const MSILMethodAttr *MA) {
  auto Report = [&](const Attr *Other, StringRef Name) {
    if (!Other || (MA->isInherited() && Other->isInherited()))
      return false;
    S.Diag(MA->isInherited() ? Other->getLocation() : MA->getLocation(),
           diag::err_msil_method_incompatible)
        << Name;
    return true;
  };
  return Report(FD->getAttr<WebAssemblyImportModuleAttr>(), "import_module") ||
         Report(FD->getAttr<WebAssemblyImportNameAttr>(), "import_name") ||
         Report(FD->getAttr<AliasAttr>(), "alias") ||
         Report(FD->getAttr<IFuncAttr>(), "ifunc") ||
         Report(FD->getAttr<WeakAttr>(), "weak");
}

/// Whether the C parameter type \p T lowers to the IL type int32: an integer,
/// bool or enumeration of at most 32 bits.
static bool lowersToInt32(const ASTContext &Ctx, QualType T) {
  QualType C = canonicalUnqualified(Ctx, T);
  return C->isIntegralOrEnumerationType() && Ctx.getTypeSize(C) <= 32;
}

/// Whether the C parameter type \p T lowers to the IL type native int: a
/// pointer (a C++ reference and nullptr_t included), or an integer or
/// enumeration as wide as a pointer.
static bool lowersToNativeInt(const ASTContext &Ctx, QualType T) {
  QualType C = canonicalUnqualified(Ctx, T);
  if (C->isPointerType() || C->isReferenceType() || C->isNullPtrType())
    return true;
  return C->isIntegralOrEnumerationType() &&
         Ctx.getTypeSize(C) ==
             Ctx.getTargetInfo().getPointerWidth(LangAS::Default);
}

/// Whether the forwarder of the constructor (\p Kind SelectConstructor) or
/// destructor managed call \p FD may take its parameters (T-M19). The
/// forwarder that clang's CodeGen registers has the managed call's
/// parameters, so they are judged as the MSIL backend judges a native
/// constructor's (MSILAsmPrinter's getConstructorArgs): none, or, where glibc
/// passes argc, argv and envp (cli::constructorsTakeArgs: ELF rules), a
/// prefix of (int32, native int, native int) by the IL types that the C
/// parameters lower to. A destructor takes none on every triple.
static bool takesStartupParams(const ASTContext &Ctx, const FunctionDecl *FD,
                               StartupKindSelect Kind) {
  unsigned N = FD->getNumParams();
  if (N == 0)
    return true;
  if (Kind != SelectConstructor || N > 3 ||
      !cli::constructorsTakeArgs(
          cli::getSectionRules(Ctx.getTargetInfo().getTriple())))
    return false;
  for (unsigned I = 0; I != N; ++I) {
    QualType T = FD->getParamDecl(I)->getType();
    if (I == 0 ? !lowersToInt32(Ctx, T) : !lowersToNativeInt(Ctx, T))
      return false;
  }
  return true;
}

/// T-M19's %select{global |}2 and %select{MSIL|msil64-unknown-dotnet}2: the
/// MSIL backend's text for the section rules of \p TT
/// (MSIL::getConstructorArgsText).
static unsigned getCtorArgsTextSelect(const llvm::Triple &TT) {
  switch (cli::getSectionRules(TT)) {
  case cli::SectionRules::ELF:
  case cli::SectionRules::COFF:
    return 0;
  case cli::SectionRules::Dotnet:
    return 1;
  }
  llvm_unreachable("unknown section rules");
}

/// A managed call with constructor or destructor (plan 5.20.4 as amended):
/// clang's CodeGen registers a forwarder that calls it, which passes no
/// msil_catch error code (T-M20) and has the managed call's parameters
/// (T-M19, the MSIL backend's text for a native constructor with arguments it
/// does not pass). Reported where the constructor or destructor attribute is
/// written, so a redeclaration that inherits it reports nothing again; with
/// msil_catch, only T-M20 is reported. dllexport and dllimport need no check.
static bool checkStartupAttributes(Sema &S, const FunctionDecl *FD,
                                   ArrayRef<const MSILCatchAttr *> Catches) {
  const Attr *Attrs[] = {FD->getAttr<ConstructorAttr>(),
                         FD->getAttr<DestructorAttr>()};
  for (StartupKindSelect Kind : {SelectConstructor, SelectDestructor}) {
    const Attr *A = Attrs[Kind];
    if (!A || A->isInherited())
      continue;
    if (!Catches.empty()) {
      const MSILCatchAttr *First = Catches.front();
      S.Diag(First->isInherited() ? A->getLocation() : First->getLocation(),
             diag::err_msil_catch_incompatible)
          << (Kind == SelectConstructor ? "constructor" : "destructor");
      return true;
    }
    if (!takesStartupParams(S.Context, FD, Kind)) {
      S.Diag(A->getLocation(), diag::err_msil_method_ctor_args)
          << unsigned(Kind) << FD
          << getCtorArgsTextSelect(S.Context.getTargetInfo().getTriple());
      return true;
    }
  }
  return false;
}

/// T-M13: the C++ subjects that msil_method does not take yet. std::nullopt
/// for a namespace-scope function. A function declared within a template (a
/// block-scope extern declaration in a function template, say) is a templated
/// entity too: its instantiations are made without these checks. A
/// replaceable global allocation or deallocation function redeclares the one
/// that clang declares implicitly, and its calls come from new and delete
/// expressions, which clang may also omit.
static std::optional<CXXKindSelect>
getUnsupportedCXXKind(const FunctionDecl *FD) {
  if (isa<CXXMethodDecl>(FD))
    return KindMemberFunction;
  if (FD->getDescribedFunctionTemplate() ||
      FD->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
      FD->isTemplated())
    return KindFunctionTemplate;
  if (FD->isReplaceableGlobalAllocationFunction())
    return FD->getDeclName().isAnyOperatorDelete() ? KindDeallocation
                                                   : KindAllocation;
  if (FD->isConsteval())
    return KindConsteval;
  if (FD->isConstexprSpecified())
    return KindConstexpr;
  return std::nullopt;
}

void clang::msil::handleMSILMethodAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // MS1: a lambda call operator is a C++ member function. Its attributes are
  // processed by ActOnStartOfLambdaDefinition, which never reaches
  // checkMSILManagedCallDecl, so it is refused here.
  if (isLambdaCallOperator(dyn_cast<DeclContext>(D))) {
    S.Diag(AL.getLoc(), diag::err_msil_method_cxx)
        << unsigned(KindMemberFunction);
    return;
  }
  StringRef Type, Member, Signature, Assembly, ExtraAssembly;
  SourceLocation TypeLoc, MemberLoc, SigLoc, AsmLoc, ExtraAsmLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Type, &TypeLoc) ||
      !S.checkStringLiteralArgumentAttr(AL, 1, Member, &MemberLoc) ||
      !S.checkStringLiteralArgumentAttr(AL, 2, Signature, &SigLoc))
    return;
  // ASSEMBLY is variadic in Attr.td only so that -ast-print prints it when it
  // is written: one string is accepted, and a second one is T-M2 below.
  bool HasAssembly = AL.getNumArgs() > 3;
  if (HasAssembly &&
      !S.checkStringLiteralArgumentAttr(AL, 3, Assembly, &AsmLoc))
    return;
  bool HasExtraAssembly = AL.getNumArgs() > 4;
  if (HasExtraAssembly &&
      !S.checkStringLiteralArgumentAttr(AL, 4, ExtraAssembly, &ExtraAsmLoc))
    return;

  // One msil_method per declaration: a second one with the same arguments
  // adds nothing.
  if (const auto *Old = D->getAttr<MSILMethodAttr>()) {
    if (Old->getType() == Type && Old->getMember() == Member &&
        Old->getSignature() == Signature && Old->getAssembly() == Assembly) {
      S.Diag(AL.getLoc(), diag::warn_duplicate_attribute_exact) << AL;
      return;
    }
    S.Diag(AL.getLoc(), diag::err_disallowed_duplicate_attribute) << AL;
    S.Diag(Old->getLocation(), diag::note_previous_attribute);
    return;
  }

  // MA1-MA3: each argument parses; T-M2 quotes the parser's reason.
  auto Report = [&](SourceLocation Loc, InvalidArg Arg, StringRef Text,
                    llvm::Error E) {
    S.Diag(Loc, diag::err_msil_attr_invalid_arg)
        << unsigned(Arg) << Text << llvm::toString(std::move(E));
  };
  // (Each Expected is tested once: testing one again after takeError marks it
  // unchecked.)
  llvm::Expected<cli::ManagedType> T = cli::parseManagedTypeName(Type);
  bool TypeOK = bool(T);
  if (!TypeOK)
    Report(TypeLoc, ArgMethodType, Type, T.takeError());
  bool MemberOK = true;
  if (llvm::Error E = cli::checkManagedMemberName(Member)) {
    Report(MemberLoc, ArgMethodMember, Member, std::move(E));
    MemberOK = false;
  }
  llvm::Expected<cli::ManagedSignature> Sig =
      cli::parseManagedSignature(Signature);
  bool SigOK = bool(Sig);
  if (!SigOK)
    Report(SigLoc, ArgMethodSignature, Signature, Sig.takeError());

  // T-M2: an ASSEMBLY that is given names an assembly. An absent ASSEMBLY is
  // the default (mscorlib); any other name is quoted where text output needs
  // it. Only one ASSEMBLY is given: a second string is reported once, at that
  // string, and the attribute keeps the first.
  if (HasAssembly && Assembly.empty())
    S.Diag(AsmLoc, diag::err_msil_attr_invalid_arg)
        << unsigned(ArgMethodAssembly) << Assembly << "it is empty";
  if (HasExtraAssembly)
    S.Diag(ExtraAsmLoc, diag::err_msil_attr_invalid_arg)
        << unsigned(ArgMethodAssembly) << ExtraAssembly
        << "only one assembly can be given";

  // MS3: SIGNATURE agrees with MEMBER and TYPE.
  if (TypeOK && MemberOK && SigOK) {
    if (std::optional<cli::ManagedShapeProblem> P =
            cli::checkManagedCallShape(*T, Member, *Sig)) {
      SourceLocation Loc = SigLoc;
      StringRef Text = Signature;
      InvalidArg Arg = ArgMethodSignature;
      switch (P->Arg) {
      case cli::ManagedCallArg::Type:
        Loc = TypeLoc, Text = Type, Arg = ArgMethodType;
        break;
      case cli::ManagedCallArg::Member:
        Loc = MemberLoc, Text = Member, Arg = ArgMethodMember;
        break;
      case cli::ManagedCallArg::Signature:
        break;
      }
      S.Diag(Loc, diag::err_msil_attr_invalid_arg)
          << unsigned(Arg) << Text << P->Reason;
    }
  }

  // No ASSEMBLY element when none is written, so that -ast-print prints none.
  D->addAttr(MSILMethodAttr::Create(S.Context, Type, Member, Signature,
                                    &Assembly, unsigned(HasAssembly), AL));
}

void clang::msil::handleMSILCatchAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // A lambda call operator never carries msil_method (see handleMSILMethodAttr)
  // and is not checked by checkMSILManagedCallDecl, so msil_catch is refused
  // here.
  if (isLambdaCallOperator(dyn_cast<DeclContext>(D))) {
    S.Diag(AL.getLoc(), diag::err_msil_catch_without_method);
    return;
  }
  StringRef Type;
  SourceLocation TypeLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Type, &TypeLoc))
    return;
  Expr *CodeExpr = AL.getArgAsExpr(1);
  // The entry is attached in every case below, with the invalid code 0 when
  // CODE has no value, so that checkMSILManagedCallDecl sees the msil_catch
  // list as written and does not report the trailing 'int *' as unexpected
  // (T-M4). A code that depends on a template parameter can only appear in a
  // templated declaration, and checkMSILManagedCallDecl refuses a templated
  // function declaration (T-M13 with msil_method, T-M5 without), so code 0
  // is never generated.
  std::optional<llvm::APSInt> Value;
  if (!CodeExpr->isValueDependent() && !CodeExpr->isTypeDependent()) {
    Value = CodeExpr->getIntegerConstantExpr(S.Context);
    if (!Value)
      S.Diag(CodeExpr->getExprLoc(), diag::err_attribute_argument_type)
          << AL << AANT_ArgumentIntegerConstant << CodeExpr->getSourceRange();
  }

  // MA5: EXCEPTION_TYPE parses (T-M2) and is not a catch-all type (T-M11).
  if (llvm::Expected<cli::ManagedType> T = cli::parseManagedCatchType(Type);
      !T) {
    S.Diag(TypeLoc, diag::err_msil_attr_invalid_arg)
        << unsigned(ArgCatchType) << Type << llvm::toString(T.takeError());
  } else if (cli::isRefusedCatchType(T->TypeName)) {
    S.Diag(TypeLoc, diag::err_msil_catch_type_refused) << T->TypeName;
  }

  // T-M15: CODE is from 1 to INT32_MAX.
  // (Without a value, CODE was reported above or is dependent.)
  int Code = 0;
  auto InRange = [](const llvm::APSInt &V) {
    return llvm::APSInt::compareValues(V, llvm::APSInt::get(1)) >= 0 &&
           llvm::APSInt::compareValues(V, llvm::APSInt::get(INT32_MAX)) <= 0;
  };
  if (Value && !InRange(*Value)) {
    SmallString<24> Text;
    Value->toString(Text, 10);
    S.Diag(CodeExpr->getExprLoc(), diag::err_msil_catch_code_range)
        << Text.str() << CodeExpr->getSourceRange();
  } else if (Value) {
    Code = int(Value->getExtValue());
  }

  D->addAttr(MSILCatchAttr::Create(S.Context, Type, Code, AL));
}

InheritableAttr *
clang::msil::mergeMSILManagedCallAttr(Sema &S, Decl *D,
                                      const InheritableAttr &A) {
  if (isa<MSILMethodAttr>(A)) {
    // A declaration that writes its own msil_method keeps it; MS2 compares
    // it with the first declaration's (checkMSILManagedCallDecl).
    if (D->hasAttr<MSILMethodAttr>())
      return nullptr;
    return cast<InheritableAttr>(A.clone(S.Context));
  }
  const auto &CA = cast<MSILCatchAttr>(A);
  // The list is inherited whole, in order, and only by a declaration that
  // writes no msil_catch itself. A type the declaration already has is not
  // added twice (a list has each type once, T-M15).
  for (const auto *Own : D->specific_attrs<MSILCatchAttr>())
    if (!Own->isInherited() || catchTypeName(*Own) == catchTypeName(CA))
      return nullptr;
  return cast<InheritableAttr>(A.clone(S.Context));
}

void clang::msil::checkMSILManagedCallDecl(Sema &S, FunctionDecl *FD,
                                           bool IsDefinition) {
  if (FD->isInvalidDecl())
    return;
  ASTContext &Ctx = S.Context;
  auto *MA = FD->getAttr<MSILMethodAttr>();
  SmallVector<const MSILCatchAttr *, 4> Catches = getCatches(FD);
  SmallVector<const MSILCatchAttr *, 4> OwnCatches = getOwnCatches(FD);
  bool OwnMethod = MA && !MA->isInherited();
  // The first declaration written: a library builtin's implicit declaration
  // that clang made before it does not count, so that T-M12 (not MS2)
  // reports msil_method on a builtin (CodeGen's getMSILFirstWrittenDecl
  // passes over it too). Any other implicit declaration counts, so that
  // msil_method after it is reported: after a C89 implicit declaration at a
  // call, T-M14 with the note at the call; on C++'s replaceable global
  // allocation and deallocation functions, which clang declares implicitly,
  // T-M13 first.
  const FunctionDecl *Prev = FD->getPreviousDecl();
  const FunctionDecl *Written = getFirstWrittenDecl(*FD);
  bool FirstWritten = Written == FD;

  // MS1: a namespace-scope C++ function (T-M13). Checked first, because an
  // explicit specialization of a function template follows the implicit
  // specialization declaration that deduction made, which MS2 would take for
  // an earlier declaration without the attribute.
  if (MA && (OwnMethod || FirstWritten) && S.getLangOpts().CPlusPlus) {
    if (std::optional<CXXKindSelect> K = getUnsupportedCXXKind(FD)) {
      S.Diag(MA->getLocation(), diag::err_msil_method_cxx) << unsigned(*K);
      return;
    }
  }

  // MS2: msil_method and the msil_catch list must both appear on the first
  // declaration written, and a redeclaration that writes one repeats that
  // declaration's (T-M14, with the note there). A redeclaration whose
  // previous declaration has none of that kind is the first to write it.
  // When the first declaration written has none but the previous one has,
  // an earlier redeclaration was refused for writing it first; this one is
  // compared with the previous declaration, so that the refusal is not
  // repeated on every later redeclaration.
  if ((OwnMethod || !OwnCatches.empty()) && !FirstWritten) {
    const FunctionDecl *First = FD->getFirstDecl();
    if (OwnMethod) {
      const auto *PrevMA = Prev->getAttr<MSILMethodAttr>();
      if (!PrevMA) {
        S.Diag(MA->getLocation(), diag::err_msil_attr_not_first_decl)
            << unsigned(SelectMethod) << FD;
        S.Diag(First->getLocation(), diag::note_msil_first_declared);
        return;
      }
      const auto *FirstMA = Written->getAttr<MSILMethodAttr>();
      if (!FirstMA)
        FirstMA = PrevMA;
      if (!sameMethodArgs(*MA, *FirstMA)) {
        S.Diag(MA->getLocation(), diag::err_msil_attr_differs)
            << unsigned(SelectMethod) << FD;
        S.Diag(FirstMA->getLocation(), diag::note_msil_first_declared);
        return;
      }
    }
    if (!OwnCatches.empty()) {
      SmallVector<const MSILCatchAttr *, 4> PrevCatches = getCatches(Prev);
      if (PrevCatches.empty()) {
        S.Diag(OwnCatches.front()->getLocation(),
               diag::err_msil_attr_not_first_decl)
            << unsigned(SelectCatch) << FD;
        S.Diag(First->getLocation(), diag::note_msil_first_declared);
        return;
      }
      SmallVector<const MSILCatchAttr *, 4> FirstCatches = getCatches(Written);
      if (FirstCatches.empty())
        FirstCatches = PrevCatches;
      if (!sameCatchLists(OwnCatches, FirstCatches)) {
        S.Diag(OwnCatches.front()->getLocation(), diag::err_msil_attr_differs)
            << unsigned(SelectCatch) << FD;
        S.Diag(FirstCatches.front()->getLocation(),
               diag::note_msil_first_declared);
        return;
      }
    }
  }

  // MS1: msil_catch needs msil_method on the same declaration (T-M5).
  if (!MA) {
    if (!OwnCatches.empty())
      S.Diag(OwnCatches.front()->getLocation(),
             diag::err_msil_catch_without_method);
    return;
  }

  // MS1: a declaration without a body (T-M1), on every declaration, so that
  // a definition after an attributed declaration is refused too.
  if (IsDefinition) {
    S.Diag(OwnMethod ? MA->getLocation() : FD->getLocation(),
           diag::err_msil_method_not_declaration);
    return;
  }

  // MS1: not with import_module, import_name, alias, ifunc or weak (T-M6).
  if (checkIncompatible(S, FD, MA))
    return;

  // constructor and destructor: no msil_catch (T-M20) and only the
  // parameters that the forwarder may take (T-M19), on every declaration
  // that writes one of them.
  if (checkStartupAttributes(S, FD, Catches))
    return;

  // The remaining checks belong to the first declaration: a redeclaration
  // repeats or inherits its arguments (MS2), and with them the conversion
  // list recorded on the first declaration's attribute (an inherited
  // attribute is a clone of the previous declaration's, which has it).
  if (!FirstWritten) {
    if (OwnMethod)
      if (const auto *PrevMA = Prev->getAttr<MSILMethodAttr>())
        MA->setConversions(Ctx, PrevMA->getConversions());
    return;
  }
  SourceLocation AttrLoc = MA->getLocation();

  // MS1: prototyped, not variadic, not main (T-M12).
  const auto *FPT = FD->getType()->getAs<FunctionProtoType>();
  if (!FPT || FPT->isVariadic() || FD->isMain()) {
    S.Diag(AttrLoc, diag::err_msil_method_not_prototyped);
    return;
  }
  // MS1: not a library builtin, whose calls clang may fold or expand without
  // the stub (T-M12).
  if (FD->getBuiltinID()) {
    S.Diag(AttrLoc, diag::err_msil_method_builtin) << FD;
    return;
  }
  // MS3: arguments that do not parse were reported by handleMSILMethodAttr.
  std::optional<ManagedCallSpec> Spec =
      parseManagedCallSpec(MA->getType(), MA->getMember(), MA->getSignature());
  if (!Spec)
    return;

  // MS6: each exception type once (T-M15).
  for (size_t I = 1, E = Catches.size(); I < E; ++I) {
    StringRef Name = catchTypeName(*Catches[I]);
    for (size_t J = 0; J != I; ++J) {
      if (catchTypeName(*Catches[J]) == Name) {
        S.Diag(Catches[I]->getLocation(), diag::err_msil_catch_duplicate)
            << Name;
        return;
      }
    }
  }

  // MS4 and MS5: the C parameters against the member, and one conversion per
  // pair (T-M4, T-M5, T-M3).
  ManagedCallClass R = classifyManagedCall(Ctx, *FD, *Spec, !Catches.empty());
  switch (R.Problem) {
  case ManagedCallClass::None:
    // MR1: Sema decides the conversions and CodeGen copies them into
    // "msil-managed-conv" from the attribute, so a struct that is completed
    // after this declaration does not change a handle's conversion.
    MA->setConversions(
        Ctx, cli::printManagedConvList(R.Convs.Params, R.Convs.Result));
    return;
  case ManagedCallClass::Arity:
    S.Diag(FD->getLocation(), diag::err_msil_method_arity)
        << FD << FD->getNumParams() << R.Expected << arityBreakdown(R);
    return;
  case ManagedCallClass::ErrorParam: {
    unsigned Last = FD->getNumParams() - 1;
    S.Diag(paramLoc(FD, Last), diag::err_msil_catch_error_param)
        << FD << FD->getParamDecl(Last)->getType();
    return;
  }
  case ManagedCallClass::Conversion: {
    QualType CType = R.Position == 2
                         ? FD->getReturnType()
                         : FD->getParamDecl(R.CParam - 1)->getType();
    SourceLocation Loc =
        R.Position == 2 ? FD->getLocation() : paramLoc(FD, R.CParam - 1);
    S.Diag(Loc, diag::err_msil_method_conversion)
        << R.Position << R.CParam << FD << CType << R.CILType.str();
    return;
  }
  }
}

bool clang::msil::checkMSILPragmaWeak(Sema &S, const NamedDecl &D,
                                      SourceLocation NameLoc) {
  // The declaration that the pragma's name finds is the latest one, which
  // inherits msil_method from an earlier declaration (mergeDeclAttribute).
  // The implicit weak attribute would only be checked against Attr.td's
  // mutual exclusions, so MS1's T-M6 is reported here, once.
  const auto *FD = dyn_cast<FunctionDecl>(&D);
  if (!FD || !FD->hasAttr<MSILMethodAttr>())
    return false;
  S.Diag(NameLoc, diag::err_msil_method_incompatible) << "weak";
  return true;
}

void clang::msil::checkMSILAsmLabel(Sema &S, StringRef Label,
                                    SourceLocation Loc) {
  if (!S.Context.getTargetInfo().getTriple().isOSBinFormatMSIL())
    return;
  // A leading '\01' (no user label prefix) names the same symbol.
  StringRef Name = Label;
  Name.consume_front("\01");
  bool Stub = cli::isManagedCallName(Name);
  if (Stub || cli::isManagedForwarderName(Name))
    S.Diag(Loc, diag::err_msil_reserved_asm_label)
        << Name << unsigned(Stub ? ReservedStub : ReservedForwarder);
}

bool clang::msil::isStartupManagedCall(const FunctionDecl &FD) {
  // The backend makes the body (the stub), and the forwarder of the
  // constructor or destructor calls it (plan 5.20.4 as amended), as the
  // program calls a constructor or destructor definition before main or at
  // exit. A redeclaration inherits all three attributes, so the latest
  // declaration, which Sema checks again at the end of the translation unit,
  // has them whichever declaration wrote them.
  return !FD.doesThisDeclarationHaveABody() && FD.hasAttr<MSILMethodAttr>() &&
         (FD.hasAttr<ConstructorAttr>() || FD.hasAttr<DestructorAttr>());
}
