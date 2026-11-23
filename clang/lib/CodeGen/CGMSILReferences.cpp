//===--- CGMSILReferences.cpp - .NET references in C (MSIL) ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The IR of the .NET references of C (ImplementationPlan.md 5.21.5 and
// 5.21.6) on the MSIL object-format triples, which the hooks that
// CGMSILReferences.h lists call: the IR type target("msil.ref") and its null
// value, the comparison helper '__msil_ref.eq', the casts between classes, the
// trap where a function that returns a reference falls off its end or returns
// without a value, the refusal of references as inline assembly operands, and
// the debug information of the type (CGDebugInfo::CreateType for a
// CLIReferenceType, defined here rather than in CGDebugInfo.cpp, whose switch
// only calls it).
// Sema has checked every use (SemaMSILReference.cpp); nothing here decides
// what is allowed.
//
//===----------------------------------------------------------------------===//

#include "CGMSILReferences.h"
#include "CGDebugInfo.h"
#include "CGMSILManagedCalls.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CLI/ManagedSignature.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

using namespace clang;
using namespace CodeGen;

llvm::Type *CodeGen::getMSILReferenceType(llvm::LLVMContext &Ctx) {
  return llvm::TargetExtType::get(Ctx, llvm::cli::ReferenceTypeName);
}

llvm::Constant *CodeGen::getMSILNullReference(llvm::LLVMContext &Ctx) {
  // The type has HasZeroInit (getTargetTypeInfo in llvm/lib/IR/Type.cpp), so
  // this is 'target("msil.ref") zeroinitializer'.
  return llvm::Constant::getNullValue(getMSILReferenceType(Ctx));
}

llvm::Function *CodeGen::getOrCreateMSILReferenceEq(CodeGenModule &CGM) {
  llvm::Module &M = CGM.getModule();
  llvm::LLVMContext &Ctx = M.getContext();
  llvm::Type *Ref = getMSILReferenceType(Ctx);
  auto *FTy = llvm::FunctionType::get(llvm::Type::getInt1Ty(Ctx), {Ref, Ref},
                                      /*isVarArg=*/false);
  if (llvm::Function *F = M.getFunction(llvm::cli::ReferenceEqName);
      F && F->isDeclaration() && F->getFunctionType() == FTy)
    return F;
  // When another global already has the name (an asm label gave it to a
  // declaration or a definition of another type), the module renames this
  // declaration; the backend then refuses both names (T-RB7).
  auto *F = llvm::Function::Create(FTy, llvm::GlobalValue::ExternalLinkage,
                                   llvm::cli::ReferenceEqName, M);
  F->setDoesNotAccessMemory();
  F->setDoesNotThrow();
  F->setWillReturn();
  F->addFnAttr(llvm::Attribute::Speculatable);
  return F;
}

llvm::Value *CodeGen::emitMSILReferenceComparison(CodeGenFunction &CGF,
                                                  const BinaryOperator *E) {
  assert(E->isEqualityOp() && "only '==' and '!=' compare .NET references");
  llvm::Value *LHS = CGF.EmitScalarExpr(E->getLHS());
  llvm::Value *RHS = CGF.EmitScalarExpr(E->getRHS());
  llvm::Value *Result = CGF.Builder.CreateCall(
      getOrCreateMSILReferenceEq(CGF.CGM), {LHS, RHS}, "ref.eq");
  if (E->getOpcode() == BO_NE)
    Result = CGF.Builder.CreateNot(Result, "ref.ne");
  // The i1 becomes the comparison's type (int in C), as a scalar comparison's
  // result does at the end of ScalarExprEmitter::EmitCompare.
  return CGF.EmitScalarConversion(Result, CGF.getContext().BoolTy, E->getType(),
                                  E->getExprLoc());
}

/// Whether \p Ref is the reference type of System.Object, whose key is the
/// primitive name 'object' (llvm::cli::getCLITypeKey folds 'class
/// System.Object' to it).
static bool isMSILObjectReference(const CLIReferenceType &Ref) {
  return Ref.getKey()->getName() ==
         llvm::cli::getManagedPrimitiveName(
             llvm::cli::ManagedType::TypeKind::Object);
}

llvm::Value *CodeGen::emitMSILReferenceCast(CodeGenFunction &CGF,
                                            llvm::Value *Src, QualType SrcTy,
                                            QualType DestTy) {
  assert(SrcTy->isCLIReferenceType() &&
         "only a .NET reference is cast to another class");
  ASTContext &Ctx = CGF.getContext();
  // Each key has one canonical type (ASTContext::getCLIReferenceType), so the
  // classes are equal when the canonical types are.
  const auto *Dest =
      cast<CLIReferenceType>(Ctx.getCanonicalType(DestTy).getTypePtr());
  if (Ctx.getCanonicalType(SrcTy).getTypePtr() == Dest ||
      isMSILObjectReference(*Dest))
    return Src;
  // A cast to another class is checked when the program runs: the stub's
  // castclass throws InvalidCastException, so the call stays even when its
  // value is unused (its declaration has no memory attributes).
  llvm::Function *Cast = getOrCreateMSILCastDeclaration(CGF.CGM, *Dest);
  llvm::CallBase *Call = CGF.EmitCallOrInvoke(Cast, Src, "ref.cast");
  Call->setCallingConv(Cast->getCallingConv());
  return Call;
}

void CodeGen::emitMSILMissingReturnTrap(CodeGenFunction &CGF, const Decl *D) {
  // C++ cannot form a reference yet (T-R5); its own missing-return path is
  // R-a2's. Of the other code declarations, neither a block (T-R12) nor an
  // Objective-C method (T-R5) returns a reference.
  const auto *FD = dyn_cast_or_null<FunctionDecl>(D);
  if (!CGF.HaveInsertPoint() || !FD || CGF.getLangOpts().CPlusPlus ||
      FD->hasImplicitReturnZero() || !FD->getReturnType()->isCLIReferenceType())
    return;
  // The trap call ends the block with 'unreachable' and leaves no insertion
  // point, so no dead block follows it.
  CGF.EmitTrapCallAndMakeUnreachable();
}

bool CodeGen::diagnoseMSILReferenceAsmOperands(CodeGenFunction &CGF,
                                               const AsmStmt &S) {
  bool Reported = false;
  auto Check = [&](const Expr *E) {
    if (!E->getType()->isCLIReferenceType())
      return;
    CGF.CGM.getDiags().Report(E->getExprLoc(), diag::err_msil_ref_asm_operand)
        << E->getSourceRange();
    Reported = true;
  };
  // In source order: the outputs (a read-write one once), then the inputs.
  for (const Expr *E : S.outputs())
    Check(E);
  for (const Expr *E : S.inputs())
    Check(E);
  return Reported;
}

/// RG7: a .NET reference is a forward-declared structure named by its class's
/// display name ("System.String", "string[]"), as the WebAssembly reference
/// types are (CGDebugInfo::CreateType for a BuiltinType), so that -g works.
/// Every spelling of one class gives the same uniqued node.
llvm::DIType *CGDebugInfo::CreateType(const CLIReferenceType *Ty,
                                      llvm::DIFile *U) {
  return DBuilder.createForwardDecl(
      llvm::dwarf::DW_TAG_structure_type,
      llvm::cli::getCLITypeDisplayName(Ty->getKey()->getName()), TheCU,
      TheCU->getFile(), 0);
}
