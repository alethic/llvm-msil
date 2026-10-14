//===- MSILInstrFloat.td - MSIL floating-point codegen -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyInstrFloat.td at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSIL floating-point arithmetic and compare code-gen constructs on F32 and
/// F64 (CIL float32 and float64). Every record is one CIL opcode that pops
/// its register uses in operand order and pushes its def. None of them traps:
/// CIL floating-point arithmetic follows IEEE 754 and never throws.
///
/// sqrt, fabs, copysign, the rounding operations, fma and min/max have no CIL
/// opcode. Their actions in MSILISelLowering.cpp make them library calls or
/// generic expansions (decision D16), so they have no records here.
///
//===----------------------------------------------------------------------===//

multiclass UnaryFP<SDPatternOperator node, string name, bits<16> cilop> {
  defm _F32 : I<(outs F32:$dst), (ins F32:$src),
                [(set F32:$dst, (node F32:$src))], name, cilop>;
  defm _F64 : I<(outs F64:$dst), (ins F64:$src),
                [(set F64:$dst, (node F64:$src))], name, cilop>;
}
multiclass BinaryFP<SDPatternOperator node, string name, bits<16> cilop> {
  defm _F32 : I<(outs F32:$dst), (ins F32:$lhs, F32:$rhs),
                [(set F32:$dst, (node F32:$lhs, F32:$rhs))], name, cilop>;
  defm _F64 : I<(outs F64:$dst), (ins F64:$lhs, F64:$rhs),
                [(set F64:$dst, (node F64:$lhs, F64:$rhs))], name, cilop>;
}
// Compares push an int32 0 or 1. The records carry no pattern: the setcc
// patterns below choose them, and build the other predicates from two of
// them.
multiclass ComparisonFP<string name, bits<16> cilop> {
  defm _F32 : I<(outs I32:$dst), (ins F32:$lhs, F32:$rhs), [], name, cilop>;
  defm _F64 : I<(outs I32:$dst), (ins F64:$lhs, F64:$rhs), [], name, cilop>;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
let isCommutable = 1 in
defm ADD : BinaryFP<fadd, "add", 0x58>;
defm SUB : BinaryFP<fsub, "sub", 0x59>;
let isCommutable = 1 in
defm MUL : BinaryFP<fmul, "mul", 0x5A>;
defm DIV : BinaryFP<fdiv, "div", 0x5B>;
// CIL 'rem' on floating-point values is C's fmod: the result has the sign of
// the dividend and is exact (isel.md 3.2, cil-runtime e22), so FREM needs no
// fmod libcall.
defm REM : BinaryFP<frem, "rem", 0x5D>;

// 'neg' flips the sign bit, so -0.0 and the sign of a NaN come out as fneg
// requires.
defm NEG : UnaryFP<fneg, "neg", 0x65>;

// ceq, cgt and clt are false when either operand is a NaN (ordered);
// cgt.un and clt.un are true then (unordered).
let isCommutable = 1 in
defm CEQ : ComparisonFP<"ceq", 0xFE01>;
defm CGT : ComparisonFP<"cgt", 0xFE02>;
defm CGT_UN : ComparisonFP<"cgt.un", 0xFE03>;
defm CLT : ComparisonFP<"clt", 0xFE04>;
defm CLT_UN : ComparisonFP<"clt.un", 0xFE05>;
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

// Comparisons as values (isel.md 5.2). Five predicates are single opcodes;
// the others are the negation of one of them with the opposite ordering,
// built as 'CEQ_I32 (<compare>), (CONST_I32 0)' so that every MI is one
// opcode: une is '!oeq', ole is '!ugt', ule is '!ogt', oge is '!ult' and uge
// is '!olt'. SETO, SETUO, SETONE and SETUEQ are Expand (MSILISelLowering.cpp):
// the legalizer builds them from oeq, une, ogt and olt. The "don't care"
// predicates, which LLVM uses when no operand can be a NaN, take the ordered
// forms (une for ne).
multiclass ComparisonFPPats<MSILRegClass rc> {
  defvar CEQ = !cast<Instruction>("CEQ_"#rc);
  defvar CGT = !cast<Instruction>("CGT_"#rc);
  defvar CGT_UN = !cast<Instruction>("CGT_UN_"#rc);
  defvar CLT = !cast<Instruction>("CLT_"#rc);
  defvar CLT_UN = !cast<Instruction>("CLT_UN_"#rc);
  foreach cc = [SETOEQ, SETEQ] in
  def : Pat<(setcc rc:$lhs, rc:$rhs, cc), (CEQ rc:$lhs, rc:$rhs)>;
  foreach cc = [SETOGT, SETGT] in
  def : Pat<(setcc rc:$lhs, rc:$rhs, cc), (CGT rc:$lhs, rc:$rhs)>;
  def : Pat<(setcc rc:$lhs, rc:$rhs, SETUGT), (CGT_UN rc:$lhs, rc:$rhs)>;
  foreach cc = [SETOLT, SETLT] in
  def : Pat<(setcc rc:$lhs, rc:$rhs, cc), (CLT rc:$lhs, rc:$rhs)>;
  def : Pat<(setcc rc:$lhs, rc:$rhs, SETULT), (CLT_UN rc:$lhs, rc:$rhs)>;
  foreach cc = [SETUNE, SETNE] in
  def : Pat<(setcc rc:$lhs, rc:$rhs, cc),
            (CEQ_I32 (CEQ rc:$lhs, rc:$rhs), (CONST_I32 0))>;
  foreach cc = [SETOLE, SETLE] in
  def : Pat<(setcc rc:$lhs, rc:$rhs, cc),
            (CEQ_I32 (CGT_UN rc:$lhs, rc:$rhs), (CONST_I32 0))>;
  def : Pat<(setcc rc:$lhs, rc:$rhs, SETULE),
            (CEQ_I32 (CGT rc:$lhs, rc:$rhs), (CONST_I32 0))>;
  foreach cc = [SETOGE, SETGE] in
  def : Pat<(setcc rc:$lhs, rc:$rhs, cc),
            (CEQ_I32 (CLT_UN rc:$lhs, rc:$rhs), (CONST_I32 0))>;
  def : Pat<(setcc rc:$lhs, rc:$rhs, SETUGE),
            (CEQ_I32 (CLT rc:$lhs, rc:$rhs), (CONST_I32 0))>;
}
defm : ComparisonFPPats<F32>;
defm : ComparisonFPPats<F64>;
