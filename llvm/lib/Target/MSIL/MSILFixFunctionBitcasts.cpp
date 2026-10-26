//===-- MSILFixFunctionBitcasts.cpp - Fix function bitcasts ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyFixFunctionBitcasts.cpp
// at d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Fix bitcasted functions.
///
/// CIL requires caller and callee signatures to match, however in LLVM,
/// some amount of slop is vaguely permitted. Detect mismatch by looking for
/// bitcasts of functions and rewrite them to use wrapper functions instead.
///
/// This doesn't catch all cases, such as when a function's address is taken in
/// one place and casted in another, but it works for many common cases.
///
/// Note that LLVM already optimizes away function bitcasts in common cases by
/// dropping arguments as needed, so this pass only ends up getting used in less
/// common cases.
///
/// Unlike WebAssembly:
/// - main is not renamed and gets no (argc, argv) wrapper: the generated entry
///   point '<msil>Main' calls main with its own signature (decision D27).
/// - A call with a variadic type of a function that is not variadic is a K&R
///   call. MSIL clang emits fixed-arity calls on every MSIL triple, but IR from
///   producers that use the native x86-64 SysV convention (native clang, whose
///   datalayout is the same, and hand-written IR) gives a call through an
///   unprototyped function type a variadic type whose fixed parameters are all
///   of the call's arguments (X86_64TargetCodeGenInfo::isNoProtoCallVariadic).
///   In C it works like a non-variadic call, so the call becomes one, and it
///   gets a wrapper only if its type still differs from the callee's.
/// - Calls of variadic functions keep their callee, so that
///   MSILTargetLowering::LowerCall diagnoses them in the caller (decision D19)
///   instead of in a wrapper.
/// - Every call whose type differs from its callee's gets a wrapper, also when
///   the arguments are only trivially castable (i64 vs ptr): LowerCall accepts
///   a direct call only if its type is the callee's.
/// - A wrapper keeps only the parameter and return attributes of the callee
///   whose types it shares; the others may not apply to its types.
/// LowerCall stays the backstop for the calls that keep a mismatched type
/// (struct types, see createWrapper).
///
/// In object mode (MSILTargetMachine::isObjectMode(), plan 5.8.10, R3) the
/// callee of a call may be defined in another object, which only msil-ld
/// sees, so:
/// - A call of a declaration for the linker (a declaration, extern_weak or
///   available_externally: Function::isDeclarationForLinker) gets no wrapper.
///   It stays a direct call with its own type, LowerCall builds it with the
///   call-site signature, and the object names the symbol with that
///   signature (one MemberRef per name and signature); msil-ld adapts it to
///   the definition.
/// - A call with a variadic type of a prototype-less declaration (it keeps its
///   variadic type and its "no-prototype" attribute in object mode,
///   MSILAddMissingPrototypes) becomes the non-variadic call of all of its
///   arguments, as a variadic call of a function that is not variadic does.
/// - A call through an alias keeps the alias as its callee: the alias is a
///   symbol of the object, and a weak one must stay overridable (plan 5.8.3
///   R1).
/// Calls of definitions keep their wrappers, and calls of truly variadic
/// functions are left to LowerCall's error (decision D19). The pass asks for
/// the mode when it runs, through the TargetPassConfig
/// (CodeGenTargetMachineImpl adds the IR passes before it creates the
/// streamer that decides the mode); without one (opt) it keeps the
/// whole-program behaviour.
///
/// MSIL only (plan 5.9.8, M20; a deviation from D30's mechanical copy that
/// section 12 records): in msil-ld's merged LTO module (the module flag
/// "msil-lto" is 1) a call of a definition follows cli::classifyCall's
/// table, so the LTO path decides as msil-ld decides for CIL objects: a call
/// whose type differs from the definition's, and a call with the
/// definition's own type where either side has an sret or byval parameter
/// (5.9.8's rows for these compare only the ABI descriptors). The call's CIL
/// signature and ABI descriptor (MSIL::getFunctionTypeSignature,
/// MSIL::getABIDescriptor over the call's own attributes, as the object
/// writer records a call site) are compared with the definition's:
/// - an adaptation is a wrapper with the table's actions (extra arguments
///   dropped, missing ones zero, int32 and native int converted, a result
///   popped, zero or converted) and a warning;
/// - a table error is an error, with the trapping '_bitcast_invalid' wrapper
///   in place so that the module stays well formed;
/// - a direct match keeps a silent wrapper only where the IR types differ
///   (i64 and ptr, i8 and i32); a call with the definition's type keeps it.
/// In both wrappers an i1, i8 or i16 argument or result, an int32 in CIL, is
/// first widened to i32 as the object path's CIL holds it (section 12): an
/// argument by the call's 'signext', a result by the definition's, and
/// otherwise zero extended. Calls of one IR type may differ in these
/// attributes, so each call is classified: the calls share a wrapper only if
/// they have the same callee, IR type, ABI descriptor and sign-extended
/// arguments, and each (callee, IR type, ABI descriptor) is reported once,
/// whatever the order of the calls. A wrapper has the 'sret' and 'byval'
/// attributes of its calls, and its call of the definition the definition's,
/// so that a later run finds them direct.
/// The wrappers keep the Phase 1 names ('<f>_bitcast'): R5 and C8 forbid
/// '<msil>' names in objects. The texts are 5.9.8's without the ' in <file>'
/// parts, since the merged module has no files; msil-ld's LTO diagnostic
/// handler prints them. Calls of declarations (native imports, or symbols of
/// other objects in object mode) keep the behaviour above.
///
/// msil-ld's LTO applies the table before LTO optimization too (plan 5.9.8,
/// section 12): MSILFixFunctionBitcastsLTOPass runs this branch at the start
/// of the LTO post-link pipelines, before "msil-lto" is set (in code
/// generation), so that InstCombine's transformConstExprCastCall cannot fold
/// a mismatched call into the definition's type first. Its output is not
/// known yet, so it adapts only the calls of definitions (an imported
/// available_externally one of ThinLTO included, which InstCombine folds
/// too) and does not look through aliases. The code generation run then
/// finds those calls retargeted, to the adapter or to the trapping wrapper,
/// and reports nothing twice. A call in an available_externally function (a
/// function that ThinLTO imported) is a copy of a call that the function's
/// own module adapts and reports, or msil-ld does: it is adapted silently.
/// The table sees only the calls that reach msil-ld's LTO still mismatched:
/// the compile step of -flto keeps its pre-link pipeline, which at -O1 and
/// above may already have folded a mismatched call inside one TU into the
/// definition's type, as 'clang -c' does on the object path, where msil-ld
/// never sees a call inside one object (section 12). A TU compiled at -O0,
/// a mismatch that cannot be folded and every mismatch across TUs reach it.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "MSILAsmPrinter.h"
#include "MSILTargetMachine.h"
#include "MSILUtilities.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CLI/CallAdapter.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>
using namespace llvm;

#define DEBUG_TYPE "msil-fix-function-bitcasts"

namespace {
class MSILFixFunctionBitcastsLegacy final : public ModulePass {
  StringRef getPassName() const override {
    return "MSIL Fix Function Bitcasts";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    ModulePass::getAnalysisUsage(AU);
  }

  bool runOnModule(Module &M) override;

public:
  static char ID;
  MSILFixFunctionBitcastsLegacy() : ModulePass(ID) {}
};
} // End anonymous namespace

char MSILFixFunctionBitcastsLegacy::ID = 0;
INITIALIZE_PASS(MSILFixFunctionBitcastsLegacy, DEBUG_TYPE,
                "Fix mismatching bitcasts for MSIL", false, false)

ModulePass *llvm::createMSILFixFunctionBitcastsLegacyPass() {
  return new MSILFixFunctionBitcastsLegacy();
}

/// "msil-lto": whether \p Attrs gives a parameter 'sret' or 'byval', the
/// attributes that make MSIL::getABIDescriptor differ between two calls of
/// one IR function type.
static bool hasAggregateABIAttr(const AttributeList &Attrs) {
  return Attrs.hasAttrSomewhere(Attribute::StructRet) ||
         Attrs.hasAttrSomewhere(Attribute::ByVal);
}

// Recursively descend the def-use lists from V to find non-bitcast users of
// bitcasts of V. In object mode a call through an alias keeps the alias (see
// the file comment), so aliases are not looked through. With SameTypeToo
// ("msil-lto", see the file comment) a call with F's own type is found too
// if it or F has an sret or byval parameter.
static void findUses(Value *V, Function &F,
                     SmallVectorImpl<std::pair<CallBase *, Function *>> &Uses,
                     bool ObjectMode, bool SameTypeToo) {
  for (User *U : V->users()) {
    if (auto *BC = dyn_cast<BitCastOperator>(U))
      findUses(BC, F, Uses, ObjectMode, SameTypeToo);
    else if (auto *A = dyn_cast<GlobalAlias>(U); A && !ObjectMode)
      findUses(A, F, Uses, ObjectMode, SameTypeToo);
    else if (auto *CB = dyn_cast<CallBase>(U)) {
      Value *Callee = CB->getCalledOperand();
      if (Callee != V)
        // Skip calls where the function isn't the callee
        continue;
      if (CB->getFunctionType() == F.getFunctionType() &&
          !(SameTypeToo && (hasAggregateABIAttr(CB->getAttributes()) ||
                            hasAggregateABIAttr(F.getAttributes()))))
        // Skip uses that are immediately called
        continue;
      Uses.push_back(std::make_pair(CB, &F));
    }
  }
}

// Give Wrapper the attributes of F that apply to it. The parameter and return
// attributes of F describe F's types, so only those of the parameters and of
// the result whose types the wrapper shares with F are kept: the verifier
// rejects, for example, an attribute on a parameter the wrapper does not have,
// or signext on a double. If the results differ, the kept parameters also lose
// the attributes that describe F's result: 'returned', and 'sret', which the
// verifier allows only in a function that returns void ('Invalid struct return
// type!'); F returns void if it has an sret parameter, so a wrapper with
// another result does not (a 'void (ptr sret(T))' callee called as
// 'i32 (ptr)'). A wrapper that returns void like F keeps sret. The wrapper has
// a real body, a call of F or a trap, so it is never naked: the verifier
// rejects a naked function that uses its arguments, and ISel would diagnose a
// naked F a second time in the wrapper.
static void setWrapperAttributes(Function *Wrapper, Function *F) {
  LLVMContext &C = F->getContext();
  FunctionType *FTy = F->getFunctionType();
  FunctionType *Ty = Wrapper->getFunctionType();
  bool SameRtnType = FTy->getReturnType() == Ty->getReturnType();
  AttributeList Attrs = F->getAttributes();
  Attrs = Attrs.removeFnAttribute(C, Attribute::Naked);
  if (!SameRtnType)
    Attrs = Attrs.removeAttributesAtIndex(C, AttributeList::ReturnIndex);
  for (unsigned I = 0, E = FTy->getNumParams(); I != E; ++I) {
    if (I >= Ty->getNumParams() || Ty->getParamType(I) != FTy->getParamType(I))
      Attrs = Attrs.removeParamAttributes(C, I);
    else if (!SameRtnType) {
      Attrs = Attrs.removeParamAttribute(C, I, Attribute::Returned);
      Attrs = Attrs.removeParamAttribute(C, I, Attribute::StructRet);
    }
  }
  Wrapper->setAttributes(Attrs);
}

// Create a wrapper function with type Ty that calls F (which may have a
// different type). Attempt to support common bitcasted function idioms:
//  - Call with more arguments than needed: arguments are dropped
//  - Call with fewer arguments than needed: arguments are filled in with poison
//  - Return value is not needed: drop it
//  - Return value needed but not present: supply a poison value
//
// Unlike WebAssembly, a wrapper is created even if all the argument types are
// trivially castable to one another (i.e. I64 vs pointer type): the call keeps
// the callee only if its type is the callee's own, which is what
// MSILTargetLowering::LowerCall checks.
//
// If there is a type mismatch that we know would result in an invalid CIL
// method then generate wrapper that contains unreachable (i.e. abort at
// runtime).  Such programs are deep into undefined behaviour territory,
// but we choose to fail at runtime rather than generate and invalid module
// or fail at compiler time.  The reason we delay the error is that we want
// to support the CMake which expects to be able to compile and link programs
// that refer to functions with entirely incorrect signatures (this is how
// CMake detects the existence of a function in a toolchain).
//
// For bitcasts that involve struct types we don't know at this stage if they
// would be equivalent at the CIL level and so we can't know if we need to
// generate a wrapper.
static Function *createWrapper(Function *F, FunctionType *Ty) {
  Module *M = F->getParent();

  Function *Wrapper = Function::Create(Ty, Function::PrivateLinkage,
                                       F->getName() + "_bitcast", M);
  setWrapperAttributes(Wrapper, F);
  BasicBlock *BB = BasicBlock::Create(M->getContext(), "body", Wrapper);
  const DataLayout &DL = BB->getDataLayout();
  IRBuilder<> Builder(BB);

  // Determine what arguments to pass.
  SmallVector<Value *, 4> Args;
  Function::arg_iterator AI = Wrapper->arg_begin();
  Function::arg_iterator AE = Wrapper->arg_end();
  FunctionType::param_iterator PI = F->getFunctionType()->param_begin();
  FunctionType::param_iterator PE = F->getFunctionType()->param_end();
  bool TypeMismatch = false;
  bool WrapperNeeded = false;

  Type *ExpectedRtnType = F->getFunctionType()->getReturnType();
  Type *RtnType = Ty->getReturnType();

  if (F->getFunctionType() != Ty)
    WrapperNeeded = true;

  for (; AI != AE && PI != PE; ++AI, ++PI) {
    Type *ArgType = AI->getType();
    Type *ParamType = *PI;

    if (ArgType == ParamType) {
      Args.push_back(&*AI);
    } else {
      if (CastInst::isBitOrNoopPointerCastable(ArgType, ParamType, DL)) {
        Args.push_back(Builder.CreateBitOrPointerCast(AI, ParamType, "cast"));
      } else if (ArgType->isStructTy() || ParamType->isStructTy()) {
        LLVM_DEBUG(dbgs() << "createWrapper: struct param type in bitcast: "
                          << F->getName() << "\n");
        WrapperNeeded = false;
      } else {
        LLVM_DEBUG(dbgs() << "createWrapper: arg type mismatch calling: "
                          << F->getName() << "\n");
        LLVM_DEBUG(dbgs() << "Arg[" << Args.size() << "] Expected: "
                          << *ParamType << " Got: " << *ArgType << "\n");
        TypeMismatch = true;
        break;
      }
    }
  }

  if (WrapperNeeded && !TypeMismatch) {
    for (; PI != PE; ++PI)
      Args.push_back(PoisonValue::get(*PI));
    if (F->isVarArg())
      for (; AI != AE; ++AI)
        Args.push_back(&*AI);

    CallInst *Call = Builder.CreateCall(F, Args);

    // Determine what value to return.
    if (RtnType->isVoidTy()) {
      Builder.CreateRetVoid();
    } else if (ExpectedRtnType->isVoidTy()) {
      LLVM_DEBUG(dbgs() << "Creating dummy return: " << *RtnType << "\n");
      Builder.CreateRet(PoisonValue::get(RtnType));
    } else if (RtnType == ExpectedRtnType) {
      Builder.CreateRet(Call);
    } else if (CastInst::isBitOrNoopPointerCastable(ExpectedRtnType, RtnType,
                                                    DL)) {
      Builder.CreateRet(Builder.CreateBitOrPointerCast(Call, RtnType, "cast"));
    } else if (RtnType->isStructTy() || ExpectedRtnType->isStructTy()) {
      LLVM_DEBUG(dbgs() << "createWrapper: struct return type in bitcast: "
                        << F->getName() << "\n");
      WrapperNeeded = false;
    } else {
      LLVM_DEBUG(dbgs() << "createWrapper: return type mismatch calling: "
                        << F->getName() << "\n");
      LLVM_DEBUG(dbgs() << "Expected: " << *ExpectedRtnType
                        << " Got: " << *RtnType << "\n");
      TypeMismatch = true;
    }
  }

  if (TypeMismatch) {
    // Create a new wrapper that simply contains `unreachable`.
    Wrapper->eraseFromParent();
    Wrapper = Function::Create(Ty, Function::PrivateLinkage,
                               F->getName() + "_bitcast_invalid", M);
    setWrapperAttributes(Wrapper, F);
    IRBuilder<> Builder(BasicBlock::Create(M->getContext(), "body", Wrapper));
    Builder.CreateUnreachable();
  } else if (!WrapperNeeded) {
    LLVM_DEBUG(dbgs() << "createWrapper: no wrapper needed: " << F->getName()
                      << "\n");
    Wrapper->eraseFromParent();
    return nullptr;
  }
  LLVM_DEBUG(dbgs() << "createWrapper: " << F->getName() << "\n");
  return Wrapper;
}

/// Whether \p M is msil-ld's merged LTO module, on which msil-ld sets the
/// module flag "msil-lto" (i32 1). A value that is not an integer constant
/// counts as absent.
static bool isMSILLTOModule(const Module &M) {
  const auto *Flag =
      mdconst::dyn_extract_or_null<ConstantInt>(M.getModuleFlag("msil-lto"));
  return Flag && !Flag->isZero();
}

/// "msil-lto": whether the calls of \p F follow cli::classifyCall's table:
/// those of a definition, which msil-ld would bind by name (plan 5.9.8),
/// and before LTO optimization (\p PostLinkLTO) those of an imported
/// available_externally one too (see the file comment).
static bool usesTable(const Function &F, bool PostLinkLTO) {
  return PostLinkLTO ? !F.isDeclaration() : !F.isDeclarationForLinker();
}

/// "msil-lto": the fields of an ABI descriptor (MSIL::getABIDescriptor,
/// without the token) as an ordered key.
using ABIKey = std::tuple<uint32_t, uint32_t, uint64_t, uint8_t,
                          std::vector<std::tuple<uint32_t, uint64_t, uint8_t>>>;

static ABIKey getABIKey(const MSILLink::SignatureInfo &SI) {
  std::vector<std::tuple<uint32_t, uint64_t, uint8_t>> Byval;
  for (const MSILLink::ByvalParam &B : SI.Byval)
    Byval.emplace_back(B.Param, B.Size, B.AlignLog2);
  return {SI.Flags, SI.FixedParams, SI.SretSize, SI.SretAlignLog2,
          std::move(Byval)};
}

/// "msil-lto": what the table's decision for a call of a definition depends
/// on, and so its report: the definition, the call's IR type (its CIL
/// signature) and the call's ABI descriptor (plan 5.9.8: msil-ld makes one
/// adapter per definition, call signature and descriptor).
using LTOReportKey = std::tuple<Function *, FunctionType *, ABIKey>;

/// "msil-lto": what the table's wrapper for the call depends on besides:
/// the arguments it sign-extends (getSignExtendedArguments).
using LTOWrapperKey = std::pair<LTOReportKey, SmallVector<unsigned, 2>>;

/// "msil-lto": \p To with the 'sret' and 'byval' attributes of \p From, and
/// their 'align' and 'alignstack', in place of its own on each of the first
/// \p NumParams parameters where either has one of the two (the parameter
/// attributes that MSIL::getABIDescriptor reads); 'sret' only if
/// \p AllowSret (the verifier rejects it in a function with a result). The
/// wrappers get them so that a later run of the table, code generation
/// after MSILFixFunctionBitcastsLTOPass, finds each call of and in a wrapper
/// a direct match (see the file comment).
static AttributeList setAggregateABIAttrs(LLVMContext &C, AttributeList To,
                                          const AttributeList &From,
                                          unsigned NumParams, bool AllowSret) {
  AttributeMask Kinds;
  Kinds.addAttribute(Attribute::StructRet);
  Kinds.addAttribute(Attribute::ByVal);
  Kinds.addAttribute(Attribute::Alignment);
  Kinds.addAttribute(Attribute::StackAlignment);
  auto HasAggregate = [](AttributeSet S) {
    return S.hasAttribute(Attribute::StructRet) ||
           S.hasAttribute(Attribute::ByVal);
  };
  for (unsigned I = 0; I != NumParams; ++I) {
    AttributeSet FromAttrs = From.getParamAttrs(I);
    if (!HasAggregate(FromAttrs) && !HasAggregate(To.getParamAttrs(I)))
      continue;
    To = To.removeParamAttributes(C, I, Kinds);
    if (!HasAggregate(FromAttrs))
      continue;
    AttrBuilder B(C);
    for (Attribute::AttrKind K :
         {Attribute::StructRet, Attribute::ByVal, Attribute::Alignment,
          Attribute::StackAlignment})
      if (FromAttrs.hasAttribute(K) && (AllowSret || K != Attribute::StructRet))
        B.addAttribute(FromAttrs.getAttribute(K));
    To = To.addParamAttributes(C, I, B);
  }
  return To;
}

/// "msil-lto": gives the calls of \p F in the wrapper \p Wrapper F's 'sret'
/// and 'byval' attributes (setAggregateABIAttrs), which a call built by
/// IRBuilder lacks. ISel reads them from F either way.
static void setCalleeABIAttrs(Function &Wrapper, const Function &F) {
  for (Instruction &I : instructions(Wrapper))
    if (auto *Call = dyn_cast<CallInst>(&I);
        Call && Call->getCalledOperand() == &F)
      Call->setAttributes(setAggregateABIAttrs(
          F.getContext(), Call->getAttributes(), F.getAttributes(),
          F.getFunctionType()->getNumParams(), /*AllowSret=*/true));
}

/// "msil-lto": the trapping wrapper of type \p Ty that a table error leaves
/// in place of \p F (createWrapper's '_bitcast_invalid'), so the module
/// stays well formed after the diagnostic. It has the 'sret' and 'byval'
/// attributes of the calls it replaces F in, \p CallAttrs (the calls of one
/// wrapper have one ABI descriptor), so that a later run of the table finds
/// them direct and reports nothing twice.
static Function *createTrapWrapper(Function *F, FunctionType *Ty,
                                   const AttributeList &CallAttrs) {
  Function *Wrapper =
      Function::Create(Ty, Function::PrivateLinkage,
                       F->getName() + "_bitcast_invalid", F->getParent());
  setWrapperAttributes(Wrapper, F);
  Wrapper->setAttributes(setAggregateABIAttrs(
      F->getContext(), Wrapper->getAttributes(), CallAttrs, Ty->getNumParams(),
      /*AllowSret=*/Ty->getReturnType()->isVoidTy()));
  IRBuilder<> Builder(BasicBlock::Create(F->getContext(), "body", Wrapper));
  Builder.CreateUnreachable();
  return Wrapper;
}

/// "msil-lto": converts \p V to \p To the way the table's adapter converts
/// the CIL value: integers and pointers by value (int32 to native int
/// sign-extends, conv.i; native int to int32 truncates, conv.i4; an i1 is
/// zero-extended, as ISel widens it), anything else by its bits. An i8 or
/// i16 is an int32 in CIL, so the wrappers first bring a narrow argument or
/// result to i32 the way the object path's CIL holds it (widenNarrowArgument,
/// widenNarrowResult), and the sign extension here is then conv.i on that
/// int32. Returns null when the types cannot be converted so (aggregates of
/// other sizes).
static Value *convertValue(IRBuilder<> &Builder, Value *V, Type *To,
                           const DataLayout &DL) {
  Type *From = V->getType();
  if (From == To)
    return V;
  auto IsIntOrPtr = [](Type *T) {
    return T->isIntegerTy() || T->isPointerTy();
  };
  if (IsIntOrPtr(From) && IsIntOrPtr(To)) {
    Type *IntPtrTy = DL.getIntPtrType(V->getContext());
    Value *I = From->isPointerTy() ? Builder.CreatePtrToInt(V, IntPtrTy) : V;
    Type *ToInt = To->isPointerTy() ? IntPtrTy : To;
    if (I->getType()->isIntegerTy(1))
      I = Builder.CreateZExtOrTrunc(I, ToInt);
    else
      I = Builder.CreateSExtOrTrunc(I, ToInt);
    return To->isPointerTy() ? Builder.CreateIntToPtr(I, To) : I;
  }
  if (CastInst::isBitOrNoopPointerCastable(From, To, DL))
    return Builder.CreateBitOrPointerCast(V, To);
  return nullptr;
}

/// "msil-lto": the result of \p Call, a call of \p F, for conversion to the
/// wrapper's result type \p To: an i1, i8 or i16 result widened to int32
/// exactly as F's CIL method returns it (SelectionDAGBuilder::visitRet and
/// MSILTargetLowering::LowerReturn), sign extended for 'signext' and
/// otherwise zero extended. Without either attribute ISel any-extends, which
/// zero-extends constants and narrow loads (ldind.u1, ldind.u2). The table's
/// int32 conversions (conv.i) then apply to that int32, as the object path's
/// adapter applies them to the CIL method's result. A result of type \p To,
/// or of another type, is unchanged.
static Value *widenNarrowResult(IRBuilder<> &Builder, CallInst *Call,
                                const Function &F, Type *To) {
  Type *RetTy = Call->getType();
  if (RetTy == To || !RetTy->isIntegerTy() || RetTy->getIntegerBitWidth() >= 32)
    return Call;
  if (F.hasRetAttribute(Attribute::SExt))
    return Builder.CreateSExt(Call, Builder.getInt32Ty());
  return Builder.CreateZExt(Call, Builder.getInt32Ty());
}

/// "msil-lto": whether the table's wrapper widens an argument of type
/// \p ArgTy for a parameter of type \p To to int32 (widenNarrowArgument):
/// an i1, i8 or i16 for a parameter of another type.
static bool isNarrowArgument(Type *ArgTy, Type *To) {
  return ArgTy != To && ArgTy->isIntegerTy() &&
         ArgTy->getIntegerBitWidth() < 32;
}

/// "msil-lto": whether the table's wrapper widens a narrow argument of type
/// \p ArgTy, argument \p ArgNo of calls with the attributes \p CallAttrs,
/// by sign: 'signext' on the call's parameter, never for an i1.
static bool signExtendsNarrowArgument(Type *ArgTy, unsigned ArgNo,
                                      const AttributeList &CallAttrs) {
  return !ArgTy->isIntegerTy(1) &&
         CallAttrs.hasParamAttr(ArgNo, Attribute::SExt);
}

/// "msil-lto": the wrapper's argument \p V, argument \p ArgNo of calls with
/// the attributes \p CallAttrs, for conversion to F's parameter type \p To:
/// an i1, i8 or i16 argument widened to int32 exactly as the object path's
/// call site materializes it (plan 5.9.8, section 12), sign extended for
/// 'signext' on the call's parameter and otherwise zero extended; an i1 is
/// always zero extended. The table's int32 conversions (conv.i) then apply
/// to that int32, as the object path's adapter applies them to the call
/// site's argument; so does a narrower parameter's truncation, as F's CIL
/// method reads the int32 it is given. An argument of type \p To, or of
/// another type, is unchanged.
static Value *widenNarrowArgument(IRBuilder<> &Builder, Value *V,
                                  unsigned ArgNo, AttributeList CallAttrs,
                                  Type *To) {
  Type *ArgTy = V->getType();
  if (!isNarrowArgument(ArgTy, To))
    return V;
  if (signExtendsNarrowArgument(ArgTy, ArgNo, CallAttrs))
    return Builder.CreateSExt(V, Builder.getInt32Ty());
  return Builder.CreateZExt(V, Builder.getInt32Ty());
}

/// "msil-lto": the arguments of calls of \p F with the type \p Ty and the
/// attributes \p CallAttrs that the table's wrapper sign-extends
/// (widenNarrowArgument): with 'zeroext' or neither attribute it
/// zero-extends them alike, so these are all that its body takes from the
/// call's attributes.
static SmallVector<unsigned, 2>
getSignExtendedArguments(const Function &F, const FunctionType *Ty,
                         const AttributeList &CallAttrs) {
  SmallVector<unsigned, 2> Args;
  const FunctionType *FTy = F.getFunctionType();
  for (unsigned I = 0, E = std::min(Ty->getNumParams(), FTy->getNumParams());
       I != E; ++I)
    if (isNarrowArgument(Ty->getParamType(I), FTy->getParamType(I)) &&
        signExtendsNarrowArgument(Ty->getParamType(I), I, CallAttrs))
      Args.push_back(I);
  return Args;
}

/// "msil-lto": the wrapper of type \p Ty that implements the table's actions
/// for calls of \p F with the attributes \p CallAttrs (those of every call
/// that shares it: the wrappers are made per sign-extended argument set,
/// getSignExtendedArguments): each of F's parameters from the call's
/// argument (a narrow integer widened to int32 by the call's extension
/// attribute, widenNarrowArgument, then converted) or zero, extra arguments
/// dropped, F's result (a narrow integer widened to int32 by F's,
/// widenNarrowResult) converted, dropped or replaced by zero. This holds for
/// a direct match too (int32 on both sides, no warning), whose wrapper
/// exists only because the IR types differ. Returns null if a type pair
/// cannot be converted (convertValue).
static Function *createTableWrapper(Function *F, FunctionType *Ty,
                                    AttributeList CallAttrs) {
  Module *M = F->getParent();
  Function *Wrapper = Function::Create(Ty, Function::PrivateLinkage,
                                       F->getName() + "_bitcast", M);
  setWrapperAttributes(Wrapper, F);
  BasicBlock *BB = BasicBlock::Create(M->getContext(), "body", Wrapper);
  const DataLayout &DL = M->getDataLayout();
  IRBuilder<> Builder(BB);
  FunctionType *FTy = F->getFunctionType();
  SmallVector<Value *, 4> Args;
  for (unsigned I = 0, E = FTy->getNumParams(); I != E; ++I) {
    Type *ParamTy = FTy->getParamType(I);
    if (I >= Ty->getNumParams()) {
      Args.push_back(Constant::getNullValue(ParamTy));
      continue;
    }
    Value *Arg = convertValue(
        Builder,
        widenNarrowArgument(Builder, Wrapper->getArg(I), I, CallAttrs, ParamTy),
        ParamTy, DL);
    if (!Arg) {
      Wrapper->eraseFromParent();
      return nullptr;
    }
    Args.push_back(Arg);
  }
  CallInst *Call = Builder.CreateCall(F, Args);
  Type *RtnType = Ty->getReturnType();
  if (RtnType->isVoidTy()) {
    Builder.CreateRetVoid();
  } else if (FTy->getReturnType()->isVoidTy()) {
    Builder.CreateRet(Constant::getNullValue(RtnType));
  } else if (Value *Ret = convertValue(
                 Builder, widenNarrowResult(Builder, Call, *F, RtnType),
                 RtnType, DL)) {
    Builder.CreateRet(Ret);
  } else {
    Wrapper->eraseFromParent();
    return nullptr;
  }
  return Wrapper;
}

/// "msil-lto": the wrapper for the calls of the definition \p F with type
/// \p Ty and the ABI descriptor \p CallABI, of which \p CB is one, by
/// cli::classifyCall's table (plan 5.9.8, see the file comment), with the
/// table's warning or error unless \p Report is false. Null if the call
/// keeps F: a direct match of F's own type, which differs from F at most in
/// attributes that the table does not compare.
static Function *createLTOWrapper(Function *F, FunctionType *Ty,
                                  const CallBase &CB,
                                  const MSILLink::SignatureInfo &CallABI,
                                  const TargetMachine &TM, bool Report) {
  const Function &Caller = *CB.getFunction();
  cli::MethodSig CallSig = MSIL::toMethodSig(
      MSIL::getFunctionTypeSignature(Ty, nullptr, Caller, TM));
  cli::MethodSig DefSig = MSIL::toMethodSig(
      MSIL::getFunctionTypeSignature(F->getFunctionType(), F, *F, TM));
  MSILLink::SignatureInfo DefABI =
      MSIL::getABIDescriptor(F->getFunctionType(), F->getAttributes(),
                             /*NoPrototype=*/false, *F, TM);
  cli::CallShape Call = cli::CallShape::get(CallSig, &CallABI);
  cli::CallShape Def = cli::CallShape::get(DefSig, &DefABI);
  cli::CallMatch Match = cli::classifyCall(Call, Def);
  if (Match.Kind == cli::CallMatchKind::Direct && Ty == F->getFunctionType())
    return nullptr;

  if (Match.Kind != cli::CallMatchKind::Direct && Report) {
    std::string Text;
    raw_string_ostream OS(Text);
    OS << "function signature mismatch: " << MSIL::getILName(*F, TM)
       << "\n>>> defined as ";
    cli::printCallShape(OS, Def);
    OS << "\n>>> called as ";
    cli::printCallShape(OS, Call);
    OS << "\n>>> ";
    if (Match.Kind == cli::CallMatchKind::Adapter)
      OS << "adapter: ";
    OS << Match.Detail;
    F->getContext().diagnose(DiagnosticInfoGeneric(
        Text, Match.Kind == cli::CallMatchKind::Error ? DS_Error : DS_Warning));
  }
  if (Match.Kind == cli::CallMatchKind::Error)
    return createTrapWrapper(F, Ty, CB.getAttributes());
  Function *Wrapper = createTableWrapper(F, Ty, CB.getAttributes());
  // An aggregate type pair: as without the flag.
  if (!Wrapper)
    Wrapper = createWrapper(F, Ty);
  if (Wrapper)
    setCalleeABIAttrs(*Wrapper, *F);
  return Wrapper;
}

/// Returns the type of a call with the variadic type \p Ty and the arguments
/// of \p CB as a non-variadic call: \p Ty without the variadic flag, so that
/// every argument is a fixed parameter (MSILAddMissingPrototypes'
/// getNonVariadicCallType computes the same type).
static FunctionType *getNonVariadicType(FunctionType *Ty, const CallBase &CB) {
  SmallVector<Type *, 8> ArgTys;
  for (const Use &Arg : CB.args())
    ArgTys.push_back(Arg->getType());
  return FunctionType::get(Ty->getReturnType(), ArgTys, /*isVarArg=*/false);
}

/// Object mode: makes every call with a variadic type of the prototype-less
/// declaration \p F (variadic, "no-prototype") the non-variadic call of all
/// of its arguments (a K&R call from IR that uses the native x86-64 SysV
/// convention, see the file comment). The call's type may be F's own type
/// ('i32 (...)' for a call without arguments), so every call counts, not
/// only those that findUses collects.
static void makeKnRCallsNonVariadic(Function &F) {
  SmallVector<Value *, 4> Worklist = {&F};
  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();
    for (User *U : V->users()) {
      if (auto *BC = dyn_cast<BitCastOperator>(U)) {
        Worklist.push_back(BC);
        continue;
      }
      auto *CB = dyn_cast<CallBase>(U);
      if (!CB || CB->getCalledOperand() != V ||
          !CB->getFunctionType()->isVarArg())
        continue;
      CB->mutateFunctionType(getNonVariadicType(CB->getFunctionType(), *CB));
    }
  }
}

/// \p TM is the target machine of the pass's TargetPassConfig, or null (opt):
/// "msil-lto"'s table needs it for the CIL signatures. \p PostLinkLTO is the
/// run before LTO optimization (MSILFixFunctionBitcastsLTOPass, see the file
/// comment), which applies the table to the calls of definitions only.
static bool fixFunctionBitcasts(Module &M, bool ObjectMode,
                                const TargetMachine *TM,
                                bool PostLinkLTO = false) {
  LLVM_DEBUG(dbgs() << "********** Fix Function Bitcasts **********\n");
  bool LTO = TM && (PostLinkLTO || isMSILLTOModule(M));

  SmallVector<std::pair<CallBase *, Function *>, 0> Uses;

  // Collect all the places that need wrappers.
  for (Function &F : M) {
    // Skip to fix when the function is swiftcc or swifttailcc because these
    // calling conventions allow bitcast type difference for swiftself,
    // swifterror, and swiftasync.
    if (F.getCallingConv() == CallingConv::Swift ||
        F.getCallingConv() == CallingConv::SwiftTail)
      continue;
    // Calls of variadic functions are not supported yet. Leave them to
    // MSILTargetLowering::LowerCall, which diagnoses them in the caller.
    // Object mode keeps prototype-less declarations variadic, and their K&R
    // calls become fixed-arity calls.
    if (F.isVarArg()) {
      if (ObjectMode && F.isDeclarationForLinker() &&
          F.hasFnAttribute("no-prototype"))
        makeKnRCallsNonVariadic(F);
      continue;
    }
    findUses(&F, F, Uses, ObjectMode || PostLinkLTO,
             /*SameTypeToo=*/LTO && usesTable(F, PostLinkLTO));
  }

  // Before LTO optimization the calls in available_externally functions come
  // last, so that a call of the module's own code decides on the
  // diagnostic of the wrapper they share (see the file comment).
  if (PostLinkLTO)
    std::stable_partition(
        Uses.begin(), Uses.end(),
        [](const std::pair<CallBase *, Function *> &U) {
          return !U.first->getFunction()->hasAvailableExternallyLinkage();
        });

  DenseMap<std::pair<Function *, FunctionType *>, Function *> Wrappers;
  // "msil-lto": the wrappers by LTOWrapperKey, and the LTOReportKeys
  // reported (see the file comment).
  std::map<LTOWrapperKey, Function *> LTOWrappers;
  std::set<LTOReportKey> LTOReported;

  for (auto &UseFunc : Uses) {
    CallBase *CB = UseFunc.first;
    Function *F = UseFunc.second;
    FunctionType *Ty = CB->getFunctionType();

    // Before LTO optimization the calls of declarations are left to code
    // generation (see the file comment).
    if (PostLinkLTO && F->isDeclaration())
      continue;

    // msil-ld's LTO module: the table for the calls of a definition, which
    // msil-ld would bind by name (plan 5.9.8).
    bool Table = LTO && usesTable(*F, PostLinkLTO);

    // F is not variadic, so a variadic call of F is a K&R call from IR that
    // uses the native x86-64 SysV convention (see the file comment): make it
    // a non-variadic call of all of its arguments.
    if (Ty->isVarArg()) {
      Ty = getNonVariadicType(Ty, *CB);
      CB->mutateFunctionType(Ty);
      if (Ty == F->getFunctionType() && !Table)
        continue;
    }

    // In object mode the definition of an undefined callee is in another
    // object: the call keeps its own type, and the object names the symbol
    // with the call-site signature (see the file comment).
    if (ObjectMode && F->isDeclarationForLinker())
      continue;

    Function *Wrapper;
    if (!Table) {
      auto Pair = Wrappers.try_emplace(std::make_pair(F, Ty));
      if (Pair.second)
        Pair.first->second = createWrapper(F, Ty);
      Wrapper = Pair.first->second;
    } else {
      // One wrapper per callee, call type, ABI descriptor and sign-extended
      // arguments; one report per callee, call type and ABI descriptor.
      MSILLink::SignatureInfo CallABI =
          MSIL::getABIDescriptor(Ty, CB->getAttributes(), /*NoPrototype=*/false,
                                 *CB->getFunction(), *TM);
      LTOReportKey ReportKey(F, Ty, getABIKey(CallABI));
      auto Pair = LTOWrappers.try_emplace(LTOWrapperKey(
          ReportKey, getSignExtendedArguments(*F, Ty, CB->getAttributes())));
      if (Pair.second) {
        // A call in an available_externally function is a copy (see the file
        // comment).
        bool Report = !CB->getFunction()->hasAvailableExternallyLinkage() &&
                      LTOReported.insert(ReportKey).second;
        Pair.first->second = createLTOWrapper(F, Ty, *CB, CallABI, *TM, Report);
      }
      Wrapper = Pair.first->second;
    }

    if (!Wrapper)
      continue;

    CB->setCalledOperand(Wrapper);
  }

  return true;
}

bool MSILFixFunctionBitcastsLegacy::runOnModule(Module &M) {
  // Asked here, when the pass runs, not when it is added (see the file
  // comment).
  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  const MSILTargetMachine *TM =
      TPC ? &TPC->getTM<MSILTargetMachine>() : nullptr;
  return fixFunctionBitcasts(M, TM && TM->isObjectMode(), TM);
}

PreservedAnalyses MSILFixFunctionBitcastsLTOPass::run(Module &M,
                                                      ModuleAnalysisManager &) {
  fixFunctionBitcasts(M, /*ObjectMode=*/false, &TM, /*PostLinkLTO=*/true);
  return PreservedAnalyses::none();
}
