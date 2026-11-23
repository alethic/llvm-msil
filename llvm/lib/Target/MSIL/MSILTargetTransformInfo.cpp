//===-- MSILTargetTransformInfo.cpp - MSIL-specific TTI -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the MSIL-specific TargetTransformInfo implementation.
///
//===----------------------------------------------------------------------===//

#include "MSILTargetTransformInfo.h"
#include "MSILReferences.h"
#include "MSILUtilities.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

bool MSILTTIImpl::shouldBuildLookupTables() const {
  // BasicTTIImplBase says yes whenever BR_JT or BRIND is legal or custom.
  // SimplifyCFG would then turn a switch whose cases select constants into a
  // private constant array plus an address computation and a load. That needs
  // memory and global data, and it is larger IL than a CIL switch, which the
  // JIT compiles to a jump table of its own.
  return false;
}

bool MSILTTIImpl::shouldBuildRelLookupTables() const {
  // Relative lookup tables hold 32-bit differences of global addresses
  // (llvm.load.relative). Global addresses exist only at run time in CIL, in
  // a data segment allocated by the module initializer.
  return false;
}

void MSILTTIImpl::getUnrollingPreferences(
    Loop *L, ScalarEvolution &SE, TTI::UnrollingPreferences &UP,
    OptimizationRemarkEmitter *ORE) const {
  BaseT::getUnrollingPreferences(L, SE, UP, ORE);
  // The JIT optimizes small methods better than large ones, and it does its
  // own loop optimizations: no partial or runtime unrolling. Full unrolling
  // of loops with a small constant trip count stays enabled.
  UP.Partial = false;
  UP.Runtime = false;
}

//===----------------------------------------------------------------------===//
// The setjmp fence of .NET references
//===----------------------------------------------------------------------===//

/// The functions whose calls the fence tests for setjmp calls, found by name:
/// the names that MSIL::isSetjmpName accepts (rule 8 counts a call of such a
/// declaration by its name), so a name that it gains belongs here too, and
/// the other functions that clang gives returns_twice by name, under
/// -fno-builtin too (CGCall.cpp).
static constexpr StringLiteral SetjmpFunctionNames[] = {
    "setjmp",      "_setjmp", "_setjmpex",  "_setjmp3", "sigsetjmp",
    "__sigsetjmp", "vfork",   "getcontext", "savectx"};

namespace {
/// The setjmp fence of .NET references for one areInlineCompatible query.
/// P1's rule 8 (ImplementationPlan.md 5.21.7 RB11) refuses a function that
/// calls setjmp and holds a reference (T-RB3), and it runs after the
/// inliners: the fence keeps them from making such a function out of two
/// that P1 accepts. Its tests are the rule's own in MSILReferences.cpp,
/// callsSetjmp for a call of setjmp and ReferenceTypeFinder::hasReference,
/// with the function's type, for a reference: a test that found more would
/// keep calls apart that P1 accepts inlined, and one that found less would
/// let the inliners make code that P1 refuses.
///
/// The inliners ask at every call site, so a query does not walk a function
/// to find its setjmp calls: it tests the calls of the module's functions
/// named in SetjmpFunctionNames, which it reads through their use lists, at
/// a cost that does not grow with the caller or the callee. It walks a
/// function only for a reference, and only when the other one calls setjmp,
/// so in a module without setjmp calls it walks neither. It keeps nothing
/// for the next query: the inliners change the caller between queries, and
/// the pass managers never invalidate a TargetTransformInfo.
///
/// Rule 8 also counts a call that has none of those functions as an
/// operand: a call of a function of another name that has returns_twice
/// (when it is a declaration, P8 refuses the call anyway), and a call with
/// returns_twice on the call site alone, which clang emits after a
/// redeclaration that adds the attribute and through an alias. The fence
/// does not see such a call; if an inliner then makes a function with such a
/// call and a reference, P1 refuses that function with T-RB3.
class SetjmpFence {
  /// Which of the two functions call setjmp, as bits.
  enum : unsigned { CallerCalls = 1U << 0, CalleeCalls = 1U << 1 };

  /// MSIL::containsReferenceType's answers for the types met so far that
  /// hold other types, which it walks anew on every call.
  SmallDenseMap<const Type *, bool, 8> Answers;

  bool containsReference(const Type *Ty);
  bool hasReference(const Instruction &I);
  bool holdsReference(const Function &F);
  static unsigned findSetjmpCalls(const Function &Caller,
                                  const Function &Callee);

public:
  /// Whether the fence separates \p Caller from \p Callee: \p Callee holds a
  /// reference and \p Caller calls setjmp, or \p Callee calls setjmp and
  /// \p Caller holds a reference. Inlining would leave \p Caller with both.
  bool separates(const Function &Caller, const Function &Callee);
};
} // end anonymous namespace

/// MSIL::containsReferenceType(\p Ty), kept in Answers for a type that holds
/// other types.
bool SetjmpFence::containsReference(const Type *Ty) {
  // A type that holds no other type contains the reference type only by
  // being it, and the reference type is a target extension type: most types
  // need no call.
  if (Ty->getNumContainedTypes() == 0)
    return isa<TargetExtType>(Ty) && MSIL::containsReferenceType(Ty);
  auto [It, Inserted] = Answers.try_emplace(Ty, false);
  if (Inserted)
    It->second = MSIL::containsReferenceType(Ty);
  return It->second;
}

/// ReferenceTypeFinder::hasReference(I): whether the result or an operand of
/// \p I contains the reference type, or, for an alloca, its allocated type.
bool SetjmpFence::hasReference(const Instruction &I) {
  if (containsReference(I.getType()))
    return true;
  if (const auto *AI = dyn_cast<AllocaInst>(&I);
      AI && containsReference(AI->getAllocatedType()))
    return true;
  return any_of(I.operands(), [&](const Use &Op) {
    return containsReference(Op->getType());
  });
}

/// Rule 8's test for a reference in \p F, as diagnoseReferenceDefinition
/// applies it: the function's type, then each instruction, up to the first.
bool SetjmpFence::holdsReference(const Function &F) {
  return containsReference(F.getFunctionType()) ||
         any_of(instructions(F),
                [&](const Instruction &I) { return hasReference(I); });
}

/// callsSetjmp(CB): a call with the returns_twice attribute, or a call whose
/// called operand is a declaration, not an intrinsic, whose name
/// MSIL::isSetjmpName accepts, whatever the call's function type
/// (MSILLowerSjLj lowers both as setjmp sites).
static bool isSetjmpCall(const CallBase &CB) {
  if (CB.hasFnAttr(Attribute::ReturnsTwice))
    return true;
  const auto *Callee = dyn_cast<Function>(CB.getCalledOperand());
  return Callee && Callee->isDeclaration() && !Callee->isIntrinsic() &&
         MSIL::isSetjmpName(Callee->getName());
}

/// Which of \p Caller and \p Callee (both bits when they are one function)
/// make a setjmp call among the uses of the functions of SetjmpFunctionNames
/// in their module. Neither function is walked.
unsigned SetjmpFence::findSetjmpCalls(const Function &Caller,
                                      const Function &Callee) {
  const Module &M = *Callee.getParent();
  unsigned Found = 0;
  for (StringRef Name : SetjmpFunctionNames) {
    const Function *Fn = M.getFunction(Name);
    if (!Fn)
      continue;
    for (const User *U : Fn->users()) {
      const auto *CB = dyn_cast<CallBase>(U);
      if (!CB || !CB->getParent())
        continue;
      // The call's function first: a call elsewhere decides nothing here.
      const Function *F = CB->getFunction();
      unsigned Bits = 0;
      if (F == &Caller)
        Bits |= CallerCalls;
      if (F == &Callee)
        Bits |= CalleeCalls;
      if ((Bits & ~Found) && isSetjmpCall(*CB)) {
        Found |= Bits;
        if (Found == (CallerCalls | CalleeCalls))
          return Found;
      }
    }
  }
  return Found;
}

bool SetjmpFence::separates(const Function &Caller, const Function &Callee) {
  // Neither function calls setjmp, as in every query of a module without
  // setjmp calls: no walk.
  unsigned Calls = findSetjmpCalls(Caller, Callee);
  if (!Calls)
    return false;
  // A recursive call has the same function on both sides.
  if (&Caller == &Callee)
    return holdsReference(Callee);
  // Then a reference across each setjmp call: in the callee, often much
  // smaller, against the caller's call, then in the caller against the
  // callee's.
  return ((Calls & CallerCalls) && holdsReference(Callee)) ||
         ((Calls & CalleeCalls) && holdsReference(Caller));
}

bool MSILTTIImpl::areInlineCompatible(const Function *Caller,
                                      const Function *Callee) const {
  // clang's Sema (RS22, T-R17) checks each function as written and P1's
  // rule 8 (T-RB3) each function after the inliners, which by themselves
  // keep back only a callee that calls a returns_twice function without
  // having that attribute itself (isInlineViable).
  // getAttributeBasedInliningDecision asks this hook at every call site
  // before it honors always_inline, so the AlwaysInliner, flatten and the LTO
  // pipelines keep to the fence too. The fence's refusal comes first.
  if (SetjmpFence().separates(*Caller, *Callee))
    return false;
  // Every other call keeps the default rule, so a module without references
  // inlines as before.
  return BaseT::areInlineCompatible(Caller, Callee);
}

bool MSILTTIImpl::areTypesABICompatible(const Function *Caller,
                                        const Function *Callee,
                                        ArrayRef<Type *> Types) const {
  // Every type, half and bfloat included: both pass as their 16-bit pattern
  // in an int32 (MSILTargetLowering::getRegisterTypeForCallingConv), so a
  // parameter that ArgumentPromotion makes from a pointer parameter costs no
  // conversion helper at the call.
  return BaseT::areTypesABICompatible(Caller, Callee, Types);
}

unsigned MSILTTIImpl::getNumberOfRegisters(unsigned ClassID) const {
  // No vector types in CIL yet. Zero vector registers turn off LoopVectorize,
  // SLPVectorizer and VectorCombine. Scalars live in an unbounded set of CIL
  // locals; keep the generic answer for them.
  bool Vector = ClassID == getRegisterClassForType(/*Vector=*/true);
  return Vector ? 0 : BaseT::getNumberOfRegisters(ClassID);
}

TypeSize MSILTTIImpl::getRegisterBitWidth(TTI::RegisterKind K) const {
  switch (K) {
  case TTI::RGK_Scalar:
    // The widest scalar register is a native int.
    return TypeSize::getFixed(getDataLayout().getPointerSizeInBits());
  case TTI::RGK_FixedWidthVector:
    return TypeSize::getFixed(0);
  case TTI::RGK_ScalableVector:
    return TypeSize::getScalable(0);
  }
  llvm_unreachable("Unsupported register kind");
}
