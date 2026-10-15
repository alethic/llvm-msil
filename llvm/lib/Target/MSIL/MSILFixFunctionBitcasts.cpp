//===-- MSILFixFunctionBitcasts.cpp - Fix function bitcasts for MSIL ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// STUB: implemented in M9
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the pass that wraps the callees of calls whose
/// function type differs from the callee's own, because a CIL call needs
/// the exact signature of its callee. M9 replaces it with a copy of
/// WebAssemblyFixFunctionBitcasts.cpp, without the renaming of main.
///
/// Until then it only neutralizes the calls that MSILTargetLowering::LowerCall
/// would reject or drop and whose result needs more than one value (i128,
/// { i64, i64 }, { double, double }, <2 x i64>, <2 x float>).
/// TargetLowering::LowerCallTo returns such a result through a hidden pointer
/// to a stack slot that it creates and loads from around LowerCall, so after
/// LowerCall's diagnostic the slot's loads would read memory that no call
/// wrote. This pass reports each such call once, with LowerCall's diagnostic
/// and in the order of LowerCall's checks, and erases it: its uses become
/// poison, and an invoke branches to its normal destination.
/// Every other call is left to LowerCall, which stays the backstop for all of
/// them. A call of a function that this module does not define (such as the
/// __muldc3 and __divsc3 that clang calls for C complex multiplication and
/// division) is lowered with its demoted result like any other call; the
/// module trailer declares its callee as a P/Invoke or reports it
/// (MSILAsmPrinter::emitEndOfAsmFile, decision D14).
///
//===----------------------------------------------------------------------===//

#include "MSIL.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetMachine.h"
#include <string>
using namespace llvm;

#define DEBUG_TYPE "msil-fix-function-bitcasts"

namespace {
class MSILFixFunctionBitcastsLegacy final : public ModulePass {
  StringRef getPassName() const override {
    return "MSIL Fix Function Bitcasts";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetPassConfig>();
    ModulePass::getAnalysisUsage(AU);
  }

  bool runOnModule(Module &M) override;

public:
  static char ID; // Pass identification, replacement for typeid
  MSILFixFunctionBitcastsLegacy() : ModulePass(ID) {}
};
} // end anonymous namespace

char MSILFixFunctionBitcastsLegacy::ID = 0;
INITIALIZE_PASS_BEGIN(MSILFixFunctionBitcastsLegacy, DEBUG_TYPE,
                      "Fix mismatching bitcasts for MSIL", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(MSILFixFunctionBitcastsLegacy, DEBUG_TYPE,
                    "Fix mismatching bitcasts for MSIL", false, false)

ModulePass *llvm::createMSILFixFunctionBitcastsLegacyPass() {
  return new MSILFixFunctionBitcastsLegacy();
}

/// Returns true if SelectionDAG returns the result of \p CB through a hidden
/// pointer: TargetLowering::LowerCallTo does when CanLowerReturn rejects the
/// parts that GetReturnInfo computes for the result.
/// MSILTargetLowering::CanLowerReturn (final in M2, decision D33) accepts at
/// most one part. It takes the caller's MachineFunction, which does not exist
/// yet, so its test is repeated here, as MSIL::computeSignatureVTs does.
static bool isResultDemoted(const CallBase &CB, const TargetLowering &TLI,
                            const DataLayout &DL) {
  SmallVector<ISD::OutputArg, 4> Outs;
  GetReturnInfo(CB.getCallingConv(), CB.getType(), CB.getAttributes(), Outs,
                TLI, DL);
  return Outs.size() > 1;
}

/// The calling conventions that MSILTargetLowering::LowerCall accepts.
static bool callingConvSupported(CallingConv::ID CallConv) {
  return CallConv == CallingConv::C || CallConv == CallingConv::Fast ||
         CallConv == CallingConv::Cold ||
         CallConv == CallingConv::PreserveMost ||
         CallConv == CallingConv::PreserveAll;
}

/// Applies the checks of MSILTargetLowering::LowerCall to \p CB, in the same
/// order. Returns false if LowerCall lowers the call. Otherwise returns true
/// and sets \p Msg to LowerCall's diagnostic, which is empty for a call that
/// LowerCall drops without one.
static bool isRejectedByLowerCall(const CallBase &CB, std::string &Msg) {
  Msg.clear();
  // Calling an undefined value is undefined behavior. MSILPrepareIR leaves
  // such calls behind when it replaces a diagnosed alias by poison.
  if (isa<UndefValue>(CB.getCalledOperand()))
    return true;

  if (!callingConvSupported(CB.getCallingConv())) {
    Msg = "calling convention is not supported for MSIL";
    return true;
  }
  if (CB.isMustTailCall()) {
    Msg = "musttail calls are not supported for MSIL yet";
    return true;
  }

  // The intrinsics that LLVM recognizes never get here (see runOnModule).
  const auto *F = dyn_cast<Function>(CB.getCalledOperand());
  if (F && F->isIntrinsic()) {
    Msg = ("unknown intrinsic '" + F->getName() + "' is not supported for MSIL")
              .str();
    return true;
  }
  if (F && CB.getFunctionType() != F->getFunctionType()) {
    Msg = "mismatched call signature";
    return true;
  }

  if (CB.getFunctionType()->isVarArg()) {
    Msg = "calls to variadic functions are not supported for MSIL yet";
    return true;
  }

  // These attributes apply only to pointers, each of which is one outgoing
  // argument of LowerCall, so the arguments are tested in its order.
  for (unsigned I = 0, E = CB.arg_size(); I != E; ++I) {
    if (CB.paramHasAttr(I, Attribute::InAlloca))
      Msg = "inalloca arguments are not supported for MSIL";
    else if (CB.paramHasAttr(I, Attribute::Preallocated))
      Msg = "preallocated arguments are not supported for MSIL";
    else if (CB.paramHasAttr(I, Attribute::ByVal))
      Msg = "byval arguments are not supported for MSIL yet";
    else if (CB.paramHasAttr(I, Attribute::Nest))
      Msg = "nest arguments are not supported for MSIL";
    else
      continue;
    return true;
  }
  return false;
}

/// Erases \p CB, whose uses become poison. An invoke is replaced by a branch
/// to its normal destination, and its unwind destination loses the edge.
static void eraseCall(CallBase &CB) {
  if (auto *II = dyn_cast<InvokeInst>(&CB)) {
    auto *BI = UncondBrInst::Create(II->getNormalDest(), II->getIterator());
    BI->setDebugLoc(II->getDebugLoc());
    II->getUnwindDest()->removePredecessor(II->getParent());
  }
  if (!CB.use_empty())
    CB.replaceAllUsesWith(PoisonValue::get(CB.getType()));
  CB.eraseFromParent();
}

bool MSILFixFunctionBitcastsLegacy::runOnModule(Module &M) {
  const auto &TM = getAnalysis<TargetPassConfig>().getTM<TargetMachine>();
  const DataLayout &DL = M.getDataLayout();
  bool Changed = false;
  for (Function &F : M) {
    // An available_externally body is never code-generated (see
    // MachineFunctionPass::runOnFunction), so LowerCall never sees its calls.
    if (F.isDeclaration() || F.hasAvailableExternallyLinkage())
      continue;
    const TargetLowering &TLI = *TM.getSubtargetImpl(F)->getTargetLowering();

    // Collect the calls first: erasing an invoke may delete PHI nodes in its
    // unwind destination.
    SmallVector<CallBase *, 4> Calls;
    for (Instruction &I : instructions(F)) {
      // SelectionDAGBuilder lowers inline assembly and the intrinsics that
      // LLVM recognizes itself; neither reaches LowerCall.
      auto *CB = dyn_cast<CallBase>(&I);
      if (CB && isa<CallInst, InvokeInst>(CB) && !CB->isInlineAsm() &&
          CB->getIntrinsicID() == Intrinsic::not_intrinsic &&
          isResultDemoted(*CB, TLI, DL))
        Calls.push_back(CB);
    }

    for (CallBase *CB : Calls) {
      std::string Msg;
      if (!isRejectedByLowerCall(*CB, Msg))
        continue;
      if (!Msg.empty())
        F.getContext().diagnose(
            DiagnosticInfoUnsupported(F, Msg, CB->getDebugLoc()));
      eraseCall(*CB);
      Changed = true;
    }
  }
  return Changed;
}
