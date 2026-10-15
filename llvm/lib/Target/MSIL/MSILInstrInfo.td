//===- MSILInstrInfo.td - Describe the MSIL instructions ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyInstrInfo.td at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSIL Instruction definitions: target nodes, operands, and the core records
/// (arguments, copies, locals, constants and addresses). The per-area files
/// included at the end hold the rest.
///
/// Record names follow WebAssembly where the meaning is the same, so the
/// passes derived from WebAssembly keep their opcode names. Asm strings name
/// only immediate and symbol operands. Records that print as several CIL
/// opcodes separate them with a newline and the two-space indent, and set
/// StackPeakExtra (see MSILInstrFormats.td).
///
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MSIL-specific DAG Node Types.
//===----------------------------------------------------------------------===//

def SDT_MSILArgument : SDTypeProfile<1, 1, [SDTCisVT<1, i32>]>;
def SDT_MSILReturn   : SDTypeProfile<0, -1, []>;
def SDT_MSILAddress  : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                            SDTCisPtrTy<0>]>;

//===----------------------------------------------------------------------===//
// MSIL-specific DAG Nodes.
//===----------------------------------------------------------------------===//

def MSILargument : SDNode<"MSILISD::ARGUMENT", SDT_MSILArgument>;
def MSILreturn   : SDNode<"MSILISD::RETURN", SDT_MSILReturn,
                          [SDNPHasChain, SDNPVariadic]>;

// Addresses (MSILTargetLowering::LowerGlobalAddress and LowerExternalSymbol):
// the address of a global variable, a TargetGlobalAddress with the offset
// folded in; the address of a function used as a value, a
// TargetGlobalAddress; and the address of a library function, a
// TargetExternalSymbol.
def MSILdata_addr : SDNode<"MSILISD::DATA_ADDR", SDT_MSILAddress>;
def MSILfunc_addr : SDNode<"MSILISD::FUNC_ADDR", SDT_MSILAddress>;
def MSILext_sym   : SDNode<"MSILISD::EXT_SYM", SDT_MSILAddress>;

//===----------------------------------------------------------------------===//
// MSIL-specific Operands.
//===----------------------------------------------------------------------===//

// The PrintMethod names are the MSILInstPrinter methods the generated
// MSILGenAsmWriter.inc calls.
let OperandNamespace = "MSIL" in {

// Index of a CIL local (ldloc/stloc).
let OperandType = "OPERAND_LOCAL" in
def local_op : Operand<i32> {
  let PrintMethod = "printLocalOperand";
}

// Index of a CIL argument (ldarg/starg).
let OperandType = "OPERAND_ARG" in
def arg_op : Operand<i32> {
  let PrintMethod = "printArgOperand";
}

let OperandType = "OPERAND_I32IMM" in
def i32imm_op : Operand<i32>;

let OperandType = "OPERAND_I64IMM" in
def i64imm_op : Operand<i64>;

// Floating-point immediates print as the bit pattern, float32(0xXXXXXXXX).
let OperandType = "OPERAND_F32IMM" in
def f32imm_op : Operand<f32> {
  let PrintMethod = "printF32ImmOperand";
}

let OperandType = "OPERAND_F64IMM" in
def f64imm_op : Operand<f64> {
  let PrintMethod = "printF64ImmOperand";
}

// A method: a function symbol until MSILMCInstLower turns it into an
// MSILMemberRefExpr that prints the method reference.
let OperandType = "OPERAND_METHOD" in
def function_op : Operand<i64>;

// A global variable: its address field (decision D12).
let OperandType = "OPERAND_DATA" in
def data_op : Operand<i64>;

// Memory access flags (filled by MSILSetMemFlags): bits 0-2 hold the
// 'unaligned.' value N (0 = no prefix, 1, 2 or 4), bit 3 'volatile.'. The
// printer prints the prefix lines.
let OperandType = "OPERAND_MEMFLAGS" in
def memflags_op : Operand<i32> {
  let PrintMethod = "printMemFlagsOperand";
}

} // OperandNamespace = "MSIL"

// Basic block label in a branch construct.
def bb_op : Operand<OtherVT>;

//===----------------------------------------------------------------------===//
// MSIL Instruction Format Definitions.
//===----------------------------------------------------------------------===//

include "MSILInstrFormats.td"

//===----------------------------------------------------------------------===//
// Arguments.
//===----------------------------------------------------------------------===//

// Incoming arguments as virtual registers. They print nothing:
// MSILExplicitLocals rewrites their uses into ARG_GET_* (ldarg).
multiclass ARGUMENT<MSILRegClass rc, ValueType vt> {
  let hasSideEffects = 1, isCodeGenOnly = 1, Defs = []<Register>,
      Uses = [ARGUMENTS] in
  defm ARGUMENT_#rc : I<(outs rc:$res), (ins i32imm:$argno),
                        [(set (vt rc:$res), (MSILargument timm:$argno))]>;
}
defm "" : ARGUMENT<I32, i32>;
defm "" : ARGUMENT<I64, i64>;
defm "" : ARGUMENT<F32, f32>;
defm "" : ARGUMENT<F64, f64>;

//===----------------------------------------------------------------------===//
// Copies, locals and arguments.
//===----------------------------------------------------------------------===//

// LOCAL_* and ARG_* are not generated by instruction selection; they are
// implied by virtual register uses and defs until MSILExplicitLocals makes
// them explicit.
multiclass LOCAL<MSILRegClass rc> {
  let hasSideEffects = 0 in {
  // COPY is not a CIL instruction: since locals are implicit during most of
  // codegen, a COPY is a no-op whose work is done by the implied ldloc and
  // stloc. MSILExplicitLocals replaces COPYs with LOCAL_GET/LOCAL_SET.
  let isAsCheapAsAMove = 1, isCodeGenOnly = 1, mayLoad = 0, mayStore = 0 in {
  defm COPY_#rc : I<(outs rc:$res), (ins rc:$src), []>;

  // TEE is similar to COPY, but writes two copies of its result. Typically
  // this would be used to stackify one result and write the other result to
  // a local (MSILExplicitLocals turns it into LOCAL_TEE or ARG_TEE).
  defm TEE_#rc : I<(outs rc:$res, rc:$also), (ins rc:$src), []>;
  }

  // ldloc / ldarg. They have mayLoad because they read a CIL local or
  // argument, which is a side effect not otherwise modeled in LLVM.
  let mayLoad = 1, mayStore = 0, isAsCheapAsAMove = 1 in {
  defm LOCAL_GET_#rc : I<(outs rc:$res), (ins local_op:$local), [],
                         "ldloc $local", 0xFE0C>;
  defm ARG_GET_#rc : I<(outs rc:$res), (ins arg_op:$arg), [],
                       "ldarg $arg", 0xFE09>;
  }

  // stloc / starg, and the tees 'dup; stloc' / 'dup; starg'. They have
  // mayStore because they write a CIL local or argument.
  let mayLoad = 0, mayStore = 1, isAsCheapAsAMove = 1 in {
  defm LOCAL_SET_#rc : I<(outs), (ins local_op:$local, rc:$src), [],
                         "stloc $local", 0xFE0E>;
  defm ARG_SET_#rc : I<(outs), (ins arg_op:$arg, rc:$src), [],
                       "starg $arg", 0xFE0B>;
  let StackPeakExtra = 1 in {
  defm LOCAL_TEE_#rc : I<(outs rc:$res), (ins local_op:$local, rc:$src), [],
                         "dup\n  stloc $local">;
  defm ARG_TEE_#rc : I<(outs rc:$res), (ins arg_op:$arg, rc:$src), [],
                       "dup\n  starg $arg">;
  }
  }

  // Unused values must be dropped.
  let mayLoad = 0, mayStore = 0 in
  defm DROP_#rc : I<(outs), (ins rc:$src), [], "pop", 0x26>;
  } // hasSideEffects = 0
}
defm "" : LOCAL<I32>;
defm "" : LOCAL<I64>;
defm "" : LOCAL<F32>;
defm "" : LOCAL<F64>;

//===----------------------------------------------------------------------===//
// Constants.
//===----------------------------------------------------------------------===//

// CONST_I64 is native int (D2): 'ldc.i8 N; conv.i' (the printer may choose
// 'ldc.i4 N; conv.i' for small values). FP constants are bit-exact.
let isMoveImm = 1, isAsCheapAsAMove = 1, isReMaterializable = 1 in {
defm CONST_I32 : I<(outs I32:$res), (ins i32imm_op:$imm),
                   [(set I32:$res, imm:$imm)],
                   "ldc.i4 $imm", 0x20>;
defm CONST_I64 : I<(outs I64:$res), (ins i64imm_op:$imm),
                   [(set I64:$res, imm:$imm)],
                   "ldc.i8 $imm\n  conv.i">;
defm CONST_F32 : I<(outs F32:$res), (ins f32imm_op:$imm),
                   [(set F32:$res, fpimm:$imm)],
                   "ldc.r4 $imm", 0x22>;
defm CONST_F64 : I<(outs F64:$res), (ins f64imm_op:$imm),
                   [(set F64:$res, fpimm:$imm)],
                   "ldc.r8 $imm", 0x23>;
} // isMoveImm = 1, isAsCheapAsAMove = 1, isReMaterializable = 1

//===----------------------------------------------------------------------===//
// Addresses.
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isAsCheapAsAMove = 1,
    isReMaterializable = 1 in {
// Address of a global variable: 'ldsfld native int <address field>', plus
// 'ldc.i4 <offset>; add' when the operand has an offset (printed by
// MSILInstPrinter). The address field is initonly and set before any code
// runs, so this does not count as a load.
let StackPeakExtra = 1 in
defm GLOBAL_ADDR : I<(outs I64:$res), (ins data_op:$addr), [],
                     "ldsfld $addr">;
// Address of a function: 'ldftn <method reference>'.
defm FUNC_ADDR : I<(outs I64:$res), (ins function_op:$func), [],
                   "ldftn $func", 0xFE06>;
}

// Global variables live in the data segment, and code reaches each one
// through its address field (decision D12). The offset of the operand is
// the offset into the global.
def : Pat<(i64 (MSILdata_addr tglobaladdr:$addr)),
          (GLOBAL_ADDR tglobaladdr:$addr)>;
// A function used as a value; an undefined one is its P/Invoke declaration.
def : Pat<(i64 (MSILfunc_addr tglobaladdr:$func)),
          (FUNC_ADDR tglobaladdr:$func)>;
// A library function used as a value. SelectionDAG names library functions
// only as callees, which LowerCall turns into target symbols itself, so this
// is a backstop.
def : Pat<(i64 (MSILext_sym texternalsym:$func)),
          (FUNC_ADDR texternalsym:$func)>;

//===----------------------------------------------------------------------===//
// Additional sets of instructions.
//===----------------------------------------------------------------------===//

include "MSILInstrInteger.td"
include "MSILInstrFloat.td"
include "MSILInstrConv.td"
include "MSILInstrMemory.td"
include "MSILInstrControl.td"
include "MSILInstrCall.td"
include "MSILInstrFrame.td"
