//===-- MSILReferences.h - P1's checks of .NET references -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The backstops of MSILPrepareIR (slot P1) for the .NET references of C
/// (ImplementationPlan.md 5.21.7 RB11). A reference is the IR type
/// target("msil.ref") (cli::isManagedReferenceType): a value that IL holds in
/// a local, an argument, a result or on the evaluation stack, never in
/// memory. clang's Sema refuses every other use in C, and these rules refuse
/// what reaches the back end anyway (hand-written IR, LTO, optimizations).
/// What the back end accepts (plan 5.21.6 RI4): internal functions whose
/// address is never taken and direct calls of them; 'ret', 'phi', 'select'
/// and 'freeze'; single-element static entry-block allocas of exactly the type
/// with plain loads, stores, lifetime markers and debug records, which
/// MSILPromoteReferences (slot P15) turns into values; managed-call and cast
/// stubs; the comparison helper (cli::ReferenceEqName); llvm.fake.use.
///
/// MSILPrepareIR.cpp, a hot file, calls these functions through hooks
/// (plan 5.13.2): its function loop calls diagnoseReferenceAsmOperands before
/// lowerEmptyInlineAsm and diagnoseDefinition calls
/// diagnoseReferenceDefinition, its declaration loop calls
/// diagnoseReferenceDeclaration, and its global loop calls
/// diagnoseReferenceGlobal. Each reports through DiagnosticInfoUnsupported
/// (the texts T-RB1 to T-RB12 of plan 5.21.12) and returns true if it
/// reported; the caller then handles the function or the global as it handles
/// P1's other errors (the definition loses its body, the calls of a
/// declaration are erased, the global is erased), so later passes never meet
/// what was reported.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILREFERENCES_H
#define LLVM_LIB_TARGET_MSIL_MSILREFERENCES_H

namespace llvm {

class Function;
class GlobalVariable;
class Type;

namespace MSIL {

/// Whether \p Ty is, or contains at any depth as the element of an array, a
/// structure or a vector, the reference type target("msil.ref")
/// (cli::isManagedReferenceType). A function type contains it when its result
/// or a parameter does.
bool containsReferenceType(const Type *Ty);

/// Rule 10: reports each inline asm call of the definition \p F with an
/// operand or a result that contains the reference type (T-RB9, 'an inline
/// asm operand'). runOnModule calls it before lowerEmptyInlineAsm, which would
/// drop such an operand of an empty asm statement silently. Returns true if
/// it reported one.
bool diagnoseReferenceAsmOperands(Function &F);

/// The rules of RB11 for the definition \p F, called from diagnoseDefinition:
/// its signature (rules 1, 3, 6 and 7: an aggregate that contains the type,
/// an external or ThinLTO-promoted function, a variadic function, 'byval',
/// 'sret', 'inalloca', 'preallocated', 'byref' or 'elementtype' of the type),
/// the uses of \p F (rule 4: an address taken), a name with
/// cli::ReferenceHelperPrefix (rule 11), and its instructions (rule 5: a call
/// with the type that is not a direct, exact-type call of an internal
/// definition, a stub or the helper; rule 6: the type as a variadic argument
/// or in 'va_arg'; rule 8: a caller of setjmp, that is of a 'returns_twice'
/// function or of a declaration whose name MSIL::isSetjmpName accepts; rule 9:
/// an alloca that is not single-element, static and in the entry block, and a
/// volatile or atomic access; rule 12: any other instruction, and an intrinsic
/// outside plan 5.21.6 RI4's list). The managed-call stubs that slot P0 made
/// and the helper are exempt from rule 3. Returns true if it reported one; \p F
/// then loses its body with P1's other errors.
bool diagnoseReferenceDefinition(Function &F);

/// The rules of RB11 for the used declaration \p F, called from
/// runOnModule's declaration loop: a declaration with the type in its
/// signature (rule 3, but for the helper), its address taken (rule 4), a
/// variadic one (rule 6), 'byval' and the other attributes of rule 7, and a
/// declaration with cli::ReferenceHelperPrefix other than the helper's own
/// (rule 11: cli::ReferenceEqName with cli::isManagedReferenceEqType's
/// type). Returns true if it reported one; the calls of \p F are then erased.
bool diagnoseReferenceDeclaration(Function &F);

/// Rule 2: reports the global variable \p GV when its type or its initializer
/// contains the reference type (T-RB9, 'a global variable'), called from
/// runOnModule's global loop. The IR verifier refuses such a global too, but
/// only in builds and tools that run it. Returns true if it reported \p GV,
/// which runOnModule then erases.
bool diagnoseReferenceGlobal(GlobalVariable &GV);

} // end namespace MSIL
} // end namespace llvm

#endif // LLVM_LIB_TARGET_MSIL_MSILREFERENCES_H
