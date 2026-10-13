//===-- MSILISelLowering.cpp - MSIL DAG Lowering Implementation -----------===//
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
/// This file implements the MSILTargetLowering class.
///
//===----------------------------------------------------------------------===//

#include "MSILISelLowering.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSILSubtarget.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;

#define DEBUG_TYPE "msil-lower"

MSILTargetLowering::MSILTargetLowering(const TargetMachine &TM,
                                       const MSILSubtarget &STI)
    : TargetLowering(TM, STI), Subtarget(&STI) {
  // Booleans always contain 0 or 1: ceq, cgt and clt push 0 or 1.
  setBooleanContents(ZeroOrOneBooleanContent);
  // We don't know the microarchitecture here, so just reduce register pressure.
  setSchedulingPreference(Sched::RegPressure);
  // There is no stack pointer: arguments travel on the evaluation stack.

  // Set up the register classes. I64 holds pointers and 64-bit integers, both
  // CIL native int.
  addRegisterClass(MVT::i32, &MSIL::I32RegClass);
  addRegisterClass(MVT::i64, &MSIL::I64RegClass);
  addRegisterClass(MVT::f32, &MSIL::F32RegClass);
  addRegisterClass(MVT::f64, &MSIL::F64RegClass);
  // Compute derived properties from the register classes.
  computeRegisterProperties(Subtarget->getRegisterInfo());

  // ldc.r4 and ldc.r8 take any value bit-exactly: no constant pool.
  setOperationAction(ISD::ConstantFP, {MVT::f32, MVT::f64}, Legal);

  // TRAP is 'ldnull; throw', a terminator ('unreachable' always becomes a
  // trap). Expanded, it would be a call to abort, after which the method could
  // fall off its end, which CIL forbids.
  setOperationAction(ISD::TRAP, MVT::Other, Legal);

  // Every operation that LowerOperation dispatches is Custom, because
  // SelectionDAG calls LowerOperation only for Custom actions. Until the
  // milestone that implements an operation replaces its stub body (and keeps
  // the action), the stub fails naming that milestone (D33) instead of leaving
  // the node to 'Cannot select'.
  // Frame indices (M7), global addresses and external symbols (M8). Pointers
  // are I64.
  setOperationAction({ISD::GlobalAddress, ISD::ExternalSymbol, ISD::FrameIndex},
                     MVT::i64, Custom);
  // Jump tables become a CIL 'switch' (M6). Custom, like Legal, keeps jump
  // tables allowed (areJTsAllowed).
  setOperationAction(ISD::BR_JT, MVT::Other, Custom);
  // i128 shifts by a variable amount (M4).
  setOperationAction({ISD::SHL_PARTS, ISD::SRA_PARTS, ISD::SRL_PARTS}, MVT::i64,
                     Custom);
  // uint64 -> FP uses the sequence of D17 (M5). UINT_TO_FP is keyed on the
  // operand type: an i32 operand stays Legal ('conv.r.un' is exact for it).
  setOperationAction(ISD::UINT_TO_FP, MVT::i64, Custom);

  // No atomics yet: AtomicExpand turns every atomic operation into an
  // __atomic_* libcall, which is diagnosed.
  setMaxAtomicSizeInBitsSupported(0);
}

//===----------------------------------------------------------------------===//
// Query hooks
//===----------------------------------------------------------------------===//

MVT MSILTargetLowering::getScalarShiftAmountTy(const DataLayout & /*DL*/,
                                               EVT /*VT*/) const {
  // CIL shifts take an int32 amount, whatever the width of the value.
  return MVT::i32;
}

EVT MSILTargetLowering::getSetCCResultType(const DataLayout & /*DL*/,
                                           LLVMContext &C, EVT VT) const {
  // There are no vector registers; vector compares are split into scalar ones.
  if (VT.isVector())
    return VT.changeVectorElementTypeToInteger();

  // ceq, cgt and clt push an int32, and every branch tests an int32.
  return EVT::getIntegerVT(C, 32);
}

bool MSILTargetLowering::isOffsetFoldingLegal(
    const GlobalAddressSDNode *GA) const {
  // The address of a global variable is its address field plus an offset
  // ('ldsfld' then 'ldc.i4; add'); a function address ('ldftn') takes no
  // offset.
  return !isa<Function>(GA->getGlobal());
}

bool MSILTargetLowering::isLegalAddressingMode(const DataLayout & /*DL*/,
                                               const AddrMode &AM,
                                               Type * /*Ty*/, unsigned /*AS*/,
                                               Instruction * /*I*/) const {
  // ldind and stind take a complete address: no global, offset or scale (a
  // register scaled by 1 without a base is the same as a base register).
  return !AM.BaseGV && AM.BaseOffs == 0 && AM.ScalableOffset == 0 &&
         (AM.Scale == 0 || (AM.Scale == 1 && !AM.HasBaseReg));
}

bool MSILTargetLowering::allowsMisalignedMemoryAccesses(
    EVT /*VT*/, unsigned /*AddrSpace*/, Align /*Align*/,
    MachineMemOperand::Flags /*Flags*/, unsigned *Fast) const {
  // A misaligned access carries the 'unaligned.' prefix, which the JIT
  // ignores on x86-64 and AArch64, so it is as fast as an aligned one.
  if (Fast)
    *Fast = 1;
  return true;
}

bool MSILTargetLowering::isIntDivCheap(EVT /*VT*/,
                                       AttributeList /*Attr*/) const {
  // The JIT turns division by a constant into multiplication itself, so keep
  // the IL small.
  return true;
}

MVT MSILTargetLowering::getJumpTableRegTy(const DataLayout & /*DL*/) const {
  // CIL 'switch' takes an int32 index. SelectionDAGBuilder range-checks the
  // full-width value before it truncates it to this type.
  return MVT::i32;
}

bool MSILTargetLowering::convertSelectOfConstantsToMath(EVT /*VT*/) const {
  // CIL has no select, so arithmetic on the 0/1 compare result is better than
  // a select of constants, which needs a branch.
  return true;
}

//===----------------------------------------------------------------------===//
// Lowering hooks
//===----------------------------------------------------------------------===//

SDValue MSILTargetLowering::LowerOperation(SDValue Op,
                                           SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::FrameIndex:
    return LowerFrameIndex(Op, DAG);
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::ExternalSymbol:
    return LowerExternalSymbol(Op, DAG);
  case ISD::BR_JT:
    return LowerBR_JT(Op, DAG);
  case ISD::SHL_PARTS:
  case ISD::SRA_PARTS:
  case ISD::SRL_PARTS:
    return LowerShiftParts(Op, DAG);
  case ISD::UINT_TO_FP:
    return LowerUINT_TO_FP(Op, DAG);
  default:
    reportFatalInternalError(Twine("MSIL: unexpected operation in "
                                   "LowerOperation: ") +
                             Op->getOperationName(&DAG));
  }
}

MachineBasicBlock *
MSILTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  case MSIL::CALL_RESULTS:
    return emitCallResults(MI, BB);
  default:
    llvm_unreachable("Unexpected instr type to insert");
  }
}

SDValue MSILTargetLowering::LowerFrameIndex(SDValue Op,
                                            SelectionDAG &DAG) const {
  reportFatalUsageError("MSIL: frame indices are implemented in M7");
}

SDValue MSILTargetLowering::LowerGlobalAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  reportFatalUsageError("MSIL: global addresses are implemented in M8");
}

SDValue MSILTargetLowering::LowerExternalSymbol(SDValue Op,
                                                SelectionDAG &DAG) const {
  reportFatalUsageError("MSIL: external symbols are implemented in M8");
}

SDValue MSILTargetLowering::LowerBR_JT(SDValue Op, SelectionDAG &DAG) const {
  reportFatalUsageError("MSIL: jump tables are implemented in M6");
}

SDValue MSILTargetLowering::LowerShiftParts(SDValue Op,
                                            SelectionDAG &DAG) const {
  reportFatalUsageError(
      "MSIL: shifts of 128-bit integers are implemented in M4");
}

SDValue MSILTargetLowering::LowerUINT_TO_FP(SDValue Op,
                                            SelectionDAG &DAG) const {
  reportFatalUsageError(
      "MSIL: unsigned integer to floating-point conversion is implemented in "
      "M5");
}

SDValue MSILTargetLowering::LowerUnsupported(SDValue Op, SelectionDAG &DAG,
                                             StringRef What) const {
  reportFatalUsageError(Twine("MSIL: unsupported operation '") + What +
                        "' (its diagnostic is implemented in M7)");
}

MachineBasicBlock *MSILTargetLowering::emitSelect(MachineInstr &MI,
                                                  MachineBasicBlock *BB) const {
  reportFatalUsageError("MSIL: select is implemented in M4");
}
