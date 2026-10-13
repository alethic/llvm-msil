//===-- MSILISelLowering.h - MSIL DAG Lowering Interface --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyISelLowering.h at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the interfaces that MSIL uses to lower LLVM code into a
/// selection DAG.
///
/// The query hooks are final: the IR optimizer reaches them through TTI long
/// before the code they describe can be selected. The lowering hooks are
/// filled in milestone by milestone; until then they fail with a message that
/// names the milestone that implements them.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSIL_MSILISELLOWERING_H
#define LLVM_LIB_TARGET_MSIL_MSILISELLOWERING_H

#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class MSILSubtarget;

class MSILTargetLowering final : public TargetLowering {
public:
  MSILTargetLowering(const TargetMachine &TM, const MSILSubtarget &STI);

  // Query hooks.
  MVT getScalarShiftAmountTy(const DataLayout &DL, EVT) const override;
  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;
  bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;
  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM, Type *Ty,
                             unsigned AS,
                             Instruction *I = nullptr) const override;
  bool allowsMisalignedMemoryAccesses(EVT, unsigned AddrSpace, Align Alignment,
                                      MachineMemOperand::Flags Flags,
                                      unsigned *Fast) const override;
  bool isIntDivCheap(EVT VT, AttributeList Attr) const override;
  MVT getJumpTableRegTy(const DataLayout &DL) const override;
  bool convertSelectOfConstantsToMath(EVT VT) const override;

  // Lowering hooks.
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *BB) const override;

  // Calls, arguments and returns (MSILISelLoweringCall.cpp).
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;
  SDValue LowerCall(CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;
  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context, const Type *RetTy) const override;
  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;

  /// Merges a CALL_PARAMS / CALL_RESULTS pair into a CALL or CALL_INDIRECT
  /// (the custom inserter of CALL_RESULTS).
  MachineBasicBlock *emitCallResults(MachineInstr &MI,
                                     MachineBasicBlock *BB) const;

private:
  /// Keep a pointer to the MSILSubtarget around so that we can make the right
  /// decision when generating code for different targets.
  const MSILSubtarget *Subtarget;

  // Custom lowering hooks.
  SDValue LowerFrameIndex(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerExternalSymbol(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_JT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerShiftParts(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerUINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;
  /// Diagnoses an operation that Phase 1 does not support and returns a
  /// well-formed dummy value, so that compilation continues.
  SDValue LowerUnsupported(SDValue Op, SelectionDAG &DAG, StringRef What) const;

  // Custom inserters.
  MachineBasicBlock *emitSelect(MachineInstr &MI, MachineBasicBlock *BB) const;
};

} // end namespace llvm

#endif
