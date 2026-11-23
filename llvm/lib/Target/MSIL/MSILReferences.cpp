//===-- MSILReferences.cpp - P1's checks of .NET references ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the backstops of MSILPrepareIR (slot P1) for the .NET
/// references of C (ImplementationPlan.md 5.21.7 RB11), which MSILReferences.h
/// declares. A reference is the IR type target("msil.ref")
/// (cli::isManagedReferenceType). What the back end accepts of it (plan
/// 5.21.6 RI4) and the rule that refuses everything else:
///
///   1. an array, structure or vector type that contains it (T-RB1);
///   2. a global variable that holds it (T-RB9);
///   3. a function with it in its signature that is not local (T-RB2: 'not
///      static', or, when it is hidden, made external by ThinLTO or by
///      --lto-partitions), but for the stubs of slot P0 and the comparison
///      helper;
///   4. such a function whose address is taken: any use but a direct call
///      with its own function type, where a listing in llvm.used or
///      llvm.compiler.used does not count (as for T-M10) and an aliasee or an
///      llvm.global.annotations entry does (T-RB2);
///   5. a call with it in its function type that is not a direct call with
///      the callee's own type (T-RB2); a direct call of a function that is
///      not local is rule 3's, at the function;
///   6. a variadic function with it in its signature (T-RB2), a variadic
///      argument of it and a va_arg of it (T-RB12);
///   7. 'byval', 'sret', 'inalloca', 'preallocated', 'byref' or
///      'elementtype' of it (T-RB1);
///   8. any argument, result, alloca, operand or instruction result of it in
///      a function that calls setjmp (T-RB3): a 'returns_twice' function, or
///      a declaration whose name MSIL::isSetjmpName accepts, whose calls
///      MSILLowerSjLj (slot P8) lowers with or without that attribute;
///   9. an alloca of it that is not one element, static and in the entry
///      block (T-RB1), and a volatile or atomic access of it (T-RB10);
///      plain loads and stores, whatever their pointer, are left to
///      MSILPromoteReferences (slot P15), whose T-RB4 refuses those it
///      cannot turn into values;
///  10. an inline asm operand or result of it (T-RB9), checked before
///      lowerEmptyInlineAsm, which would drop it from an empty statement;
///  11. a definition of a name that starts with cli::ReferenceHelperPrefix,
///      or a declaration of one other than the helper's own (T-RB7);
///  12. every other instruction with an operand or a result of it (T-RB11):
///      only 'ret', 'phi', 'select', 'freeze', the loads and stores of rule
///      9, the calls of rules 3-6 and, of the intrinsics, llvm.fake.use take
///      one (lifetime markers and debug records name a slot, not a value).
///
/// Rules 3-6 concern the calls of functions that are not intrinsics; an
/// intrinsic is rule 12's. Each text of plan 5.21.12 is reported through
/// DiagnosticInfoUnsupported, once per function: the first time it applies
/// in a function, at that place (a global variable's through
/// LLVMContext::emitError, after 'global variable @<name>: ', as P1's other
/// errors for globals).
///
//===----------------------------------------------------------------------===//

#include "MSILReferences.h"
#include "MSILManagedCalls.h"
#include "MSILUtilities.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CLI/ManagedCallIR.h"
#include "llvm/CLI/ManagedSignature.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace llvm;

//===----------------------------------------------------------------------===//
// The texts of plan 5.21.12
//===----------------------------------------------------------------------===//

/// T-RB1 (rules 1, 7 and 9), with \p What naming the memory.
static std::string getMemoryText(const Twine &What) {
  return ("a .NET reference cannot be held in C memory: " + What).str();
}

/// T-RB1's \p What for an aggregate type that contains the reference type.
static std::string getAggregateWhat(const Type *Ty) {
  std::string What = "the aggregate type '";
  raw_string_ostream OS(What);
  OS << *Ty << '\'';
  return What;
}

/// T-RB2 (rules 3-6), with one of the reasons below.
static std::string getSignatureText(StringRef Reason) {
  return ("a function with a .NET reference in its signature " + Reason +
          " is not supported for MSIL yet")
      .str();
}

static constexpr StringLiteral NotStatic = "that is not 'static'";
static constexpr StringLiteral MadeExternal =
    "that ThinLTO or --lto-partitions made external (link with full LTO and "
    "--lto-partitions=1)";
static constexpr StringLiteral AddressTaken = "whose address is taken";
static constexpr StringLiteral CalledOtherwise =
    "called through a pointer or with another function type";
static constexpr StringLiteral Variadic = "that is variadic";

/// T-RB3 (rule 8).
static constexpr StringLiteral SetjmpCallerText =
    "a .NET reference in a function that calls setjmp is not supported for "
    "MSIL yet";

/// T-RB7 (rule 11).
static std::string getReservedText(StringRef Name) {
  return ("'" + Name + "' is reserved for the MSIL toolchain").str();
}

/// T-RB9 (rules 2 and 10).
static constexpr StringLiteral GlobalText =
    "a .NET reference in a global variable is not supported for MSIL yet";
static constexpr StringLiteral AsmOperandText =
    "a .NET reference in an inline asm operand is not supported for MSIL yet";

/// T-RB10 (rule 9), with \p Kind 'a volatile' or 'an atomic': each
/// alternative carries its own article.
static std::string getAccessText(StringRef Kind) {
  return (Kind + " access of a .NET reference is not supported for MSIL").str();
}

/// T-RB11 (rule 12), for the instruction or intrinsic \p Name.
static std::string getOperandText(StringRef Name) {
  return ("a .NET reference as an operand or result of '" + Name +
          "' is not supported for MSIL")
      .str();
}

/// T-RB12 (rule 6).
static constexpr StringLiteral VariadicArgumentText =
    "a .NET reference as a variadic argument or as the type of 'va_arg' is "
    "not supported for MSIL";

//===----------------------------------------------------------------------===//
// Types and reports
//===----------------------------------------------------------------------===//

namespace {
/// Answers containsReferenceType for many types, caching the answer for every
/// aggregate and function type that it walks.
class ReferenceTypeFinder {
  DenseMap<const Type *, bool> Cache;

public:
  /// Whether \p Ty is, or contains as an element or as a function's result
  /// or parameter, the reference type.
  bool contains(const Type *Ty);

  /// Whether \p Ty is an array, structure or vector type that contains the
  /// reference type (rule 1).
  bool isAggregateWithReference(const Type *Ty) {
    return isa<ArrayType, StructType, VectorType>(Ty) && contains(Ty);
  }

  /// Whether the result or an operand of \p I contains the reference type,
  /// or, for an alloca, its allocated type (rule 8).
  bool hasReference(const Instruction &I) {
    if (contains(I.getType()))
      return true;
    if (const auto *AI = dyn_cast<AllocaInst>(&I);
        AI && contains(AI->getAllocatedType()))
      return true;
    return any_of(I.operands(),
                  [&](const Use &Op) { return contains(Op->getType()); });
  }

  /// The first aggregate type that contains the reference type (rule 1)
  /// among the result, the operands and the source element type of a
  /// getelementptr of \p I; null if there is none.
  Type *findAggregate(const Instruction &I) {
    if (isAggregateWithReference(I.getType()))
      return I.getType();
    for (const Use &Op : I.operands())
      if (isAggregateWithReference(Op->getType()))
        return Op->getType();
    if (const auto *GEP = dyn_cast<GetElementPtrInst>(&I);
        GEP && isAggregateWithReference(GEP->getSourceElementType()))
      return GEP->getSourceElementType();
    return nullptr;
  }
};

/// Reports the errors of one function through DiagnosticInfoUnsupported, each
/// text once: a function that keeps a problem in many places (a loop that
/// was unrolled, a header's inlined code) gets it at its first one.
class FunctionReporter {
  const Function &F;
  StringSet<> Reported;

public:
  /// Whether an error was reported.
  bool HasError = false;

  explicit FunctionReporter(const Function &F) : F(F) {}

  void report(const Twine &Text, const DiagnosticLocation &Loc) {
    HasError = true;
    std::string S = Text.str();
    if (Reported.insert(S).second)
      F.getContext().diagnose(DiagnosticInfoUnsupported(F, S, Loc));
  }
};
} // end anonymous namespace

bool ReferenceTypeFinder::contains(const Type *Ty) {
  if (cli::isManagedReferenceType(Ty))
    return true;
  // A target extension type's parameters are not elements of it, and no
  // other type nests one.
  if (!isa<ArrayType, StructType, VectorType, FunctionType>(Ty))
    return false;
  auto [It, Inserted] = Cache.try_emplace(Ty, false);
  if (!Inserted)
    return It->second;
  bool Found = any_of(Ty->subtypes(), [&](Type *Sub) { return contains(Sub); });
  // The recursion may have grown the map: look the entry up again.
  Cache[Ty] = Found;
  return Found;
}

bool MSIL::containsReferenceType(const Type *Ty) {
  return ReferenceTypeFinder().contains(Ty);
}

/// Whether the result or an operand of \p I has exactly the reference type
/// (rules 9 and 12; an aggregate that contains it is rule 1's).
static bool hasReferenceOperandOrResult(const Instruction &I) {
  return cli::isManagedReferenceType(I.getType()) ||
         any_of(I.operands(), [](const Use &Op) {
           return cli::isManagedReferenceType(Op->getType());
         });
}

/// Rule 7: reports each parameter of \p Attrs, for \p NumArgs arguments, and
/// the result, whose 'byval', 'sret', 'inalloca', 'preallocated', 'byref' or
/// 'elementtype' type contains the reference type.
static void checkTypeAttributes(const AttributeList &Attrs, unsigned NumArgs,
                                ReferenceTypeFinder &Finder,
                                FunctionReporter &R,
                                const DiagnosticLocation &Loc) {
  // T-RB1's reason for each attribute, with the article that LLVM writes
  // before its name.
  static constexpr struct {
    Attribute::AttrKind Kind;
    StringLiteral What;
  } Kinds[] = {{Attribute::ByVal, "a 'byval' parameter"},
               {Attribute::StructRet, "an 'sret' parameter"},
               {Attribute::InAlloca, "an 'inalloca' parameter"},
               {Attribute::Preallocated, "a 'preallocated' parameter"},
               {Attribute::ByRef, "a 'byref' parameter"},
               {Attribute::ElementType, "an 'elementtype' parameter"}};
  auto Check = [&](const AttributeSet &Set) {
    for (const auto &[Kind, What] : Kinds)
      if (Attribute A = Set.getAttribute(Kind);
          A.isValid() && Finder.contains(A.getValueAsType()))
        R.report(getMemoryText(What), Loc);
  };
  for (unsigned I = 0; I != NumArgs; ++I)
    Check(Attrs.getParamAttrs(I));
  Check(Attrs.getRetAttrs());
}

/// Rule 4: whether \p F, a function with the reference type in its
/// signature, has a use other than a direct call with its own function type:
/// T-M10's walk (cli::collectManagedCallHolders, as refuseNonCallUses in
/// MSILManagedCalls.cpp uses it), which ignores F's own element of llvm.used
/// and llvm.compiler.used and counts an alias, an llvm.global.annotations
/// entry and every other global that holds F.
static bool isAddressTaken(const Function &F) {
  SmallSetVector<const Instruction *, 4> Insts;
  SmallSetVector<const GlobalValue *, 4> Globals;
  cli::collectManagedCallHolders(F, Insts, Globals);
  return !Insts.empty() || !Globals.empty();
}

/// The rules for \p F as a function, a definition (\p IsDefinition) or a used
/// declaration: 11 for its name, 3, 4 and 6 when its signature has the
/// reference type, 1 for the aggregates of its signature and 7 for its
/// parameter attributes.
static void checkFunction(const Function &F, bool IsDefinition,
                          ReferenceTypeFinder &Finder, FunctionReporter &R) {
  DiagnosticLocation Loc(F.getSubprogram());
  FunctionType *FTy = F.getFunctionType();

  // Rule 11: the helper is a declaration with exactly RI6's type.
  bool IsHelper = !IsDefinition && F.getName() == cli::ReferenceEqName &&
                  cli::isManagedReferenceEqType(FTy);
  bool Reserved = cli::isReferenceHelperName(F.getName()) && !IsHelper;
  if (Reserved)
    R.report(getReservedText(F.getName()), Loc);

  if (Finder.contains(FTy)) {
    // Rule 3. A reserved name has its own error, P0's stubs are internal and
    // the helper is never bound.
    if (!F.hasLocalLinkage() && !Reserved && !IsHelper &&
        !MSIL::isManagedCallStub(F))
      R.report(
          getSignatureText(F.hasHiddenVisibility() ? MadeExternal : NotStatic),
          Loc);
    // Rule 4.
    if (isAddressTaken(F))
      R.report(getSignatureText(AddressTaken), Loc);
    // Rule 6.
    if (F.isVarArg())
      R.report(getSignatureText(Variadic), Loc);
  }

  // Rule 1: the result and each parameter.
  for (Type *Ty : FTy->subtypes())
    if (Finder.isAggregateWithReference(Ty))
      R.report(getMemoryText(getAggregateWhat(Ty)), Loc);

  // Rule 7.
  checkTypeAttributes(F.getAttributes(), F.arg_size(), Finder, R, Loc);
}

/// The name by which T-RB11 names the intrinsic \p Fn.
static StringRef getIntrinsicName(const Function &Fn) {
  Intrinsic::ID IID = Fn.getIntrinsicID();
  return IID == Intrinsic::not_intrinsic ? Fn.getName()
                                         : Intrinsic::getBaseName(IID);
}

/// The rules for the call \p CB, which is not inline asm: 7 for the call's
/// parameter attributes and, for an intrinsic, 12, else 6, 5 and 12 for its
/// operand bundles.
static void checkCall(const CallBase &CB, ReferenceTypeFinder &Finder,
                      FunctionReporter &R, const DiagnosticLocation &Loc) {
  checkTypeAttributes(CB.getAttributes(), CB.arg_size(), Finder, R, Loc);

  const auto *Fn = dyn_cast<Function>(CB.getCalledOperand());
  if (Fn && Fn->isIntrinsic()) {
    // Rule 12. llvm.fake.use, which clang emits at -Og, takes a reference;
    // a lifetime marker takes the slot.
    if (Fn->getIntrinsicID() != Intrinsic::fake_use &&
        hasReferenceOperandOrResult(CB))
      R.report(getOperandText(getIntrinsicName(*Fn)), Loc);
    return;
  }

  // Rule 6: a reference as a variadic argument.
  FunctionType *CallTy = CB.getFunctionType();
  if (CallTy->isVarArg())
    for (unsigned I = CallTy->getNumParams(), E = CB.arg_size(); I != E; ++I)
      if (cli::isManagedReferenceType(CB.getArgOperand(I)->getType())) {
        R.report(VariadicArgumentText, Loc);
        break;
      }

  // Rule 5. getCalledFunction is the callee only of a direct call with its
  // own function type; such a call of a local definition, a stub or the
  // helper is accepted, and one of a function that is not local is rule 3's
  // (the function is reported, and its calls are erased with it).
  if (Finder.contains(CallTy) && !CB.getCalledFunction())
    R.report(getSignatureText(CalledOtherwise), Loc);

  // Rule 12: a reference in an operand bundle.
  for (unsigned I = 0, E = CB.getNumOperandBundles(); I != E; ++I)
    if (any_of(CB.getOperandBundleAt(I).Inputs, [](const Use &U) {
          return cli::isManagedReferenceType(U->getType());
        }))
      R.report(getOperandText(CB.getOpcodeName()), Loc);
}

/// The rules for the instruction \p I of the definition \p F: 9 for an
/// alloca, 1 for an aggregate, 9 for an access, 6 for va_arg, the calls'
/// rules, and 12 for the rest.
static void checkInstruction(const Instruction &I, const Function &F,
                             ReferenceTypeFinder &Finder, FunctionReporter &R) {
  DiagnosticLocation Loc(I.getDebugLoc());

  // Rule 9 for a slot of the type, rule 1 for an aggregate one.
  if (const auto *AI = dyn_cast<AllocaInst>(&I)) {
    Type *Ty = AI->getAllocatedType();
    if (cli::isManagedReferenceType(Ty)) {
      if (AI->getParent() != &F.getEntryBlock())
        R.report(getMemoryText("a stack slot outside the entry block"), Loc);
      else if (AI->isArrayAllocation() || !AI->isStaticAlloca())
        R.report(getMemoryText("a stack slot of more than one element"), Loc);
    } else if (Finder.isAggregateWithReference(Ty)) {
      R.report(getMemoryText(getAggregateWhat(Ty)), Loc);
    }
    return;
  }

  // Rule 1. The aggregate is the problem, whatever the instruction does
  // with it.
  if (Type *Agg = Finder.findAggregate(I)) {
    R.report(getMemoryText(getAggregateWhat(Agg)), Loc);
    return;
  }

  // Rule 9: plain loads and stores of the type are P15's.
  auto CheckAccess = [&](bool IsVolatile, bool IsAtomic) {
    if (IsVolatile)
      R.report(getAccessText("a volatile"), Loc);
    if (IsAtomic)
      R.report(getAccessText("an atomic"), Loc);
  };
  if (const auto *LI = dyn_cast<LoadInst>(&I)) {
    if (cli::isManagedReferenceType(LI->getType()))
      CheckAccess(LI->isVolatile(), LI->isAtomic());
    return;
  }
  if (const auto *SI = dyn_cast<StoreInst>(&I)) {
    if (cli::isManagedReferenceType(SI->getValueOperand()->getType()))
      CheckAccess(SI->isVolatile(), SI->isAtomic());
    return;
  }
  if (isa<AtomicRMWInst, AtomicCmpXchgInst>(I)) {
    if (hasReferenceOperandOrResult(I))
      CheckAccess(/*IsVolatile=*/false, /*IsAtomic=*/true);
    return;
  }

  // Rule 6: va_arg of the type.
  if (const auto *VA = dyn_cast<VAArgInst>(&I)) {
    if (cli::isManagedReferenceType(VA->getType()))
      R.report(VariadicArgumentText, Loc);
    return;
  }

  if (const auto *CB = dyn_cast<CallBase>(&I)) {
    // Rule 10's: diagnoseReferenceAsmOperands has checked inline asm.
    if (!CB->isInlineAsm())
      checkCall(*CB, Finder, R, Loc);
    return;
  }

  // Rule 12.
  if (isa<ReturnInst, PHINode, SelectInst, FreezeInst>(I))
    return;
  if (hasReferenceOperandOrResult(I))
    R.report(getOperandText(I.getOpcodeName()), Loc);
}

/// Rule 8: whether \p CB calls setjmp: a 'returns_twice' function, or a
/// declaration (not an intrinsic) whose name MSIL::isSetjmpName accepts,
/// whose calls MSILLowerSjLj (slot P8) lowers as setjmp sites with or
/// without the attribute (clang does not give it to a declared _setjmp3, and
/// hand-written IR may leave it out). The callee is the called operand, as
/// P8 finds its sites, whatever the call's function type: a call through a
/// declaration without a prototype has a type of its own, so
/// getCalledFunction is null for it.
static bool callsSetjmp(const CallBase &CB) {
  if (CB.hasFnAttr(Attribute::ReturnsTwice))
    return true;
  const auto *Callee = dyn_cast<Function>(CB.getCalledOperand());
  return Callee && Callee->isDeclaration() && !Callee->isIntrinsic() &&
         MSIL::isSetjmpName(Callee->getName());
}

//===----------------------------------------------------------------------===//
// The entry points of MSILReferences.h
//===----------------------------------------------------------------------===//

bool MSIL::diagnoseReferenceAsmOperands(Function &F) {
  ReferenceTypeFinder Finder;
  FunctionReporter R(F);
  for (const Instruction &I : instructions(F)) {
    const auto *CB = dyn_cast<CallBase>(&I);
    if (!CB || !CB->isInlineAsm())
      continue;
    // The result, the arguments, and the memory of an indirect operand
    // ('elementtype').
    bool HasReference = Finder.contains(CB->getType());
    for (unsigned A = 0, E = CB->arg_size(); A != E && !HasReference; ++A) {
      Type *ElemTy = CB->getParamElementType(A);
      HasReference = Finder.contains(CB->getArgOperand(A)->getType()) ||
                     (ElemTy && Finder.contains(ElemTy));
    }
    if (HasReference)
      R.report(AsmOperandText, DiagnosticLocation(I.getDebugLoc()));
  }
  return R.HasError;
}

bool MSIL::diagnoseReferenceDefinition(Function &F) {
  ReferenceTypeFinder Finder;
  FunctionReporter R(F);
  checkFunction(F, /*IsDefinition=*/true, Finder, R);

  // Rule 8 needs the whole body: whether F calls setjmp and where a
  // reference first appears.
  bool CallsSetjmp = false;
  const Instruction *FirstReference = nullptr;
  for (const Instruction &I : instructions(F)) {
    if (const auto *CB = dyn_cast<CallBase>(&I); CB && callsSetjmp(*CB))
      CallsSetjmp = true;
    if (!FirstReference && Finder.hasReference(I))
      FirstReference = &I;
    checkInstruction(I, F, Finder, R);
  }
  if (CallsSetjmp && (FirstReference || Finder.contains(F.getFunctionType())))
    R.report(SetjmpCallerText,
             FirstReference ? DiagnosticLocation(FirstReference->getDebugLoc())
                            : DiagnosticLocation(F.getSubprogram()));
  return R.HasError;
}

bool MSIL::diagnoseReferenceDeclaration(Function &F) {
  ReferenceTypeFinder Finder;
  FunctionReporter R(F);
  checkFunction(F, /*IsDefinition=*/false, Finder, R);
  return R.HasError;
}

/// Whether the constant \p C, or a constant operand of it at any depth, has
/// a type that contains the reference type; \p Visited holds the constants
/// already walked. A global value's own initializer is not walked.
static bool constantHasReference(const Constant &C, ReferenceTypeFinder &Finder,
                                 SmallPtrSetImpl<const Constant *> &Visited) {
  if (Finder.contains(C.getType()))
    return true;
  if (isa<GlobalValue>(C) || !Visited.insert(&C).second)
    return false;
  return any_of(C.operands(), [&](const Use &Op) {
    return constantHasReference(*cast<Constant>(Op.get()), Finder, Visited);
  });
}

bool MSIL::diagnoseReferenceGlobal(GlobalVariable &GV) {
  ReferenceTypeFinder Finder;
  SmallPtrSet<const Constant *, 8> Visited;
  if (!Finder.contains(GV.getValueType()) &&
      !(GV.hasInitializer() &&
        constantHasReference(*GV.getInitializer(), Finder, Visited)))
    return false;
  // Rule 2.
  GV.getContext().emitError(Twine("global variable ") + MSIL::getIRName(GV) +
                            ": " + GlobalText);
  return true;
}
