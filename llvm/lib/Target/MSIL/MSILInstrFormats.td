//===- MSILInstrFormats.td - MSIL instruction formats ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derived from llvm/lib/Target/WebAssembly/WebAssemblyInstrFormats.td at
// d1e15b695381.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MSIL instruction format definitions.
///
/// Unlike WebAssembly there is one record per instruction, with no
/// register/stack twins: MachineInstrs keep their virtual-register operands
/// until the end of code generation. They never reach MC: MSILMCInstLower
/// expands each one into MC-level records (CILInst, MSILInstrCIL.td), one per
/// CIL instruction, whose only operand is the instruction's inline operand.
///
//===----------------------------------------------------------------------===//

// MSIL instruction format.
class MSILInst<bits<16> cilop, string asmstr> : Instruction {
  // CIL opcode: 0x00XX for one-byte opcodes, 0xFEXX for two-byte opcodes, and
  // 0xFFFF for pseudos and for records that stand for several opcodes.
  bits<16> Inst = cilop;

  // Extra evaluation-stack depth, above max(pops, pushes), reached by the
  // CIL_ instructions that MSILMCInstLower::lowerImpl expands this
  // MachineInstr-level record into: LOCAL_TEE, expanded to 'dup' and
  // 'stloc', is 1. Non-zero only for records whose expansion goes above
  // max(pops, pushes); CONST_I64 ('ldc' and 'conv.i') and BR_TABLE_I32
  // ('switch' and 'br') expand to two instructions and stay 0. MSILStackCheck
  // reads it, not the expansion, to compute .maxstack.
  bits<2> StackPeakExtra = 0;

  // Division and remainder: has side effects only because it traps on
  // undefined behaviour, so RegStackify may still move it.
  bit TrapsOnlyOnUB = 0;

  let Namespace = "MSIL";
  let Pattern = [];
  let AsmString = asmstr;

  // Keep in sync with MSILII in MCTargetDesc/MSILMCTargetDesc.h.
  let TSFlags{1-0} = StackPeakExtra;
  let TSFlags{2} = TrapsOnlyOnUB;
  // The CIL opcode, which MSILMCCodeEmitter encodes (MSILII::CILOpcodeMask).
  let TSFlags{18-3} = Inst;
}

// Normal instructions. Default instantiation of an MSILInst.
class NI<dag oops, dag iops, list<dag> pattern, string asmstr = "",
         bits<16> cilop = 0xFFFF>
    : MSILInst<cilop, asmstr> {
  dag OutOperandList = oops;
  dag InOperandList  = iops;
  let Pattern        = pattern;
  // Keeps the ARGUMENT_* pseudos, which use ARGUMENTS, at the function entry.
  let Defs           = [ARGUMENTS];
}

// Same name as WebAssembly's multiclass so the per-area files read alike, but
// it defines a single record.
multiclass I<dag oops, dag iops, list<dag> pattern, string asmstr = "",
             bits<16> cilop = 0xFFFF> {
  def "" : NI<oops, iops, pattern, asmstr, cilop>;
}

// One CIL instruction at the MC level (MSILInstrCIL.td): the only records that
// MSILMCInstLower emits, MSILInstPrinter prints and MSILMCCodeEmitter
// encodes. The operand list is the inline operand of the opcode alone (none,
// an immediate, a label, a token, or the labels of a switch); values on the
// evaluation stack have no operands. No pass analyzes these records, so their
// properties are the conservative ones TableGen would guess, and they keep
// NI's implicit def of ARGUMENTS like every other record that is not an
// ARGUMENT_<RC>.
class CILInst<bits<16> cilop, dag iops, string asmstr>
    : NI<(outs), iops, [], asmstr, cilop> {
  let isCodeGenOnly = 1;
  let hasSideEffects = 1;
  let mayLoad = 0;
  let mayStore = 0;
}
