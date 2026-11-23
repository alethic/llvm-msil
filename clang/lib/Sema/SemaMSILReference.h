//===--- SemaMSILReference.h - .NET references in C (MSIL) ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The Sema rules of the .NET references of C (ImplementationPlan.md 5.21.2 to
// 5.21.4) on the MSIL object-format triples: the 'cli_type' attribute, which
// makes a struct stand for a .NET class, and 'T ^' of such a struct, a value
// of the type CLIReferenceType that lives only in IL locals, arguments,
// results and the evaluation stack, never in C memory. The rules test for
// that type, which only those triples form, or for the attribute, which only
// they accept, so the hooks change nothing elsewhere.
//
// This header is private to clang/lib/Sema; Sema.h does not change for it.
// Each upstream function below calls its entry points through one-line hooks,
// and the logic is in SemaMSILReference.cpp:
//   - SemaDeclAttr.cpp: ProcessDeclAttribute (handleCLITypeAttr, RT1-RT4;
//     diagnoseCLIReferenceDeclAttr for the attributes that a function or a
//     variable with a reference cannot have, RS21, RS25) and
//     Sema::DeclApplyPragmaWeak (diagnoseCLIReferencePragmaWeakAlias, RS21);
//   - SemaDecl.cpp: ActOnTagFinishDefinition (checkCLITypeDefinition, RT5),
//     CheckVariableDeclarationType (diagnoseCLIClassByValue, RS11;
//     checkCLIReferenceVarAttrs, RS25 for a type deduced from the initializer),
//     ActOnFunctionDeclarator (checkCLIReferenceFunctionDecl, RS3, RS11, RS15,
//     RS18, RS20), Sema::CheckParameter (diagnoseCLIReferenceFunctionType,
//     RS17: a parameter whose type decays to a function pointer) and
//     ActOnFinishFunctionBody (checkCLIReferenceSetjmpCaller, RS22);
//   - SemaType.cpp: GetFullTypeForDeclarator's BlockPointer case
//     (buildCLIReferenceType, RC2, RC6, RS24; for a function pointee,
//     diagnoseCLIReferenceFunctionType, RS17), five other BlockPointer chunk
//     sites (isCLIReferenceChunk, RC5), Sema::BuildPointerType
//     (diagnoseCLIPointee, RS10, RS12, RS17), and Sema::BuildQualifiedType and
//     Sema::BuildAddressSpaceAttr, where diagnoseCLIReferenceQualifiers runs
//     (RS23);
//   - SemaExpr.cpp: the assignment checks (convertNullToCLIReference,
//     checkCLIReferenceAssignment, RS4), Sema::CheckCompareOperands
//     (checkCLIReferenceComparison, RS5), the truth tests
//     (buildCLIReferenceTruthTest, RS6), Sema::CheckConditionalOperands
//     (checkCLIReferenceConditionalOperands, RS7), Sema::CheckAddressOfOperand
//     (diagnoseCLIReferenceAddressOf, RS13), the arguments of a call without
//     a prototype and 'va_arg' (diagnoseCLIReferenceUnprototypedArg,
//     diagnoseCLIReferenceVAArg, RS19), Sema::ActOnBlockStmtExpr, the
//     block-literal hook of diagnoseCLIReferenceFunctionType (RS17), and
//     isVariableCapturable (diagnoseCLIReferenceCapture, RS26);
//   - SemaCast.cpp's CastOperation::CheckCStyleCast (checkCLIReferenceCast,
//     RS8, RS27) and SemaOverload.cpp's checkAddressOfFunctionIsAvailable
//     (diagnoseCLIReferenceFunctionAddress, RS16).
// The rules that need no logic of their own are written in their hook with
// Type::isCLIReferenceType: the zero-initialization step of
// TryDefaultInitialization (SemaInit.cpp, RS2), the const-without-initializer
// warnings (RS2), Sema::isValidVarArgType (RS19), the jump scopes of
// JumpDiagnostics.cpp and CheckVariableDeclarationType (RS28), the sizeless
// test of Sema::RequireCompleteTypeImpl (RC3) and DeclaratorLocFiller's
// VisitCLIReferenceTypeLoc (RC5). The texts are the err_msil_ref_* and
// note_msil_ref_* diagnostics (plan 5.21.12). SemaMSILManagedCall.cpp chooses
// the 'ref' and 'this-ref' conversions of managed calls (plan 5.21.9 RM2)
// with CLIReferenceType::getKey and llvm::cli::getCLITypeKey.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_SEMA_SEMAMSILREFERENCE_H
#define LLVM_CLANG_LIB_SEMA_SEMAMSILREFERENCE_H

#include "clang/AST/OperationKinds.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/Ownership.h"
#include <cstdint>
#include <optional>

namespace clang {

enum class AssignConvertType;
class CLITypeAttr;
class Decl;
class Declarator;
class Expr;
class FunctionDecl;
class IdentifierInfo;
class NamedDecl;
class ParsedAttr;
class Sema;
class Stmt;
class TagDecl;
class ValueDecl;
class VarDecl;

namespace sema {
class CapturingScopeInfo;
} // namespace sema

namespace msil {

//===----------------------------------------------------------------------===//
// Queries
//===----------------------------------------------------------------------===//

/// The 'cli_type' attribute of the struct that \p T names, looking through
/// sugar (typedefs, elaboration, parentheses, attributes), or null when \p T
/// is no such struct.
const CLITypeAttr *getCLITypeAttr(QualType T);

/// Whether \p T is a .NET reference whose class is System.Object (key
/// "object"), to which every reference converts by itself (RS4, RS5, RS7).
bool isCLIObjectReferenceType(QualType T);

/// RC5: whether the BlockPointer chunk \p ChunkIndex of \p D builds a .NET
/// reference rather than a block pointer: the type it applies to (the
/// declaration specifiers' type, past any inner parentheses chunks) is a
/// struct with 'cli_type'. False for any other chunk.
/// GetFullTypeForDeclarator's BlockPointer case builds the reference itself
/// (buildCLIReferenceType, RC2). Five other BlockPointer chunk sites of
/// SemaType.cpp test this and treat such a chunk as no pointer:
/// distributeObjCPointerTypeAttr, distributeObjCPointerTypeAttrFromDeclarator,
/// classifyPointerDeclarator, GetFullTypeForDeclarator's nullability pointer
/// count and distributeNullabilityTypeAttr's chunk loop. The other BlockPointer
/// chunk sites stay as upstream wrote them: in C they already treat a block
/// pointer as no pointer or never meet such a chunk (only parenthesis chunks
/// may enclose it), or they act only in C++ or under Objective-C ARC, where
/// T-R5 already refuses the caret (RC6).
bool isCLIReferenceChunk(const Declarator &D, unsigned ChunkIndex);

//===----------------------------------------------------------------------===//
// The 'cli_type' attribute (RT1-RT8)
//===----------------------------------------------------------------------===//

/// ProcessDeclAttribute's case for cli_type (RT1-RT3): parses the name with
/// llvm::cli::parseCLITypeName (T-R1 with the parser's reason), checks the
/// struct's earlier declarations (RT4: the attribute belongs on the first
/// declaration, and a later one repeats its key; T-R2 with its note
/// otherwise), and attaches CLITypeAttr with the key in its fake Key
/// argument. An invalid name attaches nothing. A redeclaration that writes no
/// cli_type inherits the attribute through mergeDeclAttribute's default
/// branch for an InheritableAttr, with no hook (RT4 as built: CLIType's
/// InheritEvenIfAlreadyPresent is 0, so a redeclaration that writes its own
/// keeps it).
void handleCLITypeAttr(Sema &S, Decl *D, const ParsedAttr &AL);

/// ActOnTagFinishDefinition's step (RT5): a body on a struct with 'cli_type',
/// its own or an inherited one, is T-R3. The error fails the compilation; the
/// struct stays valid (Decl::setInvalidDecl asserts on a completed TagDecl).
void checkCLITypeDefinition(Sema &S, TagDecl *Tag);

//===----------------------------------------------------------------------===//
// Forming and qualifying the type (RC2, RC6, RS10, RS12, RS17, RS23, RS24)
//===----------------------------------------------------------------------===//

/// GetFullTypeForDeclarator's BlockPointer case on the MSIL triples, when the
/// type that the caret applies to, \p Pointee, is not a function type (RC2):
/// for an unqualified struct with 'cli_type', the reference type
/// (ASTContext::getCLIReferenceType with the struct as written) with the
/// chunk's qualifiers \p TypeQuals (DeclSpec::TQ flags), after
/// diagnoseCLIReferenceQualifiers; a qualified struct is T-R19, any other
/// type T-R4 (replacing the block-pointer errors and their '-fblocks'
/// advice), and C++, Objective-C, OpenCL and CUDA are T-R5 (RC6). Returns the
/// type, or a null type after an error (the caller marks the declarator
/// invalid). A function pointee stays a block pointer, the caller's own path.
QualType buildCLIReferenceType(Sema &S, QualType Pointee, unsigned TypeQuals,
                               SourceLocation CaretLoc);

/// Sema::BuildPointerType's check of \p Pointee: a struct with 'cli_type'
/// (T-R6, RS10), a .NET reference (T-R8, RS12), and a function type with a
/// reference in its result or a parameter (T-R12 'function pointer', RS17).
/// Returns true if it reported one; BuildPointerType then returns a null type.
bool diagnoseCLIPointee(Sema &S, QualType Pointee, SourceLocation Loc);

/// The form of a function type that T-R12 names, in its %select order.
enum class CLIRefFunctionTypeForm : uint8_t {
  FunctionPointer,
  BlockPointer,
  Block,
};

/// RS17: reports \p FnType when it is a function type whose result or a
/// parameter is a .NET reference (T-R12 with \p Form), in a translation unit
/// that has formed a reference (ASTContext::hasCLIReferenceTypes). Any type
/// may be passed: every other type returns false. The callers: a block
/// pointer (GetFullTypeForDeclarator's BlockPointer case with a function
/// pointee), a block literal (Sema::ActOnBlockStmtExpr), a function pointer
/// (diagnoseCLIPointee), and Sema::CheckParameter, which passes every
/// parameter's type before its adjustment, so that a parameter whose type
/// decays to a function pointer is reported. Returns true if it reported.
bool diagnoseCLIReferenceFunctionType(Sema &S, QualType FnType,
                                      SourceLocation Loc,
                                      CLIRefFunctionTypeForm Form);

/// RS23: reports the qualifiers \p Quals that a .NET reference type \p T
/// cannot have, 'volatile', '__unaligned' and an address space (T-R18), where
/// Sema adds qualifiers to a type (the caret's own, a typedef's, an address
/// space attribute's). 'restrict', '_Atomic' and nullability keep clang's
/// texts. Returns true if it reported one.
bool diagnoseCLIReferenceQualifiers(Sema &S, QualType T, Qualifiers Quals,
                                    SourceLocation Loc);

//===----------------------------------------------------------------------===//
// Declarations (RS3, RS11, RS15, RS18, RS20-RS22, RS25)
//===----------------------------------------------------------------------===//

/// RS11: reports \p T when it is a struct with 'cli_type' by value, the type
/// of a variable ('extern' included), a parameter or a result (T-R7). Returns
/// true if it reported; the caller marks the declaration invalid.
bool diagnoseCLIClassByValue(Sema &S, QualType T, SourceLocation Loc);

/// ActOnFunctionDeclarator's step for the function \p FD, once it has its
/// attributes and its previous declaration (beside MC-1's
/// checkMSILManagedCallDecl): a parameter or the result that is a struct
/// with 'cli_type' by value (T-R7, RS11), and for a function whose type has a
/// reference: external linkage (T-R10, RS15), no prototype (T-R13, RS18) and
/// a variadic function (T-R15, RS20). A managed call (msil_method) is exempt
/// from all three: its stub is internal, and MC-1's T-M12 already refuses a
/// managed call that has no prototype or is variadic (RM4, RM5).
void checkCLIReferenceFunctionDecl(Sema &S, FunctionDecl *FD);

/// ProcessDeclAttribute's check, before the attribute \p AL is applied to
/// \p D: 'constructor', 'destructor', 'alias', 'ifunc', 'weak', 'weakref',
/// 'dllexport' and 'annotate' on a function whose type has a reference
/// (T-R16, RS21); 'cleanup', 'uninitialized' and '__block' on a variable
/// that is a reference and 'annotate' on a variable or parameter that is a
/// reference (T-R20, RS25 as amended: its llvm.var.annotation would record
/// the slot's address).
/// A variable whose type is not yet deduced here ('__auto_type', C23 'auto')
/// is checked by checkCLIReferenceVarAttrs once its type is known. T-R20
/// names the variable or parameter; for a parameter without a name it names
/// the parameter's type, as T-R9 names an operand's type when the operand is
/// not a variable; checkCLIReferenceVarAttrs names it the same way. Returns
/// true if it reported; ProcessDeclAttribute then drops the attribute.
bool diagnoseCLIReferenceDeclAttr(Sema &S, const Decl *D, const ParsedAttr &AL);

/// Sema::DeclApplyPragmaWeak's step for '#pragma weak <Alias> = <Target>',
/// taken before the pragma declares <Alias> as a copy of \p Target with
/// implicit 'weak' and 'alias' attributes, which ProcessDeclAttribute never
/// sees. When \p Target is a function whose type has a reference, reports
/// T-R16 for 'weak' on \p Alias at \p AliasLoc, the pragma's first identifier
/// (RS21 as built; MC-1's checkMSILPragmaWeak also names the pragma 'weak'),
/// and returns true; the caller then declares no alias. Returns false
/// otherwise.
bool diagnoseCLIReferencePragmaWeakAlias(Sema &S, const NamedDecl &Target,
                                         const IdentifierInfo *Alias,
                                         SourceLocation AliasLoc);

/// Sema::CheckVariableDeclarationType's step for RS25 (as amended) when the
/// type of the variable \p VD is known only after deduction from its
/// initializer ('__auto_type', C23 'auto'): ProcessDeclAttribute runs before
/// AddInitializerToDecl deduces the type, so diagnoseCLIReferenceDeclAttr saw
/// the undeduced type. When \p VD's type is a reference, reports T-R20 at
/// each 'cleanup', 'annotate', 'uninitialized' and '__block' attribute that
/// \p VD still has, and drops it. Sema::DeduceVariableDeclarationType calls
/// CheckVariableDeclarationType once the type is set, before
/// CheckAttributesOnDeducedType would check a 'cleanup' function against a
/// pointer to the reference type. For a type written in the source the step
/// runs after ProcessDeclAttributes, whose hook has already dropped those
/// attributes, so nothing is reported twice.
void checkCLIReferenceVarAttrs(Sema &S, VarDecl *VD);

/// ActOnFinishFunctionBody's step (RS22): in a translation unit that has
/// formed a reference (ASTContext::hasCLIReferenceTypes), a function \p FD
/// whose body \p Body calls a 'returns_twice' function, setjmp among them,
/// and whose signature or body has a reference, is T-R17 with the note at
/// the first use of a reference.
void checkCLIReferenceSetjmpCaller(Sema &S, const FunctionDecl *FD,
                                   const Stmt *Body);

//===----------------------------------------------------------------------===//
// Expressions (RS4-RS8, RS13, RS16, RS19, RS26, RS27)
//===----------------------------------------------------------------------===//

/// Sema::CheckSingleAssignmentConstraints' rule beside OpenCL queue_t's
/// (RS4): when \p LHSType is a reference and \p RHS a null pointer constant
/// (NULL, 0, (void *)0, '\0', nullptr), converts \p RHS to it with
/// CK_NullToPointer and returns true; the caller returns
/// AssignConvertType::Compatible.
bool convertNullToCLIReference(Sema &S, QualType LHSType, ExprResult &RHS);

/// Sema::CheckAssignmentConstraints' rule (RS4), after its test of equal
/// types, for the canonical unqualified \p LHSType and \p RHSType: when
/// either is a reference, a reference to the System.Object reference is
/// Compatible with \p Kind CK_BitCast, and every other pair Incompatible
/// (err_typecheck_convert_incompatible, a hard error). std::nullopt when
/// neither is a reference.
std::optional<AssignConvertType> checkCLIReferenceAssignment(Sema &S,
                                                             QualType LHSType,
                                                             QualType RHSType,
                                                             CastKind &Kind);

/// Sema::CheckCompareOperands' rule (RS5) for the operator \p Opc at
/// \p Loc: '==' and '!=' of the same class, of System.Object against a class
/// (the class converts) and of a null pointer constant against a reference
/// (the constant converts) give the comparison's int type; other pairs and
/// '<', '>', '<=', '>=' are clang's invalid operands, a null type.
/// std::nullopt when neither operand is a reference.
std::optional<QualType> checkCLIReferenceComparison(Sema &S, ExprResult &LHS,
                                                    ExprResult &RHS,
                                                    SourceLocation Loc,
                                                    BinaryOperatorKind Opc);

/// RS6: the truth test of the reference \p E as the comparison 'E != NULL',
/// an int, which a condition, '!', '&&', '||' and a '?:' condition then take
/// as a scalar (Sema::CheckBooleanCondition, Sema::CreateBuiltinUnaryOp,
/// Sema::CheckLogicalOperands and checkCondition, as AMDGPU's predicate is
/// rewritten). The caller tests that \p E is a reference.
ExprResult buildCLIReferenceTruthTest(Sema &S, Expr *E);

/// Sema::CheckConditionalOperands' rule (RS7), before its null pointer
/// constant diagnostic: the same class gives it; a reference and a null
/// pointer constant give the reference (the constant converts); a class and
/// System.Object give System.Object (the class converts); two classes are
/// clang's incompatible operand types, a null type. std::nullopt when neither
/// operand is a reference.
std::optional<QualType>
checkCLIReferenceConditionalOperands(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                     SourceLocation QuestionLoc);

/// CastOperation::CheckCStyleCast's rule (RS8, RS27), before its scalar-type
/// test, for the cast of \p SrcExpr to \p DestType written at \p OpRange:
/// from a reference to a reference, CK_NoOp when the canonical classes are
/// equal, CK_BitCast otherwise (CodeGen calls a cast stub unless the target
/// is System.Object, RG5); from a null pointer constant to a reference,
/// CK_NullToPointer; any other cast to a reference is T-R22, and from a
/// reference to a scalar err_typecheck_expect_scalar_operand. Returns true
/// when \p DestType is a reference, with \p Kind set or, after T-R22,
/// \p SrcExpr made invalid, and when a reference is cast to a scalar, with
/// \p SrcExpr made invalid after the error. Every other cast returns false
/// and is left to clang's own checks, which report a cast from a reference to
/// a type that is neither a reference nor a scalar. (A cast to void is
/// CheckCStyleCast's own, before this.)
bool checkCLIReferenceCast(Sema &S, ExprResult &SrcExpr, QualType DestType,
                           CastKind &Kind, SourceRange OpRange);

/// Sema::CheckAddressOfOperand's rule beside WebAssembly's reference refusal
/// (RS13): '&r' and '__builtin_addressof(r)' of a reference are T-R9, naming
/// the variable. Returns true if it reported.
bool diagnoseCLIReferenceAddressOf(Sema &S, const Expr *Op,
                                   SourceLocation OpLoc);

/// checkAddressOfFunctionIsAvailable's rule (RS16): the address of a
/// function whose type has a reference is T-R11 when \p Complain, at \p Loc.
/// Returns true when the address is not available (whether or not it
/// complained).
bool diagnoseCLIReferenceFunctionAddress(Sema &S, const FunctionDecl *FD,
                                         SourceLocation Loc, bool Complain);

/// RS19: a reference passed to a function without a prototype is T-R14's
/// first form. Returns true if it reported.
bool diagnoseCLIReferenceUnprototypedArg(Sema &S, const Expr *Arg);

/// RS19: 'va_arg' of the reference type \p T is T-R14's second form, at
/// \p Loc (Sema::BuildVAArgExpr). Returns true if it reported.
bool diagnoseCLIReferenceVAArg(Sema &S, QualType T, SourceLocation Loc);

/// isVariableCapturable's rule (RS26): capturing the reference \p Var in a
/// block, a captured statement or an OpenMP region, which \p CSI describes,
/// is refused, with T-R21 when \p Diagnose. Returns true when the capture is
/// refused.
bool diagnoseCLIReferenceCapture(Sema &S, const sema::CapturingScopeInfo *CSI,
                                 const ValueDecl *Var, SourceLocation Loc,
                                 bool Diagnose);

} // namespace msil
} // namespace clang

#endif // LLVM_CLANG_LIB_SEMA_SEMAMSILREFERENCE_H
