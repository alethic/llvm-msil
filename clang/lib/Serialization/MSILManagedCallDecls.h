//===- MSILManagedCallDecls.h - MSIL managed calls in AST files -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The MSIL managed-call declarations that a PCH or a module hands to the AST
// consumer although they have no body (ImplementationPlan.md 5.20.4). This
// header is private to clang/lib/Serialization: ASTWriterDecl.cpp's
// isRequiredDecl and ASTWriter::WriteDecl and ASTReaderDecl.cpp's
// ASTReader::isConsumerInterestedIn call its predicate, one hook each.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_SERIALIZATION_MSILMANAGEDCALLDECLS_H
#define LLVM_CLANG_LIB_SERIALIZATION_MSILMANAGEDCALLDECLS_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/TargetInfo.h"

namespace clang {
namespace msil {

/// Whether \p D declares an MSIL managed call that has forwarders (plan
/// 5.20.4's D6 forwarders): a function declaration on an MSIL object-format
/// triple, without a body and not templated, that has msil_method together
/// with dllexport, constructor or destructor (each its own or inherited).
/// clang CodeGen defines those forwarders also when the translation unit
/// never calls the managed call, once the declaration reaches EmitGlobal
/// (clang/lib/CodeGen/CGMSILManagedCalls.h). A PCH therefore lists such a
/// declaration among its eagerly deserialized declarations, and the AST
/// reader passes it to the consumer (ASTReader::isConsumerInterestedIn), so
/// that a unit built with -include-pch, or with clang-cl /Yc and /Yu, gets the
/// forwarders that the same unit built with -include gets. By the kind of AST
/// file:
/// - A PCH or a header module (a module-map module or a header unit) lists
///   the declaration among its eagerly deserialized declarations
///   (isRequiredDecl), as it lists a constructor or destructor defined in the
///   same header. So every unit that loads the file gets the forwarders,
///   whether or not it names the managed call. That includes a unit that
///   loads a module file without importing it: an unnamed -fmodule-file loads
///   it at startup, while -fmodule-file=<name>=<file> and implicit modules
///   load it on import. The unit also gets every submodule's forwarders,
///   including an explicit submodule's.
/// - A C++20 named module lists the declaration among its modular-codegen
///   declarations (ASTWriter::WriteDecl), so its own object defines the
///   forwarders, whether compiled from source or from the BMI.
/// - The reader passes the declaration to an importer's consumer once a name
///   lookup deserializes it (a call, a redeclaration or an unevaluated
///   operand).
inline bool isForwardedManagedCallDecl(const Decl *D) {
  const auto *FD = dyn_cast<FunctionDecl>(D);
  if (!FD || !FD->hasAttr<MSILMethodAttr>())
    return false;
  if (!FD->hasAttr<DLLExportAttr>() && !FD->hasAttr<ConstructorAttr>() &&
      !FD->hasAttr<DestructorAttr>())
    return false;
  if (FD->doesThisDeclarationHaveABody() || FD->isTemplated())
    return false;
  return FD->getASTContext().getTargetInfo().getTriple().isOSBinFormatMSIL();
}

} // namespace msil
} // namespace clang

#endif // LLVM_CLANG_LIB_SERIALIZATION_MSILMANAGEDCALLDECLS_H
