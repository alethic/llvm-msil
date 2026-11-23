//===--- CGMSILReferences.h - .NET references in C (MSIL) -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The IR of the .NET references of C (ImplementationPlan.md 5.21.5 and
// 5.21.6) on the MSIL object-format triples: a CLIReferenceType value is
// target("msil.ref"), a scalar whose null is zeroinitializer, compared through
// the helper '__msil_ref.eq' and cast to another class through a cast stub.
// No IR type names a class; the classes appear only in the stubs.
//
// This header is private to clang/lib/CodeGen. Each upstream function below
// calls one entry point through a one-line hook, on a reference type, which
// only the MSIL triples form, and the logic is in CGMSILReferences.cpp:
//   - CodeGenTypes::ConvertType: getMSILReferenceType (RG1);
//   - ScalarExprEmitter::VisitCastExpr: getMSILNullReference for
//     CK_NullToPointer (RG2) and emitMSILReferenceCast for CK_BitCast (RG5);
//   - ScalarExprEmitter's comparisons: emitMSILReferenceComparison (RG4);
//   - CodeGenFunction::GenerateCode and CodeGenFunction::EmitReturnStmt (a
//     return without a value): emitMSILMissingReturnTrap (RG12);
//   - CGStmt.cpp's emitMSILAsmStmt: diagnoseMSILReferenceAsmOperands (RS29).
// The rules that need no logic of their own are written in their hook, a case
// label or a Type::isCLIReferenceType test: TEK_Scalar
// (CodeGenFunction::getEvaluationKind, RG1), the Uninitialized kind of
// CodeGenFunction::getAutoVarInitKind (RG3) and never variably modified
// (CodeGenFunction::EmitVariablyModifiedType, RG8). A reference declared
// without an initializer gets its NULL store at the declaration (RS2, RG3) from
// Sema's zero-initialization step: an ImplicitValueInitExpr, which
// CodeGenFunction::EmitAutoVarInit stores as zeroinitializer
// (CodeGenModule::EmitNullConstant). One declared with an initializer written
// in the source gets it from a hook in CodeGenFunction::EmitAutoVarInit, which
// stores getMSILNullReference into the slot before the initializer runs (RG3 as
// amended), so a jump out of the initializer and 'String ^s = s;' read NULL.
// The debug information is CGDebugInfo::CreateType for a CLIReferenceType
// (RG7), defined in CGMSILReferences.cpp, and the cast stubs' declarations come
// from getOrCreateMSILCastDeclaration (CGMSILManagedCalls.h, plan 5.21.9 RM8).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGMSILREFERENCES_H
#define LLVM_CLANG_LIB_CODEGEN_CGMSILREFERENCES_H

#include "clang/AST/Type.h"

namespace llvm {
class Constant;
class Function;
class LLVMContext;
class Type;
class Value;
} // namespace llvm

namespace clang {

class AsmStmt;
class BinaryOperator;
class Decl;

namespace CodeGen {

class CodeGenFunction;
class CodeGenModule;

/// RG1: the IR type of every .NET reference, target("msil.ref")
/// (llvm::cli::ReferenceTypeName), whatever its class.
llvm::Type *getMSILReferenceType(llvm::LLVMContext &Ctx);

/// RG2, RI5: the null reference, target("msil.ref") zeroinitializer: the value
/// of NULL and of the other null pointer constants converted to a reference
/// (CK_NullToPointer). A reference without an initializer starts with the
/// same constant (RS2, RG3), which CodeGenModule::EmitNullConstant gives, and
/// one declared with an initializer holds this constant until its initializer
/// has run (RG3 as amended: the hook in CodeGenFunction::EmitAutoVarInit).
llvm::Constant *getMSILNullReference(llvm::LLVMContext &Ctx);

/// RI6: the declaration of the comparison helper in \p CGM's module, created
/// on first use: 'declare i1 @"__msil_ref.eq"(target("msil.ref"),
/// target("msil.ref"))' (llvm::cli::ReferenceEqName) with memory(none)
/// nounwind willreturn speculatable.
llvm::Function *getOrCreateMSILReferenceEq(CodeGenModule &CGM);

/// RG4: emits \p E, an '==' or '!=' whose operands are references (Sema has
/// converted a null pointer constant or a class to the other operand's type,
/// RS5, and rewritten each truth test of a reference into '!= NULL', RS6): a
/// call of the helper, negated for '!=', converted to the type of \p E (int).
llvm::Value *emitMSILReferenceComparison(CodeGenFunction &CGF,
                                         const BinaryOperator *E);

/// RG5: the CK_BitCast of the reference \p Src from \p SrcTy to the reference
/// type \p DestTy: \p Src itself when the canonical classes are equal or
/// \p DestTy is System.Object's reference, else a call of the cast stub
/// declaration of \p DestTy's class (getOrCreateMSILCastDeclaration), which
/// has no memory attributes, so that a cast whose value is unused still runs
/// and throws.
llvm::Value *emitMSILReferenceCast(CodeGenFunction &CGF, llvm::Value *Src,
                                   QualType SrcTy, QualType DestTy);

/// RG12 as amended: when control can reach this point (\p CGF has an
/// insertion point) and \p D is the FunctionDecl of a C function (not C++,
/// not main's implicit return) whose result is a reference, emits llvm.trap
/// and 'unreachable' and clears the insertion point, so that the program
/// stops loudly there at every optimization level; otherwise does nothing.
/// Called in one line from CodeGenFunction::GenerateCode after the body,
/// where control can reach the closing brace (before C++'s missing-return
/// path, which has the trap only at -O0), and from
/// CodeGenFunction::EmitReturnStmt for a 'return;' without a value, with
/// CurCodeDecl.
void emitMSILMissingReturnTrap(CodeGenFunction &CGF, const Decl *D);

/// RS29: emitMSILAsmStmt's check: an inline assembly operand (an output or
/// an input) that is a reference is T-R24 (err_msil_ref_asm_operand) at the
/// operand. Returns true if it reported; emitMSILAsmStmt then emits nothing
/// for \p S.
bool diagnoseMSILReferenceAsmOperands(CodeGenFunction &CGF, const AsmStmt &S);

} // namespace CodeGen
} // namespace clang

#endif // LLVM_CLANG_LIB_CODEGEN_CGMSILREFERENCES_H
