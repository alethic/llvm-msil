//===-- MSILISelLoweringCall.cpp - MSIL call lowering ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyISelLowering.cpp at
// 8da03434abcc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the parts of MSILTargetLowering that lower calls,
/// formal arguments and returns.
///
//===----------------------------------------------------------------------===//

#include "MSILISelLowering.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;

#define DEBUG_TYPE "msil-lower"

SDValue MSILTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  reportFatalUsageError("MSIL: function definitions are implemented in M3");
}

SDValue MSILTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  reportFatalUsageError("MSIL: calls are implemented in M4");
}

bool MSILTargetLowering::CanLowerReturn(
    CallingConv::ID /*CallConv*/, MachineFunction & /*MF*/, bool /*IsVarArg*/,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext & /*Context*/,
    const Type * /*RetTy*/) const {
  // A CIL method returns at most one value. Anything larger is demoted to a
  // hidden sret pointer parameter (computeSignatureVTs does the same).
  return Outs.size() <= 1;
}

SDValue
MSILTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                bool IsVarArg,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                const SDLoc &DL, SelectionDAG &DAG) const {
  reportFatalUsageError("MSIL: returns are implemented in M3");
}

MachineBasicBlock *
MSILTargetLowering::emitCallResults(MachineInstr &MI,
                                    MachineBasicBlock *BB) const {
  reportFatalUsageError("MSIL: calls are implemented in M4");
}
