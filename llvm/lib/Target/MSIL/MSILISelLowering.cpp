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
#include "llvm/ADT/FloatingPointMode.h"
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

  // Floating point (decision D16). fadd, fsub, fmul, fdiv, frem (CIL 'rem' is
  // fmod), fneg and the conversions are Legal and select one CIL opcode each
  // (MSILInstrFloat.td, MSILInstrConv.td).
  for (MVT T : {MVT::f32, MVT::f64}) {
    // ceq, cgt, cgt.un, clt and clt.un, and their negations, give every
    // predicate except these four; the legalizer builds them from oeq, une,
    // ogt and olt (MSILInstrFloat.td).
    setCondCodeAction({ISD::SETO, ISD::SETUO, ISD::SETONE, ISD::SETUEQ}, T,
                      Expand);
    // No CIL opcode, and no BCL members in Phase 1: these become libm calls,
    // which LowerCall diagnoses and does not build until M8 makes them
    // math-library P/Invokes (decision D14). fmin and fmax become selects
    // when no operand can be a NaN.
    setOperationAction({ISD::FMA, ISD::FSQRT, ISD::FPOW, ISD::FEXP, ISD::FEXP2,
                        ISD::FEXP10, ISD::FLOG, ISD::FLOG2, ISD::FLOG10,
                        ISD::FCEIL, ISD::FFLOOR, ISD::FTRUNC, ISD::FRINT,
                        ISD::FNEARBYINT, ISD::FROUND, ISD::FROUNDEVEN,
                        ISD::FMINNUM, ISD::FMAXNUM},
                       T, Expand);
    // fminimum and fmaximum have no libcall, and the generic expansions of
    // these and of fminimum_num and fmaximum_num test the sign of a zero on
    // the integer bits, through a BITCAST and therefore a stack slot:
    // LowerFMinMax builds compares and a SELECT instead.
    setOperationAction(
        {ISD::FMINIMUM, ISD::FMAXIMUM, ISD::FMINIMUMNUM, ISD::FMAXIMUMNUM}, T,
        Custom);
    // sin and cos are libm calls too, but LibCall rather than Expand: the
    // expansion merges a sin and a cos of the same value into a call of
    // sincos, which returns its results through memory and is not a Phase-1
    // import (decision D14). For the same reason LowerFSINCOS splits an
    // explicit FSINCOS into the two calls.
    setOperationAction({ISD::FSIN, ISD::FCOS}, T, LibCall);
    setOperationAction(ISD::FSINCOS, T, Custom);
    // frexp and modf return their second result through a pointer to a stack
    // slot, which needs frames (M7): until M8 makes them math-library
    // P/Invokes, LowerFFREXP_FMODF reports the libcall with LowerCall's text
    // instead of building it.
    setOperationAction({ISD::FFREXP, ISD::FMODF}, T, Custom);
    // llvm.fptrunc.round: only round.tonearest is supported.
    setOperationAction(ISD::FPTRUNC_ROUND, T, Custom);
    // fabs and copysign: the generic expansions work on the integer bits,
    // through BITCAST and therefore a stack slot (M7).
    setOperationAction({ISD::FABS, ISD::FCOPYSIGN}, T, Expand);
    // f16 and bf16 are not legal types: their values are held as integers,
    // and arithmetic converts them to and from f32. Those conversions are the
    // compiler-rt helpers (__extendhfsf2, __truncsfhf2, __truncdfhf2 and the
    // bf16 ones), which LowerCall diagnoses: '__' helpers are errors until
    // phase 3 (decision D14). LowerBF16Conversion calls the bf16 helpers
    // instead of the generic expansions' bitcasts through memory.
    setOperationAction({ISD::FP16_TO_FP, ISD::FP_TO_FP16}, T, Expand);
    setOperationAction({ISD::BF16_TO_FP, ISD::FP_TO_BF16}, T, Custom);
  }
  // There is no reinterpreting opcode: a bitcast between an integer and a
  // floating-point value goes through a stack slot, which needs frames (M7).
  setOperationAction(ISD::BITCAST, {MVT::i32, MVT::i64, MVT::f32, MVT::f64},
                     Expand);
  // conv.i4, conv.u4, conv.i and conv.u do not saturate on .NET 8 and .NET
  // Framework: the saturating conversions expand to compares and selects.
  setOperationAction({ISD::FP_TO_SINT_SAT, ISD::FP_TO_UINT_SAT},
                     {MVT::i32, MVT::i64}, Expand);
  // uint64 -> FP uses the sequence of decision D17 for f32 and f64 results.
  // UINT_TO_FP is keyed on the operand type: an i32 operand stays Legal
  // ('conv.r.un' is exact for it).
  setOperationAction(ISD::UINT_TO_FP, MVT::i64, Custom);
  // int64 -> f32 rounds to odd before conv.r4, so that a JIT that folds the
  // conversion through float64 still rounds once (LowerSINT_TO_FP). SINT_TO_FP
  // is keyed on the operand type too: int64 -> f64 and int32 sources stay a
  // single conv.r8 or conv.r4.
  setOperationAction(ISD::SINT_TO_FP, MVT::i64, Custom);
  // FP -> i128 would call __fixsfti, __fixdfti, __fixunssfti or
  // __fixunsdfti, whose two-value result CanLowerReturn demotes to a stack
  // slot even after LowerCall's diagnostic; ReplaceNodeResults reports the
  // libcall instead, as it does for i128 division.
  setOperationAction({ISD::FP_TO_SINT, ISD::FP_TO_UINT, ISD::STRICT_FP_TO_SINT,
                      ISD::STRICT_FP_TO_UINT},
                     MVT::i128, Custom);
  // Constrained floating point (-ffp-model=strict, FENV_ACCESS): CIL has no
  // floating-point exceptions and only round-to-nearest, so a strict
  // operation is its plain form. On f32 and f64, the legalizer and
  // instruction selection already turn the strict arithmetic into plain
  // nodes, but not the strict compares (the legalizer asserts on them) and
  // conversions (the generic expansions go through a stack slot):
  // LowerStrictFP replaces those.
  setOperationAction({ISD::STRICT_FSETCC, ISD::STRICT_FSETCCS},
                     {MVT::f32, MVT::f64}, Custom);
  setOperationAction({ISD::STRICT_SINT_TO_FP, ISD::STRICT_UINT_TO_FP,
                      ISD::STRICT_FP_TO_SINT, ISD::STRICT_FP_TO_UINT},
                     {MVT::i32, MVT::i64}, Custom);
  // The strict fminimum and fmaximum would reach instruction selection
  // unchanged: LowerStrictFP hands the plain nodes to LowerFMinMax.
  setOperationAction({ISD::STRICT_FMINIMUM, ISD::STRICT_FMAXIMUM},
                     {MVT::f32, MVT::f64}, Custom);
  // On f16 and bf16 nothing becomes plain by itself: the type legalizer's
  // soft promotion has no case for the strict arithmetic and compares, and it
  // turns the strict conversions into STRICT_FP16_TO_FP, STRICT_FP_TO_FP16,
  // STRICT_BF16_TO_FP and STRICT_FP_TO_BF16, which nothing selects. It asks
  // the target first, keyed on the f16 or bf16 result or operand: every
  // constrained operation on these types becomes its plain form there
  // (ReplaceNodeResults, LowerStrictFP), which then legalizes like the plain
  // operation, into the compiler-rt conversions that LowerCall diagnoses.
  for (MVT T : {MVT::f16, MVT::bf16}) {
#define DAG_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
  setOperationAction(ISD::STRICT_##DAGN, T, Custom);
#include "llvm/IR/ConstrainedOps.def"
  }
  // The strict conversions themselves become plain too, should one reach the
  // legalizer, on the types that their plain forms are marked on above. The
  // integer promotion of the i16 operand of STRICT_BF16_TO_FP, which has no
  // case of its own, asks the target first as well.
  setOperationAction({ISD::STRICT_FP16_TO_FP, ISD::STRICT_FP_TO_FP16,
                      ISD::STRICT_BF16_TO_FP, ISD::STRICT_FP_TO_BF16},
                     {MVT::f32, MVT::f64}, Custom);
  setOperationAction(ISD::STRICT_BF16_TO_FP, MVT::i16, Custom);
  // A strictfp function receives a bfloat argument as a STRICT_FP_ROUND of
  // its float32, which DAGCombiner does not fold into an extension of it:
  // once the rounding has become plain, LowerFP_EXTEND (keyed on the f16 or
  // bf16 operand) and LowerStrictFP fold the extensions, so that they compile
  // as without strictfp.
  setOperationAction(ISD::FP_EXTEND, {MVT::f16, MVT::bf16}, Custom);
  // llvm.set.rounding cannot change the rounding mode: LowerSET_ROUNDING
  // diagnoses it. llvm.get.rounding expands to the constant 1 (to nearest).
  setOperationAction(ISD::SET_ROUNDING, MVT::Other, Custom);
  // llvm.fptrunc.round to f16, bf16 or a vector: the type legalizer has no
  // soft promotion, scalarization or splitting of it, but asks
  // ReplaceNodeResults first, which handles it like LowerFPTRUNC_ROUND (a
  // plain fptrunc for round.tonearest).
  setOperationAction(ISD::FPTRUNC_ROUND, {MVT::f16, MVT::bf16}, Custom);
  for (MVT T : MVT::fp_fixedlen_vector_valuetypes())
    setOperationAction(ISD::FPTRUNC_ROUND, T, Custom);
  // The MSVC C runtime has no ldexpf (RuntimeLibcalls.td), and native clang
  // calls ldexp there. Do the same instead of the generic expansion, which
  // goes through a stack slot: the double result rounds once to float, so the
  // value does not change.
  if (getLibcallImpl(RTLIB::LDEXP_F32) == RTLIB::Unsupported)
    setOperationPromotedToType({ISD::FLDEXP, ISD::STRICT_FLDEXP}, MVT::f32,
                               MVT::f64);

  // SETCC stays Legal for i32, i64, f32 and f64: every integer and floating-
  // point condition code that is not Expand above is selected, the negated
  // ones as two compares (MSILInstrInteger.td, MSILInstrFloat.td). CIL has
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

/// Returns the plain form of the constrained floating-point node \p N: the
/// same operation on the same operands, without the chain.
static SDValue getPlainFP(SDNode *N, SelectionDAG &DAG) {
  unsigned Opc;
  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Unexpected constrained floating-point operation");
#define DAG_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
  case ISD::STRICT_##DAGN:                                                     \
    Opc = ISD::DAGN;                                                           \
    break;
#define CMP_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
  case ISD::STRICT_##DAGN:                                                     \
    Opc = ISD::SETCC;                                                          \
    break;
#include "llvm/IR/ConstrainedOps.def"
  case ISD::STRICT_FP16_TO_FP:
    Opc = ISD::FP16_TO_FP;
    break;
  case ISD::STRICT_FP_TO_FP16:
    Opc = ISD::FP_TO_FP16;
    break;
  case ISD::STRICT_BF16_TO_FP:
    Opc = ISD::BF16_TO_FP;
    break;
  case ISD::STRICT_FP_TO_BF16:
    Opc = ISD::FP_TO_BF16;
    break;
  }
  SmallVector<SDValue, 4> Ops(drop_begin(N->ops()));
  return DAG.getNode(Opc, SDLoc(N), N->getValueType(0), Ops, N->getFlags());
}

/// Returns \p Src extended to \p VT without the round trip through f16 or
/// bf16 when \p Src is an FP_ROUND that is known not to change the value, as
/// DAGCombiner folds a plain fp_extend of it; returns no value otherwise.
static SDValue foldExtendOfExactRound(SDValue Src, EVT VT, const SDLoc &DL,
                                      SelectionDAG &DAG) {
  if (Src.getOpcode() != ISD::FP_ROUND || Src.getConstantOperandVal(1) != 1)
    return SDValue();
  SDValue In = Src.getOperand(0);
  if (In.getValueType() == VT)
    return In;
  if (VT.bitsLT(In.getValueType()))
    return DAG.getNode(ISD::FP_ROUND, DL, VT, In, Src.getOperand(1));
  return DAG.getNode(ISD::FP_EXTEND, DL, VT, In);
}

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
  case ISD::SINT_TO_FP:
    return LowerSINT_TO_FP(Op, DAG);
  case ISD::UINT_TO_FP:
    return LowerUINT_TO_FP(Op, DAG);
#define DAG_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
  case ISD::STRICT_##DAGN:
#include "llvm/IR/ConstrainedOps.def"
  case ISD::STRICT_FP16_TO_FP:
  case ISD::STRICT_FP_TO_FP16:
  case ISD::STRICT_BF16_TO_FP:
  case ISD::STRICT_FP_TO_BF16:
    return LowerStrictFP(Op, DAG);
  case ISD::FP_EXTEND:
    return LowerFP_EXTEND(Op, DAG);
  case ISD::FMINIMUM:
  case ISD::FMAXIMUM:
  case ISD::FMINIMUMNUM:
  case ISD::FMAXIMUMNUM:
    return LowerFMinMax(Op, DAG);
  case ISD::SET_ROUNDING:
    return LowerSET_ROUNDING(Op, DAG);
  case ISD::FPTRUNC_ROUND:
    return LowerFPTRUNC_ROUND(Op, DAG);
  case ISD::FSINCOS:
    return LowerFSINCOS(Op, DAG);
  case ISD::FFREXP:
  case ISD::FMODF:
    return LowerFFREXP_FMODF(Op, DAG);
  case ISD::BF16_TO_FP:
  case ISD::FP_TO_BF16:
    return LowerBF16Conversion(Op, DAG);
  default:
    reportFatalInternalError(Twine("MSIL: unexpected operation in "
                                   "LowerOperation: ") +
                             Op->getOperationName(&DAG));
  }
}

void MSILTargetLowering::ReplaceNodeResults(SDNode *N,
                                            SmallVectorImpl<SDValue> &Results,
                                            SelectionDAG &DAG) const {
  // llvm.fptrunc.round to f16, bf16 or a vector: round.tonearest becomes a
  // plain FP_ROUND of the same type, which legalizes like 'fptrunc' (conv.r4
  // per element, or the f16 and bf16 libcalls that LowerCall diagnoses).
  if (N->getOpcode() == ISD::FPTRUNC_ROUND) {
    Results.push_back(LowerFPTRUNC_ROUND(SDValue(N, 0), DAG));
    return;
  }

  EVT VT = N->getValueType(0);
  bool IsStrict = N->isStrictFPOpcode();
  // A constrained operation with an f16 or bf16 result: its plain form, which
  // the type legalizer then soft-promotes like the plain operation, and the
  // input chain (separately: soft promotion has no case for MERGE_VALUES).
  // The constrained FP -> i128 conversions have an integer result and are
  // reported below.
  if (IsStrict && VT.isFloatingPoint()) {
    Results.push_back(getPlainFP(N, DAG));
    Results.push_back(N->getOperand(0));
    return;
  }

  // i128 division and remainder, and FP -> i128 conversions (the other Custom
  // nodes with an illegal result type): report the libcall that the type
  // legalizer would build, with LowerCall's text, and go on with an undefined
  // result.
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
  case ISD::FP_TO_SINT:
  case ISD::STRICT_FP_TO_SINT:
  case ISD::FP_TO_UINT:
  case ISD::STRICT_FP_TO_UINT: {
    // The type legalizer extends an f16 or bf16 operand (soft-promoted) to
    // f32 before it builds the libcall (ExpandIntRes_FP_TO_XINT).
    EVT OpVT = N->getOperand(IsStrict ? 1 : 0).getValueType();
    if (OpVT == MVT::bf16 ||
        getTypeAction(*DAG.getContext(), OpVT) == TypeSoftPromoteHalf)
      OpVT = MVT::f32;
    bool IsSigned = N->getOpcode() == ISD::FP_TO_SINT ||
                    N->getOpcode() == ISD::STRICT_FP_TO_SINT;
    LC = IsSigned ? RTLIB::getFPTOSINT(OpVT, VT) : RTLIB::getFPTOUINT(OpVT, VT);
    break;
  }
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
  // A constrained conversion also passes its input chain through.
  if (IsStrict)
    Results.push_back(N->getOperand(0));
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

/// Converts the native int \p X to f32 with a conv.r4 that rounds once also
/// when the JIT constant-folds it. The .NET Framework 4.8 JIT folds conv.r4 of
/// a constant int64 as (float32)(float64)x, which rounds twice when x needs
/// more than 53 bits, and conv.r4 sees a constant as soon as the JIT inlines
/// the conversion into a caller that passes one. So x is first rounded to odd
/// at bit 11 when its magnitude needs more than 53 bits, without a SELECT, a
/// branch or memory:
///
///   t  = x ^ (x >>s 63)                        |x|, or |x| - 1 below zero
///   f  = ((t >>u 53) + 1023) >>u 10            1 if t >= 2^53, else 0
///   m  = f * 0x7ff                             bits 0-10, or none
///   x' = (x & ~m) | (((x & m) + m) & (m + 1))
///
/// x' is x with bits 0-10 cleared and bit 11 set when any of them was set,
/// which in two's complement also rounds |x| to odd. x' keeps 43 or more
/// significant bits, so float64 holds it exactly, and it rounds to the same
/// float32 as x. \p NonNegative says that x is below 2^63 (then t is x).
static SDValue lowerI64ToF32(SDValue X, bool NonNegative, const SDLoc &DL,
                             SelectionDAG &DAG) {
  const MVT VT = MVT::i64;
  SDValue T = X;
  if (!NonNegative)
    T = DAG.getNode(ISD::XOR, DL, VT, X,
                    DAG.getNode(ISD::SRA, DL, VT, X,
                                DAG.getShiftAmountConstant(63, VT, DL)));
  SDValue F = DAG.getNode(
      ISD::SRL, DL, VT,
      DAG.getNode(ISD::ADD, DL, VT,
                  DAG.getNode(ISD::SRL, DL, VT, T,
                              DAG.getShiftAmountConstant(53, VT, DL)),
                  DAG.getConstant(1023, DL, VT)),
      DAG.getShiftAmountConstant(10, VT, DL));
  SDValue M = DAG.getNode(ISD::MUL, DL, VT, F, DAG.getConstant(0x7ff, DL, VT));
  SDValue High = DAG.getNode(ISD::AND, DL, VT, X, DAG.getNOT(DL, M, VT));
  SDValue Sticky = DAG.getNode(
      ISD::AND, DL, VT,
      DAG.getNode(ISD::ADD, DL, VT, DAG.getNode(ISD::AND, DL, VT, X, M), M),
      DAG.getNode(ISD::ADD, DL, VT, M, DAG.getConstant(1, DL, VT)));
  SDValue Odd = DAG.getNode(ISD::OR, DL, VT, High, Sticky);
  // conv.r4 itself: a SINT_TO_FP node would come back to LowerSINT_TO_FP.
  return SDValue(DAG.getMachineNode(MSIL::CONV_R4_I64, DL, MVT::f32, Odd), 0);
}

SDValue MSILTargetLowering::LowerSINT_TO_FP(SDValue Op,
                                            SelectionDAG &DAG) const {
  // int64 -> f64 is one conv.r8, which rounds once even when the JIT folds
  // it, and so is int64 -> f32 of a value that float64 holds exactly (at
  // most 2^53 in magnitude: 11 or more sign bits). Those stay as they are.
  SDValue X = Op.getOperand(0);
  assert(X.getValueType() == MVT::i64 && "Only int64 -> FP is Custom");
  if (Op.getValueType() != MVT::f32 || DAG.ComputeNumSignBits(X) > 10)
    return Op;
  return lowerI64ToF32(X, DAG.SignBitIsZero(X), SDLoc(Op), DAG);
}

SDValue MSILTargetLowering::LowerUINT_TO_FP(SDValue Op,
                                            SelectionDAG &DAG) const {
  // uint64 -> f32 or f64 (decision D17). conv.r.un rounds a 64-bit value
  // twice on .NET 8, .NET 9 and .NET Framework (cil-runtime e05), and LLVM's
  // generic expansions need a SELECT (f32) or a BITCAST through a stack slot
  // (f64). Instead, halve a value of 2^63 or more, keeping the bit shifted out
  // as a sticky bit, convert it as a signed value and double the result:
  //
  //   s = x >>u 63
  //   y = (x >>u s) | (x & s)
  //   result = sint_to_fp(y) * sint_to_fp(s + 1)
  //
  // Below 2^63, s is 0, y is x and the factor is 1.0. Otherwise y is below
  // 2^63 and rounds like x / 2 (the sticky bit stands for every bit shifted
  // out), and the multiplication by 2.0 is exact. This is compiler-rt's
  // __floatundidf algorithm without a branch, a select or memory. conv.r4 and
  // conv.r8 of a non-negative native int round once at run time on every
  // runtime (cil-runtime e05b), so the result is correctly rounded. For f32,
  // y is rounded to odd first (lowerI64ToF32), so that it also rounds once
  // when the JIT folds conv.r4 through float64.
  SDLoc DL(Op);
  SDValue X = Op.getOperand(0);
  EVT VT = Op.getValueType();
  assert(X.getValueType() == MVT::i64 && "Only uint64 -> FP is Custom");

  // A value below 2^63 converts as a signed one.
  if (Op->getFlags().hasNonNeg() || DAG.SignBitIsZero(X))
    return DAG.getNode(ISD::SINT_TO_FP, DL, VT, X);

  SDValue S = DAG.getNode(ISD::SRL, DL, MVT::i64, X,
                          DAG.getShiftAmountConstant(63, MVT::i64, DL));
  SDValue Halved = DAG.getNode(ISD::SRL, DL, MVT::i64, X,
                               DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, S));
  SDValue Sticky = DAG.getNode(ISD::AND, DL, MVT::i64, X, S);
  SDValue Y = DAG.getNode(ISD::OR, DL, MVT::i64, Halved, Sticky);
  SDValue R = VT == MVT::f32 ? lowerI64ToF32(Y, /*NonNegative=*/true, DL, DAG)
                             : DAG.getNode(ISD::SINT_TO_FP, DL, VT, Y);
  // s + 1 is 1 or 2, exact in float64: LowerSINT_TO_FP keeps one conversion.
  SDValue Factor = DAG.getNode(
      ISD::SINT_TO_FP, DL, VT,
      DAG.getNode(ISD::ADD, DL, MVT::i64, S, DAG.getConstant(1, DL, MVT::i64)));
  return DAG.getNode(ISD::FMUL, DL, VT, R, Factor);
}

SDValue MSILTargetLowering::LowerStrictFP(SDValue Op, SelectionDAG &DAG) const {
  // CIL floating-point operations never trap and always round to nearest,
  // so a constrained operation behaves exactly like its plain form. Build
  // that and pass the input chain through; the legalizer then handles the
  // plain node (condition codes, uint64 -> FP, fminimum and fmaximum, the
  // soft promotion of an f16 or bf16 operand, the f16 and bf16 libcalls).
  SDLoc DL(Op);
  SDValue Plain;
  if (Op.getOpcode() == ISD::STRICT_FP_EXTEND)
    Plain =
        foldExtendOfExactRound(Op.getOperand(1), Op.getValueType(), DL, DAG);
  if (!Plain)
    Plain = getPlainFP(Op.getNode(), DAG);
  return DAG.getMergeValues({Plain, Op.getOperand(0)}, DL);
}

SDValue MSILTargetLowering::LowerFP_EXTEND(SDValue Op,
                                           SelectionDAG &DAG) const {
  // The extension of an f16 or bf16 value, before the type legalizer
  // soft-promotes it. A bfloat argument or call result arrives as a float32
  // and is an exact FP_ROUND of it, which DAGCombiner folds into a plain
  // fp_extend; in a strictfp function it is a STRICT_FP_ROUND, which becomes
  // plain (ReplaceNodeResults) only after DAGCombiner ran. Fold it here, as
  // LowerStrictFP does for STRICT_FP_EXTEND, so that the extension compiles
  // as in a function without strictfp. Anything else is soft-promoted.
  return foldExtendOfExactRound(Op.getOperand(0), Op.getValueType(), SDLoc(Op),
                                DAG);
}

SDValue MSILTargetLowering::LowerFMinMax(SDValue Op, SelectionDAG &DAG) const {
  // fminimum, fmaximum, fminimum_num and fmaximum_num of a and b as one
  // SELECT, with no call and no memory. Its condition says when a is the
  // result (for the maximums read > and +0.0 instead of < and -0.0):
  //
  //   fminimum:      a < b  or  a is a NaN  or  (a == b and a is -0.0)
  //   fminimum_num:  a < b  or  b is a NaN  or  (a == b and a is -0.0)
  //
  // Otherwise b is: the smaller operand, the NaN that fminimum returns, the
  // operand that fminimum_num returns when a is a NaN, or an equal value.
  // Equal values are identical except for zeros of different signs, where
  // the sign of a decides. It is tested arithmetically, as 1.0 / a < 0.0
  // (1.0 / -0.0 is -inf), because the generic expansions' IS_FPCLASS works
  // on the integer bits, through a BITCAST and therefore a stack slot.
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue A = Op.getOperand(0);
  SDValue B = Op.getOperand(1);
  unsigned Opc = Op.getOpcode();
  bool IsMax = Opc == ISD::FMAXIMUM || Opc == ISD::FMAXIMUMNUM;
  bool IsNum = Opc == ISD::FMINIMUMNUM || Opc == ISD::FMAXIMUMNUM;
  SDNodeFlags Flags = Op->getFlags();
  EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  ISD::CondCode OrderedCC = IsMax ? ISD::SETOGT : ISD::SETOLT;

  // A NaN in PicksA makes a the result, a NaN in PicksB makes b the result.
  // The ordered compare is false for any NaN, which covers PicksB. When only
  // PicksA can be a NaN, the unordered compare covers it; when both can, it
  // needs a test of its own.
  SDValue PicksA = IsNum ? B : A;
  SDValue PicksB = IsNum ? A : B;
  bool PicksAMayBeNaN = !Flags.hasNoNaNs() && !DAG.isKnownNeverNaN(PicksA);
  bool PicksBMayBeNaN = !Flags.hasNoNaNs() && !DAG.isKnownNeverNaN(PicksB);
  ISD::CondCode CC = OrderedCC;
  if (PicksAMayBeNaN && !PicksBMayBeNaN)
    CC = IsMax ? ISD::SETUGT : ISD::SETULT;
  SDValue Cond = DAG.getSetCC(DL, CCVT, A, B, CC);
  if (PicksAMayBeNaN && PicksBMayBeNaN)
    Cond = DAG.getNode(ISD::OR, DL, CCVT, Cond,
                       DAG.getSetCC(DL, CCVT, PicksA, PicksA, ISD::SETUNE));

  // -0.0 orders below +0.0.
  if (!Flags.hasNoSignedZeros() && !DAG.isKnownNeverLogicalZero(A) &&
      !DAG.isKnownNeverLogicalZero(B)) {
    SDValue Recip =
        DAG.getNode(ISD::FDIV, DL, VT, DAG.getConstantFP(1.0, DL, VT), A);
    SDValue SignOfA = DAG.getSetCC(DL, CCVT, Recip,
                                   DAG.getConstantFP(0.0, DL, VT), OrderedCC);
    SDValue Tie = DAG.getNode(
        ISD::AND, DL, CCVT, DAG.getSetCC(DL, CCVT, A, B, ISD::SETOEQ), SignOfA);
    Cond = DAG.getNode(ISD::OR, DL, CCVT, Cond, Tie);
  }
  return DAG.getSelect(DL, VT, Cond, A, B);
}

/// Reports a change of the floating-point rounding mode, which CIL cannot
/// make: it always rounds to nearest.
static void diagnoseRoundingMode(SDValue Op, SelectionDAG &DAG) {
  DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
      DAG.getMachineFunction().getFunction(),
      "changing the floating-point rounding mode is not supported for MSIL",
      SDLoc(Op).getDebugLoc()));
}

SDValue MSILTargetLowering::LowerSET_ROUNDING(SDValue Op,
                                              SelectionDAG &DAG) const {
  // llvm.set.rounding: go on in the only mode there is.
  diagnoseRoundingMode(Op, DAG);
  return Op.getOperand(0);
}

SDValue MSILTargetLowering::LowerFPTRUNC_ROUND(SDValue Op,
                                               SelectionDAG &DAG) const {
  // llvm.fptrunc.round: conv.r4 rounds to nearest, and no other mode exists.
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  if (static_cast<RoundingMode>(Op.getConstantOperandVal(1)) ==
      RoundingMode::NearestTiesToEven)
    return DAG.getNode(ISD::FP_ROUND, DL, VT, Op.getOperand(0),
                       DAG.getIntPtrConstant(0, DL, /*isTarget=*/true),
                       Op->getFlags());
  diagnoseRoundingMode(Op, DAG);
  return DAG.getUNDEF(VT);
}

SDValue MSILTargetLowering::LowerFSINCOS(SDValue Op, SelectionDAG &DAG) const {
  // FSIN and FCOS are LibCall, so this does not recombine them.
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue Src = Op.getOperand(0);
  return DAG.getMergeValues(
      {DAG.getNode(ISD::FSIN, DL, VT, Src, Op->getFlags()),
       DAG.getNode(ISD::FCOS, DL, VT, Src, Op->getFlags())},
      DL);
}

SDValue MSILTargetLowering::LowerFFREXP_FMODF(SDValue Op,
                                              SelectionDAG &DAG) const {
  // The libcall would store its second result into a stack temporary before
  // LowerCall even sees it, so report it here, with LowerCall's text, and go
  // on with undefined results.
  EVT VT = Op.getValueType();
  auto GetLibcall = [&](EVT T) {
    return Op.getOpcode() == ISD::FFREXP ? RTLIB::getFREXP(T)
                                         : RTLIB::getMODF(T);
  };
  RTLIB::Libcall LC = GetLibcall(VT);
  // The MSVC C runtime has no frexpf: there the float32 call is the double
  // function, as native clang calls it.
  if (VT == MVT::f32 && getLibcallImpl(LC) == RTLIB::Unsupported)
    LC = GetLibcall(MVT::f64);
  const char *Name = getLibcallName(LC);
  SDLoc DL(Op);
  DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
      DAG.getMachineFunction().getFunction(),
      Twine("runtime library call '") + (Name && *Name ? Name : "?") +
          "' is not supported for MSIL yet",
      DL.getDebugLoc()));
  return DAG.getMergeValues(
      {DAG.getUNDEF(VT), DAG.getUNDEF(Op->getValueType(1))}, DL);
}

SDValue MSILTargetLowering::LowerBF16Conversion(SDValue Op,
                                                SelectionDAG &DAG) const {
  // bf16 values are held as integers. The generic expansions of these
  // conversions shift the bits and BITCAST them to or from f32 through a
  // stack slot; like the f16 conversions, call the compiler-rt helper
  // (__extendbfsf2, __truncsfbf2, __truncdfbf2) instead, which LowerCall
  // diagnoses.
  SDLoc DL(Op);
  SDValue Src = Op.getOperand(0);
  EVT VT = Op.getValueType();
  bool IsExtend = Op.getOpcode() == ISD::BF16_TO_FP;
  RTLIB::Libcall LC = IsExtend
                          ? RTLIB::FPEXT_BF16_F32
                          : RTLIB::getFPROUND(Src.getValueType(), MVT::bf16);
  // A wider result is the exact extension of the f32 one.
  EVT CallVT = IsExtend ? EVT(MVT::f32) : VT;
  SDValue Result;
  if (LC != RTLIB::UNKNOWN_LIBCALL &&
      getLibcallImpl(LC) != RTLIB::Unsupported) {
    Result = makeLibCall(DAG, LC, CallVT, Src, MakeLibCallOptions(), DL).first;
  } else {
    DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
        DAG.getMachineFunction().getFunction(),
        "bf16 conversions are not supported for MSIL", DL.getDebugLoc()));
    Result = DAG.getUNDEF(CallVT);
  }
  if (CallVT != VT)
    Result = DAG.getNode(ISD::FP_EXTEND, DL, VT, Result);
  return Result;
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
