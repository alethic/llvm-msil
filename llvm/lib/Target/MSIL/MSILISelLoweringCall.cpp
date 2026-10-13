//===-- MSILISelLoweringCall.cpp - MSIL call lowering ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyISelLowering.cpp at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the parts of MSILTargetLowering that lower calls,
/// formal arguments and returns.
///
/// Every formal argument becomes an MSILISD::ARGUMENT node (an ARGUMENT_<RC>
/// pseudo that MSILExplicitLocals turns into ldarg), and a return becomes an
/// MSILISD::RETURN node with at most one value (ret). The parameter and result
/// types are recorded in MSILFunctionInfo, from which the AsmPrinter prints
/// the method header.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSILISelLowering.h"
#include "MSILMachineFunctionInfo.h"
#include "MSILSubtarget.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
using namespace llvm;

#define DEBUG_TYPE "msil-lower"

/// Reports \p Msg as an error in the current function. Compilation goes on, so
/// the caller must still build a well-formed DAG.
static void fail(const SDLoc &DL, SelectionDAG &DAG, const char *Msg) {
  MachineFunction &MF = DAG.getMachineFunction();
  DAG.getContext()->diagnose(
      DiagnosticInfoUnsupported(MF.getFunction(), Msg, DL.getDebugLoc()));
}

// Test whether the given calling convention is supported. Every call is a
// managed call with the arguments on the evaluation stack and there are no
// call-clobbered registers, so the language-independent conventions are all
// handled the same.
static bool callingConvSupported(CallingConv::ID CallConv) {
  return CallConv == CallingConv::C || CallConv == CallingConv::Fast ||
         CallConv == CallingConv::Cold ||
         CallConv == CallingConv::PreserveMost ||
         CallConv == CallingConv::PreserveAll;
}

SDValue MSILTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  // LowerReturn does not repeat this check: every definition passes through
  // here, so the diagnostic appears once per function.
  if (!callingConvSupported(CallConv))
    fail(DL, DAG, "calling convention is not supported for MSIL");
  // MSILPrepareIR has already diagnosed every variadic definition and deleted
  // its body; this is a backstop. The signature gets no extra parameter for
  // the variable arguments (computeSignatureVTs adds none either).
  if (IsVarArg)
    fail(DL, DAG, "variadic functions are not supported for MSIL yet");

  MachineFunction &MF = DAG.getMachineFunction();
  auto *MFI = MF.getInfo<MSILFunctionInfo>();

  // Set up the incoming ARGUMENTS value, which serves to represent the liveness
  // of the incoming values before they're represented by virtual registers.
  MF.getRegInfo().addLiveIn(MSIL::ARGUMENTS);

  for (const ISD::InputArg &In : Ins) {
    // Each of these arguments is a pointer. After the diagnostic it is lowered
    // as the plain pointer parameter it is, so the output stays well-formed.
    // SelectionDAG also sets the byval flag for inalloca and preallocated
    // arguments, so test those first.
    if (In.Flags.isInAlloca())
      fail(DL, DAG, "inalloca arguments are not supported for MSIL");
    else if (In.Flags.isPreallocated())
      fail(DL, DAG, "preallocated arguments are not supported for MSIL");
    else if (In.Flags.isByVal())
      fail(DL, DAG, "byval arguments are not supported for MSIL yet");
    if (In.Flags.isNest())
      fail(DL, DAG, "nest arguments are not supported for MSIL");
    // An sret argument is an ordinary pointer parameter. Narrow integers
    // arrive as int32 (In.VT is the register type); SelectionDAGBuilder adds
    // AssertSext/AssertZext for the signext/zeroext attributes.
    InVals.push_back(In.Used ? DAG.getNode(MSILISD::ARGUMENT, DL, In.VT,
                                           DAG.getTargetConstant(InVals.size(),
                                                                 DL, MVT::i32))
                             : DAG.getUNDEF(In.VT));

    // Record the number and types of arguments.
    MFI->addParam(In.VT);
  }

  // Record the number and types of results. The method header, direct calls
  // and ldftn compute their signatures from the IR type with
  // computeSignatureVTs, so the parameters recorded above must agree with it.
  SmallVector<MVT, 4> Params;
  SmallVector<MVT, 4> Results;
  MSIL::computeSignatureVTs(MF.getFunction().getFunctionType(),
                            &MF.getFunction(), MF.getFunction(),
                            DAG.getTarget(), Params, Results);
  for (MVT VT : Results)
    MFI->addResult(VT);
  assert(MFI->getParams().size() == Params.size() &&
         std::equal(MFI->getParams().begin(), MFI->getParams().end(),
                    Params.begin()) &&
         "formal arguments disagree with computeSignatureVTs");

  return Chain;
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
MSILTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID /*CallConv*/,
                                bool /*IsVarArg*/,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                const SDLoc &DL, SelectionDAG &DAG) const {
  assert(Outs.size() <= 1 && "MSIL can only return up to one value");

  // An i1, i8 or i16 result arrives as an int32 that SelectionDAGBuilder has
  // already extended as the signext/zeroext return attribute says (any
  // extension without one), and the method header says int32 too.
  SmallVector<SDValue, 4> RetOps(1, Chain);
  RetOps.append(OutVals.begin(), OutVals.end());
  Chain = DAG.getNode(MSILISD::RETURN, DL, MVT::Other, RetOps);

  // The result types were recorded by LowerFormalArguments.
#ifndef NDEBUG
  for (const ISD::OutputArg &Out : Outs) {
    assert(!Out.Flags.isByVal() && "byval is not valid for return values");
    assert(!Out.Flags.isNest() && "nest is not valid for return values");
    assert(!Out.Flags.isVarArg() && "non-fixed return value is not valid");
    assert(!Out.Flags.isInAlloca() && !Out.Flags.isInConsecutiveRegs() &&
           !Out.Flags.isInConsecutiveRegsLast() &&
           "MSIL never requests in-memory or consecutive-register results");
  }
#endif

  return Chain;
}

MachineBasicBlock *
MSILTargetLowering::emitCallResults(MachineInstr &MI,
                                    MachineBasicBlock *BB) const {
  reportFatalUsageError("MSIL: calls are implemented in M4");
}
