//===-- MSILISelLowering.cpp - MSIL DAG Lowering Implementation -----------===//
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
/// This file implements the MSILTargetLowering class.
///
//===----------------------------------------------------------------------===//

#include "MSILISelLowering.h"
#include "MCTargetDesc/MSILMCTargetDesc.h"
#include "MSILSubtarget.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RuntimeLibcallUtil.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;

#define DEBUG_TYPE "msil-lower"

MSILTargetLowering::MSILTargetLowering(const TargetMachine &TM,
                                       const MSILSubtarget &STI)
    : TargetLowering(TM, STI), Subtarget(&STI) {
  // Booleans always contain 0 or 1: ceq, cgt and clt push 0 or 1.
  setBooleanContents(ZeroOrOneBooleanContent);
  // Schedule in IR order at every optimization level, as -O0 always does. CIL
  // div and rem throw, but ISD::SDIV, UDIV, SREM and UREM have no chain, so
  // the register-pressure scheduler may place a division above a call or a
  // TRAP that precedes it in its block: a program whose callee never returns
  // (it traps, exits or loops) would then die in the division instead (plan
  // 5.4, 'TrapsOnlyOnUB'). The bottom-up source-order scheduler picks the
  // available node that comes last in the IR, so no division is placed above
  // an earlier call or trap. Chained MSILISD division nodes would need the
  // chain in effect at the division's IR position, which legalization no
  // longer knows (the block's root, DAG.getRoot(), would create cycles).
  setSchedulingPreference(Sched::Source);
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

  // Integer operations without a CIL opcode take LLVM's generic expansions,
  // never runtime helpers or BCL calls (decision D16). i1, i8 and i16 are
  // promoted to i32 by the type legalizer.
  for (MVT T : {MVT::i32, MVT::i64}) {
    setOperationAction(
        {ISD::CTLZ, ISD::CTTZ, ISD::CTPOP, ISD::BSWAP, ISD::ROTL, ISD::ROTR}, T,
        Expand);
    setOperationAction({ISD::MULHS, ISD::MULHU, ISD::SMUL_LOHI, ISD::UMUL_LOHI,
                        ISD::SDIVREM, ISD::UDIVREM},
                       T, Expand);
    // Expand by default too; min and max expand to a compare and a SELECT.
    setOperationAction({ISD::SMIN, ISD::SMAX, ISD::UMIN, ISD::UMAX, ISD::ABS},
                       T, Expand);
  }
  // Sign extension from i8, i16 and i32 is conv.i1, conv.i2 and conv.i4
  // (MSILInstrConv.td); from i1 it is expanded into shifts.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  // SETCC stays Legal for i32 and i64: every integer condition code is
  // selected, the negated ones as two compares (MSILInstrInteger.td). CIL has
  // no select instruction, but SELECT is Legal for every value type: it
  // selects the SELECT_<RC> pseudos, which emitSelect turns into a branch
  // triangle (decision D34), because generic legalization emits SELECT
  // (variable i128 shifts, min/max). The fused forms are split into SETCC
  // plus SELECT or BRCOND.
  for (MVT T : {MVT::i32, MVT::i64, MVT::f32, MVT::f64}) {
    setOperationAction(ISD::SELECT, T, Legal);
    setOperationAction({ISD::SELECT_CC, ISD::BR_CC}, T, Expand);
  }

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
  // i128 shifts by a variable amount: Custom, so that the type legalizer
  // builds *_PARTS nodes instead of calling __ashlti3, __lshrti3 or __ashrti3.
  setOperationAction({ISD::SHL_PARTS, ISD::SRA_PARTS, ISD::SRL_PARTS}, MVT::i64,
                     Custom);
  // i128 division and remainder: DAGCombiner expands a division by a power of
  // two (and an 'exact' division by any constant) inline before type
  // legalization, because isIntDivCheap is false above 64 bits. Every other
  // i128 division would call __divti3, __udivti3, __modti3 or __umodti3, and
  // such '__' helpers are errors until phase 3 (decision D14), so it reaches
  // ReplaceNodeResults. The helper's i128 result needs two values, so
  // CanLowerReturn would make LowerCallTo demote it to a stack slot (a
  // FrameIndex) even after LowerCall's diagnostic. ReplaceNodeResults reports
  // the libcall in the type legalizer instead, before any call is built.
  setOperationAction({ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM}, MVT::i128,
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

bool MSILTargetLowering::isIntDivCheap(EVT VT, AttributeList /*Attr*/) const {
  // CIL div and rem exist only up to 64 bits (int32 and native int), and the
  // JIT strength-reduces those by a constant itself, so keep the IL small. A
  // wider division has no CIL instruction: DAGCombiner's generic expansion of
  // a division by a power of two (shifts and adds) is better than a runtime
  // helper. The scalar size keeps vectors of i32 or i64 as they are.
  return VT.getScalarSizeInBits() <= 64;
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

void MSILTargetLowering::ReplaceNodeResults(SDNode *N,
                                            SmallVectorImpl<SDValue> &Results,
                                            SelectionDAG &DAG) const {
  // i128 division and remainder (the only Custom nodes with an illegal result
  // type): report the libcall that the type legalizer would build, with
  // LowerCall's text, and go on with an undefined result.
  EVT VT = N->getValueType(0);
  RTLIB::Libcall LC;
  switch (N->getOpcode()) {
  case ISD::SDIV:
    LC = RTLIB::getSDIV(VT);
    break;
  case ISD::UDIV:
    LC = RTLIB::getUDIV(VT);
    break;
  case ISD::SREM:
    LC = RTLIB::getSREM(VT);
    break;
  case ISD::UREM:
    LC = RTLIB::getUREM(VT);
    break;
  default:
    // Not custom lowered after all: the type legalizer expands it.
    return;
  }
  const char *Name = getLibcallName(LC);
  DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
      DAG.getMachineFunction().getFunction(),
      Twine("runtime library call '") + (Name ? Name : "?") +
          "' is not supported for MSIL yet",
      SDLoc(N).getDebugLoc()));
  Results.push_back(DAG.getUNDEF(VT));
}

MachineBasicBlock *
MSILTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  case MSIL::CALL_RESULTS:
    return emitCallResults(MI, BB);
  case MSIL::SELECT_I32:
  case MSIL::SELECT_I64:
  case MSIL::SELECT_F32:
  case MSIL::SELECT_F64:
    return emitSelect(MI, BB);
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
  // A shift of an i128 by a variable amount: funnel shifts of the two native
  // int halves and a SELECT on bit 6 of the amount, all inline.
  SDValue Lo, Hi;
  expandShiftParts(Op.getNode(), Lo, Hi, DAG);
  return DAG.getMergeValues({Lo, Hi}, SDLoc(Op));
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

//===----------------------------------------------------------------------===//
// Custom inserters
//===----------------------------------------------------------------------===//

MachineBasicBlock *MSILTargetLowering::emitSelect(MachineInstr &MI,
                                                  MachineBasicBlock *BB) const {
  // CIL has no select (decision D34). Split the block after the select into a
  // triangle and join the two values with a PHI:
  //
  //   BB:       ...
  //             BR_IF SinkMBB, %cond        ; taken edge carries %t
  //   FalseMBB:                             ; empty, falls through with %f
  //   SinkMBB:  %dst = PHI %t, BB, %f, FalseMBB
  //             ...                         ; the rest of BB
  //
  // PHI elimination later puts a copy of %t before the BR_IF and a copy of %f
  // into FalseMBB, the shape of C#'s '?:'. analyzeBranch cannot analyze any
  // branch until M6, so the successor lists are built here explicitly and
  // nothing relies on branch analysis.
  const TargetInstrInfo &TII = *Subtarget->getInstrInfo();
  const DebugLoc &DL = MI.getDebugLoc();
  Register Dst = MI.getOperand(0).getReg();
  Register Cond = MI.getOperand(1).getReg();
  Register TrueVal = MI.getOperand(2).getReg();
  Register FalseVal = MI.getOperand(3).getReg();

  MachineFunction *MF = BB->getParent();
  const BasicBlock *LLVMBB = BB->getBasicBlock();
  MachineFunction::iterator InsertPos = std::next(BB->getIterator());
  MachineBasicBlock *FalseMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *SinkMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MF->insert(InsertPos, FalseMBB);
  MF->insert(InsertPos, SinkMBB);

  // SinkMBB takes over the instructions after the select, the terminators
  // among them, and the successors of BB (with the PHIs that name BB).
  SinkMBB->splice(SinkMBB->begin(), BB, std::next(MI.getIterator()), BB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(BB);

  BB->addSuccessor(FalseMBB);
  BB->addSuccessor(SinkMBB);
  FalseMBB->addSuccessor(SinkMBB);

  BuildMI(BB, DL, TII.get(MSIL::BR_IF)).addMBB(SinkMBB).addReg(Cond);
  BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(TargetOpcode::PHI), Dst)
      .addReg(TrueVal)
      .addMBB(BB)
      .addReg(FalseVal)
      .addMBB(FalseMBB);

  MI.eraseFromParent();
  // The function has PHIs now, also when it was read from MIR without any.
  MF->getProperties().resetNoPHIs();
  return SinkMBB;
}
