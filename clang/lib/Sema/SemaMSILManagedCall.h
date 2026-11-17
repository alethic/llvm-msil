//===--- SemaMSILManagedCall.h - MSIL managed calls from C/C++ --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The checks of the msil_method and msil_catch attributes
// (ImplementationPlan.md 5.20.2 and 5.20.3), which make a C or C++ function
// declaration a call of one public .NET member on the MSIL object-format
// triples. This header is private to clang/lib/Sema (no file outside that
// directory includes it); Sema.h does not change for it:
//   - SemaDeclAttr.cpp's ProcessDeclAttribute calls handleMSILMethodAttr and
//     handleMSILCatchAttr for one attribute each (the argument checks, T-M2,
//     T-M11 and T-M15's range);
//   - SemaDecl.cpp's mergeDeclAttribute calls mergeMSILManagedCallAttr when a
//     redeclaration inherits the attributes (MS2), and ActOnFunctionDeclarator
//     calls checkMSILManagedCallDecl once the declaration has all its
//     attributes, its previous declaration and its builtin identity (MS1-MS6:
//     T-M1, T-M3 to T-M6, T-M12 to T-M15, and T-M19 and T-M20 for constructor
//     and destructor);
//   - SemaDecl.cpp's checkNewAttributesAfterDef keeps msil_method and
//     msil_catch written on a redeclaration that follows a definition (a body,
//     an alias attribute, or the alias that '#pragma weak <f> = <g>'
//     declares), which it drops with a warning for other attributes, so that
//     checkMSILManagedCallDecl reports them (MS2, T-M14);
//   - SemaDecl.cpp's ActOnPragmaWeakID calls checkMSILPragmaWeak for
//     '#pragma weak <f>' when <f> is already declared, before it adds the
//     implicit weak attribute (MS1, T-M6). A '#pragma weak' before the
//     declaration adds that attribute through ProcessPragmaWeak, before
//     checkMSILManagedCallDecl runs, which then reports it;
//   - SemaDecl.cpp's ActOnVariableDeclarator and ActOnFunctionDeclarator call
//     checkMSILAsmLabel where they attach an asm label written with __asm__
//     (T-M22). '#pragma redefine_extname' needs no call: it takes an
//     identifier, which never contains the '.' of the reserved prefixes.
//   - SemaDecl.cpp's ShouldWarnIfUnusedFileScopedDecl calls
//     isStartupManagedCall beside its exemption of a constructor or
//     destructor definition, so that a managed call with constructor or
//     destructor gets no -Wunused-function or
//     -Wunneeded-internal-declaration warning.
//   - The conversion of each C parameter and of the C result (MS5's table) is
//     decided by classifyManagedCall in SemaMSILManagedCall.cpp, which is
//     private to that file. checkMSILManagedCallDecl records
//     the decision on the declaration's MSILMethodAttr (its fake Conversions
//     argument, in the "msil-managed-conv" encoding), and
//     clang/lib/CodeGen/CGMSILManagedCalls.cpp copies that text into the IR
//     attribute without classifying again (5.20.4 MR1).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_SEMA_SEMAMSILMANAGEDCALL_H
#define LLVM_CLANG_LIB_SEMA_SEMAMSILMANAGEDCALL_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"

namespace clang {

class Decl;
class FunctionDecl;
class InheritableAttr;
class NamedDecl;
class ParsedAttr;
class Sema;

namespace msil {

//===----------------------------------------------------------------------===//
// Sema entry points (SemaMSILManagedCall.cpp)
//===----------------------------------------------------------------------===//

/// ProcessDeclAttribute's case for msil_method: checks that TYPE, MEMBER and
/// SIGNATURE parse and agree (MA1-MA3, MS3; T-M2) and attaches the attribute.
/// The attribute is attached even when an argument is invalid, so that the
/// declaration's other checks do not report the missing attribute as well;
/// checkMSILManagedCallDecl skips a declaration whose arguments do not parse.
void handleMSILMethodAttr(Sema &S, Decl *D, const ParsedAttr &AL);

/// ProcessDeclAttribute's case for msil_catch: checks EXCEPTION_TYPE (MA5;
/// T-M2 and T-M11) and the range of CODE (T-M15) and attaches the attribute,
/// for the same reason even when one of them is invalid. A CODE that is not
/// an integer constant, out of range or dependent on a template parameter is
/// recorded as the invalid code 0 (a dependent one only on a templated
/// declaration, which checkMSILManagedCallDecl refuses: T-M13 or T-M5).
void handleMSILCatchAttr(Sema &S, Decl *D, const ParsedAttr &AL);

/// mergeDeclAttribute's case for an MSILMethodAttr or MSILCatchAttr \p A of a
/// previous declaration: the clone that \p D inherits, or null. A declaration
/// that writes its own msil_method, or its own msil_catch list, inherits none
/// of that kind; checkMSILManagedCallDecl compares what it writes with the
/// first declaration that the source wrote (MS2, T-M14).
InheritableAttr *mergeMSILManagedCallAttr(Sema &S, Decl *D,
                                          const InheritableAttr &A);

/// The checks that need the whole declaration \p FD (MS1-MS6), called from
/// ActOnFunctionDeclarator when \p FD carries msil_method or msil_catch, own
/// or inherited. \p IsDefinition is true when this declaration has a body (or
/// is defaulted or deleted).
void checkMSILManagedCallDecl(Sema &S, FunctionDecl *FD, bool IsDefinition);

/// ActOnPragmaWeakID's step for '#pragma weak <name>' when <name> already
/// names the declaration \p D, before the implicit weak attribute is added: a
/// managed call cannot be weak (MS1). For a function with msil_method, own or
/// inherited, reports T-M6 ('weak') at \p NameLoc, the pragma's identifier,
/// and returns true; the caller then adds no weak attribute, so a later
/// redeclaration reports nothing more. Returns false for any other
/// declaration.
bool checkMSILPragmaWeak(Sema &S, const NamedDecl &D, SourceLocation NameLoc);

/// On the MSIL triples, reports the asm label \p Label (with or without a
/// leading '\01'), written at \p Loc on a function or variable declaration,
/// a definition or not, when it starts with a prefix that the toolchain
/// reserves (plan 5.14.2 N3; T-M22): '__msil_mc.', which starts the name of a
/// managed-call stub, or '__msil_mc_ctor.' or '__msil_mc_dtor.', which start
/// the names of the forwarders that clang's CodeGen defines for a constructor
/// or destructor managed call. A definition of such a name would replace the
/// stub or the forwarder at link time. No C identifier contains the prefixes'
/// '.', and no mangled name starts with them, so this also refuses every
/// user definition with those names. (A dllexport forwarder is named the
/// managed call's own symbol, which is not reserved.)
void checkMSILAsmLabel(Sema &S, llvm::StringRef Label, SourceLocation Loc);

/// Whether \p FD is a function declaration without a body that has
/// msil_method together with constructor or destructor (each its own or
/// inherited from an earlier declaration): a managed call that clang's
/// CodeGen calls from the forwarder it registers, whether or not anything
/// else calls it. ShouldWarnIfUnusedFileScopedDecl does not warn about it, as
/// it does not warn about a constructor or destructor definition
/// (ASTContext::DeclMustBeEmitted). msil_method exists only on the MSIL
/// triples, so this is false on every other target.
bool isStartupManagedCall(const FunctionDecl &FD);

} // namespace msil
} // namespace clang

#endif // LLVM_CLANG_LIB_SEMA_SEMAMSILMANAGEDCALL_H
