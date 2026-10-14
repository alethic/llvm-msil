//===- MSILInstrInteger.td - MSIL integer codegen ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyInstrInteger.td at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSIL integer arithmetic, shift and compare code-gen constructs on I32 and
/// I64 (CIL int32 and native int). Every record is one CIL opcode that pops
/// its register uses in operand order and pushes its def.
///
//===----------------------------------------------------------------------===//

multiclass UnaryInt<SDPatternOperator node, string name, bits<16> cilop> {
  defm _I32 : I<(outs I32:$dst), (ins I32:$src),
                [(set I32:$dst, (node I32:$src))], name, cilop>;
  defm _I64 : I<(outs I64:$dst), (ins I64:$src),
                [(set I64:$dst, (node I64:$src))], name, cilop>;
}
multiclass BinaryInt<SDPatternOperator node, string name, bits<16> cilop> {
  defm _I32 : I<(outs I32:$dst), (ins I32:$lhs, I32:$rhs),
                [(set I32:$dst, (node I32:$lhs, I32:$rhs))], name, cilop>;
  defm _I64 : I<(outs I64:$dst), (ins I64:$lhs, I64:$rhs),
                [(set I64:$dst, (node I64:$lhs, I64:$rhs))], name, cilop>;
}
// The shift amount is an int32 for both widths (getScalarShiftAmountTy is
// i32), which CIL accepts for an int32 and a native int value alike.
multiclass ShiftInt<SDPatternOperator node, string name, bits<16> cilop> {
  defm _I32 : I<(outs I32:$dst), (ins I32:$lhs, I32:$amt),
                [(set I32:$dst, (node I32:$lhs, I32:$amt))], name, cilop>;
  defm _I64 : I<(outs I64:$dst), (ins I64:$lhs, I32:$amt),
                [(set I64:$dst, (node I64:$lhs, I32:$amt))], name, cilop>;
}
// Compares push an int32 0 or 1 for both operand widths. The records carry no
// pattern: the setcc patterns below choose them, and build the negated
// predicates from two of them.
multiclass ComparisonInt<string name, bits<16> cilop> {
  defm _I32 : I<(outs I32:$dst), (ins I32:$lhs, I32:$rhs), [], name, cilop>;
  defm _I64 : I<(outs I32:$dst), (ins I64:$lhs, I64:$rhs), [], name, cilop>;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
let isCommutable = 1 in
defm ADD : BinaryInt<add, "add", 0x58>;
defm SUB : BinaryInt<sub, "sub", 0x59>;
let isCommutable = 1 in
defm MUL : BinaryInt<mul, "mul", 0x5A>;
let isCommutable = 1 in {
defm AND : BinaryInt<and, "and", 0x5F>;
defm OR : BinaryInt<or, "or", 0x60>;
defm XOR : BinaryInt<xor, "xor", 0x61>;
} // isCommutable = 1

// The masks LLVM puts on shift amounts stay: CIL leaves a shift by the value
// width or more unspecified, so WebAssembly's patterns that drop them are not
// ported.
defm SHL : ShiftInt<shl, "shl", 0x62>;
defm SHR_S : ShiftInt<sra, "shr", 0x63>;
defm SHR_U : ShiftInt<srl, "shr.un", 0x64>;

defm NEG : UnaryInt<ineg, "neg", 0x65>;
defm NOT : UnaryInt<not, "not", 0x66>;

let isCommutable = 1 in
defm CEQ : ComparisonInt<"ceq", 0xFE01>;
defm CGT : ComparisonInt<"cgt", 0xFE02>;
defm CGT_UN : ComparisonInt<"cgt.un", 0xFE03>;
defm CLT : ComparisonInt<"clt", 0xFE04>;
defm CLT_UN : ComparisonInt<"clt.un", 0xFE05>;
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

// Divide and remainder throw on a zero divisor and on INT_MIN / -1, which is
// undefined behaviour in LLVM: nothing may speculate them, but RegStackify may
// still move them (TrapsOnlyOnUB).
let hasSideEffects = 1, mayLoad = 0, mayStore = 0, TrapsOnlyOnUB = 1 in {
defm DIV_S : BinaryInt<sdiv, "div", 0x5B>;
defm DIV_U : BinaryInt<udiv, "div.un", 0x5C>;
defm REM_S : BinaryInt<srem, "rem", 0x5D>;
defm REM_U : BinaryInt<urem, "rem.un", 0x5E>;
} // hasSideEffects = 1, mayLoad = 0, mayStore = 0, TrapsOnlyOnUB = 1

// Comparisons as values. CIL has ceq, cgt, cgt.un, clt and clt.un; each other
// predicate is the negation of one of them, built at ISel as
// 'CEQ_I32 (<compare>), (CONST_I32 0)' so that every MI is one opcode:
// 'ceq; ldc.i4.0; ceq' for ne, 'cgt; ldc.i4.0; ceq' for le, and so on.
multiclass ComparisonPats<MSILRegClass rc> {
  defvar CEQ = !cast<Instruction>("CEQ_"#rc);
  defvar CGT = !cast<Instruction>("CGT_"#rc);
  defvar CGT_UN = !cast<Instruction>("CGT_UN_"#rc);
  defvar CLT = !cast<Instruction>("CLT_"#rc);
  defvar CLT_UN = !cast<Instruction>("CLT_UN_"#rc);
  def : Pat<(setcc rc:$lhs, rc:$rhs, SETEQ), (CEQ rc:$lhs, rc:$rhs)>;
  def : Pat<(setcc rc:$lhs, rc:$rhs, SETGT), (CGT rc:$lhs, rc:$rhs)>;
  def : Pat<(setcc rc:$lhs, rc:$rhs, SETUGT), (CGT_UN rc:$lhs, rc:$rhs)>;
  def : Pat<(setcc rc:$lhs, rc:$rhs, SETLT), (CLT rc:$lhs, rc:$rhs)>;
  def : Pat<(setcc rc:$lhs, rc:$rhs, SETULT), (CLT_UN rc:$lhs, rc:$rhs)>;
  def : Pat<(setcc rc:$lhs, rc:$rhs, SETNE),
            (CEQ_I32 (CEQ rc:$lhs, rc:$rhs), (CONST_I32 0))>;
  def : Pat<(setcc rc:$lhs, rc:$rhs, SETLE),
            (CEQ_I32 (CGT rc:$lhs, rc:$rhs), (CONST_I32 0))>;
  def : Pat<(setcc rc:$lhs, rc:$rhs, SETULE),
            (CEQ_I32 (CGT_UN rc:$lhs, rc:$rhs), (CONST_I32 0))>;
  def : Pat<(setcc rc:$lhs, rc:$rhs, SETGE),
            (CEQ_I32 (CLT rc:$lhs, rc:$rhs), (CONST_I32 0))>;
  def : Pat<(setcc rc:$lhs, rc:$rhs, SETUGE),
            (CEQ_I32 (CLT_UN rc:$lhs, rc:$rhs), (CONST_I32 0))>;
}
defm : ComparisonPats<I32>;
defm : ComparisonPats<I64>;

// 'x != 0' is 'x >u 0': 'ldc.i4.0; cgt.un' instead of 'ldc.i4.0; ceq;
// ldc.i4.0; ceq'.
let AddedComplexity = 10 in {
def : Pat<(setcc I32:$lhs, 0, SETNE), (CGT_UN_I32 I32:$lhs, (CONST_I32 0))>;
def : Pat<(setcc I64:$lhs, 0, SETNE), (CGT_UN_I64 I64:$lhs, (CONST_I64 0))>;
} // AddedComplexity = 10
