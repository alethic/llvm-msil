//===-- MSILPreExpand.cpp - IR rewrites before ExpandIRInsts --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the module rewrites that MSILPrepareIR applies before
/// any pass runs, for IR that ExpandIRInsts must see in a form it expands.
///
//===----------------------------------------------------------------------===//

#include "MSILPreExpand.h"
#include "MSILUtilities.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"

using namespace llvm;

bool MSIL::relaxStrictInt128Conversions(Module &M) {
  bool Changed = false;
  for (Function &F : M) {
    Instruction::CastOps Op;
    switch (F.getIntrinsicID()) {
    case Intrinsic::experimental_constrained_sitofp:
      Op = Instruction::SIToFP;
      break;
    case Intrinsic::experimental_constrained_uitofp:
      Op = Instruction::UIToFP;
      break;
    case Intrinsic::experimental_constrained_fptosi:
      Op = Instruction::FPToSI;
      break;
    case Intrinsic::experimental_constrained_fptoui:
      Op = Instruction::FPToUI;
      break;
    default:
      continue;
    }
    bool FromInt = Op == Instruction::SIToFP || Op == Instruction::UIToFP;
    for (User *U : make_early_inc_range(F.users())) {
      auto *Call = dyn_cast<CallInst>(U);
      if (!Call)
        continue;
      Value *Src = Call->getArgOperand(0);
      Type *IntTy = FromInt ? Src->getType() : Call->getType();
      if (IntTy->getScalarSizeInBits() <= MaxSelectedFPConvertBits)
        continue;
      // The rounding-mode and exception-behavior operands go: one rounding
      // mode, no floating-point exceptions, as LowerStrictFP assumes for the
      // conversions it selects.
      CastInst *Plain =
          CastInst::Create(Op, Src, Call->getType(), "", Call->getIterator());
      Plain->takeName(Call);
      Plain->setDebugLoc(Call->getDebugLoc());
      Call->replaceAllUsesWith(Plain);
      Call->eraseFromParent();
      Changed = true;
    }
  }
  return Changed;
}
