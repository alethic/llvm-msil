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
#include "MSILAsmPrinter.h"
#include "MSILSelectionDAGInfo.h"
#include "MSILSubtarget.h"
#include "MSILUtilities.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/CLI/TrailerDefs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/RuntimeLibcallUtil.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "msil-lower"

/// Returns the size in bytes of the native va_list structure of \p TT
/// (Design.md 4.7): the SysV x86-64 __va_list_tag (24 bytes) or the AAPCS64
/// structure (32 bytes). Returns 0 where va_list is a plain char*: on Windows
/// (msvc and gnu, x64 and ARM64) and on Apple ARM64.
static unsigned getNativeVAListStructSize(const Triple &TT) {
  if (TT.isOSWindows() || (TT.isAArch64() && TT.isOSDarwin()))
    return 0;
  return TT.isAArch64() ? 32 : 24;
}

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
    // No CIL opcode, and no BCL members in Phase 1: these become calls of the
    // C math library, which are P/Invokes (decision D14, LowerCall). fmin and
    // fmax become selects when no operand can be a NaN. roundeven and exp10
    // are not Phase-1 imports, so LowerCall diagnoses them.
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
    // sin and cos are math library calls too, but LibCall rather than
    // Expand: the expansion merges a sin and a cos of the same value into a
    // call of sincos, which is not a Phase-1 import (decision D14). For the
    // same reason LowerFSINCOS splits an explicit FSINCOS into the two calls.
    setOperationAction({ISD::FSIN, ISD::FCOS}, T, LibCall);
    setOperationAction(ISD::FSINCOS, T, Custom);
    // frexp and modf keep their default Expand action: a call of the math
    // library function (a P/Invoke), which returns its second result through
    // a stack temporary (see below for float32 frexp on Windows).
    // llvm.fptrunc.round: only round.tonearest is supported.
    setOperationAction(ISD::FPTRUNC_ROUND, T, Custom);
    // fabs and copysign: the generic expansions work on the integer bits,
    // through BITCAST and therefore a stack slot (decision D16).
    setOperationAction({ISD::FABS, ISD::FCOPYSIGN}, T, Expand);
    // f16 and bf16 are not legal types: their values are held as integers,
    // and arithmetic converts them to and from f32. Those conversions are the
    // compiler-rt helpers (__extendhfsf2, __truncsfhf2, __truncdfhf2,
    // __truncsfbf2 and __truncdfbf2) of the helper archive clang_rt.builtins,
    // which msil-ld links and text output reports (LowerCall, plan 5.14.3
    // A9). bf16 -> f32 is exact, and its generic expansion is inline: the 16
    // bits shifted left by 16, bit cast to f32 (through a stack slot,
    // decision D16), extended for an f64 result. The generic expansion of
    // f32 -> bf16 truncates instead of rounding, so LowerBF16Conversion calls
    // the helpers, and after __truncsfbf2 it sets the quiet bit of a NaN, as
    // native x86-64 code does.
    setOperationAction({ISD::FP16_TO_FP, ISD::FP_TO_FP16}, T, Expand);
    setOperationAction(ISD::BF16_TO_FP, T, Expand);
    setOperationAction(ISD::FP_TO_BF16, T, Custom);
  }
  // There is no reinterpreting opcode: a bitcast between an integer and a
  // floating-point value goes through a stack slot (decision D16).
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
  // FP -> i128: ExpandIRInsts expands these conversions before instruction
  // selection (setMaxLargeFPConvertBitWidthSupported below), the constrained
  // ones too once MSILPrepareIR has made them plain
  // (MSIL::relaxStrictInt128Conversions), so none should get here. One that
  // did would call __fixsfti, __fixdfti, __fixunssfti or __fixunsdfti, whose
  // two-value result CanLowerReturn demotes to a stack slot even after
  // LowerCall's diagnostic; ReplaceNodeResults reports the libcall instead,
  // as it does for i128 division.
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
  // operation, into the compiler-rt conversions, which LowerCall diagnoses in
  // text output.
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
  // Calls pass a bf16 value as its bits (getRegisterTypeForCallingConv), but
  // a bf16 value that another block computed arrives in a float32 virtual
  // register (TargetLoweringBase's register type of bf16), and a strictfp
  // function receives it as a STRICT_FP_ROUND of that float32, which
  // DAGCombiner does not fold into an extension of it: once the rounding has
  // become plain, LowerFP_EXTEND (keyed on the f16 or bf16 operand) and
  // LowerStrictFP fold the extensions, so that they compile as without
  // strictfp.
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
  // The MSVC C runtime has no ldexpf and no frexpf (RuntimeLibcalls.td), and
  // native clang calls ldexp and frexp there. msvcrt.dll, the C runtime of
  // windows-gnu, does not export them either (MinGW links its own from
  // libmingwex), although RuntimeLibcalls lists them for MinGW. Wherever the
  // C runtime lacks the float32 function and the double libcall exists, call
  // the double one, instead of the generic expansions, which work on the
  // integer bits through a stack slot, or a call that cannot be resolved:
  // the double result of ldexp rounds once to float, and the fraction that
  // frexp returns for a float fits in a float, so the values do not change.
  // This depends on RuntimeLibcalls and the triple only, never on an import
  // policy, so that standalone, linked and object code agree (plan 5.10.13).
  bool NoFloatLdexpFrexp = TM.getTargetTriple().isWindowsGNUEnvironment();
  auto HasLibcall = [&](RTLIB::Libcall LC) {
    return getLibcallImpl(LC) != RTLIB::Unsupported;
  };
  if ((NoFloatLdexpFrexp || !HasLibcall(RTLIB::LDEXP_F32)) &&
      HasLibcall(RTLIB::LDEXP_F64))
    setOperationPromotedToType({ISD::FLDEXP, ISD::STRICT_FLDEXP}, MVT::f32,
                               MVT::f64);
  if ((NoFloatLdexpFrexp || !HasLibcall(RTLIB::FREXP_F32)) &&
      HasLibcall(RTLIB::FREXP_F64))
    setOperationPromotedToType(ISD::FFREXP, MVT::f32, MVT::f64);

  // SETCC stays Legal for i32, i64, f32 and f64: every integer and floating-
  // point condition code that is not Expand above is selected, the negated
  // ones as two compares (MSILInstrInteger.td, MSILInstrFloat.td). CIL has
  // no select instruction, but SELECT is Legal for every value type: it
  // selects the SELECT_<RC> pseudos, which emitSelect turns into a branch
  // triangle (decision D34), because generic legalization emits SELECT
  // (variable i128 shifts, min/max). The fused forms are split into SETCC
  // plus SELECT or BRCOND; instruction selection turns BRCOND of a SETCC into
  // one CIL compare-and-branch (beq, blt.un, ...; MSILInstrControl.td).
  for (MVT T : {MVT::i32, MVT::i64, MVT::f32, MVT::f64}) {
    setOperationAction(ISD::SELECT, T, Legal);
    setOperationAction({ISD::SELECT_CC, ISD::BR_CC}, T, Expand);
  }

  // TRAP is 'ldnull; throw', a terminator ('unreachable' always becomes a
  // trap). Expanded, it would be a call to abort, after which the method could
  // fall off its end, which CIL forbids. DEBUGTRAP and UBSANTRAP keep their
  // default Expand action, which makes them TRAP as well.
  setOperationAction(ISD::TRAP, MVT::Other, Legal);

  // llvm.clear_cache has nothing to flush: a CIL program writes no code (the
  // runtime keeps the code it compiles coherent), so CLEAR_CACHE expands to
  // nothing, as on X86, instead of its default call of compiler-rt's
  // __clear_cache, which the helper archive does not have (plan 5.14.3 A5).
  setOperationAction(ISD::CLEAR_CACHE, MVT::Other, Expand);

  // Every operation that LowerOperation dispatches is Custom, because
  // SelectionDAG calls LowerOperation only for Custom actions.
  // Frame indices, global addresses and external symbols. Pointers are I64.
  // A frame index becomes a TargetFrameIndex operand of its users, which
  // MSILRegisterInfo::eliminateFrameIndex rewrites into an address relative
  // to the frame base (decision D13). A global variable is reached through
  // its address field (decision D12), and so is a function, whose field
  // <Module>::.cctor sets with ldftn.
  setOperationAction({ISD::GlobalAddress, ISD::ExternalSymbol, ISD::FrameIndex},
                     MVT::i64, Custom);
  // Thread-local storage waits for phase 3 (MSILPrepareIR already reports
  // every thread-local variable and makes it an ordinary global, so this is a
  // backstop). IndirectBrExpand turns the address of every label that an
  // indirectbr can branch to into an integer index, so only the address of
  // a label that no indirectbr of its function targets reaches instruction
  // selection (GNU C '&&label' without a computed goto). Both are diagnosed;
  // without the Custom action they would end in 'Cannot select'.
  setOperationAction({ISD::GlobalTLSAddress, ISD::BlockAddress}, MVT::i64,
                     Custom);
  // A CopyToReg cannot take a frame index operand: LowerCopyToReg copies it
  // into a register first.
  setOperationAction(ISD::CopyToReg, MVT::Other, Custom);
  // A dynamic alloca that MSILLowerDynamicAlloca left (one on no CFG cycle)
  // is a localloc of its own (LowerDYNAMIC_STACKALLOC, plan 5.15.8 H0). That
  // pass deletes llvm.stacksave and llvm.stackrestore, so STACKSAVE and
  // STACKRESTORE are diagnosed only as backstops; the frame address needs
  // the shadow stack; CIL has no return address, entry stack pointer or
  // DWARF call frame, and no executable memory for trampolines.
  // LowerOperation diagnoses each of them with its own text (plan section 12,
  // 'M7.b diagnostic texts amended', and M26's rows). The legalizer would
  // treat FRAMEADDR, RETURNADDR, ADDROFRETURNADDR, SPONENTRY and the
  // trampolines as Custom anyway. EH_DWARF_CFA must be Custom: a Legal one is
  // expanded into a FRAMEADDR, which would report the frame address instead.
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Custom);
  setOperationAction({ISD::STACKSAVE, ISD::STACKRESTORE, ISD::INIT_TRAMPOLINE},
                     MVT::Other, Custom);
  setOperationAction({ISD::FRAMEADDR, ISD::RETURNADDR, ISD::ADDROFRETURNADDR,
                      ISD::SPONENTRY, ISD::ADJUST_TRAMPOLINE,
                      ISD::EH_DWARF_CFA},
                     MVT::i64, Custom);
  // The stack address (the shadow stack), the setjmp/longjmp builtins
  // (phase 3) and llvm.eh.return are diagnosed the same way, and must be
  // Custom too. The legalizer looks up the EH nodes by their first result
  // type and silently turns Legal into Expand, which drops the longjmp and
  // the eh.return, folds the setjmp to 0 and leaves the dispatch setup to
  // 'Cannot select'. It looks up STACKADDRESS on MVT::Other, and the default
  // Expand prints a generic text with no function name or source location.
  setOperationAction(ISD::EH_SJLJ_SETJMP, MVT::i32, Custom);
  setOperationAction({ISD::EH_SJLJ_LONGJMP, ISD::EH_SJLJ_SETUP_DISPATCH,
                      ISD::EH_RETURN, ISD::STACKADDRESS},
                     MVT::Other, Custom);
  // A va_list parameter compiles, and ExpandVariadics lowers variadic
  // definitions and calls before instruction selection (decision D19 as
  // amended, plan 5.15.2); clang lowers va_arg inline for the native va_list
  // layout of every triple (Design.md 3 and 4.7). va_end does nothing on any
  // of those ABIs: Expand makes it its input chain. va_copy copies the
  // va_list: a pointer load and store (Expand) where it is a char*, a cpblk
  // of the native structure elsewhere (LowerVACOPY). MSILPrepareIR reports a
  // va_arg instruction on the triples whose va_list is a structure, and
  // ExpandVariadics expands it where va_list is a char*, so a VAARG node is a
  // backstop that LowerOperation diagnoses; the legalizer looks VAARG up on
  // MVT::Other. A result type that the type legalizer changes asks
  // ReplaceNodeResults first, which reports the instruction once: soft
  // promotion (f16, bf16), scalarization and widening have no case for VAARG,
  // and expansion would make several.
  setOperationAction(ISD::VAEND, MVT::Other, Expand);
  bool VAListIsStruct = getNativeVAListStructSize(TM.getTargetTriple()) != 0;
  setOperationAction(ISD::VACOPY, MVT::Other, VAListIsStruct ? Custom : Expand);
  setOperationAction(ISD::VAARG, {MVT::Other, MVT::f16, MVT::bf16}, Custom);
  for (MVT T : MVT::integer_valuetypes())
    if (T.getSizeInBits() > 64)
      setOperationAction(ISD::VAARG, T, Custom);
  for (MVT T : MVT::vector_valuetypes())
    setOperationAction(ISD::VAARG, T, Custom);
  // Jump tables become a CIL 'switch' on an int32 index (LowerBR_JT, decision
  // D35). Custom, like Legal, keeps jump tables allowed (areJTsAllowed). An
  // indirectbr never gets here: IndirectBrExpand turns it into a switch.
  setOperationAction(ISD::BR_JT, MVT::Other, Custom);
  // i128 shifts by a variable amount: Custom, so that the type legalizer
  // builds *_PARTS nodes instead of calling __ashlti3, __lshrti3 or __ashrti3.
  setOperationAction({ISD::SHL_PARTS, ISD::SRA_PARTS, ISD::SRL_PARTS}, MVT::i64,
                     Custom);
  // i128 division and remainder: ExpandIRInsts expands every one in the IR
  // inline before instruction selection (setMaxDivRemBitWidthSupported
  // below), so this is a backstop for a division that SelectionDAG itself
  // builds. Such a division would call __divti3, __udivti3, __modti3 or
  // __umodti3, which the helper archive clang_rt.builtins does not have
  // (plan 5.14.3 A5), so it reaches ReplaceNodeResults, in every mode. The
  // helper's i128 result needs two values, so CanLowerReturn would make
  // LowerCallTo demote it to a stack slot (a FrameIndex) even after
  // LowerCall's diagnostic. ReplaceNodeResults reports the libcall in the
  // type legalizer instead, before any call is built.
  setOperationAction({ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM}, MVT::i128,
                     Custom);

  // Loads and stores. ldind and stind (MSILInstrMemory.td) read and write
  // int32, native int, float32 and float64 values, and int32 values from and
  // to 8 and 16 bits; an i1 in memory is a byte. The legalizer splits every
  // other extending load and truncating store into one of those and a
  // conversion: a native int goes to and from a narrower integer through an
  // int32 (conv.i, conv.u, conv.i4), a float64 to and from a float32 through
  // conv.r8 and conv.r4, and f16 and bf16 values, which are held as
  // integers, through integer loads and stores and the f16 and bf16
  // conversions.
  for (MVT T : MVT::integer_valuetypes())
    setLoadExtAction({ISD::EXTLOAD, ISD::ZEXTLOAD, ISD::SEXTLOAD}, T, MVT::i1,
                     Promote);
  for (MVT MemVT : {MVT::i8, MVT::i16, MVT::i32}) {
    setLoadExtAction({ISD::EXTLOAD, ISD::ZEXTLOAD, ISD::SEXTLOAD}, MVT::i64,
                     MemVT, Expand);
    setTruncStoreAction(MVT::i64, MemVT, Expand);
  }
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);
  for (MVT T : {MVT::f32, MVT::f64}) {
    setLoadExtAction(ISD::EXTLOAD, T, {MVT::f16, MVT::bf16}, Expand);
    setTruncStoreAction(T, MVT::f16, Expand);
    setTruncStoreAction(T, MVT::bf16, Expand);
  }
  // memcpy, memmove and memset are never expanded into loads and stores,
  // whatever their size: MSILSelectionDAGInfo turns each into cpblk, initblk
  // or Buffer::MemoryCopy (decision D15).
  MaxStoresPerMemcpy = MaxStoresPerMemcpyOptSize = 0;
  MaxStoresPerMemmove = MaxStoresPerMemmoveOptSize = 0;
  MaxStoresPerMemset = MaxStoresPerMemsetOptSize = 0;

  // No inline atomics yet: AtomicExpand turns every atomic operation into a
  // call of an __atomic_* function, libatomic's interface, which only
  // msil-ld links (plan 5.14.2 N2; text output reports it, 5.14.3 A9).
  setMaxAtomicSizeInBitsSupported(0);
  // A single-thread fence calls __msil_compiler_barrier (LowerATOMIC_FENCE);
  // every other fence selects MEMORY_BARRIER (decision D36 as amended).
  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, Custom);

  // Integer division and remainder wider than 64 bits, and the conversions
  // between such integers and floating point, are expanded inline by
  // ExpandIRInsts before instruction selection (NVPTX and AMDGPU do the same),
  // so i128 division, the f32 and f64 <-> i128 conversions (saturating ones
  // included, and constrained ones after MSIL::relaxStrictInt128Conversions)
  // and bf16 -> i128 (expanded on the value's 16-bit pattern) call no
  // __divti3, __fixdfti, ... helper, which the helper archive
  // clang_rt.builtins does not have (plan 5.14.3 A5). Still named, by the f32
  // step of their expansions, which the archive has: f16 -> i128
  // (__extendhfsf2), i128 -> f16 (__truncsfhf2) and i128 -> bf16
  // (__truncsfbf2).
  setMaxDivRemBitWidthSupported(64);
  setMaxLargeFPConvertBitWidthSupported(MSIL::MaxSelectedFPConvertBits);
}

//===----------------------------------------------------------------------===//
// Query hooks
//===----------------------------------------------------------------------===//

MVT MSILTargetLowering::getScalarShiftAmountTy(const DataLayout & /*DL*/,
                                               EVT /*VT*/) const {
  // CIL shifts take an int32 amount, whatever the width of the value.
  return MVT::i32;
}

/// The type that a call passes for a value of type \p VT: \p VT with f16 in
/// place of bf16, as a scalar or as the element type of a vector. A bf16
/// value crosses a call as f16 does, as its 16-bit pattern in an int32, in
/// every signature, and a vector of bf16 as the vector of f16 of the same
/// length does, one such int32 per element: compiler-rt's bf16 helpers return
/// their result so, and the IL signature of a function with a __bf16
/// parameter or result is the one that its callers' calls spell
/// (MSIL::computeLegalValueVTs asks the hooks below too). TargetLoweringBase
/// gives bf16 the register type f32, which would pass the elements of a
/// vector as float32 values and need __truncsfbf2 for a <1 x bfloat>. All
/// three hooks map the type, because SelectionDAGBuilder checks the parts of
/// a vector that getVectorTypeBreakdownForCallingConv describes against the
/// other two. Calling conventions are all lowered alike
/// (callingConvSupported).
static EVT getCallingConvVT(LLVMContext &Context, EVT VT) {
  return VT.getScalarType() == MVT::bf16
             ? VT.changeElementType(Context, MVT::f16)
             : VT;
}

MVT MSILTargetLowering::getRegisterTypeForCallingConv(LLVMContext &Context,
                                                      CallingConv::ID CC,
                                                      EVT VT) const {
  return TargetLowering::getRegisterTypeForCallingConv(
      Context, CC, getCallingConvVT(Context, VT));
}

unsigned MSILTargetLowering::getNumRegistersForCallingConv(LLVMContext &Context,
                                                           CallingConv::ID CC,
                                                           EVT VT) const {
  return TargetLowering::getNumRegistersForCallingConv(
      Context, CC, getCallingConvVT(Context, VT));
}

unsigned MSILTargetLowering::getVectorTypeBreakdownForCallingConv(
    LLVMContext &Context, CallingConv::ID CC, EVT VT, EVT &IntermediateVT,
    unsigned &NumIntermediates, MVT &RegisterVT) const {
  return TargetLowering::getVectorTypeBreakdownForCallingConv(
      Context, CC, getCallingConvVT(Context, VT), IntermediateVT,
      NumIntermediates, RegisterVT);
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
  // ('ldsfld' then 'ldc.i4; add'); a function address (the function's
  // address field, which holds the value of 'ldftn') takes no offset, nor
  // does the address of a function alias (object mode only).
  const GlobalValue *GV = GA->getGlobal();
  return !isa<Function>(GV) &&
         !(isa<GlobalAlias>(GV) && GV->getValueType()->isFunctionTy());
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

/// The largest jump table, in entries, that becomes one CIL 'switch'.
/// MSILISD::BR_TABLE carries every entry of its table as an SDNode operand,
/// N + 3 operands in all (chain, index, the N entries and the dummy last
/// target), and an SDNode holds at most SDNode::getMaxNumOperands() (65535)
/// operands. 4096 is far below that limit, and a switch just above it still
/// compiles fast enough for a boundary test.
static constexpr uint64_t MaxJumpTableEntries = 4096;
static_assert(MaxJumpTableEntries + 3 <= SDNode::getMaxNumOperands(),
              "A maximal jump table does not fit in one BR_TABLE node");

bool MSILTargetLowering::isSuitableForJumpTable(const SwitchInst *SI,
                                                uint64_t NumCases,
                                                uint64_t Range,
                                                ProfileSummaryInfo *PSI,
                                                BlockFrequencyInfo *BFI) const {
  // A jump table has one entry for every value in Range. Check the cap first,
  // also in optsize and minsize functions, for which the base class ignores
  // getMaximumJumpTableSize(). When optimizing, switch lowering then splits a
  // larger range into several tables, each behind LLVM's range check; at -O0
  // it builds a table only for the whole range, so a range above the cap
  // becomes a chain of compares. setMaximumJumpTableSize is not used: it sets
  // the -max-jump-table-size option, which every target in the process shares.
  if (Range > MaxJumpTableEntries)
    return false;
  return TargetLowering::isSuitableForJumpTable(SI, NumCases, Range, PSI, BFI);
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

/// The error for a va_arg instruction (LowerOperation, ReplaceNodeResults):
/// MSILPrepareIR's text (plan 5.15.2), for a va_arg that reaches instruction
/// selection without that pass.
static constexpr const char *VAArgUnsupported =
    "the va_arg instruction is not supported for MSIL on this triple; clang "
    "lowers C's va_arg itself";

/// Lowers VACOPY where va_list is the native structure (Design.md 4.7): a
/// copy of its bytes, which MSILSelectionDAGInfo makes a cpblk, as X86 and
/// AArch64 copy it. Where va_list is a char*, VACOPY is Expand.
static SDValue LowerVACOPY(SDValue Op, SelectionDAG &DAG) {
  unsigned Size = getNativeVAListStructSize(DAG.getTarget().getTargetTriple());
  assert(Size && "A pointer-sized va_list is copied by the generic expansion");
  SDValue Chain = Op.getOperand(0);
  SDValue DstPtr = Op.getOperand(1);
  SDValue SrcPtr = Op.getOperand(2);
  const Value *DstSV = cast<SrcValueSDNode>(Op.getOperand(3))->getValue();
  const Value *SrcSV = cast<SrcValueSDNode>(Op.getOperand(4))->getValue();
  SDLoc DL(Op);
  return DAG.getMemcpy(Chain, DL, DstPtr, SrcPtr,
                       DAG.getIntPtrConstant(Size, DL), Align(8), Align(8),
                       /*isVol=*/false, /*AlwaysInline=*/false, /*CI=*/nullptr,
                       std::nullopt, MachinePointerInfo(DstSV),
                       MachinePointerInfo(SrcSV));
}

SDValue MSILTargetLowering::LowerOperation(SDValue Op,
                                           SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::FrameIndex:
    return LowerFrameIndex(Op, DAG);
  case ISD::CopyToReg:
    return LowerCopyToReg(Op, DAG);
  // A dynamic alloca on no CFG cycle is a localloc of its own (plan 5.15.8
  // H0). The stack saves and restores, which MSILLowerDynamicAlloca deletes,
  // and the frame and stack addresses wait for the shadow stack (decision
  // D13 as amended); each has its own text.
  case ISD::DYNAMIC_STACKALLOC:
    return LowerDYNAMIC_STACKALLOC(Op, DAG);
  case ISD::STACKSAVE:
  case ISD::STACKRESTORE:
    return LowerStackSaveRestore(Op, DAG);
  case ISD::FRAMEADDR:
    return LowerUnsupported(Op, DAG,
                            "llvm.frameaddress is not supported for MSIL yet");
  case ISD::STACKADDRESS:
    return LowerUnsupported(Op, DAG,
                            "llvm.stackaddress is not supported for MSIL yet");
  case ISD::EH_DWARF_CFA:
    return LowerUnsupported(Op, DAG,
                            "llvm.eh.dwarf.cfa is not supported for MSIL");
  case ISD::RETURNADDR:
    return LowerUnsupported(Op, DAG,
                            "llvm.returnaddress is not supported for MSIL");
  case ISD::ADDROFRETURNADDR:
    return LowerUnsupported(
        Op, DAG, "llvm.addressofreturnaddress is not supported for MSIL");
  case ISD::SPONENTRY:
    return LowerUnsupported(Op, DAG,
                            "llvm.sponentry is not supported for MSIL");
  case ISD::INIT_TRAMPOLINE:
  case ISD::ADJUST_TRAMPOLINE:
    return LowerUnsupported(Op, DAG, "trampolines are not supported for MSIL");
  // setjmp/longjmp wait for phase 3; CIL cannot unwind to an address.
  case ISD::EH_SJLJ_SETJMP:
    return LowerUnsupported(
        Op, DAG,
        "llvm.eh.sjlj.setjmp is not supported for MSIL yet (phase 3 "
        "setjmp/longjmp)");
  case ISD::EH_SJLJ_LONGJMP:
    return LowerUnsupported(
        Op, DAG,
        "llvm.eh.sjlj.longjmp is not supported for MSIL yet (phase 3 "
        "setjmp/longjmp)");
  case ISD::EH_SJLJ_SETUP_DISPATCH:
    return LowerUnsupported(
        Op, DAG, "llvm.eh.sjlj.setup.dispatch is not supported for MSIL");
  case ISD::EH_RETURN:
    return LowerUnsupported(Op, DAG,
                            "llvm.eh.return is not supported for MSIL");
  // va_copy of a native va_list structure. A va_arg instruction never gets
  // here after MSILPrepareIR and ExpandVariadics: clang lowers va_arg inline,
  // so only hand-written IR has one.
  case ISD::VACOPY:
    return LowerVACOPY(Op, DAG);
  case ISD::VAARG:
    return LowerUnsupported(Op, DAG, VAArgUnsupported);
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::ExternalSymbol:
    return LowerExternalSymbol(Op, DAG);
  case ISD::GlobalTLSAddress: {
    // The same text as MSILPrepareIR and the AsmPrinter, with the IR spelling
    // of the global, which also names an unnamed or '\1'-escaped one.
    std::string Msg =
        "thread-local variable " +
        MSIL::getIRName(*cast<GlobalAddressSDNode>(Op)->getGlobal()) +
        " is not supported for MSIL yet";
    return LowerUnsupported(Op, DAG, Msg);
  }
  case ISD::BlockAddress:
    return LowerUnsupported(
        Op, DAG,
        "label address (blockaddress) is not supported for MSIL unless the "
        "label is a target of an indirectbr (computed goto)");
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
  case ISD::FP_TO_BF16:
    return LowerBF16Conversion(Op, DAG);
  case ISD::ATOMIC_FENCE:
    return LowerATOMIC_FENCE(Op, DAG);
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

  // A va_arg instruction of an f16, bf16, wider integer or vector type: the
  // error of LowerOperation, an undefined value and the input chain.
  if (N->getOpcode() == ISD::VAARG) {
    DAG.getContext()->diagnose(
        DiagnosticInfoUnsupported(DAG.getMachineFunction().getFunction(),
                                  VAArgUnsupported, SDLoc(N).getDebugLoc()));
    Results.push_back(DAG.getUNDEF(N->getValueType(0)));
    Results.push_back(N->getOperand(0));
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
  // nodes with an illegal result type): report the runtime helper that the
  // type legalizer would call, which no helper archive has (plan 5.14.3 A5:
  // MSIL expands the 128-bit operations itself), in every mode, with the text
  // that text output also gives a call of such a helper
  // (MSIL::getInt128HelperText), and go on with an undefined result.
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
      MSIL::getInt128HelperText(Name ? Name : "?"), SDLoc(N).getDebugLoc()));
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
  int FI = cast<FrameIndexSDNode>(Op)->getIndex();
  return DAG.getTargetFrameIndex(FI, Op.getValueType());
}

SDValue MSILTargetLowering::LowerCopyToReg(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDValue Src = Op.getOperand(2);
  if (isa<FrameIndexSDNode>(Src.getNode())) {
    // CopyToReg nodes don't support FrameIndex operands. Other targets select
    // the FI to some LEA-like instruction, but since we don't have that, we
    // need to insert some kind of instruction that can take an FI operand and
    // produces a value usable by CopyToReg (i.e. in a vreg). So insert a dummy
    // copy between Op and its FI operand.
    SDValue Chain = Op.getOperand(0);
    SDLoc DL(Op);
    Register Reg = cast<RegisterSDNode>(Op.getOperand(1))->getReg();
    EVT VT = Src.getValueType();
    assert(VT == MVT::i64 && "Frame indices are native ints");
    SDValue Copy(DAG.getMachineNode(MSIL::COPY_I64, DL, VT, Src), 0);
    return Op.getNode()->getNumValues() == 1
               ? DAG.getCopyToReg(Chain, DL, Reg, Copy)
               : DAG.getCopyToReg(Chain, DL, Reg, Copy,
                                  Op.getNumOperands() == 4 ? Op.getOperand(3)
                                                           : SDValue());
  }
  // Any other copy is legal as it is.
  return Op;
}

SDValue MSILTargetLowering::LowerGlobalAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc DL(Op);
  const auto *GA = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = GA->getGlobal();
  EVT VT = Op.getValueType();
  assert(GA->getTargetFlags() == 0 && "MSIL doesn't set target flags");

  // In object mode (MSILTargetMachine::isObjectMode(), plan 5.8.10) an alias
  // is a symbol of the object, which MSILPrepareIR has checked: the address
  // of an alias of a function, like a function's, is its FnAddr placeholder
  // (FUNC_ADDR), and that of an alias of a variable is its placeholder field
  // plus the offset (DATA_ADDR). Whole-program mode has no aliases left.
  const auto *Alias = dyn_cast<GlobalAlias>(GV);
  bool IsFunctionAlias = Alias && Alias->getValueType()->isFunctionTy();

  // A function used as a value: its address field, which <Module>::.cctor
  // sets with 'ldftn' of its method, or of its P/Invoke declaration when it
  // is not defined in this module. The field takes no offset
  // (isOffsetFoldingLegal), so one that SelectionDAG built anyway is added.
  if (isa<Function>(GV) || IsFunctionAlias) {
    SDValue Addr = DAG.getNode(MSILISD::FUNC_ADDR, DL, VT,
                               DAG.getTargetGlobalAddress(GV, DL, VT));
    if (int64_t Offset = GA->getOffset())
      Addr =
          DAG.getNode(ISD::ADD, DL, VT, Addr, DAG.getConstant(Offset, DL, VT));
    return Addr;
  }

  // MSILPrepareIR reports and erases every ifunc, and every alias in
  // whole-program mode; this is a backstop.
  if (!isa<GlobalVariable>(GV) && !Alias) {
    std::string Msg = (Twine("the address of '") + GV->getName() +
                       "' is not supported for MSIL")
                          .str();
    return LowerUnsupported(Op, DAG, Msg);
  }

  // A global variable lives in the data segment, and its address is in its
  // initonly address field (decision D12): 'ldsfld', plus the offset into
  // the global, which DAGCombiner folds into the operand
  // (isOffsetFoldingLegal).
  return DAG.getNode(MSILISD::DATA_ADDR, DL, VT,
                     DAG.getTargetGlobalAddress(GV, DL, VT, GA->getOffset()));
}

SDValue MSILTargetLowering::LowerExternalSymbol(SDValue Op,
                                                SelectionDAG &DAG) const {
  // The address of a library function. SelectionDAG names library functions
  // only as callees, which LowerCall turns into TargetExternalSymbols itself,
  // applying the library-call policy, so no external symbol is expected
  // here; MSILMCInstLower reports one that reaches FUNC_ADDR.
  const auto *ES = cast<ExternalSymbolSDNode>(Op);
  EVT VT = Op.getValueType();
  assert(ES->getTargetFlags() == 0 && "MSIL doesn't set target flags");
  return DAG.getNode(MSILISD::EXT_SYM, SDLoc(Op), VT,
                     DAG.getTargetExternalSymbol(ES->getSymbol(), VT));
}

SDValue MSILTargetLowering::LowerBR_JT(SDValue Op, SelectionDAG &DAG) const {
  // A jump table becomes a CIL 'switch' with its targets inline: BR_TABLE
  // (chain, index, entry MBBs..., first entry MBB again), printed as
  // 'switch (...)' and 'br <first entry>'. That last target is a dummy, not
  // the switch's default (see below). No table is ever emitted as data.
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  const auto *JT = cast<JumpTableSDNode>(Op.getOperand(1));
  SDValue Index = Op.getOperand(2);
  assert(JT->getTargetFlags() == 0 && "MSIL doesn't set target flags");
  // 'switch' on a native int fails ILVerify, so the index is always an int32
  // (decision D35): getJumpTableRegTy is i32, and SelectionDAGBuilder
  // truncates the index to it only after the range check on the full-width
  // value.
  assert(Index.getValueType() == MVT::i32 && "Jump-table index is not i32");

  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Index);

  MachineJumpTableInfo *MJTI = DAG.getMachineFunction().getJumpTableInfo();
  const auto &MBBs = MJTI->getJumpTables()[JT->getIndex()].MBBs;
  // isSuitableForJumpTable caps a table at MaxJumpTableEntries entries.
  assert(MBBs.size() + 3 <= SDNode::getMaxNumOperands() &&
         "Jump table too large for one BR_TABLE node");

  // Add an operand for each case.
  for (MachineBasicBlock *MBB : MBBs)
    Ops.push_back(DAG.getBasicBlock(MBB));

  // 'switch' falls through when the index is out of range, so it needs a
  // default target. SelectionDAGBuilder's range check before the jump table
  // already sends every out-of-range value to the switch's real default, so
  // the first case serves: it is a successor of this block anyway. Without the
  // range check (an unreachable default), an out-of-range index is undefined
  // behaviour. Unlike WebAssembly (WebAssemblyFixBrTableDefaults), the range
  // check is never removed.
  Ops.push_back(DAG.getBasicBlock(MBBs.front()));
  return DAG.getNode(MSILISD::BR_TABLE, DL, MVT::Other, Ops);
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
  // soft-promotes it. A bf16 value that another block computed arrives in a
  // float32 virtual register (TargetLoweringBase's register type of bf16;
  // calls pass bf16 as its bits instead) and is an exact FP_ROUND of it,
  // which DAGCombiner folds into a plain fp_extend; in a strictfp function it
  // is a STRICT_FP_ROUND, which becomes plain (ReplaceNodeResults) only after
  // DAGCombiner ran. Fold it here, as LowerStrictFP does for
  // STRICT_FP_EXTEND, so that the extension compiles as in a function without
  // strictfp. Anything else is soft-promoted.
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
  // sin and cos are Phase-1 imports, and sincos, which returns its results
  // through memory, is not (decision D14). FSIN and FCOS are LibCall, so the
  // legalizer does not merge them again.
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue Src = Op.getOperand(0);
  return DAG.getMergeValues(
      {DAG.getNode(ISD::FSIN, DL, VT, Src, Op->getFlags()),
       DAG.getNode(ISD::FCOS, DL, VT, Src, Op->getFlags())},
      DL);
}

SDValue MSILTargetLowering::LowerBF16Conversion(SDValue Op,
                                                SelectionDAG &DAG) const {
  // bf16 values are held as integers. The generic expansion of FP_TO_BF16
  // drops the low 16 bits of the f32 value (after rounding an f64 to f32
  // first) instead of rounding once to nearest; like the f16 conversions,
  // call the compiler-rt helper (__truncsfbf2, __truncdfbf2) instead, which
  // LowerCall reports in text output (plan 5.14.3 A9).
  assert(Op.getOpcode() == ISD::FP_TO_BF16 && "BF16_TO_FP is expanded");
  SDLoc DL(Op);
  SDValue Src = Op.getOperand(0);
  EVT SrcVT = Src.getValueType();
  EVT VT = Op.getValueType();
  RTLIB::Libcall LC = RTLIB::getFPROUND(SrcVT, MVT::bf16);
  if (LC == RTLIB::UNKNOWN_LIBCALL ||
      getLibcallImpl(LC) == RTLIB::Unsupported) {
    DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
        DAG.getMachineFunction().getFunction(),
        "bf16 conversions are not supported for MSIL", DL.getDebugLoc()));
    return DAG.getUNDEF(VT);
  }
  SDValue Res = makeLibCall(DAG, LC, VT, Src, MakeLibCallOptions(), DL).first;
  // Native x86-64 code rounds f32 -> bf16 inline (expandFP_ROUND), quieting
  // every NaN. __truncsfbf2 rounds every other value alike and quiets a NaN
  // too, except a signaling NaN whose low 16 bits are zero, which it copies
  // (7fa00000 gives 7fa0, natively 7fe0). Setting bf16's quiet bit (bit 6)
  // when the operand is a NaN, the only value unequal to itself, makes the
  // results equal on every float. __truncdfbf2, which native code calls as
  // well, quiets every NaN. In text output LowerCall reports the call and
  // returns UNDEF, which needs no quiet bit.
  if (SrcVT != MVT::f32 || Res.isUndef())
    return Res;
  SDValue IsNaN = DAG.getSetCC(
      DL, getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), SrcVT),
      Src, Src, ISD::SETUNE);
  SDValue QuietBit =
      DAG.getNode(ISD::SHL, DL, VT, DAG.getZExtOrTrunc(IsNaN, DL, VT),
                  DAG.getShiftAmountConstant(6, VT, DL));
  return DAG.getNode(ISD::OR, DL, VT, Res, QuietBit);
}

SDValue MSILTargetLowering::LowerATOMIC_FENCE(SDValue Op,
                                              SelectionDAG &DAG) const {
  // ATOMIC_FENCE (chain, ordering, scope). Every fence that is not
  // single-threaded is a full memory barrier: the MEMORY_BARRIER pattern
  // selects it (Interlocked::MemoryBarrier).
  if (Op.getConstantOperandVal(2) != SyncScope::SingleThread)
    return Op;
  // A single-thread fence (clang's translation of 'asm volatile("" :::
  // "memory")' and of __atomic_signal_fence, and the fences of MSILPrepareIR's
  // lowerEmptyInlineAsm) orders only the compiler's own code motion, but the
  // JIT compiles the method again and would move memory accesses across a
  // fence that leaves nothing in the IL (it hoists a plain load out of a spin
  // loop). It calls __msil_compiler_barrier instead, the trailer definition
  // (llvm/CLI/TrailerDefs.def) 'void()' that is a 'noinlining' method whose
  // body is 'ret': no JIT moves a memory access across a call that it does
  // not inline. The call is a library call on the fence's chain; LowerCall
  // binds it to the method that whole-program text defines, or in object
  // mode to the undefined symbol that msil-ld's trailer defines.
  const cli::TrailerDefInfo &Barrier =
      cli::getTrailerDefs()[static_cast<unsigned>(
          cli::TrailerDefId::CompilerBarrier)];
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(SDLoc(Op))
      .setChain(Op.getOperand(0))
      .setLibCallee(CallingConv::C, Type::getVoidTy(*DAG.getContext()),
                    DAG.getExternalSymbol(Barrier.Name.data(),
                                          getPointerTy(DAG.getDataLayout())),
                    ArgListTy());
  return LowerCallTo(CLI).second;
}

/// Returns well-formed dummy results for \p Op, an operation that is not
/// lowered: a null pointer or zero for each integer value, undef for any other
/// value and the input chain, which is operand 0 of every node with a chain
/// result.
static SDValue getDummyResults(SDValue Op, SelectionDAG &DAG) {
  SDLoc DL(Op);
  SmallVector<SDValue, 2> Results;
  for (EVT VT : Op->values()) {
    if (VT == MVT::Other)
      Results.push_back(Op.getOperand(0));
    else if (VT.isInteger())
      Results.push_back(DAG.getConstant(0, DL, VT));
    else
      Results.push_back(DAG.getUNDEF(VT));
  }
  return DAG.getMergeValues(Results, DL);
}

SDValue MSILTargetLowering::LowerUnsupported(SDValue Op, SelectionDAG &DAG,
                                             StringRef What) const {
  DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
      DAG.getMachineFunction().getFunction(), What, SDLoc(Op).getDebugLoc()));
  return getDummyResults(Op, DAG);
}

SDValue MSILTargetLowering::LowerStackSaveRestore(SDValue Op,
                                                  SelectionDAG &DAG) const {
  // MSILLowerDynamicAlloca deletes every llvm.stacksave and llvm.stackrestore
  // of a function without a cyclic dynamic alloca, and drops the body of a
  // function with one, so these texts are backstops for a pipeline without
  // that pass. They report even next to an acyclic dynamic alloca: without
  // the pass nothing has checked that the alloca is on no cycle.
  return LowerUnsupported(Op, DAG,
                          Op.getOpcode() == ISD::STACKSAVE
                              ? "llvm.stacksave is not supported for MSIL yet"
                              : "llvm.stackrestore is not supported for MSIL "
                                "yet");
}

/// Returns the alignment that the dynamic allocas of \p MF whose alignment
/// does not exceed the stack alignment need. SelectionDAGBuilder::visitAlloca
/// gives their DYNAMIC_STACKALLOC an alignment operand of 0 ('the stack
/// alignment', 16 for MSIL), so the node does not say whether 'localloc''s 8
/// bytes suffice. FunctionLoweringInfo::set records every dynamic alloca as a
/// variable-sized object of the frame, so the largest IR alignment among
/// them that is at most the stack alignment decides: a function whose
/// dynamic allocas need at most 8 gets no realignment, one with a 16-aligned
/// one realigns all of them to 16.
static Align getDefaultDynamicAllocaAlign(const MachineFunction &MF) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const Align StackAlign =
      MF.getSubtarget<MSILSubtarget>().getFrameLowering()->getStackAlign();
  Align Result(1);
  for (int FI = 0, E = MFI.getObjectIndexEnd(); FI != E; ++FI) {
    if (!MFI.isVariableSizedObjectIndex(FI))
      continue;
    const AllocaInst *AI = MFI.getObjectAllocation(FI);
    // A variable-sized object without its alloca: assume the stack alignment.
    if (!AI)
      return StackAlign;
    if (AI->getAlign() <= StackAlign)
      Result = std::max(Result, AI->getAlign());
  }
  return Result;
}

SDValue MSILTargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op,
                                                    SelectionDAG &DAG) const {
  // An alloca on no CFG cycle runs at most once per call (the others are
  // MSILLowerDynamicAlloca's error), so its memory can live until the method
  // returns, like the static frame's: a localloc of its own, with a
  // native-int size (LOCALLOC_I64; CIL 'localloc' takes a native unsigned
  // int, plan 5.15.8 H0). CIL requires the size to be the only value on the
  // evaluation stack: MSILRegStackify never nests the result into another
  // instruction, and MSILStackCheck checks the depth.
  MachineFunction &MF = DAG.getMachineFunction();
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue Chain = Op.getOperand(0);
  SDValue Size = Op.getOperand(1);

  // SelectionDAGBuilder has rounded the size up to the stack alignment, as
  // (Size + StackAlign - 1) & -StackAlign. The rounding stays: DAGCombiner
  // merges it with a program's own rounding of the same shape
  // (alloca((n + 15) & ~15)), so the AND cannot tell whose it is, and dropping
  // it could allocate fewer bytes than the alloca asks for (decision D13 lets
  // nothing rely on localloc's rounding). It costs at most StackAlign - 1
  // bytes.
  MaybeAlign Requested =
      cast<ConstantSDNode>(Op.getOperand(2))->getMaybeAlignValue();
  const Align Alignment =
      Requested ? *Requested : getDefaultDynamicAllocaAlign(MF);

  // As for the static frame (MSILFrameLowering::emitFrameBase): when the
  // alloca needs more than localloc's alignment, allocate Alignment -
  // LocallocAlign more bytes and align the address up, in native int
  // (decision D2).
  const Align LocallocAlign =
      Subtarget->getFrameLowering()->getLocallocAlign(MF);
  const bool Realign = Alignment > LocallocAlign;
  if (Realign)
    Size = DAG.getNode(
        ISD::ADD, DL, VT, Size,
        DAG.getConstant(Alignment.value() - LocallocAlign.value(), DL, VT));
  else if (!DAG.isKnownNeverZero(Size))
    // CIL 'localloc' of 0 bytes may return null (CoreCLR's does), but a
    // zero-size alloca must still yield a valid address: allocate at least
    // one byte, as the static frame does (MSILFrameLowering::emitFrameBase).
    // Size | 1 is never below Size and costs at most one byte. The realigned
    // size above is never zero, even modulo 2^64: the rounded size is a
    // multiple of the stack alignment (16) and Alignment - LocallocAlign
    // (Alignment - 8, Alignment >= 16) is not.
    Size = DAG.getNode(ISD::OR, DL, VT, Size, DAG.getConstant(1, DL, VT));
  MachineSDNode *Alloc = DAG.getMachineNode(
      MSIL::LOCALLOC_I64, DL, DAG.getVTList(VT, MVT::Other), {Size, Chain});
  SDValue Ptr(Alloc, 0);
  if (Realign) {
    Ptr = DAG.getNode(ISD::ADD, DL, VT, Ptr,
                      DAG.getConstant(Alignment.value() - 1, DL, VT));
    Ptr = DAG.getNode(ISD::AND, DL, VT, Ptr,
                      DAG.getSignedConstant(
                          -static_cast<int64_t>(Alignment.value()), DL, VT));
  }
  return DAG.getMergeValues({Ptr, SDValue(Alloc, 1)}, DL);
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
  // into FalseMBB, the shape of C#'s '?:'. The successor lists are built here
  // explicitly: nothing relies on branch analysis.
  const TargetInstrInfo &TII = *Subtarget->getInstrInfo();
  const DebugLoc &DL = MI.getDebugLoc();
  MachineFunction *MF = BB->getParent();
  // The address of a stack object is a frame index operand until prologue
  // and epilogue insertion, but a PHI needs a register: copy it into one.
  auto GetReg = [&](const MachineOperand &MO) -> Register {
    if (!MO.isFI())
      return MO.getReg();
    Register Reg = MF->getRegInfo().createVirtualRegister(&MSIL::I64RegClass);
    BuildMI(*BB, MI, DL, TII.get(MSIL::COPY_I64), Reg)
        .addFrameIndex(MO.getIndex());
    return Reg;
  };
  Register Dst = MI.getOperand(0).getReg();
  Register Cond = MI.getOperand(1).getReg();
  Register TrueVal = GetReg(MI.getOperand(2));
  Register FalseVal = GetReg(MI.getOperand(3));

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
