//===-- MSILPromoteReferences.cpp - .NET reference slots to values --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Turns the stack slots of .NET references into values (ImplementationPlan.md
/// 5.21.7 RB10; slot P15 of the IR pipeline, 5.13.6).
///
/// A reference, the IR type target("msil.ref") (cli::isManagedReferenceType),
/// is an IL 'object', which the runtime tracks only in locals, arguments,
/// results and on the evaluation stack, never in memory (Design.md section 4:
/// C memory never holds a GC reference). clang's Sema refuses every C form
/// that would put one in memory, but at -O0 every automatic variable is a
/// stack slot (an alloca), and clang spills every parameter into one.
/// Instruction selection has no memory form of a reference: its LOAD and STORE
/// of one report an error (T-RB6, the backstop behind this pass).
///
/// MSILPassConfig::addISelPrepare adds the pass after AtomicExpand and before
/// TargetPassConfig::addISelPrepare(), so after CodeGenPrepare and
/// addPassesToHandleExceptions, where an exception model's preparation would
/// run: it can demote PHI nodes into new slots. The pass runs at every
/// optimization level, in msil-ld's LTO code generation and on optnone
/// functions alike:
/// - Each static alloca of exactly the reference type, one element in the
///   entry block, that isAllocaPromotable accepts and whose loads and stores
///   all have exactly that type, is promoted with PromoteMemToReg; no other
///   alloca is (LLVM issue 81575). A function without such slots is left as
///   it is, so a program without references compiles to the same IL.
/// - A slot that an exception handler uses (a use in an EH pad or in a block
///   that one reaches) is never promoted: it is an error (T-RB8). No EH pad
///   survives slots P3 and P4 yet (MSILPassConfig::addIRPasses).
/// - Any other alloca that contains the type, and any load or store of the
///   type through another pointer, is an error (T-RB4) that gives its
///   reason: the slot is copied as memory (a memory intrinsic uses it, a load
///   or store of another type accesses it, or it holds an aggregate), the
///   slot's address escapes (any other use), or a load or a store through a
///   pointer (the access itself, for a slot that is not static or one
///   element, or one accessed volatile or atomic, and for every other
///   pointer). MSILPrepareIR (slot P1) refuses most of these shapes first
///   (MSILReferences.cpp, RB11 rules 1, 7 and 9).
/// After an error the function's body becomes a lone 'unreachable', so the
/// function stays defined for its callers, and the compilation fails (plan
/// section 2, 'Diagnostics leave well-formed output'). Each text is reported
/// once per function, at its first place.
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "MSILReferences.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CLI/ManagedCallIR.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include <string>

using namespace llvm;

#define DEBUG_TYPE "msil-promote-references"

namespace {
class MSILPromoteReferencesLegacy final : public ModulePass {
  StringRef getPassName() const override { return "MSIL Promote References"; }

  bool runOnModule(Module &M) override;

public:
  static char ID;
  MSILPromoteReferencesLegacy() : ModulePass(ID) {}
};
} // end anonymous namespace

char MSILPromoteReferencesLegacy::ID = 0;
INITIALIZE_PASS(MSILPromoteReferencesLegacy, DEBUG_TYPE,
                "MSIL Promote References", false, false)

ModulePass *llvm::createMSILPromoteReferencesLegacyPass() {
  return new MSILPromoteReferencesLegacy();
}

/// T-RB4 (plan 5.21.12), with one of the reasons below.
static std::string getLocalText(StringRef Reason) {
  return ("a .NET reference that cannot become an IL local is not supported "
          "for MSIL: " +
          Reason)
      .str();
}

static constexpr StringLiteral AddressEscapes = "the slot's address escapes";
static constexpr StringLiteral CopiedAsMemory = "the slot is copied as memory";
static constexpr StringLiteral LoadThroughPointer = "a load through a pointer";
static constexpr StringLiteral StoreThroughPointer =
    "a store through a pointer";

/// T-RB8.
static constexpr StringLiteral HandlerText =
    "a .NET reference live across an exception handler is not supported for "
    "MSIL yet";

namespace {
/// Reports the errors of one function, each text once, at its first place.
class FunctionReporter {
  const Function &F;
  StringSet<> Reported;

public:
  /// Whether an error was reported.
  bool HasError = false;

  explicit FunctionReporter(const Function &F) : F(F) {}

  void report(const Twine &Text, const Instruction &At) {
    HasError = true;
    std::string S = Text.str();
    if (Reported.insert(S).second)
      F.getContext().diagnose(DiagnosticInfoUnsupported(
          F, S, DiagnosticLocation(At.getDebugLoc())));
  }
};
} // end anonymous namespace

/// Whether \p I is a load or a store of a type that contains the reference
/// type (the stored value's type, for a store).
static bool isReferenceAccess(const Instruction &I) {
  if (const auto *LI = dyn_cast<LoadInst>(&I))
    return MSIL::containsReferenceType(LI->getType());
  if (const auto *SI = dyn_cast<StoreInst>(&I))
    return MSIL::containsReferenceType(SI->getValueOperand()->getType());
  return false;
}

/// Whether \p U, a user of an alloca, is a use that isAllocaPromotable accepts
/// besides loads and stores: a lifetime marker, a droppable use (an operand
/// bundle of llvm.assume) or llvm.fake.use.
static bool isMarker(const User *U) {
  const auto *II = dyn_cast<IntrinsicInst>(U);
  return II && (II->isLifetimeStartOrEnd() || II->isDroppable() ||
                II->getIntrinsicID() == Intrinsic::fake_use);
}

/// Whether \p U, the use of a slot by the load or store \p I, accesses the
/// slot as a value of another type than \p Ty, its allocated type.
static bool accessesAsOtherType(const Use &U, const Instruction &I, Type *Ty) {
  if (const auto *LI = dyn_cast<LoadInst>(&I))
    return LI->getType() != Ty;
  if (const auto *SI = dyn_cast<StoreInst>(&I))
    return U.getOperandNo() == SI->getPointerOperandIndex() &&
           SI->getValueOperand()->getType() != Ty;
  return false;
}

/// Whether \p AI is a slot that the pass promotes: exactly the reference type,
/// one element, static (in the entry block), accepted by isAllocaPromotable,
/// and loaded and stored only as its allocated type.
static bool isPromotableSlot(const AllocaInst &AI) {
  Type *Ty = AI.getAllocatedType();
  if (!cli::isManagedReferenceType(Ty) || AI.isArrayAllocation() ||
      !AI.isStaticAlloca() || !isAllocaPromotable(&AI))
    return false;
  return none_of(AI.uses(), [&](const Use &U) {
    return accessesAsOtherType(U, *cast<Instruction>(U.getUser()), Ty);
  });
}

/// The blocks of \p F that an exception handler reaches: each EH pad and each
/// block reachable from one; none if \p F has no EH pad.
static SmallPtrSet<const BasicBlock *, 8> getHandlerBlocks(const Function &F) {
  SmallPtrSet<const BasicBlock *, 8> Blocks;
  SmallVector<const BasicBlock *, 8> Worklist;
  for (const BasicBlock &BB : F)
    if (BB.isEHPad() && Blocks.insert(&BB).second)
      Worklist.push_back(&BB);
  while (!Worklist.empty())
    for (const BasicBlock *Succ : successors(Worklist.pop_back_val()))
      if (Blocks.insert(Succ).second)
        Worklist.push_back(Succ);
  return Blocks;
}

/// The first user of \p AI in \p Handlers (getHandlerBlocks), or null.
static const Instruction *
findHandlerUse(const AllocaInst &AI,
               const SmallPtrSetImpl<const BasicBlock *> &Handlers) {
  if (Handlers.empty())
    return nullptr;
  for (const User *U : AI.users())
    if (const auto *I = dyn_cast<Instruction>(U);
        I && Handlers.contains(I->getParent()))
      return I;
  return nullptr;
}

/// T-RB4's reason for the slot \p AI, which contains the reference type and
/// is not promoted, with \p At set to the place to report it.
static StringRef getSlotReason(const AllocaInst &AI, const Instruction *&At) {
  At = &AI;
  Type *Ty = AI.getAllocatedType();
  // An aggregate in memory that holds a reference.
  if (!cli::isManagedReferenceType(Ty))
    return CopiedAsMemory;
  // Its bytes copied or set (-ftrivial-auto-var-init=pattern's memcpy), or
  // read or written as another type.
  for (const Use &U : AI.uses()) {
    const auto *I = cast<Instruction>(U.getUser());
    if (isa<AnyMemIntrinsic>(I) || accessesAsOtherType(U, *I, Ty)) {
      At = I;
      return CopiedAsMemory;
    }
  }
  // Any use but an access of it and a marker: the slot's address itself.
  const Instruction *Load = nullptr, *Store = nullptr;
  for (const Use &U : AI.uses()) {
    const auto *I = cast<Instruction>(U.getUser());
    if (isa<LoadInst>(I)) {
      if (!Load)
        Load = I;
    } else if (const auto *SI = dyn_cast<StoreInst>(I);
               SI && U.getOperandNo() == SI->getPointerOperandIndex()) {
      if (!Store)
        Store = I;
    } else if (!isMarker(I)) {
      At = I;
      return AddressEscapes;
    }
  }
  // A slot that is not static or one element, or one accessed volatile or
  // atomic: its accesses stay accesses of memory.
  if (Load) {
    At = Load;
    return LoadThroughPointer;
  }
  if (Store) {
    At = Store;
    return StoreThroughPointer;
  }
  return AddressEscapes;
}

/// Replaces the body of \p F by a lone 'unreachable', keeping the function
/// defined with its linkage, comdat and attributes (as MSILLowerDynamicAlloca
/// drops one).
static void dropBody(Function &F) {
  // No instruction refers to another one once every operand is dropped, so
  // the blocks can then go in any order.
  for (BasicBlock &BB : F)
    BB.dropAllReferences();
  while (!F.empty())
    F.back().eraseFromParent();
  LLVMContext &Ctx = F.getContext();
  new UnreachableInst(Ctx, BasicBlock::Create(Ctx, "entry", &F));
}

/// Promotes the reference slots of the definition \p F, or reports what
/// cannot become a value and drops the body. Returns true if \p F changed.
static bool promoteReferences(Function &F) {
  // The slots that contain the type, and whether any load or store of it is
  // left; most functions have neither.
  SmallVector<AllocaInst *, 8> Slots;
  bool HasAccess = false;
  for (Instruction &I : instructions(F)) {
    if (auto *AI = dyn_cast<AllocaInst>(&I)) {
      if (MSIL::containsReferenceType(AI->getAllocatedType()))
        Slots.push_back(AI);
    } else if (!HasAccess && isReferenceAccess(I)) {
      HasAccess = true;
    }
  }
  if (Slots.empty() && !HasAccess)
    return false;

  FunctionReporter R(F);
  SmallPtrSet<const BasicBlock *, 8> Handlers = getHandlerBlocks(F);
  SmallVector<AllocaInst *, 8> Promotable;
  SmallPtrSet<const Value *, 8> PromotableSet;
  // The slots reported as a whole: their own accesses need no report.
  SmallPtrSet<const Value *, 8> ReportedSlots;
  for (AllocaInst *AI : Slots) {
    if (const Instruction *HandlerUse = findHandlerUse(*AI, Handlers)) {
      R.report(HandlerText, *HandlerUse);
      ReportedSlots.insert(AI);
    } else if (isPromotableSlot(*AI)) {
      Promotable.push_back(AI);
      PromotableSet.insert(AI);
    } else {
      const Instruction *At = nullptr;
      StringRef Reason = getSlotReason(*AI, At);
      R.report(getLocalText(Reason), *At);
      ReportedSlots.insert(AI);
    }
  }

  // The loads and stores through every other pointer.
  if (HasAccess) {
    for (Instruction &I : instructions(F)) {
      if (!isReferenceAccess(I))
        continue;
      const Value *Ptr = getLoadStorePointerOperand(&I);
      if (PromotableSet.contains(Ptr) ||
          ReportedSlots.contains(getUnderlyingObject(Ptr)))
        continue;
      R.report(getLocalText(isa<LoadInst>(I) ? LoadThroughPointer
                                             : StoreThroughPointer),
               I);
    }
  }

  if (R.HasError) {
    dropBody(F);
    return true;
  }
  if (Promotable.empty())
    return false;
  LLVM_DEBUG(dbgs() << "msil-promote-references: " << Promotable.size()
                    << " slot(s) in " << F.getName() << '\n');
  DominatorTree DT(F);
  PromoteMemToReg(Promotable, DT);
  return true;
}

bool MSILPromoteReferencesLegacy::runOnModule(Module &M) {
  bool Changed = false;
  for (Function &F : M) {
    // An available_externally body is never code-generated (see
    // MachineFunctionPass::runOnFunction).
    if (F.isDeclaration() || F.hasAvailableExternallyLinkage())
      continue;
    Changed |= promoteReferences(F);
  }
  return Changed;
}
