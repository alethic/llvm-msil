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
  /// Rejects a jump table of more than 4096 entries at every optimization
  /// level, so that its MSILISD::BR_TABLE fits in one SDNode; otherwise
  /// defers to the base class.
  bool isSuitableForJumpTable(const SwitchInst *SI, uint64_t NumCases,
                              uint64_t Range, ProfileSummaryInfo *PSI,
                              BlockFrequencyInfo *BFI) const override;
  bool convertSelectOfConstantsToMath(EVT VT) const override;

  // Lowering hooks.
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;
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
  /// Makes a frame index a TargetFrameIndex operand of its users;
  /// MSILRegisterInfo::eliminateFrameIndex turns it into an FP-relative
  /// address.
  SDValue LowerFrameIndex(SDValue Op, SelectionDAG &DAG) const;
  /// Copies a frame index into a register through a COPY_I64, because a
  /// CopyToReg cannot take a frame index operand.
  SDValue LowerCopyToReg(SDValue Op, SelectionDAG &DAG) const;
  /// Diagnoses STACKSAVE and STACKRESTORE, except in a function with a
  /// dynamic alloca, whose DYNAMIC_STACKALLOC already reports the error: there
  /// they return their dummy results silently, so that one VLA gives one
  /// error.
  SDValue LowerStackSaveRestore(SDValue Op, SelectionDAG &DAG) const;
  /// Wraps the address of a global variable in MSILISD::DATA_ADDR (its
  /// address field plus the offset, decision D12) and the address of a
  /// function in MSILISD::FUNC_ADDR (ldftn).
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  /// Wraps the address of a library function in MSILISD::EXT_SYM (ldftn).
  SDValue LowerExternalSymbol(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_JT(SDValue Op, SelectionDAG &DAG) const;
  /// Expands SHL_PARTS, SRA_PARTS and SRL_PARTS (i128 shifts by a variable
  /// amount) inline, so that no runtime helper is needed.
  SDValue LowerShiftParts(SDValue Op, SelectionDAG &DAG) const;
  /// Converts an int64 to f32 with a rounding to odd before conv.r4, so that
  /// the result is correctly rounded also when the JIT folds the conversion
  /// through float64; returns any other int64 conversion unchanged.
  SDValue LowerSINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;
  /// Converts a uint64 to f32 or f64 without conv.r.un, a SELECT or memory
  /// (decision D17).
  SDValue LowerUINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;
  /// Replaces a constrained floating-point operation with its plain form and
  /// its input chain: CIL has no floating-point exceptions or rounding modes.
  SDValue LowerStrictFP(SDValue Op, SelectionDAG &DAG) const;
  /// Folds the extension of an f16 or bf16 value that is an exact rounding of
  /// a wider one, as DAGCombiner does; returns no value for any other.
  SDValue LowerFP_EXTEND(SDValue Op, SelectionDAG &DAG) const;
  /// Lowers FMINIMUM, FMAXIMUM, FMINIMUMNUM and FMAXIMUMNUM to compares and
  /// a SELECT, testing the sign of a zero without memory.
  SDValue LowerFMinMax(SDValue Op, SelectionDAG &DAG) const;
  /// Diagnoses a change of the rounding mode (CIL only rounds to nearest).
  SDValue LowerSET_ROUNDING(SDValue Op, SelectionDAG &DAG) const;
  /// Lowers FPTRUNC_ROUND to conv.r4 when it rounds to nearest, and diagnoses
  /// any other rounding mode.
  SDValue LowerFPTRUNC_ROUND(SDValue Op, SelectionDAG &DAG) const;
  /// Splits FSINCOS into FSIN and FCOS: sincos is not a Phase-1 import
  /// (decision D14), and sin and cos are.
  SDValue LowerFSINCOS(SDValue Op, SelectionDAG &DAG) const;
  /// Converts between bf16 and f32/f64 through the compiler-rt helpers, as f16
  /// does, instead of the generic expansions' bitcasts through memory.
  SDValue LowerBF16Conversion(SDValue Op, SelectionDAG &DAG) const;
  /// Reports \p What, the diagnostic text for an operation that Phase 1 does
  /// not support, and returns well-formed dummy results (zero for an integer
  /// value, the input chain for a chain), so that compilation continues.
  SDValue LowerUnsupported(SDValue Op, SelectionDAG &DAG, StringRef What) const;

  // Custom inserters.
  /// Expands a SELECT_<RC> pseudo into a branch triangle that joins the two
  /// values with a PHI (CIL has no select instruction).
  MachineBasicBlock *emitSelect(MachineInstr &MI, MachineBasicBlock *BB) const;
};

} // end namespace llvm

#endif
